/* -*- Mode: C; indent-tabs-mode: f; c-basic-offset: 4; tab-width: 4 -*- */

/*
 *  Nemo
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License as
 *  published by the Free Software Foundation; either version 2 of the
 *  License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Suite 500, MA 02110-1335, USA.
 *
 *  Authors : Mr Jamie McCracken (jamiemcc at blueyonder dot co dot uk)
 *            Cosimo Cecchi <cosimoc@gnome.org>
 *
 */

#include <config.h>

#include <gdk/gdkkeysyms.h>
#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <gio/gio.h>
#include <math.h>
#include <cairo-gobject.h>
#include <libxapp/xapp-favorites.h>

#include <libnemo-private/nemo-dnd.h>
#include <libnemo-private/nemo-bookmark.h>
#include "dakkar-favorites.h"
#include <libnemo-private/nemo-global-preferences.h>
#include <libnemo-private/nemo-module.h>
#include <libnemo-private/nemo-metadata.h>
#include <libnemo-private/nemo-file.h>
#include <libnemo-private/nemo-file-utilities.h>
#include <libnemo-private/nemo-file-operations.h>
#include <libnemo-private/nemo-trash-monitor.h>
#include <libnemo-private/nemo-icon-names.h>
#include <libnemo-private/nemo-cell-renderer-disk.h>
#include <libnemo-private/nemo-places-tree-view.h>
#include <libnemo-private/nemo-action-manager.h>
#include <libnemo-private/nemo-action.h>
#include <libnemo-private/nemo-ui-utilities.h>

#include <eel/eel-debug.h>
#include <eel/eel-gtk-extensions.h>
#include <eel/eel-glib-extensions.h>
#include <eel/eel-graphic-effects.h>
#include <eel/eel-string.h>
#include <eel/eel-stock-dialogs.h>
#include <eel/eel-vfs-extensions.h>

#include "nemo-actions.h"
#include "nemo-application.h"
#include "nemo-bookmark-list.h"
#include "nemo-corner-style.h"
#include "nemo-places-sidebar.h"
#include "dakkar-icon-picker.h"
#include "nemo-properties-window.h"
#include "nemo-window.h"
#include "nemo-window-slot.h"
#include "dakkar-custom-icons.h"

#define DEBUG_FLAG NEMO_DEBUG_PLACES
#include <libnemo-private/nemo-debug.h>

#define EXPANDER_PAD_COLUMN_WIDTH 4
#define EJECT_COLUMN_MIN_WIDTH 22
#define EJECT_COLUMN_MAX_WIDTH 60
#define DRAG_EXPAND_CATEGORY_DELAY 500
#define EJECT_PAD_COLUMN_WIDTH 14

static gint EJECT_ICON_SIZE_NOT_HOVERED = 0;
static gint EJECT_ICON_SIZE_HOVERED = GTK_ICON_SIZE_MENU;
static gint menu_icon_pixels = 16;
#define EJECT_ICON_SIZE_REDUCTION 4

#if (!GLIB_CHECK_VERSION(2,50,0))
#define g_drive_is_removable g_drive_is_media_removable
#endif

typedef struct {
	GtkScrolledWindow  parent;
	GtkTreeView        *tree_view;
    GtkTreeViewColumn  *eject_column;
    GtkCellRenderer    *eject_icon_cell_renderer;
    GtkCellRenderer    *editable_renderer;
	char 	           *uri;
	GtkTreeStore       *store;
    GtkTreeModel       *store_filter;

	NemoWindow *window;
	NemoBookmarkList *bookmarks;
	GVolumeMonitor *volume_monitor;

    NemoActionManager *action_manager;
    gulong actions_changed_id;
    guint actions_changed_idle_id;

    GtkUIManager *ui_manager;

    GtkActionGroup *bookmark_action_group;
    guint bookmark_action_group_merge_id;
    GtkActionGroup *action_action_group;
    guint action_action_group_merge_id;

    gboolean actions_need_update;

	gboolean devices_header_added;
	gboolean bookmarks_header_added;

	/* DnD */
	GList     *drag_list;
	gboolean  drag_data_received;
	int       drag_data_info;
	gboolean  drop_occured;
    gboolean  in_drag;
    gchar     *desktop_dnd_source_fs;
    gboolean  desktop_dnd_can_delete_source;

	GtkWidget *popup_menu;

	/* volume mounting - delayed open process */
	gboolean mounting;
	NemoWindowSlot *go_to_after_mount_slot;
	NemoWindowOpenFlags go_to_after_mount_flags;

	guint bookmarks_changed_id;

    gboolean my_computer_expanded;
    gboolean bookmarks_expanded;
    gboolean favorites_expanded;
    gboolean devices_expanded;
    gboolean network_expanded;

    gboolean updating_sidebar;

    /* Store the positions of the last
     * entry prior to bookmarks, and
     * the first entry after bookmarks
     * to allow drag and drop creation
     * of new bookmarks */
    gchar *top_bookend_uri;
    gchar *bottom_bookend_uri;

    gint bookmark_breakpoint;
    guint expand_timeout_source;
    guint popup_menu_action_index;
    guint update_places_on_idle_id;

} NemoPlacesSidebar;

typedef struct {
	GtkScrolledWindowClass parent;
} NemoPlacesSidebarClass;

typedef struct {
        GObject parent;
} NemoPlacesSidebarProvider;

typedef struct {
        GObjectClass parent;
} NemoPlacesSidebarProviderClass;

enum {
	PLACES_SIDEBAR_COLUMN_ROW_TYPE,
	PLACES_SIDEBAR_COLUMN_URI,
	PLACES_SIDEBAR_COLUMN_DRIVE,
	PLACES_SIDEBAR_COLUMN_VOLUME,
	PLACES_SIDEBAR_COLUMN_MOUNT,
	PLACES_SIDEBAR_COLUMN_NAME,
	PLACES_SIDEBAR_COLUMN_GICON,
	PLACES_SIDEBAR_COLUMN_INDEX,
	PLACES_SIDEBAR_COLUMN_EJECT,
	PLACES_SIDEBAR_COLUMN_NO_EJECT,
	PLACES_SIDEBAR_COLUMN_BOOKMARK,
    PLACES_SIDEBAR_COLUMN_TOOLTIP,
    PLACES_SIDEBAR_COLUMN_EJECT_ICON,
	PLACES_SIDEBAR_COLUMN_EJECT_ICON_SIZE,
	PLACES_SIDEBAR_COLUMN_SECTION_TYPE,
	PLACES_SIDEBAR_COLUMN_HEADING_TEXT,
    PLACES_SIDEBAR_COLUMN_DF_PERCENT,
    PLACES_SIDEBAR_COLUMN_SHOW_DF,

	PLACES_SIDEBAR_COLUMN_COUNT
};

typedef enum {
	PLACES_BUILT_IN,
	PLACES_XDG_DIR,
	PLACES_MOUNTED_VOLUME,
	PLACES_BOOKMARK,
    PLACES_FAVORITE,
	PLACES_HEADING,
} PlaceType;

typedef enum {
    SECTION_COMPUTER,
    SECTION_FAVORITES,
    SECTION_XDG_BOOKMARKS,
    SECTION_BOOKMARKS,
	SECTION_DEVICES,
	SECTION_NETWORK,
} SectionType;

enum {
    POSITION_UPPER,
    POSITION_MIDDLE,
    POSITION_LOWER
};

static void  open_selected_bookmark                    (NemoPlacesSidebar        *sidebar,
							GtkTreeModel                 *model,
							GtkTreeIter                  *iter,
							NemoWindowOpenFlags flags);
static void  nemo_places_sidebar_style_set         (GtkWidget                    *widget,
							GtkStyle                     *previous_style);
static gboolean eject_or_unmount_bookmark              (NemoPlacesSidebar *sidebar,
							GtkTreePath *path);
static gboolean eject_or_unmount_selection             (NemoPlacesSidebar *sidebar);
static void  check_unmount_and_eject                   (GMount *mount,
							GVolume *volume,
							GDrive *drive,
							gboolean *show_unmount,
							gboolean *show_eject);

static void update_places                              (NemoPlacesSidebar *sidebar);
static gchar *dakkar_sidebar_item_id (GtkTreeModel *model, GtkTreeIter *iter);
static void dakkar_sidebar_save_item_icon (const gchar *id, const gchar *spec);
static void update_places_on_idle                      (NemoPlacesSidebar *sidebar);
static void rebuild_menu                               (NemoPlacesSidebar *sidebar);
static void actions_changed                            (gpointer user_data);
/* Identifiers for target types */
enum {
  GTK_TREE_MODEL_ROW,
  TEXT_URI_LIST
};

/* Target types for dragging from the shortcuts list */
static const GtkTargetEntry nemo_shortcuts_source_targets[] = {
	{ (char *)"GTK_TREE_MODEL_ROW", GTK_TARGET_SAME_WIDGET, GTK_TREE_MODEL_ROW }
};

/* Target types for dropping into the shortcuts list */
static const GtkTargetEntry nemo_shortcuts_drop_targets [] = {
	{ (char *)"GTK_TREE_MODEL_ROW", GTK_TARGET_SAME_WIDGET, GTK_TREE_MODEL_ROW },
	{ (char *)"text/uri-list", 0, TEXT_URI_LIST }
};

/* Drag and drop interface declarations */
typedef struct {
  GtkTreeStore parent;

  NemoPlacesSidebar *sidebar;
} NemoShortcutsModel;

typedef struct {
  GtkTreeStoreClass parent_class;
} NemoShortcutsModelClass;

#define NEMO_TYPE_SHORTCUTS_MODEL (_nemo_shortcuts_model_get_type ())
#define NEMO_SHORTCUTS_MODEL(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), NEMO_SHORTCUTS_MODEL_TYPE, NemoShortcutsModel))

GType _nemo_shortcuts_model_get_type (void);
static void _nemo_shortcuts_model_drag_source_init (GtkTreeDragSourceIface *iface);
G_DEFINE_TYPE_WITH_CODE (NemoShortcutsModel, _nemo_shortcuts_model, GTK_TYPE_TREE_STORE,
			 G_IMPLEMENT_INTERFACE (GTK_TYPE_TREE_DRAG_SOURCE,
						_nemo_shortcuts_model_drag_source_init));
static GtkTreeStore *nemo_shortcuts_model_new (NemoPlacesSidebar *sidebar);

G_DEFINE_TYPE (NemoPlacesSidebar, nemo_places_sidebar, GTK_TYPE_SCROLLED_WINDOW);

static void
breakpoint_changed_cb (NemoPlacesSidebar *sidebar)
{
    sidebar->bookmark_breakpoint = g_settings_get_int (nemo_window_state, NEMO_PREFERENCES_SIDEBAR_BOOKMARK_BREAKPOINT);
    update_places (sidebar);
}

static void
increment_bookmark_breakpoint (NemoPlacesSidebar *sidebar)
{
    g_signal_handlers_block_by_func (nemo_window_state, breakpoint_changed_cb, sidebar);

    sidebar->bookmark_breakpoint ++;
    g_settings_set_int (nemo_window_state, NEMO_PREFERENCES_SIDEBAR_BOOKMARK_BREAKPOINT, sidebar->bookmark_breakpoint);

    g_signal_handlers_unblock_by_func (nemo_window_state, breakpoint_changed_cb, sidebar);
}

static void
decrement_bookmark_breakpoint (NemoPlacesSidebar *sidebar)
{
    g_signal_handlers_block_by_func (nemo_window_state, breakpoint_changed_cb, sidebar);

    sidebar->bookmark_breakpoint --;
    g_settings_set_int (nemo_window_state, NEMO_PREFERENCES_SIDEBAR_BOOKMARK_BREAKPOINT, sidebar->bookmark_breakpoint);

    g_signal_handlers_unblock_by_func (nemo_window_state, breakpoint_changed_cb, sidebar);
}

static gboolean
should_show_desktop (void)
{
	return g_settings_get_boolean (nemo_desktop_preferences, NEMO_PREFERENCES_SHOW_DESKTOP) &&
	       !g_settings_get_boolean (nemo_preferences, NEMO_PREFERENCES_DESKTOP_IS_HOME_DIR);
}

static gboolean
is_built_in_bookmark (NemoFile *file)
{
	if (nemo_file_is_home (file)) {
		return TRUE;
	}

    if (nemo_file_is_desktop_directory (file) && should_show_desktop ())
        return TRUE;
    else
	    return FALSE;
}

static GtkTreeIter
add_heading (NemoPlacesSidebar *sidebar,
	     SectionType section_type,
	     const gchar *title)
{
	GtkTreeIter cat_iter;

	gtk_tree_store_append (sidebar->store, &cat_iter, NULL);
	gtk_tree_store_set (sidebar->store, &cat_iter,
			    PLACES_SIDEBAR_COLUMN_ROW_TYPE, PLACES_HEADING,
			    PLACES_SIDEBAR_COLUMN_SECTION_TYPE, section_type,
			    PLACES_SIDEBAR_COLUMN_HEADING_TEXT, title,
                PLACES_SIDEBAR_COLUMN_INDEX, -1,
			    PLACES_SIDEBAR_COLUMN_EJECT, FALSE,
			    PLACES_SIDEBAR_COLUMN_NO_EJECT, TRUE,
			    -1);

	return cat_iter;
}

static GtkTreeIter
check_heading_for_devices (NemoPlacesSidebar *sidebar,
                           SectionType section_type,
			               GtkTreeIter cat_iter)
{
    if (section_type == SECTION_DEVICES) {
        if (!sidebar->devices_header_added) {
            cat_iter = add_heading (sidebar, SECTION_DEVICES,
                        _("Devices"));
            sidebar->devices_header_added = TRUE;
        }
    }
    return cat_iter;
}

typedef struct {
       PlaceType place_type;
       SectionType section_type;
       gchar *name;
       gchar *icon_name;
       gchar *uri;
       GDrive *drive;
       GVolume *volume;
       GMount *mount;
       gint index;
       gchar *tooltip;
       gint df_percent;
       gboolean show_df_percent;
} PlaceInfo;

static gint
sort_places_func (gconstpointer a,
                  gconstpointer b)
{
    return g_utf8_collate (((PlaceInfo *) a)->name, ((PlaceInfo *) b)->name);
}

static PlaceInfo *
new_place_info (PlaceType place_type,
                SectionType section_type,
                gchar *name,
                gchar *icon_name,
                gchar *uri,
                GDrive *drive,
                GVolume *volume,
                GMount *mount,
                gint index,
                gchar *tooltip,
                gint df_percent,
                gboolean show_df_percent)
{
    PlaceInfo *info = g_new0 (PlaceInfo, 1);

    info->place_type = place_type;
    info->section_type = section_type;
    info->name = g_utf8_make_valid (name, -1);
    info->icon_name = g_strdup (icon_name);
    info->uri = (g_strdup (uri));
    info->drive = drive ? g_object_ref (drive) : NULL;
    info->volume = volume ? g_object_ref (volume) : NULL;
    info->mount = mount ? g_object_ref (mount) : NULL;
    info->index = index;
    info->tooltip = g_strdup (tooltip);
    info->df_percent = df_percent;
    info->show_df_percent = show_df_percent;

    return info;
}

static void
free_place_info (PlaceInfo *info)
{
    g_free (info->name);
    g_free (info->icon_name);
    g_free (info->uri);
    g_clear_object (&info->drive);
    g_clear_object (&info->volume);
    g_clear_object (&info->mount);
    g_free (info->tooltip);

    g_free (info);
}

static GtkTreeIter
add_place (NemoPlacesSidebar *sidebar,
	   PlaceType place_type,
	   SectionType section_type,
	   const char *name,
	   const char *icon_name,
	   const char *uri,
	   GDrive *drive,
	   GVolume *volume,
	   GMount *mount,
	   int index,
	   const char *tooltip,
       int df_percent,
       gboolean show_df_percent,
       GtkTreeIter cat_iter)
{
	GtkTreeIter           iter;
    GIcon *gicon;
	gboolean show_eject, show_unmount;
	gboolean show_eject_button;

	cat_iter = check_heading_for_devices (sidebar, section_type, cat_iter);

	check_unmount_and_eject (mount, volume, drive,
				 &show_unmount, &show_eject);

	if (show_unmount || show_eject) {
		g_assert (place_type != PLACES_BOOKMARK);
	}

	if (mount == NULL) {
		show_eject_button = FALSE;
	} else {
		show_eject_button = (show_unmount || show_eject);
	}

    gicon = (icon_name != NULL) ? g_themed_icon_new (icon_name) : NULL;

	gtk_tree_store_append (sidebar->store, &iter, &cat_iter);
	gtk_tree_store_set (sidebar->store, &iter,
			    PLACES_SIDEBAR_COLUMN_GICON, gicon,
			    PLACES_SIDEBAR_COLUMN_NAME, name,
			    PLACES_SIDEBAR_COLUMN_URI, uri,
			    PLACES_SIDEBAR_COLUMN_DRIVE, drive,
			    PLACES_SIDEBAR_COLUMN_VOLUME, volume,
			    PLACES_SIDEBAR_COLUMN_MOUNT, mount,
			    PLACES_SIDEBAR_COLUMN_ROW_TYPE, place_type,
			    PLACES_SIDEBAR_COLUMN_INDEX, index,
			    PLACES_SIDEBAR_COLUMN_EJECT, show_eject_button,
			    PLACES_SIDEBAR_COLUMN_NO_EJECT, !show_eject_button,
			    PLACES_SIDEBAR_COLUMN_BOOKMARK, place_type != PLACES_BOOKMARK,
                PLACES_SIDEBAR_COLUMN_TOOLTIP, tooltip,
                PLACES_SIDEBAR_COLUMN_EJECT_ICON, show_eject_button ? "media-eject-symbolic" : NULL,
			    PLACES_SIDEBAR_COLUMN_EJECT_ICON_SIZE, EJECT_ICON_SIZE_NOT_HOVERED,
			    PLACES_SIDEBAR_COLUMN_SECTION_TYPE, section_type,
                PLACES_SIDEBAR_COLUMN_DF_PERCENT, df_percent,
                PLACES_SIDEBAR_COLUMN_SHOW_DF, show_df_percent,
			    -1);

    g_clear_object (&gicon);

    return cat_iter;
}

typedef struct {
	const gchar *location;
	const gchar *last_uri;
	NemoPlacesSidebar *sidebar;
	GtkTreePath *path;
} RestoreLocationData;

static gboolean
restore_selection_foreach (GtkTreeModel *model,
			   GtkTreePath *path,
			   GtkTreeIter *iter,
			   gpointer user_data)
{
	RestoreLocationData *data = user_data;
	gchar *uri;

	gtk_tree_model_get (model, iter,
			    PLACES_SIDEBAR_COLUMN_URI, &uri,
			    -1);
	if (g_strcmp0 (uri, data->last_uri) == 0 ||
	    g_strcmp0 (uri, data->location) == 0) {
		data->path = gtk_tree_path_copy (path);
	}

	g_free (uri);

	return (data->path != NULL);
}

static gboolean
restore_expand_state_foreach (GtkTreeModel *model,
                              GtkTreePath *path,
                              GtkTreeIter *iter,
                              gpointer user_data)
{
    PlaceType place_type;
    SectionType section_type;
    NemoPlacesSidebar *sidebar = NEMO_PLACES_SIDEBAR (user_data);

    gtk_tree_model_get (model, iter,
                PLACES_SIDEBAR_COLUMN_ROW_TYPE, &place_type,
                PLACES_SIDEBAR_COLUMN_SECTION_TYPE, &section_type,
                -1);

    if (place_type == PLACES_HEADING) {
        if (section_type == SECTION_COMPUTER) {
            if (sidebar->my_computer_expanded)
                gtk_tree_view_expand_to_path (sidebar->tree_view, path);
            else
                gtk_tree_view_collapse_row (sidebar->tree_view, path);
        } else if (section_type == SECTION_FAVORITES) {
            if (sidebar->favorites_expanded)
                gtk_tree_view_expand_to_path (sidebar->tree_view, path);
            else
                gtk_tree_view_collapse_row (sidebar->tree_view, path);
        } else if (section_type == SECTION_BOOKMARKS) {
            if (sidebar->bookmarks_expanded)
                gtk_tree_view_expand_to_path (sidebar->tree_view, path);
            else
                gtk_tree_view_collapse_row (sidebar->tree_view, path);
        } else if (section_type == SECTION_DEVICES) {
            if (sidebar->devices_expanded)
                gtk_tree_view_expand_to_path (sidebar->tree_view, path);
            else
                gtk_tree_view_collapse_row (sidebar->tree_view, path);
        } else if (section_type == SECTION_NETWORK) {
            if (sidebar->network_expanded)
                gtk_tree_view_expand_to_path (sidebar->tree_view, path);
            else
                gtk_tree_view_collapse_row (sidebar->tree_view, path);
        }
    }
    return FALSE;
}

static void
restore_expand_state (NemoPlacesSidebar *sidebar)
{
    gtk_tree_model_foreach (GTK_TREE_MODEL (sidebar->store_filter),
                restore_expand_state_foreach, sidebar);
}


static void expand_or_collapse_category (NemoPlacesSidebar *sidebar,
                             SectionType section_type, gboolean expand)
{
    switch (section_type) {
        case SECTION_COMPUTER:
            sidebar->my_computer_expanded = expand;
            break;
        case SECTION_FAVORITES:
            sidebar->favorites_expanded = expand;
            break;
        case SECTION_BOOKMARKS:
            sidebar->bookmarks_expanded = expand;
            break;
        case SECTION_DEVICES:
            sidebar->devices_expanded = expand;
            break;
        case SECTION_NETWORK:
            sidebar->network_expanded = expand;
            break;
        case SECTION_XDG_BOOKMARKS:
        default:
            break;
    }

    restore_expand_state (sidebar);
}

static void
sidebar_update_restore_selection (NemoPlacesSidebar *sidebar,
				  const gchar *location,
				  const gchar *last_uri)
{
	RestoreLocationData data;
	GtkTreeSelection *selection;

	data.location = location;
	data.last_uri = last_uri;
	data.sidebar = sidebar;
	data.path = NULL;

	gtk_tree_model_foreach (GTK_TREE_MODEL (sidebar->store_filter),
				restore_selection_foreach, &data);

	if (data.path != NULL) {
		selection = gtk_tree_view_get_selection (sidebar->tree_view);
		gtk_tree_selection_select_path (selection, data.path);
		gtk_tree_path_free (data.path);
	}
}

static gint
get_disk_full (GFile *file, gchar **tooltip_info)
{
    GFileInfo *info;
    GError *error;
    guint64 k_used, k_total, k_free;
    gint df_percent;
    float fraction;
    int prefix;
    gchar *size_string;
    gchar *out_string;

    error = NULL;
    df_percent = -1;
    out_string = NULL;

    info = g_file_query_filesystem_info (file,
                                         "filesystem::*",
                                         NULL,
                                         &error);

    if (info != NULL) {
        k_used = g_file_info_get_attribute_uint64 (info, G_FILE_ATTRIBUTE_FILESYSTEM_USED);
        k_total = g_file_info_get_attribute_uint64 (info, G_FILE_ATTRIBUTE_FILESYSTEM_SIZE);
        k_free = g_file_info_get_attribute_uint64 (info, G_FILE_ATTRIBUTE_FILESYSTEM_FREE);

        if (k_total > 0) {
            fraction = ((float) k_used / (float) k_total) * 100.0;

            df_percent = (gint) rintf(fraction);

            prefix = nemo_global_preferences_get_size_prefix_preference ();
            size_string = g_format_size_full (k_free, prefix);

            out_string = g_strdup_printf (_("Free space: %s"), size_string);

            g_free (size_string);
        }

        g_object_unref (info);
    }

    if (error != NULL) {
        g_warning ("Couldn't get disk full info for: %s", error->message);
        g_clear_error (&error);
    }

    if (out_string == NULL) {
        out_string = g_strdup (" ");
    }

    *tooltip_info = out_string;

    return df_percent;
}

static gboolean
home_on_different_fs (const gchar *home_uri)
{
    GFile *home = g_file_new_for_uri (home_uri);
    GFile *root = g_file_new_for_uri ("file:///");
    GFileInfo *home_info, *root_info;
    const gchar *home_id, *root_id;
    gboolean res;

    res = FALSE;
    home_info = g_file_query_info (home,
                                   G_FILE_ATTRIBUTE_ID_FILESYSTEM,
                                   G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
                                   NULL, NULL);
    root_info = g_file_query_info (root,
                                   G_FILE_ATTRIBUTE_ID_FILESYSTEM,
                                   G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
                                   NULL, NULL);

    if (home_info && root_info) {
        home_id = g_file_info_get_attribute_string (home_info, G_FILE_ATTRIBUTE_ID_FILESYSTEM);
        root_id = g_file_info_get_attribute_string (root_info, G_FILE_ATTRIBUTE_ID_FILESYSTEM);
        res = g_strcmp0 (home_id, root_id) != 0;
        g_object_unref (home_info);
        g_object_unref (root_info);
    } else {
        if (home_info)
            g_object_unref (home_info);
        if (root_info)
            g_object_unref (root_info);
    }
    g_object_unref (home);
    g_object_unref (root);
    return res;
}

static gchar *
get_icon_name (const gchar *uri)
{
    NemoFile *file = nemo_file_get_by_uri (uri);
    gchar *icon_name;

    icon_name = nemo_file_get_control_icon_name (file);
    nemo_file_unref (file);

    return icon_name;
}

static gboolean dakkar_is_favorites_row (PlaceType type, SectionType section);
static gchar *dakkar_selected_favorite_uri (NemoPlacesSidebar *sidebar);

