/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/*
 * nemo-progress-info-widget.h: file operation progress user interface.
 *
 * Copyright (C) 2007, 2011 Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street - Suite 500,
 * Boston, MA 02110-1335, USA.
 *
 * Authors: Alexander Larsson <alexl@redhat.com>
 *          Cosimo Cecchi <cosimoc@redhat.com>
 *
 */

#include <config.h>
#include <glib/gi18n.h>

#include "nemo-progress-info-widget.h"
#include "nemo-job-queue.h"
#include <libnemo-private/nemo-file-operations.h>

enum {
	PROP_INFO = 1,
	NUM_PROPERTIES
};

#define START_ICON "media-playback-start-symbolic"
#define STOP_ICON "media-playback-stop-symbolic"

static GParamSpec *properties[NUM_PROPERTIES] = { NULL };

G_DEFINE_TYPE (NemoProgressInfoWidget, nemo_progress_info_widget,
               GTK_TYPE_BOX);

/* Each point is sampled from actual native byte progress, never percent.
 * The graph is local to this job/widget and uses theme colors. */
static gboolean
dakkar_speed_graph_draw (GtkWidget *area, cairo_t *cr, gpointer user_data)
{
    NemoProgressInfoWidget *self = NEMO_PROGRESS_INFO_WIDGET (user_data);
    NemoProgressInfoWidgetPriv *priv = self->priv;
    guint i, count = priv->speed_sample_count;
    gint width = gtk_widget_get_allocated_width (area);
    gint height = gtk_widget_get_allocated_height (area);
    gdouble maximum = 0;
    GdkRGBA color;
    GtkStyleContext *style = gtk_widget_get_style_context (area);
    if (count < 2 || width < 2 || height < 2) return FALSE;
    for (i = 0; i < count; i++) maximum = MAX (maximum, priv->speed_samples[i]);
    if (maximum <= 0) return FALSE;
    gtk_style_context_get_color (style, gtk_style_context_get_state (style), &color);
    gdk_cairo_set_source_rgba (cr, &color);
    cairo_set_line_width (cr, 1.5);
    for (i = 0; i < count; i++) {
        guint index = (priv->speed_write_index + 64 - count + i) % 64;
        gdouble x = 2.0 + (width - 4.0) * i / (count - 1.0);
        gdouble y = height - 2.0 - (height - 4.0) * priv->speed_samples[index] / maximum;
        if (i == 0) cairo_move_to (cr, x, y);
        else cairo_line_to (cr, x, y);
    }
    cairo_stroke (cr);
    return FALSE;
}

enum {
    DAKKAR_MANIFEST_FILE,
    DAKKAR_MANIFEST_RESULT,
    DAKKAR_MANIFEST_SIZE,
    DAKKAR_MANIFEST_SHA256,
    DAKKAR_MANIFEST_SOURCE_URI,
    DAKKAR_MANIFEST_DESTINATION_URI,
    DAKKAR_MANIFEST_STATE,
    DAKKAR_MANIFEST_COLS
};

