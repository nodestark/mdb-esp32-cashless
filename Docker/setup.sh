#!/usr/bin/env bash
set -euo pipefail

# ─── Colors ───────────────────────────────────────────────────────────────────
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
BOLD='\033[1m'
NC='\033[0m'

# ─── Utility functions ────────────────────────────────────────────────────────
info()    { echo -e "${BLUE}[INFO]${NC} $*"; }
warn()    { echo -e "${YELLOW}[WARN]${NC} $*"; }
error()   { echo -e "${RED}[ERROR]${NC} $*" >&2; exit 1; }
success() { echo -e "${GREEN}  ✔${NC} $*"; }

prompt_with_default() {
    local prompt="$1"
    local default="$2"
    local result
    if [ -n "$default" ]; then
        read -rp "$(echo -e "${CYAN}${prompt}${NC} [${default}]: ")" result
        echo "${result:-$default}"
    else
        read -rp "$(echo -e "${CYAN}${prompt}${NC}: ")" result
        echo "$result"
    fi
}

prompt_secret() {
    local prompt="$1"
    local default="${2:-}"
    local result
    if [ -n "$default" ]; then
        read -rsp "$(echo -e "${CYAN}${prompt}${NC} [${default}]: ")" result
        echo
        echo "${result:-$default}"
    else
        read -rsp "$(echo -e "${CYAN}${prompt}${NC}: ")" result
        echo
        echo "$result"
    fi
}

prompt_yes_no() {
    local prompt="$1"
    local default="${2:-n}"
    local hint
    if [ "$default" = "y" ]; then hint="[Y/n]"; else hint="[y/N]"; fi
    local result
    read -rp "$(echo -e "${CYAN}${prompt}${NC} ${hint}: ")" result
    result="${result:-$default}"
    [[ "$result" =~ ^[Yy] ]]
}

generate_random() { openssl rand -base64 "$1" 2>/dev/null | tr -d '\n'; }
generate_hex()    { openssl rand -hex "$1" 2>/dev/null | tr -d '\n'; }

# ─── JWT generation (pure openssl) ───────────────────────────────────────────
base64url_encode() {
    openssl base64 -A | tr '+/' '-_' | tr -d '='
}

generate_jwt() {
    local secret="$1"
    local role="$2"
    local now
    now=$(date +%s)
    local exp=$((now + 10 * 365 * 24 * 60 * 60))

    local header='{"alg":"HS256","typ":"JWT"}'
    local payload="{\"role\":\"${role}\",\"iss\":\"supabase\",\"iat\":${now},\"exp\":${exp}}"

    local header_b64 payload_b64 signature
    header_b64=$(printf '%s' "$header" | base64url_encode)
    payload_b64=$(printf '%s' "$payload" | base64url_encode)
    signature=$(printf '%s' "${header_b64}.${payload_b64}" | openssl dgst -sha256 -hmac "$secret" -binary | base64url_encode)

    echo "${header_b64}.${payload_b64}.${signature}"
}

# ─── Cleanup on interrupt ────────────────────────────────────────────────────
cleanup() {
    echo
    warn "Setup interrupted. If .env was partially written, remove it and re-run."
    exit 1
}
trap cleanup INT TERM

# ─── Change to script directory ──────────────────────────────────────────────
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# ═══════════════════════════════════════════════════════════════════════════════
echo -e "\n${BOLD}╔══════════════════════════════════════════════╗${NC}"
echo -e "${BOLD}║         VMflow Production Setup              ║${NC}"
echo -e "${BOLD}╚══════════════════════════════════════════════╝${NC}\n"

# ─── 1. Prerequisite checks ──────────────────────────────────────────────────
info "Checking prerequisites..."

command -v docker >/dev/null 2>&1 || error "Docker is not installed. See https://docs.docker.com/engine/install/"
docker compose version >/dev/null 2>&1 || error "Docker Compose v2 is not installed."
command -v openssl >/dev/null 2>&1 || error "OpenSSL is not installed."

