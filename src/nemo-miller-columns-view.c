/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*-
 *
 * nemo-miller-columns-view.c: NemoView subclass that shows directories
 * as cascading "Miller columns", similar to macOS Finder.
 *
 * Copyright (C) 2026 Kefkism
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 */

#include <config.h>
#include <math.h>

#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>
#include <gio/gio.h>
#include <string.h>
#include <stdlib.h>

#include "nemo-miller-columns-view.h"
#include "nemo-window-slot.h"
#include "nemo-query-editor.h"
#include "dakkar-grouping.h"
#include "nemo-view-factory.h"

#include <libnemo-private/nemo-directory.h>
#include <libnemo-private/nemo-global-preferences.h>
#include <libnemo-private/nemo-clipboard-monitor.h>
#include <libnemo-private/nemo-clipboard.h>
#include <libnemo-private/nemo-dnd.h>
#include <libnemo-private/nemo-tree-view-drag-dest.h>
#include "nemo-actions.h"
#include "nemo-view-dnd.h"
#include <gtk/gtkx.h>
#include <libnemo-private/nemo-icon-info.h>
#include <libnemo-private/nemo-metadata.h>
#include <libnemo-private/nemo-ui-utilities.h>
#include <eel/eel-vfs-extensions.h>



enum {
	COL_ICON = 0,
	COL_NAME,
	COL_FILE,	/* NemoFile* */
	COL_GFILE,	/* GFile* */
	COL_IS_DIR,
	COL_SIZE,	/* gint64: nemo_file_get_size */
	COL_MTIME,	/* gint64: nemo_file_get_mtime */
	COL_MIME,	/* string: nemo_file_get_mime_type */
	COL_FADED,	/* GdkRGBA* or NULL */
	COL_OPENED,	/* visual-only: row is the folder open in next column */
    COL_NAV_RGBA, /* theme-based active/ancestor highlight, never GTK selection */
	COL_EMBLEM,   /* separate first Nemo emblem; never part of the file icon */
    COL_GROUP_KEY, /* stable metadata-derived group identifier */
    COL_GROUP_HEADER, /* virtual view row, NEVER a NemoFile */
	COL_COUNT
};

/* drag source targets: the same pair the icon view advertises, so
 * drops between nemo windows/desktop keep their semantics */
static const GtkTargetEntry miller_drag_targets[] = {
	/* Browsers commonly request text/uri-list for native file uploads. */
	{ (char *) NEMO_ICON_DND_URI_LIST_TYPE, 0, NEMO_ICON_DND_URI_LIST },
	{ (char *) NEMO_ICON_DND_GNOME_ICON_LIST_TYPE, 0, NEMO_ICON_DND_GNOME_ICON_LIST },
};

typedef struct {
	GtkWidget     *scroll;
	GtkWidget     *tree;
	GtkListStore  *store;
	GFile         *dir;		/* never NULL now; guards stay defensive */
	NemoDirectory *directory;	/* extension column only (owned ref) */
	gboolean       is_chain;
	gboolean       monitored;	/* directory was added via add_subdirectory */
	GtkCellRenderer *pix;
	GtkCellRenderer *emblem;
	GtkCellRenderer *txt;
	NemoTreeViewDragDest *drag_dest;
	GCancellable *scan_cancel; /* invalidates outstanding async loads */
	guint64 scan_id;
	guint hover_timeout_id;
	GtkTreePath *hover_path;
    GHashTable *group_files; /* GFile -> ref NemoFile, including collapsed entries */
    GHashTable *collapsed_groups; /* this Miller column only */
    guint group_sync_id;
    guint group_rebuild_id;
	int            icon_size;
	int            auto_width;   /* fitted to the names (auto-fit mode) */
	int            manual_width; /* dragged by the user; 0 = follow auto_width */
	guint          fit_id;       /* coalesced re-measure */
} MillerCol;

/* hash/equal wrappers so GFiles can key the monitor table */
static guint
miller_file_gfile_hash (gconstpointer v)
{
	return g_file_hash ((GFile *) v);
}

static gboolean
miller_file_gfile_equal (gconstpointer a, gconstpointer b)
{
	return g_file_equal ((GFile *) a, (GFile *) b);
}

struct NemoMillerColumnsViewDetails {
	GtkWidget *vbox;
	GHashTable *monitored_dirs;	/* GFile -> NemoDirectory I added */
	GtkWidget *columns_box;
	GtkWidget *paned_root;	/* paned chain holding the columns */
	GtkWidget *columns_scrolled;	/* scrolled window around the chain */
	GArray   *col_widths;	/* dragged widths, by column index from left */
	GList     *columns;		/* MillerCol*, root -> base -> extensions */
	GList     *drag_items;	/* MillerDragItem*, snapshot taken at drag-begin */


	GCancellable *preview_cancellable;	/* video-still generation for row icons */
	char *tmp_dir;
	GHashTable *still_cache;	/* uri -> GdkPixbuf* (full-size frame) */
	GHashTable *still_pending;	/* uri -> generation in flight */
    GQueue *still_cache_order; /* bounded by 32 full-size frames */
	guint      active_clear_id;
	guint      active_pulse_id;
	gboolean   active_pulse_on;

	GList     *selection;	/* NemoFile* */
	MillerCol *action_col; /* focused/clicked column for background actions */
	/* Explicit navigation destination. Focus may remain in a parent tree
	 * after an extension column opens; Paste must not infer its destination
	 * from that focus or the selected row. */
	GFile *paste_destination;
	guint slide_tick_id;
	guint slide_start_idle_id;
	gint64 slide_start_us;
	gdouble slide_from;
	NemoFile *action_directory_file; /* borrowed by NemoView action callbacks */
	NemoFile *rename_target_file; /* strong ref to the exact file being edited */
	gboolean rename_select_all;
	gboolean pending_rename_idle_queued;
	gboolean click_to_rename;
	GFile *rename_press_file;	/* sole selected row at button press */
	guint rename_timeout_id;	/* pending click-to-rename */
	GFile *last_click_file;
	GFile *rename_opened_target; /* explicit last clicked/opened directory, never implicit root */
	guint32 last_click_time;
	NemoFileSortType sort_order;
	gboolean sort_reversed;
	gboolean sort_is_custom;
	GtkActionGroup *sort_action_group;
	guint sort_merge_id;
	gboolean updating_sort_menu;
	gboolean   in_load;
	gboolean   programmatic;
	gboolean   click_pending;
	/* rubber band: where the last primary press started */
	GtkWidget *band_press_tree;
	GtkWidget *band_suspended_tree;	/* rubber band off for a DnD press */
	GtkWidget *frozen_tree;		/* selection kept for a possible multi-drag */
	GtkTreePath *frozen_path;
	gdouble    band_press_x;
	gdouble    band_press_y;
	int        max_columns;
	int        zoom_level;
	gulong     settings_handler;
	guint widths_save_id;
	/* Two coalesced, cancelable post-operation heals, not unbounded timers. */
	guint heal_early_id;
	guint heal_late_id;
	gboolean restoring_widths;
	gboolean saving_widths; /* suppress our own synchronous GSettings echo */
	int default_width;
	gboolean auto_fit;	/* miller-auto-fit-columns */
	guint64 scan_serial;
	guint hover_delay_ms;
    /* Only the latest local selection is prefetched. Bounded to 12 rows and
     * 15 seconds: no speculative recursive scans and no remote IO. */
    GFile *prefetch_dir;
    GPtrArray *prefetch_items;
    GCancellable *prefetch_cancel;
    gint64 prefetch_time_us;
};

/* NemoView owns the actual subdirectory_list. Miller keeps this secondary
 * index only while registered: miller_clear() MUST empty it before
 * NemoView.load_directory() changes location. */
static void
miller_monitor_dir (NemoMillerColumnsView *view, MillerCol *col)
{
	if (col->dir == NULL || col->directory == NULL) {
		return;
	}
	if (g_hash_table_lookup (view->details->monitored_dirs,
				 col->dir) != NULL) {
		return;
	}
	nemo_view_add_subdirectory (NEMO_VIEW (view), col->directory);
	g_hash_table_insert (view->details->monitored_dirs, col->dir,
			     col->directory);
	col->monitored = TRUE;
}

static void
miller_unmonitor_dir (NemoMillerColumnsView *view, MillerCol *col)
{
	NemoDirectory *mon;

	if (gtk_widget_in_destruction (GTK_WIDGET (view))) {
		/* the shell tears the view down and frees the whole
		 * subdirectory list itself */
		col->monitored = FALSE;
		return;
	}
	mon = (col->dir != NULL) ?
		g_hash_table_lookup (view->details->monitored_dirs, col->dir) :
		NULL;
	if (mon != NULL && col->directory == mon) {
		nemo_view_remove_subdirectory (NEMO_VIEW (view), mon);
		g_hash_table_remove (view->details->monitored_dirs, col->dir);
	}
	col->monitored = FALSE;
}


typedef struct NemoMillerColumnsViewDetails NemoMillerColumnsViewPrivate;


static MillerCol *last_chain_column  (NemoMillerColumnsView *view);
static MillerCol *find_column_by_tree (NemoMillerColumnsView *view, GtkWidget *tree);
static void       update_col_visibility (NemoMillerColumnsView *view);
static void       miller_start_renaming_file (NemoView *nemo_view,
						NemoFile *file, gboolean select_all);
static void       miller_queue_pending_rename (NemoMillerColumnsView *view);
static void       miller_load_sort_metadata (NemoMillerColumnsView *view,
						 GFile *directory);
static void       miller_resort_all (NemoMillerColumnsView *view);
static void       miller_restore_default_zoom_level (NemoView *view);
static void       miller_update_sort_menu_state (NemoMillerColumnsView *view);
static void       miller_save_widths (NemoMillerColumnsView *view);
static void       miller_cancel_pending_heals (NemoMillerColumnsView *view);
static void       miller_rename_timer_cancel (NemoMillerColumnsView *view);
static void       handle_dir_selected (NemoMillerColumnsView *view, MillerCol *col,
                                       GFile *gfile, NemoFile *file);
static void       miller_refresh_opened_marks (NemoMillerColumnsView *view);
static void       miller_navigation_color (GtkWidget *tree, gboolean active, GdkRGBA *color);
static void       rebuild_selection (NemoMillerColumnsView *view);
/* Navigating to a folder (including via breadcrumbs) selects that folder's
 * row in its parent Miller column. Pending selection is resolved as the
 * asynchronous parent enumeration populates the store. Opened ancestors
 * remain excluded from file-operation selections by the transfer guard. */
static gboolean   miller_apply_navigation_selection (NemoMillerColumnsView *view);

static void       miller_start_rename_collapse (NemoMillerColumnsView *view,
                                                GFile *parent_location, GFile *old_location);
/* A file rename can finish after Nemo has already delivered file_changed.
 * Keep the old GFile until the callback so we can repair precisely THAT row
 * (not a newly selected row, nor a parent/root directory). */
typedef struct {
	NemoMillerColumnsView *view; /* strong ref for asynchronous callback */
	GFile *old_location;
	GFile *parent_location;
	gboolean was_opened;
} MillerRenameJob;

typedef struct {
	NemoMillerColumnsView *view; /* strong ref until the last tick */
	GFile *old_location;
	GFile *parent_location;
	GtkWidget *paned;          /* strong ref: may be unparented on navigation */
	gint start_position;
	gint end_position;
	gint64 start_us;
	gboolean finished;
} MillerRenameCollapse;


G_DEFINE_TYPE_WITH_PRIVATE (NemoMillerColumnsView,
			    nemo_miller_columns_view,
			    NEMO_TYPE_VIEW)

/* ------------------------------------------------------------------ */
/* helpers                                                             */
/* ------------------------------------------------------------------ */

static MillerCol *
last_chain_column (NemoMillerColumnsView *view)
{
	GList *l;
	MillerCol *col, *last;

	last = NULL;
	for (l = view->details->columns; l != NULL; l = l->next) {
		col = l->data;
		if (col->is_chain) {
			last = col;
		}
	}
	return last;
}

static MillerCol *
current_store_column (NemoMillerColumnsView *view)
{
	if (view->details->columns == NULL) {
		return NULL;
	}
	return last_chain_column (view);
}

static MillerCol *
find_column_by_tree (NemoMillerColumnsView *view, GtkWidget *tree)
{
	GList *l;

	for (l = view->details->columns; l != NULL; l = l->next) {
		MillerCol *col = l->data;
		if (col->tree == tree) {
			return col;
		}
	}
	return NULL;
}

/* Nemo actions are scoped to the column being interacted with, not
 * necessarily the deepest opened directory.  This is used by Paste, New
 * Folder, Open in Terminal and .nemo_action extensions. */
static void
miller_set_action_column (NemoMillerColumnsView *view, MillerCol *col)
{
	if (view->details->action_col == col) {
		return;
	}
	view->details->action_col = col;
	if (view->details->action_directory_file != NULL) {
		nemo_file_unref (view->details->action_directory_file);
		view->details->action_directory_file = NULL;
	}
	if (col != NULL && col->dir != NULL) {
		view->details->action_directory_file = nemo_file_get (col->dir);
	}
}

/* Copy/paste destination is independently tracked from tree focus and from
 * Nemo's "open" breadcrumb highlight. It is never itself a selection. */
static void
miller_set_paste_destination (NemoMillerColumnsView *view, GFile *directory)
{
    /* Directory identity is already known from Nemo's directory model;
     * avoid synchronous GIO IO on the GTK thread. */
    if (directory != NULL)
        g_set_object (&view->details->paste_destination, directory);
    else
        g_clear_object (&view->details->paste_destination);
}

/* This is an owned URI, not a borrowed reference. Background focus events do
 * not change it. Paste checks it again when the asynchronous clipboard reply
 * arrives and then revalidates every resolved filesystem path. */
char *
nemo_miller_columns_view_get_paste_uri (NemoMillerColumnsView *view)
{
    if (view->details->paste_destination == NULL ||
        gtk_widget_in_destruction (GTK_WIDGET (view)))
        return NULL;
    return g_file_get_uri (view->details->paste_destination);
}

/* The native Nemo actions API expects a borrowed NemoFile reference for
 * the background directory (including Open Terminal Here). */
NemoFile *
nemo_miller_columns_view_get_action_directory (NemoMillerColumnsView *view)
{
	if (view->details->action_directory_file != NULL) {
		return view->details->action_directory_file;
	}
	return NULL;
}

static gboolean
on_tree_focus_in (GtkWidget *widget, GdkEventFocus *event, gpointer user_data)
{
	NemoMillerColumnsView *view = user_data;
	NemoWindowSlot *slot = nemo_view_get_nemo_window_slot (NEMO_VIEW (view));
	/* Focus within either split pane must route toolbar actions and new tabs
	 * to that pane, not the last globally active pane. */
	if (slot != NULL)
		nemo_window_slot_make_hosting_pane_active (slot);
	miller_set_action_column (view, find_column_by_tree (view, widget));
	return FALSE;
}

static void
free_selection (NemoMillerColumnsView *view)
{
	if (view->details->selection != NULL) {
		nemo_file_list_free (view->details->selection);
		view->details->selection = NULL;
	}
}

/* ------------------------------------------------------------------ */
/* zoom                                                                */
/* ------------------------------------------------------------------ */

static GtkIconSize
zoom_icon_size (int level)
{
	if (level < NEMO_ZOOM_LEVEL_SMALLEST) {
		level = NEMO_ZOOM_LEVEL_SMALLEST;
	}
	if (level > NEMO_ZOOM_LEVEL_LARGEST) {
		level = NEMO_ZOOM_LEVEL_LARGEST;
	}

	switch (level) {
	case NEMO_ZOOM_LEVEL_SMALLEST:
	case NEMO_ZOOM_LEVEL_SMALLER:
		return GTK_ICON_SIZE_MENU;
	case NEMO_ZOOM_LEVEL_SMALL:
	case NEMO_ZOOM_LEVEL_STANDARD:
		return GTK_ICON_SIZE_SMALL_TOOLBAR;
	case NEMO_ZOOM_LEVEL_LARGE:
		return GTK_ICON_SIZE_LARGE_TOOLBAR;
	case NEMO_ZOOM_LEVEL_LARGER:
		return GTK_ICON_SIZE_DND;
	default:
		return GTK_ICON_SIZE_DIALOG;
	}
}

static int
zoom_pixel_size (int level)
{
	static const int pixels[] = { 16, 20, 24, 32, 48, 72, 96 };

	if (level < NEMO_ZOOM_LEVEL_SMALLEST) {
		level = NEMO_ZOOM_LEVEL_SMALLEST;
	}
	if (level > NEMO_ZOOM_LEVEL_LARGEST) {
		level = NEMO_ZOOM_LEVEL_LARGEST;
	}
	return pixels[level];
}

static double
zoom_font_scale (int level){
	static const double scales[] = { 0.85, 0.9, 0.95, 1.0, 1.1, 1.2, 1.35 };

	if (level < NEMO_ZOOM_LEVEL_SMALLEST) {
		level = NEMO_ZOOM_LEVEL_SMALLEST;
	}
	if (level > NEMO_ZOOM_LEVEL_LARGEST) {
		level = NEMO_ZOOM_LEVEL_LARGEST;
	}
	return scales[level];
}

static GdkPixbuf *miller_icon_for_file (NemoMillerColumnsView *view,
					NemoFile *file, int icon_size);
static GdkPixbuf *miller_first_emblem_for_file (NemoFile *file, int icon_size);
static GdkPixbuf *miller_scaled_still (NemoMillerColumnsView *view,
				       GdkPixbuf *base, int icon_size);
static void miller_row_refresh_cb (NemoFile *file, gpointer callback_data);
static void miller_group_schedule_sync (NemoMillerColumnsView *view, MillerCol *col);
static void miller_group_rebuild_column (NemoMillerColumnsView *view, MillerCol *col);
static void miller_group_toggle (NemoMillerColumnsView *view, MillerCol *col, const gchar *key);
static void rebuild_selection (NemoMillerColumnsView *view);

static void
apply_zoom (NemoMillerColumnsView *view)
{
	GList *l;
	double scale;
	int pixel;

	scale = zoom_font_scale (view->details->zoom_level);
	pixel = zoom_pixel_size (view->details->zoom_level);

	for (l = view->details->columns; l != NULL; l = l->next) {
		MillerCol *col = l->data;
		GtkTreeIter iter;
		gboolean valid;

		col->icon_size = pixel;

		if (col->pix != NULL) {
			g_object_set (col->pix, "stock-size", zoom_icon_size (view->details->zoom_level), NULL);
		}
		if (col->txt != NULL) {
			g_object_set (col->txt, "scale", scale, NULL);
		}

		/* re-request every icon at the new pixel size (thumbnails too) */
		valid = gtk_tree_model_get_iter_first (GTK_TREE_MODEL (col->store), &iter);
		while (valid) {
			NemoFile *file = NULL;
			GdkPixbuf *pixbuf, *emblem;

			gtk_tree_model_get (GTK_TREE_MODEL (col->store), &iter, COL_FILE, &file, -1);
			if (file != NULL) {
				pixbuf = miller_icon_for_file (view, file, pixel);
				emblem = miller_first_emblem_for_file (file, pixel);
				gtk_list_store_set (col->store, &iter,
				                    COL_ICON, pixbuf, COL_EMBLEM, emblem, -1);
				g_clear_object (&pixbuf);
				g_clear_object (&emblem);
				nemo_file_unref (file);
			}
			valid = gtk_tree_model_iter_next (GTK_TREE_MODEL (col->store), &iter);
		}
	}
}

/* ------------------------------------------------------------------ */
/* store helpers                                                       */
/* ------------------------------------------------------------------ */

static gint
col_sort_func (GtkTreeModel *model, GtkTreeIter *a, GtkTreeIter *b,
	       gpointer user_data)
{
	gboolean da, db;
	char *na, *nb, *ma, *mb;
	gint64 sa, sb, ta, tb;
	gint ret;
	NemoFileSortType order;
	gboolean reversed, dirs_first;
	NemoMillerColumnsView *view = user_data;

	dirs_first = g_settings_get_boolean (nemo_preferences,
					     NEMO_PREFERENCES_SORT_DIRECTORIES_FIRST);
	order = view->details->sort_order;
	reversed = view->details->sort_reversed;

	gtk_tree_model_get (model, a, COL_IS_DIR, &da, COL_NAME, &na,
			    COL_SIZE, &sa, COL_MTIME, &ta, COL_MIME, &ma, -1);
	gtk_tree_model_get (model, b, COL_IS_DIR, &db, COL_NAME, &nb,
			    COL_SIZE, &sb, COL_MTIME, &tb, COL_MIME, &mb, -1);
    /* The header is a real, independent row in the GTK model, with NO
     * NemoFile/GFile. Never sort it as though it were a filesystem entry. */
    {
        gchar *ka = NULL, *kb = NULL;
        gboolean ha = FALSE, hb = FALSE;
        gtk_tree_model_get (model, a, COL_GROUP_KEY, &ka,
                            COL_GROUP_HEADER, &ha, -1);
        gtk_tree_model_get (model, b, COL_GROUP_KEY, &kb,
                            COL_GROUP_HEADER, &hb, -1);
        ret = g_utf8_collate (ka != NULL ? ka : "",
                              kb != NULL ? kb : "");
        if (ret == 0 && ha != hb) ret = ha ? -1 : 1;
        g_free (ka); g_free (kb);
        if (ret != 0 || ha || hb) {
            g_free (na); g_free (nb); g_free (ma); g_free (mb);
            return ret;
        }
    }
    /* Grouping precedes ordinary sort but must not break file identity. */
    {
        NemoFile *fa = NULL, *fb = NULL;
        gtk_tree_model_get (model, a, COL_FILE, &fa, -1);
        gtk_tree_model_get (model, b, COL_FILE, &fb, -1);
        ret = dakkar_group_compare_for_mode (fa, fb,
            g_object_get_data (G_OBJECT (view), "dakkar-group-mode"));
        if (fa) nemo_file_unref (fa);
        if (fb) nemo_file_unref (fb);
        if (ret != 0) {
            g_free (na); g_free (nb); g_free (ma); g_free (mb);
            return ret;
        }
    }
	/* the sort criterion first (reversed applies to it), then
	 * directories-first as the dominant group key, like the
	 * stock views */
	switch (order) {
	case NEMO_FILE_SORT_BY_SIZE:
		ret = (sa < sb) ? -1 : (sa > sb) ? 1 : 0;
		break;
	case NEMO_FILE_SORT_BY_MTIME:
		ret = (ta < tb) ? -1 : (ta > tb) ? 1 : 0;
		break;
	case NEMO_FILE_SORT_BY_BTIME:
		{
			NemoFile *fa = NULL, *fb = NULL;
			gtk_tree_model_get (model, a, COL_FILE, &fa, -1);
			gtk_tree_model_get (model, b, COL_FILE, &fb, -1);
			ret = (fa != NULL && fb != NULL) ?
				nemo_file_compare_for_sort (fa, fb, NEMO_FILE_SORT_BY_BTIME,
					FALSE, FALSE, FALSE, NULL) : 0;
			if (fa != NULL) nemo_file_unref (fa);
			if (fb != NULL) nemo_file_unref (fb);
		}
		break;
	case NEMO_FILE_SORT_BY_TYPE:
	case NEMO_FILE_SORT_BY_DETAILED_TYPE:
		ret = g_strcmp0 (ma, mb);
		break;
	default:
		ret = 0;
		break;
	}
	if (ret == 0) {
		ret = g_utf8_collate (na != NULL ? na : "", nb != NULL ? nb : "");
		if (ret == 0) {
			ret = g_strcmp0 (na, nb);
		}
	}
	if (reversed && ret != 0) {
		ret = -ret;
	}
	/* Precedence matches Nemo: favorites, pinned files, directories,
	 * then the chosen criterion. Reversing does not invert grouping. */
	{
		NemoFile *fa = NULL, *fb = NULL;
		gboolean fav_a, fav_b, pin_a, pin_b;
		gtk_tree_model_get (model, a, COL_FILE, &fa, -1);
		gtk_tree_model_get (model, b, COL_FILE, &fb, -1);
		fav_a = fa != NULL && nemo_file_get_is_favorite (fa);
		fav_b = fb != NULL && nemo_file_get_is_favorite (fb);
		pin_a = fa != NULL && nemo_file_get_pinning (fa);
		pin_b = fb != NULL && nemo_file_get_pinning (fb);
		if (g_settings_get_boolean (nemo_preferences,
			NEMO_PREFERENCES_SORT_FAVORITES_FIRST) && fav_a != fav_b) {
			ret = fav_a ? -1 : 1;
		} else if (pin_a != pin_b) {
			ret = pin_a ? -1 : 1;
		} else if (dirs_first && da != db) {
			ret = da ? -1 : 1;
		}
		if (fa != NULL) nemo_file_unref (fa);
		if (fb != NULL) nemo_file_unref (fb);
	}

	g_free (na);
	g_free (nb);
	g_free (ma);
	g_free (mb);
	return ret;
}

/* A group is collapsed if this column collapsed it, or if the tab (slot)
 * collapsed it in List/Icons. The slot table is shared by every view of
 * the tab, so switching views keeps the same open/closed sections. */
static gboolean
miller_group_is_collapsed (NemoMillerColumnsView *view, MillerCol *col,
                           const gchar *key)
{
    GHashTable *shared = g_object_get_data (G_OBJECT (view), "dakkar-collapsed-groups");
    if (key == NULL) return FALSE;
    return (col != NULL && col->collapsed_groups != NULL &&
            g_hash_table_contains (col->collapsed_groups, key)) ||
           (shared != NULL && g_hash_table_contains (shared, key));
}

static void
make_row (NemoMillerColumnsView *view, GtkListStore *store, NemoFile *file,
	  int icon_size, int known_is_dir)
{
	GtkTreeIter iter;
	char *name, *mime;
	GFile *gfile;
	GdkPixbuf *pixbuf, *emblem;
	gboolean is_dir;
	gboolean is_opened = FALSE;
    gboolean is_active_navigation = FALSE;
    GdkRGBA navigation_color = {0, 0, 0, 0};
	GList *owner_node;

    /* Never count files hidden by Nemo's ordinary hidden/foreign filters.
     * The group registry includes all *eligible* entries, even collapsed
     * ones; it never overrides the user's visibility preferences. */
    if (!nemo_view_should_show_file (NEMO_VIEW (view), file)) {
        /* nemo_view_should_show_file() also hides members of groups that
         * the tab collapsed. Those must still be registered, otherwise the
         * group heading disappears and the files can never be expanded. */
        GHashTable *shared = g_object_get_data (G_OBJECT (view), "dakkar-collapsed-groups");
        gchar *k = dakkar_group_file_key (file,
                g_object_get_data (G_OBJECT (view), "dakkar-group-mode"));
        gboolean collapsed_only = shared != NULL && g_hash_table_contains (shared, k);
        g_free (k);
        if (!collapsed_only) return;
    }
    /* The per-column registry is authoritative for UI grouping. A collapsed
     * file remains known to us and stays in Nemo's directory monitor. */
    {
        GList *node;
        for (node = view->details->columns; node != NULL; node = node->next) {
            MillerCol *col = node->data;
            if (col->store == store) {
                GFile *location = nemo_file_get_location (file);
                gchar *group = dakkar_group_file_key (file,
                        g_object_get_data (G_OBJECT (view), "dakkar-group-mode"));
                if (location != NULL) {
                    g_hash_table_replace (col->group_files, location, nemo_file_ref (file));
                    miller_group_schedule_sync (view, col);
                }
                if (miller_group_is_collapsed (view, col, group)) {
                    g_free (group);
                    return;
                }
                g_free (group);
                break;
            }
        }
    }
	name = nemo_file_get_display_name (file);
	mime = nemo_file_get_mime_type (file);
	gfile = nemo_file_get_location (file);
	pixbuf = miller_icon_for_file (view, file, icon_size);
	emblem = miller_first_emblem_for_file (file, icon_size);

	/* Identify an open folder by the right-hand column, NOT GTK selection.
	 * Async enumeration can insert the row long after the chain is built. */
	for (owner_node = view->details->columns; owner_node != NULL;
	     owner_node = owner_node->next) {
		MillerCol *owner = owner_node->data;
		if (owner->store == store) {
			MillerCol *next = owner_node->next ? owner_node->next->data : NULL;
			is_opened = (gfile != NULL && next != NULL && next->dir != NULL &&
			             g_file_equal (gfile, next->dir));
            is_active_navigation = is_opened && next->is_chain &&
                                   owner_node->next && (owner_node->next->next == NULL ||
                                   !((MillerCol *)owner_node->next->next->data)->is_chain);
            if (is_opened)
                miller_navigation_color (owner->tree, is_active_navigation,
                                         &navigation_color);
			break;
		}
	}

	/* dir-ness from the enumerator's GFileInfo when the NemoFile
	 * has no loaded info yet (readdir rows): a wrong FALSE here
	 * sorted the freshly-rebuilt ancestor column as one big file
	 * list, throwing the clicked folder to the top */
	if (known_is_dir >= 0) {
		is_dir = (known_is_dir != 0);
	} else {
		is_dir = nemo_file_is_directory (file);
	}

	/* chain columns are filled before the file's info is known:
	 * get a callback when it arrives and repaint the row then */
	nemo_file_call_when_ready (file,
				   NEMO_FILE_ATTRIBUTES_FOR_ICON,
				   miller_row_refresh_cb, view);

    gchar *group_key = dakkar_group_file_key (file,
        g_object_get_data (G_OBJECT (view), "dakkar-group-mode"));
	gtk_list_store_append (store, &iter);
	gtk_list_store_set (store, &iter,
                COL_GROUP_KEY, group_key,
                COL_GROUP_HEADER, FALSE,
			    COL_ICON, pixbuf,
			    COL_EMBLEM, emblem,
			    COL_NAME, name,
			    COL_FILE, file,
			    COL_GFILE, gfile,
			    COL_IS_DIR, is_dir,
			    COL_SIZE, (gint64) nemo_file_get_size (file),
			    COL_MTIME, (gint64) nemo_file_get_mtime (file),
			    COL_MIME, mime,
			    COL_FADED, NULL,
			    COL_OPENED, is_opened,
                COL_NAV_RGBA, is_opened ? &navigation_color : NULL,
			    -1);

	g_clear_object (&pixbuf);
	g_clear_object (&emblem);
	g_free (name);
	g_free (mime);
    g_free (group_key);
	g_object_unref (gfile);
}

/* Per-column groups are rendered within the native tree itself. All entries
 * remain in group_files while collapsed, so enumeration, change monitoring
 * and group counts retain the underlying file identity. */
static const gchar *
miller_group_mode (NemoMillerColumnsView *view)
{
    const gchar *mode = g_object_get_data (G_OBJECT (view), "dakkar-group-mode");
    return mode != NULL ? mode : "none";
}

static void
miller_group_refresh_headings (NemoMillerColumnsView *view, MillerCol *col)
{
    GHashTable *counts;
    GHashTableIter groups;
    gpointer key, value;
    GtkTreeIter iter;
    gboolean valid;
    const gchar *mode = miller_group_mode (view);

    counts = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
    if (g_strcmp0 (mode, "none") != 0) {
        g_hash_table_iter_init (&groups, col->group_files);
        while (g_hash_table_iter_next (&groups, &key, &value)) {
            gchar *group = dakkar_group_file_key (NEMO_FILE (value), mode);
            guint count = GPOINTER_TO_UINT (g_hash_table_lookup (counts, group));
            g_hash_table_replace (counts, group, GUINT_TO_POINTER (count + 1));
        }
    }
    valid = gtk_tree_model_get_iter_first (GTK_TREE_MODEL (col->store), &iter);
    while (valid) {
        gboolean header = FALSE;
        gchar *group = NULL;
        gtk_tree_model_get (GTK_TREE_MODEL (col->store), &iter,
                            COL_GROUP_HEADER, &header, COL_GROUP_KEY, &group, -1);
        if (header) {
            guint count = GPOINTER_TO_UINT (g_hash_table_lookup (counts, group));
            if (count == 0) {
                valid = gtk_list_store_remove (col->store, &iter);
            } else {
                const gchar *label = dakkar_group_display_label (group);
                gboolean collapsed = miller_group_is_collapsed (view, col, group);
                gchar *caption = g_strdup_printf ("%s %s (%u)", collapsed ? "▸" : "▾", label, count);
                gchar *old_caption = NULL;
                gtk_tree_model_get (GTK_TREE_MODEL (col->store), &iter,
                                    COL_NAME, &old_caption, -1);
                if (g_strcmp0 (old_caption, caption) != 0)
                    gtk_list_store_set (col->store, &iter, COL_NAME, caption, -1);
                g_free (old_caption);
                {
                    GdkPixbuf *old_icon = NULL;
                    gtk_tree_model_get (GTK_TREE_MODEL (col->store), &iter,
                                        COL_ICON, &old_icon, -1);
                    if (old_icon == NULL ||
                        gdk_pixbuf_get_width (old_icon) != col->icon_size) {
                        GdkPixbuf *icon = dakkar_group_icon_pixbuf (group, mode,
                                                                   col->icon_size, 1);
                        gtk_list_store_set (col->store, &iter, COL_ICON, icon, -1);
                        g_clear_object (&icon);
                    }
                    g_clear_object (&old_icon);
                }
                g_free (caption);
                /* Only mark groups already in model; append unseen headings below. */
                g_hash_table_remove (counts, group);
                valid = gtk_tree_model_iter_next (GTK_TREE_MODEL (col->store), &iter);
            }
        } else {
            valid = gtk_tree_model_iter_next (GTK_TREE_MODEL (col->store), &iter);
        }
        g_free (group);
    }
    g_hash_table_iter_init (&groups, counts);
    while (g_hash_table_iter_next (&groups, &key, &value)) {
        const gchar *group = key;
        const gchar *label = dakkar_group_display_label (group);
        gboolean collapsed = miller_group_is_collapsed (view, col, group);
        gchar *caption = g_strdup_printf ("%s %s (%u)", collapsed ? "▸" : "▾", label,
                                           GPOINTER_TO_UINT (value));
        GdkPixbuf *icon = dakkar_group_icon_pixbuf (group, mode, col->icon_size, 1);
        gtk_list_store_append (col->store, &iter);
        gtk_list_store_set (col->store, &iter,
                            COL_GROUP_KEY, group, COL_GROUP_HEADER, TRUE,
                            COL_ICON, icon, COL_NAME, caption, -1);
        g_clear_object (&icon);
        g_free (caption);
    }
    g_hash_table_unref (counts);
}

static gboolean
miller_group_sync_idle (gpointer data)
{
    MillerCol *col = data;
    NemoMillerColumnsView *view = g_object_get_data (G_OBJECT (col->tree), "miller-view");
    col->group_sync_id = 0;
    if (view != NULL && g_list_find (view->details->columns, col))
        miller_group_refresh_headings (view, col);
    return G_SOURCE_REMOVE;
}

static void
miller_group_schedule_sync (NemoMillerColumnsView *view, MillerCol *col)
{
    (void)view;
    if (col->group_sync_id == 0)
        /* Coalesce many scan batches: O(n) count rebuild at most a few
         * times per second instead of after every 16 entries. */
        col->group_sync_id = g_timeout_add_full (G_PRIORITY_DEFAULT_IDLE,
                                                 120, miller_group_sync_idle,
                                                 col, NULL);
}

static void
miller_group_rebuild_column (NemoMillerColumnsView *view, MillerCol *col)
{
    GHashTableIter it;
    gpointer key, value;
    GList *files = NULL, *l;
    gboolean old_programmatic;
    GtkTreeSelection *sel;
    GList *selected = NULL;
    const gchar *mode = miller_group_mode (view);

    /* Snapshot the model and selected real files before rebuilding. Never
     * iterate a hash table while make_row() may replace its entries. */
    g_hash_table_iter_init (&it, col->group_files);
    while (g_hash_table_iter_next (&it, &key, &value))
        files = g_list_prepend (files, nemo_file_ref (NEMO_FILE (value)));
    sel = gtk_tree_view_get_selection (GTK_TREE_VIEW (col->tree));
    selected = gtk_tree_selection_get_selected_rows (sel, NULL);
    GHashTable *selected_files = g_hash_table_new_full (miller_file_gfile_hash,
            miller_file_gfile_equal, g_object_unref, NULL);
    for (l = selected; l != NULL; l = l->next) {
        GtkTreeIter row;
        GFile *location = NULL;
        if (gtk_tree_model_get_iter (GTK_TREE_MODEL (col->store), &row, l->data)) {
            gtk_tree_model_get (GTK_TREE_MODEL (col->store), &row,
                                COL_GFILE, &location, -1);
            if (location) g_hash_table_add (selected_files, location);
        }
    }
    g_list_free_full (selected, (GDestroyNotify) gtk_tree_path_free);
    old_programmatic = view->details->programmatic;
    view->details->programmatic = TRUE;
    gtk_list_store_clear (col->store);
    for (l = files; l != NULL; l = l->next) {
        NemoFile *file = l->data;
        gchar *group = dakkar_group_file_key (file, mode);
        if (!miller_group_is_collapsed (view, col, group))
            make_row (view, col->store, file, col->icon_size, -1);
        g_free (group);
    }
    for (l = files; l != NULL; l = l->next) nemo_file_unref (l->data);
    g_list_free (files);
    /* Select only visible files that were selected before the refresh.
     * A collapsed group can never leave a hidden destructive selection. */
    GtkTreeIter row;
    gboolean valid = gtk_tree_model_get_iter_first (GTK_TREE_MODEL (col->store), &row);
    while (valid) {
        GFile *location = NULL;
        gtk_tree_model_get (GTK_TREE_MODEL (col->store), &row,
                            COL_GFILE, &location, -1);
        if (location != NULL && g_hash_table_contains (selected_files, location)) {
            GtkTreePath *path = gtk_tree_model_get_path (GTK_TREE_MODEL (col->store), &row);
            gtk_tree_selection_select_path (sel, path);
            gtk_tree_path_free (path);
        }
        g_clear_object (&location);
        valid = gtk_tree_model_iter_next (GTK_TREE_MODEL (col->store), &row);
    }
    g_hash_table_unref (selected_files);
    view->details->programmatic = old_programmatic;
    miller_group_refresh_headings (view, col);
    if (!old_programmatic) {
        rebuild_selection (view);
        nemo_view_notify_selection_changed (NEMO_VIEW (view));
    }
}

static gboolean
miller_group_rebuild_idle (gpointer data)
{
    MillerCol *col = data;
    NemoMillerColumnsView *view = g_object_get_data (G_OBJECT (col->tree), "miller-view");
    col->group_rebuild_id = 0;
    if (view != NULL && g_list_find (view->details->columns, col))
        miller_group_rebuild_column (view, col);
    return G_SOURCE_REMOVE;
}

