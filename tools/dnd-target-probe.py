#!/usr/bin/env python3
"""Minimal XDND drop target (Chromium-like): logs offered targets and the
bytes received for text/uri-list (plus every other offered target)."""
import sys, gi
gi.require_version('Gtk', '3.0'); gi.require_version('Gdk', '3.0')
from gi.repository import Gtk, Gdk, GLib
log = open(sys.argv[1], 'a', buffering=1)
w = Gtk.Window(title='dnd-target'); w.move(int(sys.argv[2]), int(sys.argv[3])); w.set_default_size(260, 220)
lab = Gtk.Label(label='DROP HERE'); w.add(lab)
w.drag_dest_set(0, [], Gdk.DragAction.COPY | Gdk.DragAction.MOVE | Gdk.DragAction.LINK)
state = {}
def on_motion(wid, ctx, x, y, t):
    Gdk.drag_status(ctx, Gdk.DragAction.COPY, t); return True
def on_drop(wid, ctx, x, y, t):
    names = [str(a) for a in ctx.list_targets()]
    log.write('DROP targets=%s\n' % names)
    state['pending'] = list(names); state['ctx'] = ctx; state['t'] = t
    order = ['text/uri-list'] + [n for n in names if n != 'text/uri-list']
    state['order'] = [n for n in order if n in names]
    req(wid); return True
def req(wid):
    if not state['order']:
        Gtk.drag_finish(state['ctx'], True, False, state['t']); log.write('END\n'); return
    n = state['order'].pop(0)
    wid.drag_get_data(state['ctx'], Gdk.Atom.intern(n, False), state['t'])
def on_data(wid, ctx, x, y, sd, info, t):
    data = sd.get_data()
    log.write('DATA %s len=%d %r\n' % (sd.get_target(), len(data) if data else 0, bytes(data or b'')[:300]))
    GLib.idle_add(lambda: req(wid) and False)
w.connect('drag-motion', on_motion); w.connect('drag-drop', on_drop); w.connect('drag-data-received', on_data)
w.connect('destroy', Gtk.main_quit); w.show_all(); Gtk.main()
