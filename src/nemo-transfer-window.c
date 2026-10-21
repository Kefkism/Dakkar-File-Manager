/* nemo-transfer-window.c - Dakkar detailed transfer window and summary.
 *
 * A TeraCopy-style front end for Nemo's own file operations.  It never
 * copies data itself: everything shown here is read from the job's
 * NemoProgressInfo (byte statistics, file counts and the per-file manifest)
 * and every control maps onto an existing job primitive (hold/release,
 * per-file cancellable, job cancellable, a new native copy job for retry).
 *
 * Copyright (C) 2026 Dakar contributors
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#include <config.h>
#include <string.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include "nemo-transfer-window.h"
#include "nemo-corner-style.h"
#include "nemo-progress-ui-handler.h"
#include "nemo-application.h"
#include <libnemo-private/nemo-file-operations.h>
#include <libnemo-private/nemo-global-preferences.h>
#include <libnemo-private/nemo-icon-names.h>

enum {
    COL_NAME,
    COL_STATUS,
    COL_RESULT,
    COL_SIZE,
    COL_SOURCE_URI,
    COL_STATE,
    N_COLS
};

typedef struct {
    NemoProgressInfo *info;
    GtkWidget *window;
    GtkWidget *heading;
    GtkWidget *from_label;
    GtkWidget *to_label;
    GtkWidget *current_label;
    GtkWidget *file_bar;
    GtkWidget *total_bar;
    GtkWidget *speed_label;
    GtkWidget *average_label;
    GtkWidget *elapsed_label;
    GtkWidget *eta_label;
    GtkWidget *files_label;
    GtkWidget *verify_check;
    GtkWidget *pause_button;
    GtkWidget *skip_button;
    GtkWidget *cancel_button;
    GtkListStore *store;
    guint rows;          /* rows already in the store */
    guint first_open;    /* first row that may still change */
    guint timer;
    gboolean verifying;      /* current file is in the read-back phase */
    gdouble verify_fraction;
    guint failed;            /* failed + checksum-mismatch files so far */
    goffset pending_verify;  /* bytes written but not yet read back */
    gboolean finished;
    gulong finished_handler;
} DakkarTransfer;

static GList *transfers;   /* DakkarTransfer*, unfinished jobs only */
static guint watched_active;         /* queued/running jobs seen by watch() */
static GSList *activity_widgets;     /* redrawn when the busy state changes */
static GtkWidget *idle_window;       /* "No active transfers" (weak) */
static gchar *last_title;            /* last finished job, for the idle window */
static gchar *last_detail;
static GDateTime *last_time;
static void dakkar_idle_window_close (void);
static void dakkar_activity_changed (void);
static GHashTable *retried_sources; /* URIs started by "Retry failed" */

/* ---------- formatting ---------- */

gchar *
nemo_transfer_format_duration (gint64 us)
{
    guint64 total = us > 0 ? (guint64) (us / G_USEC_PER_SEC) : 0;
    guint h = total / 3600, m = (total / 60) % 60, s = total % 60;
    if (h > 0)
        /* Translators: hours:minutes:seconds */
        return g_strdup_printf (_("%u:%02u:%02u"), h, m, s);
    /* Translators: minutes:seconds */
    return g_strdup_printf (_("%u:%02u"), m, s);
}

static gchar *
dakkar_format_rate (gdouble bytes_per_second)
{
    gchar *size, *result;
    if (bytes_per_second <= 0.0)
        return g_strdup (_("—"));
    size = g_format_size ((guint64) bytes_per_second);
    /* Translators: a transfer speed such as "12.3 MB/s" */
    result = g_strdup_printf (_("%s/s"), size);
    g_free (size);
    return result;
}

static gchar *
dakkar_basename_for_uri (const gchar *uri)
{
    GFile *file;
    gchar *name;
    if (uri == NULL) return g_strdup ("");
    file = g_file_new_for_uri (uri);
    name = g_file_get_basename (file);
    g_object_unref (file);
    return name != NULL ? name : g_strdup (uri);
}

static gchar *
dakkar_parent_parse_name (const gchar *uri)
{
    GFile *file, *parent;
    gchar *name;
    if (uri == NULL) return NULL;
    file = g_file_new_for_uri (uri);
    parent = g_file_get_parent (file);
    name = g_file_get_parse_name (parent != NULL ? parent : file);
    g_clear_object (&parent);
    g_object_unref (file);
    return name;
}

static gchar *
dakkar_parse_name_for_uri (const gchar *uri)
{
    GFile *file;
    gchar *name;
    if (uri == NULL) return NULL;
    file = g_file_new_for_uri (uri);
    name = g_file_get_parse_name (file);
    g_object_unref (file);
    return name;
}

static const gchar *
dakkar_state_label (NemoTransferFileState state)
{
    switch (state) {
    case NEMO_TRANSFER_FILE_QUEUED:        return _("Queued");
    case NEMO_TRANSFER_FILE_RUNNING:       return _("Busy");
    case NEMO_TRANSFER_FILE_VERIFYING:     return _("Verifying");
    case NEMO_TRANSFER_FILE_COMPLETED:     return _("Done");
    case NEMO_TRANSFER_FILE_VERIFIED:      return _("Done (verified)");
    case NEMO_TRANSFER_FILE_SKIPPED:       return _("Skipped");
    case NEMO_TRANSFER_FILE_FAILED:        return _("Failed");
    case NEMO_TRANSFER_FILE_VERIFY_FAILED: return _("Failed");
    case NEMO_TRANSFER_FILE_CANCELLED:     return _("Cancelled");
    default:                               return "";
    }
}

static gboolean
dakkar_state_is_final (NemoTransferFileState state)
{
    return state != NEMO_TRANSFER_FILE_QUEUED &&
           state != NEMO_TRANSFER_FILE_RUNNING &&
           state != NEMO_TRANSFER_FILE_VERIFYING;
}

static gchar *
dakkar_result_text (NemoTransferFileSnapshot *e, NemoTransferOperation op)
{
    switch (e->state) {
    case NEMO_TRANSFER_FILE_COMPLETED:
    case NEMO_TRANSFER_FILE_VERIFIED:
        if (op == NEMO_TRANSFER_OP_DELETE)
            return g_strdup (_("Deleted"));
        if (op == NEMO_TRANSFER_OP_TRASH)
            return g_strdup (_("Moved to trash"));
        if (op == NEMO_TRANSFER_OP_COMPRESS)
            return g_strdup (_("Added"));
        if (op == NEMO_TRANSFER_OP_EXTRACT)
            return g_strdup (_("Extracted"));
        return g_strdup (e->overwritten ? _("Overwritten") : _("New"));
    case NEMO_TRANSFER_FILE_FAILED:
    case NEMO_TRANSFER_FILE_VERIFY_FAILED:
    case NEMO_TRANSFER_FILE_SKIPPED:
        return g_strdup (e->error != NULL ? e->error : "");
    default:
        return g_strdup ("");
    }
}

static const gchar *
dakkar_operation_verb (NemoTransferOperation op)
{
    switch (op) {
    case NEMO_TRANSFER_OP_COPY:  return _("Copying");
    case NEMO_TRANSFER_OP_MOVE:  return _("Moving");
    case NEMO_TRANSFER_OP_LINK:  return _("Linking");
    case NEMO_TRANSFER_OP_DELETE:return _("Deleting");
    case NEMO_TRANSFER_OP_TRASH: return _("Moving to trash");
    case NEMO_TRANSFER_OP_COMPRESS: return _("Compressing");
    case NEMO_TRANSFER_OP_EXTRACT:  return _("Extracting");
    default:                     return _("Transferring");
    }
}

static gboolean
dakkar_op_is_archive (NemoTransferOperation op)
{
    return op == NEMO_TRANSFER_OP_COMPRESS || op == NEMO_TRANSFER_OP_EXTRACT;
}

/* ---------- counts shared by the summary and the log ---------- */

typedef struct {
    guint succeeded, failed, skipped, cancelled, created, overwritten;
    guint verified, mismatched;
    goffset bytes;   /* size of the items that were transferred successfully */
} DakkarCounts;

static void
dakkar_count (GPtrArray *files, DakkarCounts *c)
{
    guint i;
    memset (c, 0, sizeof *c);
    for (i = 0; i < files->len; i++) {
        NemoTransferFileSnapshot *e = g_ptr_array_index (files, i);
        switch (e->state) {
        case NEMO_TRANSFER_FILE_VERIFIED:
            c->verified++;
            /* fall through */
        case NEMO_TRANSFER_FILE_COMPLETED:
            c->succeeded++;
            if (e->total > 0) c->bytes += e->total;
            if (e->overwritten) c->overwritten++; else c->created++;
            break;
        case NEMO_TRANSFER_FILE_VERIFY_FAILED:
            c->mismatched++;
            c->failed++;
            break;
        case NEMO_TRANSFER_FILE_FAILED:    c->failed++; break;
        case NEMO_TRANSFER_FILE_SKIPPED:   c->skipped++; break;
        case NEMO_TRANSFER_FILE_CANCELLED: c->cancelled++; break;
        default: break;
        }
    }
}

/* ---------- detailed window ---------- */

static void
dakkar_transfer_sync_rows (DakkarTransfer *t, GPtrArray *files, NemoTransferOperation op,
                          gboolean held)
{
    guint i;
    GtkTreeIter iter;
    gboolean all_final = TRUE;
    guint new_first_open = t->first_open;

    for (i = t->first_open; i < files->len; i++) {
        NemoTransferFileSnapshot *e = g_ptr_array_index (files, i);
        gchar *name, *result, *size;
        if (i < t->rows) {
            gchar *path = g_strdup_printf ("%u", i);
            gtk_tree_model_get_iter_from_string (GTK_TREE_MODEL (t->store), &iter, path);
            g_free (path);
        } else {
            gtk_list_store_append (t->store, &iter);
            t->rows++;
        }
        name = dakkar_basename_for_uri (e->source_uri);
        result = dakkar_result_text (e, op);
        size = e->total >= 0 ? g_format_size (e->total) : g_strdup ("");
        gtk_list_store_set (t->store, &iter,
                            COL_NAME, name,
                            COL_STATUS, held && e->state == NEMO_TRANSFER_FILE_RUNNING ?
                                        _("Paused") : dakkar_state_label (e->state),
                            COL_RESULT, result,
                            COL_SIZE, size,
                            COL_SOURCE_URI, e->source_uri,
                            COL_STATE, (gint) e->state,
                            -1);
        g_free (name);
        g_free (result);
        g_free (size);
        if (all_final && dakkar_state_is_final (e->state))
            new_first_open = i + 1;
        else
            all_final = FALSE;
    }
    t->first_open = new_first_open;
}

static NemoTransferFileSnapshot *
dakkar_current_entry (GPtrArray *files)
{
    gint i;
    for (i = (gint) files->len - 1; i >= 0; i--) {
        NemoTransferFileSnapshot *e = g_ptr_array_index (files, i);
        if (e->state == NEMO_TRANSFER_FILE_RUNNING ||
            e->state == NEMO_TRANSFER_FILE_VERIFYING)
            return e;
    }
    return NULL;
}

/* Remaining time including the read-back phase when verification is on:
 * every byte still to be copied is also read back once more. */
static gint
dakkar_verify_method (NemoProgressInfo *info)
{
    return GPOINTER_TO_INT (g_object_get_data (G_OBJECT (info), "dakkar-verify-method"));
}

/* "SHA-1", "CRC (7-Zip)" or "SHA-1 + CRC (7-Zip)" for an archive job */
static gchar *
dakkar_verify_method_name (NemoProgressInfo *info)
{
    gint method = dakkar_verify_method (info);
    gchar *algo = g_settings_get_string (nemo_preferences, "dakkar-transfer-verify-algorithm");
    const gchar *hash = g_strcmp0 (algo, "sha256") == 0 ? "SHA-256" :
                        g_strcmp0 (algo, "md5") == 0 ? "MD5" : "SHA-1";
    gchar *name;
    if (method == DAKKAR_VERIFY_METHOD_CRC7Z)
        name = g_strdup (_("CRC (7-Zip)"));
    else if (method == (DAKKAR_VERIFY_METHOD_CRC7Z | DAKKAR_VERIFY_METHOD_HASH))
        /* Translators: both checks in one job, e.g. "SHA-1 + CRC (7-Zip)" */
        name = g_strdup_printf (_("%s + CRC (7-Zip)"), hash);
    else
        name = g_strdup (hash);
    g_free (algo);
    return name;
}

static gint64
dakkar_transfer_eta (DakkarTransfer *t, NemoTransferByteStats *stats)
{
    gchar *algo;
    gdouble work;
    if (!stats->rate_available || stats->current_bytes_per_second <= 0.01)
        return -1;
    /* archive jobs include the verify pass in their own byte totals */
    algo = dakkar_op_is_archive (nemo_progress_info_get_operation (t->info)) ? NULL :
           nemo_progress_info_dup_checksum_name (t->info);
    work = (gdouble) MAX (stats->total - stats->completed, 0);
    if (algo != NULL)
        work = work * 2 + t->pending_verify;
    g_free (algo);
    if (work <= 0) return 0;
    return (gint64) (work * G_USEC_PER_SEC / stats->current_bytes_per_second);
}

/* Overall fraction; with verification every byte is written AND read back. */
static gdouble
dakkar_transfer_fraction (DakkarTransfer *t, NemoTransferByteStats *stats)
{
    gchar *algo;
    gdouble f;
    if (stats->total <= 0) return 0.0;
    algo = dakkar_op_is_archive (nemo_progress_info_get_operation (t->info)) ? NULL :
           nemo_progress_info_dup_checksum_name (t->info);
    if (algo != NULL)
        f = (2.0 * stats->completed - t->pending_verify) / (2.0 * stats->total);
    else
        f = (gdouble) stats->completed / stats->total;
    g_free (algo);
    return CLAMP (f, 0.0, 1.0);
}

/* Verify progress of the current file and the failure count. Kept up to
 * date even while the window is hidden: the tab footer reads them. */
static void
transfer_update_counters (DakkarTransfer *t, NemoTransferOperation op, GPtrArray *files,
                          NemoTransferFileSnapshot *cur)
{
    gchar *algo = nemo_progress_info_dup_checksum_name (t->info);
    guint i;

    t->verifying = cur != NULL && cur->state == NEMO_TRANSFER_FILE_VERIFYING;
    t->verify_fraction = t->verifying && cur->total > 0 ?
        (gdouble) cur->verified / cur->total : 0.0;
    t->pending_verify = 0;
    if (algo != NULL && cur != NULL && !dakkar_op_is_archive (op))
        t->pending_verify = t->verifying ? MAX (cur->total - cur->verified, 0) :
                                           cur->transferred;
    g_free (algo);
    t->failed = 0;
    for (i = 0; i < files->len; i++) {
        NemoTransferFileSnapshot *e = g_ptr_array_index (files, i);
        if (e->state == NEMO_TRANSFER_FILE_FAILED ||
            e->state == NEMO_TRANSFER_FILE_VERIFY_FAILED)
            t->failed++;
    }
}

/* From and To folders. Archive jobs name their source themselves. */
static void
transfer_show_folders (DakkarTransfer *t, NemoTransferOperation op, GPtrArray *files)
{
    gchar *a;
    GFile *dest;

    if (dakkar_op_is_archive (op) &&
        g_object_get_data (G_OBJECT (t->info), "dakkar-archive-result") != NULL &&
        ((NemoArchiveResult *) g_object_get_data (G_OBJECT (t->info),
                                                  "dakkar-archive-result"))->source_label != NULL) {
        NemoArchiveResult *ar = g_object_get_data (G_OBJECT (t->info), "dakkar-archive-result");
        gtk_label_set_text (GTK_LABEL (t->from_label), ar->source_label);
    } else if (files->len > 0) {
        NemoTransferFileSnapshot *first = g_ptr_array_index (files, 0);
        a = dakkar_parent_parse_name (first->source_uri);
        gtk_label_set_text (GTK_LABEL (t->from_label), a != NULL ? a : "");
        g_free (a);
    }
    dest = nemo_progress_info_get_destination (t->info);
    if (dest != NULL) {
        a = g_file_get_parse_name (dest);
        gtk_label_set_text (GTK_LABEL (t->to_label), a);
        g_free (a);
        g_object_unref (dest);
    } else {
        gtk_label_set_text (GTK_LABEL (t->to_label), _("—"));
    }
}