static void
miller_group_schedule_rebuild (MillerCol *col)
{
    if (col->group_rebuild_id == 0)
        col->group_rebuild_id = g_idle_add_full (G_PRIORITY_DEFAULT_IDLE,
                                       miller_group_rebuild_idle, col, NULL);
}

/* Preserve identity when a rename changes GFile; re-index from NemoFile
 * identity and schedule a batch-safe regroup instead of mutating GTK rows
 * inside Nemo's own file_changed handler. */
static void
miller_group_reindex_changed (NemoMillerColumnsView *view, MillerCol *col, NemoFile *file)
{
    GHashTableIter it;
    gpointer key, value;
    gboolean exists = FALSE, regroup = FALSE, row_found = FALSE;
    const gchar *mode = miller_group_mode (view);

    /* The old COL_GROUP_KEY is intentionally left untouched by the ordinary
     * file_changed branch. Rebuilding is needed only if grouping changed,
     * not on each thumbnail, owner or timestamp notification. */
    if (g_strcmp0 (mode, "none") != 0) {
        GtkTreeIter row;
        gboolean valid = gtk_tree_model_get_iter_first (GTK_TREE_MODEL (col->store), &row);
        gchar *new_group = dakkar_group_file_key (file, mode);
        while (valid) {
            NemoFile *old = NULL;
            gtk_tree_model_get (GTK_TREE_MODEL (col->store), &row, COL_FILE, &old, -1);
            if (old == file) {
                gchar *old_group = NULL;
                gtk_tree_model_get (GTK_TREE_MODEL (col->store), &row,
                                    COL_GROUP_KEY, &old_group, -1);
                regroup = g_strcmp0 (old_group, new_group) != 0;
                row_found = TRUE;
                g_free (old_group);
            }
            if (old != NULL) nemo_file_unref (old);
            if (row_found) break;
            valid = gtk_tree_model_iter_next (GTK_TREE_MODEL (col->store), &row);
        }
        /* A missing row may be deliberately collapsed. In that case a
         * changed MIME/name/size may make it eligible for a visible group. */
        if (!row_found && g_hash_table_size (col->collapsed_groups) != 0)
            regroup = TRUE;
        g_free (new_group);
    }

    /* The registry is keyed by actual location, not row index. Rename can
     * mutate a NemoFile's location while retaining its object identity. */
    NemoFile *retained = nemo_file_ref (file);
    g_hash_table_iter_init (&it, col->group_files);
    while (g_hash_table_iter_next (&it, &key, &value)) {
        if (value == file) {
            exists = TRUE;
            g_hash_table_iter_remove (&it);
        }
    }
    if (exists) {
        GFile *location = nemo_file_get_location (file);
        if (location != NULL)
            g_hash_table_replace (col->group_files, location, retained);
        else
            nemo_file_unref (retained);
        if (regroup)
            miller_group_schedule_rebuild (col);
        else
            miller_group_schedule_sync (view, col);
    } else {
        nemo_file_unref (retained);
    }
}

static void
miller_group_toggle (NemoMillerColumnsView *view, MillerCol *col, const gchar *key)
{
    if (key == NULL || col == NULL) return;
    {
        GHashTable *shared = g_object_get_data (G_OBJECT (view), "dakkar-collapsed-groups");
        if (miller_group_is_collapsed (view, col, key)) {
            g_hash_table_remove (col->collapsed_groups, key);
            if (shared != NULL) g_hash_table_remove (shared, key);
        } else {
            g_hash_table_add (col->collapsed_groups, g_strdup (key));
            if (shared != NULL) g_hash_table_add (shared, g_strdup (key));
        }
    }
    miller_group_rebuild_column (view, col);
}

/* A breadcrumb/current folder has a visual highlight only; it is NOT a
 * GtkTreeSelection entry and can never become an implicit delete target. */
static void
miller_navigation_color (GtkWidget *tree, gboolean active, GdkRGBA *color)
{
    GtkStyleContext *style = gtk_widget_get_style_context (tree);
    if (!gtk_style_context_lookup_color (style, "theme_selected_bg_color", color))
        gdk_rgba_parse (color, "#5d9fe0");
    color->alpha = active ? 0.42 : 0.11;
}

/* ------------------------------------------------------------------ */
/* column construction / destruction                                   */
/* ------------------------------------------------------------------ */

static void
on_col_selection_changed (GtkTreeSelection *selection, gpointer user_data);
static void
on_row_activated (GtkTreeView *tree, GtkTreePath *path,
		  GtkTreeViewColumn *column, gpointer user_data);
static gboolean
on_tree_button_press (GtkWidget *widget, GdkEventButton *event,
		      gpointer user_data);
static gboolean
on_tree_button_release (GtkWidget *widget, GdkEventButton *event,
			gpointer user_data);
static gboolean
on_tree_key_press (GtkWidget *widget, GdkEventKey *event,
		   gpointer user_data);
static void       rebuild_selection  (NemoMillerColumnsView *view);
static void       col_resync_dir     (MillerCol *col, NemoMillerColumnsView *view);
static gboolean   column_find_gfile  (MillerCol *col, GFile *gfile, GtkTreeIter *out);
static gboolean   select_gfile_in_column (MillerCol *col, GFile *gfile);
static gboolean   miller_activate_action (NemoMillerColumnsView *view, const char *name);
static void       miller_apply_cut_fade (NemoMillerColumnsView *view);
static void       miller_divider_redraw_cb (gpointer view);
static void       on_name_edited (GtkCellRendererText *renderer,
				  char *path_str, char *new_text,
				  gpointer user_data);
static void       miller_start_inline_rename (NemoMillerColumnsView *view,
					      MillerCol *col);
static void       on_rename_editing_started (GtkCellRenderer *renderer, GtkCellEditable *editable,
					     const gchar *path_str, gpointer user_data);
static void       on_rename_editing_canceled (GtkCellRendererText *renderer, gpointer user_data);
static gboolean   on_tree_popup_menu (GtkWidget *widget, gpointer user_data);
static gboolean   on_tree_query_tooltip (GtkWidget *widget, gint x, gint y,
					 gboolean kb_mode, GtkTooltip *tooltip,
					 gpointer user_data);
static void       on_tree_drag_begin (GtkWidget *widget, GdkDragContext *context,
				      gpointer user_data);
static void       on_tree_drag_end (GtkWidget *widget, GdkDragContext *context,
				    gpointer user_data);
static void       on_tree_drag_data_get (GtkWidget *widget,
					 GdkDragContext *context,
					 GtkSelectionData *selection_data,
					 guint info, guint time,
					 gpointer user_data);
static char      *miller_get_root_uri_cb (NemoTreeViewDragDest *dest,
					  gpointer user_data);
static NemoFile  *miller_get_file_for_path_cb (NemoTreeViewDragDest *dest,
					       GtkTreePath *path,
					       gpointer user_data);
static void       miller_move_copy_cb (NemoTreeViewDragDest *dest,
				       const GList *item_uris,
				       const char *target_uri,
				       guint action, int x, int y,
				       gpointer user_data);
static void       miller_handle_uri_list_cb (NemoTreeViewDragDest *dest,
					     const char *item_uris,
					     const char *target_uri,
					     GdkDragAction action, int x, int y,
					     gpointer user_data);
static void       miller_handle_netscape_url_cb (NemoTreeViewDragDest *dest,
						 const char *encoded_url,
						 const char *target_uri,
						 GdkDragAction action, int x, int y,
						 gpointer user_data);
static void       miller_handle_text_cb (NemoTreeViewDragDest *dest,
					 const char *text,
					 const char *target_uri,
					 GdkDragAction action, int x, int y,
					 gpointer user_data);

/* Hover-to-expand is additive to NemoTreeViewDragDest: the shared DnD
 * handler retains responsibility for targets, highlights and copy/move. */
static gboolean
miller_drag_hover_timeout (gpointer user_data)
{
	MillerCol *col = user_data;
	NemoMillerColumnsView *view = g_object_get_data (G_OBJECT (col->tree), "miller-view");
	GtkTreeIter iter;
	NemoFile *file = NULL;
	GFile *location = NULL;
	gboolean is_dir = FALSE;
	col->hover_timeout_id = 0;
	if (view == NULL || col->hover_path == NULL ||
	    !g_list_find (view->details->columns, col)) return G_SOURCE_REMOVE;
	if (gtk_tree_model_get_iter (GTK_TREE_MODEL (col->store), &iter, col->hover_path)) {
		gtk_tree_model_get (GTK_TREE_MODEL (col->store), &iter,
		                    COL_FILE, &file, COL_IS_DIR, &is_dir, -1);
		if (is_dir && file) {
			location = nemo_file_get_location (file);
			handle_dir_selected (view, col, location, file);
		}
	}
	g_clear_object (&location);
	if (file) nemo_file_unref (file);
	return G_SOURCE_REMOVE;
}

static void
miller_drag_cancel_hover (MillerCol *col)
{
	if (col->hover_timeout_id) {
		g_source_remove (col->hover_timeout_id);
		col->hover_timeout_id = 0;
	}
	g_clear_pointer (&col->hover_path, gtk_tree_path_free);
}

static void
miller_drag_leave (GtkWidget *tree, GdkDragContext *context, guint time, gpointer data)
{
	NemoMillerColumnsView *view = data;
	MillerCol *col = find_column_by_tree (view, tree);
	if (col) miller_drag_cancel_hover (col);
}

static gboolean
miller_drag_motion (GtkWidget *tree, GdkDragContext *context, gint x,
                    gint y, guint time, gpointer data)
{
	NemoMillerColumnsView *view = data;
	MillerCol *col = find_column_by_tree (view, tree);
	GtkTreePath *path = NULL;
	GtkAdjustment *vertical, *horizontal;
	GtkAllocation allocation;
	gint hx, hy;
	if (!col) return FALSE;
	gtk_widget_get_allocation (tree, &allocation);
	vertical = gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (col->scroll));
	if (y < 32) gtk_adjustment_set_value (vertical,
		MAX (gtk_adjustment_get_lower (vertical), gtk_adjustment_get_value (vertical) - 18));
	else if (y > allocation.height - 32) gtk_adjustment_set_value (vertical,
		MIN (gtk_adjustment_get_upper (vertical) - gtk_adjustment_get_page_size (vertical),
		     gtk_adjustment_get_value (vertical) + 18));
	if (gtk_widget_translate_coordinates (tree, view->details->columns_scrolled,
	                                    x, y, &hx, &hy)) {
		horizontal = gtk_scrolled_window_get_hadjustment (
			GTK_SCROLLED_WINDOW (view->details->columns_scrolled));
		gtk_widget_get_allocation (view->details->columns_scrolled, &allocation);
		if (hx < 36) gtk_adjustment_set_value (horizontal,
			MAX (gtk_adjustment_get_lower (horizontal), gtk_adjustment_get_value (horizontal) - 22));
		else if (hx > allocation.width - 36) gtk_adjustment_set_value (horizontal,
			MIN (gtk_adjustment_get_upper (horizontal) - gtk_adjustment_get_page_size (horizontal),
			     gtk_adjustment_get_value (horizontal) + 22));
	}
	if (!g_settings_get_boolean (nemo_preferences, NEMO_PREFERENCES_MILLER_DRAG_HOVER)) {
		miller_drag_cancel_hover (col);
		return FALSE;
	}
	if (!gtk_tree_view_get_path_at_pos (GTK_TREE_VIEW (tree), x, y,
	                                &path, NULL, NULL, NULL)) {
		miller_drag_cancel_hover (col);
		return FALSE;
	}
	if (col->hover_path && gtk_tree_path_compare (col->hover_path, path) == 0) {
		gtk_tree_path_free (path);
		return FALSE;
	}
	miller_drag_cancel_hover (col);
	col->hover_path = path;
	col->hover_timeout_id = g_timeout_add (600, miller_drag_hover_timeout, col);
	return FALSE; /* do not consume DnD: Nemo must still decide drop action */
}

/* GROUPED MILLER MODEL: virtual headers are separate rows without files.
 * Selection/drag/delete paths never receive a synthetic NemoFile. */
static gboolean
miller_group_select_file_only (GtkTreeSelection *selection, GtkTreeModel *model,
                               GtkTreePath *path, gboolean selected, gpointer data)
{
    GtkTreeIter iter;
    gboolean header = FALSE;
    /* While a press on an already selected row of a multi-selection may
     * become a drag, GTK must not collapse the selection to that row
     * (GtkTreeView clears and selects on press). */
    if (g_object_get_data (G_OBJECT (gtk_tree_selection_get_tree_view (selection)),
                           "miller-selection-frozen") != NULL)
        return FALSE;
    if (!gtk_tree_model_get_iter (model, &iter, path)) return FALSE;
    gtk_tree_model_get (model, &iter, COL_GROUP_HEADER, &header, -1);
    return !header;
}

static void
miller_group_text_cell (GtkTreeViewColumn *column, GtkCellRenderer *renderer,
                        GtkTreeModel *model, GtkTreeIter *iter, gpointer data)
{
    gboolean header = FALSE;
    gtk_tree_model_get (model, iter, COL_GROUP_HEADER, &header, -1);
    g_object_set (renderer, "weight", header ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL, NULL);
    /* Only headers are forced read-only. File rows keep the value set by
     * miller_start_inline_rename: GTK runs this data func inside
     * gtk_tree_view_set_cursor_on_cell, so resetting "editable" here
     * silently cancelled every F2/menu/click rename. */
    if (header)
        g_object_set (renderer, "editable", FALSE, NULL);
}

/* ---------- auto-fit column widths ----------
 * Each column is as wide as its longest name (icon + emblem + name +
 * padding), capped at 30 average characters of the current font and zoom
 * (Pango metrics, not pixels) and never narrower than a sensible minimum.
 * Longer names are ellipsized; the tooltip shows the full name. A divider
 * dragged by the user wins for that column until it is double-clicked
 * (back to auto-fit) or the column is replaced. With auto-fit turned off
 * the old fixed/saved widths apply unchanged. */
#define MILLER_FIT_CHARS 30
static void miller_slide_columns_to_end (NemoMillerColumnsView *view);
#define MILLER_FIT_MIN_CHARS 8

static int
miller_col_measure (NemoMillerColumnsView *view, MillerCol *col)
{
	PangoContext *pctx;
	PangoFontDescription *fd;
	PangoFontMetrics *metrics;
	PangoLayout *layout;
	GtkStyleContext *style;
	GtkBorder pad;
	GtkTreeIter iter;
	double scale, char_w;
	int size, cap_text, max_text = 0, emblem_w = 0, hsep = 0, extra, min_w;
	int xpad_pix = 0, xpad_emb = 0, xpad_txt = 0;
	gboolean valid;

	pctx = gtk_widget_get_pango_context (col->tree);
	fd = pango_font_description_copy (pango_context_get_font_description (pctx));
	scale = zoom_font_scale (view->details->zoom_level);
	size = pango_font_description_get_size (fd);
	if (size > 0) {
		if (pango_font_description_get_size_is_absolute (fd))
			pango_font_description_set_absolute_size (fd, size * scale);
		else
			pango_font_description_set_size (fd, (gint) (size * scale));
	}
	metrics = pango_context_get_metrics (pctx, fd, NULL);
	char_w = (double) pango_font_metrics_get_approximate_char_width (metrics) / PANGO_SCALE;
	pango_font_metrics_unref (metrics);
	cap_text = (int) ceil (char_w * MILLER_FIT_CHARS);

	layout = gtk_widget_create_pango_layout (col->tree, NULL);
	pango_layout_set_font_description (layout, fd);
	valid = gtk_tree_model_get_iter_first (GTK_TREE_MODEL (col->store), &iter);
	while (valid && max_text < cap_text) {
		char *name = NULL;
		GdkPixbuf *emblem = NULL;
		int w = 0;
		gtk_tree_model_get (GTK_TREE_MODEL (col->store), &iter,
		                    COL_NAME, &name, COL_EMBLEM, &emblem, -1);
		if (name != NULL) {
			pango_layout_set_text (layout, name, -1);
			pango_layout_get_pixel_size (layout, &w, NULL);
			/* group headers are measured like names and capped too */
			max_text = MAX (max_text, MIN (w, cap_text));
		}
		if (emblem != NULL)
			emblem_w = MAX (emblem_w, gdk_pixbuf_get_width (emblem));
		g_free (name);
		g_clear_object (&emblem);
		valid = gtk_tree_model_iter_next (GTK_TREE_MODEL (col->store), &iter);
	}
	g_object_unref (layout);
	pango_font_description_free (fd);

	gtk_cell_renderer_get_padding (col->pix, &xpad_pix, NULL);
	gtk_cell_renderer_get_padding (col->emblem, &xpad_emb, NULL);
	gtk_cell_renderer_get_padding (col->txt, &xpad_txt, NULL);
	gtk_widget_style_get (col->tree, "horizontal-separator", &hsep, NULL);
	style = gtk_widget_get_style_context (col->tree);
	gtk_style_context_get_padding (style, gtk_style_context_get_state (style), &pad);
	/* icon, optional emblem, text cell paddings, tree padding, and room
	 * for the vertical scrollbar and the ellipsis/focus margin */
	extra = col->icon_size + 2 * xpad_pix + 2 * xpad_txt + hsep +
	        pad.left + pad.right + 18 +
	        (emblem_w > 0 ? emblem_w + 2 * xpad_emb : 0);
	min_w = MAX (112, extra + (int) ceil (char_w * MILLER_FIT_MIN_CHARS));
	return CLAMP (max_text + extra, min_w, MAX (min_w, cap_text + extra));
}

static int
miller_col_target_width (NemoMillerColumnsView *view, MillerCol *col)
{
	if (col == NULL || !view->details->auto_fit)
		return 0;
	if (col->manual_width > 0)
		return col->manual_width;
	return col->auto_width;
}

/* Push the target width into the GtkPaned (or the trailing leaf). The
 * size request makes the chain overflow into the horizontal scrollbar
 * instead of squeezing every column to the 112 px minimum. */
static void
miller_col_apply_width (NemoMillerColumnsView *view, MillerCol *col)
{
	GtkWidget *parent;
	int target = miller_col_target_width (view, col);

	if (target <= 0 || col->scroll == NULL)
		return;
	parent = gtk_widget_get_parent (col->scroll);
	if (GTK_IS_PANED (parent) &&
	    g_object_get_data (G_OBJECT (parent), "miller-user-resizing"))
		return;
	gtk_widget_set_size_request (col->scroll, target, -1);
	if (GTK_IS_PANED (parent) &&
	    gtk_paned_get_child1 (GTK_PANED (parent)) == col->scroll) {
		gboolean old = view->details->restoring_widths;
		view->details->restoring_widths = TRUE;
		gtk_paned_set_position (GTK_PANED (parent), target);
		view->details->restoring_widths = old;
	}
}

static gboolean
miller_col_fit_timeout (gpointer data)
{
	MillerCol *col = data;
	NemoMillerColumnsView *view = g_object_get_data (G_OBJECT (col->tree), "miller-view");
	col->fit_id = 0;
	if (view == NULL || !g_list_find (view->details->columns, col) ||
	    !view->details->auto_fit)
		return G_SOURCE_REMOVE;
	{
		int old = miller_col_target_width (view, col);
		GtkAdjustment *hadj = gtk_scrolled_window_get_hadjustment (
			GTK_SCROLLED_WINDOW (view->details->columns_scrolled));
		/* the view was showing the newest column (or about to slide
		 * there): keep it in view after the chain gets wider */
		gboolean at_end = view->details->slide_tick_id != 0 ||
			view->details->slide_start_idle_id != 0 ||
			gtk_adjustment_get_value (hadj) >=
			gtk_adjustment_get_upper (hadj) - gtk_adjustment_get_page_size (hadj) - 2;
		col->auto_width = miller_col_measure (view, col);
		if (miller_col_target_width (view, col) == old)
			return G_SOURCE_REMOVE;
		miller_col_apply_width (view, col);
		if (at_end)
			miller_slide_columns_to_end (view);
	}
	return G_SOURCE_REMOVE;
}

static void
miller_col_schedule_fit (MillerCol *col)
{
	if (col->fit_id == 0)
		col->fit_id = g_timeout_add_full (G_PRIORITY_DEFAULT_IDLE, 90,
		                                  miller_col_fit_timeout, col, NULL);
}

static void
miller_col_row_inserted_or_changed (GtkTreeModel *model, GtkTreePath *path,
                                    GtkTreeIter *iter, gpointer data)
{
	miller_col_schedule_fit (data);
	(void) model; (void) path; (void) iter;
}

static void
miller_col_row_deleted (GtkTreeModel *model, GtkTreePath *path, gpointer data)
{
	miller_col_schedule_fit (data);
	(void) model; (void) path;
}

static void
miller_col_style_updated (GtkWidget *widget, gpointer data)
{
	miller_col_schedule_fit (data);
	(void) widget;
}

static void
miller_refit_all (NemoMillerColumnsView *view)
{
	GList *l;
	for (l = view->details->columns; l != NULL; l = l->next) {
		MillerCol *col = l->data;
		if (view->details->auto_fit)
			miller_col_schedule_fit (col);
	}
}

/* Icon, emblem and name renderers in one tree column. The opened-path
 * tint is set here too, apart from the real GtkTreeSelection. */
static void
miller_col_renderers_init (NemoMillerColumnsView *view, MillerCol *col, const char *title)
{
	GtkTreeViewColumn *tcol;
	GtkCellRenderer *pix, *emblem, *txt;

	pix = gtk_cell_renderer_pixbuf_new ();
	emblem = gtk_cell_renderer_pixbuf_new ();
	txt = gtk_cell_renderer_text_new ();
	/* Clicking/holding never enters GtkCellRenderer edit mode directly; the
	 * renderer is editable only while F2 / Rename / click-to-rename edits. */
	g_object_set (txt, "ellipsize", PANGO_ELLIPSIZE_END,
			   "editable", FALSE, NULL);
	g_signal_connect (txt, "edited",
			  G_CALLBACK (on_name_edited), view);
	g_signal_connect (txt, "editing-started",
			  G_CALLBACK (on_rename_editing_started), view);
	g_signal_connect (txt, "editing-canceled",
			  G_CALLBACK (on_rename_editing_canceled), view);

	col->pix = pix;
	col->emblem = emblem;
	col->txt = txt;
	/* Visual opened-path tint is deliberately separate from GtkTreeSelection.
	 * In particular, it must NEVER reach NemoView file transfer/delete APIs. */
	{
		GdkRGBA opened_bg;
		GtkStyleContext *style = gtk_widget_get_style_context (col->tree);
		if (!gtk_style_context_lookup_color (style, "theme_selected_bg_color", &opened_bg))
			gdk_rgba_parse (&opened_bg, "#6da7de");
		opened_bg.alpha = 0.12;
		g_object_set (pix, "cell-background-rgba", &opened_bg, NULL);
		g_object_set (emblem, "cell-background-rgba", &opened_bg, NULL);
		g_object_set (txt, "cell-background-rgba", &opened_bg, NULL);
	}
	g_object_set (pix, "stock-size", zoom_icon_size (view->details->zoom_level), NULL);
	g_object_set (txt, "scale", zoom_font_scale (view->details->zoom_level), NULL);

	tcol = gtk_tree_view_column_new ();
	gtk_tree_view_column_set_title (tcol, title != NULL ? title : "");
	gtk_tree_view_column_set_sizing (tcol, GTK_TREE_VIEW_COLUMN_AUTOSIZE);
	gtk_tree_view_column_pack_start (tcol, pix, FALSE);
	gtk_tree_view_column_set_attributes (tcol, pix, "pixbuf", COL_ICON,
	                                     "cell-background-set", COL_OPENED,
	                                     "cell-background-rgba", COL_NAV_RGBA, NULL);
	/* Keep the thumbnail/file icon untouched. Show only the first emblem
	 * in a separate compact renderer, immediately before the file name. */
	g_object_set (emblem, "xpad", 0, "ypad", 0, NULL);
	gtk_tree_view_column_pack_start (tcol, emblem, FALSE);
	gtk_tree_view_column_set_attributes (tcol, emblem,
	                                     "pixbuf", COL_EMBLEM,
	                                     "cell-background-set", COL_OPENED,
	                                     "cell-background-rgba", COL_NAV_RGBA,
	                                     NULL);
	gtk_tree_view_column_pack_start (tcol, txt, TRUE);
    gtk_tree_view_column_set_cell_data_func (tcol, txt,
        miller_group_text_cell, NULL, NULL);
	gtk_tree_view_column_set_attributes (tcol, txt, "text", COL_NAME,
					     "foreground-rgba", COL_FADED,
					     "cell-background-set", COL_OPENED,
                         "cell-background-rgba", COL_NAV_RGBA,
					     NULL);
	gtk_tree_view_append_column (GTK_TREE_VIEW (col->tree), tcol);
}

/* Selection mode, type-to-select and the tree and store signals. */
static void
miller_col_tree_init (NemoMillerColumnsView *view, MillerCol *col, const char *title)
{
	gtk_tree_selection_set_mode (gtk_tree_view_get_selection (GTK_TREE_VIEW (col->tree)),
				     GTK_SELECTION_MULTIPLE);
	/* Rubber-band selection, confined to this column's tree.
	 * GTK starts a band on empty space or an unselected row and keeps
	 * DnD for drags that start on an already selected row. */
	gtk_tree_view_set_rubber_banding (GTK_TREE_VIEW (col->tree), TRUE);
    /* Only real files can ever enter a GtkTreeSelection. */
    gtk_tree_selection_set_select_function (
        gtk_tree_view_get_selection (GTK_TREE_VIEW (col->tree)),
        miller_group_select_file_only, NULL, NULL);
	gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (col->tree),
					   title != NULL);
	/* GTK incremental type-to-select: typing H selects the first visible
	 * item whose name starts with H.  Use the real TreeSelection,
	 * never an implicit ancestor/transfer target. */
	gtk_tree_view_set_enable_search (GTK_TREE_VIEW (col->tree), TRUE);
	gtk_tree_view_set_search_column (GTK_TREE_VIEW (col->tree), COL_NAME);

	g_signal_connect (gtk_tree_view_get_selection (GTK_TREE_VIEW (col->tree)),
			  "changed", G_CALLBACK (on_col_selection_changed), view);
	g_signal_connect (col->tree, "focus-in-event",
			  G_CALLBACK (on_tree_focus_in), view);
	g_signal_connect (col->tree, "row-activated",
			  G_CALLBACK (on_row_activated), view);
	g_signal_connect (col->tree, "button-press-event",
			  G_CALLBACK (on_tree_button_press), view);
	g_signal_connect (col->tree, "button-release-event",
			  G_CALLBACK (on_tree_button_release), view);
	g_signal_connect (col->tree, "key-press-event",
			  G_CALLBACK (on_tree_key_press), view);
	g_signal_connect (col->tree, "popup-menu",
			  G_CALLBACK (on_tree_popup_menu), view);
	gtk_widget_set_has_tooltip (col->tree, TRUE);
	g_signal_connect (col->tree, "query-tooltip",
			  G_CALLBACK (on_tree_query_tooltip), view);
	/* auto-fit: re-measure when rows load/change/disappear or fonts change */
	g_signal_connect (col->store, "row-inserted",
			  G_CALLBACK (miller_col_row_inserted_or_changed), col);
	g_signal_connect (col->store, "row-changed",
			  G_CALLBACK (miller_col_row_inserted_or_changed), col);
	g_signal_connect (col->store, "row-deleted",
			  G_CALLBACK (miller_col_row_deleted), col);
	g_signal_connect (col->tree, "style-updated",
			  G_CALLBACK (miller_col_style_updated), col);
}

/* Drag source with the shared file targets, and Nemo's tree-view drag
 * destination for drops into the column or onto a folder row. */
static void
miller_col_dnd_init (NemoMillerColumnsView *view, MillerCol *col)
{
	gtk_tree_view_enable_model_drag_source (GTK_TREE_VIEW (col->tree),
						GDK_BUTTON1_MASK,
						miller_drag_targets,
						G_N_ELEMENTS (miller_drag_targets),
						GDK_ACTION_COPY | GDK_ACTION_MOVE |
						GDK_ACTION_LINK | GDK_ACTION_ASK);
	/* Replace the plain entries with the shared file-drag target list:
	 * text/uri-list (+ portal file-transfer targets for sandboxed
	 * receivers such as Flatpak Discord) and the GNOME icon list. */
	{
		GtkTargetList *tl = nemo_drag_source_target_list_new (miller_drag_targets,
								      G_N_ELEMENTS (miller_drag_targets));
		gtk_drag_source_set_target_list (col->tree, tl);
		gtk_target_list_unref (tl);
	}
	g_signal_connect (col->tree, "drag-data-get",
			  G_CALLBACK (on_tree_drag_data_get), view);
	g_signal_connect (col->tree, "drag-begin",
			  G_CALLBACK (on_tree_drag_begin), view);
	g_signal_connect (col->tree, "drag-end",
			  G_CALLBACK (on_tree_drag_end), view);

	/* Connect before Nemo's DnD handlers; their handled result may stop
	 * drag-motion emission before later handlers see it. */
	g_signal_connect (col->tree, "drag-motion", G_CALLBACK (miller_drag_motion), view);
	g_signal_connect (col->tree, "drag-leave", G_CALLBACK (miller_drag_leave), view);
	col->drag_dest = nemo_tree_view_drag_dest_new (GTK_TREE_VIEW (col->tree),
						       TRUE);
	g_signal_connect (col->drag_dest, "get_root_uri",
			  G_CALLBACK (miller_get_root_uri_cb), view);
	g_signal_connect (col->drag_dest, "get_file_for_path",
			  G_CALLBACK (miller_get_file_for_path_cb), view);
	g_signal_connect (col->drag_dest, "move_copy_items",
			  G_CALLBACK (miller_move_copy_cb), view);
	g_signal_connect (col->drag_dest, "handle_uri_list",
			  G_CALLBACK (miller_handle_uri_list_cb), view);
	g_signal_connect (col->drag_dest, "handle_netscape_url",
			  G_CALLBACK (miller_handle_netscape_url_cb), view);
	g_signal_connect (col->drag_dest, "handle_text",
			  G_CALLBACK (miller_handle_text_cb), view);
	/* Keep NemoTreeViewDragDest active: drops into a column (or onto a
	 * folder row) go through nemo_view_move_copy_items like List view. */
}

static MillerCol *
miller_col_new (NemoMillerColumnsView *view, GFile *dir, gboolean is_chain,
		const char *title)
{
	MillerCol *col;

	col = g_new0 (MillerCol, 1);
	col->is_chain = is_chain;
	col->dir = (dir != NULL) ? g_object_ref (dir) : NULL;
	col->icon_size = zoom_pixel_size (view->details->zoom_level);
    col->group_files = g_hash_table_new_full (miller_file_gfile_hash,
                        miller_file_gfile_equal, g_object_unref, (GDestroyNotify)nemo_file_unref);
    col->collapsed_groups = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

	col->store = gtk_list_store_new (COL_COUNT,
					 GDK_TYPE_PIXBUF,
					 G_TYPE_STRING,
					 NEMO_TYPE_FILE,
					 G_TYPE_FILE,
					 G_TYPE_BOOLEAN,
					 G_TYPE_INT64,
					 G_TYPE_INT64,
					 G_TYPE_STRING,
					 GDK_TYPE_RGBA,
					 G_TYPE_BOOLEAN,
					 GDK_TYPE_RGBA,
					 GDK_TYPE_PIXBUF,
                     G_TYPE_STRING,
                     G_TYPE_BOOLEAN);

	gtk_tree_sortable_set_sort_func (GTK_TREE_SORTABLE (col->store),
					 COL_NAME, col_sort_func, view, NULL);
	gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (col->store),
					      COL_NAME, GTK_SORT_ASCENDING);

	col->tree = gtk_tree_view_new_with_model (GTK_TREE_MODEL (col->store));
	g_object_set_data (G_OBJECT (col->tree), "miller-view", view);

	miller_col_renderers_init (view, col, title);
	miller_col_tree_init (view, col, title);
	miller_col_dnd_init (view, col);

	col->scroll = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (col->scroll),
					GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (col->scroll),
                                             GTK_SHADOW_NONE);
	gtk_widget_set_size_request (col->scroll, 112, -1);
	gtk_container_add (GTK_CONTAINER (col->scroll), col->tree);
	g_signal_connect_object (col->scroll, "size-allocate",
	                         G_CALLBACK (miller_divider_redraw_cb), view, G_CONNECT_SWAPPED);

	return col;
}

static void
miller_col_destroy (NemoMillerColumnsView *view, MillerCol *col,
		    gboolean remove_from_list, gboolean drop_subdirectory)
{
	if (view->details->band_press_tree == col->tree) {
		view->details->band_press_tree = NULL;
	}
	if (view->details->band_suspended_tree == col->tree) {
		view->details->band_suspended_tree = NULL;
	}
	if (view->details->frozen_tree == col->tree) {
		view->details->frozen_tree = NULL;
		g_clear_pointer (&view->details->frozen_path, gtk_tree_path_free);
	}
	if (remove_from_list) {
		view->details->columns = g_list_remove (view->details->columns, col);
	}
	if (drop_subdirectory) {
		miller_unmonitor_dir (view, col);
	}
	if (view->details->action_col == col) {
		miller_set_action_column (view, NULL);
	}
	if (col->directory != NULL) {
		nemo_directory_unref (col->directory);
	}
	miller_drag_cancel_hover (col);
	if (col->scan_cancel != NULL) {
		g_cancellable_cancel (col->scan_cancel);
		g_object_unref (col->scan_cancel);
	}
	if (col->drag_dest != NULL) {
		g_object_unref (col->drag_dest);
	}
	if (!gtk_widget_in_destruction (GTK_WIDGET (view))) {
		gtk_widget_destroy (col->scroll);
	}
	if (col->dir != NULL) {
		g_object_unref (col->dir);
	}
	if (col->group_sync_id) g_source_remove (col->group_sync_id);
    if (col->group_rebuild_id) g_source_remove (col->group_rebuild_id);
	if (col->fit_id) g_source_remove (col->fit_id);
	g_signal_handlers_disconnect_by_data (col->store, col);
    g_clear_pointer (&col->group_files, g_hash_table_unref);
    g_clear_pointer (&col->collapsed_groups, g_hash_table_unref);
	g_object_unref (col->store);
	g_free (col);
}

static void
remove_extension_columns_after (NemoMillerColumnsView *view, MillerCol *keep)
{
	GList *l, *next;
	GList *found;
	MillerCol *col;

	found = g_list_find (view->details->columns, keep);
	if (found == NULL) {
		return;
	}

	for (l = found->next; l != NULL; l = next) {
		next = l->next;
		col = l->data;
		miller_col_destroy (view, col, TRUE, TRUE);
	}
}

/* Persist genuine divider changes even if GTK doesn't deliver button-release
 * on the handle window (Wayland/GTK themes differ in event routing). */
static gboolean
miller_save_widths_later (gpointer user_data)
{
    NemoMillerColumnsView *view = user_data;
    view->details->widths_save_id = 0;
    if (!gtk_widget_in_destruction (GTK_WIDGET (view)))
        miller_save_widths (view);
    return G_SOURCE_REMOVE;
}

static void
miller_schedule_width_save (NemoMillerColumnsView *view)
{
    if (view->details->widths_save_id != 0)
        g_source_remove (view->details->widths_save_id);
    view->details->widths_save_id = g_timeout_add (650,
            miller_save_widths_later, view);
}

static void
on_paned_position_notify (GObject *gobject, GParamSpec *pspec, gpointer user_data)
{
	NemoMillerColumnsView *view = user_data;
	int idx, pos;

	idx = GPOINTER_TO_INT (g_object_get_data (gobject, "miller-index"));
	g_object_get (gobject, "position", &pos, NULL);
	if (view->details->restoring_widths ||
	    g_object_get_data (gobject, "miller-rename-collapsing") != NULL ||
	    g_object_get_data (gobject, "miller-column-opening") != NULL)
		return;
	/* An ordinary GTK relayout must not overwrite the user's saved sizes.
	 * GtkPaned's handle is focusable for keyboard size adjustments. */
	if (!g_object_get_data (gobject, "miller-user-resizing") &&
	    !gtk_widget_has_focus (GTK_WIDGET (gobject)))
		return;
	if (pos < 112) return;
	if (view->details->auto_fit) {
		MillerCol *col = g_list_nth_data (view->details->columns, idx);
		if (col != NULL)
			col->manual_width = pos; /* this column only, not persisted */
		return;
	}
	while (view->details->col_widths->len <= idx) {
		int def = view->details->default_width;

		g_array_append_val (view->details->col_widths, def);
	}
	g_array_index (view->details->col_widths, int, idx) = pos;
	miller_schedule_width_save (view);
}

/* Only a real primary-button handle press is a resize gesture.
 * Keep state on GtkPaned itself, so an animation or container allocation
 * never masquerades as a user drag. */
static gboolean
on_paned_button_press (GtkWidget *widget, GdkEventButton *event,
                       gpointer user_data)
{
    if (event->button == GDK_BUTTON_PRIMARY &&
        event->type == GDK_BUTTON_PRESS &&
        event->window == gtk_paned_get_handle_window (GTK_PANED (widget))) {
        g_object_set_data (G_OBJECT (widget), "miller-user-resizing",
                           GINT_TO_POINTER (1));
        /* auto-fit keeps the fitted width as a size request; relax it
         * during the drag so the column can also be made narrower */
        if (gtk_paned_get_child1 (GTK_PANED (widget)) != NULL)
            gtk_widget_set_size_request (gtk_paned_get_child1 (GTK_PANED (widget)), 112, -1);
    }
    (void) user_data;
    return FALSE;
}

static gboolean
on_paned_button_release (GtkWidget *widget, GdkEventButton *event,
			 gpointer user_data)
{
	NemoMillerColumnsView *view = user_data;
	int idx;
	MillerCol *col;

	/* GTK themes/Wayland may deliver release on the widget window,
	 * not the handle. The press marker is authoritative. */
	if (event->button != GDK_BUTTON_PRIMARY ||
	    !g_object_get_data (G_OBJECT (widget), "miller-user-resizing"))
		return FALSE;
	g_object_set_data (G_OBJECT (widget), "miller-user-resizing", NULL);

	/* drag finished: apply the recorded width as the column's minimum
	 * width so a deep chain overflows the window again (scrollbar) */
	idx = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (widget), "miller-index"));
	col = g_list_nth_data (view->details->columns, idx);
	if (col != NULL && view->details->auto_fit) {
		col->manual_width = gtk_paned_get_position (GTK_PANED (widget));
		miller_col_apply_width (view, col);
		return FALSE;
	}
	if (col != NULL && idx < view->details->col_widths->len) {
        /* GtkPaned.position already records the desired width. A size
         * request equal to that width would become a *hard minimum*,
         * preventing dragging the handle to the left on the next attempt. */
        gtk_widget_set_size_request (col->scroll, 112, -1);
        miller_save_widths (view);
	}
	return FALSE;
}

