/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/*
 * Nemo
 *
 * Copyright (C) 2011, Red Hat, Inc.
 *
 * Nemo is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * Nemo is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Suite 500, MA 02110-1335, USA.
 *
 * Author: Cosimo Cecchi <cosimoc@redhat.com>
 *
 */

#include <config.h>

#include "nemo-toolbar.h"
#include "nemo-corner-style.h"
#include "nemo-actions.h"
#include "nemo-window-pane.h"
#include "nemo-window.h"
#include "nemo-file-management-properties.h"
#include "nemo-window-slot.h"
#include "nemo-miller-columns-view.h"
#include "nemo-icon-view.h"
#include "nemo-list-view.h"
#include "dakkar-grouping.h"
#include "nemo-transfer-window.h"

#include "nemo-location-bar.h"
#include "nemo-pathbar.h"
#include "nemo-window-private.h"
#include "nemo-actions.h"
#include "nemo-file-utilities.h"
#include <glib/gi18n.h>
#include <libnemo-private/nemo-global-preferences.h>
#include <libnemo-private/nemo-ui-utilities.h>

struct _NemoToolbarPriv {
	GtkWidget *toolbar;

	GtkActionGroup *action_group;
	GtkUIManager *ui_manager;

    GtkWidget *previous_button;
    GtkWidget *next_button;
    GtkWidget *up_button;
    GtkWidget *refresh_button;
    GtkWidget *home_button;
    GtkWidget *computer_button;
    GtkWidget *toggle_location_button;
    GtkWidget *open_terminal_button;
    GtkWidget *new_folder_button;
    GtkWidget *search_button;
    GtkWidget *icon_view_button;
    GtkWidget *list_view_button;
    GtkWidget *compact_view_button;
    GtkWidget *miller_view_button;
    GtkWidget *view_menu_button;
    GtkWidget *view_menu_image;
    GtkWidget *sort_menu_button;
    GtkWidget *group_menu_button;
    GtkWidget *group_popover;
    GtkWidget *group_rows[8];
    GtkWidget *extra_pane_button;
    GtkWidget *places_tree_button;
    GtkWidget *sort_menu_popover;
    GtkWidget *sort_menu_rows[6];
    GtkWidget *sort_reverse_button;
    GtkWidget *new_tab_button;
    GtkWidget *transfers_button;
    GtkWidget *show_thumbnails_button;
    GtkWidget *settings_tool_button;
    GtkWidget *left_box;
    GtkWidget *right_box;

	GtkWidget *path_bar;
	GtkWidget *location_bar;
    GtkWidget *root_bar;
    GtkWidget *stack;

	gboolean show_main_bar;
	gboolean show_location_entry;
    gboolean show_root_bar;
};

enum {
	PROP_ACTION_GROUP = 1,
	PROP_SHOW_LOCATION_ENTRY,
	PROP_SHOW_MAIN_BAR,
	NUM_PROPERTIES
};

static GParamSpec *properties[NUM_PROPERTIES] = { NULL, };

enum {
    CHECK_ADMIN_LOCATION,
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE (NemoToolbar, nemo_toolbar, GTK_TYPE_BOX);

static void
nemo_toolbar_update_root_state (NemoToolbar *self)
{
    gboolean is_admin_uri;

    g_signal_emit (self, signals[CHECK_ADMIN_LOCATION], 0, &is_admin_uri);

    if ((is_admin_uri ||
         (nemo_user_is_root () && !nemo_treating_root_as_normal())) &&
         g_settings_get_boolean (nemo_preferences, NEMO_PREFERENCES_SHOW_ROOT_WARNING)) {
        if (self->priv->show_root_bar != TRUE) {
            self->priv->show_root_bar = TRUE;
        }
    } else {
        self->priv->show_root_bar = FALSE;
    }
}

/* Look up the visible view in the pane that owns this toolbar.  Using the
 * window's global active slot would sort the wrong side of an extra pane. */
static NemoView *
files_toolbar_current_view (NemoToolbar *self)
{
    NemoWindowPane *pane = g_object_get_data (G_OBJECT (self), "files-owner-pane");
    if (pane == NULL || pane->active_slot == NULL)
        return NULL;
    return nemo_window_slot_get_current_view (pane->active_slot);
}

static gboolean
files_toolbar_view_supports_sort (NemoView *view)
{
    return NEMO_IS_MILLER_COLUMNS_VIEW (view) ||
           NEMO_IS_ICON_VIEW (view) ||
           NEMO_IS_LIST_VIEW (view);
}

/* The List View model may not expose a sortable column for every attribute
 * (e.g. a special directory or an extension-provided column set). Never
 * advertise such a criterion as a functioning toolbar action. */
static gboolean
files_toolbar_can_sort_by (NemoView *view, NemoFileSortType order)
{
    if (!files_toolbar_view_supports_sort (view))
        return FALSE;
    if (NEMO_IS_LIST_VIEW (view))
        return nemo_list_view_can_sort_by (NEMO_LIST_VIEW (view), order);
    /* Icons and Compact share NemoIconView; the menu only contains keys
     * present in their SortCriterion table. Desktop views are out of scope. */
    return TRUE;
}

static NemoFileSortType
files_toolbar_get_sort_order (NemoView *view)
{
    if (NEMO_IS_MILLER_COLUMNS_VIEW (view))
        return nemo_miller_columns_view_get_sort_order (NEMO_MILLER_COLUMNS_VIEW (view));
    if (NEMO_IS_ICON_VIEW (view))
        return nemo_icon_view_get_sort_order (NEMO_ICON_VIEW (view));
    if (NEMO_IS_LIST_VIEW (view))
        return nemo_list_view_get_sort_order (NEMO_LIST_VIEW (view));
    return NEMO_FILE_SORT_BY_DISPLAY_NAME;
}

static gboolean
files_toolbar_get_sort_reversed (NemoView *view)
{
    if (NEMO_IS_MILLER_COLUMNS_VIEW (view))
        return nemo_miller_columns_view_get_sort_reversed (NEMO_MILLER_COLUMNS_VIEW (view));
    if (NEMO_IS_ICON_VIEW (view))
        return nemo_icon_view_get_sort_reversed (NEMO_ICON_VIEW (view));
    if (NEMO_IS_LIST_VIEW (view))
        return nemo_list_view_get_sort_reversed (NEMO_LIST_VIEW (view));
    return FALSE;
}

static void
files_toolbar_set_sort_order (NemoView *view, NemoFileSortType order)
{
    if (NEMO_IS_MILLER_COLUMNS_VIEW (view))
        nemo_miller_columns_view_set_sort_order (NEMO_MILLER_COLUMNS_VIEW (view), order);
    else if (NEMO_IS_ICON_VIEW (view))
        nemo_icon_view_set_sort_criterion_by_sort_type (NEMO_ICON_VIEW (view), order);
    else if (NEMO_IS_LIST_VIEW (view))
        nemo_list_view_set_sort_order (NEMO_LIST_VIEW (view), order);
}

static void
files_toolbar_set_sort_reversed (NemoView *view, gboolean reversed)
{
    if (NEMO_IS_MILLER_COLUMNS_VIEW (view))
        nemo_miller_columns_view_set_sort_reversed (NEMO_MILLER_COLUMNS_VIEW (view), reversed);
    else if (NEMO_IS_ICON_VIEW (view))
        nemo_icon_view_apply_sort_reversed (NEMO_ICON_VIEW (view), reversed);
    else if (NEMO_IS_LIST_VIEW (view))
        nemo_list_view_set_sort_reversed (NEMO_LIST_VIEW (view), reversed);
}

static const struct {
    NemoFileSortType order;
    const char *label;
} files_sort_modes[] = {
    { NEMO_FILE_SORT_BY_DISPLAY_NAME, N_("Name") },
    { NEMO_FILE_SORT_BY_BTIME, N_("Creation Date") },
    { NEMO_FILE_SORT_BY_MTIME, N_("Modification Date") },
    { NEMO_FILE_SORT_BY_TYPE, N_("File Type") },
    { NEMO_FILE_SORT_BY_DETAILED_TYPE, N_("Detailed Type") },
    { NEMO_FILE_SORT_BY_SIZE, N_("Size") }
};

/* The Group By toolbar glyph is hand-drawn with GTK theme foreground;
 * unlike the list-view icon, it depicts rows gathered into categories. */
static gboolean
dakkar_group_glyph_draw (GtkWidget *widget, cairo_t *cr, gpointer data)
{
    GtkStyleContext *context = gtk_widget_get_style_context (widget);
    GdkRGBA color;
    gint row;
    gtk_style_context_get_color (context,
        gtk_style_context_get_state (context), &color);
    gdk_cairo_set_source_rgba (cr, &color);
    /* 16x16 grid, 1px margin, the same weight as symbolic icons */
    for (row = 0; row < 3; row++) {
        const gdouble top = 3.0 + row * 4.0 + (row > 0 ? 1.0 : 0.0);
        cairo_rectangle (cr, 1.0, top, 3.0, 2.0);
        cairo_rectangle (cr, 6.0, top, 9.0, 2.0);
    }
    cairo_fill (cr);
    (void)data;
    return FALSE;
}

/* Group By is a FIXED control, directly after Sort and before Settings.
 * Selection is a preference shared by the four view implementations. */
static const struct { const char *id; const char *name; } dakkar_group_options[] = {
    { "none", N_("Off") }, { "type", N_("File type") },
    { "name", N_("Name") }, { "size", N_("Size") },
    { "created", N_("Date created") }, { "modified", N_("Last modified") },
    { "owner", N_("Owner") }, { "permissions", N_("Permissions") }
};

static void
dakkar_group_refresh (GtkWidget *widget, gpointer data)
{
    NemoToolbar *self = NEMO_TOOLBAR (data);
    NemoWindowPane *pane = g_object_get_data (G_OBJECT (self), "files-owner-pane");
    const gchar *choice = (pane != NULL && pane->active_slot != NULL) ?
        nemo_window_slot_get_group_mode (pane->active_slot) : "none";
    guint i;
    for (i = 0; i < G_N_ELEMENTS (dakkar_group_options); i++) {
        gchar *label = g_strdup_printf ("%s  %s",
            g_strcmp0 (choice, dakkar_group_options[i].id) == 0 ? "✓" : "  ",
            _(dakkar_group_options[i].name));
        gtk_button_set_label (GTK_BUTTON (self->priv->group_rows[i]), label);
        g_free (label);
    }
    GtkStyleContext *style = gtk_widget_get_style_context (
        self->priv->group_menu_button);
    if (g_strcmp0 (choice, "none") == 0)
        gtk_style_context_remove_class (style, "dakkar-group-enabled");
    else
        gtk_style_context_add_class (style, "dakkar-group-enabled");
    (void)widget;
}

static void
dakkar_group_option_clicked (GtkButton *button, gpointer data)
{
    NemoToolbar *self = NEMO_TOOLBAR (data);
    const char *choice = g_object_get_data (G_OBJECT (button), "dakkar-group-choice");
    NemoWindowPane *pane = g_object_get_data (G_OBJECT (self), "files-owner-pane");
    if (choice != NULL && pane != NULL && pane->active_slot != NULL)
        nemo_window_slot_set_group_mode (pane->active_slot, choice);
    dakkar_group_refresh (self->priv->group_popover, self);
    gtk_popover_popdown (GTK_POPOVER (self->priv->group_popover));
}

static void toolbar_menu_button_set_icon (GtkWidget *menu, GtkWidget *image);

static GtkWidget *
dakkar_group_picker (NemoToolbar *self)
{
    GtkWidget *button = gtk_menu_button_new ();
    GtkWidget *popover = gtk_popover_new (button);
    GtkWidget *vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 3);
    guint i;
    self->priv->group_menu_button = button;
    self->priv->group_popover = popover;
    /* Compact icon-only Group By, with an accessible name and full menu. */
    GtkWidget *image;
    /* A 16px symbolic theme icon like every other framed button; a theme
     * without any "group"-like icon gets the hand-drawn glyph, drawn on
     * the same 16x16 grid. */
    const gchar *group_icon = dakkar_icon_pick ("view-group-symbolic",
                                               "view-list-symbolic", NULL);
    if (gtk_icon_theme_has_icon (gtk_icon_theme_get_default (), group_icon)) {
        image = gtk_image_new_from_icon_name (group_icon, GTK_ICON_SIZE_BUTTON);
    } else {
        image = gtk_drawing_area_new ();
        g_signal_connect (image, "draw", G_CALLBACK (dakkar_group_glyph_draw), NULL);
    }
    toolbar_menu_button_set_icon (button, image);
    gtk_widget_set_tooltip_text (button, _("Group By: type, date, owner or permissions"));
    atk_object_set_name (gtk_widget_get_accessible (button), _("Group By"));
    gtk_style_context_add_class (gtk_widget_get_style_context (button), "files-group-picker");
    gtk_container_set_border_width (GTK_CONTAINER (vbox), 6);
    for (i = 0; i < G_N_ELEMENTS (dakkar_group_options); i++) {
        GtkWidget *row = gtk_button_new_with_label (_(dakkar_group_options[i].name));
        self->priv->group_rows[i] = row;
        g_object_set_data (G_OBJECT (row), "dakkar-group-choice",
                           (gpointer)dakkar_group_options[i].id);
        gtk_box_pack_start (GTK_BOX (vbox), row, FALSE, FALSE, 0);
        g_signal_connect (row, "clicked", G_CALLBACK (dakkar_group_option_clicked), self);
    }
    gtk_container_add (GTK_CONTAINER (popover), vbox);
    gtk_menu_button_set_popover (GTK_MENU_BUTTON (button), popover);
    g_signal_connect (popover, "show", G_CALLBACK (dakkar_group_refresh), self);
    gtk_widget_show_all (vbox);
    return button;
}

