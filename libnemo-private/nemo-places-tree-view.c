/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*-

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

*/

#include "nemo-places-tree-view.h"
#include "nemo-global-preferences.h"

G_DEFINE_TYPE (NemoPlacesTreeView, nemo_places_tree_view,
               GTK_TYPE_TREE_VIEW);

static void     nemo_places_tree_view_finalize (GObject *gobject);

static gpointer parent_class;

static void
nemo_places_tree_view_init (NemoPlacesTreeView *tree_view)
{
    gtk_style_context_add_class (gtk_widget_get_style_context (GTK_WIDGET (tree_view)), "places-treeview");

    /* The cells must NEVER paint a selection/hover background of their
     * own: GtkTreeView paints it per cell (expander, icon+name, eject), and
     * together with the one rounded highlight drawn below that produced a
     * bar with round blobs at both ends.  The rule lives on this widget at
     * USER+1 priority so no GTK theme, ~/.config/gtk-3.0/gtk.css (KDE writes
     * one) or custom Dakkar palette can bring the per-cell paint back. */
    {
        static GtkCssProvider *cells = NULL;
        if (cells == NULL) {
            cells = gtk_css_provider_new ();
            gtk_css_provider_load_from_data (cells,
                "treeview.view, treeview.view:hover, treeview.view:selected, "
                "treeview.view:selected:hover, treeview.view:selected:focus, "
                "treeview.view:selected:backdrop, treeview.view:focus, "
                "treeview.view:backdrop {"
                " background-color: transparent; background-image: none;"
                " border-radius: 0; box-shadow: none; }"
                "treeview.view:selected, treeview.view:selected:focus {"
                " color: @theme_selected_fg_color; }", -1, NULL);
        }
        gtk_style_context_add_provider (gtk_widget_get_style_context (GTK_WIDGET (tree_view)),
                                        GTK_STYLE_PROVIDER (cells),
                                        GTK_STYLE_PROVIDER_PRIORITY_USER + 1);
    }
}

/* Dakkar: GtkTreeView paints a row's background once PER CELL (expander,
 * icon+name and eject columns).  A CSS border-radius on treeview.view is
 * therefore applied to every cell: the selected row turns into a chain of
 * bumps and unselected rows leave small star-shaped gaps between them.
 * The places tree view keeps its cell backgrounds transparent (CSS in nemo-corner-style.c) and paints ONE
 * rounded highlight per row here, under the cells, using the theme's
 * selection colour and the "sidebar selection radius" preference.  */

#define DAKKAR_ROW_INSET 4

static gboolean
places_row_lookup_color (GtkWidget *widget, const gchar *name, GdkRGBA *color)
{
    return gtk_style_context_lookup_color (gtk_widget_get_style_context (widget),
                                           name, color);
}

static void
places_row_path (cairo_t *cr, gdouble x, gdouble y, gdouble w, gdouble h, gdouble r)
{
    r = MIN (r, MIN (w / 2.0, h / 2.0));
    cairo_new_sub_path (cr);
    if (r <= 0.5) {
        cairo_rectangle (cr, x, y, w, h);
        return;
    }
    cairo_arc (cr, x + w - r, y + r, r, -G_PI / 2.0, 0);
    cairo_arc (cr, x + w - r, y + h - r, r, 0, G_PI / 2.0);
    cairo_arc (cr, x + r, y + h - r, r, G_PI / 2.0, G_PI);
    cairo_arc (cr, x + r, y + r, r, G_PI, 3.0 * G_PI / 2.0);
    cairo_close_path (cr);
}

