/* Dakkar: Change Icon / Reset Icon / icon colour (see the header). */
#include <config.h>
#include "dakkar-custom-icons.h"
#include "dakkar-icon-picker.h"

#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <string.h>
#include <libnemo-private/nemo-file.h>
#include <libnemo-private/nemo-metadata.h>
#include <libnemo-private/nemo-global-preferences.h>

#define EXPORT_PREFIX "dakkar-custom-"

static gchar *
active_theme (void)
{
    gchar *name = NULL;
    GtkSettings *settings = gtk_settings_get_default ();
    if (settings != NULL)
        g_object_get (settings, "gtk-icon-theme-name", &name, NULL);
    return name != NULL ? name : g_strdup ("hicolor");
}

gboolean
dakkar_custom_icons_type_allowed (const gchar *mime)
{
    return mime != NULL && mime[0] != '\0' && strchr (mime, '/') != NULL &&
           !g_content_type_is_unknown (mime) &&
           g_strcmp0 (mime, "application/octet-stream") != 0 &&
           g_strcmp0 (mime, "application/x-zerosize") != 0 &&
           !g_str_has_prefix (mime, "inode/");
}

/* ---------- specs <-> storage ---------- */

/* An icon of the active theme is kept by name (it follows the theme's sizes);
 * anything else (another pack, an own image) by its image file. */
static void
spec_split (const gchar *spec, gchar **out_path, gchar **out_name)
{
    gchar *t = NULL, *n = NULL, *p = NULL, *active = active_theme ();
    *out_path = *out_name = NULL;
    if (dakkar_icon_spec_parse (spec, &t, &n, &p)) {
        if (n != NULL && (g_strcmp0 (t, active) == 0 || p == NULL))
            *out_name = g_strdup (n);
        else if (p != NULL)
            *out_path = g_strdup (p);
    }
    g_free (t); g_free (n); g_free (p); g_free (active);
}

gchar *
dakkar_custom_icons_spec_for_metadata (const gchar *icon_uri, const gchar *icon_name)
{
    if (icon_uri != NULL && icon_uri[0] != '\0') {
        gchar *path = g_str_has_prefix (icon_uri, "file:") ?
                      g_filename_from_uri (icon_uri, NULL, NULL) :
                      (g_path_is_absolute (icon_uri) ? g_strdup (icon_uri) : NULL);
        gchar *spec = path != NULL ? g_strconcat ("file:", path, NULL) : NULL;
        g_free (path);
        if (spec != NULL) return spec;
    }
    if (icon_name != NULL && icon_name[0] != '\0' && strchr (icon_name, '|') == NULL) {
        gchar *active = active_theme ();
        gchar *spec = g_strdup_printf ("theme:%s|%s|", active, icon_name);
        g_free (active);
        return spec;
    }
    return NULL;
}

static gchar *
file_spec (NemoFile *file)
{
    gchar *uri = nemo_file_get_metadata (file, NEMO_METADATA_KEY_CUSTOM_ICON, NULL);
    gchar *name = nemo_file_get_metadata (file, NEMO_METADATA_KEY_CUSTOM_ICON_NAME, NULL);
    gchar *spec = NULL;
    if (uri != NULL && !strchr (uri, ':') && nemo_file_is_directory (file)) {
        /* Nemo allows a URI relative to the folder itself */
        gchar *dir = nemo_file_get_uri (file);
        gchar *full = g_build_filename (dir, uri, NULL);
        g_free (uri);
        uri = full;
        g_free (dir);
    }
    spec = dakkar_custom_icons_spec_for_metadata (uri, name);
    g_free (uri); g_free (name);
    return spec;
}

static gboolean
file_has_own_icon (NemoFile *file)
{
    gchar *uri = nemo_file_get_metadata (file, NEMO_METADATA_KEY_CUSTOM_ICON, NULL);
    gchar *name = nemo_file_get_metadata (file, NEMO_METADATA_KEY_CUSTOM_ICON_NAME, NULL);
    gboolean has = (uri != NULL && uri[0] != '\0') || (name != NULL && name[0] != '\0');
    g_free (uri); g_free (name);
    return has;
}

static void
file_set_icon (NemoFile *file, const gchar *path, const gchar *name)
{
    gchar *uri = path != NULL ? g_filename_to_uri (path, NULL, NULL) : NULL;
    nemo_file_set_metadata (file, NEMO_METADATA_KEY_CUSTOM_ICON_NAME, NULL, name);
    nemo_file_set_metadata (file, NEMO_METADATA_KEY_CUSTOM_ICON, NULL, uri);
    nemo_file_set_metadata (file, NEMO_METADATA_KEY_ICON_SCALE, NULL, NULL);
    g_free (uri);
}