static void
dakkar_extra_pane_tool_clicked (GtkButton *button, gpointer data)
{
    NemoToolbar *self = NEMO_TOOLBAR (data);
    NemoWindowPane *pane = g_object_get_data (G_OBJECT (self), "files-owner-pane");
    if (pane == NULL || pane->window == NULL) return;
    GtkAction *action = gtk_action_group_get_action (
        nemo_window_get_main_action_group (pane->window), NEMO_ACTION_SHOW_HIDE_EXTRA_PANE);
    if (GTK_IS_TOGGLE_ACTION (action))
        gtk_toggle_action_set_active (GTK_TOGGLE_ACTION (action),
            !gtk_toggle_action_get_active (GTK_TOGGLE_ACTION (action)));
    nemo_toolbar_update_for_location (self);
    (void)button;
}

static void
dakkar_places_tree_tool_clicked (GtkButton *button, gpointer data)
{
    NemoToolbar *self = NEMO_TOOLBAR (data);
    NemoWindowPane *pane = g_object_get_data (G_OBJECT (self), "files-owner-pane");
    if (pane == NULL || pane->window == NULL) return;
    const gchar *current = nemo_window_get_sidebar_id (pane->window);
    nemo_window_set_sidebar_id (pane->window,
        g_strcmp0 (current, NEMO_WINDOW_SIDEBAR_TREE) == 0 ?
        NEMO_WINDOW_SIDEBAR_PLACES : NEMO_WINDOW_SIDEBAR_TREE);
    nemo_toolbar_update_for_location (self);
    (void)button;
}

static void
files_toolbar_preferences_clicked (GtkButton *button, NemoToolbar *toolbar)
{
    GtkWidget *toplevel = gtk_widget_get_toplevel (GTK_WIDGET (toolbar));
    if (GTK_IS_WINDOW (toplevel))
        nemo_file_management_properties_dialog_show (GTK_WINDOW (toplevel),
                                                     "appearance");
    (void)button;
}

static void
files_toolbar_sort_refresh (GtkWidget *popover, gpointer data)
{
    NemoToolbar *self = NEMO_TOOLBAR (data);
    NemoView *view = files_toolbar_current_view (self);
    gboolean supported = files_toolbar_view_supports_sort (view);
    guint i;
    for (i = 0; i < G_N_ELEMENTS (files_sort_modes); i++) {
        GtkWidget *button = self->priv->sort_menu_rows[i];
        if (button != NULL) {
            gboolean selected = supported &&
                files_toolbar_get_sort_order (view) == files_sort_modes[i].order;
            char *title = g_strdup_printf ("%s  %s", selected ? "✓" : "  ",
                                           _(files_sort_modes[i].label));
            gboolean available = files_toolbar_can_sort_by (view, files_sort_modes[i].order);
            gtk_button_set_label (GTK_BUTTON (button), title);
            gtk_widget_set_sensitive (button, available);
            gtk_widget_set_tooltip_text (button, available ?
                _(files_sort_modes[i].label) :
                _("This view cannot sort by this property in the current folder"));
            g_free (title);
        }
    }
    if (self->priv->sort_reverse_button != NULL) {
        gboolean reversed = supported && files_toolbar_get_sort_reversed (view);
        gtk_button_set_label (GTK_BUTTON (self->priv->sort_reverse_button),
                              reversed ? _("✓  Reverse Order") : _("    Reverse Order"));
        gtk_widget_set_sensitive (self->priv->sort_reverse_button, supported);
    }
}

