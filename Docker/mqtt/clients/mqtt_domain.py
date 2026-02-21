# paho-mqtt
# supabase

import os
import re
import paho.mqtt.client as mqtt
from supabase import create_client, Client

from datetime import datetime, timezone
import time

role_key = os.environ.get('SERVICE_ROLE_KEY')

supabaseUrl = os.environ.get('SUPABASE_PUBLIC_URL')
print(supabaseUrl)
print(role_key)
supabase: Client = create_client(supabaseUrl, role_key)


def to_scale_factor(p: float, x: float, y: float) -> float:
    return p / x / (10 ** -y)


def from_scale_factor(p: float, x: float, y: float) -> float:
    return p * x * (10 ** -y)


def on_connect(client, userdata, flags, rc):
    if rc == 0:
        print(" Connected to broker!")
        client.subscribe("/domain/+/#")
    else:
        print(f" Connection failed. Code: {rc}")


def on_message(client, userdata, msg):
    try:
        match = re.match(r"^/domain/(\d+)/(sale|status|paxcounter)$", msg.topic)
        if match:
            domain_id = int(match.group(1))
            event_type = match.group(2)  # "sale" or "status"
            payload = bytearray(msg.payload)
            # print(event_type)

            if event_type == "status":
                supabase.table("embeddeds").update({"status_at": datetime.now(timezone.utc).isoformat(), "status": payload.decode('utf-8', errors='ignore')}).eq("subdomain", domain_id).execute()

            if event_type == "paxcounter":
                res = supabase.table("embeddeds").select("passkey,subdomain,id,owner_id").eq("subdomain", domain_id).execute()

                embedded = res.data[0]
                passkey = [ord(c) for c in embedded["passkey"]]

                for k in range(len(passkey)):
                    payload[k + 1] ^= passkey[k]

                chk = sum(payload[:-1])
                if payload[-1] == (chk & 0xFF):

                    timestamp_sec = int.from_bytes(payload[8:12], byteorder="big")

                    current_time = int(time.time())

                    if abs(current_time - timestamp_sec) <= 8:
                        pax_counter = int.from_bytes(payload[12:14], byteorder="big")

                        supabase.table("paxcounter").insert([{"owner_id": embedded["owner_id"],
                                                         "embedded_id": embedded["id"],
                                                         "count": pax_counter}]).execute()

            if event_type == "sale":
                res = supabase.table("embeddeds").select("passkey,subdomain,id,owner_id").eq("subdomain", domain_id).execute()

                embedded = res.data[0]
                passkey = [ord(c) for c in embedded["passkey"]]

                for k in range(len(passkey)):
                    payload[k + 1] ^= passkey[k]

                chk = sum(payload[:-1])
                if payload[-1] == (chk & 0xFF):

                    timestamp_sec = int.from_bytes(payload[8:12], byteorder="big")

                    current_time = int(time.time())

                    if abs(current_time - timestamp_sec) <= 8:
                        item_price = int.from_bytes(payload[2:6], byteorder="big")
                        item_number = int.from_bytes(payload[6:8], byteorder="big")

                        supabase.table("sales").insert([{"owner_id": embedded["owner_id"],
                                                         "embedded_id": embedded["id"],
                                                         "item_number": item_number,
                                                         "item_price": from_scale_factor(item_price, 1, 2),
                                                         "channel": "cash"}]).execute()
    except Exception as e:
        print(f"An unexpected error occurred: {e}")

client = mqtt.Client()

client.on_connect = on_connect
client.on_message = on_message

mqttHost = os.environ.get('MQTT_HOST')
print(mqttHost)
client.connect(mqttHost, 1883, keepalive=60)
client.loop_forever()
