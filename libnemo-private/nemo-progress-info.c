/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*-

   nemo-progress-info.h: file operation progress info.
 
   Copyright (C) 2007 Red Hat, Inc.
  
   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.
  
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.
  
   You should have received a copy of the GNU General Public
   License along with this program; if not, write to the
   Free Software Foundation, Inc., 51 Franklin Street - Suite 500,
   Boston, MA 02110-1335, USA.
  
   Author: Alexander Larsson <alexl@redhat.com>
*/

#include <config.h>
#include <math.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <glib/gstdio.h>
#include <glib/gi18n.h>
#include <eel/eel-string.h>
#include <eel/eel-glib-extensions.h>
#include "nemo-progress-info.h"
#include "nemo-progress-info-manager.h"

enum {
  CHANGED,
  QUEUED,
  PROGRESS_CHANGED,
  STARTED,
  FINISHED,
  LAST_SIGNAL
};

#define SIGNAL_DELAY_MSEC 100
#define DAKKAR_MAX_MANIFEST_ENTRIES 8192

static void queue_idle (NemoProgressInfo *info, gboolean now);

static guint signals[LAST_SIGNAL] = { 0 };

struct _NemoProgressInfo
{
	GObject parent_instance;
	
	GCancellable *cancellable;
	
	char *status;
	char *details;
    char *initial_details;
    GFile *destination;
    gchar *job_id;  /* stable operation identity for future history/retry */
    NemoTransferOperation operation;
    gboolean has_byte_progress;
    NemoTransferByteStats byte_stats;
    gint64 bytes_started_us;
    gint64 bytes_sample_us;
    goffset bytes_sample_value;
	double progress;
    gboolean progress_known; /* no percent before the backend reports a total */
	gboolean activity_mode;
	gboolean started;
	gboolean finished;
	gboolean paused;
    gboolean queued;
    /* The progress object IS the canonical job record; UI reads snapshots. */
    GPtrArray *manifest;         /* owns DakkarFileEntry instances */
    GHashTable *manifest_by_source;  /* borrowed GFile* keys and entry values */
    gboolean manifest_truncated;

    /* Separate from the existing 'paused' flag used around modal dialogs. */
    GMutex hold_mutex;
    GCond hold_cond;
    gboolean hold_requested;
    gboolean hold_waiting;
    gulong hold_cancel_handler;
    gboolean skip_wakeup;        /* under hold_mutex: Skip ends a pause wait */

    /* Per-file cancellable for "Skip current file" (under progress lock). */
    GCancellable *file_cancellable;
    gulong file_cancel_handler;
    gboolean skip_requested;
    guint files_done;
    guint files_total;
    gint64 started_us;
    gint64 finished_us;
    gchar *checksum_name;
	
	GSource *idle_source;
	gboolean source_is_now;
	
	gboolean start_at_idle;
	gboolean finish_at_idle;
	gboolean changed_at_idle;
	gboolean progress_at_idle;
    gboolean queue_at_idle;
};

struct _NemoProgressInfoClass
{
	GObjectClass parent_class;
};

typedef struct {
    GFile *source;
    GFile *destination;
    goffset transferred;
    goffset total;
    NemoTransferFileState state;
    gchar *error;
    gchar *sha256;
    gboolean overwritten;
    goffset verified;
} DakkarFileEntry;

static void
dakkar_file_entry_free (gpointer data)
{
    DakkarFileEntry *entry = data;
    g_clear_object (&entry->source);
    g_clear_object (&entry->destination);
    g_free (entry->error);
    g_free (entry->sha256);
    g_free (entry);
}

G_LOCK_DEFINE_STATIC(progress_info);

G_DEFINE_TYPE (NemoProgressInfo, nemo_progress_info, G_TYPE_OBJECT)

static void
nemo_progress_info_finalize (GObject *object)
{
	NemoProgressInfo *info;
	
	info = NEMO_PROGRESS_INFO (object);

	g_free (info->status);
	g_free (info->details);
    g_free (info->initial_details);
    g_clear_object (&info->destination);
    g_clear_pointer (&info->job_id, g_free);
    g_clear_pointer (&info->checksum_name, g_free);
    g_clear_object (&info->file_cancellable);
    if (info->manifest_by_source != NULL)
        g_hash_table_unref (info->manifest_by_source);
    if (info->manifest != NULL)
        g_ptr_array_unref (info->manifest);
    if (info->hold_cancel_handler != 0)
        g_cancellable_disconnect (info->cancellable, info->hold_cancel_handler);
    g_cond_clear (&info->hold_cond);
    g_mutex_clear (&info->hold_mutex);
	g_object_unref (info->cancellable);
	
	if (G_OBJECT_CLASS (nemo_progress_info_parent_class)->finalize) {
		(*G_OBJECT_CLASS (nemo_progress_info_parent_class)->finalize) (object);
	}
}

static void
nemo_progress_info_dispose (GObject *object)
{
	NemoProgressInfo *info;
	
	info = NEMO_PROGRESS_INFO (object);

	G_LOCK (progress_info);

	/* Destroy source in dispose, because the callback
	   could come here before the destroy, which should
	   ressurect the object for a while */
	if (info->idle_source) {
		g_source_destroy (info->idle_source);
		g_source_unref (info->idle_source);
		info->idle_source = NULL;
	}
	G_UNLOCK (progress_info);
}

static void
nemo_progress_info_class_init (NemoProgressInfoClass *klass)
{
	GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
	
	gobject_class->finalize = nemo_progress_info_finalize;
	gobject_class->dispose = nemo_progress_info_dispose;
	
	signals[CHANGED] =
		g_signal_new ("changed",
			      NEMO_TYPE_PROGRESS_INFO,
			      G_SIGNAL_RUN_LAST,
			      0,
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);
	
    signals[QUEUED] =
        g_signal_new ("queued",
                  NEMO_TYPE_PROGRESS_INFO,
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);

	signals[PROGRESS_CHANGED] =
		g_signal_new ("progress-changed",
			      NEMO_TYPE_PROGRESS_INFO,
			      G_SIGNAL_RUN_LAST,
			      0,
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);
	
	signals[STARTED] =
		g_signal_new ("started",
			      NEMO_TYPE_PROGRESS_INFO,
			      G_SIGNAL_RUN_LAST,
			      0,
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);
	
	signals[FINISHED] =
		g_signal_new ("finished",
			      NEMO_TYPE_PROGRESS_INFO,
			      G_SIGNAL_RUN_LAST,
			      0,
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);
	
}

