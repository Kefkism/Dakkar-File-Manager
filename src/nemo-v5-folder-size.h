/* Bounded, non-recursive-in-main-loop directory size service.
 * This API is shared by all Nemo view types, not just Miller Columns. */
#ifndef NEMO_V5_FOLDER_SIZE_H
#define NEMO_V5_FOLDER_SIZE_H
#include <gio/gio.h>

typedef struct {
    guint64 bytes;
    guint entries;      /* recursive, every level */
    guint direct;       /* direct children, hidden (dot/backup) excluded */
    gboolean partial;
} NemoV5FolderSize;

void nemo_v5_folder_size_async (GFile *folder, GCancellable *cancel,
                                GAsyncReadyCallback callback, gpointer user_data);
NemoV5FolderSize *nemo_v5_folder_size_finish (GFile *folder, GAsyncResult *result,
                                               GError **error);
#endif