static void
places_row_paint (GtkTreeView *tree_view, cairo_t *cr, GtkTreePath *path,
                  const GdkRGBA *color, gint radius)
{
    GdkRectangle row, visible;
    gint bx, by;

    gtk_tree_view_get_background_area (tree_view, path, NULL, &row);
    gtk_tree_view_get_visible_rect (tree_view, &visible);
    gtk_tree_view_convert_tree_to_bin_window_coords (tree_view, visible.x, visible.y,
                                                     &bx, &by);
    if (row.height < 2 || visible.width <= 2 * DAKKAR_ROW_INSET)
        return;
    /* Contained in the visible sidebar width, never past its edges. */
    places_row_path (cr, bx + DAKKAR_ROW_INSET, row.y + 1,
                     visible.width - 2 * DAKKAR_ROW_INSET, row.height - 2, radius);
    gdk_cairo_set_source_rgba (cr, color);
    cairo_fill (cr);
}

static gboolean
places_row_selectable (GtkTreeView *tree_view, GtkTreePath *path)
{
    GtkTreeSelection *selection = gtk_tree_view_get_selection (tree_view);
    GtkTreeSelectionFunc func = gtk_tree_selection_get_select_function (selection);
    if (func == NULL)
        return TRUE;
    return func (selection, gtk_tree_view_get_model (tree_view), path, FALSE,
                 gtk_tree_selection_get_user_data (selection));
}

static gboolean
nemo_places_tree_view_draw (GtkWidget *widget, cairo_t *cr)
{
    GtkTreeView *tree_view = GTK_TREE_VIEW (widget);
    GdkWindow *bin = gtk_tree_view_get_bin_window (tree_view);

    if (bin != NULL && gtk_cairo_should_draw_window (cr, bin) &&
        gtk_tree_view_get_model (tree_view) != NULL) {
        GtkTreeSelection *selection = gtk_tree_view_get_selection (tree_view);
        GList *rows, *l;
        GdkRGBA selected, hover;
        const gchar *hover_path;
        gint radius = 0;
        gboolean backdrop = (gtk_widget_get_state_flags (widget) & GTK_STATE_FLAG_BACKDROP) != 0;

        if (nemo_preferences != NULL)
            radius = CLAMP (g_settings_get_int (nemo_preferences,
                                                DAKKAR_SIDEBAR_SELECTION_RADIUS), 0, 16);
        /* Theme colours only (Breeze, Adwaita, ... all define these). */
        /* With custom Dakkar colours the sidebar uses the chosen accent,
         * like the List and Miller selections do. */
        if (!(nemo_preferences != NULL &&
              g_settings_get_boolean (nemo_preferences, DAKKAR_THEME_CUSTOM) &&
              places_row_lookup_color (widget, "dakkar_accent", &selected)) &&
            !(backdrop && places_row_lookup_color (widget, "theme_unfocused_selected_bg_color", &selected)) &&
            !places_row_lookup_color (widget, "theme_selected_bg_color", &selected) &&
            !places_row_lookup_color (widget, "selected_bg_color", &selected))
            gtk_style_context_get_color (gtk_widget_get_style_context (widget),
                                         GTK_STATE_FLAG_SELECTED, &selected);
        if (!places_row_lookup_color (widget, "theme_fg_color", &hover))
            gtk_style_context_get_color (gtk_widget_get_style_context (widget),
                                         GTK_STATE_FLAG_NORMAL, &hover);
        hover.alpha *= 0.07;

        cairo_save (cr);
        gtk_cairo_transform_to_window (cr, widget, bin);
        hover_path = g_object_get_data (G_OBJECT (widget), "dakkar-hover-row");
        if (hover_path != NULL) {
            GtkTreePath *path = gtk_tree_path_new_from_string (hover_path);
            if (path != NULL && !gtk_tree_selection_path_is_selected (selection, path) &&
                places_row_selectable (tree_view, path))
                places_row_paint (tree_view, cr, path, &hover, radius);
            if (path != NULL)
                gtk_tree_path_free (path);
        }
        rows = gtk_tree_selection_get_selected_rows (selection, NULL);
        for (l = rows; l != NULL; l = l->next)
            places_row_paint (tree_view, cr, l->data, &selected, radius);
        g_list_free_full (rows, (GDestroyNotify) gtk_tree_path_free);
        cairo_restore (cr);
    }
    return GTK_WIDGET_CLASS (parent_class)->draw (widget, cr);
}