static void
dakkar_hold_cancelled (GCancellable *cancellable, gpointer user_data)
{
    NemoProgressInfo *info = user_data;
    g_mutex_lock (&info->hold_mutex);
    info->hold_requested = FALSE;
    g_cond_broadcast (&info->hold_cond);
    g_mutex_unlock (&info->hold_mutex);
    (void)cancellable;
}

static void
nemo_progress_info_init (NemoProgressInfo *info)
{
	NemoProgressInfoManager *manager;

	info->cancellable = g_cancellable_new ();
    info->job_id = g_uuid_string_random ();
    info->operation = NEMO_TRANSFER_OP_OTHER;
    g_mutex_init (&info->hold_mutex);
    g_cond_init (&info->hold_cond);
    info->manifest = g_ptr_array_new_with_free_func (dakkar_file_entry_free);
    info->manifest_by_source = g_hash_table_new (g_file_hash, (GEqualFunc)g_file_equal);
    info->hold_cancel_handler = g_cancellable_connect (info->cancellable,
        G_CALLBACK (dakkar_hold_cancelled), info, NULL);

	manager = nemo_progress_info_manager_new ();
	nemo_progress_info_manager_add_new_info (manager, info);
	g_object_unref (manager);
}

gchar *
nemo_progress_info_dup_job_id (NemoProgressInfo *info)
{
    gchar *result;
    g_return_val_if_fail (NEMO_IS_PROGRESS_INFO (info), NULL);
    G_LOCK (progress_info);
    result = g_strdup (info->job_id);
    G_UNLOCK (progress_info);
    return result;
}

void
nemo_progress_info_set_operation (NemoProgressInfo *info, NemoTransferOperation op)
{
    g_return_if_fail (NEMO_IS_PROGRESS_INFO (info));
    g_return_if_fail (op >= NEMO_TRANSFER_OP_OTHER && op <= NEMO_TRANSFER_OP_EXTRACT);
    G_LOCK (progress_info);
    /* Once executing, the native job must never silently become a different
     * kind of operation because a UI element changed or was reopened. */
    if (!info->started)
        info->operation = op;
    G_UNLOCK (progress_info);
}

NemoTransferOperation
nemo_progress_info_get_operation (NemoProgressInfo *info)
{
    NemoTransferOperation op;
    g_return_val_if_fail (NEMO_IS_PROGRESS_INFO (info), NEMO_TRANSFER_OP_OTHER);
    G_LOCK (progress_info);
    op = info->operation;
    G_UNLOCK (progress_info);
    return op;
}

/* Called in the native Nemo worker at the existing real-byte progress point.
 * Only the copied snapshot is exposed to GTK via thread-safe accessors. */
void
nemo_progress_info_report_bytes (NemoProgressInfo *info, goffset completed,
                                  goffset total)
{
    gint64 now;
    g_return_if_fail (NEMO_IS_PROGRESS_INFO (info));
    if (completed < 0 || total < 0) return;
    now = g_get_monotonic_time ();
    G_LOCK (progress_info);
    if (!info->has_byte_progress || completed < info->byte_stats.completed) {
        memset (&info->byte_stats, 0, sizeof (info->byte_stats));
        info->bytes_started_us = now;
        info->bytes_sample_us = now;
        info->bytes_sample_value = completed;
        info->has_byte_progress = TRUE;
        info->byte_stats.remaining_microseconds = -1;
    }
    info->byte_stats.completed = completed;
    info->byte_stats.total = MAX (total, completed);
    info->byte_stats.elapsed_microseconds = now - info->bytes_started_us;
    if (now - info->bytes_sample_us >= 250000) {
        gint64 delta_us = now - info->bytes_sample_us;
        goffset delta_bytes = MAX ((goffset)0, completed - info->bytes_sample_value);
        gdouble measured = (gdouble)delta_bytes * G_USEC_PER_SEC / delta_us;
        info->byte_stats.current_bytes_per_second = info->byte_stats.rate_available ?
            (info->byte_stats.current_bytes_per_second * 0.60 + measured * 0.40) : measured;
        info->byte_stats.rate_available = TRUE;
        info->bytes_sample_us = now;
        info->bytes_sample_value = completed;
    }
    if (info->byte_stats.elapsed_microseconds > 0)
        info->byte_stats.average_bytes_per_second =
            (gdouble)completed * G_USEC_PER_SEC / info->byte_stats.elapsed_microseconds;
    info->byte_stats.remaining_microseconds = -1;
    if (info->byte_stats.rate_available &&
        info->byte_stats.current_bytes_per_second > 0.01 &&
        info->byte_stats.total > completed) {
        gdouble estimate = (info->byte_stats.total - completed) *
            G_USEC_PER_SEC / info->byte_stats.current_bytes_per_second;
        if (estimate < G_MAXINT64)
            info->byte_stats.remaining_microseconds = (gint64)estimate;
    }
    G_UNLOCK (progress_info);
}

gboolean
nemo_progress_info_get_byte_stats (NemoProgressInfo *info,
                                   NemoTransferByteStats *out)
{
    gboolean valid;
    g_return_val_if_fail (NEMO_IS_PROGRESS_INFO (info) && out != NULL, FALSE);
    G_LOCK (progress_info);
    valid = info->has_byte_progress;
    if (valid) *out = info->byte_stats;
    G_UNLOCK (progress_info);
    return valid;
}

void
nemo_progress_info_set_destination (NemoProgressInfo *info, GFile *folder)
{
    g_return_if_fail (NEMO_IS_PROGRESS_INFO (info));
    G_LOCK (progress_info);
    g_set_object (&info->destination, folder);
    G_UNLOCK (progress_info);
}

GFile *
nemo_progress_info_get_destination (NemoProgressInfo *info)
{
    GFile *result;
    g_return_val_if_fail (NEMO_IS_PROGRESS_INFO (info), NULL);
    G_LOCK (progress_info);
    result = info->destination ? g_object_ref (info->destination) : NULL;
    G_UNLOCK (progress_info);
    return result;
}

