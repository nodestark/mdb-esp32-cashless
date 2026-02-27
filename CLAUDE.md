# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

An open-source MDB (Multi-Drop Bus) cashless payment implementation for vending machines using ESP32-S3. The system has four main parts:

- **mdb-slave-esp32s3** – ESP32-S3 firmware acting as an MDB cashless device (peripheral)
- **mdb-master-esp32s3** – ESP32-S3 firmware simulating a VMC (vending machine controller) for testing
- **Docker** – Self-hosted backend: Supabase (PostgreSQL + auth + edge functions), MQTT broker, Vue.js legacy dashboard
- **management-frontend** – Nuxt 4 management dashboard (TypeScript, shadcn-nuxt, TailwindCSS 4)

---

## Firmware (ESP-IDF)

Both components use **ESP-IDF v5.x** with CMake and FreeRTOS.

```bash
. $IDF_PATH/export.sh   # activate ESP-IDF environment first

idf.py build
idf.py flash monitor
idf.py menuconfig       # hardware pins, features
```

### mdb-slave-esp32s3 Architecture

Single main file `main/mdb-slave-esp32s3.c` runs these concurrent FreeRTOS tasks:
- `mdb_cashless_loop` – MDB protocol handler on UART2 (GPIO4 RX, GPIO5 TX, 9600 baud, 9-bit mode)
- `bleprph_host_task` – NimBLE BLE peripheral (in `nimble.c`) for legacy device config and vend approvals
- MQTT client over WiFi for credit delivery and sales publishing
- Telemetry reader on UART1 (GPIO43 TX, GPIO44 RX) for DEX/DDCMP data

**MDB State machine**: `INACTIVE → DISABLED → ENABLED → IDLE → VEND → IDLE`

**Security**: MQTT and BLE payloads use XOR obfuscation with an 18-byte `passkey` plus a ±8 second timestamp window to prevent replay attacks.

**NVS namespace `vmflow`** keys:
- `domain` – device subdomain (integer string, used in MQTT topic `/domain/{subdomain}/...`)
- `passkey` – 18-char XOR cipher key
- `prov_code` – one-time provisioning code (erased after successful claim)
- `srv_url` – backend server URL

**WiFi / provisioning boot flow**:
1. On `WIFI_EVENT_STA_START`, calls `esp_wifi_connect()`. If it returns an error (no saved credentials), immediately starts SoftAP + captive portal DNS + HTTP server.
2. On repeated `WIFI_EVENT_STA_DISCONNECTED` (retry limit hit), also starts SoftAP.
3. Captive portal serves `webui/index.html` (embedded via CMakeLists `EMBED_FILES`). User enters WiFi credentials + provisioning code + server URL.
4. `webui_server.c` saves `prov_code` and `srv_url` to NVS, then calls `esp_wifi_connect()`.
5. On `IP_EVENT_STA_GOT_IP`, if `prov_code` is in NVS, spawns `provision_claim_task` instead of starting MQTT.
6. `provision_claim_task` POSTs `{short_code, mac_address}` to `{srv_url}/functions/v1/claim-device`, saves returned `domain` + `passkey` to NVS, erases `prov_code`, calls `esp_restart()`.
7. After reboot, device finds `domain` + `passkey` in NVS and starts MQTT normally.

**CMakeLists.txt**: Uses explicit `REQUIRES` — adding a new header include likely requires adding the owning component to `REQUIRES` in `main/CMakeLists.txt` (e.g. `esp_wifi`, `esp_http_client`, `mqtt`, `driver`, `bt`, etc.).

### mdb-master-esp32s3 Architecture

VMC simulator, polls MDB peripherals at addresses 0x08, 0x10, 0x60. Button ISR (`button0_isr_handler`) triggers a vend cycle. LED strip on GPIO48. DEX reader on GPIO17/18.

---

## Docker Backend

All services configured via `Docker/.env` (copy from `.env.example`). Run commands from the `Docker/` directory.

```bash
# Supabase + API gateway only
docker compose up

# With MQTT broker + domain bridge
docker compose -f docker-compose.yml -f docker-compose.mqtt.yml up

# Legacy Vue.js dashboard
docker compose -f docker-compose.vuejs.yml up vue-dev

# Tear down everything
docker compose -f docker-compose.yml -f docker-compose.mqtt.yml down -v --remove-orphans
```

### Key Services

| Service | Purpose |
|---------|---------|
| `kong` (port 8000) | API gateway |
| `db` | PostgreSQL |
| `auth` | Supabase GoTrue |
| `functions` | Deno edge runtime |
| `broker` (port 1883) | Eclipse Mosquitto MQTT |
| `domain` | Python MQTT→Supabase bridge |

### MQTT Domain Bridge

`Docker/mqtt/clients/mqtt_domain.py` subscribes to `/domain/+/(sale|status|paxcounter)`, decrypts XOR payloads, validates checksum + timestamp (±8s), writes to Supabase tables.

