/* nemo-compress-dialog.h - Dakkar "Compress…" dialog and archive actions.
 * GPL-2.0-or-later. */
#ifndef NEMO_COMPRESS_DIALOG_H
#define NEMO_COMPRESS_DIALOG_H

#include <gtk/gtk.h>

/* TRUE when Compress / Extract Here run in process (built with libarchive). */
gboolean nemo_compress_available (void);
/* All @files (GFile) are local items that can go into an archive. */
gboolean nemo_compress_can_compress (GList *files);
/* All @files (GFile) are local archives libarchive can read. */
gboolean nemo_compress_can_extract (GList *files);
/* Ask name, destination and format, then start the compress job; progress
 * is shown in the transfer window. @files is a list of GFile. */
void nemo_compress_dialog_show (GtkWindow *parent, GList *files);
/* Extract every archive into a new folder next to it. */
/* Asks for a folder (and whether each archive gets its own folder),
 * then extracts @files (archives) there. */
void nemo_compress_extract_to (GtkWindow *parent, GList *files);
void nemo_compress_extract_here (GtkWindow *parent, GList *files);

#endif