/* The worker owns the original GFiles, the manifest owns references, and a
 * snapshot owns strings only. No GTK pointers are retained by a job. */
static DakkarFileEntry *
dakkar_find_or_begin_locked (NemoProgressInfo *info, GFile *source,
                            GFile *destination)
{
    DakkarFileEntry *entry = g_hash_table_lookup (info->manifest_by_source, source);
    if (entry == NULL && info->manifest->len < DAKKAR_MAX_MANIFEST_ENTRIES) {
        entry = g_new0 (DakkarFileEntry, 1);
        entry->source = g_object_ref (source);
        entry->total = -1;
        entry->state = NEMO_TRANSFER_FILE_RUNNING;
        g_ptr_array_add (info->manifest, entry);
        g_hash_table_insert (info->manifest_by_source, entry->source, entry);
    } else if (entry == NULL) {
        info->manifest_truncated = TRUE;
    }
    if (entry != NULL && destination != NULL)
        g_set_object (&entry->destination, destination);
    return entry;
}

static void
dakkar_manifest_notify_locked (NemoProgressInfo *info)
{
    info->changed_at_idle = TRUE;
    queue_idle (info, FALSE);
}

void
nemo_progress_info_file_begin (NemoProgressInfo *info, GFile *source,
                                GFile *destination)
{
    DakkarFileEntry *entry;
    g_return_if_fail (NEMO_IS_PROGRESS_INFO (info) && G_IS_FILE (source));
    G_LOCK (progress_info);
    entry = dakkar_find_or_begin_locked (info, source, destination);
    if (entry != NULL) {
        entry->transferred = 0;
        entry->total = -1;
        entry->state = NEMO_TRANSFER_FILE_RUNNING;
        entry->overwritten = FALSE;
        entry->verified = 0;
        g_clear_pointer (&entry->error, g_free);
        g_clear_pointer (&entry->sha256, g_free);
    }
    dakkar_manifest_notify_locked (info);
    G_UNLOCK (progress_info);
}

void
nemo_progress_info_file_progress (NemoProgressInfo *info, GFile *source,
                                   goffset transferred, goffset total)
{
    DakkarFileEntry *entry;
    g_return_if_fail (NEMO_IS_PROGRESS_INFO (info) && G_IS_FILE (source));
    if (transferred < 0 || total < 0) return;
    G_LOCK (progress_info);
    entry = dakkar_find_or_begin_locked (info, source, NULL);
    if (entry != NULL) {
        entry->transferred = transferred;
        entry->total = total;
        entry->state = NEMO_TRANSFER_FILE_RUNNING;
        dakkar_manifest_notify_locked (info);
    }
    G_UNLOCK (progress_info);
}

void
nemo_progress_info_file_end (NemoProgressInfo *info, GFile *source,
                              NemoTransferFileState state, const gchar *error)
{
    DakkarFileEntry *entry;
    g_return_if_fail (NEMO_IS_PROGRESS_INFO (info) && G_IS_FILE (source));
    G_LOCK (progress_info);
    entry = dakkar_find_or_begin_locked (info, source, NULL);
    if (entry != NULL) {
        /* Keep the genuine failed state after the user elects Skip; callers
         * may emit SKIPPED at the shared cleanup label afterwards. */
        if (entry->state == NEMO_TRANSFER_FILE_FAILED &&
            state == NEMO_TRANSFER_FILE_SKIPPED)
            state = NEMO_TRANSFER_FILE_FAILED;
        entry->state = state;
        /* Preserve the last real GIO failure when the user's resolution is
         * Skip. begin() resets it when the job retries successfully. */
        if (error != NULL) {
            g_free (entry->error);
            entry->error = g_strdup (error);
        }
        if (state == NEMO_TRANSFER_FILE_COMPLETED && entry->total >= 0)
            entry->transferred = entry->total;
        dakkar_manifest_notify_locked (info);
    }
    G_UNLOCK (progress_info);
}

void
nemo_progress_info_file_verification (NemoProgressInfo *info, GFile *source,
                                       gboolean matched, const gchar *sha256,
                                       const gchar *diagnostic)
{
    DakkarFileEntry *entry;
    g_return_if_fail (NEMO_IS_PROGRESS_INFO (info) && G_IS_FILE (source));
    /* A verification result without a genuine digest match is never VERIFIED. */
    g_return_if_fail (!matched || (sha256 != NULL && strlen (sha256) >= 32));
    G_LOCK (progress_info);
    entry = g_hash_table_lookup (info->manifest_by_source, source);
    if (entry != NULL && (entry->state == NEMO_TRANSFER_FILE_COMPLETED ||
                          entry->state == NEMO_TRANSFER_FILE_VERIFYING)) {
        entry->state = matched ? NEMO_TRANSFER_FILE_VERIFIED :
                                 NEMO_TRANSFER_FILE_VERIFY_FAILED;
        g_clear_pointer (&entry->sha256, g_free);
        if (matched) entry->sha256 = g_strdup (sha256);
        if (!matched) {
            g_free (entry->error);
            entry->error = g_strdup (diagnostic != NULL ? diagnostic :
                                     _("checksum mismatch"));
        }
        dakkar_manifest_notify_locked (info);
    }
    G_UNLOCK (progress_info);
}

/* Never claim success from a percentage alone. Files without a completed
 * manifest entry, an overflowed manifest, cancellation, or any unresolved
 * skipped/failed/verification-pending item suppress the success indicator. */
gboolean
nemo_progress_info_has_confirmed_success (NemoProgressInfo *info)
{
    guint i;
    gboolean success;
    g_return_val_if_fail (NEMO_IS_PROGRESS_INFO (info), FALSE);
    G_LOCK (progress_info);
    success = info->finished && !info->manifest_truncated &&
        info->manifest->len > 0 &&
        (info->operation == NEMO_TRANSFER_OP_COPY ||
         info->operation == NEMO_TRANSFER_OP_MOVE);
    if (success) {
        for (i = 0; i < info->manifest->len; i++) {
            DakkarFileEntry *entry = g_ptr_array_index (info->manifest, i);
            if (entry->state != NEMO_TRANSFER_FILE_COMPLETED &&
                entry->state != NEMO_TRANSFER_FILE_VERIFIED) {
                success = FALSE;
                break;
            }
        }
    }
    G_UNLOCK (progress_info);
    if (success) {
        GCancellable *cancellable = nemo_progress_info_get_cancellable (info);
        success = !g_cancellable_is_cancelled (cancellable);
        g_object_unref (cancellable);
    }
    return success;
}