static void
dakkar_refresh_manifest (NemoProgressInfoWidget *self)
{
    GPtrArray *snapshot;
    gboolean truncated = FALSE;
    guint total_observed = 0;
    guint i;
    gint64 now = g_get_monotonic_time ();
    if (!self->priv->graph_expanded || self->priv->manifest_store == NULL)
        return;
    /* Rebuilding the GtkListStore every GIO byte callback is expensive.
     * All snapshots are captured under the native job's lock. */
    if (now - self->priv->manifest_last_refresh_us < 500000)
        return;
    self->priv->manifest_last_refresh_us = now;
    snapshot = nemo_progress_info_dup_file_manifest (self->priv->info, 400,
                                                      &truncated, &total_observed);
    gtk_list_store_clear (self->priv->manifest_store);
    for (i = 0; i < snapshot->len; i++) {
        NemoTransferFileSnapshot *file = g_ptr_array_index (snapshot, i);
        GFile *source = file->source_uri ? g_file_new_for_uri (file->source_uri) : NULL;
        gchar *name = source ? g_file_get_basename (source) : g_strdup (_("Unknown file"));
        gchar *size, *result;
        GtkTreeIter row;
        if (file->total > 0) {
            gchar *done = g_format_size (MAX (file->transferred, (goffset)0));
            gchar *total = g_format_size (file->total);
            size = g_strdup_printf ("%s / %s", done, total);
            g_free (done); g_free (total);
        } else {
            size = g_strdup (file->total == 0 ? "0 B" : "—");
        }
        switch (file->state) {
            case NEMO_TRANSFER_FILE_COMPLETED: result = g_strdup (_("Copied / moved (not verified)")); break;
            case NEMO_TRANSFER_FILE_VERIFIED: result = g_strdup (_("Verified (SHA-256)")); break;
            case NEMO_TRANSFER_FILE_VERIFY_FAILED: result = g_strdup (_("Verification failed — file retained")); break;
            case NEMO_TRANSFER_FILE_SKIPPED: result = g_strdup (_("Skipped")); break;
            case NEMO_TRANSFER_FILE_FAILED: result = g_strdup (_("Failed")); break;
            case NEMO_TRANSFER_FILE_CANCELLED: result = g_strdup (_("Cancelled")); break;
            default: result = g_strdup (_("Transferring")); break;
        }
        if (file->error != NULL) {
            gchar *with_error = g_strdup_printf ("%s — %s", result, file->error);
            g_free (result);
            result = with_error;
        }
        gtk_list_store_append (self->priv->manifest_store, &row);
        gtk_list_store_set (self->priv->manifest_store, &row,
            DAKKAR_MANIFEST_FILE, name,
            DAKKAR_MANIFEST_RESULT, result,
            DAKKAR_MANIFEST_SIZE, size,
            DAKKAR_MANIFEST_SHA256, file->sha256 ? file->sha256 : "",
            DAKKAR_MANIFEST_SOURCE_URI, file->source_uri,
            DAKKAR_MANIFEST_DESTINATION_URI, file->destination_uri,
            DAKKAR_MANIFEST_STATE, (gint)file->state, -1);
        g_free (name); g_free (result); g_free (size);
        g_clear_object (&source);
    }
    {
        gchar *summary;
        if (truncated)
            summary = g_strdup_printf (_("Showing %u of %u observed files (recording limit: 8192)"),
                                       snapshot->len, total_observed);
        else if (total_observed > snapshot->len)
            summary = g_strdup_printf (_("Showing latest %u of %u observed files"),
                                       snapshot->len, total_observed);
        else
            summary = g_strdup_printf (ngettext ("%u observed file", "%u observed files",
                                                 total_observed), total_observed);
        gtk_label_set_text (GTK_LABEL (self->priv->manifest_summary), summary);
        g_free (summary);
    }
    g_ptr_array_unref (snapshot);
}

static void
dakkar_update_retry_button (NemoProgressInfoWidget *self)
{
    GtkTreeModel *model;
    GtkTreeIter iter;
    gchar *src = NULL, *dest = NULL;
    gint state = -1;
    gboolean selected = FALSE;
    if (self->priv->retry_button == NULL || self->priv->manifest_view == NULL)
        return;
    model = GTK_TREE_MODEL (self->priv->manifest_store);
    selected = gtk_tree_selection_get_selected (
        gtk_tree_view_get_selection (GTK_TREE_VIEW (self->priv->manifest_view)),
        &model, &iter);
    if (selected) {
        gtk_tree_model_get (model, &iter,
            DAKKAR_MANIFEST_SOURCE_URI, &src,
            DAKKAR_MANIFEST_DESTINATION_URI, &dest,
            DAKKAR_MANIFEST_STATE, &state, -1);
    }
    gtk_widget_set_sensitive (self->priv->retry_button,
        nemo_progress_info_get_is_finished (self->priv->info) &&
        nemo_progress_info_get_operation (self->priv->info) == NEMO_TRANSFER_OP_COPY &&
        src != NULL && dest != NULL &&
        (state == NEMO_TRANSFER_FILE_SKIPPED || state == NEMO_TRANSFER_FILE_FAILED ||
         state == NEMO_TRANSFER_FILE_VERIFY_FAILED) &&
        !g_hash_table_contains (self->priv->retried_sources, src));
    g_free (src);
    g_free (dest);
}

static void
dakkar_retry_selection_changed (GtkTreeSelection *selection, NemoProgressInfoWidget *self)
{
    dakkar_update_retry_button (self);
    (void)selection;
}

