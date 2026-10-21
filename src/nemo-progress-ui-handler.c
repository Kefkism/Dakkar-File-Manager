/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/*
 * nemo-progress-ui-handler.c: file operation progress user interface.
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

#include "nemo-progress-ui-handler.h"
#include "nemo-transfer-window.h"

#include "nemo-application.h"
#include "nemo-progress-info-widget.h"
#include "nemo-statusbar.h"
#include "nemo-window-private.h"
#include "dakkar-transfer-route-choice.h"

#include <gio/gio.h>
#include <glib/gi18n.h>

#include <eel/eel-string.h>

#include <libnemo-private/nemo-progress-info.h>
#include <libnemo-private/nemo-global-preferences.h>
#include <libnemo-private/nemo-progress-info-manager.h>

#include <libxapp/xapp-gtk-window.h>
#include <libxapp/xapp-status-icon.h>
#include <libnemo-private/nemo-icon-names.h>

struct _NemoProgressUIHandlerPriv {
	NemoProgressInfoManager *manager;

	GtkWidget *progress_window;
	GtkWidget *window_vbox;

    GtkWidget *list;
    gboolean transfer_expanded;

	guint active_infos;
    guint active_percent;
    gboolean active_percent_known;
	GList *infos;

	XAppStatusIcon *status_icon;
    gboolean should_show_status_icon;
    gboolean destination_window_hooks_installed;
    gboolean batch_failed_or_unknown;
};

G_DEFINE_TYPE (NemoProgressUIHandler, nemo_progress_ui_handler, G_TYPE_OBJECT);

void
nemo_progress_ui_handler_present (NemoProgressUIHandler *self)
{
    g_return_if_fail (NEMO_IS_PROGRESS_UI_HANDLER (self));
    /* Dakkar: the detailed TeraCopy-style windows replace the old list. */
    if (nemo_transfer_window_present_all ()) {
        self->priv->should_show_status_icon = FALSE;
        if (self->priv->status_icon != NULL)
            xapp_status_icon_set_visible (self->priv->status_icon, FALSE);
        return;
    }
    if (self->priv->progress_window != NULL) {
        self->priv->should_show_status_icon = FALSE;
        if (self->priv->status_icon != NULL)
            xapp_status_icon_set_visible (self->priv->status_icon, FALSE);
        gtk_window_present (GTK_WINDOW (self->priv->progress_window));
    }
}

static void
dakkar_save_transfer_window_size (NemoProgressUIHandler *self)
{
    gint width, height;
    if (self->priv->progress_window == NULL) return;
    gtk_window_get_size (GTK_WINDOW (self->priv->progress_window), &width, &height);
    g_settings_set_int (nemo_preferences, "dakkar-transfer-window-width", CLAMP (width, 360, 2000));
    g_settings_set_int (nemo_preferences, "dakkar-transfer-window-height", CLAMP (height, 220, 1400));
}

static void
dakkar_minimize_progress_to_footer (GtkButton *button, NemoProgressUIHandler *self)
{
    if (self->priv->progress_window != NULL) {
        dakkar_save_transfer_window_size (self);
        gtk_widget_hide (self->priv->progress_window);
    }
    self->priv->should_show_status_icon = FALSE;
    if (self->priv->status_icon != NULL)
        xapp_status_icon_set_visible (self->priv->status_icon, FALSE);
    /* If the last Files window closed, the status icon is the safe fallback. */
    nemo_progress_ui_handler_refresh_global ();
    (void)button;
}

/* Track the handler for tab/destination changes without extra signal hookups.
 * The pointer is invalidated on dispose and no reference cycle is created. */
static NemoProgressUIHandler *dakkar_live_progress_handler;
static void progress_ui_handler_update_status_icon (NemoProgressUIHandler *self);

/* A weak route hint never keeps a tab, pane or window alive. All hints
 * are observed only from the GTK main thread. */
static void
dakkar_weak_route_free (gpointer data)
{
    GWeakRef *ref = data;
    g_weak_ref_clear (ref);
    g_free (ref);
}

static GObject *
dakkar_weak_route_get (NemoProgressInfo *info, const gchar *key)
{
    GWeakRef *ref = g_object_get_data (G_OBJECT (info), key);
    return ref != NULL ? g_weak_ref_get (ref) : NULL;
}

