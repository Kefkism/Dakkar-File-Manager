/* nemo-terminal.h - the one terminal launcher shared by every
 * "Open Terminal" / "Open Terminal as Root" entry point.
 *
 * Copyright (C) 2026 Dakar contributors
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 */

#ifndef NEMO_TERMINAL_H
#define NEMO_TERMINAL_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

/* Label for the entry points: "Open Terminal Here",
 * "Open Terminal in Enclosing Folder", "Open Terminal as Root Here" or
 * "Open Terminal as Root in Enclosing Folder" (already translated). */
const gchar *nemo_terminal_get_label   (gboolean as_root,
                                        gboolean enclosing_folder);
const gchar *nemo_terminal_get_tooltip (gboolean as_root,
                                        gboolean enclosing_folder);

/* Build the argv used to open a terminal in @directory (and, with
 * @as_root, run pkexec /bin/bash in it).  Returns NULL and sets @error
 * when no terminal could be found.  Exposed for the tests. */
gchar      **nemo_terminal_build_argv  (const gchar *directory,
                                        gboolean     as_root,
                                        GError     **error);

/* Open a terminal in @directory, a local folder.  With @defer the spawn
 * happens from an idle callback (Miller context menus must be gone
 * first).  Failures are shown in an error dialog on @parent; the
 * function returns FALSE when the launch failed synchronously. */
gboolean     nemo_terminal_open        (GtkWindow   *parent,
                                        const gchar *directory,
                                        gboolean     as_root,
                                        gboolean     defer);

G_END_DECLS

#endif /* NEMO_TERMINAL_H */