static void
update_places (NemoPlacesSidebar *sidebar)
{
	GtkTreeSelection *selection;
	GtkTreeIter last_iter, cat_iter;
	GtkTreeModel *model;
	GVolumeMonitor *volume_monitor;
	GList *mounts, *l, *ll;
	GMount *mount;
	GList *drives;
	GDrive *drive;
	GList *volumes;
	GVolume *volume;
	char *location, *mount_uri, *name, *last_uri, *identifier;
	gchar *icon;
	GFile *root, *df_file;
	NemoWindowSlot *slot;
	char *tooltip;
    gchar *tooltip_info;
	GList *network_mounts, *network_volumes;
    gint full;

	DEBUG ("Updating places sidebar");

    sidebar->updating_sidebar = TRUE;

	model = NULL;
	last_uri = NULL;

    g_clear_pointer (&sidebar->top_bookend_uri, g_free);
    g_clear_pointer (&sidebar->bottom_bookend_uri, g_free);

	selection = gtk_tree_view_get_selection (sidebar->tree_view);
	if (gtk_tree_selection_get_selected (selection, &model, &last_iter)) {
		gtk_tree_model_get (model,
				    &last_iter,
				    PLACES_SIDEBAR_COLUMN_URI, &last_uri, -1);
	}
	gtk_tree_store_clear (sidebar->store);

	sidebar->devices_header_added = FALSE;
	sidebar->bookmarks_header_added = FALSE;

	slot = nemo_window_get_active_slot (sidebar->window);
	location = nemo_window_slot_get_current_uri (slot);

	network_mounts = network_volumes = NULL;
	volume_monitor = sidebar->volume_monitor;

    cat_iter = add_heading (sidebar, SECTION_COMPUTER,
                                    _("My Computer"));
    /* add built in bookmarks */

    /* home folder */
    mount_uri = nemo_get_home_directory_uri ();
    icon = get_icon_name (mount_uri);

    df_file = g_file_new_for_uri (mount_uri);
    full = get_disk_full (df_file, &tooltip_info);
    g_clear_object (&df_file);

    tooltip = g_strdup_printf (_("Open your personal folder\n%s"), tooltip_info);
    g_free (tooltip_info);
    cat_iter = add_place (sidebar, PLACES_BUILT_IN,
                           SECTION_COMPUTER,
                           _("Home"), icon,
                           mount_uri, NULL, NULL, NULL, 0,
                           tooltip,
                           full, home_on_different_fs (mount_uri) && full > -1,
                           cat_iter);
    g_free (icon);
    sidebar->top_bookend_uri = g_strdup (mount_uri);
    g_free (mount_uri);
    g_free (tooltip);

    /* Actual bookmarks are displayed below the unified Favorites heading. */

    gboolean recent_enabled;
    recent_enabled = g_settings_get_boolean (cinnamon_privacy_preferences,
                                             NEMO_PREFERENCES_RECENT_ENABLED);

    if (recent_enabled && eel_vfs_supports_uri_scheme ("recent")) {
        mount_uri = (char *)"recent:///"; /* No need to strdup */
        icon = NEMO_ICON_SYMBOLIC_FOLDER_RECENT;
        cat_iter = add_place (sidebar, PLACES_BUILT_IN,
                              SECTION_COMPUTER,
                              _("Recent"), icon, mount_uri,
                              NULL, NULL, NULL, 0,
                              _("Recent files"), 0, FALSE, cat_iter);

        if (sidebar->bottom_bookend_uri == NULL) {
            sidebar->bottom_bookend_uri = g_strdup (mount_uri);
        }
    }

    /* file system root */
    mount_uri = (char *)"file:///"; /* No need to strdup */
    icon = NEMO_ICON_SYMBOLIC_FILESYSTEM;

    df_file = g_file_new_for_uri (mount_uri);
    full = get_disk_full (df_file, &tooltip_info);
    g_clear_object (&df_file);

    tooltip = g_strdup_printf (_("Open the contents of the File System\n%s"), tooltip_info);
    g_free (tooltip_info);
    cat_iter = add_place (sidebar, PLACES_BUILT_IN,
                           SECTION_COMPUTER,
                           _("File System"), icon,
                           mount_uri, NULL, NULL, NULL, 0,
                           tooltip,
                           full, full > -1,
                           cat_iter);
    g_free (tooltip);

    if (sidebar->bottom_bookend_uri == NULL) {
        sidebar->bottom_bookend_uri = g_strdup (mount_uri);
    }

    if (eel_vfs_supports_uri_scheme("trash")) {
        mount_uri = (char *)"trash:///"; /* No need to strdup */
        icon = nemo_trash_monitor_get_symbolic_icon_name ();
        cat_iter = add_place (sidebar, PLACES_BUILT_IN,
                               SECTION_COMPUTER,
                               _("Trash"), icon, mount_uri,
                               NULL, NULL, NULL, 0,
                               _("Open the trash"), 0, FALSE,
                               cat_iter);
        g_free (icon);
    }

    /* One Favorites section: XApp favorites, the standard folders and the
     * GTK bookmarks, in the single display order the user chose (sidebar
     * Move Up/Down, drag and drop, Preferences > Favorites).  Each item
     * keeps its own storage; see dakkar-favorites.c. */
    {
        GList *favs, *fl;

        cat_iter = add_heading (sidebar, SECTION_FAVORITES, _("Favorites"));
        favs = dakkar_favorites_collect ();
        for (fl = favs; fl != NULL; fl = fl->next) {
            DakkarFavorite *fav = fl->data;
            switch (fav->kind) {
            case DAKKAR_FAVORITE_XAPP:
                cat_iter = add_place (sidebar, PLACES_FAVORITE, SECTION_FAVORITES,
                                      fav->name, fav->icon_name != NULL ? fav->icon_name : "text-x-generic",
                                      fav->uri, NULL, NULL, NULL, 0,
                                      fav->uri, 0, FALSE, cat_iter);
                break;
            case DAKKAR_FAVORITE_XDG:
                cat_iter = add_place (sidebar, PLACES_XDG_DIR, SECTION_FAVORITES,
                                      fav->name, fav->icon_name, fav->uri,
                                      NULL, NULL, NULL, 0,
                                      fav->tooltip, 0, FALSE, cat_iter);
                break;
            case DAKKAR_FAVORITE_BOOKMARK:
            default:
                cat_iter = add_place (sidebar, PLACES_BOOKMARK, SECTION_BOOKMARKS,
                                      fav->name, fav->icon_name, fav->uri,
                                      NULL, NULL, NULL, fav->bookmark_index,
                                      fav->tooltip, 0, FALSE, cat_iter);
                break;
            }
        }
        g_list_free_full (favs, (GDestroyNotify) dakkar_favorite_free);
    }

    GList *place_infos = NULL;
    PlaceInfo *place_info;

    /* add mounts that has no volume (/etc/mtab mounts, ftp, sftp,...) */
    mounts = g_volume_monitor_get_mounts (volume_monitor);

    for (l = mounts; l != NULL; l = l->next) {
        mount = l->data;
        if (g_mount_is_shadowed (mount)) {
            g_object_unref (mount);
            continue;
        }
        volume = g_mount_get_volume (mount);
        if (volume != NULL) {
                g_object_unref (volume);
            g_object_unref (mount);
            continue;
        }
        root = g_mount_get_default_location (mount);

        if (!g_file_is_native (root)) {
            gboolean really_network = TRUE;
            gchar *path = g_file_get_path (root);
            if (!path) {
                network_mounts = g_list_prepend (network_mounts, mount);
                g_object_unref (root);
                continue;
            }
            gchar *escaped1 = g_uri_unescape_string (path, "");
            gchar *escaped2 = g_uri_unescape_string (escaped1, "");
            gchar *ptr = g_strrstr (escaped2, "file://");
            if (ptr != NULL) {
                GFile *actual_file = g_file_new_for_uri (ptr);
                if (g_file_is_native(actual_file)) {
                    really_network = FALSE;
                }
                g_object_unref(actual_file);
            }
            g_free (path);
            g_free (escaped1);
            g_free (escaped2);
            if (really_network) {
                network_mounts = g_list_prepend (network_mounts, mount);
                g_object_unref (root);
                continue;
            }
        }

        icon = nemo_get_mount_icon_name (mount);
        mount_uri = g_file_get_uri (root);
        name = g_mount_get_name (mount);
        tooltip = g_file_get_parse_name (root);
        place_info = new_place_info (PLACES_MOUNTED_VOLUME,
                                     SECTION_DEVICES,
                                     name, icon, mount_uri,
                                     NULL, NULL, mount, 0, tooltip, 0, FALSE);
        place_infos = g_list_prepend (place_infos, place_info);
        g_object_unref (root);
        g_object_unref (mount);
        g_free (icon);
        g_free (name);
        g_free (mount_uri);
        g_free (tooltip);

    }
    g_list_free (mounts);

    /* first go through all connected drives */
    drives = g_volume_monitor_get_connected_drives (volume_monitor);

    for (l = drives; l != NULL; l = l->next) {
        drive = l->data;

        volumes = g_drive_get_volumes (drive);
        if (volumes != NULL) {
            for (ll = volumes; ll != NULL; ll = ll->next) {
                volume = ll->data;
                identifier = g_volume_get_identifier (volume, G_VOLUME_IDENTIFIER_KIND_CLASS);

                if (g_strcmp0 (identifier, "network") == 0) {
                    g_free (identifier);
                    network_volumes = g_list_prepend (network_volumes, volume);
                    continue;
                }
                g_free (identifier);

                mount = g_volume_get_mount (volume);
                if (mount != NULL) {
                    gchar *full_display_name, *volume_id;
                    /* Show mounted volume in the sidebar */
                    icon = nemo_get_mount_icon_name (mount);
                    root = g_mount_get_default_location (mount);
                    mount_uri = g_file_get_uri (root);
                    name = g_mount_get_name (mount);

                    volume_id = g_volume_get_identifier (volume, G_VOLUME_IDENTIFIER_KIND_UNIX_DEVICE);
                    full_display_name = g_file_get_parse_name (root);

                    df_file = g_file_new_for_uri (mount_uri);
                    full = get_disk_full (df_file, &tooltip_info);
                    g_clear_object (&df_file);

                    tooltip = g_strdup_printf (_("%s (%s)\n%s"),
                                               full_display_name,
                                               volume_id,
                                               tooltip_info);
                    g_free (tooltip_info);
                    place_info = new_place_info (PLACES_MOUNTED_VOLUME,
                                                 SECTION_DEVICES,
                                                 name, icon, mount_uri,
                                                 drive, volume, mount, 0, tooltip, full, full > -1);
                    place_infos = g_list_prepend (place_infos, place_info);
                    g_object_unref (root);
                    g_object_unref (mount);
                    g_free (icon);
                    g_free (tooltip);
                    g_free (name);
                    g_free (mount_uri);
                    g_free (volume_id);
                    g_free (full_display_name);
                } else {
                    /* Do show the unmounted volumes in the sidebar;
                     * this is so the user can mount it (in case automounting
                     * is off).
                     *
                     * Also, even if automounting is enabled, this gives a visual
                     * cue that the user should remember to yank out the media if
                     * he just unmounted it.
                     */
                    gchar *volume_id;
                    icon = nemo_get_volume_icon_name (volume);
                    name = g_volume_get_name (volume);

                    volume_id = g_volume_get_identifier (volume,
                                                         G_VOLUME_IDENTIFIER_KIND_UNIX_DEVICE);
                    tooltip = g_strdup_printf (_("Mount and open %s (%s)"), name, volume_id);

                    place_info = new_place_info (PLACES_MOUNTED_VOLUME,
                                                 SECTION_DEVICES,
                                                 name, icon, NULL,
                                                 drive, volume, NULL, 0, tooltip, 0, FALSE);
                    place_infos = g_list_prepend (place_infos, place_info);

                    g_free (icon);
                    g_free (name);
                    g_free (tooltip);
                    g_free (volume_id);
                }
                g_object_unref (volume);
            }
            g_list_free (volumes);
        } else {
            if (g_drive_is_removable (drive) && !g_drive_is_media_check_automatic (drive)) {
                /* If the drive has no mountable volumes and we cannot detect media change.. we
                 * display the drive in the sidebar so the user can manually poll the drive by
                 * right clicking and selecting "Rescan..."
                 *
                 * This is mainly for drives like floppies where media detection doesn't
                 * work.. but it's also for human beings who like to turn off media detection
                 * in the OS to save battery juice.
                 */
                icon = nemo_get_drive_icon_name (drive);
                name = g_drive_get_name (drive);
                tooltip = g_strdup_printf (_("Mount and open %s"), name);

                place_info = new_place_info (PLACES_BUILT_IN,
                                             SECTION_DEVICES,
                                             name, icon, NULL,
                                             drive, NULL, NULL, 0, tooltip, 0, FALSE);
                place_infos = g_list_prepend (place_infos, place_info);

                g_free (icon);
                g_free (tooltip);
                g_free (name);
            }
        }
        g_object_unref (drive);
    }
    g_list_free (drives);

    /* add all volumes that is not associated with a drive */
    volumes = g_volume_monitor_get_volumes (volume_monitor);
    for (l = volumes; l != NULL; l = l->next) {
        volume = l->data;
        drive = g_volume_get_drive (volume);
        if (drive != NULL) {
                g_object_unref (volume);
            g_object_unref (drive);
            continue;
        }

        identifier = g_volume_get_identifier (volume, G_VOLUME_IDENTIFIER_KIND_CLASS);

        if (g_strcmp0 (identifier, "network") == 0) {
            g_free (identifier);
            network_volumes = g_list_prepend (network_volumes, volume);
            continue;
        }
        g_free (identifier);

        mount = g_volume_get_mount (volume);
        if (mount != NULL) {
            g_autofree gchar *parse_name = NULL;
            icon = nemo_get_mount_icon_name (mount);
            root = g_mount_get_default_location (mount);
            mount_uri = g_file_get_uri (root);

            df_file = g_file_new_for_uri (mount_uri);
            full = get_disk_full (df_file, &tooltip_info);
            g_clear_object (&df_file);

            parse_name = g_file_get_parse_name (root);
            tooltip = g_strdup_printf (_("%s\n%s"), parse_name, tooltip_info);

            g_free (tooltip_info);
            g_object_unref (root);
            name = g_mount_get_name (mount);

            place_info = new_place_info (PLACES_MOUNTED_VOLUME,
                                         SECTION_DEVICES,
                                         name, icon, mount_uri,
                                         NULL, volume, mount, 0, tooltip, full, full > -1);
            place_infos = g_list_prepend (place_infos, place_info);

            g_object_unref (mount);
            g_free (icon);
            g_free (name);
            g_free (tooltip);
            g_free (mount_uri);
        } else {
            /* see the comment above on why we add an icon for an unmounted mountable volume */
            icon = nemo_get_volume_icon_name (volume);
            name = g_volume_get_name (volume);

            place_info = new_place_info (PLACES_MOUNTED_VOLUME,
                                         SECTION_DEVICES,
                                         name, icon, NULL,
                                         NULL, volume, NULL, 0, name, 0, FALSE);
            place_infos = g_list_prepend (place_infos, place_info);

            g_free (icon);
            g_free (name);
        }
        g_object_unref (volume);
    }
    g_list_free (volumes);

    place_infos = g_list_sort (place_infos, (GCompareFunc) sort_places_func);

    for (l = place_infos; l != NULL; l = l->next) {
        PlaceInfo *info = (PlaceInfo *) l->data;

        cat_iter = add_place (sidebar,
                              info->place_type,
                              info->section_type,
                              info->name,
                              info->icon_name,
                              info->uri,
                              info->drive,
                              info->volume,
                              info->mount,
                              info->index,
                              info->tooltip,
                              info->df_percent,
                              info->show_df_percent,
                              cat_iter);

        free_place_info (info);
    }

    g_list_free (place_infos);

	/* network */
	cat_iter = add_heading (sidebar, SECTION_NETWORK,
		     _("Network"));

	network_volumes = g_list_reverse (network_volumes);
	for (l = network_volumes; l != NULL; l = l->next) {
		volume = l->data;
		mount = g_volume_get_mount (volume);

		if (mount != NULL) {
			network_mounts = g_list_prepend (network_mounts, mount);
			continue;
		} else {
			icon = nemo_get_volume_icon_name (volume);
			name = g_volume_get_name (volume);
			tooltip = g_strdup_printf (_("Mount and open %s"), name);

			cat_iter = add_place (sidebar, PLACES_MOUNTED_VOLUME,
                				   SECTION_NETWORK,
                				   name, icon, NULL,
                				   NULL, volume, NULL, 0, tooltip, 0, FALSE,
                                   cat_iter);
			g_free (icon);
			g_free (name);
			g_free (tooltip);
		}
	}

	g_list_free_full (network_volumes, g_object_unref);

	network_mounts = g_list_reverse (network_mounts);
	for (l = network_mounts; l != NULL; l = l->next) {
		mount = l->data;
		root = g_mount_get_default_location (mount);
		icon = nemo_get_mount_icon_name (mount);
		mount_uri = g_file_get_uri (root);
		name = g_mount_get_name (mount);
		tooltip = g_file_get_parse_name (root);
		cat_iter = add_place (sidebar, PLACES_MOUNTED_VOLUME,
                			   SECTION_NETWORK,
                			   name, icon, mount_uri,
                			   NULL, NULL, mount, 0, tooltip, 0, FALSE,
                               cat_iter);
		g_object_unref (root);
		g_free (icon);
		g_free (name);
		g_free (mount_uri);
		g_free (tooltip);
	}

	g_list_free_full (network_mounts, g_object_unref);

	/* network:// */
 	mount_uri = (char *)"network:///"; /* No need to strdup */
	icon = NEMO_ICON_SYMBOLIC_NETWORK;
	cat_iter = add_place (sidebar, PLACES_BUILT_IN,
                		   SECTION_NETWORK,
                		   _("Network"), icon,
                		   mount_uri, NULL, NULL, NULL, 0,
                		   _("Browse the contents of the network"), 0, FALSE,
                           cat_iter);

	/* restore selection */
    restore_expand_state (sidebar);
	sidebar_update_restore_selection (sidebar, location, last_uri);

    actions_changed (sidebar);

    sidebar->updating_sidebar = FALSE;

	g_free (location);
	g_free (last_uri);
}

static void
mount_added_callback (GVolumeMonitor *volume_monitor,
		      GMount *mount,
		      NemoPlacesSidebar *sidebar)
{
	update_places_on_idle (sidebar);
}

static void
mount_removed_callback (GVolumeMonitor *volume_monitor,
			GMount *mount,
			NemoPlacesSidebar *sidebar)
{
	update_places_on_idle (sidebar);
}

static void
mount_changed_callback (GVolumeMonitor *volume_monitor,
			GMount *mount,
			NemoPlacesSidebar *sidebar)
{
	update_places_on_idle (sidebar);
}

static void
volume_added_callback (GVolumeMonitor *volume_monitor,
		       GVolume *volume,
		       NemoPlacesSidebar *sidebar)
{
	update_places_on_idle (sidebar);
}

static void
volume_removed_callback (GVolumeMonitor *volume_monitor,
			 GVolume *volume,
			 NemoPlacesSidebar *sidebar)
{
	update_places_on_idle (sidebar);
}

static void
volume_changed_callback (GVolumeMonitor *volume_monitor,
			 GVolume *volume,
			 NemoPlacesSidebar *sidebar)
{
	update_places_on_idle (sidebar);
}

static void
drive_disconnected_callback (GVolumeMonitor *volume_monitor,
			     GDrive         *drive,
			     NemoPlacesSidebar *sidebar)
{
	update_places_on_idle (sidebar);
}

static void
drive_connected_callback (GVolumeMonitor *volume_monitor,
			  GDrive         *drive,
			  NemoPlacesSidebar *sidebar)
{
	update_places_on_idle (sidebar);
}

static void
drive_changed_callback (GVolumeMonitor *volume_monitor,
			GDrive         *drive,
			NemoPlacesSidebar *sidebar)
{
	update_places_on_idle (sidebar);
}

static gboolean
over_eject_button (NemoPlacesSidebar *sidebar,
		   gint x,
		   gint y,
		   GtkTreePath **path)
{
    GtkTreeViewColumn *column;
    int width, col_x_offset, cell_x_offset;
    gboolean show_eject;
    GtkTreeIter iter;
    GtkTreeModel *model;

    *path = NULL;
    model = gtk_tree_view_get_model (sidebar->tree_view);

    if (gtk_tree_view_get_path_at_pos (sidebar->tree_view,
                                       x, y,
                                       path, &column, &col_x_offset, NULL)) {

        gtk_tree_model_get_iter (model, &iter, *path);
        gtk_tree_model_get (model, &iter,
                            PLACES_SIDEBAR_COLUMN_EJECT, &show_eject,
                            -1);

        if (!show_eject) {
            goto out;
        }

        if (column == sidebar->eject_column) {
            gtk_tree_view_column_cell_set_cell_data (column, model, &iter, FALSE, FALSE);

            gtk_tree_view_column_cell_get_position (column,
                                                    sidebar->eject_icon_cell_renderer,
                                                    &cell_x_offset, &width);
            if ((col_x_offset >= cell_x_offset) && (col_x_offset < cell_x_offset + width)) {
                return TRUE;
            }
        }
    }

    out:
    if (*path != NULL) {
        gtk_tree_path_free (*path);
        *path = NULL;
    }

    return FALSE;
}

static gboolean
clicked_eject_button (NemoPlacesSidebar *sidebar,
		      GtkTreePath **path)
{
	GdkEvent *event;

	event = gtk_get_current_event ();

	if (event) {
		GdkEventButton *button_event = (GdkEventButton *) event;
		if ((event->type == GDK_BUTTON_PRESS || event->type == GDK_BUTTON_RELEASE) &&
		    over_eject_button (sidebar, button_event->x, button_event->y, path)) {
			return TRUE;
		}
	}

	return FALSE;
}

static void
desktop_setting_changed_callback (gpointer user_data)
{
	NemoPlacesSidebar *sidebar;

	sidebar = NEMO_PLACES_SIDEBAR (user_data);

	update_places (sidebar);
}

static void
loading_uri_callback (NemoWindow *window,
                            char *location,
               NemoPlacesSidebar *sidebar)
{
    GtkTreeSelection *selection;
    GtkTreeIter       iter_cat, iter_child;
    gboolean          valid_cat, valid_child;
    char              *uri;
    gboolean found = FALSE;

    if (strcmp (sidebar->uri, location) != 0) {
        g_free (sidebar->uri);
                sidebar->uri = g_strdup (location);

        /* set selection if any place matches location */
        selection = gtk_tree_view_get_selection (sidebar->tree_view);
        gtk_tree_selection_unselect_all (selection);
        valid_cat = gtk_tree_model_get_iter_first (GTK_TREE_MODEL (sidebar->store_filter),
                                                    &iter_cat);

        while (valid_cat) {
            valid_child = gtk_tree_model_iter_children (GTK_TREE_MODEL (sidebar->store_filter),
                                                        &iter_child,
                                                        &iter_cat);
            while (valid_child) {
                gtk_tree_model_get (GTK_TREE_MODEL (sidebar->store_filter), &iter_child,
                               	    PLACES_SIDEBAR_COLUMN_URI, &uri,
                                    -1);
                if (uri != NULL) {
                    if (strcmp (uri, location) == 0) {
                        g_free (uri);
                        gtk_tree_selection_select_iter (selection, &iter_child);
                        found = TRUE;
                        break;
                    }
                    g_free (uri);
                }
                valid_child = gtk_tree_model_iter_next (GTK_TREE_MODEL (sidebar->store_filter),
                                                        &iter_child);
            }
            if (found) {
                break;
            }
            valid_cat = gtk_tree_model_iter_next (GTK_TREE_MODEL (sidebar->store_filter),
							                         &iter_cat);
		}
    }
}

typedef struct {
    NemoPlacesSidebar *sidebar;
    GdkRectangle rect;
    SectionType section_type;
} CategoryExpandPayload;

static gboolean
pointer_is_still_in_cell (gint x,
                          gint y,
                  GdkRectangle rect)
{
    gint max_x = rect.x + rect.width;
    gint max_y = rect.y + rect.height;
    if ((x >= rect.x && x <= max_x) &&
        (y >= rect.y && y <= max_y)) {
        return TRUE;
    } else {
        return FALSE;
    }
}

static gboolean
maybe_expand_category (gpointer data)
{
    CategoryExpandPayload *payload = (CategoryExpandPayload *) data;
    GdkDeviceManager *manager;
    GdkDevice *pointer;
    GdkWindow *window;
    int x, y;
    g_assert (GTK_IS_WIDGET (payload->sidebar->tree_view));
    window = gtk_widget_get_window (GTK_WIDGET (payload->sidebar->tree_view));

    manager = gdk_display_get_device_manager (gtk_widget_get_display (GTK_WIDGET (payload->sidebar->tree_view)));
    pointer = gdk_device_manager_get_client_pointer (manager);
    gdk_window_get_device_position (window, pointer,
                                    &x, &y, NULL);
    if (pointer_is_still_in_cell (x, y, payload->rect)) {
        expand_or_collapse_category (payload->sidebar, payload->section_type, TRUE);
    }

    g_source_remove (payload->sidebar->expand_timeout_source);
    payload->sidebar->expand_timeout_source = 0;
    return FALSE;
}


static gboolean
cat_is_expanded (NemoPlacesSidebar *sidebar,
                 SectionType section_type)
{
    switch (section_type) {
        case SECTION_COMPUTER:
            return sidebar->my_computer_expanded;
        case SECTION_FAVORITES:
            return sidebar->favorites_expanded;
        case SECTION_BOOKMARKS:
            return sidebar->bookmarks_expanded;
        case SECTION_DEVICES:
            return sidebar->devices_expanded;
        case SECTION_NETWORK:
            return sidebar->network_expanded;
        case SECTION_XDG_BOOKMARKS:
        default:
            return TRUE;
    }
}


static GtkTreeViewDropPosition
get_drag_type (NemoPlacesSidebar *sidebar,
                           gchar *drop_target_uri,
                     GdkRectangle rect,
                              int y,
                      SectionType section_type)
{
    gint zone;
    gint upper_bound = rect.y + (rect.height / 4);
    gint lower_bound = rect.y + rect.height - (rect.height / 4);

    if (y <= upper_bound) {
        zone = POSITION_UPPER;
    } else if (y > upper_bound && y < lower_bound) {
        return GTK_TREE_VIEW_DROP_INTO_OR_BEFORE;
    } else {
        zone = POSITION_LOWER;
    }

    if (g_strcmp0 (drop_target_uri, sidebar->top_bookend_uri) == 0 &&
        zone == POSITION_LOWER) {
        return GTK_TREE_VIEW_DROP_AFTER;
    } else  if (g_strcmp0 (drop_target_uri, sidebar->bottom_bookend_uri) == 0 &&
        zone == POSITION_UPPER) {
        return GTK_TREE_VIEW_DROP_BEFORE;
    }

    if ((section_type == SECTION_XDG_BOOKMARKS || section_type == SECTION_BOOKMARKS)
        && zone == POSITION_UPPER) {
        return GTK_TREE_VIEW_DROP_BEFORE;
    } else if ((section_type == SECTION_XDG_BOOKMARKS || section_type == SECTION_BOOKMARKS)
        && zone == POSITION_LOWER) {
        return GTK_TREE_VIEW_DROP_AFTER;
    } else {
        /* or else you want to drag items INTO the existing bookmarks */
        return GTK_TREE_VIEW_DROP_INTO_OR_BEFORE;
    }
}


