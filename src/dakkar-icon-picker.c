/* Dakkar: icon picker for sidebar items.
 *
 * Installed icon themes are indexed once per process in a worker thread
 * (only directory listings and index.theme files are read there; no GTK).
 * The grid is a GtkIconView over a flat model whose images are loaded
 * lazily for the visible range only, so packs with tens of thousands of
 * icons stay responsive. All colours come from the GTK theme.
 */
#include <config.h>
#include <string.h>
#include <stdlib.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <cairo-gobject.h>

#include "nemo-corner-style.h"
#include "dakkar-icon-picker.h"

/* ---------- index ---------- */

typedef enum {
    CTX_OTHER, CTX_PLACES, CTX_DEVICES, CTX_APPS, CTX_MIME, CTX_ACTIONS,
    CTX_STATUS, CTX_EMBLEMS, CTX_CATEGORIES, CTX_N
} IconContext;

typedef struct {
    gchar *dir;          /* theme directory name, e.g. "Papirus" */
    gchar *name;         /* display Name from index.theme */
    GPtrArray *bases;    /* folders that contain a directory of this theme */
    GPtrArray *subdirs;  /* icon directories listed in index.theme */
    GArray *contexts;    /* IconContext per subdir */
    GArray *sizes;       /* nominal size per subdir (scalable = 512) */
} IconPack;

typedef struct {
    gchar *name;
    gchar *path;
    guint16 pack;
    guint8 context;
    guint8 shared;   /* only found in folders borrowed from another theme */
    gint16 score;
} IconEntry;

typedef struct {
    GPtrArray *packs;    /* IconPack* */
    GArray *entries;     /* IconEntry, sorted by name */
    gboolean truncated;
} IconIndex;

/* a guard against absurdly large or looping installations */
#define DAKKAR_ICON_INDEX_MAX 400000

static IconIndex *the_index;          /* built once, immutable afterwards */
static gboolean index_running;
static GList *index_waiters;          /* GTask* waiting for the index */

static void
icon_pack_free (IconPack *p)
{
    g_free (p->dir);
    g_free (p->name);
    g_ptr_array_unref (p->bases);
    g_ptr_array_unref (p->subdirs);
    g_array_unref (p->contexts);
    g_array_unref (p->sizes);
    g_free (p);
}

static IconContext
context_from_string (const gchar *s)
{
    if (s == NULL) return CTX_OTHER;
    if (g_ascii_strcasecmp (s, "Places") == 0 || g_ascii_strcasecmp (s, "FileSystems") == 0)
        return CTX_PLACES;
    if (g_ascii_strcasecmp (s, "Devices") == 0) return CTX_DEVICES;
    if (g_ascii_strcasecmp (s, "Applications") == 0 || g_ascii_strcasecmp (s, "Apps") == 0)
        return CTX_APPS;
    if (g_ascii_strcasecmp (s, "MimeTypes") == 0) return CTX_MIME;
    if (g_ascii_strcasecmp (s, "Actions") == 0) return CTX_ACTIONS;
    if (g_ascii_strcasecmp (s, "Status") == 0 || g_ascii_strcasecmp (s, "Notifications") == 0)
        return CTX_STATUS;
    if (g_ascii_strcasecmp (s, "Emblems") == 0) return CTX_EMBLEMS;
    if (g_ascii_strcasecmp (s, "Categories") == 0) return CTX_CATEGORIES;
    return CTX_OTHER;
}

static void
add_base (GPtrArray *bases, const gchar *path)
{
    guint i;
    for (i = 0; i < bases->len; i++)
        if (g_strcmp0 (g_ptr_array_index (bases, i), path) == 0) return;
    g_ptr_array_add (bases, g_strdup (path));
}

static GPtrArray *
icon_base_dirs (void)
{
    GPtrArray *bases = g_ptr_array_new_with_free_func (g_free);
    const gchar *const *data = g_get_system_data_dirs ();
    gchar *p;
    guint i;
    p = g_build_filename (g_get_user_data_dir (), "icons", NULL);
    add_base (bases, p); g_free (p);
    p = g_build_filename (g_get_home_dir (), ".icons", NULL);
    add_base (bases, p); g_free (p);
    for (i = 0; data != NULL && data[i] != NULL; i++) {
        p = g_build_filename (data[i], "icons", NULL);
        add_base (bases, p); g_free (p);
    }
    add_base (bases, "/usr/local/share/icons");
    add_base (bases, "/usr/share/icons");
    return bases;
}

/* Reads one index.theme. Returns NULL for anything that is not a real,
 * visible icon theme (missing or broken index, Hidden, no Name, cursor-only). */
static IconPack *
icon_pack_read (const gchar *theme_path, const gchar *dir)
{
    GKeyFile *kf = g_key_file_new ();
    gchar *index = g_build_filename (theme_path, "index.theme", NULL);
    IconPack *pack = NULL;
    gchar **dirs = NULL, **scaled = NULL, *name = NULL;
    gsize n = 0, n_scaled = 0, i;

    g_key_file_set_list_separator (kf, ',');
    if (!g_key_file_load_from_file (kf, index, G_KEY_FILE_NONE, NULL))
        goto out;
    if (g_key_file_get_boolean (kf, "Icon Theme", "Hidden", NULL))
        goto out;
    name = g_key_file_get_locale_string (kf, "Icon Theme", "Name", NULL, NULL);
    if (name == NULL || *g_strstrip (name) == '\0')
        goto out;
    dirs = g_key_file_get_string_list (kf, "Icon Theme", "Directories", &n, NULL);
    scaled = g_key_file_get_string_list (kf, "Icon Theme", "ScaledDirectories", &n_scaled, NULL);
    if (n + n_scaled == 0)
        goto out;

    pack = g_new0 (IconPack, 1);
    pack->dir = g_strdup (dir);
    pack->name = name; name = NULL;
    pack->bases = g_ptr_array_new_with_free_func (g_free);
    pack->subdirs = g_ptr_array_new_with_free_func (g_free);
    pack->contexts = g_array_new (FALSE, FALSE, sizeof (guint8));
    pack->sizes = g_array_new (FALSE, FALSE, sizeof (gint));
    for (i = 0; i < n + n_scaled; i++) {
        const gchar *sub = i < n ? dirs[i] : scaled[i - n];
        gchar *ctx, *type;
        guint8 c;
        gint size;
        if (sub == NULL || *sub == '\0' || strstr (sub, "..") != NULL) continue;
        ctx = g_key_file_get_string (kf, sub, "Context", NULL);
        type = g_key_file_get_string (kf, sub, "Type", NULL);
        size = g_key_file_get_integer (kf, sub, "Size", NULL);
        if (g_strcmp0 (type, "Scalable") == 0) size = 512;
        c = (guint8) context_from_string (ctx);
        g_ptr_array_add (pack->subdirs, g_strdup (sub));
        g_array_append_val (pack->contexts, c);
        g_array_append_val (pack->sizes, size);
        g_free (ctx);
        g_free (type);
    }
out:
    g_strfreev (dirs);
    g_strfreev (scaled);
    g_free (name);
    g_free (index);
    g_key_file_unref (kf);
    return pack;
}

