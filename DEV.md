# Local Development Guide

This guide walks you through setting up the full VMflow development environment on your local machine.

## Prerequisites

| Tool | Version | Purpose |
|------|---------|---------|
| Docker & Docker Compose | v2+ | Backend services (Supabase, MQTT, PostgreSQL) |
| Node.js | 18+ | Management frontend (local dev) |
| npm | 9+ | Package management |
| Supabase CLI | latest | Local Supabase dev (migrations, seed, studio) |
| ESP-IDF | v5.x | Firmware compilation (only if working on firmware) |
| Git | any | Version control |

### Install Supabase CLI

```bash
# macOS
brew install supabase/tap/supabase

# npm (all platforms)
npm install -g supabase

# Linux / Windows (via scoop, etc.) — see https://supabase.com/docs/guides/cli
```

---

## 1. Clone the Repository

```bash
git clone https://github.com/<your-org>/mdb-esp32-cashless.git
cd mdb-esp32-cashless
```

---

## 2. Start Supabase (Local CLI)

The recommended approach for development uses the **Supabase CLI**, which manages PostgreSQL, Auth, Storage, Edge Functions, and Studio as lightweight local containers.

```bash
cd Docker/supabase
supabase start
```

This starts:

| Service | URL |
|---------|-----|
| API (Kong) | `http://127.0.0.1:54321` |
| Database (Postgres) | `postgresql://postgres:postgres@127.0.0.1:54322/postgres` |
| Studio (Dashboard) | `http://127.0.0.1:54323` |
| Inbucket (Email) | `http://127.0.0.1:54324` |

After startup, the CLI prints your local **anon key** and **service_role key**. You'll need the anon key for the frontend `.env`.

### Reset the database (apply all migrations + seed data)

```bash
cd Docker/supabase
supabase db reset
```

This runs all migrations in `Docker/supabase/migrations/` and then the seed file (`seed.sql`), which creates:
- A test user: `test@test.com` / `password123`
- A company ("VMFlow Corp"), device, vending machine, products, categories, trays, and sample sales

### Apply only new migrations (without losing data)

```bash
cd Docker/supabase
supabase migration up
```

### View email confirmations

Navigate to Inbucket at `http://127.0.0.1:54324` to see emails sent by Auth (signup confirmations, invites, etc.). No real email server is needed during development.

---

## 3. Start the MQTT Broker (Optional)

Only needed if you're testing firmware-to-backend communication or the MQTT forwarder.

You can start just the MQTT services from the unified compose file:

```bash
cd Docker
docker compose up broker forwarder -d
```

Ensure these values are set in `Docker/.env`:

```env
MQTT_HOST=<your-lan-ip>          # e.g. 10.0.1.130 (not localhost — devices need LAN access)
MQTT_WEBHOOK_SECRET=change-me-to-a-random-secret
```

When using the Supabase CLI for edge functions, the forwarder needs to reach it. Set `SUPABASE_PUBLIC_URL=http://host.docker.internal:54321` in `.env` so the forwarder container can access the host's Supabase.

### Test MQTT manually

```bash
# Subscribe to all topics
mosquitto_sub -h localhost -p 1883 -t '#' -v

# Publish a test message
mosquitto_pub -h localhost -p 1883 -t '/1/1/status' -m 'online'
```

---

## 4. Start the Management Frontend

```bash
cd management-frontend
npm install
```

### Configure environment

Create or edit `management-frontend/.env`:

```env
SUPABASE_URL=http://127.0.0.1:54321
SUPABASE_KEY=<anon-key-from-supabase-start>
```

> **Important**: Use port `54321` (API), **not** `54323` (Studio).

### Run the dev server

```bash
npm run dev
```

The frontend starts at `http://localhost:3000` with hot-module reload.

### Login with seed user

- Email: `test@test.com`
- Password: `password123`

---

## 5. Firmware Development (Optional)

Only needed if you're working on the ESP32-S3 firmware.

### Setup ESP-IDF

```bash
# Follow the official ESP-IDF installation guide for your platform:
# https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/get-started/

# Activate the environment in your current shell
. $IDF_PATH/export.sh
```

### Build & Flash the Slave (MDB Peripheral)

```bash
cd mdb-slave-esp32s3
idf.py build
idf.py flash monitor    # flash and open serial monitor
```

### Build & Flash the Master (VMC Simulator)

```bash
cd mdb-master-esp32s3
idf.py build
idf.py flash monitor
```

### Configure firmware via menuconfig

```bash
idf.py menuconfig
```

This lets you set hardware pins, WiFi credentials, and feature flags.

### Firmware provisioning flow

1. On first boot (no saved WiFi), the device starts a SoftAP + captive portal
2. Connect to the AP and enter: WiFi SSID, password, provisioning code, server URL
3. The device claims itself via the `claim-device` edge function and reboots
4. After reboot, it connects to WiFi and MQTT normally

