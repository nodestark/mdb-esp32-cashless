# VMflow — Agent Context

> Drop-in context for AI agents managing a VMflow vending fleet.
> Two control planes: **MQTT RPC** (real-time, signed, device-direct) and **Supabase REST** (data, credits, telemetry history).

Each device is identified by its `<subdomain>` (e.g. `51`) and holds a per-device
`passkey` used for both credit XOR encoding and RPC HMAC signing.

---

## 1. MQTT RPC — real-time device control

Signed commands sent straight to a device over MQTT. Authentication is an HMAC,
not a session — every message is self-contained and expires fast.

**Envelope**
```
<cmd>:<ts>:<hmac>
```
- `cmd`  — one of the commands below
- `ts`   — UTC epoch seconds (`date +%s`)
- `hmac` — `HMAC-SHA256(passkey, "<cmd>:<ts>")`, lowercase hex

Commands that take arguments (e.g. `credit`) extend the envelope to
`<cmd>:<args>:<ts>:<hmac>`; the HMAC then covers everything before the last colon.

**Freshness:** device accepts only if `|now - ts| <= 10s`. Sign at send time; do not cache signatures.

**Topics**
| Direction | Topic |
|-----------|-------|
| Publish (command) | `<subdomain>.vmflow.xyz/rpc` |
| Reply / result    | `domain.vmflow.xyz/<subdomain>/rpc/<cmd>` |

Broker host: `mqtt.vmflow.xyz`. (`<subdomain>.vmflow.xyz` is the **topic prefix**, not a hostname.)

**Commands**
| cmd | Effect | Reply topic |
|-----|--------|-------------|
| `info`    | Publish device snapshot JSON (see schema) | `.../rpc/info` |
| `credit`  | Grant credit. Envelope `credit:<amount>:<ts>:<hmac>`, `<amount>` in 1/100 units (cents) | `.../rpc/credit` (`ok`) |
| `dex`     | Trigger EVA-DTS DEX/DDCMP telemetry pull from the VMC | — |
| `oos`     | Send MDB "command out of sequence" to the VMC | — |
| `echo`    | Liveness / RTT probe — replies with the request `ts` | `.../rpc/echo` |
| `buzzer`  | 1s beep (physical locate) | — |
| `restart` | Ack then reboot the device | `.../rpc/restart` (`ok`) |

**`info` snapshot schema** (what an agent reads for diagnostics)
```json
{
  "version": "fw version string",
  "uptime_s": 12345,
  "reset_reason": 1,
  "free_heap": 180000,
  "min_free_heap": 150000,
  "machine_state": "ENABLED|IDLE|VEND|OTHER",
  "last_sale_price": 150,
  "last_sale_item": 3,
  "last_vend_success_time": 1730000000,
  "vend_fail_count": 0
}
```

**Send a command** (helper: `mdb-slave-esp32s3/tools/rpc.sh`)
```bash
SUB=51 PASSKEY=<passkey> ./mdb-slave-esp32s3/tools/rpc.sh info
./mdb-slave-esp32s3/tools/rpc.sh -s 51 -k <passkey> -w echo   # -w waits for reply
```

**Raw equivalent** (no helper)
```bash
SUB=51; PASSKEY=<passkey>; CMD=info
TS=$(date +%s)
SIG=$(printf '%s' "$CMD:$TS" | openssl dgst -sha256 -hmac "$PASSKEY" -hex | awk '{print $NF}')
mosquitto_pub -h mqtt.vmflow.xyz -t "$SUB.vmflow.xyz/rpc" -m "$CMD:$TS:$SIG" -q 1
```

---

## 2. Supabase REST — data & credits

Base: `https://supabase.vmflow.xyz`. All requests need `apikey` (anon) and a
`Bearer` access token from the login step.

**Anon key** (public, safe to embed — gates only at the API edge; row access is enforced by RLS after login):
```
eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJyb2xlIjoiYW5vbiIsImlzcyI6InN1cGFiYXNlLWRlbW8iLCJpYXQiOjE2NDE3NjkyMDAsImV4cCI6MTc5OTUzNTYwMH0.VGEEIztVo-do9cy_Qw2-2sF8bSONckhX71Nvtwj15X4
```

**Get a token**
```bash
curl -X POST 'https://supabase.vmflow.xyz/auth/v1/token?grant_type=password' \
  -H "apikey: <ANON_KEY>" -H "Content-Type: application/json" \
  -d '{"email":"you@domain.xyz","password":"..."}'
```

**Endpoints**
| Action | Method | Path |
|--------|--------|------|
| Send credit to a machine (signs a `credit` RPC over MQTT) | POST | `/functions/v1/send-credit` — body `{"subdomain":51,"amount":1.50}` |
| List sales      | GET | `/rest/v1/sales` |
| List devices    | GET | `/rest/v1/embedded` |
| PAX foot-traffic | GET | `/rest/v1/metrics?name=eq.paxcounter` |

All `/rest/v1/*` calls take `apikey` + `Authorization: Bearer <token>`.

---

## 3. Agent playbooks

- **Health sweep:** `info` each device → flag `machine_state=OTHER`, rising `vend_fail_count`, or `min_free_heap` near zero.
- **Is it alive?** `echo` → measure RTT against reply on `.../rpc/echo`.
- **Locate a machine:** `buzzer`.
- **Pull telemetry:** `dex`, then read DEX/DDCMP results downstream.
- **Recover a stuck VMC:** `oos`, escalate to `restart` if needed.
- **Reconcile sales:** `/rest/v1/sales` vs the device's `last_sale_*` from `info`.

## Notes
- Secrets: `passkey` per device (RPC HMAC + credit XOR). Never log or commit it.
- Repo: <https://github.com/nodestark/mdb-esp32-cashless>
- Firmware RPC handler: `mdb-slave-esp32s3/main/mdb-slave-esp32s3.c`