static void
files_toolbar_sort_clicked (GtkButton *button, gpointer data)
{
    NemoToolbar *self = NEMO_TOOLBAR (data);
    NemoView *view = files_toolbar_current_view (self);
    NemoWindowPane *pane = g_object_get_data (G_OBJECT (self), "files-owner-pane");
    if (!files_toolbar_view_supports_sort (view))
        return;
    if (GTK_WIDGET (button) != self->priv->sort_reverse_button) {
        NemoFileSortType order = GPOINTER_TO_INT (
            g_object_get_data (G_OBJECT (button), "files-sort-order"));
        if (!files_toolbar_can_sort_by (view, order)) {
            g_warning ("Files: sort criterion %d unavailable for %s",
                       (int) order, G_OBJECT_TYPE_NAME (view));
            return;
        }
    }
    /* Mirror the view picker's active-pane handling.  It prevents action
     * and directory metadata listeners acting on another split pane. */
    if (pane != NULL && pane->window != NULL)
        nemo_window_set_active_pane (pane->window, pane);
    if (GTK_WIDGET (button) == self->priv->sort_reverse_button)
        files_toolbar_set_sort_reversed (view, !files_toolbar_get_sort_reversed (view));
    else {
        NemoFileSortType order = GPOINTER_TO_INT (
            g_object_get_data (G_OBJECT (button), "files-sort-order"));
        files_toolbar_set_sort_order (view, order);
    }
    /* every view must report the requested order back; warn when it did not
     * apply instead of looking like a successful click */
    if (GTK_WIDGET (button) != self->priv->sort_reverse_button) {
        NemoFileSortType order = GPOINTER_TO_INT (
            g_object_get_data (G_OBJECT (button), "files-sort-order"));
        if (files_toolbar_get_sort_order (view) != order)
            g_warning ("Files: requested sort %d not applied in active %s view",
                       (int) order, G_OBJECT_TYPE_NAME (view));
    }
    files_toolbar_sort_refresh (self->priv->sort_menu_popover, self);
    gtk_popover_popdown (GTK_POPOVER (self->priv->sort_menu_popover));
}

static GtkWidget *
files_toolbar_sort_picker (NemoToolbar *self)
{
    GtkWidget *menu = gtk_menu_button_new ();
    GtkWidget *popover = gtk_popover_new (menu);
    GtkWidget *items = gtk_box_new (GTK_ORIENTATION_VERTICAL, 3);
    /* The generic "sort" glyph where the theme has one (Breeze), not the
     * direction-specific arrow that read as "move down". */
    GtkWidget *image = gtk_image_new_from_icon_name (
        dakkar_icon_pick ("view-sort-symbolic", "view-sort-ascending-symbolic",
                         "view-sort-descending-symbolic", NULL),
        GTK_ICON_SIZE_BUTTON);
    guint i;
    self->priv->sort_menu_button = menu;
    self->priv->sort_menu_popover = popover;
    toolbar_menu_button_set_icon (menu, image);
    gtk_widget_set_tooltip_text (menu, _("Sort Files"));
    gtk_style_context_add_class (gtk_widget_get_style_context (menu), "files-sort-picker");
    gtk_container_set_border_width (GTK_CONTAINER (items), 6);
    for (i = 0; i < G_N_ELEMENTS (files_sort_modes); i++) {
        GtkWidget *button = gtk_button_new_with_label (_(files_sort_modes[i].label));
        gtk_widget_set_halign (button, GTK_ALIGN_FILL);
        gtk_widget_set_tooltip_text (button, _(files_sort_modes[i].label));
        gtk_box_pack_start (GTK_BOX (items), button, FALSE, FALSE, 0);
        g_object_set_data (G_OBJECT (button), "files-sort-order",
                           GINT_TO_POINTER (files_sort_modes[i].order));
        g_signal_connect (button, "clicked", G_CALLBACK (files_toolbar_sort_clicked), self);
        self->priv->sort_menu_rows[i] = button;
    }
    self->priv->sort_reverse_button = gtk_button_new_with_label (_("Reverse Order"));
    gtk_box_pack_start (GTK_BOX (items), gtk_separator_new (GTK_ORIENTATION_HORIZONTAL),
                        FALSE, FALSE, 3);
    gtk_box_pack_start (GTK_BOX (items), self->priv->sort_reverse_button, FALSE, FALSE, 0);
    g_signal_connect (self->priv->sort_reverse_button, "clicked",
                      G_CALLBACK (files_toolbar_sort_clicked), self);
    gtk_container_add (GTK_CONTAINER (popover), items);
    gtk_menu_button_set_popover (GTK_MENU_BUTTON (menu), popover);
    g_signal_connect (popover, "show", G_CALLBACK (files_toolbar_sort_refresh), self);
    gtk_widget_show_all (items);
    return menu;
}

/* The view picker uses the existing Nemo radio actions: this is only a
 * different presentation, not a second view-selection implementation. */
static void
files_toolbar_sync_view_icon (NemoToolbar *self)
{
    const struct { const char *action; const char *icon; } views[] = {
        { NEMO_ACTION_ICON_VIEW, dakkar_view_icon_name (NEMO_ACTION_ICON_VIEW) },
        { NEMO_ACTION_LIST_VIEW, dakkar_view_icon_name (NEMO_ACTION_LIST_VIEW) },
        { NEMO_ACTION_COMPACT_VIEW, dakkar_view_icon_name (NEMO_ACTION_COMPACT_VIEW) },
        { NEMO_ACTION_MILLER_VIEW, dakkar_view_icon_name (NEMO_ACTION_MILLER_VIEW) }
    };
    guint i;

    if (self->priv->sort_menu_button != NULL) {
        /* The view may still be loading while the toolbar is constructed.
         * Keep the picker available; refresh its choices when opened. */
        gtk_widget_set_sensitive (self->priv->sort_menu_button, TRUE);
    }
    if (self->priv->view_menu_image == NULL)
        return;
    for (i = 0; i < G_N_ELEMENTS (views); i++) {
        GtkAction *action = gtk_action_group_get_action (self->priv->action_group,
                                                         views[i].action);
        if (GTK_IS_TOGGLE_ACTION (action) &&
            gtk_toggle_action_get_active (GTK_TOGGLE_ACTION (action))) {
            gtk_image_set_from_icon_name (GTK_IMAGE (self->priv->view_menu_image),
                                          views[i].icon, GTK_ICON_SIZE_BUTTON);
            return;
        }
    }
}

static void
files_toolbar_view_state_changed (GObject *action, GParamSpec *pspec, gpointer data)
{
    files_toolbar_sync_view_icon (NEMO_TOOLBAR (data));
}

static void
files_toolbar_select_view (GtkButton *button, gpointer data)
{
    NemoToolbar *self = NEMO_TOOLBAR (data);
    const char *name = g_object_get_data (G_OBJECT (button), "files-view-action");
    GtkAction *action = gtk_action_group_get_action (self->priv->action_group, name);
    NemoWindowPane *pane = g_object_get_data (G_OBJECT (self), "files-owner-pane");
    /* Actions are bound to the window's active slot, so activate the pane
     * that owns this picker before issuing the existing Nemo action. */
    if (pane != NULL && pane->window != NULL)
        nemo_window_set_active_pane (pane->window, pane);
    if (action != NULL)
        gtk_action_activate (action);
    gtk_popover_popdown (GTK_POPOVER (gtk_menu_button_get_popover (
                                        GTK_MENU_BUTTON (self->priv->view_menu_button))));
    files_toolbar_sync_view_icon (self);
}

static GtkWidget *
files_toolbar_view_picker (NemoToolbar *self)
{
    const struct { const char *action; const char *icon; const char *label; } views[] = {
        { NEMO_ACTION_ICON_VIEW, dakkar_view_icon_name (NEMO_ACTION_ICON_VIEW), N_("Icons") },
        { NEMO_ACTION_LIST_VIEW, dakkar_view_icon_name (NEMO_ACTION_LIST_VIEW), N_("List") },
        { NEMO_ACTION_COMPACT_VIEW, dakkar_view_icon_name (NEMO_ACTION_COMPACT_VIEW), N_("Compact") },
        { NEMO_ACTION_MILLER_VIEW, dakkar_view_icon_name (NEMO_ACTION_MILLER_VIEW), N_("Miller Columns") }
    };
    GtkWidget *menu = gtk_menu_button_new ();
    GtkWidget *popover = gtk_popover_new (menu);
    GtkWidget *items = gtk_box_new (GTK_ORIENTATION_VERTICAL, 3);
    guint i;

    self->priv->view_menu_button = menu;
    self->priv->view_menu_image = gtk_image_new_from_icon_name (
                                  dakkar_view_icon_name (NEMO_ACTION_ICON_VIEW),
                                  GTK_ICON_SIZE_BUTTON);
    toolbar_menu_button_set_icon (menu, self->priv->view_menu_image);
    gtk_widget_set_tooltip_text (menu, _("Choose view"));
    gtk_style_context_add_class (gtk_widget_get_style_context (menu), "files-view-picker");
    gtk_container_set_border_width (GTK_CONTAINER (items), 6);
    for (i = 0; i < G_N_ELEMENTS (views); i++) {
        GtkWidget *item = gtk_button_new ();
        GtkWidget *row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 10);
        GtkWidget *icon = gtk_image_new_from_icon_name (views[i].icon, GTK_ICON_SIZE_MENU);
        GtkWidget *label = gtk_label_new (_(views[i].label));
        GtkAction *action = gtk_action_group_get_action (self->priv->action_group,
                                                         views[i].action);
        gtk_widget_set_halign (label, GTK_ALIGN_START);
        gtk_box_pack_start (GTK_BOX (row), icon, FALSE, FALSE, 0);
        gtk_box_pack_start (GTK_BOX (row), label, TRUE, TRUE, 0);
        gtk_container_add (GTK_CONTAINER (item), row);
        gtk_box_pack_start (GTK_BOX (items), item, FALSE, FALSE, 0);
        g_object_set_data (G_OBJECT (item), "files-view-action", (gpointer) views[i].action);
        g_signal_connect (item, "clicked", G_CALLBACK (files_toolbar_select_view), self);
        if (action != NULL)
            g_signal_connect_object (action, "notify::active",
                                     G_CALLBACK (files_toolbar_view_state_changed), self, 0);
    }
    gtk_container_add (GTK_CONTAINER (popover), items);
    gtk_menu_button_set_popover (GTK_MENU_BUTTON (menu), popover);
    gtk_widget_show_all (items); /* Keep the popover closed until the menu button is clicked. */
    files_toolbar_sync_view_icon (self);
    return menu;
}