/* Computes the appropriate row and position for dropping */
static gboolean
compute_drop_position (GtkTreeView *tree_view,
		       int                      x,
		       int                      y,
		       GtkTreePath            **path,
		       GtkTreeViewDropPosition *pos,
		       NemoPlacesSidebar *sidebar)
{
	GtkTreeModel *model;
	GtkTreeIter iter;
	PlaceType place_type;
	SectionType section_type;
    gchar *drop_target_uri = NULL;

	if (!gtk_tree_view_get_dest_row_at_pos (tree_view,
						x, y,
						path, pos)) {
		return FALSE;
	}
	model = gtk_tree_view_get_model (tree_view);

	gtk_tree_model_get_iter (model, &iter, *path);
	gtk_tree_model_get (model, &iter,
			    PLACES_SIDEBAR_COLUMN_ROW_TYPE, &place_type,
			    PLACES_SIDEBAR_COLUMN_SECTION_TYPE, &section_type,
                PLACES_SIDEBAR_COLUMN_URI, &drop_target_uri,
			    -1);
    /* With no "All Favorites" row, the heading itself accepts files/folders
     * as favorite references, never as a copy/move destination. */
    if (section_type == SECTION_FAVORITES && place_type == PLACES_HEADING &&
        sidebar->drag_data_received && sidebar->drag_data_info == TEXT_URI_LIST) {
        *pos = GTK_TREE_VIEW_DROP_INTO_OR_BEFORE;
        g_free (drop_target_uri);
        return TRUE;
    }
	if (!cat_is_expanded (sidebar, section_type) && place_type == PLACES_HEADING) {
        if (sidebar->expand_timeout_source > 0) {
            goto fail;
        }
        CategoryExpandPayload *payload;
        GtkTreeViewColumn *col;
        col = gtk_tree_view_get_column (GTK_TREE_VIEW (tree_view), 2);
        payload = g_new0 (CategoryExpandPayload, 1);
        payload->sidebar = sidebar;
        gtk_tree_view_get_cell_area (tree_view,
                                     *path,
                                     col,
                                     &payload->rect);
        payload->section_type = section_type;
        sidebar->expand_timeout_source = g_timeout_add_full (G_PRIORITY_DEFAULT,
                                                             DRAG_EXPAND_CATEGORY_DELAY,
                                                             (GSourceFunc) maybe_expand_category,
                                                             payload,
                                                             (GDestroyNotify) g_free);
        goto fail;
	} else if (place_type == PLACES_HEADING) {
        if (section_type == SECTION_BOOKMARKS &&
            (int)nemo_bookmark_list_length (sidebar->bookmarks) == sidebar->bookmark_breakpoint) {
            *pos = GTK_TREE_VIEW_DROP_AFTER;
            g_free (drop_target_uri);
            return TRUE;
        } else {
            goto fail;
        }
    }

    /* Reordering inside the unified Favorites section: any Favorites row
     * (XApp favorite, standard folder, bookmark) is a valid before/after
     * target for another Favorites row. */
    if (sidebar->drag_data_received &&
        sidebar->drag_data_info == GTK_TREE_MODEL_ROW) {
        GdkRectangle row_area;

        if (!dakkar_is_favorites_row (place_type, section_type))
            goto fail;
        gtk_tree_view_get_background_area (tree_view, *path, NULL, &row_area);
        *pos = (y < row_area.y + row_area.height / 2) ? GTK_TREE_VIEW_DROP_BEFORE
                                                       : GTK_TREE_VIEW_DROP_AFTER;
        g_free (drop_target_uri);
        return TRUE;
    }

	if (section_type != SECTION_XDG_BOOKMARKS &&
        section_type != SECTION_BOOKMARKS &&
	    sidebar->drag_data_received &&
	    sidebar->drag_data_info == GTK_TREE_MODEL_ROW &&
        g_strcmp0 (drop_target_uri, sidebar->top_bookend_uri) != 0) {
		/* don't allow dropping bookmarks into non-bookmark areas */

        goto fail;
	}

    /* No file transfers into individual favorites: those rows only navigate. */
    if (section_type == SECTION_FAVORITES)
        goto fail;

    if (g_strcmp0 (drop_target_uri, "recent:///") == 0) {
        goto fail;
    }

    GdkRectangle rect;
    GtkTreeViewColumn *col;
    col = gtk_tree_view_get_column (GTK_TREE_VIEW (tree_view), 1);
    gtk_tree_view_get_cell_area (tree_view,
                                     *path,
                                     col,
                                     &rect);

    *pos = get_drag_type (sidebar, drop_target_uri, rect, y, section_type);

	if (*pos != GTK_TREE_VIEW_DROP_BEFORE &&
	    sidebar->drag_data_received &&
	    sidebar->drag_data_info == GTK_TREE_MODEL_ROW) {
		/* bookmark rows are never dragged into other bookmark rows */
		*pos = GTK_TREE_VIEW_DROP_AFTER;
	}

	return TRUE;

fail:
    g_free (drop_target_uri);
    gtk_tree_path_free (*path);
    *path = NULL;
    return FALSE;
}

static gboolean
get_drag_data (GtkTreeView *tree_view,
	       GdkDragContext *context,
	       unsigned int time)
{
	GdkAtom target;

	target = gtk_drag_dest_find_target (GTK_WIDGET (tree_view),
					    context,
					    NULL);

	if (target == GDK_NONE) {
		return FALSE;
	}

	gtk_drag_get_data (GTK_WIDGET (tree_view),
			   context, target, time);

	return TRUE;
}

static void
free_drag_data (NemoPlacesSidebar *sidebar)
{
	sidebar->drag_data_received = FALSE;

	if (sidebar->drag_list) {
		nemo_drag_destroy_selection_list (sidebar->drag_list);
		sidebar->drag_list = NULL;
	}

    g_clear_pointer (&sidebar->desktop_dnd_source_fs, g_free);
    sidebar->desktop_dnd_can_delete_source = FALSE;
}

static gboolean
can_accept_file_as_bookmark (NemoFile *file)
{
	return (nemo_file_is_directory (file) &&
		!is_built_in_bookmark (file));
}

static gboolean
can_accept_items_as_bookmarks (const GList *items)
{
	int max;
	char *uri;
	NemoFile *file;

	/* Iterate through selection checking if item will get accepted as a bookmark.
	 * If more than 100 items selected, return an over-optimistic result.
	 */
	for (max = 100; items != NULL && max >= 0; items = items->next, max--) {
		uri = ((NemoDragSelectionItem *)items->data)->uri;
		file = nemo_file_get_by_uri (uri);
		if (!can_accept_file_as_bookmark (file)) {
			nemo_file_unref (file);
			return FALSE;
		}
		nemo_file_unref (file);
	}

	return TRUE;
}

static gboolean
drag_motion_callback (GtkTreeView *tree_view,
		      GdkDragContext *context,
		      int x,
		      int y,
		      unsigned int time,
		      NemoPlacesSidebar *sidebar)
{
	GtkTreePath *path;
	GtkTreeViewDropPosition pos;
	int action;
	GtkTreeIter iter;
	char *uri;
	gboolean res;

    action = 0;

	if (!sidebar->drag_data_received) {
		if (!get_drag_data (tree_view, context, time)) {
			return FALSE;
		}
	}

    if (!sidebar->in_drag) {
        sidebar->in_drag = TRUE;
        gtk_tree_model_filter_refilter (GTK_TREE_MODEL_FILTER (sidebar->store_filter));
    }

	path = NULL;
	res = compute_drop_position (tree_view, x, y, &path, &pos, sidebar);

	if (!res) {
        gtk_tree_view_set_drag_dest_row (tree_view, NULL, GTK_TREE_VIEW_DROP_BEFORE);
		goto out;
	}

	if (pos == GTK_TREE_VIEW_DROP_BEFORE ||
	    pos == GTK_TREE_VIEW_DROP_AFTER ) {
		if (sidebar->drag_data_received &&
		    sidebar->drag_data_info == GTK_TREE_MODEL_ROW) {
			action = GDK_ACTION_MOVE;
		} else if (can_accept_items_as_bookmarks (sidebar->drag_list)) {
			action = GDK_ACTION_COPY;
		} else {
			action = 0;
		}
	} else {
		if (sidebar->drag_list == NULL) {
			action = 0;
		} else {
			gtk_tree_model_get_iter (GTK_TREE_MODEL (sidebar->store_filter),
						 &iter, path);
            SectionType target_section;
            PlaceType target_type;
            gtk_tree_model_get (GTK_TREE_MODEL (sidebar->store_filter),
                                &iter,
                                PLACES_SIDEBAR_COLUMN_URI, &uri,
                                PLACES_SIDEBAR_COLUMN_SECTION_TYPE, &target_section,
                                PLACES_SIDEBAR_COLUMN_ROW_TYPE, &target_type,
                                -1);
            if (target_section == SECTION_FAVORITES && target_type == PLACES_HEADING &&
                sidebar->drag_data_info == TEXT_URI_LIST) {
                action = GDK_ACTION_COPY; /* Only an XApp favorite reference */
            } else if (uri != NULL) {
                nemo_drag_default_drop_action_for_icons (context,
                                                         uri,
                                                         sidebar->drag_list,
                                                         &action,
                                                         &sidebar->desktop_dnd_source_fs,
                                                         &sidebar->desktop_dnd_can_delete_source);
            }
            g_free (uri);
		}
	}

	if (action != 0) {
		gtk_tree_view_set_drag_dest_row (tree_view, path, pos);
	}

	if (path != NULL) {
		gtk_tree_path_free (path);
	}

 out:
	g_signal_stop_emission_by_name (tree_view, "drag-motion");

	if (action != 0) {
		gdk_drag_status (context, action, time);
	} else {
		gdk_drag_status (context, 0, time);
	}

	return TRUE;
}

static void
drag_leave_callback (GtkTreeView *tree_view,
		     GdkDragContext *context,
		     unsigned int time,
		     NemoPlacesSidebar *sidebar)
{
	free_drag_data (sidebar);
	gtk_tree_view_set_drag_dest_row (tree_view, NULL, GTK_TREE_VIEW_DROP_BEFORE);
	g_signal_stop_emission_by_name (tree_view, "drag-leave");
}

/* Parses a "text/uri-list" string and inserts its URIs as bookmarks */
static void
bookmarks_drop_uris (NemoPlacesSidebar *sidebar,
                     GtkSelectionData  *selection_data,
                                  int   position,
                          SectionType   section_type)
{
	NemoBookmark *bookmark;
	NemoFile *file;
	char *uri;
	char **uris;
	int i;
	GFile *location;

	uris = gtk_selection_data_get_uris (selection_data);
	if (!uris)
		return;

	for (i = 0; uris[i]; i++) {
		uri = uris[i];
		file = nemo_file_get_by_uri (uri);

		if (!can_accept_file_as_bookmark (file)) {
			nemo_file_unref (file);
			continue;
		}

		uri = nemo_file_get_drop_target_uri (file);
		location = g_file_new_for_uri (uri);
		nemo_file_unref (file);

		bookmark = nemo_bookmark_new (location, NULL, NULL, NULL);

		if (!nemo_bookmark_list_contains (sidebar->bookmarks, bookmark)) {
            if (position < sidebar->bookmark_breakpoint ||
                (position == sidebar->bookmark_breakpoint && (section_type == SECTION_XDG_BOOKMARKS ||
                                                              section_type == SECTION_COMPUTER))) {
                increment_bookmark_breakpoint (sidebar);
            }
			nemo_bookmark_list_insert_item (sidebar->bookmarks, bookmark, position++);
		}

		g_object_unref (location);
		g_object_unref (bookmark);
		g_free (uri);
	}

	g_strfreev (uris);
}

static GList *
uri_list_from_selection (GList *selection)
{
	NemoDragSelectionItem *item;
	GList *ret;
	GList *l;

	ret = NULL;
	for (l = selection; l != NULL; l = l->next) {
		item = l->data;
		ret = g_list_prepend (ret, item->uri);
	}

	return g_list_reverse (ret);
}

static GList*
build_selection_list (const char *data)
{
	NemoDragSelectionItem *item;
	GList *result;
	char **uris;
	char *uri;
	int i;

	uris = g_uri_list_extract_uris (data);

	result = NULL;
	for (i = 0; uris[i]; i++) {
		uri = uris[i];
		item = nemo_drag_selection_item_new ();
		item->uri = g_strdup (uri);
		item->got_icon_position = FALSE;
		result = g_list_prepend (result, item);
	}

	g_strfreev (uris);

	return g_list_reverse (result);
}

static gboolean
get_selected_iter (NemoPlacesSidebar *sidebar,
		   GtkTreeIter *iter)
{
	GtkTreeSelection *selection;

	selection = gtk_tree_view_get_selection (sidebar->tree_view);

	return gtk_tree_selection_get_selected (selection, NULL, iter);
}

static void
update_bookmark_breakpoint (NemoPlacesSidebar *sidebar,
                                  SectionType  old_type,
                                  SectionType  new_type)
{
    if (old_type != new_type) {
        if (old_type == SECTION_XDG_BOOKMARKS && new_type != SECTION_COMPUTER)
            decrement_bookmark_breakpoint (sidebar);
        else if (old_type == SECTION_BOOKMARKS)
            increment_bookmark_breakpoint (sidebar);
    }
}

/* Reorders the selected bookmark to the specified position */
static void
reorder_bookmarks (NemoPlacesSidebar *sidebar,
                                 int  new_position,
                         SectionType  new_section_type)
{
	GtkTreeIter iter;
	PlaceType type;
    SectionType old_section_type;
	int old_position;

	/* Get the selected path */
	if (!get_selected_iter (sidebar, &iter)) {
		return;
	}

	gtk_tree_model_get (GTK_TREE_MODEL (sidebar->store_filter), &iter,
			    PLACES_SIDEBAR_COLUMN_ROW_TYPE, &type,
                PLACES_SIDEBAR_COLUMN_SECTION_TYPE, &old_section_type,
			    PLACES_SIDEBAR_COLUMN_INDEX, &old_position,
			    -1);

	if (type != PLACES_BOOKMARK ||
	    old_position < 0 ||
	    old_position >= (int)nemo_bookmark_list_length (sidebar->bookmarks)) {
		return;
	}

    update_bookmark_breakpoint (sidebar, old_section_type, new_section_type);

	nemo_bookmark_list_move_item (sidebar->bookmarks, old_position,
					  new_position);

    if (old_position == new_position)
        update_places (sidebar);
}

static gboolean
idle_hide_bookmarks (gpointer user_data)
{
    NemoPlacesSidebar *sidebar = NEMO_PLACES_SIDEBAR (user_data);

    if (sidebar->in_drag) {
        sidebar->in_drag = FALSE;
        gtk_tree_model_filter_refilter (GTK_TREE_MODEL_FILTER (sidebar->store_filter));
    }

    return FALSE;
}

static void
drag_data_received_callback (GtkWidget *widget,
			     GdkDragContext *context,
			     int x,
			     int y,
			     GtkSelectionData *selection_data,
			     unsigned int info,
			     unsigned int time,
			     NemoPlacesSidebar *sidebar)
{
	GtkTreeView *tree_view;
	GtkTreePath *tree_path;
	GtkTreeViewDropPosition tree_pos;
	GtkTreeIter iter;
	int position;
	GtkTreeModel *model;
	char *drop_uri;
	GList *selection_list, *uris;
	PlaceType place_type;
	SectionType section_type;
	gboolean success;

	tree_view = GTK_TREE_VIEW (widget);

	if (!sidebar->drag_data_received) {
		if (gtk_selection_data_get_target (selection_data) != GDK_NONE &&
		    info == TEXT_URI_LIST) {
			sidebar->drag_list = build_selection_list ((const gchar *) gtk_selection_data_get_data (selection_data));
		} else {
			sidebar->drag_list = NULL;
		}
		sidebar->drag_data_received = TRUE;
		sidebar->drag_data_info = info;
	}

	g_signal_stop_emission_by_name (widget, "drag-data-received");

	if (!sidebar->drop_occured) {
		return;
	}

	/* Compute position */
	success = compute_drop_position (tree_view, x, y, &tree_path, &tree_pos, sidebar);
	if (!success) {
		goto out;
	}

	success = FALSE;

	if (tree_pos == GTK_TREE_VIEW_DROP_BEFORE ||
	    tree_pos == GTK_TREE_VIEW_DROP_AFTER) {
		model = gtk_tree_view_get_model (tree_view);
		if (!gtk_tree_model_get_iter (model, &iter, tree_path)) {
			goto out;
		}

		gtk_tree_model_get (model, &iter,
				    PLACES_SIDEBAR_COLUMN_SECTION_TYPE, &section_type,
    				    PLACES_SIDEBAR_COLUMN_ROW_TYPE, &place_type,
				    PLACES_SIDEBAR_COLUMN_INDEX, &position,
				    -1);

        if (section_type == SECTION_COMPUTER && tree_pos == GTK_TREE_VIEW_DROP_BEFORE) {
            position = nemo_bookmark_list_length(sidebar->bookmarks);
        } else if (section_type == SECTION_BOOKMARKS && position == -1) {
            /* Check for (temporarily) visible Bookmarks heading, only drop-reactive when
             * it has no children, so we can assume that (bookmark_breakpoint == bookmark_count)
             */
            position = nemo_bookmark_list_length (sidebar->bookmarks);
        } else if (tree_pos == GTK_TREE_VIEW_DROP_AFTER &&
                   (section_type == SECTION_XDG_BOOKMARKS || section_type == SECTION_BOOKMARKS)) {
            position++;
        }

		switch (info) {
		case TEXT_URI_LIST:
			bookmarks_drop_uris (sidebar, selection_data, position, section_type);
			success = TRUE;
			break;
		case GTK_TREE_MODEL_ROW:
		{
			gchar *source_uri = dakkar_selected_favorite_uri (sidebar);
			gchar *target_uri = NULL;

			gtk_tree_model_get (model, &iter,
			                    PLACES_SIDEBAR_COLUMN_URI, &target_uri, -1);
			if (source_uri != NULL && target_uri != NULL &&
			    dakkar_is_favorites_row (place_type, section_type)) {
				/* unified Favorites order (XApp, standard folders, bookmarks) */
				dakkar_favorites_move_to (source_uri, target_uri,
				                          tree_pos == GTK_TREE_VIEW_DROP_AFTER);
			} else {
				reorder_bookmarks (sidebar, position, section_type);
			}
			g_free (source_uri);
			g_free (target_uri);
			success = TRUE;
			break;
		}
		default:
			g_assert_not_reached ();
			break;
		}
	} else {
		GdkDragAction real_action;

		/* file transfer requested */
		real_action = gdk_drag_context_get_selected_action (context);

		if (real_action == GDK_ACTION_ASK) {
			real_action =
				nemo_drag_drop_action_ask (GTK_WIDGET (tree_view),
							       gdk_drag_context_get_actions (context));
		}

		if (real_action > 0) {
			model = gtk_tree_view_get_model (tree_view);

			gtk_tree_model_get_iter (model, &iter, tree_path);
            gtk_tree_model_get (model, &iter,
                                PLACES_SIDEBAR_COLUMN_URI, &drop_uri,
                                PLACES_SIDEBAR_COLUMN_SECTION_TYPE, &section_type,
                                PLACES_SIDEBAR_COLUMN_ROW_TYPE, &place_type,
                                -1);

			switch (info) {
			case TEXT_URI_LIST:
				selection_list = build_selection_list ((const gchar *) gtk_selection_data_get_data (selection_data));
				uris = uri_list_from_selection (selection_list);

                GList *l;

                if (section_type == SECTION_FAVORITES && place_type == PLACES_HEADING) {
                    for (l = uris; l != NULL; l = l->next) {
                        gchar *uri = (gchar *) l->data;
                        NemoFile *source_file = nemo_file_get_by_uri (uri);
                        nemo_file_set_is_favorite (source_file, TRUE);
                        nemo_file_unref (source_file);
                    }
                    expand_or_collapse_category (sidebar, SECTION_FAVORITES, TRUE);
                } else if (drop_uri != NULL) {
                    nemo_file_operations_copy_move (uris, NULL, drop_uri,
                                        real_action, GTK_WIDGET (tree_view),
                                        NULL, NULL);
                }

				nemo_drag_destroy_selection_list (selection_list);
				g_list_free (uris);
				success = TRUE;
				break;
			case GTK_TREE_MODEL_ROW:
				success = FALSE;
				break;
			default:
				g_assert_not_reached ();
				break;
			}

			g_free (drop_uri);
		}
	}

out:
	sidebar->drop_occured = FALSE;
	free_drag_data (sidebar);
	gtk_drag_finish (context, success, FALSE, time);
	gtk_tree_path_free (tree_path);

    g_timeout_add (250, (GSourceFunc) idle_hide_bookmarks, sidebar);
}

static gboolean
drag_drop_callback (GtkTreeView *tree_view,
		    GdkDragContext *context,
		    int x,
		    int y,
		    unsigned int time,
		    NemoPlacesSidebar *sidebar)
{
	gboolean retval = FALSE;
	sidebar->drop_occured = TRUE;
	retval = get_drag_data (tree_view, context, time);
	g_signal_stop_emission_by_name (tree_view, "drag-drop");
	return retval;
}

static void
check_unmount_and_eject (GMount *mount,
			 GVolume *volume,
			 GDrive *drive,
			 gboolean *show_unmount,
			 gboolean *show_eject)
{
	*show_unmount = FALSE;
	*show_eject = FALSE;

	if (drive != NULL) {
		*show_eject = g_drive_can_eject (drive);
	}

	if (volume != NULL) {
		*show_eject |= g_volume_can_eject (volume);
	}
	if (mount != NULL) {
		*show_eject |= g_mount_can_eject (mount);
		*show_unmount = g_mount_can_unmount (mount) && !*show_eject;
	}
}

static void
check_visibility (GMount           *mount,
		  GVolume          *volume,
		  GDrive           *drive,
		  gboolean         *show_mount,
		  gboolean         *show_unmount,
		  gboolean         *show_eject,
		  gboolean         *show_rescan,
		  gboolean         *show_start,
		  gboolean         *show_stop)
{
	*show_mount = FALSE;
	*show_rescan = FALSE;
	*show_start = FALSE;
	*show_stop = FALSE;

	check_unmount_and_eject (mount, volume, drive, show_unmount, show_eject);

	if (drive != NULL) {
		if (g_drive_is_removable (drive) &&
		    !g_drive_is_media_check_automatic (drive) &&
		    g_drive_can_poll_for_media (drive))
			*show_rescan = TRUE;

		*show_start = g_drive_can_start (drive) || g_drive_can_start_degraded (drive);
		*show_stop  = g_drive_can_stop (drive);

		if (*show_stop)
			*show_unmount = FALSE;
	}

	if (volume != NULL) {
		if (mount == NULL)
			*show_mount = g_volume_can_mount (volume);
	}
}

static void
set_action_visible (GtkActionGroup *action_group,
                    const gchar    *name,
                    gboolean        visible)
{
    GtkAction *action;

    action = gtk_action_group_get_action (action_group, name);
    gtk_action_set_visible (action, visible);
}

