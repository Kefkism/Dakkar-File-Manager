/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*-

   nemo-window-slot.c: Nemo window slot

   Copyright (C) 2008 Free Software Foundation, Inc.

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

   Author: Christian Neumair <cneumair@gnome.org>
*/

#include "config.h"

#include "nemo-window-slot.h"

#include "nemo-actions.h"
#include "nemo-desktop-window.h"
#include "nemo-floating-bar.h"
#include "nemo-window-private.h"
#include "nemo-window-manage-views.h"
#include "nemo-window-types.h"
#include "nemo-window-slot-dnd.h"
#include "nemo-miller-columns-view.h"
#include "nemo-list-view.h"
#include "nemo-icon-view.h"
#include "nemo-progress-ui-handler.h"
#include "nemo-quick-actions.h"
#include "dakkar-inspector-layout.h"
#include "nemo-v5-folder-size.h"
#include "dakkar-grouping.h"
#include "dakkar-qa-run.h"
#include "dakkar-preview.h"
#include "dakkar-media.h"

#include <glib/gi18n.h>

#include <libnemo-private/nemo-icon-names.h>
#include <libnemo-private/nemo-file.h>
#include <libnemo-private/nemo-file-utilities.h>
#include <libnemo-private/nemo-global-preferences.h>
#include <libnemo-private/nemo-progress-info.h>

#define DEBUG_FLAG NEMO_DEBUG_WINDOW
#include <libnemo-private/nemo-debug.h>

#include <eel/eel-string.h>

G_DEFINE_TYPE (NemoWindowSlot, nemo_window_slot, GTK_TYPE_BOX);

/* Group headings are widgets, NEVER synthetic NemoFile rows. Each tab owns
 * its collapsed keys and the file model remains Nemo's original model. */
static void dakkar_slot_refresh_group_headings (NemoWindowSlot *slot);

static void
dakkar_group_header_toggled (GtkToggleButton *button, NemoWindowSlot *slot)
{
    const gchar *key = g_object_get_data (G_OBJECT (button), "dakkar-group-key");
    gboolean collapsed = !gtk_toggle_button_get_active (button);
    if (!key || !slot->dakkar_group_collapsed) return;
    if (collapsed)
        g_hash_table_add (slot->dakkar_group_collapsed, g_strdup (key));
    else
        g_hash_table_remove (slot->dakkar_group_collapsed, key);

    /* Keep selections belonging to other groups. Only remove selections
     * that the newly collapsed group will actually hide. Force reload then
     * captures those remaining selections through Nemo's native machinery. */
    if (slot->content_view != NULL) {
        GList *selection = nemo_view_get_selection (slot->content_view);
        GList *remaining = NULL, *item;
        for (item = selection; item != NULL; item = item->next) {
            NemoFile *file = item->data;
            gchar *group = dakkar_group_file_key (file,
                                                 nemo_window_slot_get_group_mode (slot));
            if (!collapsed || g_strcmp0 (group, key) != 0)
                remaining = g_list_prepend (remaining, file);
            g_free (group);
        }
        remaining = g_list_reverse (remaining);
        if (g_list_length (remaining) != g_list_length (selection))
            nemo_view_set_selection (slot->content_view, remaining);
        g_list_free (remaining); /* borrowed NemoFile entries */
        g_list_free_full (selection, g_object_unref);
        nemo_window_slot_force_reload (slot);
    }
}

static void
dakkar_slot_refresh_group_headings (NemoWindowSlot *slot)
{
    gchar *mode;
    GList *files, *item, *children;
    NemoDirectory *directory;
    GHashTable *counts;
    GList *keys, *key_iter;

    if (slot->dakkar_group_bar == NULL) return;
    /* these views draw their own inline group headings; the strip would
     * duplicate them and force a destructive reload */
    if (slot->content_view != NULL &&
        (NEMO_IS_MILLER_COLUMNS_VIEW (slot->content_view) ||
         NEMO_IS_LIST_VIEW (slot->content_view) ||
         NEMO_IS_ICON_VIEW (slot->content_view))) {
        gtk_widget_hide (slot->dakkar_group_strip);
        return;
    }
    children = gtk_container_get_children (GTK_CONTAINER (slot->dakkar_group_bar));
    for (item = children; item; item = item->next)
        gtk_widget_destroy (GTK_WIDGET (item->data));
    g_list_free (children);

    mode = g_strdup (slot->dakkar_group_mode != NULL ? slot->dakkar_group_mode : "none");
    if (g_str_equal (mode, "none") || slot->viewed_file == NULL) {
        gtk_widget_hide (slot->dakkar_group_strip);
        g_free (mode);
        return;
    }
    directory = nemo_directory_get_for_file (slot->viewed_file);
    if (directory == NULL) {
        gtk_widget_hide (slot->dakkar_group_strip);
        g_free (mode);
        return;
    }
    counts = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
    files = nemo_directory_get_file_list (directory);
    for (item = files; item; item = item->next) {
        NemoFile *file = NEMO_FILE (item->data);
        gchar *key = dakkar_group_file_key (file, mode);
        guint count = GPOINTER_TO_UINT (g_hash_table_lookup (counts, key));
        /* Keys generated from file metadata; not file names or paths. */
        g_hash_table_replace (counts, key, GUINT_TO_POINTER (count + 1));
    }
    nemo_file_list_free (files);
    nemo_directory_unref (directory);

    keys = g_hash_table_get_keys (counts);
    gboolean have_groups = (keys != NULL);
    keys = g_list_sort (keys, (GCompareFunc) g_utf8_collate);
    for (key_iter = keys; key_iter; key_iter = key_iter->next) {
        const gchar *key = key_iter->data;
        const gchar *label = dakkar_group_display_label (key);
        guint count = GPOINTER_TO_UINT (g_hash_table_lookup (counts, key));
        gboolean collapsed = g_hash_table_contains (slot->dakkar_group_collapsed, key);
        gchar *caption = g_strdup_printf ("%s (%u)", label, count);
        GtkWidget *toggle = gtk_toggle_button_new_with_label (caption);
        g_free (caption);
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (toggle), !collapsed);
        gtk_widget_set_tooltip_text (toggle, collapsed ?
            _("Expand this group") : _("Collapse this group"));
        gtk_style_context_add_class (gtk_widget_get_style_context (toggle),
                                     "dakkar-group-heading");
        g_object_set_data_full (G_OBJECT (toggle), "dakkar-group-key", g_strdup (key), g_free);
        gtk_box_pack_start (GTK_BOX (slot->dakkar_group_bar), toggle, FALSE, FALSE, 0);
        g_signal_connect (toggle, "toggled",
                          G_CALLBACK (dakkar_group_header_toggled), slot);
        gtk_widget_show (toggle);
    }
    g_list_free (keys);
    g_hash_table_unref (counts);
    gtk_widget_set_visible (slot->dakkar_group_strip, have_groups);
    g_free (mode);
}

static void
dakkar_slot_group_mode_changed (GSettings *settings, gchar *key,
                               NemoWindowSlot *slot)
{
    /* External changes update the inherited default, not an already-open
     * tab's independent choice. Newly opened tabs pick up the new default. */
    (void)settings; (void)key; (void)slot;
}


/* One location tab owns its grouping choice. All four view sorters read it
 * from their own view/model, so switching the other pane never reorders us. */
const gchar *
nemo_window_slot_get_group_mode (NemoWindowSlot *slot)
{
    g_return_val_if_fail (NEMO_IS_WINDOW_SLOT (slot), "none");
    return slot->dakkar_group_mode != NULL ? slot->dakkar_group_mode : "none";
}

static void
dakkar_slot_attach_group_mode (NemoWindowSlot *slot, NemoView *view)
{
    if (view == NULL) return;
    g_object_set_data_full (G_OBJECT (view), "dakkar-group-mode",
                            g_strdup (nemo_window_slot_get_group_mode (slot)), g_free);
    if (NEMO_IS_LIST_VIEW (view))
        nemo_list_view_set_group_mode (NEMO_LIST_VIEW (view),
                                       nemo_window_slot_get_group_mode (slot));
    else if (NEMO_IS_ICON_VIEW (view))
        nemo_icon_view_resort_groups (NEMO_ICON_VIEW (view));
    else if (NEMO_IS_MILLER_COLUMNS_VIEW (view))
        nemo_miller_columns_view_resort_groups (NEMO_MILLER_COLUMNS_VIEW (view));
}