static void
places_row_set_hover (GtkWidget *widget, GtkTreePath *path)
{
    gchar *now = path != NULL ? gtk_tree_path_to_string (path) : NULL;
    if (g_strcmp0 (now, g_object_get_data (G_OBJECT (widget), "dakkar-hover-row")) != 0) {
        g_object_set_data_full (G_OBJECT (widget), "dakkar-hover-row", now, g_free);
        gtk_widget_queue_draw (widget);
    } else {
        g_free (now);
    }
}

static gboolean
nemo_places_tree_view_motion (GtkWidget *widget, GdkEventMotion *event)
{
    GtkTreeView *tree_view = GTK_TREE_VIEW (widget);
    if (event->window == gtk_tree_view_get_bin_window (tree_view)) {
        GtkTreePath *path = NULL;
        if (gtk_tree_view_get_path_at_pos (tree_view, (gint) event->x, (gint) event->y,
                                           &path, NULL, NULL, NULL)) {
            places_row_set_hover (widget, path);
            gtk_tree_path_free (path);
        } else {
            places_row_set_hover (widget, NULL);
        }
    }
    return GTK_WIDGET_CLASS (parent_class)->motion_notify_event (widget, event);
}

static gboolean
nemo_places_tree_view_leave (GtkWidget *widget, GdkEventCrossing *event)
{
    places_row_set_hover (widget, NULL);
    return GTK_WIDGET_CLASS (parent_class)->leave_notify_event (widget, event);
}

static void
nemo_places_tree_view_class_init (NemoPlacesTreeViewClass *klass)
{
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
    GObjectClass         *object_class = G_OBJECT_CLASS(klass);
    parent_class           = g_type_class_peek_parent (klass);
    object_class->finalize = nemo_places_tree_view_finalize;
    widget_class->draw = nemo_places_tree_view_draw;
    widget_class->motion_notify_event = nemo_places_tree_view_motion;
    widget_class->leave_notify_event = nemo_places_tree_view_leave;

    gtk_widget_class_install_style_property (widget_class,
                         g_param_spec_boxed ("disk-full-bg-color",
                                             "Unselected disk indicator background color",
                                             "Unselected disk indicator background color",
                                             GDK_TYPE_COLOR,
                                             G_PARAM_READABLE));

    gtk_widget_class_install_style_property (widget_class,
                         g_param_spec_boxed ("disk-full-fg-color",
                                             "Unselected disk indicator foreground color",
                                             "Unselected disk indicator foreground color",
                                             GDK_TYPE_COLOR,
                                             G_PARAM_READABLE));

    gtk_widget_class_install_style_property (widget_class,
                           g_param_spec_int ("disk-full-bar-width",
                                             "Disk indicator bar width",
                                             "Disk indicator bar width",
                                             0, G_MAXINT, 2,
                                             G_PARAM_READABLE));

    gtk_widget_class_install_style_property (widget_class,
                           g_param_spec_int ("disk-full-bar-radius",
                                             "Disk indicator bar radius (usually half the width)",
                                             "Disk indicator bar radius (usually half the width)",
                                             0, G_MAXINT, 1,
                                             G_PARAM_READABLE));

    gtk_widget_class_install_style_property (widget_class,
                           g_param_spec_int ("disk-full-bottom-padding",
                                             "Extra padding under the disk indicator",
                                             "Extra padding under the disk indicator",
                                             0, G_MAXINT, 1,
                                             G_PARAM_READABLE));

    gtk_widget_class_install_style_property (widget_class,
                           g_param_spec_int ("disk-full-max-length",
                                             "Maximum length of the disk indicator",
                                             "Maximum length of the disk indicator",
                                             0, G_MAXINT, 70,
                                             G_PARAM_READABLE));

}

GtkWidget *
nemo_places_tree_view_new (void)
{
    return g_object_new (NEMO_TYPE_PLACES_TREE_VIEW, NULL);
}

static void
nemo_places_tree_view_finalize (GObject *object)
{
    G_OBJECT_CLASS (parent_class)->finalize (object);
}
