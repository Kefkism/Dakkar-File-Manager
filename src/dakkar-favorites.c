/* Dakkar Favorites, see dakkar-favorites.h.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <config.h>
#include "dakkar-favorites.h"

#include <glib/gi18n.h>
#include <libxapp/xapp-favorites.h>
#include <libnemo-private/nemo-global-preferences.h>
#include <libnemo-private/nemo-file.h>
#include "nemo-bookmark-list.h"

#define ORDER_KEY  "dakkar-favorite-order"
#define HIDDEN_KEY "dakkar-favorite-hidden"

void
dakkar_favorite_free (DakkarFavorite *favorite)
{
    if (favorite == NULL)
        return;
    g_free (favorite->uri);
    g_free (favorite->name);
    g_free (favorite->icon_name);
    g_free (favorite->tooltip);
    g_free (favorite);
}

static gchar *
favorite_icon_name (const gchar *uri)
{
    NemoFile *file = nemo_file_get_by_uri (uri);
    gchar *icon = file != NULL ? nemo_file_get_control_icon_name (file) : NULL;
    if (file != NULL)
        nemo_file_unref (file);
    return icon;
}

static gchar *
favorite_tooltip (const gchar *uri)
{
    GFile *file = g_file_new_for_uri (uri);
    gchar *name = g_file_get_parse_name (file);
    g_object_unref (file);
    return name;
}

static gboolean
strv_has (gchar **strv, const gchar *s)
{
    return strv != NULL && s != NULL && g_strv_contains ((const gchar * const *) strv, s);
}

static gint
order_index (gchar **order, const gchar *uri)
{
    gint i;
    for (i = 0; order != NULL && order[i] != NULL; i++)
        if (g_strcmp0 (order[i], uri) == 0)
            return i;
    return G_MAXINT;
}

typedef struct { DakkarFavorite *fav; gint natural; } Sortable;

static gint
sortable_compare (gconstpointer a, gconstpointer b, gpointer order)
{
    const Sortable *x = a, *y = b;
    gint xi = order_index (order, x->fav->uri);
    gint yi = order_index (order, y->fav->uri);
    if (xi != yi)
        return xi < yi ? -1 : 1;
    return x->natural - y->natural;          /* stable for unknown items */
}