/* Double-click a divider to fit its longest visible filename. Cap the
 * result to prevent a single unusual filename swallowing the window. */
static gboolean
miller_paned_auto_fit (GtkWidget *widget, GdkEventButton *event, gpointer data)
{
	NemoMillerColumnsView *view = data;
	MillerCol *col;
	GtkTreeIter iter;
	int index, width, max_width;
	PangoLayout *layout;

	if (event->button != GDK_BUTTON_PRIMARY || event->type != GDK_2BUTTON_PRESS ||
	    event->window != gtk_paned_get_handle_window (GTK_PANED (widget))) return FALSE;
	index = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (widget), "miller-index"));
	col = g_list_nth_data (view->details->columns, index);
	if (col == NULL) return FALSE;
	if (view->details->auto_fit) {
		/* double-click returns a manually sized column to auto-fit */
		g_object_set_data (G_OBJECT (widget), "miller-user-resizing", NULL);
		col->manual_width = 0;
		col->auto_width = miller_col_measure (view, col);
		miller_col_apply_width (view, col);
		return TRUE;
	}
	layout = gtk_widget_create_pango_layout (col->tree, NULL);
	max_width = view->details->default_width;
	if (gtk_tree_model_get_iter_first (GTK_TREE_MODEL (col->store), &iter)) do {
		char *name = NULL;
		gtk_tree_model_get (GTK_TREE_MODEL (col->store), &iter, COL_NAME, &name, -1);
		if (name != NULL) {
			pango_layout_set_text (layout, name, -1);
			pango_layout_get_pixel_size (layout, &width, NULL);
			max_width = MAX (max_width, MIN (800, width + col->icon_size + 52));
		}
		g_free (name);
	} while (gtk_tree_model_iter_next (GTK_TREE_MODEL (col->store), &iter));
	g_object_unref (layout);
	max_width = CLAMP (max_width, 120, 800);
    gtk_widget_set_size_request (col->scroll, 112, -1);
    gtk_paned_set_position (GTK_PANED (widget), max_width);
	while (view->details->col_widths->len <= index) {
		int def = view->details->default_width;
		g_array_append_val (view->details->col_widths, def);
	}
	g_array_index (view->details->col_widths, int, index) = max_width;
	miller_save_widths (view);
	return TRUE;
}

/* Column divider, painted by Dakkar itself after the paned has drawn its
 * handle.  A CSS-only separator can be hidden by anything that styles
 * "paned > separator" (the GTK theme, the custom Dakkar palette, a user
 * gtk.css, or a theme whose handle is transparent): on Breeze Dark the old
 * 2px @theme_bg_color strip was #202326 on #141618.  The colour comes from
 * the paned's own text colour (system theme or custom Dakkar text colour),
 * which by definition contrasts with the content background behind it. */
static void
miller_divider_color (GtkWidget *widget, GdkRGBA *color)
{
	GtkStyleContext *context = gtk_widget_get_style_context (widget);

	gtk_style_context_get_color (context, gtk_style_context_get_state (context), color);
	if (color->alpha < 0.5 &&
	    !gtk_style_context_lookup_color (context, "theme_fg_color", color) &&
	    !gtk_style_context_lookup_color (context, "theme_text_color", color)) {
		/* never transparent: fall back to the toplevel's text colour */
		GtkWidget *top = gtk_widget_get_toplevel (widget);
		gtk_style_context_get_color (gtk_widget_get_style_context (top),
		                             GTK_STATE_FLAG_NORMAL, color);
	}
	color->alpha = 0.30;
}

static gboolean
miller_paned_draw_divider (GtkWidget *paned, cairo_t *cr, gpointer user_data)
{
	GtkWidget *child1 = gtk_paned_get_child1 (GTK_PANED (paned));
	GtkWidget *child2 = gtk_paned_get_child2 (GTK_PANED (paned));
	GdkRGBA color;
	gint x1 = 0, x2 = 0, y = 0;
	gdouble x;

	if (child1 == NULL || child2 == NULL ||
	    !gtk_widget_get_visible (child1) || !gtk_widget_get_visible (child2) ||
	    !gtk_widget_get_realized (child1) || !gtk_widget_get_realized (child2))
		return FALSE;
	/* GtkPaned emits "draw" once per GdkWindow it owns (its own one plus
	 * the two child clip windows).  Paint only in the pass for the
	 * paned's own window, where the handle lives; otherwise the line was
	 * painted again in the children's passes (doubled, darker lines). */
	if (!gtk_cairo_should_draw_window (cr, gtk_widget_get_window (paned)))
		return FALSE;
	if (gtk_widget_get_allocated_width (child1) <= 1 ||
	    gtk_widget_get_allocated_width (child2) <= 1)
		return FALSE;
	/* The children of a GtkPaned live in their own child GdkWindows, so
	 * their allocations are relative to THOSE windows (x is 0 for both).
	 * Mixing them with the paned's allocation put the line at arbitrary
	 * x positions.  Translate the real edges into paned coordinates; this
	 * also takes horizontal scrolling into account.  A new column slides
	 * in through its start margin, which is not part of the boundary. */
	if (!gtk_widget_translate_coordinates (child1, paned,
	                                       gtk_widget_get_allocated_width (child1), 0,
	                                       &x1, &y) ||
	    !gtk_widget_translate_coordinates (child2, paned,
	                                       -gtk_widget_get_margin_start (child2), 0,
	                                       &x2, &y))
		return FALSE;
	if (x2 < x1)
		return FALSE;
	/* centre of the handle between the two columns */
	x = floor ((x1 + x2) / 2.0);
	miller_divider_color (paned, &color);
	cairo_save (cr);
	gdk_cairo_set_source_rgba (cr, &color);
	cairo_rectangle (cr, x, 0, 1, gtk_widget_get_allocated_height (paned));
	cairo_fill (cr);
	cairo_restore (cr);
	(void) user_data;
	return FALSE;
}

/* The dividers live in the paneds' own windows, which GTK does not
 * repaint when only a column inside them moves or changes size, so a
 * new column could show up without its line until the next click. */
static void
miller_queue_divider_redraw (NemoMillerColumnsView *view)
{
	if (view->details != NULL && GTK_IS_WIDGET (view->details->paned_root))
		gtk_widget_queue_draw (view->details->paned_root);
}

static void
miller_divider_redraw_cb (gpointer view)
{
	miller_queue_divider_redraw (NEMO_MILLER_COLUMNS_VIEW (view));
}

/* shared setup for every Miller GtkPaned: width memory + drag handlers */
static void
miller_setup_paned (NemoMillerColumnsView *view, GtkPaned *paned, int idx)
{
	int width;

	if (g_object_get_data (G_OBJECT (paned), "dakkar-divider") == NULL) {
		g_object_set_data (G_OBJECT (paned), "dakkar-divider", GINT_TO_POINTER (1));
		gtk_style_context_add_class (gtk_widget_get_style_context (GTK_WIDGET (paned)),
		                             "miller-column-divider");
		g_signal_connect_after (paned, "draw",
		                        G_CALLBACK (miller_paned_draw_divider), NULL);
	}

	g_object_set_data (G_OBJECT (paned), "miller-index",
			   GINT_TO_POINTER (idx));
	while (view->details->col_widths->len <= idx) {
		int def = view->details->default_width;

		g_array_append_val (view->details->col_widths, def);
	}
	width = g_array_index (view->details->col_widths, int, idx);
	{
		MillerCol *col = g_list_nth_data (view->details->columns, idx);
		int target = miller_col_target_width (view, col);
		if (target <= 0 && view->details->auto_fit && col != NULL) {
			col->auto_width = miller_col_measure (view, col);
			target = col->auto_width;
		}
		if (target > 0) {
			gtk_widget_set_size_request (gtk_paned_get_child1 (paned), target, -1);
			gtk_paned_set_position (paned, target);
			width = -1;
		}
	}
	if (width == -1) {
		/* auto-fit applied above */
	} else if (width > 0) {
        gtk_widget_set_size_request (gtk_paned_get_child1 (paned), 112, -1);
        gtk_paned_set_position (paned, width);
	} else {
		gtk_paned_set_position (paned, view->details->default_width);
	}
	g_signal_connect (paned, "notify::position",
			  G_CALLBACK (on_paned_position_notify), view);
	g_signal_connect (paned, "button-press-event",
			  G_CALLBACK (on_paned_button_press), view);
	g_signal_connect (paned, "button-release-event",
			  G_CALLBACK (on_paned_button_release), view);
	g_signal_connect (paned, "button-press-event",
			  G_CALLBACK (miller_paned_auto_fit), view);
}

static GtkWidget *
build_paned_chain (NemoMillerColumnsView *view, GList *from)
{
	GtkWidget *first_scroll;
	GtkWidget *paned;
	int idx;

	first_scroll = ((MillerCol *) from->data)->scroll;

	if (from->next == NULL) {
		return first_scroll;
	}

	paned = gtk_paned_new (GTK_ORIENTATION_HORIZONTAL);
	gtk_paned_pack1 (GTK_PANED (paned), first_scroll, FALSE, FALSE);
	gtk_paned_pack2 (GTK_PANED (paned),
			 build_paned_chain (view, from->next), TRUE, FALSE);

	idx = g_list_position (view->details->columns, from);
	miller_setup_paned (view, GTK_PANED (paned), idx);
	/* The surgical rebuild (rewire_tail) packs this tail into an existing
	 * paned without show_all: an unshown paned hid every new column when
	 * jumping two or more levels (location bar, bookmark, after deleting
	 * the open folder), leaving the view without its current column. */
	gtk_widget_show (paned);

	return paned;
}

/* Fade only the content, after its first asynchronous batch was received.
 * ScrolledWindow opacity affects GTK3 windowed children (ghost rows), so
 * a GTK frame-clock tick updates the tree, and owns its own state. */
typedef struct {
    gint64 start_us;
} MillerContentFade;