/* "folder-music.svg" -> "folder-music"; "x.symbolic.png" -> "x-symbolic" */
static gchar *
icon_name_from_file (const gchar *file, gboolean *is_svg)
{
    gsize len = strlen (file);
    *is_svg = FALSE;
    if (len > 13 && g_str_has_suffix (file, ".symbolic.png")) {
        gchar *stem = g_strndup (file, len - 13);
        gchar *name = g_strconcat (stem, "-symbolic", NULL);
        g_free (stem);
        return name;
    }
    if (len > 4 && g_str_has_suffix (file, ".svg")) {
        *is_svg = TRUE;
        return g_strndup (file, len - 4);
    }
    if (len > 4 && (g_str_has_suffix (file, ".png") || g_str_has_suffix (file, ".xpm")))
        return g_strndup (file, len - 4);
    return NULL;
}

static gint
icon_entry_compare (gconstpointer a, gconstpointer b)
{
    const IconEntry *x = a, *y = b;
    gint r = strcmp (x->name, y->name);
    return r != 0 ? r : (gint) x->pack - (gint) y->pack;
}

static void
icon_index_thread (GTask *task, gpointer source, gpointer data, GCancellable *cancellable)
{
    IconIndex *idx = g_new0 (IconIndex, 1);
    GPtrArray *bases = icon_base_dirs ();
    GHashTable *seen = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
    guint b, p;

    idx->packs = g_ptr_array_new_with_free_func ((GDestroyNotify) icon_pack_free);
    idx->entries = g_array_new (FALSE, FALSE, sizeof (IconEntry));

    for (b = 0; b < bases->len; b++) {
        const gchar *base = g_ptr_array_index (bases, b);
        GDir *d = g_dir_open (base, 0, NULL);
        const gchar *dir;
        if (d == NULL) continue;
        while ((dir = g_dir_read_name (d)) != NULL) {
            gchar *path = g_build_filename (base, dir, NULL);
            gpointer known;
            if (!g_file_test (path, G_FILE_TEST_IS_DIR)) { g_free (path); continue; }
            if (g_hash_table_lookup_extended (seen, dir, NULL, &known)) {
                /* the same theme in a later folder adds icons (GTK merges them) */
                gint at = GPOINTER_TO_INT (known) - 1;
                if (at >= 0)
                    add_base (((IconPack *) g_ptr_array_index (idx->packs, at))->bases, path);
                g_free (path);
                continue;
            }
            {
                gchar *index = g_build_filename (path, "index.theme", NULL);
                gboolean has_index = g_file_test (index, G_FILE_TEST_IS_REGULAR);
                g_free (index);
                if (!has_index) { g_free (path); continue; }
            }
            {
                IconPack *pack = icon_pack_read (path, dir);
                if (pack != NULL) {
                    add_base (pack->bases, path);
                    g_ptr_array_add (idx->packs, pack);
                    g_hash_table_insert (seen, g_strdup (dir), GINT_TO_POINTER (idx->packs->len));
                } else {
                    g_hash_table_insert (seen, g_strdup (dir), GINT_TO_POINTER (0));
                }
            }
            g_free (path);
        }
        g_dir_close (d);
    }

    for (p = 0; p < idx->packs->len && !idx->truncated; p++) {
        IconPack *pack = g_ptr_array_index (idx->packs, p);
        GHashTable *by_name = g_hash_table_new (g_str_hash, g_str_equal);
        guint s;
        if (g_cancellable_is_cancelled (cancellable)) break;
        for (b = 0; b < pack->bases->len; b++) {
            gchar *theme_real = realpath (g_ptr_array_index (pack->bases, b), NULL);
            for (s = 0; s < pack->subdirs->len; s++) {
                gchar *folder = g_build_filename (g_ptr_array_index (pack->bases, b),
                                                  g_ptr_array_index (pack->subdirs, s), NULL);
                GDir *d = g_dir_open (folder, 0, NULL);
                /* Papirus-Dark links most size folders to ../Papirus: those
                 * icons are shown once under "All installed icon packs" */
                gboolean own = TRUE;
                if (d != NULL && theme_real != NULL) {
                    gchar *folder_real = realpath (folder, NULL);
                    gsize tl = strlen (theme_real);
                    own = folder_real == NULL ||
                          (strncmp (folder_real, theme_real, tl) == 0 && folder_real[tl] == '/');
                    free (folder_real);
                }
                const gchar *file;
                gint size = g_array_index (pack->sizes, gint, s);
                /* prefer scalable art, then the size closest to 48 px */
                gint16 score_base = (gint16) (size >= 512 ? 2000 : 1000 - ABS (size - 48));
                if (d == NULL) { g_free (folder); continue; }
                while ((file = g_dir_read_name (d)) != NULL) {
                    gboolean svg;
                    gchar *name = icon_name_from_file (file, &svg);
                    gpointer at;
                    gint16 score;
                    if (name == NULL) continue;
                    score = score_base + (svg ? 1 : 0);
                    if (g_hash_table_lookup_extended (by_name, name, NULL, &at)) {
                        IconEntry *e = &g_array_index (idx->entries, IconEntry, GPOINTER_TO_UINT (at));
                        if (own) e->shared = 0;
                        if (score > e->score) {
                            g_free (e->path);
                            e->path = g_build_filename (folder, file, NULL);
                            e->score = score;
                            e->context = g_array_index (pack->contexts, guint8, s);
                        }
                        g_free (name);
                        continue;
                    }
                    if (idx->entries->len >= DAKKAR_ICON_INDEX_MAX) {
                        idx->truncated = TRUE;
                        g_free (name);
                        break;
                    }
                    {
                        IconEntry e;
                        e.name = name;
                        e.path = g_build_filename (folder, file, NULL);
                        e.pack = (guint16) p;
                        e.context = g_array_index (pack->contexts, guint8, s);
                        e.score = score;
                        e.shared = own ? 0 : 1;
                        g_array_append_val (idx->entries, e);
                        g_hash_table_insert (by_name, e.name,
                                             GUINT_TO_POINTER (idx->entries->len - 1));
                    }
                }
                g_dir_close (d);
                g_free (folder);
            }
            free (theme_real);
        }
        g_hash_table_unref (by_name);
    }
    g_array_sort (idx->entries, icon_entry_compare);
    g_hash_table_unref (seen);
    g_ptr_array_unref (bases);
    g_task_return_pointer (task, idx, NULL);
    (void) source; (void) data;
}