static void
update_menu_states (NemoPlacesSidebar *sidebar)
{
	GtkTreeIter iter;
	PlaceType type;
	GDrive *drive = NULL;
	GVolume *volume = NULL;
	GMount *mount = NULL;
	GFile *location;
	NemoDirectory *directory = NULL;
	gboolean show_mount;
	gboolean show_unmount;
	gboolean show_eject;
	gboolean show_rescan;
	gboolean show_start;
	gboolean show_stop;
	gboolean show_empty_trash;
	gboolean show_properties;
	char *uri = NULL;

	type = PLACES_BUILT_IN;

	if (sidebar->popup_menu == NULL) {
		return;
	}

	if (get_selected_iter (sidebar, &iter)) {
		gtk_tree_model_get (GTK_TREE_MODEL (sidebar->store_filter), &iter,
				    PLACES_SIDEBAR_COLUMN_ROW_TYPE, &type,
				    PLACES_SIDEBAR_COLUMN_DRIVE, &drive,
				    PLACES_SIDEBAR_COLUMN_VOLUME, &volume,
 				    PLACES_SIDEBAR_COLUMN_MOUNT, &mount,
				    PLACES_SIDEBAR_COLUMN_URI, &uri,
				    -1);
	}

    if (uri) {
        NemoFile *file = nemo_file_get_by_uri (uri);
        NemoFile *parent = nemo_file_get_parent (file);

        GList *selection = g_list_prepend (NULL, file);

        nemo_action_manager_update_action_states (sidebar->action_manager,
                                                  sidebar->action_action_group,
                                                  selection,
                                                  parent,
                                                  TRUE,
                                                  FALSE,
                                                  GTK_WINDOW (sidebar->window));
        nemo_file_list_free (selection);
        nemo_file_unref (parent);
    }

    set_action_visible (sidebar->bookmark_action_group, NEMO_ACTION_ADD_BOOKMARK, (type == PLACES_MOUNTED_VOLUME));
    set_action_visible (sidebar->bookmark_action_group, NEMO_ACTION_SIDEBAR_REMOVE, (type == PLACES_BOOKMARK || type == PLACES_FAVORITE));
    GtkAction *remove_action = gtk_action_group_get_action (sidebar->bookmark_action_group,
                                                               NEMO_ACTION_SIDEBAR_REMOVE);
    if (remove_action != NULL)
        gtk_action_set_label (remove_action, type == PLACES_FAVORITE ?
                              _("Remove from Favorites") : _("Remove"));
    set_action_visible (sidebar->bookmark_action_group, NEMO_ACTION_RENAME, (type == PLACES_BOOKMARK));
    set_action_visible (sidebar->bookmark_action_group, NEMO_ACTION_EMPTY_TRASH_CONDITIONAL, !nemo_trash_monitor_is_empty ());

 	check_visibility (mount, volume, drive,
 			  &show_mount, &show_unmount, &show_eject, &show_rescan, &show_start, &show_stop);

	/* We actually want both eject and unmount since eject will unmount all volumes.
	 * TODO: hide unmount if the drive only has a single mountable volume
	 */
	show_empty_trash = (uri != NULL) &&
			   (!strcmp (uri, "trash:///"));

    g_free (uri);

	/* Only show properties for local mounts */
	show_properties = (mount != NULL);
	if (mount != NULL) {
		location = g_mount_get_default_location (mount);
		directory = nemo_directory_get (location);

		show_properties = nemo_directory_is_local (directory);

		nemo_directory_unref (directory);
		g_object_unref (location);
	}

    set_action_visible (sidebar->bookmark_action_group, NEMO_ACTION_MOUNT_VOLUME, show_mount);
    set_action_visible (sidebar->bookmark_action_group, NEMO_ACTION_UNMOUNT_VOLUME, show_unmount);
    set_action_visible (sidebar->bookmark_action_group, NEMO_ACTION_EJECT_VOLUME, show_eject);
    set_action_visible (sidebar->bookmark_action_group, NEMO_ACTION_SIDEBAR_DETECT_MEDIA, show_rescan);
    set_action_visible (sidebar->bookmark_action_group, NEMO_ACTION_START_VOLUME, show_start);
    set_action_visible (sidebar->bookmark_action_group, NEMO_ACTION_STOP_VOLUME, show_stop);
    set_action_visible (sidebar->bookmark_action_group, NEMO_ACTION_EMPTY_TRASH_CONDITIONAL, show_empty_trash);
    set_action_visible (sidebar->bookmark_action_group, NEMO_ACTION_PROPERTIES, show_properties);

	/* Adjust start/stop items to reflect the type of the drive */
    GtkAction *start_action, *stop_action;

    start_action = gtk_action_group_get_action (sidebar->bookmark_action_group, NEMO_ACTION_START_VOLUME);
    stop_action = gtk_action_group_get_action (sidebar->bookmark_action_group, NEMO_ACTION_STOP_VOLUME);

	gtk_action_set_label (start_action, _("_Start"));
	gtk_action_set_label (stop_action, _("_Stop"));
	if ((show_start || show_stop) && drive != NULL) {
		switch (g_drive_get_start_stop_type (drive)) {
		case G_DRIVE_START_STOP_TYPE_SHUTDOWN:
			/* start() for type G_DRIVE_START_STOP_TYPE_SHUTDOWN is normally not used */
			gtk_action_set_label (start_action, _("_Power On"));
			gtk_action_set_label (stop_action, _("_Safely Remove Drive"));
			break;
		case G_DRIVE_START_STOP_TYPE_NETWORK:
			gtk_action_set_label (start_action, _("_Connect Drive"));
			gtk_action_set_label (stop_action, _("_Disconnect Drive"));
			break;
		case G_DRIVE_START_STOP_TYPE_MULTIDISK:
			gtk_action_set_label (start_action, _("_Start Multi-disk Device"));
			gtk_action_set_label (stop_action, _("_Stop Multi-disk Device"));
			break;
		case G_DRIVE_START_STOP_TYPE_PASSWORD:
			/* stop() for type G_DRIVE_START_STOP_TYPE_PASSWORD is normally not used */
			gtk_action_set_label (start_action, _("_Unlock Drive"));
			gtk_action_set_label (stop_action, _("_Lock Drive"));
			break;

		default:
		case G_DRIVE_START_STOP_TYPE_UNKNOWN:
			/* uses defaults set above */
			break;
		}
	}

    g_clear_object (&drive);
    g_clear_object (&volume);
    g_clear_object (&mount);
}

/* Callback used when the selection in the shortcuts tree changes */
static void
bookmarks_selection_changed_cb (GtkTreeSelection      *selection,
				NemoPlacesSidebar *sidebar)
{
    update_menu_states (sidebar);
}

static void
volume_mounted_cb (GVolume *volume,
		   gboolean success,
		   GObject *user_data)
{
	GMount *mount;
	NemoPlacesSidebar *sidebar;
	GFile *location;

	sidebar = NEMO_PLACES_SIDEBAR (user_data);

	sidebar->mounting = FALSE;

	mount = g_volume_get_mount (volume);
	if (mount != NULL) {
		location = g_mount_get_default_location (mount);

		if (sidebar->go_to_after_mount_slot != NULL) {
            if ((sidebar->go_to_after_mount_flags & NEMO_WINDOW_OPEN_FLAG_NEW_WINDOW) == 0) {
                g_object_set_data_full (G_OBJECT (sidebar->go_to_after_mount_slot),
                                        "miller-sidebar-navigation-pending", g_object_ref (location),
                                        g_object_unref);
                nemo_window_slot_open_location (sidebar->go_to_after_mount_slot, location,
								    sidebar->go_to_after_mount_flags);
			} else {
				NemoWindow *new, *cur;

				cur = NEMO_WINDOW (sidebar->window);
                new = nemo_application_create_window (nemo_application_get_singleton (),
                                                      gtk_window_get_screen (GTK_WINDOW (cur)));
                {
                    NemoWindowSlot *new_slot = nemo_window_get_active_slot (new);
                    if (new_slot != NULL)
                        g_object_set_data_full (G_OBJECT (new_slot), "miller-sidebar-root",
                                                g_object_ref (location), g_object_unref);
                }
                nemo_window_go_to (new, location);
			}
		}

		g_object_unref (G_OBJECT (location));
		g_object_unref (G_OBJECT (mount));
	}

	if (sidebar->go_to_after_mount_slot) {
		g_object_remove_weak_pointer (G_OBJECT (sidebar->go_to_after_mount_slot),
					      (gpointer *) &sidebar->go_to_after_mount_slot);
		sidebar->go_to_after_mount_slot = NULL;
	}
}

static void
drive_start_from_bookmark_cb (GObject      *source_object,
			      GAsyncResult *res,
			      gpointer      user_data)
{
	GError *error;
	char *primary;
	char *name;

	error = NULL;
	if (!g_drive_poll_for_media_finish (G_DRIVE (source_object), res, &error)) {
		if (error->code != G_IO_ERROR_FAILED_HANDLED) {
			name = g_drive_get_name (G_DRIVE (source_object));
			primary = g_strdup_printf (_("Unable to start %s"), name);
			g_free (name);
			eel_show_error_dialog (primary,
					       error->message,
					       NULL);
			g_free (primary);
		}
		g_error_free (error);
	}
}

static void
open_selected_bookmark (NemoPlacesSidebar *sidebar,
			GtkTreeModel	      *model,
			GtkTreeIter	      *iter,
			NemoWindowOpenFlags	      flags)
{
	NemoWindowSlot *slot;
	GFile *location;
	char *uri;

	if (!iter) {
		return;
	}

	gtk_tree_model_get (model, iter, PLACES_SIDEBAR_COLUMN_URI, &uri, -1);

	if (uri != NULL) {
		DEBUG ("Activating bookmark %s", uri);

        /* Favorites may point to files as well as directories. Don't try
         * to navigate into a file as if it were a folder. */
        PlaceType place_type = PLACES_BUILT_IN;
        gtk_tree_model_get (model, iter,
                            PLACES_SIDEBAR_COLUMN_ROW_TYPE, &place_type, -1);
        if (place_type == PLACES_FAVORITE) {
            XAppFavoriteInfo *info = xapp_favorites_find_by_uri (
                xapp_favorites_get_default (), uri);
            if (info != NULL && g_strcmp0 (info->cached_mimetype, "inode/directory") != 0) {
                /* XApp knows the favorite's cached MIME type and handles
                 * missing targets and preferred application consistently. */
                xapp_favorites_launch (xapp_favorites_get_default (), uri,
                                      gtk_get_current_event_time ());
                g_free (uri);
                return;
            }
        }

		location = g_file_new_for_uri (uri);
		/* Navigate to the clicked location */
        if ((flags & NEMO_WINDOW_OPEN_FLAG_NEW_WINDOW) == 0) {
            slot = nemo_window_get_active_slot (sidebar->window);
            /* Save the user's explicit sidebar root on the source slot.
             * open_location_full forwards it to a newly created tab. */
            g_object_set_data_full (G_OBJECT (slot), "miller-sidebar-navigation-pending",
                                    g_object_ref (location), g_object_unref);
            nemo_window_slot_open_location (slot, location, flags);
		} else {
			NemoWindow *cur, *new;

			cur = NEMO_WINDOW (sidebar->window);
            new = nemo_application_create_window (nemo_application_get_singleton (),
                                                  gtk_window_get_screen (GTK_WINDOW (cur)));
            slot = nemo_window_get_active_slot (new);
            if (slot != NULL)
                g_object_set_data_full (G_OBJECT (slot), "miller-sidebar-root",
                                        g_object_ref (location), g_object_unref);
            nemo_window_go_to (new, location);
		}
		g_object_unref (location);
		g_free (uri);

	} else {
		GDrive *drive;
		GVolume *volume;
		NemoWindowSlot *slt;

		gtk_tree_model_get (model, iter,
				    PLACES_SIDEBAR_COLUMN_DRIVE, &drive,
				    PLACES_SIDEBAR_COLUMN_VOLUME, &volume,
				    -1);

		if (volume != NULL && !sidebar->mounting) {
			sidebar->mounting = TRUE;

			g_assert (sidebar->go_to_after_mount_slot == NULL);

			slt = nemo_window_get_active_slot (sidebar->window);
			sidebar->go_to_after_mount_slot = slt;
			g_object_add_weak_pointer (G_OBJECT (sidebar->go_to_after_mount_slot),
						   (gpointer *) &sidebar->go_to_after_mount_slot);

			sidebar->go_to_after_mount_flags = flags | NEMO_WINDOW_OPEN_FLAG_MOUNT;

			nemo_file_operations_mount_volume_full (NULL, volume,
								    volume_mounted_cb,
								    G_OBJECT (sidebar));
		} else if (volume == NULL && drive != NULL &&
			   (g_drive_can_start (drive) || g_drive_can_start_degraded (drive))) {
			GMountOperation *mount_op;

			mount_op = gtk_mount_operation_new (GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (sidebar))));
			g_drive_start (drive, G_DRIVE_START_NONE, mount_op, NULL, drive_start_from_bookmark_cb, NULL);
			g_object_unref (mount_op);
		}

		if (drive != NULL)
			g_object_unref (drive);
		if (volume != NULL)
			g_object_unref (volume);
	}
}

static void
open_shortcut_from_menu (NemoPlacesSidebar *sidebar,
			 NemoWindowOpenFlags	       flags)
{
	GtkTreeModel *model;
	GtkTreeIter iter;
	GtkTreePath *path = NULL;

	model = gtk_tree_view_get_model (sidebar->tree_view);
	gtk_tree_view_get_cursor (sidebar->tree_view, &path, NULL);

	if (path != NULL && gtk_tree_model_get_iter (model, &iter, path)) {
		open_selected_bookmark (sidebar, model, &iter, flags);
	}

	gtk_tree_path_free (path);
}

static void
open_shortcut_cb (GtkAction		*item,
		  NemoPlacesSidebar	*sidebar)
{
	open_shortcut_from_menu (sidebar, 0);
}

static void
open_shortcut_in_new_window_cb (GtkAction	      *item,
				NemoPlacesSidebar *sidebar)
{
	open_shortcut_from_menu (sidebar, NEMO_WINDOW_OPEN_FLAG_NEW_WINDOW);
}

static void
open_shortcut_in_new_tab_cb (GtkAction	      *item,
				NemoPlacesSidebar *sidebar)
{
	open_shortcut_from_menu (sidebar, NEMO_WINDOW_OPEN_FLAG_NEW_TAB);
}

/* Add bookmark for the selected item */
static void
add_bookmark (NemoPlacesSidebar *sidebar)
{
	GtkTreeModel *model;
	GtkTreeIter iter;
	char *uri;
	GFile *location;
	NemoBookmark *bookmark;

	model = gtk_tree_view_get_model (sidebar->tree_view);

	if (get_selected_iter (sidebar, &iter)) {
		gtk_tree_model_get (model, &iter, PLACES_SIDEBAR_COLUMN_URI, &uri, -1);

		if (uri == NULL) {
			return;
		}

		location = g_file_new_for_uri (uri);
		bookmark = nemo_bookmark_new (location, NULL, NULL, NULL);

		if (!nemo_bookmark_list_contains (sidebar->bookmarks, bookmark)) {
			nemo_bookmark_list_append (sidebar->bookmarks, bookmark);
		}

		g_object_unref (location);
		g_object_unref (bookmark);
		g_free (uri);
	}
}

static void
add_shortcut_cb (GtkAction           *item,
		 NemoPlacesSidebar *sidebar)
{
	add_bookmark (sidebar);
}

/* Rename the selected bookmark */
static void
rename_selected_bookmark (NemoPlacesSidebar *sidebar)
{
	GtkTreeIter iter;
	GtkTreePath *path;
	GtkTreeViewColumn *column;
	PlaceType type;

	if (get_selected_iter (sidebar, &iter)) {
		gtk_tree_model_get (GTK_TREE_MODEL (sidebar->store_filter), &iter,
				    PLACES_SIDEBAR_COLUMN_ROW_TYPE, &type,
				    -1);

		if (type != PLACES_BOOKMARK) {
			return;
		}

		path = gtk_tree_model_get_path (GTK_TREE_MODEL (sidebar->store_filter), &iter);
		column = gtk_tree_view_get_column (GTK_TREE_VIEW (sidebar->tree_view), 2);
		g_object_set (sidebar->editable_renderer, "editable", TRUE, NULL);
		gtk_tree_view_set_cursor_on_cell (GTK_TREE_VIEW (sidebar->tree_view),
						path, column, sidebar->editable_renderer, TRUE);
		gtk_tree_path_free (path);
	}
}

static void
rename_shortcut_cb (GtkAction           *item,
		    NemoPlacesSidebar *sidebar)
{
	rename_selected_bookmark (sidebar);
}

/* Removes the selected bookmarks */
static void
remove_selected_bookmarks (NemoPlacesSidebar *sidebar)
{
	GtkTreeIter iter;
	PlaceType type;
	int index;

	if (!get_selected_iter (sidebar, &iter)) {
		return;
	}

	gtk_tree_model_get (GTK_TREE_MODEL (sidebar->store_filter), &iter,
			    PLACES_SIDEBAR_COLUMN_ROW_TYPE, &type,
			    -1);

    if (type == PLACES_FAVORITE || type == PLACES_BOOKMARK) {
        /* a removed bookmark takes its custom icon along */
        gchar *icon_id = dakkar_sidebar_item_id (GTK_TREE_MODEL (sidebar->store_filter), &iter);
        if (icon_id != NULL && g_str_has_prefix (icon_id, "uri:"))
            dakkar_sidebar_save_item_icon (icon_id, NULL);
        g_free (icon_id);
    }

    if (type == PLACES_FAVORITE) {
        gchar *favorite_uri = NULL;
        gtk_tree_model_get (GTK_TREE_MODEL (sidebar->store_filter), &iter,
                            PLACES_SIDEBAR_COLUMN_URI, &favorite_uri, -1);
        if (favorite_uri != NULL) {
            /* Remove the saved favorite only, never delete its target. */
            xapp_favorites_remove (xapp_favorites_get_default (), favorite_uri);
            g_free (favorite_uri);
        }
        return;
    }

    if (type != PLACES_BOOKMARK) {
        return;
    }

    gtk_tree_model_get (GTK_TREE_MODEL (sidebar->store_filter), &iter,
                        PLACES_SIDEBAR_COLUMN_INDEX, &index, -1);

    if (index < sidebar->bookmark_breakpoint)
        decrement_bookmark_breakpoint (sidebar);

    nemo_bookmark_list_delete_item_at (sidebar->bookmarks, index);
}

static void
remove_shortcut_cb (GtkAction           *item,
		    NemoPlacesSidebar *sidebar)
{
	remove_selected_bookmarks (sidebar);
}

static void
mount_shortcut_cb (GtkAction           *item,
		   NemoPlacesSidebar *sidebar)
{
	GtkTreeIter iter;
	GVolume *volume;

	if (!get_selected_iter (sidebar, &iter)) {
		return;
	}

	gtk_tree_model_get (GTK_TREE_MODEL (sidebar->store_filter), &iter,
			    PLACES_SIDEBAR_COLUMN_VOLUME, &volume,
			    -1);

	if (volume != NULL) {
		nemo_file_operations_mount_volume (NULL, volume);
		g_object_unref (volume);
	}
}

static void
unmount_done (gpointer data)
{
	NemoWindow *window;

	window = data;
	g_object_unref (window);
}

static void
show_unmount_progress_cb (GMountOperation *op,
                              const gchar *message,
                                    gint64 time_left,
                                    gint64 bytes_left,
                                  gpointer user_data)
{
    NemoApplication *app = NEMO_APPLICATION (g_application_get_default ());

    if (bytes_left == 0) {
        nemo_application_notify_unmount_done (app, message);
    } else {
        nemo_application_notify_unmount_show (app, message);
    }
}

static void
show_unmount_progress_aborted_cb (GMountOperation *op,
                                  gpointer user_data)
{
    NemoApplication *app = NEMO_APPLICATION (g_application_get_default ());
    nemo_application_notify_unmount_done (app, NULL);
}

static GMountOperation *
get_unmount_operation (NemoPlacesSidebar *sidebar)
{
    GMountOperation *mount_op;

    mount_op = gtk_mount_operation_new (GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (sidebar))));
    g_signal_connect (mount_op, "show-unmount-progress",
                      G_CALLBACK (show_unmount_progress_cb), sidebar);
    g_signal_connect (mount_op, "aborted",
                      G_CALLBACK (show_unmount_progress_aborted_cb), sidebar);

    return mount_op;
}

static void
do_unmount (GMount *mount,
	    NemoPlacesSidebar *sidebar)
{
    GMountOperation *mount_op;

	if (mount != NULL) {
        mount_op = get_unmount_operation (sidebar);
        nemo_file_operations_unmount_mount_full (NULL, mount, mount_op, FALSE, TRUE,
                                                 unmount_done,
                                                 g_object_ref (sidebar->window));
        g_object_unref (mount_op);
	}
}

static void
do_unmount_selection (NemoPlacesSidebar *sidebar)
{
	GtkTreeIter iter;
	GMount *mount;

	if (!get_selected_iter (sidebar, &iter)) {
		return;
	}

	gtk_tree_model_get (GTK_TREE_MODEL (sidebar->store_filter), &iter,
			    PLACES_SIDEBAR_COLUMN_MOUNT, &mount,
			    -1);

	if (mount != NULL) {
		do_unmount (mount, sidebar);
		g_object_unref (mount);
	}
}

static void
unmount_shortcut_cb (GtkAction           *item,
		     NemoPlacesSidebar *sidebar)
{
	do_unmount_selection (sidebar);
}

static void
handle_mount_unmount_failure (const gchar *primary,
                              GError      *error)
{
    const gchar *message = NULL;

    if (error && error->code == G_IO_ERROR_FAILED_HANDLED) {
        return;
    }

    if (error) {
        message = error->message;
    }

    eel_show_error_dialog (primary,
                           message,
                           NULL);
}

static void
drive_eject_cb (GObject *source_object,
		GAsyncResult *res,
		gpointer user_data)
{
	NemoWindow *window;
	GError *error;

	window = user_data;
	g_object_unref (window);

	error = NULL;
	if (!g_drive_eject_with_operation_finish (G_DRIVE (source_object), res, &error)) {
        char *name, *primary;

        name = g_drive_get_name (G_DRIVE (source_object));
        primary = g_strdup_printf (_("Unable to eject %s"), name);

        handle_mount_unmount_failure (primary, error);

        g_free (name);
        g_free (primary);
        g_clear_error (&error);
    }
}

static void
volume_eject_cb (GObject *source_object,
		GAsyncResult *res,
		gpointer user_data)
{
	NemoWindow *window;
	GError *error;

	window = user_data;
	g_object_unref (window);

	error = NULL;
	if (!g_volume_eject_with_operation_finish (G_VOLUME (source_object), res, &error)) {
        char *name, *primary;

        name = g_volume_get_name (G_VOLUME (source_object));
        primary = g_strdup_printf (_("Unable to eject %s"), name);

        handle_mount_unmount_failure (primary, error);

        g_free (name);
        g_free (primary);
        g_clear_error (&error);
    }
}

static void
mount_eject_cb (GObject *source_object,
		GAsyncResult *res,
		gpointer user_data)
{
	NemoWindow *window;
	GError *error;

	window = user_data;
	g_object_unref (window);

	error = NULL;
	if (!g_mount_eject_with_operation_finish (G_MOUNT (source_object), res, &error)) {
        char *name, *primary;

        name = g_mount_get_name (G_MOUNT (source_object));
        primary = g_strdup_printf (_("Unable to eject %s"), name);

        handle_mount_unmount_failure (primary, error);

        g_free (name);
        g_free (primary);
        g_clear_error (&error);
    }
}

static void
do_eject (GMount *mount,
	  GVolume *volume,
	  GDrive *drive,
	  NemoPlacesSidebar *sidebar)
{
    GMountOperation *mount_op = get_unmount_operation (sidebar);

	if (mount != NULL) {
		g_mount_eject_with_operation (mount, 0, mount_op, NULL, mount_eject_cb,
					      g_object_ref (sidebar->window));
	} else if (volume != NULL) {
		g_volume_eject_with_operation (volume, 0, mount_op, NULL, volume_eject_cb,
					      g_object_ref (sidebar->window));
	} else if (drive != NULL) {
		g_drive_eject_with_operation (drive, 0, mount_op, NULL, drive_eject_cb,
					      g_object_ref (sidebar->window));
	}
	g_object_unref (mount_op);
}

static void
eject_shortcut_cb (GtkAction           *item,
		   NemoPlacesSidebar *sidebar)
{
	GtkTreeIter iter;
	GMount *mount;
	GVolume *volume;
	GDrive *drive;

	if (!get_selected_iter (sidebar, &iter)) {
		return;
	}

	gtk_tree_model_get (GTK_TREE_MODEL (sidebar->store_filter), &iter,
			    PLACES_SIDEBAR_COLUMN_MOUNT, &mount,
			    PLACES_SIDEBAR_COLUMN_VOLUME, &volume,
			    PLACES_SIDEBAR_COLUMN_DRIVE, &drive,
			    -1);

	do_eject (mount, volume, drive, sidebar);

    g_clear_object (&mount);
    g_clear_object (&volume);
    g_clear_object (&drive);
}

static gboolean
eject_or_unmount_bookmark (NemoPlacesSidebar *sidebar,
			   GtkTreePath *path)
{
	GtkTreeModel *model;
	GtkTreeIter iter;
	gboolean can_unmount, can_eject;
	GMount *mount;
	GVolume *volume;
	GDrive *drive;
	gboolean ret;

	model = GTK_TREE_MODEL (sidebar->store_filter);

	if (!path) {
		return FALSE;
	}
	if (!gtk_tree_model_get_iter (model, &iter, path)) {
		return FALSE;
	}

	gtk_tree_model_get (model, &iter,
			    PLACES_SIDEBAR_COLUMN_MOUNT, &mount,
			    PLACES_SIDEBAR_COLUMN_VOLUME, &volume,
			    PLACES_SIDEBAR_COLUMN_DRIVE, &drive,
			    -1);

	ret = FALSE;

	check_unmount_and_eject (mount, volume, drive, &can_unmount, &can_eject);
	/* if we can eject, it has priority over unmount */
	if (can_eject) {
		do_eject (mount, volume, drive, sidebar);
		ret = TRUE;
	} else if (can_unmount) {
		do_unmount (mount, sidebar);
		ret = TRUE;
	}

    g_clear_object (&mount);
    g_clear_object (&volume);
    g_clear_object (&drive);

	return ret;
}

static gboolean
eject_or_unmount_selection (NemoPlacesSidebar *sidebar)
{
	GtkTreeIter iter;
	GtkTreePath *path;
	gboolean ret;

	if (!get_selected_iter (sidebar, &iter)) {
		return FALSE;
	}

	path = gtk_tree_model_get_path (GTK_TREE_MODEL (sidebar->store_filter), &iter);
	if (path == NULL) {
		return FALSE;
	}

	ret = eject_or_unmount_bookmark (sidebar, path);

	gtk_tree_path_free (path);

	return ret;
}

static void
drive_poll_for_media_cb (GObject *source_object,
			 GAsyncResult *res,
			 gpointer user_data)
{
	GError *error;

	error = NULL;
	if (!g_drive_poll_for_media_finish (G_DRIVE (source_object), res, &error)) {
        char *name, *primary;

        name = g_drive_get_name (G_DRIVE (source_object));
        primary = g_strdup_printf (_("Unable to poll %s for media changes"), name);

        handle_mount_unmount_failure (primary, error);

        g_free (name);
        g_free (primary);
        g_clear_error (&error);
    }
}

static void
rescan_shortcut_cb (GtkAction           *item,
		    NemoPlacesSidebar *sidebar)
{
	GtkTreeIter iter;
	GDrive  *drive;

	if (!get_selected_iter (sidebar, &iter)) {
		return;
	}

	gtk_tree_model_get (GTK_TREE_MODEL (sidebar->store_filter), &iter,
			    PLACES_SIDEBAR_COLUMN_DRIVE, &drive,
			    -1);

	if (drive != NULL) {
		g_drive_poll_for_media (drive, NULL, drive_poll_for_media_cb, NULL);
        g_object_unref (drive);
	}
}

static void
drive_start_cb (GObject      *source_object,
		GAsyncResult *res,
		gpointer      user_data)
{
	GError *error;

	error = NULL;

	if (!g_drive_start_finish (G_DRIVE (source_object), res, &error)) {
        char *name, *primary;

        name = g_drive_get_name (G_DRIVE (source_object));
        primary = g_strdup_printf (_("Unable to start %s"), name);

        handle_mount_unmount_failure (primary, error);

        g_free (name);
        g_free (primary);
        g_clear_error (&error);
    }
}

