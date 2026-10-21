#!/usr/bin/env bash
set -euo pipefail
src="$(cd "$(dirname "$0")/../.." && pwd)"
out="$src/packaging/aur"
version=1.0
# Keeps one top-level Dakkar folder expected by PKGBUILD.
tar -C "$(dirname "$src")" --sort=name --mtime="@0" --owner=0 --group=0 --numeric-owner \
  -czf "$out/dakkar-${version}.tar.gz" \
  --exclude='Dakkar/_release' --exclude='Dakkar/_build-aur' \
  --exclude='Dakkar/build' --exclude='Dakkar/.git' --exclude='Dakkar/packaging/aur/*.tar.gz' \
  --exclude='Dakkar/packaging/aur/src' --exclude='Dakkar/packaging/aur/pkg' \
  --exclude='Dakkar/packaging/aur/PKGBUILD' Dakkar
sha="$(sha256sum "$out/dakkar-${version}.tar.gz" | awk '{print $1}')"
sed -i "s/^sha256sums=.*/sha256sums=('$sha')/" "$out/PKGBUILD"
echo 'Now: cd packaging/aur && makepkg -si'
echo 'For real AUR publication: replace source with URL of tagged GitHub tarball and recalculate SHA256.'
