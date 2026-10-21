#!/usr/bin/env bash
# Source build only. Intentionally does NOT overwrite the desktop's Nemo package.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="${DAKKAR_BUILD_DIR:-$ROOT/build-dakkar}"
PREFIX="${DAKKAR_PREFIX:-$HOME/.local}"
LOG="${DAKKAR_BUILD_LOG:-$ROOT/DAKKAR-BUILD-ATTEMPT.log}"
{
  echo "Files (Dakkar 1.0) isolated native build — $(date -Is)"
  echo "Build directory: $BUILD"; echo "Prefix: $PREFIX"
  missing=0
  for exe in meson ninja gcc pkg-config; do
    if ! command -v "$exe" >/dev/null 2>&1; then
      printf 'MISSING build tool: %s\n' "$exe"; missing=1
    fi
  done
  for lib in gtk+-3.0 gio-2.0 gio-unix-2.0 glib-2.0 json-glib-1.0 cinnamon-desktop gail-3.0 x11 xapp pango; do
    if pkg-config --exists "$lib" 2>/dev/null; then
      printf 'FOUND %s %s\n' "$lib" "$(pkg-config --modversion "$lib")"
    else
      printf 'MISSING development library: %s\n' "$lib"; missing=1
    fi
  done
  # optional: in-process Compress / Extract Here (menus hidden without it)
  if pkg-config --exists libarchive 2>/dev/null; then
    printf 'FOUND %s %s\n' libarchive "$(pkg-config --modversion libarchive)"
  else
    echo 'OPTIONAL library not found: libarchive (Compress… and Extract Here will be unavailable)'
  fi
  if (( missing )); then
    echo 'STOPPED: install your Linux distribution development packages first.'
    echo 'No files were installed and no binary was falsely reported as built.'
    exit 2
  fi
  meson setup "$BUILD" "$ROOT" --prefix "$PREFIX" -Dgtk_doc=false -Dtracker=false
  meson compile -C "$BUILD"
  echo 'Native build finished. Nothing was installed.'
  echo 'Avoid system-wide installation until Nemo naming and path conflicts are resolved.'
} 2>&1 | tee "$LOG"