/* File Transfers: show the transfer window(s) or the idle window. */
static void
files_toolbar_transfers_clicked (GtkButton *button, gpointer data)
{
    GtkWidget *top = gtk_widget_get_toplevel (GTK_WIDGET (button));
    nemo_transfer_window_show (GTK_IS_WINDOW (top) ? GTK_WINDOW (top) : NULL);
    (void) data;
}

/* While transfers run: a small dot in the accent colour, top-right inside
 * the button's 28px square (clear of the 16px icon). */
static gboolean
files_toolbar_transfers_draw (GtkWidget *button, cairo_t *cr, gpointer data)
{
    GtkStyleContext *ctx;
    GdkRGBA accent;
    gint w;
    if (!nemo_transfer_window_is_busy ())
        return FALSE;
    ctx = gtk_widget_get_style_context (button);
    if (!(g_settings_get_boolean (nemo_preferences, DAKKAR_THEME_CUSTOM) &&
          gtk_style_context_lookup_color (ctx, "dakkar_accent", &accent)) &&
        !gtk_style_context_lookup_color (ctx, "theme_selected_bg_color", &accent))
        gtk_style_context_get_color (ctx, gtk_style_context_get_state (ctx), &accent);
    accent.alpha = 1.0;
    w = gtk_widget_get_allocated_width (button);
    cairo_save (cr);
    cairo_arc (cr, w - 6.5, 6.5, 3.0, 0, 2 * G_PI);
    gdk_cairo_set_source_rgba (cr, &accent);
    cairo_fill (cr);
    cairo_restore (cr);
    (void) data;
    return FALSE;
}

static void
files_toolbar_new_tab_clicked (GtkButton *button, gpointer data)
{
    NemoToolbar *self = NEMO_TOOLBAR (data);
    NemoWindowPane *pane = g_object_get_data (G_OBJECT (self), "files-owner-pane");
    if (pane == NULL || pane->window == NULL)
        return;
    nemo_window_set_active_pane (pane->window, pane);
    nemo_window_new_tab (pane->window);
}

/* ------------------------------------------------------------------ */
/* Toolbar button order (gsettings "toolbar-button-order")             */
/* ------------------------------------------------------------------ */

/* Default order; also the fallback for ids missing from the setting.
 * The first six live left of the location bar, the rest on the right. */
static const gchar *const dakkar_toolbar_default_left[] = {
    "previous", "next", "up", "refresh", "home", "computer"
};
static const gchar *const dakkar_toolbar_default_right[] = {
    "location", "terminal", "new-folder", "search", "thumbnails", "transfers",
    "new-tab", "places-tree", "view", "sort", "group", "extra-pane", "settings"
};

static GtkWidget *
toolbar_button_for_id (NemoToolbar *self, const gchar *id)
{
    const struct { const gchar *id; GtkWidget *widget; } map[] = {
        { "previous", self->priv->previous_button },
        { "next", self->priv->next_button },
        { "up", self->priv->up_button },
        { "refresh", self->priv->refresh_button },
        { "home", self->priv->home_button },
        { "computer", self->priv->computer_button },
        { "location", self->priv->toggle_location_button },
        { "terminal", self->priv->open_terminal_button },
        { "new-folder", self->priv->new_folder_button },
        { "search", self->priv->search_button },
        { "thumbnails", self->priv->show_thumbnails_button },
        { "new-tab", self->priv->new_tab_button },
        { "transfers", self->priv->transfers_button },
        { "view", self->priv->view_menu_button },
        { "sort", self->priv->sort_menu_button },
        { "group", self->priv->group_menu_button },
        { "settings", self->priv->settings_tool_button },
        { "extra-pane", self->priv->extra_pane_button },
        { "places-tree", self->priv->places_tree_button },
    };
    guint i;
    for (i = 0; i < G_N_ELEMENTS (map); i++)
        if (g_strcmp0 (map[i].id, id) == 0)
            return map[i].widget;
    return NULL;
}

/* Reorder one group's buttons inside its own box.  Ids from the setting
 * come first in the stored order (unknown ids, duplicates and ids of the
 * other group are skipped); ids the setting does not mention follow in
 * their default order, so a partial or stale setting can never lose a
 * button.  Packing padding is reset so every button gets the same gap. */
static void
toolbar_apply_group_order (NemoToolbar *self, GtkWidget *box,
                           const gchar *const *defaults, guint n_defaults,
                           gchar **stored)
{
    GPtrArray *ordered;
    guint i, j;

    if (!GTK_IS_BOX (box))
        return;
    ordered = g_ptr_array_new ();
    for (i = 0; stored != NULL && stored[i] != NULL; i++) {
        for (j = 0; j < n_defaults; j++) {
            if (g_strcmp0 (stored[i], defaults[j]) == 0) {
                GtkWidget *w = toolbar_button_for_id (self, defaults[j]);
                if (w != NULL && gtk_widget_get_parent (w) == box &&
                    !g_ptr_array_find (ordered, w, NULL))
                    g_ptr_array_add (ordered, w);
                break;
            }
        }
    }
    /* An id the saved order does not know (a button added in a newer
     * version) goes right after its nearest default predecessor, so it
     * lands among its neighbours instead of at the end of the group. */
    for (j = 0; j < n_defaults; j++) {
        GtkWidget *w = toolbar_button_for_id (self, defaults[j]);
        guint at = 0, k;
        gint p;
        if (w == NULL || gtk_widget_get_parent (w) != box ||
            g_ptr_array_find (ordered, w, NULL))
            continue;
        for (p = (gint) j - 1; p >= 0; p--) {
            GtkWidget *prev = toolbar_button_for_id (self, defaults[p]);
            if (prev != NULL && g_ptr_array_find (ordered, prev, &k)) {
                at = k + 1;
                break;
            }
        }
        g_ptr_array_insert (ordered, (gint) at, w);
    }
    /* The settings button always closes the framed group. */
    if (self->priv->settings_tool_button != NULL &&
        g_ptr_array_remove (ordered, self->priv->settings_tool_button))
        g_ptr_array_add (ordered, self->priv->settings_tool_button);
    for (i = 0; i < ordered->len; i++) {
        GtkWidget *w = g_ptr_array_index (ordered, i);
        gtk_box_set_child_packing (GTK_BOX (box), w, FALSE, FALSE, 0, GTK_PACK_START);
        gtk_box_reorder_child (GTK_BOX (box), w, (gint) i);
    }
    g_ptr_array_free (ordered, TRUE);
}

static void
toolbar_apply_button_order (NemoToolbar *self)
{
    gchar **stored = g_settings_get_strv (nemo_preferences, DAKKAR_TOOLBAR_BUTTON_ORDER);
    toolbar_apply_group_order (self, self->priv->left_box, dakkar_toolbar_default_left,
                               G_N_ELEMENTS (dakkar_toolbar_default_left), stored);
    toolbar_apply_group_order (self, self->priv->right_box, dakkar_toolbar_default_right,
                               G_N_ELEMENTS (dakkar_toolbar_default_right), stored);
    g_strfreev (stored);
}

static void
toolbar_button_order_changed (GSettings *settings, gchar *key, gpointer data)
{
    toolbar_apply_button_order (NEMO_TOOLBAR (data));
    (void) settings; (void) key;
}

/* ---- framed toolbar squares -------------------------------------------
 * View, Sort, Group By, Extra Pane and Settings are identical squares:
 * DAKKAR_TOOL_SQUARE x DAKKAR_TOOL_SQUARE outside, 1px border, 7px radius,
 * no padding, a 16px icon centred.  The size is enforced three ways:
 *   - gtk_widget_set_size_request (minimum),
 *   - a widget-level CSS provider at USER+1, so neither the theme nor a
 *     KDE-written ~/.config/gtk-3.0/gtk.css can add padding/min-size,
 *   - the only child is the 16px image (GtkMenuButton's own arrow is
 *     replaced), so nothing inside can grow the button.
 * Menu buttons show their dropdown as a small triangle painted INSIDE the
 * square, bottom-right, in the button's own theme foreground colour. */
#define DAKKAR_TOOL_SQUARE 28
#define DAKKAR_TOOL_ICON   16

static GtkCssProvider *
toolbar_square_provider (void)
{
    static GtkCssProvider *provider = NULL;
    if (provider == NULL) {
        provider = gtk_css_provider_new ();
        gtk_css_provider_load_from_data (provider,
            "button, button:hover, button:active, button:checked, "
            "button:disabled, button:backdrop {"
            " min-width:26px; min-height:26px; padding:0; margin:0;"
            " border-width:1px; border-style:solid; border-radius:7px;"
            " outline-offset:-3px; }", -1, NULL);
    }
    return provider;
}