static void
start_shortcut_cb (GtkAction           *item,
		   NemoPlacesSidebar *sidebar)
{
	GtkTreeIter iter;
	GDrive  *drive;

	if (!get_selected_iter (sidebar, &iter)) {
		return;
	}

	gtk_tree_model_get (GTK_TREE_MODEL (sidebar->store_filter), &iter,
			    PLACES_SIDEBAR_COLUMN_DRIVE, &drive,
			    -1);

	if (drive != NULL) {
		GMountOperation *mount_op;

		mount_op = gtk_mount_operation_new (GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (sidebar))));

		g_drive_start (drive, G_DRIVE_START_NONE, mount_op, NULL, drive_start_cb, NULL);

		g_object_unref (mount_op);
        g_object_unref (drive);
	}
}

static void
drive_stop_cb (GObject *source_object,
	       GAsyncResult *res,
	       gpointer user_data)
{
	NemoWindow *window;
	GError *error;

	window = user_data;
	g_object_unref (window);

	error = NULL;

    if (!g_drive_stop_finish (G_DRIVE (source_object), res, &error)) {
        char *name, *primary;

        name = g_drive_get_name (G_DRIVE (source_object));
        primary = g_strdup_printf (_("Unable to stop %s"), name);

        handle_mount_unmount_failure (primary, error);

        g_free (name);
        g_free (primary);
        g_clear_error (&error);
    }
}

static void
stop_shortcut_cb (GtkAction           *item,
		  NemoPlacesSidebar *sidebar)
{
	GtkTreeIter iter;
	GDrive  *drive;

	if (!get_selected_iter (sidebar, &iter)) {
		return;
	}

	gtk_tree_model_get (GTK_TREE_MODEL (sidebar->store_filter), &iter,
			    PLACES_SIDEBAR_COLUMN_DRIVE, &drive,
			    -1);

	if (drive != NULL) {
        GMountOperation *mount_op = get_unmount_operation (sidebar);
		g_drive_stop (drive, G_MOUNT_UNMOUNT_NONE, mount_op, NULL, drive_stop_cb,
			      g_object_ref (sidebar->window));
		g_object_unref (mount_op);
        g_object_unref (drive);
	}
}

static void
empty_trash_cb (GtkAction           *item,
		NemoPlacesSidebar *sidebar)
{
	nemo_file_operations_empty_trash (GTK_WIDGET (sidebar->window));
}

static gboolean
find_prev_or_next_row (NemoPlacesSidebar *sidebar,
		       GtkTreeIter *iter,
		       gboolean go_up)
{
	GtkTreeModel *model = GTK_TREE_MODEL (sidebar->store_filter);
	gboolean res;
	int place_type;

	if (go_up) {
		res = gtk_tree_model_iter_previous (model, iter);
	} else {
		res = gtk_tree_model_iter_next (model, iter);
	}

	if (res) {
		gtk_tree_model_get (model, iter,
				    PLACES_SIDEBAR_COLUMN_ROW_TYPE, &place_type,
				    -1);
		if (place_type == PLACES_HEADING) {
			if (go_up) {
				res = gtk_tree_model_iter_previous (model, iter);
			} else {
				res = gtk_tree_model_iter_next (model, iter);
			}
		}
	}

	return res;
}

static gboolean
find_prev_row (NemoPlacesSidebar *sidebar, GtkTreeIter *iter)
{
	return find_prev_or_next_row (sidebar, iter, TRUE);
}

static gboolean
find_next_row (NemoPlacesSidebar *sidebar, GtkTreeIter *iter)
{
	return find_prev_or_next_row (sidebar, iter, FALSE);
}

static void
properties_cb (GtkAction           *item,
	       NemoPlacesSidebar *sidebar)
{
	GtkTreeModel *model;
	GtkTreePath *path = NULL;
	GtkTreeIter iter;
	GList *list;
	NemoFile *file;
	char *uri;

	model = gtk_tree_view_get_model (sidebar->tree_view);
	gtk_tree_view_get_cursor (sidebar->tree_view, &path, NULL);

	if (path == NULL || !gtk_tree_model_get_iter (model, &iter, path)) {
		gtk_tree_path_free (path);
		return;
	}

	gtk_tree_model_get (model, &iter, PLACES_SIDEBAR_COLUMN_URI, &uri, -1);

	if (uri != NULL) {

		file = nemo_file_get_by_uri (uri);
		list = g_list_prepend (NULL, nemo_file_ref (file));

		nemo_properties_window_present (list, GTK_WIDGET (sidebar), NULL);

		nemo_file_list_free (list);
		g_free (uri);
	}

	gtk_tree_path_free (path);
}

static gboolean
nemo_places_sidebar_focus (GtkWidget *widget,
			       GtkDirectionType direction)
{
	NemoPlacesSidebar *sidebar = NEMO_PLACES_SIDEBAR (widget);
	GtkTreePath *path;
	GtkTreeIter iter, child_iter;
	gboolean res;

	res = get_selected_iter (sidebar, &iter);
	if (!res) {
		gtk_tree_model_get_iter_first (GTK_TREE_MODEL (sidebar->store_filter), &iter);
        gtk_tree_model_iter_children (GTK_TREE_MODEL (sidebar->store_filter), &child_iter, &iter);
		res = find_next_row (sidebar, &child_iter);
		if (res) {
			path = gtk_tree_model_get_path (GTK_TREE_MODEL (sidebar->store_filter), &iter);
			gtk_tree_view_set_cursor (sidebar->tree_view, path, NULL, FALSE);
			gtk_tree_path_free (path);
		}
	}

	return GTK_WIDGET_CLASS (nemo_places_sidebar_parent_class)->focus (widget, direction);
}

/* Handler for GtkWidget::key-press-event on the shortcuts list */
static gboolean
bookmarks_key_press_event_cb (GtkWidget             *widget,
			      GdkEventKey           *event,
			      NemoPlacesSidebar *sidebar)
{
  guint modifiers;
  GtkTreeIter selected_iter;
  GtkTreePath *path;

  if (event->keyval == GDK_KEY_slash ||
    event->keyval == GDK_KEY_KP_Divide ||
    event->keyval == GDK_KEY_asciitilde) {
    if (gtk_bindings_activate_event (G_OBJECT (sidebar->window), event)) {
        return GDK_EVENT_STOP;
    }
  }

  if (!get_selected_iter (sidebar, &selected_iter)) {
	  return FALSE;
  }

  modifiers = gtk_accelerator_get_default_mod_mask ();

  if ((event->keyval == GDK_KEY_Return ||
       event->keyval == GDK_KEY_KP_Enter ||
       event->keyval == GDK_KEY_ISO_Enter ||
       event->keyval == GDK_KEY_space)) {
      NemoWindowOpenFlags flags = 0;

      if ((event->state & modifiers) == GDK_SHIFT_MASK) {
          flags = NEMO_WINDOW_OPEN_FLAG_NEW_TAB;
      } else if ((event->state & modifiers) == GDK_CONTROL_MASK) {
          flags = NEMO_WINDOW_OPEN_FLAG_NEW_WINDOW;
      }

      open_selected_bookmark (sidebar, GTK_TREE_MODEL (sidebar->store_filter),
			      &selected_iter, flags);
      return TRUE;
  }

  if (event->keyval == GDK_KEY_Down &&
      (event->state & modifiers) == GDK_MOD1_MASK) {
      return eject_or_unmount_selection (sidebar);
  }

  if (event->keyval == GDK_KEY_Up) {
      if (find_prev_row (sidebar, &selected_iter)) {
	      path = gtk_tree_model_get_path (GTK_TREE_MODEL (sidebar->store_filter), &selected_iter);
	      gtk_tree_view_set_cursor (sidebar->tree_view, path, NULL, FALSE);
	      gtk_tree_path_free (path);
      }
      return TRUE;
  }

  if (event->keyval == GDK_KEY_Down) {
      if (find_next_row (sidebar, &selected_iter)) {
	      path = gtk_tree_model_get_path (GTK_TREE_MODEL (sidebar->store_filter), &selected_iter);
	      gtk_tree_view_set_cursor (sidebar->tree_view, path, NULL, FALSE);
	      gtk_tree_path_free (path);
      }
      return TRUE;
  }

  if ((event->keyval == GDK_KEY_F2)
      && (event->state & modifiers) == 0) {
      rename_selected_bookmark (sidebar);
      return TRUE;
  }

  return FALSE;
}

static void
run_action_callback (GtkAction *action, gpointer user_data)
{
    NemoPlacesSidebar *sidebar = NEMO_PLACES_SIDEBAR (user_data);
    gchar *uri = NULL;
    GtkTreeIter iter;

    if (get_selected_iter (sidebar, &iter)) {
        gtk_tree_model_get (GTK_TREE_MODEL (sidebar->store_filter), &iter,
                    PLACES_SIDEBAR_COLUMN_URI, &uri,
                    -1);
    }

    if (!uri) {
        return;
    }

    NemoFile *file = nemo_file_get_by_uri (uri);
    NemoFile *parent = nemo_file_get_parent (file);
    GList *selection = g_list_prepend (NULL, file);

    nemo_action_activate (NEMO_ACTION (action), selection, parent, GTK_WINDOW (sidebar->window));

    nemo_file_list_free (selection);
    nemo_file_unref (parent);

    g_free (uri);
}

#if GTK_CHECK_VERSION (3, 24, 8)
static void
moved_to_rect_cb (GdkWindow          *window,
                  const GdkRectangle *flipped_rect,
                  const GdkRectangle *final_rect,
                  gboolean            flipped_x,
                  gboolean            flipped_y,
                  GtkMenu            *menu)
{
    g_signal_emit_by_name (menu,
                           "popped-up",
                           0,
                           flipped_rect,
                           final_rect,
                           flipped_x,
                           flipped_y);

    // Don't let the emission run in gtkmenu.c
    g_signal_stop_emission_by_name (window, "moved-to-rect");
}

static void
popup_menu_realized (GtkWidget    *menu,
                     gpointer      user_data)
{
    GdkWindow *toplevel;

    toplevel = gtk_widget_get_window (gtk_widget_get_toplevel (menu));

    g_signal_handlers_disconnect_by_func (toplevel, moved_to_rect_cb, menu);

    g_signal_connect (toplevel, "moved-to-rect", G_CALLBACK (moved_to_rect_cb),
                      menu);
}
#endif

/* ---------- Dakkar: per-item custom icons ("Change Icon…") ----------
 * Stored in the GSettings key dakkar-sidebar-item-icons (a{ss}):
 * stable item id -> icon spec (see dakkar-icon-picker.h).  Ids:
 *   place:home / place:recent / place:filesystem / place:trash / place:network
 *   uuid:<volume or mount UUID>, dev:<unix device>, drive:<unix device>
 *   uri:<location>  (bookmarks, favorites, other mounts and network places)
 * NULL means the row cannot carry a custom icon (headings). */
static gchar *
dakkar_sidebar_item_id (GtkTreeModel *model, GtkTreeIter *iter)
{
    PlaceType type = PLACES_HEADING;
    gchar *uri = NULL, *id = NULL;
    GDrive *drive = NULL;
    GVolume *volume = NULL;
    GMount *mount = NULL;

    gtk_tree_model_get (model, iter,
                        PLACES_SIDEBAR_COLUMN_ROW_TYPE, &type,
                        PLACES_SIDEBAR_COLUMN_URI, &uri,
                        PLACES_SIDEBAR_COLUMN_DRIVE, &drive,
                        PLACES_SIDEBAR_COLUMN_VOLUME, &volume,
                        PLACES_SIDEBAR_COLUMN_MOUNT, &mount, -1);
    if (type == PLACES_HEADING)
        goto out;
    if (mount != NULL || volume != NULL || drive != NULL) {
        GVolume *v = volume != NULL ? g_object_ref (volume) :
                     mount != NULL ? g_mount_get_volume (mount) : NULL;
        gchar *uuid = v != NULL ? g_volume_get_uuid (v) : NULL;
        if (uuid == NULL && mount != NULL) uuid = g_mount_get_uuid (mount);
        if (uuid != NULL) {
            id = g_strconcat ("uuid:", uuid, NULL);
        } else {
            gchar *dev = v != NULL ? g_volume_get_identifier (v, G_VOLUME_IDENTIFIER_KIND_UNIX_DEVICE) : NULL;
            if (dev != NULL)
                id = g_strconcat ("dev:", dev, NULL);
            else if (uri != NULL)
                id = g_strconcat ("uri:", uri, NULL);
            else if (drive != NULL) {
                gchar *did = g_drive_get_identifier (drive, G_DRIVE_IDENTIFIER_KIND_UNIX_DEVICE);
                if (did != NULL) id = g_strconcat ("drive:", did, NULL);
                g_free (did);
            }
            g_free (dev);
        }
        g_free (uuid);
        g_clear_object (&v);
        goto out;
    }
    if (uri == NULL)
        goto out;
    if (type == PLACES_BUILT_IN) {
        gchar *home = nemo_get_home_directory_uri ();
        if (g_strcmp0 (uri, home) == 0) id = g_strdup ("place:home");
        else if (g_str_has_prefix (uri, "recent:")) id = g_strdup ("place:recent");
        else if (g_strcmp0 (uri, "file:///") == 0) id = g_strdup ("place:filesystem");
        else if (g_str_has_prefix (uri, "trash:")) id = g_strdup ("place:trash");
        else if (g_str_has_prefix (uri, "network:")) id = g_strdup ("place:network");
        g_free (home);
        if (id != NULL) goto out;
    }
    id = g_strconcat ("uri:", uri, NULL);
out:
    g_clear_object (&drive);
    g_clear_object (&volume);
    g_clear_object (&mount);
    g_free (uri);
    return id;
}

static gchar *
dakkar_sidebar_icon_spec_for (const gchar *id)
{
    GVariant *icons;
    const gchar *found = NULL;
    gchar *spec = NULL;
    if (id == NULL) return NULL;
    icons = g_settings_get_value (nemo_preferences, "dakkar-sidebar-item-icons");
    if (g_variant_lookup (icons, id, "&s", &found) && dakkar_icon_spec_is_valid (found))
        spec = g_strdup (found);
    g_variant_unref (icons);
    return spec;
}

static void
dakkar_sidebar_save_item_icon (const gchar *id, const gchar *spec)
{
    GVariant *old;
    GVariantBuilder builder;
    GVariantIter iter;
    const gchar *key, *val;
    gboolean changed = spec != NULL;
    if (id == NULL) return;
    old = g_settings_get_value (nemo_preferences, "dakkar-sidebar-item-icons");
    g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{ss}"));
    g_variant_iter_init (&iter, old);
    while (g_variant_iter_next (&iter, "{&s&s}", &key, &val)) {
        if (g_strcmp0 (key, id) == 0) { changed = TRUE; continue; }
        g_variant_builder_add (&builder, "{ss}", key, val);
    }
    if (spec != NULL)
        g_variant_builder_add (&builder, "{ss}", id, spec);
    if (changed)
        g_settings_set_value (nemo_preferences, "dakkar-sidebar-item-icons",
                              g_variant_builder_end (&builder));
    else
        g_variant_unref (g_variant_ref_sink (g_variant_builder_end (&builder)));
    g_variant_unref (old);
}

/* Rendered custom icons, keyed by spec + size + scale + tint. Cleared
 * whenever the icons, colours, scale or the theme change. */
static GHashTable *dakkar_custom_icon_cache;

static void
dakkar_sidebar_custom_icons_flush (void)
{
    if (dakkar_custom_icon_cache != NULL)
        g_hash_table_remove_all (dakkar_custom_icon_cache);
}

static void
dakkar_sidebar_custom_icons_changed (GtkWidget *tree_view)
{
    dakkar_sidebar_custom_icons_flush ();
    gtk_widget_queue_draw (tree_view);
}

/* A NULL tint draws symbolic icons in the sidebar's text colour;
 * full-colour icons are never tinted (or turned into silhouettes). */
static cairo_surface_t *
dakkar_sidebar_custom_surface (GtkWidget *widget, const gchar *spec, gint pixels,
                              gint scale, const GdkRGBA *tint)
{
    cairo_surface_t *surface;
    gchar *tint_str = tint != NULL ? gdk_rgba_to_string (tint) : g_strdup ("-");
    gchar *key = g_strdup_printf ("%s\n%d\n%d\n%s", spec, pixels, scale, tint_str);
    g_free (tint_str);
    if (dakkar_custom_icon_cache == NULL)
        dakkar_custom_icon_cache = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
                                                         (GDestroyNotify) cairo_surface_destroy);
    surface = g_hash_table_lookup (dakkar_custom_icon_cache, key);
    if (surface == NULL && !g_hash_table_contains (dakkar_custom_icon_cache, key)) {
        GdkPixbuf *pixbuf = dakkar_icon_spec_load (spec, pixels, scale,
                                                  gtk_widget_get_style_context (widget),
                                                  tint, NULL);
        surface = pixbuf != NULL ? gdk_cairo_surface_create_from_pixbuf (pixbuf, scale, NULL) : NULL;
        g_clear_object (&pixbuf);
        /* a missing icon is remembered as NULL: the row falls back to its
         * default icon without retrying on every redraw */
        g_hash_table_insert (dakkar_custom_icon_cache, key, surface);
        key = NULL;
    }
    g_free (key);
    return surface;
}

/* A bookmark or place of a local folder shows that folder's own custom
 * icon (metadata::custom-icon / custom-icon-name, set with Change Icon… in
 * the views) unless the sidebar row has an icon of its own. */
static GHashTable *dakkar_folder_icon_files;   /* uri -> NemoFile (ref) */
static GSList *dakkar_folder_icon_trees;       /* sidebars to redraw */

static void
dakkar_folder_icon_redraw (void)
{
    GSList *l;
    for (l = dakkar_folder_icon_trees; l != NULL; l = l->next)
        gtk_widget_queue_draw (GTK_WIDGET (l->data));
}

static void
dakkar_folder_icon_ready (NemoFile *file, gpointer data)
{
    dakkar_folder_icon_redraw ();
    (void) file; (void) data;
}

static void
dakkar_folder_icon_tree_gone (GtkWidget *tree, gpointer data)
{
    dakkar_folder_icon_trees = g_slist_remove (dakkar_folder_icon_trees, tree);
    (void) data;
}

static gchar *
dakkar_sidebar_folder_spec (GtkWidget *tree, const gchar *uri)
{
    NemoFile *file;
    gchar *icon_uri, *icon_name, *spec;

    if (uri == NULL || !g_str_has_prefix (uri, "file://"))
        return NULL;
    if (tree != NULL && g_slist_find (dakkar_folder_icon_trees, tree) == NULL) {
        dakkar_folder_icon_trees = g_slist_prepend (dakkar_folder_icon_trees, tree);
        g_signal_connect (tree, "destroy", G_CALLBACK (dakkar_folder_icon_tree_gone), NULL);
    }
    if (dakkar_folder_icon_files == NULL)
        dakkar_folder_icon_files = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
                                                         (GDestroyNotify) nemo_file_unref);
    file = g_hash_table_lookup (dakkar_folder_icon_files, uri);
    if (file == NULL) {
        file = nemo_file_get_by_uri (uri);
        if (file == NULL)
            return NULL;
        g_hash_table_insert (dakkar_folder_icon_files, g_strdup (uri), file);
        g_signal_connect (file, "changed", G_CALLBACK (dakkar_folder_icon_ready), NULL);
        if (!nemo_file_check_if_ready (file, NEMO_FILE_ATTRIBUTE_INFO)) {
            nemo_file_call_when_ready (file, NEMO_FILE_ATTRIBUTE_INFO, dakkar_folder_icon_ready, NULL);
            return NULL;
        }
    }
    if (!nemo_file_check_if_ready (file, NEMO_FILE_ATTRIBUTE_INFO) || !nemo_file_is_directory (file))
        return NULL;
    icon_uri = nemo_file_get_metadata (file, NEMO_METADATA_KEY_CUSTOM_ICON, NULL);
    icon_name = nemo_file_get_metadata (file, NEMO_METADATA_KEY_CUSTOM_ICON_NAME, NULL);
    spec = dakkar_custom_icons_spec_for_metadata (icon_uri, icon_name);
    g_free (icon_uri);
    g_free (icon_name);
    return spec;
}

static void
dakkar_sidebar_change_icon (GtkMenuItem *item, NemoPlacesSidebar *sidebar)
{
    GtkTreeIter selected;
    gchar *id, *current, *name = NULL, *title, *spec;
    if (!get_selected_iter (sidebar, &selected)) return;
    id = dakkar_sidebar_item_id (GTK_TREE_MODEL (sidebar->store_filter), &selected);
    if (id == NULL) return;
    gtk_tree_model_get (GTK_TREE_MODEL (sidebar->store_filter), &selected,
                        PLACES_SIDEBAR_COLUMN_NAME, &name, -1);
    current = dakkar_sidebar_icon_spec_for (id);
    /* Translators: dialog title, %s is a sidebar item such as "Music" */
    title = g_strdup_printf (_("Change Icon of “%s”"), name != NULL ? name : "");
    spec = dakkar_icon_picker_run (GTK_WINDOW (sidebar->window), title, current);
    if (spec != NULL)
        dakkar_sidebar_save_item_icon (id, spec);
    g_free (spec);
    g_free (title);
    g_free (current);
    g_free (name);
    g_free (id);
    (void) item;
}

static void
dakkar_sidebar_reset_icon (GtkMenuItem *item, NemoPlacesSidebar *sidebar)
{
    GtkTreeIter selected;
    gchar *id;
    if (!get_selected_iter (sidebar, &selected)) return;
    id = dakkar_sidebar_item_id (GTK_TREE_MODEL (sidebar->store_filter), &selected);
    dakkar_sidebar_save_item_icon (id, NULL);
    g_free (id);
    (void) item;
}

/* Hidden for rows that cannot carry an icon; Reset only with a custom icon. */
static void
dakkar_sidebar_update_icon_items (NemoPlacesSidebar *sidebar)
{
    GtkWidget *change = g_object_get_data (G_OBJECT (sidebar->popup_menu), "dakkar-change-icon");
    GtkWidget *reset = g_object_get_data (G_OBJECT (sidebar->popup_menu), "dakkar-reset-icon");
    GtkTreeIter selected;
    gchar *id = NULL, *spec;
    if (change == NULL || reset == NULL) return;
    if (get_selected_iter (sidebar, &selected))
        id = dakkar_sidebar_item_id (GTK_TREE_MODEL (sidebar->store_filter), &selected);
    spec = dakkar_sidebar_icon_spec_for (id);
    {
        /* Preferences > Context Menus > Sidebar (live: read at every popup) */
        gboolean show_icon = g_settings_get_boolean (nemo_menu_config_preferences,
                                                     DAKKAR_MENU_SIDEBAR_CHANGE_ICON);
        gboolean show_color = g_settings_get_boolean (nemo_menu_config_preferences,
                                                      DAKKAR_MENU_SIDEBAR_ICON_COLOR);
        GtkWidget *color = g_object_get_data (G_OBJECT (sidebar->popup_menu), "dakkar-icon-color-menu");
        GtkWidget *color_reset = g_object_get_data (G_OBJECT (sidebar->popup_menu), "dakkar-icon-color-reset");
        GtkWidget *separator = g_object_get_data (G_OBJECT (sidebar->popup_menu), "dakkar-icon-separator");
        if (color != NULL) gtk_widget_set_visible (color, show_color);
        if (color_reset != NULL) gtk_widget_set_visible (color_reset, show_color);
        if (separator != NULL) gtk_widget_set_visible (separator, show_color || (show_icon && id != NULL));
        gtk_widget_set_visible (change, id != NULL && show_icon);
        gtk_widget_set_visible (reset, id != NULL && show_icon);
    }
    gtk_widget_set_sensitive (reset, spec != NULL);
    g_free (spec);
    g_free (id);
}

/* Explicit per-URI icon colors: mounted drives and individual bookmarks are
 * separately editable rather than sharing the category swatch. Only sidebar
 * icon metadata is changed; files, links and system icon themes are untouched. */
static void
dakkar_sidebar_save_item_color (const gchar *uri, const gchar *value)
{
    /* The key is a{ss}.  GVariantDict always builds a{sv}, which
     * g_settings_set_value() rejects (type mismatch): no colour was ever
     * stored, so "Set icon color" silently did nothing.  Build a{ss}. */
    GVariant *old = g_settings_get_value (nemo_preferences, "dakkar-sidebar-item-colors");
    GVariantBuilder builder;
    GVariantIter iter;
    const gchar *key, *val;
    g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{ss}"));
    g_variant_iter_init (&iter, old);
    while (g_variant_iter_next (&iter, "{&s&s}", &key, &val))
        if (g_strcmp0 (key, uri) != 0)
            g_variant_builder_add (&builder, "{ss}", key, val);
    if (value != NULL)
        g_variant_builder_add (&builder, "{ss}", uri, value);
    g_settings_set_value (nemo_preferences, "dakkar-sidebar-item-colors",
                          g_variant_builder_end (&builder));
    g_variant_unref (old);
}
/* A coloured local folder looks the same in the sidebar and in the views:
 * the per-item colour is mirrored into the folder's Dakkar folder-colour
 * metadata (the one the Properties window sets, used by nemo-file.c).
 * Only file:// locations; drives, network and virtual places are skipped. */
static void
dakkar_sidebar_mirror_folder_color (const gchar *uri, const gchar *value)
{
    NemoFile *file;
    if (uri == NULL || !g_str_has_prefix (uri, "file://") || g_strcmp0 (uri, "file:///") == 0)
        return;
    file = nemo_file_get_by_uri (uri);
    if (file != NULL) {
        nemo_file_set_dakkar_folder_color (file, value);
        nemo_file_unref (file);
    }
}

