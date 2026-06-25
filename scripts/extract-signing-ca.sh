#!/usr/bin/env bash
#
# extract-signing-ca.sh
#
# Extracts the Verokey intermediate CA certificate from the eToken and saves
# it to resources/win/verokey-ca.crt so sign-win.sh can embed it in the
# Authenticode signature chain.
#
# Run once after setting up the token. Commit the resulting .crt file.
#
# Requires: opensc (brew install opensc), SIGN_MODULE env var set.
#
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${REPO_ROOT}/resources/win/verokey-ca.crt"
MODULE="${SIGN_MODULE:-/Library/Frameworks/eToken.framework/Versions/Current/libeToken.dylib}"

if ! command -v pkcs11-tool &>/dev/null; then
    echo "ERROR: pkcs11-tool not found. Install with: brew install opensc"
    exit 1
fi

if [ -z "${SIGN_PASS:-}" ]; then
    echo "ERROR: SIGN_PASS environment variable is not set."
    exit 1
fi

echo "==> Extracting Verokey Secure Code certificate from token..."

pkcs11-tool \
    --module "$MODULE" \
    --login \
    --pin "$SIGN_PASS" \
    --read-object \
    --type cert \
    --label "Verokey Secure Code" \
    --output-file /tmp/verokey-ca.der

openssl x509 -inform DER -in /tmp/verokey-ca.der -out "$OUT"
rm /tmp/verokey-ca.der

echo "==> Saved: ${OUT}"
echo ""
openssl x509 -in "$OUT" -noout -subject -issuer -dates
echo ""
echo "Commit this file so sign-win.sh can embed the CA chain in signatures."