static void
dakkar_weak_route_set (NemoProgressInfo *info, const gchar *key, GObject *target)
{
    GWeakRef *ref = g_object_get_data (G_OBJECT (info), key);
    if (ref == NULL) {
        ref = g_new0 (GWeakRef, 1);
        g_weak_ref_init (ref, target);
        g_object_set_data_full (G_OBJECT (info), key, ref, dakkar_weak_route_free);
    } else {
        g_weak_ref_set (ref, target);
    }
}

static gboolean
dakkar_window_is_open (NemoApplication *app, NemoWindow *window)
{
    return window != NULL &&
        g_list_find (gtk_application_get_windows (GTK_APPLICATION (app)), window) != NULL;
}

static gboolean
dakkar_slot_is_open (NemoApplication *app, NemoWindowSlot *slot)
{
    NemoWindowPane *pane;
    if (slot == NULL || (pane = slot->pane) == NULL || pane->window == NULL)
        return FALSE;
    return dakkar_window_is_open (app, pane->window) &&
        g_list_find (pane->window->details->panes, pane) != NULL &&
        g_list_find (pane->slots, slot) != NULL &&
        gtk_widget_get_parent (GTK_WIDGET (slot)) != NULL;
}

/* The user-selected target stays fixed when focus switches. Only a closed
 * destination tab may be rebound, preferably to a surviving tab in the
 * original pane; otherwise its window status bar remains available. */
static NemoWindowSlot *
dakkar_bound_destination_slot (NemoApplication *app, NemoProgressInfo *info)
{
    GObject *obj = dakkar_weak_route_get (info, "dakkar-bound-slot");
    NemoWindowSlot *bound = obj && NEMO_IS_WINDOW_SLOT (obj) ? NEMO_WINDOW_SLOT (obj) : NULL;
    GFile *dest;
    GObject *origin_object, *pane_object, *view_object;
    NemoWindow *origin = NULL;
    NemoWindowSlot *best = NULL;
    DakkarTransferRouteChoice choice = { 0 };
    GList *w;

    if (dakkar_slot_is_open (app, bound))
        return bound; /* owned reference; never re-route on a focus change */
    g_clear_object (&obj);
    /* An existing association was detached. Do not select a random parent
     * directory in a different window after closing the destination tab. */
    gboolean previously_bound = g_object_get_data (G_OBJECT (info), "dakkar-bound-slot") != NULL;

    dest = nemo_progress_info_get_destination (info);
    if (dest == NULL) return NULL;
    origin_object = dakkar_weak_route_get (info, "dakkar-origin-window");
    if (origin_object != NULL && NEMO_IS_WINDOW (origin_object) &&
        dakkar_window_is_open (app, NEMO_WINDOW (origin_object)))
        origin = NEMO_WINDOW (origin_object);
    view_object = dakkar_weak_route_get (info, "dakkar-origin-view");
    for (w = gtk_application_get_windows (GTK_APPLICATION (app)); w != NULL; w = w->next) {
        NemoWindow *window = NEMO_IS_WINDOW (w->data) ? NEMO_WINDOW (w->data) : NULL;
        GList *p;
        if (window == NULL) continue;
        for (p = window->details->panes; p != NULL; p = p->next) {
            NemoWindowPane *pane = p->data;
            GList *t;
            for (t = pane->slots; t != NULL; t = t->next) {
                NemoWindowSlot *slot = t->data;
                GFile *shown = nemo_window_slot_get_location (slot);
                guint match = 0;
                gsize length = 0;
                gboolean in_origin = window == origin;
                if (shown == NULL) continue;
                if (g_file_equal (dest, shown))
                    match = 2; /* exact target overrides all parent matches */
                else if (!previously_bound && g_file_has_prefix (dest, shown))
                    match = 1;
                /* When the real receiving view is known (DND/paste), it
                 * wins over another tab displaying the very same folder. */
                if (match != 0 && view_object != NULL &&
                    ((gpointer) slot->content_view == (gpointer) view_object ||
                     (gpointer) slot->new_content_view == (gpointer) view_object))
                    match = 3;
                if (match != 0) {
                    gchar *uri = g_file_get_uri (shown);
                    length = uri != NULL ? strlen (uri) : 0;
                    g_free (uri);
                }
                g_object_unref (shown);
                if (match == 0) continue;
                DakkarTransferRouteRank candidate = { match, length, in_origin };
                if (dakkar_transfer_route_consider (&choice, candidate)) {
                    g_clear_object (&best);
                    best = g_object_ref (slot);
                }
                /* On an equal-ranked second candidate, choice.ambiguous is
                 * set; no later focus state can make the tie unique. */
            }
        }
    }
    g_object_unref (dest);
    g_clear_object (&origin_object);
    g_clear_object (&view_object);

    if (choice.ambiguous) {
        g_clear_object (&best); /* use original window footer, not wrong pane */
    }
    if (best == NULL && previously_bound) {
        /* After the original tab closes, preserve its pane when possible.
         * The URI remains on NemoProgressInfo, not on this replacement tab. */
        pane_object = dakkar_weak_route_get (info, "dakkar-bound-pane");
        if (pane_object != NULL && NEMO_IS_WINDOW_PANE (pane_object)) {
            NemoWindowPane *pane = NEMO_WINDOW_PANE (pane_object);
            if (pane->active_slot != NULL &&
                dakkar_slot_is_open (app, pane->active_slot))
                best = g_object_ref (pane->active_slot);
        }
        g_clear_object (&pane_object);
    }
    if (best != NULL) {
        dakkar_weak_route_set (info, "dakkar-bound-slot", G_OBJECT (best));
        dakkar_weak_route_set (info, "dakkar-bound-pane", G_OBJECT (best->pane));
        dakkar_weak_route_set (info, "dakkar-bound-window", G_OBJECT (best->pane->window));
    }
    return best; /* owned reference or NULL */
}