/* Current file and its own progress (copy phase or verify phase). */
static void
transfer_show_current (DakkarTransfer *t, NemoTransferFileSnapshot *cur)
{
    gchar *text, *a, *b;
    gdouble fraction;

    if (cur != NULL) {
        gchar *name = dakkar_basename_for_uri (cur->source_uri);
        gchar *dest_dir = dakkar_parent_parse_name (cur->destination_uri);
        if (dest_dir != NULL)
            /* Translators: "<file name>  →  <destination folder>" */
            text = g_strdup_printf (_("%s  →  %s"), name, dest_dir);
        else
            text = g_strdup (name);
        gtk_label_set_text (GTK_LABEL (t->current_label), text);
        g_free (text);
        g_free (name);
        g_free (dest_dir);
        if (cur->state == NEMO_TRANSFER_FILE_VERIFYING) {
            gchar *algo = nemo_progress_info_dup_checksum_name (t->info);
            fraction = cur->total > 0 ? (gdouble) cur->verified / cur->total : 0.0;
            a = g_format_size (cur->verified);
            b = g_format_size (MAX (cur->total, 0));
            /* Translators: "Verifying (SHA-1): 10 MB of 20 MB" */
            text = g_strdup_printf (_("Verifying (%s): %s of %s"),
                                    algo != NULL ? algo : "", a, b);
            g_free (algo);
        } else if (cur->total > 0) {
            fraction = (gdouble) cur->transferred / cur->total;
            a = g_format_size (cur->transferred);
            b = g_format_size (cur->total);
            /* Translators: "10 MB of 20 MB" */
            text = g_strdup_printf (_("%s of %s"), a, b);
        } else {
            fraction = 0.0;
            a = b = NULL;
            text = g_strdup ("");
        }
        gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (t->file_bar), CLAMP (fraction, 0.0, 1.0));
        gtk_progress_bar_set_text (GTK_PROGRESS_BAR (t->file_bar), text);
        g_free (text);
        g_free (a);
        g_free (b);
    } else {
        gtk_label_set_text (GTK_LABEL (t->current_label), _("—"));
        gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (t->file_bar), 0.0);
        gtk_progress_bar_set_text (GTK_PROGRESS_BAR (t->file_bar), "");
    }
}

/* Totals: bytes and items done / total. */
static void
transfer_show_totals (DakkarTransfer *t, NemoTransferOperation op,
                      gboolean have_bytes, NemoTransferByteStats *stats)
{
    guint done = 0, total = 0;
    gchar *text, *a, *b;
    gdouble fraction;

    nemo_progress_info_get_file_counts (t->info, &done, &total);
    if (have_bytes && stats->total > 0) {
        gchar *algo = nemo_progress_info_dup_checksum_name (t->info);
        fraction = dakkar_transfer_fraction (t, stats);
        if (dakkar_op_is_archive (op) && algo != NULL && op == NEMO_TRANSFER_OP_COMPRESS) {
            /* compress + verify reports write and read-back in one total */
            goffset half = stats->total / 2;
            gboolean verify_phase = stats->completed > half;
            a = g_format_size (verify_phase ? stats->completed - half : stats->completed);
            b = g_format_size (half);
            /* Translators: "Verified 10 MB of 20 MB" (archive read-back phase) */
            text = g_strdup_printf (verify_phase ? _("Verified %s of %s") : _("%s of %s"), a, b);
        } else {
            a = g_format_size (stats->completed);
            b = g_format_size (stats->total);
            text = g_strdup_printf (_("%s of %s"), a, b);
        }
        g_free (algo);
        g_free (a);
        g_free (b);
    } else {
        fraction = nemo_progress_info_get_progress (t->info);
        text = fraction >= 0.0 ? g_strdup_printf ("%u%%", (guint) (fraction * 100)) : g_strdup ("");
    }
    gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (t->total_bar), CLAMP (fraction, 0.0, 1.0));
    gtk_progress_bar_set_text (GTK_PROGRESS_BAR (t->total_bar), text);
    g_free (text);
    text = g_strdup_printf (ngettext ("%u of %u item", "%u of %u items", total), done, total);
    gtk_label_set_text (GTK_LABEL (t->files_label), text);
    g_free (text);
}

/* Current and average speed, elapsed time and ETA. */
static void
transfer_show_rates (DakkarTransfer *t, gboolean held, gboolean have_bytes,
                     NemoTransferByteStats *stats)
{
    gchar *a;

    a = dakkar_format_rate (have_bytes && stats->rate_available && !held && !t->verifying ?
                           stats->current_bytes_per_second : 0.0);
    gtk_label_set_text (GTK_LABEL (t->speed_label), a);
    g_free (a);
    a = dakkar_format_rate (have_bytes ? stats->average_bytes_per_second : 0.0);
    gtk_label_set_text (GTK_LABEL (t->average_label), a);
    g_free (a);
    a = nemo_transfer_format_duration (nemo_progress_info_get_elapsed_us (t->info));
    gtk_label_set_text (GTK_LABEL (t->elapsed_label), a);
    g_free (a);
    if (held)
        gtk_label_set_text (GTK_LABEL (t->eta_label), _("Paused"));
    else if (have_bytes && dakkar_transfer_eta (t, stats) >= 0) {
        a = nemo_transfer_format_duration (dakkar_transfer_eta (t, stats));
        gtk_label_set_text (GTK_LABEL (t->eta_label), a);
        g_free (a);
    } else
        gtk_label_set_text (GTK_LABEL (t->eta_label), _("—"));
}

/* Archive jobs verify what they wrote, not a copy: name the check that
 * is really done (the 7z program tests CRCs, no SHA-1). Only redone when
 * the method changes. */
static void
transfer_update_verify_label (DakkarTransfer *t, NemoTransferOperation op)
{
    gint method;
    gchar *algo_name, *label;

    if (GPOINTER_TO_INT (g_object_get_data (G_OBJECT (t->verify_check), "dakkar-archive-label")) ==
        dakkar_verify_method (t->info) + 1)
        return;
    method = dakkar_verify_method (t->info);
    algo_name = dakkar_verify_method_name (t->info);
    label = g_strdup_printf (op == NEMO_TRANSFER_OP_COMPRESS ?
        /* Translators: %s is a checksum algorithm such as SHA-1 */
        _("Verify the archive with a checksum (%s)") :
        /* Translators: %s is a checksum algorithm such as SHA-1 */
        _("Verify extracted files with a checksum (%s)"), algo_name);
    gtk_button_set_label (GTK_BUTTON (t->verify_check), label);
    gtk_widget_set_tooltip_text (t->verify_check,
        method == DAKKAR_VERIFY_METHOD_CRC7Z ?
        (op == NEMO_TRANSFER_OP_COMPRESS ?
         _("The finished archive is tested by the 7z program with the password: every file's CRC is checked.") :
         _("The 7z program checks every file's CRC while it extracts; no separate SHA-1 read-back.")) :
        op == NEMO_TRANSFER_OP_COMPRESS ?
        _("Applies to files that have not been started yet. Each file is hashed while it is added; the finished archive is read back from disk and every entry is compared.") :
        _("Applies to files that have not been started yet. Each file is hashed while it is extracted, then read back from disk and compared."));
    g_object_set_data (G_OBJECT (t->verify_check), "dakkar-archive-label",
                       GINT_TO_POINTER (method + 1));
    g_free (label);
    g_free (algo_name);
}

static void
dakkar_transfer_refresh (DakkarTransfer *t)
{
    NemoTransferOperation op = nemo_progress_info_get_operation (t->info);
    NemoTransferByteStats stats;
    gboolean have_bytes = nemo_progress_info_get_byte_stats (t->info, &stats);
    GPtrArray *files = nemo_progress_info_dup_file_manifest (t->info, 0, NULL, NULL);
    NemoTransferFileSnapshot *cur = dakkar_current_entry (files);
    gboolean held, waiting = FALSE;
    gchar *text;

    transfer_update_counters (t, op, files, cur);
    if (t->window == NULL || !gtk_widget_get_visible (t->window)) {
        g_ptr_array_unref (files);
        return;
    }
    text = nemo_progress_info_get_status (t->info);
    gtk_label_set_text (GTK_LABEL (t->heading), text);
    gtk_window_set_title (GTK_WINDOW (t->window), text);
    g_free (text);
    transfer_show_folders (t, op, files);
    transfer_show_current (t, cur);
    transfer_show_totals (t, op, have_bytes, &stats);
    held = nemo_progress_info_hold_requested (t->info, &waiting);
    transfer_show_rates (t, held, have_bytes, &stats);

    gtk_button_set_label (GTK_BUTTON (t->pause_button), held ? _("_Resume") : _("_Pause"));
    gtk_widget_set_sensitive (t->skip_button, nemo_progress_info_can_skip (t->info));
    gtk_widget_set_visible (t->verify_check,
                            op == NEMO_TRANSFER_OP_COPY || op == NEMO_TRANSFER_OP_MOVE ||
                            dakkar_op_is_archive (op));
    if (dakkar_op_is_archive (op))
        transfer_update_verify_label (t, op);

    dakkar_transfer_sync_rows (t, files, op, held);
    g_ptr_array_unref (files);
}

static gboolean
dakkar_transfer_tick (gpointer data)
{
    DakkarTransfer *t = data;
    gboolean was_verifying = t->verifying;
    dakkar_transfer_refresh (t);
    /* The checksum read-back reports through the file manifest only, so no
     * progress-changed signal reaches the status bars; refresh them here. */
    if (t->verifying || was_verifying)
        nemo_progress_ui_handler_refresh_global ();
    return G_SOURCE_CONTINUE;
}

static void
dakkar_transfer_minimize (DakkarTransfer *t)
{
    if (t->window != NULL)
        gtk_widget_hide (t->window);
    nemo_progress_ui_handler_refresh_global ();
}

static gboolean
dakkar_transfer_delete_event (GtkWidget *widget, GdkEvent *event, DakkarTransfer *t)
{
    /* Closing the window never stops the job; it collapses to the status bar. */
    dakkar_transfer_minimize (t);
    (void) widget; (void) event;
    return TRUE;
}

static void
dakkar_transfer_pause_clicked (GtkButton *button, DakkarTransfer *t)
{
    if (nemo_progress_info_hold_requested (t->info, NULL))
        nemo_progress_info_release_hold (t->info);
    else
        nemo_progress_info_request_hold (t->info);
    dakkar_transfer_refresh (t);
    nemo_progress_ui_handler_refresh_global ();
    (void) button;
}

static void
dakkar_transfer_skip_clicked (GtkButton *button, DakkarTransfer *t)
{
    nemo_progress_info_request_skip (t->info);
    (void) button;
}

static void
dakkar_transfer_cancel_clicked (GtkButton *button, DakkarTransfer *t)
{
    nemo_progress_info_cancel (t->info);
    (void) button;
}

static void
dakkar_transfer_minimize_clicked (GtkButton *button, DakkarTransfer *t)
{
    dakkar_transfer_minimize (t);
    (void) button;
}

static GtkWidget *
dakkar_value_label (GtkWidget *grid, const gchar *caption, gint col, gint row)
{
    GtkWidget *key = gtk_label_new (caption);
    GtkWidget *value = gtk_label_new ("");
    gtk_style_context_add_class (gtk_widget_get_style_context (key), "dim-label");
    gtk_label_set_xalign (GTK_LABEL (key), 1.0);
    gtk_label_set_xalign (GTK_LABEL (value), 0.0);
    gtk_label_set_ellipsize (GTK_LABEL (value), PANGO_ELLIPSIZE_MIDDLE);
    gtk_widget_set_hexpand (value, TRUE);
    gtk_grid_attach (GTK_GRID (grid), key, col, row, 1, 1);
    gtk_grid_attach (GTK_GRID (grid), value, col + 1, row, 1, 1);
    return value;
}

static GtkWidget *
dakkar_build_file_view (GtkListStore *store)
{
    GtkWidget *view = gtk_tree_view_new_with_model (GTK_TREE_MODEL (store));
    GtkCellRenderer *r;
    GtkTreeViewColumn *c;

    r = gtk_cell_renderer_text_new ();
    g_object_set (r, "ellipsize", PANGO_ELLIPSIZE_MIDDLE, NULL);
    c = gtk_tree_view_column_new_with_attributes (_("File"), r, "text", COL_NAME, NULL);
    gtk_tree_view_column_set_expand (c, TRUE);
    gtk_tree_view_column_set_resizable (c, TRUE);
    gtk_tree_view_append_column (GTK_TREE_VIEW (view), c);
    r = gtk_cell_renderer_text_new ();
    c = gtk_tree_view_column_new_with_attributes (_("Status"), r, "text", COL_STATUS, NULL);
    gtk_tree_view_append_column (GTK_TREE_VIEW (view), c);
    r = gtk_cell_renderer_text_new ();
    g_object_set (r, "ellipsize", PANGO_ELLIPSIZE_END, NULL);
    c = gtk_tree_view_column_new_with_attributes (_("Result"), r, "text", COL_RESULT, NULL);
    gtk_tree_view_column_set_expand (c, TRUE);
    gtk_tree_view_column_set_resizable (c, TRUE);
    gtk_tree_view_append_column (GTK_TREE_VIEW (view), c);
    r = gtk_cell_renderer_text_new ();
    g_object_set (r, "xalign", 1.0, NULL);
    c = gtk_tree_view_column_new_with_attributes (_("Size"), r, "text", COL_SIZE, NULL);
    gtk_tree_view_append_column (GTK_TREE_VIEW (view), c);
    return view;
}