/* A completed COPY may retain its UI only when there is at least one
 * observed file requiring a user decision. Move jobs have no auto retry. */
gboolean
nemo_progress_info_has_retryable_copies (NemoProgressInfo *info)
{
    guint i;
    gboolean found = FALSE;
    g_return_val_if_fail (NEMO_IS_PROGRESS_INFO (info), FALSE);
    G_LOCK (progress_info);
    if (info->operation == NEMO_TRANSFER_OP_COPY) {
        for (i = 0; i < info->manifest->len; i++) {
            DakkarFileEntry *entry = g_ptr_array_index (info->manifest, i);
            if (entry->destination != NULL &&
                (entry->state == NEMO_TRANSFER_FILE_FAILED ||
                 entry->state == NEMO_TRANSFER_FILE_SKIPPED ||
                 entry->state == NEMO_TRANSFER_FILE_VERIFY_FAILED)) {
                found = TRUE;
                break;
            }
        }
    }
    G_UNLOCK (progress_info);
    return found;
}

void
nemo_transfer_file_snapshot_free (NemoTransferFileSnapshot *entry)
{
    if (entry == NULL) return;
    g_free (entry->source_uri);
    g_free (entry->destination_uri);
    g_free (entry->error);
    g_free (entry->sha256);
    g_free (entry);
}

GPtrArray *
nemo_progress_info_dup_file_manifest (NemoProgressInfo *info,
                                      guint last_n,
                                      gboolean *truncated,
                                      guint *total_observed)
{
    GPtrArray *result = g_ptr_array_new_with_free_func (
        (GDestroyNotify)nemo_transfer_file_snapshot_free);
    guint i, first;
    g_return_val_if_fail (NEMO_IS_PROGRESS_INFO (info), result);
    G_LOCK (progress_info);
    if (total_observed != NULL) *total_observed = info->manifest->len;
    first = (last_n != 0 && info->manifest->len > last_n) ?
        info->manifest->len - last_n : 0;
    for (i = first; i < info->manifest->len; ++i) {
        DakkarFileEntry *entry = g_ptr_array_index (info->manifest, i);
        NemoTransferFileSnapshot *snap = g_new0 (NemoTransferFileSnapshot, 1);
        snap->source_uri = g_file_get_uri (entry->source);
        snap->destination_uri = entry->destination ?
            g_file_get_uri (entry->destination) : NULL;
        snap->transferred = entry->transferred;
        snap->total = entry->total;
        snap->state = entry->state;
        snap->error = g_strdup (entry->error);
        snap->sha256 = g_strdup (entry->sha256);
        snap->overwritten = entry->overwritten;
        snap->verified = entry->verified;
        g_ptr_array_add (result, snap);
    }
    if (truncated != NULL) *truncated = info->manifest_truncated;
    G_UNLOCK (progress_info);
    return result;
}

void
nemo_progress_info_file_queue (NemoProgressInfo *info, GFile *source,
                                GFile *destination)
{
    DakkarFileEntry *entry;
    g_return_if_fail (NEMO_IS_PROGRESS_INFO (info) && G_IS_FILE (source));
    G_LOCK (progress_info);
    if (g_hash_table_lookup (info->manifest_by_source, source) == NULL) {
        entry = dakkar_find_or_begin_locked (info, source, destination);
        if (entry != NULL)
            entry->state = NEMO_TRANSFER_FILE_QUEUED;
        dakkar_manifest_notify_locked (info);
    }
    G_UNLOCK (progress_info);
}

/* Dakkar: the file ended up somewhere else than where it was written
 * (Extract To… moves files in from a hidden folder). */
void
nemo_progress_info_file_set_destination (NemoProgressInfo *info, GFile *source,
                                          GFile *destination)
{
    DakkarFileEntry *entry;
    g_return_if_fail (NEMO_IS_PROGRESS_INFO (info) && G_IS_FILE (source) && G_IS_FILE (destination));
    G_LOCK (progress_info);
    entry = g_hash_table_lookup (info->manifest_by_source, source);
    if (entry != NULL) {
        g_set_object (&entry->destination, destination);
        dakkar_manifest_notify_locked (info);
    }
    G_UNLOCK (progress_info);
}

void
nemo_progress_info_file_set_overwritten (NemoProgressInfo *info, GFile *source,
                                          gboolean overwritten)
{
    DakkarFileEntry *entry;
    g_return_if_fail (NEMO_IS_PROGRESS_INFO (info) && G_IS_FILE (source));
    G_LOCK (progress_info);
    entry = g_hash_table_lookup (info->manifest_by_source, source);
    if (entry != NULL) {
        entry->overwritten = overwritten;
        dakkar_manifest_notify_locked (info);
    }
    G_UNLOCK (progress_info);
}

void
nemo_progress_info_file_verify_progress (NemoProgressInfo *info, GFile *source,
                                          goffset done, goffset total)
{
    DakkarFileEntry *entry;
    g_return_if_fail (NEMO_IS_PROGRESS_INFO (info) && G_IS_FILE (source));
    G_LOCK (progress_info);
    entry = g_hash_table_lookup (info->manifest_by_source, source);
    if (entry != NULL) {
        entry->state = NEMO_TRANSFER_FILE_VERIFYING;
        entry->verified = done;
        if (total >= 0) entry->total = total;
        dakkar_manifest_notify_locked (info);
    }
    G_UNLOCK (progress_info);
}

void
nemo_progress_info_report_files (NemoProgressInfo *info, guint done, guint total)
{
    g_return_if_fail (NEMO_IS_PROGRESS_INFO (info));
    G_LOCK (progress_info);
    info->files_done = done;
    info->files_total = MAX (total, done);
    G_UNLOCK (progress_info);
}

void
nemo_progress_info_get_file_counts (NemoProgressInfo *info, guint *done, guint *total)
{
    g_return_if_fail (NEMO_IS_PROGRESS_INFO (info));
    G_LOCK (progress_info);
    if (done != NULL) *done = info->files_done;
    if (total != NULL) *total = info->files_total;
    G_UNLOCK (progress_info);
}