/* Render the combined transfer state into every Files window. The old
 * per-tab progress footers are kept hidden; the weak destination binding
 * is still used for the completion notice in the destination tab. */
static void
dakkar_refresh_transfer_footers (NemoProgressUIHandler *self)
{
    NemoApplication *app = nemo_application_get_singleton ();
    GList *w;
    gboolean has_files_window = FALSE;
    /* Dakkar transfer UI: while anything runs, the bottom status bar of
     * EVERY Files window shows one combined bar (% / speed / time left,
     * pause button); clicking it reopens the detailed windows. When the
     * last job finishes the normal status text returns. */
    for (w = gtk_application_get_windows (GTK_APPLICATION (app)); w != NULL; w = w->next) {
        NemoWindow *window = NEMO_IS_WINDOW (w->data) ? NEMO_WINDOW (w->data) : NULL;
        GList *p;
        gdouble fraction = 0.0;
        gboolean paused = FALSE, can_pause = FALSE;
        gchar *caption;
        if (window == NULL) continue;
        has_files_window = TRUE;
        for (p = window->details->panes; p != NULL; p = p->next) {
            NemoWindowPane *pane = p->data;
            GList *t;
            for (t = pane->slots; t != NULL; t = t->next)
                nemo_window_slot_set_transfer (t->data, NULL, 0.0, NULL);
        }
        if (window->details->nemo_status_bar == NULL) continue;
        caption = nemo_transfer_window_dup_caption (
            gtk_widget_get_allocated_width (GTK_WIDGET (window)),
            &fraction, &paused, &can_pause);
        nemo_status_bar_set_transfer (NEMO_STATUS_BAR (window->details->nemo_status_bar),
                                      caption, fraction, caption ? G_OBJECT (self) : NULL);
        nemo_status_bar_set_transfer_paused (NEMO_STATUS_BAR (window->details->nemo_status_bar),
                                             caption != NULL && can_pause, paused);
        if (caption != NULL) {
            gboolean verifying = FALSE;
            gdouble verify_fraction = 0.0;
            guint failed = 0;
            NemoStatusBarTransferState state = NEMO_STATUS_BAR_TRANSFER_NORMAL;
            nemo_transfer_window_get_bar_state (&verifying, &verify_fraction, &failed);
            if (paused)
                state = NEMO_STATUS_BAR_TRANSFER_PAUSED;
            else if (failed > 0)
                state = NEMO_STATUS_BAR_TRANSFER_ERROR;
            else if (verifying)
                state = NEMO_STATUS_BAR_TRANSFER_VERIFYING;
            nemo_status_bar_set_transfer_state (NEMO_STATUS_BAR (window->details->nemo_status_bar),
                                                state, verifying ? verify_fraction : -1.0);
        }
        g_free (caption);
    }
    /* Never strand an active or paused job when its last destination
     * window closes. The original transfer dialog remains available via
     * the system indicator; opening a new Files window restores footers. */
    if (self->priv->active_infos > 0 && !has_files_window) {
        self->priv->should_show_status_icon = TRUE;
        progress_ui_handler_update_status_icon (self);
    } else if (has_files_window && self->priv->should_show_status_icon) {
        self->priv->should_show_status_icon = FALSE;
        if (self->priv->status_icon != NULL)
            xapp_status_icon_set_visible (self->priv->status_icon, FALSE);
    }
}