static void
dakkar_close_results (GtkButton *button, NemoProgressInfoWidget *self)
{
    GtkWidget *top = gtk_widget_get_toplevel (GTK_WIDGET (self));
    GtkWidget *list = gtk_widget_get_parent (GTK_WIDGET (self));
    gtk_widget_destroy (GTK_WIDGET (self));
    if (GTK_IS_WINDOW (top) && GTK_IS_CONTAINER (list)) {
        GList *children = gtk_container_get_children (GTK_CONTAINER (list));
        if (children == NULL) gtk_widget_hide (top);
        g_list_free (children);
    }
    (void)button;
}

static void
dakkar_retry_selected_copy (GtkButton *button, NemoProgressInfoWidget *self)
{
    GtkTreeModel *model = GTK_TREE_MODEL (self->priv->manifest_store);
    GtkTreeIter iter;
    GFile *src = NULL, *dest = NULL, *dest_dir = NULL, *expected = NULL;
    gchar *src_uri = NULL, *dest_uri = NULL, *name = NULL;
    gint state = -1;
    GList one_file = { 0, };
    GtkWindow *parent = GTK_IS_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (self))) ?
        GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (self))) : NULL;
    GtkWidget *dialog;
    gint response;
    gboolean safe = FALSE;
    if (!nemo_progress_info_get_is_finished (self->priv->info) ||
        nemo_progress_info_get_operation (self->priv->info) != NEMO_TRANSFER_OP_COPY ||
        !gtk_tree_selection_get_selected (
            gtk_tree_view_get_selection (GTK_TREE_VIEW (self->priv->manifest_view)),
            &model, &iter)) return;
    gtk_tree_model_get (model, &iter, DAKKAR_MANIFEST_SOURCE_URI, &src_uri,
        DAKKAR_MANIFEST_DESTINATION_URI, &dest_uri,
        DAKKAR_MANIFEST_STATE, &state, -1);
    if (src_uri == NULL || dest_uri == NULL ||
        (state != NEMO_TRANSFER_FILE_FAILED && state != NEMO_TRANSFER_FILE_SKIPPED &&
         state != NEMO_TRANSFER_FILE_VERIFY_FAILED) ||
        g_hash_table_contains (self->priv->retried_sources, src_uri)) goto done;
    src = g_file_new_for_uri (src_uri);
    dest = g_file_new_for_uri (dest_uri);
    dest_dir = g_file_get_parent (dest);
    name = g_file_get_basename (src);
    if (dest_dir == NULL || name == NULL) goto done;
    expected = g_file_get_child (dest_dir, name);
    /* Never silently retry a renamed target, a directory move, or a copy
     * onto itself. A new job always starts with overwrite=FALSE. */
    safe = g_file_equal (expected, dest) && !g_file_equal (src, dest) &&
        g_file_query_file_type (src, G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL) ==
            G_FILE_TYPE_REGULAR &&
        g_file_query_file_type (dest_dir, G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL) ==
            G_FILE_TYPE_DIRECTORY;
    if (!safe) {
        dialog = gtk_message_dialog_new (parent, GTK_DIALOG_MODAL,
            GTK_MESSAGE_WARNING, GTK_BUTTONS_CLOSE,
            "%s", _("This file cannot be retried safely."));
        gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
            "%s", _("The source or destination has changed, or this is not a regular file. No files were modified."));
        gtk_dialog_run (GTK_DIALOG (dialog));
        gtk_widget_destroy (dialog);
        goto done;
    }
    dialog = gtk_message_dialog_new (parent, GTK_DIALOG_MODAL,
        GTK_MESSAGE_QUESTION, GTK_BUTTONS_NONE,
        "%s", _("Retry this copy as a new Nemo job?"));
    gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
        _("Source: %s\nDestination: %s\n\nIf the target exists, Nemo will ask how to resolve the conflict. No overwrite is authorized by this retry."),
        src_uri, dest_uri);
    gtk_dialog_add_buttons (GTK_DIALOG (dialog), _("_Cancel"), GTK_RESPONSE_CANCEL,
        _("_Retry copy"), GTK_RESPONSE_ACCEPT, NULL);
    gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_CANCEL);
    response = gtk_dialog_run (GTK_DIALOG (dialog));
    gtk_widget_destroy (dialog);
    if (response != GTK_RESPONSE_ACCEPT) goto done;
    /* A one-file job reuses the native Nemo queue, error dialog, conflict
     * policy, and progress accounting; no custom overwrite or file I/O. */
    one_file.data = src;
    nemo_file_operations_copy (&one_file, NULL, dest_dir, parent, NULL, NULL);
    g_hash_table_add (self->priv->retried_sources, g_strdup (src_uri));
