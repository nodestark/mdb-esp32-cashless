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

# Freshness window for signed device->server messages (must match firmware RPC_FRESHNESS_SEC).
FRESHNESS_SEC = 10


def verify_signed_line(passkey: str, line: str):
    """Verify "<fields...>:<ts>:<hmac_hex>". Returns the list of leading fields
    (everything before <ts>) if the HMAC is valid and <ts> is fresh, else None."""
    idx = line.rfind(":")
    if idx < 0:
        return None
    msg, sig = line[:idx], line[idx + 1:]

    calc = hmac.new(passkey.encode(), msg.encode(), hashlib.sha256).hexdigest()
    if not hmac.compare_digest(calc, sig):
        return None

    fields = msg.split(":")
    if len(fields) < 2:
        return None

    try:
        ts = int(fields[-1])
    except ValueError:
        return None
    if abs(int(time.time()) - ts) > FRESHNESS_SEC:
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
        match = re.match(r"^domain.vmflow.xyz/(\d+)/(sale|status|paxcounter|vend_fail)$", msg.topic)
        if match:
            domain_id = int(match.group(1))
            event_type = match.group(2)  # "sale", "status" or "paxcounter"

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

                fields = verify_signed_line(embedded["passkey"], line)
                if fields and len(fields) == 1:
                    pax_counter = int(fields[0])

                    supabase.table("metrics").insert([{"embedded_id": embedded["id"],
                                                     "machine_id": embedded["machine_id"],
                                                     "name": "paxcounter",
                                                     "value": pax_counter}]).execute()

            if event_type == "sale":
                res = supabase.table("embedded").select("passkey,subdomain,id,owner_id,machine_id").eq("subdomain", domain_id).execute()
                embedded = res.data[0]

                fields = verify_signed_line(embedded["passkey"], line)
                if fields and len(fields) == 2:
                    item_price = int(fields[0])
                    item_number = int(fields[1])

                    supabase.table("sales").insert([{"owner_id":   embedded["owner_id"],
                                                     "embedded_id": embedded["id"],
                                                     "machine_id":  embedded["machine_id"],
                                                     "item_number": item_number,
                                                     "item_price":  from_scale_factor(item_price, 1, 2),
                                                     "channel":     "cash"}]).execute()

            if event_type == "vend_fail":
                res = supabase.table("embedded").select("id, machine_id").eq("subdomain", domain_id).execute()
                if not res.data:
                    return
                embedded = res.data[0]

                parts = msg.payload.decode('utf-8', errors='ignore').split(',', 1)
                vf_price  = int(parts[0]) if len(parts) > 0 and parts[0] else None
                vf_item   = int(parts[1]) if len(parts) > 1 and parts[1] else None

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
