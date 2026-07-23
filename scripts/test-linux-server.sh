#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
LOG="/tmp/fx-linux-test.log"
SERVER_CFG="$ROOT/test-resources/test-server.cfg"
CFX_SERVER="$ROOT/server-linux/alpine/opt/cfx-server"
SYSTEM_RESOURCE="$CFX_SERVER/citizen/system_resources/fx-sandbox-test"
TIMEOUT_SEC=90

bash "$ROOT/scripts/sync-sandbox-test-resource.sh"

bash "$ROOT/scripts/build-linux-musl.sh"
cp -f "$ROOT/dist/linux-musl-amd64/libfx-hook.so" "$ROOT/dist/linux-musl-amd64/FXWrapper" "$CFX_SERVER/"
cp -f "$ROOT/test-resources/test-server.cfg" "$CFX_SERVER/test-server.cfg"

rm -rf "$SYSTEM_RESOURCE"
cp -a "$ROOT/test-resources/fx-sandbox-test" "$SYSTEM_RESOURCE"

pkill -f '/FXServer|FXWrapper' 2>/dev/null || true
for port in 30121 40121; do
  fuser -k "${port}/tcp" 2>/dev/null || true
done
sleep 2

rm -f "$LOG"
cfg_args=(+set sv_endpointPrivacy true +endpoint_add_tcp 0.0.0.0:30121 +endpoint_add_udp 0.0.0.0:30121 +exec "$CFX_SERVER/test-server.cfg")

export TXHOST_TXA_PORT=40121
export TXHOST_FXS_PORT=30121

bash "$ROOT/server-linux/run.sh" "${cfg_args[@]}" >"$LOG" 2>&1 &
server_pid=$!

for ((i = 0; i < TIMEOUT_SEC; i++)); do
  sleep 1
  if [[ -f "$LOG" ]] && grep -qE '\[fx-sandbox-test\] (ALL|LUA_ALL):(PASS|FAIL)' "$LOG"; then
    if grep -q '\[fx-sandbox-test\] ALL:PASS' "$LOG" && grep -q '\[fx-sandbox-test\] LUA_ALL:PASS' "$LOG"; then
      break
    fi
    if grep -q '\[fx-sandbox-test\] ALL:FAIL' "$LOG" || grep -q '\[fx-sandbox-test\] LUA_ALL:FAIL' "$LOG"; then
      break
    fi
  fi
  if ! kill -0 "$server_pid" 2>/dev/null; then
    break
  fi
done

kill "$server_pid" 2>/dev/null || true
pkill -P "$server_pid" 2>/dev/null || true
pkill -f '/FXServer|FXWrapper' 2>/dev/null || true
wait "$server_pid" 2>/dev/null || true

echo '=== hook / wrapper ==='
grep -E 'fx-wrapper|Hook 安装成功|NodePermission' "$LOG" || true

echo '=== sandbox test ==='
grep -E '\[fx-sandbox-test\]' "$LOG" || true

hook_ok=0
sandbox_ok=0
grep -q 'Hook 安装成功' "$LOG" && hook_ok=1
sandbox_ok=0
if grep -q '\[fx-sandbox-test\] ALL:PASS' "$LOG" && grep -q '\[fx-sandbox-test\] LUA_ALL:PASS' "$LOG"; then
  sandbox_ok=1
fi
crash_count="$(grep -c SIGSEGV "$LOG" || true)"

echo "=== result: hook=$hook_ok sandbox=$sandbox_ok crash=$crash_count ==="
if [[ "$hook_ok" -ne 1 || "$sandbox_ok" -ne 1 || "$crash_count" -ne 0 ]]; then
  exit 1
fi
