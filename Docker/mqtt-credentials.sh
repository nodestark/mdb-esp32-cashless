#!/usr/bin/env bash
set -euo pipefail

# ─── MQTT Credential Generator ───────────────────────────────────────────────
# Generates (or rotates) the MQTT admin password used by the forwarder,
# edge functions, and any debugging client.
#
# Device credentials are hardcoded: vmflow / vmflow
#
# Updates:
#   Docker/.env              → prod (docker compose reads this)
#   Docker/supabase/.env     → dev  (supabase CLI reads this via config.toml env())
#   Docker/mqtt/config/passwd → mosquitto broker password hashes
#
# Usage:
#   ./mqtt-credentials.sh              # auto-generate a random password
#   ./mqtt-credentials.sh mypassword   # use a specific password
# ──────────────────────────────────────────────────────────────────────────────

RED='\033[0;31m'
GREEN='\033[0;32m'
BLUE='\033[0;34m'
BOLD='\033[1m'
NC='\033[0m'

info()    { echo -e "${BLUE}[INFO]${NC} $*"; }
error()   { echo -e "${RED}[ERROR]${NC} $*" >&2; exit 1; }
success() { echo -e "${GREEN}  ✔${NC} $*"; }

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DOCKER_ENV="${SCRIPT_DIR}/.env"
SUPABASE_ENV="${SCRIPT_DIR}/supabase/.env"
PASSWD_FILE="${SCRIPT_DIR}/mqtt/config/passwd"

# Portable sed in-place (macOS uses -i '', GNU/Linux uses -i)
sedi() {
    if [[ "$OSTYPE" == darwin* ]]; then
        sed -i '' "$@"
    else
        sed -i "$@"
    fi
}

# ─── Determine admin password ────────────────────────────────────────────────
if [ -n "${1:-}" ]; then
    MQTT_ADMIN_PASS="$1"
    info "Using provided password"
else
    MQTT_ADMIN_PASS=$(openssl rand -base64 16 | tr -d '\n')
    info "Generated random password"
fi

# ─── Helper: upsert a KEY=VALUE in a file ────────────────────────────────────
# If KEY exists, replace the line. If not, append it.
upsert_env() {
    local file="$1" key="$2" value="$3"
    if grep -q "^${key}=" "$file" 2>/dev/null; then
        sedi "s|^${key}=.*|${key}=${value}|" "$file"
    else
        echo "${key}=${value}" >> "$file"
    fi
}

# ─── Update Docker/.env (prod: docker compose) ──────────────────────────────
if [ ! -f "$DOCKER_ENV" ]; then
    error ".env file not found at ${DOCKER_ENV}. Run setup.sh first."
fi

# Clean up legacy multi-user vars if present
sedi '/^MQTT_DEVICE_USER=/d;
      /^MQTT_DEVICE_PASS=/d;
      /^MQTT_FORWARDER_USER=/d;
      /^MQTT_FORWARDER_PASS=/d;
      /^MQTT_BACKEND_USER=/d;
      /^MQTT_BACKEND_PASS=/d' "$DOCKER_ENV"

upsert_env "$DOCKER_ENV" "MQTT_ADMIN_USER" "admin"
upsert_env "$DOCKER_ENV" "MQTT_ADMIN_PASS" "$MQTT_ADMIN_PASS"

success "Docker/.env updated"

# ─── Update Docker/supabase/.env (dev: supabase CLI) ────────────────────────
if [ -f "$SUPABASE_ENV" ]; then
    # Clean up legacy vars if present
    sedi '/^MQTT_DEVICE_USER=/d;
          /^MQTT_DEVICE_PASS=/d;
          /^MQTT_FORWARDER_USER=/d;
          /^MQTT_FORWARDER_PASS=/d;
          /^MQTT_BACKEND_USER=/d;
          /^MQTT_BACKEND_PASS=/d' "$SUPABASE_ENV"

    upsert_env "$SUPABASE_ENV" "MQTT_ADMIN_USER" "admin"
    upsert_env "$SUPABASE_ENV" "MQTT_ADMIN_PASS" "$MQTT_ADMIN_PASS"

    # Sync MQTT_WEBHOOK_SECRET from Docker/.env if missing here
    if ! grep -q '^MQTT_WEBHOOK_SECRET=' "$SUPABASE_ENV" 2>/dev/null; then
        webhook_secret=$(grep '^MQTT_WEBHOOK_SECRET=' "$DOCKER_ENV" | cut -d= -f2- || true)
        if [ -n "${webhook_secret:-}" ]; then
            echo "MQTT_WEBHOOK_SECRET=${webhook_secret}" >> "$SUPABASE_ENV"
        fi
    fi

    success "Docker/supabase/.env updated"
else
    info "Docker/supabase/.env not found — skipping (not needed for production)"
fi

# ─── Regenerate mosquitto passwd file ────────────────────────────────────────
rm -f "$PASSWD_FILE"
touch "$PASSWD_FILE"

docker run --rm -v "${SCRIPT_DIR}/mqtt/config:/mosquitto/config" eclipse-mosquitto:2.1.2-alpine \
  sh -c "
    mosquitto_passwd -b /mosquitto/config/passwd vmflow 'vmflow' && \
    mosquitto_passwd -b /mosquitto/config/passwd admin '${MQTT_ADMIN_PASS}'
  " 2>/dev/null

chmod 600 "$PASSWD_FILE"
success "Mosquitto passwd file regenerated"

# ─── Restart running containers if docker compose is active ─────────────────
if docker compose -f "${SCRIPT_DIR}/docker-compose.yml" ps --status running 2>/dev/null | grep -q 'broker\|forwarder'; then
    info "Restarting broker + forwarder to apply new credentials..."
    docker compose -f "${SCRIPT_DIR}/docker-compose.yml" up -d --force-recreate broker forwarder
    success "broker + forwarder restarted"
else
    info "No running broker/forwarder containers detected — skipping restart"
fi

# ─── Summary ─────────────────────────────────────────────────────────────────
echo ""
echo -e "${BOLD}MQTT Credentials${NC}"
echo -e "  Device:  vmflow / vmflow  (hardcoded in firmware)"
echo -e "  Admin:   admin / ${MQTT_ADMIN_PASS}"
echo ""
echo -e "${BOLD}Files updated:${NC}"
echo -e "  ${DOCKER_ENV}"
[ -f "$SUPABASE_ENV" ] && echo -e "  ${SUPABASE_ENV}"
echo -e "  ${PASSWD_FILE}"
echo ""
echo -e "${BOLD}If using local dev (supabase CLI), also restart:${NC}"
echo "  cd Docker/supabase && supabase stop && supabase start"