void
nemo_progress_ui_handler_refresh_global (void)
{
    if (dakkar_live_progress_handler != NULL)
        dakkar_refresh_transfer_footers (dakkar_live_progress_handler);
}

static void
status_icon_activate_cb (XAppStatusIcon        *icon,
                         guint                  button,
                         guint                  _time,
                         NemoProgressUIHandler *self)
{
    self->priv->should_show_status_icon = FALSE;
    xapp_status_icon_set_visible (icon, FALSE);
    nemo_progress_ui_handler_present (self);
}

static void
progress_ui_handler_ensure_status_icon (NemoProgressUIHandler *self)
{
	XAppStatusIcon *status_icon;

	if (self->priv->status_icon != NULL) {
		return;
	}

    status_icon = xapp_status_icon_new ();
    xapp_status_icon_set_icon_name (status_icon, "nemo-progress-0-symbolic");
    g_signal_connect (status_icon, "activate",
                      (GCallback) status_icon_activate_cb,
                      self);

	xapp_status_icon_set_visible (status_icon, FALSE);

	self->priv->status_icon = status_icon;
}

static gchar *
get_icon_name_from_percent (guint pct)
{
    gchar *icon_name;
    guint rounded = 0;
    gint ones = pct % 10;

    if (ones < 5)
        rounded = pct - ones;
    else
        rounded = pct + (10 - ones);

    icon_name = g_strdup_printf ("nemo-progress-%d-symbolic", rounded);

    return icon_name;
}

static void
progress_ui_handler_update_status_icon (NemoProgressUIHandler *self)
{
    gchar *tooltip, *name;
    progress_ui_handler_ensure_status_icon (self);
    if (self->priv->active_percent_known) {
        tooltip = g_strdup_printf (ngettext ("%u file operation active · %u%% complete",
                                             "%u file operations active · %u%% complete",
                                             self->priv->active_infos),
                                   self->priv->active_infos, self->priv->active_percent);
    } else {
        tooltip = g_strdup_printf (ngettext ("%u file operation active",
                                             "%u file operations active",
                                             self->priv->active_infos),
                                   self->priv->active_infos);
    }
    xapp_status_icon_set_tooltip_text (self->priv->status_icon, tooltip);
    name = get_icon_name_from_percent (self->priv->active_percent);
    xapp_status_icon_set_icon_name (self->priv->status_icon, name);
    g_free (name);
    g_free (tooltip);
    xapp_status_icon_set_visible (self->priv->status_icon, self->priv->should_show_status_icon);
}

static gboolean
progress_window_delete_event (GtkWidget *widget,
			      GdkEvent *event,
			      NemoProgressUIHandler *self)
{
    dakkar_save_transfer_window_size (self);
    gtk_widget_hide (widget);

    /* Closing the detailed dialog returns to the footer, not a second UI. */
    self->priv->should_show_status_icon = FALSE;
    if (self->priv->status_icon != NULL)
        xapp_status_icon_set_visible (self->priv->status_icon, FALSE);
    nemo_progress_ui_handler_refresh_global ();
    return TRUE;
}

static void
ensure_first_separator_hidden (NemoProgressUIHandler *self)
{
    GList *l = gtk_container_get_children (GTK_CONTAINER (self->priv->list));

    if (l == NULL)
        return;

    NemoProgressInfoWidgetPriv *priv = NEMO_PROGRESS_INFO_WIDGET (l->data)->priv;

    gtk_widget_hide (GTK_WIDGET (priv->separator));

    g_list_free (l);
}