# Test that openssl HMAC signing works
TEST_SIG=$(printf 'test' | openssl dgst -sha256 -hmac "key" -binary 2>/dev/null | base64url_encode 2>/dev/null) || true
if [ -z "$TEST_SIG" ]; then
    error "openssl dgst -sha256 -hmac is not supported on this system."
fi

success "All prerequisites met"

# ─── 2. Existing .env detection ──────────────────────────────────────────────
if [ -f .env ]; then
    echo
    warn "An existing .env file was found."
    if prompt_yes_no "Back up existing .env and create a new one?" "y"; then
        BACKUP=".env.backup.$(date +%Y%m%d_%H%M%S)"
        cp .env "$BACKUP"
        success "Backed up to ${BACKUP}"
    else
        info "Exiting. Edit .env manually or remove it and re-run setup."
        exit 0
    fi
fi

# ═══════════════════════════════════════════════════════════════════════════════
# Interactive Prompts
# ═══════════════════════════════════════════════════════════════════════════════

# ─── 3a. Domain configuration ────────────────────────────────────────────────
echo -e "\n${BOLD}── Domain Configuration ──${NC}\n"

DOMAIN=""
while [ -z "$DOMAIN" ] || [[ ! "$DOMAIN" =~ \. ]]; do
    DOMAIN=$(prompt_with_default "Enter your domain name (e.g. yourdomain.com)" "")
    if [ -z "$DOMAIN" ] || [[ ! "$DOMAIN" =~ \. ]]; then
        warn "Please enter a valid domain name (must contain a dot)."
    fi
done

SUPABASE_PUBLIC_URL="https://supabase.${DOMAIN}"
API_EXTERNAL_URL="https://supabase.${DOMAIN}"
SITE_URL="https://app.${DOMAIN}"

echo
info "Derived URLs:"
echo -e "  Supabase API:  ${GREEN}${SUPABASE_PUBLIC_URL}${NC}"
echo -e "  Frontend:      ${GREEN}${SITE_URL}${NC}"
echo -e "  Auth external: ${GREEN}${API_EXTERNAL_URL}${NC}"

# ─── 3b. MQTT configuration ──────────────────────────────────────────────────
echo -e "\n${BOLD}── MQTT Configuration ──${NC}\n"

MQTT_HOST=$(prompt_with_default "MQTT broker host (IP or hostname reachable by ESP32 devices)" "mqtt.${DOMAIN}")

# ─── 3c. SMTP configuration (optional) ───────────────────────────────────────
echo -e "\n${BOLD}── Email / SMTP Configuration ──${NC}\n"

CONFIGURE_SMTP=false
ENABLE_EMAIL_AUTOCONFIRM="false"

if prompt_yes_no "Configure SMTP now? (You can skip this and configure later)" "n"; then
    CONFIGURE_SMTP=true
    SMTP_HOST=$(prompt_with_default "SMTP host" "smtp.sendgrid.net")
    SMTP_PORT=$(prompt_with_default "SMTP port" "587")
    SMTP_USER=$(prompt_with_default "SMTP user" "apikey")
    SMTP_PASS=$(prompt_secret "SMTP password")
    while [ -z "$SMTP_PASS" ]; do
        warn "SMTP password cannot be empty."
        SMTP_PASS=$(prompt_secret "SMTP password")
    done
    SMTP_ADMIN_EMAIL=$(prompt_with_default "Sender email address" "noreply@${DOMAIN}")
    SMTP_SENDER_NAME=$(prompt_with_default "Sender display name" "VMflow")
else
    warn "SMTP not configured. Email verification will be disabled (auto-confirm enabled)."
    warn "Configure SMTP in .env before going to production."
    SMTP_HOST="supabase-mail"
    SMTP_PORT="2500"
    SMTP_USER="fake_mail_user"
    SMTP_PASS="fake_mail_password"
    SMTP_ADMIN_EMAIL="admin@example.com"
    SMTP_SENDER_NAME="VMflow"
    ENABLE_EMAIL_AUTOCONFIRM="true"
