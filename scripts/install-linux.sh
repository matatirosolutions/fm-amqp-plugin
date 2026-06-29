#!/usr/bin/env bash
#
# install-linux.sh
#
# Downloads and installs the latest AMQP FM Plugin release for this server.
# Detects Ubuntu version (22 / 24) and architecture (x64 / arm64).
#
# Usage:
#   curl -fsSL https://raw.githubusercontent.com/matatirosolutions/fm-amqp-plugin/main/scripts/install-linux.sh | sudo bash
#
# Or download and run:
#   sudo bash install-linux.sh
#
set -euo pipefail

REPO="matatirosolutions/fm-amqp-plugin"
EXTENSIONS_DIR="/opt/FileMaker/FileMaker Server/Database Server/Extensions"

# ── Colours ──────────────────────────────────────────────────────────────────

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; NC='\033[0m'
info()  { echo -e "${GREEN}==>${NC} $*"; }
warn()  { echo -e "${YELLOW}WARN:${NC} $*"; }
error() { echo -e "${RED}ERROR:${NC} $*" >&2; exit 1; }

# ── Root check ───────────────────────────────────────────────────────────────

if [ "$EUID" -ne 0 ]; then
    error "Please run as root (sudo bash $0)"
fi

# ── Detect Ubuntu version ────────────────────────────────────────────────────

if [ ! -f /etc/os-release ]; then
    error "Cannot read /etc/os-release — is this Ubuntu?"
fi
. /etc/os-release

if [ "${ID:-}" != "ubuntu" ]; then
    error "This script requires Ubuntu (detected: ${ID:-unknown})"
fi

UBUNTU_MAJOR="${VERSION_ID%%.*}"
case "$UBUNTU_MAJOR" in
    22) UBUNTU_TAG="U22" ;;
    24) UBUNTU_TAG="U24" ;;
    *)  error "Unsupported Ubuntu version ${VERSION_ID} — only 22 and 24 are supported" ;;
esac
info "Detected Ubuntu ${VERSION_ID} (${UBUNTU_TAG})"

# ── Detect architecture ──────────────────────────────────────────────────────

ARCH="$(uname -m)"
case "$ARCH" in
    x86_64)  ARCH_TAG="x64"   ;;
    aarch64) ARCH_TAG="arm64" ;;
    *)        error "Unsupported architecture: ${ARCH} — only x86_64 and aarch64 are supported" ;;
esac
info "Detected architecture: ${ARCH} (${ARCH_TAG})"

# ── Fetch latest release version ─────────────────────────────────────────────

info "Fetching latest release from GitHub..."

if ! command -v curl &>/dev/null; then
    error "curl is required but not installed. Run: apt-get install -y curl"
fi

API_RESPONSE=$(curl -fsSL "https://api.github.com/repos/${REPO}/releases/latest") \
    || error "Failed to reach GitHub API — check your internet connection"

VERSION=$(echo "$API_RESPONSE" | grep -m1 '"tag_name"' | sed -E 's/.*"tag_name": *"v?([^"]+)".*/\1/')

if [ -z "$VERSION" ]; then
    error "Could not determine latest release version from GitHub API"
fi
info "Latest release: v${VERSION}"

# ── Build asset name and download URL ────────────────────────────────────────

ASSET="AMQPFMPlugin-linux-${UBUNTU_TAG}-${ARCH_TAG}-${VERSION}.fmx"
DOWNLOAD_URL="https://github.com/${REPO}/releases/download/v${VERSION}/${ASSET}"

info "Downloading ${ASSET}..."
TMPFILE="$(mktemp /tmp/AMQPFMPlugin.XXXXXX.fmx)"
trap 'rm -f "$TMPFILE"' EXIT

curl -fsSL --progress-bar -o "$TMPFILE" "$DOWNLOAD_URL" \
    || error "Download failed. Check that release v${VERSION} has a Linux ${UBUNTU_TAG} ${ARCH_TAG} asset."

# ── Verify FileMaker Server is installed ─────────────────────────────────────

if [ ! -d "$EXTENSIONS_DIR" ]; then
    error "Extensions directory not found: ${EXTENSIONS_DIR}
Is FileMaker Server installed?"
fi

# ── Install ───────────────────────────────────────────────────────────────────

DEST="${EXTENSIONS_DIR}/AMQPFMPlugin.fmx"

if [ -f "$DEST" ]; then
    warn "Replacing existing plugin: ${DEST}"
fi

cp "$TMPFILE" "$DEST"
chmod 644 "$DEST"

info "Installed to: ${DEST}"

# ── Restart fmse ─────────────────────────────────────────────────────────────

echo ""
echo -e "${YELLOW}Note:${NC} Restart FileMaker Server (or the fmse service) to load the new plugin:"
echo "  sudo systemctl restart fmserver"
echo ""
echo -e "${GREEN}Done.${NC} AMQP FM Plugin v${VERSION} installed for Ubuntu ${VERSION_ID} ${ARCH_TAG}."