static void
progress_ui_handler_sort_by_active (NemoProgressUIHandler *self)
{
    gint first_pending = -1;
    gint current_index = 0;
    GList *iter;
    GList *l = gtk_container_get_children (GTK_CONTAINER (self->priv->list));

    if (l == NULL)
        return;

    for (iter = l; iter != NULL; iter = iter->next) {
        NemoProgressInfoWidgetPriv *priv = NEMO_PROGRESS_INFO_WIDGET (iter->data)->priv;

        if (nemo_progress_info_get_is_started (priv->info)) {
            if (first_pending > 0) {
                gtk_box_reorder_child (GTK_BOX (self->priv->list), GTK_WIDGET (iter->data), first_pending);
                break;
            }
        } else {
            if (first_pending == -1) {
                first_pending = current_index;
            }
        }

        current_index++;
    }

    g_list_free (l);
}

static void
dakkar_transfer_expanded_toggled (GtkToggleButton *button, gpointer user_data)
{
    NemoProgressUIHandler *self = NEMO_PROGRESS_UI_HANDLER (user_data);
    GList *children, *it;
    self->priv->transfer_expanded = gtk_toggle_button_get_active (button);
    g_settings_set_boolean (nemo_preferences, "dakkar-transfer-expanded",
                            self->priv->transfer_expanded);
    children = gtk_container_get_children (GTK_CONTAINER (self->priv->list));
    for (it = children; it != NULL; it = it->next)
        if (NEMO_IS_PROGRESS_INFO_WIDGET (it->data))
            nemo_progress_info_widget_set_expanded (NEMO_PROGRESS_INFO_WIDGET (it->data),
                                                     self->priv->transfer_expanded);
    g_list_free (children);
}

static void
progress_ui_handler_ensure_window (NemoProgressUIHandler *self)
{
    NemoProgressUIHandlerPriv *priv = NEMO_PROGRESS_UI_HANDLER (self)->priv;

    GtkWidget *main_box, *progress_window;
    GtkWidget *w, *frame;

	if (self->priv->progress_window != NULL) {
		return;
	}
	
	progress_window = xapp_gtk_window_new (GTK_WINDOW_TOPLEVEL);
	self->priv->progress_window = progress_window;

    gtk_window_set_type_hint (GTK_WINDOW (progress_window), GDK_WINDOW_TYPE_HINT_DIALOG);
    gtk_window_set_resizable (GTK_WINDOW (progress_window), TRUE);
    gtk_window_set_default_size (GTK_WINDOW (progress_window),
        CLAMP (g_settings_get_int (nemo_preferences, "dakkar-transfer-window-width"), 360, 2000),
        CLAMP (g_settings_get_int (nemo_preferences, "dakkar-transfer-window-height"), 220, 1400));

	gtk_window_set_title (GTK_WINDOW (progress_window),
			      _("File Operations"));
	gtk_window_set_wmclass (GTK_WINDOW (progress_window),
				"file_progress", "Dakkar");
	gtk_window_set_position (GTK_WINDOW (progress_window),
				 GTK_WIN_POS_CENTER);
	xapp_gtk_window_set_icon_name (XAPP_GTK_WINDOW (progress_window),
                                   DAKKAR_APP_ICON);

	main_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 2);
	gtk_container_add (GTK_CONTAINER (progress_window),
                       main_box);
	self->priv->window_vbox = main_box;

    frame = gtk_frame_new (NULL);
    gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_NONE);

    w = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    priv->list = w;
    gtk_container_add (GTK_CONTAINER (frame), w);

    g_object_set (priv->list,
                  "margin-left", 5,
                  "margin-right", 5,
                  "margin-top", 5,
                  "margin-bottom", 5,
                  NULL);

    gtk_box_pack_start (GTK_BOX (main_box), frame, FALSE, FALSE, 0);
    w = gtk_check_button_new_with_label (_("Show transfer speed graph"));
    gtk_box_pack_start (GTK_BOX (main_box), w, FALSE, FALSE, 4);
    self->priv->transfer_expanded = g_settings_get_boolean (
        nemo_preferences, "dakkar-transfer-expanded");
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (w), self->priv->transfer_expanded);
    g_signal_connect (w, "toggled", G_CALLBACK (dakkar_transfer_expanded_toggled), self);
    w = gtk_button_new_with_label (_("Show in Files footer"));
    gtk_widget_set_tooltip_text (w, _("Hide this dialog; keep transfer progress in the destination window"));
    gtk_box_pack_end (GTK_BOX (main_box), w, FALSE, FALSE, 4);
    g_signal_connect (w, "clicked", G_CALLBACK (dakkar_minimize_progress_to_footer), self);
    gtk_widget_show_all (main_box);

	g_signal_connect (progress_window,
			  "delete-event",
			  (GCallback) progress_window_delete_event, self);
}

