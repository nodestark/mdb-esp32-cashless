# Architecture Overview

## System Diagram

```
+------------------+
|   Management UI  |
|   (Nuxt 4)       |
|   :3000          |
+--------+---------+
         |
         | HTTP (Supabase JS client)
         v
+------------------+         +------------------+
|   Supabase       |  SQL    |   PostgreSQL     |
|   Kong :8000     +-------->+   :5432          |
|   Edge Functions |         |                  |
|   Auth (GoTrue)  |         |   embeddeds      |
+--------+---------+         |   sales          |
         ^                   |   paxcounter     |
         |                   |   companies      |
         | HTTP POST         |   org_members    |
         |                   |   vendingMachine |
+--------+---------+         +------------------+
| MQTT Forwarder   |
| (Deno)           |
+--------+---------+
         ^ subscribe
         |
+--------+---------+          send-credit publishes
| Mosquitto Broker +<-------- via edge function
| :1883            |
+--------+---------+
    ^          |
    | pub      | credit (sub)
    |          v
+---+----------+---+
|    ESP32-S3      |
|    Firmware      |
+------------------+
```

**Data flows:**
- **Device -> DB**: ESP32 publishes to Mosquitto -> Forwarder subscribes and POSTs to `mqtt-webhook` edge function -> edge function writes to PostgreSQL
- **DB -> Device**: UI calls `send-credit` edge function -> edge function connects to Mosquitto and publishes -> ESP32 receives on subscribed topic
- **UI -> DB**: Nuxt frontend calls Supabase edge functions / REST API directly via HTTP

---

## Components

### 1. ESP32-S3 Firmware (`mdb-slave-esp32s3/`)

MDB cashless peripheral that sits inside a vending machine.

| Aspect | Detail |
|--------|--------|
| Framework | ESP-IDF v5.x, FreeRTOS, C |
| MDB bus | UART2 @ 9600 baud, 9-bit mode (GPIO4 RX, GPIO5 TX) |
| Networking | WiFi STA + SoftAP fallback, MQTT over TCP |
| Bluetooth | NimBLE peripheral for legacy config + mobile vend approvals |
| Telemetry | DEX/DDCMP reader on UART1 (GPIO43 TX, GPIO44 RX) |
| Storage | NVS namespace `vmflow` (keys: `company_id`, `device_id`, `passkey`, `mqtt_host`) |

**State machine:** `INACTIVE -> DISABLED -> ENABLED -> IDLE -> VEND -> IDLE`

**Provisioning flow:**
1. No WiFi credentials -> SoftAP + captive portal (`webui/index.html`)
2. User enters SSID, password, provisioning code, server URL
3. Device POSTs `{short_code, mac_address}` to `claim-device` edge function
4. Receives `{company_id, device_id, passkey, mqtt_host}`, saves to NVS, reboots
5. On boot: reads NVS, connects to MQTT broker, starts MDB polling

### 2. MQTT Broker (Mosquitto)

Eclipse Mosquitto running in Docker. Devices publish sensor data, backend publishes credit commands.

| Port | Protocol |
|------|----------|
| 1883 | MQTT TCP |
| 9001 | WebSockets |

**Topic scheme:** `/{company_id}/{device_id}/{event}`

| Topic | Direction | Payload |
|-------|-----------|---------|
| `/{cid}/{did}/status` | Device -> Broker | UTF-8 string: `online` or `offline` (LWT) |
| `/{cid}/{did}/sale` | Device -> Broker | 19 bytes, XOR encrypted |
| `/{cid}/{did}/paxcounter` | Device -> Broker | 19 bytes, XOR encrypted |
| `/{cid}/{did}/dex` | Device -> Broker | Raw DEX/DDCMP telemetry |
| `/{cid}/{did}/credit` | Backend -> Device | 19 bytes, XOR encrypted |

**Authentication:** Username/password (`vmflow`/`vmflow`) for ACL scoping. Payload security relies on per-device XOR cipher, not transport encryption.

### 3. MQTT Forwarder (`Docker/mqtt/forwarder/`)

Stateless Deno script (~35 lines). Subscribes to MQTT topics, base64-encodes payloads, POSTs to the `mqtt-webhook` edge function. No business logic.

```
MQTT message -> base64 encode -> HTTP POST {topic, payload} -> mqtt-webhook
```

**Environment:**
- `MQTT_HOST` - Broker hostname (Docker: `broker`)
- `SUPABASE_URL` - Edge function base URL
- `MQTT_WEBHOOK_SECRET` - Shared secret for webhook auth