fi

# ─── 3d. Dashboard credentials ───────────────────────────────────────────────
echo -e "\n${BOLD}── Supabase Studio Dashboard ──${NC}\n"

DASHBOARD_USERNAME=$(prompt_with_default "Dashboard username" "supabase")
DEFAULT_DASHBOARD_PASS=$(generate_hex 16)
DASHBOARD_PASSWORD=$(prompt_with_default "Dashboard password" "$DEFAULT_DASHBOARD_PASS")

# ─── 3e. Signup toggle ───────────────────────────────────────────────────────
echo -e "\n${BOLD}── Application Settings ──${NC}\n"

DISABLE_SIGNUP="false"
if prompt_yes_no "Disable public signup? (Recommended for private deployments)" "n"; then
    DISABLE_SIGNUP="true"
fi

# ═══════════════════════════════════════════════════════════════════════════════
# Secret Generation
# ═══════════════════════════════════════════════════════════════════════════════
echo -e "\n${BOLD}── Generating Secrets ──${NC}\n"

POSTGRES_PASSWORD=$(generate_hex 32)
JWT_SECRET=$(generate_random 32)
SECRET_KEY_BASE=$(generate_random 48)
VAULT_ENC_KEY=$(generate_hex 16)
PG_META_CRYPTO_KEY=$(generate_random 24)
LOGFLARE_PUBLIC_ACCESS_TOKEN=$(generate_random 32)
LOGFLARE_PRIVATE_ACCESS_TOKEN=$(generate_random 32)
MQTT_WEBHOOK_SECRET=$(generate_hex 16)
POOLER_TENANT_ID=$(generate_hex 8)

success "Generated POSTGRES_PASSWORD"
success "Generated JWT_SECRET"
success "Generated SECRET_KEY_BASE"
success "Generated VAULT_ENC_KEY"
success "Generated PG_META_CRYPTO_KEY"
success "Generated LOGFLARE tokens"
success "Generated MQTT_WEBHOOK_SECRET"

# ─── JWT Key Generation ──────────────────────────────────────────────────────
info "Generating JWT keys (ANON_KEY, SERVICE_ROLE_KEY)..."

ANON_KEY=$(generate_jwt "$JWT_SECRET" "anon")
SERVICE_ROLE_KEY=$(generate_jwt "$JWT_SECRET" "service_role")

success "Generated ANON_KEY"
success "Generated SERVICE_ROLE_KEY"

# ═══════════════════════════════════════════════════════════════════════════════
# Write .env
# ═══════════════════════════════════════════════════════════════════════════════
info "Writing .env file..."

cat > .env << ENVEOF
############
# Secrets
# Generated by setup.sh on $(date -u +"%Y-%m-%d %H:%M:%S UTC")
############

POSTGRES_PASSWORD=${POSTGRES_PASSWORD}
JWT_SECRET=${JWT_SECRET}
ANON_KEY=${ANON_KEY}
SERVICE_ROLE_KEY=${SERVICE_ROLE_KEY}
DASHBOARD_USERNAME=${DASHBOARD_USERNAME}
DASHBOARD_PASSWORD=${DASHBOARD_PASSWORD}
SECRET_KEY_BASE=${SECRET_KEY_BASE}
VAULT_ENC_KEY=${VAULT_ENC_KEY}
PG_META_CRYPTO_KEY=${PG_META_CRYPTO_KEY}


############
# Database - You can change these to any PostgreSQL database that has logical replication enabled.
############

POSTGRES_HOST=db
POSTGRES_DB=postgres
POSTGRES_PORT=5432
# default user is postgres