static gboolean
miller_content_fade_tick (GtkWidget *widget, GdkFrameClock *clock,
                          gpointer user_data)
{
    MillerContentFade *fade = user_data;
    gint64 now = gdk_frame_clock_get_frame_time (clock);
    gdouble p, eased;
    if (fade->start_us == 0)
        fade->start_us = now;
    p = CLAMP ((now - fade->start_us) / 180000.0, 0.0, 1.0);
    eased = 1.0 - (1.0-p)*(1.0-p)*(1.0-p);
    gtk_widget_set_opacity (widget, 0.55 + eased * 0.45);
    if (p >= 1.0) {
        gtk_widget_set_opacity (widget, 1.0);
        return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}

static void
miller_fade_on_map (GtkWidget *widget, gpointer user_data)
{
    g_signal_handlers_disconnect_by_func (widget, miller_fade_on_map, NULL);
    gtk_widget_set_opacity (widget, 0.55);
    gtk_widget_add_tick_callback (widget, miller_content_fade_tick,
                                  g_new0 (MillerContentFade, 1), g_free);
}

static void
miller_fade_in (GtkWidget *tree)
{
    if (!GTK_IS_TREE_VIEW (tree))
        return;
    if (!gtk_widget_get_mapped (tree)) {
        if (!g_signal_handler_find (tree, G_SIGNAL_MATCH_FUNC, 0, 0,
                                    NULL, G_CALLBACK (miller_fade_on_map), NULL))
            g_signal_connect (tree, "map", G_CALLBACK (miller_fade_on_map), NULL);
        return;
    }
    gtk_widget_set_opacity (tree, 0.55);
    gtk_widget_add_tick_callback (tree, miller_content_fade_tick,
                                  g_new0 (MillerContentFade, 1), g_free);
}

/* Visibly slide a newly opened column even when the horizontal adjustment
 * has NO overflow. A scrollbar animation alone cannot do this. Keep the
 * tick attached to the column widget: when the column disappears GTK removes
 * its tick and frees the context safely. No callback retains a MillerCol*. */
typedef struct {
    gint64 start_us;
    gint64 last_frame_us;
    gint offset;
    GWeakRef paned_ref;
    gboolean registered;
} MillerColumnEnter;

/* GTK can cancel the tick when a tab or column is destroyed mid-animation.
 * Always clear the transient paned guard, including on cancellation. */
static void
miller_column_enter_free (gpointer data)
{
    MillerColumnEnter *anim = data;
    GtkWidget *paned = g_weak_ref_get (&anim->paned_ref);
    if (paned != NULL) {
        gint count = GPOINTER_TO_INT (g_object_get_data (
                       G_OBJECT (paned), "miller-column-opening"));
        g_object_set_data (G_OBJECT (paned), "miller-column-opening",
                           count > 1 ? GINT_TO_POINTER (count - 1) : NULL);
        g_object_unref (paned);
    }
    g_weak_ref_clear (&anim->paned_ref);
    g_free (anim);
}

static gboolean
miller_column_enter_tick (GtkWidget *widget, GdkFrameClock *clock,
                          gpointer data)
{
    MillerColumnEnter *anim = data;
    gdouble progress, ease;
    gint64 now = gdk_frame_clock_get_frame_time (clock);
    if (anim->last_frame_us > 0 && now - anim->last_frame_us > 35000 &&
        g_getenv ("NEMO_MILLER_DEBUG_ANIMATION") != NULL)
        g_message ("Miller animation frame interval: %.1f ms",
                   (now - anim->last_frame_us) / 1000.0);
    anim->last_frame_us = now;
    if (anim->start_us == 0)
        anim->start_us = now;
    progress = CLAMP ((now - anim->start_us) / 230000.0, 0.0, 1.0);
    ease = 1.0 - (1.0-progress)*(1.0-progress)*(1.0-progress);
    if (!anim->registered) {
        GtkWidget *paned = gtk_widget_get_ancestor (widget, GTK_TYPE_PANED);
        if (paned != NULL) {
            gint count = GPOINTER_TO_INT (g_object_get_data (
                           G_OBJECT (paned), "miller-column-opening"));
            g_object_set_data (G_OBJECT (paned), "miller-column-opening",
                               GINT_TO_POINTER (count + 1));
            g_weak_ref_set (&anim->paned_ref, paned);
            anim->registered = TRUE;
        }
    }
    gtk_widget_set_margin_start (widget, (gint) (anim->offset * (1.0-ease)));
    if (progress >= 1.0) {
        gtk_widget_set_margin_start (widget, 0);
        return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}

static void
miller_animate_new_column (GtkWidget *scroll)
{
    MillerColumnEnter *anim;
    if (!GTK_IS_WIDGET (scroll))
        return;
    /* 64px displacement is visible without an expensive resize of the
     * entire paned chain each frame; GtkPaned owns final column widths. */
    anim = g_new0 (MillerColumnEnter, 1);
    anim->offset = 64;
    g_weak_ref_init (&anim->paned_ref, NULL);
    gtk_widget_set_margin_start (scroll, anim->offset);
    gtk_widget_add_tick_callback (scroll, miller_column_enter_tick,
                                  anim, miller_column_enter_free);
}

/* wire `tail` into the paned chain as the rightmost subtree WITHOUT
 * touching the columns before index `seam`: the click flash came from
 * reparenting (and refilling) the whole chain on every navigation.
 * tail == NULL means the chain ends at `seam`: unwrap the last kept
 * column so it becomes the rightmost leaf again. */
static void
rewire_tail (NemoMillerColumnsView *view, int seam, GtkWidget *tail)
{
	MillerCol *last;
	GtkWidget *parent;

	if (seam <= 0) {
		return;
	}
	last = g_list_nth_data (view->details->columns, seam - 1);
	if (last == NULL) {
		return;
	}
	parent = gtk_widget_get_parent (last->scroll);

	if (tail == NULL) {
		if (GTK_IS_PANED (parent) &&
		    gtk_paned_get_child1 (GTK_PANED (parent)) == last->scroll) {
			GtkWidget *grand;
			GtkWidget *paned = parent;

			grand = gtk_widget_get_parent (paned);
			g_object_ref (last->scroll);
			gtk_container_remove (GTK_CONTAINER (paned),
					      last->scroll);
			gtk_widget_destroy (paned);
			if (GTK_IS_PANED (grand)) {
				gtk_paned_pack2 (GTK_PANED (grand),
						 last->scroll, TRUE, FALSE);
			} else {
				gtk_box_pack_start (
					GTK_BOX (view->details->paned_root),
					last->scroll, TRUE, TRUE, 0);
			}
			g_object_unref (last->scroll);
		}
		/* already rightmost (child2 of a paned or bare in the
		 * box): nothing to do */
		return;
	}

	if (GTK_IS_PANED (parent)) {
		GtkPaned *paned = GTK_PANED (parent);

		if (gtk_paned_get_child1 (paned) == last->scroll) {
			GtkWidget *husk;

			/* the paned's right half is free (tail columns
			 * just destroyed): drop the tail straight in */
			while ((husk = gtk_paned_get_child2 (paned)) != NULL) {
				gtk_widget_destroy (husk);
			}
			gtk_paned_pack2 (paned, tail, TRUE, FALSE);
		} else {
			/* last is the rightmost leaf: wrap it in a new
			 * paned together with the tail */
			GtkWidget *np;

			np = gtk_paned_new (GTK_ORIENTATION_HORIZONTAL);
			g_object_ref (last->scroll);
			gtk_container_remove (GTK_CONTAINER (parent),
					      last->scroll);
			gtk_paned_pack1 (GTK_PANED (np), last->scroll,
					 FALSE, FALSE);
			gtk_paned_pack2 (GTK_PANED (np), tail, TRUE, FALSE);
			gtk_paned_pack2 (paned, np, TRUE, FALSE);
			miller_setup_paned (view, GTK_PANED (np), seam - 1);
			gtk_widget_show_all (np);
			g_object_unref (last->scroll);
		}
	} else {
		/* single column sits directly in paned_root: wrap it */
		GtkWidget *np;

		np = gtk_paned_new (GTK_ORIENTATION_HORIZONTAL);
		g_object_ref (last->scroll);
		gtk_container_remove (GTK_CONTAINER (parent), last->scroll);
		gtk_paned_pack1 (GTK_PANED (np), last->scroll, FALSE, FALSE);
		gtk_paned_pack2 (GTK_PANED (np), tail, TRUE, FALSE);
		gtk_box_pack_start (GTK_BOX (view->details->paned_root),
				    np, TRUE, TRUE, 0);
		miller_setup_paned (view, GTK_PANED (np), seam - 1);
		gtk_widget_show_all (np);
		g_object_unref (last->scroll);
	}
}

/* drag one separator wider/narrower: that column resizes and everything
 * to its right follows; columns to its left keep their width.
 * paned_root is a permanent wrapper box created in init(); columns are
 * only ever re-parented, never destroyed as part of the layout */
static void
relayout_columns (NemoMillerColumnsView *view)
{
	GList *l, *children;
	GtkAdjustment *vadj;
	gdouble vpos = 0.0;

	if (view->details->columns == NULL ||
	    gtk_widget_in_destruction (GTK_WIDGET (view)))
		return;

	/* reparenting rebuilds every GtkTreeView and resets its
	 * vertical scroll: remember the first column's position and
	 * restore it after the rebuild (the "column jumps to the
	 * top" glitch) */
	vadj = gtk_scrolled_window_get_vadjustment (
		GTK_SCROLLED_WINDOW (((MillerCol *) view->details->columns->data)->scroll));
	vpos = gtk_adjustment_get_value (vadj);

	/* ref every current scroll so re-parenting never destroys one */
	for (l = view->details->columns; l != NULL; l = l->next) {
		g_object_ref (((MillerCol *) l->data)->scroll);
	}

	view->details->restoring_widths = TRUE;
	/* Detach every column from its old GtkPaned first.  The
	 * old paneds die when they leave paned_root, and a dying GtkContainer
	 * destroys its children: the (merely referenced) column scrolls would
	 * be re-packed already destroyed and crash in gtk_widget_map
	 * (toggling "Fit each column to its names" off with 2+ columns). */
	for (l = view->details->columns; l != NULL; l = l->next) {
		GtkWidget *scroll = ((MillerCol *) l->data)->scroll;
		GtkWidget *parent = gtk_widget_get_parent (scroll);
		if (parent != NULL && parent != view->details->paned_root)
			gtk_container_remove (GTK_CONTAINER (parent), scroll);
	}
	children = gtk_container_get_children (GTK_CONTAINER (view->details->paned_root));
	for (l = children; l != NULL; l = l->next) {
		gtk_container_remove (GTK_CONTAINER (view->details->paned_root),
				      GTK_WIDGET (l->data));
	}
	g_list_free (children);

	if (view->details->columns != NULL) {
		GtkWidget *chain;
		GtkAdjustment *adj;

		chain = build_paned_chain (view, view->details->columns);
		gtk_box_pack_start (GTK_BOX (view->details->paned_root),
				    chain, TRUE, TRUE, 0);
		gtk_widget_show_all (chain);

		adj = gtk_scrolled_window_get_vadjustment (
			GTK_SCROLLED_WINDOW (((MillerCol *) view->details->columns->data)->scroll));
		gtk_adjustment_set_value (adj, vpos);
	}

	for (l = view->details->columns; l != NULL; l = l->next) {
		g_object_unref (((MillerCol *) l->data)->scroll);
	}
	view->details->restoring_widths = FALSE;
	miller_queue_divider_redraw (view);
}

/* Frame-clock driven horizontal navigation. Unlike an interval timer, this
 * follows GTK's actual repaint cadence, interpolates using elapsed time and
 * cancels the previous animation on rapid navigation. The directory model
 * can continue loading asynchronously without blocking a frame. */
typedef struct {
    GWeakRef view_ref; /* avoid retaining an unmapped/closed tab forever */
    gint64 start_us;
    gdouble from;
    guint settle_frames;
} SlideCtx;

static void
miller_slide_ctx_free (gpointer data)
{
    SlideCtx *ctx = data;
    g_weak_ref_clear (&ctx->view_ref);
    g_free (ctx);
}

static gboolean
miller_slide_tick (GtkWidget *widget, GdkFrameClock *clock, gpointer data)
{
    SlideCtx *ctx = data;
    NemoMillerColumnsView *view = g_weak_ref_get (&ctx->view_ref);
    GtkAdjustment *hadj;
    gdouble to, progress, ease;
    gint64 now;

    if (view == NULL) return G_SOURCE_REMOVE;
    if (gtk_widget_in_destruction (widget) ||
        gtk_widget_in_destruction (GTK_WIDGET (view))) {
        view->details->slide_tick_id = 0;
        g_object_unref (view);
        return G_SOURCE_REMOVE;
    }
    hadj = gtk_scrolled_window_get_hadjustment (
        GTK_SCROLLED_WINDOW (view->details->columns_scrolled));
    now = gdk_frame_clock_get_frame_time (clock);
    to = MAX (gtk_adjustment_get_lower (hadj),
              gtk_adjustment_get_upper (hadj) - gtk_adjustment_get_page_size (hadj));
    if (to <= ctx->from + 0.5) {
        /* GtkPaned may not have finished allocating its new child on
         * the first frame. Wait briefly for a stable scrollbar range. */
        if (++ctx->settle_frames < 4) {
            g_object_unref (view);
            return G_SOURCE_CONTINUE;
        }
        view->details->slide_tick_id = 0;
        g_object_unref (view);
        return G_SOURCE_REMOVE;
    }
    if (ctx->start_us == 0) {
        ctx->start_us = now;
        gtk_adjustment_set_value (hadj, CLAMP (ctx->from,
                                  gtk_adjustment_get_lower (hadj), to));
    }
    progress = CLAMP ((now - ctx->start_us) / 250000.0, 0.0, 1.0);
    /* Quartic ease-out, same visual rhythm as the rename collapse. */
    ease = 1.0 - (1.0 - progress) * (1.0 - progress) *
                  (1.0 - progress) * (1.0 - progress);
    gtk_adjustment_set_value (hadj, CLAMP (ctx->from + (to - ctx->from) * ease,
                                            gtk_adjustment_get_lower (hadj), to));
    if (progress >= 1.0) {
        view->details->slide_tick_id = 0;
        g_object_unref (view);
        return G_SOURCE_REMOVE;
    }
    g_object_unref (view);
    return G_SOURCE_CONTINUE;
}

/* Defer the start by one frame so GTK can allocate the *new* paned
 * subtree. Starting synchronously saw the old adjustment upper bound and
 * produced a snap rather than an actual animated horizontal movement. */
static gboolean
miller_slide_start_after_layout (gpointer user_data)
{
    NemoMillerColumnsView *view = user_data;
    SlideCtx *ctx;
    GtkWidget *scrolled = view->details->columns_scrolled;
    view->details->slide_start_idle_id = 0;
    if (gtk_widget_in_destruction (GTK_WIDGET (view)) ||
        !gtk_widget_get_mapped (scrolled))
        return G_SOURCE_REMOVE;
    if (view->details->slide_tick_id != 0) {
        gtk_widget_remove_tick_callback (scrolled, view->details->slide_tick_id);
        view->details->slide_tick_id = 0;
    }
    ctx = g_new0 (SlideCtx, 1);
    ctx->from = view->details->slide_from;
    g_weak_ref_init (&ctx->view_ref, view);
    view->details->slide_tick_id = gtk_widget_add_tick_callback (
        scrolled, miller_slide_tick, ctx, miller_slide_ctx_free);
    return G_SOURCE_REMOVE;
}

static void
miller_slide_columns_to_end (NemoMillerColumnsView *view)
{
    GtkWidget *scrolled = view->details->columns_scrolled;
    GtkAdjustment *hadj = gtk_scrolled_window_get_hadjustment (
                             GTK_SCROLLED_WINDOW (scrolled));
    /* Take the start position before resizing the paned subtree. */
    view->details->slide_from = gtk_adjustment_get_value (hadj);
    if (view->details->slide_start_idle_id != 0) {
        g_source_remove (view->details->slide_start_idle_id);
        view->details->slide_start_idle_id = 0;
    }
    if (view->details->slide_tick_id != 0) {
        gtk_widget_remove_tick_callback (scrolled, view->details->slide_tick_id);
        view->details->slide_tick_id = 0;
    }
    gtk_widget_queue_resize (scrolled);
    view->details->slide_start_idle_id = g_idle_add_full (
        G_PRIORITY_DEFAULT_IDLE, miller_slide_start_after_layout,
        g_object_ref (view), g_object_unref);
}

static void
update_col_visibility (NemoMillerColumnsView *view)
{
	/* every column stays put: a deep chain simply overflows the window
	 * and the horizontal scrollbar below the columns handles it */
	relayout_columns (view);
	miller_slide_columns_to_end (view);
}

/* ------------------------------------------------------------------ */
/* manual enumeration for ancestor (chain) columns                     */
/* ------------------------------------------------------------------ */

/* GIO batches keep slow/remote directories off GTK's main thread. The
 * scan holds its view alive and identifies the column by generation: a
 * completed callback must never dereference a destroyed column. */
typedef struct {
	NemoMillerColumnsView *view;
	GFile *dir;
	GCancellable *cancel;
	GFileEnumerator *enumerator;
	GHashTable *seen;
	GHashTable *known;
	GtkListStore *store;
	gboolean suppress_row_deleted;
	gulong row_changed_handler;
	gulong row_deleted_handler;
	guint64 serial;
	gboolean reconcile;
	gboolean show_hidden;
    /* The GIO enumerator runs off the main loop; model/icon creation does
     * not.  Drain each reply over multiple short idle slices to keep frames
     * responsive even when the directory contains many thumbnailed files. */
    GList *pending_items;
    guint drain_idle_id;
} MillerScan;

static MillerCol *
miller_scan_col (MillerScan *scan)
{
	GList *it;
	if (g_cancellable_is_cancelled (scan->cancel)) return NULL;
	for (it = scan->view->details->columns; it; it = it->next) {
		MillerCol *col = it->data;
		if (col->scan_id == scan->serial && col->dir &&
		    g_file_equal (col->dir, scan->dir)) return col;
	}
	return NULL;
}

static void
miller_scan_known_from_model (MillerScan *scan)
{
	GtkTreeIter iter;
	gboolean valid;
	g_hash_table_remove_all (scan->known);
	valid = gtk_tree_model_get_iter_first (GTK_TREE_MODEL (scan->store), &iter);
	while (valid) {
		GFile *file = NULL;
		gtk_tree_model_get (GTK_TREE_MODEL (scan->store), &iter, COL_GFILE, &file, -1);
		if (file) g_hash_table_add (scan->known, file);
		valid = gtk_tree_model_iter_next (GTK_TREE_MODEL (scan->store), &iter);
	}
    MillerCol *col = miller_scan_col (scan);
    if (col != NULL) {
        GHashTableIter it; gpointer k, v;
        g_hash_table_iter_init (&it, col->group_files);
        while (g_hash_table_iter_next (&it, &k, &v))
            g_hash_table_add (scan->known, g_object_ref (k));
    }
}

static void
miller_scan_row_changed (GtkTreeModel *model, GtkTreePath *path,
                          GtkTreeIter *iter, gpointer data)
{
	MillerScan *scan = data;
	GFile *file = NULL;
	gtk_tree_model_get (model, iter, COL_GFILE, &file, -1);
	if (file) g_hash_table_add (scan->known, file);
}

static void
miller_scan_row_deleted (GtkTreeModel *model, GtkTreePath *path, gpointer data)
{
	MillerScan *scan = data;
	if (!scan->suppress_row_deleted) miller_scan_known_from_model (scan);
}

static void
miller_scan_free (MillerScan *scan)
{
    if (scan->drain_idle_id != 0)
        g_source_remove (scan->drain_idle_id);
    g_list_free_full (scan->pending_items, g_object_unref);
	g_clear_object (&scan->enumerator);
	g_clear_object (&scan->dir);
	g_clear_object (&scan->cancel);
	g_clear_pointer (&scan->seen, g_hash_table_unref);
	if (scan->store != NULL) {
		g_signal_handler_disconnect (scan->store, scan->row_changed_handler);
		g_signal_handler_disconnect (scan->store, scan->row_deleted_handler);
		g_clear_object (&scan->store);
	}
	g_clear_pointer (&scan->known, g_hash_table_unref);
	g_object_unref (scan->view);
	g_free (scan);
}

/* One outstanding enumerator request per column; process its GFileInfo
 * results in at most ~4ms of GTK work per idle iteration.  Generation checks
 * run before EACH slice, so navigating away cannot write into stale stores. */
static void miller_scan_next_cb (GObject *source, GAsyncResult *result, gpointer data);

static gboolean
miller_scan_drain_idle (gpointer user_data)
{
    MillerScan *scan = user_data;
    MillerCol *col = miller_scan_col (scan);
    gint64 started = g_get_monotonic_time ();
    guint processed = 0;
    scan->drain_idle_id = 0;
    if (col == NULL) {
        miller_scan_free (scan);
        return G_SOURCE_REMOVE;
    }
    while (scan->pending_items != NULL && processed < 16) {
        GList *head = scan->pending_items;
        GFileInfo *info = head->data;
        const char *name = g_file_info_get_name (info);
        scan->pending_items = g_list_delete_link (scan->pending_items, head);
        if (name != NULL &&
            (!g_file_info_get_is_hidden (info) || scan->show_hidden)) {
            GFile *child = g_file_get_child (scan->dir, name);
            if (scan->seen != NULL)
                g_hash_table_add (scan->seen, g_object_ref (child));
            if (!g_hash_table_contains (scan->known, child)) {
                NemoFile *file = nemo_file_get (child);
                make_row (scan->view, col->store, file, col->icon_size,
                          g_file_info_get_file_type (info) == G_FILE_TYPE_DIRECTORY);
                nemo_file_unref (file);
                g_hash_table_add (scan->known, g_object_ref (child));
            }
            g_object_unref (child);
        }
        g_object_unref (info);
        processed++;
        if (g_get_monotonic_time () - started >= 4000)
            break;
    }
    if (g_getenv ("NEMO_MILLER_DEBUG_ANIMATION") != NULL &&
        g_get_monotonic_time () - started > 12000)
        g_message ("Miller GTK batch: %.1f ms for %u rows",
                   (g_get_monotonic_time () - started) / 1000.0, processed);

    if (processed > 0) {
        if (g_object_get_data (G_OBJECT (col->tree),
                               "miller-pending-content-fade") != NULL) {
            g_object_set_data (G_OBJECT (col->tree),
                               "miller-pending-content-fade", NULL);
            miller_fade_in (col->tree);
        }
        miller_apply_navigation_selection (scan->view);
        miller_queue_pending_rename (scan->view);
    }
    if (scan->pending_items != NULL) {
        /* Lower than GTK drawing/input events, never spin in a callback. */
        scan->drain_idle_id = g_idle_add_full (G_PRIORITY_DEFAULT_IDLE + 5,
                                              miller_scan_drain_idle, scan, NULL);
    } else {
        g_file_enumerator_next_files_async (scan->enumerator, 48,
            G_PRIORITY_DEFAULT, scan->cancel, miller_scan_next_cb, scan);
    }
    return G_SOURCE_REMOVE;
}

static void
miller_scan_next_cb (GObject *source, GAsyncResult *result, gpointer data)
{
    MillerScan *scan = data;
    MillerCol *col = miller_scan_col (scan);
    GError *error = NULL;
    GList *items = g_file_enumerator_next_files_finish (
        G_FILE_ENUMERATOR (source), result, &error);

    if (col == NULL || error != NULL || items == NULL) {
        if (col != NULL && error == NULL && scan->reconcile) {
            GtkTreeIter iter;
            gboolean valid;
            GtkTreeSelection *rsel =
                gtk_tree_view_get_selection (GTK_TREE_VIEW (col->tree));
            gboolean saved_prog = scan->view->details->programmatic;
            gboolean sel_dropped = FALSE;
            scan->suppress_row_deleted = TRUE;
            /* Same rule as remove_file_from_column: rows vanishing in a
             * rescan (e.g. the opened folder was trashed) must not let GTK
             * promote the next row to an implicit selection. */
            scan->view->details->programmatic = TRUE;
            valid = gtk_tree_model_get_iter_first (GTK_TREE_MODEL (col->store), &iter);
            while (valid) {
                GFile *file = NULL;
                gtk_tree_model_get (GTK_TREE_MODEL (col->store), &iter,
                                    COL_GFILE, &file, -1);
                /* Virtual headers must survive until group_sync updates
                 * them; only real file rows are subject to reconciliation. */
                if (file == NULL || g_hash_table_contains (scan->seen, file))
                    valid = gtk_tree_model_iter_next (GTK_TREE_MODEL (col->store), &iter);
                else {
                    gint expected = gtk_tree_selection_count_selected_rows (rsel) -
                        (gtk_tree_selection_iter_is_selected (rsel, &iter) ? 1 : 0);
                    if (scan->view->details->rename_opened_target != NULL &&
                        g_file_equal (scan->view->details->rename_opened_target, file))
                        g_clear_object (&scan->view->details->rename_opened_target);
                    valid = gtk_list_store_remove (col->store, &iter);
                    if (gtk_tree_selection_count_selected_rows (rsel) != expected) {
                        gtk_tree_selection_unselect_all (rsel);
                        sel_dropped = TRUE;
                    }
                }
                g_clear_object (&file);
            }
            scan->view->details->programmatic = saved_prog;
            if (sel_dropped) {
                rebuild_selection (scan->view);
                nemo_view_notify_selection_changed (NEMO_VIEW (scan->view));
            }
            scan->suppress_row_deleted = FALSE;
            /* A collapsed entry is not in GtkListStore, but it remains in
             * group_files and must be reconciled against this GIO scan. */
            GHashTableIter known_iter;
            gpointer known_key, known_value;
            g_hash_table_iter_init (&known_iter, col->group_files);
            while (g_hash_table_iter_next (&known_iter, &known_key, &known_value)) {
                if (!g_hash_table_contains (scan->seen, known_key))
                    g_hash_table_iter_remove (&known_iter);
            }
            miller_group_schedule_sync (scan->view, col);
            miller_scan_known_from_model (scan);
            rebuild_selection (scan->view);
            nemo_view_notify_selection_changed (NEMO_VIEW (scan->view));
        }
        g_list_free_full (items, g_object_unref);
        g_clear_error (&error);
        if (col != NULL)
            miller_queue_divider_redraw (scan->view);
        miller_scan_free (scan);
        return;
    }
    /* Transfer GFileInfo ownership to the idle drain.  Never issue the next
     * async enumeration until all rows from this reply are incorporated. */
    scan->pending_items = items;
    miller_scan_drain_idle (scan);
}

static void
miller_scan_open_cb (GObject *source, GAsyncResult *result, gpointer data)
{
	MillerScan *scan = data;
	GError *error = NULL;
	GFileEnumerator *enumerator = g_file_enumerate_children_finish (G_FILE (source), result, &error);
    if (enumerator == NULL || miller_scan_col (scan) == NULL) {
        /* On an inaccessible folder, speculative rows must not survive. */
        MillerCol *current = miller_scan_col (scan);
        if (enumerator == NULL && current != NULL && scan->reconcile)
            gtk_list_store_clear (current->store);
		g_clear_object (&enumerator);
		g_clear_error (&error);
		miller_scan_free (scan);
		return;
	}
	scan->enumerator = enumerator;
	g_file_enumerator_next_files_async (enumerator, 48, G_PRIORITY_DEFAULT,
                                       scan->cancel, miller_scan_next_cb, scan);
}

/* Prefetch only a small, immediately adjacent local folder after a real
 * selection.  It is a disposable read-ahead cache, not another directory
 * monitor or a second tree model.  The normal asynchronous scan always runs
 * afterward to reconcile current filesystem contents. */
typedef struct {
    NemoMillerColumnsView *view;
    GFile *dir;
    GCancellable *cancel;
} MillerPrefetchJob;

static void
miller_prefetch_job_free (MillerPrefetchJob *job)
{
    g_clear_object (&job->cancel);
    g_clear_object (&job->dir);
    g_object_unref (job->view);
    g_free (job);
}

static gboolean
miller_prefetch_is_current (MillerPrefetchJob *job)
{
    NemoMillerColumnsViewDetails *d = job->view->details;
    return !g_cancellable_is_cancelled (job->cancel) &&
           d->prefetch_cancel == job->cancel && d->prefetch_dir != NULL &&
           g_file_equal (job->dir, d->prefetch_dir);
}

static void
miller_prefetch_items_cb (GObject *object, GAsyncResult *result, gpointer data)
{
    MillerPrefetchJob *job = data;
    GError *error = NULL;
    GList *items = g_file_enumerator_next_files_finish (
        G_FILE_ENUMERATOR (object), result, &error);
    if (error == NULL && miller_prefetch_is_current (job)) {
        GPtrArray *array = g_ptr_array_new_with_free_func (g_object_unref);
        GList *l;
        for (l = items; l != NULL; l = l->next)
            g_ptr_array_add (array, g_object_ref (l->data));
        g_clear_pointer (&job->view->details->prefetch_items, g_ptr_array_unref);
        job->view->details->prefetch_items = array;
        job->view->details->prefetch_time_us = g_get_monotonic_time ();
    }
    /* Release directory resources asynchronously; never block GTK on close. */
    g_file_enumerator_close_async (G_FILE_ENUMERATOR (object), G_PRIORITY_LOW,
                                   NULL, NULL, NULL);
    g_list_free_full (items, g_object_unref);
    g_clear_error (&error);
    miller_prefetch_job_free (job);
}

static void
miller_prefetch_open_cb (GObject *object, GAsyncResult *result, gpointer data)
{
    MillerPrefetchJob *job = data;
    GError *error = NULL;
    GFileEnumerator *e = g_file_enumerate_children_finish (
        G_FILE (object), result, &error);
    if (e != NULL && miller_prefetch_is_current (job)) {
        g_file_enumerator_next_files_async (e,
            CLAMP (g_settings_get_int (nemo_preferences, "miller-prefetch-limit"), 1, 24),
            G_PRIORITY_LOW, job->cancel, miller_prefetch_items_cb, job);
        g_object_unref (e);
    } else {
        g_clear_object (&e);
        miller_prefetch_job_free (job);
    }
    g_clear_error (&error);
}

static void
miller_prefetch_start (NemoMillerColumnsView *view, GFile *dir)
{
    NemoMillerColumnsViewDetails *d = view->details;
    MillerPrefetchJob *job;
    if (d->prefetch_cancel != NULL)
        g_cancellable_cancel (d->prefetch_cancel);
    g_clear_object (&d->prefetch_cancel);
    g_clear_object (&d->prefetch_dir);
    g_clear_pointer (&d->prefetch_items, g_ptr_array_unref);
    d->prefetch_time_us = 0;
    if (dir == NULL || !g_file_is_native (dir) ||
        !g_settings_get_boolean (nemo_preferences, "miller-prefetch-navigation"))
        return;
    d->prefetch_dir = g_object_ref (dir);
    d->prefetch_cancel = g_cancellable_new ();
    job = g_new0 (MillerPrefetchJob, 1);
    job->view = g_object_ref (view);
    job->dir = g_object_ref (dir);
    job->cancel = g_object_ref (d->prefetch_cancel);
    g_file_enumerate_children_async (dir,
         G_FILE_ATTRIBUTE_STANDARD_NAME "," G_FILE_ATTRIBUTE_STANDARD_TYPE ","
         G_FILE_ATTRIBUTE_STANDARD_IS_HIDDEN,
         G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, G_PRIORITY_LOW,
         job->cancel, miller_prefetch_open_cb, job);
}

static gboolean
miller_prefetch_use (MillerCol *col, NemoMillerColumnsView *view)
{
    NemoMillerColumnsViewDetails *d = view->details;
    guint i;
    if (col->dir == NULL || d->prefetch_dir == NULL ||
        d->prefetch_items == NULL ||
        !g_file_equal (col->dir, d->prefetch_dir) ||
        g_get_monotonic_time () - d->prefetch_time_us > 15 * G_USEC_PER_SEC)
        return FALSE;
    guint limit = CLAMP (g_settings_get_int (nemo_preferences,
                                            "miller-prefetch-limit"), 1, 24);
    for (i = 0; i < d->prefetch_items->len && i < limit; i++) {
        GFileInfo *info = g_ptr_array_index (d->prefetch_items, i);
        const char *name = g_file_info_get_name (info);
        if (name == NULL || (g_file_info_get_is_hidden (info) &&
            !g_settings_get_boolean (nemo_preferences, NEMO_PREFERENCES_SHOW_HIDDEN_FILES)))
            continue;
        GFile *child = g_file_get_child (col->dir, name);
        NemoFile *file = nemo_file_get (child);
        make_row (view, col->store, file, col->icon_size,
                  g_file_info_get_file_type (info) == G_FILE_TYPE_DIRECTORY);
        nemo_file_unref (file);
        g_object_unref (child);
    }
    g_clear_pointer (&d->prefetch_items, g_ptr_array_unref);
    g_clear_object (&d->prefetch_dir);
    g_clear_object (&d->prefetch_cancel);
    d->prefetch_time_us = 0;
    return TRUE;
}

static void
miller_start_scan (MillerCol *col, NemoMillerColumnsView *view, gboolean reconcile)
{
	MillerScan *scan;
	if (col->dir == NULL) return;
	if (col->scan_cancel != NULL) {
		g_cancellable_cancel (col->scan_cancel);
		g_object_unref (col->scan_cancel);
	}
	col->scan_cancel = g_cancellable_new ();
	col->scan_id = ++view->details->scan_serial;
	scan = g_new0 (MillerScan, 1);
	scan->view = g_object_ref (view);
	scan->dir = g_object_ref (col->dir);
	scan->cancel = g_object_ref (col->scan_cancel);
	scan->serial = col->scan_id;
	scan->reconcile = reconcile;
	scan->show_hidden = g_settings_get_boolean (nemo_preferences, NEMO_PREFERENCES_SHOW_HIDDEN_FILES);
	scan->known = g_hash_table_new_full (miller_file_gfile_hash, miller_file_gfile_equal,
	                                    g_object_unref, NULL);
	if (!reconcile && miller_prefetch_use (col, view)) {
		scan->reconcile = TRUE; /* drop stale speculative rows after scan */
	}
	scan->store = g_object_ref (col->store);
	miller_scan_known_from_model (scan);
	scan->row_changed_handler = g_signal_connect (scan->store, "row-changed",
	                                             G_CALLBACK (miller_scan_row_changed), scan);
	scan->row_deleted_handler = g_signal_connect (scan->store, "row-deleted",
	                                             G_CALLBACK (miller_scan_row_deleted), scan);
	if (scan->reconcile)
		scan->seen = g_hash_table_new_full (miller_file_gfile_hash, miller_file_gfile_equal,
		                                    g_object_unref, NULL);
	g_file_enumerate_children_async (scan->dir,
	                                G_FILE_ATTRIBUTE_STANDARD_NAME ","
	                                G_FILE_ATTRIBUTE_STANDARD_TYPE ","
	                                G_FILE_ATTRIBUTE_STANDARD_IS_HIDDEN,
	                                G_FILE_QUERY_INFO_NONE, G_PRIORITY_DEFAULT,
                                    scan->cancel, miller_scan_open_cb, scan);
}

static void
col_load_dir (MillerCol *col, NemoMillerColumnsView *view)
{
	miller_start_scan (col, view, FALSE);
}

static gboolean
select_gfile_in_column (MillerCol *col, GFile *gfile)
{
	GtkTreeIter iter;
	GtkTreeSelection *sel;

	if (gfile == NULL ||
	    !gtk_tree_model_get_iter_first (GTK_TREE_MODEL (col->store), &iter)) {
		return FALSE;
	}

	do {
		GFile *row;
		gboolean match;

		gtk_tree_model_get (GTK_TREE_MODEL (col->store), &iter, COL_GFILE, &row, -1);
		match = (row != NULL && g_file_equal (row, gfile));
		if (row != NULL) {
			g_object_unref (row);
		}
		if (match) {
            GtkTreePath *row_path = gtk_tree_model_get_path (GTK_TREE_MODEL (col->store), &iter);
			sel = gtk_tree_view_get_selection (GTK_TREE_VIEW (col->tree));
			gtk_tree_selection_select_iter (sel, &iter);
            if (row_path != NULL) {
                gtk_tree_view_scroll_to_cell (GTK_TREE_VIEW (col->tree),
                                              row_path, NULL, FALSE, 0.0, 0.0);
                gtk_tree_path_free (row_path);
            }
			return TRUE;
		}
	} while (gtk_tree_model_iter_next (GTK_TREE_MODEL (col->store), &iter));

	return FALSE;
}

/* A URI change invalidates ordinary selections, but the new location still
 * needs a visible, actual GTK row selection when its parent row exists.
 * Do not change action_col (which supplies the current folder for paste or
 * background operations). This is UI selection, never transfer permission. */
static gboolean
miller_apply_navigation_selection (NemoMillerColumnsView *view)
{
    GFile *target = g_object_get_data (G_OBJECT (view),
                                     "miller-pending-navigation-selection");
    GFile *parent;
    GList *l;
    gboolean found = FALSE;

    if (target == NULL || view->details->in_load)
        return FALSE;
    parent = g_file_get_parent (target);
    if (parent == NULL)
        return FALSE;
    for (l = view->details->columns; l != NULL; l = l->next) {
        MillerCol *col = l->data;
        if (col->dir == NULL || !g_file_equal (col->dir, parent))
            continue;
        view->details->programmatic = TRUE;
        found = select_gfile_in_column (col, target);
        view->details->programmatic = FALSE;
        if (found) {
            /* Clear before updating the UI. In particular, a focus/selection
             * signal must never re-select an older navigation target. */
            g_object_set_data (G_OBJECT (view),
                               "miller-pending-navigation-selection", NULL);
            rebuild_selection (view);
            nemo_view_notify_selection_changed (NEMO_VIEW (view));
        }
        break;
    }
    g_object_unref (parent);
    return found;
}

static void
resort_all (NemoMillerColumnsView *view)
{
	GList *l;
	MillerCol *col;

	for (l = view->details->columns; l != NULL; l = l->next) {
		col = l->data;
		gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (col->store),
						      COL_NAME, GTK_SORT_ASCENDING);
	}
}

/* ------------------------------------------------------------------ */
/* chain rebuild on navigation                                         */
/* ------------------------------------------------------------------ */

static void
miller_clear_active (NemoMillerColumnsView *view)
{
	GList *l;

	if (view->details->active_clear_id != 0) {
		g_source_remove (view->details->active_clear_id);
		view->details->active_clear_id = 0;
	}
	if (view->details->active_pulse_id != 0) {
		g_source_remove (view->details->active_pulse_id);
		view->details->active_pulse_id = 0;
	}
	view->details->active_pulse_on = FALSE;

	for (l = view->details->columns; l != NULL; l = l->next) {
		MillerCol *col = l->data;

		if (col->scroll == NULL || !GTK_IS_WIDGET (col->scroll)) {
			continue;
		}
		gtk_style_context_remove_class (
			gtk_widget_get_style_context (col->scroll),
			"miller-active");
		gtk_style_context_remove_class (
			gtk_widget_get_style_context (col->scroll),
			"miller-ancestor");
	}
}

/* NO stored column pointer: navigating rebuilds the chain and frees
 * columns, so the pulse re-resolves the deepest chain column on
 * every tick instead of touching a possibly freed one */
static gboolean
miller_active_pulse (gpointer user_data)
{
	NemoMillerColumnsView *view = user_data;
	MillerCol *col;

	col = last_chain_column (view);
	if (col == NULL ||
	    col->scroll == NULL ||
	    !GTK_IS_WIDGET (col->scroll)) {
		view->details->active_pulse_id = 0;
		return FALSE;
	}

	view->details->active_pulse_on = !view->details->active_pulse_on;
	if (view->details->active_pulse_on) {
		gtk_style_context_add_class (
			gtk_widget_get_style_context (col->scroll),
			"miller-active");
	} else {
		gtk_style_context_remove_class (
			gtk_widget_get_style_context (col->scroll),
			"miller-active");
	}
	return TRUE;	/* keep pulsing until cleared */
}

static gboolean
miller_active_clear_idle (gpointer user_data)
{
	NemoMillerColumnsView *view = user_data;
	MillerCol *col;

	view->details->active_clear_id = 0;
	if (view->details->active_pulse_id != 0) {
		g_source_remove (view->details->active_pulse_id);
		view->details->active_pulse_id = 0;
	}
	col = last_chain_column (view);
	if (col != NULL &&
	    col->scroll != NULL &&
	    GTK_IS_WIDGET (col->scroll)) {
		/* settle on the highlighted frame, no more pulsing */
		gtk_style_context_add_class (
			gtk_widget_get_style_context (col->scroll),
			"miller-active");
	}
	return FALSE;
}

static void
miller_mark_active (NemoMillerColumnsView *view)
{
	GList *l;
	MillerCol *col;
	int active_idx = -1, i;

	if (view->details->active_clear_id != 0) {
		g_source_remove (view->details->active_clear_id);
		view->details->active_clear_id = 0;
	}
	active_idx = g_list_length (view->details->columns) - 1;
	for (l = view->details->columns, i = 0; l != NULL; l = l->next, i++) {
		MillerCol *other = l->data;

		if (other->scroll == NULL || !GTK_IS_WIDGET (other->scroll)) {
			continue;
		}
		gtk_style_context_remove_class (
			gtk_widget_get_style_context (other->scroll),
			"miller-active");
		gtk_style_context_remove_class (
			gtk_widget_get_style_context (other->scroll),
			"miller-ancestor");
		if (i < active_idx - 1 && other->is_chain) {
			gtk_style_context_add_class (
				gtk_widget_get_style_context (other->scroll),
				"miller-ancestor");
		}
	}

	col = last_chain_column (view);
	if (col == NULL ||
	    col->scroll == NULL ||
	    !GTK_IS_WIDGET (col->scroll)) {
		return;
	}
	view->details->active_pulse_on = TRUE;
	gtk_style_context_add_class (
		gtk_widget_get_style_context (col->scroll),
		"miller-active");
	view->details->active_pulse_id =
		g_timeout_add (350, miller_active_pulse, view);
	/* stop the subtle pulse after 3.5s: it is only a
	 * transition hint, not a permanent highlight */
	view->details->active_clear_id =
		g_timeout_add (3500, miller_active_clear_idle, view);
}

/* Path decoration only. Every parent column gets a tinted row if the
 * immediate next column is browsing that row's directory. No GTK selection
 * is created, so Delete/Trash cannot accidentally target an ancestor. */
static void
miller_refresh_opened_marks (NemoMillerColumnsView *view)
{
	GList *node;
	for (node = view->details->columns; node != NULL; node = node->next) {
		MillerCol *col = node->data;
		MillerCol *right = node->next ? node->next->data : NULL;
		GFile *opened = (right != NULL) ? right->dir : NULL;
		GtkTreeIter iter;
		gboolean valid = gtk_tree_model_get_iter_first (
			GTK_TREE_MODEL (col->store), &iter);
		while (valid) {
			GFile *row_file = NULL;
			gboolean prior_opened = FALSE;
			gboolean now_opened;
			gtk_tree_model_get (GTK_TREE_MODEL (col->store), &iter,
			                    COL_GFILE, &row_file,
			                    COL_OPENED, &prior_opened, -1);
			now_opened = (opened != NULL && row_file != NULL &&
			              g_file_equal (row_file, opened));
            if (now_opened || prior_opened) {
                GdkRGBA bg = {0, 0, 0, 0};
                gboolean active = right != NULL && right->is_chain &&
                                  node->next && (node->next->next == NULL ||
                                  !((MillerCol *)node->next->next->data)->is_chain);
                if (now_opened)
                    miller_navigation_color (col->tree, active, &bg);
                gtk_list_store_set (col->store, &iter,
                                    COL_OPENED, now_opened,
                                    COL_NAV_RGBA, now_opened ? &bg : NULL, -1);
            }
			g_clear_object (&row_file);
			valid = gtk_tree_model_iter_next (GTK_TREE_MODEL (col->store), &iter);
		}
	}
}

/* First column of the chain for @dir: $HOME when @dir is home or below,
 * otherwise the root of the enclosing mount (a removable volume, say), or
 * NULL to walk up to the filesystem root.
 *
 * A click on Places, Bookmarks, Favorites, Devices or the tree makes that
 * location the start instead, for as long as navigation stays inside it;
 * the ancestors are not shown, which is the point of a bookmark. The marker
 * lives on the slot so tabs and panes keep their own root, and leaving the
 * sidebar root clears it. */
static GFile *
miller_chain_start (NemoMillerColumnsView *view, GFile *dir)
{
	NemoWindowSlot *slot;
	GFile *sidebar_root;
	GFile *start = NULL;
	GFile *home;

	home = g_file_new_for_path (g_get_home_dir ());
	if (g_file_equal (dir, home) || g_file_has_prefix (dir, home)) {
		start = g_object_ref (home);
	}
	g_object_unref (home);

	if (start == NULL) {
		GMount *mount;

		mount = g_file_find_enclosing_mount (dir, NULL, NULL);
		if (mount != NULL) {
			GFile *mroot;
			GFile *slash;

			mroot = g_mount_get_root (mount);
			slash = g_file_new_for_path ("/");
			if (!g_file_equal (mroot, slash)) {
				start = g_object_ref (mroot);
			}
			g_object_unref (slash);
			g_object_unref (mroot);
			g_object_unref (mount);
		}
	}

	slot = nemo_view_get_nemo_window_slot (NEMO_VIEW (view));
	sidebar_root = slot ? g_object_get_data (G_OBJECT (slot), "miller-sidebar-root") : NULL;
	if (sidebar_root != NULL) {
		if (g_file_equal (dir, sidebar_root) ||
		    g_file_has_prefix (dir, sidebar_root)) {
			g_clear_object (&start);
			start = g_object_ref (sidebar_root);
		} else {
			g_object_set_data (G_OBJECT (slot), "miller-sidebar-root", NULL);
		}
	}
	return start;
}

/* List of owned GFile refs from @start (or the root) down to @dir.
 * Consumes @start. */
static GList *
miller_chain_new (GFile *dir, GFile *start)
{
	GList *chain = NULL;
	GFile *cur;

	cur = g_object_ref (dir);
	while (cur != NULL) {
		chain = g_list_prepend (chain, cur);
		if (start != NULL && g_file_equal (cur, start)) {
			break;
		}
		cur = g_file_get_parent (cur);
	}
	if (start != NULL) {
		g_object_unref (start);
	}
	return chain;
}

/* New chain column for @dir, appended to the view; takes the ref. */
static MillerCol *
miller_chain_column_append (NemoMillerColumnsView *view, GFile *dir)
{
	MillerCol *col;

	col = miller_col_new (view, dir, TRUE, NULL);
	g_object_unref (col->dir);
	col->dir = dir;
	col->directory = nemo_directory_get (dir);
	view->details->columns = g_list_append (view->details->columns, col);
	return col;
}

/* Fill a new chain column synchronously, so it never trickles in. The
 * deepest one is NemoView's own model; ancestors get a monitor too, or
 * deletes and renames in their listings never reach the view. */
static void
miller_chain_column_load (NemoMillerColumnsView *view, MillerCol *col, gboolean deepest)
{
	if (!deepest) {
		miller_monitor_dir (view, col);
	}
	col_load_dir (col, view);
}

/* Nothing in common with the old chain: replace every column. */
static void
miller_chain_rebuild_all (NemoMillerColumnsView *view, GList *chain)
{
	GList *to_destroy, *dl, *l;
	MillerCol *base_col = NULL;

	to_destroy = g_list_copy (view->details->columns);
	view->details->columns = NULL;
	for (dl = to_destroy; dl != NULL; dl = dl->next) {
		miller_col_destroy (view, dl->data, FALSE, TRUE);
	}
	g_list_free (to_destroy);

	for (l = chain; l != NULL; l = l->next) {
		MillerCol *col = miller_chain_column_append (view, l->data);

		miller_chain_column_load (view, col, l->next == NULL);
		if (l->next == NULL) {
			base_col = col;
		}
	}
	update_col_visibility (view);
	/* a full location switch fades the new content in too */
	if (base_col != NULL) {
		g_object_set_data (G_OBJECT (base_col->tree),
		                   "miller-pending-content-fade",
		                   GINT_TO_POINTER (1));
		miller_animate_new_column (base_col->scroll);
	}
}

/* The first @k columns already show the start of @chain: keep them (store
 * and scroll position stay, no flash) and replace only the tail, starting
 * at @nl. */
static void
miller_chain_replace_tail (NemoMillerColumnsView *view, GList *chain, int k, GList *nl)
{
	GList *node, *l, *cl;
	GtkWidget *tail = NULL;
	MillerCol *base_col = NULL;
	int i = 0;

	node = g_list_nth (view->details->columns, k);
	while (node != NULL) {
		GList *next = node->next;

		miller_col_destroy (view, node->data, TRUE, TRUE);
		node = next;
	}

	for (l = nl; l != NULL; l = l->next) {
		MillerCol *col = miller_chain_column_append (view, l->data);

		/* the full rebuild gets shown by relayout's show_all,
		 * here the new columns show themselves */
		gtk_widget_show_all (col->scroll);
		miller_chain_column_load (view, col, l->next == NULL);
		if (l->next == NULL) {
			base_col = col;
		}
		/* reveal the column now; the content fades in once the
		 * first directory batch has filled it */
		g_object_set_data (G_OBJECT (col->tree),
		                   "miller-pending-content-fade",
		                   GINT_TO_POINTER (1));
		miller_animate_new_column (col->scroll);
	}

	if (nl != NULL) {
		if (nl->next != NULL) {
			tail = build_paned_chain (view,
					g_list_nth (view->details->columns, k));
		} else {
			tail = base_col->scroll;
		}
	}
	rewire_tail (view, k, tail);
	miller_queue_divider_redraw (view);
	miller_slide_columns_to_end (view);

	/* the kept columns own their dirs already: drop our refs */
	for (cl = chain; cl != NULL && i < k; cl = cl->next) {
		g_object_unref (cl->data);
		i++;
	}
}

/* NemoView dropped every subdirectory registration for the old location
 * before BEGIN_LOADING. New ancestors register as they are built; reused
 * ones need it again here. The deepest chain column is NemoView's main
 * model and already monitored. */
static void
miller_monitor_chain_ancestors (NemoMillerColumnsView *view)
{
	MillerCol *deepest = last_chain_column (view);
	GList *ml;

	for (ml = view->details->columns; ml != NULL; ml = ml->next) {
		MillerCol *tracked = ml->data;
		if (tracked != deepest && tracked->directory != NULL) {
			miller_monitor_dir (view, tracked);
		}
	}
}

/* Show the columns from the chain start down to @dir, reusing the columns
 * the old chain has in common with the new one. */
static void
rebuild_chain (NemoMillerColumnsView *view, GFile *dir)
{
	GList *chain, *ol, *nl;
	int k = 0;

	/* stop the active-column pulse first, its tick would touch
	 * freed widgets */
	miller_clear_active (view);

	chain = miller_chain_new (dir, miller_chain_start (view, dir));

	ol = view->details->columns;
	nl = chain;
	while (ol != NULL && nl != NULL &&
	       ((MillerCol *) ol->data)->dir != NULL &&
	       g_file_equal (((MillerCol *) ol->data)->dir, nl->data)) {
		k++;
		ol = ol->next;
		nl = nl->next;
	}
	if (k == 0) {
		miller_chain_rebuild_all (view, chain);
	} else {
		miller_chain_replace_tail (view, chain, k, nl);
	}
	g_list_free (chain);

	miller_monitor_chain_ancestors (view);

	/* Recreate the visible open-path highlight without creating a selection.
	 * Ordinary Delete / Trash uses only real, verified GTK selections. */
	miller_refresh_opened_marks (view);

	miller_apply_cut_fade (view);

	/* resync the shell's selection view with the trees' actual
	 * state: the rebuild pruned columns whose selection is gone,
	 * and a stale empty shell-side selection makes every
	 * selection-dependent action insensitive (dead shortcuts) */
	rebuild_selection (view);
	nemo_view_notify_selection_changed (NEMO_VIEW (view));
}

static void
miller_begin_loading (NemoView *nemo_view)
{
	NemoMillerColumnsView *view;
	char *uri;
	GFile *dir;

	view = NEMO_MILLER_COLUMNS_VIEW (nemo_view);
    {
        GtkWidget *toplevel = gtk_widget_get_toplevel (GTK_WIDGET (view));
        if (GTK_IS_WINDOW (toplevel))
            gtk_style_context_add_class (gtk_widget_get_style_context (toplevel),
                                         "miller-files-window");
    }

	uri = nemo_view_get_uri (nemo_view);
	if (uri == NULL) {
		return;
	}
	{
	}
	dir = g_file_new_for_uri (uri);
	g_free (uri);
    /* The clicked/current directory is the selected item in its parent
     * column. Do not confuse this with automatic destructive selection. */
    g_object_set_data_full (G_OBJECT (view),
                            "miller-pending-navigation-selection",
                            g_object_ref (dir), g_object_unref);
	if (view->details->rename_opened_target != NULL &&
	    !g_file_equal (dir, view->details->rename_opened_target)) {
		g_clear_object (&view->details->rename_opened_target);
	}
	miller_set_paste_destination (view, dir);
	miller_load_sort_metadata (view, dir);
	miller_update_sort_menu_state (view);

	/* A navigation changes the meaning of selected rows. Do not carry
	 * over stale selections or select an ancestor as a breadcrumb. */
	view->details->programmatic = TRUE;
	{
		GList *node;
		for (node = view->details->columns; node != NULL; node = node->next) {
			MillerCol *old = node->data;
			gtk_tree_selection_unselect_all (
				gtk_tree_view_get_selection (GTK_TREE_VIEW (old->tree)));
		}
	}
	view->details->programmatic = FALSE;
	free_selection (view);
	view->details->in_load = TRUE;
	view->details->click_pending = FALSE;
	g_clear_object (&view->details->last_click_file);
	view->details->last_click_time = 0;
	miller_rename_timer_cancel (view);
	rebuild_chain (view, dir);
	g_object_unref (dir);
	view->details->in_load = FALSE;

    miller_apply_navigation_selection (view);

	/* keyboard focus: hand it to the deepest column so the class
	 * binding sets (Delete/F2), the arrows and Enter work without
	 * a click first, but never steal focus from elsewhere */
	{
		GtkWidget *toplevel;
		GtkWidget *focus;
		MillerCol *last;

		toplevel = gtk_widget_get_toplevel (GTK_WIDGET (view));
		last = last_chain_column (view);
		if (GTK_IS_WINDOW (toplevel) && last != NULL) {
			focus = gtk_window_get_focus (GTK_WINDOW (toplevel));
			if (focus == NULL ||
			    !gtk_widget_is_ancestor (focus, GTK_WIDGET (view))) {
				gtk_widget_grab_focus (last->tree);
			}
		}
	}
}

static void
miller_clear (NemoView *nemo_view)
{
	NemoMillerColumnsView *view = NEMO_MILLER_COLUMNS_VIEW (nemo_view);
	GList *l;

	/* A pending operation for the previous path must never resync this one. */
	miller_cancel_pending_heals (view);
	/* NemoView.load_directory() emits CLEAR, then removes EVERY item
	 * from its private subdirectory_list, before it emits BEGIN_LOADING.
	 * If we keep our own registration table here, deleting the old
	 * columns on BEGIN_LOADING calls nemo_view_remove_subdirectory()
	 * a second time and trips NemoView's assertion.
	 *
	 * Release our subdirectory monitors while NemoView still owns them.
	 * Do not clear stores/selection: rebuild_chain() preserves the
	 * matching prefix, including its tree state and widths. */
	for (l = view->details->columns; l != NULL; l = l->next) {
		MillerCol *col = l->data;
		miller_unmonitor_dir (view, col);
	}
	g_hash_table_remove_all (view->details->monitored_dirs);
}

/* heal the ancestor columns: the model swallows monitor events that
 * arrive while a load is in progress (e.g. deleting the viewed
 * directory), so their stores can drift from disk. Re-verify every
 * ancestor listing once the load settles. */
static void
miller_heal_columns (NemoMillerColumnsView *view)
{
	GList *l;

	/* the BASE column is healed too: its model can swallow the
	 * remove event of a trashed/deleted file (the ghost row) */
	for (l = view->details->columns; l != NULL; l = l->next) {
		MillerCol *col = l->data;

		if (col->dir == NULL) {
			continue;
		}
		col_resync_dir (col, view);
	}
}

/* Operation-based heals are bounded and belong to this view. Closing
 * the tab, navigating or repeating Delete cancels the previous timers.
 * The normal end_loading path heals too, if an operation overlaps loading. */
static void
miller_cancel_pending_heals (NemoMillerColumnsView *view)
{
    if (view->details->heal_early_id != 0) {
        g_source_remove (view->details->heal_early_id);
        view->details->heal_early_id = 0;
    }
    if (view->details->heal_late_id != 0) {
        g_source_remove (view->details->heal_late_id);
        view->details->heal_late_id = 0;
    }
}

static gboolean
miller_heal_timeout (gpointer data)
{
    NemoMillerColumnsView *view = data;
    /* Source IDs must be cleared before callbacks can schedule new ones. */
    if (view->details->heal_early_id == g_source_get_id (g_main_current_source ()))
        view->details->heal_early_id = 0;
    else if (view->details->heal_late_id == g_source_get_id (g_main_current_source ()))
        view->details->heal_late_id = 0;
    if (!gtk_widget_in_destruction (GTK_WIDGET (view)) &&
        !view->details->in_load)
        miller_heal_columns (view);
    return G_SOURCE_REMOVE;
}

static void
miller_schedule_heals (NemoMillerColumnsView *view)
{
    /* Coalesce repeated Delete keypresses: never more than two outstanding
     * async-resync callbacks. No synchronous filesystem scan runs here. */
    miller_cancel_pending_heals (view);
    view->details->heal_early_id = g_timeout_add_full (
        G_PRIORITY_DEFAULT, 400, miller_heal_timeout,
        g_object_ref (view), g_object_unref);
    view->details->heal_late_id = g_timeout_add_full (
        G_PRIORITY_DEFAULT, 1200, miller_heal_timeout,
        g_object_ref (view), g_object_unref);
}

static void
miller_resort_all (NemoMillerColumnsView *view)
{
	GList *l;

	for (l = view->details->columns; l != NULL; l = l->next) {
		MillerCol *col = l->data;

		/* unsorted-then-sorted forces a full re-sort even when
		 * the sort column id itself does not change */
		gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (col->store),
						      GTK_TREE_SORTABLE_UNSORTED_SORT_COLUMN_ID,
						      GTK_SORT_ASCENDING);
		gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (col->store),
						      COL_NAME, GTK_SORT_ASCENDING);
	}
}

/* Only Miller's own metadata is modified by the View/Sort menu.  In
 * particular, sorting a folder must not reset the global default sort
 * preference of Nemo's icon and list views. */
static const struct {
	NemoFileSortType type;
	const char *value;
} miller_sort_options[] = {
	{ NEMO_FILE_SORT_BY_DISPLAY_NAME, "name" },
	{ NEMO_FILE_SORT_BY_SIZE, "size" },
	{ NEMO_FILE_SORT_BY_TYPE, "type" },
	{ NEMO_FILE_SORT_BY_DETAILED_TYPE, "detailed_type" },
	{ NEMO_FILE_SORT_BY_MTIME, "modification date" },
	{ NEMO_FILE_SORT_BY_BTIME, "creation date" }
};

static const char *
miller_sort_to_string (NemoFileSortType type)
{
	guint i;
	for (i = 0; i < G_N_ELEMENTS (miller_sort_options); i++) {
		if (miller_sort_options[i].type == type)
			return miller_sort_options[i].value;
	}
	return "name";
}

static NemoFileSortType
miller_sort_from_string (const char *value, NemoFileSortType fallback)
{
	guint i;
	for (i = 0; i < G_N_ELEMENTS (miller_sort_options); i++) {
		if (g_strcmp0 (miller_sort_options[i].value, value) == 0)
			return miller_sort_options[i].type;
	}
	return fallback;
}

static void
miller_load_sort_metadata (NemoMillerColumnsView *view, GFile *directory)
{
	NemoFile *file;
	char *sort, *reversed;
	NemoFileSortType fallback;
	gboolean fallback_reversed;

	fallback = g_settings_get_enum (nemo_preferences,
		NEMO_PREFERENCES_DEFAULT_SORT_ORDER);
	fallback_reversed = g_settings_get_boolean (nemo_preferences,
		NEMO_PREFERENCES_DEFAULT_SORT_IN_REVERSE_ORDER);
	view->details->sort_order = fallback;
	view->details->sort_reversed = fallback_reversed;
	view->details->sort_is_custom = FALSE;
	if (directory != NULL &&
	    !nemo_global_preferences_get_ignore_view_metadata ()) {
		file = nemo_file_get (directory);
		sort = nemo_file_get_metadata (file,
			NEMO_METADATA_KEY_MILLER_VIEW_SORT_BY, NULL);
		reversed = nemo_file_get_metadata (file,
			NEMO_METADATA_KEY_MILLER_VIEW_SORT_REVERSED, NULL);
		if (sort != NULL) {
			view->details->sort_order = miller_sort_from_string (sort, fallback);
		}
		if (reversed != NULL) {
			view->details->sort_reversed = g_strcmp0 (reversed, "true") == 0;
		}
		view->details->sort_is_custom = sort != NULL || reversed != NULL;
		g_free (sort);
		g_free (reversed);
		nemo_file_unref (file);
	}
	miller_resort_all (view);
}

static void
miller_save_sort_metadata (NemoMillerColumnsView *view)
{
	NemoFile *file;
	char *uri;

	if (!nemo_global_preferences_get_ignore_view_metadata ()) {
		uri = nemo_view_get_uri (NEMO_VIEW (view));
		if (uri != NULL) {
			file = nemo_file_get_by_uri (uri);
			nemo_file_set_metadata (file, NEMO_METADATA_KEY_MILLER_VIEW_SORT_BY,
				NULL, miller_sort_to_string (view->details->sort_order));
			nemo_file_set_metadata (file, NEMO_METADATA_KEY_MILLER_VIEW_SORT_REVERSED,
				NULL, view->details->sort_reversed ? "true" : "false");
			nemo_file_unref (file);
			g_free (uri);
		}
		view->details->sort_is_custom = TRUE;
	}
	miller_resort_all (view);
}

static void miller_update_sort_menu_state (NemoMillerColumnsView *view);

/* Toolbar sorting operates on the active pane's actual Miller view.
 * Persist through the same per-directory metadata as View > Sort Items. */
NemoFileSortType
nemo_miller_columns_view_get_sort_order (NemoMillerColumnsView *view)
{
	g_return_val_if_fail (NEMO_IS_MILLER_COLUMNS_VIEW (view), NEMO_FILE_SORT_BY_DISPLAY_NAME);
	return view->details->sort_order;
}

gboolean
nemo_miller_columns_view_get_sort_reversed (NemoMillerColumnsView *view)
{
	g_return_val_if_fail (NEMO_IS_MILLER_COLUMNS_VIEW (view), FALSE);
	return view->details->sort_reversed;
}

void
nemo_miller_columns_view_set_sort_order (NemoMillerColumnsView *view, NemoFileSortType order)
{
	g_return_if_fail (NEMO_IS_MILLER_COLUMNS_VIEW (view));
	if (view->details->sort_order == order) return;
	view->details->sort_order = order;
	miller_save_sort_metadata (view);
	miller_update_sort_menu_state (view);
}

void
nemo_miller_columns_view_set_sort_reversed (NemoMillerColumnsView *view, gboolean reversed)
{
	g_return_if_fail (NEMO_IS_MILLER_COLUMNS_VIEW (view));
	if (view->details->sort_reversed == reversed) return;
	view->details->sort_reversed = reversed;
	miller_save_sort_metadata (view);
	miller_update_sort_menu_state (view);
}

static void
miller_sort_radio_changed (GtkRadioAction *action, GtkRadioAction *current,
			   gpointer user_data)
{
	NemoMillerColumnsView *view = user_data;
	if (view->details->updating_sort_menu) return;
	view->details->sort_order = gtk_radio_action_get_current_value (current);
	miller_save_sort_metadata (view);
}

static void
miller_sort_reverse_toggled (GtkToggleAction *action, gpointer user_data)
{
	NemoMillerColumnsView *view = user_data;
	if (view->details->updating_sort_menu) return;
	view->details->sort_reversed = gtk_toggle_action_get_active (action);
	miller_save_sort_metadata (view);
}

static const GtkRadioActionEntry miller_sort_radio_actions[] = {
	{ "MillerSortName", NULL, N_("By _Name"), NULL,
	  N_("Sort by name"), NEMO_FILE_SORT_BY_DISPLAY_NAME },
	{ "MillerSortSize", NULL, N_("By _Size"), NULL,
	  N_("Sort by size"), NEMO_FILE_SORT_BY_SIZE },
	{ "MillerSortType", NULL, N_("By _Type"), NULL,
	  N_("Sort by file type"), NEMO_FILE_SORT_BY_TYPE },
	{ "MillerSortDetailedType", NULL, N_("By _Detailed Type"), NULL,
	  N_("Sort by detailed file type"), NEMO_FILE_SORT_BY_DETAILED_TYPE },
	{ "MillerSortDate", NULL, N_("By Modification _Date"), NULL,
	  N_("Sort by modification date"), NEMO_FILE_SORT_BY_MTIME },
	{ "MillerSortCreated", NULL, N_("By _Creation Date"), NULL,
	  N_("Sort by creation date"), NEMO_FILE_SORT_BY_BTIME }
};
static const GtkActionEntry miller_sort_action_entries[] = {
	{ "MillerArrange", NULL, N_("Sort _Items"), NULL,
	  N_("Change sorting in Miller Columns"), NULL }
};
static const GtkToggleActionEntry miller_sort_toggle_actions[] = {
	{ "MillerSortReverse", NULL, N_("Re_versed Order"), NULL,
	  N_("Reverse the sort order"), G_CALLBACK (miller_sort_reverse_toggled), FALSE }
};

static void
miller_update_sort_menu_state (NemoMillerColumnsView *view)
{
	GtkAction *name, *reverse;
	if (view->details->sort_action_group == NULL) return;
	view->details->updating_sort_menu = TRUE;
	name = gtk_action_group_get_action (view->details->sort_action_group,
		"MillerSortName");
	reverse = gtk_action_group_get_action (view->details->sort_action_group,
		"MillerSortReverse");
	if (name != NULL) {
		gtk_radio_action_set_current_value (GTK_RADIO_ACTION (name),
			view->details->sort_order);
	}
	if (reverse != NULL) {
		gtk_toggle_action_set_active (GTK_TOGGLE_ACTION (reverse),
			view->details->sort_reversed);
	}
	view->details->updating_sort_menu = FALSE;
}

