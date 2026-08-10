#!/usr/bin/env bash
# Upload THE OBSERVER to itch.io with butler.
#   packaging/itch/push.sh <itchuser>/<game-slug> [version]
# e.g.
#   packaging/itch/push.sh crithitstudio/the-observer 0.9.0
set -euo pipefail
cd "$(dirname "$0")/../.."

TARGET="${1:?usage: push.sh <itchuser>/<game-slug> [version]}"
VERSION="${2:-0.9.0}"

[ -d dist/TheObserver-windows-x64 ] && butler push dist/TheObserver-windows-x64 "$TARGET:windows-x64" --userversion "$VERSION"
[ -d dist/TheObserver-linux-x64 ] && butler push dist/TheObserver-linux-x64 "$TARGET:linux-x64" --userversion "$VERSION"
butler status "$TARGET"