static void
icon_index_done (GObject *source, GAsyncResult *result, gpointer data)
{
    GList *waiters, *l;
    the_index = g_task_propagate_pointer (G_TASK (result), NULL);
    index_running = FALSE;
    waiters = index_waiters;
    index_waiters = NULL;
    for (l = waiters; l != NULL; l = l->next) {
        g_task_return_boolean (l->data, the_index != NULL);
        g_object_unref (l->data);
    }
    g_list_free (waiters);
    (void) source; (void) data;
}

/* Calls @callback on the main loop once the index exists. */
static void
icon_index_ensure (GAsyncReadyCallback callback, gpointer data)
{
    GTask *waiter = g_task_new (NULL, NULL, callback, data);
    if (the_index != NULL) {
        g_task_return_boolean (waiter, TRUE);
        g_object_unref (waiter);
        return;
    }
    index_waiters = g_list_append (index_waiters, waiter);
    if (!index_running) {
        GTask *task = g_task_new (NULL, NULL, icon_index_done, NULL);
        index_running = TRUE;
        g_task_run_in_thread (task, icon_index_thread);
        g_object_unref (task);
    }
}

/* ---------- specs ---------- */

static gboolean
spec_parse (const gchar *spec, gchar **theme, gchar **name, gchar **path)
{
    *theme = *name = *path = NULL;
    if (spec == NULL) return FALSE;
    if (g_str_has_prefix (spec, "theme:")) {
        gchar **parts = g_strsplit (spec + 6, "|", 3);
        gboolean ok = g_strv_length (parts) >= 2 && parts[0][0] != '\0' && parts[1][0] != '\0';
        if (ok) {
            *theme = g_strdup (parts[0]);
            *name = g_strdup (parts[1]);
            *path = parts[2] != NULL && parts[2][0] != '\0' ? g_strdup (parts[2]) : NULL;
        }
        g_strfreev (parts);
        return ok;
    }
    if (g_str_has_prefix (spec, "file:") && g_path_is_absolute (spec + 5)) {
        *path = g_strdup (spec + 5);
        return TRUE;
    }
    return FALSE;
}

gboolean
dakkar_icon_spec_is_valid (const gchar *spec)
{
    gchar *t, *n, *p;
    gboolean ok = spec_parse (spec, &t, &n, &p);
    g_free (t); g_free (n); g_free (p);
    return ok;
}

/* A private GtkIconTheme per installed theme: an icon of a theme that is
 * not the active one is still looked up at the right size. */
static GtkIconTheme *
theme_instance (const gchar *dir)
{
    static GHashTable *themes;
    GtkIconTheme *theme;
    if (themes == NULL)
        themes = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);
    theme = g_hash_table_lookup (themes, dir);
    if (theme == NULL) {
        theme = gtk_icon_theme_new ();
        if (gdk_screen_get_default () != NULL)
            gtk_icon_theme_set_screen (theme, gdk_screen_get_default ());
        gtk_icon_theme_set_custom_theme (theme, dir);
        g_hash_table_insert (themes, g_strdup (dir), theme);
    }
    return theme;
}

static GtkIconInfo *
file_icon_info (const gchar *path, gint size, gint scale)
{
    GFile *file;
    GIcon *icon;
    GtkIconInfo *info;
    if (path == NULL || !g_file_test (path, G_FILE_TEST_IS_REGULAR)) return NULL;
    file = g_file_new_for_path (path);
    icon = g_file_icon_new (file);
    info = gtk_icon_theme_lookup_by_gicon_for_scale (gtk_icon_theme_get_default (), icon,
                                                     size, scale, GTK_ICON_LOOKUP_FORCE_SIZE);
    g_object_unref (icon);
    g_object_unref (file);
    return info;
}

static GdkPixbuf *
load_info (GtkIconInfo *info, GtkStyleContext *context, const GdkRGBA *tint,
           gboolean *out_symbolic)
{
    GdkPixbuf *pixbuf = NULL;
    gboolean symbolic = gtk_icon_info_is_symbolic (info);
    if (symbolic && tint != NULL)
        pixbuf = gtk_icon_info_load_symbolic (info, tint, NULL, NULL, NULL, NULL, NULL);
    else if (symbolic && context != NULL)
        pixbuf = gtk_icon_info_load_symbolic_for_context (info, context, NULL, NULL);
    if (pixbuf == NULL)
        pixbuf = gtk_icon_info_load_icon (info, NULL);
    if (out_symbolic != NULL) *out_symbolic = symbolic;
    return pixbuf;
}