############
# Supavisor -- Database pooler
############
# Port Supavisor listens on for transaction pooling connections
POOLER_PROXY_PORT_TRANSACTION=6543
# Maximum number of PostgreSQL connections Supavisor opens per pool
POOLER_DEFAULT_POOL_SIZE=20
# Maximum number of client connections Supavisor accepts per pool
POOLER_MAX_CLIENT_CONN=100
# Unique tenant identifier
POOLER_TENANT_ID=${POOLER_TENANT_ID}
# Pool size for internal metadata storage used by Supavisor
# This is separate from client connections and used only by Supavisor itself
POOLER_DB_POOL_SIZE=5


############
# API Proxy - Configuration for the Kong Reverse proxy.
############

KONG_HTTP_PORT=8000
KONG_HTTPS_PORT=8443


############
# API - Configuration for PostgREST.
############

PGRST_DB_SCHEMAS=public,storage,graphql_public


############
# Auth - Configuration for the GoTrue authentication server.
############

## General
SITE_URL=${SITE_URL}
ADDITIONAL_REDIRECT_URLS=
JWT_EXPIRY=3600
DISABLE_SIGNUP=${DISABLE_SIGNUP}
API_EXTERNAL_URL=${API_EXTERNAL_URL}

## Mailer Config
MAILER_URLPATHS_CONFIRMATION="/auth/v1/verify"
MAILER_URLPATHS_INVITE="/auth/v1/verify"
MAILER_URLPATHS_RECOVERY="/auth/v1/verify"
MAILER_URLPATHS_EMAIL_CHANGE="/auth/v1/verify"

## Email auth
ENABLE_EMAIL_SIGNUP=true
ENABLE_EMAIL_AUTOCONFIRM=${ENABLE_EMAIL_AUTOCONFIRM}
SMTP_ADMIN_EMAIL=${SMTP_ADMIN_EMAIL}
SMTP_HOST=${SMTP_HOST}
SMTP_PORT=${SMTP_PORT}
SMTP_USER=${SMTP_USER}
SMTP_PASS=${SMTP_PASS}
SMTP_SENDER_NAME=${SMTP_SENDER_NAME}
ENABLE_ANONYMOUS_USERS=false

## Phone auth
ENABLE_PHONE_SIGNUP=false
ENABLE_PHONE_AUTOCONFIRM=false


############
# Studio - Configuration for the Dashboard
############

STUDIO_DEFAULT_ORGANIZATION=Default Organization
STUDIO_DEFAULT_PROJECT=Default Project

SUPABASE_PUBLIC_URL=${SUPABASE_PUBLIC_URL}

# Enable webp support
IMGPROXY_ENABLE_WEBP_DETECTION=true

# Add your OpenAI API key to enable SQL Editor Assistant
OPENAI_API_KEY=


############
# Functions - Configuration for Functions
############
# NOTE: VERIFY_JWT applies to all functions. Per-function VERIFY_JWT is not supported yet.
FUNCTIONS_VERIFY_JWT=false


############
# Logs - Configuration for Analytics
# Please refer to https://supabase.com/docs/reference/self-hosting-analytics/introduction
############

# Change vector.toml sinks to reflect this change
# these cannot be the same value
LOGFLARE_PUBLIC_ACCESS_TOKEN=${LOGFLARE_PUBLIC_ACCESS_TOKEN}
LOGFLARE_PRIVATE_ACCESS_TOKEN=${LOGFLARE_PRIVATE_ACCESS_TOKEN}

# Docker socket location - this value will differ depending on your OS
DOCKER_SOCKET_LOCATION=/var/run/docker.sock

# Google Cloud Project details
GOOGLE_PROJECT_ID=GOOGLE_PROJECT_ID
GOOGLE_PROJECT_NUMBER=GOOGLE_PROJECT_NUMBER

##########
# MQTT
#########

MQTT_HOST=${MQTT_HOST}
MQTT_WEBHOOK_SECRET=${MQTT_WEBHOOK_SECRET}
ENVEOF

success ".env written successfully"