static gchar *
type_lookup (const gchar *mime)
{
    GVariant *map = g_settings_get_value (nemo_preferences, DAKKAR_TYPE_ICONS);
    const gchar *value = NULL;
    gchar *result = NULL;
    if (mime != NULL && g_variant_lookup (map, mime, "&s", &value) && value[0] != '\0')
        result = g_strdup (value);
    g_variant_unref (map);
    return result;
}

static gchar *
type_spec (const gchar *mime)
{
    gchar *value = type_lookup (mime), *spec = NULL;
    if (value != NULL)
        spec = g_path_is_absolute (value) ? g_strconcat ("file:", value, NULL)
                                          : dakkar_custom_icons_spec_for_metadata (NULL, value);
    g_free (value);
    return spec;
}

/* @value NULL removes the type */
static void
type_store (const gchar *mime, const gchar *value)
{
    GVariant *old = g_settings_get_value (nemo_preferences, DAKKAR_TYPE_ICONS);
    GVariantBuilder builder;
    GVariantIter iter;
    const gchar *k, *v;
    g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{ss}"));
    g_variant_iter_init (&iter, old);
    while (g_variant_iter_next (&iter, "{&s&s}", &k, &v))
        if (g_strcmp0 (k, mime) != 0)
            g_variant_builder_add (&builder, "{ss}", k, v);
    if (value != NULL)
        g_variant_builder_add (&builder, "{ss}", mime, value);
    g_settings_set_value (nemo_preferences, DAKKAR_TYPE_ICONS, g_variant_builder_end (&builder));
    g_variant_unref (old);
}

/* ---------- export for other applications ---------- */

static gchar *
export_icon_name (const gchar *mime)
{
    GString *s = g_string_new (EXPORT_PREFIX);
    const gchar *c;
    for (c = mime; *c != '\0'; c++)
        g_string_append_c (s, g_ascii_isalnum (*c) || *c == '.' || *c == '+' || *c == '-' || *c == '_' ? *c : '-');
    return g_string_free (s, FALSE);
}

/* The image file behind an icon string: an absolute path, or the active
 * theme's file for a name (preferably scalable). */
static gchar *
resolve_image (const gchar *value)
{
    GtkIconInfo *info;
    gchar *path = NULL;
    if (g_path_is_absolute (value))
        return g_file_test (value, G_FILE_TEST_IS_REGULAR) ? g_strdup (value) : NULL;
    info = gtk_icon_theme_lookup_icon (gtk_icon_theme_get_default (), value, 256, 0);
    if (info != NULL) {
        const gchar *f = gtk_icon_info_get_filename (info);
        if (f != NULL) path = g_strdup (f);
        g_object_unref (info);
    }
    return path;
}

static void
remove_stale (const gchar *dir, GHashTable *keep)
{
    GDir *d = g_dir_open (dir, 0, NULL);
    const gchar *entry;
    if (d == NULL) return;
    while ((entry = g_dir_read_name (d)) != NULL) {
        if ((g_str_has_prefix (entry, EXPORT_PREFIX) && !g_hash_table_contains (keep, entry)) ||
            g_str_has_prefix (entry, "dakar-custom-")) {   /* pre-rename export */
            gchar *full = g_build_filename (dir, entry, NULL);
            g_unlink (full);
            g_free (full);
        }
    }
    g_dir_close (d);
}

static void
run_tool (const gchar *tool, const gchar *arg1, const gchar *arg2, const gchar *arg3)
{
    gchar *path = g_find_program_in_path (tool);
    if (path != NULL) {
        const gchar *argv[] = { path, arg1, arg2, arg3, NULL };
        g_spawn_async (NULL, (gchar **) argv, NULL,
                       G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL,
                       NULL, NULL, NULL, NULL);
        g_free (path);
    }
}