GList *
dakkar_favorites_collect (void)
{
    static const struct { GUserDirectory dir; const gchar *label; } xdg[] = {
        { G_USER_DIRECTORY_DESKTOP, N_("Desktop") },
        { G_USER_DIRECTORY_DOWNLOAD, N_("Downloads") },
        { G_USER_DIRECTORY_MUSIC, N_("Music") },
        { G_USER_DIRECTORY_PICTURES, N_("Pictures") },
        { G_USER_DIRECTORY_VIDEOS, N_("Videos") },
    };
    GHashTable *seen = g_hash_table_new (g_str_hash, g_str_equal);
    GArray *items = g_array_new (FALSE, FALSE, sizeof (Sortable));
    gchar **order = g_settings_get_strv (nemo_window_state, ORDER_KEY);
    gchar **hidden = g_settings_get_strv (nemo_window_state, HIDDEN_KEY);
    GList *xfavs, *l, *result = NULL;
    NemoBookmarkList *bookmarks;
    guint i, n;

#define ADD(f) G_STMT_START { \
        Sortable s_ = { (f), (gint) items->len }; \
        g_hash_table_add (seen, s_.fav->uri); \
        g_array_append_val (items, s_); \
    } G_STMT_END

    /* 1. XApp favorites */
    xfavs = xapp_favorites_get_favorites (xapp_favorites_get_default (), NULL);
    for (l = xfavs; l != NULL; l = l->next) {
        XAppFavoriteInfo *info = l->data;
        DakkarFavorite *f;
        if (info == NULL || info->uri == NULL || g_hash_table_contains (seen, info->uri))
            continue;
        f = g_new0 (DakkarFavorite, 1);
        f->kind = DAKKAR_FAVORITE_XAPP;
        f->uri = g_strdup (info->uri);
        f->name = g_strdup (info->display_name != NULL ? info->display_name : info->uri);
        f->icon_name = favorite_icon_name (info->uri);
        f->tooltip = favorite_tooltip (info->uri);
        f->bookmark_index = -1;
        ADD (f);
    }
    g_list_free_full (xfavs, (GDestroyNotify) xapp_favorite_info_free);

    /* 2. standard folders that exist and were not removed */
    for (i = 0; i < G_N_ELEMENTS (xdg); i++) {
        const gchar *path = g_get_user_special_dir (xdg[i].dir);
        gchar *uri;
        DakkarFavorite *f;
        if (path == NULL || !g_file_test (path, G_FILE_TEST_IS_DIR))
            continue;
        uri = g_filename_to_uri (path, NULL, NULL);
        if (uri == NULL || g_hash_table_contains (seen, uri) || strv_has (hidden, uri)) {
            g_free (uri);
            continue;
        }
        f = g_new0 (DakkarFavorite, 1);
        f->kind = DAKKAR_FAVORITE_XDG;
        f->uri = uri;
        f->name = g_strdup (_(xdg[i].label));
        f->icon_name = favorite_icon_name (uri);
        f->tooltip = g_strdup (path);
        f->bookmark_index = -1;
        ADD (f);
    }

    /* 3. GTK bookmarks */
    bookmarks = nemo_bookmark_list_get_default ();
    n = nemo_bookmark_list_length (bookmarks);
    for (i = 0; i < n; i++) {
        NemoBookmark *bookmark = nemo_bookmark_list_item_at (bookmarks, i);
        gchar *uri = nemo_bookmark_get_uri (bookmark);
        DakkarFavorite *f;
        if (uri == NULL || g_hash_table_contains (seen, uri)) {
            g_free (uri);
            continue;
        }
        f = g_new0 (DakkarFavorite, 1);
        f->kind = DAKKAR_FAVORITE_BOOKMARK;
        f->uri = uri;
        f->name = g_strdup (nemo_bookmark_get_name (bookmark));
        f->icon_name = nemo_bookmark_get_icon_name (bookmark);
        f->tooltip = favorite_tooltip (uri);
        f->bookmark_index = (gint) i;
        ADD (f);
    }
#undef ADD

    g_array_sort_with_data (items, sortable_compare, order);
    for (i = items->len; i > 0; i--)
        result = g_list_prepend (result, g_array_index (items, Sortable, i - 1).fav);

    g_array_free (items, TRUE);
    g_hash_table_destroy (seen);
    g_strfreev (order);
    g_strfreev (hidden);
    return result;
}

void
dakkar_favorites_set_order (const gchar * const *uris)
{
    g_settings_set_strv (nemo_window_state, ORDER_KEY, uris);
}

/* Current display order as a GPtrArray of URIs (NULL-terminated). */
static GPtrArray *
current_order (void)
{
    GList *favs = dakkar_favorites_collect (), *l;
    GPtrArray *uris = g_ptr_array_new_with_free_func (g_free);
    for (l = favs; l != NULL; l = l->next)
        g_ptr_array_add (uris, g_strdup (((DakkarFavorite *) l->data)->uri));
    g_list_free_full (favs, (GDestroyNotify) dakkar_favorite_free);
    return uris;
}

static gint
find_uri (GPtrArray *uris, const gchar *uri)
{
    guint i;
    for (i = 0; i < uris->len; i++)
        if (g_strcmp0 (g_ptr_array_index (uris, i), uri) == 0)
            return (gint) i;
    return -1;
}

static void
save_order (GPtrArray *uris)
{
    g_ptr_array_add (uris, NULL);
    dakkar_favorites_set_order ((const gchar * const *) uris->pdata);
    g_ptr_array_remove_index (uris, uris->len - 1);
}