# ═══════════════════════════════════════════════════════════════════════════════
# Summary
# ═══════════════════════════════════════════════════════════════════════════════
echo
echo -e "${BOLD}╔══════════════════════════════════════════════════════════╗${NC}"
echo -e "${BOLD}║               Setup Complete                             ║${NC}"
echo -e "${BOLD}╠══════════════════════════════════════════════════════════╣${NC}"
echo -e "${BOLD}║${NC}                                                          ${BOLD}║${NC}"
echo -e "${BOLD}║${NC}  Supabase API:  ${GREEN}${SUPABASE_PUBLIC_URL}${NC}"
echo -e "${BOLD}║${NC}  Frontend:      ${GREEN}${SITE_URL}${NC}"
echo -e "${BOLD}║${NC}  MQTT Host:     ${GREEN}${MQTT_HOST}${NC}"
echo -e "${BOLD}║${NC}"
echo -e "${BOLD}║${NC}  Dashboard:     ${CYAN}${DASHBOARD_USERNAME}${NC} / ${CYAN}${DASHBOARD_PASSWORD}${NC}"
if [ "$CONFIGURE_SMTP" = true ]; then
echo -e "${BOLD}║${NC}  SMTP:          ${GREEN}Configured${NC} (${SMTP_HOST})"
else
echo -e "${BOLD}║${NC}  SMTP:          ${YELLOW}Not configured${NC} (auto-confirm enabled)"
fi
echo -e "${BOLD}║${NC}"
echo -e "${BOLD}║${NC}  .env written to: ${CYAN}$(pwd)/.env${NC}"
echo -e "${BOLD}║${NC}                                                          ${BOLD}║${NC}"
echo -e "${BOLD}╚══════════════════════════════════════════════════════════╝${NC}"

echo
echo -e "${BOLD}Next steps:${NC}"
echo -e "  1. Set up DNS records:"
echo -e "     ${CYAN}supabase.${DOMAIN}${NC}  →  A record  →  your server IP"
echo -e "     ${CYAN}app.${DOMAIN}${NC}        →  A record  →  your server IP"
echo -e "  2. Configure a reverse proxy (Caddy or Nginx) — see PROD.md"
if [ "$CONFIGURE_SMTP" = false ]; then
echo -e "  3. ${YELLOW}Configure SMTP in .env before going to production${NC}"
fi

# ─── Optional: Start the stack ────────────────────────────────────────────────
STACK_STARTED=false
echo
if prompt_yes_no "Start the stack now with 'docker compose up -d'?" "n"; then
    info "Starting services..."
    docker compose up -d

    info "Waiting for database to be ready..."
    for i in $(seq 1 12); do
        if docker compose exec -T db pg_isready -U postgres >/dev/null 2>&1; then
            success "Database is ready"
            STACK_STARTED=true
            break
        fi
        sleep 5
    done

    if [ "$STACK_STARTED" = false ]; then
        warn "Database did not become ready within 60 seconds. Check logs with: docker compose logs db"
    else
        echo
        docker compose ps
    fi
fi

# ─── Optional: Apply database migrations ─────────────────────────────────────
if [ "$STACK_STARTED" = true ]; then
    MIGRATION_COUNT=$(find supabase/migrations -name '*.sql' 2>/dev/null | wc -l | tr -d ' ')
    if [ "$MIGRATION_COUNT" -gt 0 ]; then
        echo
        info "Found ${MIGRATION_COUNT} database migration files."
        if prompt_yes_no "Apply database migrations now?" "y"; then
            info "Applying migrations..."
            for f in supabase/migrations/*.sql; do
                fname=$(basename "$f")
                info "  ${fname}..."
                if docker compose exec -T db psql -U postgres -d postgres < "$f" > /dev/null 2>&1; then
                    success "  ${fname}"
                else
                    warn "  ${fname} — may have already been applied or had errors"
                fi
            done
            echo
            success "Migration process complete."
        fi
    fi
fi

echo
info "Done! See PROD.md for reverse proxy setup, MQTT hardening, and backups."
