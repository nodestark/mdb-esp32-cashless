#!/usr/bin/env bash
#
# rpc.sh — send a signed RPC command to a VMflow mdb-slave device over MQTT.
#
# Envelope: "<cmd>:<ts>:<hmac>"  where hmac = HMAC-SHA256(passkey, "<cmd>:<ts>")
# Topic:    <sub>.vmflow.xyz/rpc        Reply: domain.vmflow.xyz/<sub>/rpc/<cmd>
#
# Usage:
#   SUB=51 PASSKEY=af6c51a556fd71b345 ./rpc.sh info
#   ./rpc.sh -s 51 -k af6c51a556fd71b345 dex
#   ./rpc.sh -s 51 -k <key> -w info        # -w: also wait for the reply
#
# Commands: dex info oos buzzer echo restart
#
# Broker auth/TLS: pass through extra mosquitto flags after `--`, e.g.
#   ./rpc.sh -s 51 -k <key> info -- -u user -P pass
#   ./rpc.sh -s 51 -k <key> info -- -p 8883 --cafile ca.pem

set -euo pipefail

SUB="${SUB:-}"
PASSKEY="${PASSKEY:-}"
HOST="${HOST:-}"
WAIT=0

usage() { sed -n '2,/^set -euo/p' "$0" | sed 's/^# \{0,1\}//; s/^#//'; exit "${1:-0}"; }

while getopts ":s:k:H:wh" opt; do
  case "$opt" in
    s) SUB="$OPTARG" ;;
    k) PASSKEY="$OPTARG" ;;
    H) HOST="$OPTARG" ;;
    w) WAIT=1 ;;
    h) usage 0 ;;
    *) usage 1 ;;
  esac
done
shift $((OPTIND - 1))

CMD="${1:-}"
[ -n "$CMD" ] || { echo "error: missing command" >&2; usage 1; }
shift || true

# Anything after the command (or after `--`) is passed straight to mosquitto.
EXTRA=("$@")

[ -n "$SUB" ]     || { echo "error: set SUB or pass -s <subdomain>" >&2; exit 1; }
[ -n "$PASSKEY" ] || { echo "error: set PASSKEY or pass -k <passkey>" >&2; exit 1; }
HOST="${HOST:-$SUB.vmflow.xyz}"

TS="$(date +%s)"                 # UTC epoch — fresh (device enforces a 10s window)
MSG="$CMD:$TS"
SIG="$(printf '%s' "$MSG" | openssl dgst -sha256 -hmac "$PASSKEY" -hex | awk '{print $NF}')"
PAYLOAD="$MSG:$SIG"

REPLY_TOPIC="domain.vmflow.xyz/$SUB/rpc/$CMD"

if [ "$WAIT" -eq 1 ]; then
  # Subscribe to the reply first so we don't miss it, then publish.
  mosquitto_sub -h "$HOST" -t "$REPLY_TOPIC" -C 1 -W 15 "${EXTRA[@]}" &
  SUB_PID=$!
  sleep 0.3
fi

echo "-> $HOST  $SUB.vmflow.xyz/rpc  '$PAYLOAD'" >&2
mosquitto_pub -h "$HOST" -t "$SUB.vmflow.xyz/rpc" -m "$PAYLOAD" -q 1 "${EXTRA[@]}"

if [ "$WAIT" -eq 1 ]; then
  echo "<- waiting on $REPLY_TOPIC (15s)" >&2
  wait "$SUB_PID"
fi
