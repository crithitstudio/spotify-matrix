#!/usr/bin/env bash
# THE OBSERVER - build a distributable package.
#   tools/package.sh linux-x64              (binary in build/)
#   tools/package.sh windows-x64 build/Release
set -euo pipefail
cd "$(dirname "$0")/.."

PLAT="${1:-linux-x64}"
BINDIR="${2:-build}"
NAME="TheObserver-$PLAT"
OUT="dist/$NAME"

rm -rf "$OUT"
mkdir -p "$OUT"

if [[ "$PLAT" == windows-* ]]; then
  cp "$BINDIR/observer.exe" "$OUT/TheObserver.exe"
else
  cp "$BINDIR/observer" "$OUT/TheObserver"
  cp packaging/linux/observer.sh "$OUT/run.sh"
  chmod +x "$OUT/TheObserver" "$OUT/run.sh"
fi

cp -r content "$OUT/content"
mkdir -p "$OUT/assets"
for d in textures audio witness fonts ui; do
  [ -d "assets/$d" ] && cp -r "assets/$d" "$OUT/assets/$d"
done
# strip raw generation sources from the shipped tree
rm -rf "$OUT/assets/textures/src"
mkdir -p "$OUT/shaders"
cp -r engine/shaders/spv "$OUT/shaders/spv"
cp docs/PLAYER_MANUAL.md "$OUT/README.md" 2>/dev/null || true
cp THIRD-PARTY-LICENSES.md "$OUT/" 2>/dev/null || true

mkdir -p dist
if [[ "$PLAT" == windows-* ]]; then
  (cd dist && rm -f "$NAME.zip" && (command -v zip >/dev/null && zip -qr "$NAME.zip" "$NAME" || python3 -c "import shutil; shutil.make_archive('$NAME','zip','.','$NAME')"))
  echo "dist/$NAME.zip"
else
  tar -czf "dist/$NAME.tar.gz" -C dist "$NAME"
  echo "dist/$NAME.tar.gz"
fi