### 4. Supabase Backend (`Docker/supabase/`)

Self-hosted Supabase stack: PostgreSQL + GoTrue auth + Kong API gateway + Deno edge runtime.

#### Edge Functions

| Function | Auth | Purpose |
|----------|------|---------|
| `claim-device` | None (device) | Validate provisioning code, create device, return credentials |
| `mqtt-webhook` | Shared secret | Receive MQTT data from forwarder, decrypt, write to DB |
| `send-credit` | JWT | Encrypt credit amount, publish to device MQTT topic |
| `request-credit` | JWT | Process BLE credit request (decrypt, validate, insert sale) |
| `create-organization` | JWT | Create company + admin membership |
| `invite-member` | Admin | Generate invitation token |
| `accept-invitation` | JWT | Join organization via token |
| `get-my-organization` | JWT | Return user's org + role |
| `create-provisioning-token` | Admin | Generate 8-char one-time device code |

#### Database (PostgreSQL)

Multi-tenant with row-level security. All queries scoped by `my_company_id()` SQL helper.

**Core tables:**
- `companies` - Organizations
- `organization_members` - User-to-company mapping with role (`admin`/`viewer`)
- `embeddeds` - Registered devices (passkey, status, MAC address)
- `sales` - Vend transactions (price, item number, channel, GPS)
- `paxcounter` - Foot traffic counts
- `vendingMachine` - Physical machine metadata
- `device_provisioning` - One-time provisioning codes
- `invitations` - Org invite tokens

### 5. Management Frontend (`management-frontend/`)

Nuxt 4 dashboard for managing devices, viewing sales, and inviting team members.

| Technology | Version |
|------------|---------|
| Nuxt | 4 |
| TypeScript | 5.x |
| UI components | shadcn-nuxt (Radix-based) |
| CSS | TailwindCSS 4 |
| Charts | Unovis |
| Supabase client | @nuxtjs/supabase |

**Pages:**
- `/` - Dashboard: KPI cards (today/week sales) + 30-day area chart
- `/machines` - Device table with live status, "Add Device" provisioning flow
- `/machines/[id]` - Per-device sales chart + history
- `/members` - Team management + invite flow (admin only)
- `/auth/login`, `/auth/register` - Authentication
- `/onboarding/create-organization`, `/onboarding/accept-invitation` - Org setup

**Auth flow:** Middleware checks JWT -> checks org membership -> redirects if needed.

**Real-time:** Supabase realtime channel on `embeddeds` table for live device status.

### 6. VMC Simulator (`mdb-master-esp32s3/`)

Test tool. Simulates a vending machine controller that polls MDB peripherals. Button press triggers a vend cycle. Used for development without a real vending machine.

---

## Payload Security

All encrypted MQTT/BLE payloads use the same 19-byte format:

```
Byte 0:      Command ID (unencrypted)
Bytes 1-18:  XOR'd with 18-byte device passkey
  Byte 1:    Version
  Bytes 2-5: Item price (big-endian uint32, scale factor applied)
  Bytes 6-7: Item number (big-endian uint16)
  Bytes 8-11: Unix timestamp (big-endian uint32)
  Bytes 12-17: Event-specific data (e.g. paxcounter count at 12-13)
  Byte 18:   Checksum (sum of bytes 0-17, masked to uint8)
```

**Validation:** Receiver decrypts, verifies checksum, checks timestamp within +-8 seconds of current time.

---

## Running Locally

```bash
# 1. Start Supabase + MQTT
cd Docker
cp .env.example .env                    # edit MQTT_HOST to your LAN IP
docker compose -f docker-compose.yml -f docker-compose.mqtt.yml up

# 2. Start management UI
cd management-frontend
npm install && npm run dev              # http://localhost:3000

# 3. Flash firmware
cd mdb-slave-esp32s3
. $IDF_PATH/export.sh
idf.py build flash monitor
```

**Key `.env` variables:**
- `SUPABASE_PUBLIC_URL` - Kong API gateway URL (use LAN IP for device access)
- `MQTT_HOST` - Broker address devices connect to (LAN IP, no port)
- `MQTT_WEBHOOK_SECRET` - Shared secret between forwarder and webhook
- `SERVICE_ROLE_KEY` - Supabase service role key (used by MQTT bridge)

---

## Docker Services

```bash
# Core (Supabase stack)
docker compose up

# Core + MQTT broker + forwarder
docker compose -f docker-compose.yml -f docker-compose.mqtt.yml up

# Legacy Vue.js dashboard
docker compose -f docker-compose.vuejs.yml up vue-dev
```
