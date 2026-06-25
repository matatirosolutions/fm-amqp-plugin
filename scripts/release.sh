#!/usr/bin/env bash
#
# release.sh
#
# Creates a GitHub release and uploads all signed plugin binaries.
#
# Prerequisites:
#   gh (GitHub CLI, authenticated)
#
# Expected signed binaries (build all platforms first):
#   build/mac/AMQPFMPlugin.fmplugin        (scripts/build-mac.sh + scripts/notarize-mac.sh)
#   build/win/signed/AMQPFMPlugin.fmx64    (scripts/sign-win.sh)
#   build/linux/U22/x64/AMQPFMPlugin.fmx
#   build/linux/U22/arm64/AMQPFMPlugin.fmx
#   build/linux/U24/x64/AMQPFMPlugin.fmx
#   build/linux/U24/arm64/AMQPFMPlugin.fmx
#
# Usage:
#   scripts/release.sh 1.0.0 "Release notes here"
#   scripts/release.sh 1.0.0  # opens $EDITOR for release notes
#
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"

# ── Arguments ────────────────────────────────────────────────────────────────

if [ $# -lt 1 ]; then
    echo "Usage: $0 <version> [release-notes]"
    echo "  e.g. $0 1.0.0 \"First release\""
    exit 1
fi

VERSION="$1"
TAG="v${VERSION}"
NOTES="${2:-}"

# ── Check gh is available ────────────────────────────────────────────────────

if ! command -v gh &>/dev/null; then
    echo "ERROR: gh not found. Install with: brew install gh"
    exit 1
fi

# ── Verify all binaries exist ────────────────────────────────────────────────

MACOS_PLUGIN="${REPO_ROOT}/build/mac/AMQPFMPlugin.fmplugin"
WIN_PLUGIN="${REPO_ROOT}/build/win/signed/AMQPFMPlugin.fmx64"
LINUX_U22_X64="${REPO_ROOT}/build/linux/U22/x64/AMQPFMPlugin.fmx"
LINUX_U22_ARM64="${REPO_ROOT}/build/linux/U22/arm64/AMQPFMPlugin.fmx"
LINUX_U24_X64="${REPO_ROOT}/build/linux/U24/x64/AMQPFMPlugin.fmx"
LINUX_U24_ARM64="${REPO_ROOT}/build/linux/U24/arm64/AMQPFMPlugin.fmx"

missing=()
[ -d "$MACOS_PLUGIN" ]    || missing+=("build/mac/AMQPFMPlugin.fmplugin  →  scripts/build-mac.sh + scripts/notarize-mac.sh")
[ -f "$WIN_PLUGIN" ]      || missing+=("build/win/signed/AMQPFMPlugin.fmx64  →  scripts/sign-win.sh")
[ -f "$LINUX_U22_X64" ]   || missing+=("build/linux/U22/x64/AMQPFMPlugin.fmx  →  scripts/build-linux-docker.sh u22")
[ -f "$LINUX_U22_ARM64" ] || missing+=("build/linux/U22/arm64/AMQPFMPlugin.fmx  →  scripts/build-linux-docker.sh u22 arm64")
[ -f "$LINUX_U24_X64" ]   || missing+=("build/linux/U24/x64/AMQPFMPlugin.fmx  →  scripts/build-linux-docker.sh u24")
[ -f "$LINUX_U24_ARM64" ] || missing+=("build/linux/U24/arm64/AMQPFMPlugin.fmx  →  scripts/build-linux-docker.sh u24 arm64")

if [ ${#missing[@]} -gt 0 ]; then
    echo "ERROR: Missing binaries — build these first:"
    for m in "${missing[@]}"; do
        echo "  $m"
    done
    exit 1
fi

# ── Verify the version matches CMakeLists.txt ────────────────────────────────

CMAKE_VERSION=$(grep -E "^project\(" "${REPO_ROOT}/CMakeLists.txt" | \
    sed -E 's/.*VERSION ([0-9]+\.[0-9]+\.[0-9]+).*/\1/')

if [ "${CMAKE_VERSION}" != "${VERSION}" ]; then
    echo "ERROR: Version mismatch — you specified ${VERSION} but CMakeLists.txt has ${CMAKE_VERSION}"
    echo "Update the version in CMakeLists.txt and rebuild before releasing."
    exit 1
fi

# ── Zip the macOS bundle and Linux binaries for upload ───────────────────────
# GitHub releases don't preserve directory structure, so we zip items that
# need it. The .fmplugin is a bundle (directory), and the Linux .fmx files
# need to be distinguishable by platform.

STAGING=$(mktemp -d)
trap 'rm -rf "$STAGING"' EXIT

echo "==> Preparing release assets"

# macOS — zip the bundle
ditto -c -k --keepParent "$MACOS_PLUGIN" "${STAGING}/AMQPFMPlugin-mac-${VERSION}.zip"

# Windows — rename to include version
cp "$WIN_PLUGIN" "${STAGING}/AMQPFMPlugin-win-x64-${VERSION}.fmx64"

# Linux — rename to include platform/arch/version
cp "$LINUX_U22_X64"   "${STAGING}/AMQPFMPlugin-linux-U22-x64-${VERSION}.fmx"
cp "$LINUX_U22_ARM64" "${STAGING}/AMQPFMPlugin-linux-U22-arm64-${VERSION}.fmx"
cp "$LINUX_U24_X64"   "${STAGING}/AMQPFMPlugin-linux-U24-x64-${VERSION}.fmx"
cp "$LINUX_U24_ARM64" "${STAGING}/AMQPFMPlugin-linux-U24-arm64-${VERSION}.fmx"

echo "    Assets:"
ls -lh "${STAGING}/"

# ── Create the release ───────────────────────────────────────────────────────

echo ""
echo "==> Creating GitHub release ${TAG}"

if [ -n "$NOTES" ]; then
    gh release create "$TAG" \
        --title "AMQP FM Plugin ${VERSION}" \
        --notes "$NOTES" \
        "${STAGING}"/*
else
    # Opens $EDITOR for release notes if none provided
    gh release create "$TAG" \
        --title "AMQP FM Plugin ${VERSION}" \
        --notes-file <(echo "## What's changed") \
        "${STAGING}"/*
fi

echo ""
echo "Released: https://github.com/matatirosolutions/fm-amqp-plugin/releases/tag/${TAG}"
