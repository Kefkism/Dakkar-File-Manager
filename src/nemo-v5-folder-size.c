/* Copyright (C) 2026 Files contributors; GPL-2.0-or-later
 * Bounded folder-size calculation.  Never follows symlinks, never scans
 * non-native roots, and never blocks the GTK main loop. */
#include <config.h>
#include <glib/gi18n.h>
#include "nemo-v5-folder-size.h"

#define FILES_SIZE_MAX_ENTRIES 20000
#define FILES_SIZE_MAX_DEPTH 24
#define FILES_SIZE_MAX_USEC (3 * G_USEC_PER_SEC)

typedef struct {
    GFile *file;
    guint depth;
} SizePath;

static void
size_path_free (SizePath *item)
{
    g_object_unref (item->file);
    g_free (item);
}

static void
size_worker (GTask *task, gpointer source_object, gpointer unused,
             GCancellable *cancel)
{
    GQueue queue = G_QUEUE_INIT;
    GFile *root = G_FILE (source_object);
    NemoV5FolderSize *result = g_new0 (NemoV5FolderSize, 1);
    gint64 started = g_get_monotonic_time ();
    GFileInfo *root_info;
    GFileInfo *fs_info;
    guint32 root_device = 0;
    gboolean has_device = FALSE;
    const char *fs_type;
    SizePath *head;

    /* Conservatively reject known network mounts even if their paths are
     * locally addressable. Local bind mounts on another device are not
     * recursively crossed either. */
    fs_info = g_file_query_filesystem_info (root, "filesystem::type", cancel, NULL);
    fs_type = fs_info ? g_file_info_get_attribute_string (fs_info, "filesystem::type") : NULL;
    if (fs_type != NULL &&
        (g_str_has_prefix (fs_type, "nfs") || g_str_has_prefix (fs_type, "cifs") ||
         g_str_has_prefix (fs_type, "smb") || g_str_has_prefix (fs_type, "9p") ||
         g_str_has_prefix (fs_type, "fuse.sshfs") ||
         g_str_has_prefix (fs_type, "fuse.rclone") ||
         g_str_has_prefix (fs_type, "davfs"))) {
        g_clear_object (&fs_info);
        g_free (result);
        g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                                 _("Network filesystem excluded from automatic size scan"));
        return;
    }
    g_clear_object (&fs_info);
    root_info = g_file_query_info (root,
                                   G_FILE_ATTRIBUTE_UNIX_DEVICE ","
                                   G_FILE_ATTRIBUTE_STANDARD_TYPE,
                                   G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
                                   cancel, NULL);
    if (root_info != NULL &&
        g_file_info_get_file_type (root_info) == G_FILE_TYPE_SYMBOLIC_LINK) {
        g_object_unref (root_info);
        g_free (result);
        g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                                 _("Following a symbolic-link folder is disabled"));
        return;
    }
    if (root_info != NULL) {
        has_device = g_file_info_has_attribute (root_info,
                                                G_FILE_ATTRIBUTE_UNIX_DEVICE);
        if (has_device)
            root_device = g_file_info_get_attribute_uint32 (
                root_info, G_FILE_ATTRIBUTE_UNIX_DEVICE);
        g_object_unref (root_info);
    }
    head = g_new0 (SizePath, 1);
    head->file = g_object_ref (root);
    g_queue_push_tail (&queue, head);
    while (!g_queue_is_empty (&queue)) {
        SizePath *item;
        GFileEnumerator *enumerator;
        GError *error = NULL;
        if (g_cancellable_is_cancelled (cancel))
            break;
        if (result->entries >= FILES_SIZE_MAX_ENTRIES ||
            g_get_monotonic_time () - started >= FILES_SIZE_MAX_USEC) {
            result->partial = TRUE;
            break;
        }
        item = g_queue_pop_head (&queue);
        enumerator = g_file_enumerate_children (item->file,
            G_FILE_ATTRIBUTE_STANDARD_NAME ","
            G_FILE_ATTRIBUTE_STANDARD_TYPE ","
            G_FILE_ATTRIBUTE_STANDARD_SIZE ","
            G_FILE_ATTRIBUTE_UNIX_DEVICE,
            G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
            cancel, &error);
        if (enumerator == NULL) {
            /* Unreadable directories are reported as a partial total.
             * Cancellation is handled below, never presented as a value. */
            result->partial = TRUE;
        } else {
            GFileInfo *info;
            while ((info = g_file_enumerator_next_file (enumerator, cancel,
                                                         &error)) != NULL) {
                GFileType type = g_file_info_get_file_type (info);
                const char *name = g_file_info_get_name (info);
                result->entries++;
                if (item->depth == 0 && name != NULL && name[0] != '.' &&
                    !g_str_has_suffix (name, "~"))
                    result->direct++;
                if (type == G_FILE_TYPE_DIRECTORY && name != NULL) {
                    if (has_device &&
                        g_file_info_has_attribute (info, G_FILE_ATTRIBUTE_UNIX_DEVICE) &&
                        g_file_info_get_attribute_uint32 (info,
                          G_FILE_ATTRIBUTE_UNIX_DEVICE) != root_device) {
                        result->partial = TRUE;
                    } else if (item->depth < FILES_SIZE_MAX_DEPTH) {
                        SizePath *child = g_new0 (SizePath, 1);
                        child->file = g_file_get_child (item->file, name);
                        child->depth = item->depth + 1;
                        g_queue_push_tail (&queue, child);
                    } else {
                        result->partial = TRUE;
                    }
                } else if (g_file_info_has_attribute (info,
                             G_FILE_ATTRIBUTE_STANDARD_SIZE)) {
                    goffset size = g_file_info_get_size (info);
                    if (size > 0) {
                        /* Saturate instead of wrapping on huge or invalid IO. */
                        guint64 add = (guint64) size;
                        if (G_MAXUINT64 - result->bytes < add) {
                            result->bytes = G_MAXUINT64;
                            result->partial = TRUE;
                        } else {
                            result->bytes += add;
                        }
                    }
                }
                g_object_unref (info);
                if (result->entries >= FILES_SIZE_MAX_ENTRIES ||
                    g_cancellable_is_cancelled (cancel) ||
                    g_get_monotonic_time () - started >= FILES_SIZE_MAX_USEC) {
                    result->partial = TRUE;
                    break;
                }
            }
            /* Use NULL cancellable for closing after cancellation. */
            g_file_enumerator_close (enumerator, NULL, NULL);
            g_object_unref (enumerator);
            if (error != NULL) result->partial = TRUE;
        }
        g_clear_error (&error);
        size_path_free (item);
    }
    while (!g_queue_is_empty (&queue))
        size_path_free (g_queue_pop_head (&queue));
    if (g_cancellable_is_cancelled (cancel)) {
        g_free (result);
        g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_CANCELLED,
                                 _("Folder size scan cancelled"));
    } else {
        g_task_return_pointer (task, result, g_free);
    }
}

void
nemo_v5_folder_size_async (GFile *folder, GCancellable *cancel,
                           GAsyncReadyCallback callback, gpointer user_data)
{
    GTask *task;
    g_return_if_fail (G_IS_FILE (folder));
    task = g_task_new (folder, cancel, callback, user_data);
    g_task_set_priority (task, G_PRIORITY_LOW);
    if (!g_file_is_native (folder)) {
        g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                                 _("Remote locations are excluded from automatic size scans"));
    } else {
        g_task_run_in_thread (task, size_worker);
    }
    g_object_unref (task);
}

NemoV5FolderSize *
nemo_v5_folder_size_finish (GFile *folder, GAsyncResult *result,
                            GError **error)
{
    g_return_val_if_fail (g_task_is_valid (result, folder), NULL);
    return g_task_propagate_pointer (G_TASK (result), error);
}