static void
dakkar_transfer_build_window (DakkarTransfer *t)
{
    GtkWidget *box, *grid, *w, *scroll, *buttons;
    const gchar *algo_name;
    gchar *algo, *text;

    t->window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
    gtk_window_set_type_hint (GTK_WINDOW (t->window), GDK_WINDOW_TYPE_HINT_DIALOG);
    gtk_window_set_default_size (GTK_WINDOW (t->window), 760, 560);
    gtk_window_set_position (GTK_WINDOW (t->window), GTK_WIN_POS_CENTER);
    gtk_window_set_icon_name (GTK_WINDOW (t->window), DAKKAR_APP_ICON);
    gtk_window_set_wmclass (GTK_WINDOW (t->window), "file_progress", "Nemo");
    gtk_widget_set_name (t->window, "dakkar-transfer-window");
    dakkar_dialog_style_attach (t->window);
    g_signal_connect (t->window, "delete-event", G_CALLBACK (dakkar_transfer_delete_event), t);

    box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width (GTK_CONTAINER (box), 12);
    gtk_container_add (GTK_CONTAINER (t->window), box);

    t->heading = gtk_label_new ("");
    gtk_label_set_xalign (GTK_LABEL (t->heading), 0.0);
    gtk_style_context_add_class (gtk_widget_get_style_context (t->heading), "title");
    {
        PangoAttrList *attrs = pango_attr_list_new ();
        pango_attr_list_insert (attrs, pango_attr_weight_new (PANGO_WEIGHT_BOLD));
        pango_attr_list_insert (attrs, pango_attr_scale_new (1.2));
        gtk_label_set_attributes (GTK_LABEL (t->heading), attrs);
        pango_attr_list_unref (attrs);
    }
    gtk_box_pack_start (GTK_BOX (box), t->heading, FALSE, FALSE, 0);

    grid = gtk_grid_new ();
    gtk_grid_set_row_spacing (GTK_GRID (grid), 4);
    gtk_grid_set_column_spacing (GTK_GRID (grid), 8);
    t->from_label = dakkar_value_label (grid, _("From:"), 0, 0);
    t->to_label = dakkar_value_label (grid, _("To:"), 0, 1);
    t->current_label = dakkar_value_label (grid, _("Current file:"), 0, 2);
    gtk_box_pack_start (GTK_BOX (box), grid, FALSE, FALSE, 0);

    t->file_bar = gtk_progress_bar_new ();
    gtk_progress_bar_set_show_text (GTK_PROGRESS_BAR (t->file_bar), TRUE);
    gtk_box_pack_start (GTK_BOX (box), t->file_bar, FALSE, FALSE, 0);

    w = gtk_label_new (_("Total"));
    gtk_label_set_xalign (GTK_LABEL (w), 0.0);
    gtk_style_context_add_class (gtk_widget_get_style_context (w), "dim-label");
    gtk_box_pack_start (GTK_BOX (box), w, FALSE, FALSE, 0);
    t->total_bar = gtk_progress_bar_new ();
    gtk_progress_bar_set_show_text (GTK_PROGRESS_BAR (t->total_bar), TRUE);
    gtk_box_pack_start (GTK_BOX (box), t->total_bar, FALSE, FALSE, 0);

    grid = gtk_grid_new ();
    gtk_grid_set_row_spacing (GTK_GRID (grid), 4);
    gtk_grid_set_column_spacing (GTK_GRID (grid), 8);
    gtk_grid_set_column_homogeneous (GTK_GRID (grid), FALSE);
    t->files_label = dakkar_value_label (grid, _("Items:"), 0, 0);
    t->speed_label = dakkar_value_label (grid, _("Speed:"), 2, 0);
    t->average_label = dakkar_value_label (grid, _("Average:"), 4, 0);
    t->elapsed_label = dakkar_value_label (grid, _("Elapsed:"), 0, 1);
    t->eta_label = dakkar_value_label (grid, _("Remaining:"), 2, 1);
    gtk_box_pack_start (GTK_BOX (box), grid, FALSE, FALSE, 0);

    algo = g_settings_get_string (nemo_preferences, "dakkar-transfer-verify-algorithm");
    algo_name = g_strcmp0 (algo, "sha256") == 0 ? "SHA-256" :
                g_strcmp0 (algo, "md5") == 0 ? "MD5" : "SHA-1";
    /* Translators: %s is a checksum algorithm such as SHA-1 */
    text = g_strdup_printf (_("Verify copied files with a checksum (%s)"), algo_name);
    t->verify_check = gtk_check_button_new_with_label (text);
    gtk_widget_set_tooltip_text (t->verify_check,
        _("Applies to files that have not been started yet. The source is hashed while it is copied; the destination is read back from disk and compared."));
    g_free (text);
    g_free (algo);
    g_settings_bind (nemo_preferences, "dakkar-transfer-verify-after-copy",
                     t->verify_check, "active", G_SETTINGS_BIND_DEFAULT);
    gtk_widget_set_no_show_all (t->verify_check, TRUE);
    gtk_box_pack_start (GTK_BOX (box), t->verify_check, FALSE, FALSE, 0);

    scroll = gtk_scrolled_window_new (NULL, NULL);
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroll),
                                    GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (scroll), GTK_SHADOW_IN);
    gtk_widget_set_vexpand (scroll, TRUE);
    t->store = gtk_list_store_new (N_COLS, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
                                   G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INT);
    gtk_container_add (GTK_CONTAINER (scroll), dakkar_build_file_view (t->store));
    gtk_box_pack_start (GTK_BOX (box), scroll, TRUE, TRUE, 0);

    buttons = gtk_button_box_new (GTK_ORIENTATION_HORIZONTAL);
    gtk_button_box_set_layout (GTK_BUTTON_BOX (buttons), GTK_BUTTONBOX_END);
    gtk_box_set_spacing (GTK_BOX (buttons), 6);
    w = gtk_button_new_with_mnemonic (_("_Minimize to status bar"));
    gtk_widget_set_tooltip_text (w, _("Hide this window; progress stays visible in the status bar of every Files window"));
    g_signal_connect (w, "clicked", G_CALLBACK (dakkar_transfer_minimize_clicked), t);
    gtk_container_add (GTK_CONTAINER (buttons), w);
    gtk_button_box_set_child_secondary (GTK_BUTTON_BOX (buttons), w, TRUE);
    t->pause_button = gtk_button_new_with_mnemonic (_("_Pause"));
    gtk_widget_set_tooltip_text (t->pause_button, _("Pause or resume this transfer (takes effect immediately, also inside a large file)"));
    g_signal_connect (t->pause_button, "clicked", G_CALLBACK (dakkar_transfer_pause_clicked), t);
    gtk_container_add (GTK_CONTAINER (buttons), t->pause_button);
    t->skip_button = gtk_button_new_with_mnemonic (_("S_kip file"));
    gtk_widget_set_tooltip_text (t->skip_button, _("Skip the file that is being transferred now; a partially written new file is removed"));
    g_signal_connect (t->skip_button, "clicked", G_CALLBACK (dakkar_transfer_skip_clicked), t);
    gtk_container_add (GTK_CONTAINER (buttons), t->skip_button);
    t->cancel_button = gtk_button_new_with_mnemonic (_("_Cancel"));
    g_signal_connect (t->cancel_button, "clicked", G_CALLBACK (dakkar_transfer_cancel_clicked), t);
    gtk_container_add (GTK_CONTAINER (buttons), t->cancel_button);
    gtk_box_pack_start (GTK_BOX (box), buttons, FALSE, FALSE, 0);

    gtk_widget_show_all (box);
}

/* ---------- per-item analysis shared by the summary list and the log ---------- */

typedef enum {
    DAKKAR_ITEM_OK,          /* done and verified (or nothing to verify, e.g. deleted) */
    DAKKAR_ITEM_UNVERIFIED,  /* done, but no checksum: folder, link, verification off */
    DAKKAR_ITEM_FAILED,      /* failed, checksum mismatch, skipped after an error */
    DAKKAR_ITEM_NOT_DONE     /* skipped by a choice, or not reached (cancelled) */
} DakkarItemStatus;

typedef enum {
    DAKKAR_KIND_UNKNOWN,
    DAKKAR_KIND_FILE,
    DAKKAR_KIND_DIR,
    DAKKAR_KIND_LINK,
    DAKKAR_KIND_SPECIAL
} DakkarItemKind;

typedef struct {
    guint n;
    guint8 *kind;            /* DakkarItemKind per entry */
    guint8 *status;          /* DakkarItemStatus per entry */
    gchar **reason;          /* plain-text reason per entry (owned) */
    gchar *base_uri;         /* common source folder, without trailing '/' */
    guint files, folders, links, others;   /* completed items by kind */
    guint eligible;          /* files that a checksum applies to */
    guint partial_dirs;      /* folders created but "skipped" for a child */
    guint n_status[4];
    gboolean verify_on;      /* verification was (or is) switched on */
} DakkarItems;

static void
dakkar_items_free (DakkarItems *it)
{
    guint i;
    if (it == NULL) return;
    for (i = 0; i < it->n; i++) g_free (it->reason[i]);
    g_free (it->reason);
    g_free (it->kind);
    g_free (it->status);
    g_free (it->base_uri);
    g_free (it);
}

/* A file type without blocking on remote locations: a verified entry is a
 * regular file by definition; other entries are inspected only when local. */
static DakkarItemKind
dakkar_item_kind (NemoTransferFileSnapshot *e)
{
    const gchar *uris[2];
    guint k;
    if (e->state == NEMO_TRANSFER_FILE_VERIFIED ||
        e->state == NEMO_TRANSFER_FILE_VERIFY_FAILED)
        return DAKKAR_KIND_FILE;
    uris[0] = e->destination_uri;
    uris[1] = e->source_uri;
    for (k = 0; k < 2; k++) {
        GFile *file;
        GFileType type = G_FILE_TYPE_UNKNOWN;
        if (uris[k] == NULL) continue;
        file = g_file_new_for_uri (uris[k]);
        if (g_file_is_native (file))
            type = g_file_query_file_type (file, G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL);
        g_object_unref (file);
        switch (type) {
        case G_FILE_TYPE_REGULAR:      return DAKKAR_KIND_FILE;
        case G_FILE_TYPE_DIRECTORY:    return DAKKAR_KIND_DIR;
        case G_FILE_TYPE_SYMBOLIC_LINK:return DAKKAR_KIND_LINK;
        case G_FILE_TYPE_SPECIAL:      return DAKKAR_KIND_SPECIAL;
        default: break;
        }
    }
    return DAKKAR_KIND_UNKNOWN;
}

static DakkarItemStatus
dakkar_item_classify (NemoTransferFileSnapshot *e, DakkarItemKind kind,
                     NemoTransferOperation op, const gchar *algo,
                     gboolean verify_on, NemoArchiveResult *ar, gchar **reason)
{
    const gchar *sum = algo != NULL ? algo : "SHA-1";
    switch (e->state) {
    case NEMO_TRANSFER_FILE_VERIFIED:
        /* Translators: %s is a checksum name such as "SHA-1" */
        *reason = g_strdup_printf (_("Copied and verified: the %s checksums of the source and the copy match."), sum);
        return DAKKAR_ITEM_OK;
    case NEMO_TRANSFER_FILE_VERIFY_FAILED:
        *reason = e->error != NULL ?
            g_strdup_printf (_("Checksum mismatch: the copy differs from the source (%s)."), e->error) :
            g_strdup (_("Checksum mismatch: the copy differs from the source."));
        return DAKKAR_ITEM_FAILED;
    case NEMO_TRANSFER_FILE_FAILED:
        *reason = g_strdup (e->error != NULL ? e->error : _("Failed (no details were reported)."));
        return DAKKAR_ITEM_FAILED;
    case NEMO_TRANSFER_FILE_SKIPPED:
        if (kind == DAKKAR_KIND_DIR && e->error == NULL && e->destination_uri != NULL) {
            /* Nemo marks a folder "skipped" when anything inside it was:
             * the folder itself was created */
            *reason = g_strdup (_("Folder created, but not everything inside it was copied; see the items inside it."));
            return DAKKAR_ITEM_UNVERIFIED;
        }
        if (g_strcmp0 (e->error, _("Verification skipped by user")) == 0) {
            *reason = g_strdup (_("Copied, but you skipped the checksum verification."));
            return DAKKAR_ITEM_UNVERIFIED;
        }
        if (op == NEMO_TRANSFER_OP_DELETE || op == NEMO_TRANSFER_OP_TRASH) {
            /* a deletion is only skipped after an error was reported */
            if (e->error != NULL)
                *reason = g_strdup_printf (op == NEMO_TRANSFER_OP_DELETE ? _("Not deleted: %s") :
                                           _("Not moved to the trash: %s"), e->error);
            else
                *reason = g_strdup (kind == DAKKAR_KIND_DIR ?
                    (op == NEMO_TRANSFER_OP_DELETE ?
                     _("Not deleted: something inside this folder could not be deleted.") :
                     _("Not moved to the trash: something inside this folder could not be moved.")) :
                    (op == NEMO_TRANSFER_OP_DELETE ? _("Not deleted: an error occurred and it was skipped.") :
                     _("Not moved to the trash: an error occurred and it was skipped.")));
            return DAKKAR_ITEM_FAILED;
        }
        *reason = g_strdup_printf (_("Not copied: %s"),
                                   e->error != NULL ? e->error : _("skipped"));
        return DAKKAR_ITEM_NOT_DONE;
    case NEMO_TRANSFER_FILE_COMPLETED:
        break;
    default:
        *reason = g_strdup (_("Not done: the job was cancelled before this item."));
        return DAKKAR_ITEM_NOT_DONE;
    }

    /* COMPLETED: done, without a checksum of its own */
    switch (op) {
    case NEMO_TRANSFER_OP_DELETE:
        *reason = g_strdup (_("Deleted."));
        return DAKKAR_ITEM_OK;
    case NEMO_TRANSFER_OP_TRASH:
        *reason = g_strdup (_("Moved to the trash."));
        return DAKKAR_ITEM_OK;
    case NEMO_TRANSFER_OP_LINK:
        *reason = g_strdup (_("Link created."));
        return DAKKAR_ITEM_OK;
    default:
        break;
    }
    if (dakkar_op_is_archive (op) && ar != NULL && ar->cli_checked && ar->cli_check_ok &&
        kind != DAKKAR_KIND_DIR) {
        *reason = g_strdup (_("Tested by the 7z program (CRC) as part of the archive."));
        return DAKKAR_ITEM_OK;
    }
    switch (kind) {
    case DAKKAR_KIND_DIR:
        *reason = g_strdup (_("Folder: created at the destination. A folder has no content of its own, so it cannot be checksummed; the files inside it are listed separately."));
        break;
    case DAKKAR_KIND_LINK:
        *reason = g_strdup (_("Symbolic link: copied as a link. Links are not checksummed."));
        break;
    case DAKKAR_KIND_SPECIAL:
        *reason = g_strdup (_("Special file (device, socket or pipe): not checksummed."));
        break;
    default:
        if (op == NEMO_TRANSFER_OP_MOVE && verify_on)
            *reason = g_strdup (_("Moved by renaming on the same drive: no data was copied, so there was nothing to verify."));
        else if (!verify_on && (op == NEMO_TRANSFER_OP_COPY || op == NEMO_TRANSFER_OP_MOVE))
            *reason = g_strdup (_("Copied, not verified: checksum verification was switched off."));
        else
            *reason = g_strdup (_("Done, not verified: no checksum was computed for this item."));
        break;
    }
    return DAKKAR_ITEM_UNVERIFIED;
}

/* Common source folder of all entries, so names can be shown relative to it. */
static gchar *
dakkar_items_base_uri (GPtrArray *files)
{
    gchar *base = NULL;
    guint i;
    for (i = 0; i < files->len; i++) {
        NemoTransferFileSnapshot *e = g_ptr_array_index (files, i);
        const gchar *uri = e->source_uri;
        if (uri == NULL) continue;
        if (base == NULL) {
            const gchar *slash = strrchr (uri, '/');
            base = slash != NULL ? g_strndup (uri, slash - uri) : g_strdup ("");
        }
        while (base[0] != '\0') {
            gsize len = strlen (base);
            gchar *slash;
            if (strncmp (uri, base, len) == 0 && uri[len] == '/') break;
            slash = strrchr (base, '/');
            if (slash == NULL) { base[0] = '\0'; break; }
            *slash = '\0';
        }
    }
    return base;
}

static gchar *
dakkar_items_relative (const DakkarItems *it, const gchar *uri)
{
    gsize len;
    gchar *rel;
    if (uri == NULL) return g_strdup ("");
    len = it->base_uri != NULL ? strlen (it->base_uri) : 0;
    if (len > 0 && strncmp (uri, it->base_uri, len) == 0 && uri[len] == '/')
        rel = g_uri_unescape_string (uri + len + 1, NULL);
    else
        rel = dakkar_parse_name_for_uri (uri);
    return rel != NULL ? rel : g_strdup (uri);
}