static void
dakkar_sidebar_edit_icon_color (GtkMenuItem *item, NemoPlacesSidebar *sidebar)
{
    GtkTreeIter selected;
    gchar *uri = NULL;
    GVariant *colors;
    const gchar *stored = NULL;
    GdkRGBA initial;
    if (!get_selected_iter (sidebar, &selected)) return;
    gtk_tree_model_get (GTK_TREE_MODEL (sidebar->store_filter), &selected,
                        PLACES_SIDEBAR_COLUMN_URI, &uri, -1);
    if (uri == NULL) return;
    GtkWidget *dialog = gtk_color_chooser_dialog_new (_("Icon color"),
        GTK_WINDOW (sidebar->window));
    colors = g_settings_get_value (nemo_preferences, "dakkar-sidebar-item-colors");
    if (g_variant_lookup (colors, uri, "&s", &stored) &&
        gdk_rgba_parse (&initial, stored))
        gtk_color_chooser_set_rgba (GTK_COLOR_CHOOSER (dialog), &initial);
    g_variant_unref (colors);
    if (gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_OK) {
        GdkRGBA chosen;
        gchar *value;
        gtk_color_chooser_get_rgba (GTK_COLOR_CHOOSER (dialog), &chosen);
        value = gdk_rgba_to_string (&chosen);
        /* Only this item is tinted; the other sidebar icons keep their
         * theme colours (forcing DAKKAR_SIDEBAR_ICONS_MONO on turned every
         * icon into a white silhouette). */
        dakkar_sidebar_save_item_color (uri, value);
        dakkar_sidebar_mirror_folder_color (uri, value);
        g_free (value);
    }
    gtk_widget_destroy (dialog);
    g_free (uri);
    (void)item;
}
static void
dakkar_sidebar_reset_icon_color (GtkMenuItem *item, NemoPlacesSidebar *sidebar)
{
    GtkTreeIter selected;
    gchar *uri = NULL;
    if (!get_selected_iter (sidebar, &selected)) return;
    gtk_tree_model_get (GTK_TREE_MODEL (sidebar->store_filter), &selected,
                        PLACES_SIDEBAR_COLUMN_URI, &uri, -1);
    if (uri != NULL) {
        dakkar_sidebar_save_item_color (uri, NULL);
        dakkar_sidebar_mirror_folder_color (uri, NULL);
        g_free (uri);
    }
    (void)item;
}

/* Every row below the Favorites heading (XApp favorite, standard folder,
 * bookmark) shares one display order, so any of them can move past the
 * others. */
static gboolean
dakkar_is_favorites_row (PlaceType type, SectionType section)
{
    if (type == PLACES_FAVORITE || type == PLACES_XDG_DIR)
        return section == SECTION_FAVORITES;
    if (type == PLACES_BOOKMARK)
        return section == SECTION_BOOKMARKS || section == SECTION_FAVORITES;
    return FALSE;
}

static gchar *
dakkar_selected_favorite_uri (NemoPlacesSidebar *sidebar)
{
    GtkTreeIter iter;
    PlaceType type;
    SectionType section;
    gchar *uri = NULL;

    if (!get_selected_iter (sidebar, &iter))
        return NULL;
    gtk_tree_model_get (GTK_TREE_MODEL (sidebar->store_filter), &iter,
                        PLACES_SIDEBAR_COLUMN_ROW_TYPE, &type,
                        PLACES_SIDEBAR_COLUMN_SECTION_TYPE, &section,
                        PLACES_SIDEBAR_COLUMN_URI, &uri, -1);
    if (!dakkar_is_favorites_row (type, section)) {
        g_free (uri);
        return NULL;
    }
    return uri;
}

static void
dakkar_sidebar_move_favorite (NemoPlacesSidebar *sidebar, gint direction)
{
    gchar *uri = dakkar_selected_favorite_uri (sidebar);
    if (uri != NULL)
        dakkar_favorites_move (uri, direction);   /* sidebar reloads on the setting change */
    g_free (uri);
}

static void
dakkar_sidebar_favorite_up (GtkMenuItem *item, NemoPlacesSidebar *sidebar)
{
    dakkar_sidebar_move_favorite (sidebar, -1);
    (void) item;
}

static void
dakkar_sidebar_favorite_down (GtkMenuItem *item, NemoPlacesSidebar *sidebar)
{
    dakkar_sidebar_move_favorite (sidebar, 1);
    (void) item;
}

static void
dakkar_sidebar_update_move_items (NemoPlacesSidebar *sidebar)
{
    GtkWidget *up, *down;
    gchar *uri;
    gint position = -1, length = 0;
    GList *favorites, *node;

    up = g_object_get_data (G_OBJECT (sidebar->popup_menu), "dakkar-fav-up");
    down = g_object_get_data (G_OBJECT (sidebar->popup_menu), "dakkar-fav-down");
    GtkWidget *separator = g_object_get_data (G_OBJECT (sidebar->popup_menu),
                                               "dakkar-fav-separator");
    if (up == NULL || down == NULL) return;
    uri = dakkar_selected_favorite_uri (sidebar);
    if (separator != NULL)
        gtk_widget_set_visible (separator, uri != NULL);
    gtk_widget_set_visible (up, uri != NULL);
    gtk_widget_set_visible (down, uri != NULL);
    if (uri != NULL) {
        favorites = dakkar_favorites_collect ();
        for (node = favorites; node != NULL; node = node->next) {
            if (g_strcmp0 (uri, ((DakkarFavorite *) node->data)->uri) == 0)
                position = length;
            length++;
        }
        gtk_widget_set_sensitive (up, position > 0);
        gtk_widget_set_sensitive (down, position >= 0 && position < length - 1);
        g_list_free_full (favorites, (GDestroyNotify) dakkar_favorite_free);
    }
    g_free (uri);
}

static void
bookmarks_popup_menu (NemoPlacesSidebar *sidebar,
		      GdkEventButton        *event)
{
    update_menu_states (sidebar);
    if (sidebar->popup_menu != NULL &&
        g_object_get_data (G_OBJECT (sidebar->popup_menu), "dakkar-icon-color-menu") == NULL) {
        GtkWidget *separator = gtk_separator_menu_item_new ();
        GtkWidget *choose = gtk_menu_item_new_with_label (_("Set icon color…"));
        GtkWidget *reset = gtk_menu_item_new_with_label (_("Reset icon color"));
        gtk_menu_shell_append (GTK_MENU_SHELL (sidebar->popup_menu), separator);
        gtk_menu_shell_append (GTK_MENU_SHELL (sidebar->popup_menu), choose);
        gtk_menu_shell_append (GTK_MENU_SHELL (sidebar->popup_menu), reset);
        g_signal_connect (choose, "activate", G_CALLBACK (dakkar_sidebar_edit_icon_color), sidebar);
        g_signal_connect (reset, "activate", G_CALLBACK (dakkar_sidebar_reset_icon_color), sidebar);
        gtk_widget_show_all (separator);
        gtk_widget_show_all (choose);
        gtk_widget_show_all (reset);
        g_object_set_data (G_OBJECT (sidebar->popup_menu), "dakkar-icon-color-menu", choose);
        g_object_set_data (G_OBJECT (sidebar->popup_menu), "dakkar-icon-color-reset", reset);
        g_object_set_data (G_OBJECT (sidebar->popup_menu), "dakkar-icon-separator", separator);
        {
            GtkWidget *change = gtk_menu_item_new_with_mnemonic (_("Change _Icon…"));
            GtkWidget *reset_icon = gtk_menu_item_new_with_mnemonic (_("Reset Ic_on"));
            gtk_menu_shell_append (GTK_MENU_SHELL (sidebar->popup_menu), change);
            gtk_menu_shell_append (GTK_MENU_SHELL (sidebar->popup_menu), reset_icon);
            g_signal_connect (change, "activate", G_CALLBACK (dakkar_sidebar_change_icon), sidebar);
            g_signal_connect (reset_icon, "activate", G_CALLBACK (dakkar_sidebar_reset_icon), sidebar);
            gtk_widget_show (change);
            gtk_widget_show (reset_icon);
            g_object_set_data (G_OBJECT (sidebar->popup_menu), "dakkar-change-icon", change);
            g_object_set_data (G_OBJECT (sidebar->popup_menu), "dakkar-reset-icon", reset_icon);
        }
    }
    if (sidebar->popup_menu != NULL &&
        g_object_get_data (G_OBJECT (sidebar->popup_menu), "dakkar-fav-up") == NULL) {
        GtkWidget *separator = gtk_separator_menu_item_new ();
        GtkWidget *up = gtk_menu_item_new_with_label (_("Move Favorite Up"));
        GtkWidget *down = gtk_menu_item_new_with_label (_("Move Favorite Down"));
        gtk_menu_shell_append (GTK_MENU_SHELL (sidebar->popup_menu), separator);
        gtk_menu_shell_append (GTK_MENU_SHELL (sidebar->popup_menu), up);
        gtk_menu_shell_append (GTK_MENU_SHELL (sidebar->popup_menu), down);
        g_signal_connect (up, "activate", G_CALLBACK (dakkar_sidebar_favorite_up), sidebar);
        g_signal_connect (down, "activate", G_CALLBACK (dakkar_sidebar_favorite_down), sidebar);
        gtk_widget_show (separator);
        gtk_widget_show (up);
        gtk_widget_show (down);
        g_object_set_data (G_OBJECT (sidebar->popup_menu), "dakkar-fav-separator", separator);
        g_object_set_data (G_OBJECT (sidebar->popup_menu), "dakkar-fav-up", up);
        g_object_set_data (G_OBJECT (sidebar->popup_menu), "dakkar-fav-down", down);
    }
    if (sidebar->popup_menu != NULL) {
        dakkar_sidebar_update_move_items (sidebar);
        dakkar_sidebar_update_icon_items (sidebar);
    }
    eel_pop_up_context_menu (GTK_MENU(sidebar->popup_menu),
                             (GdkEvent *) event,
                             GTK_WIDGET (sidebar));
}

/* Callback used for the GtkWidget::popup-menu signal of the shortcuts list */
static gboolean
bookmarks_popup_menu_cb (GtkWidget *widget,
			 NemoPlacesSidebar *sidebar)
{
	bookmarks_popup_menu (sidebar, NULL);
	return TRUE;
}

static void
reset_menu (NemoPlacesSidebar *sidebar)
{
    sidebar->actions_need_update = TRUE;
    rebuild_menu (sidebar);
}

static gboolean
actions_changed_idle_cb (gpointer user_data)
{
    NemoPlacesSidebar *sidebar = NEMO_PLACES_SIDEBAR (user_data);

    reset_menu (sidebar);

    sidebar->actions_changed_idle_id = 0;
    return G_SOURCE_REMOVE;
}

static void
actions_changed (gpointer user_data)
{
    NemoPlacesSidebar *sidebar = NEMO_PLACES_SIDEBAR (user_data);

    g_clear_handle_id (&sidebar->actions_changed_idle_id, g_source_remove);
    sidebar->actions_changed_idle_id = g_idle_add (actions_changed_idle_cb, sidebar);
}

static void
add_action_to_ui (NemoActionManager    *manager,
                  GtkAction            *action,
                  GtkUIManagerItemType  type,
                  const gchar          *path,
                  const gchar          *accelerator,
                  gpointer              user_data)
{
    NemoPlacesSidebar *sidebar = NEMO_PLACES_SIDEBAR (user_data);

    static const gchar *roots[] = {
        "/selection/PlacesSidebarActionsPlaceholder",
        NULL,
    };

    nemo_action_manager_add_action_ui (manager,
                                       sidebar->ui_manager,
                                       action,
                                       path,
                                       accelerator,
                                       sidebar->action_action_group,
                                       sidebar->action_action_group_merge_id,
                                       roots,
                                       type,
                                       G_CALLBACK (run_action_callback),
                                       sidebar);
}

static void
clear_ui (NemoPlacesSidebar *sidebar)
{

    nemo_ui_unmerge_ui (sidebar->ui_manager,
                        &sidebar->bookmark_action_group_merge_id,
                        &sidebar->bookmark_action_group);

    nemo_ui_unmerge_ui (sidebar->ui_manager,
                        &sidebar->action_action_group_merge_id,
                        &sidebar->action_action_group);

}

static const GtkActionEntry bookmark_action_entries[] = {
    { NEMO_ACTION_OPEN,                    "folder-open-symbolic", N_("_Open"),                NULL, NULL, G_CALLBACK (open_shortcut_cb)               },
    { NEMO_ACTION_OPEN_IN_NEW_TAB,         NULL,                   N_("Open in New _Tab"),     NULL, NULL, G_CALLBACK (open_shortcut_in_new_tab_cb)    },
    { NEMO_ACTION_OPEN_ALTERNATE,          NULL,                   N_("Open in New _Window"),  NULL, NULL, G_CALLBACK (open_shortcut_in_new_window_cb) },
    { NEMO_ACTION_ADD_BOOKMARK,            NULL,                   N_("_Add Bookmark"),        NULL, NULL, G_CALLBACK (add_shortcut_cb)                },
    { NEMO_ACTION_SIDEBAR_REMOVE,          "list-remove-symbolic", N_("Remove"),               NULL, NULL, G_CALLBACK (remove_shortcut_cb)             },
    { NEMO_ACTION_RENAME,                  NULL,                   N_("_Rename..."),           NULL, NULL, G_CALLBACK (rename_shortcut_cb)             },
    { NEMO_ACTION_MOUNT_VOLUME,            NULL,                   N_("_Mount"),               NULL, NULL, G_CALLBACK (mount_shortcut_cb)              },
    { NEMO_ACTION_UNMOUNT_VOLUME,          NULL,                   N_("_Unmount"),             NULL, NULL, G_CALLBACK (unmount_shortcut_cb)            },
    { NEMO_ACTION_EJECT_VOLUME,            NULL,                   N_("_Eject"),               NULL, NULL, G_CALLBACK (eject_shortcut_cb)              },
    { NEMO_ACTION_SIDEBAR_DETECT_MEDIA,    NULL,                   N_("_Detect Media"),        NULL, NULL, G_CALLBACK (rescan_shortcut_cb)             },
    { NEMO_ACTION_START_VOLUME,            NULL,                   N_("_Start"),               NULL, NULL, G_CALLBACK (start_shortcut_cb)              },
    { NEMO_ACTION_STOP_VOLUME,             NULL,                   N_("_Stop"),                NULL, NULL, G_CALLBACK (stop_shortcut_cb)               },
    { NEMO_ACTION_EMPTY_TRASH_CONDITIONAL, NULL,                   N_("_Empty _Trash"),        NULL, NULL, G_CALLBACK (empty_trash_cb)                 },
    { NEMO_ACTION_PROPERTIES,              NULL,                   N_("_Properties"),          NULL, NULL, G_CALLBACK (properties_cb)                  },
};

static void
rebuild_menu (NemoPlacesSidebar *sidebar)
{
    if (!gtk_widget_get_realized (GTK_WIDGET (sidebar))) {
        return;
    }

    if (!sidebar->actions_need_update) {
        return;
    }

    clear_ui (sidebar);

    nemo_ui_prepare_merge_ui (sidebar->ui_manager,
                              "NemoPlacesSidebarBookmarkActions",
                              &sidebar->bookmark_action_group_merge_id,
                              &sidebar->bookmark_action_group);

    nemo_ui_prepare_merge_ui (sidebar->ui_manager,
                              "NemoPlacesSidebarActionActions",
                              &sidebar->action_action_group_merge_id,
                              &sidebar->action_action_group);

    sidebar->bookmark_action_group_merge_id =
            gtk_ui_manager_add_ui_from_resource (sidebar->ui_manager, "/org/dakkar/nemo-places-sidebar-ui.xml", NULL);

    gtk_action_group_add_actions (sidebar->bookmark_action_group,
                                  bookmark_action_entries,
                                  G_N_ELEMENTS (bookmark_action_entries),
                                  sidebar);

    nemo_action_manager_iterate_actions (sidebar->action_manager,
                                         (NemoActionManagerIterFunc) add_action_to_ui,
                                         sidebar);

    if (sidebar->popup_menu == NULL) {
        GtkWidget *menu = gtk_ui_manager_get_widget (sidebar->ui_manager, "/selection");
        gtk_menu_set_screen (GTK_MENU (menu), gtk_widget_get_screen (GTK_WIDGET (sidebar->window)));
        sidebar->popup_menu = menu;

#if GTK_CHECK_VERSION (3, 24, 8)
        g_signal_connect (sidebar->popup_menu, "realize",
                          G_CALLBACK (popup_menu_realized),
                          sidebar);
        gtk_widget_realize (sidebar->popup_menu);
#endif

        gtk_widget_show (menu);
    }

    sidebar->actions_need_update = FALSE;
}

static gboolean
bookmarks_button_release_event_cb (GtkWidget *widget,
				   GdkEventButton *event,
				   NemoPlacesSidebar *sidebar)
{
	GtkTreePath *path;
	GtkTreeIter iter;
	GtkTreeModel *model;
	GtkTreeView *tree_view;
	gboolean res;

	path = NULL;

	if (event->type != GDK_BUTTON_RELEASE) {
		return TRUE;
	}

	if (clicked_eject_button (sidebar, &path)) {
		eject_or_unmount_bookmark (sidebar, path);
		gtk_tree_path_free (path);

		return FALSE;
	}

	tree_view = GTK_TREE_VIEW (widget);
	model = gtk_tree_view_get_model (tree_view);

	if (event->button == 1) {

		if (event->window != gtk_tree_view_get_bin_window (tree_view)) {
			return FALSE;
		}

		res = gtk_tree_view_get_path_at_pos (tree_view, (int) event->x, (int) event->y,
						     &path, NULL, NULL, NULL);

		if (!res) {
			return FALSE;
		}

		gtk_tree_model_get_iter (model, &iter, path);

		open_selected_bookmark (sidebar, model, &iter, 0);

		gtk_tree_path_free (path);
	}

	return FALSE;
}

static gboolean
bookmarks_button_press_event_cb (GtkWidget             *widget,
				 GdkEventButton        *event,
				 NemoPlacesSidebar *sidebar)

{
	GtkTreeModel *model;
	GtkTreeView *tree_view;
	GtkTreeIter iter;
	GtkTreePath *path = NULL;
	gboolean retval = FALSE;
	PlaceType row_type;

	if (event->type != GDK_BUTTON_PRESS) {
		/* ignore multiple clicks */
		return TRUE;
	}

	tree_view = GTK_TREE_VIEW (widget);
	model = gtk_tree_view_get_model (tree_view);
	gtk_tree_view_get_path_at_pos (tree_view, (int) event->x, (int) event->y,
				       &path, NULL, NULL, NULL);

	if (path == NULL || !gtk_tree_model_get_iter (model, &iter, path)) {
		return FALSE;
	}

	if (event->button == 3) {
		gtk_tree_model_get (model, &iter,
				    PLACES_SIDEBAR_COLUMN_ROW_TYPE, &row_type,
				    -1);

		if (row_type != PLACES_HEADING) {
			/* Select the clicked row FIRST: the menu (its sensitivity and
			 * every action, incl. Set icon color) works on the selected
			 * row, and GtkTreeView only selects it after this handler. */
			gtk_tree_selection_select_path (gtk_tree_view_get_selection (tree_view), path);
			bookmarks_popup_menu (sidebar, event);
		}
	} else if (event->button == 2) {
		NemoWindowOpenFlags flags = 0;

		if (g_settings_get_boolean (nemo_preferences,
					    NEMO_PREFERENCES_ALWAYS_USE_BROWSER)) {
			flags = (event->state & GDK_CONTROL_MASK) ?
				NEMO_WINDOW_OPEN_FLAG_NEW_WINDOW :
				NEMO_WINDOW_OPEN_FLAG_NEW_TAB;
		} else {
			flags = NEMO_WINDOW_OPEN_FLAG_CLOSE_BEHIND;
		}

		open_selected_bookmark (sidebar, model, &iter, flags);
		retval = TRUE;
	}

	gtk_tree_path_free (path);

	return retval;
}

typedef struct {
    NemoPlacesSidebar *sidebar;
    GtkTreePath *hovered_path;
} ClearHoverData;

static gboolean
clear_eject_hover (GtkTreeModel *model,
                    GtkTreePath  *path,
                    GtkTreeIter  *iter,
                    gpointer      data)
{
    ClearHoverData *hdata = data;
    gint size;
    gboolean can_eject = FALSE;

    gtk_tree_model_get (model, iter,
                        PLACES_SIDEBAR_COLUMN_EJECT, &can_eject,
                        -1);

    if (can_eject && hdata->hovered_path != NULL && gtk_tree_path_compare (path, hdata->hovered_path) == 0) {
        size = EJECT_ICON_SIZE_HOVERED;

    } else {
        size = EJECT_ICON_SIZE_NOT_HOVERED;
    }

    gtk_tree_store_set (hdata->sidebar->store, iter,
                        PLACES_SIDEBAR_COLUMN_EJECT_ICON_SIZE, size,
                        -1);

    return FALSE;
}

static gboolean
motion_notify_cb (GtkWidget         *widget,
                  GdkEventMotion    *event,
                  NemoPlacesSidebar *sidebar)
{
    GtkTreeModel *model;
    GtkTreePath *path = NULL;
    GtkTreePath *store_path = NULL;
    gboolean editing;

    if (event->type != GDK_MOTION_NOTIFY) {
        return TRUE;
    }

    g_object_get (sidebar->editable_renderer, "editing", &editing, NULL);
    if (editing) {
        return GDK_EVENT_PROPAGATE;
    }

    model = gtk_tree_view_get_model (GTK_TREE_VIEW (sidebar->tree_view));

    if (over_eject_button (sidebar, event->x, event->y, &path)) {
        store_path = gtk_tree_model_filter_convert_path_to_child_path (GTK_TREE_MODEL_FILTER (model), path);
    }

    ClearHoverData data = { sidebar, store_path };
    gtk_tree_model_foreach (GTK_TREE_MODEL (sidebar->store), (GtkTreeModelForeachFunc) clear_eject_hover, &data);

    if (store_path != NULL) {
        gtk_tree_path_free (store_path);
    }

    gtk_tree_path_free (path);

    return FALSE;
}

static gboolean
leave_notify_cb (GtkWidget         *widget,
                 GdkEventCrossing  *event,
                 NemoPlacesSidebar *sidebar)
{
    gboolean editing;

    if (event->type != GDK_LEAVE_NOTIFY) {
        return TRUE;
    }

    g_object_get (sidebar->editable_renderer, "editing", &editing, NULL);
    if (editing) {
        return GDK_EVENT_PROPAGATE;
    }

    ClearHoverData data = { sidebar, NULL };
    gtk_tree_model_foreach (GTK_TREE_MODEL (sidebar->store), (GtkTreeModelForeachFunc) clear_eject_hover, &data);

    return FALSE;
}


static gboolean
query_tooltip_callback (GtkWidget *widget,
                        gint x,
                        gint y,
                        gboolean kb_mode,
                        GtkTooltip *tooltip,
                        gpointer user_data)
{
    GtkTreeIter iter;
    GtkTreePath *path = NULL;
    GtkTreeModel *model;

    if (gtk_tree_view_get_tooltip_context (GTK_TREE_VIEW (widget), &x, &y,
                                           kb_mode,
                                           &model, &path, &iter)) {
        gboolean can_eject;
        gint icon_size, type;

        gtk_tree_model_get (model,
                            &iter,
                            PLACES_SIDEBAR_COLUMN_ROW_TYPE, &type,
                            PLACES_SIDEBAR_COLUMN_EJECT, &can_eject,
                            /* The 'hovered' bool column needs an accompanying
                             * icon-size column for the cell renderer; this is
                             * simpler than a custom cell_data_func. */
                            PLACES_SIDEBAR_COLUMN_EJECT_ICON_SIZE, &icon_size,
                            -1);

        if (type == PLACES_HEADING) {
            gtk_tree_path_free (path);
            return FALSE;
        }

        g_autofree gchar *tooltip_markup = NULL;

        if (can_eject && icon_size == EJECT_ICON_SIZE_HOVERED) {
            g_autoptr(GMount) mount = NULL;
            g_autoptr(GDrive) drive = NULL;
            g_autoptr(GVolume) volume = NULL;

            gtk_tree_model_get (model,
                                &iter,
                                PLACES_SIDEBAR_COLUMN_MOUNT, &mount,
                                PLACES_SIDEBAR_COLUMN_DRIVE, &drive,
                                PLACES_SIDEBAR_COLUMN_VOLUME, &volume,
                                -1);
            if (mount != NULL) {
                tooltip_markup = g_strdup (_("Unmount"));
            }
            else
            if (drive != NULL) {
                tooltip_markup = g_strdup (_("Eject"));
            }
            else
            if (volume != NULL) {
                tooltip_markup = g_strdup (_("Stop"));
            }
        } else {
            gtk_tree_model_get (model,
                                &iter,
                                PLACES_SIDEBAR_COLUMN_TOOLTIP, &tooltip_markup,
                                -1);
        }

        gtk_tooltip_set_markup (tooltip, tooltip_markup);
        gtk_tree_view_set_tooltip_cell (GTK_TREE_VIEW (widget), tooltip, path, NULL, NULL);

        gtk_tree_path_free (path);
        return TRUE;
    }

    return FALSE;
}


static void
update_expanded_state (GtkTreeView *tree_view,
                       GtkTreeIter *iter,
                       GtkTreePath *path,
                           gpointer user_data,
                           gboolean expanded)
{
    NemoPlacesSidebar *sidebar = NEMO_PLACES_SIDEBAR (user_data);

    if (sidebar->updating_sidebar)
        return;

    SectionType type;
    GtkTreeIter heading_iter;
    GtkTreeModel *model = gtk_tree_view_get_model (tree_view);
    gtk_tree_model_get_iter (GTK_TREE_MODEL (model), &heading_iter, path);
    gtk_tree_model_get (model, iter,
                    PLACES_SIDEBAR_COLUMN_SECTION_TYPE, &type,
                    -1);
    if (type == SECTION_COMPUTER) {
        sidebar->my_computer_expanded = expanded;
        g_settings_set_boolean (nemo_window_state, NEMO_WINDOW_STATE_MY_COMPUTER_EXPANDED, expanded);
    } else if (type == SECTION_FAVORITES) {
        sidebar->favorites_expanded = expanded;
        g_settings_set_boolean (nemo_window_state, "favorites-expanded", expanded);
    } else if (type == SECTION_BOOKMARKS) {
        sidebar->bookmarks_expanded = expanded;
        g_settings_set_boolean (nemo_window_state, NEMO_WINDOW_STATE_BOOKMARKS_EXPANDED, expanded);
    } else if (type == SECTION_DEVICES) {
        sidebar->devices_expanded = expanded;
        g_settings_set_boolean (nemo_window_state, NEMO_WINDOW_STATE_DEVICES_EXPANDED, expanded);
    } else if (type == SECTION_NETWORK) {
        sidebar->network_expanded = expanded;
        g_settings_set_boolean (nemo_window_state, NEMO_WINDOW_STATE_NETWORK_EXPANDED, expanded);
    }
}

