# paho-mqtt
# supabase

import os
import re
import hmac
import hashlib
import paho.mqtt.client as mqtt
from supabase import create_client, Client

from datetime import datetime, timezone
import time

# Freshness window for signed device->server messages. Devices on flaky links keep
# QoS1 publishes in the outbox and deliver them after reconnecting, so this is far
# wider than the firmware's server->device RPC_FRESHNESS_SEC (10s). Replays inside
# the window are rejected by signature dedupe below.
FRESHNESS_SEC = 3600

# How long after a remote credit the device's vend_success may still claim that sale.
VEND_MATCH_SEC = 900

_seen_sigs = {}


def _seen_before(sig: str) -> bool:
    now = time.time()
    for k in [k for k, exp in _seen_sigs.items() if exp < now]:
        del _seen_sigs[k]
    if sig in _seen_sigs:
        return True
    _seen_sigs[sig] = now + FRESHNESS_SEC
    return False


def verify_signed_line(passkey: str, line: str, tag: str = ""):
    """Verify "<fields...>:<ts>:<hmac_hex>". Returns the list of leading fields
    (everything before <ts>) if the HMAC is valid, <ts> is fresh and the message
    was not seen before, else None (reason is logged)."""
    idx = line.rfind(":")
    if idx < 0:
        print(f" [{tag}] rejected: malformed {line!r}")
        return None
    msg, sig = line[:idx], line[idx + 1:]

    calc = hmac.new(passkey.encode(), msg.encode(), hashlib.sha256).hexdigest()
    if not hmac.compare_digest(calc, sig):
        print(f" [{tag}] rejected: bad HMAC {line!r}")
        return None

    fields = msg.split(":")
    if len(fields) < 2:
        print(f" [{tag}] rejected: malformed {line!r}")
        return None

    try:
        ts = int(fields[-1])
    except ValueError:
        print(f" [{tag}] rejected: bad ts {line!r}")
        return None
    dt = int(time.time()) - ts
    if abs(dt) > FRESHNESS_SEC:
        print(f" [{tag}] rejected: stale ts (dt={dt}s) {line!r}")
        return None
    if dt > 10:
        print(f" [{tag}] late delivery accepted (dt={dt}s)")

    if _seen_before(sig):
        print(f" [{tag}] rejected: duplicate {line!r}")
        return None

    return fields[:-1]

role_key = os.environ.get('SERVICE_ROLE_KEY')

supabaseUrl = os.environ.get('SUPABASE_PUBLIC_URL')
supabase: Client = create_client(supabaseUrl, role_key)


def to_scale_factor(p: float, x: float, y: float) -> float:
    return p / x / (10 ** -y)


def from_scale_factor(p: float, x: float, y: float) -> float:
    return p * x * (10 ** -y)


def on_connect(client, userdata, flags, rc):
    if rc == 0:
        print(" Connected to broker!")
        client.subscribe("domain.vmflow.xyz/+/#")
    else:
        print(f" Connection failed. Code: {rc}")


