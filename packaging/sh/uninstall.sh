#!/usr/bin/env bash
# Uninstall only Dakkar user-prefix files. Never modify Nemo or shared system settings.
set -euo pipefail
prefix="${DAKKAR_PREFIX:-$HOME/.local/opt/dakkar}"
[[ "$prefix" == "$HOME/"* ]] || { echo 'Refusing to delete outside $HOME' >&2; exit 2; }
[[ -f "$prefix/share/glib-2.0/schemas/org.dakkar.gschema.xml" ]] || { echo 'No Dakkar user installation found at specified prefix' >&2; exit 1; }
rm -rf -- "$prefix"
rm -f -- "$HOME/.local/bin/dakkar" "$HOME/.local/share/applications/org.dakkar.Files.desktop" "$HOME/.local/share/metainfo/org.dakkar.Files.metainfo.xml"
find "$HOME/.local/share/icons/hicolor" -path '*/apps/org.dakkar.Files.png' -type f -delete 2>/dev/null || true
command -v update-desktop-database >/dev/null && update-desktop-database "$HOME/.local/share/applications" || true
echo 'Uninstalled Dakkar. Nemo was not changed; dconf settings were left untouched.'
