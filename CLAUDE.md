# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is an open-source MDB (Multi-Drop Bus) protocol implementation for cashless vending machines using ESP32-S3. The system consists of:

- **mdb-slave-esp32s3**: ESP32-S3 firmware acting as an MDB cashless device (peripheral) that plugs into a vending machine
- **mdb-master-esp32s3**: ESP32-S3 firmware simulating a vending machine controller (VMC) for testing
- **Docker**: Self-hosted backend stack (Supabase, MQTT broker, Vue.js dashboard)
- **management-frontend**: Nuxt.js management frontend

## Firmware (ESP-IDF)

Both firmware components use **ESP-IDF v5.x** with CMake and FreeRTOS.

### Build & Flash (mdb-slave or mdb-master)
```bash
# Set up ESP-IDF environment first
. $IDF_PATH/export.sh

# Build
idf.py build

# Flash and monitor
idf.py flash monitor

# Flash only
idf.py flash

# Menu configuration (hardware pins, features)
idf.py menuconfig
```

### mdb-slave-esp32s3 Architecture
The slave firmware has a single main file `main/mdb-slave-esp32s3.c` with these concurrent FreeRTOS tasks:
- `mdb_cashless_loop` – Core MDB protocol handler on UART2 (GPIO4 RX, GPIO5 TX, 9600 baud, 9-bit)
- `bleprph_host_task` – NimBLE BLE peripheral for device configuration and vend approvals
- MQTT client over WiFi for credit delivery and sales publishing
- Telemetry reader on UART1 (GPIO43 TX, GPIO44 RX) for DEX/DDCMP data collection

**MDB State machine**: `INACTIVE → DISABLED → ENABLED → IDLE → VEND → IDLE`

**Security**: All MQTT and BLE payloads use XOR encryption with an 18-byte `passkey` plus a timestamp window of ±8 seconds to prevent replay attacks.

**NVS**: Device `subdomain` (integer ID) and `passkey` are persisted in NVS flash and configured at runtime via BLE writes.

**MQTT topics**: `/domain/{subdomain}/sale`, `/domain/{subdomain}/credit`, `/domain/{subdomain}/status`, `/domain/{subdomain}/dex`, `/domain/{subdomain}/paxcounter`

### mdb-master-esp32s3 Architecture
Acts as the VMC (bus master), polling MDB peripherals at addresses 0x08, 0x10, 0x60. Contains a button ISR (`button0_isr_handler`) that triggers a vend cycle. LED strip on GPIO48. DEX reader on GPIO17/18.

## Docker Backend

Located in `Docker/`. All services are configured via `Docker/.env` (copy from `.env.example`).

### Commands
```bash
# Start core Supabase stack
docker compose up

# Start with MQTT broker + domain bridge
docker compose -f docker-compose.yml -f docker-compose.mqtt.yml up

# Start Vue.js dashboard (dev)
docker compose -f docker-compose.vuejs.yml up vue-dev

# Start Vue.js dashboard (prod with nginx)
docker compose -f docker-compose.vuejs.yml up vue-nginx

# Tear down and remove volumes
docker compose -f docker-compose.yml -f docker-compose.mqtt.yml down -v --remove-orphans
```

### Backend Stack Components
| Service | Image | Purpose |
|---------|-------|---------|
| `kong` | kong:2.8.1 | API gateway, port 8000 |
| `db` | supabase/postgres:15.x | PostgreSQL database |
| `auth` | supabase/gotrue | Auth service |
| `rest` | postgrest/postgrest | Auto REST API |
| `functions` | supabase/edge-runtime | Deno edge functions |
| `storage` | supabase/storage-api | File storage |
| `studio` | supabase/studio | Admin dashboard |
| `broker` | eclipse-mosquitto:2.1.2 | MQTT broker (port 1883) |
| `domain` | custom Python | MQTT→Supabase bridge |

### MQTT Domain Bridge
`Docker/mqtt/clients/mqtt_domain.py` – Python script subscribing to `/domain/+/(sale|status|paxcounter)`, decrypting XOR payloads, and writing to Supabase tables. Validates checksum and timestamp (±8s window) before inserting.

### Supabase Edge Functions
- `send-credit` – Encrypts and publishes a credit payload to the device's MQTT topic; inserts a `sales` record if device is online
- `request-credit` – Related credit flow

### Database Schema (`supabase/migrations/`)
- `embeddeds` – Registered devices: `subdomain` (auto-increment bigint ID), `mac_address`, `status`, `status_at`, `passkey`
- `sales` – Vend events: `embedded_id`, `item_price`, `item_number`, `channel`, `lat`, `lng`
- `paxcounter` – Foot traffic data: `embedded_id`, `count`

### Supabase Local Development
```bash
cd Docker/supabase
supabase start   # starts local Supabase stack
```

## Vue.js Dashboard

Located in `Docker/vuejs/`. Vue 3 + Vite + vue-router + @supabase/supabase-js + chart.js.

```bash
cd Docker/vuejs
npm install
npm run dev      # dev server on port 5173
npm run build    # production build
```

Views: `Login.vue`, `Dashboard.vue`, `Embedded.vue`
Components: `SalesLineChart.vue`

## management-frontend

Located in `management-frontend/`. Nuxt.js (TypeScript) management interface.

```bash
cd management-frontend
npm install
# See nuxt.config.ts for configuration
```

## Key Configuration Variables (`.env`)

- `SUPABASE_PUBLIC_URL` / `API_EXTERNAL_URL` – Public Supabase URL (use local IP for dev, e.g. `http://10.0.1.181:8000`)
- `MQTT_HOST` – MQTT broker hostname (local IP without port for dev)
- `POSTGRES_PASSWORD`, `JWT_SECRET`, `ANON_KEY`, `SERVICE_ROLE_KEY` – Generated secrets
- `KONG_HTTP_PORT` – Gateway port (default 8000)
