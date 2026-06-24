#!/usr/bin/env bash
#
# build-mac.sh
#
# Builds AMQPFMPlugin.fmplugin for macOS (universal arm64 + x86_64).
#
# Prerequisites:
#   Xcode Command Line Tools  (xcode-select --install)
#   CMake 3.21+               (brew install cmake)
#   OpenSSL static libs       (run scripts/build-openssl-mac.sh first)
#
# Output:   build/mac/AMQPFMPlugin.fmplugin
# Install:  cp -R build/mac/AMQPFMPlugin.fmplugin \
#             ~/Library/Application\ Support/FileMaker/Extensions/
#
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${REPO_ROOT}/build/mac"

# Override with -i flag or CODESIGN_IDENTITY env var; default = Developer ID cert
CODESIGN_IDENTITY="${CODESIGN_IDENTITY:-Developer ID Application: Matatiro Solutions Limited (RM5TNT52M5)}"

while getopts "i:" opt; do
    case $opt in
        i) CODESIGN_IDENTITY="$OPTARG" ;;
        *) echo "Usage: $0 [-i codesign-identity]"; exit 1 ;;
    esac
done

# ── Prerequisites check ──────────────────────────────────────────────────────

for cmd in cmake; do
    if ! command -v "$cmd" &>/dev/null; then
        echo "ERROR: '$cmd' not found. Install via: brew install cmake"
        exit 1
    fi
done

if [ ! -f "${REPO_ROOT}/sdk/Libraries/Mac/FMWrapper.framework/FMWrapper" ]; then
    echo "ERROR: FileMaker SDK not found in sdk/"
    echo "Download the FileMaker Plugin SDK and place it in sdk/"
    exit 1
fi

OPENSSL_LIB="${REPO_ROOT}/third_party/openssl/mac/lib/libssl.a"
if [ ! -f "$OPENSSL_LIB" ]; then
    echo "WARNING: Bundled static OpenSSL not found."
    echo "  Run scripts/build-openssl-mac.sh first to enable TLS support."
    echo "  Building without SSL..."
fi

# ── Configure ────────────────────────────────────────────────────────────────

echo "==> Configuring (universal arm64 + x86_64)"
echo "    Signing identity: ${CODESIGN_IDENTITY}"
cmake -S "${REPO_ROOT}" -B "${BUILD_DIR}" \
    -G "Unix Makefiles" \
    -DCMAKE_CXX_COMPILER="$(xcrun -find clang++)" \
    -DCMAKE_C_COMPILER="$(xcrun -find clang)" \
    -DCMAKE_BUILD_TYPE=Release \
    -DUNIVERSAL=ON \
    -DCODESIGN_IDENTITY="${CODESIGN_IDENTITY}"

# ── Build ────────────────────────────────────────────────────────────────────

echo "==> Building"
cmake --build "${BUILD_DIR}" -- -j"$(sysctl -n hw.logicalcpu)"

PLUGIN="${BUILD_DIR}/AMQPFMPlugin.fmplugin"

echo ""
echo "Done: ${PLUGIN}"
echo ""

# Verify the signature
echo "==> Verifying signature"
codesign -vvv --strict "${PLUGIN}"

echo ""
if [[ "${CODESIGN_IDENTITY}" == "-" ]]; then
    echo "Built with ad-hoc signature. To notarize, re-run with your Developer ID:"
    echo "  CODESIGN_IDENTITY=\"Developer ID Application: ...\" $0"
else
    echo "To notarize and staple, run:"
    echo "  scripts/notarize-mac.sh"
fi
echo ""
echo "To install:"
echo "  cp -R \"${PLUGIN}\" \\"
echo "    \"\$HOME/Library/Application Support/FileMaker/Extensions/\""
