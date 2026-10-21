# Dakkar icon verification

App ID: `org.dakkar.Files`  ·  Desktop ID: `org.dakkar.Files.desktop`

Dakkar embeds its 128px folder icon as a GResource. GTK window icons therefore
work even before the host icon theme/cache has been updated. The `.desktop`
files and AppStream metainfo use the same ID. Native install packages export
all sizes of the icon to `/usr/share/icons/hicolor/<size>/apps/` and a 256px
compatibility image to `/usr/share/pixmaps/`.

## Build a NEW Debian package

```sh
# From the Dakkar source root, after installing all build dependencies:
rm -rf _release/deb/build _release/deb/stage
./packaging/deb/build-deb.sh
```

Inspect that the DEB actually contains its launcher, metainfo and icons:

```sh
dpkg-deb --contents _release/deb/dakkar_1.0-3_*.deb \
  | grep -E 'org\.dakkar\.Files(\.desktop|\.metainfo\.xml|\.png)'
```

Install the newly built DEB and refresh the desktop icon cache where present:

```sh
sudo apt install ./_release/deb/dakkar_1.0-3_*.deb
sudo gtk-update-icon-cache -f -t /usr/share/icons/hicolor
```

Close any old running Dakkar process and relaunch the installed application
using its desktop ID. Note that a **local .deb file itself** usually displays a
standard package MIME icon in file managers. GDebi and some installers also
show their own generic package icon in the package overview, regardless of the
contents. There is no universal Debian control-file field that forces this
icon; graphical software centers may resolve AppStream icon/desktop metadata,
especially for packages in a software repository. Do not conflate the `.deb`
file's MIME icon with the installed application's icon.

## Flatpak

Flatpak installs the same standard icon under `/app/share/icons/hicolor/...`,
and the window icon comes from GResource. Run Flatpak icon checks only after a
successful Flatpak build. Note that the Flatpak manifest is still experimental.

## Plasma/Wayland window icon (fix in Dakkar 1.0-3)

On GTK 3, `g_set_prgname ("org.dakkar.Files")` is essential: Plasma uses the
Wayland `xdg_toplevel.app_id` to resolve the installed `.desktop` file and its
`Icon=org.dakkar.Files` entry. Merely embedding the image with GTK's
`gtk_window_set_icon()` does **not** change the Wayland window identity.

X11 still uses `gdk_set_program_class ("dakkar")`, matching
`StartupWMClass=dakkar` in both desktop launchers.

Check installed launcher/icon after upgrading:

```sh
ls -l /usr/share/applications/org.dakkar.Files.desktop
ls -l /usr/share/icons/hicolor/128x128/apps/org.dakkar.Files.png
```

Close **all** Dakkar processes before reopening it via the application menu.
Do not relaunch an already-running process using an old executable: GTK's
GApplication single-instance behaviour may pass requests to that old process.

To inspect a Wayland app ID when required, launch from a terminal with
`WAYLAND_DEBUG=client` and search for `xdg_toplevel.set_app_id`. X11 users can
use `xprop WM_CLASS` and click the Dakkar window. A running `dakkar` process
must originate from the newly installed package, not an old user build.