static void
toolbar_square_image (GtkWidget *image)
{
    if (GTK_IS_IMAGE (image))
        gtk_image_set_pixel_size (GTK_IMAGE (image), DAKKAR_TOOL_ICON);
    gtk_widget_set_size_request (image, DAKKAR_TOOL_ICON, DAKKAR_TOOL_ICON);
    gtk_widget_set_halign (image, GTK_ALIGN_CENTER);
    gtk_widget_set_valign (image, GTK_ALIGN_CENTER);
    gtk_widget_set_hexpand (image, FALSE);
    gtk_widget_set_vexpand (image, FALSE);
    gtk_widget_set_margin_start (image, 0);
    gtk_widget_set_margin_end (image, 0);
    gtk_widget_set_margin_top (image, 0);
    gtk_widget_set_margin_bottom (image, 0);
}

static gboolean
toolbar_square_draw_triangle (GtkWidget *button, cairo_t *cr, gpointer data)
{
    GtkStyleContext *ctx = gtk_widget_get_style_context (button);
    gint w = gtk_widget_get_allocated_width (button);
    gint h = gtk_widget_get_allocated_height (button);
    GdkRGBA color;
    gtk_style_context_get_color (ctx, gtk_style_context_get_state (ctx), &color);
    color.alpha *= 0.80;
    gdk_cairo_set_source_rgba (cr, &color);
    /* 4px right-angled triangle tucked into the bottom-right corner,
     * 4px in from the outer edge (clear of the 7px rounded border) */
    cairo_move_to (cr, w - 4, h - 8);
    cairo_line_to (cr, w - 4, h - 4);
    cairo_line_to (cr, w - 8, h - 4);
    cairo_close_path (cr);
    cairo_fill (cr);
    (void) data;
    return FALSE;
}

static void
toolbar_square_geometry (GtkWidget *button, gboolean framed, gboolean dropdown)
{
    GtkStyleContext *ctx = gtk_widget_get_style_context (button);
    GtkWidget *child = gtk_bin_get_child (GTK_BIN (button));
    gtk_style_context_add_class (ctx, "dakkar-tool-button");
    if (framed)
        gtk_style_context_add_class (ctx, "dakkar-tool-framed");
    if (dropdown)
        gtk_style_context_add_class (ctx, "dakkar-tool-dropdown");
    gtk_style_context_add_provider (ctx, GTK_STYLE_PROVIDER (toolbar_square_provider ()),
                                    GTK_STYLE_PROVIDER_PRIORITY_USER + 1);
    gtk_widget_set_size_request (button, DAKKAR_TOOL_SQUARE, DAKKAR_TOOL_SQUARE);
    gtk_widget_set_halign (button, GTK_ALIGN_CENTER);
    gtk_widget_set_valign (button, GTK_ALIGN_CENTER);
    gtk_widget_set_hexpand (button, FALSE);
    gtk_widget_set_vexpand (button, FALSE);
    if (GTK_IS_BUTTON (button) && gtk_button_get_image (GTK_BUTTON (button)) != NULL) {
        gtk_button_set_always_show_image (GTK_BUTTON (button), TRUE);
        toolbar_square_image (gtk_button_get_image (GTK_BUTTON (button)));
    } else if (child != NULL) {
        toolbar_square_image (child);
    }
    if (dropdown && g_object_get_data (G_OBJECT (button), "dakkar-square-triangle") == NULL) {
        g_object_set_data (G_OBJECT (button), "dakkar-square-triangle", GINT_TO_POINTER (1));
        g_signal_connect_after (button, "draw", G_CALLBACK (toolbar_square_draw_triangle), NULL);
    }
}

static void
toolbar_make_square (GtkWidget *button, gboolean dropdown)
{
    toolbar_square_geometry (button, TRUE, dropdown);
}

/* The flat (frameless) tool buttons share the exact same 28x28 footprint
 * and 16px icon, so a user gtk.css cannot make them larger either. */
static void
toolbar_make_flat (GtkWidget *button)
{
    toolbar_square_geometry (button, FALSE, FALSE);
}

/* Menu pickers: the icon is the button's only child (it replaces the
 * GtkMenuButton arrow), the dropdown hint is the painted triangle. */
static void
toolbar_menu_button_set_icon (GtkWidget *menu, GtkWidget *image)
{
    GtkWidget *old = gtk_bin_get_child (GTK_BIN (menu));
    if (old != NULL)
        gtk_container_remove (GTK_CONTAINER (menu), old);
    gtk_container_add (GTK_CONTAINER (menu), image);
    gtk_widget_show (image);
    toolbar_make_square (menu, TRUE);
}

static void
toolbar_update_appearance (NemoToolbar *self)
{
	GtkWidget *widgetitem;
	gboolean icon_toolbar;
	gboolean show_location_entry;

    nemo_toolbar_update_root_state (self);

	show_location_entry = self->priv->show_location_entry;

	gtk_widget_set_visible (GTK_WIDGET(self->priv->toolbar),
				self->priv->show_main_bar);

    if (show_location_entry) {
        gtk_stack_set_visible_child_name (GTK_STACK (self->priv->stack), "location_bar");
    } else {
        gtk_stack_set_visible_child_name (GTK_STACK (self->priv->stack), "path_bar");
    }

    gtk_widget_set_visible (self->priv->root_bar,
                self->priv->show_root_bar);

        /* Please refer to the element name, not the action name after the forward slash, otherwise the prefs will not work*/

    widgetitem = self->priv->previous_button;
    icon_toolbar = g_settings_get_boolean (nemo_preferences, NEMO_PREFERENCES_SHOW_PREVIOUS_ICON_TOOLBAR);
    if ( icon_toolbar == FALSE ) { gtk_widget_hide (widgetitem); }
    else {gtk_widget_show (GTK_WIDGET(widgetitem));}

    widgetitem = self->priv->next_button;
    icon_toolbar = g_settings_get_boolean (nemo_preferences, NEMO_PREFERENCES_SHOW_NEXT_ICON_TOOLBAR);
    if ( icon_toolbar == FALSE ) { gtk_widget_hide (widgetitem); }
    else {gtk_widget_show (GTK_WIDGET(widgetitem));}

    widgetitem = self->priv->up_button;
    icon_toolbar = g_settings_get_boolean (nemo_preferences, NEMO_PREFERENCES_SHOW_UP_ICON_TOOLBAR);
    if ( icon_toolbar == FALSE ) { gtk_widget_hide (widgetitem); }
    else {gtk_widget_show (GTK_WIDGET(widgetitem));}

    widgetitem = self->priv->refresh_button;
    icon_toolbar = g_settings_get_boolean (nemo_preferences, NEMO_PREFERENCES_SHOW_RELOAD_ICON_TOOLBAR);
    if ( icon_toolbar == FALSE ) { gtk_widget_hide (widgetitem); }
    else {gtk_widget_show (GTK_WIDGET(widgetitem));}

    widgetitem = self->priv->home_button;
    icon_toolbar = g_settings_get_boolean (nemo_preferences, NEMO_PREFERENCES_SHOW_HOME_ICON_TOOLBAR);
    if ( icon_toolbar == FALSE ) { gtk_widget_hide (widgetitem); }
    else {gtk_widget_show (GTK_WIDGET(widgetitem));}

    widgetitem = self->priv->computer_button;
    icon_toolbar = g_settings_get_boolean (nemo_preferences, NEMO_PREFERENCES_SHOW_COMPUTER_ICON_TOOLBAR);
    if ( icon_toolbar == FALSE ) { gtk_widget_hide (widgetitem); }
    else {gtk_widget_show (GTK_WIDGET(widgetitem));}

    widgetitem = self->priv->search_button;
    icon_toolbar = g_settings_get_boolean (nemo_preferences, NEMO_PREFERENCES_SHOW_SEARCH_ICON_TOOLBAR);
    if ( icon_toolbar == FALSE ) { gtk_widget_hide (widgetitem); }
    else {gtk_widget_show (GTK_WIDGET(widgetitem));}

    widgetitem = self->priv->new_folder_button;
    icon_toolbar = g_settings_get_boolean (nemo_preferences, NEMO_PREFERENCES_SHOW_NEW_FOLDER_ICON_TOOLBAR);
    if ( icon_toolbar == FALSE ) { gtk_widget_hide (widgetitem); }
    else {gtk_widget_show (GTK_WIDGET(widgetitem));}

    widgetitem = self->priv->open_terminal_button;
    icon_toolbar = g_settings_get_boolean (nemo_preferences, NEMO_PREFERENCES_SHOW_OPEN_IN_TERMINAL_TOOLBAR);
    if (icon_toolbar == FALSE ) {gtk_widget_hide (widgetitem); }
    else {gtk_widget_show (GTK_WIDGET(widgetitem));}

    widgetitem = self->priv->toggle_location_button;
    icon_toolbar = g_settings_get_boolean (nemo_preferences, NEMO_PREFERENCES_SHOW_EDIT_ICON_TOOLBAR);
    if ( icon_toolbar == FALSE ) { gtk_widget_hide (widgetitem); }
    else {gtk_widget_show (GTK_WIDGET(widgetitem));}

    /* The four radio-action proxies remain alive to track Nemo's active
     * view, but the user sees one compact selector instead. */
    gtk_widget_hide (self->priv->icon_view_button);
    gtk_widget_hide (self->priv->list_view_button);
    gtk_widget_hide (self->priv->compact_view_button);
    gtk_widget_hide (self->priv->miller_view_button);
    files_toolbar_sync_view_icon (self);

    if (self->priv->extra_pane_button != NULL) {
        gtk_widget_set_visible (self->priv->extra_pane_button,
            g_settings_get_boolean (nemo_preferences, "dakkar-show-extra-pane-tool"));
        NemoWindowPane *owner = g_object_get_data (G_OBJECT (self), "files-owner-pane");
        if (owner != NULL && owner->window != NULL) {
            gboolean showing = nemo_window_split_view_showing (owner->window);
            gtk_widget_set_tooltip_text (self->priv->extra_pane_button,
                showing ? _("Close Extra Pane (F3)") : _("Open Extra Pane (F3)"));
        }
    }
    if (self->priv->places_tree_button != NULL) {
        gtk_widget_set_visible (self->priv->places_tree_button,
            g_settings_get_boolean (nemo_preferences, "dakkar-show-places-tree-tool"));
        NemoWindowPane *owner = g_object_get_data (G_OBJECT (self), "files-owner-pane");
        if (owner != NULL && owner->window != NULL) {
            gboolean tree = g_strcmp0 (nemo_window_get_sidebar_id (owner->window),
                                      NEMO_WINDOW_SIDEBAR_TREE) == 0;
            gtk_button_set_image (GTK_BUTTON (self->priv->places_tree_button),
                gtk_image_new_from_icon_name (tree ? "folder-symbolic" :
                                              dakkar_icon_pick ("view-list-tree-symbolic", "sidebar-tree-symbolic", "view-list-symbolic", NULL),
                                              GTK_ICON_SIZE_BUTTON));
            gtk_widget_set_tooltip_text (self->priv->places_tree_button,
                tree ? _("Show Places") : _("Show Tree View"));
        }
    }
    if (self->priv->new_tab_button != NULL)
        gtk_widget_set_visible (self->priv->new_tab_button,
            g_settings_get_boolean (nemo_preferences, DAKKAR_SHOW_NEW_TAB_TOOL));
    if (self->priv->transfers_button != NULL)
        gtk_widget_set_visible (self->priv->transfers_button,
            g_settings_get_boolean (nemo_preferences, DAKKAR_SHOW_TRANSFERS_TOOL));

    widgetitem = self->priv->show_thumbnails_button;
    icon_toolbar = g_settings_get_boolean (nemo_preferences, NEMO_PREFERENCES_SHOW_SHOW_THUMBNAILS_TOOLBAR);
    if ( icon_toolbar == FALSE ) { gtk_widget_hide (widgetitem); }
    else {gtk_widget_show (GTK_WIDGET(widgetitem));}
}