static void
row_collapsed_cb (GtkTreeView *tree_view,
                              GtkTreeIter *iter,
                              GtkTreePath *path,
                              gpointer user_data)
{
    update_expanded_state (tree_view,
                           iter,
                           path,
                           user_data,
                           FALSE);
}


static void
row_expanded_cb (GtkTreeView *tree_view,
                              GtkTreeIter *iter,
                              GtkTreePath *path,
                              gpointer user_data)
{
    update_expanded_state (tree_view,
                           iter,
                           path,
                           user_data,
                           TRUE);
}

static void
row_activated_cb (GtkTreeView       *tree_view,
                  GtkTreePath       *path,
                  GtkTreeViewColumn *column,
                  gpointer           user_data)
{
    GtkTreeIter iter;
    SectionType section_type;
    PlaceType place_type;

    NemoPlacesSidebar *sidebar = NEMO_PLACES_SIDEBAR (user_data);
    GtkTreeModel *model = gtk_tree_view_get_model (tree_view);
    gtk_tree_model_get_iter (GTK_TREE_MODEL (model), &iter, path);
    gtk_tree_model_get (model, &iter,
                    PLACES_SIDEBAR_COLUMN_SECTION_TYPE, &section_type,
                    PLACES_SIDEBAR_COLUMN_ROW_TYPE, &place_type,
                    -1);
    if (place_type == PLACES_HEADING) {
        if (section_type == SECTION_COMPUTER) {
            sidebar->my_computer_expanded = !sidebar->my_computer_expanded;
        } else if (section_type == SECTION_FAVORITES) {
            sidebar->favorites_expanded = !sidebar->favorites_expanded;
        } else if (section_type == SECTION_BOOKMARKS) {
            sidebar->bookmarks_expanded = !sidebar->bookmarks_expanded;
        } else if (section_type == SECTION_DEVICES) {
            sidebar->devices_expanded = !sidebar->devices_expanded;
        } else if (section_type == SECTION_NETWORK) {
            sidebar->network_expanded = !sidebar->network_expanded;
        }
        restore_expand_state (sidebar);
    }
}

static void
bookmarks_edited (GtkCellRenderer       *cell,
		  gchar                 *path_string,
		  gchar                 *new_text,
		  NemoPlacesSidebar *sidebar)
{
	GtkTreePath *path;
	GtkTreeIter iter;
	NemoBookmark *bookmark;
	int index;

	g_object_set (cell, "editable", FALSE, NULL);

	path = gtk_tree_path_new_from_string (path_string);
	gtk_tree_model_get_iter (GTK_TREE_MODEL (sidebar->store_filter), &iter, path);
	gtk_tree_model_get (GTK_TREE_MODEL (sidebar->store_filter), &iter,
		            PLACES_SIDEBAR_COLUMN_INDEX, &index,
		            -1);
	gtk_tree_path_free (path);
	bookmark = nemo_bookmark_list_item_at (sidebar->bookmarks, index);

	if (bookmark != NULL) {
		nemo_bookmark_set_custom_name (bookmark, new_text);
	}
}

static void
bookmarks_editing_canceled (GtkCellRenderer       *cell,
			    NemoPlacesSidebar *sidebar)
{
	g_object_set (cell, "editable", FALSE, NULL);
}

static void
trash_state_changed_cb (NemoTrashMonitor *trash_monitor,
			gboolean             state,
			gpointer             data)
{
	NemoPlacesSidebar *sidebar;

	sidebar = NEMO_PLACES_SIDEBAR (data);

	/* The trash icon changed, update the sidebar */
	update_places (sidebar);

	reset_menu (sidebar);
}

static void
favorites_changed_cb (gpointer data)
{
    NemoPlacesSidebar *sidebar = NEMO_PLACES_SIDEBAR (data);

    update_places (sidebar);
}

static gboolean
tree_selection_func (GtkTreeSelection *selection,
		     GtkTreeModel *model,
		     GtkTreePath *path,
		     gboolean path_currently_selected,
		     gpointer user_data)
{
	GtkTreeIter iter;
	PlaceType row_type;

	gtk_tree_model_get_iter (model, &iter, path);
	gtk_tree_model_get (model, &iter,
			    PLACES_SIDEBAR_COLUMN_ROW_TYPE, &row_type,
			    -1);

	if (row_type == PLACES_HEADING) {
		return FALSE;
	}

	return TRUE;
}

/* Match the sidebar zoom control to both text and icon size.  GtkIconSize
 * values are registered once for each pixel size and reused per row. */
/* Sidebar icon pixels: the dedicated "Sidebar icon size" (icons only, the
 * text keeps its size), else 16px times the sidebar icon & text scale. */
gint
dakkar_sidebar_icon_pixels (void)
{
    gint size;
    if (nemo_preferences == NULL) return 16;
    size = g_settings_get_int (nemo_preferences, DAKKAR_SIDEBAR_ICON_SIZE);
    if (size > 0)
        return CLAMP (size, 12, 48);
    return CLAMP ((gint) (16.0 * g_settings_get_double (nemo_preferences,
                              NEMO_PREFERENCES_FILES_SIDEBAR_SCALE) + 0.5), 10, 31);
}

static GtkIconSize
files_sidebar_icon_size (void)
{
    static GtkIconSize sizes[39] = { 0 };
    gint px = dakkar_sidebar_icon_pixels ();

    if (sizes[px - 10] == 0) {
        gchar *name = g_strdup_printf ("files-sidebar-size-%d", px);
        sizes[px - 10] = gtk_icon_size_register (name, px, px);
        g_free (name);
    }
    return sizes[px - 10];
}

/* GtkCellRendererDisk paints text independently of the parent TreeView CSS.
 * Apply the zoom directly to its cell renderer, not just the TreeView style.
 * The same scale is used for both regular rows and section headings. */
static void disk_cell_renderer_func (GtkTreeViewColumn *column, GtkCellRenderer *cell,
                                     GtkTreeModel *model, GtkTreeIter *iter, gpointer user_data);

static void
files_sidebar_text_scale_func (GtkTreeViewColumn *column,
                              GtkCellRenderer *cell,
                              GtkTreeModel *model,
                              GtkTreeIter *iter,
                              gpointer user_data)
{
    gdouble zoom = CLAMP (g_settings_get_double (nemo_preferences,
                           NEMO_PREFERENCES_FILES_SIDEBAR_SCALE), 0.65, 1.85);
    g_object_set (cell, "scale", zoom, "scale-set", TRUE,
                  "ypad", CLAMP (g_settings_get_int (nemo_preferences,
                                    DAKKAR_SIDEBAR_ROW_PADDING), 1, 12), NULL);
    if (NEMO_IS_CELL_RENDERER_DISK (cell))
        disk_cell_renderer_func (column, cell, model, iter, user_data);
}

void
nemo_places_sidebar_refresh_scale (GtkWidget *widget)
{
    GtkWidget *tree;
    g_return_if_fail (NEMO_IS_PLACES_SIDEBAR (widget));
    tree = gtk_bin_get_child (GTK_BIN (widget));
    if (GTK_IS_VIEWPORT (tree))
        tree = gtk_bin_get_child (GTK_BIN (tree));
    if (GTK_IS_TREE_VIEW (tree)) {
        GList *columns = gtk_tree_view_get_columns (GTK_TREE_VIEW (tree));
        GList *c;
        for (c = columns; c != NULL; c = c->next)
            gtk_tree_view_column_queue_resize (GTK_TREE_VIEW_COLUMN (c->data));
        g_list_free (columns);
        gtk_widget_queue_resize (tree);
        gtk_widget_queue_draw (tree);
    }
    gtk_widget_queue_resize (widget);
}

/* Per-location tinting only when explicitly enabled. In normal mode the
 * model's original GIcon continues to render with the active icon theme. */
static const gchar *
dakkar_sidebar_color_key (const gchar *uri, const gchar *name,
                         PlaceType type, SectionType section)
{
    if (type == PLACES_BOOKMARK) return "dakkar-sidebar-color-bookmarks";
    if (section == SECTION_FAVORITES) return "dakkar-sidebar-color-favorites";
    if (section == SECTION_DEVICES) return "dakkar-sidebar-color-devices";
    if (section == SECTION_NETWORK) return "dakkar-sidebar-color-network";
    if (uri != NULL) {
        if (g_str_has_prefix (uri, "favorites:")) return "dakkar-sidebar-color-favorites";
        if (g_str_has_prefix (uri, "trash:")) return "dakkar-sidebar-color-trash";
        if (g_str_has_prefix (uri, "recent:")) return "dakkar-sidebar-color-recent";
        if (g_strcmp0 (uri, "file:///") == 0) return "dakkar-sidebar-color-filesystem";
    }
    if (g_strcmp0 (name, _("Desktop")) == 0) return "dakkar-sidebar-color-desktop";
    if (g_strcmp0 (name, _("Home")) == 0) return "dakkar-sidebar-color-home";
    return "dakkar-sidebar-color-bookmarks";
}

/* The disk usage bar under drive names is optional (off by default): on
 * dark palettes it read as a stray underline below Home / File System. */
static void
disk_cell_renderer_func (GtkTreeViewColumn *column, GtkCellRenderer *cell,
                         GtkTreeModel *model, GtkTreeIter *iter, gpointer user_data)
{
    gboolean show = FALSE;
    gtk_tree_model_get (model, iter, PLACES_SIDEBAR_COLUMN_SHOW_DF, &show, -1);
    g_object_set (cell, "show-disk-full-percent",
                  show && g_settings_get_boolean (nemo_preferences, DAKKAR_SIDEBAR_DISK_USAGE),
                  NULL);
    (void) column; (void) user_data;
}

/* A theme icon at a non-default sidebar size.  GTK's own symbolic
 * recolouring forces a fill on every path, which turns the transparent
 * frame rect of Breeze's 24/32/48px symbolic icons into a solid square.
 * Load the icon as drawn and recolour only symbolic icons through their
 * alpha with the sidebar text colour; HiDPI via the device scale. */
static cairo_surface_t *
dakkar_sidebar_plain_surface (GtkWidget *tree, GIcon *gicon, gint pixels)
{
    gint scale = gtk_widget_get_scale_factor (tree);
    GtkIconInfo *info = gtk_icon_theme_lookup_by_gicon_for_scale (
        gtk_icon_theme_get_default (), gicon, pixels, scale, GTK_ICON_LOOKUP_FORCE_SIZE);
    GdkPixbuf *image;
    cairo_surface_t *surface;
    if (info == NULL) return NULL;
    image = gtk_icon_info_load_icon (info, NULL);
    if (image != NULL && gtk_icon_info_is_symbolic (info)) {
        GdkRGBA fg;
        GdkPixbuf *rgba = gdk_pixbuf_get_has_alpha (image) ?
            gdk_pixbuf_copy (image) : gdk_pixbuf_add_alpha (image, FALSE, 0, 0, 0);
        gint x, y, w = gdk_pixbuf_get_width (rgba), h = gdk_pixbuf_get_height (rgba);
        gint stride = gdk_pixbuf_get_rowstride (rgba), ch = gdk_pixbuf_get_n_channels (rgba);
        guchar *data = gdk_pixbuf_get_pixels (rgba);
        gtk_style_context_get_color (gtk_widget_get_style_context (tree),
                                     GTK_STATE_FLAG_NORMAL, &fg);
        for (y = 0; y < h; y++) for (x = 0; x < w; x++) {
            guchar *px = data + y * stride + x * ch;
            px[0] = (guchar) (255 * fg.red); px[1] = (guchar) (255 * fg.green);
            px[2] = (guchar) (255 * fg.blue);
            px[3] = (guchar) (px[3] * fg.alpha);
        }
        g_object_unref (image);
        image = rgba;
    }
    g_object_unref (info);
    if (image == NULL) return NULL;
    surface = gdk_cairo_surface_create_from_pixbuf (image, scale, NULL);
    g_object_unref (image);
    return surface;
}

static void
icon_cell_renderer_func (GtkTreeViewColumn *column,
			 GtkCellRenderer *cell,
			 GtkTreeModel *model,
			 GtkTreeIter *iter,
			 gpointer user_data)
{
	PlaceType type;
    gchar *uri = NULL, *name = NULL;
    GIcon *gicon = NULL;
    SectionType section = SECTION_COMPUTER;
    gboolean monochrome = g_settings_get_boolean (nemo_preferences,
                                                   DAKKAR_SIDEBAR_ICONS_MONO);
	gtk_tree_model_get (model, iter,
                    PLACES_SIDEBAR_COLUMN_ROW_TYPE, &type,
                    PLACES_SIDEBAR_COLUMN_URI, &uri,
                    PLACES_SIDEBAR_COLUMN_NAME, &name,
                    PLACES_SIDEBAR_COLUMN_GICON, &gicon,
                    PLACES_SIDEBAR_COLUMN_SECTION_TYPE, &section,
                    -1);

    /* Recycled cell: drop the previous row's image of any kind first (a
     * surface is not replaced by a NULL gicon). */
    g_object_set (cell, "surface", NULL, "gicon", NULL, "pixbuf", NULL, NULL);
    GtkIconTheme *pack = dakkar_sidebar_icon_theme ();
    gchar *item_color = NULL;
    if (uri != NULL && type != PLACES_HEADING) {
        GVariant *saved = g_settings_get_value (nemo_preferences, "dakkar-sidebar-item-colors");
        const gchar *found = NULL;
        GdkRGBA probe;
        if (g_variant_lookup (saved, uri, "&s", &found) && gdk_rgba_parse (&probe, found))
            item_color = g_strdup (found);
        g_variant_unref (saved);
    }
    cairo_surface_t *custom = NULL;
    if (type != PLACES_HEADING) {
        gchar *icon_id = dakkar_sidebar_item_id (model, iter);
        gchar *spec = dakkar_sidebar_icon_spec_for (icon_id);
        if (spec == NULL && icon_id != NULL &&
            (g_str_has_prefix (icon_id, "uri:") || g_strcmp0 (icon_id, "place:home") == 0))
            spec = dakkar_sidebar_folder_spec (gtk_tree_view_column_get_tree_view (column), uri);
        if (spec != NULL) {
            GtkWidget *tree = gtk_tree_view_column_get_tree_view (column);
            gint pixels = dakkar_sidebar_icon_pixels ();
            GdkRGBA tint;
            gboolean tinted = FALSE;
            /* the per-item colour, or the category colour in mono mode,
             * applies to symbolic icons only */
            if (item_color != NULL)
                tinted = gdk_rgba_parse (&tint, item_color);
            else if (monochrome) {
                gchar *str = g_settings_get_string (nemo_preferences,
                                 dakkar_sidebar_color_key (uri, name, type, section));
                tinted = gdk_rgba_parse (&tint, str);
                g_free (str);
            }
            custom = dakkar_sidebar_custom_surface (tree, spec, pixels,
                                                   gtk_widget_get_scale_factor (tree),
                                                   tinted ? &tint : NULL);
        }
        g_free (spec);
        g_free (icon_id);
    }
    if (custom != NULL) {
        g_object_set (cell, "visible", TRUE, "gicon", NULL, "pixbuf", NULL,
                      "surface", custom, "xpad", 3, "ypad",
                      CLAMP (g_settings_get_int (nemo_preferences, DAKKAR_SIDEBAR_ROW_PADDING), 1, 12), NULL);
    } else if (type == PLACES_HEADING) {
        g_object_set (cell, "visible", FALSE, "pixbuf", NULL, NULL);
    } else if ((monochrome || pack != NULL || item_color != NULL) && gicon != NULL) {
        gint pixels = dakkar_sidebar_icon_pixels ();
        /* Every path, tinted ones included, loads at the device scale and
         * is drawn as a cairo surface: sharp at HiDPI and any icon size. */
        gint scale = gtk_widget_get_scale_factor (gtk_tree_view_column_get_tree_view (column));
        GtkIconInfo *info = pack != NULL ?
            gtk_icon_theme_lookup_by_gicon_for_scale (pack, gicon, pixels, scale,
                                                      GTK_ICON_LOOKUP_FORCE_SIZE) : NULL;
        if (info == NULL)
            info = gtk_icon_theme_lookup_by_gicon_for_scale (gtk_icon_theme_get_default (),
                              gicon, pixels, scale, GTK_ICON_LOOKUP_FORCE_SIZE);
        GdkPixbuf *image = info != NULL ? gtk_icon_info_load_icon (info, NULL) : NULL;
        if (info != NULL) g_object_unref (info);
        if (image != NULL && !monochrome && item_color == NULL) {
            /* Icon pack without tinting: themed icon at the sidebar size. */
            cairo_surface_t *surface = gdk_cairo_surface_create_from_pixbuf (image, scale, NULL);
            g_object_set (cell, "visible", TRUE, "gicon", NULL, "pixbuf", NULL,
                          "surface", surface, "xpad", 3, "ypad",
                          CLAMP (g_settings_get_int (nemo_preferences, DAKKAR_SIDEBAR_ROW_PADDING), 1, 12), NULL);
            cairo_surface_destroy (surface);
            g_object_unref (image);
        } else if (image != NULL) {
            const gchar *key = dakkar_sidebar_color_key (uri, name, type, section);
            /* an explicit per-item colour wins over the category colour */
            gchar *str = item_color != NULL ? g_strdup (item_color) :
                         g_settings_get_string (nemo_preferences, key);
            GdkRGBA tint;
            if (!gdk_rgba_parse (&tint, str)) gdk_rgba_parse (&tint, "#ffffff");
            g_free (str);
            GdkPixbuf *rgba = gdk_pixbuf_get_has_alpha (image) ?
                gdk_pixbuf_copy (image) : gdk_pixbuf_add_alpha (image, FALSE, 0,0,0);
            gint x,y,w=gdk_pixbuf_get_width (rgba),h=gdk_pixbuf_get_height (rgba);
            gint stride=gdk_pixbuf_get_rowstride (rgba),ch=gdk_pixbuf_get_n_channels (rgba);
            guchar *data = gdk_pixbuf_get_pixels (rgba);
            for (y=0;y<h;y++) for (x=0;x<w;x++) {
                guchar *p=data+y*stride+x*ch;
                p[0]=(guchar)(255*tint.red); p[1]=(guchar)(255*tint.green);
                p[2]=(guchar)(255*tint.blue);
            }
            cairo_surface_t *tinted = gdk_cairo_surface_create_from_pixbuf (rgba, scale, NULL);
            g_object_set (cell, "visible", TRUE, "gicon", NULL, "pixbuf", NULL,
                          "surface", tinted, "xpad", 3, "ypad",
                          CLAMP (g_settings_get_int (nemo_preferences, DAKKAR_SIDEBAR_ROW_PADDING), 1, 12), NULL);
            cairo_surface_destroy (tinted);
            g_object_unref (rgba);
            g_object_unref (image);
        } else {
            /* Unsupported GIcon or unavailable icon theme: preserve Nemo's
             * original row icon rather than showing a stale recycled cell. */
            g_object_set (cell, "visible", TRUE, "pixbuf", NULL,
                          "gicon", gicon, "stock-size", files_sidebar_icon_size (), NULL);
        }
    } else if (gicon != NULL && dakkar_sidebar_icon_pixels () != 16 &&
               (custom = dakkar_sidebar_plain_surface (gtk_tree_view_column_get_tree_view (column),
                                                      gicon, dakkar_sidebar_icon_pixels ())) != NULL) {
        g_object_set (cell, "visible", TRUE, "gicon", NULL, "pixbuf", NULL,
                      "surface", custom, "xpad", 3, "ypad",
                      CLAMP (g_settings_get_int (nemo_preferences, DAKKAR_SIDEBAR_ROW_PADDING), 1, 12), NULL);
        cairo_surface_destroy (custom);
    } else {
        g_object_set (cell, "visible", TRUE, "pixbuf", NULL,
            "gicon", gicon, "xpad", 3, "ypad",
            CLAMP (g_settings_get_int (nemo_preferences, DAKKAR_SIDEBAR_ROW_PADDING), 1, 12),
            "stock-size", files_sidebar_icon_size (), NULL);
    }
    g_clear_object (&gicon);
    g_free (item_color);
    g_free (uri);
    g_free (name);
}

static void
padding_cell_renderer_func (GtkTreeViewColumn *column,
			    GtkCellRenderer *cell,
			    GtkTreeModel *model,
			    GtkTreeIter *iter,
			    gpointer user_data)
{
	PlaceType type;

	gtk_tree_model_get (model, iter,
			    PLACES_SIDEBAR_COLUMN_ROW_TYPE, &type,
			    -1);

	if (type == PLACES_HEADING) {
		g_object_set (cell,
			      "visible", FALSE,
			      "xpad", 0,
			      "ypad", 0,
			      NULL);
	} else {
		g_object_set (cell,
			      "visible", TRUE,
			      "ypad", CLAMP (g_settings_get_int (nemo_preferences,
			              DAKKAR_SIDEBAR_ROW_PADDING), 1, 12),
			      NULL);
	}
}

static void
heading_cell_renderer_func (GtkTreeViewColumn *column,
			    GtkCellRenderer *cell,
			    GtkTreeModel *model,
			    GtkTreeIter *iter,
			    gpointer user_data)
{
	PlaceType type;
    gdouble zoom = CLAMP (g_settings_get_double (nemo_preferences,
                        NEMO_PREFERENCES_FILES_SIDEBAR_SCALE), 0.65, 1.85);
    g_object_set (cell, "scale", zoom, "scale-set", TRUE,
                  "ypad", CLAMP (g_settings_get_int (nemo_preferences,
                                    DAKKAR_SIDEBAR_HEADING_PADDING), 1, 16),
                  "xpad", 6, NULL);

	gtk_tree_model_get (model, iter,
			    PLACES_SIDEBAR_COLUMN_ROW_TYPE, &type,
			    -1);

	if (type == PLACES_HEADING) {
        gboolean custom = g_settings_get_boolean (nemo_preferences,
                                                   DAKKAR_THEME_CUSTOM);
        GdkRGBA tint;
        gchar *color_string = g_settings_get_string (nemo_preferences,
                                                     DAKKAR_THEME_SIDEBAR_HEADING);
        if (!gdk_rgba_parse (&tint, color_string))
            gdk_rgba_parse (&tint, "#b8c0ce");
        g_object_set (cell, "visible", TRUE,
                      "weight", PANGO_WEIGHT_SEMIBOLD,
                      "weight-set", TRUE,
                      "foreground-rgba", custom ? &tint : NULL,
                      "foreground-set", custom,
                      NULL);
        g_free (color_string);
    } else {
        g_object_set (cell, "visible", FALSE,
                      "foreground-set", FALSE, NULL);
    }
}

static gboolean
row_visibility_function (GtkTreeModel *model,
                         GtkTreeIter  *iter,
                         gpointer      data)
{
    NemoPlacesSidebar *sidebar = NEMO_PLACES_SIDEBAR (data);

    SectionType section_type;
    PlaceType type;

    gtk_tree_model_get (model, iter,
                        PLACES_SIDEBAR_COLUMN_SECTION_TYPE, &section_type,
                        PLACES_SIDEBAR_COLUMN_ROW_TYPE, &type,
                        -1);

    if (type != PLACES_HEADING || section_type != SECTION_BOOKMARKS)
        return TRUE;

    if (sidebar->in_drag)
        return TRUE;

    if ((int)nemo_bookmark_list_length (sidebar->bookmarks) > sidebar->bookmark_breakpoint)
        return TRUE;

    return FALSE;
}