static void
dakkar_cancel_file_cancellable (GCancellable *job_cancellable, gpointer user_data)
{
    g_cancellable_cancel (G_CANCELLABLE (user_data));
    (void) job_cancellable;
}

/* One cancellable per file: cancelling it skips just that file, while the
 * job cancellable (Cancel) is chained into it and stops everything. */
GCancellable *
nemo_progress_info_begin_file_cancellable (NemoProgressInfo *info)
{
    GCancellable *c;
    g_return_val_if_fail (NEMO_IS_PROGRESS_INFO (info), NULL);
    c = g_cancellable_new ();
    G_LOCK (progress_info);
    g_clear_object (&info->file_cancellable);
    info->file_cancellable = g_object_ref (c);
    info->skip_requested = FALSE;
    G_UNLOCK (progress_info);
    info->file_cancel_handler = g_cancellable_connect (info->cancellable,
        G_CALLBACK (dakkar_cancel_file_cancellable), g_object_ref (c), g_object_unref);
    return c;
}

gboolean
nemo_progress_info_end_file_cancellable (NemoProgressInfo *info,
                                         GCancellable *file_cancellable)
{
    gboolean skipped;
    g_return_val_if_fail (NEMO_IS_PROGRESS_INFO (info), FALSE);
    if (info->file_cancel_handler != 0) {
        g_cancellable_disconnect (info->cancellable, info->file_cancel_handler);
        info->file_cancel_handler = 0;
    }
    G_LOCK (progress_info);
    skipped = info->skip_requested && !g_cancellable_is_cancelled (info->cancellable);
    info->skip_requested = FALSE;
    if (info->file_cancellable == file_cancellable)
        g_clear_object (&info->file_cancellable);
    G_UNLOCK (progress_info);
    g_mutex_lock (&info->hold_mutex);
    info->skip_wakeup = FALSE;
    g_mutex_unlock (&info->hold_mutex);
    g_clear_object (&file_cancellable);
    return skipped;
}

gboolean
nemo_progress_info_can_skip (NemoProgressInfo *info)
{
    gboolean result;
    g_return_val_if_fail (NEMO_IS_PROGRESS_INFO (info), FALSE);
    G_LOCK (progress_info);
    result = info->started && !info->finished && info->file_cancellable != NULL;
    G_UNLOCK (progress_info);
    return result;
}

gboolean
nemo_progress_info_request_skip (NemoProgressInfo *info)
{
    GCancellable *c = NULL;
    g_return_val_if_fail (NEMO_IS_PROGRESS_INFO (info), FALSE);
    G_LOCK (progress_info);
    if (!info->finished && info->file_cancellable != NULL) {
        info->skip_requested = TRUE;
        c = g_object_ref (info->file_cancellable);
    }
    G_UNLOCK (progress_info);
    if (c == NULL) return FALSE;
    g_mutex_lock (&info->hold_mutex);
    info->skip_wakeup = TRUE;
    g_cond_broadcast (&info->hold_cond);
    g_mutex_unlock (&info->hold_mutex);
    g_cancellable_cancel (c);
    g_object_unref (c);
    return TRUE;
}

gint64
nemo_progress_info_get_elapsed_us (NemoProgressInfo *info)
{
    gint64 result;
    g_return_val_if_fail (NEMO_IS_PROGRESS_INFO (info), 0);
    G_LOCK (progress_info);
    if (info->started_us == 0) result = 0;
    else result = (info->finished_us != 0 ? info->finished_us : g_get_monotonic_time ())
                  - info->started_us;
    G_UNLOCK (progress_info);
    return result;
}

void
nemo_progress_info_set_checksum_name (NemoProgressInfo *info, const gchar *name)
{
    g_return_if_fail (NEMO_IS_PROGRESS_INFO (info));
    G_LOCK (progress_info);
    g_free (info->checksum_name);
    info->checksum_name = g_strdup (name);
    G_UNLOCK (progress_info);
}

gchar *
nemo_progress_info_dup_checksum_name (NemoProgressInfo *info)
{
    gchar *result;
    g_return_val_if_fail (NEMO_IS_PROGRESS_INFO (info), NULL);
    G_LOCK (progress_info);
    result = g_strdup (info->checksum_name);
    G_UNLOCK (progress_info);
    return result;
}

gboolean
nemo_progress_info_request_hold (NemoProgressInfo *info)
{
    gboolean supported;
    g_return_val_if_fail (NEMO_IS_PROGRESS_INFO (info), FALSE);
    G_LOCK (progress_info);
    supported = !info->finished && info->started &&
        (info->operation == NEMO_TRANSFER_OP_COPY ||
         info->operation == NEMO_TRANSFER_OP_MOVE ||
         info->operation == NEMO_TRANSFER_OP_DELETE ||
         info->operation == NEMO_TRANSFER_OP_TRASH ||
         info->operation == NEMO_TRANSFER_OP_COMPRESS ||
         info->operation == NEMO_TRANSFER_OP_EXTRACT);
    G_UNLOCK (progress_info);
    if (!supported || g_cancellable_is_cancelled (info->cancellable)) return FALSE;
    g_mutex_lock (&info->hold_mutex);
    info->hold_requested = TRUE;
    g_mutex_unlock (&info->hold_mutex);
    G_LOCK (progress_info);
    dakkar_manifest_notify_locked (info);
    G_UNLOCK (progress_info);
    return TRUE;
}

void
nemo_progress_info_release_hold (NemoProgressInfo *info)
{
    g_return_if_fail (NEMO_IS_PROGRESS_INFO (info));
    g_mutex_lock (&info->hold_mutex);
    info->hold_requested = FALSE;
    g_cond_broadcast (&info->hold_cond);
    g_mutex_unlock (&info->hold_mutex);
    G_LOCK (progress_info);
    dakkar_manifest_notify_locked (info);
    G_UNLOCK (progress_info);
}

gboolean
nemo_progress_info_hold_requested (NemoProgressInfo *info,
                                    gboolean *actually_waiting)
{
    gboolean hold;
    g_return_val_if_fail (NEMO_IS_PROGRESS_INFO (info), FALSE);
    g_mutex_lock (&info->hold_mutex);
    hold = info->hold_requested;
    if (actually_waiting != NULL) *actually_waiting = info->hold_waiting;
    g_mutex_unlock (&info->hold_mutex);
    return hold;
}