GdkPixbuf *
dakkar_icon_spec_load (const gchar *spec, gint size, gint scale,
                      GtkStyleContext *context, const GdkRGBA *tint,
                      gboolean *out_symbolic)
{
    gchar *t, *n, *p;
    GtkIconInfo *info = NULL;
    GdkPixbuf *pixbuf = NULL;
    if (out_symbolic != NULL) *out_symbolic = FALSE;
    if (!spec_parse (spec, &t, &n, &p)) return NULL;
    size = CLAMP (size, 8, 512);
    scale = CLAMP (scale, 1, 4);
    /* The file shown in the picker is drawn, scaled to the sidebar size,
     * so the sidebar matches the preview (Papirus' own 16px places icons
     * are grey glyphs, not the folder the user picked). If the theme was
     * updated and the file is gone, the theme lookup finds the icon again. */
    info = file_icon_info (p, size, scale);
    if (info == NULL && t != NULL && n != NULL)
        info = gtk_icon_theme_lookup_icon_for_scale (theme_instance (t), n, size, scale,
                                                     GTK_ICON_LOOKUP_FORCE_SIZE);
    if (info != NULL) {
        pixbuf = load_info (info, context, tint, out_symbolic);
        g_object_unref (info);
    }
    g_free (t); g_free (n); g_free (p);
    return pixbuf;
}

/* ---------- dialog ---------- */

enum { COL_ENTRY, COL_SURFACE, COL_LOADED, COL_TIP, N_COLS };

typedef struct {
    GtkWidget *dialog;
    GtkWidget *search;
    GtkWidget *pack_combo;
    GtkWidget *context_combo;
    GtkWidget *view;
    GtkWidget *scroll;
    GtkWidget *status;
    GtkWidget *spinner;
    GtkWidget *preview;
    GtkWidget *preview_name;
    GtkWidget *preview_theme;
    GtkWidget *apply;
    GtkListStore *store;
    gchar *spec;                 /* the chosen spec */
    gchar *current_spec;
    guint load_idle;
    guint refilter_timeout;
    gboolean alive;
    gint ref;
} Picker;

static void
picker_unref (Picker *pk)
{
    if (--pk->ref > 0) return;
    g_free (pk->spec);
    g_free (pk->current_spec);
    g_free (pk);
}

static void
picker_set_preview (Picker *pk, const gchar *spec)
{
    GdkPixbuf *pixbuf;
    gint scale = gtk_widget_get_scale_factor (pk->preview);
    gchar *t, *n, *p;
    pixbuf = spec != NULL ? dakkar_icon_spec_load (spec, 64, scale,
                 gtk_widget_get_style_context (pk->preview), NULL, NULL) : NULL;
    if (pixbuf != NULL) {
        cairo_surface_t *surface = gdk_cairo_surface_create_from_pixbuf (pixbuf, scale, NULL);
        gtk_image_set_from_surface (GTK_IMAGE (pk->preview), surface);
        cairo_surface_destroy (surface);
        g_object_unref (pixbuf);
    } else {
        gtk_image_clear (GTK_IMAGE (pk->preview));
    }
    if (spec_parse (spec, &t, &n, &p)) {
        gchar *base = p != NULL ? g_path_get_basename (p) : NULL;
        const gchar *theme_name = t;
        guint i;
        for (i = 0; the_index != NULL && t != NULL && i < the_index->packs->len; i++) {
            IconPack *pack = g_ptr_array_index (the_index->packs, i);
            if (g_strcmp0 (pack->dir, t) == 0) theme_name = pack->name;
        }
        gtk_label_set_text (GTK_LABEL (pk->preview_name), n != NULL ? n : base);
        gtk_label_set_text (GTK_LABEL (pk->preview_theme),
                            t != NULL ? theme_name : _("Image file"));
        gtk_widget_set_tooltip_text (pk->preview_theme, p);
        g_free (base);
    } else {
        gtk_label_set_text (GTK_LABEL (pk->preview_name), _("No icon selected"));
        gtk_label_set_text (GTK_LABEL (pk->preview_theme), "");
    }
    g_free (t); g_free (n); g_free (p);
}

static void
picker_choose (Picker *pk, gchar *spec /* owned */)
{
    g_free (pk->spec);
    pk->spec = spec;
    picker_set_preview (pk, spec);
    gtk_widget_set_sensitive (pk->apply, spec != NULL);
}

/* Loads the images of the visible rows (plus a little look-ahead), a few
 * per idle iteration so scrolling never blocks. */
static gboolean
picker_load_visible (gpointer data)
{
    Picker *pk = data;
    GtkTreePath *start = NULL, *end = NULL;
    GtkTreeModel *model;
    gint first, last, i, loaded = 0;
    gint scale;
    GtkStyleContext *ctx;

    if (!pk->alive || pk->store == NULL ||
        !gtk_icon_view_get_visible_range (GTK_ICON_VIEW (pk->view), &start, &end)) {
        pk->load_idle = 0;
        return G_SOURCE_REMOVE;
    }
    model = GTK_TREE_MODEL (pk->store);
    first = gtk_tree_path_get_indices (start)[0];
    last = gtk_tree_path_get_indices (end)[0] + 48;
    gtk_tree_path_free (start);
    gtk_tree_path_free (end);
    scale = gtk_widget_get_scale_factor (pk->view);
    ctx = gtk_widget_get_style_context (pk->view);
    for (i = first; i <= last && loaded < 24; i++) {
        GtkTreeIter iter;
        gboolean done = FALSE;
        guint entry = 0;
        if (!gtk_tree_model_iter_nth_child (model, &iter, NULL, i)) break;
        gtk_tree_model_get (model, &iter, COL_LOADED, &done, COL_ENTRY, &entry, -1);
        if (done) continue;
        {
            IconEntry *e = &g_array_index (the_index->entries, IconEntry, entry);
            GtkIconInfo *info = file_icon_info (e->path, 32, scale);
            GdkPixbuf *pixbuf = info != NULL ? load_info (info, ctx, NULL, NULL) : NULL;
            cairo_surface_t *surface = pixbuf != NULL ?
                gdk_cairo_surface_create_from_pixbuf (pixbuf, scale, NULL) : NULL;
            gtk_list_store_set (pk->store, &iter, COL_SURFACE, surface, COL_LOADED, TRUE, -1);
            if (surface != NULL) cairo_surface_destroy (surface);
            g_clear_object (&pixbuf);
            g_clear_object (&info);
        }
        loaded++;
    }
    if (loaded > 0) return G_SOURCE_CONTINUE;
    pk->load_idle = 0;
    return G_SOURCE_REMOVE;
}