def on_message(client, userdata, msg):
    try:
        match = re.match(r"^domain.vmflow.xyz/(\d+)/(sale|status|paxcounter|vend_fail|vend_success)$", msg.topic)
        if match:
            domain_id = int(match.group(1))
            event_type = match.group(2)

            if event_type == "status":
                raw = msg.payload.decode('utf-8', errors='ignore')
                parts = raw.split(',', 1)
                new_status = parts[0]
                reset_reason = float(parts[1]) if len(parts) > 1 else None

                res = supabase.table("embedded").select("id, machine_id, status").eq("subdomain", domain_id).execute()
                if not res.data:
                    return
                embedded_s = res.data[0]
                current_status = embedded_s["status"]

                supabase.table("embedded").update({
                    "status_at": datetime.now(timezone.utc).isoformat(),
                    "status": new_status
                }).eq("subdomain", domain_id).execute()

                if new_status != current_status:
                    supabase.table("metrics").insert([{
                        "embedded_id": embedded_s["id"],
                        "machine_id":  embedded_s["machine_id"],
                        "name":        new_status,
                        "value":       reset_reason
                    }]).execute()

            # Signed-text envelopes: "<count>:<ts>:<hmac>" (pax), "<price>:<item>:<ts>:<hmac>" (sale).
            line = msg.payload.decode('utf-8', errors='ignore')

            if event_type == "paxcounter":
                res = supabase.table("embedded").select("passkey, subdomain, id, machine_id").eq("subdomain", domain_id).execute()
                embedded = res.data[0]

                fields = verify_signed_line(embedded["passkey"], line, f"{domain_id}/paxcounter")
                if fields and len(fields) == 1:
                    pax_counter = int(fields[0])

                    supabase.table("metrics").insert([{"embedded_id": embedded["id"],
                                                     "machine_id": embedded["machine_id"],
                                                     "name": "paxcounter",
                                                     "value": pax_counter}]).execute()

            if event_type == "sale":
                res = supabase.table("embedded").select("passkey,subdomain,id,owner_id,machine_id").eq("subdomain", domain_id).execute()
                embedded = res.data[0]

                fields = verify_signed_line(embedded["passkey"], line, f"{domain_id}/sale")
                if fields and len(fields) == 2:
                    item_price = int(fields[0])
                    item_number = int(fields[1])

                    supabase.table("sales").insert([{"owner_id":   embedded["owner_id"],
                                                     "embedded_id": embedded["id"],
                                                     "machine_id":  embedded["machine_id"],
                                                     "item_number": item_number,
                                                     "item_price":  from_scale_factor(item_price, 1, 2),
                                                     "channel":     "cash"}]).execute()

            # Remote-credit vend: "<price>:<item>:<ts>:<hmac>". The sale row was
            # inserted (item_number NULL) when the credit was sent; attach the item
            # so trg_fill_sale_coil_data maps product/coil and debits stock.
            if event_type == "vend_success":
                res = supabase.table("embedded").select("passkey, id").eq("subdomain", domain_id).execute()
                if not res.data:
                    return
                embedded = res.data[0]

                fields = verify_signed_line(embedded["passkey"], line, f"{domain_id}/vend_success")
                if fields and len(fields) == 2:
                    item_number = int(fields[1])
                    since = datetime.fromtimestamp(int(line.split(":")[2]) - VEND_MATCH_SEC, timezone.utc).isoformat()

                    sale = supabase.table("sales").select("id") \
                        .eq("embedded_id", embedded["id"]).eq("channel", "mqtt") \
                        .is_("item_number", "null").gte("created_at", since) \
                        .order("created_at", desc=True).limit(1).execute()
                    if not sale.data:
                        print(f" [{domain_id}/vend_success] no pending remote-credit sale for item {item_number}")
                        return

                    supabase.table("sales").update({"item_number": item_number}) \
                        .eq("id", sale.data[0]["id"]).is_("item_number", "null").execute()

            if event_type == "vend_fail":
                res = supabase.table("embedded").select("passkey, id, machine_id").eq("subdomain", domain_id).execute()
                if not res.data:
                    return
                embedded = res.data[0]

                fields = verify_signed_line(embedded["passkey"], line, f"{domain_id}/vend_fail")
                if fields and len(fields) == 1:
                    parts = fields[0].split(",")
                    if len(parts) == 2:
                        vf_price = int(parts[0])
                        vf_item  = int(parts[1])

                        supabase.table("metrics").insert([{
                            "embedded_id": embedded["id"],
                            "machine_id":  embedded["machine_id"],
                            "name":        "vend_fail",
                            "payload":     {"item_price": vf_price, "item_number": vf_item}
                        }]).execute()
    except Exception as e:
        print(f"An unexpected error occurred: {e}")

client = mqtt.Client()

client.on_connect = on_connect
client.on_message = on_message

mqttHost = os.environ.get('MQTT_HOST')
print(mqttHost)
client.connect(mqttHost, 1883, keepalive=60)
client.loop_forever()
