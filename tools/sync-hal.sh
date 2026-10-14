#!/usr/bin/env bash
# Refresh third_party/mgahal from MGA-Glide (docs/milestones-m7.md step C): the
# shared Matrox HAL as MGA-Glide's tools/hal-export.sh exports it, of which GLOS
# keeps the HAL itself (hal/: its sources, headers and ports, glos.c among them),
# the licence and the 2D reference. The copy is never edited here: HAL changes
# are made in MGA-Glide, merged there, and synced (as DOS-GL does).
#
#   tools/sync-hal.sh [MGA_GLIDE_DIR]     (default ~/MGA-Glide)
#   tools/sync-hal.sh --check             the copy against its MANIFEST
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
dest=$root/third_party/mgahal
if [ "${1:-}" = --check ]; then
  (cd "$dest" && sha256sum --quiet -c MANIFEST) || { echo "sync-hal: third_party/mgahal differs from its MANIFEST" >&2; exit 1; }
  extra=$(cd "$dest" && comm -13 <(awk '{print $2}' MANIFEST | sort) <(find . -type f ! -name MANIFEST | sort))
  [ -z "$extra" ] || { echo "sync-hal: files not in MANIFEST:" >&2; echo "$extra" >&2; exit 1; }
  echo "sync-hal: third_party/mgahal matches MANIFEST ($(cat "$dest/VERSION"))"
  exit 0
fi
src=${1:-$HOME/MGA-Glide}
if [ -n "$(git -C "$src" status --porcelain -- hal tools docs 2>/dev/null)" ]; then
  echo "sync-hal: $src has uncommitted changes; commit them in MGA-Glide first" >&2
  exit 1
fi
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
"$src/tools/hal-export.sh" "$tmp/x" >/dev/null
mkdir -p "$tmp/mgahal/docs"
cp -a "$tmp/x/hal" "$tmp/x/LICENSE" "$tmp/x/VERSION" "$tmp/mgahal/"
cp -a "$tmp/x/docs/matrox-2d.md" "$tmp/mgahal/docs/"
(cd "$tmp/mgahal" && find . -type f ! -name MANIFEST | LC_ALL=C sort | xargs sha256sum > MANIFEST)
rm -rf "$dest"
mkdir -p "$(dirname "$dest")"
cp -a "$tmp/mgahal" "$dest"
echo "sync-hal: third_party/mgahal = MGA-Glide $(cat "$dest/VERSION")"