static void
progress_ui_handler_add_to_window (NemoProgressUIHandler *self,
				   NemoProgressInfo *info)
{
	GtkWidget *progress;

	progress = nemo_progress_info_widget_new (info);

	progress_ui_handler_ensure_window (self);
    nemo_progress_info_widget_set_expanded (NEMO_PROGRESS_INFO_WIDGET (progress),
                                            self->priv->transfer_expanded);

    gtk_box_pack_start (GTK_BOX (self->priv->list), progress, FALSE, FALSE, 0);
    gtk_widget_show (progress);

    ensure_first_separator_hidden (self);
    progress_ui_handler_sort_by_active (self);
}

static void
progress_ui_handler_show_complete_notification (NemoProgressUIHandler *self)
{
	GNotification *complete_notification;
    if (self->priv->batch_failed_or_unknown ||
        !g_settings_get_boolean (nemo_preferences,
            "dakkar-transfer-completion-notifications"))
        return;

	complete_notification = g_notification_new (_("File Operations"));
	g_notification_set_body (complete_notification, _("All file operations have been successfully completed"));
	{
		GIcon *app_icon = g_themed_icon_new (DAKKAR_APP_ICON);
		g_notification_set_icon (complete_notification, app_icon);
		g_object_unref (app_icon);
	}
	
	g_application_send_notification (G_APPLICATION (nemo_application_get_singleton ()), NULL, complete_notification);
	g_object_unref (complete_notification);
}

static void
progress_ui_handler_hide_status (NemoProgressUIHandler *self)
{
	if (self->priv->status_icon != NULL) {
        self->priv->should_show_status_icon = FALSE;
		xapp_status_icon_set_visible (self->priv->status_icon, FALSE);
	}
}

/* Results for failed copies are intentionally retained. A later successful
 * job must not hide those unresolved results or announce universal success. */
static gboolean
dakkar_has_retained_copy_results (NemoProgressUIHandler *self)
{
    GList *children, *l;
    gboolean found = FALSE;
    if (self->priv->list == NULL) return FALSE;
    children = gtk_container_get_children (GTK_CONTAINER (self->priv->list));
    for (l = children; l != NULL; l = l->next) {
        if (NEMO_IS_PROGRESS_INFO_WIDGET (l->data)) {
            NemoProgressInfoWidget *widget = NEMO_PROGRESS_INFO_WIDGET (l->data);
            if (widget->priv->info != NULL &&
                nemo_progress_info_get_is_finished (widget->priv->info) &&
                nemo_progress_info_has_retryable_copies (widget->priv->info)) {
                found = TRUE;
                break;
            }
        }
    }
    g_list_free (children);
    return found;
}

static void
progress_info_finished_cb (NemoProgressInfo *info,
			   NemoProgressUIHandler *self)
{
    gboolean retryable = nemo_progress_info_has_retryable_copies (info);
    /* No success notification if even one member of this batch failed,
     * was cancelled, skipped, or lacks a complete verified outcome. */
    if (!nemo_progress_info_has_confirmed_success (info))
        self->priv->batch_failed_or_unknown = TRUE;
	self->priv->active_infos--;
    self->priv->active_percent_known = FALSE; /* previous aggregate no longer applies */
    /* Capture the confirmed destination before removing the finished job;
     * only a real file-level success, never a mere 100% estimate, qualifies. */
    NemoWindowSlot *completed_slot = NULL;
    if (nemo_progress_info_has_confirmed_success (info)) {
        completed_slot = dakkar_bound_destination_slot (
            nemo_application_get_singleton (), info);
    }
	self->priv->infos = g_list_remove (self->priv->infos, info);
	dakkar_refresh_transfer_footers (self);
    if (completed_slot != NULL) {
        gboolean still_active = FALSE;
        GList *it;
        for (it = self->priv->infos; it != NULL; it = it->next) {
            NemoWindowSlot *target = dakkar_bound_destination_slot (
                nemo_application_get_singleton (), it->data);
            if (target == completed_slot) still_active = TRUE;
            g_clear_object (&target);
            if (still_active) break;
        }
        if (!still_active)
            nemo_window_slot_show_transfer_completed (completed_slot);
        g_object_unref (completed_slot);
    }

    /* Failed/skipped results are reported by the transfer summary dialog
     * (nemo-transfer-window.c); the legacy list is no longer popped up. */
    (void) retryable;
    if (self->priv->active_infos > 0) {
		if (!gtk_widget_get_visible (self->priv->progress_window)) {
			progress_ui_handler_update_status_icon (self);
		}

        ensure_first_separator_hidden (self);
	} else {
		if (gtk_widget_get_visible (self->priv->progress_window)) {
			gtk_widget_hide (self->priv->progress_window);
		} else {
			progress_ui_handler_hide_status (self);
			progress_ui_handler_show_complete_notification (self);
		}
	}
}

