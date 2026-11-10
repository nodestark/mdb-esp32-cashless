# VMflow Docker Backend

[![Supabase](https://img.shields.io/badge/Supabase-3FCF8E?logo=supabase&logoColor=white)](https://supabase.com)
[![Eclipse Mosquitto](https://img.shields.io/badge/MQTT-Eclipse%20Mosquitto-3C5280?logo=eclipsemosquitto&logoColor=white)](https://mosquitto.org)
[![Vue.js](https://img.shields.io/badge/Vue.js-4FC08D?logo=vuedotjs&logoColor=white)](https://vuejs.org)

Self-hosted backend for [VMflow](https://vmflow.xyz): Supabase (Postgres, Auth, REST, Realtime, Storage, Edge Functions), an MQTT broker (Eclipse Mosquitto), the MQTT-to-database bridge (`domain`) and the Vue.js dashboard.

## Requirements

- Docker Engine 24+ with the Compose plugin (`docker compose version`)
- `openssl` (used to generate secrets)
- About 4 GB RAM and 6 GB free disk for the images
- Free host ports: `8000` (API), `1883`/`9001` (MQTT), `5173` (dashboard), `5432` (Postgres, loopback only). Each one can be changed in `.env`.

## Quick start (local)

```bash
cd docker
cp .env.example .env
./scripts/gen-secrets.sh .env          # fills JWT secret, API keys and passwords

# API + MQTT + dashboard, plus a local mail catcher for sign-up emails
docker compose -f docker-compose.yml -f docker-compose.vuejs.yml --profile dev up -d --build
```

The first start initialises the database and can take a couple of minutes. Then:

| What | URL |
|------|-----|
| Dashboard | http://localhost:5173 |
| API (Kong) | http://localhost:8000 |
| Supabase Studio | http://localhost:8000 (user `DASHBOARD_USERNAME`, password `DASHBOARD_PASSWORD` from `.env`) |
| Sign-up / recovery emails (dev) | http://localhost:8025 |
| MQTT | `mqtt://localhost:1883` |

Create an account in the dashboard, open the confirmation email in Mailpit (http://localhost:8025) and sign in.

Check that everything is up:

```bash
docker compose -f docker-compose.yml -f docker-compose.vuejs.yml ps -a
```

All services should be `running`/`healthy`. The `bootstrap` service is one-shot, so it shows `Exited (0)`.

## Production

Edit `.env` before the first start:

| Variable | Example | Notes |
|----------|---------|-------|
| `SUPABASE_PUBLIC_URL`, `API_EXTERNAL_URL` | `https://supabase.example.com` | Public URL of Kong (port 8000 behind your TLS reverse proxy) |
| `SITE_URL` | `https://app.example.com` | Dashboard URL. Auth emails redirect here |
| `ADDITIONAL_REDIRECT_URLS` | | Other allowed redirect URLs, comma-separated |
| `SMTP_HOST`, `SMTP_PORT`, `SMTP_USER`, `SMTP_PASS`, `SMTP_ADMIN_EMAIL`, `SMTP_SENDER_NAME` | | A real SMTP server. Without one, sign-up fails with `Error sending confirmation email` |
| `MQTT_PORT`, `MQTT_WS_PORT`, `POSTGRES_HOST_PORT`, `DASHBOARD_PORT` | | Host ports |

Start the stack without the `dev` profile, so Mailpit is not started:

```bash
docker compose -f docker-compose.yml -f docker-compose.vuejs.yml up -d --build
```

The dashboard is built with `SUPABASE_PUBLIC_URL` and `ANON_KEY` baked in. Rebuild it (`--build`) whenever either one changes.

Devices currently connect to `mqtt.vmflow.xyz` (hard-coded in the firmware). To use your own broker, point that hostname at your server or build the firmware with your host.

## Services

| Service | Role | Host port |
|---------|------|-----------|
| `kong` | API gateway for everything under `/auth`, `/rest`, `/realtime`, `/storage`, `/functions` | `8000` |
| `db` | Postgres 15 with the Supabase extensions, plus the VMflow schema loaded on first boot | `127.0.0.1:5432` |
| `auth`, `rest`, `realtime`, `storage`, `imgproxy`, `meta`, `functions`, `studio` | Supabase services | none |
| `bootstrap` | One-shot. Creates the `product-images` bucket and its policies, and the realtime publication | none |
| `broker` | Mosquitto | `1883`, `9001` |
| `domain` | Bridges MQTT device events (`status`, `sale`, `vend_*`, `paxcounter`) into the database | none |
| `dashboard` | Vue SPA served by nginx (`docker-compose.vuejs.yml`) | `5173` |
| `mail` | Mailpit, `dev` profile only | `8025` |
| `dashboard-dev` | Vite dev server with hot reload, `dev` profile only | `5174` |

## Day-to-day

```bash
C="docker compose -f docker-compose.yml -f docker-compose.vuejs.yml"
$C logs -f domain            # follow a service
$C restart kong              # see Troubleshooting
$C down                      # stop (data is kept)
```

### Full reset (deletes all data)

`docker compose down -v` does not delete the database. Its data lives in bind mounts under `volumes/`, owned by root:

```bash
$C down -v
docker run --rm -v "$PWD/volumes:/v" alpine rm -rf /v/db/data /v/storage /v/mqtt/data
```

## Troubleshooting

| Symptom | Cause and fix |
|---------|---------------|
| `502` from `localhost:8000` right after recreating one service | Kong cached the old container IP. Run `$C restart kong` |
| `auth` restarting with `running db migrations: error executing migrations/00_init_auth_schema.up.sql` | Database init was interrupted, so `volumes/db/data` is half-initialised. Do a full reset |
| `Bind for 0.0.0.0:1883 failed: port is already allocated` | Another broker or Postgres is running. Change `MQTT_PORT` / `POSTGRES_HOST_PORT` in `.env` |
| `Conflict. The container name "/realtime-dev.supabase-realtime" is already in use` | Another copy of the stack is running. Realtime needs that fixed container name, so only one copy can run per host |
| Sign-up returns `Error sending confirmation email` | SMTP is not configured. Use `--profile dev` locally, or set `SMTP_*` |
| Dashboard talks to `supabase.vmflow.xyz` instead of your server | It was built without the build args. Rebuild with `$C up -d --build dashboard` |

The database schema is `supabase/migrations/20260718000000_schema_dump.sql`. It only runs when `volumes/db/data` is empty. Apply later schema changes to an existing database with `psql`.
