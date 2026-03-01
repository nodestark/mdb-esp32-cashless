#!/usr/bin/env bash
set -euo pipefail

# ═══════════════════════════════════════════════════════════════════════════════
# VMflow Update Script
# Pulls latest code, applies new migrations, rebuilds & restarts services.
# Run from the Docker/ directory.
# ═══════════════════════════════════════════════════════════════════════════════

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

# ── Colors ────────────────────────────────────────────────────────────────────
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
BOLD='\033[1m'
DIM='\033[2m'
NC='\033[0m'

info()    { echo -e "  ${CYAN}ℹ${NC}  $1"; }
success() { echo -e "  ${GREEN}✔${NC}  $1"; }
warn()    { echo -e "  ${YELLOW}⚠${NC}  $1"; }
error()   { echo -e "  ${RED}✖${NC}  $1"; exit 1; }
step()    { echo; echo -e "${BOLD}═══ $1 ═══${NC}"; echo; }

# ═══════════════════════════════════════════════════════════════════════════════
# Pre-flight checks
# ═══════════════════════════════════════════════════════════════════════════════

[ -f .env ] || error ".env not found. Run setup.sh first."
docker compose ps --services >/dev/null 2>&1 || error "Docker stack is not running. Start it with: docker compose up -d"

# ═══════════════════════════════════════════════════════════════════════════════
# Step 1: Pull latest code
# ═══════════════════════════════════════════════════════════════════════════════
step "1/4 — Pulling Latest Code"

cd "$SCRIPT_DIR/.."

BEFORE=$(git rev-parse HEAD)
git pull --ff-only
AFTER=$(git rev-parse HEAD)

if [ "$BEFORE" = "$AFTER" ]; then
    info "Already up to date (${BEFORE:0:7})"
else
    success "Updated: ${BEFORE:0:7} → ${AFTER:0:7}"
    echo
    git --no-pager log --oneline "${BEFORE}..${AFTER}"
fi

cd "$SCRIPT_DIR"

# ═══════════════════════════════════════════════════════════════════════════════
# Step 2: Apply new database migrations
# ═══════════════════════════════════════════════════════════════════════════════
step "2/4 — Applying New Migrations"

MIGRATION_DIR="supabase/migrations"
APPLIED=0
SKIPPED=0
FAILED=0

# Check if the _migrations tracking table exists
HAS_TRACKING=$(docker compose exec -T db psql -U postgres -d postgres -tAc \
    "SELECT 1 FROM information_schema.tables WHERE table_schema='public' AND table_name='_migrations'" 2>/dev/null || echo "")

if [ "$HAS_TRACKING" = "1" ]; then
    # Use tracking table: only apply migrations not yet recorded
    for f in "$MIGRATION_DIR"/*.sql; do
        fname=$(basename "$f")

        ALREADY=$(docker compose exec -T db psql -U postgres -d postgres -tAc \
            "SELECT 1 FROM public._migrations WHERE name = '${fname}'" 2>/dev/null || echo "")

        if [ "$ALREADY" = "1" ]; then
            SKIPPED=$((SKIPPED + 1))
            continue
        fi

        echo -ne "  Applying ${fname}... "
        if docker compose exec -T db psql -U postgres -d postgres < "$f" > /tmp/migration_out.txt 2>&1; then
            # Record as applied
            docker compose exec -T db psql -U postgres -d postgres -c \
                "INSERT INTO public._migrations (name) VALUES ('${fname}') ON CONFLICT DO NOTHING" >/dev/null 2>&1
            echo -e "${GREEN}✔${NC}"
            APPLIED=$((APPLIED + 1))
        else
            echo -e "${RED}✖${NC}"
            echo -e "  ${DIM}$(cat /tmp/migration_out.txt | head -5)${NC}"
            FAILED=$((FAILED + 1))
        fi
    done
else
    # No tracking table yet — fall back to apply-all (setup.sh style)
    warn "Migration tracking table not found. Applying all migrations (errors = already applied)."
    echo
    for f in "$MIGRATION_DIR"/*.sql; do
        fname=$(basename "$f")
        if docker compose exec -T db psql -U postgres -d postgres < "$f" > /dev/null 2>&1; then
            success "$fname"
            APPLIED=$((APPLIED + 1))
        else
            echo -e "  ${YELLOW}⊘${NC} ${fname} ${DIM}(already applied or had errors)${NC}"
            SKIPPED=$((SKIPPED + 1))
        fi
    done
fi

echo
if [ "$FAILED" -gt 0 ]; then
    warn "Migrations: ${APPLIED} applied, ${SKIPPED} skipped, ${RED}${FAILED} failed${NC}"
else
    success "Migrations: ${APPLIED} applied, ${SKIPPED} skipped"
fi

# ═══════════════════════════════════════════════════════════════════════════════
# Step 3: Rebuild & restart services
# ═══════════════════════════════════════════════════════════════════════════════
step "3/4 — Rebuilding Services"

# Rebuild frontend (and any other build-from-source services)
info "Building frontend image..."
docker compose build --no-cache frontend
success "Frontend image built"

# Rebuild forwarder (MQTT bridge) in case it changed
info "Building forwarder image..."
docker compose build forwarder
success "Forwarder image built"

# Restart services that use the new images
info "Restarting updated services..."
docker compose up -d --no-deps frontend forwarder

# Restart edge functions (picks up new/changed function code from volume mount)
info "Restarting edge functions..."
docker compose restart functions

success "All services restarted"

# ═══════════════════════════════════════════════════════════════════════════════
# Step 4: Health check
# ═══════════════════════════════════════════════════════════════════════════════
step "4/4 — Health Check"

sleep 3

# Check frontend
if docker compose ps frontend | grep -q "Up\|running"; then
    success "Frontend is running"
else
    warn "Frontend may not be healthy — check: docker compose logs frontend"
fi

# Check functions
if docker compose ps functions | grep -q "Up\|running"; then
    success "Edge functions are running"
else
    warn "Edge functions may not be healthy — check: docker compose logs functions"
fi

echo
docker compose ps
echo
success "Update complete!"