static void
nemo_places_sidebar_init (NemoPlacesSidebar *sidebar)
{
	GtkTreeView       *tree_view;
	GtkTreeViewColumn *primary_column, *expander_column, *expander_pad_column;
	GtkCellRenderer   *cell;
	GtkTreeSelection  *selection;
	GtkStyleContext   *style_context;

    sidebar->action_manager = nemo_action_manager_new ();
    sidebar->actions_changed_id = g_signal_connect_swapped (sidebar->action_manager,
                                                            "changed",
                                                            G_CALLBACK (actions_changed),
                                                            sidebar);
    sidebar->ui_manager = gtk_ui_manager_new ();

    sidebar->in_drag = FALSE;

    sidebar->desktop_dnd_source_fs = NULL;
    sidebar->desktop_dnd_can_delete_source = FALSE;

	sidebar->volume_monitor = g_volume_monitor_get ();

    sidebar->update_places_on_idle_id = 0;

    sidebar->my_computer_expanded = g_settings_get_boolean (nemo_window_state,
                                                            NEMO_WINDOW_STATE_MY_COMPUTER_EXPANDED);
    sidebar->favorites_expanded = g_settings_get_boolean (nemo_window_state, "favorites-expanded");
    sidebar->bookmarks_expanded = g_settings_get_boolean (nemo_window_state,
                                                             NEMO_WINDOW_STATE_BOOKMARKS_EXPANDED);
    sidebar->devices_expanded = g_settings_get_boolean (nemo_window_state,
                                                        NEMO_WINDOW_STATE_DEVICES_EXPANDED);
    sidebar->network_expanded = g_settings_get_boolean (nemo_window_state,
                                                        NEMO_WINDOW_STATE_NETWORK_EXPANDED);

    gtk_widget_set_size_request (GTK_WIDGET (sidebar), 140, -1);

	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (sidebar),
					GTK_POLICY_NEVER,
					GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_hadjustment (GTK_SCROLLED_WINDOW (sidebar), NULL);
	gtk_scrolled_window_set_vadjustment (GTK_SCROLLED_WINDOW (sidebar), NULL);
	gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (sidebar), GTK_SHADOW_NONE);

	style_context = gtk_widget_get_style_context (GTK_WIDGET (sidebar));
	gtk_style_context_set_junction_sides (style_context, GTK_JUNCTION_RIGHT | GTK_JUNCTION_LEFT);

	/* Make it easier for theme authors to style the sidebar */
	gtk_style_context_add_class (style_context, "nemo-places-sidebar");

  	/* tree view */
	tree_view = GTK_TREE_VIEW (nemo_places_tree_view_new ());

	gtk_tree_view_set_headers_visible (tree_view, FALSE);

    primary_column = GTK_TREE_VIEW_COLUMN (gtk_tree_view_column_new ());
    gtk_tree_view_column_set_max_width (GTK_TREE_VIEW_COLUMN (primary_column), NEMO_ICON_SIZE_SMALLER);
    gtk_tree_view_column_set_expand (primary_column, TRUE);

	/* initial padding */
	cell = gtk_cell_renderer_text_new ();
	gtk_tree_view_column_pack_start (primary_column, cell, FALSE);

	/* headings */
	cell = gtk_cell_renderer_text_new ();
	gtk_tree_view_column_pack_start (primary_column, cell, FALSE);
	gtk_tree_view_column_set_attributes (primary_column, cell,
					     "text", PLACES_SIDEBAR_COLUMN_HEADING_TEXT,
					     NULL);
	g_object_set (cell,
		      "weight", PANGO_WEIGHT_BOLD,
		      "weight-set", TRUE,
		      "ypad", 0,
		      "xpad", 0,
		      NULL);
	gtk_tree_view_column_set_cell_data_func (primary_column, cell,
						 heading_cell_renderer_func,
						 sidebar, NULL);

	/* icon padding */
	cell = gtk_cell_renderer_text_new ();
	gtk_tree_view_column_pack_start (primary_column, cell, FALSE);
	gtk_tree_view_column_set_cell_data_func (primary_column, cell,
						 padding_cell_renderer_func,
						 sidebar, NULL);

	/* icon renderer */
	cell = gtk_cell_renderer_pixbuf_new ();
    g_object_set (cell,
                  "follow-state", TRUE,
                  NULL);
	gtk_tree_view_column_pack_start (primary_column, cell, FALSE);
	gtk_tree_view_column_set_attributes (primary_column, cell,
					     "gicon", PLACES_SIDEBAR_COLUMN_GICON,
					     NULL);
	gtk_tree_view_column_set_cell_data_func (primary_column, cell,
						 icon_cell_renderer_func,
						 sidebar, NULL);

    /* normal text renderer */
    cell = sidebar->editable_renderer = nemo_cell_renderer_disk_new ();
    NEMO_CELL_RENDERER_DISK (cell)->direction = gtk_widget_get_direction (GTK_WIDGET (tree_view));
    gtk_tree_view_column_pack_start (primary_column, cell, TRUE);
    g_object_set (G_OBJECT (cell), "editable", FALSE, NULL);
    gtk_tree_view_column_set_attributes (primary_column, cell,
                         "text", PLACES_SIDEBAR_COLUMN_NAME,
                         "editable-set", PLACES_SIDEBAR_COLUMN_BOOKMARK,
                         "disk-full-percent", PLACES_SIDEBAR_COLUMN_DF_PERCENT,
                         "show-disk-full-percent", PLACES_SIDEBAR_COLUMN_SHOW_DF,
                         NULL);
    g_object_set (cell,
              "ellipsize", PANGO_ELLIPSIZE_END,
              "ellipsize-set", TRUE,
              "xpad", 5,
              "yalign", 0.5,
              NULL);

    gtk_tree_view_column_set_cell_data_func (primary_column, cell,
                                             files_sidebar_text_scale_func,
                                             sidebar, NULL);

    g_signal_connect (cell, "edited",
              G_CALLBACK (bookmarks_edited), sidebar);
    g_signal_connect (cell, "editing-canceled",
              G_CALLBACK (bookmarks_editing_canceled), sidebar);

    /* eject column */
    sidebar->eject_column = GTK_TREE_VIEW_COLUMN (gtk_tree_view_column_new ());
    gtk_tree_view_column_set_sizing (sidebar->eject_column, GTK_TREE_VIEW_COLUMN_GROW_ONLY);
    gtk_tree_view_column_set_min_width (sidebar->eject_column, EJECT_COLUMN_MIN_WIDTH);
    gtk_tree_view_column_set_max_width (sidebar->eject_column, EJECT_COLUMN_MAX_WIDTH);

    cell = gtk_cell_renderer_text_new ();
    gtk_tree_view_column_pack_start (sidebar->eject_column, cell, FALSE);
    g_object_set (cell, "width", 5, NULL);

	/* eject icon renderer */
	cell = gtk_cell_renderer_pixbuf_new ();
	sidebar->eject_icon_cell_renderer = cell;
	g_object_set (cell,
		      "mode", GTK_CELL_RENDERER_MODE_ACTIVATABLE,
		      "yalign", 0.8,
              "width", menu_icon_pixels,
		      NULL);
	gtk_tree_view_column_pack_start (sidebar->eject_column, cell, FALSE);
	gtk_tree_view_column_set_attributes (sidebar->eject_column, cell,
					     "visible", PLACES_SIDEBAR_COLUMN_EJECT,
					     "icon-name", PLACES_SIDEBAR_COLUMN_EJECT_ICON,
                         "stock-size", PLACES_SIDEBAR_COLUMN_EJECT_ICON_SIZE,
                         NULL);

    /* eject icon trailing padding (adjusts to always avoid overlay-scrollbars) */
    gboolean overlay_scrolling;
    GtkSettings *gtksettings = gtk_settings_get_default ();
    g_object_get (gtksettings,
                  "gtk-overlay-scrolling", &overlay_scrolling,
                  NULL);

    cell = gtk_cell_renderer_text_new ();
    gtk_tree_view_column_pack_start (sidebar->eject_column, cell, FALSE);
    gtk_tree_view_column_set_attributes (sidebar->eject_column, cell,
                                         "visible", PLACES_SIDEBAR_COLUMN_EJECT,
                                         NULL);

    if (overlay_scrolling) {
        GtkWidget *vscrollbar = gtk_scrolled_window_get_vscrollbar (GTK_SCROLLED_WINDOW (sidebar));
        gint nat_width;

        gtk_widget_get_preferred_width (vscrollbar, NULL, &nat_width);
        g_object_set (cell, "width", nat_width, NULL);
    } else {
        g_object_set (cell, "width", 2, NULL);
    }

    expander_pad_column = GTK_TREE_VIEW_COLUMN (gtk_tree_view_column_new());
    gtk_tree_view_column_set_sizing (expander_pad_column, GTK_TREE_VIEW_COLUMN_FIXED);
    gtk_tree_view_column_set_fixed_width (expander_pad_column, EXPANDER_PAD_COLUMN_WIDTH);

    expander_column = GTK_TREE_VIEW_COLUMN (gtk_tree_view_column_new ());
    gtk_tree_view_column_set_sizing (expander_column, GTK_TREE_VIEW_COLUMN_FIXED);

    gint expander_size;
    gtk_widget_style_get (GTK_WIDGET (tree_view), "expander-size", &expander_size, NULL);
    gtk_tree_view_column_set_fixed_width (expander_column, expander_size);

    gtk_tree_view_append_column (tree_view, expander_pad_column);
    gtk_tree_view_append_column (tree_view, expander_column);
	gtk_tree_view_append_column (tree_view, primary_column);
    gtk_tree_view_append_column (tree_view, sidebar->eject_column);

    gtk_tree_view_set_expander_column (tree_view, expander_column);

	sidebar->store = nemo_shortcuts_model_new (sidebar);
	gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (sidebar->store),
                                          GTK_TREE_SORTABLE_UNSORTED_SORT_COLUMN_ID,
                                          GTK_SORT_ASCENDING);

    sidebar->store_filter = gtk_tree_model_filter_new (GTK_TREE_MODEL (sidebar->store), NULL);

    gtk_tree_model_filter_set_visible_func (GTK_TREE_MODEL_FILTER (sidebar->store_filter),
                                            (GtkTreeModelFilterVisibleFunc) row_visibility_function,
                                            sidebar, NULL);

    gtk_tree_view_set_model (tree_view, GTK_TREE_MODEL (sidebar->store_filter));

    gtk_container_add (GTK_CONTAINER (sidebar), GTK_WIDGET (tree_view));
    gtk_widget_show (GTK_WIDGET (tree_view));

	gtk_widget_show (GTK_WIDGET (sidebar));
	sidebar->tree_view = tree_view;

	gtk_tree_view_set_search_column (tree_view, PLACES_SIDEBAR_COLUMN_NAME);
	selection = gtk_tree_view_get_selection (tree_view);
	gtk_tree_selection_set_mode (selection, GTK_SELECTION_BROWSE);

	gtk_tree_selection_set_select_function (selection,
						tree_selection_func,
						sidebar,
						NULL);

	gtk_tree_view_enable_model_drag_source (GTK_TREE_VIEW (tree_view),
						GDK_BUTTON1_MASK,
						nemo_shortcuts_source_targets,
						G_N_ELEMENTS (nemo_shortcuts_source_targets),
						GDK_ACTION_MOVE);
	gtk_drag_dest_set (GTK_WIDGET (tree_view),
			   0,
			   nemo_shortcuts_drop_targets, G_N_ELEMENTS (nemo_shortcuts_drop_targets),
			   GDK_ACTION_MOVE | GDK_ACTION_COPY | GDK_ACTION_LINK);

	g_signal_connect (tree_view, "key-press-event",
			  G_CALLBACK (bookmarks_key_press_event_cb), sidebar);

	g_signal_connect (tree_view, "drag-motion",
			  G_CALLBACK (drag_motion_callback), sidebar);
	g_signal_connect (tree_view, "drag-leave",
			  G_CALLBACK (drag_leave_callback), sidebar);
	g_signal_connect (tree_view, "drag-data-received",
			  G_CALLBACK (drag_data_received_callback), sidebar);
	g_signal_connect (tree_view, "drag-drop",
			  G_CALLBACK (drag_drop_callback), sidebar);

	g_signal_connect (selection, "changed",
			  G_CALLBACK (bookmarks_selection_changed_cb), sidebar);
	g_signal_connect (tree_view, "popup-menu",
			  G_CALLBACK (bookmarks_popup_menu_cb), sidebar);
	g_signal_connect (tree_view, "button-press-event",
			  G_CALLBACK (bookmarks_button_press_event_cb), sidebar);
	g_signal_connect (tree_view, "button-release-event",
			  G_CALLBACK (bookmarks_button_release_event_cb), sidebar);
    g_signal_connect (tree_view, "row-expanded",
              G_CALLBACK (row_expanded_cb), sidebar);
    g_signal_connect (tree_view, "row-collapsed",
              G_CALLBACK (row_collapsed_cb), sidebar);
    g_signal_connect (tree_view, "row-activated",
              G_CALLBACK (row_activated_cb), sidebar);
    g_signal_connect (tree_view, "motion-notify-event",
              G_CALLBACK (motion_notify_cb), sidebar);
    g_signal_connect (tree_view, "leave-notify-event",
              G_CALLBACK (leave_notify_cb), sidebar);

    g_signal_connect_object (GTK_WIDGET (tree_view), "query-tooltip",
                             G_CALLBACK (query_tooltip_callback), sidebar, 0);
    gtk_widget_set_has_tooltip (GTK_WIDGET (tree_view), TRUE);

    g_signal_connect_object (nemo_preferences, "changed::dakkar-sidebar-item-colors",
                             G_CALLBACK (dakkar_sidebar_custom_icons_changed), tree_view, G_CONNECT_SWAPPED);
    g_signal_connect_object (nemo_preferences, "changed::dakkar-sidebar-item-icons",
                             G_CALLBACK (dakkar_sidebar_custom_icons_changed), tree_view, G_CONNECT_SWAPPED);
    g_signal_connect_object (nemo_preferences, "changed::" DAKKAR_SIDEBAR_ICONS_MONO,
                             G_CALLBACK (dakkar_sidebar_custom_icons_changed), tree_view, G_CONNECT_SWAPPED);
    g_signal_connect_object (nemo_preferences, "changed::" NEMO_PREFERENCES_FILES_SIDEBAR_SCALE,
                             G_CALLBACK (dakkar_sidebar_custom_icons_changed), tree_view, G_CONNECT_SWAPPED);
    g_signal_connect_object (nemo_preferences, "changed::" DAKKAR_SIDEBAR_ICON_SIZE,
                             G_CALLBACK (dakkar_sidebar_custom_icons_changed), tree_view, G_CONNECT_SWAPPED);
    g_signal_connect_object (tree_view, "style-updated",
                             G_CALLBACK (dakkar_sidebar_custom_icons_changed), tree_view, G_CONNECT_SWAPPED);
    g_signal_connect_object (nemo_preferences, "changed::" DAKKAR_SIDEBAR_DISK_USAGE,
                             G_CALLBACK (gtk_widget_queue_draw), tree_view, G_CONNECT_SWAPPED);
    g_signal_connect_object (nemo_preferences, "changed::" DAKKAR_SIDEBAR_ICONS_MONO,
                             G_CALLBACK (gtk_widget_queue_draw), tree_view, G_CONNECT_SWAPPED);
    g_signal_connect_object (nemo_preferences, "changed::" DAKKAR_SIDEBAR_ICON_PACK,
                             G_CALLBACK (gtk_widget_queue_draw), tree_view, G_CONNECT_SWAPPED);
    const gchar *icon_types[] = {"home", "desktop", "recent", "filesystem", "trash",
                                  "devices", "network", "favorites", "bookmarks"};
    guint color_index;
    for (color_index=0; color_index<G_N_ELEMENTS (icon_types); color_index++) {
        gchar *signal = g_strdup_printf ("changed::dakkar-sidebar-color-%s", icon_types[color_index]);
        g_signal_connect_object (nemo_preferences, signal,
            G_CALLBACK (gtk_widget_queue_draw), tree_view, G_CONNECT_SWAPPED);
        g_free (signal);
    }
	g_signal_connect_swapped (nemo_preferences, "changed::" NEMO_PREFERENCES_DESKTOP_IS_HOME_DIR,
				  G_CALLBACK(desktop_setting_changed_callback),
				  sidebar);

	g_signal_connect_swapped (nemo_desktop_preferences, "changed::" NEMO_PREFERENCES_SHOW_DESKTOP,
				  G_CALLBACK(desktop_setting_changed_callback),
				  sidebar);

    g_signal_connect_swapped (cinnamon_privacy_preferences, "changed::" NEMO_PREFERENCES_RECENT_ENABLED,
                  G_CALLBACK(desktop_setting_changed_callback),
                  sidebar);

	g_signal_connect_object (nemo_trash_monitor_get (),
				 "trash_state_changed",
				 G_CALLBACK (trash_state_changed_cb),
				 sidebar, 0);

    g_signal_connect_swapped (xapp_favorites_get_default (),
                              "changed",
                              G_CALLBACK (favorites_changed_cb),
                              sidebar);
    g_signal_connect_object (nemo_window_state, "changed::dakkar-favorite-order",
                             G_CALLBACK (update_places), sidebar, G_CONNECT_SWAPPED);
    g_signal_connect_object (nemo_window_state, "changed::dakkar-favorite-hidden",
                             G_CALLBACK (update_places), sidebar, G_CONNECT_SWAPPED);
}

static void
nemo_places_sidebar_dispose (GObject *object)
{
	NemoPlacesSidebar *sidebar;

	sidebar = NEMO_PLACES_SIDEBAR (object);

	sidebar->window = NULL;
	sidebar->tree_view = NULL;

	g_free (sidebar->uri);
	sidebar->uri = NULL;

	free_drag_data (sidebar);

    g_clear_handle_id (&sidebar->actions_changed_idle_id, g_source_remove);

	if (sidebar->bookmarks_changed_id != 0) {
		g_signal_handler_disconnect (sidebar->bookmarks,
					     sidebar->bookmarks_changed_id);
		sidebar->bookmarks_changed_id = 0;
	}

    if (sidebar->actions_changed_id != 0) {
        g_signal_handler_disconnect (sidebar->action_manager,
                                     sidebar->actions_changed_id);
        sidebar->actions_changed_id = 0;
    }

    g_clear_object (&sidebar->action_manager);

    if (sidebar->update_places_on_idle_id != 0) {
        g_source_remove (sidebar->update_places_on_idle_id);
        sidebar->update_places_on_idle_id = 0;
    }

	g_clear_object (&sidebar->store);

    g_clear_pointer (&sidebar->top_bookend_uri, g_free);
    g_clear_pointer (&sidebar->bottom_bookend_uri, g_free);

	if (sidebar->go_to_after_mount_slot) {
		g_object_remove_weak_pointer (G_OBJECT (sidebar->go_to_after_mount_slot),
					      (gpointer *) &sidebar->go_to_after_mount_slot);
		sidebar->go_to_after_mount_slot = NULL;
	}

    g_signal_handlers_disconnect_by_func (nemo_window_state,
                                          breakpoint_changed_cb,
                                          sidebar);

	g_signal_handlers_disconnect_by_func (nemo_preferences,
					      desktop_setting_changed_callback,
					      sidebar);

	g_signal_handlers_disconnect_by_func (gnome_background_preferences,
					      desktop_setting_changed_callback,
					      sidebar);

    g_signal_handlers_disconnect_by_func (cinnamon_privacy_preferences,
                          desktop_setting_changed_callback,
                          sidebar);

    g_signal_handlers_disconnect_by_func (xapp_favorites_get_default (),
                                          favorites_changed_cb,
                                          sidebar);

	if (sidebar->volume_monitor != NULL) {
		g_signal_handlers_disconnect_by_func (sidebar->volume_monitor,
						      volume_added_callback, sidebar);
		g_signal_handlers_disconnect_by_func (sidebar->volume_monitor,
						      volume_removed_callback, sidebar);
		g_signal_handlers_disconnect_by_func (sidebar->volume_monitor,
						      volume_changed_callback, sidebar);
		g_signal_handlers_disconnect_by_func (sidebar->volume_monitor,
						      mount_added_callback, sidebar);
		g_signal_handlers_disconnect_by_func (sidebar->volume_monitor,
						      mount_removed_callback, sidebar);
		g_signal_handlers_disconnect_by_func (sidebar->volume_monitor,
						      mount_changed_callback, sidebar);
		g_signal_handlers_disconnect_by_func (sidebar->volume_monitor,
						      drive_disconnected_callback, sidebar);
		g_signal_handlers_disconnect_by_func (sidebar->volume_monitor,
						      drive_connected_callback, sidebar);
		g_signal_handlers_disconnect_by_func (sidebar->volume_monitor,
						      drive_changed_callback, sidebar);

		g_clear_object (&sidebar->volume_monitor);
	}

	G_OBJECT_CLASS (nemo_places_sidebar_parent_class)->dispose (object);
}

static void
nemo_places_sidebar_class_init (NemoPlacesSidebarClass *class)
{
    GObjectClass *oclass = G_OBJECT_CLASS (class);
    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (class);

    oclass->dispose = nemo_places_sidebar_dispose;

	widget_class->style_set = nemo_places_sidebar_style_set;
	widget_class->focus = nemo_places_sidebar_focus;

    gtk_icon_size_lookup (GTK_ICON_SIZE_MENU, &menu_icon_pixels, NULL);
    EJECT_ICON_SIZE_NOT_HOVERED = gtk_icon_size_register ("menu-icon-size-small",
                                                          menu_icon_pixels - EJECT_ICON_SIZE_REDUCTION,
                                                          menu_icon_pixels - EJECT_ICON_SIZE_REDUCTION);
}

static gboolean
update_places_on_idle_callback (NemoPlacesSidebar *sidebar)
{
    sidebar->update_places_on_idle_id = 0;

    update_places (sidebar);

    return FALSE;
}

static void
update_places_on_idle (NemoPlacesSidebar *sidebar)
{
    if (sidebar->update_places_on_idle_id != 0) {
        g_source_remove (sidebar->update_places_on_idle_id);
        sidebar->update_places_on_idle_id = 0;
    }

    sidebar->update_places_on_idle_id = g_idle_add_full (G_PRIORITY_LOW,
                                                         (GSourceFunc) update_places_on_idle_callback,
                                                         sidebar, NULL);
}

static void
nemo_places_sidebar_set_parent_window (NemoPlacesSidebar *sidebar,
					   NemoWindow *window)
{
	NemoWindowSlot *slot;
    gint breakpoint;

	sidebar->window = window;

	slot = nemo_window_get_active_slot (window);

	sidebar->bookmarks = nemo_bookmark_list_get_default ();
	sidebar->uri = nemo_window_slot_get_current_uri (slot);

    breakpoint = g_settings_get_int (nemo_window_state, NEMO_PREFERENCES_SIDEBAR_BOOKMARK_BREAKPOINT);

    if (breakpoint < 0) {     // Default gsettings value is -1 (which translates to 'not previously set')
        breakpoint = nemo_bookmark_list_length (sidebar->bookmarks);
        g_settings_set_int (nemo_window_state, NEMO_PREFERENCES_SIDEBAR_BOOKMARK_BREAKPOINT, breakpoint);
    }

    sidebar->bookmark_breakpoint = breakpoint;
    g_signal_connect_swapped (nemo_window_state, "changed::" NEMO_PREFERENCES_SIDEBAR_BOOKMARK_BREAKPOINT,
                              G_CALLBACK (breakpoint_changed_cb), sidebar);

	sidebar->bookmarks_changed_id =
		g_signal_connect_swapped (sidebar->bookmarks, "changed",
					  G_CALLBACK (update_places_on_idle),
					  sidebar);

	g_signal_connect_object (window, "loading_uri",
				 G_CALLBACK (loading_uri_callback),
				 sidebar, 0);

	g_signal_connect_object (sidebar->volume_monitor, "volume_added",
				 G_CALLBACK (volume_added_callback), sidebar, 0);
	g_signal_connect_object (sidebar->volume_monitor, "volume_removed",
				 G_CALLBACK (volume_removed_callback), sidebar, 0);
	g_signal_connect_object (sidebar->volume_monitor, "volume_changed",
				 G_CALLBACK (volume_changed_callback), sidebar, 0);
	g_signal_connect_object (sidebar->volume_monitor, "mount_added",
				 G_CALLBACK (mount_added_callback), sidebar, 0);
	g_signal_connect_object (sidebar->volume_monitor, "mount_removed",
				 G_CALLBACK (mount_removed_callback), sidebar, 0);
	g_signal_connect_object (sidebar->volume_monitor, "mount_changed",
				 G_CALLBACK (mount_changed_callback), sidebar, 0);
	g_signal_connect_object (sidebar->volume_monitor, "drive_disconnected",
				 G_CALLBACK (drive_disconnected_callback), sidebar, 0);
	g_signal_connect_object (sidebar->volume_monitor, "drive_connected",
				 G_CALLBACK (drive_connected_callback), sidebar, 0);
	g_signal_connect_object (sidebar->volume_monitor, "drive_changed",
				 G_CALLBACK (drive_changed_callback), sidebar, 0);

	g_signal_connect_swapped (nemo_preferences, "changed::" NEMO_PREFERENCES_ALWAYS_USE_BROWSER,
				 G_CALLBACK (reset_menu), sidebar);
	update_places (sidebar);
}

static void
nemo_places_sidebar_style_set (GtkWidget *widget,
				   GtkStyle  *previous_style)
{
	NemoPlacesSidebar *sidebar;

	sidebar = NEMO_PLACES_SIDEBAR (widget);
	update_places (sidebar);
}

GtkWidget *
nemo_places_sidebar_new (NemoWindow *window)
{
	NemoPlacesSidebar *sidebar;

	sidebar = g_object_new (NEMO_TYPE_PLACES_SIDEBAR, NULL);
	nemo_places_sidebar_set_parent_window (sidebar, window);

	return GTK_WIDGET (sidebar);
}


/* Drag and drop interfaces */

/* GtkTreeDragSource::row_draggable implementation for the shortcuts filter model */

static gboolean
nemo_shortcuts_model_row_draggable (GtkTreeDragSource *drag_source,
					GtkTreePath       *path)
{
	GtkTreeModel *model;
	GtkTreeIter iter;
	PlaceType place_type;
	SectionType section_type;

	model = GTK_TREE_MODEL (drag_source);

	gtk_tree_model_get_iter (model, &iter, path);
	gtk_tree_model_get (model, &iter,
			    PLACES_SIDEBAR_COLUMN_ROW_TYPE, &place_type,
			    PLACES_SIDEBAR_COLUMN_SECTION_TYPE, &section_type,
			    -1);

	if (place_type != PLACES_HEADING &&
        (section_type == SECTION_XDG_BOOKMARKS || section_type == SECTION_BOOKMARKS))
		return TRUE;

	/* XApp favorites and standard folders reorder within Favorites too. */
	if (dakkar_is_favorites_row (place_type, section_type))
		return TRUE;

	return FALSE;
}

static void
_nemo_shortcuts_model_class_init (NemoShortcutsModelClass *klass)
{

}

static void
_nemo_shortcuts_model_init (NemoShortcutsModel *model)
{
	model->sidebar = NULL;
}

static void
_nemo_shortcuts_model_drag_source_init (GtkTreeDragSourceIface *iface)
{
	iface->row_draggable = nemo_shortcuts_model_row_draggable;
}

static GtkTreeStore *
nemo_shortcuts_model_new (NemoPlacesSidebar *sidebar)
{
	NemoShortcutsModel *model;
	GType model_types[PLACES_SIDEBAR_COLUMN_COUNT] = {
        G_TYPE_INT,
		G_TYPE_STRING,
		G_TYPE_DRIVE,
		G_TYPE_VOLUME,
		G_TYPE_MOUNT,
		G_TYPE_STRING,
		G_TYPE_ICON,
		G_TYPE_INT,
		G_TYPE_BOOLEAN,
		G_TYPE_BOOLEAN,
		G_TYPE_BOOLEAN,
		G_TYPE_STRING,
		G_TYPE_STRING,
        G_TYPE_INT,
		G_TYPE_INT,
		G_TYPE_STRING,
        G_TYPE_INT,
        G_TYPE_BOOLEAN
	};

	model = g_object_new (NEMO_TYPE_SHORTCUTS_MODEL, NULL);
	model->sidebar = sidebar;

	gtk_tree_store_set_column_types (GTK_TREE_STORE (model),
					 PLACES_SIDEBAR_COLUMN_COUNT,
					 model_types);

	return GTK_TREE_STORE (model);
}
