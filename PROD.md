# Production Deployment Guide

This guide covers deploying VMflow to a production server. A single `docker compose up` deploys the entire stack.

## Architecture Overview

```
Internet
   |
   +-- HTTPS (443) --> Reverse Proxy (Caddy/Nginx/Traefik)
   |                      +-- supabase.yourdomain.com  --> Kong (:8000)
   |                      +-- app.yourdomain.com       --> Nuxt (:3000)
   |
   +-- MQTT (1883) ---> Mosquitto Broker (:1883)
                           +-- Forwarder --> mqtt-webhook edge function
```

## Prerequisites

| Requirement | Details |
|-------------|---------|
| VPS / Server | 2+ CPU cores, 4+ GB RAM, 40+ GB disk |
| Docker & Docker Compose | v2+ |
| Domain name | With DNS A records pointing to your server |

> Node.js is **not** required on the server. The frontend is built inside Docker.

---

## 1. Server Setup

### Install Docker

```bash
# Ubuntu/Debian
curl -fsSL https://get.docker.com | sh
sudo usermod -aG docker $USER
```

### Clone the repository

```bash
git clone https://github.com/<your-org>/mdb-esp32-cashless.git
cd mdb-esp32-cashless
```

---

## 2. Configure Environment Variables

### Option A: Interactive setup script (recommended)

The setup script generates all secrets, asks for your domain/MQTT/SMTP settings, and writes a complete `.env` file:

```bash
cd Docker
bash setup.sh
```

The script will:
- Auto-generate all cryptographic secrets (passwords, JWT keys, tokens)
- Ask for your domain name and derive all URLs (`supabase.yourdomain.com`, `app.yourdomain.com`)
- Optionally configure SMTP (can be skipped and added later)
- Ask for Supabase Studio dashboard credentials
- Optionally start the stack and apply database migrations

### Option B: Manual configuration

If you prefer to configure manually, copy and edit the `.env` file:

```bash
cd Docker
cp .env .env.backup
```

Generate secrets using the Supabase utility script:

```bash
bash <(curl -s https://raw.githubusercontent.com/supabase/supabase/refs/heads/master/docker/utils/generate-keys.sh)
```

Then edit `Docker/.env` with the generated values and set your domain, MQTT host, and SMTP configuration. See the comments in `.env` for guidance on each variable.

---

## 3. Database Migrations

Before starting services for the first time, you need to apply database migrations. The Supabase Docker image initializes a bare PostgreSQL database -- migrations create your application schema.

### Option A: Using Supabase CLI (recommended)

If the Supabase CLI is installed on the server:

```bash
cd Docker

# Start only Postgres first
docker compose up db -d

# Wait for it to be healthy
docker compose exec db pg_isready -U postgres

# Apply migrations
cd supabase
supabase db push --db-url postgresql://postgres:<POSTGRES_PASSWORD>@127.0.0.1:5432/postgres
```

### Option B: Manual migration

```bash
cd Docker

# Start only Postgres
docker compose up db -d
docker compose exec db pg_isready -U postgres

# Apply each migration file in order
for f in supabase/migrations/*.sql; do
  echo "Applying $f..."
  docker compose exec -T db psql -U postgres -d postgres < "$f"
done
```

### Applying future migrations

When you pull new code with additional migration files:

```bash
# With Supabase CLI
cd Docker/supabase
supabase db push --db-url postgresql://postgres:<POSTGRES_PASSWORD>@127.0.0.1:5432/postgres

# Or manually
docker compose exec -T db psql -U postgres -d postgres < supabase/migrations/<new-migration>.sql
```

> **Warning**: Do NOT run `supabase db reset` in production. It drops and recreates the entire database.

---

## 4. Start the Stack

A single command deploys everything (Supabase, MQTT, and the management frontend):

```bash
cd Docker
docker compose up -d
```

Verify services are running:

```bash
docker compose ps
```

All services should show `Up (healthy)` or `Up`.

### Test API access

```bash
curl -s http://localhost:8000/rest/v1/ \
  -H "apikey: <your-anon-key>" | head -c 200
```

### Test the frontend

Open `http://<your-server-ip>:3000` in a browser (before setting up the reverse proxy).

---

## 5. Reverse Proxy & TLS

You need a reverse proxy to terminate TLS and route traffic.

### Option A: Caddy (simplest -- automatic HTTPS)

Install Caddy and create `/etc/caddy/Caddyfile`:

```caddyfile
supabase.yourdomain.com {
    reverse_proxy localhost:8000
}

app.yourdomain.com {
    reverse_proxy localhost:3000
}
```

```bash
sudo systemctl restart caddy
```

Caddy automatically provisions and renews Let's Encrypt certificates.

### Option B: Nginx + Certbot

