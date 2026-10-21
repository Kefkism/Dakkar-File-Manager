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

#ifndef NEMO_PROGRESS_INFO_H
#define NEMO_PROGRESS_INFO_H

#include <glib-object.h>
#include <gio/gio.h>

#define NEMO_TYPE_PROGRESS_INFO         (nemo_progress_info_get_type ())
#define NEMO_PROGRESS_INFO(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), NEMO_TYPE_PROGRESS_INFO, NemoProgressInfo))
#define NEMO_PROGRESS_INFO_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), NEMO_TYPE_PROGRESS_INFO, NemoProgressInfoClass))
#define NEMO_IS_PROGRESS_INFO(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), NEMO_TYPE_PROGRESS_INFO))
#define NEMO_IS_PROGRESS_INFO_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), NEMO_TYPE_PROGRESS_INFO))
#define NEMO_PROGRESS_INFO_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), NEMO_TYPE_PROGRESS_INFO, NemoProgressInfoClass))

typedef struct _NemoProgressInfo      NemoProgressInfo;
typedef struct _NemoProgressInfoClass NemoProgressInfoClass;

/* Identifies the native backend operation, not a second copy engine. */
typedef enum {
    NEMO_TRANSFER_OP_OTHER = 0,
    NEMO_TRANSFER_OP_COPY,
    NEMO_TRANSFER_OP_MOVE,
    NEMO_TRANSFER_OP_LINK,
    NEMO_TRANSFER_OP_DELETE,
    NEMO_TRANSFER_OP_TRASH,
    NEMO_TRANSFER_OP_COMPRESS,
    NEMO_TRANSFER_OP_EXTRACT
} NemoTransferOperation;

GType nemo_progress_info_get_type (void) G_GNUC_CONST;

/* Signals:
   "changed" - status or details changed
   "progress-changed" - the percentage progress changed (or we pulsed if in activity_mode
   "started" - emited on job start
   "finished" - emitted when job is done
   
   All signals are emitted from idles in main loop.
   All methods are threadsafe.
 */

NemoProgressInfo *nemo_progress_info_new (void);

GList *       nemo_get_all_progress_info (void);

/* Dakkar: optional transfer destination, returned with a new reference. */
void          nemo_progress_info_set_destination (NemoProgressInfo *info, GFile *folder);
GFile *       nemo_progress_info_get_destination (NemoProgressInfo *info);
/* UUID never changes for a given Nemo progress object. Caller frees copy. */
gchar *       nemo_progress_info_dup_job_id (NemoProgressInfo *info);
void          nemo_progress_info_set_operation (NemoProgressInfo *info, NemoTransferOperation op);
NemoTransferOperation nemo_progress_info_get_operation (NemoProgressInfo *info);

/* Byte-specific metrics are only available for copy/move jobs that report
 * actual byte counts. Never derive a throughput rate from percentage alone. */
typedef struct {
    goffset completed;
    goffset total;
    gdouble current_bytes_per_second;
    gdouble average_bytes_per_second;
    gint64 elapsed_microseconds;
    gint64 remaining_microseconds; /* -1 when unestimatable */
    gboolean rate_available;
} NemoTransferByteStats;
void nemo_progress_info_report_bytes (NemoProgressInfo *info, goffset completed, goffset total);
gboolean nemo_progress_info_get_byte_stats (NemoProgressInfo *info,
                                            NemoTransferByteStats *out);

/* Dakkar: bounded, transient manifest of files observed by Nemo's native
 * copy/move engine. No second copy backend, no invented verification state.
 * Snapshots and their members are owned by the caller; free with
 * g_ptr_array_unref(). Entries are capped to avoid unbounded memory growth.
 */
typedef enum {
    NEMO_TRANSFER_FILE_RUNNING,
    NEMO_TRANSFER_FILE_COMPLETED,
    NEMO_TRANSFER_FILE_SKIPPED,
    NEMO_TRANSFER_FILE_FAILED,
    NEMO_TRANSFER_FILE_CANCELLED,
    NEMO_TRANSFER_FILE_VERIFIED,
    NEMO_TRANSFER_FILE_VERIFY_FAILED,
    NEMO_TRANSFER_FILE_QUEUED,     /* announced, not yet started */
    NEMO_TRANSFER_FILE_VERIFYING   /* data written; reading back for checksum */
} NemoTransferFileState;

typedef struct {
    gchar *source_uri;
    gchar *destination_uri;
    goffset transferred;
    goffset total;  /* -1 when unknown */
    NemoTransferFileState state;
    gchar *error;  /* optional native GIO diagnostic, never fabricated */
    gchar *sha256; /* non-NULL only on an actual checksum match (any algorithm) */
    gboolean overwritten; /* an existing destination was replaced */
    goffset verified;     /* bytes read back during the verify phase */
} NemoTransferFileSnapshot;

void nemo_transfer_file_snapshot_free (NemoTransferFileSnapshot *entry);
GPtrArray *nemo_progress_info_dup_file_manifest (NemoProgressInfo *info,
                                                   guint last_n,
                                                   gboolean *truncated,
                                                   guint *total_observed);
gboolean nemo_progress_info_has_retryable_copies (NemoProgressInfo *info);
/* Conservative success predicate for the destination-pane completion toast. */
gboolean nemo_progress_info_has_confirmed_success (NemoProgressInfo *info);
void nemo_progress_info_file_begin (NemoProgressInfo *info, GFile *source,
                                     GFile *destination);
