#!/usr/bin/env bash
# Upload THE OBSERVER to Steam. Requires steamcmd and a Steamworks partner
# account with the app + depots configured (see docs/PUBLISHING.md).
#
#   packaging/steam/push_build.sh <steam_login>
#
# Both platform packages must exist in dist/ first:
#   tools/package.sh linux-x64
#   tools/package.sh windows-x64 build/Release   (from a Windows build)
set -euo pipefail
cd "$(dirname "$0")"

LOGIN="${1:?usage: push_build.sh <steam_login>}"

for d in ../../dist/TheObserver-windows-x64 ../../dist/TheObserver-linux-x64; do
  [ -d "$d" ] || { echo "missing $d — run tools/package.sh for both platforms first"; exit 1; }
done

steamcmd +login "$LOGIN" +run_app_build "$(pwd)/app_build_observer.vdf" +quit
echo "Build uploaded. Set it live in Steamworks > SteamPipe > Builds."