static void
picker_queue_load (Picker *pk)
{
    if (pk->load_idle == 0 && pk->alive)
        pk->load_idle = g_idle_add_full (G_PRIORITY_LOW, picker_load_visible, pk, NULL);
}

/* every search word must occur in the name ("folder mus" finds folder-music) */
static gboolean
picker_matches (const gchar *name, gchar **words)
{
    guint i;
    for (i = 0; words != NULL && words[i] != NULL; i++)
        if (words[i][0] != '\0' && strstr (name, words[i]) == NULL)
            return FALSE;
    return TRUE;
}

static void
picker_refill (Picker *pk)
{
    const gchar *pack_id = gtk_combo_box_get_active_id (GTK_COMBO_BOX (pk->pack_combo));
    const gchar *ctx_id = gtk_combo_box_get_active_id (GTK_COMBO_BOX (pk->context_combo));
    gchar *query = g_utf8_strdown (gtk_entry_get_text (GTK_ENTRY (pk->search)), -1);
    gchar **words;
    gint pack_filter = -1, ctx_filter = ctx_id != NULL ? atoi (ctx_id) : -1;
    GtkListStore *store;
    guint i, shown = 0;
    gchar *text;

    if (the_index == NULL) { g_free (query); return; }
    g_strdelimit (query, "-_", ' ');
    words = g_strsplit (g_strstrip (query), " ", -1);
    if (pack_id != NULL && g_strcmp0 (pack_id, "*") != 0)
        for (i = 0; i < the_index->packs->len; i++)
            if (g_strcmp0 (((IconPack *) g_ptr_array_index (the_index->packs, i))->dir, pack_id) == 0)
                pack_filter = (gint) i;

    store = gtk_list_store_new (N_COLS, G_TYPE_UINT, CAIRO_GOBJECT_TYPE_SURFACE,
                                G_TYPE_BOOLEAN, G_TYPE_STRING);
    for (i = 0; i < the_index->entries->len; i++) {
        IconEntry *e = &g_array_index (the_index->entries, IconEntry, i);
        IconPack *pack;
        gchar *tip;
        if (pack_filter >= 0 && e->pack != pack_filter) continue;
        if (pack_filter < 0 && e->shared) continue;
        if (ctx_filter >= 0 && e->context != ctx_filter) continue;
        if (!picker_matches (e->name, words)) continue;
        pack = g_ptr_array_index (the_index->packs, e->pack);
        tip = g_markup_printf_escaped ("<b>%s</b>\n%s", e->name, pack->name);
        gtk_list_store_insert_with_values (store, NULL, -1, COL_ENTRY, i,
                                           COL_LOADED, FALSE, COL_TIP, tip, -1);
        g_free (tip);
        shown++;
    }
    g_strfreev (words);
    g_free (query);

    gtk_icon_view_set_model (GTK_ICON_VIEW (pk->view), GTK_TREE_MODEL (store));
    g_clear_object (&pk->store);
    pk->store = store;
    gtk_adjustment_set_value (gtk_scrolled_window_get_vadjustment (
        GTK_SCROLLED_WINDOW (pk->scroll)), 0.0);
    text = g_strdup_printf (ngettext ("%u icon", "%u icons", shown), shown);
    gtk_label_set_text (GTK_LABEL (pk->status), text);
    g_free (text);
    picker_queue_load (pk);
}

static gboolean
picker_refill_timeout (gpointer data)
{
    Picker *pk = data;
    pk->refilter_timeout = 0;
    if (pk->alive) picker_refill (pk);
    return G_SOURCE_REMOVE;
}

/* debounced: typing quickly rebuilds the grid once */
static void
picker_queue_refill (Picker *pk)
{
    if (pk->refilter_timeout != 0) g_source_remove (pk->refilter_timeout);
    pk->refilter_timeout = g_timeout_add (180, picker_refill_timeout, pk);
}

static void
picker_search_changed (GtkSearchEntry *entry, Picker *pk)
{
    picker_queue_refill (pk);
    (void) entry;
}

static void
picker_filter_changed (GtkComboBox *combo, Picker *pk)
{
    picker_refill (pk);
    (void) combo;
}

static void
picker_selection_changed (GtkIconView *view, Picker *pk)
{
    GList *sel = gtk_icon_view_get_selected_items (view);
    GtkTreeIter iter;
    if (sel != NULL && pk->store != NULL &&
        gtk_tree_model_get_iter (GTK_TREE_MODEL (pk->store), &iter, sel->data)) {
        guint entry = 0;
        IconEntry *e;
        IconPack *pack;
        gtk_tree_model_get (GTK_TREE_MODEL (pk->store), &iter, COL_ENTRY, &entry, -1);
        e = &g_array_index (the_index->entries, IconEntry, entry);
        pack = g_ptr_array_index (the_index->packs, e->pack);
        picker_choose (pk, g_strdup_printf ("theme:%s|%s|%s", pack->dir, e->name, e->path));
    }
    g_list_free_full (sel, (GDestroyNotify) gtk_tree_path_free);
}