void
nemo_window_slot_set_group_mode (NemoWindowSlot *slot, const gchar *mode)
{
    static const char *const allowed[] = { "none", "type", "name", "size",
        "created", "modified", "owner", "permissions", NULL };
    guint i;
    gboolean valid = FALSE;
    g_return_if_fail (NEMO_IS_WINDOW_SLOT (slot));
    for (i = 0; allowed[i] != NULL; i++) {
        if (g_strcmp0 (mode, allowed[i]) == 0) { valid = TRUE; break; }
    }
    if (!valid || g_strcmp0 (mode, nemo_window_slot_get_group_mode (slot)) == 0)
        return;
    g_free (slot->dakkar_group_mode);
    slot->dakkar_group_mode = g_strdup (mode);
    g_hash_table_remove_all (slot->dakkar_group_collapsed);
    dakkar_slot_attach_group_mode (slot, slot->content_view);
    dakkar_slot_refresh_group_headings (slot);
    /* Reload through Nemo's own view machinery to preserve source identity,
     * selection and filesystem monitors. This never initiates any file action. */
    if (slot->content_view != NULL)
        nemo_window_slot_force_reload (slot);
}

/* This control belongs to the tab, not the global window status bar.
 * Its callback holds only a weak connection to the existing Nemo UI handler. */
static void
dakkar_slot_transfer_clicked (GtkButton *button, gpointer handler)
{
    nemo_progress_ui_handler_present (NEMO_PROGRESS_UI_HANDLER (handler));
    (void)button;
}

static void
dakkar_slot_transfer_hold_clicked (GtkButton *button, gpointer user_data)
{
    GWeakRef *ref = g_object_get_data (G_OBJECT (button), "dakkar-job-weak");
    NemoProgressInfo *info = ref != NULL ? g_weak_ref_get (ref) : NULL;
    gboolean waiting;
    if (info == NULL) return;
    if (!nemo_progress_info_get_is_finished (info) &&
        (nemo_progress_info_get_operation (info) == NEMO_TRANSFER_OP_COPY ||
         nemo_progress_info_get_operation (info) == NEMO_TRANSFER_OP_MOVE)) {
        if (nemo_progress_info_hold_requested (info, &waiting))
            nemo_progress_info_release_hold (info);
        else
            nemo_progress_info_request_hold (info);
    }
    g_object_unref (info);
    /* Hold/release does not promise a progress-changed signal immediately. */
    nemo_progress_ui_handler_refresh_global ();
    (void)user_data;
}

static void
dakkar_slot_transfer_hold_weak_free (gpointer user_data)
{
    GWeakRef *ref = user_data;
    g_weak_ref_clear (ref);
    g_free (ref);
}

void
nemo_window_slot_set_transfer_job (NemoWindowSlot *slot, NemoProgressInfo *info)
{
    GWeakRef *ref;
    gboolean waiting = FALSE;
    gboolean held;
    gboolean supported;
    g_return_if_fail (NEMO_IS_WINDOW_SLOT (slot));
    if (slot->dakkar_transfer_pause == NULL) return;
    ref = g_object_get_data (G_OBJECT (slot->dakkar_transfer_pause), "dakkar-job-weak");
    if (ref == NULL) {
        ref = g_new0 (GWeakRef, 1);
        g_weak_ref_init (ref, NULL);
        g_object_set_data_full (G_OBJECT (slot->dakkar_transfer_pause),
                                "dakkar-job-weak", ref, dakkar_slot_transfer_hold_weak_free);
    }
    supported = info != NULL && !nemo_progress_info_get_is_finished (info) &&
        nemo_progress_info_get_is_started (info) &&
        (nemo_progress_info_get_operation (info) == NEMO_TRANSFER_OP_COPY ||
         nemo_progress_info_get_operation (info) == NEMO_TRANSFER_OP_MOVE);
    g_weak_ref_set (ref, supported ? G_OBJECT (info) : NULL);
    if (!supported) {
        gtk_widget_hide (slot->dakkar_transfer_pause);
        return;
    }
    held = nemo_progress_info_hold_requested (info, &waiting);
    gtk_button_set_image (GTK_BUTTON (slot->dakkar_transfer_pause),
                          gtk_image_new_from_icon_name (held ? "media-playback-start-symbolic" :
                                                        "media-playback-pause-symbolic",
                                                        GTK_ICON_SIZE_MENU));
    gtk_widget_set_tooltip_text (slot->dakkar_transfer_pause,
        held ? _("Resume transfer") : _("Pause transfer"));
    gtk_widget_show (slot->dakkar_transfer_pause);
}

static void
dakkar_slot_transfer_tab_indicator (NemoWindowSlot *slot, const gchar *label)
{
    GtkWidget *notebook = gtk_widget_get_ancestor (GTK_WIDGET (slot), GTK_TYPE_NOTEBOOK);
    GtkWidget *tab_label, *indicator;
    if (notebook == NULL) return;
    tab_label = gtk_notebook_get_tab_label (GTK_NOTEBOOK (notebook), GTK_WIDGET (slot));
    if (tab_label == NULL) return;
    indicator = g_object_get_data (G_OBJECT (tab_label), "dakkar-transfer-indicator");
    if (indicator == NULL) return;
    if (label != NULL) {
        gtk_widget_set_tooltip_text (indicator, label);
        gtk_widget_show (indicator);
    } else {
        gtk_widget_hide (indicator);
    }
}

/* A completion message does not represent an active transfer. It uses the
 * same destination-tab footer briefly, and must never clear a newer job. */
static gboolean
dakkar_slot_completion_timeout_cb (gpointer user_data)
{
    NemoWindowSlot *slot = NEMO_WINDOW_SLOT (user_data);
    slot->dakkar_transfer_complete_timeout_id = 0;
    nemo_window_slot_set_transfer (slot, NULL, 0.0, NULL);
    return G_SOURCE_REMOVE;
}

void
nemo_window_slot_show_transfer_completed (NemoWindowSlot *slot)
{
    g_return_if_fail (NEMO_IS_WINDOW_SLOT (slot));
    if (slot->dakkar_transfer_button == NULL) return;
    if (slot->dakkar_transfer_complete_timeout_id != 0) {
        g_source_remove (slot->dakkar_transfer_complete_timeout_id);
        slot->dakkar_transfer_complete_timeout_id = 0;
    }
    nemo_window_slot_set_transfer (slot, _("Transfer completed"), 1.0, NULL);
    nemo_window_slot_set_transfer_job (slot, NULL);
    /* Destroyed tabs remove this source in dispose; no dangling GTK pointer. */
    slot->dakkar_transfer_complete_timeout_id = g_timeout_add_seconds (
        5, dakkar_slot_completion_timeout_cb, slot);
}

void
nemo_window_slot_set_transfer (NemoWindowSlot *slot, const gchar *label,
                                gdouble progress, GObject *handler)
{
    g_return_if_fail (NEMO_IS_WINDOW_SLOT (slot));
    if (slot->dakkar_transfer_button == NULL) return;
    /* New active progress cancels the previous completion banner. Idle
     * refreshes must not erase a banner until its own timeout expires. */
    if (label == NULL && slot->dakkar_transfer_complete_timeout_id != 0)
        return;
    if (label != NULL && slot->dakkar_transfer_complete_timeout_id != 0) {
        g_source_remove (slot->dakkar_transfer_complete_timeout_id);
        slot->dakkar_transfer_complete_timeout_id = 0;
    }
    dakkar_slot_transfer_tab_indicator (slot, label);
    /* Disconnect even when the bar becomes idle. Weak connections prevent
     * stale handler calls when an old transfer manager is replaced. */
    g_signal_handlers_disconnect_matched (slot->dakkar_transfer_button,
        G_SIGNAL_MATCH_FUNC, 0, 0, NULL,
        G_CALLBACK (dakkar_slot_transfer_clicked), NULL);
    if (label == NULL) {
        gtk_widget_hide (slot->dakkar_transfer_button);
        nemo_window_slot_set_transfer_job (slot, NULL);
        return;
    }
    gtk_label_set_text (GTK_LABEL (slot->dakkar_transfer_label), label);
    /* A completion banner is a status, not a dead clickable control. */
    gtk_widget_set_sensitive (slot->dakkar_transfer_button, handler != NULL);
    gtk_widget_set_tooltip_text (slot->dakkar_transfer_button, label);
    if (progress < 0.0) {
        gtk_progress_bar_pulse (GTK_PROGRESS_BAR (slot->dakkar_transfer_progress));
    } else {
        gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (slot->dakkar_transfer_progress),
                                       CLAMP (progress, 0.0, 1.0));
    }
    if (handler != NULL)
        g_signal_connect_object (slot->dakkar_transfer_button, "clicked",
                                 G_CALLBACK (dakkar_slot_transfer_clicked), handler, 0);
    gtk_widget_show (slot->dakkar_transfer_button);
}