gboolean
dakkar_favorites_move (const gchar *uri, gint delta)
{
    GPtrArray *uris = current_order ();
    gint from = find_uri (uris, uri), to = from + delta;
    gboolean moved = FALSE;

    if (from >= 0 && to >= 0 && to < (gint) uris->len && delta != 0) {
        gpointer item = g_ptr_array_steal_index (uris, from);
        g_ptr_array_insert (uris, to, item);
        save_order (uris);
        moved = TRUE;
    }
    g_ptr_array_free (uris, TRUE);
    return moved;
}

gboolean
dakkar_favorites_move_to (const gchar *uri, const gchar *target_uri, gboolean after)
{
    GPtrArray *uris = current_order ();
    gint from = find_uri (uris, uri), to;
    gboolean moved = FALSE;

    if (from >= 0 && g_strcmp0 (uri, target_uri) != 0) {
        gpointer item = g_ptr_array_steal_index (uris, from);
        to = find_uri (uris, target_uri);
        if (to < 0)
            to = after ? (gint) uris->len : 0;
        else if (after)
            to++;
        g_ptr_array_insert (uris, to, item);
        save_order (uris);
        moved = TRUE;
    }
    g_ptr_array_free (uris, TRUE);
    return moved;
}

void
dakkar_favorites_remove (const gchar *uri)
{
    GList *favs = dakkar_favorites_collect (), *l;

    for (l = favs; l != NULL; l = l->next) {
        DakkarFavorite *f = l->data;
        if (g_strcmp0 (f->uri, uri) != 0)
            continue;
        if (f->kind == DAKKAR_FAVORITE_XAPP) {
            xapp_favorites_remove (xapp_favorites_get_default (), uri);
        } else if (f->kind == DAKKAR_FAVORITE_BOOKMARK) {
            nemo_bookmark_list_delete_items_with_uri (nemo_bookmark_list_get_default (), uri);
        } else {
            gchar **hidden = g_settings_get_strv (nemo_window_state, HIDDEN_KEY);
            GPtrArray *h = g_ptr_array_new ();
            guint i;
            for (i = 0; hidden != NULL && hidden[i] != NULL; i++)
                g_ptr_array_add (h, hidden[i]);
            g_ptr_array_add (h, (gpointer) uri);
            g_ptr_array_add (h, NULL);
            g_settings_set_strv (nemo_window_state, HIDDEN_KEY,
                                 (const gchar * const *) h->pdata);
            g_ptr_array_free (h, TRUE);
            g_strfreev (hidden);
        }
        break;
    }
    g_list_free_full (favs, (GDestroyNotify) dakkar_favorite_free);
}

/* ------------------------------------------------------------------ */
/* Preferences > Favorites                                             */
/* ------------------------------------------------------------------ */

#include <gtk/gtk.h>

enum {
    FAV_COL_GICON,
    FAV_COL_MARKUP,
    FAV_COL_URI,
    FAV_COL_TOOLTIP,
    FAV_N_COLS
};

typedef struct {
    GtkWidget    *page;
    GtkTreeView  *view;
    GtkListStore *store;
    GtkWidget    *up, *down, *remove, *restore, *empty;
    gboolean      loading;
    guint         reload_id;
    guint         save_id;
} FavPage;

static gchar *
fav_page_selected_uri (FavPage *fp, gint *position, gint *count)
{
    GtkTreeModel *model;
    GtkTreeIter iter;
    gchar *uri = NULL;

    if (count != NULL)
        *count = gtk_tree_model_iter_n_children (GTK_TREE_MODEL (fp->store), NULL);
    if (position != NULL)
        *position = -1;
    if (!gtk_tree_selection_get_selected (gtk_tree_view_get_selection (fp->view), &model, &iter))
        return NULL;
    gtk_tree_model_get (model, &iter, FAV_COL_URI, &uri, -1);
    if (position != NULL) {
        GtkTreePath *path = gtk_tree_model_get_path (model, &iter);
        *position = gtk_tree_path_get_indices (path)[0];
        gtk_tree_path_free (path);
    }
    return uri;
}

