#!/usr/bin/env python3
"""
Test Supabase Realtime WebSocket connection.
Usage: python test_realtime.py
"""

import asyncio
import json
import time
import websockets

SUPABASE_URL = "https://supabase.vmflow.xyz"
ANON_KEY = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJyb2xlIjoiYW5vbiIsImlzcyI6InN1cGFiYXNlLWRlbW8iLCJpYXQiOjE2NDE3NjkyMDAsImV4cCI6MTc5OTUzNTYwMH0.VGEEIztVo-do9cy_Qw2-2sF8bSONckhX71Nvtwj15X4"

WS_URL = f"{SUPABASE_URL.replace('https://', 'wss://')}/realtime/v1/websocket?apikey={ANON_KEY}&vsn=2.0.0"


async def test_realtime():
    print(f"Connecting to: {WS_URL[:80]}...")

    try:
        async with websockets.connect(
            WS_URL,
            additional_headers={"apikey": ANON_KEY},
            open_timeout=10,
        ) as ws:
            print("✓ WebSocket connected\n")

            # Phoenix V2 protocol: [join_ref, ref, topic, event, payload]
            # 1. Send heartbeat
            heartbeat = json.dumps([None, "1", "phoenix", "heartbeat", {}])
            await ws.send(heartbeat)
            print(f"→ Sent heartbeat")

            resp = await asyncio.wait_for(ws.recv(), timeout=5)
            msg = json.loads(resp)
            print(f"← {msg}\n")

            # 2. Join realtime channel (same as useAlerts.js)
            join = json.dumps([
                "1", "2", "realtime:test-channel", "phx_join",
                {
                    "config": {
                        "broadcast": {"self": False},
                        "presence": {"key": ""},
                        "postgres_changes": [
                            {"event": "UPDATE", "schema": "public", "table": "machine_coils"},
                            {"event": "UPDATE", "schema": "public", "table": "embedded"},
                        ]
                    },
                    "access_token": ANON_KEY
                }
            ])
            await ws.send(join)
            print(f"→ Joined channel 'realtime:test-channel'")

            for _ in range(3):
                resp = await asyncio.wait_for(ws.recv(), timeout=5)
                msg = json.loads(resp)
                # V2: [join_ref, ref, topic, event, payload]
                event = msg[3] if len(msg) > 3 else "?"
                payload = msg[4] if len(msg) > 4 else {}
                print(f"← event={event} status={payload.get('status', '')} {payload.get('response', '')}")
                if event == "phx_reply":
                    break

            print("\n✓ Realtime is working correctly!")

    except websockets.exceptions.InvalidStatus as e:
        print(f"✗ Connection rejected — HTTP {e.response.status_code}")
        print(f"  Headers: {dict(e.response.headers)}")
        if e.response.status_code == 401:
            print("  → JWT validation failed: JWT_SECRET mismatch on server")
        elif e.response.status_code == 403:
            print("  → Forbidden: check RLS or Realtime config")

    except (TimeoutError, asyncio.TimeoutError):
        print("✗ Timeout — no response from server")
        print("  → Check if proxy (nginx/traefik) supports WebSocket upgrade")

    except OSError as e:
        print(f"✗ Connection error: {e}")
        print("  → DNS resolution failed or host unreachable")

    except Exception as e:
        print(f"✗ Unexpected error: {type(e).__name__}: {e}")


if __name__ == "__main__":
    asyncio.run(test_realtime())