static void
progress_info_changed_cb (NemoProgressInfo *info,
                          NemoProgressUIHandler *self)
{
    if (self->priv->infos != NULL && self->priv->progress_window != NULL) {
        NemoProgressInfo *first = self->priv->infos->data;
        GList *it;
        gdouble total = 0.0;
        guint count = 0;
        gboolean all_known = TRUE;
        gchar *status = nemo_progress_info_get_status (first);
        for (it = self->priv->infos; it != NULL; it = it->next) {
            gdouble fraction = nemo_progress_info_get_progress (it->data);
            if (fraction < 0.0)
                all_known = FALSE;
            else
                total += CLAMP (fraction, 0.0, 1.0);
            count++;
        }
        self->priv->active_percent_known = count != 0 && all_known;
        if (self->priv->active_percent_known) {
            self->priv->active_percent = (guint) (100.0 * total / count);
            gchar *caption = g_strdup_printf (_("%u%% %s"),
                           self->priv->active_percent,
                           status != NULL ? status : _("Transferring"));
            gtk_window_set_title (GTK_WINDOW (self->priv->progress_window), caption);
            xapp_gtk_window_set_progress (XAPP_GTK_WINDOW (self->priv->progress_window),
                                         self->priv->active_percent);
            g_free (caption);
        } else {
            self->priv->active_percent = 0;
            gtk_window_set_title (GTK_WINDOW (self->priv->progress_window),
                                  status != NULL ? status : _("Transferring"));
            xapp_gtk_window_set_progress (XAPP_GTK_WINDOW (self->priv->progress_window), 0);
        }
        if (self->priv->should_show_status_icon)
            progress_ui_handler_update_status_icon (self);
        g_free (status);
    }
    dakkar_refresh_transfer_footers (self);
    (void)info;
}

/* Recalculate weak job routing after a Files window appears/disappears.
 * Closed windows are never kept alive for a transfer status button. */
static void
dakkar_application_window_changed (GtkApplication *application,
                                  GtkWindow *window,
                                  NemoProgressUIHandler *self)
{
    dakkar_refresh_transfer_footers (self);
    (void) application;
    (void) window;
}

static void
progress_info_started_cb (NemoProgressUIHandler *self)
{
    progress_ui_handler_sort_by_active (self);
    ensure_first_separator_hidden (self);
}

static void
handle_new_progress_info (NemoProgressUIHandler *self,
			  NemoProgressInfo *info)
{
	self->priv->infos = g_list_append (self->priv->infos, info);	
	
	g_signal_connect_after (info, "finished",
			  G_CALLBACK (progress_info_finished_cb), self);

    g_signal_connect_swapped (info, "started",
              G_CALLBACK (progress_info_started_cb), self);
			  
	g_signal_connect (info, "progress-changed",
			  G_CALLBACK (progress_info_changed_cb), self);

    if (self->priv->active_infos == 0)
        self->priv->batch_failed_or_unknown =
            dakkar_has_retained_copy_results (self);
	self->priv->active_infos++;
    if (!self->priv->destination_window_hooks_installed) {
        NemoApplication *app = nemo_application_get_singleton ();
        g_signal_connect_object (app, "window-added",
            G_CALLBACK (dakkar_application_window_changed), self, 0);
        g_signal_connect_object (app, "window-removed",
            G_CALLBACK (dakkar_application_window_changed), self, 0);
        self->priv->destination_window_hooks_installed = TRUE;
    }

	/* The legacy list widget is still maintained (hidden) for its
	 * bookkeeping; the user sees the detailed transfer window instead. */
	progress_ui_handler_add_to_window (self, info);
	if (self->priv->active_infos == 1)
		gtk_window_set_title (GTK_WINDOW (self->priv->progress_window),
		                      nemo_progress_info_get_details (info));
	if (self->priv->should_show_status_icon)
		progress_ui_handler_update_status_icon (self);
	nemo_transfer_window_track (info, g_settings_get_boolean (nemo_preferences,
	                                  "dakkar-transfer-show-window-on-start"));
    dakkar_refresh_transfer_footers (self);
}

