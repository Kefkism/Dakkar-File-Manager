#!/usr/bin/env bash
set -euo pipefail
src="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$src/packaging/flatpak"
command -v flatpak-builder >/dev/null || { echo 'Install flatpak-builder first.' >&2; exit 1; }
# Keep the Flatpak checkout/build output outside the source tree to avoid
# recursively copying it into the local directory source module.
cache="${XDG_CACHE_HOME:-$HOME/.cache}/dakkar-flatpak-build"
mkdir -p "$cache" "$src/_release"
flatpak-builder --force-clean --repo="$cache/repo" \
  "$cache/work" org.dakkar.Files.json
flatpak build-bundle "$cache/repo" \
  "$src/_release/Dakkar-1.0.flatpak" org.dakkar.Files