static void
picker_item_activated (GtkIconView *view, GtkTreePath *path, Picker *pk)
{
    if (pk->spec != NULL)
        gtk_dialog_response (GTK_DIALOG (pk->dialog), GTK_RESPONSE_APPLY);
    (void) view; (void) path;
}

static void
picker_choose_file (GtkButton *button, Picker *pk)
{
    GtkWidget *chooser = gtk_file_chooser_dialog_new (_("Choose Image File"),
        GTK_WINDOW (pk->dialog), GTK_FILE_CHOOSER_ACTION_OPEN,
        _("_Cancel"), GTK_RESPONSE_CANCEL, _("_Open"), GTK_RESPONSE_ACCEPT, NULL);
    GtkFileFilter *filter = gtk_file_filter_new ();
    gtk_file_filter_set_name (filter, _("Images (PNG, SVG)"));
    gtk_file_filter_add_mime_type (filter, "image/png");
    gtk_file_filter_add_mime_type (filter, "image/svg+xml");
    gtk_file_filter_add_pattern (filter, "*.png");
    gtk_file_filter_add_pattern (filter, "*.svg");
    gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (chooser), filter);
    if (gtk_dialog_run (GTK_DIALOG (chooser)) == GTK_RESPONSE_ACCEPT) {
        gchar *path = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (chooser));
        if (path != NULL) {
            GdkPixbuf *probe = gdk_pixbuf_new_from_file_at_size (path, 16, 16, NULL);
            if (probe != NULL) {
                g_object_unref (probe);
                gtk_icon_view_unselect_all (GTK_ICON_VIEW (pk->view));
                picker_choose (pk, g_strconcat ("file:", path, NULL));
            } else {
                gtk_label_set_text (GTK_LABEL (pk->preview_theme),
                                    _("This file is not an image that can be shown."));
            }
            g_free (path);
        }
    }
    gtk_widget_destroy (chooser);
    (void) button;
}

static void
picker_scrolled (GtkAdjustment *adj, Picker *pk)
{
    picker_queue_load (pk);
    (void) adj;
}

static void
picker_index_ready (GObject *source, GAsyncResult *result, gpointer data)
{
    Picker *pk = data;
    gboolean ok = g_task_propagate_boolean (G_TASK (result), NULL);
    if (pk->alive) {
        GtkSettings *settings = gtk_settings_get_default ();
        gchar *current = NULL;
        guint i;
        gtk_spinner_stop (GTK_SPINNER (pk->spinner));
        gtk_widget_hide (pk->spinner);
        if (settings != NULL)
            g_object_get (settings, "gtk-icon-theme-name", &current, NULL);
        g_signal_handlers_block_by_func (pk->pack_combo, picker_filter_changed, pk);
        /* the active theme first, marked */
        for (i = 0; ok && i < the_index->packs->len; i++) {
            IconPack *pack = g_ptr_array_index (the_index->packs, i);
            if (g_strcmp0 (pack->dir, current) == 0) {
                /* Translators: the icon theme that is in use now */
                gchar *label = g_strdup_printf (_("%s (current)"), pack->name);
                gtk_combo_box_text_append (GTK_COMBO_BOX_TEXT (pk->pack_combo), pack->dir, label);
                g_free (label);
            }
        }
        for (i = 0; ok && i < the_index->packs->len; i++) {
            IconPack *pack = g_ptr_array_index (the_index->packs, i);
            if (g_strcmp0 (pack->dir, current) != 0)
                gtk_combo_box_text_append (GTK_COMBO_BOX_TEXT (pk->pack_combo), pack->dir, pack->name);
        }
        g_signal_handlers_unblock_by_func (pk->pack_combo, picker_filter_changed, pk);
        g_free (current);
        gtk_widget_set_sensitive (pk->search, TRUE);
        gtk_widget_set_sensitive (pk->pack_combo, TRUE);
        gtk_widget_set_sensitive (pk->context_combo, TRUE);
        if (ok) picker_refill (pk);
        else gtk_label_set_text (GTK_LABEL (pk->status), _("No icon themes were found."));
        if (pk->current_spec != NULL) picker_set_preview (pk, pk->current_spec);
        gtk_widget_grab_focus (pk->search);
    }
    picker_unref (pk);
    (void) source;
}

static void
picker_allocated (GtkWidget *widget, GdkRectangle *allocation, Picker *pk)
{
    picker_queue_load (pk);
    (void) widget; (void) allocation;
}

gboolean
dakkar_icon_spec_parse (const gchar *spec, gchar **theme, gchar **name, gchar **path)
{
    return spec_parse (spec, theme, name, path);
}

gchar *
dakkar_icon_picker_run (GtkWindow *parent, const gchar *title, const gchar *current_spec)
{
    return dakkar_icon_picker_run_scoped (parent, title, current_spec, NULL, NULL, FALSE, NULL, NULL);
}

/* Search entry, icon pack and category filters. */
static GtkWidget *
picker_filter_bar_new (Picker *pk)
{
    GtkWidget *top;
    static const struct { gint ctx; const gchar *label; } contexts[] = {
        { -1, N_("All categories") }, { CTX_PLACES, N_("Places") },
        { CTX_DEVICES, N_("Devices") }, { CTX_APPS, N_("Apps") },
        { CTX_MIME, N_("File types") }, { CTX_ACTIONS, N_("Actions") },
        { CTX_STATUS, N_("Status") }, { CTX_EMBLEMS, N_("Emblems") },
        { CTX_CATEGORIES, N_("Categories") }, { CTX_OTHER, N_("Other") },
    };
    guint i;

    top = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
    pk->search = gtk_search_entry_new ();
    gtk_entry_set_placeholder_text (GTK_ENTRY (pk->search), _("Search icons by name…"));
    gtk_widget_set_hexpand (pk->search, TRUE);
    pk->pack_combo = gtk_combo_box_text_new ();
    gtk_combo_box_text_append (GTK_COMBO_BOX_TEXT (pk->pack_combo), "*", _("All installed icon packs"));
    gtk_combo_box_set_active_id (GTK_COMBO_BOX (pk->pack_combo), "*");
    gtk_widget_set_tooltip_text (pk->pack_combo, _("Icon pack"));
    pk->context_combo = gtk_combo_box_text_new ();
    for (i = 0; i < G_N_ELEMENTS (contexts); i++) {
        gchar *id = g_strdup_printf ("%d", contexts[i].ctx);
        gtk_combo_box_text_append (GTK_COMBO_BOX_TEXT (pk->context_combo), id, _(contexts[i].label));
        g_free (id);
    }
    gtk_combo_box_set_active_id (GTK_COMBO_BOX (pk->context_combo), "-1");
    gtk_widget_set_tooltip_text (pk->context_combo, _("Category"));
    gtk_box_pack_start (GTK_BOX (top), pk->search, TRUE, TRUE, 0);
    gtk_box_pack_start (GTK_BOX (top), pk->pack_combo, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (top), pk->context_combo, FALSE, FALSE, 0);
    return top;
}