static void
setup_root_info_bar (NemoToolbar *self) {

    GtkWidget *root_bar = gtk_info_bar_new ();
    gtk_info_bar_set_message_type (GTK_INFO_BAR (root_bar), GTK_MESSAGE_ERROR);
    GtkWidget *content_area = gtk_info_bar_get_content_area (GTK_INFO_BAR (root_bar));

    GtkWidget *label = gtk_label_new (_("Elevated Privileges"));
    gtk_widget_show (label);
    gtk_container_add (GTK_CONTAINER (content_area), label);

    self->priv->root_bar = root_bar;
    gtk_box_pack_start (GTK_BOX (self), self->priv->root_bar, TRUE, TRUE, 0);
}

static void
toolbar_sync_tooltip (GtkAction *action, GParamSpec *pspec, gpointer button)
{
    gtk_widget_set_tooltip_text (GTK_WIDGET (button), gtk_action_get_tooltip (action));
}

static GtkWidget *
toolbar_create_toolbutton (NemoToolbar *self,
                gboolean create_toggle,
                const gchar *name)
{
    GtkWidget *button;
    GtkWidget *image;
    GtkAction *action;

    if (create_toggle)
    {
        button = gtk_toggle_button_new ();
    } else {
        button = gtk_button_new ();
    }

    image = gtk_image_new ();

    gtk_button_set_image (GTK_BUTTON (button), image);
    action = gtk_action_group_get_action (self->priv->action_group, name);
    gtk_activatable_set_related_action (GTK_ACTIVATABLE (button), action);
    gtk_button_set_label (GTK_BUTTON (button), NULL);
    gtk_widget_set_tooltip_text (button, gtk_action_get_tooltip (action));
    gtk_widget_set_can_focus (button, FALSE);
    gtk_style_context_add_class (gtk_widget_get_style_context (button), GTK_STYLE_CLASS_FLAT);
    toolbar_make_flat (button);

    return button;
}