static void
miller_merge_menus (NemoView *nemo_view)
{
	NemoMillerColumnsView *view = NEMO_MILLER_COLUMNS_VIEW (nemo_view);
	GtkUIManager *ui;
	GtkActionGroup *group;
	NEMO_VIEW_CLASS (nemo_miller_columns_view_parent_class)->merge_menus (nemo_view);
	ui = nemo_view_get_ui_manager (nemo_view);
	if (ui == NULL) return;
	group = gtk_action_group_new ("MillerColumnsSortActions");
	gtk_action_group_set_translation_domain (group, GETTEXT_PACKAGE);
	gtk_action_group_add_actions (group, miller_sort_action_entries,
		G_N_ELEMENTS (miller_sort_action_entries), view);
	gtk_action_group_add_toggle_actions (group, miller_sort_toggle_actions,
		G_N_ELEMENTS (miller_sort_toggle_actions), view);
	gtk_action_group_add_radio_actions (group, miller_sort_radio_actions,
		G_N_ELEMENTS (miller_sort_radio_actions), -1,
		G_CALLBACK (miller_sort_radio_changed), view);
	view->details->sort_action_group = group;
	gtk_ui_manager_insert_action_group (ui, group, 0);
	g_object_unref (group);
	view->details->sort_merge_id = gtk_ui_manager_add_ui_from_resource (ui,
		"/org/dakkar/nemo-miller-view-ui.xml", NULL);
	miller_update_sort_menu_state (view);
}

static void
miller_unmerge_menus (NemoView *nemo_view)
{
	NemoMillerColumnsView *view = NEMO_MILLER_COLUMNS_VIEW (nemo_view);
	GtkUIManager *ui = nemo_view_get_ui_manager (nemo_view);
	if (ui != NULL) {
		nemo_ui_unmerge_ui (ui, &view->details->sort_merge_id,
				 &view->details->sort_action_group);
	}
	NEMO_VIEW_CLASS (nemo_miller_columns_view_parent_class)->unmerge_menus (nemo_view);
}

static void
miller_update_menus (NemoView *nemo_view)
{
    NEMO_VIEW_CLASS (nemo_miller_columns_view_parent_class)->update_menus (nemo_view);
    miller_update_sort_menu_state (NEMO_MILLER_COLUMNS_VIEW (nemo_view));
}

static void
miller_sort_pref_changed (GSettings *settings, const char *key,
			  gpointer user_data)
{
	NemoMillerColumnsView *view = NEMO_MILLER_COLUMNS_VIEW (user_data);
	if (!view->details->sort_is_custom) {
		view->details->sort_order = g_settings_get_enum (nemo_preferences,
			NEMO_PREFERENCES_DEFAULT_SORT_ORDER);
		view->details->sort_reversed = g_settings_get_boolean (nemo_preferences,
			NEMO_PREFERENCES_DEFAULT_SORT_IN_REVERSE_ORDER);
		miller_resort_all (view);
		miller_update_sort_menu_state (view);
	}
}

static void
miller_end_loading (NemoView *nemo_view, gboolean all_files_seen)
{
	NemoMillerColumnsView *view;
	view = NEMO_MILLER_COLUMNS_VIEW (nemo_view);
	miller_heal_columns (view);
	miller_apply_cut_fade (view);

	/* The asynchronous parent listing will trigger this again when its
	 * row actually exists. Never require the old (unsafe) root selection. */
	miller_queue_pending_rename (view);
}

/* enumerate the directory and diff the store against it: rows for
 * entries that vanished are removed, new entries are appended */
static void
col_resync_dir (MillerCol *col, NemoMillerColumnsView *view)
{
	miller_start_scan (col, view, TRUE);
}

static MillerCol *
find_column_for_directory (NemoMillerColumnsView *view, NemoDirectory *directory)
{
	GList *l;
	MillerCol *col;

	if (directory == nemo_view_get_model (NEMO_VIEW (view))) {
		return last_chain_column (view);
	}

	for (l = view->details->columns; l != NULL; l = l->next) {
		col = l->data;
		if (col->directory == directory) {
			return col;
		}
	}
	return NULL;
}

/* find the row for a gfile in a column (dedup / refresh lookups) */
static gboolean
column_find_gfile (MillerCol *col, GFile *gfile, GtkTreeIter *out)
{
	GtkTreeIter iter;
	gboolean valid;

	if (gfile == NULL ||
	    !gtk_tree_model_get_iter_first (GTK_TREE_MODEL (col->store),
					    &iter)) {
		return FALSE;
	}
	valid = TRUE;
	while (valid) {
		GFile *row = NULL;
		gboolean match;

		gtk_tree_model_get (GTK_TREE_MODEL (col->store), &iter,
				    COL_GFILE, &row, -1);
		match = (row != NULL && g_file_equal (row, gfile));
		if (row != NULL) {
			g_object_unref (row);
		}
		if (match) {
			*out = iter;
			return TRUE;
		}
		valid = gtk_tree_model_iter_next (GTK_TREE_MODEL (col->store),
						  &iter);
	}
	return FALSE;
}

static void
miller_add_file (NemoView *nemo_view, NemoFile *file, NemoDirectory *directory)
{
	NemoMillerColumnsView *view;
	MillerCol *col;

	view = NEMO_MILLER_COLUMNS_VIEW (nemo_view);

	if (!nemo_view_should_show_file (nemo_view, file)) {
		return;
	}

	col = find_column_for_directory (view, directory);
	if (col == NULL) {
		return;
	}
	{
		GFile *gfile;
		GtkTreeIter iter;

		gfile = nemo_file_get_location (file);
		/* the column was pre-filled synchronously (col_load_dir):
		 * skip the model's duplicate initial batch, just refresh
		 * the existing row (its icon may have changed) */
		if (column_find_gfile (col, gfile, &iter)) {
			GdkPixbuf *pixbuf, *emblem;

			pixbuf = miller_icon_for_file (view, file, col->icon_size);
			emblem = miller_first_emblem_for_file (file, col->icon_size);
			gtk_list_store_set (col->store, &iter,
			                    COL_ICON, pixbuf,
			                    COL_EMBLEM, emblem, -1);
			g_clear_object (&pixbuf);
			g_clear_object (&emblem);
			g_object_unref (gfile);
			return;
		}
		g_object_unref (gfile);
	}
	make_row (view, col->store, file, col->icon_size, -1);
	miller_queue_pending_rename (view);
}

/* Returns TRUE when the removed row was selected. */
static gboolean
remove_file_from_column (NemoMillerColumnsView *view, MillerCol *col, NemoFile *file)
{
	GtkTreeIter iter;
	GFile *gfile;
	gboolean was_selected = FALSE;

	gfile = nemo_file_get_location (file);
	if (gtk_tree_model_get_iter_first (GTK_TREE_MODEL (col->store), &iter)) {
		do {
			GFile *row;
			NemoFile *row_file = NULL;
			gboolean match;

			gtk_tree_model_get (GTK_TREE_MODEL (col->store), &iter,
			                    COL_GFILE, &row, COL_FILE, &row_file, -1);
			/* A moved file keeps its NemoFile but already has its NEW
			 * location: match the identity too, or the row stays behind. */
			match = row_file == file || (row != NULL && g_file_equal (row, gfile));
			if (row != NULL) {
				g_object_unref (row);
			}
			if (row_file != NULL) nemo_file_unref (row_file);
			if (match) {
				GtkTreeSelection *sel =
					gtk_tree_view_get_selection (GTK_TREE_VIEW (col->tree));
				gboolean saved = view->details->programmatic;
				gint expected;

				was_selected = gtk_tree_selection_iter_is_selected (sel, &iter);
				expected = gtk_tree_selection_count_selected_rows (sel) -
				           (was_selected ? 1 : 0);
				/* Removing the cursor row (a selected row, or the opened
				 * folder whose column had focus) makes GTK clear the
				 * selection and select the next row.  Miller does not adopt
				 * that implicit selection: it would neither open that folder
				 * nor be what the user chose, and a second Delete would hit
				 * it.  After deleting/moving away the row, the containing
				 * folder is the context again, exactly as after navigating
				 * into it (Finder-style).  The keyboard cursor stays on the
				 * next row so the arrow keys continue from there. */
				view->details->programmatic = TRUE;
				gtk_list_store_remove (col->store, &iter);
				if (gtk_tree_selection_count_selected_rows (sel) != expected) {
					gtk_tree_selection_unselect_all (sel);
					was_selected = TRUE;	/* selection changed: resync */
				}
				view->details->programmatic = saved;
				if (view->details->rename_opened_target != NULL && gfile != NULL &&
				    g_file_equal (view->details->rename_opened_target, gfile)) {
					g_clear_object (&view->details->rename_opened_target);
				}
				gtk_widget_queue_draw (col->tree);
				break;
			}
		} while (gtk_tree_model_iter_next (GTK_TREE_MODEL (col->store), &iter));
	}
	g_object_unref (gfile);
	return was_selected;
}

static void
miller_remove_file (NemoView *nemo_view, NemoFile *file, NemoDirectory *directory)
{
	NemoMillerColumnsView *view;
	MillerCol *col;
	gboolean was_selected;

	view = NEMO_MILLER_COLUMNS_VIEW (nemo_view);
	col = find_column_for_directory (view, directory);
	if (col == NULL) {
		return;
	}

    GHashTableIter group_iter;
    gpointer group_path, group_file;
    g_hash_table_iter_init (&group_iter, col->group_files);
    while (g_hash_table_iter_next (&group_iter, &group_path, &group_file)) {
        if (group_file == file) g_hash_table_iter_remove (&group_iter);
    }
	was_selected = remove_file_from_column (view, col, file);
    miller_group_schedule_sync (view, col);

	/* A moved file is the same NemoFile in its new column and may be
	 * selected there (paste selects it): recompute from the trees instead
	 * of dropping it from the selection. */
	if (was_selected ||
	    (view->details->selection != NULL &&
	     g_list_find (view->details->selection, file) != NULL)) {
		rebuild_selection (view);
		nemo_view_notify_selection_changed (nemo_view);
	}
}

static void
miller_file_changed (NemoView *nemo_view, NemoFile *file, NemoDirectory *directory)
{
	NemoMillerColumnsView *view;
	MillerCol *col;
	GtkTreeIter iter;
	GFile *gfile;
	char *name;

	view = NEMO_MILLER_COLUMNS_VIEW (nemo_view);
	col = find_column_for_directory (view, directory);
	if (col == NULL) {
		return;
	}

	gfile = nemo_file_get_location (file);
	/* A file moved to another folder (Cut/Paste, DnD move) or deleted keeps
	 * its NemoFile identity but no longer belongs to this column: drop the
	 * row instead of leaving a stale entry. */
	if (gfile == NULL || nemo_file_is_gone (file) ||
	    (col->dir != NULL && !g_file_has_parent (gfile, col->dir))) {
		g_clear_object (&gfile);
		miller_remove_file (nemo_view, file, directory);
		return;
	}
	if (gtk_tree_model_get_iter_first (GTK_TREE_MODEL (col->store), &iter)) {
		do {
			GFile *row;
			NemoFile *row_file = NULL;
			gboolean match;
			gboolean location_changed;

			gtk_tree_model_get (GTK_TREE_MODEL (col->store), &iter,
			                    COL_GFILE, &row, COL_FILE, &row_file, -1);
			/* NemoFile keeps its identity when a rename changes its URI.
			 * A GFile-only lookup misses the old row and leaves ghost names. */
			match = (row != NULL && g_file_equal (row, gfile)) ||
			        row_file == file;
			location_changed = (row == NULL || !g_file_equal (row, gfile));
			g_clear_object (&row);
			if (row_file != NULL) nemo_file_unref (row_file);
			if (match) {
				GdkPixbuf *pixbuf, *emblem;
				char *old_name;
				gboolean old_is_dir, is_dir, changed;
				gint64 size, mtime, old_size, old_mtime;
				char *mime, *old_mime;

				gtk_tree_model_get (GTK_TREE_MODEL (col->store),
						    &iter,
						    COL_NAME, &old_name,
						    COL_IS_DIR, &old_is_dir,
						    COL_SIZE, &old_size,
						    COL_MTIME, &old_mtime,
						    COL_MIME, &old_mime, -1);
				name = nemo_file_get_display_name (file);
				size = (gint64) nemo_file_get_size (file);
				mtime = (gint64) nemo_file_get_mtime (file);
				mime = nemo_file_get_mime_type (file);
				pixbuf = miller_icon_for_file (view, file,
							       col->icon_size);
				emblem = miller_first_emblem_for_file (file, col->icon_size);
				is_dir = nemo_file_is_directory (file);
				/* sort-column sets repaint the whole column:
				 * only touch COL_NAME/COL_IS_DIR on real change */
				changed = (g_strcmp0 (old_name, name) != 0 ||
					   old_is_dir != is_dir || old_size != size ||
					   old_mtime != mtime || g_strcmp0 (old_mime, mime) != 0);
				if (changed || location_changed) {
					gtk_list_store_set (col->store, &iter,
							    COL_ICON, pixbuf,
							    COL_EMBLEM, emblem,
							    COL_NAME, name,
							    COL_FILE, file,
							    COL_GFILE, gfile,
							    COL_IS_DIR, is_dir,
							    COL_SIZE, size,
							    COL_MTIME, mtime,
							    COL_MIME, mime,
							    -1);
				} else {
					gtk_list_store_set (col->store, &iter,
							    COL_ICON, pixbuf,
							    COL_EMBLEM, emblem,
							    COL_FILE, file,
							    -1);
				}
				g_free (old_name);
				g_free (old_mime);
				g_free (mime);
				g_clear_object (&pixbuf);
				g_clear_object (&emblem);
				g_free (name);
				break;
			}
		} while (gtk_tree_model_iter_next (GTK_TREE_MODEL (col->store), &iter));
	}
    miller_group_reindex_changed (view, col, file);
	g_object_unref (gfile);
}

/* ------------------------------------------------------------------ */
/* selection vfuncs                                                    */
/* ------------------------------------------------------------------ */

static GList *
miller_get_selection (NemoView *nemo_view)
{
	NemoMillerColumnsView *view;
	GList *copy;

	view = NEMO_MILLER_COLUMNS_VIEW (nemo_view);
	copy = g_list_copy (view->details->selection);
	g_list_foreach (copy, (GFunc) nemo_file_ref, NULL);
	return copy;
}

static GList *
miller_peek_selection (NemoView *nemo_view)
{
	return NEMO_MILLER_COLUMNS_VIEW (nemo_view)->details->selection;
}

/* For native file operations (Trash/Delete, copy and other supported
 * actions), return only real file rows explicitly selected in the active
 * Miller column. A breadcrumb, group heading, stale cross-column selection
 * or an already-open ancestor must never become a deletion target. */
static GList *
miller_get_verified_transfer_selection (NemoView *nemo_view)
{
	NemoMillerColumnsView *view = NEMO_MILLER_COLUMNS_VIEW (nemo_view);
	MillerCol *col = view->details->action_col;
	GtkTreeSelection *sel;
	GList *rows, *r, *result = NULL, *node;

	if (col == NULL ||
	    g_list_find (view->details->columns, col) == NULL ||
	    !GTK_IS_TREE_VIEW (col->tree)) {
		return NULL;
	}
	sel = gtk_tree_view_get_selection (GTK_TREE_VIEW (col->tree));
	rows = gtk_tree_selection_get_selected_rows (sel, NULL);
	if (rows == NULL) {
		/* Clicking a folder in Miller navigates into it: the folder stays
		 * highlighted (and is reported as "selected" in the status bar),
		 * but keyboard focus moves to its (empty-selection) column. If the
		 * user explicitly clicked exactly this folder and nothing since,
		 * Delete/Trash/Copy act on that folder, as in List/Icon view.
		 * rename_opened_target is cleared by any other click or by
		 * navigating elsewhere (miller_begin_loading). */
		MillerCol *base = last_chain_column (view);
		GFile *target = view->details->rename_opened_target;
		/* The action column is the opened folder's own column, or the
		 * column the user clicked it in (its parent's column, which is
		 * where focus stays after clicking an earlier-column folder). */
		if (base != NULL && base->dir != NULL &&
		    (col == base ||
		     (col->dir != NULL && target != NULL &&
		      g_file_has_parent (target, col->dir))) &&
		    target != NULL && g_file_equal (base->dir, target) &&
		    g_file_has_parent (target, NULL)) {
			NemoFile *opened = nemo_file_get (target);
			return opened != NULL ? g_list_prepend (NULL, opened) : NULL;
		}
		return NULL;
	}

	for (r = rows; r != NULL; r = r->next) {
		GtkTreeIter iter;
		NemoFile *file = NULL;
		GFile *path = NULL, *actual = NULL;
		gboolean safe = FALSE;

		if (gtk_tree_model_get_iter (GTK_TREE_MODEL (col->store), &iter, r->data)) {
			gtk_tree_model_get (GTK_TREE_MODEL (col->store), &iter,
			                    COL_FILE, &file, COL_GFILE, &path, -1);
		}
		/* Real row (headers have no NemoFile) whose identity still
		 * matches the model. A folder that is merely *opened* in the next
		 * column but also explicitly GTK-selected by the user is a valid
		 * target, exactly as in List/Icon view (click folder + Delete). */
		if (file != NULL && path != NULL &&
		    (col->dir == NULL || g_file_has_parent (path, col->dir) ||
		     !g_file_is_native (col->dir))) {
			actual = nemo_file_get_location (file);
			safe = actual != NULL && g_file_equal (actual, path);
		}
		(void) node;
		g_clear_object (&actual);
		g_clear_object (&path);
		if (!safe) {
			if (file != NULL) nemo_file_unref (file);
			g_list_free_full (rows, (GDestroyNotify) gtk_tree_path_free);
			nemo_file_list_free (result);
			g_warning ("Miller: refusing ambiguous selection for file operation");
			return NULL;
		}
		/* Never copy a stale or duplicate selection from the model. */
		if (g_list_find (result, file) != NULL) {
			nemo_file_unref (file);
			g_list_free_full (rows, (GDestroyNotify) gtk_tree_path_free);
			nemo_file_list_free (result);
			g_warning ("Miller: repeated file in transfer selection");
			return NULL;
		}
		result = g_list_prepend (result, file);
	}
	g_list_free_full (rows, (GDestroyNotify) gtk_tree_path_free);
	return g_list_reverse (result);
}

static gint
miller_get_selection_count (NemoView *nemo_view)
{
	return g_list_length (NEMO_MILLER_COLUMNS_VIEW (nemo_view)->details->selection);
}

static void
miller_set_selection (NemoView *nemo_view, GList *selection)
{
	NemoMillerColumnsView *view;
	GList *l, *m;

	view = NEMO_MILLER_COLUMNS_VIEW (nemo_view);

	{
	}

	view->details->programmatic = TRUE;

	for (m = view->details->columns; m != NULL; m = m->next) {
		MillerCol *col = m->data;
		gtk_tree_selection_unselect_all (gtk_tree_view_get_selection (GTK_TREE_VIEW (col->tree)));
	}

	for (l = selection; l != NULL; l = l->next) {
		NemoFile *file = l->data;
		GFile *gfile;

		gfile = nemo_file_get_location (file);
		for (m = view->details->columns; m != NULL; m = m->next) {
			MillerCol *col = m->data;
			if (select_gfile_in_column (col, gfile)) {
				break;
			}
		}
		g_object_unref (gfile);
	}

	view->details->programmatic = FALSE;
	rebuild_selection (view);
	nemo_view_notify_selection_changed (nemo_view);
}

/* File operations and background actions target the column being used.
 * The actual window/navigation URI remains managed by the window slot. */
static char *
miller_get_backing_uri (NemoView *nemo_view)
{
	NemoMillerColumnsView *view;
	MillerCol *col;

	view = NEMO_MILLER_COLUMNS_VIEW (nemo_view);
	if (view->details->columns == NULL) {
		return NULL;
	}
	col = view->details->action_col;
	if (col == NULL || col->dir == NULL) {
		col = last_chain_column (view);
	}
	if (col != NULL && col->dir != NULL) {
		return g_file_get_uri (col->dir);
	}
	return NULL;
}

static void
miller_select_all (NemoView *nemo_view)
{
	NemoMillerColumnsView *view;
	MillerCol *col;

	view = NEMO_MILLER_COLUMNS_VIEW (nemo_view);
	col = view->details->action_col;
	if (col == NULL) col = current_store_column (view);
	if (col != NULL) {
		gtk_tree_selection_select_all (gtk_tree_view_get_selection (GTK_TREE_VIEW (col->tree)));
	}
}

static void
miller_invert_selection (NemoView *nemo_view)
{
	NemoMillerColumnsView *view;
	MillerCol *col;
	GtkTreeIter iter;

	view = NEMO_MILLER_COLUMNS_VIEW (nemo_view);
	col = view->details->action_col;
	if (col == NULL) col = current_store_column (view);
	if (col == NULL ||
	    !gtk_tree_model_get_iter_first (GTK_TREE_MODEL (col->store), &iter)) {
		return;
	}

	do {
		GtkTreeSelection *sel;
		gboolean selected;

		sel = gtk_tree_view_get_selection (GTK_TREE_VIEW (col->tree));
		selected = gtk_tree_selection_iter_is_selected (sel, &iter);
		if (selected) {
			gtk_tree_selection_unselect_iter (sel, &iter);
		} else {
			gtk_tree_selection_select_iter (sel, &iter);
		}
	} while (gtk_tree_model_iter_next (GTK_TREE_MODEL (col->store), &iter));
}

/* ------------------------------------------------------------------ */
/* misc vfuncs                                                         */
/* ------------------------------------------------------------------ */

static guint
miller_get_item_count (NemoView *nemo_view)
{
	NemoMillerColumnsView *view;
	MillerCol *col;

	view = NEMO_MILLER_COLUMNS_VIEW (nemo_view);
	col = current_store_column (view);
	if (col == NULL) {
		return 0;
	}
	return gtk_tree_model_iter_n_children (GTK_TREE_MODEL (col->store), NULL);
}

static gboolean
miller_is_empty (NemoView *nemo_view)
{
	return miller_get_item_count (nemo_view) == 0;
}

static int
miller_compare_files (NemoView *nemo_view, NemoFile *a, NemoFile *b)
{
	char *na, *nb;
	int ret;

	na = nemo_file_get_display_name (a);
	nb = nemo_file_get_display_name (b);
	ret = g_utf8_collate (na, nb);
	g_free (na);
	g_free (nb);
	return ret;
}

static void
miller_reset_to_defaults (NemoView *nemo_view)
{
	NemoMillerColumnsView *view = NEMO_MILLER_COLUMNS_VIEW (nemo_view);
	char *uri = nemo_view_get_uri (nemo_view);
	if (uri != NULL && !nemo_global_preferences_get_ignore_view_metadata ()) {
		NemoFile *folder = nemo_file_get_by_uri (uri);
		nemo_file_set_metadata (folder, NEMO_METADATA_KEY_MILLER_VIEW_SORT_BY,
			NULL, NULL);
		nemo_file_set_metadata (folder, NEMO_METADATA_KEY_MILLER_VIEW_SORT_REVERSED,
			NULL, NULL);
		nemo_file_unref (folder);
	}
	g_free (uri);
	view->details->sort_order = g_settings_get_enum (nemo_preferences,
		NEMO_PREFERENCES_DEFAULT_SORT_ORDER);
	view->details->sort_reversed = g_settings_get_boolean (nemo_preferences,
		NEMO_PREFERENCES_DEFAULT_SORT_IN_REVERSE_ORDER);
	view->details->sort_is_custom = FALSE;
	miller_resort_all (view);
	miller_update_sort_menu_state (view);
	miller_restore_default_zoom_level (nemo_view);
}

static void
miller_bump_zoom_level (NemoView *nemo_view, int zoom_increment)
{
	NemoMillerColumnsView *view = NEMO_MILLER_COLUMNS_VIEW (nemo_view);
	int level = view->details->zoom_level + zoom_increment;

	if (level < NEMO_ZOOM_LEVEL_SMALLEST) {
		level = NEMO_ZOOM_LEVEL_SMALLEST;
	}
	if (level > NEMO_ZOOM_LEVEL_LARGEST) {
		level = NEMO_ZOOM_LEVEL_LARGEST;
	}
	if (level != view->details->zoom_level) {
		view->details->zoom_level = level;
		apply_zoom (view);
	}
}

static void
miller_zoom_to_level (NemoView *nemo_view, NemoZoomLevel level)
{
	NemoMillerColumnsView *view = NEMO_MILLER_COLUMNS_VIEW (nemo_view);

	if (level < NEMO_ZOOM_LEVEL_SMALLEST) {
		level = NEMO_ZOOM_LEVEL_SMALLEST;
	}
	if (level > NEMO_ZOOM_LEVEL_LARGEST) {
		level = NEMO_ZOOM_LEVEL_LARGEST;
	}
	if (level != view->details->zoom_level) {
		view->details->zoom_level = level;
		apply_zoom (view);
	}
}

static NemoZoomLevel
miller_get_zoom_level (NemoView *nemo_view)
{
	return NEMO_MILLER_COLUMNS_VIEW (nemo_view)->details->zoom_level;
}

static void
miller_restore_default_zoom_level (NemoView *nemo_view)
{
	NemoMillerColumnsView *view = NEMO_MILLER_COLUMNS_VIEW (nemo_view);

	if (view->details->zoom_level != NEMO_ZOOM_LEVEL_STANDARD) {
		view->details->zoom_level = NEMO_ZOOM_LEVEL_STANDARD;
		apply_zoom (view);
	}
}

static gboolean
miller_can_zoom_in (NemoView *nemo_view)
{
	return NEMO_MILLER_COLUMNS_VIEW (nemo_view)->details->zoom_level <
		NEMO_ZOOM_LEVEL_LARGEST;
}

static gboolean
miller_can_zoom_out (NemoView *nemo_view)
{
	return NEMO_MILLER_COLUMNS_VIEW (nemo_view)->details->zoom_level >
		NEMO_ZOOM_LEVEL_SMALLEST;
}

static void
miller_reveal_selection (NemoView *nemo_view)
{
	NemoMillerColumnsView *view;
	MillerCol *col;
	GtkTreeSelection *sel;
	GList *rows;

	view = NEMO_MILLER_COLUMNS_VIEW (nemo_view);
	col = current_store_column (view);
	if (col == NULL) {
		return;
	}
	sel = gtk_tree_view_get_selection (GTK_TREE_VIEW (col->tree));
	rows = gtk_tree_selection_get_selected_rows (sel, NULL);
	if (rows != NULL) {
		GdkRectangle cell, visible;
		gboolean do_scroll;

		gtk_tree_view_get_cell_area (GTK_TREE_VIEW (col->tree),
					     rows->data, NULL, &cell);
		gtk_tree_view_get_visible_rect (GTK_TREE_VIEW (col->tree),
						&visible);
		/* scroll only when the row is genuinely off-screen AND
		 * the tree is mapped with real geometry: a scroll on an
		 * unmapped/mid-reparent view snaps the row to the top */
		do_scroll = gtk_widget_get_mapped (col->tree) &&
			    visible.height > 0 &&
			    cell.height > 0 &&
			    (cell.y < visible.y ||
			     cell.y + cell.height > visible.y + visible.height);
		if (do_scroll) {
			gtk_tree_view_scroll_to_cell (GTK_TREE_VIEW (col->tree),
						      rows->data, NULL,
						      FALSE, 0.0, 0.0);
		}
		g_list_free_full (rows, (GDestroyNotify) gtk_tree_path_free);
	}
}

static void
miller_widget_to_file_operation_position (NemoView *nemo_view, GdkPoint *position)
{
}

static void
miller_click_policy_changed (NemoView *nemo_view)
{
	/* Click policy is read at activation time so changes apply immediately. */
}

static void
miller_sort_directories_first_changed (NemoView *nemo_view)
{
	resort_all (NEMO_MILLER_COLUMNS_VIEW (nemo_view));
}

static void
miller_sort_favorites_first_changed (NemoView *nemo_view)
{
	resort_all (NEMO_MILLER_COLUMNS_VIEW (nemo_view));
}

static void
miller_click_to_rename_mode_changed (NemoView *nemo_view)
{
	/* Same global preference as the other views. */
	NEMO_MILLER_COLUMNS_VIEW (nemo_view)->details->click_to_rename =
		g_settings_get_boolean (nemo_preferences, NEMO_PREFERENCES_CLICK_TO_RENAME);
}

/* Click-to-rename: a slow second click on the single selected row starts
 * renaming after the double-click interval, unless a double click, another
 * click, a key press, navigation or a selection change happens first. */
static void
miller_rename_timer_cancel (NemoMillerColumnsView *view)
{
	if (view->details->rename_timeout_id != 0) {
		g_source_remove (view->details->rename_timeout_id);
		view->details->rename_timeout_id = 0;
	}
}

typedef struct {
	NemoMillerColumnsView *view;
	NemoFile *file;
} MillerRenameClick;

static void
miller_rename_click_free (gpointer data)
{
	MillerRenameClick *rc = data;
	nemo_file_unref (rc->file);
	g_free (rc);
}

static gboolean
miller_rename_click_fire (gpointer data)
{
	MillerRenameClick *rc = data;
	NemoMillerColumnsView *view = rc->view;
	view->details->rename_timeout_id = 0;
	if (!gtk_widget_in_destruction (GTK_WIDGET (view)) &&
	    !nemo_view_get_is_renaming (NEMO_VIEW (view)) &&
	    view->details->selection != NULL &&
	    view->details->selection->next == NULL &&
	    view->details->selection->data == (gpointer) rc->file)
		miller_start_renaming_file (NEMO_VIEW (view), rc->file, FALSE);
	return G_SOURCE_REMOVE;
}

static void
miller_rename_click_schedule (NemoMillerColumnsView *view, NemoFile *file)
{
	MillerRenameClick *rc;
	gint dct = 400;
	g_object_get (gtk_widget_get_settings (GTK_WIDGET (view)),
	              "gtk-double-click-time", &dct, NULL);
	miller_rename_timer_cancel (view);
	rc = g_new0 (MillerRenameClick, 1);
	rc->view = view;
	rc->file = nemo_file_ref (file);
	view->details->rename_timeout_id =
		g_timeout_add_full (G_PRIORITY_DEFAULT, MAX (dct + 100, 600),
		                    miller_rename_click_fire, rc, miller_rename_click_free);
}

static const char *
miller_get_view_id (NemoView *nemo_view)
{
	return NEMO_MILLER_VIEW_ID;
}

static char *
miller_get_first_visible_file (NemoView *nemo_view)
{
	NemoMillerColumnsView *view;
	MillerCol *col;
	GtkTreeIter iter;
	GFile *row;
	char *uri;

	view = NEMO_MILLER_COLUMNS_VIEW (nemo_view);
	col = current_store_column (view);
	if (col == NULL ||
	    !gtk_tree_model_get_iter_first (GTK_TREE_MODEL (col->store), &iter)) {
		return NULL;
	}
	row = NULL;
	gtk_tree_model_get (GTK_TREE_MODEL (col->store), &iter, COL_GFILE, &row, -1);
	uri = (row != NULL) ? g_file_get_uri (row) : NULL;
	if (row != NULL) {
		g_object_unref (row);
	}
	return uri;
}

static void
miller_scroll_to_file (NemoView *nemo_view, const char *uri)
{
	NemoMillerColumnsView *view;
	MillerCol *col;
	GtkTreeIter iter;
	GFile *gfile;
	GdkRectangle cell, visible;

	view = NEMO_MILLER_COLUMNS_VIEW (nemo_view);
	col = current_store_column (view);
	if (col == NULL || uri == NULL) {
		return;
	}
	gfile = g_file_new_for_uri (uri);
	if (gtk_tree_model_get_iter_first (GTK_TREE_MODEL (col->store), &iter)) {
		do {
			GFile *row;
			gboolean match;

			gtk_tree_model_get (GTK_TREE_MODEL (col->store), &iter, COL_GFILE, &row, -1);
			match = (row != NULL && g_file_equal (row, gfile));
			if (row != NULL) {
				g_object_unref (row);
			}
			if (match) {
				GtkTreePath *path;

				path = gtk_tree_model_get_path (GTK_TREE_MODEL (col->store),
								&iter);
				/* scroll only if the row is not fully
				 * visible: forced alignment made the
				 * reload path jump the list one row */
				gtk_tree_view_get_cell_area (GTK_TREE_VIEW (col->tree),
							     path, NULL, &cell);
				gtk_tree_view_get_visible_rect (GTK_TREE_VIEW (col->tree),
								&visible);
				if (cell.height > 0 &&
				    (cell.y < visible.y ||
				     cell.y + cell.height > visible.y + visible.height)) {
					gtk_tree_view_scroll_to_cell (GTK_TREE_VIEW (col->tree),
								      path, NULL,
								      FALSE, 0.0, 0.0);
				}
				gtk_tree_path_free (path);
				break;
			}
		} while (gtk_tree_model_iter_next (GTK_TREE_MODEL (col->store), &iter));
	}
	g_object_unref (gfile);
}
/* ------------------------------------------------------------------ */
/* video stills for the column icons                                    */
/* ------------------------------------------------------------------ */

typedef struct {
	NemoMillerColumnsView *view;	/* weak pointer */
	char *uri;
	char *tmp_path;
	GSubprocess *subprocess;
} StillJob;

static void
still_job_free (StillJob *job)
{
	if (job->view != NULL) {
		g_object_remove_weak_pointer (G_OBJECT (job->view),
					      (gpointer *) &job->view);
	}
	if (job->subprocess != NULL) {
		g_object_unref (job->subprocess);
	}
	g_free (job->uri);
	g_free (job->tmp_path);
	g_free (job);
}

static void
miller_refresh_still (NemoMillerColumnsView *view, const char *uri,
		      GdkPixbuf *pixbuf)
{
	GList *l;

	for (l = view->details->columns; l != NULL; l = l->next) {
		MillerCol *col = l->data;
		GtkTreeIter iter;
		gboolean valid;

		valid = gtk_tree_model_get_iter_first (GTK_TREE_MODEL (col->store),
						       &iter);
		while (valid) {
			GFile *gfile = NULL;
			char *row_uri;
			GdkPixbuf *scaled;

			gtk_tree_model_get (GTK_TREE_MODEL (col->store), &iter,
					    COL_GFILE, &gfile, -1);
            if (gfile == NULL) {
                valid = gtk_tree_model_iter_next (GTK_TREE_MODEL (col->store), &iter);
                continue;
            }
			row_uri = g_file_get_uri (gfile);
			g_object_unref (gfile);
			if (g_strcmp0 (row_uri, uri) == 0) {
				scaled = miller_scaled_still (view, pixbuf,
							      col->icon_size);
				gtk_list_store_set (col->store, &iter,
						    COL_ICON, scaled, -1);
				if (scaled != NULL) {
					g_object_unref (scaled);
				}
			}
			g_free (row_uri);
            valid = gtk_tree_model_iter_next (GTK_TREE_MODEL (col->store), &iter);
		}
	}
}

static void
miller_still_gen_cb (GObject *source, GAsyncResult *res, gpointer user_data)
{
	StillJob *job = user_data;
	NemoMillerColumnsView *view = job->view;
	GdkPixbuf *pixbuf = NULL;
	GError *error = NULL;

	g_subprocess_wait_finish (job->subprocess, res, &error);
	if (error != NULL) {
		g_error_free (error);
	} else if (g_subprocess_get_exit_status (job->subprocess) == 0) {
		pixbuf = gdk_pixbuf_new_from_file (job->tmp_path, NULL);
	}
	g_unlink (job->tmp_path);

	if (view == NULL) {
		if (pixbuf != NULL) {
			g_object_unref (pixbuf);
		}
		still_job_free (job);
		return;
	}

	g_hash_table_remove (view->details->still_pending, job->uri);
    if (pixbuf != NULL) {
        if (!g_hash_table_contains (view->details->still_cache, job->uri)) {
            if (g_queue_get_length (view->details->still_cache_order) >= 32) {
                char *oldest = g_queue_pop_head (view->details->still_cache_order);
                g_hash_table_remove (view->details->still_cache, oldest);
                g_free (oldest);
            }
            g_queue_push_tail (view->details->still_cache_order, g_strdup (job->uri));
        }
        g_hash_table_replace (view->details->still_cache,
                              g_strdup (job->uri), pixbuf);
        miller_refresh_still (view, job->uri, pixbuf);
    }
	still_job_free (job);
}

static void
miller_start_still_gen (NemoMillerColumnsView *view, const char *uri)
{
	StillJob *job;
	GFile *gfile;
	char *path;
	GError *error = NULL;
	static guint still_seq = 0;

	if (!g_str_has_prefix (uri, "file://")) {
		return;
	}
	gfile = g_file_new_for_uri (uri);
	path = g_file_get_path (gfile);
	g_object_unref (gfile);
	if (path == NULL) {
		return;
	}

	if (view->details->tmp_dir == NULL) {
		view->details->tmp_dir = g_dir_make_tmp ("nemo-miller-XXXXXX",
							 NULL);
	}
	if (view->details->tmp_dir == NULL) {
		g_free (path);
		return;
	}

	job = g_new0 (StillJob, 1);
	job->view = view;
	g_object_add_weak_pointer (G_OBJECT (view), (gpointer *) &job->view);
	job->uri = g_strdup (uri);
	job->tmp_path = g_strdup_printf ("%s%sstill%u.png",
					 view->details->tmp_dir,
					 G_DIR_SEPARATOR_S,
					 g_atomic_int_add (&still_seq, 1));

	{
		const char * const argv[] = { "ffmpeg", "-y", "-loglevel",
					      "error",
					      "-ss", "1", "-i", path,
					      "-frames:v", "1",
					      "-vf", "scale=256:-2",
					      job->tmp_path, NULL };

		job->subprocess = g_subprocess_newv (argv,
						     G_SUBPROCESS_FLAGS_NONE,
						     &error);
	}
	g_free (path);

	if (error != NULL) {
		g_error_free (error);
		g_hash_table_remove (view->details->still_pending, uri);
		still_job_free (job);
		return;
	}

	g_subprocess_wait_async (job->subprocess,
				 view->details->preview_cancellable,
				 miller_still_gen_cb, job);
}

static GdkPixbuf *
miller_scaled_still (NemoMillerColumnsView *view, GdkPixbuf *base, int icon_size)
{
	int w, h;
	GdkPixbuf *scaled;

	w = gdk_pixbuf_get_width (base);
	h = gdk_pixbuf_get_height (base);
	if (w <= 0 || h <= 0 || icon_size <= 0) {
		g_object_ref (base);
		return base;
	}
	if (w >= h) {
		int nh = (int) ((double) h * icon_size / w + 0.5);

		scaled = gdk_pixbuf_scale_simple (base, icon_size,
						  MAX (nh, 1),
						  GDK_INTERP_BILINEAR);
	} else {
		int nw = (int) ((double) w * icon_size / h + 0.5);

		scaled = gdk_pixbuf_scale_simple (base, MAX (nw, 1),
						  icon_size,
						  GDK_INTERP_BILINEAR);
	}
	return scaled;
}

