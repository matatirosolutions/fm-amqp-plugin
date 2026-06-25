#!/usr/bin/env bash
#
# sign-win.sh
#
# Waits for the latest Windows GitHub Actions build to complete, downloads the
# unsigned AMQPFMPlugin.fmx64 artifact, signs it with osslsigncode via a
# PKCS11 token, and places the signed binary in build/win/signed/.
#
# Requires:
#   gh          GitHub CLI, authenticated  (brew install gh)
#   osslsigncode                           (brew install osslsigncode)
#
# Required environment variables (add to ~/.zshrc or ~/.zprofile):
#   SIGN_ENGINE   Path to the PKCS11 engine shared library
#   SIGN_MODULE   Path to the PKCS11 module shared library
#   SIGN_TOKEN    PKCS11 token name
#   SIGN_OBJECT   PKCS11 object (key) name
#   SIGN_PASS     Token PIN / password
#
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SIGN_NAME="MSDev AMQP FM Plugin"
SIGN_TIMESTAMP="http://timestamp.digicert.com"
SIGN_CA="${REPO_ROOT}/resources/win/verokey-ca.crt"
OUT_DIR="${REPO_ROOT}/build/win/signed"
ARTIFACT_NAME="AMQPFMPlugin-win"

# ── Dependency check ─────────────────────────────────────────────────────────

check_deps() {
    local missing=()
    for cmd in gh osslsigncode python3; do
        command -v "$cmd" &>/dev/null || missing+=("$cmd")
    done
    if [ ${#missing[@]} -gt 0 ]; then
        echo "Missing required tools: ${missing[*]}"
        echo "Install with: brew install ${missing[*]}"
        exit 1
    fi

    for var in SIGN_ENGINE SIGN_MODULE SIGN_TOKEN SIGN_OBJECT SIGN_PASS; do
        [ -n "${!var:-}" ] || { echo "Error: environment variable $var is not set."; exit 1; }
    done

    if [ ! -f "${SIGN_CA}" ]; then
        echo "Error: CA certificate not found at ${SIGN_CA}"
        echo "Extract it once with: scripts/extract-signing-ca.sh"
        exit 1
    fi
}

# ── Sign ─────────────────────────────────────────────────────────────────────

sign() {
    local in_file="$1"
    local out_file="${in_file}.signed"
    echo "==> Signing: $(basename "$in_file")"
    osslsigncode sign \
        -pkcs11engine  "$SIGN_ENGINE" \
        -pkcs11module  "$SIGN_MODULE" \
        -pkcs11cert    "pkcs11:token=$SIGN_TOKEN;object=$SIGN_OBJECT" \
        -pass          "$SIGN_PASS" \
        -ac            "$SIGN_CA" \
        -n             "$SIGN_NAME" \
        -ts            "$SIGN_TIMESTAMP" \
        -in            "$in_file" \
        -out           "$out_file"
    mv "$out_file" "$in_file"
    echo "==> Verifying signature"
    osslsigncode verify -in "$in_file"
}

# ── Main ─────────────────────────────────────────────────────────────────────

check_deps

echo "==> Finding latest Windows build run on main..."
RUN_JSON=$(gh run list --workflow build-windows.yml --branch main --limit 1 \
    --json databaseId,status,conclusion)

RUN_ID=$(echo "$RUN_JSON"   | python3 -c "import sys,json; print(json.load(sys.stdin)[0]['databaseId'])")
STATUS=$(echo "$RUN_JSON"   | python3 -c "import sys,json; print(json.load(sys.stdin)[0]['status'])")

if [ -z "$RUN_ID" ]; then
    echo "No runs found for build-windows.yml on main. Push your changes first."
    exit 1
fi

echo "    Run ID: $RUN_ID  Status: $STATUS"

if [ "$STATUS" != "completed" ]; then
    echo "==> Waiting for run to complete..."
    gh run watch "$RUN_ID"
fi

CONCLUSION=$(gh run view "$RUN_ID" --json conclusion -q '.conclusion')
if [ "$CONCLUSION" != "success" ]; then
    echo "Build did not succeed (conclusion: $CONCLUSION). Aborting."
    gh run view "$RUN_ID" --log-failed
    exit 1
fi

echo "==> Downloading artifact..."
rm -rf "${OUT_DIR}"
mkdir -p "${OUT_DIR}"
gh run download "$RUN_ID" --name "$ARTIFACT_NAME" --dir "${OUT_DIR}"

BINARY="${OUT_DIR}/AMQPFMPlugin.fmx64"
if [ ! -f "$BINARY" ]; then
    echo "ERROR: Expected ${BINARY} — artifact contents:"
    ls -la "${OUT_DIR}/"
    exit 1
fi

sign "$BINARY"

echo ""
echo "Done: ${BINARY}"
echo ""
echo "To install, copy to:"
echo "  C:\\Program Files\\FileMaker\\FileMaker Pro\\Extensions\\"