void
dakkar_custom_icons_export (void)
{
    GVariant *map = g_settings_get_value (nemo_preferences, DAKKAR_TYPE_ICONS);
    gchar *mime_dir = g_build_filename (g_get_user_data_dir (), "mime", NULL);
    gchar *pkg_dir = g_build_filename (mime_dir, "packages", NULL);
    gchar *pkg = g_build_filename (pkg_dir, "dakkar-custom-icons.xml", NULL);
    gchar *hicolor = g_build_filename (g_get_user_data_dir (), "icons", "hicolor", NULL);
    gchar *svg_dir = g_build_filename (hicolor, "scalable", "mimetypes", NULL);
    gchar *png_dir = g_build_filename (hicolor, "256x256", "mimetypes", NULL);
    GHashTable *keep = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
    GString *xml = g_string_new ("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<!-- Written by Dakkar (Change Icon… for all files of a type). Dakkar rewrites this file;\n"
        "     reset the icons in Dakkar instead of editing it. -->\n"
        "<mime-info xmlns=\"http://www.freedesktop.org/standards/shared-mime-info\">\n");
    GVariantIter iter;
    const gchar *mime, *value;
    guint count = 0;
    gboolean had_package = g_file_test (pkg, G_FILE_TEST_EXISTS);
    gchar *legacy_pkg = g_build_filename (pkg_dir, "dakar-custom-icons.xml", NULL);
    if (g_file_test (legacy_pkg, G_FILE_TEST_EXISTS)) {   /* pre-rename export */
        g_unlink (legacy_pkg);
        had_package = TRUE;
    }
    g_free (legacy_pkg);

    g_variant_iter_init (&iter, map);
    while (g_variant_iter_next (&iter, "{&s&s}", &mime, &value)) {
        gchar *image, *name, *esc, *target = NULL;
        if (!dakkar_custom_icons_type_allowed (mime) || !g_utf8_validate (mime, -1, NULL))
            continue;
        image = resolve_image (value);
        if (image == NULL)
            continue;
        name = export_icon_name (mime);
        {
            gboolean svg = g_str_has_suffix (image, ".svg") || g_str_has_suffix (image, ".svgz");
            gboolean png = g_str_has_suffix (image, ".png");
            if (svg || png) {
                gchar *base = g_strconcat (name, svg ? (g_str_has_suffix (image, ".svgz") ? ".svgz" : ".svg") : ".png", NULL);
                gchar *dir = svg ? svg_dir : png_dir;
                gchar *contents = NULL;
                gsize length = 0;
                target = g_build_filename (dir, base, NULL);
                if (g_mkdir_with_parents (dir, 0755) != 0 ||
                    !g_file_get_contents (image, &contents, &length, NULL) ||
                    !g_file_set_contents (target, contents, length, NULL))
                    g_clear_pointer (&target, g_free);
                else
                    g_hash_table_add (keep, g_strdup (base));
                g_free (contents);
                g_free (base);
            }
        }
        if (target != NULL) {
            esc = g_markup_escape_text (mime, -1);
            g_string_append_printf (xml, "  <mime-type type=\"%s\">\n    <icon name=\"%s\"/>\n  </mime-type>\n",
                                    esc, name);
            g_free (esc);
            count++;
        }
        g_free (target);
        g_free (name);
        g_free (image);
    }
    g_string_append (xml, "</mime-info>\n");
    remove_stale (svg_dir, keep);
    remove_stale (png_dir, keep);

    if (count > 0) {
        if (g_mkdir_with_parents (pkg_dir, 0755) == 0)
            g_file_set_contents (pkg, xml->str, xml->len, NULL);
    } else if (had_package) {
        g_unlink (pkg);
    }
    if (count > 0 || had_package)
        run_tool ("update-mime-database", mime_dir, NULL, NULL);
    {
        /* Only refresh an icon cache that already exists: creating one would
         * hide icons that other programs add later without updating it. */
        gchar *cache = g_build_filename (hicolor, "icon-theme.cache", NULL);
        if (g_file_test (cache, G_FILE_TEST_EXISTS))
            run_tool ("gtk-update-icon-cache", "-f", "-t", hicolor);
        g_free (cache);
    }
    gtk_icon_theme_rescan_if_needed (gtk_icon_theme_get_default ());

    g_hash_table_unref (keep);
    g_string_free (xml, TRUE);
    g_free (svg_dir); g_free (png_dir); g_free (hicolor);
    g_free (pkg); g_free (pkg_dir); g_free (mime_dir);
    g_variant_unref (map);
}

/* ---------- Change Icon ---------- */

/* The common MIME type of a selection of files (no folders), or NULL. */
static gchar *
common_mime (GList *files)
{
    gchar *mime = NULL;
    GList *l;
    for (l = files; l != NULL; l = l->next) {
        NemoFile *f = NEMO_FILE (l->data);
        gchar *m;
        if (nemo_file_is_directory (f)) { g_free (mime); return NULL; }
        m = nemo_file_get_mime_type (f);
        if (m == NULL || (mime != NULL && g_strcmp0 (m, mime) != 0)) {
            g_free (m); g_free (mime); return NULL;
        }
        if (mime == NULL) mime = m; else g_free (m);
    }
    return mime;
}