/* the icon for a row: nemo's thumbnail when it has one; for video
 * files our own async-generated still, cached per view */
static void
miller_row_refresh_cb (NemoFile *file, gpointer callback_data)
{
	NemoMillerColumnsView *view = NEMO_MILLER_COLUMNS_VIEW (callback_data);
	GList *l;
	GFile *gfile;

	if (view->details->columns == NULL) {
		return;
	}
	/* one-shot: the row now carries its real info; later changes
	 * arrive through the file_changed vfunc. Without cancelling,
	 * the registration stays live and re-fires on every attribute
	 * refresh of this file (nemo re-queries often), re-sorting and
	 * repainting whole columns each time */
	nemo_file_cancel_call_when_ready (file, miller_row_refresh_cb,
					  callback_data);

	/* the file's info arrived: repaint its row icon in every
	 * column that shows it */
	gfile = nemo_file_get_location (file);
	for (l = view->details->columns; l != NULL; l = l->next) {
		MillerCol *col = l->data;
		GtkTreeIter iter;
		gboolean valid;

		valid = gtk_tree_model_get_iter_first (GTK_TREE_MODEL (col->store),
						       &iter);
		while (valid) {
			GFile *row = NULL;
			gboolean match;

			gtk_tree_model_get (GTK_TREE_MODEL (col->store), &iter,
					    COL_GFILE, &row, -1);
			match = (row != NULL && g_file_equal (row, gfile));
			if (row != NULL) {
				g_object_unref (row);
			}
			if (match) {
				GdkPixbuf *pixbuf, *emblem;
				char *name, *old_name, *mime, *old_mime;
				gboolean is_dir, old_is_dir;
				gint64 old_size, old_mtime, new_size, new_mtime;
				gboolean changed;

				pixbuf = miller_icon_for_file (view, file,
							       col->icon_size);
				emblem = miller_first_emblem_for_file (file, col->icon_size);
				gtk_tree_model_get (GTK_TREE_MODEL (col->store),
						    &iter,
						    COL_NAME, &old_name,
						    COL_IS_DIR, &old_is_dir,
						    COL_SIZE, &old_size,
						    COL_MTIME, &old_mtime,
						    COL_MIME, &old_mime, -1);
				name = nemo_file_get_display_name (file);
				is_dir = nemo_file_is_directory (file);
				new_size = (gint64) nemo_file_get_size (file);
				new_mtime = (gint64) nemo_file_get_mtime (file);
				mime = nemo_file_get_mime_type (file);
				/* touch the sort columns only when the
				 * value really changed: any set on them
				 * re-sorts and repaints the entire column */
				changed = (g_strcmp0 (old_name, name) != 0 ||
					   old_is_dir != is_dir ||
					   old_size != new_size ||
					   old_mtime != new_mtime ||
					   g_strcmp0 (old_mime, mime) != 0);
                /* An initial GIO row may be classified before NemoFile's
                 * MIME or directory attributes become available. Move it
                 * between groups after the model has stabilized. */
                if (g_strcmp0 (miller_group_mode (view), "none") != 0) {
                    gchar *old_key = NULL;
                    gchar *new_key = dakkar_group_file_key (file, miller_group_mode (view));
                    gtk_tree_model_get (GTK_TREE_MODEL (col->store), &iter,
                                        COL_GROUP_KEY, &old_key, -1);
                    if (g_strcmp0 (old_key, new_key) != 0)
                        miller_group_schedule_rebuild (col);
                    g_free (old_key);
                    g_free (new_key);
                }
				if (changed) {
					gtk_list_store_set (col->store, &iter,
							    COL_ICON, pixbuf,
							    COL_EMBLEM, emblem,
							    COL_NAME, name,
							    COL_IS_DIR, is_dir,
							    COL_SIZE, new_size,
							    COL_MTIME, new_mtime,
							    COL_MIME, mime,
							    -1);
				} else {
					gtk_list_store_set (col->store, &iter,
							    COL_ICON, pixbuf,
							    COL_EMBLEM, emblem, -1);
				}
				g_free (old_name);
				g_free (old_mime);
				g_free (name);
				g_free (mime);
				g_clear_object (&pixbuf);
				g_clear_object (&emblem);
			}
			/* advance AFTER touching the row, never before */
			valid = gtk_tree_model_iter_next (GTK_TREE_MODEL (col->store),
							  &iter);
		}
	}
	g_object_unref (gfile);
	/* A pending F2 rename may have been waiting for NemoFile attributes. */
	miller_queue_pending_rename (view);
}

/* Obtain Nemo's canonical first emblem: symlink arrow, read-only padlock,
 * favorites, custom metadata and extensions. The parent is needed so the
 * permissions emblem matches the other Nemo views.  Missing theme emblems
 * simply leave the separate renderer blank. Owned ref returned or NULL. */
static GdkPixbuf *
miller_first_emblem_for_file (NemoFile *file, int icon_size)
{
	NemoFile *parent;
	GList *emblem_icons;
	GIcon *emblem_icon;
	NemoIconInfo *info;
	GdkPixbuf *emblem_pix = NULL;
	int size;

	if (file == NULL) {
		return NULL;
	}
	parent = nemo_file_get_parent (file);
	emblem_icons = nemo_file_get_emblem_icons (file, parent);
	if (parent != NULL) {
		nemo_file_unref (parent);
	}
	if (emblem_icons == NULL) {
		return NULL;
	}
	/* Fixed, modest footprint for small columns; scales with zoom. */
	size = CLAMP (icon_size / 2, 12, 20);
	emblem_icon = G_ICON (emblem_icons->data);
	info = nemo_icon_info_lookup (emblem_icon, size, 1);
	if (info != NULL) {
		emblem_pix = nemo_icon_info_get_pixbuf_at_size (info, size);
		nemo_icon_info_unref (info);
	}
	g_list_free_full (emblem_icons, g_object_unref);
	return emblem_pix;
}

static GdkPixbuf *
miller_icon_for_file (NemoMillerColumnsView *view, NemoFile *file, int icon_size)
{
	GdkPixbuf *pixbuf;
	char *mime;

	pixbuf = nemo_file_get_icon_pixbuf (file, icon_size, TRUE, 1,
					    NEMO_FILE_ICON_FLAGS_USE_THUMBNAILS);

	mime = nemo_file_get_mime_type (file);
	/* Video stills are thumbnails too; honour "Show thumbnails"
	 * (never / local only / always), the size limit and the per-folder
	 * thumbnail toggle like the other views do through nemo-file. */
	if (mime != NULL && g_str_has_prefix (mime, "video/") &&
	    nemo_file_should_show_thumbnail (file)) {
		char *uri = nemo_file_get_uri (file);
		GdkPixbuf *cached = g_hash_table_lookup (view->details->still_cache,
							 uri);

		if (cached != NULL) {
			GdkPixbuf *scaled = miller_scaled_still (view, cached,
								 icon_size);

			if (pixbuf != NULL) {
				g_object_unref (pixbuf);
			}
			pixbuf = scaled;
        } else if (!g_hash_table_contains (view->details->still_pending, uri) &&
                   g_hash_table_size (view->details->still_pending) < 2) {
            g_hash_table_add (view->details->still_pending, g_strdup (uri));
			miller_start_still_gen (view, uri);
		}
		g_free (uri);
	}
	g_free (mime);

	return pixbuf;
}

/* ------------------------------------------------------------------ */
/* tree event handlers                                                 */
/* ------------------------------------------------------------------ */

static void
handle_dir_selected (NemoMillerColumnsView *view, MillerCol *col,
		     GFile *gfile, NemoFile *file)
{
	GList *l;

	if (col->is_chain) {
		MillerCol *base = last_chain_column (view);
		/* This was an explicit user choice to open a directory, so F2
		 * may rename this directory even though navigation clears selection. */
		g_set_object (&view->details->rename_opened_target, gfile);

		if (g_file_equal (gfile, base->dir)) {
			return;		/* current directory re-selected */
		}
		/* Ancestor or sibling in an earlier column: navigate the slot
		 * there, with the clicked folder itself as the new selection.
		 * Without an explicit selection, Nemo's "going up selects the
		 * folder you came from" rule selected a row in the NEXT column
		 * and wiped the clicked row: Delete/Trash then silently did
		 * nothing (or targeted the folder the user came from). */
		{
			NemoWindowSlot *slot =
				nemo_view_get_nemo_window_slot (NEMO_VIEW (view));
			if (slot != NULL && nemo_file_is_directory (file)) {
				GList *sel = g_list_prepend (NULL, nemo_file_ref (file));
				nemo_window_slot_open_location_full (slot, gfile, 0,
								     sel, NULL, NULL);
				nemo_file_list_free (sel);
			} else {
				nemo_view_activate_file (NEMO_VIEW (view), file, 0);
			}
		}
		return;
	}

	/* base or extension column: cascade a new column to the right,
	 * replacing whatever was cascaded after this column before */
	for (l = view->details->columns; l != NULL; l = l->next) {
		MillerCol *c = l->data;

		if (c == col) {
			GList *after = l->next;
			MillerCol *ext;
			GFile *existing;

			if (after != NULL) {
				MillerCol *opened = after->data;
				existing = opened->dir;
				if (existing != NULL && g_file_equal (existing, gfile)) {
					miller_set_action_column (view, opened);
					miller_set_paste_destination (view, opened->dir);
					return;	/* already open right after this column */
				}
			}
			remove_extension_columns_after (view, col);

			ext = miller_col_new (view, gfile, FALSE, NULL);
			ext->directory = nemo_directory_get (gfile);
			miller_monitor_dir (view, ext);
			gtk_widget_show_all (ext->scroll);
			view->details->columns = g_list_append (view->details->columns, ext);
			/* A folder click opens a destination: Paste now targets this
			 * folder, not the parent row that received the mouse event. */
			miller_set_action_column (view, ext);
			miller_set_paste_destination (view, ext->dir);
			miller_refresh_opened_marks (view);
			/* surgical: wire the ext column into the chain
			 * without reparenting the columns before it */
			rewire_tail (view,
				     g_list_length (view->details->columns) - 1,
				     ext->scroll);
			miller_queue_divider_redraw (view);
			miller_slide_columns_to_end (view);
			miller_fade_in (ext->tree);
			return;
		}
	}
}

static void
rebuild_selection (NemoMillerColumnsView *view)
{
	GList *l, *rows;
	MillerCol *col;

	free_selection (view);

	for (l = view->details->columns; l != NULL; l = l->next) {
		GtkTreeSelection *sel;

		col = l->data;
		sel = gtk_tree_view_get_selection (GTK_TREE_VIEW (col->tree));
		rows = gtk_tree_selection_get_selected_rows (sel, NULL);
		GList *r;
		for (r = rows; r != NULL; r = r->next) {
			GtkTreeIter iter;
			NemoFile *file = NULL;

			if (gtk_tree_model_get_iter (GTK_TREE_MODEL (col->store),
						     &iter, r->data)) {
				gtk_tree_model_get (GTK_TREE_MODEL (col->store), &iter,
						    COL_FILE, &file, -1);
				if (file != NULL) {
					view->details->selection =
						g_list_prepend (view->details->selection, file);
				}
			}
		}
		g_list_free_full (rows, (GDestroyNotify) gtk_tree_path_free);
	}
	view->details->selection = g_list_reverse (view->details->selection);
}

static void
on_col_selection_changed (GtkTreeSelection *selection, gpointer user_data)
{
	NemoMillerColumnsView *view = user_data;
	GtkTreeView *tree;
	GList *l;
	MillerCol *col;
	NemoFile *file = NULL;
	gboolean is_dir = FALSE;
	GFile *gfile = NULL;
	guint count;

	if (view->details->programmatic || view->details->in_load) {
		return;
	}
	tree = gtk_tree_selection_get_tree_view (selection);
	col = find_column_by_tree (view, GTK_WIDGET (tree));
	if (col == NULL) {
		return;
	}
	miller_set_action_column (view, col);

	/* miller semantics: one active selection across all columns */
	view->details->programmatic = TRUE;
	for (l = view->details->columns; l != NULL; l = l->next) {
		MillerCol *other = l->data;
		GtkTreeSelection *osel;

		if (other == col) {
			continue;
		}
		osel = gtk_tree_view_get_selection (GTK_TREE_VIEW (other->tree));
		if (gtk_tree_selection_count_selected_rows (osel) > 0) {
			gtk_tree_selection_unselect_all (osel);
		}
	}
	view->details->programmatic = FALSE;

	rebuild_selection (view);

	count = g_list_length (view->details->selection);
	nemo_view_notify_selection_changed (NEMO_VIEW (view));

	if (count == 1) {
		GtkTreeSelection *sel;
		GList *rows;
		GtkTreeIter iter;

		sel = gtk_tree_view_get_selection (GTK_TREE_VIEW (col->tree));
		rows = gtk_tree_selection_get_selected_rows (sel, NULL);
		if (rows != NULL &&
		    gtk_tree_model_get_iter (GTK_TREE_MODEL (col->store), &iter, rows->data)) {
			gtk_tree_model_get (GTK_TREE_MODEL (col->store), &iter,
					    COL_FILE, &file,
					    COL_IS_DIR, &is_dir,
					    COL_GFILE, &gfile,
					    -1);
		}
		g_list_free_full (rows, (GDestroyNotify) gtk_tree_path_free);
	}

    /* Read ahead from an explicitly selected *local* folder; never traverse
     * a network share or ancestor merely because it is visually opened. */
    miller_prefetch_start (view,
         is_dir && file != NULL && !nemo_file_is_symbolic_link (file) ?
         gfile : NULL);
	if (file != NULL) {
		nemo_file_unref (file);
	}
	if (gfile != NULL) {
		g_object_unref (gfile);
	}
}

static void
on_row_activated (GtkTreeView *tree, GtkTreePath *path,
		  GtkTreeViewColumn *column, gpointer user_data)
{
	NemoMillerColumnsView *view = user_data;
	MillerCol *col;
	GtkTreeIter iter;
	NemoFile *file = NULL;
	gboolean is_dir = FALSE;

	col = find_column_by_tree (view, GTK_WIDGET (tree));
	if (col == NULL ||
	    !gtk_tree_model_get_iter (GTK_TREE_MODEL (col->store), &iter, path)) {
		return;
	}

	gtk_tree_model_get (GTK_TREE_MODEL (col->store), &iter,
			    COL_FILE, &file, COL_IS_DIR, &is_dir, -1);
	if (file == NULL) {
		return;
	}

	if (!is_dir && g_settings_get_enum (nemo_preferences,
			NEMO_PREFERENCES_CLICK_POLICY) == NEMO_CLICK_POLICY_SINGLE) {
		nemo_file_unref (file);
		return; /* already activated by single click */
	}
	if (is_dir) {
		/* double click: navigate INTO this directory */
		nemo_view_activate_file (NEMO_VIEW (view), file, 0);
	} else {
		GList *files;

		files = g_list_prepend (NULL, nemo_file_ref (file));
		nemo_view_activate_files (NEMO_VIEW (view), files, 0, TRUE);
		nemo_file_list_free (files);
	}

	nemo_file_unref (file);
}

/* Background menus must have an empty selection across EVERY column.
 * The focus may have moved by keyboard without changing the prior tree's
 * selection; keeping that selection would make the menu's actions refer to
 * a file not visible in the active column. Do not treat opened breadcrumbs
 * as selections, and never derive a file-operation target from the path. */
static void
miller_clear_selection_for_background_menu (NemoMillerColumnsView *view)
{
	GList *l;
	gboolean changed = FALSE;

	view->details->programmatic = TRUE;
	for (l = view->details->columns; l != NULL; l = l->next) {
		MillerCol *col = l->data;
		GtkTreeSelection *selection =
			gtk_tree_view_get_selection (GTK_TREE_VIEW (col->tree));
		if (gtk_tree_selection_count_selected_rows (selection) > 0) {
			gtk_tree_selection_unselect_all (selection);
			changed = TRUE;
		}
	}
	view->details->programmatic = FALSE;

	if (changed) {
		rebuild_selection (view);
		nemo_view_notify_selection_changed (NEMO_VIEW (view));
	}
}

/* TRUE when (x, y) lies on a row's icon or name text (not on the blank
 * part to the right of the name).  Presses there drag the file (DnD);
 * presses on blank space start a rubber band. */
static gboolean
miller_point_on_row_content (MillerCol *col, gint x, gint y)
{
	GtkTreeView *tree;
	GtkTreePath *path = NULL;
	GtkTreeViewColumn *tcol = NULL;
	GtkTreeIter iter;
	GdkRectangle cell;
	gint start = 0, width = 0, nat = 0, xpad = 0;
	gboolean on = FALSE;

	if (col == NULL)
		return FALSE;
	tree = GTK_TREE_VIEW (col->tree);
	if (!gtk_tree_view_get_path_at_pos (tree, x, y, &path, &tcol, NULL, NULL))
		return FALSE;
	if (tcol != NULL &&
	    gtk_tree_model_get_iter (GTK_TREE_MODEL (col->store), &iter, path)) {
		gtk_tree_view_get_cell_area (tree, path, tcol, &cell);
		gtk_tree_view_column_cell_set_cell_data (tcol, GTK_TREE_MODEL (col->store),
		                                         &iter, FALSE, FALSE);
		if (gtk_tree_view_column_cell_get_position (tcol, col->txt, &start, &width)) {
			gtk_cell_renderer_get_preferred_width (col->txt, col->tree, NULL, &nat);
			gtk_cell_renderer_get_padding (col->txt, &xpad, NULL);
			on = x <= cell.x + start + MIN (width, nat) + xpad + 4;
		} else {
			on = TRUE;
		}
	}
	gtk_tree_path_free (path);
	return on;
}

/* Undo a press-time selection freeze.  When @click_path_tree is the
 * frozen tree (a plain click without drag), the click now selects only the
 * pressed row, as a click on a selected row normally does. */
static void
miller_unfreeze_selection (NemoMillerColumnsView *view, GtkWidget *click_path_tree)
{
	GtkWidget *tree = view->details->frozen_tree;
	GtkTreePath *path = view->details->frozen_path;

	view->details->frozen_tree = NULL;
	view->details->frozen_path = NULL;
	if (tree != NULL)
		g_object_set_data (G_OBJECT (tree), "miller-selection-frozen", NULL);
	if (tree != NULL && tree == click_path_tree && path != NULL) {
		GtkTreeSelection *sel = gtk_tree_view_get_selection (GTK_TREE_VIEW (tree));
		gtk_tree_selection_unselect_all (sel);
		gtk_tree_selection_select_path (sel, path);
	}
	if (path != NULL)
		gtk_tree_path_free (path);
}

static void
miller_resume_rubber_band (NemoMillerColumnsView *view)
{
	if (view->details->band_suspended_tree != NULL) {
		gtk_tree_view_set_rubber_banding (GTK_TREE_VIEW (view->details->band_suspended_tree), TRUE);
		view->details->band_suspended_tree = NULL;
	}
}

/* A press on an inline group heading: a primary click folds the group.
 * Headings are no file rows, so TRUE stops every other press handling
 * (rename, drag, context menu, navigation). */
static gboolean
miller_press_group_header (NemoMillerColumnsView *view, GtkWidget *widget,
                           GdkEventButton *event)
{
    GtkTreeView *tree = GTK_TREE_VIEW (widget);
    MillerCol *col = find_column_by_tree (view, widget);
    GtkTreePath *hit = NULL;
    GtkTreeIter iter;
    gboolean header = FALSE;
    gchar *key = NULL;
    if (col != NULL && gtk_tree_view_get_path_at_pos (tree, (int)event->x,
                                     (int)event->y, &hit, NULL, NULL, NULL) &&
        gtk_tree_model_get_iter (GTK_TREE_MODEL (col->store), &iter, hit))
        gtk_tree_model_get (GTK_TREE_MODEL (col->store), &iter,
                            COL_GROUP_HEADER, &header, COL_GROUP_KEY, &key, -1);
    if (hit != NULL) gtk_tree_path_free (hit);
    if (header && key && event->button == GDK_BUTTON_PRIMARY &&
        event->type == GDK_BUTTON_PRESS) miller_group_toggle (view, col, key);
    g_free (key);
    return header;
}

/* Middle click opens the item under the pointer in a new tab. */
static gboolean
miller_press_middle (NemoMillerColumnsView *view, GtkWidget *widget,
                     GdkEventButton *event)
{
	GtkTreeView *tree = GTK_TREE_VIEW (widget);
	GtkTreePath *mpath = NULL;
	GtkTreeIter mi;
	MillerCol *mcol = find_column_by_tree (view, widget);
	NemoFile *mfile = NULL;
	gtk_tree_view_get_path_at_pos (tree, (gint) event->x, (gint) event->y,
				       &mpath, NULL, NULL, NULL);
	if (mcol != NULL && mpath != NULL &&
	    gtk_tree_model_get_iter (GTK_TREE_MODEL (mcol->store), &mi, mpath)) {
		gtk_tree_model_get (GTK_TREE_MODEL (mcol->store), &mi,
				    COL_FILE, &mfile, -1);
	}
	if (mpath != NULL) gtk_tree_path_free (mpath);
	if (mfile != NULL) {
		nemo_view_activate_file (NEMO_VIEW (view), mfile,
					 NEMO_WINDOW_OPEN_FLAG_NEW_TAB);
		nemo_file_unref (mfile);
		return TRUE;
	}
	return FALSE;
}

/* Click-to-rename: a plain press on the one selected row remembers its
 * file; the release decides whether to start renaming. */
static void
miller_press_note_rename (NemoMillerColumnsView *view, GtkWidget *widget,
                          GdkEventButton *event)
{
	GtkTreeView *tree = GTK_TREE_VIEW (widget);
	GtkTreePath *rpath = NULL;
	GtkTreeSelection *rsel = gtk_tree_view_get_selection (tree);
	MillerCol *rcol = find_column_by_tree (view, widget);
	GtkTreeIter riter;
	GFile *rfile = NULL;
	g_clear_object (&view->details->rename_press_file);
	if (view->details->click_to_rename && rcol != NULL &&
	    (event->state & gtk_accelerator_get_default_mod_mask ()) == 0 &&
	    gtk_tree_view_get_path_at_pos (tree, (gint) event->x, (gint) event->y,
	                                   &rpath, NULL, NULL, NULL) &&
	    gtk_tree_selection_count_selected_rows (rsel) == 1 &&
	    gtk_tree_selection_path_is_selected (rsel, rpath) &&
	    gtk_tree_model_get_iter (GTK_TREE_MODEL (rcol->store), &riter, rpath)) {
		gtk_tree_model_get (GTK_TREE_MODEL (rcol->store), &riter,
		                    COL_GFILE, &rfile, -1);
		view->details->rename_press_file = rfile;	/* owned */
	}
	if (rpath != NULL) gtk_tree_path_free (rpath);
}

/* Double/triple click. The first click already opened a folder, so the
 * repeats are swallowed there: they could land on the rebuilt column and
 * open something else. Files still activate on double click. */
static gboolean
miller_press_repeat (NemoMillerColumnsView *view, GtkWidget *widget,
                     GdkEventButton *event)
{
	GtkTreePath *dpath = NULL;

	g_clear_object (&view->details->rename_press_file);
	gtk_tree_view_get_path_at_pos (GTK_TREE_VIEW (widget),
				       (gint) event->x, (gint) event->y,
				       &dpath, NULL, NULL, NULL);
	if (dpath != NULL) {
		MillerCol *dcol = find_column_by_tree (view, widget);
		GtkTreeIter diter;
		gboolean d_is_dir = FALSE;

		if (dcol != NULL &&
		    gtk_tree_model_get_iter (GTK_TREE_MODEL (dcol->store), &diter, dpath)) {
			gtk_tree_model_get (GTK_TREE_MODEL (dcol->store), &diter,
					    COL_IS_DIR, &d_is_dir, -1);
		}
		gtk_tree_path_free (dpath);
		if (d_is_dir) {
			view->details->click_pending = FALSE;
			return TRUE;
		}
	}
	return FALSE;
}

/* Single primary press: mark the click as pending (directories open on
 * release, so no rebuild can come between press and release) and choose
 * between dragging the file under the pointer and a rubber band. */
static void
miller_press_primary (NemoMillerColumnsView *view, GtkWidget *widget,
                      GdkEventButton *event)
{
	GtkTreeView *tree = GTK_TREE_VIEW (widget);

	view->details->click_pending = TRUE;
	view->details->band_press_tree = widget;
	view->details->band_press_x = event->x;
	view->details->band_press_y = event->y;
	/* A press on a file's icon/name drags that file (also when it
	 * was not selected yet), exactly like the list view; GTK would
	 * otherwise start a rubber band on any unselected row. */
	miller_resume_rubber_band (view);
	miller_unfreeze_selection (view, NULL);
	if (miller_point_on_row_content (find_column_by_tree (view, widget),
	                                 (gint) event->x, (gint) event->y)) {
		GtkTreePath *fpath = NULL;
		GtkTreeSelection *fsel = gtk_tree_view_get_selection (tree);

		gtk_tree_view_set_rubber_banding (tree, FALSE);
		view->details->band_suspended_tree = widget;
		/* Dragging one of several selected files drags them all. */
		if ((event->state & (GDK_CONTROL_MASK | GDK_SHIFT_MASK)) == 0 &&
		    gtk_tree_view_get_path_at_pos (tree, (gint) event->x, (gint) event->y,
		                                   &fpath, NULL, NULL, NULL) &&
		    gtk_tree_selection_path_is_selected (fsel, fpath) &&
		    gtk_tree_selection_count_selected_rows (fsel) > 1) {
			view->details->frozen_tree = widget;
			view->details->frozen_path = fpath;
			fpath = NULL;
			g_object_set_data (G_OBJECT (widget), "miller-selection-frozen",
			                   GINT_TO_POINTER (1));
		}
		if (fpath != NULL)
			gtk_tree_path_free (fpath);
	}
	/* A plain press on empty space starts a fresh band selection:
	 * clear first (GTK leaves dead-space presses alone).  With
	 * Ctrl/Shift the band toggles/extends the existing selection. */
	if ((event->state & (GDK_CONTROL_MASK | GDK_SHIFT_MASK)) == 0 &&
	    !gtk_tree_view_get_path_at_pos (tree, (gint) event->x, (gint) event->y,
	                                    NULL, NULL, NULL, NULL)) {
		miller_clear_selection_for_background_menu (view);
	}
}

/* Right click: the selection menu for a row (selecting it first when it
 * was not selected), the background menu for empty space. */
static gboolean
miller_press_context_menu (NemoMillerColumnsView *view, GtkWidget *widget,
                           GdkEventButton *event)
{
	GtkTreeView *tree = GTK_TREE_VIEW (widget);
	GtkTreePath *path = NULL;

	gtk_tree_view_get_path_at_pos (tree, (gint) event->x, (gint) event->y,
				       &path, NULL, NULL, NULL);

	if (path != NULL) {
		GtkTreeSelection *sel;

		sel = gtk_tree_view_get_selection (tree);
		if (!gtk_tree_selection_path_is_selected (sel, path)) {
			gtk_tree_selection_unselect_all (sel);
			gtk_tree_selection_select_path (sel, path);
		}
		gtk_tree_path_free (path);
		nemo_view_pop_up_selection_context_menu (NEMO_VIEW (view), event);
	} else {
		miller_clear_selection_for_background_menu (view);
		nemo_view_pop_up_background_context_menu (NEMO_VIEW (view), event);
	}

	/* Never let GtkTreeView change the selection *after* menu creation. */
	return TRUE;
}

static gboolean
on_tree_button_press (GtkWidget *widget, GdkEventButton *event,
		      gpointer user_data)
{
	NemoMillerColumnsView *view = user_data;

	if (event->button != 3 && event->button != 1 && event->button != 2) {
		return FALSE;
	}
	if (miller_press_group_header (view, widget, event))
		return TRUE;
	/* Only real input cancels deferred breadcrumb selection. GTK can emit
	 * selection-changed during a sorted model refresh before the row arrives. */
	g_object_set_data (G_OBJECT (view),
	                   "miller-pending-navigation-selection", NULL);
	{
		MillerCol *clicked_col = find_column_by_tree (view, widget);
		miller_set_action_column (view, clicked_col);
		/* Explicit mouse interaction identifies the column. A subsequent
		 * directory activation replaces it with the opened child. */
		if (clicked_col != NULL)
			miller_set_paste_destination (view, clicked_col->dir);
	}
	if (event->button == 2 && event->type == GDK_BUTTON_PRESS)
		return miller_press_middle (view, widget, event);

	miller_rename_timer_cancel (view);
	if (event->button == 1 && event->type == GDK_BUTTON_PRESS)
		miller_press_note_rename (view, widget, event);
	if (event->button == 1) {
		if (event->type == GDK_2BUTTON_PRESS || event->type == GDK_3BUTTON_PRESS)
			return miller_press_repeat (view, widget, event);
		miller_press_primary (view, widget, event);
	}

	if (event->button != 3 || event->type != GDK_BUTTON_PRESS) {
		return FALSE;
	}
	return miller_press_context_menu (view, widget, event);
}

static gboolean
on_tree_button_release (GtkWidget *widget, GdkEventButton *event,
			gpointer user_data)
{
	NemoMillerColumnsView *view = user_data;
	MillerCol *col;
	GtkTreePath *path = NULL;
	GtkTreeIter iter;
	NemoFile *file = NULL;
	gboolean is_dir = FALSE;
	GFile *gfile = NULL;

	if (event->button != 1) {
		return FALSE;
	}
    {
        MillerCol *hit_col = find_column_by_tree (view, widget);
        GtkTreePath *hit_path = NULL;
        GtkTreeIter hit_iter;
        gboolean is_header = FALSE;
        if (hit_col != NULL &&
            gtk_tree_view_get_path_at_pos (GTK_TREE_VIEW (widget), (gint)event->x,
                                           (gint)event->y, &hit_path, NULL, NULL, NULL) &&
            gtk_tree_model_get_iter (GTK_TREE_MODEL (hit_col->store),
                                     &hit_iter, hit_path))
            gtk_tree_model_get (GTK_TREE_MODEL (hit_col->store), &hit_iter,
                                COL_GROUP_HEADER, &is_header, -1);
        if (hit_path != NULL) gtk_tree_path_free (hit_path);
        if (is_header) {
            view->details->click_pending = FALSE;
            return TRUE; /* collapsing must not deselect other groups */
        }
    }

	view->details->click_pending = FALSE;
	miller_resume_rubber_band (view);
	/* A release here means no drag happened: a plain click on one of
	 * several selected rows selects just that row. */
	miller_unfreeze_selection (view, widget);

	if (view->details->in_load || view->details->programmatic) {
		return FALSE;
	}

	/* The release of a rubber-band drag (pointer moved past the
	 * drag threshold since the press in this tree) must not navigate,
	 * clear the selection or arm click-to-rename. */
	if (view->details->band_press_tree == widget &&
	    gtk_drag_check_threshold (widget,
	                              (gint) view->details->band_press_x,
	                              (gint) view->details->band_press_y,
	                              (gint) event->x, (gint) event->y)) {
		MillerCol *bcol = find_column_by_tree (view, widget);
		view->details->band_press_tree = NULL;
		g_clear_object (&view->details->rename_press_file);
		gtk_widget_grab_focus (widget);
		if (bcol != NULL) {
			miller_set_action_column (view, bcol);
			miller_set_paste_destination (view, bcol->dir);
		}
		return FALSE;
	}
	view->details->band_press_tree = NULL;

	/* keyboard focus: Delete/F2 are class binding sets that only
	 * activate through the focus widget's propagation chain; with
	 * no focused widget every shortcut dies */
	gtk_widget_grab_focus (widget);

	col = find_column_by_tree (view, widget);
	if (col == NULL) {
		return FALSE;
	}

	/* Use GtkTreeSelection MULTIPLE's native Shift+click contiguous-range
	 * selection (and Ctrl+click additive selection), but NEVER navigate
	 * when a modifier is held. This also prevents selecting multiple
	 * folder rows from creating an accidental opened breadcrumb target.
	 * The actual transfer target is verified independently. */
	if ((event->state & (GDK_CONTROL_MASK | GDK_SHIFT_MASK)) != 0) {
		return FALSE;
	}

	gtk_tree_view_get_path_at_pos (GTK_TREE_VIEW (widget),
				       (gint) event->x, (gint) event->y,
				       &path, NULL, NULL, NULL);
	if (path != NULL &&
	    gtk_tree_model_get_iter (GTK_TREE_MODEL (col->store), &iter, path)) {
		gtk_tree_model_get (GTK_TREE_MODEL (col->store), &iter,
				    COL_FILE, &file,
				    COL_IS_DIR, &is_dir,
				    COL_GFILE, &gfile, -1);
	}
	if (path != NULL) {
		gtk_tree_path_free (path);
	}

	if (file == NULL) {
		/* A blank click selects the background, even when the last
		 * selected file belongs to a different Miller column. */
		miller_clear_selection_for_background_menu (view);
	}
	if (file == NULL || !is_dir) {
		g_clear_object (&view->details->rename_opened_target);
	}

	/* Click-to-rename (global preference): only a second, unmodified
	 * click on the row that was already the single selection.  In
	 * single-click mode files activate instead, as in the other views. */
	g_clear_object (&view->details->last_click_file);
	view->details->last_click_time = 0;
	if (view->details->click_to_rename && file != NULL && gfile != NULL &&
	    view->details->rename_press_file != NULL &&
	    g_file_equal (view->details->rename_press_file, gfile) &&
	    (event->state & gtk_accelerator_get_default_mod_mask ()) == 0 &&
	    nemo_file_can_rename (file) &&
	    (is_dir || g_settings_get_enum (nemo_preferences, NEMO_PREFERENCES_CLICK_POLICY) !=
	               NEMO_CLICK_POLICY_SINGLE)) {
		g_clear_object (&view->details->rename_press_file);
		miller_rename_click_schedule (view, file);
		nemo_file_unref (file);
		g_object_unref (gfile);
		return FALSE;
	}
	g_clear_object (&view->details->rename_press_file);

	/* Directories always cascade with one click (the defining Miller
	 * interaction); double-click policy affects activation of files. */
	if (file != NULL && is_dir && gfile != NULL) {
		handle_dir_selected (view, col, gfile, file);
		miller_mark_active (view);
	} else if (file != NULL &&
		   g_settings_get_enum (nemo_preferences, NEMO_PREFERENCES_CLICK_POLICY) ==
			NEMO_CLICK_POLICY_SINGLE &&
		   !nemo_view_get_is_renaming (NEMO_VIEW (view))) {
		/* The GTK double-click row-activated signal handles double mode. */
		/* A deliberate first single click activates the file. */
		nemo_view_activate_file (NEMO_VIEW (view), file, 0);
	}

	if (file != NULL) {
		nemo_file_unref (file);
	}
	if (gfile != NULL) {
		g_object_unref (gfile);
	}
	return FALSE;
}

static void
navigate_to_parent (NemoMillerColumnsView *view)
{
	MillerCol *base;
	GFile *parent;
	NemoFile *file;

	base = last_chain_column (view);
	if (base == NULL || base->dir == NULL) {
		return;
	}
	parent = g_file_get_parent (base->dir);
	if (parent == NULL) {
		return;
	}
	file = nemo_file_get (parent);
	g_object_unref (parent);
	if (file != NULL) {
		nemo_view_activate_file (NEMO_VIEW (view), file, 0);
		nemo_file_unref (file);
	}
}

static void
activate_selected_in_column (NemoMillerColumnsView *view, MillerCol *col,
			     NemoWindowOpenFlags flags)
{
	GtkTreePath *cursor = NULL;
	GtkTreeIter iter;
	NemoFile *file = NULL;
	gboolean is_dir = FALSE;

	if (col == NULL) {
		return;
	}

	/* Match list view: Enter activates the entire selected set. */
	{
		GtkTreeSelection *sel = gtk_tree_view_get_selection (GTK_TREE_VIEW (col->tree));
		if (gtk_tree_selection_count_selected_rows (sel) > 1) {
			GList *files = miller_get_selection (NEMO_VIEW (view));
			nemo_view_activate_files (NEMO_VIEW (view), files, flags, TRUE);
			nemo_file_list_free (files);
			return;
		}
	}
	/* use the keyboard cursor, not the (walkable) selection */
	gtk_tree_view_get_cursor (GTK_TREE_VIEW (col->tree), &cursor, NULL);
	if (cursor == NULL) {
		return;
	}

	if (gtk_tree_model_get_iter (GTK_TREE_MODEL (col->store), &iter, cursor)) {
		gtk_tree_model_get (GTK_TREE_MODEL (col->store), &iter,
				    COL_FILE, &file, COL_IS_DIR, &is_dir, -1);
	}
	gtk_tree_path_free (cursor);

	if (file != NULL) {
		if (is_dir) {
			GFile *opened = nemo_file_get_location (file);
			g_set_object (&view->details->rename_opened_target, opened);
			g_clear_object (&opened);
			nemo_view_activate_file (NEMO_VIEW (view), file, flags);
		} else {
			GList *files;

			files = g_list_prepend (NULL, nemo_file_ref (file));
			nemo_view_activate_files (NEMO_VIEW (view), files, flags, TRUE);
			nemo_file_list_free (files);
		}
		nemo_file_unref (file);
	}
}

/* select the first or the last row of a column (Home/End) */
static void
miller_select_end_row (MillerCol *col, gboolean first)
{
	GtkTreeIter iter;
	gboolean valid;
	GtkTreePath *path;
	int n;

	if (col == NULL) {
		return;
	}

	if (first) {
		valid = gtk_tree_model_get_iter_first (GTK_TREE_MODEL (col->store),
						       &iter);
	} else {
		n = gtk_tree_model_iter_n_children (GTK_TREE_MODEL (col->store),
						    NULL);
		valid = (n > 0) &&
			gtk_tree_model_iter_nth_child (GTK_TREE_MODEL (col->store),
						       &iter, NULL, n - 1);
	}
	if (!valid) {
		return;
	}

	path = gtk_tree_model_get_path (GTK_TREE_MODEL (col->store), &iter);
	gtk_tree_view_set_cursor (GTK_TREE_VIEW (col->tree), path, NULL, FALSE);
	gtk_tree_view_scroll_to_cell (GTK_TREE_VIEW (col->tree), path, NULL,
				      FALSE, 0.0, 0.0);
	gtk_tree_path_free (path);
}

