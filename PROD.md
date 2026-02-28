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

```bash
cd Docker
cp .env .env.backup   # back up defaults
```

Edit `Docker/.env` with production values:

### Generate secrets

```bash
bash <(curl -s https://raw.githubusercontent.com/supabase/supabase/refs/heads/master/docker/utils/generate-keys.sh)
```

This outputs new values for `POSTGRES_PASSWORD`, `JWT_SECRET`, `ANON_KEY`, `SERVICE_ROLE_KEY`, and other secrets. Copy them into `.env`.

### Required changes

```env
############
# Secrets -- MUST be changed
############
POSTGRES_PASSWORD=<generated-strong-password>
JWT_SECRET=<generated-jwt-secret-min-32-chars>
ANON_KEY=<generated-anon-key>
SERVICE_ROLE_KEY=<generated-service-role-key>
DASHBOARD_USERNAME=<strong-username>
DASHBOARD_PASSWORD=<strong-password>
SECRET_KEY_BASE=<generated-secret>
VAULT_ENC_KEY=<generated-32-char-key>
PG_META_CRYPTO_KEY=<generated-32-char-key>

############
# URLs -- set to your domain
############
SUPABASE_PUBLIC_URL=https://supabase.yourdomain.com
API_EXTERNAL_URL=https://supabase.yourdomain.com
SITE_URL=https://app.yourdomain.com

############
# MQTT
############
MQTT_HOST=<your-server-ip-or-domain>
MQTT_WEBHOOK_SECRET=<generated-random-secret>

############
# Email (production SMTP)
############
SMTP_HOST=smtp.sendgrid.net        # or your SMTP provider
SMTP_PORT=587
SMTP_USER=apikey
SMTP_PASS=<your-smtp-api-key>
SMTP_ADMIN_EMAIL=noreply@yourdomain.com
SMTP_SENDER_NAME=VMflow
ENABLE_EMAIL_AUTOCONFIRM=false     # require email verification
```

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

## 9. Updating

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
