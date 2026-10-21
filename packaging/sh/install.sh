#!/usr/bin/env bash
# Per-user source installation; no sudo, no replacement of Nemo.
set -euo pipefail
src="$(cd "$(dirname "$0")/../.." && pwd)"
prefix="${DAKKAR_PREFIX:-$HOME/.local/opt/dakkar}"
[[ "$prefix" == "$HOME/"* ]] || { echo 'This installer only supports prefixes inside $HOME' >&2; exit 2; }
work="${DAKKAR_BUILD_DIR:-$src/_release/user-build}"
meson setup "$work" "$src" --prefix="$prefix" --libdir=lib --buildtype=release -Dgtk_doc=false -Dtracker=false
meson compile -C "$work"
meson install -C "$work" --no-rebuild
# Compile its OWN settings in isolated prefix. This must not touch system Nemo schemas.
glib-compile-schemas "$prefix/share/glib-2.0/schemas"
mkdir -p "$HOME/.local/bin" "$HOME/.local/share/applications" "$HOME/.local/share/metainfo"
cat > "$HOME/.local/bin/dakkar" <<EOF
#!/usr/bin/env bash
prefix="$prefix"
export GSETTINGS_SCHEMA_DIR="\$prefix/share/glib-2.0/schemas"
export XDG_DATA_DIRS="\$prefix/share:\${XDG_DATA_DIRS:-/usr/local/share:/usr/share}"
export PATH="\$prefix/bin:\$PATH"
export LD_LIBRARY_PATH="\$prefix/lib\${LD_LIBRARY_PATH:+:\$LD_LIBRARY_PATH}"
export GI_TYPELIB_PATH="\$prefix/lib/girepository-1.0\${GI_TYPELIB_PATH:+:\$GI_TYPELIB_PATH}"
exec "\$prefix/bin/dakkar" "\$@"
EOF
chmod 755 "$HOME/.local/bin/dakkar"
sed "s|^Exec=dakkar |Exec=$HOME/.local/bin/dakkar |" \
  "$src/packaging/common/org.dakkar.Files.desktop" \
  > "$HOME/.local/share/applications/org.dakkar.Files.desktop"
cp "$src/data/org.dakkar.Files.metainfo.xml" "$HOME/.local/share/metainfo/org.dakkar.Files.metainfo.xml"
for f in "$prefix"/share/icons/hicolor/*/apps/org.dakkar.Files.png; do
  [[ -f "$f" ]] || continue
  size="$(basename "$(dirname "$f")")"
  mkdir -p "$HOME/.local/share/icons/hicolor/$size/apps"
  cp "$f" "$HOME/.local/share/icons/hicolor/$size/apps/org.dakkar.Files.png"
done
command -v update-desktop-database >/dev/null && update-desktop-database "$HOME/.local/share/applications" || true
printf 'Dakkar installed: %s\nLaunch with: %s/.local/bin/dakkar\n' "$prefix" "$HOME"