/* cut items render semi-transparent, like the icon and list views */
static GdkPixbuf *
miller_faded_pixbuf (GdkPixbuf *src)
{
	GdkPixbuf *alpha_src = NULL, *work, *dst;
	int w, h;

	if (src == NULL) {
		return NULL;
	}
	work = src;
	if (!gdk_pixbuf_get_has_alpha (work)) {
		alpha_src = gdk_pixbuf_add_alpha (work, FALSE, 0, 0, 0);
		work = alpha_src;
	}
	w = gdk_pixbuf_get_width (work);
	h = gdk_pixbuf_get_height (work);
	dst = gdk_pixbuf_new (GDK_COLORSPACE_RGB, TRUE, 8, w, h);
	gdk_pixbuf_fill (dst, 0x00000000);
	gdk_pixbuf_composite (work, dst, 0, 0, w, h, 0.0, 0.0, 1.0, 1.0,
			      GDK_INTERP_BILINEAR, 120);
	if (alpha_src != NULL) {
		g_object_unref (alpha_src);
	}
	return dst;
}

static GdkRGBA *
miller_faded_fg (GtkWidget *tree)
{
	GtkStyleContext *ctx;
	GdkRGBA fg, *out;

	ctx = gtk_widget_get_style_context (tree);
	gtk_style_context_get_color (ctx, GTK_STATE_FLAG_NORMAL, &fg);
	out = gdk_rgba_copy (&fg);
	out->alpha *= 0.4;
	return out;
}

static void
miller_apply_cut_fade (NemoMillerColumnsView *view)
{
	NemoClipboardMonitor *monitor;
	NemoClipboardInfo *info;
	GHashTable *cut = NULL;
	GList *l;
	int n_faded = 0;

	monitor = nemo_clipboard_monitor_get ();
	info = nemo_clipboard_monitor_get_clipboard_info (monitor);
	if (info != NULL && info->cut && info->files != NULL) {
		GList *f;

		cut = g_hash_table_new (g_str_hash, g_str_equal);
		for (f = info->files; f != NULL; f = f->next) {
			char *u = nemo_file_get_uri (NEMO_FILE (f->data));

			g_hash_table_insert (cut, u, u);
		}
	}

	for (l = view->details->columns; l != NULL; l = l->next) {
		MillerCol *col = l->data;
		GtkTreeIter iter;
		gboolean valid;
		GdkRGBA *fg;

		fg = miller_faded_fg (col->tree);
		valid = gtk_tree_model_get_iter_first (GTK_TREE_MODEL (col->store),
						       &iter);
		while (valid) {
			NemoFile *file = NULL;
			gboolean is_cut = FALSE;

			gtk_tree_model_get (GTK_TREE_MODEL (col->store), &iter,
					    COL_FILE, &file, -1);
			if (file != NULL) {
				if (cut != NULL) {
					char *u = nemo_file_get_uri (file);

					is_cut = (u != NULL &&
						  g_hash_table_contains (cut, u));
					g_free (u);
				}
				{
					GdkPixbuf *pix, *emblem, *shown_icon, *shown_emblem;

					pix = miller_icon_for_file (view, file, col->icon_size);
					emblem = miller_first_emblem_for_file (file, col->icon_size);
					shown_icon = is_cut ? miller_faded_pixbuf (pix) : pix;
					shown_emblem = is_cut ? miller_faded_pixbuf (emblem) : emblem;
					if (is_cut)
						n_faded++;
					gtk_list_store_set (col->store, &iter,
					                    COL_ICON, shown_icon,
					                    COL_EMBLEM, shown_emblem,
					                    COL_FADED, is_cut ? fg : NULL, -1);
					if (is_cut) {
						g_clear_object (&shown_icon);
						g_clear_object (&shown_emblem);
					}
					g_clear_object (&pix);
					g_clear_object (&emblem);
				}
				nemo_file_unref (file);
			}
			valid = gtk_tree_model_iter_next (GTK_TREE_MODEL (col->store),
							  &iter);
		}
		if (fg != NULL) {
			gdk_rgba_free (fg);
		}
	}
	if (cut != NULL) {
		g_hash_table_destroy (cut);
	}
}

static void
on_clipboard_info (NemoClipboardMonitor *monitor,
		   NemoClipboardInfo *info,
		   gpointer user_data)
{
	miller_apply_cut_fade (NEMO_MILLER_COLUMNS_VIEW (user_data));
}

/* Apply F2 to the exact directory we opened, not to an implicit GTK
 * selection. This is an idle callback because the ancestor row can arrive
 * after asynchronous GIO enumeration. The pending NemoFile owns a ref. */
static gboolean
miller_finish_pending_rename (gpointer data)
{
	NemoMillerColumnsView *view = NEMO_MILLER_COLUMNS_VIEW (data);
	NemoFile *pending;
	GFile *location, *parent;
	GList *l;
	gboolean parent_visible = FALSE;

	view->details->pending_rename_idle_queued = FALSE;
	if (gtk_widget_in_destruction (GTK_WIDGET (view))) return G_SOURCE_REMOVE;
	pending = g_object_get_data (G_OBJECT (view), "miller-pending-rename");
	if (pending == NULL) return G_SOURCE_REMOVE;
	location = nemo_file_get_location (pending);
	parent = location ? g_file_get_parent (location) : NULL;
	/* The parent column can be to the LEFT of the still-open folder.
	 * Do not navigate or close any column before the edit is committed. */
	for (l = view->details->columns; l != NULL; l = l->next) {
		MillerCol *col = l->data;
		if (parent != NULL && col->dir != NULL &&
		    g_file_equal (col->dir, parent)) {
			parent_visible = TRUE;
			break;
		}
	}
	if (!parent_visible) {
		g_object_set_data (G_OBJECT (view), "miller-pending-rename", NULL);
		g_clear_object (&location);
		g_clear_object (&parent);
		return G_SOURCE_REMOVE;
	}
 	/* Wait for both the directory's row and its rename permission info.
	 * The NemoFile ready callback reschedules this if needed. */
	if (!nemo_file_can_rename (pending)) {
		g_clear_object (&location);
		g_clear_object (&parent);
		return G_SOURCE_REMOVE;
	}
	for (l = view->details->columns; l != NULL; l = l->next) {
		MillerCol *col = l->data;
		GtkTreeIter iter;
		GtkTreePath *path;
		NemoFile *target;
		if (col->dir == NULL || !g_file_equal (col->dir, parent) ||
		    !column_find_gfile (col, location, &iter)) continue;
		/* We now have an actual row for the same GFile in the correct
		 * parent column. Only this row may enter inline rename. */
		target = g_object_steal_data (G_OBJECT (view), "miller-pending-rename");
		path = gtk_tree_model_get_path (GTK_TREE_MODEL (col->store), &iter);
		miller_set_action_column (view, col);
		gtk_widget_grab_focus (col->tree);
		gtk_tree_view_set_cursor (GTK_TREE_VIEW (col->tree), path, NULL, FALSE);
		gtk_tree_path_free (path);
		miller_start_renaming_file (NEMO_VIEW (view), target, FALSE);
		nemo_file_unref (target);
		break;
	}
	g_clear_object (&location);
	g_clear_object (&parent);
	return G_SOURCE_REMOVE;
}

static void
miller_queue_pending_rename (NemoMillerColumnsView *view)
{
	if (g_object_get_data (G_OBJECT (view), "miller-pending-rename") == NULL ||
	    view->details->pending_rename_idle_queued) return;
	view->details->pending_rename_idle_queued = TRUE;
	g_idle_add_full (G_PRIORITY_DEFAULT_IDLE,
	                 miller_finish_pending_rename, g_object_ref (view),
	                 g_object_unref);
}

/* inline rename, icon/list-view parity: F2 (or a second click on an
 * already-selected row) edits the name in place */
static void
miller_start_inline_rename (NemoMillerColumnsView *view, MillerCol *col)
{
	GtkTreePath *path = NULL;
	GtkTreeViewColumn *tcol;

	if (col == NULL) {
		return;
	}

	tcol = gtk_tree_view_get_column (GTK_TREE_VIEW (col->tree), 0);
	gtk_tree_view_get_cursor (GTK_TREE_VIEW (col->tree), &path, NULL);
	if (path != NULL && tcol != NULL && view->details->rename_target_file != NULL) {
		/* GTK starts editing synchronously. Restore the default immediately,
		 * so subsequent clicks, holds and drags cannot start a new editor. */
		g_object_set (col->txt, "editable", TRUE, NULL);
		g_object_set_data (G_OBJECT (view), "miller-rename-entry", NULL);
		gtk_tree_view_set_cursor_on_cell (GTK_TREE_VIEW (col->tree),
						  path, tcol, col->txt, TRUE);
		g_object_set (col->txt, "editable", FALSE, NULL);
		{
			GtkWidget *entry = g_object_get_data (G_OBJECT (view), "miller-rename-entry");
			if (entry != NULL && GTK_IS_EDITABLE (entry))
				gtk_editable_select_region (GTK_EDITABLE (entry),
					GPOINTER_TO_INT (g_object_get_data (G_OBJECT (view), "miller-rename-start")),
					GPOINTER_TO_INT (g_object_get_data (G_OBJECT (view), "miller-rename-end")));
			g_object_set_data (G_OBJECT (view), "miller-rename-entry", NULL);
		}
	}
	if (path != NULL) {
		gtk_tree_path_free (path);
	}
}

/* All rename entry points (F2, Shift+F2, right-click Rename and
 * automatic naming after New Folder) must go through NemoView's vfunc. */
static void
miller_start_renaming_file (NemoView *nemo_view, NemoFile *file,
			    gboolean select_all)
{
	NemoMillerColumnsView *view = NEMO_MILLER_COLUMNS_VIEW (nemo_view);
	GFile *location;
	GList *l;

	if (file == NULL || !nemo_file_can_rename (file)) {
		return;
	}
	location = nemo_file_get_location (file);
	/* Keep an opened folder visible while editing its name. A successful
	 * rename will first update the row, THEN animate the right-hand column
	 * closed. Failed or cancelled renames must leave the columns alone. */

	for (l = view->details->columns; l != NULL; l = l->next) {
		MillerCol *col = l->data;
		GtkTreeIter iter;
		GtkTreePath *path;

		if (col->dir == NULL ||
		    !g_file_has_parent (location, col->dir) ||
		    !column_find_gfile (col, location, &iter)) {
			continue;
		}
		/* Save exact identity: asynchronous sorting may move the GTK row
		 * while the user is entering a filename. */
		if (view->details->rename_target_file != NULL)
			nemo_file_unref (view->details->rename_target_file);
		view->details->rename_target_file = nemo_file_ref (file);
		/* Nemo's superclass handles the normal selection bookkeeping. */
		NEMO_VIEW_CLASS (nemo_miller_columns_view_parent_class)->
			start_renaming_file (nemo_view, file, select_all);
		path = gtk_tree_model_get_path (GTK_TREE_MODEL (col->store), &iter);
		miller_set_action_column (view, col);
		gtk_widget_grab_focus (col->tree);
		view->details->rename_select_all = select_all || nemo_file_is_directory (file);
		gtk_tree_view_set_cursor (GTK_TREE_VIEW (col->tree), path, NULL, FALSE);
		gtk_tree_view_scroll_to_cell (GTK_TREE_VIEW (col->tree), path,
					      NULL, FALSE, 0.0, 0.0);
		miller_start_inline_rename (view, col);
		gtk_tree_path_free (path);
		g_object_unref (location);
		return;
	}
	/* A parent row may not have arrived yet from the async enumerator. */
	if (location != NULL) {
		GFile *parent = g_file_get_parent (location);
		gboolean opened = FALSE, parent_visible = FALSE;
		for (l = view->details->columns; l != NULL; l = l->next) {
			MillerCol *c = l->data;
			if (c->dir == NULL) continue;
			if (g_file_equal (c->dir, location)) opened = TRUE;
			if (parent != NULL && g_file_equal (c->dir, parent)) parent_visible = TRUE;
		}
		if (opened && parent_visible) {
			g_object_set_data_full (G_OBJECT (view), "miller-pending-rename",
			                        nemo_file_ref (file), (GDestroyNotify) nemo_file_unref);
			miller_queue_pending_rename (view);
		}
		g_clear_object (&parent);
	}
	g_object_unref (location);
}

static void
on_rename_editing_started (GtkCellRenderer *renderer, GtkCellEditable *editable,
			   const gchar *path_str, gpointer user_data)
{
	NemoMillerColumnsView *view = user_data;
	GtkEntry *entry;
	int start = 0, end = -1;
	if (!GTK_IS_ENTRY (editable)) return;
	/* Defensive against a stray GTK edit event not explicitly requested. */
	if (view->details->rename_target_file == NULL) {
		gtk_cell_editable_editing_done (editable);
		gtk_cell_editable_remove_widget (editable);
		return;
	}
	entry = GTK_ENTRY (editable);
	nemo_view_set_is_renaming (NEMO_VIEW (view), TRUE);
	if (!view->details->rename_select_all) {
		eel_filename_get_rename_region (gtk_entry_get_text (entry),
						&start, &end);
	}
	gtk_editable_select_region (GTK_EDITABLE (entry), start, end);
	/* The entry selects everything when it takes focus right after this
	 * signal; miller_start_inline_rename re-applies the region then. */
	g_object_set_data (G_OBJECT (view), "miller-rename-start", GINT_TO_POINTER (start));
	g_object_set_data (G_OBJECT (view), "miller-rename-end", GINT_TO_POINTER (end));
	g_object_set_data_full (G_OBJECT (view), "miller-rename-entry",
	                        g_object_ref (entry), g_object_unref);
	/* Copy/Cut/Paste inside the name editor must edit text, not files. */
	nemo_clipboard_set_up_editable (GTK_EDITABLE (entry),
		nemo_view_get_ui_manager (NEMO_VIEW (view)), FALSE);
	view->details->rename_select_all = FALSE;
}

static void
on_rename_editing_canceled (GtkCellRendererText *renderer, gpointer user_data)
{
	NemoMillerColumnsView *view = NEMO_MILLER_COLUMNS_VIEW (user_data);
	nemo_view_set_is_renaming (NEMO_VIEW (view), FALSE);
	if (view->details->rename_target_file != NULL) {
		nemo_file_unref (view->details->rename_target_file);
		view->details->rename_target_file = NULL;
	}
}

/* Only the right-hand subtree changes width. GtkPaned's existing layout
 * stays intact: no opacity on windowless scrollers (that caused ghost rows).
 * The operation can be aborted safely if navigation changes the columns. */
static void
miller_rename_collapse_free (gpointer user_data)
{
	MillerRenameCollapse *anim = user_data;
	GtkWidget *tail = NULL;

	if (GTK_IS_PANED (anim->paned)) {
		g_object_set_data (G_OBJECT (anim->paned), "miller-rename-collapsing", NULL);
		if (!anim->finished && !gtk_widget_in_destruction (anim->paned)) {
			tail = gtk_paned_get_child2 (GTK_PANED (anim->paned));
			if (tail != NULL) {
				gtk_paned_set_position (GTK_PANED (anim->paned),
				                        anim->start_position);
				gtk_container_child_set (GTK_CONTAINER (anim->paned), tail,
				                         "shrink", FALSE, NULL);
			}
		}
	}
	g_clear_object (&anim->paned);
	g_clear_object (&anim->parent_location);
	g_clear_object (&anim->old_location);
	g_object_unref (anim->view);
	g_free (anim);
}

static gboolean
miller_rename_collapse_tick (gpointer user_data)
{
	MillerRenameCollapse *anim = user_data;
	NemoMillerColumnsView *view = anim->view;
	GList *l;
	MillerCol *parent = NULL, *opened = NULL;
	gdouble progress, eased;

	if (gtk_widget_in_destruction (GTK_WIDGET (view)) ||
	    !GTK_IS_PANED (anim->paned)) return G_SOURCE_REMOVE;
	for (l = view->details->columns; l != NULL; l = l->next) {
		MillerCol *col = l->data;
		if (col->dir == NULL) continue;
		if (g_file_equal (col->dir, anim->parent_location)) parent = col;
		if (g_file_equal (col->dir, anim->old_location)) opened = col;
	}
	if (parent == NULL || opened == NULL ||
	    gtk_widget_get_parent (parent->scroll) != anim->paned ||
	    gtk_paned_get_child1 (GTK_PANED (anim->paned)) != parent->scroll)
		return G_SOURCE_REMOVE;

	progress = CLAMP ((g_get_monotonic_time () - anim->start_us) / 220000.0, 0.0, 1.0);
	eased = 1.0 - (1.0 - progress) * (1.0 - progress) * (1.0 - progress);
	gtk_paned_set_position (GTK_PANED (anim->paned),
	                       anim->start_position + (gint) ((anim->end_position -
	                       anim->start_position) * eased));
	if (progress < 1.0) return G_SOURCE_CONTINUE;

	/* Mark complete before the navigation/rewire destroys the old pane. */
	anim->finished = TRUE;
	g_object_set_data (G_OBJECT (anim->paned), "miller-rename-collapsing", NULL);
	if (opened->is_chain) {
		NemoFile *parent_file = nemo_file_get (anim->parent_location);
		/* Slot-owned chain: let Nemo navigate back to the parent only NOW. */
		nemo_view_activate_file (NEMO_VIEW (view), parent_file, 0);
		nemo_file_unref (parent_file);
	} else {
		gint seam = g_list_index (view->details->columns, parent) + 1;
		miller_set_action_column (view, parent);
		miller_set_paste_destination (view, parent->dir);
		remove_extension_columns_after (view, parent);
		rewire_tail (view, seam, NULL);
		miller_queue_divider_redraw (view);
		update_col_visibility (view);
		miller_refresh_opened_marks (view);
		rebuild_selection (view);
		nemo_view_notify_selection_changed (NEMO_VIEW (view));
	}
	return G_SOURCE_REMOVE;
}

static void
miller_start_rename_collapse (NemoMillerColumnsView *view,
                              GFile *parent_location, GFile *old_location)
{
	MillerCol *parent = NULL, *opened = NULL;
	GtkWidget *pane, *tail;
	MillerRenameCollapse *anim;
	GList *l;

	for (l = view->details->columns; l != NULL; l = l->next) {
		MillerCol *col = l->data;
		if (col->dir == NULL) continue;
		if (g_file_equal (col->dir, parent_location)) parent = col;
		if (g_file_equal (col->dir, old_location)) opened = col;
	}
	if (parent == NULL || opened == NULL) return; /* already navigated away */
	pane = gtk_widget_get_parent (parent->scroll);
	if (!GTK_IS_PANED (pane) ||
	    gtk_paned_get_child1 (GTK_PANED (pane)) != parent->scroll ||
	    g_object_get_data (G_OBJECT (pane), "miller-rename-collapsing") != NULL)
		return;
	tail = gtk_paned_get_child2 (GTK_PANED (pane));
	if (tail == NULL) return;
	anim = g_new0 (MillerRenameCollapse, 1);
	anim->view = g_object_ref (view);
	anim->parent_location = g_object_ref (parent_location);
	anim->old_location = g_object_ref (old_location);
	anim->paned = g_object_ref (pane);
	anim->start_position = gtk_paned_get_position (GTK_PANED (pane));
	anim->end_position = MAX (anim->start_position,
	                          gtk_widget_get_allocated_width (pane) - 1);
	anim->start_us = g_get_monotonic_time ();
	g_object_set_data (G_OBJECT (pane), "miller-rename-collapsing", GINT_TO_POINTER (1));
	/* This is a temporary animation only; don't overwrite saved widths. */
	gtk_container_child_set (GTK_CONTAINER (pane), tail, "shrink", TRUE, NULL);
	g_timeout_add_full (G_PRIORITY_DEFAULT, 16, miller_rename_collapse_tick,
	                    anim, miller_rename_collapse_free);
}

static void
on_rename_done (NemoFile *file, GFile *result_location, GError *error,
                gpointer callback_data)
{
	MillerRenameJob *job = callback_data;
	NemoMillerColumnsView *view = job->view;

	if (error != NULL) {
		g_warning ("Miller columns: rename failed: %s", error->message);
	} else if (!gtk_widget_in_destruction (GTK_WIDGET (view))) {
		GFile *new_location = nemo_file_get_location (file);
		GList *l;

		/* Refresh the exact old row immediately: event-driven file_changed
		 * may not find it once the NemoFile's GFile location has changed. */
		for (l = view->details->columns; l != NULL; l = l->next) {
			MillerCol *col = l->data;
			GtkTreeIter iter, dupe;
			char *name;
			if (col->dir == NULL ||
			    !g_file_equal (col->dir, job->parent_location) ||
			    !column_find_gfile (col, job->old_location, &iter)) continue;
			if (new_location != NULL &&
			    column_find_gfile (col, new_location, &dupe) &&
			    !g_file_equal (new_location, job->old_location)) {
				/* A monitor may have inserted the new row already. */
				gtk_list_store_remove (col->store, &iter);
			} else {
				name = nemo_file_get_display_name (file);
				gtk_list_store_set (col->store, &iter,
				                    COL_GFILE, new_location,
				                    COL_FILE, file,
				                    COL_NAME, name,
				                    COL_OPENED, job->was_opened, -1);
				g_free (name);
			}
			gtk_widget_queue_draw (col->tree);
			break;
		}
		if (job->was_opened) {
			if (view->details->rename_opened_target != NULL &&
			    g_file_equal (view->details->rename_opened_target, job->old_location))
				g_clear_object (&view->details->rename_opened_target);
			miller_start_rename_collapse (view, job->parent_location, job->old_location);
		}
		g_clear_object (&new_location);
	}
	g_object_unref (job->old_location);
	g_object_unref (job->parent_location);
	g_object_unref (job->view);
	g_free (job);
}

static void
on_name_edited (GtkCellRendererText *renderer,
		char *path_str, char *new_text,
		gpointer user_data)
{
	NemoMillerColumnsView *view = user_data;
	MillerCol *col = NULL;
	GList *l;
	GtkTreePath *path;
	GtkTreeIter iter;
	NemoFile *file = NULL;
	char *old_name = NULL;

	for (l = view->details->columns; l != NULL && col == NULL; l = l->next) {
		MillerCol *c = l->data;

		if (c->txt == GTK_CELL_RENDERER (renderer)) {
			col = c;
		}
	}
	nemo_view_set_is_renaming (NEMO_VIEW (view), FALSE);
	if (col == NULL || new_text == NULL) {
		if (view->details->rename_target_file != NULL) {
			nemo_file_unref (view->details->rename_target_file);
			view->details->rename_target_file = NULL;
		}
		return;
	}

	path = gtk_tree_path_new_from_string (path_str);
	if (path != NULL &&
	    gtk_tree_model_get_iter (GTK_TREE_MODEL (col->store), &iter, path)) {
		gtk_tree_model_get (GTK_TREE_MODEL (col->store), &iter,
				    COL_FILE, &file, COL_NAME, &old_name, -1);
	}
	if (path != NULL) {
		gtk_tree_path_free (path);
	}

	if (file != NULL && old_name != NULL &&
	    strlen (new_text) > 0 && strcmp (new_text, old_name) != 0) {
		GFile *actual = nemo_file_get_location (file);
		/* Never rename a different file than the row in this column,
		 * especially after a navigation/async refresh. */
		if (actual != NULL && col->dir != NULL &&
		    view->details->rename_target_file == file &&
		    g_file_has_parent (actual, col->dir) &&
		    column_find_gfile (col, actual, &iter)) {
			MillerRenameJob *job = g_new0 (MillerRenameJob, 1);
			GList *node;
			job->view = g_object_ref (view);
			job->old_location = g_object_ref (actual);
			job->parent_location = g_object_ref (col->dir);
			for (node = view->details->columns; node != NULL; node = node->next) {
				MillerCol *opened = node->data;
				if (opened->dir != NULL && g_file_equal (opened->dir, actual)) {
					job->was_opened = TRUE;
					break;
				}
			}
			nemo_file_rename (file, new_text, on_rename_done, job);
		} else {
			g_warning ("Miller: blocked a stale or ambiguous rename target");
		}
		g_clear_object (&actual);
	}
	if (file != NULL) {
		nemo_file_unref (file);
	}
	if (view->details->rename_target_file != NULL) {
		nemo_file_unref (view->details->rename_target_file);
		view->details->rename_target_file = NULL;
	}
	g_free (old_name);
}

/* ------------------------------------------------------------------ */
/* drag & drop                                                          */
/* ------------------------------------------------------------------ */

/* iterate the SOURCE column's selection for nemo_drag_drag_data_get */
static void
miller_each_selected_binder (NemoDragEachSelectedItemDataGet iteratee,
			     gpointer iterator_context, gpointer data)
{
	MillerCol *col = iterator_context;
	GtkTreeSelection *sel;
	GtkTreeViewColumn *tcol;
	GList *rows, *l;

	if (col == NULL) {
		return;
	}

	sel = gtk_tree_view_get_selection (GTK_TREE_VIEW (col->tree));
	tcol = gtk_tree_view_get_column (GTK_TREE_VIEW (col->tree), 0);
	rows = gtk_tree_selection_get_selected_rows (sel, NULL);
	for (l = rows; l != NULL; l = l->next) {
		GtkTreeIter it;
		NemoFile *file = NULL;

		if (!gtk_tree_model_get_iter (GTK_TREE_MODEL (col->store),
					      &it, l->data)) {
			continue;
		}
		gtk_tree_model_get (GTK_TREE_MODEL (col->store), &it,
				    COL_FILE, &file, -1);
		if (file != NULL) {
			GdkRectangle area = {0, 0, 0, 0};
			char *uri;

			gtk_tree_view_get_cell_area (GTK_TREE_VIEW (col->tree),
						     l->data, tcol, &area);
			/* Export only an explicitly selected *local child* of
			 * this column, never an implicitly opened ancestor/root. */
			GFile *location = nemo_file_get_location (file);
			GFile *parent = location ? g_file_get_parent (location) : NULL;
			if (location != NULL &&
			    (col->dir == NULL ||
			     (parent != NULL && g_file_equal (parent, col->dir)) ||
			     !g_file_is_native (col->dir))) {
				uri = nemo_file_get_uri (file);
				iteratee (uri, NULL, 0, area.y, area.width, area.height,
				          data);
				g_free (uri);
			}
			g_clear_object (&parent);
			g_clear_object (&location);
			nemo_file_unref (file);
		}
	}
	g_list_free_full (rows, (GDestroyNotify) gtk_tree_path_free);
}

/* The dragged rows are captured at drag-begin. Dwelling on a folder
 * (spring-loaded open) changes the column selection during the drag, and
 * GTK only asks for the data at drop time: without the snapshot the drop
 * would carry the hovered folder instead of the dragged files. */
typedef struct {
	char *uri;
	int y, w, h;
} MillerDragItem;

static void
miller_drag_item_free (gpointer data)
{
	MillerDragItem *item = data;
	g_free (item->uri);
	g_free (item);
}

static void
miller_drag_collect (const char *uri, const char *path_str,
		     int x, int y, int w, int h, gpointer data)
{
	GList **list = data;
	MillerDragItem *item = g_new0 (MillerDragItem, 1);
	item->uri = g_strdup (uri);
	item->y = y;
	item->w = w;
	item->h = h;
	*list = g_list_prepend (*list, item);
}

static void
miller_each_snapshot_item (NemoDragEachSelectedItemDataGet iteratee,
			   gpointer iterator_context, gpointer data)
{
	GList *l;

	for (l = iterator_context; l != NULL; l = l->next) {
		MillerDragItem *item = l->data;
		iteratee (item->uri, NULL, 0, item->y, item->w, item->h, data);
	}
}

static void
on_tree_drag_begin (GtkWidget *widget, GdkDragContext *context,
		    gpointer user_data)
{
	NemoMillerColumnsView *view = user_data;
	MillerCol *col = find_column_by_tree (view, widget);
	GList *items = NULL;

	g_list_free_full (view->details->drag_items, miller_drag_item_free);
	view->details->drag_items = NULL;
	miller_rename_timer_cancel (view);
	if (col == NULL) {
		return;
	}
	miller_each_selected_binder (miller_drag_collect, col, &items);
	view->details->drag_items = g_list_reverse (items);
}

static void
on_tree_drag_end (GtkWidget *widget, GdkDragContext *context,
		  gpointer user_data)
{
	NemoMillerColumnsView *view = user_data;

	miller_resume_rubber_band (view);
	miller_unfreeze_selection (view, NULL);
	g_list_free_full (view->details->drag_items, miller_drag_item_free);
	view->details->drag_items = NULL;
}

static void
on_tree_drag_data_get (GtkWidget *widget, GdkDragContext *context,
		       GtkSelectionData *selection_data,
		       guint info, guint time, gpointer user_data)
{
	NemoMillerColumnsView *view = user_data;
	MillerCol *col = find_column_by_tree (view, widget);

	if (view->details->drag_items != NULL) {
		nemo_drag_drag_data_get (widget, context, selection_data, info, time,
					 view->details->drag_items,
					 miller_each_snapshot_item);
		return;
	}
	if (col == NULL) {
		return;
	}

	nemo_drag_drag_data_get (widget, context, selection_data, info, time,
				 col, miller_each_selected_binder);
}

static char *
miller_get_root_uri_cb (NemoTreeViewDragDest *dest, gpointer user_data)
{
	NemoMillerColumnsView *view = user_data;
	MillerCol *col = NULL;
	GList *l;

	/* the dest emits this for drops on blank space, i.e. into the
	 * column's own directory */
	for (l = view->details->columns; l != NULL; l = l->next) {
		MillerCol *c = l->data;

		if (c->drag_dest == dest) {
			col = c;
			break;
		}
	}
	if (col == NULL || col->dir == NULL) {
		return NULL;
	}
	return g_file_get_uri (col->dir);
}

static NemoFile *
miller_get_file_for_path_cb (NemoTreeViewDragDest *dest, GtkTreePath *path,
			     gpointer user_data)
{
	NemoMillerColumnsView *view = user_data;
	MillerCol *col = NULL;
	GList *l;
	GtkTreeIter it;
	NemoFile *file = NULL;

	for (l = view->details->columns; l != NULL; l = l->next) {
		MillerCol *c = l->data;

		if (c->drag_dest == dest) {
			col = c;
			break;
		}
	}
	if (col == NULL || path == NULL ||
	    !gtk_tree_model_get_iter (GTK_TREE_MODEL (col->store), &it, path)) {
		return NULL;
	}
	gtk_tree_model_get (GTK_TREE_MODEL (col->store), &it,
			    COL_FILE, &file, -1);
	return file;
}

static void
miller_move_copy_cb (NemoTreeViewDragDest *dest,
		     const GList *item_uris,
		     const char *target_uri,
		     guint action, int x, int y,
		     gpointer user_data)
{
	NemoView *view = user_data;

	/* Same route as List/Icon view: NemoView decides copy/move/link and
	 * nemo-file-operations handles conflicts, undo and progress. */
	nemo_clipboard_clear_if_colliding_uris (GTK_WIDGET (view), item_uris,
					       nemo_view_get_copied_files_atom (view));
	nemo_view_move_copy_items (view, item_uris, NULL, target_uri,
				   action, x, y);
	miller_schedule_heals (NEMO_MILLER_COLUMNS_VIEW (view));
}

static void
miller_handle_uri_list_cb (NemoTreeViewDragDest *dest, const char *item_uris,
			   const char *target_uri, GdkDragAction action,
			   int x, int y, gpointer user_data)
{
	nemo_view_handle_uri_list_drop (NEMO_VIEW (user_data), item_uris,
					target_uri, action, x, y);
}

static void
miller_handle_netscape_url_cb (NemoTreeViewDragDest *dest,
			       const char *encoded_url,
			       const char *target_uri, GdkDragAction action,
			       int x, int y, gpointer user_data)
{
	nemo_view_handle_netscape_url_drop (NEMO_VIEW (user_data), encoded_url,
					    target_uri, action, x, y);
}

static void
miller_handle_text_cb (NemoTreeViewDragDest *dest, const char *text,
		       const char *target_uri, GdkDragAction action,
		       int x, int y, gpointer user_data)
{
	nemo_view_handle_text_drop (NEMO_VIEW (user_data), text,
				    target_uri, action, x, y);
}

static gboolean
on_tree_popup_menu (GtkWidget *widget, gpointer user_data)
{
	NemoMillerColumnsView *view = user_data;
	MillerCol *col = find_column_by_tree (view, widget);
	GtkTreeSelection *sel;

	if (col == NULL)
		return FALSE;

	/* Menu / Shift+F10 routes commands to the keyboard-focused column,
	 * including background actions and Paste. Selection must be an actual
	 * GtkTreeSelection in that same column, not an opened breadcrumb. */
	miller_set_action_column (view, col);
	miller_set_paste_destination (view, col->dir);
	sel = gtk_tree_view_get_selection (GTK_TREE_VIEW (widget));
	if (gtk_tree_selection_count_selected_rows (sel) > 0) {
		nemo_view_pop_up_selection_context_menu (NEMO_VIEW (view), NULL);
	} else {
		miller_clear_selection_for_background_menu (view);
		nemo_view_pop_up_background_context_menu (NEMO_VIEW (view), NULL);
	}
	return TRUE;
}

static gboolean
on_tree_query_tooltip (GtkWidget *widget, gint x, gint y,
		       gboolean kb_mode, GtkTooltip *tooltip,
		       gpointer user_data)
{
	GtkTreeIter iter;
	GtkTreePath *path = NULL;
	GtkTreeModel *model;
	NemoFile *file = NULL;
	char *text;
	gboolean ret = FALSE;

	if (!gtk_tree_view_get_tooltip_context (GTK_TREE_VIEW (widget), &x, &y,
						kb_mode, &model, &path,
						&iter)) {
		return FALSE;
	}
	gtk_tree_model_get (model, &iter, COL_FILE, &file, -1);
	if (file != NULL) {
		text = nemo_file_construct_tooltip (file,
						    nemo_global_preferences_get_tooltip_flags (),
						    NULL);
		{
			/* The name may be ellipsized at the 30-character cap:
			 * the tooltip always starts with the full name. */
			char *display_name = nemo_file_get_display_name (file);
			char *escaped = g_markup_escape_text (display_name != NULL ?
							      display_name : "", -1);
			if (text == NULL || text[0] == '\0') {
				g_free (text);
				text = g_strdup (escaped);
			} else if (strstr (text, escaped) == NULL) {
				char *joined = g_strdup_printf ("<b>%s</b>\n%s", escaped, text);
				g_free (text);
				text = joined;
			}
			g_free (escaped);
			g_free (display_name);
		}
		if (text[0] != '\0') {
			gtk_tooltip_set_markup (tooltip, text);
			gtk_tree_view_set_tooltip_cell (GTK_TREE_VIEW (widget), tooltip,
						path, NULL, NULL);
			ret = TRUE;
		}
		g_free (text);
		nemo_file_unref (file);
	}
	gtk_tree_path_free (path);
	return ret;
}

/* Any key but a lone modifier makes the focused column the action
 * column and, unless a just-opened folder holds it, the Paste target. */
static void
miller_key_retarget (NemoMillerColumnsView *view, GtkWidget *widget, GdkEventKey *event)
{
	MillerCol *focused = find_column_by_tree (view, widget);
	GList *node = focused != NULL ?
		g_list_find (view->details->columns, focused) : NULL;
	MillerCol *opened = node != NULL && node->next != NULL ?
		node->next->data : NULL;
	/* Pressing only a modifier (Ctrl of Ctrl+V, Shift, ...) is not a
	 * keyboard action and must not retarget the pending Paste. */
	gboolean modifier_only = event->is_modifier;
	/* A folder that was just clicked in this column is open in the
	 * next column: it stays the Paste destination, exactly as
	 * the mouse path decided. */
	gboolean keep_opened = opened != NULL && opened->dir != NULL &&
		view->details->paste_destination != NULL &&
		g_file_equal (view->details->paste_destination, opened->dir);
	if (!modifier_only)
		miller_set_action_column (view, focused);
	/* A keyboard action must target the focused column, never the
	 * last folder reached by an unrelated mouse click. This is a
	 * destination only, NOT a selected item. */
	if (focused != NULL && !modifier_only && !keep_opened)
		miller_set_paste_destination (view, focused->dir);
}

/* Return or Space on a group heading folds the group. TRUE if it was one. */
static gboolean
miller_key_toggle_group (NemoMillerColumnsView *view, GtkWidget *widget)
{
    MillerCol *col = find_column_by_tree (view, widget);
    GtkTreePath *path = NULL;
    GtkTreeIter iter;
    gboolean header = FALSE;
    gchar *key = NULL;
    if (col != NULL) gtk_tree_view_get_cursor (GTK_TREE_VIEW (widget), &path, NULL);
    if (path != NULL && gtk_tree_model_get_iter (GTK_TREE_MODEL (col->store), &iter, path))
        gtk_tree_model_get (GTK_TREE_MODEL (col->store), &iter,
                            COL_GROUP_HEADER, &header, COL_GROUP_KEY, &key, -1);
    if (path) gtk_tree_path_free (path);
    if (header && key) miller_group_toggle (view, col, key);
    g_free (key);
    return header;
}

/* Left: focus the previous column and make it the target. */
static void
miller_key_focus_previous (NemoMillerColumnsView *view, GtkWidget *widget)
{
	MillerCol *current = find_column_by_tree (view, widget);
	GList *node = g_list_find (view->details->columns, current);
	if (node && node->prev) {
		MillerCol *previous = node->prev->data;
		gtk_widget_grab_focus (previous->tree);
		miller_set_action_column (view, previous);
		miller_set_paste_destination (view, previous->dir);
	}
}

/* Right on a folder: focus its column when it is already open next door,
 * open it in place in the chain, or open it as a new column. */
static void
miller_key_open_selected (NemoMillerColumnsView *view, GtkWidget *widget)
{
	MillerCol *col;
	GtkTreeSelection *sel;
	GList *rows;
	GtkTreeIter iter;
	NemoFile *file = NULL;
	gboolean is_dir = FALSE;

	col = find_column_by_tree (view, widget);
	if (col != NULL) {
		sel = gtk_tree_view_get_selection (GTK_TREE_VIEW (col->tree));
		rows = gtk_tree_selection_get_selected_rows (sel, NULL);
		if (rows != NULL &&
		    gtk_tree_model_get_iter (GTK_TREE_MODEL (col->store), &iter, rows->data)) {
			gtk_tree_model_get (GTK_TREE_MODEL (col->store), &iter,
					    COL_FILE, &file, COL_IS_DIR, &is_dir, -1);
		}
		g_list_free_full (rows, (GDestroyNotify) gtk_tree_path_free);
		if (file != NULL && is_dir) {
			GList *node = g_list_find (view->details->columns, col);
			MillerCol *following = node && node->next ? node->next->data : NULL;
			GFile *location = nemo_file_get_location (file);
			if (following != NULL && following->dir != NULL &&
			    g_file_equal (following->dir, location)) {
				gtk_widget_grab_focus (following->tree);
				miller_set_action_column (view, following);
				miller_set_paste_destination (view, following->dir);
			} else if (col->is_chain) {
				nemo_view_activate_file (NEMO_VIEW (view), file, 0);
			} else {
				handle_dir_selected (view, col, location, file);
				following = g_list_last (view->details->columns)->data;
				gtk_widget_grab_focus (following->tree);
				miller_set_action_column (view, following);
				miller_set_paste_destination (view, following->dir);
			}
			g_object_unref (location);
		}
		if (file != NULL) {
			nemo_file_unref (file);
		}
	}
}