static DakkarItems *
dakkar_items_analyze (NemoProgressInfo *info, GPtrArray *files)
{
    DakkarItems *it = g_new0 (DakkarItems, 1);
    NemoTransferOperation op = nemo_progress_info_get_operation (info);
    gchar *algo = nemo_progress_info_dup_checksum_name (info);
    NemoArchiveResult *ar = g_object_get_data (G_OBJECT (info), "dakkar-archive-result");
    guint i;

    it->n = files->len;
    it->kind = g_new0 (guint8, MAX (it->n, 1));
    it->status = g_new0 (guint8, MAX (it->n, 1));
    it->reason = g_new0 (gchar *, MAX (it->n, 1));
    it->base_uri = dakkar_items_base_uri (files);
    it->verify_on = algo != NULL ||
        g_settings_get_boolean (nemo_preferences, "dakkar-transfer-verify-after-copy");
    for (i = 0; i < it->n; i++) {
        NemoTransferFileSnapshot *e = g_ptr_array_index (files, i);
        DakkarItemKind kind = dakkar_item_kind (e);
        DakkarItemStatus st = dakkar_item_classify (e, kind, op, algo, it->verify_on, ar,
                                                  &it->reason[i]);
        gboolean partial = e->state == NEMO_TRANSFER_FILE_SKIPPED &&
                           kind == DAKKAR_KIND_DIR && st == DAKKAR_ITEM_UNVERIFIED;
        gboolean done = e->state == NEMO_TRANSFER_FILE_COMPLETED ||
                        e->state == NEMO_TRANSFER_FILE_VERIFIED || partial;
        it->kind[i] = kind;
        it->status[i] = st;
        it->n_status[st]++;
        if (partial) it->partial_dirs++;
        if (done) {
            if (kind == DAKKAR_KIND_DIR) it->folders++;
            else if (kind == DAKKAR_KIND_LINK) it->links++;
            else if (kind == DAKKAR_KIND_SPECIAL) it->others++;
            else it->files++;
        }
        /* a checksum applies to regular (or not inspectable) files whose
         * data went through the copy; a rename-move or a deletion has none */
        if (e->state == NEMO_TRANSFER_FILE_VERIFIED ||
            e->state == NEMO_TRANSFER_FILE_VERIFY_FAILED ||
            (g_strcmp0 (e->error, _("Verification skipped by user")) == 0 &&
             e->state == NEMO_TRANSFER_FILE_SKIPPED) ||
            (e->state == NEMO_TRANSFER_FILE_COMPLETED && st == DAKKAR_ITEM_UNVERIFIED &&
             (kind == DAKKAR_KIND_FILE || kind == DAKKAR_KIND_UNKNOWN) &&
             !(op == NEMO_TRANSFER_OP_MOVE && it->verify_on)))
            it->eligible++;
    }
    g_free (algo);
    return it;
}

/* A partially copied folder exists at the destination: count it as done,
 * not as skipped, so "items done" and the folder count agree. */
static void
dakkar_counts_adjust (DakkarCounts *c, DakkarItems *it, NemoTransferOperation op)
{
    guint n = MIN (it->partial_dirs, c->skipped);
    c->skipped -= n;
    c->succeeded += n;
    c->created += n;
    if (op == NEMO_TRANSFER_OP_DELETE || op == NEMO_TRANSFER_OP_TRASH) {
        /* only skipped after an error: these are failures */
        c->failed += c->skipped;
        c->skipped = 0;
    }
}

/* "26 files, 1 folder" */
static gchar *
dakkar_items_breakdown (DakkarItems *it)
{
    GString *s = g_string_new (NULL);
    guint parts[4];
    guint k;
    parts[0] = it->files;
    parts[1] = it->folders;
    parts[2] = it->links;
    parts[3] = it->others;
    for (k = 0; k < 4; k++) {
        const gchar *fmt;
        if (parts[k] == 0) continue;
        if (s->len > 0) g_string_append (s, ", ");
        switch (k) {
        case 0:  fmt = ngettext ("%u file", "%u files", parts[k]); break;
        case 1:  fmt = ngettext ("%u folder", "%u folders", parts[k]); break;
        case 2:  fmt = ngettext ("%u link", "%u links", parts[k]); break;
        default: fmt = ngettext ("%u special file", "%u special files", parts[k]); break;
        }
        g_string_append_printf (s, fmt, parts[k]);
    }
    return g_string_free (s, FALSE);
}

static const gchar *
dakkar_item_status_word (DakkarItemStatus st)
{
    switch (st) {
    case DAKKAR_ITEM_OK:         return _("OK");
    case DAKKAR_ITEM_UNVERIFIED: return _("NOT VERIFIED");
    case DAKKAR_ITEM_FAILED:     return _("FAILED");
    default:                    return _("NOT DONE");
    }
}

/* ---------- completion summary ---------- */

static gchar *
dakkar_build_log (NemoProgressInfo *info, GPtrArray *files, DakkarCounts *c)
{
    GString *log = g_string_new (NULL);
    NemoTransferByteStats stats;
    gchar *duration = nemo_transfer_format_duration (nemo_progress_info_get_elapsed_us (info));
    gchar *algo = nemo_progress_info_dup_checksum_name (info);
    DakkarItems *it = dakkar_items_analyze (info, files);
    gchar *breakdown = dakkar_items_breakdown (it);
    gboolean truncated = FALSE;
    DakkarCounts adjusted = *c;
    guint i;

    dakkar_counts_adjust (&adjusted, it, nemo_progress_info_get_operation (info));
    c = &adjusted;

    {
        GFile *dest = nemo_progress_info_get_destination (info);
        gchar *where = dest != NULL ? g_file_get_parse_name (dest) : NULL;
        gchar *from = it->base_uri != NULL && it->base_uri[0] != '\0' ?
                      dakkar_parse_name_for_uri (it->base_uri) : NULL;
        /* Translators: log header, e.g. "Copying → /home/me/Backup" */
        g_string_append_printf (log, _("%s → %s\n"),
                                dakkar_operation_verb (nemo_progress_info_get_operation (info)),
                                where != NULL ? where : "—");
        if (from != NULL)
            /* Translators: log line, the common source folder */
            g_string_append_printf (log, _("From: %s\n"), from);
        g_free (from);
        g_free (where);
        g_clear_object (&dest);
    }
    g_string_append_printf (log, _("Succeeded: %u, failed: %u, skipped: %u, new: %u, overwritten: %u\n"),
                            c->succeeded, c->failed, c->skipped, c->created, c->overwritten);
    if (breakdown[0] != '\0')
        /* Translators: log line, e.g. "Items done: 27 (26 files, 1 folder)" */
        g_string_append_printf (log, _("Items done: %u (%s)\n"), c->succeeded, breakdown);
    if (dakkar_verify_method (info) == DAKKAR_VERIFY_METHOD_CRC7Z) {
        /* the 7z program did the check: no per-file hashes to list */
        g_string_append (log, _("Checksum (CRC, 7-Zip): tested by the 7z program\n"));
    } else {
        gchar *name = dakkar_verify_method (info) != 0 ? dakkar_verify_method_name (info) : NULL;
        g_string_append_printf (log, _("Checksum (%s): %u verified, %u mismatched\n"),
                                name != NULL && algo != NULL ? name : algo != NULL ? algo : _("off"),
                                c->verified, c->mismatched);
        if (algo != NULL)
            /* Translators: log line, e.g. "Files verified (SHA-1): 26 of 26 files" */
            g_string_append_printf (log, ngettext ("Files verified (%s): %u of %u file\n",
                                                   "Files verified (%s): %u of %u files\n",
                                                   it->eligible),
                                    algo, c->verified, it->eligible);
        g_free (name);
    }
    if (nemo_progress_info_get_byte_stats (info, &stats)) {
        gchar *size = g_format_size (c->bytes);
        g_string_append_printf (log, _("Total size: %s, duration: %s\n"), size, duration);
        g_free (size);
    } else {
        g_string_append_printf (log, _("Duration: %s\n"), duration);
    }
    g_string_append_printf (log, _("Results: %u OK, %u not verified, %u failed, %u not done\n"),
                            it->n_status[DAKKAR_ITEM_OK], it->n_status[DAKKAR_ITEM_UNVERIFIED],
                            it->n_status[DAKKAR_ITEM_FAILED], it->n_status[DAKKAR_ITEM_NOT_DONE]);
    g_string_append (log, "\n");
    for (i = 0; i < files->len; i++) {
        NemoTransferFileSnapshot *e = g_ptr_array_index (files, i);
        gchar *rel = dakkar_items_relative (it, e->source_uri);
        gchar *dest = dakkar_parse_name_for_uri (e->destination_uri);
        gchar *size = e->total >= 0 && it->kind[i] != DAKKAR_KIND_DIR ?
                      g_format_size (e->total) : g_strdup ("");
        g_string_append_printf (log, "%s\t%s%s\t%s%s%s%s%s\n",
            dakkar_item_status_word (it->status[i]),
            rel, it->kind[i] == DAKKAR_KIND_DIR ? "/" : "",
            size,
            dest != NULL ? "\t→ " : "", dest != NULL ? dest : "",
            e->overwritten ? "\t" : "", e->overwritten ? _("overwritten") : "");
        g_string_append_printf (log, "\t%s\n", it->reason[i]);
        if (e->sha256 != NULL)
            g_string_append_printf (log, "\t%s %s\n", algo != NULL ? algo : "", e->sha256);
        g_free (size);
        g_free (rel);
        g_free (dest);
    }
    g_ptr_array_unref (nemo_progress_info_dup_file_manifest (info, 1, &truncated, NULL));
    if (truncated)
        g_string_append (log, _("(The list is incomplete: only the first 8192 items are kept.)\n"));
    dakkar_items_free (it);
    g_free (breakdown);
    g_free (duration);
    g_free (algo);
    return g_string_free (log, FALSE);
}

static void
dakkar_summary_row (GtkWidget *grid, gint row, const gchar *caption, const gchar *value)
{
    GtkWidget *k = gtk_label_new (caption), *v = gtk_label_new (value);
    gtk_label_set_xalign (GTK_LABEL (k), 1.0);
    gtk_label_set_xalign (GTK_LABEL (v), 0.0);
    gtk_label_set_ellipsize (GTK_LABEL (v), PANGO_ELLIPSIZE_MIDDLE);
    gtk_widget_set_hexpand (v, TRUE);
    gtk_style_context_add_class (gtk_widget_get_style_context (k), "dim-label");
    gtk_grid_attach (GTK_GRID (grid), k, 0, row, 1, 1);
    gtk_grid_attach (GTK_GRID (grid), v, 1, row, 1, 1);
}

/* ---- summary look: theme colours only ---- */

enum {
    SUM_ICON,       /* GIcon: file type */
    SUM_NAME,       /* markup: dimmed relative folder + name */
    SUM_SIZE,
    SUM_STATUS_PIX, /* GdkPixbuf: coloured status icon */
    SUM_TIP,        /* markup tooltip */
    SUM_STATUS,     /* DakkarItemStatus */
    SUM_DEST_URI,
    SUM_SOURCE_URI,
    SUM_N_COLS
};

/* A named colour of the current theme; the theme's text colour otherwise. */
static void
dakkar_transfer_theme_color (GtkWidget *widget, const gchar *name, const gchar *alt, GdkRGBA *out)
{
    GtkStyleContext *ctx = gtk_widget_get_style_context (widget);
    if (gtk_style_context_lookup_color (ctx, name, out)) return;
    if (alt != NULL && gtk_style_context_lookup_color (ctx, alt, out)) return;
    gtk_style_context_get_color (ctx, gtk_style_context_get_state (ctx), out);
}

static void
dakkar_status_color (GtkWidget *widget, DakkarItemStatus st, GdkRGBA *out)
{
    switch (st) {
    case DAKKAR_ITEM_OK:         dakkar_transfer_theme_color (widget, "success_color", "theme_selected_bg_color", out); break;
    case DAKKAR_ITEM_UNVERIFIED: dakkar_transfer_theme_color (widget, "warning_color", NULL, out); break;
    case DAKKAR_ITEM_FAILED:     dakkar_transfer_theme_color (widget, "error_color", NULL, out); break;
    default:
        dakkar_transfer_theme_color (widget, "insensitive_fg_color", "unfocused_insensitive_color", out);
        break;
    }
}

static const gchar *
dakkar_status_icon_name (DakkarItemStatus st)
{
    switch (st) {
    case DAKKAR_ITEM_OK:
        return dakkar_icon_pick ("emblem-ok-symbolic", "object-select-symbolic",
                                "checkbox-checked-symbolic", NULL);
    case DAKKAR_ITEM_UNVERIFIED:
        return dakkar_icon_pick ("dialog-question-symbolic", "help-contents-symbolic",
                                "help-browser-symbolic", "dialog-information-symbolic", NULL);
    case DAKKAR_ITEM_FAILED:
        return dakkar_icon_pick ("window-close-symbolic", "dialog-error-symbolic",
                                "process-stop-symbolic", NULL);
    default:
        return dakkar_icon_pick ("media-playback-stop-symbolic", "list-remove-symbolic", NULL);
    }
}

/* A symbolic theme icon drawn in a theme colour (NULL when the theme lacks it). */
static GdkPixbuf *
dakkar_colored_icon (GtkWidget *widget, const gchar *icon_name, const GdkRGBA *color, gint size)
{
    GtkIconTheme *theme = gtk_icon_theme_get_for_screen (gtk_widget_get_screen (widget));
    gint scale = gtk_widget_get_scale_factor (widget);
    GtkIconInfo *info = gtk_icon_theme_lookup_icon_for_scale (theme, icon_name, size, scale,
                                                              GTK_ICON_LOOKUP_FORCE_SIZE);
    GdkPixbuf *pixbuf = NULL;
    if (info == NULL) return NULL;
    pixbuf = gtk_icon_info_load_symbolic (info, color, color, color, color, NULL, NULL);
    g_object_unref (info);
    return pixbuf;
}

static GtkWidget *
dakkar_stat_card (const gchar *value, const gchar *caption, const gchar *note,
                 const GdkRGBA *value_color)
{
    GtkWidget *card = gtk_box_new (GTK_ORIENTATION_VERTICAL, 1);
    GtkWidget *v = gtk_label_new (value), *c = gtk_label_new (caption);
    PangoAttrList *attrs = pango_attr_list_new ();
    pango_attr_list_insert (attrs, pango_attr_weight_new (PANGO_WEIGHT_BOLD));
    pango_attr_list_insert (attrs, pango_attr_scale_new (1.35));
    if (value_color != NULL)
        pango_attr_list_insert (attrs, pango_attr_foreground_new (
            (guint16) (value_color->red * 65535), (guint16) (value_color->green * 65535),
            (guint16) (value_color->blue * 65535)));
    gtk_label_set_attributes (GTK_LABEL (v), attrs);
    pango_attr_list_unref (attrs);
    gtk_label_set_xalign (GTK_LABEL (v), 0.0);
    gtk_label_set_ellipsize (GTK_LABEL (v), PANGO_ELLIPSIZE_END);
    gtk_label_set_xalign (GTK_LABEL (c), 0.0);
    gtk_label_set_ellipsize (GTK_LABEL (c), PANGO_ELLIPSIZE_END);
    gtk_style_context_add_class (gtk_widget_get_style_context (c), "dim-label");
    gtk_style_context_add_class (gtk_widget_get_style_context (card), "dakkar-stat-card");
    gtk_box_pack_start (GTK_BOX (card), v, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (card), c, FALSE, FALSE, 0);
    if (note != NULL && note[0] != '\0') {
        GtkWidget *n = gtk_label_new (note);
        PangoAttrList *small = pango_attr_list_new ();
        pango_attr_list_insert (small, pango_attr_scale_new (0.85));
        gtk_label_set_attributes (GTK_LABEL (n), small);
        pango_attr_list_unref (small);
        gtk_label_set_xalign (GTK_LABEL (n), 0.0);
        gtk_label_set_line_wrap (GTK_LABEL (n), TRUE);
        gtk_label_set_max_width_chars (GTK_LABEL (n), 26);
        gtk_style_context_add_class (gtk_widget_get_style_context (n), "dim-label");
        gtk_box_pack_start (GTK_BOX (card), n, FALSE, FALSE, 0);
    }
    gtk_widget_set_hexpand (card, TRUE);
    return card;
}

