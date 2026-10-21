/* Dakkar group ordering shared by Miller, Icons/Compact and List.
 * This never changes file identity or operates on disk. GTK headers and
 * collapsible sections require an additional view-layer implementation. */
#ifndef DAKKAR_GROUPING_H
#define DAKKAR_GROUPING_H
#include <glib.h>
#include <glib/gi18n.h>
#include <libnemo-private/nemo-file.h>
#include <libnemo-private/nemo-file-private.h>
#include <libnemo-private/nemo-global-preferences.h>
#include <time.h>
#include <gtk/gtk.h>

static inline gchar *
dakkar_group_file_key (NemoFile *file, const char *mode)
{
    if (file == NULL || mode == NULL || g_str_equal (mode, "none"))
        return g_strdup ("");
    if (g_str_equal (mode, "type")) {
        if (nemo_file_is_directory (file)) return g_strdup ("00 Folders");
        gchar *mime = nemo_file_get_mime_type (file);
        gchar *filename = nemo_file_get_name (file);
        gchar *lower = filename != NULL ? g_ascii_strdown (filename, -1) : NULL;
        const char *kind = "90 Other";
        /* Extension specificity keeps development folders readable even
         * when the host's MIME database treats all scripts as plain text. */
        if (lower != NULL &&
            (g_str_has_suffix (lower, ".js") ||
             g_str_has_suffix (lower, ".jsx") ||
             g_str_has_suffix (lower, ".mjs") ||
             g_str_has_suffix (lower, ".cjs"))) kind = "11 JavaScript";
        else if (lower != NULL &&
                 (g_str_has_suffix (lower, ".ts") ||
                  g_str_has_suffix (lower, ".tsx"))) kind = "12 TypeScript";
        else if (lower != NULL &&
                 (g_str_has_suffix (lower, ".htm") ||
                  g_str_has_suffix (lower, ".html"))) kind = "13 HTML";
        else if (lower != NULL &&
                 (g_str_has_suffix (lower, ".css") ||
                  g_str_has_suffix (lower, ".scss") ||
                  g_str_has_suffix (lower, ".sass"))) kind = "14 Stylesheets";
        else if (mime != NULL) {
            if (g_str_has_prefix (mime, "image/")) kind = "10 Images";
            else if (g_str_has_prefix (mime, "audio/")) kind = "20 Audio";
            else if (g_str_has_prefix (mime, "video/")) kind = "30 Videos";
            else if (g_str_has_prefix (mime, "text/") ||
                     g_str_has_prefix (mime, "application/pdf") ||
                     g_str_has_prefix (mime, "application/vnd.oasis.opendocument") ||
                     g_str_has_prefix (mime, "application/vnd.openxmlformats-officedocument"))
                kind = "40 Documents";
            else if (g_str_has_prefix (mime, "application/zip") ||
                     g_str_has_prefix (mime, "application/x-tar") ||
                     g_str_has_prefix (mime, "application/x-7z") ||
                     g_str_has_prefix (mime, "application/x-rar")) kind = "50 Archives";
        }
        g_free (lower);
        g_free (filename);
        g_free (mime);
        return g_strdup (kind);
    }
    if (g_str_equal (mode, "name")) {
        gchar *name = nemo_file_get_display_name (file);
        gunichar initial = name != NULL ? g_utf8_get_char (name) : 0;
        gunichar upper = g_unichar_toupper (initial);
        gchar *label = g_unichar_isalpha (upper) ?
            g_ucs4_to_utf8 (&upper, 1, NULL, NULL, NULL) : g_strdup ("#");
        g_free (name);
        return label;
    }
    if (g_str_equal (mode, "size")) {
        /* Folders have no meaningful comparable file size here. */
        if (nemo_file_is_directory (file)) return g_strdup ("00 Folders");
        goffset bytes = nemo_file_get_size (file);
        if (bytes < 0) return g_strdup ("99 Unknown size");
        if (bytes < 1024 * 1024) return g_strdup ("10 Under 1 MiB");
        if (bytes < (goffset)100 * 1024 * 1024) return g_strdup ("20 1–100 MiB");
        if (bytes < (goffset)1024 * 1024 * 1024) return g_strdup ("30 100 MiB–1 GiB");
        return g_strdup ("40 Over 1 GiB");
    }
    if (g_str_equal (mode, "owner")) {
        gchar *owner = nemo_file_get_owner_name (file);
        if (owner == NULL || !*owner) { g_free (owner); return g_strdup (_("Unknown owner")); }
        return owner;
    }
    if (g_str_equal (mode, "permissions")) {
        if (!nemo_file_can_get_permissions (file)) return g_strdup (_("Unknown permissions"));
        return g_strdup_printf ("%04o", nemo_file_get_permissions (file) & 07777);
    }
    if (g_str_equal (mode, "modified") || g_str_equal (mode, "created")) {
        time_t when = 0;
        NemoDateType kind = g_str_equal (mode, "created") ?
            NEMO_DATE_TYPE_CREATED : NEMO_DATE_TYPE_MODIFIED;
        if (!nemo_file_get_date (file, kind, &when) || when <= 0)
            return g_strdup ("9999 Unknown date");
        GDateTime *dt = g_date_time_new_from_unix_local ((gint64)when);
        if (dt == NULL) return g_strdup ("9999 Unknown date");
        gchar *date = g_date_time_format (dt, "%Y-%m");
        g_date_time_unref (dt);
        return date;
    }
    return g_strdup ("");
}

