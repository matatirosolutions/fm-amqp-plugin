#!/usr/bin/env bash
#
# build-linux.sh
#
# Builds AMQPFMPlugin.fmx for FileMaker Server on Ubuntu 22.04 or 24.04 LTS
# (x64 or arm64).
#
# Prerequisites (run once):
#   sudo apt-get install -y build-essential cmake git perl
#
# OpenSSL (run once before first build):
#   ./build-openssl-linux.sh
#
# Output:   build/linux/AMQPFMPlugin.fmx
#
# Install (FileMaker Server):
#   sudo cp build/linux/AMQPFMPlugin.fmx \
#     "/opt/FileMaker/FileMaker Server/Database Server/Extensions/"
#   sudo fmsadmin restart fmse
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
# BUILD_DIR is set after platform detection so U22/U24 don't clobber each other

# ── Prerequisites check ──────────────────────────────────────────────────────

for cmd in cmake g++ git; do
    if ! command -v "$cmd" &>/dev/null; then
        echo "ERROR: '$cmd' not found."
        echo "  sudo apt-get install -y build-essential cmake git"
        exit 1
    fi
done

# ── Detect platform ──────────────────────────────────────────────────────────

UBUNTU_MAJOR=$(lsb_release -rs 2>/dev/null | cut -d. -f1)
MACHINE=$(uname -m)
case "$MACHINE" in
    x86_64)  ARCH="x64"   ;;
    aarch64) ARCH="arm64" ;;
    *)       echo "ERROR: Unsupported architecture: ${MACHINE}"; exit 1 ;;
esac

case "$UBUNTU_MAJOR" in
    22) U_PLATFORM="U22" ;;
    24) U_PLATFORM="U24" ;;
    *)  U_PLATFORM="U22"
        echo "WARNING: Ubuntu ${UBUNTU_MAJOR} not explicitly supported, using U22 SDK" ;;
esac

BUILD_DIR="${SCRIPT_DIR}/build/linux/${U_PLATFORM}/${ARCH}"

# ── SDK check ────────────────────────────────────────────────────────────────

SDK_LIB="${SCRIPT_DIR}/sdk/Libraries/Linux/${U_PLATFORM}/${ARCH}/libFMWrapper.so"
if [ ! -f "$SDK_LIB" ]; then
    echo "ERROR: FileMaker SDK not found at ${SDK_LIB}"
    echo "Download the FileMaker Plugin SDK and place it in sdk/"
    exit 1
fi

# ── OpenSSL check ────────────────────────────────────────────────────────────

OPENSSL_LIB="${SCRIPT_DIR}/third_party/openssl/linux/${U_PLATFORM}/${ARCH}/lib/libssl.a"
if [ ! -f "$OPENSSL_LIB" ]; then
    echo "WARNING: Bundled static OpenSSL not found at ${OPENSSL_LIB}"
    echo "  Run ./build-openssl-linux.sh first to enable TLS support."
    echo "  Building without SSL..."
fi

# ── Configure ────────────────────────────────────────────────────────────────

echo "==> Configuring (Ubuntu ${UBUNTU_MAJOR} / ${U_PLATFORM}, ${ARCH})"
cmake -S "${SCRIPT_DIR}" -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_COMPILER=g++ \
    -DCMAKE_C_COMPILER=gcc

# ── Build ────────────────────────────────────────────────────────────────────

echo "==> Building"
cmake --build "${BUILD_DIR}" -- -j"$(nproc)"

echo ""
echo "Done: ${BUILD_DIR}/AMQPFMPlugin.fmx"
echo ""
echo "To install on FileMaker Server:"
echo "  sudo cp \"${BUILD_DIR}/AMQPFMPlugin.fmx\" \\"
echo "    \"/opt/FileMaker/FileMaker Server/Database Server/Extensions/\""
echo "  sudo systemctl restart fmserver  # or: sudo fmsadmin restart fmse"