static void
dakkar_summary_style_ensure (void)
{
    static GtkCssProvider *provider;
    if (provider != NULL) return;
    provider = gtk_css_provider_new ();
    /* currentColor = the theme's own text colour; no fixed colours */
    gtk_css_provider_load_from_data (provider,
        "#dakkar-transfer-summary .dakkar-stat-card {"
        "  border: 1px solid alpha(currentColor, 0.16);"
        "  border-radius: 6px;"
        "  padding: 7px 10px;"
        "  background-color: alpha(currentColor, 0.04);"
        "}", -1, NULL);
    gtk_style_context_add_provider_for_screen (gdk_screen_get_default (),
        GTK_STYLE_PROVIDER (provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
}

typedef struct {
    NemoProgressInfo *info;
    GtkWidget *dialog;
    GtkWidget *retry_button;
    GtkWidget *view;
    GtkTreeModel *filter;
    gboolean problems_only;
} DakkarSummary;

static void
dakkar_summary_free (gpointer data)
{
    DakkarSummary *s = data;
    g_clear_object (&s->info);
    g_clear_object (&s->filter);
    g_free (s);
}

static gboolean
dakkar_summary_visible (GtkTreeModel *model, GtkTreeIter *iter, gpointer data G_GNUC_UNUSED)
{
    gint st = DAKKAR_ITEM_OK;
    /* state lives on the model itself: no pointer to the dialog's struct */
    if (g_object_get_data (G_OBJECT (model), "dakkar-problems-only") == NULL)
        return TRUE; /* @model is the child list store */
    gtk_tree_model_get (model, iter, SUM_STATUS, &st, -1);
    return st != DAKKAR_ITEM_OK;
}

static void
dakkar_summary_filter_toggled (GtkToggleButton *button, DakkarSummary *s)
{
    if (!gtk_toggle_button_get_active (button)) return;
    s->problems_only = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (button), "dakkar-problems"));
    if (s->filter != NULL) {
        g_object_set_data (G_OBJECT (gtk_tree_model_filter_get_model (
                               GTK_TREE_MODEL_FILTER (s->filter))),
                           "dakkar-problems-only", GINT_TO_POINTER (s->problems_only));
        gtk_tree_model_filter_refilter (GTK_TREE_MODEL_FILTER (s->filter));
    }
}

/* Open the folder that holds the item (the copy when it exists, else the
 * source), with the item selected. */
static void
dakkar_summary_open_folder (DakkarSummary *s, GtkTreeIter *iter)
{
    gchar *dest_uri = NULL, *source_uri = NULL;
    const gchar *pick[2];
    guint k;
    gtk_tree_model_get (s->filter, iter, SUM_DEST_URI, &dest_uri, SUM_SOURCE_URI, &source_uri, -1);
    pick[0] = dest_uri;
    pick[1] = source_uri;
    for (k = 0; k < 2; k++) {
        GFile *file, *parent;
        if (pick[k] == NULL) continue;
        file = g_file_new_for_uri (pick[k]);
        parent = g_file_get_parent (file);
        if (parent != NULL &&
            (!g_file_is_native (file) ||
             g_file_query_exists (file, NULL) || g_file_query_exists (parent, NULL))) {
            gboolean exists = !g_file_is_native (file) || g_file_query_exists (file, NULL);
            nemo_application_open_location (nemo_application_get_singleton (), parent,
                                            exists ? file : NULL, NULL, FALSE);
            g_object_unref (parent);
            g_object_unref (file);
            break;
        }
        g_clear_object (&parent);
        g_object_unref (file);
    }
    g_free (dest_uri);
    g_free (source_uri);
}

static void
dakkar_summary_row_activated (GtkTreeView *view, GtkTreePath *path,
                             GtkTreeViewColumn *column, DakkarSummary *s)
{
    GtkTreeIter iter;
    if (gtk_tree_model_get_iter (s->filter, &iter, path))
        dakkar_summary_open_folder (s, &iter);
}

static void
dakkar_summary_menu_open (GtkMenuItem *item, DakkarSummary *s)
{
    GtkTreeIter iter;
    GtkTreeSelection *sel = gtk_tree_view_get_selection (GTK_TREE_VIEW (s->view));
    if (gtk_tree_selection_get_selected (sel, NULL, &iter))
        dakkar_summary_open_folder (s, &iter);
}

static gboolean
dakkar_summary_button_press (GtkWidget *view, GdkEventButton *event, DakkarSummary *s)
{
    GtkTreePath *path = NULL;
    GtkWidget *menu, *item;
    if (event->type != GDK_BUTTON_PRESS || event->button != GDK_BUTTON_SECONDARY)
        return FALSE;
    if (!gtk_tree_view_get_path_at_pos (GTK_TREE_VIEW (view), (gint) event->x, (gint) event->y,
                                        &path, NULL, NULL, NULL))
        return FALSE;
    gtk_tree_selection_select_path (gtk_tree_view_get_selection (GTK_TREE_VIEW (view)), path);
    gtk_tree_path_free (path);
    menu = gtk_menu_new ();
    item = gtk_menu_item_new_with_mnemonic (_("Open _Containing Folder"));
    g_signal_connect (item, "activate", G_CALLBACK (dakkar_summary_menu_open), s);
    gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
    gtk_widget_show_all (menu);
    gtk_menu_attach_to_widget (GTK_MENU (menu), view, NULL);
    g_signal_connect (menu, "deactivate", G_CALLBACK (gtk_widget_destroy), NULL);
    gtk_menu_popup_at_pointer (GTK_MENU (menu), (GdkEvent *) event);
    return TRUE;
}

static void
dakkar_summary_remember_size (GtkWidget *dialog)
{
    gint w = 0, h = 0;
    gtk_window_get_size (GTK_WINDOW (dialog), &w, &h);
    if (w > 0 && h > 0) {
        g_settings_set_int (nemo_preferences, "dakkar-transfer-summary-width", CLAMP (w, 420, 4000));
        g_settings_set_int (nemo_preferences, "dakkar-transfer-summary-height", CLAMP (h, 260, 4000));
    }
}

/* Retry only entries whose destination is the plain name in an existing
 * folder: a new native Nemo job, so conflicts are asked again (choose
 * "Overwrite" to replace a copy that failed verification). */
static guint
dakkar_retry_failed (NemoProgressInfo *info, GtkWindow *parent)
{
    GPtrArray *files = nemo_progress_info_dup_file_manifest (info, 0, NULL, NULL);
    GHashTable *by_dir = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
    NemoTransferOperation op = nemo_progress_info_get_operation (info);
    GHashTableIter it;
    gpointer key, value;
    guint i, started = 0;

    for (i = 0; i < files->len; i++) {
        NemoTransferFileSnapshot *e = g_ptr_array_index (files, i);
        GFile *src, *dest, *dir;
        if ((e->state != NEMO_TRANSFER_FILE_FAILED &&
             e->state != NEMO_TRANSFER_FILE_VERIFY_FAILED) ||
            e->source_uri == NULL || e->destination_uri == NULL)
            continue;
        src = g_file_new_for_uri (e->source_uri);
        dest = g_file_new_for_uri (e->destination_uri);
        dir = g_file_get_parent (dest);
        if (dir != NULL && g_file_query_exists (src, NULL) &&
            g_file_query_file_type (dir, 0, NULL) == G_FILE_TYPE_DIRECTORY) {
            gchar *dir_uri = g_file_get_uri (dir);
            GList *list = g_hash_table_lookup (by_dir, dir_uri);
            if (retried_sources == NULL)
                retried_sources = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
            g_hash_table_add (retried_sources, g_strdup (e->source_uri));
            list = g_list_append (list, g_object_ref (src));
            g_hash_table_replace (by_dir, dir_uri, list);
        }
        g_clear_object (&dir);
        g_object_unref (dest);
        g_object_unref (src);
    }
    g_hash_table_iter_init (&it, by_dir);
    while (g_hash_table_iter_next (&it, &key, &value)) {
        GFile *dir = g_file_new_for_uri (key);
        GList *list = value;
        started += g_list_length (list);
        if (op == NEMO_TRANSFER_OP_MOVE)
            nemo_file_operations_move (list, NULL, dir, parent, NULL, NULL);
        else
            nemo_file_operations_copy (list, NULL, dir, parent, NULL, NULL);
        g_list_free_full (list, g_object_unref);
        g_object_unref (dir);
    }
    g_hash_table_unref (by_dir);
    g_ptr_array_unref (files);
    return started;
}

#define DAKKAR_RESPONSE_COPY_LOG 10
#define DAKKAR_RESPONSE_RETRY    11

static void
dakkar_summary_response (GtkDialog *dialog, gint response, DakkarSummary *s)
{
    if (response == DAKKAR_RESPONSE_COPY_LOG) {
        GPtrArray *files = nemo_progress_info_dup_file_manifest (s->info, 0, NULL, NULL);
        DakkarCounts c;
        gchar *log;
        dakkar_count (files, &c);
        log = dakkar_build_log (s->info, files, &c);
        gtk_clipboard_set_text (gtk_widget_get_clipboard (GTK_WIDGET (dialog),
                                                          GDK_SELECTION_CLIPBOARD), log, -1);
        g_free (log);
        g_ptr_array_unref (files);
        return; /* keep the summary open */
    }
    if (response == DAKKAR_RESPONSE_RETRY) {
        dakkar_retry_failed (s->info, NULL);
        gtk_widget_set_sensitive (s->retry_button, FALSE);
        return;
    }
    dakkar_summary_remember_size (GTK_WIDGET (dialog));
    gtk_widget_destroy (GTK_WIDGET (dialog));
}

/* "27 items transferred", or "25 of 27 items transferred" when some were not done */
static gchar *
dakkar_summary_title (NemoTransferOperation op, guint done, guint total)
{
    if (total > done) {
        switch (op) {
        case NEMO_TRANSFER_OP_MOVE:
            return g_strdup_printf (ngettext ("%u of %u item moved", "%u of %u items moved", total), done, total);
        case NEMO_TRANSFER_OP_DELETE:
            return g_strdup_printf (ngettext ("%u of %u item deleted", "%u of %u items deleted", total), done, total);
        case NEMO_TRANSFER_OP_TRASH:
            return g_strdup_printf (ngettext ("%u of %u item moved to the trash", "%u of %u items moved to the trash", total), done, total);
        case NEMO_TRANSFER_OP_COMPRESS:
            return g_strdup_printf (ngettext ("%u of %u item added to the archive", "%u of %u items added to the archive", total), done, total);
        case NEMO_TRANSFER_OP_EXTRACT:
            return g_strdup_printf (ngettext ("%u of %u item extracted", "%u of %u items extracted", total), done, total);
        default:
            return g_strdup_printf (ngettext ("%u of %u item transferred", "%u of %u items transferred", total), done, total);
        }
    }
    switch (op) {
    case NEMO_TRANSFER_OP_MOVE:
        return g_strdup_printf (ngettext ("%u item moved", "%u items moved", done), done);
    case NEMO_TRANSFER_OP_DELETE:
        return g_strdup_printf (ngettext ("%u item deleted", "%u items deleted", done), done);
    case NEMO_TRANSFER_OP_TRASH:
        return g_strdup_printf (ngettext ("%u item moved to the trash", "%u items moved to the trash", done), done);
    case NEMO_TRANSFER_OP_COMPRESS:
        return g_strdup_printf (ngettext ("%u item added to the archive", "%u items added to the archive", done), done);
    case NEMO_TRANSFER_OP_EXTRACT:
        return g_strdup_printf (ngettext ("%u item extracted", "%u items extracted", done), done);
    default:
        return g_strdup_printf (ngettext ("%u item transferred", "%u items transferred", done), done);
    }
}

/* Everything the summary dialog shows, gathered once from the finished job. */
typedef struct {
    NemoProgressInfo *info;
    NemoTransferOperation op;
    GPtrArray *files;
    DakkarCounts c;
    DakkarItems *it;
    NemoArchiveResult *ar;       /* compress/extract result, or NULL */
    gchar *algo;                 /* checksum name, NULL when verification was off */
    NemoTransferByteStats stats;
    gboolean have_bytes;
    gboolean cancelled;
    gboolean truncated;          /* manifest capped, the list is incomplete */
    gboolean is_copy_like;
    gboolean is_archive;
    gboolean all_verified;
    gint64 elapsed;
    GtkWidget *dialog;
} SummaryData;

static void
summary_data_init (SummaryData *d, NemoProgressInfo *info)
{
    GCancellable *cancellable = nemo_progress_info_get_cancellable (info);

    memset (d, 0, sizeof (*d));
    d->info = info;
    d->files = nemo_progress_info_dup_file_manifest (info, 0, NULL, NULL);
    d->op = nemo_progress_info_get_operation (info);
    d->have_bytes = nemo_progress_info_get_byte_stats (info, &d->stats);
    d->cancelled = cancellable != NULL && g_cancellable_is_cancelled (cancellable);
    d->elapsed = nemo_progress_info_get_elapsed_us (info);
    d->algo = nemo_progress_info_dup_checksum_name (info);
    d->is_copy_like = d->op == NEMO_TRANSFER_OP_COPY || d->op == NEMO_TRANSFER_OP_MOVE;
    d->is_archive = dakkar_op_is_archive (d->op);
    d->ar = g_object_get_data (G_OBJECT (info), "dakkar-archive-result");
    g_clear_object (&cancellable);

    if (d->files == NULL) d->files = g_ptr_array_new ();
    dakkar_count (d->files, &d->c);
    d->it = dakkar_items_analyze (info, d->files);
    dakkar_counts_adjust (&d->c, d->it, d->op);
    d->all_verified = d->it->eligible > 0 && d->c.verified == d->it->eligible && d->c.mismatched == 0;
    /* only asked for the truncation flag */
    g_ptr_array_unref (nemo_progress_info_dup_file_manifest (info, 1, &d->truncated, NULL));
}

static void
summary_data_clear (SummaryData *d)
{
    dakkar_items_free (d->it);
    g_free (d->algo);
    g_ptr_array_unref (d->files);
}

static const gchar *
summary_window_title (const SummaryData *d)
{
    const DakkarCounts *c = &d->c;

    if (d->op == NEMO_TRANSFER_OP_COMPRESS)
        return d->cancelled ? _("Compression cancelled") :
               (c->failed > 0 || (d->ar != NULL && !d->ar->created) ?
                _("Compression finished with errors") : _("Compression complete"));
    if (d->op == NEMO_TRANSFER_OP_EXTRACT)
        return d->cancelled ? _("Extraction cancelled") :
               (c->failed > 0 ? _("Extraction finished with errors") : _("Extraction complete"));
    return d->cancelled ? _("Transfer cancelled") :
           (c->failed > 0 ? _("Transfer finished with errors") : _("Transfer complete"));
}

/* Colour and icon of the header: failed beats unverified beats OK. */
static DakkarItemStatus
summary_overall_status (const SummaryData *d)
{
    const DakkarCounts *c = &d->c;
    const NemoArchiveResult *ar = d->ar;

    if (c->failed > 0 || c->mismatched > 0 ||
        (d->op == NEMO_TRANSFER_OP_COMPRESS && ar != NULL && !ar->created && !d->cancelled) ||
        (ar != NULL && ar->cli_checked && !ar->cli_check_ok))
        return DAKKAR_ITEM_FAILED;
    if (d->cancelled || c->skipped > 0 || c->cancelled > 0 ||
        (d->it->verify_on && d->it->eligible > c->verified))
        return DAKKAR_ITEM_UNVERIFIED;
    return DAKKAR_ITEM_OK;
}