/* The icon grid in its scroller (pk->view, pk->scroll). */
static void
picker_grid_init (Picker *pk)
{
    GtkCellRenderer *cell;

    pk->view = gtk_icon_view_new ();
    gtk_icon_view_set_selection_mode (GTK_ICON_VIEW (pk->view), GTK_SELECTION_BROWSE);
    gtk_icon_view_set_item_width (GTK_ICON_VIEW (pk->view), 40);
    gtk_icon_view_set_item_padding (GTK_ICON_VIEW (pk->view), 4);
    gtk_icon_view_set_spacing (GTK_ICON_VIEW (pk->view), 0);
    gtk_icon_view_set_row_spacing (GTK_ICON_VIEW (pk->view), 2);
    gtk_icon_view_set_column_spacing (GTK_ICON_VIEW (pk->view), 2);
    gtk_icon_view_set_margin (GTK_ICON_VIEW (pk->view), 6);
    gtk_icon_view_set_tooltip_column (GTK_ICON_VIEW (pk->view), COL_TIP);
    cell = gtk_cell_renderer_pixbuf_new ();
    /* every item has the same size: layout stays cheap for huge packs */
    gtk_cell_renderer_set_fixed_size (cell, 40, 40);
    gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (pk->view), cell, TRUE);
    gtk_cell_layout_add_attribute (GTK_CELL_LAYOUT (pk->view), cell, "surface", COL_SURFACE);
    pk->scroll = gtk_scrolled_window_new (NULL, NULL);
    gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (pk->scroll), GTK_SHADOW_IN);
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (pk->scroll),
                                    GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_container_add (GTK_CONTAINER (pk->scroll), pk->view);
    gtk_widget_set_hexpand (pk->scroll, TRUE);
    gtk_widget_set_vexpand (pk->scroll, TRUE);
}

/* Preview pane: the chosen icon large, its name and its theme. */
static GtkWidget *
picker_preview_new (Picker *pk)
{
    GtkWidget *side;

    side = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_size_request (side, 190, -1);
    {
        GtkWidget *caption = gtk_label_new (_("Preview"));
        gtk_style_context_add_class (gtk_widget_get_style_context (caption), "dim-label");
        gtk_label_set_xalign (GTK_LABEL (caption), 0.0);
        gtk_box_pack_start (GTK_BOX (side), caption, FALSE, FALSE, 0);
    }
    pk->preview = gtk_image_new ();
    gtk_widget_set_size_request (pk->preview, 96, 96);
    gtk_box_pack_start (GTK_BOX (side), pk->preview, FALSE, FALSE, 6);
    pk->preview_name = gtk_label_new ("");
    gtk_label_set_line_wrap (GTK_LABEL (pk->preview_name), TRUE);
    gtk_label_set_line_wrap_mode (GTK_LABEL (pk->preview_name), PANGO_WRAP_WORD_CHAR);
    gtk_label_set_max_width_chars (GTK_LABEL (pk->preview_name), 22);
    gtk_label_set_selectable (GTK_LABEL (pk->preview_name), TRUE);
    {
        PangoAttrList *attrs = pango_attr_list_new ();
        pango_attr_list_insert (attrs, pango_attr_weight_new (PANGO_WEIGHT_BOLD));
        gtk_label_set_attributes (GTK_LABEL (pk->preview_name), attrs);
        pango_attr_list_unref (attrs);
    }
    gtk_box_pack_start (GTK_BOX (side), pk->preview_name, FALSE, FALSE, 0);
    pk->preview_theme = gtk_label_new ("");
    gtk_label_set_line_wrap (GTK_LABEL (pk->preview_theme), TRUE);
    gtk_label_set_max_width_chars (GTK_LABEL (pk->preview_theme), 22);
    gtk_style_context_add_class (gtk_widget_get_style_context (pk->preview_theme), "dim-label");
    gtk_box_pack_start (GTK_BOX (side), pk->preview_theme, FALSE, FALSE, 0);
    return side;
}

/* Spinner, status line and the Choose Image File button (returned). */
static GtkWidget *
picker_status_row_add (Picker *pk, GtkWidget *content)
{
    GtkWidget *bottom, *button;

    bottom = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
    pk->spinner = gtk_spinner_new ();
    pk->status = gtk_label_new (_("Indexing icon packs…"));
    gtk_style_context_add_class (gtk_widget_get_style_context (pk->status), "dim-label");
    button = gtk_button_new_with_mnemonic (_("Choose _Image File…"));
    gtk_widget_set_tooltip_text (button, _("Use your own PNG or SVG image"));
    gtk_box_pack_start (GTK_BOX (bottom), pk->spinner, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (bottom), pk->status, FALSE, FALSE, 0);
    gtk_box_pack_end (GTK_BOX (bottom), button, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (content), bottom, FALSE, FALSE, 0);
    return button;
}

/* Files only: apply to the whole file type (default) or to this file.
 * Returns the "whole type" radio. */
