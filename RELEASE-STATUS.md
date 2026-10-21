# Dakkar 1.0 — release status (2026-10-21)

## Changes
- Own application identity and icon: `org.dakkar.Files`, `Name=Dakkar`.
- Own `org.dakkar.*` GSettings schema IDs and `/org/dakkar/...` keys, isolated from Nemo dconf.
- Distinct D-Bus file-operations interface, and no attempt to own `org.freedesktop.FileManager1`.
- No built `nemo-desktop` or `nemo-autorun-software`; no globally installed Nemo policy, activators, MIME XML or man pages.
- Private code/resources under `/opt/dakkar` for `.deb`/Arch or a user prefix for the `.sh` installation; exported only Dakkar names into `/usr`.
- The X11 Open as Root menu calls GVfs `admin://` instead of spawning Nemo.
- User data and bookmark metadata use Dakkar paths.
- Upstream code symbols and copyright attributions remain intact.

## Verification performed
- Shell syntax (`bash -n`) and Python syntax validated.
- XML parsed (GSettings, AppStream, GResource, D-Bus).
- Flatpak JSON parsed.
- Static identity checks passed.
- Local AUR source archive generated and SHA256 verified for repeatability.
- ZIP checksum/integrity validated after packaging.

## NOT verified / release blockers
- **Native compilation has NOT been performed**: Meson and the Gtk/Cinnamon/XApp development libraries are missing from the available runtime.
- **No `.deb` executable package** or actual `.flatpak` binary has been built or launched.
- Real-world collision-free coexistence with installed Nemo is not certified until both apps are tested together in a VM.
- Debian runtime dependency names may differ among distro versions; re-evaluate per release.
- Flatpak dependency chain (Cinnamon Desktop / XApp / libgsf and additional stack) and sandbox/portals need work. The manifest is an experimental starting point, not a publishable Flathub manifest.
- AUR upstream source URL/checksum must be set to the *real, published* GitHub tag for AUR submission.
- The `admin://` backend depends on `gvfs-backends` and the desktop's privilege policy.

Run `python3 packaging/check-identity.py` and see `packaging/README.md` for packaging commands.