gboolean
nemo_progress_info_wait_if_held (NemoProgressInfo *info)
{
    g_return_val_if_fail (NEMO_IS_PROGRESS_INFO (info), FALSE);
    g_mutex_lock (&info->hold_mutex);
    if (info->hold_requested) {
        info->hold_waiting = TRUE;
        /* Never acquire the progress lock while holding hold_mutex. */
        g_mutex_unlock (&info->hold_mutex);
        G_LOCK (progress_info);
        dakkar_manifest_notify_locked (info);
        G_UNLOCK (progress_info);
        g_mutex_lock (&info->hold_mutex);
    }
    while (info->hold_requested && !info->skip_wakeup &&
           !g_cancellable_is_cancelled (info->cancellable))
        g_cond_wait (&info->hold_cond, &info->hold_mutex);
    info->hold_waiting = FALSE;
    g_mutex_unlock (&info->hold_mutex);
    return !g_cancellable_is_cancelled (info->cancellable);
}

NemoProgressInfo *
nemo_progress_info_new (void)
{
	NemoProgressInfo *info;
	
	info = g_object_new (NEMO_TYPE_PROGRESS_INFO, NULL);
	
	return info;
}

char *
nemo_progress_info_get_status (NemoProgressInfo *info)
{
	char *res;
	
	G_LOCK (progress_info);
	
	if (info->status) {
		res = g_strdup (info->status);
	} else {
		res = g_strdup (_("Preparing"));
	}
	
	G_UNLOCK (progress_info);
	
	return res;
}

char *
nemo_progress_info_get_details (NemoProgressInfo *info)
{
	char *res;
	
	G_LOCK (progress_info);
	
	if (info->details) {
		res = g_strdup (info->details);
	} else {
		res = g_strdup (_("Preparing"));
	}
	
	G_UNLOCK (progress_info);

	return res;
}

char *
nemo_progress_info_get_initial_details (NemoProgressInfo *info)
{
    char *res;
    
    G_LOCK (progress_info);
    
    if (info->initial_details) {
        res = g_strdup (info->initial_details);
    } else {
        res = g_strdup (_("Preparing"));
    }
    
    G_UNLOCK (progress_info);

    return res;
}

double
nemo_progress_info_get_progress (NemoProgressInfo *info)
{
	double res;
	
	G_LOCK (progress_info);

	if (info->activity_mode || !info->progress_known) {
		res = -1.0;
	} else {
		res = info->progress;
	}
	
	G_UNLOCK (progress_info);
	
	return res;
}

void
nemo_progress_info_cancel (NemoProgressInfo *info)
{
	G_LOCK (progress_info);
	
	g_cancellable_cancel (info->cancellable);
	
	G_UNLOCK (progress_info);
}

GCancellable *
nemo_progress_info_get_cancellable (NemoProgressInfo *info)
{
	GCancellable *c;
	
	G_LOCK (progress_info);
	
	c = g_object_ref (info->cancellable);
	
	G_UNLOCK (progress_info);
	
	return c;
}

gboolean
nemo_progress_info_get_is_started (NemoProgressInfo *info)
{
	gboolean res;
	
	G_LOCK (progress_info);
	
	res = info->started;
	
	G_UNLOCK (progress_info);
	
	return res;
}

gboolean
nemo_progress_info_get_is_finished (NemoProgressInfo *info)
{
	gboolean res;
	
	G_LOCK (progress_info);
	
	res = info->finished;
	
	G_UNLOCK (progress_info);
	
	return res;
}

gboolean
nemo_progress_info_get_is_paused (NemoProgressInfo *info)
{
	gboolean res;
	
	G_LOCK (progress_info);
	
	res = info->paused;
	
	G_UNLOCK (progress_info);
	
	return res;
}

static gboolean
idle_callback (gpointer data)
{
	NemoProgressInfo *info = data;
	gboolean start_at_idle;
	gboolean finish_at_idle;
	gboolean changed_at_idle;
	gboolean progress_at_idle;
    gboolean queue_at_idle;
	GSource *source;

	source = g_main_current_source ();
	
	G_LOCK (progress_info);

	/* Protect agains races where the source has
	   been destroyed on another thread while it
	   was being dispatched.
	   Similar to what gdk_threads_add_idle does.
	*/
	if (g_source_is_destroyed (source)) {
		G_UNLOCK (progress_info);
		return FALSE;
	}

	/* We hadn't destroyed the source, so take a ref.
	 * This might ressurect the object from dispose, but
	 * that should be ok.
	 */
	g_object_ref (info);

	g_assert (source == info->idle_source);
	
	g_source_unref (source);
	info->idle_source = NULL;
	
	start_at_idle = info->start_at_idle;
	finish_at_idle = info->finish_at_idle;
	changed_at_idle = info->changed_at_idle;
	progress_at_idle = info->progress_at_idle;
    queue_at_idle = info->queue_at_idle;
	
	info->start_at_idle = FALSE;
	info->finish_at_idle = FALSE;
	info->changed_at_idle = FALSE;
	info->progress_at_idle = FALSE;
    info->queue_at_idle = FALSE;
	
	G_UNLOCK (progress_info);

    if (queue_at_idle) {
        g_signal_emit (info,
                   signals[QUEUED],
                   0);
    }

	if (start_at_idle) {
		g_signal_emit (info,
			       signals[STARTED],
			       0);
	}
	
	if (changed_at_idle) {
		g_signal_emit (info,
			       signals[CHANGED],
			       0);
	}
	
	if (progress_at_idle) {
		g_signal_emit (info,
			       signals[PROGRESS_CHANGED],
			       0);
	}
	
	if (finish_at_idle) {
		g_signal_emit (info,
			       signals[FINISHED],
			       0);
	}

	g_object_unref (info);
	
	return FALSE;
}

/* Called with lock held */
static void
queue_idle (NemoProgressInfo *info, gboolean now)
{
	if (info->idle_source == NULL ||
	    (now && !info->source_is_now)) {
		if (info->idle_source) {
			g_source_destroy (info->idle_source);
			g_source_unref (info->idle_source);
			info->idle_source = NULL;
		}
		
		info->source_is_now = now;
		if (now) {
			info->idle_source = g_idle_source_new ();
		} else {
			info->idle_source = g_timeout_source_new (SIGNAL_DELAY_MSEC);
		}
		g_source_set_callback (info->idle_source, idle_callback, info, NULL);
		g_source_attach (info->idle_source, NULL);
	}
}