typedef struct {
	NemoProgressInfo *info;
	NemoProgressUIHandler *self;
} TimeoutData;

static void
timeout_data_free (TimeoutData *data)
{
	g_clear_object (&data->self);
	g_clear_object (&data->info);

	g_free (data);
}

static TimeoutData *
timeout_data_new (NemoProgressUIHandler *self,
		  NemoProgressInfo *info)
{
	TimeoutData *retval;

	retval = g_new0 (TimeoutData, 1);
	retval->self = g_object_ref (self);
	retval->info = g_object_ref (info);

	return retval;
}

static gboolean
new_op_queued_timeout (TimeoutData *data)
{
	NemoProgressInfo *info = data->info;
	NemoProgressUIHandler *self = data->self;

	if (nemo_progress_info_get_is_paused (info)) {
		return TRUE;
	}

	if (!nemo_progress_info_get_is_finished (info)) {
		handle_new_progress_info (self, info);
	}

	timeout_data_free (data);

	return FALSE;
}

static void
release_application (NemoProgressInfo *info,
		     NemoProgressUIHandler *self)
{
	NemoApplication *app;

	/* release the GApplication hold we acquired */
	app = nemo_application_get_singleton ();
	g_application_release (G_APPLICATION (app));
}

static void
progress_info_queued_cb (NemoProgressInfo *info,
			  NemoProgressUIHandler *self)
{
	NemoApplication *app;
	TimeoutData *data;

	/* hold GApplication so we never quit while there's an operation pending */
	app = nemo_application_get_singleton ();
	g_application_hold (G_APPLICATION (app));

	g_signal_connect (info, "finished",
			  G_CALLBACK (release_application), self);
	nemo_transfer_window_watch (info);

	data = timeout_data_new (self, info);

	/* timeout for the progress window to appear */
	g_timeout_add_seconds (2,
			       (GSourceFunc) new_op_queued_timeout,
			       data);
}

static void
new_progress_info_cb (NemoProgressInfoManager *manager,
		      NemoProgressInfo *info,
		      NemoProgressUIHandler *self)
{
    g_signal_connect (info, "queued",
                      G_CALLBACK (progress_info_queued_cb), self);
}

static void
nemo_progress_ui_handler_dispose (GObject *obj)
{
	NemoProgressUIHandler *self = NEMO_PROGRESS_UI_HANDLER (obj);

    if (dakkar_live_progress_handler == self) dakkar_live_progress_handler = NULL;
	g_clear_object (&self->priv->manager);

	G_OBJECT_CLASS (nemo_progress_ui_handler_parent_class)->dispose (obj);
}

static void
nemo_progress_ui_handler_init (NemoProgressUIHandler *self)
{
	self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, NEMO_TYPE_PROGRESS_UI_HANDLER,
						  NemoProgressUIHandlerPriv);

	self->priv->manager = nemo_progress_info_manager_new ();
    dakkar_live_progress_handler = self;
	nemo_transfer_window_init_prompts ();
	g_signal_connect (self->priv->manager, "new-progress-info",
			  G_CALLBACK (new_progress_info_cb), self);
    self->priv->should_show_status_icon = FALSE;
}

static void
nemo_progress_ui_handler_class_init (NemoProgressUIHandlerClass *klass)
{
	GObjectClass *oclass;

	oclass = G_OBJECT_CLASS (klass);
	oclass->dispose = nemo_progress_ui_handler_dispose;
	
	g_type_class_add_private (klass, sizeof (NemoProgressUIHandlerPriv));
}

NemoProgressUIHandler *
nemo_progress_ui_handler_new (void)
{
	return g_object_new (NEMO_TYPE_PROGRESS_UI_HANDLER, NULL);
}
