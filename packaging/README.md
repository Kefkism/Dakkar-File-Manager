Dakkar — Release Packaging

This directory contains packaging recipes, not precompiled distribution packages.

Native package model

The application executable, libraries, schemas, helper files, assets, and Nemo-derived internal components are located under /opt/dakkar. Only the following Dakkar-specific names are exported to the system:

/usr/bin/dakkar (wrapper)
/usr/share/applications/org.dakkar.Files.desktop
/usr/share/metainfo/org.dakkar.Files.metainfo.xml
/usr/share/icons/hicolor/*/apps/org.dakkar.Files.png

No nemo.desktop, nemo executable, org.nemo.* schema, or Nemo D-Bus activation service is installed system-wide. Nemo remains installed and available. Name=Dakkar identifies it as a separate application in desktop menus.

1. Debian / Ubuntu / Mint

Install the source build dependencies (see README.md) and dpkg-dev, then run:

./packaging/deb/build-deb.sh
sudo apt install ./_release/deb/dakkar_1.0-3_*.deb


Build separately for each distribution version, because dependencies and ABIs differ. The control file contains a baseline list of runtime dependencies; before publication, verify it with dpkg-shlibdeps, lintian, a clean VM, and an actual application launch. This is not yet a universal .deb package.

2. Arch / AUR
./packaging/aur/prepare-local.sh
cd packaging/aur
makepkg -si


For official AUR publication, the PKGBUILD must use the tag URL of an existing GitHub release and its actual SHA256 checksum. Then generate .SRCINFO with makepkg --printsrcinfo > .SRCINFO and commit both files to the AUR repository.

3. Shell installer (compiles locally, without root privileges)
./packaging/sh/install.sh
~/.local/bin/dakkar
./packaging/sh/uninstall.sh  # optional


This does not provide a universally executable binary; a compiler and development dependencies must be available locally.

4. Flatpak
flatpak install flathub org.gnome.Platform//50 org.gnome.Sdk//50
./packaging/flatpak/build-flatpak.sh
flatpak install --user ./_release/Dakkar-1.0.flatpak


Flatpak is no longer experimental. The manifest is maintained as part of the release packaging.

The GNOME runtime does not provide the required Cinnamon/XApp/GSF/Exempi stack without additional dependency modules. xapp and cinnamon-desktop are included as upstream Git sources; their transitive dependencies and Flatpak sandbox/file-operation requirements (GVfs, mounts, and portals) must be integrated and tested.

A file manager requires broader filesystem permissions than a typical sandboxed application. Review and verify these permissions carefully before publication.

Identity versus upstream heritage

nemo-*.c files and Nemo* C symbols remain as internal implementation details, including their copyright notices and licenses. These names are not published application IDs. A complete migration to different public C APIs or an extension ABI is deliberately not presented as finished.

Release verification

Run python3 packaging/check-identity.py for static checks, then test Dakkar and Nemo running simultaneously in a clean VM. The packages have not been natively compiled in this environment. Publishing a tested release requires a successful build and smoke test for each distribution.

Dakkar icon

Dakkar uses org.dakkar.Files.png for GTK windows, desktop launchers, and AppStream software metadata. The window icon is also embedded as a GResource, so it works with a local build even when no icon theme is installed.

A local .deb file normally retains the default package-file icon. Some graphical package installers also always display a generic icon in their own package previews. This behavior is determined by the installer and cannot be universally enforced through a Debian control file.

See packaging/ICON-VERIFICATION.md for the inspection commands.