```nginx
# /etc/nginx/sites-available/vmflow

server {
    listen 80;
    server_name supabase.yourdomain.com app.yourdomain.com;
    return 301 https://$host$request_uri;
}

server {
    listen 443 ssl;
    server_name supabase.yourdomain.com;

    ssl_certificate /etc/letsencrypt/live/supabase.yourdomain.com/fullchain.pem;
    ssl_certificate_key /etc/letsencrypt/live/supabase.yourdomain.com/privkey.pem;

    location / {
        proxy_pass http://127.0.0.1:8000;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto $scheme;

        # WebSocket support (for Supabase Realtime)
        proxy_http_version 1.1;
        proxy_set_header Upgrade $http_upgrade;
        proxy_set_header Connection "upgrade";
    }
}

server {
    listen 443 ssl;
    server_name app.yourdomain.com;

    ssl_certificate /etc/letsencrypt/live/app.yourdomain.com/fullchain.pem;
    ssl_certificate_key /etc/letsencrypt/live/app.yourdomain.com/privkey.pem;

    location / {
        proxy_pass http://127.0.0.1:3000;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto $scheme;
    }
}
```

```bash
sudo certbot --nginx -d supabase.yourdomain.com -d app.yourdomain.com
sudo systemctl restart nginx
```

---

## 6. MQTT Security (Production)

The default Mosquitto config allows anonymous connections. For production, harden it.

### Edit `Docker/mqtt/config/mosquitto.conf`:

```conf
listener 1883
persistence true
persistence_location /mosquitto/data

# Disable anonymous access
allow_anonymous false
password_file /mosquitto/config/passwd

# Optional: enable TLS
# listener 8883
# certfile /mosquitto/certs/server.crt
# keyfile /mosquitto/certs/server.key
# cafile /mosquitto/certs/ca.crt
```

### Update MQTT passwords

```bash
# Generate a hashed password file
docker compose exec broker mosquitto_passwd -c /mosquitto/config/passwd vmflow
# Enter a strong password when prompted

# Restart the broker
docker compose restart broker
```

Update the forwarder and firmware to use the new MQTT credentials.

### Firewall rules

```bash
# Allow only necessary ports
sudo ufw allow 22/tcp    # SSH
sudo ufw allow 80/tcp    # HTTP (redirect to HTTPS)
sudo ufw allow 443/tcp   # HTTPS
sudo ufw allow 1883/tcp  # MQTT (or restrict to known device IPs)
sudo ufw enable
```

> Consider restricting port `1883` to known IP ranges if your devices have static IPs or a VPN.

---

## 7. Backups

### Database backups

```bash
# Manual backup
docker compose exec db pg_dump -U postgres postgres > backup_$(date +%Y%m%d).sql

# Restore
docker compose exec -T db psql -U postgres postgres < backup_20260301.sql
```

### Automated daily backups (cron)

```bash
crontab -e
```

Add:

```
0 3 * * * cd /path/to/mdb-esp32-cashless/Docker && docker compose exec -T db pg_dump -U postgres postgres | gzip > /backups/vmflow_$(date +\%Y\%m\%d).sql.gz
```

### Storage backups

Product images are stored in `Docker/volumes/storage/`. Back up this directory alongside database dumps.

---

## 8. Monitoring & Logs

### View service logs

```bash
cd Docker

# All services
docker compose logs -f

# Specific service
docker compose logs -f auth
docker compose logs -f functions
docker compose logs -f broker
docker compose logs -f frontend
```

### Supabase Studio

Studio is available at `http://localhost:8000` (via Kong) with the `DASHBOARD_USERNAME` / `DASHBOARD_PASSWORD` credentials. To expose it externally, add it to your reverse proxy config. Consider restricting access via IP allowlist or VPN.

---

## 9. OTA Firmware Updates

The management dashboard supports over-the-air firmware updates to ESP32 devices in the field.

### Overview

```
Developer machine                 Server                         ESP32 Device
     |                              |                                |
     |  1. idf.py build             |                                |
     |  2. Upload .bin via UI  ---->|  Stored in Supabase Storage    |
     |  3. Click "Trigger OTA" ---->|  trigger-ota edge function     |
     |                              |    |                           |
     |                              |    +-- Publishes OTA command   |
     |                              |        via MQTT with           |
     |                              |        download URL            |
     |                              |              |                 |
     |                              |              +---------------->|
     |                              |                                |  4. Downloads .bin
     |                              |<-------------------------------|     from Storage
     |                              |                                |  5. Flashes & reboots
     |                              |                                |  6. Reports new version
     |                              |                                |     via MQTT status
```

### Step-by-step process

#### 1. Build the firmware

On your development machine:

```bash
cd mdb-slave-esp32s3

# Tag the release version (this becomes the firmware version string)
git tag v1.1.0
git push origin v1.1.0

# Clean build to pick up the new tag and a fresh build timestamp
idf.py fullclean && idf.py build
```