#include "nemo-slot-quick-actions.inc"
#include <libnemo-private/nemo-icon-names.h>

enum {
	ACTIVE,
	INACTIVE,
	CHANGED_PANE,
	LOCATION_CHANGED,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

static void
sync_search_directory (NemoWindowSlot *slot)
{
	NemoDirectory *directory;
	NemoQuery *query;

	g_assert (NEMO_IS_FILE (slot->viewed_file));

	directory = nemo_directory_get_for_file (slot->viewed_file);
	g_assert (NEMO_IS_SEARCH_DIRECTORY (directory));

	query = nemo_query_editor_get_query (slot->query_editor);

    if (query) {
        nemo_query_set_show_hidden (query,
                                    g_settings_get_boolean (nemo_preferences,
                                                            NEMO_PREFERENCES_SHOW_HIDDEN_FILES));
    }

	nemo_search_directory_set_query (NEMO_SEARCH_DIRECTORY (directory),
					     query);

    g_clear_object (&query);

	nemo_window_slot_force_reload (slot);

	nemo_directory_unref (directory);
}

static void
sync_search_location_cb (NemoWindow *window,
			 GError *error,
			 gpointer user_data)
{
	NemoWindowSlot *slot = user_data;

	sync_search_directory (slot);
}

static void
create_new_search (NemoWindowSlot *slot)
{
	char *uri;
	NemoDirectory *directory;
	GFile *location;

	uri = nemo_search_directory_generate_new_uri ();
	location = g_file_new_for_uri (uri);

	directory = nemo_directory_get (location);
	g_assert (NEMO_IS_SEARCH_DIRECTORY (directory));

	nemo_window_slot_open_location_full (slot, location, NEMO_WINDOW_OPEN_FLAG_SEARCH, NULL, sync_search_location_cb, slot);

	nemo_directory_unref (directory);
	g_object_unref (location);
	g_free (uri);
}

static void
query_editor_cancel_callback (NemoQueryEditor *editor,
			      NemoWindowSlot *slot)
{
	GtkAction *search;

	search = gtk_action_group_get_action (slot->pane->toolbar_action_group,
					      NEMO_ACTION_SEARCH);

	gtk_toggle_action_set_active (GTK_TOGGLE_ACTION (search), FALSE);
}

static void
query_editor_changed_callback (NemoQueryEditor *editor,
			       NemoQuery *query,
			       gboolean reload,
			       NemoWindowSlot *slot)
{
	NemoDirectory *directory;

	g_assert (NEMO_IS_FILE (slot->viewed_file));

    gtk_widget_hide (slot->no_search_results_box);
	directory = nemo_directory_get_for_file (slot->viewed_file);
	if (!NEMO_IS_SEARCH_DIRECTORY (directory)) {
		/* this is the first change from the query editor. we
		   ask for a location change to the search directory,
		   indicate the directory needs to be sync'd with the
		   current query. */
		create_new_search (slot);
		/* Focus is now on the new slot, move it back to query_editor */
		gtk_widget_grab_focus (GTK_WIDGET (slot->query_editor));
	} else {
		sync_search_directory (slot);
	}

	nemo_directory_unref (directory);
}

static void
update_query_editor (NemoWindowSlot *slot)
{
	NemoDirectory *directory;
	NemoSearchDirectory *search_directory;

	directory = nemo_directory_get (slot->location);

	if (NEMO_IS_SEARCH_DIRECTORY (directory)) {
		NemoQuery *query;
		search_directory = NEMO_SEARCH_DIRECTORY (directory);
		query = nemo_search_directory_get_query (search_directory);
		if (query != NULL) {
			nemo_query_editor_set_query (slot->query_editor,
							 query);
			g_object_unref (query);
		}
	} else {
		nemo_query_editor_set_location (slot->query_editor, slot->location);
	}

	nemo_directory_unref (directory);
}

static void
ensure_query_editor (NemoWindowSlot *slot)
{
	g_assert (slot->query_editor != NULL);

	update_query_editor (slot);

    nemo_query_editor_set_active (NEMO_QUERY_EDITOR (slot->query_editor),
                                  nemo_window_slot_get_location_uri (slot),
                                  TRUE);

	gtk_widget_grab_focus (GTK_WIDGET (slot->query_editor));
}

/* Advanced search is a mode of the LOWER Details pane, not a second
 * overlay above the file area and not a replacement for the preview. One
 * NemoQueryEditor owns the filters; reparent its controls to the active view. */
static void
dakkar_advanced_reparent (GtkWidget *child, GtkWidget *destination)
{
    GtkWidget *parent;
    if (!GTK_IS_WIDGET (child) || !GTK_IS_BOX (destination)) return;
    parent = gtk_widget_get_parent (child);
    if (parent == destination) return;
    g_object_ref (child);
    if (parent != NULL) gtk_container_remove (GTK_CONTAINER (parent), child);
    gtk_box_pack_start (GTK_BOX (destination), child, FALSE, FALSE, 0);
    g_object_unref (child);
    gtk_widget_show (child);
}

void
nemo_window_slot_set_advanced_search (NemoWindowSlot *slot, gboolean active)
{
    GtkWidget *page = g_object_get_data (G_OBJECT (slot), "dakkar-advanced-page");
    GtkWidget *classic_host = g_object_get_data (G_OBJECT (slot), "dakkar-advanced-classic-host");
    GtkWidget *stack = g_object_get_data (G_OBJECT (slot), "dakkar-inspector-tools-stack");
    /* Every view (Miller included) uses the same details sidebar. */
    if (page == NULL || classic_host == NULL) return;
    if (active && !nemo_query_editor_get_active (slot->query_editor)) return;
    dakkar_advanced_reparent (page, classic_host);
    if (GTK_IS_STACK (stack))
        gtk_stack_set_visible_child_name (GTK_STACK (stack),
                                          active ? "advanced" : "actions");
    g_object_set_data (G_OBJECT (slot), "dakkar-advanced-mode",
                       GINT_TO_POINTER (active));
}

static void
dakkar_advanced_open_clicked (GtkButton *button, NemoWindowSlot *slot)
{
    nemo_window_slot_set_advanced_search (slot, TRUE);
    (void)button;
}

static void
dakkar_advanced_back_clicked (GtkButton *button, NemoWindowSlot *slot)
{
    nemo_window_slot_set_advanced_search (slot, FALSE);
    (void)button;
}

void
nemo_window_slot_set_query_editor_visible (NemoWindowSlot *slot,
					       gboolean            visible)
{
    gtk_widget_hide (slot->no_search_results_box);
    GtkWidget *open_button = g_object_get_data (G_OBJECT (slot), "dakkar-advanced-open");
    if (!visible) nemo_window_slot_set_advanced_search (slot, FALSE);
    if (open_button != NULL)
        gtk_widget_set_visible (open_button, visible);

	if (visible) {
		ensure_query_editor (slot);

		if (slot->qe_changed_id == 0)
			slot->qe_changed_id = g_signal_connect (slot->query_editor, "changed",
								G_CALLBACK (query_editor_changed_callback), slot);
		if (slot->qe_cancel_id == 0)
			slot->qe_cancel_id = g_signal_connect (slot->query_editor, "cancel",
							       G_CALLBACK (query_editor_cancel_callback), slot);

	} else {
        nemo_query_editor_set_active (NEMO_QUERY_EDITOR (slot->query_editor), NULL, FALSE);

        if (slot->qe_changed_id > 0) {
            g_signal_handler_disconnect (slot->query_editor, slot->qe_changed_id);
            slot->qe_changed_id = 0;
        }

        if (slot->qe_cancel_id > 0) {
            g_signal_handler_disconnect (slot->query_editor, slot->qe_cancel_id);
            slot->qe_cancel_id = 0;
        }

        nemo_query_editor_set_query (slot->query_editor, NULL);
	}
}

static void
real_active (NemoWindowSlot *slot)
{
	NemoWindow *window;
	NemoWindowPane *pane;
	int page_num;

	window = nemo_window_slot_get_window (slot);
	pane = slot->pane;
	page_num = gtk_notebook_page_num (GTK_NOTEBOOK (pane->notebook),
					  GTK_WIDGET (slot));
	g_assert (page_num >= 0);

	gtk_notebook_set_current_page (GTK_NOTEBOOK (pane->notebook), page_num);

	/* sync window to new slot */
	nemo_window_push_status (window, slot->status_text);
	nemo_window_sync_allow_stop (window, slot);
	nemo_window_sync_title (window, slot);
	nemo_window_sync_zoom_widgets (window);
    nemo_window_sync_bookmark_action (window);
	nemo_window_pane_sync_location_widgets (slot->pane);
	nemo_window_pane_sync_search_widgets (slot->pane);
	nemo_window_sync_thumbnail_action(window);

	if (slot->viewed_file != NULL) {
		nemo_window_sync_view_type (window);
		nemo_window_load_extension_menus (window);
	}
}

static void
real_inactive (NemoWindowSlot *slot)
{
	NemoWindow *window;

	window = nemo_window_slot_get_window (slot);
	g_assert (slot == nemo_window_get_active_slot (window));
}

static void
floating_bar_action_cb (NemoFloatingBar *floating_bar,
			gint action,
			NemoWindowSlot *slot)
{
	if (action == NEMO_FLOATING_BAR_ACTION_ID_STOP) {
		nemo_window_slot_stop_loading (slot);
	}
}

static GtkWidget *
create_nsr_box (void)
{
    GtkWidget *box;
    GtkWidget *widget;
    PangoAttrList *attrs;

    box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 10);

