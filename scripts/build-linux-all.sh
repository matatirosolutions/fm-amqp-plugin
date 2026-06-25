#!/usr/bin/env bash
#
# build-linux-all.sh
#
# Builds AMQPFMPlugin.fmx for all supported Linux targets:
#   Ubuntu 22.04 x64, Ubuntu 22.04 arm64
#   Ubuntu 24.04 x64, Ubuntu 24.04 arm64
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

"${SCRIPT_DIR}/build-linux-docker.sh" u22 x64
"${SCRIPT_DIR}/build-linux-docker.sh" u22 arm64
"${SCRIPT_DIR}/build-linux-docker.sh" u24 x64
"${SCRIPT_DIR}/build-linux-docker.sh" u24 arm64

echo ""
echo "All Linux builds complete:"
find "$(cd "${SCRIPT_DIR}/.." && pwd)/build/linux" -name "*.fmx" | sort | while read -r f; do
    echo "  $f"
done