done:
    g_clear_object (&expected);
    g_clear_object (&dest_dir);
    g_clear_object (&dest);
    g_clear_object (&src);
    g_free (src_uri);
    g_free (dest_uri);
    g_free (name);
    dakkar_update_retry_button (self);
    (void)button;
}

static void
dakkar_update_hold_button (NemoProgressInfoWidget *self)
{
    gboolean waiting = FALSE;
    gboolean requested;
    if (self->priv->hold_button == NULL) return;
    requested = nemo_progress_info_hold_requested (self->priv->info, &waiting);
    gtk_widget_set_visible (self->priv->hold_button,
        !nemo_progress_info_get_is_finished (self->priv->info) &&
        (nemo_progress_info_get_operation (self->priv->info) == NEMO_TRANSFER_OP_COPY ||
         nemo_progress_info_get_operation (self->priv->info) == NEMO_TRANSFER_OP_MOVE));
    gtk_widget_set_sensitive (self->priv->hold_button,
        nemo_progress_info_get_is_started (self->priv->info));
    gtk_button_set_label (GTK_BUTTON (self->priv->hold_button),
        requested ? (waiting ? _("Paused — Resume") : _("Pausing after this file — Resume")) :
                    _("Pause after current file"));
}

static void
dakkar_hold_clicked (GtkButton *button, NemoProgressInfoWidget *self)
{
    if (nemo_progress_info_hold_requested (self->priv->info, NULL))
        nemo_progress_info_release_hold (self->priv->info);
    else
        nemo_progress_info_request_hold (self->priv->info);
    dakkar_update_hold_button (self);
    (void)button;
}

void
nemo_progress_info_widget_set_expanded (NemoProgressInfoWidget *self,
                                          gboolean expanded)
{
    g_return_if_fail (NEMO_IS_PROGRESS_INFO_WIDGET (self));
    self->priv->graph_expanded = expanded;
    if (self->priv->speed_graph != NULL)
        gtk_widget_set_visible (self->priv->speed_graph, expanded &&
                                self->priv->speed_sample_count > 1);
    if (self->priv->manifest_box != NULL) {
        gtk_widget_set_visible (self->priv->manifest_box, expanded);
        if (expanded) {
            self->priv->manifest_last_refresh_us = 0;
            dakkar_refresh_manifest (self);
        }
    }
}

static void
update_data (NemoProgressInfoWidget *self)
{
	char *status, *details;
	char *markup;

	status = nemo_progress_info_get_status (self->priv->info);
	gtk_label_set_text (GTK_LABEL (self->priv->status), status);
	g_free (status);

	details = nemo_progress_info_get_details (self->priv->info);
	markup = g_markup_printf_escaped ("<span size='small'>%s</span>", details);
	gtk_label_set_markup (GTK_LABEL (self->priv->details), markup);
	g_free (details);
	g_free (markup);
    dakkar_update_hold_button (self);
    dakkar_refresh_manifest (self);
}

static void
update_progress (NemoProgressInfoWidget *self)
{
	double progress;

	progress = nemo_progress_info_get_progress (self->priv->info);
    if (self->priv->speed_label != NULL) {
        NemoTransferByteStats stats;
        if (nemo_progress_info_get_byte_stats (self->priv->info, &stats)) {
            gchar *done = g_format_size (stats.completed);
            gchar *total = g_format_size (stats.total);
            gchar *rate = stats.rate_available ?
                g_format_size ((guint64)MAX (stats.current_bytes_per_second, 0.0)) : NULL;
            gchar *label = rate != NULL ?
                g_strdup_printf (_("%s / %s — %s/s"), done, total, rate) :
                g_strdup_printf (_("%s / %s"), done, total);
            gtk_label_set_text (GTK_LABEL (self->priv->speed_label), label);
            gtk_widget_show (self->priv->speed_label);
            g_free (label); g_free (rate); g_free (done); g_free (total);
            if (stats.rate_available) {
                guint i = self->priv->speed_write_index;
                self->priv->speed_samples[i] = stats.current_bytes_per_second;
                self->priv->speed_write_index = (i + 1) % 64;
                self->priv->speed_sample_count = MIN (64, self->priv->speed_sample_count + 1);
                if (self->priv->graph_expanded && self->priv->speed_sample_count > 1)
                    gtk_widget_show (self->priv->speed_graph);
                gtk_widget_queue_draw (self->priv->speed_graph);
            }
        } else {
            gtk_widget_hide (self->priv->speed_label);
        }
    }
    dakkar_update_hold_button (self);
    dakkar_refresh_manifest (self);
	if (progress < 0) {
		gtk_progress_bar_pulse (GTK_PROGRESS_BAR (self->priv->progress_bar));
	} else {
		gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (self->priv->progress_bar), progress);
	}
}