    widget = gtk_image_new_from_icon_name ("system-search-symbolic", GTK_ICON_SIZE_DIALOG);
    gtk_box_pack_start (GTK_BOX (box), widget, FALSE, FALSE, 0);

    widget = gtk_label_new (_("No files found"));
    attrs = pango_attr_list_new ();
    pango_attr_list_insert (attrs, pango_attr_size_new (20 * PANGO_SCALE));
    gtk_label_set_attributes (GTK_LABEL (widget), attrs);
    pango_attr_list_unref (attrs);
    gtk_box_pack_start (GTK_BOX (box), widget, FALSE, FALSE, 0);

    gtk_widget_set_halign (box, GTK_ALIGN_CENTER);
    gtk_widget_set_valign (box, GTK_ALIGN_CENTER);

    gtk_widget_show_all (box);
    gtk_widget_set_no_show_all (box, TRUE);
    gtk_widget_hide (box);
    return box;
}

/* Group-by heading strip above the view; hidden until a group mode is set. */
static void
slot_group_bar_init (NemoWindowSlot *slot)
{
    slot->dakkar_group_mode = g_settings_get_string (nemo_preferences, "dakkar-group-by");
    slot->dakkar_group_collapsed = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                         g_free, NULL);
    slot->dakkar_group_bar = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_widget_set_margin_start (slot->dakkar_group_bar, 8);
    gtk_widget_set_margin_end (slot->dakkar_group_bar, 8);
    gtk_widget_set_margin_top (slot->dakkar_group_bar, 3);
    gtk_widget_set_margin_bottom (slot->dakkar_group_bar, 3);
    gtk_style_context_add_class (gtk_widget_get_style_context (slot->dakkar_group_bar),
                                 "dakkar-group-headings");
    slot->dakkar_group_strip = gtk_scrolled_window_new (NULL, NULL);
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (slot->dakkar_group_strip),
                                    GTK_POLICY_AUTOMATIC, GTK_POLICY_NEVER);
    gtk_widget_set_size_request (slot->dakkar_group_strip, -1, 36);
    gtk_container_add (GTK_CONTAINER (slot->dakkar_group_strip), slot->dakkar_group_bar);
    gtk_widget_show (slot->dakkar_group_bar);
    gtk_box_pack_start (GTK_BOX (slot), slot->dakkar_group_strip, FALSE, FALSE, 0);
    gtk_widget_set_no_show_all (slot->dakkar_group_strip, TRUE);
    gtk_widget_hide (slot->dakkar_group_strip);
    g_signal_connect_object (nemo_preferences, "changed::dakkar-group-by",
                             G_CALLBACK (dakkar_slot_group_mode_changed), slot, 0);
}

/* Per-destination-tab transfer footer. It takes no height while idle and
 * stays with the tab while the other pane navigates. */
static void
slot_transfer_footer_init (NemoWindowSlot *slot)
{
    slot->dakkar_transfer_button = gtk_button_new ();
    GtkWidget *transfer_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
    slot->dakkar_transfer_label = gtk_label_new (NULL);
    slot->dakkar_transfer_progress = gtk_progress_bar_new ();
    gtk_label_set_ellipsize (GTK_LABEL (slot->dakkar_transfer_label),
                             PANGO_ELLIPSIZE_END);
    gtk_label_set_max_width_chars (GTK_LABEL (slot->dakkar_transfer_label), 55);
    gtk_widget_set_size_request (slot->dakkar_transfer_progress, 65, -1);
    gtk_widget_set_hexpand (slot->dakkar_transfer_label, TRUE);
    gtk_label_set_xalign (GTK_LABEL (slot->dakkar_transfer_label), 0.0);
    gtk_box_pack_start (GTK_BOX (transfer_box), slot->dakkar_transfer_label,
                        TRUE, TRUE, 0);
    gtk_box_pack_start (GTK_BOX (transfer_box), slot->dakkar_transfer_progress,
                        FALSE, FALSE, 0);
    gtk_container_add (GTK_CONTAINER (slot->dakkar_transfer_button), transfer_box);
    gtk_style_context_add_class (
        gtk_widget_get_style_context (slot->dakkar_transfer_button),
        "dakkar-destination-transfer");
    gtk_widget_set_tooltip_text (slot->dakkar_transfer_button,
                                 _("Open the file transfer manager"));
    /* A separate sibling control avoids nesting a button inside a button. */
    slot->dakkar_transfer_pause = gtk_button_new_from_icon_name (
        "media-playback-pause-symbolic", GTK_ICON_SIZE_MENU);
    gtk_button_set_relief (GTK_BUTTON (slot->dakkar_transfer_pause), GTK_RELIEF_NONE);
    gtk_widget_set_tooltip_text (slot->dakkar_transfer_pause,
                                 _("Pause transfer"));
    g_signal_connect (slot->dakkar_transfer_pause, "clicked",
                      G_CALLBACK (dakkar_slot_transfer_hold_clicked), NULL);
    gtk_box_pack_end (GTK_BOX (slot), slot->dakkar_transfer_pause, FALSE, FALSE, 0);
    gtk_widget_set_no_show_all (slot->dakkar_transfer_pause, TRUE);
    gtk_widget_hide (slot->dakkar_transfer_pause);
    gtk_box_pack_end (GTK_BOX (slot), slot->dakkar_transfer_button, FALSE, FALSE, 0);
    gtk_widget_show_all (transfer_box);
    gtk_widget_set_no_show_all (slot->dakkar_transfer_button, TRUE);
    gtk_widget_hide (slot->dakkar_transfer_button);
}

/* Preview, name, type, media grid and the metadata labels of the details
 * panel, inside @scroller. */
static void
slot_inspector_details_init (NemoWindowSlot *slot, GtkWidget *scroller)
{
    GtkWidget *inspector_body;

    inspector_body = gtk_box_new (GTK_ORIENTATION_VERTICAL, 7);
    /* Match Miller's 8px metadata inset, not a 15px all-round border
     * that consumes most of a 92px-wide inspector. */
    gtk_container_set_border_width (GTK_CONTAINER (inspector_body), 0);
    gtk_widget_set_margin_start (inspector_body, 8);
    gtk_widget_set_margin_end (inspector_body, 8);
    gtk_widget_set_margin_top (inspector_body, 18);
    gtk_widget_set_halign (inspector_body, GTK_ALIGN_FILL);
    gtk_container_add (GTK_CONTAINER (scroller), inspector_body);
    /* Width constrained by the shared shrinkable-scroller helper. */
    gtk_widget_set_size_request (inspector_body, 0, -1);
    slot->inspector_metadata_box = inspector_body;
    gtk_style_context_add_class (gtk_widget_get_style_context (inspector_body),
                                 "miller-preview-contents");
    slot->inspector_image = gtk_image_new ();
    gtk_widget_set_halign (slot->inspector_image, GTK_ALIGN_CENTER);
    gtk_box_pack_start (GTK_BOX (inspector_body), slot->inspector_image,
                        FALSE, FALSE, 2);
    g_object_set_data (G_OBJECT (slot->inspector_image), "dakkar-slot", slot);
    slot->inspector_video = dakkar_video_new (240);
    gtk_widget_set_halign (slot->inspector_video, GTK_ALIGN_CENTER);
    gtk_box_pack_start (GTK_BOX (inspector_body), slot->inspector_video,
                        FALSE, FALSE, 2);
    gtk_widget_set_no_show_all (slot->inspector_video, TRUE);
    {
        GtkWidget *audio = dakkar_player_new (DAKKAR_PLAYER_AUDIO, 200);
        gtk_widget_set_halign (audio, GTK_ALIGN_FILL);
        gtk_box_pack_start (GTK_BOX (inspector_body), audio, FALSE, FALSE, 2);
        gtk_widget_set_no_show_all (audio, TRUE);
        gtk_widget_hide (audio);
        g_object_set_data (G_OBJECT (slot), "dakkar-inspector-audio", audio);
        g_signal_connect_swapped (audio, "destroy", G_CALLBACK (dakkar_slot_forget_audio), slot);
    }
    slot->inspector_name = files_slot_inspector_label (inspector_body,
                                                       "miller-preview-title", FALSE);
    slot->inspector_type = files_slot_inspector_label (inspector_body,
                                                       "miller-preview-meta", FALSE);
    {
        GtkWidget *grid = dakkar_media_grid_new ();
        gtk_box_pack_start (GTK_BOX (inspector_body), grid, FALSE, FALSE, 2);
        g_object_set_data (G_OBJECT (slot), "dakkar-inspector-media-grid", grid);
        g_signal_connect_swapped (grid, "destroy", G_CALLBACK (dakkar_slot_forget_grid), slot);
    }
    slot->quick_actions_folder_size = files_slot_inspector_label (
        inspector_body, "miller-preview-meta", FALSE);
    slot->inspector_modified = files_slot_inspector_label (inspector_body,
                                                            "miller-preview-meta", FALSE);
    slot->inspector_path = files_slot_inspector_label (inspector_body,
                                                        "miller-preview-path", TRUE);
    slot->inspector_owner = files_slot_inspector_label (inspector_body,
                                                         "miller-preview-meta", FALSE);
}

