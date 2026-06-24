#!/usr/bin/env bash
#
# build-openssl-linux.sh
#
# Builds a static OpenSSL for the current Ubuntu version and architecture,
# placing the output in third_party/openssl/linux/<U22|U24>/<x64|arm64>/.
#
# Run once per target platform/arch combination.
#
# Prerequisites:
#   sudo apt-get install -y build-essential perl
#
set -euo pipefail

OPENSSL_VERSION="3.3.2"
OPENSSL_SRC="openssl-${OPENSSL_VERSION}"
OPENSSL_TAR="${OPENSSL_SRC}.tar.gz"
OPENSSL_URL="https://www.openssl.org/source/${OPENSSL_TAR}"

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="/tmp/openssl-amqp-linux"

# ── Detect platform ──────────────────────────────────────────────────────────

UBUNTU_MAJOR=$(lsb_release -rs | cut -d. -f1)
case "$UBUNTU_MAJOR" in
    22) U_PLATFORM="U22" ;;
    24) U_PLATFORM="U24" ;;
    *)
        echo "WARNING: Unrecognised Ubuntu version ${UBUNTU_MAJOR}, treating as U22"
        U_PLATFORM="U22"
        ;;
esac

MACHINE=$(uname -m)
case "$MACHINE" in
    x86_64)  ARCH="x64"   ; OPENSSL_TARGET="linux-x86_64"  ;;
    aarch64) ARCH="arm64" ; OPENSSL_TARGET="linux-aarch64" ;;
    *)
        echo "ERROR: Unsupported architecture: ${MACHINE}"
        exit 1
        ;;
esac

OUT_DIR="${REPO_ROOT}/third_party/openssl/linux/${U_PLATFORM}/${ARCH}"
echo "==> Platform: Ubuntu ${UBUNTU_MAJOR} (${U_PLATFORM}), arch: ${ARCH}"
echo "==> Output:   ${OUT_DIR}"

# ── Prerequisites check ──────────────────────────────────────────────────────

for cmd in perl make gcc; do
    if ! command -v "$cmd" &>/dev/null; then
        echo "ERROR: '$cmd' not found."
        echo "  sudo apt-get install -y build-essential perl"
        exit 1
    fi
done

# ── Download ─────────────────────────────────────────────────────────────────

rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

echo "==> Downloading OpenSSL ${OPENSSL_VERSION}"
curl -LO "${OPENSSL_URL}"

echo "==> Extracting"
tar xf "${OPENSSL_TAR}"

# ── Build ────────────────────────────────────────────────────────────────────

cd "${OPENSSL_SRC}"

echo "==> Configuring for ${OPENSSL_TARGET}"
./Configure "${OPENSSL_TARGET}" \
    no-shared \
    no-tests \
    no-apps \
    --prefix="${BUILD_DIR}/install" \
    --openssldir="${BUILD_DIR}/install/ssl"

echo "==> Building"
make -j"$(nproc)"

echo "==> Installing"
make install_sw

# ── Copy to third_party ──────────────────────────────────────────────────────

echo "==> Copying to ${OUT_DIR}"
# OpenSSL 3.x installs to lib64 on some Linux distros, lib on others
if [ -f "${BUILD_DIR}/install/lib/libssl.a" ]; then
    OPENSSL_LIB_DIR="${BUILD_DIR}/install/lib"
elif [ -f "${BUILD_DIR}/install/lib64/libssl.a" ]; then
    OPENSSL_LIB_DIR="${BUILD_DIR}/install/lib64"
else
    echo "ERROR: Could not find libssl.a in install/lib or install/lib64"
    exit 1
fi

rm -rf "${OUT_DIR}"
mkdir -p "${OUT_DIR}/lib"
cp "${OPENSSL_LIB_DIR}/libssl.a"    "${OUT_DIR}/lib/"
cp "${OPENSSL_LIB_DIR}/libcrypto.a" "${OUT_DIR}/lib/"
cp -r "${BUILD_DIR}/install/include" "${OUT_DIR}/"

echo "==> Cleaning up"
rm -rf "${BUILD_DIR}"

echo ""
echo "Done:"
ls -lh "${OUT_DIR}/lib/"
