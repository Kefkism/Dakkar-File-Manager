/* Dakkar: shared preview helpers for the details sidebar of every view
 * (date format, multi-selection summary, inline video player, Quick Look).
 * GPL-2.0-or-later. */
#ifndef DAKKAR_PREVIEW_H
#define DAKKAR_PREVIEW_H

#include <gtk/gtk.h>
#include <libnemo-private/nemo-file.h>

/* "YYYY-MM-DD HH:MM" modification time, or NULL. */
gchar *dakkar_preview_format_date (NemoFile *file);

/* Summary for a multi-selection: "N items selected" and total size of the
 * selected files (folders are counted but not recursed). */
void dakkar_preview_multi_summary (GList *selection, gchar **title, gchar **size);

/* Small muted video player: first frame paused, click Play to start. */
GtkWidget *dakkar_video_new (gint width);
/* TRUE when the GStreamer elements for the inline player are installed. */
gboolean dakkar_video_supported (void);
void dakkar_video_set_uri (GtkWidget *video, const char *uri);   /* NULL: stop */

/* Quick Look window (Space): large image, text, video with sound. */
void dakkar_quick_look_toggle (GtkWidget *anchor, NemoFile *file);
void dakkar_quick_look_close (void);
gboolean dakkar_quick_look_is_open (void);

#endif
