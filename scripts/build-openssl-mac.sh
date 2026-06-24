#!/usr/bin/env bash
#
# Build a universal (arm64 + x86_64) static OpenSSL for macOS and place the
# output in third_party/openssl/mac/.  Run once; re-run to upgrade versions.
#
# Requirements: Xcode Command Line Tools, Perl (ships with macOS)
#
set -euo pipefail

OPENSSL_VERSION="3.3.2"
OPENSSL_SRC="openssl-${OPENSSL_VERSION}"
OPENSSL_TAR="${OPENSSL_SRC}.tar.gz"
OPENSSL_URL="https://www.openssl.org/source/${OPENSSL_TAR}"

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT_DIR="${REPO_ROOT}/third_party/openssl/mac"
# Build in /tmp to avoid OpenSSL Makefile issues with spaces in paths
BUILD_DIR="/tmp/openssl-amqp-build"

echo "==> Downloading OpenSSL ${OPENSSL_VERSION}"
mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

if [ ! -f "${OPENSSL_TAR}" ]; then
    curl -LO "${OPENSSL_URL}"
fi

echo "==> Extracting"
rm -rf "${OPENSSL_SRC}"
tar xf "${OPENSSL_TAR}"

build_arch() {
    local ARCH=$1
    local TARGET=$2
    local PREFIX="${BUILD_DIR}/install-${ARCH}"

    echo "==> Building OpenSSL for ${ARCH}"
    rm -rf "build-${ARCH}"
    cp -r "${OPENSSL_SRC}" "build-${ARCH}"
    cd "build-${ARCH}"

    ./Configure \
        "${TARGET}" \
        no-shared \
        no-tests \
        no-apps \
        --prefix="${PREFIX}" \
        --openssldir="${PREFIX}/ssl" \
        -mmacosx-version-min=11.0

    make -j"$(sysctl -n hw.logicalcpu)"
    make install_sw

    cd "${BUILD_DIR}"
}

build_arch "arm64"  "darwin64-arm64-cc"
build_arch "x86_64" "darwin64-x86_64-cc"

echo "==> Lipoing universal binaries"
mkdir -p "${OUT_DIR}/lib"
lipo -create \
    "${BUILD_DIR}/install-arm64/lib/libssl.a" \
    "${BUILD_DIR}/install-x86_64/lib/libssl.a" \
    -output "${OUT_DIR}/lib/libssl.a"

lipo -create \
    "${BUILD_DIR}/install-arm64/lib/libcrypto.a" \
    "${BUILD_DIR}/install-x86_64/lib/libcrypto.a" \
    -output "${OUT_DIR}/lib/libcrypto.a"

echo "==> Copying headers"
rm -rf "${OUT_DIR}/include"
cp -r "${BUILD_DIR}/install-arm64/include" "${OUT_DIR}/include"

echo "==> Cleaning up build intermediates"
rm -rf "${BUILD_DIR}"

echo ""
echo "Done. Output:"
echo "  ${OUT_DIR}/lib/libssl.a    ($(lipo -archs "${OUT_DIR}/lib/libssl.a"))"
echo "  ${OUT_DIR}/lib/libcrypto.a ($(lipo -archs "${OUT_DIR}/lib/libcrypto.a"))"
echo "  ${OUT_DIR}/include/openssl/"