static void
nemo_toolbar_constructed (GObject *obj)
{
	NemoToolbar *self = NEMO_TOOLBAR (obj);
	GtkWidget *toolbar;
    GtkWidget *hbox;
    GtkToolItem *tool_box;
    GtkWidget *box;
	GtkStyleContext *context;

	G_OBJECT_CLASS (nemo_toolbar_parent_class)->constructed (obj);

	gtk_style_context_set_junction_sides (gtk_widget_get_style_context (GTK_WIDGET (self)),
					      GTK_JUNCTION_BOTTOM);

    self->priv->show_location_entry = g_settings_get_boolean (nemo_preferences, NEMO_PREFERENCES_SHOW_LOCATION_ENTRY);

	/* add the UI */
	self->priv->ui_manager = gtk_ui_manager_new ();
	gtk_ui_manager_insert_action_group (self->priv->ui_manager, self->priv->action_group, 0);

	toolbar = gtk_toolbar_new ();
	self->priv->toolbar = toolbar;
    gtk_box_pack_start (GTK_BOX (self), self->priv->toolbar, TRUE, TRUE, 0);

	context = gtk_widget_get_style_context (GTK_WIDGET(toolbar));
	gtk_style_context_add_class (context, GTK_STYLE_CLASS_PRIMARY_TOOLBAR);

    /* Left side of the toolbar */
    tool_box = gtk_tool_item_new ();
    box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 2);

    self->priv->previous_button = toolbar_create_toolbutton (self, FALSE, NEMO_ACTION_BACK);
    gtk_container_add (GTK_CONTAINER (box), self->priv->previous_button);

    self->priv->next_button = toolbar_create_toolbutton (self, FALSE, NEMO_ACTION_FORWARD);
    gtk_container_add (GTK_CONTAINER (box), self->priv->next_button);

    self->priv->up_button = toolbar_create_toolbutton (self, FALSE, NEMO_ACTION_UP);
    gtk_container_add (GTK_CONTAINER (box), self->priv->up_button);

    self->priv->refresh_button = toolbar_create_toolbutton (self, FALSE, NEMO_ACTION_RELOAD);
    gtk_container_add (GTK_CONTAINER (box), self->priv->refresh_button);

    self->priv->home_button = toolbar_create_toolbutton (self, FALSE, NEMO_ACTION_HOME);
    gtk_container_add (GTK_CONTAINER (box), self->priv->home_button);

    self->priv->computer_button = toolbar_create_toolbutton (self, FALSE, NEMO_ACTION_COMPUTER);
    gtk_container_add (GTK_CONTAINER (box), self->priv->computer_button);
    self->priv->left_box = box;
    gtk_box_set_spacing (GTK_BOX (box), 4);

    gtk_container_add (GTK_CONTAINER (tool_box), GTK_WIDGET (box));
    gtk_container_add (GTK_CONTAINER (self->priv->toolbar), GTK_WIDGET (tool_box));

    gtk_widget_show_all (GTK_WIDGET (tool_box));
    gtk_widget_set_margin_right (GTK_WIDGET (tool_box), 6);

    /* Container to hold the location and pathbars */
    self->priv->stack = gtk_stack_new();
    gtk_stack_set_transition_type (GTK_STACK (self->priv->stack), GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    gtk_stack_set_transition_duration (GTK_STACK (self->priv->stack), 150);

    /* Regular Path Bar */
    hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_pack_start (GTK_BOX (hbox), GTK_WIDGET (self->priv->stack), TRUE, TRUE, 0);

    self->priv->path_bar = g_object_new (NEMO_TYPE_PATH_BAR, NULL);
    gtk_stack_add_named(GTK_STACK (self->priv->stack), GTK_WIDGET (self->priv->path_bar), "path_bar");

    /* Entry-Like Location Bar */
    self->priv->location_bar = nemo_location_bar_new ();
    gtk_stack_add_named(GTK_STACK (self->priv->stack), GTK_WIDGET (self->priv->location_bar), "location_bar");
    gtk_widget_show_all (hbox);

    tool_box = gtk_tool_item_new ();
    gtk_tool_item_set_expand (tool_box, TRUE);
    gtk_container_add (GTK_CONTAINER (tool_box), hbox);
    gtk_container_add (GTK_CONTAINER (self->priv->toolbar), GTK_WIDGET (tool_box));
    gtk_widget_show (GTK_WIDGET (tool_box));

    /* Right Side of the toolbar */
    tool_box = gtk_tool_item_new ();
    box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 2);

    self->priv->right_box = box;
    gtk_box_set_spacing (GTK_BOX (box), 4);
    self->priv->toggle_location_button = toolbar_create_toolbutton (self, FALSE, NEMO_ACTION_TOGGLE_LOCATION);
    gtk_container_add (GTK_CONTAINER (box), self->priv->toggle_location_button);

    self->priv->open_terminal_button = toolbar_create_toolbutton (self, FALSE, NEMO_ACTION_OPEN_IN_TERMINAL_TOOLBAR);
    /* the tooltip follows the selection ("Here" / "in Enclosing Folder") */
    g_signal_connect_object (gtk_activatable_get_related_action (GTK_ACTIVATABLE (self->priv->open_terminal_button)),
                             "notify::tooltip", G_CALLBACK (toolbar_sync_tooltip),
                             self->priv->open_terminal_button, 0);
    gtk_container_add (GTK_CONTAINER (box), self->priv->open_terminal_button);

    self->priv->new_folder_button = toolbar_create_toolbutton (self, FALSE, NEMO_ACTION_NEW_FOLDER);
    gtk_container_add (GTK_CONTAINER (box), self->priv->new_folder_button);

    self->priv->search_button = toolbar_create_toolbutton (self, TRUE, NEMO_ACTION_SEARCH);
    gtk_container_add (GTK_CONTAINER (box), self->priv->search_button);

    self->priv->show_thumbnails_button = toolbar_create_toolbutton (self, TRUE, NEMO_ACTION_SHOW_THUMBNAILS);
    gtk_container_add (GTK_CONTAINER (box), self->priv->show_thumbnails_button);

    setup_root_info_bar (self);

    self->priv->icon_view_button = toolbar_create_toolbutton (self, TRUE, NEMO_ACTION_ICON_VIEW);
    gtk_container_add (GTK_CONTAINER (box), self->priv->icon_view_button);

    self->priv->list_view_button = toolbar_create_toolbutton (self, TRUE, NEMO_ACTION_LIST_VIEW);
    gtk_container_add (GTK_CONTAINER (box), self->priv->list_view_button);

    self->priv->compact_view_button = toolbar_create_toolbutton (self, TRUE, NEMO_ACTION_COMPACT_VIEW);
    gtk_container_add (GTK_CONTAINER (box), self->priv->compact_view_button);

    self->priv->miller_view_button = toolbar_create_toolbutton (self, TRUE, NEMO_ACTION_MILLER_VIEW);
    gtk_container_add (GTK_CONTAINER (box), self->priv->miller_view_button);
    gtk_widget_set_no_show_all (self->priv->icon_view_button, TRUE);
    gtk_widget_set_no_show_all (self->priv->list_view_button, TRUE);
    gtk_widget_set_no_show_all (self->priv->compact_view_button, TRUE);
    gtk_widget_set_no_show_all (self->priv->miller_view_button, TRUE);

    self->priv->new_tab_button = gtk_button_new_from_icon_name (
                                 "tab-new-symbolic", GTK_ICON_SIZE_BUTTON);
    gtk_widget_set_tooltip_text (self->priv->new_tab_button, _("New Tab (Ctrl+T)"));
    gtk_style_context_add_class (gtk_widget_get_style_context (self->priv->new_tab_button),
                                 GTK_STYLE_CLASS_FLAT);
    toolbar_make_flat (self->priv->new_tab_button);
    gtk_widget_set_no_show_all (self->priv->new_tab_button, TRUE);
    gtk_widget_set_can_focus (self->priv->new_tab_button, FALSE);
    g_signal_connect (self->priv->new_tab_button, "clicked",
                      G_CALLBACK (files_toolbar_new_tab_clicked), self);
    gtk_box_pack_start (GTK_BOX (box), self->priv->new_tab_button, FALSE, FALSE, 8);

    self->priv->transfers_button = gtk_button_new_from_icon_name (
        dakkar_icon_pick ("folder-download-symbolic", "emblem-synchronizing-symbolic",
                         "view-refresh-symbolic", NULL), GTK_ICON_SIZE_BUTTON);
    gtk_widget_set_tooltip_text (self->priv->transfers_button, _("Show File Transfers"));
    atk_object_set_name (gtk_widget_get_accessible (self->priv->transfers_button),
                         _("File Transfers"));
    gtk_style_context_add_class (gtk_widget_get_style_context (self->priv->transfers_button),
                                 GTK_STYLE_CLASS_FLAT);
    toolbar_make_flat (self->priv->transfers_button);
    gtk_widget_set_no_show_all (self->priv->transfers_button, TRUE);
    gtk_widget_set_can_focus (self->priv->transfers_button, FALSE);
    g_signal_connect (self->priv->transfers_button, "clicked",
                      G_CALLBACK (files_toolbar_transfers_clicked), self);
    g_signal_connect_after (self->priv->transfers_button, "draw",
                            G_CALLBACK (files_toolbar_transfers_draw), NULL);
    nemo_transfer_window_add_activity_widget (self->priv->transfers_button);
    gtk_box_pack_start (GTK_BOX (box), self->priv->transfers_button, FALSE, FALSE, 0);

    GtkWidget *picker = files_toolbar_view_picker (self);
    gtk_box_pack_start (GTK_BOX (box), picker, FALSE, FALSE, 10);
    /* Placed immediately after View, with separate per-pane metadata. */
    GtkWidget *sort_picker = files_toolbar_sort_picker (self);
    gtk_box_pack_start (GTK_BOX (box), sort_picker, FALSE, FALSE, 0);
    /* Fixed order: View, Sort, Group By, Settings. */
    GtkWidget *group_picker = dakkar_group_picker (self);
    gtk_box_pack_start (GTK_BOX (box), group_picker, FALSE, FALSE, 4);
    /* Settings live beside Group By, not in the lower-left status controls. */
    GtkWidget *settings_button = gtk_button_new_from_icon_name (
                                     "emblem-system-symbolic", GTK_ICON_SIZE_BUTTON);
    gtk_widget_set_tooltip_text (settings_button, _("Files Settings"));
    gtk_style_context_add_class (gtk_widget_get_style_context (settings_button),
                                 "files-settings-picker");
    toolbar_make_square (settings_button, FALSE);
    self->priv->settings_tool_button = settings_button;
    gtk_box_pack_start (GTK_BOX (box), settings_button, FALSE, FALSE, 2);
    g_signal_connect (settings_button, "clicked",
                      G_CALLBACK (files_toolbar_preferences_clicked), self);
    /* Optional navigation tools reuse existing actions rather than duplicating logic. */
    self->priv->extra_pane_button = gtk_button_new_from_icon_name (
        "view-dual-symbolic", GTK_ICON_SIZE_BUTTON);
    gtk_widget_set_no_show_all (self->priv->extra_pane_button, TRUE);
    toolbar_make_square (self->priv->extra_pane_button, FALSE);
    gtk_widget_set_tooltip_text (self->priv->extra_pane_button, _("Extra Pane (F3)"));
    g_signal_connect (self->priv->extra_pane_button, "clicked",
                      G_CALLBACK (dakkar_extra_pane_tool_clicked), self);
    gtk_box_pack_start (GTK_BOX (box), self->priv->extra_pane_button, FALSE, FALSE, 2);
    self->priv->places_tree_button = gtk_button_new_from_icon_name (
        dakkar_icon_pick ("view-list-tree-symbolic", "sidebar-tree-symbolic", "view-list-symbolic", NULL),
        GTK_ICON_SIZE_BUTTON);
    gtk_widget_set_no_show_all (self->priv->places_tree_button, TRUE);
    toolbar_make_flat (self->priv->places_tree_button);
    gtk_widget_set_tooltip_text (self->priv->places_tree_button, _("Switch Places / Tree"));
    g_signal_connect (self->priv->places_tree_button, "clicked",
                      G_CALLBACK (dakkar_places_tree_tool_clicked), self);
    gtk_box_pack_start (GTK_BOX (box), self->priv->places_tree_button, FALSE, FALSE, 2);
    files_toolbar_sync_view_icon (self);

    gtk_container_add (GTK_CONTAINER (tool_box), GTK_WIDGET (box));
    gtk_container_add (GTK_CONTAINER (self->priv->toolbar), GTK_WIDGET (tool_box));

    gtk_widget_show_all (GTK_WIDGET (tool_box));
    gtk_widget_set_margin_left (GTK_WIDGET (tool_box), 6);

    /* User-chosen button order, applied now and live on every change. */
    toolbar_apply_button_order (self);
    g_signal_connect_object (nemo_preferences, "changed::" DAKKAR_TOOLBAR_BUTTON_ORDER,
                             G_CALLBACK (toolbar_button_order_changed), self, 0);
    g_signal_connect_swapped (nemo_preferences,
                  "changed::" DAKKAR_SHOW_NEW_TAB_TOOL,
                  G_CALLBACK (toolbar_update_appearance), self);
    g_signal_connect_object (nemo_preferences,
                  "changed::" DAKKAR_SHOW_TRANSFERS_TOOL,
                  G_CALLBACK (toolbar_update_appearance), self, G_CONNECT_SWAPPED);

    /* Optional tool changes must affect this toolbar immediately. */
    g_signal_connect_swapped (nemo_preferences,
                  "changed::dakkar-show-extra-pane-tool",
                  G_CALLBACK (toolbar_update_appearance), self);
    g_signal_connect_swapped (nemo_preferences,
                  "changed::dakkar-show-places-tree-tool",
                  G_CALLBACK (toolbar_update_appearance), self);
    /* nemo patch */
    g_signal_connect_swapped (nemo_preferences,
                  "changed::" NEMO_PREFERENCES_SHOW_PREVIOUS_ICON_TOOLBAR,
                  G_CALLBACK (toolbar_update_appearance), self);
    g_signal_connect_swapped (nemo_preferences,
                  "changed::" NEMO_PREFERENCES_SHOW_NEXT_ICON_TOOLBAR,
                  G_CALLBACK (toolbar_update_appearance), self);
    g_signal_connect_swapped (nemo_preferences,
                  "changed::" NEMO_PREFERENCES_SHOW_UP_ICON_TOOLBAR,
                  G_CALLBACK (toolbar_update_appearance), self);
    g_signal_connect_swapped (nemo_preferences,
                  "changed::" NEMO_PREFERENCES_SHOW_EDIT_ICON_TOOLBAR,
                  G_CALLBACK (toolbar_update_appearance), self);
    g_signal_connect_swapped (nemo_preferences,
                  "changed::" NEMO_PREFERENCES_SHOW_RELOAD_ICON_TOOLBAR,
                  G_CALLBACK (toolbar_update_appearance), self);
    g_signal_connect_swapped (nemo_preferences,
                  "changed::" NEMO_PREFERENCES_SHOW_HOME_ICON_TOOLBAR,
                  G_CALLBACK (toolbar_update_appearance), self);
    g_signal_connect_swapped (nemo_preferences,
                  "changed::" NEMO_PREFERENCES_SHOW_COMPUTER_ICON_TOOLBAR,
                  G_CALLBACK (toolbar_update_appearance), self);
    g_signal_connect_swapped (nemo_preferences,
                  "changed::" NEMO_PREFERENCES_SHOW_SEARCH_ICON_TOOLBAR,
                  G_CALLBACK (toolbar_update_appearance), self);
    g_signal_connect_swapped (nemo_preferences,
                  "changed::" NEMO_PREFERENCES_SHOW_NEW_FOLDER_ICON_TOOLBAR,
                  G_CALLBACK (toolbar_update_appearance), self);
    g_signal_connect_swapped (nemo_preferences,
                  "changed::" NEMO_PREFERENCES_SHOW_OPEN_IN_TERMINAL_TOOLBAR,
                  G_CALLBACK (toolbar_update_appearance), self);
    g_signal_connect_swapped (nemo_preferences,
                  "changed::" NEMO_PREFERENCES_SHOW_ICON_VIEW_ICON_TOOLBAR,
                  G_CALLBACK (toolbar_update_appearance), self);
    g_signal_connect_swapped (nemo_preferences,
                  "changed::" NEMO_PREFERENCES_SHOW_LIST_VIEW_ICON_TOOLBAR,
                  G_CALLBACK (toolbar_update_appearance), self);
    g_signal_connect_swapped (nemo_preferences,
                  "changed::" NEMO_PREFERENCES_SHOW_COMPACT_VIEW_ICON_TOOLBAR,
                  G_CALLBACK (toolbar_update_appearance), self);
    g_signal_connect_swapped (nemo_preferences,
                  "changed::" NEMO_PREFERENCES_SHOW_SHOW_THUMBNAILS_TOOLBAR,
                  G_CALLBACK (toolbar_update_appearance), self);
    g_signal_connect_swapped (nemo_preferences,
                  "changed::" NEMO_PREFERENCES_SHOW_IMAGE_FILE_THUMBNAILS,
                  G_CALLBACK (toolbar_update_appearance), self);
    g_signal_connect_swapped (nemo_preferences,
                  "changed::" NEMO_PREFERENCES_SHOW_LOCATION_ENTRY,
                  G_CALLBACK (toolbar_update_appearance), self);

	toolbar_update_appearance (self);
}