#if 0
/* Heading names, listed for xgettext only (keys stay English). */
N_("Folders")
N_("Other")
N_("JavaScript")
N_("TypeScript")
N_("HTML")
N_("Stylesheets")
N_("Images")
N_("Audio")
N_("Videos")
N_("Documents")
N_("Archives")
N_("Unknown size")
N_("Under 1 MiB")
N_("1–100 MiB")
N_("100 MiB–1 GiB")
N_("Over 1 GiB")
N_("Unknown date")
#endif

/* Visible, translated heading text for a group key: the numeric prefix
 * ("10 ", "9999 ") only enforces the order and is never shown. */
static inline const gchar *
dakkar_group_display_label (const gchar *key)
{
    const gchar *p = key, *q;
    if (key == NULL) return "";
    for (q = p; g_ascii_isdigit (*q); q++) ;
    if (q - p >= 2 && *q == ' ') return _(q + 1);
    return key;
}

/* Group order for one view's mode.  NULL falls back to the global
 * dakkar-group-by setting, for callers without a pane. */
static inline gint
dakkar_group_compare_for_mode (NemoFile *a, NemoFile *b, const gchar *mode)
{
    gchar *fallback = NULL;
    if (mode == NULL) {
        fallback = g_settings_get_string (nemo_preferences, "dakkar-group-by");
        mode = fallback;
    }
    if (g_strcmp0 (mode, "none") == 0) { g_free (fallback); return 0; }
    gchar *ka = dakkar_group_file_key (a, mode);
    gchar *kb = dakkar_group_file_key (b, mode);
    gint comparison = g_utf8_collate (ka, kb);
    g_free (ka); g_free (kb); g_free (fallback);
    return comparison < 0 ? -1 : comparison > 0 ? 1 : 0;
}

/* Icon shown in front of every group heading, in every view (List,
 * Icons/Compact and Miller). For "type" the icon matches the file type of
 * the group; for the other modes a stable, mode-specific icon is used.
 * Names are tried in order, so every icon theme ends on a generic icon. */
static inline GIcon *
dakkar_group_icon_new (const char *key, const char *mode)
{
    const char *names[4] = { NULL, NULL, "text-x-generic", NULL };
    const char *k = key != NULL ? key : "";
    if (mode == NULL) mode = "type";
    if (g_str_has_prefix (k, "00 Folders")) {
        names[0] = "folder"; names[1] = "inode-directory";
    } else if (g_str_equal (mode, "type")) {
        if (g_str_has_prefix (k, "10 ")) { names[0] = "image-x-generic"; }
        else if (g_str_has_prefix (k, "11 ")) { names[0] = "text-x-javascript"; names[1] = "application-javascript"; }
        else if (g_str_has_prefix (k, "12 ")) { names[0] = "text-x-typescript"; names[1] = "text-x-script"; }
        else if (g_str_has_prefix (k, "13 ")) { names[0] = "text-html"; }
        else if (g_str_has_prefix (k, "14 ")) { names[0] = "text-css"; names[1] = "text-x-script"; }
        else if (g_str_has_prefix (k, "20 ")) { names[0] = "audio-x-generic"; }
        else if (g_str_has_prefix (k, "30 ")) { names[0] = "video-x-generic"; }
        else if (g_str_has_prefix (k, "40 ")) { names[0] = "x-office-document"; }
        else if (g_str_has_prefix (k, "50 ")) { names[0] = "package-x-generic"; }
        else { names[0] = "text-x-generic"; }
    } else if (g_str_equal (mode, "name")) {
        names[0] = "text-x-generic";
    } else if (g_str_equal (mode, "size")) {
        names[0] = "drive-harddisk"; names[1] = "text-x-generic";
    } else if (g_str_equal (mode, "modified") || g_str_equal (mode, "created")) {
        names[0] = "x-office-calendar"; names[1] = "appointment-soon";
    } else if (g_str_equal (mode, "owner")) {
        names[0] = "avatar-default"; names[1] = "system-users";
    } else if (g_str_equal (mode, "permissions")) {
        names[0] = "dialog-password"; names[1] = "changes-prevent";
    } else {
        names[0] = "text-x-generic";
    }
    {
        const char *list[4]; int n = 0, i;
        for (i = 0; i < 3; i++) if (names[i] != NULL) list[n++] = names[i];
        list[n] = NULL;
        return g_themed_icon_new_from_names ((char **) list, n);
    }
}

/* Owned pixbuf (or NULL) for a heading icon at size*scale pixels. */
static inline GdkPixbuf *
dakkar_group_icon_pixbuf (const char *key, const char *mode, int size, int scale)
{
    GIcon *icon = dakkar_group_icon_new (key, mode);
    GtkIconInfo *info = gtk_icon_theme_lookup_by_gicon_for_scale (
        gtk_icon_theme_get_default (), icon, MAX (size, 8), MAX (scale, 1),
        GTK_ICON_LOOKUP_FORCE_SIZE | GTK_ICON_LOOKUP_GENERIC_FALLBACK);
    GdkPixbuf *pixbuf = NULL;
    if (info != NULL) {
        pixbuf = gtk_icon_info_load_icon (info, NULL);
        g_object_unref (info);
    }
    g_object_unref (icon);
    return pixbuf;
}
#endif