### Supabase Local Development

```bash
cd Docker/supabase
supabase start    # starts local Supabase on ports 54321 (API), 54322 (DB), 54323 (Studio)
supabase db reset # re-runs all migrations + seed
```

### Database Schema

Multi-tenancy model: users belong to `companies` via `organization_members`. All RLS policies use the helper functions `my_company_id()` and `i_am_admin()` which read from `organization_members` for the current JWT user.

Tables:
- `companies` – organisations
- `organization_members` – `(company_id, user_id, role)` where role ∈ `{admin, viewer}`
- `invitations` – email-scoped invite tokens with expiry
- `embeddeds` – registered devices: `subdomain` (bigint, auto-increment), `mac_address`, `passkey`, `status`
- `sales` – vend events: `embedded_id`, `item_price`, `item_number`, `channel`, `lat`, `lng`
- `paxcounter` – foot traffic: `embedded_id`, `count`
- `device_provisioning` – one-time provisioning codes: `short_code`, `expires_at`, `used_at`, `embedded_id`
- `vendingMachine` – physical machine records linked to embedded devices
- `products`, `product_category` – product catalogue per company

### Edge Functions (`Docker/supabase/functions/`)

All functions use `verify_jwt = false` in `config.toml` (workaround for ES256 `CryptoKey` bug in local edge runtime). Identity is verified inside each function via `adminClient.auth.getUser(token)` where `token` is extracted from the `Authorization` header.

| Function | Auth required | Purpose |
|----------|--------------|---------|
| `create-organization` | yes | Create company + make caller admin |
| `invite-member` | admin | Upsert invitation token |
| `accept-invitation` | yes | Join org via invite token |
| `get-my-organization` | yes | Returns `{organization, role}` or `{organization: null}` |
| `create-provisioning-token` | admin | Generate 8-char one-time device code (chars: `ABCDEFGHJKMNPQRSTUVWXYZ23456789`) |
| `claim-device` | none | Called by firmware; validates code, creates `embeddeds` row, returns `{subdomain, passkey, mqtt_host}` |
| `send-credit` | yes | Encrypt + publish credit to device MQTT topic |
| `request-credit` | yes | Related credit request flow |

---

## management-frontend

Nuxt 4 (`app/` directory convention), TypeScript, `@nuxtjs/supabase`, shadcn-nuxt, TailwindCSS 4, `@vueuse/core`.

```bash
cd management-frontend
npm install
npm run dev      # dev server on http://localhost:3000
```

**Environment** (`.env` in `management-frontend/`):
```
SUPABASE_URL=http://127.0.0.1:54321   # port 54321 = API, NOT 54323 (Studio)
SUPABASE_KEY=<anon key>
```

### Auth & Organisation Flow

The `app/middleware/auth.ts` middleware runs on every protected route:
1. Checks `useSupabaseUser()` → redirect to `/auth/login` if unauthenticated
2. Calls `get-my-organization` edge function (cached in `useState('organization')`)
3. If no organisation → redirect to `/onboarding/create-organization`

Public routes (no auth check): `/auth/login`, `/auth/register`, `/onboarding/*`

### Key Composables

- `useOrganization()` – wraps `get-my-organization`, exposes `organization`, `role`, `fetchOrganization()`
- `useMachines()` – fetches `vendingMachine` joined with `embeddeds`, enriched with last sale; `subscribeToStatusUpdates()` opens a Supabase realtime channel on the `embeddeds` table
- `useTheme()` – wraps `useDark` from `@vueuse/core`; theme persisted to `localStorage` as `color-scheme`; `plugins/theme.client.ts` applies `.dark` class before hydration to avoid flash

### Pages

- `/` – Dashboard: KPI cards (today/week sales, machine counts) + 30-day sales chart
- `/machines` – Table of vending machines with live status; "Add Device" modal triggers `create-provisioning-token` and shows 8-char code + setup instructions
- `/machines/[id]` – Per-machine detail: 30-day chart + sales history table
- `/members` – Active members table + pending invitations (admin only); invite modal calls `invite-member`
- `/onboarding/create-organization` – Calls `create-organization` edge function
- `/onboarding/accept-invitation` – Reads `?token=` from URL, calls `accept-invitation`

---

## Key Configuration Variables (`Docker/.env`)

- `SUPABASE_PUBLIC_URL` / `API_EXTERNAL_URL` – Public Supabase URL (use LAN IP for dev, e.g. `http://10.0.1.181:8000`)
- `MQTT_HOST` – MQTT broker hostname (LAN IP without port)
- `POSTGRES_PASSWORD`, `JWT_SECRET`, `ANON_KEY`, `SERVICE_ROLE_KEY` – Generated secrets
- `KONG_HTTP_PORT` – API gateway port (default 8000)
