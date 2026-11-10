#!/bin/sh
# Generate fresh secrets into an .env file (in place). Prints nothing secret.
# Usage: gen-secrets.sh path/to/.env
set -eu
ENV_FILE=${1:-.env}
command -v openssl >/dev/null || { echo "openssl required" >&2; exit 1; }

b64url() { openssl enc -base64 -A | tr '+/' '-_' | tr -d '='; }
jwt() {
  h=$(printf '%s' '{"alg":"HS256","typ":"JWT"}' | b64url)
  p=$(printf '%s' "$1" | b64url)
  s=$(printf '%s' "$h.$p" | openssl dgst -binary -sha256 -hmac "$JWT_SECRET" | b64url)
  printf '%s.%s.%s' "$h" "$p" "$s"
}
set_var() { # replace or append KEY=VALUE
  if grep -q "^$1=" "$ENV_FILE"; then
    sed -i "s|^$1=.*|$1=$2|" "$ENV_FILE"
  else
    printf '%s=%s\n' "$1" "$2" >> "$ENV_FILE"
  fi
}

JWT_SECRET=$(openssl rand -hex 32)
iat=$(date +%s); exp=$((iat + 5*365*24*3600))

set_var JWT_SECRET "$JWT_SECRET"
set_var ANON_KEY "$(jwt "{\"role\":\"anon\",\"iss\":\"supabase\",\"iat\":$iat,\"exp\":$exp}")"
set_var SERVICE_ROLE_KEY "$(jwt "{\"role\":\"service_role\",\"iss\":\"supabase\",\"iat\":$iat,\"exp\":$exp}")"
set_var SECRET_KEY_BASE "$(openssl rand -hex 48)"
set_var VAULT_ENC_KEY "$(openssl rand -hex 16)"
set_var PG_META_CRYPTO_KEY "$(openssl rand -hex 24)"
set_var LOGFLARE_PUBLIC_ACCESS_TOKEN "$(openssl rand -hex 24)"
set_var LOGFLARE_PRIVATE_ACCESS_TOKEN "$(openssl rand -hex 24)"
set_var POSTGRES_PASSWORD "$(openssl rand -hex 16)"
set_var DASHBOARD_PASSWORD "$(openssl rand -hex 16)"
echo "Secrets written to $ENV_FILE"
