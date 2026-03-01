# Production Deployment Guide

This guide covers deploying VMflow to a production server. A single `bash setup.sh` configures, starts, and initializes everything.

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

## 2. Run the Setup Script

A single script handles everything: environment configuration, secret generation, stack startup, and database migrations.

```bash
cd Docker
bash setup.sh
```

The script will:
1. **Configure** — ask for your domain, MQTT host, SMTP (optional), and dashboard credentials
2. **Generate secrets** — all passwords, JWT keys, and tokens are auto-generated via `openssl`
3. **Start the stack** — runs `docker compose up -d` (Supabase, MQTT, frontend)
4. **Apply migrations** — waits for PostgreSQL to be healthy, then applies all SQL migrations
5. **Print summary** — shows URLs, credentials, and next steps

If you re-run `setup.sh` and an `.env` already exists, it will offer to back it up. If you keep the existing `.env`, it skips straight to starting the stack and applying migrations — useful after pulling new code with additional migration files.

### Manual alternative

If you prefer to configure manually instead of using the setup script:

```bash
cd Docker
cp .env .env.backup

# Generate secrets
bash <(curl -s https://raw.githubusercontent.com/supabase/supabase/refs/heads/master/docker/utils/generate-keys.sh)

# Edit .env with generated values + your domain, MQTT host, SMTP config
nano .env

# Start everything
docker compose up -d

# Wait for the database, then apply migrations
docker compose exec db pg_isready -U postgres
for f in supabase/migrations/*.sql; do
  docker compose exec -T db psql -U postgres -d postgres < "$f"
done
```

> **Warning**: Do NOT run `supabase db reset` in production. It drops and recreates the entire database.

---

## 3. Verify the Stack

```bash
cd Docker
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

## 4. Reverse Proxy & TLS

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

## 5. MQTT Security (Production)

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

## 6. Backups

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

## 7. Monitoring & Logs

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

## 8. OTA Firmware Updates

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

## 9. Updating the Platform

Use the dedicated update script:

```bash
cd Docker
bash update.sh
```

The script handles the full update process:

1. **Pulls latest code** — `git pull --ff-only` (fast-forward only, won't auto-merge)
2. **Applies new migrations** — uses a `_migrations` tracking table to skip already-applied SQL files, so only new migrations run
3. **Rebuilds services** — rebuilds the frontend Docker image (multi-stage Nuxt build) and the MQTT forwarder, then restarts edge functions
4. **Health check** — verifies services came back up

### What gets updated

| Component | How it updates |
|-----------|---------------|
| **Frontend** | Docker image rebuilt from source (`docker compose build frontend`), container restarted |
| **Edge functions** | Mounted from `supabase/functions/` volume — container restart picks up changes |
| **Database schema** | New migration `.sql` files applied in order |
| **MQTT forwarder** | Docker image rebuilt, container restarted |
| **Supabase services** | Not rebuilt (use pinned upstream images). To upgrade Supabase itself, update image tags in `docker-compose.yml` and run `docker compose up -d` |

### Migration tracking

The update script tracks applied migrations in a `public._migrations` table. This is created automatically by the `20260302500000_migration_tracking.sql` migration (applied during setup or on the first update). Only migration files not yet in this table are executed, so updates are safe to re-run.

### Manual alternative

If you prefer to update step by step:

```bash
# Pull code
cd /path/to/mdb-esp32-cashless
git pull