static void
on_info_started (NemoProgressInfoWidget *self) {
    gtk_stack_set_visible_child_name (GTK_STACK (self->priv->stack), "running");
    dakkar_update_hold_button (self);
}

static void
on_info_finished (NemoProgressInfoWidget *self)
{
    if (nemo_progress_info_has_retryable_copies (self->priv->info)) {
        nemo_progress_info_widget_set_expanded (self, TRUE);
        if (self->priv->close_results_button)
            gtk_widget_show (self->priv->close_results_button);
        if (self->priv->retry_button)
            gtk_widget_show (self->priv->retry_button);
        dakkar_update_retry_button (self);
        return;
    }
    gtk_widget_destroy (GTK_WIDGET (self));
}

static void
cancel_clicked (GtkWidget *button,
		NemoProgressInfoWidget *self)
{
	nemo_progress_info_cancel (self->priv->info);

    NemoJobQueue *queue = nemo_job_queue_get ();
    nemo_job_queue_start_job_by_info (queue, self->priv->info);

	gtk_widget_set_sensitive (button, FALSE);
}

/* Reorder a waiting copy/move job in Nemo's existing native queue. */
static void
dakkar_queue_priority_clicked (GtkButton *button, NemoProgressInfoWidget *self)
{
    NemoJobQueue *queue = nemo_job_queue_get ();
    gboolean earlier = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (button),
                                                           "dakkar-queue-earlier"));
    nemo_job_queue_move_pending_transfer (queue, self->priv->info, earlier);
}

static void
start_clicked (GtkWidget *button,
               NemoProgressInfoWidget *self)
{
    NemoJobQueue *queue = nemo_job_queue_get ();
    nemo_job_queue_start_job_by_info (queue, self->priv->info);
}