/* The one-line verdict under the title, most important outcome first. */
static gchar *
summary_verdict (const SummaryData *d)
{
    const DakkarCounts *c = &d->c;
    const NemoArchiveResult *ar = d->ar;
    const DakkarItems *it = d->it;
    NemoTransferOperation op = d->op;

    if (!d->cancelled && c->failed == 0 && c->skipped == 0 && c->succeeded > 0 &&
        d->all_verified && d->is_copy_like)
        return g_strdup (_("Checksum verified — copy fully successful"));
    if (op == NEMO_TRANSFER_OP_COMPRESS && d->cancelled)
        return g_strdup (_("Compression was cancelled; no archive was created"));
    if (op == NEMO_TRANSFER_OP_EXTRACT && d->cancelled)
        return g_strdup (_("Extraction was cancelled; the unfinished folder was removed"));
    if (op == NEMO_TRANSFER_OP_COMPRESS && ar != NULL && !ar->created)
        /* Translators: %s is the reason, e.g. "No file could be added…" */
        return g_strdup_printf (_("The archive was not created: %s"),
                                ar->error != NULL ? ar->error : _("unknown error"));
    if (d->is_archive && !d->cancelled && c->failed == 0 && c->skipped == 0 && c->mismatched == 0 &&
        ((c->verified > 0 && d->algo != NULL) || (ar != NULL && ar->cli_checked)) &&
        !(ar != NULL && ar->cli_checked && !ar->cli_check_ok))
        return g_strdup (op == NEMO_TRANSFER_OP_COMPRESS ?
                         _("Archive verified — all files intact") :
                         _("Checksum verified — extraction fully successful"));
    if (op == NEMO_TRANSFER_OP_COMPRESS && c->failed == 0 && c->skipped == 0)
        return g_strdup (_("Archive created"));
    if (op == NEMO_TRANSFER_OP_EXTRACT && c->failed == 0 && c->skipped == 0 && !d->cancelled)
        return g_strdup (_("Archive extracted"));
    if (d->cancelled)
        return g_strdup (_("The transfer was cancelled"));
    if (c->mismatched > 0)
        return g_strdup_printf (ngettext ("%u file failed checksum verification",
                                          "%u files failed checksum verification",
                                          c->mismatched), c->mismatched);
    if (c->failed > 0 && op == NEMO_TRANSFER_OP_COMPRESS)
        return g_strdup_printf (ngettext ("%u item could not be added to the archive",
                                          "%u items could not be added to the archive",
                                          c->failed), c->failed);
    if (c->failed > 0 && op == NEMO_TRANSFER_OP_EXTRACT)
        return g_strdup_printf (ngettext ("%u item could not be extracted",
                                          "%u items could not be extracted",
                                          c->failed), c->failed);
    if (c->failed > 0 && op == NEMO_TRANSFER_OP_DELETE)
        return g_strdup_printf (ngettext ("%u item could not be deleted",
                                          "%u items could not be deleted",
                                          c->failed), c->failed);
    if (c->failed > 0 && op == NEMO_TRANSFER_OP_TRASH)
        return g_strdup_printf (ngettext ("%u item could not be moved to the trash",
                                          "%u items could not be moved to the trash",
                                          c->failed), c->failed);
    if (c->failed > 0 && op == NEMO_TRANSFER_OP_MOVE)
        return g_strdup_printf (ngettext ("%u item could not be moved",
                                          "%u items could not be moved",
                                          c->failed), c->failed);
    if (c->failed > 0)
        return g_strdup_printf (ngettext ("%u item could not be transferred",
                                          "%u items could not be transferred",
                                          c->failed), c->failed);
    if (c->skipped > 0)
        return g_strdup_printf (ngettext ("%u item was skipped", "%u items were skipped",
                                          c->skipped), c->skipped);
    if (d->is_copy_like && it->eligible > 0 && d->algo == NULL)
        return g_strdup (_("Done — checksum verification was off"));
    if (d->is_copy_like && it->eligible > c->verified)
        return g_strdup_printf (ngettext ("Done — %u file was not verified",
                                          "Done — %u files were not verified",
                                          it->eligible - c->verified), it->eligible - c->verified);
    return g_strdup_printf (ngettext ("%u item done", "%u items done", c->succeeded),
                            c->succeeded);
}

/* "source → destination", or whichever of the two is known; "" for none. */
static gchar *
summary_folders_text (const SummaryData *d)
{
    GFile *dest = nemo_progress_info_get_destination (d->info);
    gchar *to = dest != NULL ? g_file_get_parse_name (dest) : NULL;
    gchar *from = d->it->base_uri != NULL && d->it->base_uri[0] != '\0' ?
                  dakkar_parse_name_for_uri (d->it->base_uri) : NULL;
    gchar *text;

    g_clear_object (&dest);
    if (from != NULL && to != NULL && !d->is_archive)
        /* Translators: "source folder → destination folder" */
        text = g_strdup_printf (_("%s → %s"), from, to);
    else if (to != NULL)
        text = g_strdup (to);
    else
        text = g_strdup (from != NULL ? from : "");
    g_free (from);
    g_free (to);
    return text;
}

/* Status icon, "27 items transferred", the verdict and the folders. */
static GtkWidget *
summary_header_new (const SummaryData *d)
{
    DakkarItemStatus overall = summary_overall_status (d);
    GtkWidget *dialog = d->dialog;
    const gchar *name;
    GtkWidget *header, *titles, *w;
    GdkPixbuf *big;
    GdkRGBA color;
    PangoAttrList *attrs;
    gchar *text;

    header = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 14);
    name = overall == DAKKAR_ITEM_FAILED ?
               dakkar_icon_pick ("dialog-error-symbolic", "dialog-error", NULL) :
           overall == DAKKAR_ITEM_UNVERIFIED ?
               (d->cancelled ? dakkar_icon_pick ("process-stop-symbolic", "dialog-warning-symbolic", NULL) :
                               dakkar_icon_pick ("dialog-warning-symbolic", "dialog-warning", NULL)) :
               dakkar_icon_pick ("emblem-ok-symbolic", "object-select-symbolic",
                                "dialog-information-symbolic", NULL);
    dakkar_status_color (dialog, overall, &color);
    big = dakkar_colored_icon (dialog, name, &color, 48);
    w = big != NULL ? gtk_image_new_from_pixbuf (big) :
                      gtk_image_new_from_icon_name (name, GTK_ICON_SIZE_DIALOG);
    g_clear_object (&big);
    gtk_widget_set_valign (w, GTK_ALIGN_START);
    gtk_box_pack_start (GTK_BOX (header), w, FALSE, FALSE, 0);

    titles = gtk_box_new (GTK_ORIENTATION_VERTICAL, 3);
    gtk_box_pack_start (GTK_BOX (header), titles, TRUE, TRUE, 0);

    text = dakkar_summary_title (d->op, d->c.succeeded,
                                 d->c.succeeded + d->c.failed + d->c.skipped + d->c.cancelled);
    w = gtk_label_new (text);
    g_free (text);
    gtk_label_set_xalign (GTK_LABEL (w), 0.0);
    gtk_label_set_ellipsize (GTK_LABEL (w), PANGO_ELLIPSIZE_END);
    attrs = pango_attr_list_new ();
    pango_attr_list_insert (attrs, pango_attr_weight_new (PANGO_WEIGHT_BOLD));
    pango_attr_list_insert (attrs, pango_attr_scale_new (1.45));
    gtk_label_set_attributes (GTK_LABEL (w), attrs);
    pango_attr_list_unref (attrs);
    gtk_box_pack_start (GTK_BOX (titles), w, FALSE, FALSE, 0);

    text = summary_verdict (d);
    w = gtk_label_new (text);
    g_free (text);
    gtk_label_set_xalign (GTK_LABEL (w), 0.0);
    gtk_label_set_line_wrap (GTK_LABEL (w), TRUE);
    gtk_box_pack_start (GTK_BOX (titles), w, FALSE, FALSE, 0);

    text = summary_folders_text (d);
    if (text[0] != '\0') {
        w = gtk_label_new (text);
        gtk_label_set_xalign (GTK_LABEL (w), 0.0);
        gtk_label_set_ellipsize (GTK_LABEL (w), PANGO_ELLIPSIZE_MIDDLE);
        gtk_label_set_selectable (GTK_LABEL (w), TRUE);
        gtk_widget_set_can_focus (w, FALSE);
        gtk_widget_set_tooltip_text (w, text);
        gtk_style_context_add_class (gtk_widget_get_style_context (w), "dim-label");
        gtk_box_pack_start (GTK_BOX (titles), w, FALSE, FALSE, 0);
    }
    g_free (text);
    return header;
}

/* Cards are laid out three per row in the order they are added. */
static void
summary_card_add (GtkWidget *cards, gint *n, const gchar *value, const gchar *caption,
                  const gchar *note, const GdkRGBA *color)
{
    gtk_grid_attach (GTK_GRID (cards), dakkar_stat_card (value, caption, note, color),
                     *n % 3, *n / 3, 1, 1);
    (*n)++;
}

/* The checksum card: CRC from 7-Zip, "n of m" verified, nothing to
 * verify, or verification off. */
static void
summary_checksum_card_add (const SummaryData *d, GtkWidget *cards, gint *n)
{
    const DakkarCounts *c = &d->c;
    const DakkarItems *it = d->it;
    const NemoArchiveResult *ar = d->ar;
    GdkRGBA color;
    gchar *value, *caption, *note = NULL;

    if (d->is_archive && ar != NULL && ar->cli_checked && (c->verified == 0 || d->algo == NULL)) {
        dakkar_status_color (d->dialog, ar->cli_check_ok ? DAKKAR_ITEM_OK : DAKKAR_ITEM_FAILED, &color);
        summary_card_add (cards, n, ar->cli_check_ok ? _("passed") : _("failed"),
                          _("Checksum (CRC, 7-Zip):"), NULL, &color);
        return;
    }
    if (it->eligible == 0 && (d->algo != NULL || it->verify_on)) {
        summary_card_add (cards, n, _("—"), _("files verified"),
                          c->succeeded == 0 ? _("No file was copied.") :
                          _("None of the items needed a checksum (folders, links or renamed items)."),
                          NULL);
        return;
    }
    if (!(d->algo != NULL || (d->is_copy_like && it->eligible > 0 && it->verify_on))) {
        summary_card_add (cards, n, _("Off"), _("checksum verification"),
                          _("Turn it on in the transfer window or in Preferences."), NULL);
        return;
    }

    /* Translators: stat card value, "26 of 26" files verified */
    value = g_strdup_printf (_("%u of %u"), c->verified, it->eligible);
    /* Translators: stat card caption under "26 of 26", %s = "SHA-1" */
    caption = g_strdup_printf (ngettext ("file verified (%s)", "files verified (%s)",
                                         it->eligible), d->algo != NULL ? d->algo : "SHA-1");
    if (it->folders > 0)
        note = g_strdup_printf (ngettext ("%u folder: no checksum (folders have no content of their own)",
                                          "%u folders: no checksum (folders have no content of their own)",
                                          it->folders), it->folders);
    else if (it->links + it->others > 0)
        note = g_strdup_printf (ngettext ("%u link or special file: not checksummed",
                                          "%u links or special files: not checksummed",
                                          it->links + it->others), it->links + it->others);
    if (c->mismatched > 0) {
        gchar *m = g_strdup_printf (ngettext ("%u checksum mismatch", "%u checksum mismatches",
                                              c->mismatched), c->mismatched);
        gchar *joined = note != NULL ? g_strconcat (m, " · ", note, NULL) : g_strdup (m);
        g_free (m);
        g_free (note);
        note = joined;
    }
    dakkar_status_color (d->dialog, c->mismatched > 0 ? DAKKAR_ITEM_FAILED :
                        c->verified == it->eligible ? DAKKAR_ITEM_OK : DAKKAR_ITEM_UNVERIFIED,
                        &color);
    summary_card_add (cards, n, value, caption, note, it->eligible > 0 ? &color : NULL);
    g_free (value);
    g_free (caption);
    g_free (note);
}

/* Done, checksum, failed, size, duration and speed cards. */
static GtkWidget *
summary_cards_new (const SummaryData *d)
{
    const DakkarCounts *c = &d->c;
    NemoTransferOperation op = d->op;
    GtkWidget *cards = gtk_grid_new ();
    GdkRGBA color;
    gchar *value, *note = NULL, *breakdown;
    gint n = 0;

    gtk_grid_set_column_spacing (GTK_GRID (cards), 8);
    gtk_grid_set_row_spacing (GTK_GRID (cards), 8);
    gtk_grid_set_column_homogeneous (GTK_GRID (cards), TRUE);

    value = g_strdup_printf ("%u", c->succeeded);
    breakdown = dakkar_items_breakdown (d->it);
    summary_card_add (cards, &n, value,
                      op == NEMO_TRANSFER_OP_COMPRESS ? _("files added") :
                      op == NEMO_TRANSFER_OP_EXTRACT ? _("items extracted") :
                      op == NEMO_TRANSFER_OP_DELETE || op == NEMO_TRANSFER_OP_TRASH ? _("items removed") :
                      _("items done"), breakdown, NULL);
    g_free (breakdown);
    g_free (value);

    if (d->is_copy_like || (d->is_archive && (d->algo != NULL || (d->ar != NULL && d->ar->cli_checked))))
        summary_checksum_card_add (d, cards, &n);

    if (c->skipped > 0 && c->cancelled > 0)
        note = g_strdup_printf (_("%u skipped, %u not done (cancelled)"), c->skipped, c->cancelled);
    else if (c->skipped > 0)
        note = g_strdup_printf (ngettext ("%u skipped", "%u skipped", c->skipped), c->skipped);
    else if (c->cancelled > 0)
        note = g_strdup_printf (ngettext ("%u not done (cancelled)", "%u not done (cancelled)",
                                          c->cancelled), c->cancelled);
    value = g_strdup_printf ("%u", c->failed);
    dakkar_status_color (d->dialog, DAKKAR_ITEM_FAILED, &color);
    summary_card_add (cards, &n, value, _("failed"), note, c->failed > 0 ? &color : NULL);
    g_free (value);
    g_free (note);

    if (d->have_bytes && !d->is_archive) {
        value = g_format_size (c->bytes);
        summary_card_add (cards, &n, value, _("total size"), NULL, NULL);
        g_free (value);
    }
    value = nemo_transfer_format_duration (d->elapsed);
    summary_card_add (cards, &n, value, _("duration"), NULL, NULL);
    g_free (value);
    if (d->have_bytes && d->elapsed > 0 && !d->is_archive) {
        value = dakkar_format_rate ((gdouble) c->bytes * G_USEC_PER_SEC / d->elapsed);
        summary_card_add (cards, &n, value, _("average speed"), NULL, NULL);
        g_free (value);
    }
    return cards;
}

/* Caption/value rows that do not fit a card, mostly archive details.
 * Returns NULL when there is nothing to show. */
static GtkWidget *
summary_details_new (const SummaryData *d)
{
    const DakkarCounts *c = &d->c;
    const NemoArchiveResult *ar = d->ar;
    NemoTransferOperation op = d->op;
    GtkWidget *grid = gtk_grid_new ();
    gint row = 0;
    gchar *value;

    gtk_grid_set_row_spacing (GTK_GRID (grid), 3);
    gtk_grid_set_column_spacing (GTK_GRID (grid), 10);
    if (d->is_copy_like && c->succeeded > 0) {
        /* Translators: "27 new, 0 overwritten" */
        value = g_strdup_printf (_("%u new, %u overwritten"), c->created, c->overwritten);
        dakkar_summary_row (grid, row++, _("Destination:"), value);
        g_free (value);
    }
    if (d->is_archive && ar != NULL && ar->cli_checked && c->verified > 0 && d->algo != NULL)
        /* encrypted 7z/RAR: the 7z program checked every file's CRC32 */
        dakkar_summary_row (grid, row++, _("Checksum (CRC, 7-Zip):"),
                           ar->cli_check_ok ? _("passed") : _("failed"));
    if (op == NEMO_TRANSFER_OP_EXTRACT && c->overwritten > 0) {
        value = g_strdup_printf ("%u", c->overwritten);
        dakkar_summary_row (grid, row++, _("Overwritten:"), value);
        g_free (value);
    }
    if (op == NEMO_TRANSFER_OP_EXTRACT && ar != NULL && ar->n_protected > 0) {
        value = g_strdup_printf ("%u", ar->n_protected);
        dakkar_summary_row (grid, row++, _("Password-protected archives:"), value);
        g_free (value);
    }
    if (op == NEMO_TRANSFER_OP_COMPRESS && ar != NULL && ar->created && ar->encryption != NULL)
        dakkar_summary_row (grid, row++, _("Encryption:"), ar->encryption);
    if (d->is_archive && ar != NULL && ar->archive_path != NULL && ar->created)
        dakkar_summary_row (grid, row++,
                           op == NEMO_TRANSFER_OP_COMPRESS ? _("Archive:") : _("Extracted to:"),
                           ar->archive_path);
    if (d->is_archive && ar != NULL && ar->created && !d->cancelled) {
        gchar *packed = g_format_size (ar->archive_size);
        gchar *plain = g_format_size (ar->content_size);
        dakkar_summary_row (grid, row++, _("Archive size:"), packed);
        dakkar_summary_row (grid, row++,
                           op == NEMO_TRANSFER_OP_COMPRESS ? _("Size of the files:") :
                                                             _("Extracted size:"), plain);
        if (ar->content_size > 0 && ar->archive_size > 0) {
            gdouble ratio = 100.0 * ar->archive_size / ar->content_size;
            /* Translators: compression ratio, e.g. "38% (62% smaller)" */
            value = ratio <= 100.0 ?
                g_strdup_printf (_("%.0f%% (%.0f%% smaller)"), ratio, 100.0 - ratio) :
                g_strdup_printf (_("%.0f%% (larger than the files)"), ratio);
            dakkar_summary_row (grid, row++, _("Compression ratio:"), value);
            g_free (value);
        }
        g_free (packed);
        g_free (plain);
    }
    if (row == 0) {
        gtk_widget_destroy (grid);
        return NULL;
    }
    return grid;
}

