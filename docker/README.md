# VMflow Docker Backend

[![Supabase](https://img.shields.io/badge/Supabase-3FCF8E?logo=supabase&logoColor=white)](https://supabase.com)
[![Eclipse Mosquitto](https://img.shields.io/badge/MQTT-Eclipse%20Mosquitto-3C5280?logo=eclipsemosquitto&logoColor=white)](https://mosquitto.org)
[![Vue.js](https://img.shields.io/badge/Vue.js-4FC08D?logo=vuedotjs&logoColor=white)](https://vuejs.org)
[![n8n](https://img.shields.io/badge/n8n-EA4B71?logo=n8n&logoColor=white)](https://n8n.io)

Self-hosted backend stack for [VMflow](https://vmflow.xyz): Supabase, MQTT broker (Eclipse Mosquitto), and Vue.js dashboard.

## Setup

1. Copy `.env.example` to `.env`:
   ```bash
   cp .env.example .env
   ```

2. Generate secrets automatically:
   ```bash
   bash <(curl -s https://raw.githubusercontent.com/supabase/supabase/refs/heads/master/docker/utils/generate-keys.sh)
   ```

3. Configure the following variables in `.env`:

   | Variable | Description | Example |
   |----------|-------------|---------|
   | `SUPABASE_PUBLIC_URL` | Public-facing URL | `https://supabase.vmflow.xyz` |
   | `API_EXTERNAL_URL` | Public-facing API URL | `https://supabase.vmflow.xyz` |
   | `N8N_HOST` | n8n hostname | `n8n.vmflow.xyz` |
   | `MQTT_HOST` | MQTT broker hostname | `mqtt.vmflow.xyz` |

   > **Local development:** set `SUPABASE_PUBLIC_URL` and `API_EXTERNAL_URL` to your local IP with port 8000 (e.g. `http://10.0.1.181:8000`) and `MQTT_HOST` to the same IP without port.

## Usage

```bash
# Supabase only
docker compose up

# Supabase + MQTT broker
docker compose -f docker-compose.yml -f docker-compose.mqtt.yml up

# Supabase + Vue.js dashboard
docker compose -f docker-compose.yml -f docker-compose.vuejs.yml up

# Stop
docker compose down

# Destroy (removes volumes)
docker compose -f docker-compose.yml -f docker-compose.mqtt.yml down -v --remove-orphans
```

## Services

| Service | Compose file | Ports |
|---------|-------------|-------|
| Supabase | `docker-compose.yml` | `8000` (API) |
| MQTT Broker | `docker-compose.mqtt.yml` | `1883` (TCP), `9001` (WebSocket) |
| Vue.js Dashboard | `docker-compose.vuejs.yml` | `5173` |