static void
nemo_toolbar_init (NemoToolbar *self)
{
	self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, NEMO_TYPE_TOOLBAR,
						  NemoToolbarPriv);
	self->priv->show_main_bar = TRUE;	
}

static void
nemo_toolbar_get_property (GObject *object,
			       guint property_id,
			       GValue *value,
			       GParamSpec *pspec)
{
	NemoToolbar *self = NEMO_TOOLBAR (object);

	switch (property_id) {
	case PROP_SHOW_LOCATION_ENTRY:
		g_value_set_boolean (value, self->priv->show_location_entry);
		break;
	case PROP_SHOW_MAIN_BAR:
		g_value_set_boolean (value, self->priv->show_main_bar);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
		break;
	}
}

static void
nemo_toolbar_set_property (GObject *object,
			       guint property_id,
			       const GValue *value,
			       GParamSpec *pspec)
{
	NemoToolbar *self = NEMO_TOOLBAR (object);

	switch (property_id) {
	case PROP_ACTION_GROUP:
		self->priv->action_group = g_value_dup_object (value);
		break;
	case PROP_SHOW_LOCATION_ENTRY:
		nemo_toolbar_set_show_location_entry (self, g_value_get_boolean (value));
		break;
	case PROP_SHOW_MAIN_BAR:
		nemo_toolbar_set_show_main_bar (self, g_value_get_boolean (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
		break;
	}
}

static void
nemo_toolbar_dispose (GObject *obj)
{
	NemoToolbar *self = NEMO_TOOLBAR (obj);

	g_clear_object (&self->priv->action_group);

	g_signal_handlers_disconnect_by_func (nemo_preferences,
					      toolbar_update_appearance, self);

	G_OBJECT_CLASS (nemo_toolbar_parent_class)->dispose (obj);
}

static void
nemo_toolbar_class_init (NemoToolbarClass *klass)
{
	GObjectClass *oclass;

	oclass = G_OBJECT_CLASS (klass);
	oclass->get_property = nemo_toolbar_get_property;
	oclass->set_property = nemo_toolbar_set_property;
	oclass->constructed = nemo_toolbar_constructed;
	oclass->dispose = nemo_toolbar_dispose;

	properties[PROP_ACTION_GROUP] =
		g_param_spec_object ("action-group",
				     "The action group",
				     "The action group to get actions from",
				     GTK_TYPE_ACTION_GROUP,
				     G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY |
				     G_PARAM_STATIC_STRINGS);
	properties[PROP_SHOW_LOCATION_ENTRY] =
		g_param_spec_boolean ("show-location-entry",
				      "Whether to show the location entry",
				      "Whether to show the location entry instead of the pathbar",
				      FALSE,
				      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
	properties[PROP_SHOW_MAIN_BAR] =
		g_param_spec_boolean ("show-main-bar",
				      "Whether to show the main bar",
				      "Whether to show the main toolbar",
				      TRUE,
				      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
	
    signals[CHECK_ADMIN_LOCATION] =
        g_signal_new ("check-admin-location",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_BOOLEAN, 0);

	g_type_class_add_private (klass, sizeof (NemoToolbarClass));
	g_object_class_install_properties (oclass, NUM_PROPERTIES, properties);
}

GtkWidget *
nemo_toolbar_new (GtkActionGroup *action_group)
{
	return g_object_new (NEMO_TYPE_TOOLBAR,
			     "action-group", action_group,
			     "orientation", GTK_ORIENTATION_VERTICAL,
			     NULL);
}

GtkWidget *
nemo_toolbar_get_path_bar (NemoToolbar *self)
{
	return self->priv->path_bar;
}

GtkWidget *
nemo_toolbar_get_location_bar (NemoToolbar *self)
{
	return self->priv->location_bar;
}

gboolean
nemo_toolbar_get_show_location_entry (NemoToolbar *self)
{
	return self->priv->show_location_entry;
}

void
nemo_toolbar_set_show_main_bar (NemoToolbar *self,
				    gboolean show_main_bar)
{
	if (show_main_bar != self->priv->show_main_bar) {
		self->priv->show_main_bar = show_main_bar;
		toolbar_update_appearance (self);

		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SHOW_MAIN_BAR]);
	}
}

void
nemo_toolbar_set_show_location_entry (NemoToolbar *self,
					  gboolean show_location_entry)
{
	if (show_location_entry != self->priv->show_location_entry) {
		self->priv->show_location_entry = show_location_entry;
		toolbar_update_appearance (self);

		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SHOW_LOCATION_ENTRY]);
	}
}

void
nemo_toolbar_update_for_location (NemoToolbar *self)
{
    toolbar_update_appearance (self);
}

void
nemo_toolbar_sync_inspector (NemoToolbar *self)
{
    NemoWindowPane *pane;
    g_return_if_fail (NEMO_IS_TOOLBAR (self));
    pane = g_object_get_data (G_OBJECT (self), "files-owner-pane");
    if (pane != NULL && NEMO_IS_WINDOW (pane->window))
        nemo_window_sync_inspector_control (pane->window);
}