/* Icon for a summary row, shared per content type through @icons. */
static GIcon *
summary_item_icon (GHashTable *icons, DakkarItemKind kind, const gchar *base)
{
    gchar *type;
    GIcon *icon;

    if (kind == DAKKAR_KIND_DIR)
        type = g_strdup ("inode/directory");
    else if (kind == DAKKAR_KIND_LINK)
        type = g_strdup ("inode/symlink");
    else if (kind == DAKKAR_KIND_SPECIAL)
        type = g_strdup ("inode/chardevice");
    else
        type = g_content_type_guess (base, NULL, 0, NULL);
    icon = g_hash_table_lookup (icons, type);
    if (icon == NULL) {
        icon = kind == DAKKAR_KIND_DIR ? g_themed_icon_new ("folder") : g_content_type_get_icon (type);
        g_hash_table_insert (icons, type, icon);
    } else {
        g_free (type);
    }
    return icon;
}

/* One row per item: icon, dimmed folder + name, size, status icon and a
 * tooltip with the reason (and the checksum when there is one). */
static GtkListStore *
summary_item_store_new (const SummaryData *d)
{
    const DakkarItems *it = d->it;
    GtkListStore *store = gtk_list_store_new (SUM_N_COLS, G_TYPE_ICON, G_TYPE_STRING,
        G_TYPE_STRING, GDK_TYPE_PIXBUF, G_TYPE_STRING, G_TYPE_INT, G_TYPE_STRING,
        G_TYPE_STRING);
    GHashTable *icons = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);
    GdkPixbuf *pix[4];
    GdkRGBA color;
    guint i, k;

    for (k = 0; k < 4; k++) {
        dakkar_status_color (d->dialog, k, &color);
        pix[k] = dakkar_colored_icon (d->dialog, dakkar_status_icon_name (k), &color, 16);
    }
    for (i = 0; i < it->n; i++) {
        NemoTransferFileSnapshot *e = g_ptr_array_index (d->files, i);
        gchar *rel = dakkar_items_relative (it, e->source_uri);
        gchar *slash = strrchr (rel, '/');
        const gchar *base = slash != NULL ? slash + 1 : rel;
        GIcon *icon = summary_item_icon (icons, it->kind[i], base);
        gchar *name_markup, *tip, *size;

        if (slash != NULL) {
            gchar *dir = g_strndup (rel, slash + 1 - rel);
            name_markup = g_markup_printf_escaped ("<span alpha=\"60%%\">%s</span>%s",
                                                   dir, base);
            g_free (dir);
        } else {
            name_markup = g_markup_escape_text (rel, -1);
        }
        size = e->total >= 0 && it->kind[i] != DAKKAR_KIND_DIR ?
               g_format_size (e->total) : g_strdup ("");
        if (e->sha256 != NULL)
            tip = g_markup_printf_escaped ("<b>%s</b>\n%s\n<small><tt>%s %s</tt></small>",
                                           rel, it->reason[i],
                                           d->algo != NULL ? d->algo : "", e->sha256);
        else
            tip = g_markup_printf_escaped ("<b>%s</b>\n%s", rel, it->reason[i]);
        gtk_list_store_insert_with_values (store, NULL, -1,
            SUM_ICON, icon, SUM_NAME, name_markup, SUM_SIZE, size,
            SUM_STATUS_PIX, pix[it->status[i]], SUM_TIP, tip,
            SUM_STATUS, (gint) it->status[i],
            SUM_DEST_URI, e->destination_uri, SUM_SOURCE_URI, e->source_uri, -1);
        g_free (tip);
        g_free (size);
        g_free (name_markup);
        g_free (rel);
    }
    for (k = 0; k < 4; k++) g_clear_object (&pix[k]);
    g_hash_table_unref (icons);
    return store;
}

static void
summary_view_add_column (GtkWidget *view, const gchar *title, GtkCellRenderer *r,
                         const gchar *attribute, gint column, gint width, gboolean expand)
{
    GtkTreeViewColumn *col = gtk_tree_view_column_new_with_attributes (title, r, attribute, column, NULL);
    gtk_tree_view_column_set_sizing (col, GTK_TREE_VIEW_COLUMN_FIXED);
    gtk_tree_view_column_set_fixed_width (col, width);
    if (expand)
        gtk_tree_view_column_set_expand (col, TRUE);
    gtk_tree_view_append_column (GTK_TREE_VIEW (view), col);
}

/* All/Problems switch, hint and the item list. */
static void
summary_item_list_add (const SummaryData *d, DakkarSummary *s, GtkWidget *content)
{
    const DakkarItems *it = d->it;
    guint problems = it->n - it->n_status[DAKKAR_ITEM_OK];
    GtkListStore *store = summary_item_store_new (d);
    GtkWidget *bar, *row_box, *hint, *scroll, *all, *prob, *view;
    GtkCellRenderer *r;
    gchar *text;

    bar = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_style_context_add_class (gtk_widget_get_style_context (bar), "linked");
    text = g_strdup_printf (_("All (%u)"), it->n);
    all = gtk_radio_button_new_with_label (NULL, text);
    g_free (text);
    text = g_strdup_printf (_("Problems (%u)"), problems);
    prob = gtk_radio_button_new_with_label_from_widget (GTK_RADIO_BUTTON (all), text);
    g_free (text);
    gtk_widget_set_tooltip_text (prob, _("Only items that failed, were not verified or were not done"));
    g_object_set_data (G_OBJECT (prob), "dakkar-problems", GINT_TO_POINTER (1));
    gtk_toggle_button_set_mode (GTK_TOGGLE_BUTTON (all), FALSE);
    gtk_toggle_button_set_mode (GTK_TOGGLE_BUTTON (prob), FALSE);
    gtk_widget_set_sensitive (prob, problems > 0);
    gtk_box_pack_start (GTK_BOX (bar), all, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (bar), prob, FALSE, FALSE, 0);

    row_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start (GTK_BOX (row_box), bar, FALSE, FALSE, 0);
    hint = gtk_label_new (d->truncated ?
        _("Only the first 8192 items are listed. Double-click an item to open its folder.") :
        _("Double-click an item to open its folder."));
    gtk_label_set_xalign (GTK_LABEL (hint), 1.0);
    gtk_label_set_ellipsize (GTK_LABEL (hint), PANGO_ELLIPSIZE_END);
    gtk_style_context_add_class (gtk_widget_get_style_context (hint), "dim-label");
    gtk_box_pack_end (GTK_BOX (row_box), hint, TRUE, TRUE, 0);
    gtk_box_pack_start (GTK_BOX (content), row_box, FALSE, FALSE, 0);

    s->filter = gtk_tree_model_filter_new (GTK_TREE_MODEL (store), NULL);
    g_object_unref (store);
    gtk_tree_model_filter_set_visible_func (GTK_TREE_MODEL_FILTER (s->filter),
                                            dakkar_summary_visible, NULL, NULL);
    view = gtk_tree_view_new_with_model (s->filter);
    s->view = view;
    gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (view), FALSE);
    gtk_tree_view_set_tooltip_column (GTK_TREE_VIEW (view), SUM_TIP);
    gtk_tree_view_set_enable_search (GTK_TREE_VIEW (view), FALSE);

    r = gtk_cell_renderer_pixbuf_new ();
    g_object_set (r, "stock-size", GTK_ICON_SIZE_MENU, NULL);
    summary_view_add_column (view, "", r, "gicon", SUM_ICON, 28, FALSE);
    r = gtk_cell_renderer_text_new ();
    g_object_set (r, "ellipsize", PANGO_ELLIPSIZE_MIDDLE, NULL);
    summary_view_add_column (view, _("Name"), r, "markup", SUM_NAME, 200, TRUE);
    r = gtk_cell_renderer_text_new ();
    g_object_set (r, "xalign", 1.0, NULL);
    summary_view_add_column (view, _("Size"), r, "text", SUM_SIZE, 84, FALSE);
    r = gtk_cell_renderer_pixbuf_new ();
    summary_view_add_column (view, "", r, "pixbuf", SUM_STATUS_PIX, 30, FALSE);
    /* every column is fixed: thousands of rows lay out in constant time */
    gtk_tree_view_set_fixed_height_mode (GTK_TREE_VIEW (view), TRUE);

    g_signal_connect (view, "row-activated", G_CALLBACK (dakkar_summary_row_activated), s);
    g_signal_connect (view, "button-press-event", G_CALLBACK (dakkar_summary_button_press), s);
    g_signal_connect (all, "toggled", G_CALLBACK (dakkar_summary_filter_toggled), s);
    g_signal_connect (prob, "toggled", G_CALLBACK (dakkar_summary_filter_toggled), s);

    scroll = gtk_scrolled_window_new (NULL, NULL);
    gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (scroll), GTK_SHADOW_IN);
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroll),
                                    GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_min_content_height (GTK_SCROLLED_WINDOW (scroll), 110);
    gtk_container_add (GTK_CONTAINER (scroll), view);
    gtk_widget_set_vexpand (scroll, TRUE);
    gtk_box_pack_start (GTK_BOX (content), scroll, TRUE, TRUE, 0);
    /* a job with problems opens on them */
    if (it->n_status[DAKKAR_ITEM_FAILED] > 0)
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (prob), TRUE);
}

/* The dialog shown when a job ends: header, stat cards, details and the
 * list of every item, with Copy log, Retry failed and OK. */
static void
dakkar_show_summary (NemoProgressInfo *info)
{
    SummaryData d;
    DakkarSummary *s;
    GtkWidget *dialog, *content, *details;

    summary_data_init (&d, info);
    dakkar_summary_style_ensure ();

    dialog = d.dialog = gtk_dialog_new ();
    dakkar_dialog_style_attach (dialog);
    gtk_window_set_title (GTK_WINDOW (dialog), summary_window_title (&d));
    gtk_window_set_default_size (GTK_WINDOW (dialog),
        CLAMP (g_settings_get_int (nemo_preferences, "dakkar-transfer-summary-width"), 420, 4000),
        d.it->n > 0 ?
        CLAMP (g_settings_get_int (nemo_preferences, "dakkar-transfer-summary-height"), 260, 4000) : -1);
    gtk_window_set_resizable (GTK_WINDOW (dialog), TRUE);
    gtk_window_set_position (GTK_WINDOW (dialog), GTK_WIN_POS_CENTER);
    gtk_window_set_icon_name (GTK_WINDOW (dialog), DAKKAR_APP_ICON);
    gtk_widget_set_name (dialog, "dakkar-transfer-summary");
    content = gtk_dialog_get_content_area (GTK_DIALOG (dialog));
    gtk_container_set_border_width (GTK_CONTAINER (content), 12);
    gtk_box_set_spacing (GTK_BOX (content), 10);

    gtk_box_pack_start (GTK_BOX (content), summary_header_new (&d), FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (content), summary_cards_new (&d), FALSE, FALSE, 0);
    details = summary_details_new (&d);
    if (details != NULL)
        gtk_box_pack_start (GTK_BOX (content), details, FALSE, FALSE, 0);

    s = g_new0 (DakkarSummary, 1);
    s->info = g_object_ref (info);
    s->dialog = dialog;
    if (d.it->n > 0)
        summary_item_list_add (&d, s, content);

    gtk_dialog_add_button (GTK_DIALOG (dialog), _("Copy _log"), DAKKAR_RESPONSE_COPY_LOG);
    gtk_widget_set_tooltip_text (gtk_dialog_get_widget_for_response (GTK_DIALOG (dialog),
                                     DAKKAR_RESPONSE_COPY_LOG),
                                 _("Copy the summary and the result of every item to the clipboard"));
    if (d.is_copy_like && d.c.failed > 0) {
        s->retry_button = gtk_dialog_add_button (GTK_DIALOG (dialog), _("_Retry failed"),
                                                 DAKKAR_RESPONSE_RETRY);
        gtk_widget_set_tooltip_text (s->retry_button,
            _("Start a new job for the failed files. Existing destination files are not replaced unless you choose Overwrite in the conflict dialog."));
    }
    gtk_dialog_add_button (GTK_DIALOG (dialog), _("_OK"), GTK_RESPONSE_OK);
    gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);
    g_signal_connect_data (dialog, "response", G_CALLBACK (dakkar_summary_response),
                           s, (GClosureNotify) dakkar_summary_free, 0);
    gtk_widget_show_all (dialog);
    gtk_widget_grab_focus (gtk_dialog_get_widget_for_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK));
    gtk_window_present (GTK_WINDOW (dialog));
    summary_data_clear (&d);
}

/* ---------- lifetime ---------- */

static void
dakkar_transfer_finished (NemoProgressInfo *info, DakkarTransfer *t)
{
    transfers = g_list_remove (transfers, t);
    if (t->timer != 0) {
        g_source_remove (t->timer);
        t->timer = 0;
    }
    g_signal_handler_disconnect (info, t->finished_handler);
    if (t->window != NULL)
        gtk_widget_destroy (t->window);
    g_clear_object (&t->store);
    if (g_settings_get_boolean (nemo_preferences, "dakkar-transfer-show-summary"))
        dakkar_show_summary (info);
    g_object_unref (t->info);
    g_free (t);
    nemo_progress_ui_handler_refresh_global ();
    dakkar_activity_changed ();
}

/* Short jobs (finished before the window appeared) still get a summary
 * when something needs attention: a failure, a skip or a checksum mismatch. */
/* Remember the last finished job for the idle "File Transfers" window.
 * Read-only: the summary dialog itself is untouched. */
static void
dakkar_remember_last (NemoProgressInfo *info)
{
    GPtrArray *files = nemo_progress_info_dup_file_manifest (info, 0, NULL, NULL);
    GCancellable *cancellable = nemo_progress_info_get_cancellable (info);
    gboolean cancelled = cancellable != NULL && g_cancellable_is_cancelled (cancellable);
    DakkarCounts c;
    gchar *size;
    if (cancellable != NULL) g_object_unref (cancellable);
    if (files == NULL) return;
    dakkar_count (files, &c);
    g_free (last_title);
    g_free (last_detail);
    g_clear_pointer (&last_time, g_date_time_unref);
    last_title = g_strdup (cancelled ? _("Transfer cancelled") :
                           c.failed > 0 ? _("Transfer finished with errors") :
                                          _("Transfer complete"));
    size = g_format_size (c.bytes);
    last_detail = g_strdup_printf (_("%u succeeded, %u failed, %u skipped (%s)"),
                                   c.succeeded, c.failed, c.skipped, size);
    g_free (size);
    last_time = g_date_time_new_now_local ();
    g_ptr_array_unref (files);
}

