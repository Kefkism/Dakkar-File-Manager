#!/usr/bin/env python3
from pathlib import Path
import re
root=Path(__file__).resolve().parent.parent
assert 'Name=Dakkar' in (root/'packaging/common/org.dakkar.Files.desktop').read_text()
# Wayland: the GTK 3 program name must match the desktop-file basename;
# X11: the window class must match StartupWMClass in the launcher.
main=(root/'src/nemo-main.c').read_text()
assert 'g_set_prgname ("org.dakkar.Files");' in main
assert 'gdk_set_program_class ("dakkar");' in main
for desktop in (root/'packaging/common/org.dakkar.Files.desktop',
                root/'data/org.dakkar.Files.desktop.in'):
    entry=desktop.read_text()
    assert 'Icon=org.dakkar.Files' in entry
    assert 'StartupWMClass=dakkar' in entry
assert '"application-id", "org.dakkar.Files"' in (root/'src/nemo-main-application.c').read_text()
assert 'org.dakkar.preferences' in (root/'libnemo-private/org.dakkar.gschema.xml').read_text()
assert '/org/dakkar/preferences/' in (root/'libnemo-private/org.dakkar.gschema.xml').read_text()
assert 'org.nemo.preferences' not in (root/'libnemo-private/org.dakkar.gschema.xml').read_text()
assert not (root/'libnemo-private/org.nemo.gschema.xml').exists()
src=(root/'src/meson.build').read_text()
for name in ('nemo-desktop', 'nemo-autorun-software'):
    assert "executable('"+name+"'" not in src
assert 'nemo_freedesktop_dbus_new ();' not in (root/'src/nemo-main-application.c').read_text()
assert 'org.dakkar.FileOperations' in (root/'data/dbus-interfaces.xml').read_text()
assert 'g_settings_new("org.dakkar.preferences")' in (root/'libnemo-private/nemo-global-preferences.c').read_text()
print('Static identity checks: PASS (native compilation and collision testing not performed)')
