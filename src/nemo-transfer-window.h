/* nemo-transfer-window.h - Dakkar detailed transfer window and summary.
 *
 * Copyright (C) 2026 Dakar contributors
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */
#ifndef NEMO_TRANSFER_WINDOW_H
#define NEMO_TRANSFER_WINDOW_H

#include <gtk/gtk.h>
#include <libnemo-private/nemo-progress-info.h>

/* Track a job that has been visible for a while (the progress UI handler's
 * usual 2 second delay). show_window: open the detailed window right away,
 * otherwise the job starts collapsed into the status bar. */
void      nemo_transfer_window_track (NemoProgressInfo *info, gboolean show_window);

/* Watch a job from the moment it is queued (summary for short jobs). */
/* Lets jobs bring their window up for a question (archive passwords). */
void      nemo_transfer_window_init_prompts (void);
void      nemo_transfer_window_watch (NemoProgressInfo *info);

/* Present every active detailed transfer window; FALSE if there is none. */
gboolean  nemo_transfer_window_present_all (void);

/* The toolbar "File Transfers" button: present (show, un-minimize, raise)
 * every active transfer window; with no transfer running show a small
 * "No active transfers" window with the last finished transfer, if any. */
void      nemo_transfer_window_show (GtkWindow *parent);

/* TRUE while any file operation is queued or running. */
gboolean  nemo_transfer_window_is_busy (void);

/* @widget is redrawn whenever nemo_transfer_window_is_busy() may have
 * changed (held weakly; nothing to undo when it is destroyed). */
void      nemo_transfer_window_add_activity_widget (GtkWidget *widget);

/* Pause all active jobs, or resume them if all are paused. */
void      nemo_transfer_window_toggle_pause_all (void);

/* Combined state of all tracked, unfinished jobs for the status bar.
 * Returns NULL when nothing is running. */
gchar    *nemo_transfer_window_dup_caption (gint width, gdouble *fraction,
                                            gboolean *paused, gboolean *can_pause);

/* Pure helpers, exposed for the summary/log code and tests. */
/* State for the status-bar bar: verifying (single transfer in the read-back
 * phase, with that file's checksum fraction) and the number of failed files
 * so far across all tracked transfers. */
void      nemo_transfer_window_get_bar_state (gboolean *verifying,
                                              gdouble *verify_fraction,
                                              guint *failed);
gchar    *nemo_transfer_format_duration (gint64 microseconds);

#endif