/* Advanced Search page of the tools stack: a back row plus the query
 * editor's filter expander, opened. */
static GtkWidget *
slot_advanced_page_new (NemoWindowSlot *slot, GtkWidget **host_out, GtkWidget **page_out)
{
    GtkWidget *advanced_scroller = gtk_scrolled_window_new (NULL, NULL);
    GtkWidget *advanced_host = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
    GtkWidget *advanced_page = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
    GtkWidget *back_button = gtk_button_new_from_icon_name ("go-previous-symbolic", GTK_ICON_SIZE_BUTTON);
    GtkWidget *back_row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 7);
    GtkWidget *back_label = gtk_label_new (_("Quick Actions"));
    GtkWidget *advanced_filters = g_object_get_data (G_OBJECT (slot->query_editor),
                                                    "dakkar-advanced-filters");
    gtk_button_set_relief (GTK_BUTTON (back_button), GTK_RELIEF_NONE);
    gtk_widget_set_tooltip_text (back_button, _("Back to Quick Actions"));
    gtk_box_pack_start (GTK_BOX (back_row), back_button, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (back_row), back_label, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (advanced_page), back_row, FALSE, FALSE, 5);
    if (advanced_filters != NULL) {
        if (GTK_IS_EXPANDER (advanced_filters))
            gtk_expander_set_expanded (GTK_EXPANDER (advanced_filters), TRUE);
        gtk_box_pack_start (GTK_BOX (advanced_page), advanced_filters, FALSE, FALSE, 0);
    }
    gtk_box_pack_start (GTK_BOX (advanced_host), advanced_page, FALSE, FALSE, 0);
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (advanced_scroller),
                                    GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    dakkar_details_make_scroller_shrinkable (advanced_scroller);
    gtk_container_add (GTK_CONTAINER (advanced_scroller), advanced_host);
    g_signal_connect (back_button, "clicked", G_CALLBACK (dakkar_advanced_back_clicked), slot);
    *host_out = advanced_host;
    *page_out = advanced_page;
    return advanced_scroller;
}

/* Lower half of the details panel: a stack with the Quick Actions list
 * and the Advanced Search page. Returns the Advanced Search button. */