void
nemo_progress_info_queue (NemoProgressInfo *info)
{
    G_LOCK (progress_info);
    
    if (!info->queued) {
        info->queued = TRUE;
        
        info->queue_at_idle = TRUE;
        queue_idle (info, TRUE);
    }
    
    G_UNLOCK (progress_info);
}

void
nemo_progress_info_pause (NemoProgressInfo *info)
{
	G_LOCK (progress_info);

	if (!info->paused) {
		info->paused = TRUE;
	}

	G_UNLOCK (progress_info);
}

void
nemo_progress_info_resume (NemoProgressInfo *info)
{
	G_LOCK (progress_info);

	if (info->paused) {
		info->paused = FALSE;
	}

	G_UNLOCK (progress_info);
}

void
nemo_progress_info_start (NemoProgressInfo *info)
{
	G_LOCK (progress_info);
	
	if (!info->started) {
		info->started = TRUE;
        info->started_us = g_get_monotonic_time ();
		
		info->start_at_idle = TRUE;
		queue_idle (info, TRUE);
	}
	
	G_UNLOCK (progress_info);
}

/* An opt-in, bounded local history of completed native jobs.
 * Each record is an independent keyfile (0600) so separate Nemo windows
 * never overwrite the same aggregate database.  Entries are not a restart
 * journal: they must NEVER be treated as instructions to redo an operation. */
static gint
dakkar_compare_history_names (gconstpointer left, gconstpointer right)
{
    const gchar *a = *(const gchar * const *) left;
    const gchar *b = *(const gchar * const *) right;
    return g_strcmp0 (a, b);
}

