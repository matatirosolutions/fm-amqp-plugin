#!/usr/bin/env bash
#
# build-linux-docker.sh
#
# Builds AMQPFMPlugin.fmx inside Docker for Ubuntu 22.04 and/or 24.04.
# Works on macOS (Apple Silicon or Intel) and Linux hosts.
#
# Usage:
#   ./build-linux-docker.sh           # build for both U22 and U24, x64
#   ./build-linux-docker.sh u22       # Ubuntu 22.04 only
#   ./build-linux-docker.sh u24       # Ubuntu 24.04 only
#   ./build-linux-docker.sh u22 arm64 # Ubuntu 22.04, arm64
#
# Output:
#   build/linux/U22/x64/AMQPFMPlugin.fmx
#   build/linux/U24/x64/AMQPFMPlugin.fmx
#
# Prerequisites:
#   Docker Desktop (https://www.docker.com/products/docker-desktop)
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
DOCKERFILE="${SCRIPT_DIR}/docker/Dockerfile.ubuntu"

# ── Parse arguments ──────────────────────────────────────────────────────────

TARGET="${1:-both}"
ARCH="${2:-x64}"

case "$ARCH" in
    x64|amd64) DOCKER_PLATFORM="linux/amd64" ;;
    arm64)     DOCKER_PLATFORM="linux/arm64" ;;
    *)
        echo "ERROR: Unknown architecture '${ARCH}'. Use x64 or arm64."
        exit 1
        ;;
esac

# ── Check Docker is running ──────────────────────────────────────────────────

if ! docker info &>/dev/null; then
    echo "ERROR: Docker is not running. Start Docker Desktop and try again."
    exit 1
fi

# ── Build function ───────────────────────────────────────────────────────────

build_for_ubuntu() {
    local UBUNTU_VERSION=$1   # e.g. "22.04"
    local U_PLATFORM=$2       # e.g. "U22"
    local IMAGE_TAG="amqp-fm-plugin-builder:ubuntu${UBUNTU_VERSION}-${ARCH}"

    echo ""
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    echo "  Building for Ubuntu ${UBUNTU_VERSION} (${U_PLATFORM}) / ${ARCH}"
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

    # Build the Docker image (cached after first run)
    echo "==> Building Docker image ${IMAGE_TAG}"
    docker build \
        --platform "${DOCKER_PLATFORM}" \
        --build-arg "UBUNTU_VERSION=${UBUNTU_VERSION}" \
        -t "${IMAGE_TAG}" \
        -f "${DOCKERFILE}" \
        "${SCRIPT_DIR}"

    # Run the build inside the container.
    # The project root is mounted read-write so:
    #   - build-openssl-linux.sh writes to third_party/ (persisted for next run)
    #   - build-linux.sh writes to build/linux/U22|U24/
    echo "==> Running build inside container"
    docker run --rm \
        --platform "${DOCKER_PLATFORM}" \
        -v "${SCRIPT_DIR}:/plugin" \
        "${IMAGE_TAG}" \
        bash -c "
            set -e
            cd /plugin

            # Build static OpenSSL if not already present for this platform
            OPENSSL_LIB=\"third_party/openssl/linux/${U_PLATFORM}/${ARCH}/lib/libssl.a\"
            if [ ! -f \"\$OPENSSL_LIB\" ]; then
                echo '==> OpenSSL not found, building...'
                bash scripts/build-openssl-linux.sh
            else
                echo '==> OpenSSL already built, skipping'
            fi

            bash scripts/build-linux.sh
        "

    local OUTPUT="${SCRIPT_DIR}/build/linux/${U_PLATFORM}/${ARCH}/AMQPFMPlugin.fmx"
    if [ -f "$OUTPUT" ]; then
        echo ""
        echo "✓ ${U_PLATFORM}/${ARCH}: ${OUTPUT}"
    else
        echo "ERROR: Expected output not found: ${OUTPUT}"
        exit 1
    fi
}

# ── Run ──────────────────────────────────────────────────────────────────────

case "$TARGET" in
    u22|U22) build_for_ubuntu "22.04" "U22" ;;
    u24|U24) build_for_ubuntu "24.04" "U24" ;;
    both)
        build_for_ubuntu "22.04" "U22"
        build_for_ubuntu "24.04" "U24"
        ;;
    *)
        echo "ERROR: Unknown target '${TARGET}'. Use u22, u24, or both."
        exit 1
        ;;
esac

echo ""
echo "All done. Built files:"
find "${SCRIPT_DIR}/build/linux" -name "*.fmx" 2>/dev/null | sort | while read -r f; do
    echo "  $f"
done
