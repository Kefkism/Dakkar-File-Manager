#!/usr/bin/env bash
# Build on the matching Debian / Ubuntu / Mint release where you will distribute.
set -euo pipefail
src="$(cd "$(dirname "$0")/../.." && pwd)"
command -v dpkg-deb >/dev/null || { echo 'Install dpkg-dev first.' >&2; exit 1; }
command -v meson >/dev/null || { echo 'Install meson and the build dependencies first.' >&2; exit 1; }
arch="$(dpkg --print-architecture)"
work="${DAKKAR_RELEASE_WORK:-$src/_release/deb}"
mkdir -p "$work"
"$src/packaging/common/build.sh" "$src" "$work/build" /opt/dakkar
stage="$work/stage"
rm -rf "$stage"
DESTDIR="$stage" meson install -C "$work/build" --no-rebuild
"$src/packaging/common/export.sh" "$stage"
mkdir -p "$stage/DEBIAN" "$stage/usr/share/doc/dakkar"
install -m 0644 "$src/COPYING" "$stage/usr/share/doc/dakkar/copyright"
# A graphical package manager can read AppStream, but the local .deb's
# file-type icon remains controlled by the viewer/desktop environment.
# Refresh only the standard desktop caches after installing/removing Dakkar.
cat > "$stage/DEBIAN/postinst" <<'POSTINST'
#!/bin/sh
set -e
if [ "$1" = configure ]; then
  if command -v gtk-update-icon-cache >/dev/null 2>&1; then
    gtk-update-icon-cache -f -t /usr/share/icons/hicolor >/dev/null 2>&1 || true
  fi
  if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database /usr/share/applications >/dev/null 2>&1 || true
  fi
fi
exit 0
POSTINST
cat > "$stage/DEBIAN/postrm" <<'POSTRM'
#!/bin/sh
set -e
if [ "$1" = remove ] || [ "$1" = purge ]; then
  if command -v gtk-update-icon-cache >/dev/null 2>&1; then
    gtk-update-icon-cache -f -t /usr/share/icons/hicolor >/dev/null 2>&1 || true
  fi
  if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database /usr/share/applications >/dev/null 2>&1 || true
  fi
fi
exit 0
POSTRM
chmod 0755 "$stage/DEBIAN/postinst" "$stage/DEBIAN/postrm"
cat > "$stage/DEBIAN/control" <<EOF
Package: dakkar
Version: 1.0-3
Section: utils
Priority: optional
Architecture: $arch
Maintainer: Dakkar Project <noreply@github.com>
Homepage: https://github.com/Kefkism/Dakkar
Depends: libgtk-3-0 | libgtk-3-0t64, libglib2.0-0 | libglib2.0-0t64, libcinnamon-desktop4, libxapp1, libjson-glib-1.0-0, libgstreamer1.0-0, libgstreamer-plugins-base1.0-0, libgsf-1-114, gvfs
Description: Dakkar independent file manager
 Dakkar is a standalone GTK3 file manager based on Nemo with its own
 settings, desktop identity and isolated runtime under /opt/dakkar.
EOF
# Important: package deps must be validated/recalculated per distro release
out="$work/dakkar_1.0-3_${arch}.deb"
dpkg-deb --root-owner-group --build "$stage" "$out"
echo "Created: $out"