The built binary is at `build/mdb-slave-esp32s3.bin`.

> **Version & build date**: The firmware version comes from `git describe --tags`. The build timestamp includes the build machine's timezone offset, so it displays correctly for any viewer regardless of their timezone.

#### 2. Upload the firmware binary

1. Open the management dashboard and navigate to the **Firmware** page (`/firmware`)
2. Click **Upload New Firmware**
3. Select the `.bin` file from `build/mdb-slave-esp32s3.bin`
4. Enter a version label (e.g., `v1.1.0`) and optional release notes
5. Click **Upload** — the file is stored in the `firmware` Supabase Storage bucket

#### 3. Trigger OTA on a device

1. On the same **Firmware** page, select the firmware version and a target device from the dropdown
2. Click **Trigger OTA Update**
3. The `trigger-ota` edge function:
   - Looks up the firmware download URL from Storage
   - Publishes an OTA command to the device's MQTT topic (`/{company}/{device}/ota`)
   - The download URL uses `PUBLIC_SUPABASE_URL` (Supabase CLI) or `SUPABASE_PUBLIC_URL` (Docker Compose) so the device can reach it

#### 4. Device-side update

The device:
1. Receives the OTA command via MQTT
2. Sets its status to `ota_updating` (visible in the dashboard as a yellow badge)
3. Downloads the firmware binary over HTTP from Supabase Storage
4. Validates and flashes it to the OTA partition
5. Reboots into the new firmware
6. Publishes its new version + build date via the MQTT status topic
7. Status returns to `online` with the updated version in the dashboard

If the update fails, the device rolls back to the previous firmware and reports `ota_failed` (red badge).

### Important notes

- **HTTP allowed for OTA**: The firmware's `sdkconfig` has `CONFIG_OTA_ALLOW_HTTP=y` for local development. For production over the internet, serve Supabase Storage behind HTTPS and disable HTTP OTA.
- **`PUBLIC_SUPABASE_URL` / `SUPABASE_PUBLIC_URL`**: The OTA download URL must be reachable by the ESP32 device. In the Docker Compose setup, the internal `SUPABASE_URL` (`http://kong:8000`) is not accessible to devices — the public URL is used instead.
- **MQTT must be reachable**: The device must be connected to MQTT to receive the OTA trigger. Offline devices will not receive the update.
- **One device at a time**: Currently, OTA is triggered per-device. For fleet-wide rollouts, trigger each device individually from the UI.

---

## 10. Updating the Platform

### Pull latest code

```bash
git pull origin main
```

### Apply new migrations

```bash
cd Docker/supabase
supabase db push --db-url postgresql://postgres:<PASSWORD>@127.0.0.1:5432/postgres
```

### Rebuild and restart services

```bash
cd Docker

# Rebuild images (frontend, forwarder)
docker compose build

# Restart with new images
docker compose up -d
```

---

## Production Checklist

- [ ] All secrets in `.env` are unique, strong, and not the defaults
- [ ] `ENABLE_EMAIL_AUTOCONFIRM=false` -- users must verify email
- [ ] SMTP configured with a real email provider (SendGrid, Mailgun, etc.)
- [ ] TLS/HTTPS enabled on all public endpoints
- [ ] MQTT `allow_anonymous` set to `false`
- [ ] Firewall configured (only ports 22, 80, 443, 1883 open)
- [ ] Database backups automated
- [ ] Storage volume (`Docker/volumes/storage/`) backed up
- [ ] `DISABLE_SIGNUP=true` if you don't want public registration
- [ ] Supabase Studio access restricted (IP allowlist or VPN)
- [ ] `SUPABASE_PUBLIC_URL` and `API_EXTERNAL_URL` use HTTPS URLs
- [ ] Firmware devices configured with production MQTT host and server URL
- [ ] `SUPABASE_PUBLIC_URL` in `Docker/.env` is reachable by ESP32 devices (for OTA downloads)
- [ ] Consider disabling `CONFIG_OTA_ALLOW_HTTP` in firmware and serving Storage over HTTPS only

---

## DNS Records

| Record | Type | Value |
|--------|------|-------|
| `supabase.yourdomain.com` | A | `<server-ip>` |
| `app.yourdomain.com` | A | `<server-ip>` |

---

## Service Ports (Internal)

These ports are used internally. Only expose them through the reverse proxy.

| Port | Service | Expose externally? |
|------|---------|--------------------|
| 8000 | Kong (Supabase API gateway) | Yes, via HTTPS reverse proxy |
| 3000 | Nuxt frontend | Yes, via HTTPS reverse proxy |
| 1883 | MQTT broker | Yes, for ESP32 devices |
| 5432 | PostgreSQL | No -- bind to 127.0.0.1 only |
| 9001 | MQTT WebSockets | Optional (for browser MQTT clients) |
