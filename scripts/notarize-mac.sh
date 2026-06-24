#!/usr/bin/env bash
#
# notarize-mac.sh
#
# Zips the signed .fmplugin, submits it to Apple's notarization service,
# waits for approval, then staples the ticket so Gatekeeper works offline.
#
# Prerequisites:
#   - Plugin already built and signed with a Developer ID certificate:
#       scripts/build-mac.sh
#   - An App Store Connect API key stored in Keychain, or set the three
#     env vars below. Create the key at:
#       https://appstoreconnect.apple.com/access/api
#     and store it with:
#       xcrun notarytool store-credentials "AC_PASSWORD" \
#           --apple-id <your@email> \
#           --team-id RM5TNT52M5 \
#           --password <app-specific-password>
#
# Usage:
#   ./scripts/notarize-mac.sh
#
# Environment variables (alternative to stored credentials):
#   APPLE_ID        your Apple ID email
#   APPLE_TEAM_ID   your Team ID (RM5TNT52M5)
#   APPLE_APP_PWD   app-specific password from appleid.apple.com
#
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${REPO_ROOT}/build/mac"
PLUGIN="${BUILD_DIR}/AMQPFMPlugin.fmplugin"
ZIP="${BUILD_DIR}/AMQPFMPlugin-notarize.zip"

# Keychain profile name (set via: xcrun notarytool store-credentials "AC_PASSWORD" ...)
KEYCHAIN_PROFILE="${KEYCHAIN_PROFILE:-AC_PASSWORD}"

# ── Check plugin exists ──────────────────────────────────────────────────────

if [ ! -d "${PLUGIN}" ]; then
    echo "ERROR: Plugin not found at ${PLUGIN}"
    echo "Run scripts/build-mac.sh first."
    exit 1
fi

# ── Verify it is Developer ID signed (not ad-hoc) ───────────────────────────

SIGNING_ID=$(codesign -dvv "${PLUGIN}" 2>&1 | grep "^Authority=" | head -1 || true)
if [[ -z "${SIGNING_ID}" ]] || [[ "${SIGNING_ID}" == *"adhoc"* ]]; then
    echo "ERROR: Plugin is not signed with a Developer ID certificate."
    echo "Re-build with: scripts/build-mac.sh"
    exit 1
fi
echo "Signed as: ${SIGNING_ID}"

# ── Zip for submission ───────────────────────────────────────────────────────

echo ""
echo "==> Creating zip for notarization"
rm -f "${ZIP}"
ditto -c -k --keepParent "${PLUGIN}" "${ZIP}"
echo "    ${ZIP}"

# ── Submit ───────────────────────────────────────────────────────────────────

echo ""
echo "==> Submitting to Apple Notary Service (this takes 1-5 minutes)"

if [ -n "${APPLE_ID:-}" ] && [ -n "${APPLE_TEAM_ID:-}" ] && [ -n "${APPLE_APP_PWD:-}" ]; then
    # Use env vars directly
    SUBMISSION=$(xcrun notarytool submit "${ZIP}" \
        --apple-id "${APPLE_ID}" \
        --team-id "${APPLE_TEAM_ID}" \
        --password "${APPLE_APP_PWD}" \
        --wait \
        --output-format json)
else
    # Use stored keychain credentials
    SUBMISSION=$(xcrun notarytool submit "${ZIP}" \
        --keychain-profile "${KEYCHAIN_PROFILE}" \
        --wait \
        --output-format json)
fi

echo "${SUBMISSION}"

STATUS=$(echo "${SUBMISSION}" | python3 -c "import sys,json; print(json.load(sys.stdin)['status'])")
if [ "${STATUS}" != "Accepted" ]; then
    echo ""
    echo "ERROR: Notarization failed (status: ${STATUS})"
    echo "Check the log with:"
    ID=$(echo "${SUBMISSION}" | python3 -c "import sys,json; print(json.load(sys.stdin)['id'])")
    echo "  xcrun notarytool log ${ID} --keychain-profile \"${KEYCHAIN_PROFILE}\""
    exit 1
fi

# ── Staple ───────────────────────────────────────────────────────────────────

echo ""
echo "==> Stapling notarization ticket"
xcrun stapler staple "${PLUGIN}"

echo ""
echo "==> Verifying"
spctl -a -vvv -t install "${PLUGIN}"

echo ""
echo "Notarization complete: ${PLUGIN}"
echo ""
echo "To install:"
echo "  cp -R \"${PLUGIN}\" \\"
echo "    \"\$HOME/Library/Application Support/FileMaker/Extensions/\""
