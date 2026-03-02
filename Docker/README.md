# VMflow Docker

Single Docker Compose file to self-host the entire VMflow stack:
- **Supabase** (PostgreSQL, Auth, Storage, Edge Functions, Studio, Kong)
- **MQTT** (Mosquitto broker + Deno forwarder)
- **Management Frontend** (Nuxt 4 dashboard)

## Quick Start

```bash
cd Docker
cp .env.example .env   # or edit the existing .env
docker compose up -d
```

## Setup

1. Copy or edit `Docker/.env` with your secrets.
2. Generate Supabase secrets automatically:
   ```bash
   bash <(curl -s https://raw.githubusercontent.com/supabase/supabase/refs/heads/master/docker/utils/generate-keys.sh)
   ```
3. Set these to match your environment:
   - `SUPABASE_PUBLIC_URL` / `API_EXTERNAL_URL` — public Supabase URL (e.g. `https://supabase.yourdomain.com`)
   - `MQTT_HOST` — broker address devices connect to (LAN IP or public hostname, no port)
   - `MQTT_WEBHOOK_SECRET` — shared secret between forwarder and webhook function

### Local development note

For local testing, set `SUPABASE_PUBLIC_URL` and `API_EXTERNAL_URL` to your LAN IP with port 8000 (e.g. `http://10.0.1.181:8000`) and `MQTT_HOST` to your LAN IP (no port).

## Services

| Service | Port | Description |
|---------|------|-------------|
| `frontend` | 3000 | Nuxt 4 management dashboard |
| `kong` | 8000 | Supabase API gateway |
| `broker` | 1883, 9001 | MQTT broker (TCP + WebSocket) |
| `db` | 5432 (localhost only) | PostgreSQL |
| `studio` | via Kong | Supabase Studio dashboard |
| `functions` | via Kong | Deno edge functions |
| `forwarder` | — | MQTT-to-Supabase webhook bridge |

## Commands

```bash
docker compose up -d                       # start all services
docker compose down                        # stop all services
docker compose down -v --remove-orphans    # destroy everything (including data)
docker compose logs -f [service]           # tail logs
docker compose build                       # rebuild images (frontend, forwarder)
```

See [DEV.md](../DEV.md) and [PROD.md](../PROD.md) for detailed development and production guides.