static void
fav_page_update_buttons (FavPage *fp)
{
    gint position, count;
    gchar *uri = fav_page_selected_uri (fp, &position, &count);
    gchar **hidden = g_settings_get_strv (nemo_window_state, HIDDEN_KEY);

    gtk_widget_set_sensitive (fp->up, uri != NULL && position > 0);
    gtk_widget_set_sensitive (fp->down, uri != NULL && position >= 0 && position < count - 1);
    gtk_widget_set_sensitive (fp->remove, uri != NULL);
    gtk_widget_set_visible (fp->restore, hidden != NULL && hidden[0] != NULL);
    gtk_widget_set_visible (fp->empty, count == 0);
    g_strfreev (hidden);
    g_free (uri);
}

static void
fav_page_reload (FavPage *fp)
{
    GList *favs, *l;
    gchar *selected = fav_page_selected_uri (fp, NULL, NULL);
    GtkTreeIter iter;

    fp->loading = TRUE;
    gtk_list_store_clear (fp->store);
    favs = dakkar_favorites_collect ();
    for (l = favs; l != NULL; l = l->next) {
        DakkarFavorite *f = l->data;
        GIcon *icon = NULL;
        gchar *markup;

        if (f->icon_name != NULL && !g_str_has_prefix (f->icon_name, "/") &&
            !g_str_has_suffix (f->icon_name, "-symbolic")) {
            /* Same symbolic style as the sidebar rows, coloured by the theme. */
            gchar *names[3];
            names[0] = g_strconcat (f->icon_name, "-symbolic", NULL);
            names[1] = f->icon_name;
            names[2] = NULL;
            icon = g_themed_icon_new_from_names (names, 2);
            g_free (names[0]);
        } else if (f->icon_name != NULL) {
            icon = g_icon_new_for_string (f->icon_name, NULL);
        }
        if (icon == NULL)
            icon = g_themed_icon_new (f->kind == DAKKAR_FAVORITE_XAPP ? "text-x-generic" : "folder");
        /* The path is dimmed with the theme's own text colour (alpha only). */
        markup = g_markup_printf_escaped ("%s\n<small><span alpha=\"65%%\">%s</span></small>",
                                          f->name != NULL ? f->name : f->uri,
                                          f->tooltip != NULL ? f->tooltip : f->uri);
        gtk_list_store_insert_with_values (fp->store, &iter, -1,
                                           FAV_COL_GICON, icon,
                                           FAV_COL_MARKUP, markup,
                                           FAV_COL_URI, f->uri,
                                           FAV_COL_TOOLTIP, f->tooltip,
                                           -1);
        if (selected != NULL && g_strcmp0 (selected, f->uri) == 0)
            gtk_tree_selection_select_iter (gtk_tree_view_get_selection (fp->view), &iter);
        g_object_unref (icon);
        g_free (markup);
    }
    g_list_free_full (favs, (GDestroyNotify) dakkar_favorite_free);
    fp->loading = FALSE;
    g_free (selected);
    fav_page_update_buttons (fp);
}

static gboolean
fav_page_reload_idle (gpointer data)
{
    FavPage *fp = data;
    fp->reload_id = 0;
    fav_page_reload (fp);
    return G_SOURCE_REMOVE;
}

static void
fav_page_queue_reload (FavPage *fp)
{
    if (fp->reload_id == 0)
        fp->reload_id = g_idle_add (fav_page_reload_idle, fp);
}

static void
fav_page_changed_cb (gpointer instance, gpointer data)
{
    (void) instance;
    fav_page_queue_reload (data);
}

static void
fav_page_settings_changed_cb (GSettings *settings, const gchar *key, gpointer data)
{
    (void) settings;
    if (g_strcmp0 (key, ORDER_KEY) == 0 || g_strcmp0 (key, HIDDEN_KEY) == 0)
        fav_page_queue_reload (data);
}