# Apply only new migrations
cd Docker
for f in supabase/migrations/*.sql; do
  fname=$(basename "$f")
  already=$(docker compose exec -T db psql -U postgres -d postgres -tAc \
    "SELECT 1 FROM public._migrations WHERE name = '${fname}'" 2>/dev/null)
  if [ "$already" != "1" ]; then
    echo "Applying $fname..."
    docker compose exec -T db psql -U postgres -d postgres < "$f"
    docker compose exec -T db psql -U postgres -d postgres -c \
      "INSERT INTO public._migrations (name) VALUES ('${fname}') ON CONFLICT DO NOTHING"
  fi
done

# Rebuild and restart frontend
docker compose build frontend
docker compose up -d --no-deps frontend

# Restart edge functions
docker compose restart functions
```

### Rollback

If an update causes issues:

```bash
# Revert to the previous commit
cd /path/to/mdb-esp32-cashless
git log --oneline -5          # find the commit to revert to
git checkout <commit-hash>

# Rebuild and restart
cd Docker
docker compose build frontend
docker compose up -d --no-deps frontend
docker compose restart functions
```

> **Note**: Database migrations cannot be automatically rolled back. If a migration causes issues, write a corrective migration or restore from a backup.

---

## Push Notifications

VMflow supports Web Push notifications via PWA (Progressive Web App). When a sale occurs or stock drops below the refill threshold, subscribed users receive a push notification — even when the browser is closed.

### How It Works

1. **PWA**: The Nuxt frontend registers a service worker and exposes an "Add to Home Screen" flow. On iOS (16.4+), the app **must** be added to the Home Screen for push to work.
2. **VAPID keys**: The server authenticates with push services (Apple, Google, Mozilla) using a VAPID ECDSA P-256 key pair. Both the public and private keys are stored in `Docker/.env`; the public key is also in `management-frontend/.env`.
3. **Subscription**: When a user enables notifications in Settings, the browser creates a push subscription (endpoint + encryption keys). This is stored in the `push_subscriptions` table via the `register-push` edge function.
4. **Dispatch**: The `mqtt-webhook` edge function sends push notifications after recording a sale. It checks each user's `notification_preferences` table — if a user hasn't explicitly disabled a notification type, they receive it.

### VAPID Keys

VAPID keys are **automatically generated** by `setup.sh` during initial setup. For existing production servers, `update.sh` detects missing keys and generates them automatically.

The same `VAPID_PUBLIC_KEY` must be present in both:
- `Docker/.env` — used by edge functions to sign push messages
- `management-frontend/.env` — used by the browser to subscribe

**Manual generation** (if needed):

```bash
# Generate an EC P-256 key pair
openssl ecparam -name prime256v1 -genkey -noout -out /tmp/vapid.pem

# Extract the private key (base64url-encoded raw 32 bytes)
openssl ec -in /tmp/vapid.pem -noout -text 2>/dev/null \
  | grep -A3 'priv:' | tail -n+2 | tr -d ' :\n' \
  | xxd -r -p | openssl base64 -A | tr '+/' '-_' | tr -d '='

# Extract the public key (base64url-encoded uncompressed 65 bytes)
openssl ec -in /tmp/vapid.pem -pubout -outform DER 2>/dev/null \
  | tail -c 65 | openssl base64 -A | tr '+/' '-_' | tr -d '='

rm /tmp/vapid.pem
```

Set `VAPID_SUBJECT` to a `mailto:` URI (e.g. `mailto:admin@yourdomain.com`).

### Notification Types

| Type | Trigger | Description |
|------|---------|-------------|
| `sale` | Every vending sale | Item number, price, and payment channel |
| `low_stock` | Stock falls at or below `min_stock` | Product name, machine name, remaining count |

New types can be added by:
1. Adding an entry to `notificationTypes` in `management-frontend/app/composables/useNotifications.ts`
2. Calling `sendPushToUsers()` with the new type key in the relevant edge function

Users configure their preferences in **Settings > Push Notifications**. All types are enabled by default (opt-out model).

### iOS Requirements

- **iOS 16.4+** required for Web Push support
- App must be **added to Home Screen** (Settings page shows guidance)
- Site must be served over **HTTPS**
- User must grant notification permission when prompted

---

## Production Checklist

- [ ] All secrets in `.env` are unique, strong, and not the defaults
- [ ] VAPID keys generated and present in both `Docker/.env` and `management-frontend/.env`
- [ ] Push notifications tested (subscribe in Settings, trigger a sale, verify push arrives)
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