static void
nemo_progress_info_widget_constructed (GObject *obj)
{
	GtkWidget *label, *progress_bar, *hbox, *vbox, *button, *view, *bb, *main_box;
	NemoProgressInfoWidget *self = NEMO_PROGRESS_INFO_WIDGET (obj);
    NemoProgressInfoWidgetPriv *priv = NEMO_PROGRESS_INFO_WIDGET (obj)->priv;

	G_OBJECT_CLASS (nemo_progress_info_widget_parent_class)->constructed (obj);

    main_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add (GTK_CONTAINER (self), main_box);

    priv->separator = gtk_separator_new (GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start (GTK_BOX (main_box), priv->separator, FALSE, FALSE, 5);

    priv->stack = gtk_stack_new ();
    gtk_stack_set_transition_type (GTK_STACK (priv->stack), GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    gtk_stack_set_transition_duration (GTK_STACK (priv->stack), 200);
    gtk_box_pack_start (GTK_BOX (main_box), priv->stack, FALSE, FALSE, 0);

    gtk_widget_show_all (main_box);

    /* contruct the initial stack page (job just queued, minimal info and
     * start & cancel buttons) */

    view = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    gtk_stack_add_named (GTK_STACK (priv->stack), view, "pending");

    hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_pack_start (GTK_BOX (view), hbox, TRUE, TRUE, 0);

    vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 4);
    gtk_box_pack_start (GTK_BOX (hbox), vbox, TRUE, TRUE, 0);

    gchar *initial_details = nemo_progress_info_get_initial_details (self->priv->info);
    gchar *markup = g_markup_printf_escaped ("<span size='small'>%s</span>", initial_details);

    label = gtk_label_new (NULL);
    gtk_label_set_markup (GTK_LABEL (label), markup);

    g_free (initial_details);
    g_free (markup);

    gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
    gtk_label_set_line_wrap_mode (GTK_LABEL (label), PANGO_WRAP_WORD_CHAR);
    gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
    gtk_label_set_max_width_chars (GTK_LABEL (label), 50);
    gtk_box_pack_start (GTK_BOX (vbox), label, TRUE, TRUE, 2);
    priv->pre_info = label;

    bb = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_halign (bb, GTK_ALIGN_START);
    gtk_widget_set_valign (bb, GTK_ALIGN_CENTER);
    gtk_box_pack_end (GTK_BOX (hbox), bb, FALSE, FALSE, 0);

    if (nemo_progress_info_get_operation (priv->info) == NEMO_TRANSFER_OP_COPY ||
        nemo_progress_info_get_operation (priv->info) == NEMO_TRANSFER_OP_MOVE) {
        GtkWidget *earlier = gtk_button_new_with_label (_("Earlier"));
        GtkWidget *later = gtk_button_new_with_label (_("Later"));
        gtk_widget_set_tooltip_text (earlier, _("Move a waiting transfer earlier in Nemo's queue"));
        gtk_widget_set_tooltip_text (later, _("Move a waiting transfer later in Nemo's queue"));
        g_object_set_data (G_OBJECT (earlier), "dakkar-queue-earlier", GINT_TO_POINTER (1));
        gtk_box_pack_start (GTK_BOX (bb), earlier, FALSE, FALSE, 2);
        gtk_box_pack_start (GTK_BOX (bb), later, FALSE, FALSE, 2);
        g_signal_connect (earlier, "clicked", G_CALLBACK (dakkar_queue_priority_clicked), self);
        g_signal_connect (later, "clicked", G_CALLBACK (dakkar_queue_priority_clicked), self);
    }
    button = gtk_button_new_from_icon_name (START_ICON, GTK_ICON_SIZE_BUTTON);
    gtk_button_set_relief (GTK_BUTTON (button), GTK_RELIEF_NONE);
    gtk_box_pack_start (GTK_BOX (bb), button, FALSE, FALSE, 2);
    g_signal_connect (button, "clicked", G_CALLBACK (start_clicked), self);

    button = gtk_button_new_from_icon_name (STOP_ICON, GTK_ICON_SIZE_BUTTON);
    gtk_button_set_relief (GTK_BUTTON (button), GTK_RELIEF_NONE);
    gtk_box_pack_start (GTK_BOX (bb), button, FALSE, FALSE, 2);
    g_signal_connect (button, "clicked", G_CALLBACK (cancel_clicked), self);

    gtk_widget_show_all (view);

    /* construct normal in-progress stack page */

    view = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    gtk_stack_add_named (GTK_STACK (priv->stack), view, "running");

    hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_pack_start (GTK_BOX (view), hbox, TRUE, TRUE, 0);

    vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 4);
    gtk_box_pack_start (GTK_BOX (hbox), vbox, TRUE, TRUE, 0);

	label = gtk_label_new ("status");
	gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
	gtk_label_set_line_wrap_mode (GTK_LABEL (label), PANGO_WRAP_WORD_CHAR);
    gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
    gtk_label_set_max_width_chars (GTK_LABEL (label), 40);
	gtk_box_pack_start (GTK_BOX (vbox), label, TRUE, FALSE, 2);
	priv->status = label;

	progress_bar = gtk_progress_bar_new ();
	priv->progress_bar = progress_bar;
	gtk_progress_bar_set_pulse_step (GTK_PROGRESS_BAR (progress_bar), 0.05);
	gtk_box_pack_start(GTK_BOX (vbox), progress_bar, TRUE, FALSE, 2);

    label = gtk_label_new ("details");
    gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
    gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
    gtk_label_set_max_width_chars (GTK_LABEL (label), 50);

    gtk_box_pack_start (GTK_BOX (vbox), label, TRUE, FALSE, 2);
    priv->details = label;

    label = gtk_label_new (NULL);
    gtk_label_set_xalign (GTK_LABEL (label), 0.0);
    gtk_widget_set_halign (label, GTK_ALIGN_START);
    gtk_box_pack_start (GTK_BOX (vbox), label, FALSE, FALSE, 0);
    priv->speed_label = label;
    priv->speed_graph = gtk_drawing_area_new ();
    gtk_widget_set_size_request (priv->speed_graph, -1, 54);
    gtk_style_context_add_class (gtk_widget_get_style_context (priv->speed_graph),
                                 "dakkar-transfer-speed-graph");
    g_signal_connect (priv->speed_graph, "draw",
                      G_CALLBACK (dakkar_speed_graph_draw), self);
    gtk_box_pack_start (GTK_BOX (vbox), priv->speed_graph, FALSE, FALSE, 0);

    /* Real per-file manifest from native GFile copy/move callbacks.
     * It is informational: sorting the view does not reorder execution. */
    priv->manifest_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 4);
    gtk_box_pack_start (GTK_BOX (vbox), priv->manifest_box, FALSE, FALSE, 2);
    priv->manifest_summary = gtk_label_new (NULL);
    gtk_label_set_xalign (GTK_LABEL (priv->manifest_summary), 0);
    gtk_box_pack_start (GTK_BOX (priv->manifest_box), priv->manifest_summary,
                        FALSE, FALSE, 0);
    priv->manifest_store = gtk_list_store_new (DAKKAR_MANIFEST_COLS,
                                               G_TYPE_STRING, G_TYPE_STRING,
                                               G_TYPE_STRING, G_TYPE_STRING,
                                               G_TYPE_STRING, G_TYPE_STRING,
                                               G_TYPE_INT);
    priv->retried_sources = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
    {
        GtkWidget *scroller = gtk_scrolled_window_new (NULL, NULL);
        GtkWidget *table = gtk_tree_view_new_with_model (GTK_TREE_MODEL (priv->manifest_store));
        GtkCellRenderer *renderer;
        gtk_widget_set_size_request (scroller, -1, 175);
        gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroller),
                                        GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
        priv->manifest_view = table;
        gtk_tree_selection_set_mode (gtk_tree_view_get_selection (GTK_TREE_VIEW (table)),
                                     GTK_SELECTION_SINGLE);
        g_signal_connect (gtk_tree_view_get_selection (GTK_TREE_VIEW (table)),
                          "changed", G_CALLBACK (dakkar_retry_selection_changed), self);
        gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (table), TRUE);
        renderer = gtk_cell_renderer_text_new ();
        gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (table), -1,
            _("File"), renderer, "text", DAKKAR_MANIFEST_FILE, NULL);
        renderer = gtk_cell_renderer_text_new ();
        gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (table), -1,
            _("Result"), renderer, "text", DAKKAR_MANIFEST_RESULT, NULL);
        renderer = gtk_cell_renderer_text_new ();
        gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (table), -1,
            _("Progress"), renderer, "text", DAKKAR_MANIFEST_SIZE, NULL);
        renderer = gtk_cell_renderer_text_new ();
        gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (table), -1,
            _("SHA-256 (verified)"), renderer, "text", DAKKAR_MANIFEST_SHA256, NULL);
        gtk_container_add (GTK_CONTAINER (scroller), table);
        gtk_box_pack_start (GTK_BOX (priv->manifest_box), scroller, TRUE, TRUE, 0);
        gtk_widget_show_all (priv->manifest_box);
    }
    {
        GtkWidget *actions = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
        priv->retry_button = gtk_button_new_with_label (_("Retry selected copy"));
        gtk_widget_set_tooltip_text (priv->retry_button,
            _("Only after completion; starts a new native Nemo copy with normal conflict confirmation"));
        gtk_widget_set_sensitive (priv->retry_button, FALSE);
        gtk_widget_set_no_show_all (priv->retry_button, TRUE);
        g_signal_connect (priv->retry_button, "clicked",
                          G_CALLBACK (dakkar_retry_selected_copy), self);
        gtk_box_pack_start (GTK_BOX (actions), priv->retry_button, FALSE, FALSE, 0);
        priv->close_results_button = gtk_button_new_with_label (_("Close results"));
        gtk_widget_set_no_show_all (priv->close_results_button, TRUE);
        g_signal_connect (priv->close_results_button, "clicked",
                          G_CALLBACK (dakkar_close_results), self);
        gtk_box_pack_start (GTK_BOX (actions), priv->close_results_button, FALSE, FALSE, 0);
        gtk_box_pack_start (GTK_BOX (priv->manifest_box), actions, FALSE, FALSE, 0);
        gtk_widget_show_all (actions);
        gtk_widget_hide (priv->retry_button);
        gtk_widget_hide (priv->close_results_button);
    }
    g_object_unref (priv->manifest_store);



    bb = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_halign (bb, GTK_ALIGN_START);
    gtk_widget_set_valign (bb, GTK_ALIGN_CENTER);
    gtk_box_pack_end (GTK_BOX (hbox), bb, FALSE, FALSE, 0);

    button = gtk_button_new_from_icon_name (START_ICON, GTK_ICON_SIZE_BUTTON);
    gtk_button_set_relief (GTK_BUTTON (button), GTK_RELIEF_NONE);
    gtk_widget_set_sensitive (button, FALSE);
    gtk_box_pack_start (GTK_BOX (bb), button, FALSE, FALSE, 2);

    priv->hold_button = gtk_button_new_with_label (_("Pause after current file"));
    gtk_widget_set_tooltip_text (priv->hold_button,
        _("Wait at the next file boundary; the current file is not interrupted."));
    gtk_box_pack_start (GTK_BOX (bb), priv->hold_button, FALSE, FALSE, 2);
    g_signal_connect (priv->hold_button, "clicked",
                      G_CALLBACK (dakkar_hold_clicked), self);

	button = gtk_button_new_from_icon_name (STOP_ICON, GTK_ICON_SIZE_BUTTON);
    gtk_button_set_relief (GTK_BUTTON (button), GTK_RELIEF_NONE);
    gtk_box_pack_start (GTK_BOX (bb), button, FALSE, FALSE, 2);
    g_signal_connect (button, "clicked", G_CALLBACK (cancel_clicked), self);

    gtk_widget_show_all (view);
    gtk_widget_hide (priv->speed_graph);
    gtk_widget_hide (priv->speed_label);
    gtk_widget_hide (priv->manifest_box);

	update_data (self);
	update_progress (self);

    g_signal_connect_swapped (priv->info, "changed", G_CALLBACK (update_data), self);
    g_signal_connect_swapped (priv->info, "progress-changed", G_CALLBACK (update_progress), self);

    gboolean started = nemo_progress_info_get_is_started (self->priv->info);
    gtk_stack_set_visible_child_name (GTK_STACK (self->priv->stack), started ? "running" : "pending");
    g_signal_connect_swapped (priv->info, "started", G_CALLBACK (on_info_started), self);

    g_signal_connect_swapped (priv->info, "finished", G_CALLBACK (on_info_finished), self);
}

