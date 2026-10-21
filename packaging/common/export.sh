#!/usr/bin/env bash
# Build a self-contained filesystem tree; must run AFTER DESTDIR meson install.
# Intended tree: STAGE/opt/dakkar (private payload) + STAGE/usr (only Dakkar exports).
set -euo pipefail
stage="${1:?pass DESTDIR containing opt/dakkar}"
prefix="$stage/opt/dakkar"
[[ -x "$prefix/bin/dakkar" ]] || { echo 'Missing compiled Dakkar executable' >&2; exit 1; }
[[ -f "$prefix/share/glib-2.0/schemas/org.dakkar.gschema.xml" ]] || { echo 'Missing org.dakkar schemas' >&2; exit 1; }
glib-compile-schemas "$prefix/share/glib-2.0/schemas"
mkdir -p "$stage/usr/bin" "$stage/usr/share/applications" "$stage/usr/share/metainfo"
install -m 0755 "$(dirname "$0")/dakkar-wrapper" "$stage/usr/bin/dakkar"
install -m 0644 "$(dirname "$0")/org.dakkar.Files.desktop" "$stage/usr/share/applications/org.dakkar.Files.desktop"
install -m 0644 "$prefix/share/metainfo/org.dakkar.Files.metainfo.xml" "$stage/usr/share/metainfo/org.dakkar.Files.metainfo.xml"
# A missing Dakkar icon is a packaging error; fail instead of silently
# shipping a .deb with a generic application picture.
for size in 64 128 256 512; do
  [[ -s "$prefix/share/icons/hicolor/${size}x${size}/apps/org.dakkar.Files.png" ]] || {
    echo "Missing Dakkar app icon (${size}x${size})" >&2
    exit 1
  }
done
# Export only the namespaced app icons; inherited Nemo UI assets stay private.
for icon in "$prefix"/share/icons/hicolor/*/apps/org.dakkar.Files.png; do
  [[ -f "$icon" ]] || continue
  size="$(basename "$(dirname "$icon")")"
  mkdir -p "$stage/usr/share/icons/hicolor/$size/apps"
  install -m 0644 "$icon" "$stage/usr/share/icons/hicolor/$size/apps/org.dakkar.Files.png"
done
# Compatibility for older desktop/file managers that still look in pixmaps.
# The canonical identity stays org.dakkar.Files in hicolor + AppStream.
if [[ -f "$prefix/share/icons/hicolor/256x256/apps/org.dakkar.Files.png" ]]; then
  mkdir -p "$stage/usr/share/pixmaps"
  install -m 0644 "$prefix/share/icons/hicolor/256x256/apps/org.dakkar.Files.png" \
    "$stage/usr/share/pixmaps/org.dakkar.Files.png"
fi
# Meson-generated desktop file is intentionally private; public one invokes wrapper.
echo 'Exported org.dakkar.Files as separate native app; Nemo paths untouched.'