void
dakkar_custom_icons_change (GtkWindow *parent, GList *files)
{
    guint n = g_list_length (files);
    gchar *mime, *title, *current = NULL, *spec;
    gchar *type_choice = NULL;
    const gchar *single_choice = NULL;
    gboolean allowed = FALSE, for_type = FALSE;
    gchar *path = NULL, *name = NULL;
    GList *l;

    if (n == 0) return;
    mime = common_mime (files);
    if (mime != NULL) {
        gchar *desc = g_content_type_get_description (mime);
        allowed = dakkar_custom_icons_type_allowed (mime);
        /* Translators: %s is a file type description such as "PDF document" */
        type_choice = g_strdup_printf (_("All “%s” files"), desc != NULL ? desc : mime);
        single_choice = n == 1 ? _("Only this file") : _("Only the selected files");
        g_free (desc);
    }
    if (n == 1) {
        gchar *display = nemo_file_get_display_name (NEMO_FILE (files->data));
        /* Translators: dialog title, %s is a file or folder name */
        title = g_strdup_printf (_("Change Icon of “%s”"), display != NULL ? display : "");
        g_free (display);
        current = file_spec (NEMO_FILE (files->data));
    } else {
        title = g_strdup_printf (ngettext ("Change Icon of %u Item", "Change Icon of %u Items", n), n);
    }
    /* the whole type is the default, unless this file already has its own icon */
    for_type = allowed && current == NULL;
    if (current == NULL && mime != NULL)
        current = type_spec (mime);

    spec = dakkar_icon_picker_run_scoped (parent, title, current, type_choice, single_choice, allowed,
                                         _("Unknown file types can only get an icon per file"),
                                         &for_type);
    if (spec != NULL) {
        spec_split (spec, &path, &name);
        if (path == NULL && name == NULL) {
            /* nothing usable */
        } else if (for_type && allowed && mime != NULL) {
            type_store (mime, path != NULL ? path : name);
            /* a per-file icon on the chosen files would hide the new type icon */
            for (l = files; l != NULL; l = l->next)
                if (file_has_own_icon (NEMO_FILE (l->data)))
                    file_set_icon (NEMO_FILE (l->data), NULL, NULL);
            dakkar_custom_icons_export ();
        } else {
            for (l = files; l != NULL; l = l->next)
                file_set_icon (NEMO_FILE (l->data), path, name);
        }
    }
    g_free (path); g_free (name);
    g_free (spec);
    g_free (current);
    g_free (title);
    g_free (type_choice);
    g_free (mime);
}

/* ---------- Reset Icon ---------- */

static GPtrArray *
typed_mimes (GList *files)
{
    GPtrArray *mimes = g_ptr_array_new_with_free_func (g_free);
    GList *l;
    for (l = files; l != NULL; l = l->next) {
        NemoFile *f = NEMO_FILE (l->data);
        gchar *m, *v;
        guint i;
        gboolean seen = FALSE;
        if (nemo_file_is_directory (f)) continue;
        m = nemo_file_get_mime_type (f);
        v = type_lookup (m);
        for (i = 0; i < mimes->len && !seen; i++)
            seen = g_strcmp0 (g_ptr_array_index (mimes, i), m) == 0;
        if (v != NULL && !seen) g_ptr_array_add (mimes, m); else g_free (m);
        g_free (v);
    }
    return mimes;
}

gboolean
dakkar_custom_icons_any (GList *files)
{
    GList *l;
    GPtrArray *mimes;
    gboolean any;
    for (l = files; l != NULL; l = l->next)
        if (file_has_own_icon (NEMO_FILE (l->data)))
            return TRUE;
    mimes = typed_mimes (files);
    any = mimes->len > 0;
    g_ptr_array_unref (mimes);
    return any;
}