static void
dakkar_watch_finished (NemoProgressInfo *info, gpointer user_data)
{
    GList *l;
    GPtrArray *files;
    DakkarCounts c;
    if (g_object_get_data (G_OBJECT (info), "dakkar-transfer-counted") != NULL) {
        g_object_set_data (G_OBJECT (info), "dakkar-transfer-counted", NULL);
        if (watched_active > 0) watched_active--;
        dakkar_remember_last (info);
        dakkar_activity_changed ();
    }
    for (l = transfers; l != NULL; l = l->next)
        if (((DakkarTransfer *) l->data)->info == info) return;
    if (g_object_get_data (G_OBJECT (info), "dakkar-transfer-tracked") != NULL) return;
    if (!g_settings_get_boolean (nemo_preferences, "dakkar-transfer-show-summary")) return;
    files = nemo_progress_info_dup_file_manifest (info, 0, NULL, NULL);
    dakkar_count (files, &c);
    {
        /* A retry always reports its outcome, even when it was quick. */
        guint i;
        gboolean was_retry = FALSE;
        for (i = 0; retried_sources != NULL && i < files->len; i++) {
            NemoTransferFileSnapshot *e = g_ptr_array_index (files, i);
            if (e->source_uri != NULL && g_hash_table_remove (retried_sources, e->source_uri))
                was_retry = TRUE;
        }
        g_ptr_array_unref (files);
        if (c.failed > 0 || c.skipped > 0 || was_retry)
            dakkar_show_summary (info);
    }
    (void) user_data;
}

/* A job that must ask something (an archive password) first brings its
 * transfer window up, so the question can be modal on it. */
static gpointer
dakkar_transfer_present_for_prompt (NemoProgressInfo *info)
{
    GList *l;
    nemo_transfer_window_track (info, TRUE);
    for (l = transfers; l != NULL; l = l->next) {
        DakkarTransfer *t = l->data;
        if (t->info != info) continue;
        gtk_widget_show (t->window);
        dakkar_transfer_refresh (t);
        gtk_window_present (GTK_WINDOW (t->window));
        return t->window;
    }
    return NULL;
}

void
nemo_transfer_window_init_prompts (void)
{
    nemo_progress_info_set_present_func (dakkar_transfer_present_for_prompt);
}

void
nemo_transfer_window_watch (NemoProgressInfo *info)
{
    g_return_if_fail (NEMO_IS_PROGRESS_INFO (info));
    if (!nemo_progress_info_get_is_finished (info) &&
        g_object_get_data (G_OBJECT (info), "dakkar-transfer-counted") == NULL) {
        g_object_set_data (G_OBJECT (info), "dakkar-transfer-counted", GINT_TO_POINTER (1));
        watched_active++;
        dakkar_activity_changed ();
    }
    g_signal_connect (info, "finished", G_CALLBACK (dakkar_watch_finished), NULL);
}

void
nemo_transfer_window_track (NemoProgressInfo *info, gboolean show_window)
{
    DakkarTransfer *t;
    GList *l;
    g_return_if_fail (NEMO_IS_PROGRESS_INFO (info));
    if (nemo_progress_info_get_is_finished (info)) return;
    for (l = transfers; l != NULL; l = l->next)
        if (((DakkarTransfer *) l->data)->info == info) return;
    t = g_new0 (DakkarTransfer, 1);
    t->info = g_object_ref (info);
    g_object_set_data (G_OBJECT (info), "dakkar-transfer-tracked", GINT_TO_POINTER (1));
    dakkar_transfer_build_window (t);
    t->finished_handler = g_signal_connect (info, "finished",
                                            G_CALLBACK (dakkar_transfer_finished), t);
    t->timer = g_timeout_add (250, dakkar_transfer_tick, t);
    transfers = g_list_append (transfers, t);
    dakkar_activity_changed ();
    /* the idle window was asked for explicitly: replace it by the real one */
    if (idle_window != NULL) {
        dakkar_idle_window_close ();
        show_window = TRUE;
    }
    if (show_window) {
        gtk_widget_show (t->window);
        dakkar_transfer_refresh (t);
        gtk_window_present (GTK_WINDOW (t->window));
    }
}

gboolean
nemo_transfer_window_present_all (void)
{
    GList *l;
    for (l = transfers; l != NULL; l = l->next) {
        DakkarTransfer *t = l->data;
        gtk_widget_show (t->window);
        dakkar_transfer_refresh (t);
        gtk_window_present (GTK_WINDOW (t->window));
    }
    nemo_progress_ui_handler_refresh_global ();
    return transfers != NULL;
}

/* ---------- toolbar entry point ---------- */

gboolean
nemo_transfer_window_is_busy (void)
{
    return transfers != NULL || watched_active > 0;
}

static void
dakkar_activity_widget_gone (gpointer data, GObject *where_the_object_was)
{
    activity_widgets = g_slist_remove (activity_widgets, where_the_object_was);
    (void) data;
}

void
nemo_transfer_window_add_activity_widget (GtkWidget *widget)
{
    g_return_if_fail (GTK_IS_WIDGET (widget));
    if (g_slist_find (activity_widgets, widget) != NULL) return;
    activity_widgets = g_slist_prepend (activity_widgets, widget);
    g_object_weak_ref (G_OBJECT (widget), dakkar_activity_widget_gone, NULL);
}

static void
dakkar_activity_changed (void)
{
    GSList *l;
    for (l = activity_widgets; l != NULL; l = l->next)
        gtk_widget_queue_draw (GTK_WIDGET (l->data));
}

static void
dakkar_idle_window_close (void)
{
    if (idle_window != NULL)
        gtk_widget_destroy (idle_window);
}

static GtkWidget *
dakkar_idle_label (const gchar *text, const gchar *css_class, gboolean wrap)
{
    GtkWidget *label = gtk_label_new (text);
    gtk_label_set_xalign (GTK_LABEL (label), 0.0);
    gtk_label_set_line_wrap (GTK_LABEL (label), wrap);
    gtk_label_set_max_width_chars (GTK_LABEL (label), 48);
    if (css_class != NULL)
        gtk_style_context_add_class (gtk_widget_get_style_context (label), css_class);
    return label;
}

static void
dakkar_idle_window_build (GtkWindow *parent)
{
    GtkWidget *box, *row, *icon, *texts, *heading, *buttons, *close;
    gchar *markup;

    idle_window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
    dakkar_dialog_style_attach (idle_window);
    g_object_add_weak_pointer (G_OBJECT (idle_window), (gpointer *) &idle_window);
    gtk_window_set_title (GTK_WINDOW (idle_window), _("File Transfers"));
    gtk_window_set_icon_name (GTK_WINDOW (idle_window), DAKKAR_APP_ICON);
    gtk_widget_set_name (idle_window, "dakkar-transfer-idle");
    gtk_window_set_default_size (GTK_WINDOW (idle_window), 420, -1);
    gtk_window_set_resizable (GTK_WINDOW (idle_window), FALSE);
    gtk_window_set_type_hint (GTK_WINDOW (idle_window), GDK_WINDOW_TYPE_HINT_DIALOG);
    if (parent != NULL) {
        gtk_window_set_transient_for (GTK_WINDOW (idle_window), parent);
        gtk_window_set_position (GTK_WINDOW (idle_window), GTK_WIN_POS_CENTER_ON_PARENT);
    } else {
        gtk_window_set_position (GTK_WINDOW (idle_window), GTK_WIN_POS_CENTER);
    }

    box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
    gtk_container_set_border_width (GTK_CONTAINER (box), 16);
    row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 14);
    icon = gtk_image_new_from_icon_name (
        dakkar_icon_pick ("folder-download-symbolic", "emblem-synchronizing-symbolic",
                         "view-refresh-symbolic", NULL), GTK_ICON_SIZE_DIALOG);
    gtk_style_context_add_class (gtk_widget_get_style_context (icon), "dim-label");
    gtk_widget_set_valign (icon, GTK_ALIGN_START);
    texts = gtk_box_new (GTK_ORIENTATION_VERTICAL, 4);
    heading = dakkar_idle_label (NULL, NULL, FALSE);
    markup = g_markup_printf_escaped ("<b>%s</b>", _("No active transfers"));
    gtk_label_set_markup (GTK_LABEL (heading), markup);
    g_free (markup);
    gtk_box_pack_start (GTK_BOX (texts), heading, FALSE, FALSE, 0);
    if (last_title != NULL) {
        gchar *when = last_time != NULL ? g_date_time_format (last_time, "%X") : NULL;
        gchar *line = g_strdup_printf (_("Last transfer: %s at %s"), last_title,
                                       when != NULL ? when : "");
        gtk_box_pack_start (GTK_BOX (texts), dakkar_idle_label (line, NULL, TRUE), FALSE, FALSE, 0);
        gtk_box_pack_start (GTK_BOX (texts),
                            dakkar_idle_label (last_detail, "dim-label", TRUE), FALSE, FALSE, 0);
        g_free (line);
        g_free (when);
    } else {
        gtk_box_pack_start (GTK_BOX (texts),
            dakkar_idle_label (_("Copy, move, compress and extract jobs appear here while they run."),
                              "dim-label", TRUE), FALSE, FALSE, 0);
    }
    gtk_box_pack_start (GTK_BOX (row), icon, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (row), texts, TRUE, TRUE, 0);
    gtk_box_pack_start (GTK_BOX (box), row, FALSE, FALSE, 0);

    buttons = gtk_button_box_new (GTK_ORIENTATION_HORIZONTAL);
    gtk_button_box_set_layout (GTK_BUTTON_BOX (buttons), GTK_BUTTONBOX_END);
    close = gtk_button_new_with_mnemonic (_("_Close"));
    g_signal_connect_swapped (close, "clicked", G_CALLBACK (gtk_widget_destroy), idle_window);
    gtk_container_add (GTK_CONTAINER (buttons), close);
    gtk_box_pack_start (GTK_BOX (box), buttons, FALSE, FALSE, 0);
    gtk_container_add (GTK_CONTAINER (idle_window), box);
    gtk_widget_show_all (box);
    gtk_widget_grab_focus (close);
}

void
nemo_transfer_window_show (GtkWindow *parent)
{
    GList *l;
    if (transfers != NULL) {
        for (l = transfers; l != NULL; l = l->next) {
            DakkarTransfer *t = l->data;
            if (t->window == NULL) continue;
            gtk_widget_show (t->window);
            gtk_window_deiconify (GTK_WINDOW (t->window));
            dakkar_transfer_refresh (t);
            gtk_window_present (GTK_WINDOW (t->window));
        }
        nemo_progress_ui_handler_refresh_global ();
        return;
    }
    /* queued but not tracked yet (the first ~2 s), or nothing at all:
     * the idle window; a job that gets tracked replaces it */
    if (idle_window == NULL)
        dakkar_idle_window_build (parent);
    gtk_window_deiconify (GTK_WINDOW (idle_window));
    gtk_window_present (GTK_WINDOW (idle_window));
}

void
nemo_transfer_window_toggle_pause_all (void)
{
    GList *l;
    gboolean all_paused = transfers != NULL;
    for (l = transfers; l != NULL; l = l->next)
        if (!nemo_progress_info_hold_requested (((DakkarTransfer *) l->data)->info, NULL))
            all_paused = FALSE;
    for (l = transfers; l != NULL; l = l->next) {
        DakkarTransfer *t = l->data;
        if (all_paused)
            nemo_progress_info_release_hold (t->info);
        else
            nemo_progress_info_request_hold (t->info);
        dakkar_transfer_refresh (t);
    }
    nemo_progress_ui_handler_refresh_global ();
}

void
nemo_transfer_window_get_bar_state (gboolean *verifying, gdouble *verify_fraction,
                                    guint *failed)
{
    GList *l;
    guint n = 0, nfailed = 0;
    for (l = transfers; l != NULL; l = l->next) {
        DakkarTransfer *t = l->data;
        n++;
        nfailed += t->failed;
    }
    if (verifying)
        *verifying = n == 1 && ((DakkarTransfer *) transfers->data)->verifying;
    if (verify_fraction)
        *verify_fraction = n == 1 ? ((DakkarTransfer *) transfers->data)->verify_fraction : 0.0;
    if (failed)
        *failed = nfailed;
}

gchar *
nemo_transfer_window_dup_caption (gint width, gdouble *fraction,
                                  gboolean *paused, gboolean *can_pause)
{
    GList *l;
    goffset done = 0, total = 0;
    gdouble rate = 0.0, other_sum = 0.0;
    gint64 eta = -1;
    guint count = 0, held = 0, verifying = 0, others = 0, failed = 0;
    const gchar *verb = NULL;
    GString *caption;
    gdouble frac;

    for (l = transfers; l != NULL; l = l->next) {
        DakkarTransfer *t = l->data;
        NemoTransferByteStats stats;
        count++;
        if (nemo_progress_info_hold_requested (t->info, NULL)) held++;
        verb = dakkar_operation_verb (nemo_progress_info_get_operation (t->info));
        if (nemo_progress_info_get_byte_stats (t->info, &stats) && stats.total > 0) {
            done += (goffset) (dakkar_transfer_fraction (t, &stats) * stats.total);
            total += stats.total;
            if (stats.rate_available && !t->verifying) rate += stats.current_bytes_per_second;
            if (dakkar_transfer_eta (t, &stats) > eta) eta = dakkar_transfer_eta (t, &stats);
        } else {
            gdouble p = nemo_progress_info_get_progress (t->info);
            other_sum += p >= 0.0 ? CLAMP (p, 0.0, 1.0) : 0.0;
            others++;
        }
        if (t->verifying) verifying++;
        failed += t->failed;
    }
    if (count == 0) {
        if (fraction) *fraction = 0.0;
        if (paused) *paused = FALSE;
        if (can_pause) *can_pause = FALSE;
        return NULL;
    }
    /* Byte-weighted for transfers, equal weight for item-only jobs. */
    if (total > 0 && others == 0) frac = (gdouble) done / total;
    else if (total > 0) frac = ((gdouble) done / total + other_sum) / (1 + others);
    else frac = other_sum / MAX (others, 1u);
    frac = CLAMP (frac, 0.0, 1.0);

    caption = g_string_new (NULL);
    if (held == count)
        g_string_append (caption, _("Paused"));
    else if (count > 1)
        g_string_append_printf (caption, ngettext ("%u transfer", "%u transfers", count), count);
    else if (verifying > 0)
        g_string_append (caption, _("Verifying"));
    else
        g_string_append (caption, verb);
    g_string_append_printf (caption, " · %u%%", (guint) (frac * 100));
    if (verifying > 0 && count == 1 && held != count) {
        DakkarTransfer *t = transfers->data;
        /* Translators: progress of the checksum read-back of the current file */
        g_string_append_printf (caption, " · ");
        g_string_append_printf (caption, _("checksum %u%%"), (guint) (t->verify_fraction * 100));
    }
    if (failed > 0) {
        g_string_append (caption, " · ");
        /* Translators: shown in the status-bar transfer bar, %u files failed so far */
        g_string_append_printf (caption, ngettext ("%u failed", "%u failed", failed), failed);
    }
    if (held != count && rate > 0.0) {
        gchar *r = dakkar_format_rate (rate);
        g_string_append_printf (caption, " · %s", r);
        g_free (r);
    }
    if (held != count && eta >= 0 && width >= 420) {
        gchar *d = nemo_transfer_format_duration (eta);
        /* Translators: %s is a duration such as 1:05 */
        g_string_append_printf (caption, " · ");
        g_string_append_printf (caption, _("%s left"), d);
        g_free (d);
    }
    if (fraction) *fraction = frac;
    if (paused) *paused = held == count;
    if (can_pause) *can_pause = TRUE;
    return g_string_free (caption, FALSE);
}