Generate a provisioning code from the management frontend (`/machines` page > "Add Device") or via the `create-provisioning-token` edge function.

---

## 6. Full Stack via Docker Compose

If you prefer to run **everything** via Docker Compose (instead of the Supabase CLI + local npm), a single file deploys the entire stack:

```bash
cd Docker
docker compose up -d
```

This starts all services: Supabase (PostgreSQL, Auth, Storage, Edge Functions, Studio, Kong), MQTT (broker + forwarder), and the management frontend.

> **Note**: The Docker Compose stack exposes Kong on port `8000` (not `54321`). The frontend container is pre-configured to connect via the compose network.

### Tear down everything

```bash
docker compose down -v --remove-orphans
```

---

## Project Structure

```
mdb-esp32-cashless/
├── Docker/
│   ├── docker-compose.yml     # Unified stack (Supabase + MQTT + frontend)
│   ├── .env                   # Environment variables
│   ├── supabase/              # Supabase CLI project
│   │   ├── config.toml        # Local Supabase config
│   │   ├── migrations/        # SQL migrations (applied in order)
│   │   ├── functions/         # Deno edge functions (single source of truth)
│   │   └── seed.sql           # Dev seed data
│   ├── mqtt/
│   │   ├── config/            # Mosquitto config + ACL + passwd
│   │   └── forwarder/         # Deno MQTT-to-webhook bridge
│   └── volumes/               # Docker volume data (db init scripts, storage)
├── management-frontend/       # Nuxt 4 dashboard
│   ├── Dockerfile             # Production Docker image
│   ├── app/                   # Nuxt 4 app directory
│   │   ├── pages/             # Route pages
│   │   ├── components/        # Vue components
│   │   ├── composables/       # Reusable logic (useOrganization, useMachines, etc.)
│   │   └── middleware/        # Auth middleware
│   ├── nuxt.config.ts
│   └── package.json
├── mdb-slave-esp32s3/         # ESP32 MDB peripheral firmware
├── mdb-master-esp32s3/        # ESP32 VMC simulator firmware
└── kicad/                     # PCB design files
```

---

## Edge Functions

Edge functions live in `Docker/supabase/functions/`. This is the single source of truth — both the Supabase CLI and Docker Compose mount from this directory.

| Function | Auth | Purpose |
|----------|------|---------|
| `main` | — | Edge runtime router (dispatches to other functions) |
| `claim-device` | None | Firmware provisioning — validates code, creates device |
| `mqtt-webhook` | Shared secret | Receives MQTT data from forwarder, decrypts, writes to DB |
| `send-credit` | JWT | Sends credit to a device via MQTT |
| `request-credit` | JWT | Processes BLE credit requests |
| `create-organization` | JWT | Creates a company + admin membership |
| `invite-member` | Admin | Generates an invitation token |
| `accept-invitation` | JWT | Joins an org via invite token |
| `get-my-organization` | JWT | Returns user's org and role |
| `create-provisioning-token` | Admin | Generates an 8-char one-time device code |

### Testing edge functions locally

```bash
# Via curl (Supabase CLI)
curl -X POST http://127.0.0.1:54321/functions/v1/get-my-organization \
  -H "Authorization: Bearer <access-token>" \
  -H "apikey: <anon-key>"

# Via curl (Docker Compose)
curl -X POST http://127.0.0.1:8000/functions/v1/get-my-organization \
  -H "Authorization: Bearer <access-token>" \
  -H "apikey: <anon-key>"
```

---

## Troubleshooting

### Frontend auth fails on page load (SSR)

The `supabase-url.client.ts` plugin rewrites the Supabase URL to the browser hostname on the client side. SSR calls still use the raw `127.0.0.1` URL. The auth middleware skips org fetching on SSR (`import.meta.server`) to avoid this.

### Edge function returns 401

All edge functions use `verify_jwt = false` in `config.toml` (workaround for ES256 `CryptoKey` bug in local edge runtime). Identity is verified manually inside each function by calling `adminClient.auth.getUser(token)`.

### Database types show `never`

The project does not use generated Supabase types. Query results should be cast manually:
```typescript
const { data } = await client.from('sales').select('*')
const sales = data as { embedded_id: string; item_price: number; /* ... */ }[]
```

### MQTT forwarder can't reach Supabase

When using the Supabase CLI (not Docker Compose), the forwarder runs inside Docker but Supabase is on the host. Set `SUPABASE_PUBLIC_URL=http://host.docker.internal:54321` in `Docker/.env`.

### Ports reference

| Port | Service |
|------|---------|
| 3000 | Nuxt dev server / frontend container |
| 54321 | Supabase API (CLI) |
| 54322 | PostgreSQL (CLI) |
| 54323 | Supabase Studio (CLI) |
| 54324 | Inbucket (email) |
| 8000 | Kong API gateway (Docker Compose) |
| 1883 | MQTT broker |
| 9001 | MQTT WebSockets |