void nemo_progress_info_file_progress (NemoProgressInfo *info, GFile *source,
                                        goffset transferred, goffset total);
void nemo_progress_info_file_end (NemoProgressInfo *info, GFile *source,
                                   NemoTransferFileState state,
                                   const gchar *error);
/* Only a successful two-sided digest comparison may set VERIFIED. */
void nemo_progress_info_file_verification (NemoProgressInfo *info, GFile *source,
                                           gboolean matched, const gchar *sha256,
                                           const gchar *diagnostic);

/* Cooperative pause at the next safe native file boundary. Never advertises
 * mid-file byte-offset resume; the current GIO call is allowed to finish.
 * Cancellation always wakes a waiting worker. Only copy/move supports this. */
/* Dakkar transfer window support (all thread-safe). */
void nemo_progress_info_file_queue (NemoProgressInfo *info, GFile *source,
                                     GFile *destination);
void nemo_progress_info_file_set_destination (NemoProgressInfo *info, GFile *source,
                                              GFile *destination);
void nemo_progress_info_file_set_overwritten (NemoProgressInfo *info, GFile *source,
                                               gboolean overwritten);
void nemo_progress_info_file_verify_progress (NemoProgressInfo *info, GFile *source,
                                               goffset done, goffset total);
void nemo_progress_info_report_files (NemoProgressInfo *info, guint done, guint total);
void nemo_progress_info_get_file_counts (NemoProgressInfo *info, guint *done, guint *total);
GCancellable *nemo_progress_info_begin_file_cancellable (NemoProgressInfo *info);
gboolean nemo_progress_info_end_file_cancellable (NemoProgressInfo *info,
                                                  GCancellable *file_cancellable);
gboolean nemo_progress_info_request_skip (NemoProgressInfo *info);
gboolean nemo_progress_info_can_skip (NemoProgressInfo *info);
gint64   nemo_progress_info_get_elapsed_us (NemoProgressInfo *info);
/* Object data "dakkar-verify-method" on a progress info (archive jobs):
 * which checks the job uses, so the window can name them correctly. */
#define DAKKAR_VERIFY_METHOD_HASH  1   /* read back + SHA-1/SHA-256/MD5 */
#define DAKKAR_VERIFY_METHOD_CRC7Z 2   /* the 7z program's CRC test */

void     nemo_progress_info_set_checksum_name (NemoProgressInfo *info, const gchar *name);
gchar   *nemo_progress_info_dup_checksum_name (NemoProgressInfo *info);

/* Dakkar: a worker that must ask the user something (e.g. a password)
 * makes the transfer window visible first. The UI registers the function;
 * call nemo_progress_info_present_window () on the main thread only. It
 * returns the window (a GtkWindow) or NULL. */
typedef gpointer (*NemoProgressInfoPresentFunc) (NemoProgressInfo *info);
void     nemo_progress_info_set_present_func (NemoProgressInfoPresentFunc func);
gpointer nemo_progress_info_present_window   (NemoProgressInfo *info);

gboolean nemo_progress_info_request_hold (NemoProgressInfo *info);
void nemo_progress_info_release_hold (NemoProgressInfo *info);
gboolean nemo_progress_info_hold_requested (NemoProgressInfo *info,
                                             gboolean *actually_waiting);
gboolean nemo_progress_info_wait_if_held (NemoProgressInfo *info);

char *        nemo_progress_info_get_status      (NemoProgressInfo *info);
char *        nemo_progress_info_get_details     (NemoProgressInfo *info);
char *        nemo_progress_info_get_initial_details (NemoProgressInfo *info);
double        nemo_progress_info_get_progress    (NemoProgressInfo *info);
GCancellable *nemo_progress_info_get_cancellable (NemoProgressInfo *info);
void          nemo_progress_info_cancel          (NemoProgressInfo *info);
gboolean      nemo_progress_info_get_is_started  (NemoProgressInfo *info);
gboolean      nemo_progress_info_get_is_finished (NemoProgressInfo *info);
gboolean      nemo_progress_info_get_is_paused   (NemoProgressInfo *info);

void          nemo_progress_info_queue           (NemoProgressInfo *info);
void          nemo_progress_info_start           (NemoProgressInfo *info);
void          nemo_progress_info_finish          (NemoProgressInfo *info);
void          nemo_progress_info_pause           (NemoProgressInfo *info);
void          nemo_progress_info_resume          (NemoProgressInfo *info);
void          nemo_progress_info_set_status      (NemoProgressInfo *info,
						      const char           *status);
void          nemo_progress_info_take_status     (NemoProgressInfo *info,
						      char                 *status);
void          nemo_progress_info_set_details     (NemoProgressInfo *info,
						      const char           *details);
void          nemo_progress_info_take_initial_details (NemoProgressInfo *info,
                              char                 *initial_details);
void          nemo_progress_info_take_details    (NemoProgressInfo *info,
						      char                 *details);
void          nemo_progress_info_set_progress    (NemoProgressInfo *info,
						      double                current,
						      double                total);
void          nemo_progress_info_pulse_progress  (NemoProgressInfo *info);



#endif /* NEMO_PROGRESS_INFO_H */
