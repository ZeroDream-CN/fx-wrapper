#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SOURCE="$ROOT/test-resources/fx-sandbox-test"
TARGETS=(
  "$ROOT/txData/FiveMBasicServerCFXDefault_60318B.base/resources/fx-sandbox-test"
  "$ROOT/server-linux/txData/FiveMBasicServerCFXDefault_60318B.base/resources/fx-sandbox-test"
)

for target in "${TARGETS[@]}"; do
  rm -rf "$target"
  cp -a "$SOURCE" "$target"
  echo "Synced sandbox test resource to $target"
done