/* After a drag and drop inside the list, store the list order. */
static gboolean
fav_page_save_idle (gpointer data)
{
    FavPage *fp = data;
    GPtrArray *uris = g_ptr_array_new_with_free_func (g_free);
    GtkTreeIter iter;
    gboolean valid;

    fp->save_id = 0;
    for (valid = gtk_tree_model_get_iter_first (GTK_TREE_MODEL (fp->store), &iter); valid;
         valid = gtk_tree_model_iter_next (GTK_TREE_MODEL (fp->store), &iter)) {
        gchar *uri = NULL;
        gtk_tree_model_get (GTK_TREE_MODEL (fp->store), &iter, FAV_COL_URI, &uri, -1);
        if (uri != NULL)
            g_ptr_array_add (uris, uri);
    }
    save_order (uris);
    g_ptr_array_free (uris, TRUE);
    return G_SOURCE_REMOVE;
}

static void
fav_page_row_deleted (GtkTreeModel *model, GtkTreePath *path, gpointer data)
{
    FavPage *fp = data;
    (void) model; (void) path;
    /* A reorder drag inserts the copy and then deletes the source row. */
    if (!fp->loading && fp->save_id == 0)
        fp->save_id = g_idle_add (fav_page_save_idle, fp);
}

static void
fav_page_move (FavPage *fp, gint delta)
{
    gchar *uri = fav_page_selected_uri (fp, NULL, NULL);
    if (uri != NULL)
        dakkar_favorites_move (uri, delta);
    g_free (uri);
}

static void fav_page_up_cb (GtkButton *b, FavPage *fp) { (void) b; fav_page_move (fp, -1); }
static void fav_page_down_cb (GtkButton *b, FavPage *fp) { (void) b; fav_page_move (fp, 1); }

static void
fav_page_remove_cb (GtkButton *button, FavPage *fp)
{
    gint position, count;
    gchar *uri = fav_page_selected_uri (fp, &position, &count);
    (void) button;
    if (uri == NULL)
        return;
    dakkar_favorites_remove (uri);
    g_free (uri);
    /* keep a selection at the same place for repeated removal */
    fav_page_reload (fp);
    count = gtk_tree_model_iter_n_children (GTK_TREE_MODEL (fp->store), NULL);
    if (count > 0) {
        GtkTreePath *path = gtk_tree_path_new_from_indices (MIN (position, count - 1), -1);
        gtk_tree_selection_select_path (gtk_tree_view_get_selection (fp->view), path);
        gtk_tree_path_free (path);
    }
}

static void
fav_page_restore_cb (GtkButton *button, FavPage *fp)
{
    (void) button; (void) fp;
    g_settings_reset (nemo_window_state, HIDDEN_KEY);
}

static void
fav_page_selection_changed (GtkTreeSelection *selection, FavPage *fp)
{
    (void) selection;
    if (!fp->loading)
        fav_page_update_buttons (fp);
}

static gboolean
fav_page_key_press (GtkWidget *widget, GdkEventKey *event, FavPage *fp)
{
    (void) widget;
    if (event->keyval == GDK_KEY_Delete || event->keyval == GDK_KEY_KP_Delete) {
        fav_page_remove_cb (NULL, fp);
        return TRUE;
    }
    if ((event->state & GDK_MOD1_MASK) && (event->keyval == GDK_KEY_Up || event->keyval == GDK_KEY_Down)) {
        fav_page_move (fp, event->keyval == GDK_KEY_Up ? -1 : 1);
        return TRUE;
    }
    return FALSE;
}

static void
fav_page_free (FavPage *fp)
{
    g_signal_handlers_disconnect_by_data (nemo_window_state, fp);
    g_signal_handlers_disconnect_by_data (xapp_favorites_get_default (), fp);
    g_signal_handlers_disconnect_by_data (nemo_bookmark_list_get_default (), fp);
    if (fp->reload_id != 0)
        g_source_remove (fp->reload_id);
    if (fp->save_id != 0)
        g_source_remove (fp->save_id);
    g_clear_object (&fp->store);
    g_free (fp);
}