static void
nemo_progress_info_widget_dispose (GObject *obj)
{
    NemoProgressInfoWidget *self = NEMO_PROGRESS_INFO_WIDGET (obj);

    if (self->priv->manifest_view != NULL) {
        GtkTreeSelection *selection = gtk_tree_view_get_selection (
            GTK_TREE_VIEW (self->priv->manifest_view));
        g_signal_handlers_disconnect_by_data (selection, self);
        /* dispose may run again after the children are gone */
        self->priv->manifest_view = NULL;
    }
    if (self->priv->info != NULL)
        g_signal_handlers_disconnect_by_data (self->priv->info, self);
    g_clear_object (&self->priv->info);
    g_clear_pointer (&self->priv->retried_sources, g_hash_table_unref);

	G_OBJECT_CLASS (nemo_progress_info_widget_parent_class)->dispose (obj);
}

static void
nemo_progress_info_widget_set_property (GObject *object,
					    guint property_id,
					    const GValue *value,
					    GParamSpec *pspec)
{
	NemoProgressInfoWidget *self = NEMO_PROGRESS_INFO_WIDGET (object);

	switch (property_id) {
	case PROP_INFO:
		self->priv->info = g_value_dup_object (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
		break;
	}		
}

static void
nemo_progress_info_widget_init (NemoProgressInfoWidget *self)
{
	self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, NEMO_TYPE_PROGRESS_INFO_WIDGET,
						  NemoProgressInfoWidgetPriv);

	
}

static void
nemo_progress_info_widget_class_init (NemoProgressInfoWidgetClass *klass)
{
	GObjectClass *oclass;

	oclass = G_OBJECT_CLASS (klass);
	oclass->set_property = nemo_progress_info_widget_set_property;
	oclass->constructed = nemo_progress_info_widget_constructed;
	oclass->dispose = nemo_progress_info_widget_dispose;

	properties[PROP_INFO] =
		g_param_spec_object ("info",
				     "NemoProgressInfo",
				     "The NemoProgressInfo associated with this widget",
				     NEMO_TYPE_PROGRESS_INFO,
				     G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY |
				     G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (oclass, NUM_PROPERTIES, properties);

	g_type_class_add_private (klass, sizeof (NemoProgressInfoWidgetPriv));
}

GtkWidget *
nemo_progress_info_widget_new (NemoProgressInfo *info)
{
	return g_object_new (NEMO_TYPE_PROGRESS_INFO_WIDGET,
			     "info", info,
			     "orientation", GTK_ORIENTATION_VERTICAL,
			     "homogeneous", FALSE,
			     "spacing", 5,
			     NULL);
}