static GtkWidget *
slot_inspector_tools_init (NemoWindowSlot *slot)
{
    GtkWidget *actions_scroller, *inspector_header, *advanced_host, *advanced_page;
    GtkWidget *advanced_scroller;

    slot->quick_actions_section = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_halign (slot->quick_actions_section, GTK_ALIGN_FILL);
    gtk_widget_set_size_request (slot->quick_actions_section, 0, -1);
    gtk_widget_set_margin_top (slot->quick_actions_section, 16);
    actions_scroller = gtk_scrolled_window_new (NULL, NULL);
    dakkar_details_make_scroller_shrinkable (actions_scroller);
    GtkWidget *actions_root = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_size_request (actions_root, 0, -1);
    gtk_widget_set_halign (actions_root, GTK_ALIGN_FILL);
    gtk_container_add (GTK_CONTAINER (actions_scroller), actions_root);
    gtk_box_pack_start (GTK_BOX (actions_root), slot->quick_actions_section,
                        FALSE, FALSE, 0);
    GtkWidget *tool_stack = gtk_stack_new ();
    /* The hidden search editor must not fix the natural width of the
     * visible Quick Actions. GtkStack is homogeneous by default. */
    gtk_stack_set_hhomogeneous (GTK_STACK (tool_stack), FALSE);
    gtk_stack_set_vhomogeneous (GTK_STACK (tool_stack), FALSE);
    gtk_widget_set_hexpand (tool_stack, TRUE);
    gtk_widget_set_halign (tool_stack, GTK_ALIGN_FILL);
    advanced_scroller = slot_advanced_page_new (slot, &advanced_host, &advanced_page);
    gtk_stack_add_named (GTK_STACK (tool_stack), actions_scroller, "actions");
    gtk_stack_add_named (GTK_STACK (tool_stack), advanced_scroller, "advanced");
    gtk_stack_set_visible_child_name (GTK_STACK (tool_stack), "actions");
    gtk_paned_pack2 (GTK_PANED (slot->inspector_vertical_paned),
                     tool_stack, TRUE, FALSE);
    g_object_set_data (G_OBJECT (slot), "dakkar-inspector-tools-stack", tool_stack);
    g_object_set_data (G_OBJECT (slot), "dakkar-advanced-classic-host", advanced_host);
    /* Keep the panel alive while a Miller view is destroyed/replaced. */
    g_object_set_data_full (G_OBJECT (slot), "dakkar-advanced-page",
                            g_object_ref (advanced_page), (GDestroyNotify)g_object_unref);
    inspector_header = gtk_label_new (_("Quick Actions"));
    gtk_label_set_xalign (GTK_LABEL (inspector_header), 0.5);
    gtk_widget_set_halign (inspector_header, GTK_ALIGN_CENTER);
    gtk_style_context_add_class (
        gtk_widget_get_style_context (inspector_header),
        "dakkar-inspector-qa-title");
    gtk_box_pack_start (GTK_BOX (slot->quick_actions_section), inspector_header,
                        FALSE, FALSE, 0);
    GtkWidget *advanced_open = gtk_button_new_with_label (_("Advanced Search…"));
    gtk_widget_set_tooltip_text (advanced_open, _("Open search filters in this panel"));
    gtk_style_context_add_class (gtk_widget_get_style_context (advanced_open),
                                 "dakkar-surface-button");
    /* Accessible even when Quick Actions themselves are disabled. */
    gtk_box_pack_start (GTK_BOX (actions_root), advanced_open,
                        FALSE, FALSE, 0);
    gtk_box_reorder_child (GTK_BOX (actions_root), advanced_open, 0);
    g_object_set_data (G_OBJECT (slot), "dakkar-advanced-open", advanced_open);
    g_signal_connect (advanced_open, "clicked", G_CALLBACK (dakkar_advanced_open_clicked), slot);
    slot->quick_actions_list = gtk_box_new (GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_size_request (slot->quick_actions_list, 0, -1);
    gtk_widget_set_halign (slot->quick_actions_list, GTK_ALIGN_FILL);
    gtk_box_pack_start (GTK_BOX (slot->quick_actions_section),
                        slot->quick_actions_list, FALSE, FALSE, 0);
    return advanced_open;
}

/* Details panel to the right of the view: details on top, tools below,
 * split by a draggable divider, with the saved width. */
static void
slot_inspector_init (NemoWindowSlot *slot, GtkWidget *content_row)
{
    GtkWidget *inspector_scroller, *advanced_open;

    /* Same information hierarchy as the Miller inspector, without stacking
     * another clickable overlay on top of the file view. One per tab. */
    slot->quick_actions_panel = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    /* Same compact minimum as Miller; content is allowed to reflow. */
    gtk_widget_set_size_request (slot->quick_actions_panel, 92, -1);
    gtk_style_context_add_class (
        gtk_widget_get_style_context (slot->quick_actions_panel),
        "files-general-quick-actions");
    gtk_style_context_add_class (
        gtk_widget_get_style_context (slot->quick_actions_panel),
        "files-corner-preview");
    gtk_paned_pack2 (GTK_PANED (content_row), slot->quick_actions_panel,
                     FALSE, TRUE); /* allow the details to shrink to 92px */
    slot->inspector_width = dakkar_inspector_saved_int (
        "dakkar-inspector-width", 92, 650);
    g_signal_connect (content_row, "size-allocate",
                      G_CALLBACK (files_slot_inspector_resize_allocation), slot);
    g_signal_connect (slot->quick_actions_panel, "size-allocate",
                      G_CALLBACK (files_slot_inspector_responsive), slot);
    g_signal_connect (content_row, "notify::position",
                      G_CALLBACK (files_slot_inspector_resize_notify), slot);
    g_signal_connect (content_row, "button-release-event",
                      G_CALLBACK (files_slot_inspector_resize_release), slot);
    slot->inspector_vertical_paned = gtk_paned_new (GTK_ORIENTATION_VERTICAL);
    gtk_style_context_add_class (
        gtk_widget_get_style_context (slot->inspector_vertical_paned),
        "dakkar-preview-action-divider");
    gtk_box_pack_start (GTK_BOX (slot->quick_actions_panel),
                        slot->inspector_vertical_paned, TRUE, TRUE, 0);
    inspector_scroller = gtk_scrolled_window_new (NULL, NULL);
    g_object_set_data (G_OBJECT (slot), "dakkar-inspector-details", inspector_scroller);
    dakkar_details_make_scroller_shrinkable (inspector_scroller);
    gtk_paned_pack1 (GTK_PANED (slot->inspector_vertical_paned),
                     inspector_scroller, TRUE, FALSE);
    g_signal_connect (slot->inspector_vertical_paned, "size-allocate",
                      G_CALLBACK (files_slot_preview_allocation), slot);
    g_signal_connect (slot->inspector_vertical_paned, "button-release-event",
                      G_CALLBACK (files_slot_preview_release), slot);
    g_signal_connect (slot->inspector_vertical_paned, "notify::position",
                      G_CALLBACK (files_slot_preview_live_position), slot);
    slot_inspector_details_init (slot, inspector_scroller);
    advanced_open = slot_inspector_tools_init (slot);
    g_signal_connect (slot, "destroy",
                      G_CALLBACK (files_slot_size_destroy), slot);
    gtk_widget_show_all (slot->quick_actions_panel);
    files_slot_inspector_apply_fields (slot);
    gtk_widget_hide (advanced_open);
    gtk_widget_hide (slot->quick_actions_panel);
    g_signal_connect_object (nemo_preferences, "changed",
                             G_CALLBACK (files_slot_qa_settings_changed), slot, 0);
}

static void
nemo_window_slot_init (NemoWindowSlot *slot)
{
	GtkWidget *extras_vbox;
    GtkWidget *content_row;

	gtk_orientable_set_orientation (GTK_ORIENTABLE (slot),
					GTK_ORIENTATION_VERTICAL);
	gtk_widget_show (GTK_WIDGET (slot));

	extras_vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
	slot->extra_location_widgets = extras_vbox;
	gtk_box_pack_start (GTK_BOX (slot), extras_vbox, FALSE, FALSE, 0);
	gtk_widget_show (extras_vbox);

	slot->query_editor = NEMO_QUERY_EDITOR (nemo_query_editor_new ());

	nemo_window_slot_add_extra_location_widget (slot, GTK_WIDGET (slot->query_editor));

    slot_group_bar_init (slot);

    content_row = gtk_paned_new (GTK_ORIENTATION_HORIZONTAL);
    gtk_style_context_add_class (gtk_widget_get_style_context (content_row),
                                 "dakkar-inspector-resize");
    slot->dakkar_content_row = content_row;
    slot->dakkar_inspector_left = FALSE;
    gtk_box_pack_start (GTK_BOX (slot), content_row, TRUE, TRUE, 0);
    gtk_widget_show (content_row);

    slot_transfer_footer_init (slot);

	slot->view_overlay = gtk_overlay_new ();
	gtk_widget_add_events (slot->view_overlay,
			       GDK_ENTER_NOTIFY_MASK |
			       GDK_LEAVE_NOTIFY_MASK);
    gtk_paned_pack1 (GTK_PANED (content_row), slot->view_overlay, TRUE, FALSE);
	gtk_widget_show (slot->view_overlay);

	slot->floating_bar = nemo_floating_bar_new ("", FALSE);
	gtk_widget_set_halign (slot->floating_bar, GTK_ALIGN_END);
	gtk_widget_set_valign (slot->floating_bar, GTK_ALIGN_END);
	gtk_overlay_add_overlay (GTK_OVERLAY (slot->view_overlay),
				 slot->floating_bar);

    slot->no_search_results_box = create_nsr_box ();
    gtk_overlay_add_overlay (GTK_OVERLAY (slot->view_overlay),
                             slot->no_search_results_box);

	g_signal_connect (slot->floating_bar, "action",
			  G_CALLBACK (floating_bar_action_cb), slot);

    slot_inspector_init (slot, content_row);

    slot->cache_bar = NULL;

	slot->title = g_strdup (_("Loading..."));
}

/* Extra Pane mirrors its inspectors towards the outside edges.
 * Classic Nemo content keeps the *same* inspector/selection object; changing
 * GtkBox child order never rebuilds the view or touches any file operations. */
void
nemo_window_slot_set_inspector_side (NemoWindowSlot *slot, gboolean left)
{
    g_return_if_fail (NEMO_IS_WINDOW_SLOT (slot));
    gboolean side_changed = slot->dakkar_inspector_left != left;
    slot->dakkar_inspector_left = left;
    if (side_changed && slot->dakkar_content_row != NULL &&
        slot->quick_actions_panel != NULL) {
        GtkPaned *paned = GTK_PANED (slot->dakkar_content_row);
        GtkWidget *content = slot->view_overlay;
        GtkWidget *inspector = slot->quick_actions_panel;
        gint allocated = gtk_widget_get_allocated_width (GTK_WIDGET (paned));
        slot->inspector_repositioning = TRUE;
        g_object_ref (content);
        g_object_ref (inspector);
        gtk_container_remove (GTK_CONTAINER (paned), content);
        gtk_container_remove (GTK_CONTAINER (paned), inspector);
        if (left) {
            gtk_paned_pack1 (paned, inspector, FALSE, TRUE);
            gtk_paned_pack2 (paned, content, TRUE, FALSE);
        } else {
            gtk_paned_pack1 (paned, content, TRUE, FALSE);
            gtk_paned_pack2 (paned, inspector, FALSE, TRUE);
        }
        if (allocated > 400)
            gtk_paned_set_position (paned, left ? slot->inspector_width :
                MAX (92, allocated - slot->inspector_width));
        else
            slot->inspector_width_ready = FALSE;
        g_object_unref (content);
        g_object_unref (inspector);
        slot->inspector_repositioning = FALSE;
        GtkStyleContext *context = gtk_widget_get_style_context (inspector);
        if (left) gtk_style_context_add_class (context, "dakkar-inspector-left");
        else gtk_style_context_remove_class (context, "dakkar-inspector-left");
    }
}

static void
view_end_loading_cb (NemoView       *view,
		     		 gboolean        all_files_seen,
		     		 NemoWindowSlot *slot)
{
	/* A new location often keeps an empty selection, so no
	 * "selection-changed" is emitted: refresh the details sidebar here,
	 * otherwise it keeps showing the previous folder's preview. */
	if (view == slot->content_view)
		files_slot_qa_refresh (slot);
	if (slot->needs_reload) {
		nemo_window_slot_queue_reload (slot, FALSE);
		slot->needs_reload = FALSE;
	} else if (all_files_seen) {
        dakkar_slot_refresh_group_headings (slot);
        NemoDirectory *directory;

        directory = nemo_directory_get_for_file (slot->viewed_file);

        if (NEMO_IS_SEARCH_DIRECTORY (directory)) {
            if (!nemo_directory_is_not_empty (directory)) {
                gtk_widget_show (slot->no_search_results_box);
            } else {
                gtk_widget_hide (slot->no_search_results_box);

            }
        }

        nemo_directory_unref (directory);
    }
}

static void
nemo_window_slot_dispose (GObject *object)
{
	NemoWindowSlot *slot;
	GtkWidget *widget;

	slot = NEMO_WINDOW_SLOT (object);
    if (slot->quick_actions_size_cancel != NULL)
        g_cancellable_cancel (slot->quick_actions_size_cancel);
    g_clear_object (&slot->quick_actions_size_cancel);
    g_clear_pointer (&slot->quick_actions_size_uri, g_free);

	nemo_window_slot_clear_forward_list (slot);
	nemo_window_slot_clear_back_list (slot);
    nemo_window_slot_remove_extra_location_widgets (slot);

	if (slot->content_view) {
		widget = GTK_WIDGET (slot->content_view);
		gtk_widget_destroy (widget);
		g_object_unref (slot->content_view);
		slot->content_view = NULL;
	}

	if (slot->new_content_view) {
		widget = GTK_WIDGET (slot->new_content_view);
		gtk_widget_destroy (widget);
		g_object_unref (slot->new_content_view);
		slot->new_content_view = NULL;
	}

    if (slot->dakkar_transfer_complete_timeout_id != 0) {
        g_source_remove (slot->dakkar_transfer_complete_timeout_id);
        slot->dakkar_transfer_complete_timeout_id = 0;
    }
    g_clear_pointer (&slot->dakkar_group_collapsed, g_hash_table_unref);
    g_clear_pointer (&slot->dakkar_group_mode, g_free);

	if (slot->set_status_timeout_id != 0) {
		g_source_remove (slot->set_status_timeout_id);
		slot->set_status_timeout_id = 0;
	}

	if (slot->loading_timeout_id != 0) {
		g_source_remove (slot->loading_timeout_id);
		slot->loading_timeout_id = 0;
	}

	nemo_window_slot_set_viewed_file (slot, NULL);
	/* TODO? why do we unref here? the file is NULL.
	 * It was already here before the slot move, though */
	nemo_file_unref (slot->viewed_file);

	if (slot->location) {
		/* TODO? why do we ref here, instead of unreffing?
		 * It was already here before the slot migration, though */
		g_object_ref (slot->location);
	}

	g_list_free_full (slot->pending_selection, g_object_unref);
	slot->pending_selection = NULL;

	g_clear_object (&slot->current_location_bookmark);
	g_clear_object (&slot->last_location_bookmark);

	if (slot->find_mount_cancellable != NULL) {
		g_cancellable_cancel (slot->find_mount_cancellable);
		slot->find_mount_cancellable = NULL;
	}

	slot->pane = NULL;

	g_free (slot->title);
	slot->title = NULL;

	g_free (slot->status_text);
	slot->status_text = NULL;

	G_OBJECT_CLASS (nemo_window_slot_parent_class)->dispose (object);
}

static void
nemo_window_slot_class_init (NemoWindowSlotClass *klass)
{
	GObjectClass *oclass = G_OBJECT_CLASS (klass);

	klass->active = real_active;
	klass->inactive = real_inactive;

	oclass->dispose = nemo_window_slot_dispose;

	signals[ACTIVE] =
		g_signal_new ("active",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (NemoWindowSlotClass, active),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);

	signals[INACTIVE] =
		g_signal_new ("inactive",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (NemoWindowSlotClass, inactive),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);

	signals[CHANGED_PANE] =
		g_signal_new ("changed-pane",
			G_TYPE_FROM_CLASS (klass),
			G_SIGNAL_RUN_LAST,
			G_STRUCT_OFFSET (NemoWindowSlotClass, changed_pane),
			NULL, NULL,
			g_cclosure_marshal_VOID__VOID,
			G_TYPE_NONE, 0);

	signals[LOCATION_CHANGED] =
		g_signal_new ("location-changed",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_LAST,
			      0,
			      NULL, NULL,
			      g_cclosure_marshal_generic,
			      G_TYPE_NONE, 2,
			      G_TYPE_STRING,
			      G_TYPE_STRING);
}

GFile *
nemo_window_slot_get_location (NemoWindowSlot *slot)
{
	g_assert (slot != NULL);

	if (slot->location != NULL) {
		return g_object_ref (slot->location);
	}
	return NULL;
}

char *
nemo_window_slot_get_location_uri (NemoWindowSlot *slot)
{
	g_assert (NEMO_IS_WINDOW_SLOT (slot));

	if (slot->location) {
		return g_file_get_uri (slot->location);
	}
	return NULL;
}

void
nemo_window_slot_make_hosting_pane_active (NemoWindowSlot *slot)
{
	g_assert (NEMO_IS_WINDOW_PANE (slot->pane));

	nemo_window_set_active_slot (nemo_window_slot_get_window (slot),
					 slot);
}

NemoWindow *
nemo_window_slot_get_window (NemoWindowSlot *slot)
{
	g_assert (NEMO_IS_WINDOW_SLOT (slot));
	return slot->pane->window;
}

/* nemo_window_slot_update_title:
 *
 * Re-calculate the slot title.
 * Called when the location or view has changed.
 * @slot: The NemoWindowSlot in question.
 *
 */
void
nemo_window_slot_update_title (NemoWindowSlot *slot)
{
	NemoWindow *window;
	char *title;
	gboolean do_sync = FALSE;

	title = nemo_compute_title_for_location (slot->location);
	window = nemo_window_slot_get_window (slot);

	if (g_strcmp0 (title, slot->title) != 0) {
		do_sync = TRUE;

		g_free (slot->title);
		slot->title = title;
		title = NULL;
	}

	if (slot->title != NULL && slot->title[0] != '\0' &&
	    slot->current_location_bookmark != NULL) {
		do_sync = TRUE;
	}

	if (do_sync) {
		nemo_window_sync_title (window, slot);
	}

	if (title != NULL) {
		g_free (title);
	}
}

/* nemo_window_slot_update_icon:
 *
 * Re-calculate the slot icon
 * Called when the location or view or icon set has changed.
 * @slot: The NemoWindowSlot in question.
 */
void
nemo_window_slot_update_icon (NemoWindowSlot *slot)
{
	NemoWindow *window;

	/* Dakkar: the titlebar / task switcher always shows the Dakkar app
	 * icon (not the folder icon of the current location), so window
	 * managers that badge windows by icon show the right application. */
	window = nemo_window_slot_get_window (slot);
	if (window == NULL) {
		return;
	}
	/* Keep the bundled pixbuf installed by nemo_window_init(): resetting
	 * icon-name on every location change would discard that icon. */
	if (gtk_window_get_icon (GTK_WINDOW (window)) == NULL &&
	    g_strcmp0 (DAKKAR_APP_ICON, gtk_window_get_icon_name (GTK_WINDOW (window))) != 0) {
		gtk_window_set_icon_name (GTK_WINDOW (window), DAKKAR_APP_ICON);
	}
}

void
nemo_window_slot_set_show_thumbnails (NemoWindowSlot *slot,
                                      gboolean show_thumbnails)
{
  NemoDirectory *directory;

  directory = nemo_directory_get (slot->location);
  nemo_directory_set_show_thumbnails(directory, show_thumbnails);
  nemo_directory_unref (directory);
}

void
nemo_window_slot_set_content_view_widget (NemoWindowSlot *slot,
					      NemoView *new_view)
{
	NemoWindow *window;
	GtkWidget *widget;

	window = nemo_window_slot_get_window (slot);

	if (slot->content_view != NULL) {
        /* A search page can currently belong to Miller's lower pane. Move
         * it back to the slot BEFORE destroying the outgoing view. */
        nemo_window_slot_set_advanced_search (slot, FALSE);
		/* disconnect old view */
        g_signal_handlers_disconnect_by_func (slot->content_view, G_CALLBACK (view_end_loading_cb), slot);

		nemo_window_disconnect_content_view (window, slot->content_view);

		widget = GTK_WIDGET (slot->content_view);
		gtk_widget_destroy (widget);
		g_object_unref (slot->content_view);
		slot->content_view = NULL;
    }
    /* The outgoing view has already been destroyed. Only refresh after
     * assigning the replacement; an intermediate refresh can transiently
     * hide/show the panel and invalidate its focused buttons. */
    if (new_view == NULL)
        files_slot_qa_refresh (slot);

	if (new_view != NULL) {
		widget = GTK_WIDGET (new_view);
		gtk_container_add (GTK_CONTAINER (slot->view_overlay), widget);
		gtk_widget_show (widget);

		slot->content_view = new_view;
        dakkar_slot_attach_group_mode (slot, new_view);
        g_object_set_data (G_OBJECT (new_view), "dakkar-collapsed-groups",
                           slot->dakkar_group_collapsed);
        nemo_window_slot_set_inspector_side (slot, slot->dakkar_inspector_left);
		g_object_ref (slot->content_view);

		g_signal_connect (new_view, "end_loading", G_CALLBACK (view_end_loading_cb), slot);
        g_signal_connect_object (new_view, "selection-changed",
                                 G_CALLBACK (files_slot_qa_selection_changed), slot, 0);
        files_slot_qa_refresh (slot);

		/* connect new view */
		nemo_window_connect_content_view (window, new_view);
	}
}

void
nemo_window_slot_set_allow_stop (NemoWindowSlot *slot,
				     gboolean allow)
{
	NemoWindow *window;

	g_assert (NEMO_IS_WINDOW_SLOT (slot));

	slot->allow_stop = allow;

	window = nemo_window_slot_get_window (slot);
	nemo_window_sync_allow_stop (window, slot);
}

static void
real_slot_set_short_status (NemoWindowSlot *slot,
			    const gchar *status)
{

	gboolean show_statusbar;
	gboolean disable_chrome;

	nemo_floating_bar_cleanup_actions (NEMO_FLOATING_BAR (slot->floating_bar));
	nemo_floating_bar_set_show_spinner (NEMO_FLOATING_BAR (slot->floating_bar),
						FALSE);

	show_statusbar = g_settings_get_boolean (nemo_window_state,
						 NEMO_WINDOW_STATE_START_WITH_STATUS_BAR);

	g_object_get (nemo_window_slot_get_window (slot),
		      "disable-chrome", &disable_chrome,
		      NULL);

	if (status == NULL || show_statusbar || disable_chrome) {
		gtk_widget_hide (slot->floating_bar);
		return;
	}

	nemo_floating_bar_set_label (NEMO_FLOATING_BAR (slot->floating_bar), status);
	gtk_widget_show (slot->floating_bar);
}

typedef struct {
	gchar *status;
	NemoWindowSlot *slot;
} SetStatusData;

static void
set_status_data_free (gpointer data)
{
	SetStatusData *status_data = data;

	g_free (status_data->status);

	g_free (data);
}

static gboolean
set_status_timeout_cb (gpointer data)
{
	SetStatusData *status_data = data;

	status_data->slot->set_status_timeout_id = 0;
	real_slot_set_short_status (status_data->slot, status_data->status);

	return FALSE;
}

static void
set_floating_bar_status (NemoWindowSlot *slot,
			 const gchar *status)
{
	GtkSettings *settings;
	gint double_click_time;
	SetStatusData *status_data;

	if (slot->set_status_timeout_id != 0) {
		g_source_remove (slot->set_status_timeout_id);
		slot->set_status_timeout_id = 0;
	}

	settings = gtk_settings_get_for_screen (gtk_widget_get_screen (GTK_WIDGET (slot->content_view)));
	g_object_get (settings,
		      "gtk-double-click-time", &double_click_time,
		      NULL);

	status_data = g_new0 (SetStatusData, 1);
	status_data->status = g_strdup (status);
	status_data->slot = slot;

	/* waiting for half of the double-click-time before setting
	 * the status seems to be a good approximation of not setting it
	 * too often and not delaying the statusbar too much.
	 */
	slot->set_status_timeout_id =
		g_timeout_add_full (G_PRIORITY_DEFAULT,
				    (guint) (double_click_time / 2),
				    set_status_timeout_cb,
				    status_data,
				    set_status_data_free);
}

void
nemo_window_slot_set_status (NemoWindowSlot *slot,
                             const char *status,
                             const char *short_status,
                             gboolean    location_loading)
{
	NemoWindow *window;

	g_assert (NEMO_IS_WINDOW_SLOT (slot));

	g_free (slot->status_text);
	slot->status_text = g_strdup (status);

	if (slot->content_view != NULL && !location_loading) {
		set_floating_bar_status (slot, short_status);
	}

	window = nemo_window_slot_get_window (slot);
	if (slot == nemo_window_get_active_slot (window)) {
		nemo_window_push_status (window, slot->status_text);
	}
}

static void
remove_all_extra_location_widgets (GtkWidget *widget,
				   gpointer data)
{
	NemoWindowSlot *slot = data;
	NemoDirectory *directory;

	directory = nemo_directory_get (slot->location);
	if (widget != GTK_WIDGET (slot->query_editor)) {
		gtk_container_remove (GTK_CONTAINER (slot->extra_location_widgets), widget);
	}

	nemo_directory_unref (directory);
}

void
nemo_window_slot_remove_extra_location_widgets (NemoWindowSlot *slot)
{
    gtk_container_foreach (GTK_CONTAINER (slot->extra_location_widgets),
                           remove_all_extra_location_widgets,
                           slot);
}

void
nemo_window_slot_add_extra_location_widget (NemoWindowSlot *slot,
						GtkWidget *widget)
{
	gtk_box_pack_start (GTK_BOX (slot->extra_location_widgets),
			    widget, TRUE, TRUE, 0);
	gtk_widget_show (slot->extra_location_widgets);
}

/* returns either the pending or the actual current uri */
char *
nemo_window_slot_get_current_uri (NemoWindowSlot *slot)
{
	if (slot->pending_location != NULL) {
		return g_file_get_uri (slot->pending_location);
	}

	if (slot->location != NULL) {
		return g_file_get_uri (slot->location);
	}

	g_assert_not_reached ();
	return NULL;
}

NemoView *
nemo_window_slot_get_current_view (NemoWindowSlot *slot)
{
	if (slot->content_view != NULL) {
		return slot->content_view;
	} else if (slot->new_content_view) {
		return slot->new_content_view;
	}

	return NULL;
}

void
nemo_window_slot_go_home (NemoWindowSlot *slot,
			      NemoWindowOpenFlags flags)
{
	GFile *home;

	g_return_if_fail (NEMO_IS_WINDOW_SLOT (slot));

	home = g_file_new_for_path (g_get_home_dir ());
	nemo_window_slot_open_location (slot, home, flags);
	g_object_unref (home);
}

void
nemo_window_slot_go_up (NemoWindowSlot *slot,
			    NemoWindowOpenFlags flags)
{
	GFile *parent;
	char * uri;

	if (slot->location == NULL) {
		return;
	}

	parent = g_file_get_parent (slot->location);
	if (parent == NULL) {
		if (g_file_has_uri_scheme (slot->location, "smb")) {
			uri = g_file_get_uri (slot->location);

            DEBUG ("Starting samba URI for navigation: %s", uri);

			if (g_strcmp0 ("smb:///", uri) == 0) {
				parent = g_file_new_for_uri ("network:///");
			}
			else {
                GString *gstr;
                char * temp;

                gstr = g_string_new (uri);

				// Remove last /
                if (g_str_has_suffix (gstr->str, "/")) {
                    gstr = g_string_set_size (gstr, gstr->len - 1);
                }

				// Remove last part of string after last remaining /
				temp = g_strrstr (gstr->str, "/") + 1;
				if (temp != NULL) {
                    gstr = g_string_set_size (gstr, temp - gstr->str);
				}

                // if we're going to end up with smb://, redirect it to network instead.
                if (g_strcmp0 ("smb://", gstr->str) == 0) {
                    gstr = g_string_assign (gstr, "network:///");
                }

                uri = g_string_free (gstr, FALSE);

				parent = g_file_new_for_uri (uri);

                DEBUG ("Ending samba URI for navigation: %s", uri);
			}
			g_free (uri);
		}
		else {
			return;
		}
	}

	nemo_window_slot_open_location (slot, parent, flags);
	g_object_unref (parent);
}

void
nemo_window_slot_clear_forward_list (NemoWindowSlot *slot)
{
	g_assert (NEMO_IS_WINDOW_SLOT (slot));

	g_list_free_full (slot->forward_list, g_object_unref);
	slot->forward_list = NULL;
}

void
nemo_window_slot_clear_back_list (NemoWindowSlot *slot)
{
	g_assert (NEMO_IS_WINDOW_SLOT (slot));

	g_list_free_full (slot->back_list, g_object_unref);
	slot->back_list = NULL;
}

gboolean
nemo_window_slot_should_close_with_mount (NemoWindowSlot *slot,
					      GMount *mount)
{
	GFile *mount_location;
	gboolean close_with_mount;

	mount_location = g_mount_get_root (mount);
	close_with_mount =
		g_file_has_prefix (NEMO_WINDOW_SLOT (slot)->location, mount_location) ||
		g_file_equal (NEMO_WINDOW_SLOT (slot)->location, mount_location);

	g_object_unref (mount_location);

	return close_with_mount;
}

NemoWindowSlot *
nemo_window_slot_new (NemoWindowPane *pane)
{
	NemoWindowSlot *slot;

	slot = g_object_new (NEMO_TYPE_WINDOW_SLOT, NULL);
	slot->pane = pane;

	return slot;
}
