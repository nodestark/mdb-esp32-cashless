# paho-mqtt
# supabase

import re
import paho.mqtt.client as mqtt
from supabase import create_client, Client

from datetime import datetime, timezone
import time

role_key="eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJyb2xlIjoic2VydmljZV9yb2xlIiwiaXNzIjoic3VwYWJhc2UtZGVtbyIsImlhdCI6MTY0MTc2OTIwMCwiZXhwIjoxNzk5NTM1NjAwfQ.j2KYKapYLHx_R86feOZUle8hKtjtLzOyohitXaXi3-M"

supabase: Client = create_client("https://supabase.vmflow.xyz", role_key)

def to_scale_factor(p: float, x: float, y: float) -> float:
    return p / x / (10 ** -y)

def from_scale_factor(p: float, x: float, y: float) -> float:
    return p * x * (10 ** -y)

def on_connect(client, userdata, flags, rc):

    if rc == 0:
        print(" Conectado ao broker!")
        client.subscribe("/domain/+/sale")
        client.subscribe("/domain/+/status")
    else:
        print(f" Falha na conexão. Código: {rc}")

def on_message(client, userdata, msg):
    try:
        match = re.match(r"^/domain/(\d+)/(sale|status)$", msg.topic)
        if match:
            domain_id = int(match.group(1))
            event_type = match.group(2) # "sale" or "status"
            payload = bytearray(msg.payload)
            # print(event_type)

            if event_type == "status":
                supabase.table("embeddeds").update({"status_at": datetime.now(timezone.utc).isoformat(), "status": payload.decode('utf-8', errors='ignore')}).eq("subdomain", domain_id).execute()

            if event_type == "sale":
                res = supabase.table("embeddeds").select("passkey,subdomain,id,owner_id").eq("subdomain", domain_id).execute()

                embedded = res.data[0]
                passkey = [ord(c) for c in embedded["passkey"]]

                for k in range(len(passkey)):
                    payload[k + 1] ^= passkey[k]

                chk = sum(payload[:-1])
                if payload[-1] == (chk & 0xFF):

                    timestampSec = ((payload[6] << 24) |
                                    (payload[7] << 16) |
                                    (payload[8] << 8) |
                                    (payload[9] << 0))

                    current_time = int(time.time())

                    if abs(current_time - timestampSec) <= 8:

                        item_price = (payload[2] << 8) | (payload[3] << 0)
                        item_number = (payload[4] << 8) | (payload[5] << 0)

                        supabase.table("sales").insert([{"owner_id":embedded["owner_id"],"embedded_id": embedded["id"],"item_number": item_number,"item_price": from_scale_factor(item_price, 1, 2),"channel": "cash"}]).execute()
    except Exception as e:
        print(f"An unexpected error occurred: {e}")

client = mqtt.Client()
client.on_connect = on_connect
client.on_message = on_message

client.connect("mqtt.vmflow.xyz", 1883, keepalive=60)
client.loop_forever()