static GtkWidget *
picker_scope_add (GtkWidget *content, const gchar *type_choice, const gchar *single_choice,
                  gboolean type_allowed, const gchar *type_blocked_reason,
                  gboolean *inout_for_type)
{
    GtkWidget *type_radio;
    GtkWidget *scope = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget *caption = gtk_label_new (_("Apply to:"));
    GtkWidget *single;
    gtk_style_context_add_class (gtk_widget_get_style_context (scope), "dakkar-icon-scope");
    type_radio = gtk_radio_button_new_with_mnemonic (NULL, type_choice);
    single = gtk_radio_button_new_with_mnemonic_from_widget (GTK_RADIO_BUTTON (type_radio),
                                                              single_choice);
    gtk_widget_set_tooltip_text (type_radio,
        _("Every file of this type gets the icon, in Dakkar and in other applications. "
          "Real thumbnails (photos, videos) are still shown while thumbnails are on."));
    gtk_widget_set_tooltip_text (single, _("Only the selected file gets the icon"));
    if (!type_allowed) {
        gtk_widget_set_sensitive (type_radio, FALSE);
        if (type_blocked_reason != NULL)
            gtk_widget_set_tooltip_text (type_radio, type_blocked_reason);
    }
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (type_allowed && *inout_for_type ? type_radio : single), TRUE);
    gtk_box_pack_start (GTK_BOX (scope), caption, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (scope), type_radio, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (scope), single, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (content), scope, FALSE, FALSE, 0);
    gtk_box_reorder_child (GTK_BOX (content), scope, 0);
    return type_radio;
}

gchar *
dakkar_icon_picker_run_scoped (GtkWindow *parent, const gchar *title, const gchar *current_spec,
                              const gchar *type_choice, const gchar *single_choice,
                              gboolean type_allowed, const gchar *type_blocked_reason,
                              gboolean *inout_for_type)
{
    GtkWidget *type_radio = NULL;
    Picker *pk = g_new0 (Picker, 1);
    GtkWidget *content, *middle, *button;
    gchar *result = NULL;

    pk->ref = 2;   /* the dialog and the index callback */
    pk->alive = TRUE;
    pk->current_spec = dakkar_icon_spec_is_valid (current_spec) ? g_strdup (current_spec) : NULL;
    pk->dialog = gtk_dialog_new_with_buttons (title != NULL ? title : _("Change Icon"), parent,
                                              GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                              _("_Cancel"), GTK_RESPONSE_CANCEL,
                                              _("_Apply"), GTK_RESPONSE_APPLY, NULL);
    gtk_widget_set_name (pk->dialog, "dakkar-icon-picker");
    dakkar_dialog_style_attach (pk->dialog);
    pk->apply = gtk_dialog_get_widget_for_response (GTK_DIALOG (pk->dialog), GTK_RESPONSE_APPLY);
    gtk_widget_set_sensitive (pk->apply, FALSE);
    gtk_dialog_set_default_response (GTK_DIALOG (pk->dialog), GTK_RESPONSE_APPLY);
    gtk_window_set_default_size (GTK_WINDOW (pk->dialog), 860, 600);
    content = gtk_dialog_get_content_area (GTK_DIALOG (pk->dialog));
    gtk_container_set_border_width (GTK_CONTAINER (content), 10);
    gtk_box_set_spacing (GTK_BOX (content), 8);

    gtk_box_pack_start (GTK_BOX (content), picker_filter_bar_new (pk), FALSE, FALSE, 0);

    middle = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 10);
    picker_grid_init (pk);
    gtk_box_pack_start (GTK_BOX (middle), pk->scroll, TRUE, TRUE, 0);
    gtk_box_pack_start (GTK_BOX (middle), picker_preview_new (pk), FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (content), middle, TRUE, TRUE, 0);
    button = picker_status_row_add (pk, content);

    if (type_choice != NULL && single_choice != NULL && inout_for_type != NULL)
        type_radio = picker_scope_add (content, type_choice, single_choice, type_allowed,
                                       type_blocked_reason, inout_for_type);

    g_signal_connect (pk->search, "search-changed", G_CALLBACK (picker_search_changed), pk);
    g_signal_connect (pk->pack_combo, "changed", G_CALLBACK (picker_filter_changed), pk);
    g_signal_connect (pk->context_combo, "changed", G_CALLBACK (picker_filter_changed), pk);
    g_signal_connect (pk->view, "selection-changed", G_CALLBACK (picker_selection_changed), pk);
    g_signal_connect (pk->view, "item-activated", G_CALLBACK (picker_item_activated), pk);
    g_signal_connect (pk->view, "size-allocate", G_CALLBACK (picker_allocated), pk);
    g_signal_connect (gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (pk->scroll)),
                      "value-changed", G_CALLBACK (picker_scrolled), pk);
    g_signal_connect (button, "clicked", G_CALLBACK (picker_choose_file), pk);

    gtk_widget_set_sensitive (pk->search, FALSE);
    gtk_widget_set_sensitive (pk->pack_combo, FALSE);
    gtk_widget_set_sensitive (pk->context_combo, FALSE);
    gtk_widget_show_all (pk->dialog);
    gtk_spinner_start (GTK_SPINNER (pk->spinner));
    picker_set_preview (pk, pk->current_spec);
    icon_index_ensure (picker_index_ready, pk);

    if (gtk_dialog_run (GTK_DIALOG (pk->dialog)) == GTK_RESPONSE_APPLY && pk->spec != NULL) {
        result = g_strdup (pk->spec);
        if (type_radio != NULL)
            *inout_for_type = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (type_radio));
    }

    pk->alive = FALSE;
    if (pk->load_idle != 0) g_source_remove (pk->load_idle);
    if (pk->refilter_timeout != 0) g_source_remove (pk->refilter_timeout);
    pk->load_idle = pk->refilter_timeout = 0;
    gtk_widget_destroy (pk->dialog);
    g_clear_object (&pk->store);
    picker_unref (pk);
    return result;
}