static void
dakkar_transfer_write_history (NemoProgressInfo *info)
{
    GSettings *settings;
    GPtrArray *files, *names;
    GKeyFile *record;
    GDir *directory;
    GFile *destination;
    gchar *folder, *filename, *path, *contents, *job_id, *uri;
    const gchar *name;
    guint i, observed = 0, completed = 0, verified = 0, failed = 0;
    guint skipped = 0, cancelled = 0, stored;
    gboolean truncated = FALSE;
    gsize size = 0;
    gint fd;
    gboolean write_ok;
    gssize written;
    gsize offset;

    if (nemo_progress_info_get_operation (info) != NEMO_TRANSFER_OP_COPY &&
        nemo_progress_info_get_operation (info) != NEMO_TRANSFER_OP_MOVE)
        return;
    settings = g_settings_new ("org.dakkar.preferences");
    if (!g_settings_get_boolean (settings, "dakkar-transfer-retain-history")) {
        g_object_unref (settings);
        return;
    }
    g_object_unref (settings);

    files = nemo_progress_info_dup_file_manifest (info, 500, &truncated, &observed);
    record = g_key_file_new ();
    job_id = nemo_progress_info_dup_job_id (info);
    g_key_file_set_string (record, "Job", "id", job_id);
    g_key_file_set_int64 (record, "Job", "completed-unix-seconds", g_get_real_time () / G_USEC_PER_SEC);
    g_key_file_set_string (record, "Job", "type",
        nemo_progress_info_get_operation (info) == NEMO_TRANSFER_OP_COPY ? "copy" : "move");
    destination = nemo_progress_info_get_destination (info);
    uri = destination ? g_file_get_uri (destination) : NULL;
    if (uri != NULL) g_key_file_set_string (record, "Job", "destination-uri", uri);
    g_free (uri);
    g_clear_object (&destination);
    for (i = 0; i < files->len; ++i) {
        NemoTransferFileSnapshot *entry = g_ptr_array_index (files, i);
        gchar *section = g_strdup_printf ("File %u", i + 1);
        switch (entry->state) {
            case NEMO_TRANSFER_FILE_VERIFIED: verified++; completed++; break;
            case NEMO_TRANSFER_FILE_COMPLETED: completed++; break;
            case NEMO_TRANSFER_FILE_VERIFY_FAILED: failed++; break;
            case NEMO_TRANSFER_FILE_FAILED: failed++; break;
            case NEMO_TRANSFER_FILE_SKIPPED: skipped++; break;
            case NEMO_TRANSFER_FILE_CANCELLED: cancelled++; break;
            default: break;
        }
        if (entry->source_uri != NULL)
            g_key_file_set_string (record, section, "source-uri", entry->source_uri);
        if (entry->destination_uri != NULL)
            g_key_file_set_string (record, section, "destination-uri", entry->destination_uri);
        g_key_file_set_integer (record, section, "state", entry->state);
        g_key_file_set_int64 (record, section, "transferred", entry->transferred);
        if (entry->sha256 != NULL)
            g_key_file_set_string (record, section, "sha256", entry->sha256);
        if (entry->error != NULL)
            g_key_file_set_string (record, section, "error", entry->error);
        g_free (section);
    }
    stored = files->len;
    g_key_file_set_integer (record, "Job", "observed-file-count", observed);
    g_key_file_set_integer (record, "Job", "stored-file-count", stored);
    g_key_file_set_boolean (record, "Job", "manifest-incomplete", truncated || observed > stored);
    g_key_file_set_integer (record, "Job", "completed-in-stored-files", completed);
    g_key_file_set_integer (record, "Job", "verified-in-stored-files", verified);
    g_key_file_set_integer (record, "Job", "failed-in-stored-files", failed);
    g_key_file_set_integer (record, "Job", "skipped-in-stored-files", skipped);
    g_key_file_set_integer (record, "Job", "cancelled-in-stored-files", cancelled);
    contents = g_key_file_to_data (record, &size, NULL);
    folder = g_build_filename (g_get_user_data_dir (), "dakkar", "transfer-history", NULL);
    if (g_mkdir_with_parents (folder, 0700) != 0) goto done;
    filename = g_strdup_printf ("%020" G_GINT64_FORMAT "-%s.ini",
                                 g_get_real_time (), job_id);
    path = g_build_filename (folder, filename, NULL);
    /* Do not expose path or checksum data to other local users. */
    fd = g_open (path, O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (fd >= 0) {
        offset = 0;
        write_ok = TRUE;
        while (offset < size) {
            written = write (fd, contents + offset, size - offset);
            if (written < 0 && errno == EINTR) continue;
            if (written <= 0) { write_ok = FALSE; break; }
            offset += written;
        }
        if (close (fd) != 0) write_ok = FALSE;
        if (!write_ok) g_unlink (path);
    }
    g_free (path);
    g_free (filename);

    /* Bounded retention: filenames begin with a zero-padded timestamp.
     * Remove only our *.ini entries in the dedicated history directory. */
    directory = g_dir_open (folder, 0, NULL);
    if (directory != NULL) {
        names = g_ptr_array_new_with_free_func (g_free);
        while ((name = g_dir_read_name (directory)) != NULL) {
            if (g_str_has_suffix (name, ".ini") && strlen (name) > 25 &&
                g_ascii_isdigit (name[0]))
                g_ptr_array_add (names, g_strdup (name));
        }
        g_dir_close (directory);
        g_ptr_array_sort (names, dakkar_compare_history_names);
        for (i = 0; i + 100 < names->len; ++i) {
            gchar *old = g_build_filename (folder, g_ptr_array_index (names, i), NULL);
            g_unlink (old);
            g_free (old);
        }
        g_ptr_array_unref (names);
    }
done:
    g_free (folder);
    g_free (job_id);
    g_free (contents);
    g_key_file_unref (record);
    g_ptr_array_unref (files);
}

void
nemo_progress_info_finish (NemoProgressInfo *info)
{
    gboolean first_finish = FALSE;
    /* Never strand a worker waiting at a safe boundary during teardown. */
    nemo_progress_info_release_hold (info);
    G_LOCK (progress_info);
    if (!info->finished) {
        guint i;
        info->finished = TRUE;
        info->finished_us = g_get_monotonic_time ();
        /* Anything still announced as queued was never attempted. */
        for (i = 0; i < info->manifest->len; i++) {
            DakkarFileEntry *entry = g_ptr_array_index (info->manifest, i);
            if (entry->state == NEMO_TRANSFER_FILE_QUEUED ||
                entry->state == NEMO_TRANSFER_FILE_RUNNING)
                entry->state = NEMO_TRANSFER_FILE_CANCELLED;
        }
        info->finish_at_idle = TRUE;
        queue_idle (info, TRUE);
        first_finish = TRUE;
    }
    G_UNLOCK (progress_info);
    /* Write only once, on the worker, outside the progress lock. */
    if (first_finish)
        dakkar_transfer_write_history (info);
}

void
nemo_progress_info_take_status (NemoProgressInfo *info,
				    char *status)
{
	G_LOCK (progress_info);
	
	if (g_strcmp0 (info->status, status) != 0) {
		g_free (info->status);
		info->status = status;
		
		info->changed_at_idle = TRUE;
		queue_idle (info, FALSE);
	} else {
		g_free (status);
	}
	
	G_UNLOCK (progress_info);
}

void
nemo_progress_info_set_status (NemoProgressInfo *info,
				   const char *status)
{
	G_LOCK (progress_info);
	
	if (g_strcmp0 (info->status, status) != 0) {
		g_free (info->status);
		info->status = g_strdup (status);
		
		info->changed_at_idle = TRUE;
		queue_idle (info, FALSE);
	}
	
	G_UNLOCK (progress_info);
}


void
nemo_progress_info_take_details (NemoProgressInfo *info,
				     char           *details)
{
	G_LOCK (progress_info);
	
	if (g_strcmp0 (info->details, details) != 0) {
		g_free (info->details);
		info->details = details;
		
		info->changed_at_idle = TRUE;
		queue_idle (info, FALSE);
	} else {
		g_free (details);
	}
  
	G_UNLOCK (progress_info);
}

void
nemo_progress_info_set_details (NemoProgressInfo *info,
				    const char           *details)
{
	G_LOCK (progress_info);
	
	if (g_strcmp0 (info->details, details) != 0) {
		g_free (info->details);
		info->details = g_strdup (details);
		
		info->changed_at_idle = TRUE;
		queue_idle (info, FALSE);
	}
  
	G_UNLOCK (progress_info);
}

void
nemo_progress_info_take_initial_details (NemoProgressInfo *info,
                                                     char *initial_details)
{
    G_LOCK (progress_info);
    
    if (g_strcmp0 (info->initial_details, initial_details) != 0) {
        g_free (info->initial_details);
        info->initial_details = initial_details;
        
        info->changed_at_idle = TRUE;
        queue_idle (info, FALSE);
    } else {
        g_free (initial_details);
    }
  
    G_UNLOCK (progress_info);
}

void
nemo_progress_info_pulse_progress (NemoProgressInfo *info)
{
	G_LOCK (progress_info);

	info->activity_mode = TRUE;
    info->progress_known = FALSE;
	info->progress = 0.0;
	info->progress_at_idle = TRUE;
	queue_idle (info, FALSE);
	
	G_UNLOCK (progress_info);
}

void
nemo_progress_info_set_progress (NemoProgressInfo *info,
				     double                current,
				     double                total)
{
	double current_percent;
	
	if (total <= 0) {
		current_percent = 1.0;
	} else {
		current_percent = current / total;

		if (current_percent < 0) {
			current_percent	= 0;
		}
		
		if (current_percent > 1.0) {
			current_percent	= 1.0;
		}
	}
	
	G_LOCK (progress_info);
	
	if (!info->progress_known || info->activity_mode || /* emit when first known */
	    fabs (current_percent - info->progress) > 0
	    ) {
		info->activity_mode = FALSE;
        info->progress_known = TRUE;
		info->progress = current_percent;
		info->progress_at_idle = TRUE;
		queue_idle (info, FALSE);
	}
	
	G_UNLOCK (progress_info);
}

static NemoProgressInfoPresentFunc dakkar_present_func = NULL;

void
nemo_progress_info_set_present_func (NemoProgressInfoPresentFunc func)
{
    dakkar_present_func = func;
}

gpointer
nemo_progress_info_present_window (NemoProgressInfo *info)
{
    g_return_val_if_fail (NEMO_IS_PROGRESS_INFO (info), NULL);
    return dakkar_present_func != NULL ? dakkar_present_func (info) : NULL;
}
