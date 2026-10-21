#!/usr/bin/env bash
# Reusable Meson setup for native packages (Debian / Arch / generic installer).
set -euo pipefail
src="${1:?source path required}"
build="${2:?build directory required}"
prefix="${3:-/opt/dakkar}"
meson setup "$build" "$src" \
  --prefix="$prefix" --libdir=lib --buildtype=release \
  -Dgtk_doc=false -Dtracker=false
meson compile -C "$build"
