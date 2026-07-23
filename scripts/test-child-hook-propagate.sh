#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CFX="$ROOT/server-linux/alpine/opt/cfx-server"
LOG="/tmp/fx-child-hook-test.log"

cp -f "$ROOT/dist/linux-musl-amd64/libfx-hook.so" "$ROOT/dist/linux-musl-amd64/FXWrapper" "$CFX/"
bash "$ROOT/scripts/sync-sandbox-test-resource.sh"

export FX_HOOK_DEBUG=1
export TXHOST_TXA_PORT=40121
export TXHOST_FXS_PORT=30121
export TXHOST_DATA_PATH="$ROOT/server-linux/txData"

pkill -f '/FXServer|FXWrapper' 2>/dev/null || true
sleep 2

# Start txAdmin host only; txAdmin spawns the game FXServer child.
bash "$ROOT/server-linux/run.sh" \
  +set sv_endpointPrivacy true \
  +endpoint_add_tcp "0.0.0.0:30121" \
  +endpoint_add_udp "0.0.0.0:30121" >"$LOG" 2>&1 &
server_pid=$!

for ((i = 0; i < 90; i++)); do
  sleep 1
  if [[ -f "$LOG" ]] && grep -q 'Hook 安装成功' "$LOG"; then
    hook_count="$(grep -c 'Hook 安装成功' "$LOG" || true)"
    if [[ "$hook_count" -ge 2 ]]; then
      break
    fi
  fi
  if [[ -f "$LOG" ]] && grep -q '\[fx-sandbox-test\] ALL:PASS' "$LOG"; then
    break
  fi
  if ! kill -0 "$server_pid" 2>/dev/null; then
    break
  fi
done

kill "$server_pid" 2>/dev/null || true
pkill -f '/FXServer|FXWrapper' 2>/dev/null || true

echo "=== hook success count ==="
grep -c 'Hook 安装成功' "$LOG" || true

echo "=== propagate count ==="
grep -c 'Propagating hook library' "$LOG" || true

echo "=== spawn hook count ==="
grep -c 'Process spawn hook installed' "$LOG" || true

echo "=== sandbox on child ==="
grep '\[fx-sandbox-test\]' "$LOG" | tail -10 || true

echo "=== key lines ==="
grep -E 'Propagating|Hook 安装成功|FXServer Starting|Process spawn hook' "$LOG" || true

hook_count="$(grep -c 'Hook 安装成功' "$LOG" || true)"
spawn_hook_count="$(grep -c 'Process spawn hook installed' "$LOG" || true)"
if [[ "$hook_count" -lt 2 || "$spawn_hook_count" -lt 2 ]]; then
  exit 1
fi