void
dakkar_custom_icons_reset (GtkWindow *parent, GList *files)
{
    GPtrArray *mimes = typed_mimes (files);
    gboolean own = FALSE, reset_own, reset_type;
    GList *l;
    guint i;

    for (l = files; l != NULL && !own; l = l->next)
        own = file_has_own_icon (NEMO_FILE (l->data));
    reset_own = own;
    reset_type = mimes->len > 0;

    if (own && mimes->len > 0) {
        /* both kinds: ask which */
        GtkWidget *dialog = gtk_dialog_new_with_buttons (_("Reset Icon"), parent,
                                GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                _("_Cancel"), GTK_RESPONSE_CANCEL,
                                _("_Reset"), GTK_RESPONSE_OK, NULL);
        GtkWidget *content = gtk_dialog_get_content_area (GTK_DIALOG (dialog));
        GtkWidget *own_check = gtk_check_button_new_with_mnemonic (
            g_list_length (files) == 1 ? _("The icon of _this file") : _("The icons of the _selected files"));
        GString *types = g_string_new (NULL);
        GtkWidget *type_check;
        gchar *label;
        for (i = 0; i < mimes->len; i++) {
            gchar *desc = g_content_type_get_description (g_ptr_array_index (mimes, i));
            if (i > 0) g_string_append (types, ", ");
            g_string_append (types, desc != NULL ? desc : (gchar *) g_ptr_array_index (mimes, i));
            g_free (desc);
        }
        /* Translators: %s is one or more file type descriptions, "PDF document" */
        label = g_strdup_printf (_("The icon of _all “%s” files"), types->str);
        type_check = gtk_check_button_new_with_mnemonic (label);
        g_free (label);
        g_string_free (types, TRUE);
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (own_check), TRUE);
        gtk_container_set_border_width (GTK_CONTAINER (content), 12);
        gtk_box_set_spacing (GTK_BOX (content), 6);
        gtk_box_pack_start (GTK_BOX (content), own_check, FALSE, FALSE, 0);
        gtk_box_pack_start (GTK_BOX (content), type_check, FALSE, FALSE, 0);
        gtk_widget_set_name (dialog, "dakkar-reset-icon");
        gtk_widget_show_all (dialog);
        if (gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_OK) {
            reset_own = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (own_check));
            reset_type = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (type_check));
        } else {
            reset_own = reset_type = FALSE;
        }
        gtk_widget_destroy (dialog);
    }
    if (reset_own)
        for (l = files; l != NULL; l = l->next)
            if (file_has_own_icon (NEMO_FILE (l->data)))
                file_set_icon (NEMO_FILE (l->data), NULL, NULL);
    if (reset_type) {
        for (i = 0; i < mimes->len; i++)
            type_store (g_ptr_array_index (mimes, i), NULL);
        dakkar_custom_icons_export ();
    }
    g_ptr_array_unref (mimes);
}

/* ---------- icon colour (folders) ---------- */

gboolean
dakkar_custom_icons_any_color (GList *folders)
{
    GList *l;
    for (l = folders; l != NULL; l = l->next) {
        gchar *v = nemo_file_get_dakkar_folder_color (NEMO_FILE (l->data));
        gboolean has = v != NULL && v[0] != '\0';
        g_free (v);
        if (has) return TRUE;
    }
    return FALSE;
}

void
dakkar_custom_icons_set_color (GtkWindow *parent, GList *folders)
{
    GtkWidget *dialog;
    GdkRGBA color;
    gchar *current;
    GList *l;

    if (folders == NULL) return;
    dialog = gtk_color_chooser_dialog_new (_("Icon Color"), parent);
    gtk_color_chooser_set_use_alpha (GTK_COLOR_CHOOSER (dialog), FALSE);
    current = nemo_file_get_dakkar_folder_color (NEMO_FILE (folders->data));
    if (current != NULL && gdk_rgba_parse (&color, current))
        gtk_color_chooser_set_rgba (GTK_COLOR_CHOOSER (dialog), &color);
    g_free (current);
    if (gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_OK) {
        gchar *hex;
        gtk_color_chooser_get_rgba (GTK_COLOR_CHOOSER (dialog), &color);
        hex = g_strdup_printf ("#%02x%02x%02x", (int) (color.red * 255 + .5),
                               (int) (color.green * 255 + .5), (int) (color.blue * 255 + .5));
        for (l = folders; l != NULL; l = l->next)
            if (nemo_file_is_directory (NEMO_FILE (l->data)))
                nemo_file_set_dakkar_folder_color (NEMO_FILE (l->data), hex);
        g_free (hex);
    }
    gtk_widget_destroy (dialog);
}

void
dakkar_custom_icons_reset_color (GList *folders)
{
    GList *l;
    for (l = folders; l != NULL; l = l->next)
        nemo_file_set_dakkar_folder_color (NEMO_FILE (l->data), NULL);
}