/* Ctrl shortcuts: zoom (icon-view parity) and copy/cut/paste/select all,
 * through the same actions as the context menu since the accelerator
 * groups do not fire for this view. Returns FALSE for other keys;
 * otherwise *result is the handler's return value. */
static gboolean
miller_key_ctrl_shortcut (NemoMillerColumnsView *view, GdkEventKey *event, gboolean *result)
{
	switch (event->keyval) {
	case GDK_KEY_plus:
	case GDK_KEY_equal:
	case GDK_KEY_KP_Add:
		miller_bump_zoom_level (NEMO_VIEW (view), 1);
		*result = TRUE;
		return TRUE;
	case GDK_KEY_minus:
	case GDK_KEY_KP_Subtract:
		miller_bump_zoom_level (NEMO_VIEW (view), -1);
		*result = TRUE;
		return TRUE;
	case GDK_KEY_0:
	case GDK_KEY_KP_0:
		miller_restore_default_zoom_level (NEMO_VIEW (view));
		*result = TRUE;
		return TRUE;
	case GDK_KEY_c:
	case GDK_KEY_C:
		*result = miller_activate_action (view, NEMO_ACTION_COPY);
		return TRUE;
	case GDK_KEY_x:
	case GDK_KEY_X:
		*result = miller_activate_action (view, NEMO_ACTION_CUT);
		return TRUE;
	case GDK_KEY_v:
	case GDK_KEY_V:
		*result = miller_activate_action (view, NEMO_ACTION_PASTE);
		return TRUE;
	case GDK_KEY_a:
	case GDK_KEY_A:
		*result = miller_activate_action (view, NEMO_ACTION_SELECT_ALL);
		return TRUE;
	default:
		return FALSE;
	}
}

/* F2 / Shift+F2. */
static gboolean
miller_key_rename (NemoMillerColumnsView *view, GdkEventKey *event)
{
	if (view->details->selection == NULL) {
		MillerCol *current = last_chain_column (view);
		/* F2 after clicking into a folder: the GTK selection has
		 * correctly been cleared to prevent destructive root actions.
		 * Renaming is an explicit, isolated exception. */
		if (current != NULL && current->dir != NULL &&
		    view->details->rename_opened_target != NULL &&
		    g_file_equal (current->dir, view->details->rename_opened_target)) {
			GFile *parent = g_file_get_parent (current->dir);
			if (parent != NULL) {
				NemoFile *opened = nemo_file_get (current->dir);
				g_clear_object (&view->details->rename_opened_target);
				miller_start_renaming_file (NEMO_VIEW (view), opened,
				                           !!(event->state & GDK_SHIFT_MASK));
				nemo_file_unref (opened);
				g_object_unref (parent);
				return TRUE;
			}
		}
	}
	return miller_activate_action (view,
		(event->state & GDK_SHIFT_MASK) ?
		"RenameSelectAll" : NEMO_ACTION_RENAME);
}

static gboolean
on_tree_key_press (GtkWidget *widget, GdkEventKey *event,
		   gpointer user_data)
{
	NemoMillerColumnsView *view = user_data;
	gboolean result;

	miller_rename_timer_cancel (view);
	g_object_set_data (G_OBJECT (view),
	                   "miller-pending-navigation-selection", NULL);
	miller_key_retarget (view, widget, event);
	if (nemo_view_get_is_renaming (NEMO_VIEW (view))) return FALSE;
	if ((event->keyval == GDK_KEY_Return || event->keyval == GDK_KEY_KP_Enter ||
	     event->keyval == GDK_KEY_space) &&
	    miller_key_toggle_group (view, widget))
		return TRUE;

	/* Handle Menu and Shift+F10 explicitly. Depending on the GTK theme
	 * and parent accelerator group the default popup-menu binding may
	 * otherwise be swallowed before the tree gets its selection menu. */
	if (event->keyval == GDK_KEY_Menu ||
	    (event->keyval == GDK_KEY_F10 &&
	     (event->state & GDK_SHIFT_MASK) &&
	     !(event->state & (GDK_CONTROL_MASK | GDK_MOD1_MASK))))
		return on_tree_popup_menu (widget, view);

	if (event->keyval == GDK_KEY_BackSpace) {
		navigate_to_parent (view);
		return TRUE;
	}
	if (event->keyval == GDK_KEY_Left && !(event->state & (GDK_MOD1_MASK | GDK_CONTROL_MASK))) {
		miller_key_focus_previous (view, widget);
		return TRUE;
	}

	if (event->keyval == GDK_KEY_Return || event->keyval == GDK_KEY_KP_Enter) {
		/* explicit activation of the current selection;
		 * Shift+Enter opens in a new window (icon-view parity) */
		activate_selected_in_column (view, find_column_by_tree (view, widget),
					     (event->state & GDK_SHIFT_MASK) ?
					     NEMO_WINDOW_OPEN_FLAG_NEW_WINDOW : 0);
		return TRUE;
	}

	if (!(event->state & GDK_SHIFT_MASK) &&
	    (event->keyval == GDK_KEY_Home || event->keyval == GDK_KEY_KP_Home)) {
		miller_select_end_row (find_column_by_tree (view, widget), TRUE);
		return TRUE;
	}

	if (!(event->state & GDK_SHIFT_MASK) &&
	    (event->keyval == GDK_KEY_End || event->keyval == GDK_KEY_KP_End)) {
		miller_select_end_row (find_column_by_tree (view, widget), FALSE);
		return TRUE;
	}

	if (event->keyval == GDK_KEY_Right && !(event->state & (GDK_MOD1_MASK | GDK_CONTROL_MASK))) {
		miller_key_open_selected (view, widget);
		return TRUE;
	}

	if ((event->state & GDK_CONTROL_MASK) != 0 &&
	    miller_key_ctrl_shortcut (view, event, &result))
		return result;

	if (event->keyval == GDK_KEY_Delete || event->keyval == GDK_KEY_KP_Delete) {
		gboolean swap = g_settings_get_boolean (nemo_preferences,
					NEMO_PREFERENCES_SWAP_TRASH_DELETE);
		gboolean permanent = !!(event->state & GDK_SHIFT_MASK) != swap;
		gboolean ok = miller_activate_action (view, permanent ?
					NEMO_ACTION_DELETE : NEMO_ACTION_TRASH);

		if (ok) {
			/* trash moves complete asynchronously and the
			 * model may swallow the removal event */
			miller_schedule_heals (view);
		}
		return ok;
	}

	/* Space (Quick Look) is handled by the window for every view. */

	if (event->keyval == GDK_KEY_F2)
		return miller_key_rename (view, event);

	return FALSE;
}

/* find and activate one of the view's actions (the same objects the
 * context menu drives): they can live in any action group */
static gboolean
miller_activate_action (NemoMillerColumnsView *view, const char *name)
{
	GtkUIManager *ui;
	GList *groups;
	GtkAction *action = NULL;

	nemo_view_update_actions_and_extensions (NEMO_VIEW (view));
	ui = nemo_view_get_ui_manager (NEMO_VIEW (view));
	if (ui == NULL) {
		return FALSE;
	}
	for (groups = gtk_ui_manager_get_action_groups (ui);
	     groups != NULL && action == NULL;
	     groups = groups->next) {
		action = gtk_action_group_get_action (GTK_ACTION_GROUP (groups->data),
						      name);
	}
	if (action == NULL || !gtk_action_is_sensitive (action)) {
		return FALSE;
	}
	gtk_action_activate (action);
	return TRUE;
}

/* Persist widths only when a divider is actually manipulated, not while
 * rebuilding GtkPaneds. GSettings is shared by all Nemo windows. */
static void
miller_save_widths (NemoMillerColumnsView *view)
{
	GVariantBuilder builder;
	guint i;
	g_variant_builder_init (&builder, G_VARIANT_TYPE ("ai"));
	for (i = 0; i < view->details->col_widths->len && i < 32; i++) {
		int width = g_array_index (view->details->col_widths, int, i);
		g_variant_builder_add (&builder, "i", CLAMP (width, 120, 800));
	}
	/* GSettings emits changed synchronously. Without a guard the callback
	 * was rebuilding the complete GtkPaned hierarchy while a divider
	 * was still processing button-release-event (confirmed SIGSEGV). */
	view->details->saving_widths = TRUE;
	g_settings_set_value (nemo_preferences, NEMO_PREFERENCES_MILLER_COLUMN_WIDTHS,
	                      g_variant_builder_end (&builder));
	view->details->saving_widths = FALSE;
}

static void
miller_preferences_changed (GSettings *settings, gchar *key, gpointer data)
{
	NemoMillerColumnsView *view = data;
    if (g_str_equal (key, NEMO_PREFERENCES_MILLER_AUTO_FIT)) {
        GList *l;
        view->details->auto_fit = g_settings_get_boolean (settings, key);
        for (l = view->details->columns; l != NULL; l = l->next) {
            MillerCol *col = l->data;
            col->manual_width = 0;
            if (!view->details->auto_fit)
                gtk_widget_set_size_request (col->scroll, 112, -1);
        }
        if (view->details->auto_fit)
            miller_refit_all (view);
        else
            relayout_columns (view);
        return;
    }
    if (g_str_equal (key, "miller-prefetch-navigation") &&
        !g_settings_get_boolean (settings, key)) {
        if (view->details->prefetch_cancel != NULL)
            g_cancellable_cancel (view->details->prefetch_cancel);
        g_clear_object (&view->details->prefetch_cancel);
        g_clear_object (&view->details->prefetch_dir);
        g_clear_pointer (&view->details->prefetch_items, g_ptr_array_unref);
    }
    if (g_str_equal (key, NEMO_PREFERENCES_MILLER_COLUMN_WIDTH)) {
        /* Apply the new default to *unsaved* column positions immediately.
         * Explicitly resized positions keep their stored sizes. Do not
         * reconstruct GtkPaned or touch an actively dragged handle. */
        GVariant *saved = g_settings_get_value (settings,
                                               NEMO_PREFERENCES_MILLER_COLUMN_WIDTHS);
        gsize saved_count = g_variant_n_children (saved);
        GList *node;
        guint n;
        gboolean dragging = FALSE;
        view->details->default_width = g_settings_get_int (settings,
                                               NEMO_PREFERENCES_MILLER_COLUMN_WIDTH);
        for (node = view->details->columns; node; node = node->next) {
            MillerCol *col = node->data;
            GtkWidget *parent = gtk_widget_get_parent (col->scroll);
            if (GTK_IS_PANED (parent) &&
                g_object_get_data (G_OBJECT (parent), "miller-user-resizing")) {
                dragging = TRUE;
                break;
            }
        }
        if (!dragging && !view->details->saving_widths &&
            !gtk_widget_in_destruction (GTK_WIDGET (view))) {
            view->details->restoring_widths = TRUE;
            for (node = view->details->columns, n = 0; node; node = node->next, n++) {
                MillerCol *col = node->data;
                GtkWidget *parent = gtk_widget_get_parent (col->scroll);
                if (n < saved_count) continue;
                while (view->details->col_widths->len <= n) {
                    gint width = view->details->default_width;
                    g_array_append_val (view->details->col_widths, width);
                }
                g_array_index (view->details->col_widths, gint, n) =
                    view->details->default_width;
                if (GTK_IS_PANED (parent) &&
                    gtk_paned_get_child1 (GTK_PANED (parent)) == col->scroll) {
                    gtk_widget_set_size_request (col->scroll, 112, -1);
                    gtk_paned_set_position (GTK_PANED (parent),
                                            view->details->default_width);
                }
            }
            view->details->restoring_widths = FALSE;
            if (GTK_IS_WIDGET (view->details->paned_root))
                gtk_widget_queue_resize (view->details->paned_root);
        }
        g_variant_unref (saved);
    }
	if (g_str_equal (key, NEMO_PREFERENCES_MILLER_COLUMN_WIDTHS)) {
		/* The current view already has these widths. Do not mutate GTK's
		 * widget tree during an active divider button-release callback. */
		if (view->details->saving_widths ||
		    gtk_widget_in_destruction (GTK_WIDGET (view)))
			return;
		GVariant *saved = g_settings_get_value (settings, NEMO_PREFERENCES_MILLER_COLUMN_WIDTHS);
		GVariantIter iter;
		gint32 w;
		GList *node;
		guint n = 0;
		view->details->restoring_widths = TRUE;
		g_array_set_size (view->details->col_widths, 0);
		g_variant_iter_init (&iter, saved);
		while (n++ < 32 && g_variant_iter_next (&iter, "i", &w)) {
			int width = CLAMP (w, 120, 800);
			g_array_append_val (view->details->col_widths, width);
		}
		for (node = view->details->columns, n = 0; node; node = node->next, n++) {
			MillerCol *col = node->data;
			GtkWidget *parent = gtk_widget_get_parent (col->scroll);
			int width = n < view->details->col_widths->len ?
				g_array_index (view->details->col_widths, int, n) : view->details->default_width;
			/* Each column except the last is child1 of its own handle.
			 * Update that handle in place, with signals suppressed, so tabs
			 * follow an external settings change without a dangerous rebuild. */
			if (GTK_IS_PANED (parent) &&
			    gtk_paned_get_child1 (GTK_PANED (parent)) == col->scroll) {
				gtk_widget_set_size_request (col->scroll, 112, -1);
				gtk_paned_set_position (GTK_PANED (parent), CLAMP (width, 120, 800));
			}
		}
		g_variant_unref (saved);
		view->details->restoring_widths = FALSE;
		if (GTK_IS_WIDGET (view->details->paned_root))
			gtk_widget_queue_resize (view->details->paned_root);
	}
}

static gboolean
miller_columns_scroll (GtkWidget *widget, GdkEventScroll *event, gpointer data)
{
	NemoMillerColumnsView *view = data;
	GtkAdjustment *adjustment;
	gdouble delta = 0.0, value;
	if (!g_settings_get_boolean (nemo_preferences, NEMO_PREFERENCES_MILLER_WHEEL_HORIZONTAL))
		return FALSE;
	if (event->direction == GDK_SCROLL_SMOOTH) delta = event->delta_x * 60.0;
	else if (event->direction == GDK_SCROLL_LEFT) delta = -60.0;
	else if (event->direction == GDK_SCROLL_RIGHT) delta = 60.0;
	if (delta == 0.0) return FALSE; /* preserve ordinary vertical scrolling */
	adjustment = gtk_scrolled_window_get_hadjustment (
		GTK_SCROLLED_WINDOW (view->details->columns_scrolled));
	value = CLAMP (gtk_adjustment_get_value (adjustment) + delta,
	               gtk_adjustment_get_lower (adjustment),
	               MAX (gtk_adjustment_get_lower (adjustment),
	                    gtk_adjustment_get_upper (adjustment) - gtk_adjustment_get_page_size (adjustment)));
	gtk_adjustment_set_value (adjustment, value);
	return TRUE;
}

/* ------------------------------------------------------------------ */
/* settings                                                            */
/* ------------------------------------------------------------------ */

static void
miller_count_changed (GSettings *settings, const char *key, gpointer user_data)
{
	NemoMillerColumnsView *view = user_data;
	int count;

	count = g_settings_get_int (settings, NEMO_PREFERENCES_MILLER_COLUMNS_COUNT);
	if (count < 3) {
		count = 3;
	}
	if (count > 6) {
		count = 6;
	}
	if (count != view->details->max_columns) {
		view->details->max_columns = count;
		update_col_visibility (view);
	}
}

/* ------------------------------------------------------------------ */
/* GObject boilerplate                                                 */
/* ------------------------------------------------------------------ */

static void
load_miller_css (void)
{
	static gboolean loaded = FALSE;
	GtkCssProvider *provider;
    const char *css =
        /* Finder-inspired, but theme-adaptive: no hardcoded light/dark palette. */
        ".miller-files-window headerbar { padding: 6px 12px; min-height: 42px; }\n"
        ".miller-files-window notebook > header { padding: 4px 10px 0 10px; "
        "background: alpha(@theme_bg_color,0.96); }\n"
        ".miller-files-window notebook > header tab { padding: 6px 16px; "
        "border-radius: 9px 9px 0 0; margin: 1px 3px 0 0; }\n"
        ".miller-files-window notebook > header tab:checked { "
        "background: alpha(@theme_selected_bg_color,0.16); "
        "color: @theme_fg_color; }\n"
        ".miller-files-window notebook > header tab:hover:not(:checked) { "
        "background: alpha(@theme_fg_color,0.06); }\n"
        ".miller-files-window notebook > header tab:focus { "
        "outline-color: @theme_selected_bg_color; }\n"
        ".miller-columns-view { background: @theme_base_color; }\n"
        ".miller-files-window .miller-single-tab-controls { "
        "padding: 1px 9px; min-height: 25px; "
        "background: alpha(@theme_bg_color,0.96); }\n"
        ".miller-files-window .miller-single-tab-controls button { "
        "border-radius: 7px; min-height: 21px; min-width: 28px; }\n"
        ".miller-columns-view .miller-toolbar { padding: 8px 16px; "
        "border-bottom: 1px solid alpha(@theme_fg_color,0.09); "
        "background: alpha(@theme_bg_color,0.90); }\n"
        ".miller-columns-view .miller-toolbar button { border-radius: 9px; "
        "padding: 5px 13px; }\n"
        ".miller-columns-view .miller-header-label { font-weight: 700; "
        "font-size: 10px; "
        "color: alpha(@theme_fg_color,0.60); }\n"
        ".miller-columns-view treeview.view { padding: 3px 4px; "
        "background: @theme_base_color; }\n"
        ".miller-columns-view treeview.view:selected, "
        ".miller-columns-view treeview.view:selected:focus { "
        "background: @theme_selected_bg_color; "
        "color: @theme_selected_fg_color; }\n"
        /* Column separators: a transparent 5px drag target; the 1px line
         * itself is painted in C (miller_paned_draw_divider) so no theme or
         * custom palette can hide it.  Hover shows the selection colour. */
        ".miller-columns-view paned.horizontal > separator, "
        ".miller-columns-view paned.horizontal > separator.wide { "
        "min-width: 5px; margin: 0; border: none; box-shadow: none; "
        "background-color: transparent; background-image: none; }\n"
        ".miller-columns-view paned.horizontal > separator:hover { "
        "background-image: linear-gradient(to right, transparent 1px, "
        "alpha(@theme_selected_bg_color,0.80) 1px, "
        "alpha(@theme_selected_bg_color,0.80) 4px, transparent 4px); }\n"
        ".miller-columns-view separator { background: @theme_bg_color; }\n"
        ".miller-columns-view .miller-preview-pane { "
        "background: transparent; border: none; }\n"
        ".miller-columns-view .miller-active { "
        "background: alpha(@theme_selected_bg_color,0.22); }\n"
        ".miller-columns-view .miller-ancestor { "
        "background: alpha(@theme_selected_bg_color,0.11); }\n"
        ".miller-columns-view .miller-preview-title { font-weight: 700; "
        "font-size: 120%; }\n"
        ".miller-columns-view .miller-preview-meta, "
        ".miller-columns-view .miller-action-subtitle { "
        "color: alpha(@theme_fg_color,0.64); font-size: 91%; }\n"
        ".miller-columns-view .miller-action-title { font-weight: 600; }\n"
        ".miller-columns-view .miller-preview-path { "
        "color: alpha(@theme_fg_color,0.58); font-size: 90%; }\n"
        ".miller-columns-view .miller-action-button { "
        "border-radius: 11px; padding: 5px 9px; "
        "background: alpha(@theme_base_color,0.80); "
        "border: 1px solid alpha(@theme_fg_color,0.12); "
        "}\n"
        ".miller-columns-view .miller-action-button:hover { "
        "background: alpha(@theme_selected_bg_color,0.11); }\n"
        ".miller-columns-view .miller-action-button:active { "
        "background: alpha(@theme_selected_bg_color,0.19); }\n"
        ".miller-columns-view .miller-action-button:disabled { "
        "opacity: 0.55; }\n"
        ".miller-columns-view .miller-action-button:focus { "
        "outline-color: @theme_selected_bg_color; }\n"
        ".miller-columns-view .miller-video-overlay button { "
        "background: rgba(0,0,0,0.35); border: none; border-radius: 99px; }\n";

	if (loaded) {
		return;
	}
	loaded = TRUE;

	provider = gtk_css_provider_new ();
	gtk_css_provider_load_from_data (provider, css, -1, NULL);
	gtk_style_context_add_provider_for_screen (gdk_screen_get_default (),
						   GTK_STYLE_PROVIDER (provider),
						   GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
	g_object_unref (provider);
}

void
nemo_miller_columns_view_resort_groups (NemoMillerColumnsView *view)
{
    GList *it;
    g_return_if_fail (NEMO_IS_MILLER_COLUMNS_VIEW (view));
    if (view->details == NULL) return;
    for (it = view->details->columns; it != NULL; it = it->next) {
        MillerCol *col = it->data;
        if (col->store != NULL)
            miller_group_rebuild_column (view, col);
    }
}

static void
dakkar_miller_group_changed (GSettings *settings, gchar *key, gpointer data)
{
    nemo_miller_columns_view_resort_groups (NEMO_MILLER_COLUMNS_VIEW (data));
    (void)settings; (void)key;
}

static void
nemo_miller_columns_view_init (NemoMillerColumnsView *view)
{
	GtkWidget *main_hbox;
	int count;

	view->details = nemo_miller_columns_view_get_instance_private (view);
	memset (view->details, 0, sizeof (NemoMillerColumnsViewDetails));
	view->details->zoom_level = NEMO_ZOOM_LEVEL_STANDARD;
	view->details->click_to_rename =
		g_settings_get_boolean (nemo_preferences, NEMO_PREFERENCES_CLICK_TO_RENAME);
	view->details->sort_order = g_settings_get_enum (nemo_preferences,
		NEMO_PREFERENCES_DEFAULT_SORT_ORDER);
	view->details->sort_reversed = g_settings_get_boolean (nemo_preferences,
		NEMO_PREFERENCES_DEFAULT_SORT_IN_REVERSE_ORDER);
	view->details->default_width = g_settings_get_int (nemo_preferences,
		NEMO_PREFERENCES_MILLER_COLUMN_WIDTH);
	view->details->auto_fit = g_settings_get_boolean (nemo_preferences,
		NEMO_PREFERENCES_MILLER_AUTO_FIT);
	view->details->col_widths = g_array_new (FALSE, TRUE, sizeof (int));
	{
		GVariant *saved = g_settings_get_value (nemo_preferences,
			NEMO_PREFERENCES_MILLER_COLUMN_WIDTHS);
		GVariantIter iter;
		gint32 width;
		g_variant_iter_init (&iter, saved);
		while (g_variant_iter_next (&iter, "i", &width) &&
		       view->details->col_widths->len < 32) {
			int w = CLAMP (width, 120, 800);
			g_array_append_val (view->details->col_widths, w);
		}
		g_variant_unref (saved);
	}
    view->details->still_cache_order = g_queue_new ();
	view->details->still_cache = g_hash_table_new_full (g_str_hash, g_str_equal,
							    g_free,
							    g_object_unref);
	view->details->still_pending = g_hash_table_new_full (g_str_hash, g_str_equal,
							      g_free, NULL);

	count = g_settings_get_int (nemo_preferences, NEMO_PREFERENCES_MILLER_COLUMNS_COUNT);
	if (count < 3) {
		count = 3;
	}
	if (count > 6) {
		count = 6;
	}
	view->details->max_columns = count;

	view->details->vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);

	g_signal_connect_object (nemo_clipboard_monitor_get (),
				 "clipboard_info",
				 G_CALLBACK (on_clipboard_info), view, 0);
	g_signal_connect_object (nemo_preferences,
				 "changed::" NEMO_PREFERENCES_DEFAULT_SORT_ORDER,
				 G_CALLBACK (miller_sort_pref_changed), view, 0);
	g_signal_connect_object (nemo_preferences,
				 "changed::" NEMO_PREFERENCES_DEFAULT_SORT_IN_REVERSE_ORDER,
				 G_CALLBACK (miller_sort_pref_changed), view, 0);
	g_signal_connect_object (nemo_preferences,
				 "changed::" NEMO_PREFERENCES_SORT_DIRECTORIES_FIRST,
				 G_CALLBACK (miller_sort_pref_changed), view, 0);

	view->details->monitored_dirs =
		g_hash_table_new (miller_file_gfile_hash,
				  miller_file_gfile_equal);

	/* The details sidebar, Quick Actions and Quick Look are the shared
	 * per-slot ones (nemo-slot-quick-actions.inc), exactly as for the
	 * Icon/List/Compact views; this view only owns its columns. */
	main_hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 4);
	view->details->columns_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_box_pack_start (GTK_BOX (main_hbox), view->details->columns_box,
			    TRUE, TRUE, 0);

	/* permanent wrapper: columns are re-parented inside it on every
	 * layout change, so a single-column chain never gets destroyed.
	 * It lives in its own scrolled window so the horizontal scrollbar
	 * sits below the columns. */
	view->details->paned_root = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
	view->details->columns_scrolled = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (view->details->columns_scrolled),
					GTK_POLICY_AUTOMATIC, GTK_POLICY_NEVER);
	gtk_container_add (GTK_CONTAINER (view->details->columns_scrolled),
			   view->details->paned_root);
	g_signal_connect (view->details->columns_scrolled, "scroll-event",
	                  G_CALLBACK (miller_columns_scroll), view);
	{
		GtkAdjustment *hadj = gtk_scrolled_window_get_hadjustment (
			GTK_SCROLLED_WINDOW (view->details->columns_scrolled));
		g_signal_connect_object (hadj, "value-changed",
		                         G_CALLBACK (miller_divider_redraw_cb), view, G_CONNECT_SWAPPED);
		g_signal_connect_object (hadj, "changed",
		                         G_CALLBACK (miller_divider_redraw_cb), view, G_CONNECT_SWAPPED);
	}
	g_signal_connect_after (view, "map", G_CALLBACK (miller_divider_redraw_cb), NULL);
	gtk_box_pack_start (GTK_BOX (view->details->columns_box),
			    view->details->columns_scrolled, TRUE, TRUE, 0);

	gtk_box_pack_start (GTK_BOX (view->details->vbox), main_hbox,
			    TRUE, TRUE, 0);

	gtk_container_add (GTK_CONTAINER (view), view->details->vbox);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (view),
					GTK_POLICY_NEVER, GTK_POLICY_NEVER);
	gtk_widget_show_all (GTK_WIDGET (view));

	load_miller_css ();
	gtk_style_context_add_class (gtk_widget_get_style_context (GTK_WIDGET (view)),
				     "miller-columns-view");
	gtk_style_context_add_class (gtk_widget_get_style_context (GTK_WIDGET (view)),
				     "files-corner-view");

	g_signal_connect_object (nemo_preferences, "changed",
	                         G_CALLBACK (miller_preferences_changed), view, 0);
	/* Connect Group By only after the private state and widgets exist. */
	g_signal_connect_object (nemo_preferences, "changed::dakkar-group-by",
	                         G_CALLBACK (dakkar_miller_group_changed), view, 0);

	view->details->settings_handler =
		g_signal_connect (nemo_preferences,
				  "changed::" NEMO_PREFERENCES_MILLER_COLUMNS_COUNT,
				  G_CALLBACK (miller_count_changed), view);
}

static void
nemo_miller_columns_view_destroy (GtkWidget *widget)
{
	NemoMillerColumnsView *view = NEMO_MILLER_COLUMNS_VIEW (widget);
	miller_cancel_pending_heals (view);
	miller_rename_timer_cancel (view);
	g_clear_object (&view->details->rename_press_file);
	if (view->details->prefetch_cancel != NULL) {
		g_cancellable_cancel (view->details->prefetch_cancel);
	}
	if (view->details->widths_save_id != 0) {
		g_source_remove (view->details->widths_save_id);
		view->details->widths_save_id = 0;
	}
	if (view->details->slide_start_idle_id != 0) {
		g_source_remove (view->details->slide_start_idle_id);
		view->details->slide_start_idle_id = 0;
	}
	if (view->details->slide_tick_id != 0) {
        gtk_widget_remove_tick_callback (view->details->columns_scrolled,
                                         view->details->slide_tick_id);
        view->details->slide_tick_id = 0;
    }

	{
		GList *it;
		for (it = view->details->columns; it; it = it->next) {
			MillerCol *col = it->data;
			GtkTreeIter row;
			gboolean valid;
			if (col->scan_cancel) g_cancellable_cancel (col->scan_cancel);
			miller_drag_cancel_hover (col);
			/* make_row() registered miller_row_refresh_cb with this view
			 * as data; a late callback would touch a finalized view. */
			valid = col->store != NULL &&
				gtk_tree_model_get_iter_first (GTK_TREE_MODEL (col->store), &row);
			while (valid) {
				NemoFile *file = NULL;
				gtk_tree_model_get (GTK_TREE_MODEL (col->store), &row,
						    COL_FILE, &file, -1);
				if (file != NULL) {
					nemo_file_cancel_call_when_ready (file,
						miller_row_refresh_cb, view);
					nemo_file_unref (file);
				}
				valid = gtk_tree_model_iter_next (GTK_TREE_MODEL (col->store), &row);
			}
			if (col->group_files != NULL) {
				GHashTableIter gi;
				gpointer gk, gv;
				g_hash_table_iter_init (&gi, col->group_files);
				while (g_hash_table_iter_next (&gi, &gk, &gv))
					nemo_file_cancel_call_when_ready (NEMO_FILE (gv),
						miller_row_refresh_cb, view);
			}
		}
	}

	GTK_WIDGET_CLASS (nemo_miller_columns_view_parent_class)->destroy (widget);
}

static void
nemo_miller_columns_view_finalize (GObject *object)
{
	NemoMillerColumnsView *view;
	MillerCol *col;
	GList *l, *next;

	view = NEMO_MILLER_COLUMNS_VIEW (object);
	g_clear_pointer (&view->details->frozen_path, gtk_tree_path_free);
	g_list_free_full (view->details->drag_items, miller_drag_item_free);
	view->details->drag_items = NULL;

	if (view->details->settings_handler != 0) {
		g_signal_handler_disconnect (nemo_preferences,
					     view->details->settings_handler);
		view->details->settings_handler = 0;
	}

	if (view->details->preview_cancellable != NULL) {
		g_cancellable_cancel (view->details->preview_cancellable);
		g_object_unref (view->details->preview_cancellable);
		view->details->preview_cancellable = NULL;
	}
	for (l = view->details->columns; l != NULL; l = next) {
		next = l->next;
		col = l->data;
		/* widget tree destruction already handled the widgets;
		 * only release non-widget resources here */
		if (col->directory != NULL) {
			nemo_directory_unref (col->directory);
		}
		if (col->dir != NULL) {
			g_object_unref (col->dir);
		}
		if (col->group_sync_id) g_source_remove (col->group_sync_id);
    if (col->group_rebuild_id) g_source_remove (col->group_rebuild_id);
		if (col->fit_id) g_source_remove (col->fit_id);
		g_signal_handlers_disconnect_by_data (col->store, col);
        g_clear_pointer (&col->group_files, g_hash_table_unref);
        g_clear_pointer (&col->collapsed_groups, g_hash_table_unref);
		g_object_unref (col->store);
		g_free (col);
	}
	g_list_free (view->details->columns);
	view->details->columns = NULL;

	if (view->details->tmp_dir != NULL) {
		g_rmdir (view->details->tmp_dir);
		g_free (view->details->tmp_dir);
		view->details->tmp_dir = NULL;
	}
    g_clear_object (&view->details->prefetch_cancel);
    g_clear_object (&view->details->prefetch_dir);
    g_clear_pointer (&view->details->prefetch_items, g_ptr_array_unref);
    if (view->details->still_cache_order != NULL) {
        g_queue_free_full (view->details->still_cache_order, g_free);
        view->details->still_cache_order = NULL;
    }
	if (view->details->still_cache != NULL) {
		g_hash_table_unref (view->details->still_cache);
		view->details->still_cache = NULL;
	}
	if (view->details->still_pending != NULL) {
		g_hash_table_unref (view->details->still_pending);
		view->details->still_pending = NULL;
	}

	miller_set_action_column (view, NULL);
	g_clear_object (&view->details->paste_destination);
	g_clear_object (&view->details->last_click_file);
	g_clear_object (&view->details->rename_opened_target);
	if (view->details->rename_target_file != NULL) {
		nemo_file_unref (view->details->rename_target_file);
		view->details->rename_target_file = NULL;
	}
	free_selection (view);
	g_array_free (view->details->col_widths, TRUE);

	G_OBJECT_CLASS (nemo_miller_columns_view_parent_class)->finalize (object);
}

static void
nemo_miller_columns_view_class_init (NemoMillerColumnsViewClass *klass)
{
	GObjectClass *oclass;
	GtkWidgetClass *widget_class;
	NemoViewClass *nemo_view_class;

	oclass = G_OBJECT_CLASS (klass);
	oclass->finalize = nemo_miller_columns_view_finalize;

	widget_class = GTK_WIDGET_CLASS (klass);
	widget_class->destroy = nemo_miller_columns_view_destroy;

	nemo_view_class = NEMO_VIEW_CLASS (klass);

	/* model callbacks */
	nemo_view_class->clear = miller_clear;
	nemo_view_class->add_file = miller_add_file;
	nemo_view_class->remove_file = miller_remove_file;
	nemo_view_class->file_changed = miller_file_changed;
	nemo_view_class->begin_loading = miller_begin_loading;
	nemo_view_class->end_loading = miller_end_loading;

	/* selection */
	nemo_view_class->get_selection = miller_get_selection;
	nemo_view_class->peek_selection = miller_peek_selection;
	nemo_view_class->get_selection_count = miller_get_selection_count;
	nemo_view_class->get_selection_for_file_transfer = miller_get_verified_transfer_selection;
	nemo_view_class->get_backing_uri = miller_get_backing_uri;
	nemo_view_class->select_all = miller_select_all;
	nemo_view_class->set_selection = miller_set_selection;
	nemo_view_class->start_renaming_file = miller_start_renaming_file;
	nemo_view_class->invert_selection = miller_invert_selection;

	/* misc */
	nemo_view_class->get_item_count = miller_get_item_count;
	nemo_view_class->is_empty = miller_is_empty;
	nemo_view_class->compare_files = miller_compare_files;
	nemo_view_class->reset_to_defaults = miller_reset_to_defaults;
	nemo_view_class->bump_zoom_level = miller_bump_zoom_level;
	nemo_view_class->zoom_to_level = miller_zoom_to_level;
	nemo_view_class->get_zoom_level = miller_get_zoom_level;
	nemo_view_class->restore_default_zoom_level = miller_restore_default_zoom_level;
	nemo_view_class->can_zoom_in = miller_can_zoom_in;
	nemo_view_class->can_zoom_out = miller_can_zoom_out;
	nemo_view_class->reveal_selection = miller_reveal_selection;
	nemo_view_class->widget_to_file_operation_position =
		miller_widget_to_file_operation_position;
	nemo_view_class->click_policy_changed = miller_click_policy_changed;
	nemo_view_class->click_to_rename_mode_changed = miller_click_to_rename_mode_changed;
	nemo_view_class->sort_directories_first_changed = miller_sort_directories_first_changed;
	nemo_view_class->sort_favorites_first_changed = miller_sort_favorites_first_changed;
	nemo_view_class->get_view_id = miller_get_view_id;
	nemo_view_class->merge_menus = miller_merge_menus;
	nemo_view_class->unmerge_menus = miller_unmerge_menus;
	nemo_view_class->update_menus = miller_update_menus;
	nemo_view_class->get_first_visible_file = miller_get_first_visible_file;
	nemo_view_class->scroll_to_file = miller_scroll_to_file;
}

/* ------------------------------------------------------------------ */
/* registration                                                        */
/* ------------------------------------------------------------------ */

static NemoView *
nemo_miller_columns_view_create (NemoWindowSlot *slot)
{
	NemoMillerColumnsView *view;

	view = g_object_new (NEMO_TYPE_MILLER_COLUMNS_VIEW,
			     "window-slot", slot,
			     "supports-zooming", TRUE,
			     NULL);
	return NEMO_VIEW (view);
}

static gboolean
nemo_miller_columns_view_supports_uri (const char *uri,
				       GFileType file_type,
				       const char *mime_type)
{
	if (g_str_has_prefix (uri, EEL_SEARCH_URI)) return FALSE;
	if (file_type == G_FILE_TYPE_DIRECTORY) return TRUE;
	/* Global search results (x-nemo-search://) have no folder chain, so
	 * the column view showed an empty pane. Let the slot fall back to the
	 * List view for them (nemo-window-manage-views.c picks it for
	 * "x-nemo-search"); leaving the search returns to the column view. */
	return g_str_has_prefix (uri, "trash:") ||
	       g_str_has_prefix (uri, "recent:") ||
	       g_str_has_prefix (uri, "favorites:");
}

static NemoViewInfo nemo_miller_columns_view_view_info = {
	(char *)NEMO_MILLER_VIEW_ID,
	(char *)N_("Miller Columns View"),
	(char *)N_("_Miller Columns"),
	(char *)N_("The Miller columns view encountered an error."),
	(char *)N_("The Miller columns view encountered an error while starting up."),
	(char *)N_("Display this location with the Miller columns view."),
	nemo_miller_columns_view_create,
	nemo_miller_columns_view_supports_uri
};

void
nemo_miller_columns_view_register (void)
{
	static gboolean registered = FALSE;

	if (!registered) {
		nemo_miller_columns_view_view_info.view_combo_label =
			_(nemo_miller_columns_view_view_info.view_combo_label);
		nemo_miller_columns_view_view_info.view_menu_label_with_mnemonic =
			_(nemo_miller_columns_view_view_info.view_menu_label_with_mnemonic);
		nemo_miller_columns_view_view_info.error_label =
			_(nemo_miller_columns_view_view_info.error_label);
		nemo_miller_columns_view_view_info.startup_error_label =
			_(nemo_miller_columns_view_view_info.startup_error_label);
		nemo_miller_columns_view_view_info.display_location_label =
			_(nemo_miller_columns_view_view_info.display_location_label);

		nemo_view_factory_register (&nemo_miller_columns_view_view_info);
		registered = TRUE;
	}
}