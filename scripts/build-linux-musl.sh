#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT}/build-linux-musl"
DIST_DIR="${ROOT}/dist/linux-musl-amd64"
IMAGE="fx-wrapper-musl-builder:3.20"
DOCKERFILE="${ROOT}/scripts/Dockerfile.musl"

if command -v podman >/dev/null 2>&1; then
    CONTAINER=podman
elif command -v docker >/dev/null 2>&1; then
    CONTAINER=docker
else
    echo "Neither podman nor docker found in PATH." >&2
    exit 1
fi

PROXY_ARGS=()
BUILD_ARGS=(--network host)
for var in HTTP_PROXY HTTPS_PROXY http_proxy https_proxy NO_PROXY no_proxy; do
    if [ -n "${!var:-}" ]; then
        PROXY_ARGS+=(-e "${var}=${!var}")
        if [ "${var}" != "NO_PROXY" ] && [ "${var}" != "no_proxy" ]; then
            BUILD_ARGS+=(--build-arg "${var}=${!var}")
        fi
    fi
done

if ! "${CONTAINER}" image exists "${IMAGE}" >/dev/null 2>&1; then
    echo "Building builder image ${IMAGE} (one-time, includes apk packages)..."
    "${CONTAINER}" build "${BUILD_ARGS[@]}" -t "${IMAGE}" -f "${DOCKERFILE}" "${ROOT}/scripts"
fi

"${CONTAINER}" run --rm --network host \
    "${PROXY_ARGS[@]}" \
    -e GIT_TERMINAL_PROMPT=0 \
    -v "${ROOT}:/src:Z" \
    -w /src \
    "${IMAGE}" \
    sh -ec '
        cmake -S . -B build-linux-musl -DCMAKE_BUILD_TYPE=Release
        cmake --build build-linux-musl -j"$(nproc)"
        mkdir -p dist/linux-musl-amd64
        cp -f build-linux-musl/FXWrapper build-linux-musl/libfx-hook.so dist/linux-musl-amd64/
    '

echo ""
echo "Deployment bundle (copy entire directory to alpine/opt/cfx-server/):"
echo "  ${DIST_DIR}/"
ls -lh "${DIST_DIR}"