static GtkWidget *
fav_page_button (const gchar *mnemonic, const gchar *icon, const gchar *tooltip)
{
    GtkWidget *button = gtk_button_new_with_mnemonic (mnemonic);
    gtk_button_set_image (GTK_BUTTON (button),
                          gtk_image_new_from_icon_name (icon, GTK_ICON_SIZE_BUTTON));
    gtk_button_set_always_show_image (GTK_BUTTON (button), TRUE);
    gtk_widget_set_tooltip_text (button, tooltip);
    return button;
}

GtkWidget *
dakkar_favorites_preferences_page (void)
{
    FavPage *fp = g_new0 (FavPage, 1);
    GtkWidget *page, *label, *scroller, *buttons, *outer;
    GtkCellRenderer *renderer;
    GtkTreeViewColumn *column;
    gchar *markup;

    page = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width (GTK_CONTAINER (page), 18);
    gtk_widget_set_name (page, "dakkar-prefs-favorites");
    fp->page = page;

    markup = g_markup_printf_escaped ("<b>%s</b>", _("Favorites"));
    label = gtk_label_new (NULL);
    gtk_label_set_markup (GTK_LABEL (label), markup);
    gtk_label_set_xalign (GTK_LABEL (label), 0.0);
    g_free (markup);
    gtk_box_pack_start (GTK_BOX (page), label, FALSE, FALSE, 0);

    label = gtk_label_new (_("Files and folders in the Favorites section of the sidebar. "
                             "Drag a row or use the buttons to change the order; changes "
                             "apply immediately."));
    gtk_label_set_xalign (GTK_LABEL (label), 0.0);
    gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
    gtk_label_set_max_width_chars (GTK_LABEL (label), 40);
    gtk_style_context_add_class (gtk_widget_get_style_context (label), "dim-label");
    gtk_box_pack_start (GTK_BOX (page), label, FALSE, FALSE, 0);

    fp->store = gtk_list_store_new (FAV_N_COLS, G_TYPE_ICON, G_TYPE_STRING,
                                    G_TYPE_STRING, G_TYPE_STRING);
    fp->view = GTK_TREE_VIEW (gtk_tree_view_new_with_model (GTK_TREE_MODEL (fp->store)));
    gtk_widget_set_name (GTK_WIDGET (fp->view), "dakkar-favorites-list");
    gtk_tree_view_set_headers_visible (fp->view, FALSE);
    gtk_tree_view_set_reorderable (fp->view, TRUE);
    gtk_tree_view_set_tooltip_column (fp->view, FAV_COL_TOOLTIP);
    gtk_tree_view_set_enable_search (fp->view, FALSE);
    atk_object_set_name (gtk_widget_get_accessible (GTK_WIDGET (fp->view)), _("Favorites"));

    column = gtk_tree_view_column_new ();
    renderer = gtk_cell_renderer_pixbuf_new ();
    g_object_set (renderer, "stock-size", GTK_ICON_SIZE_MENU, "xpad", 8, "ypad", 4, NULL);
    gtk_tree_view_column_pack_start (column, renderer, FALSE);
    gtk_tree_view_column_add_attribute (column, renderer, "gicon", FAV_COL_GICON);
    renderer = gtk_cell_renderer_text_new ();
    g_object_set (renderer, "ellipsize", PANGO_ELLIPSIZE_MIDDLE, "ypad", 4, NULL);
    gtk_tree_view_column_pack_start (column, renderer, TRUE);
    gtk_tree_view_column_add_attribute (column, renderer, "markup", FAV_COL_MARKUP);
    gtk_tree_view_append_column (fp->view, column);

    scroller = gtk_scrolled_window_new (NULL, NULL);
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroller),
                                    GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (scroller), GTK_SHADOW_IN);
    gtk_scrolled_window_set_min_content_height (GTK_SCROLLED_WINDOW (scroller), 220);
    gtk_widget_set_vexpand (scroller, TRUE);
    gtk_container_add (GTK_CONTAINER (scroller), GTK_WIDGET (fp->view));
    gtk_box_pack_start (GTK_BOX (page), scroller, TRUE, TRUE, 0);

    fp->empty = gtk_label_new (_("No favorites yet. Right-click a file or folder and choose "
                                 "“Add to favorites”."));
    gtk_label_set_line_wrap (GTK_LABEL (fp->empty), TRUE);
    gtk_label_set_xalign (GTK_LABEL (fp->empty), 0.0);
    gtk_style_context_add_class (gtk_widget_get_style_context (fp->empty), "dim-label");
    gtk_box_pack_start (GTK_BOX (page), fp->empty, FALSE, FALSE, 0);

    /* A flow box wraps the buttons onto a second line in a narrow dialog. */
    buttons = gtk_flow_box_new ();
    gtk_flow_box_set_selection_mode (GTK_FLOW_BOX (buttons), GTK_SELECTION_NONE);
    gtk_flow_box_set_homogeneous (GTK_FLOW_BOX (buttons), FALSE);
    gtk_flow_box_set_column_spacing (GTK_FLOW_BOX (buttons), 6);
    gtk_flow_box_set_row_spacing (GTK_FLOW_BOX (buttons), 6);
    gtk_flow_box_set_max_children_per_line (GTK_FLOW_BOX (buttons), 4);
    gtk_widget_set_name (buttons, "dakkar-favorites-buttons");
    fp->up = fav_page_button (C_("favorites", "Move _Up"), "go-up-symbolic", _("Move the selected favorite up"));
    fp->down = fav_page_button (C_("favorites", "Move _Down"), "go-down-symbolic", _("Move the selected favorite down"));
    fp->remove = fav_page_button (C_("favorites", "_Remove"), "list-remove-symbolic",
                                  _("Remove the selected item from Favorites (the file itself is kept)"));
    fp->restore = fav_page_button (_("Restore _Standard Folders"), "edit-undo-symbolic",
                                   _("Show the removed standard folders (Desktop, Downloads, …) again"));
    gtk_widget_set_name (fp->up, "dakkar-favorites-up");
    gtk_widget_set_name (fp->down, "dakkar-favorites-down");
    gtk_widget_set_name (fp->remove, "dakkar-favorites-remove");
    gtk_container_add (GTK_CONTAINER (buttons), fp->up);
    gtk_container_add (GTK_CONTAINER (buttons), fp->down);
    gtk_container_add (GTK_CONTAINER (buttons), fp->remove);
    gtk_container_add (GTK_CONTAINER (buttons), fp->restore);
    gtk_box_pack_start (GTK_BOX (page), buttons, FALSE, FALSE, 0);

    g_signal_connect (fp->up, "clicked", G_CALLBACK (fav_page_up_cb), fp);
    g_signal_connect (fp->down, "clicked", G_CALLBACK (fav_page_down_cb), fp);
    g_signal_connect (fp->remove, "clicked", G_CALLBACK (fav_page_remove_cb), fp);
    g_signal_connect (fp->restore, "clicked", G_CALLBACK (fav_page_restore_cb), fp);
    g_signal_connect (gtk_tree_view_get_selection (fp->view), "changed",
                      G_CALLBACK (fav_page_selection_changed), fp);
    g_signal_connect (fp->view, "key-press-event", G_CALLBACK (fav_page_key_press), fp);
    g_signal_connect (fp->store, "row-deleted", G_CALLBACK (fav_page_row_deleted), fp);

    /* Live: the sidebar, other windows and other programs may change it. */
    g_signal_connect (nemo_window_state, "changed",
                      G_CALLBACK (fav_page_settings_changed_cb), fp);
    g_signal_connect (xapp_favorites_get_default (), "changed",
                      G_CALLBACK (fav_page_changed_cb), fp);
    g_signal_connect (nemo_bookmark_list_get_default (), "changed",
                      G_CALLBACK (fav_page_changed_cb), fp);

    outer = gtk_scrolled_window_new (NULL, NULL);
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (outer),
                                    GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_container_add (GTK_CONTAINER (outer), page);
    g_object_set_data_full (G_OBJECT (outer), "dakkar-favorites-page", fp,
                            (GDestroyNotify) fav_page_free);
    gtk_widget_show_all (outer);
    fav_page_reload (fp);
    return outer;
}
