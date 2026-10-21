/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; -*-
 * Miller Columns Quick Actions registry and Preferences UI.
 * GPL-2.0-or-later.
 */
#include <config.h>
#include "nemo-quick-actions.h"
#include <glib/gi18n.h>
#include <string.h>

#define F NEMO_QA_FILE
#define D NEMO_QA_DIRECTORY
#define M NEMO_QA_MULTI
#define E NEMO_QA_EMPTY
#define I NEMO_QA_IMAGE
#define V NEMO_QA_VIDEO
#define A NEMO_QA_AUDIO
#define R NEMO_QA_ARCHIVE
#define G NEMO_QA_GIT
#define SAFE NEMO_QA_SAFE
#define CONTROLLED NEMO_QA_CONTROLLED
#define TODO NEMO_QA_NOT_READY

/* IDs may appear in persisted settings; don't change them after release.
 * Unsupported commands remain visible in Preferences but are disabled in
 * the preview instead of pretending to work. */
static const NemoQAAction catalog[] = {
 {"new-folder", N_("Create New Folder"), "folder-new-symbolic", N_("File & Folder Management"), F|D|E|M, SAFE, TRUE},
 {"new-file", N_("Create New File"), "document-new-symbolic", N_("File & Folder Management"), F|D|E, TODO, FALSE},
 {"rename", N_("Rename"), "insert-text-symbolic", N_("File & Folder Management"), F|D, SAFE, TRUE},
 {"batch-rename", N_("Batch Rename…"), "edit-rename-symbolic", N_("File & Folder Management"), M, SAFE, FALSE},
 {"duplicate", N_("Duplicate"), "edit-copy-symbolic", N_("File & Folder Management"), F|D|M, SAFE, FALSE},
 {"copy-path", N_("Copy Path"), "edit-copy-symbolic", N_("File & Folder Management"), F|D|M, SAFE, TRUE},
 {"copy-relative-path", N_("Copy Relative Path"), "edit-copy-symbolic", N_("File & Folder Management"), F|D, SAFE, FALSE},
 {"copy-filename", N_("Copy Filename"), "edit-copy-symbolic", N_("File & Folder Management"), F|D, SAFE, FALSE},
 {"copy-move-to", N_("Copy / Move To…"), "folder-move-symbolic", N_("File & Folder Management"), F|D|M, TODO, FALSE},
 {"make-symlink", N_("Create Symbolic Link"), "insert-link-symbolic", N_("File & Folder Management"), F|D, SAFE, FALSE},
 {"compare-files", N_("Compare Files…"), "view-compare-symbolic", N_("File & Folder Management"), M, SAFE, FALSE},
 {"compare-folders", N_("Compare Folders…"), "folder-symbolic", N_("File & Folder Management"), M, TODO, FALSE},
 {"find-duplicates", N_("Find Duplicates…"), "edit-find-symbolic", N_("File & Folder Management"), D, TODO, FALSE},
 {"properties", N_("Properties"), "document-properties-symbolic", N_("File & Folder Management"), F|D|M|E, SAFE, TRUE},

 {"compress", N_("Compress to Archive…"), "package-x-generic-symbolic", N_("Archives & Compression"), F|D|M, CONTROLLED, TRUE},
 {"extract-here", N_("Extract Here"), "archive-extract-symbolic", N_("Archives & Compression"), R, SAFE, TRUE},
 {"extract-to", N_("Extract To…"), "archive-extract-symbolic", N_("Archives & Compression"), R, SAFE, TRUE},
 {"archive-contents", N_("View Archive Contents"), "package-x-generic-symbolic", N_("Archives & Compression"), R, SAFE, FALSE},
 {"split-files", N_("Split Files…"), "edit-cut-symbolic", N_("Archives & Compression"), F, TODO, FALSE},
 {"join-files", N_("Join Files…"), "document-open-symbolic", N_("Archives & Compression"), M, TODO, FALSE},

 {"terminal", N_("Open Terminal"), "utilities-terminal-symbolic", N_("Development & Git"), F|D|E|M, SAFE, TRUE},
 {"root-terminal", N_("Open Terminal as Root"), "dialog-password-symbolic", N_("Development & Git"), F|D|E|M, SAFE, FALSE},
 {"code-editor", N_("Open in Code Editor"), "accessories-text-editor-symbolic", N_("Development & Git"), F|D, TODO, FALSE},
 {"project-editor", N_("Open Project in Editor"), "applications-development-symbolic", N_("Development & Git"), D, TODO, FALSE},
 {"repository-remote", N_("Open Repository Remote"), "web-browser-symbolic", N_("Development & Git"), G, TODO, FALSE},
 {"git", N_("Git Actions…"), "git-symbolic", N_("Development & Git"), G|D, SAFE, TRUE},
 {"run-build", N_("Run Build…"), "system-run-symbolic", N_("Development & Git"), D|G, TODO, FALSE},
 {"run-tests", N_("Run Tests…"), "emblem-default-symbolic", N_("Development & Git"), D|G, TODO, FALSE},
 {"dependencies", N_("Inspect Dependencies…"), "package-x-generic-symbolic", N_("Development & Git"), D|G, TODO, FALSE},

 {"resize-images", N_("Resize Images…"), "image-x-generic-symbolic", N_("Images & Photography"), I|M, TODO, FALSE},
 {"optimize-image", N_("Optimize Image…"), "image-x-generic-symbolic", N_("Images & Photography"), I|M, TODO, FALSE},
 {"thumbnails", N_("Generate Thumbnails…"), "view-grid-symbolic", N_("Images & Photography"), I|M, TODO, FALSE},
 {"contact-sheet", N_("Create Contact Sheet…"), "view-grid-symbolic", N_("Images & Photography"), I|M, TODO, FALSE},
 {"edit-exif", N_("Edit EXIF Metadata…"), "document-properties-symbolic", N_("Images & Photography"), I, TODO, FALSE},

 {"inspect-media", N_("Inspect Media…"), "dialog-information-symbolic", N_("Audio & Video"), A|V, SAFE, TRUE},
 {"convert-audio", N_("Convert Audio…"), "audio-x-generic-symbolic", N_("Audio & Video"), A, TODO, FALSE},
 {"convert-video", N_("Convert Video…"), "video-x-generic-symbolic", N_("Audio & Video"), V, TODO, FALSE},
 {"extract-audio", N_("Extract Audio…"), "audio-x-generic-symbolic", N_("Audio & Video"), V, TODO, FALSE},
 {"edit-audio-tags", N_("Edit Audio Metadata…"), "document-properties-symbolic", N_("Audio & Video"), A, TODO, FALSE},

 {"pdf-from-images", N_("Create PDF from Images…"), "application-pdf-symbolic", N_("Documents & PDF"), I|M, TODO, FALSE},
 {"file-information", N_("File Information…"), "dialog-information-symbolic", N_("Documents & PDF"), F|D, SAFE, FALSE},

 {"checksum", N_("Calculate Checksum…"), "security-high-symbolic", N_("Security & Advanced Tools"), F, SAFE, TRUE},
 {"verify-checksum", N_("Verify Checksum…"), "security-high-symbolic", N_("Security & Advanced Tools"), F, TODO, FALSE},
 {"permissions", N_("Permissions & Ownership…"), "dialog-password-symbolic", N_("Security & Advanced Tools"), F|D|M, TODO, FALSE},
 {"encrypt", N_("Encrypt / Decrypt…"), "changes-prevent-symbolic", N_("Security & Advanced Tools"), F|D|M, TODO, FALSE},
 {"signature", N_("Digital Signature…"), "security-high-symbolic", N_("Security & Advanced Tools"), F, TODO, FALSE},
 {"inspect-symlink", N_("Inspect Symlink…"), "insert-link-symbolic", N_("Security & Advanced Tools"), F|D, SAFE, FALSE},
 {"disk-usage", N_("Disk Usage Analysis…"), "drive-harddisk-symbolic", N_("Security & Advanced Tools"), D, TODO, FALSE},
 {"advanced-info", N_("Advanced File Information…"), "dialog-information-symbolic", N_("Security & Advanced Tools"), F|D, SAFE, FALSE},

 {"open-with", N_("Open With…"), "document-open-symbolic", N_("Open & Share"), F, SAFE, FALSE},
 {"share", N_("Share / Send To…"), "document-send-symbolic", N_("Open & Share"), F|D|M, TODO, FALSE}
};

/* Archive MIME aliases are shared between every file view. Filename fallback
 * is used for generic application/octet-stream responses from some mounts. */
gboolean
nemo_qa_archive_matches (const char *mime, const char *filename)
{
    static const char *types[] = {
        "application/zip", "application/x-zip", "application/x-zip-compressed",
        "application/x-7z-compressed", "application/x-rar", "application/vnd.rar",
        "application/x-rar-compressed", "application/x-tar", "application/x-compressed-tar",
        "application/gzip", "application/x-gzip", "application/x-bzip2",
        "application/x-xz", "application/x-zstd-compressed-tar", NULL
    };
    static const char *suffixes[] = {
        ".zip", ".7z", ".rar", ".tar", ".tgz", ".tar.gz",
        ".tar.xz", ".txz", ".tar.bz2", ".tbz2", ".tar.zst", NULL
    };
    guint i;
    for (i = 0; types[i]; i++)
        if (g_strcmp0 (mime, types[i]) == 0) return TRUE;
    if (filename == NULL) return FALSE;
    char *lower = g_ascii_strdown (filename, -1);
    gboolean matches = FALSE;
    for (i = 0; suffixes[i]; i++) {
        if (g_str_has_suffix (lower, suffixes[i])) { matches = TRUE; break; }
    }
    g_free (lower);
    return matches;
}

/* Prefer the local desktop archive manager. The program handles the actual
 * decompression and any collision dialog; never invoke shell parsing. */
gboolean
nemo_qa_archive_handler_available (const char *action_id)
{
    gboolean extracting = g_strcmp0 (action_id, "extract-here") == 0 ||
                          g_strcmp0 (action_id, "extract-to") == 0;
    char *ark = g_find_program_in_path ("ark");
    char *roller = g_find_program_in_path ("file-roller");
    char *engrampa = g_find_program_in_path ("engrampa");
    gboolean available = ark != NULL || roller != NULL || engrampa != NULL;
    if (!extracting && g_strcmp0 (action_id, "archive-contents") == 0)
        available = TRUE; /* GIO may have another default archive handler. */
    g_free (ark); g_free (roller); g_free (engrampa);
    return available;
}

gboolean
nemo_qa_archive_launch (GtkWindow *parent, const char *action_id,
                        GFile *archive, GError **error)
{
    gboolean here = g_strcmp0 (action_id, "extract-here") == 0;
    gboolean to = g_strcmp0 (action_id, "extract-to") == 0;
    gboolean browse = g_strcmp0 (action_id, "archive-contents") == 0;
    char *path = g_file_get_path (archive);
    char *program = NULL, *parent_dir = NULL;
    gboolean result = FALSE;
    const char *argv[9] = {NULL};
    guint n = 0;
    if (!here && !to && !browse) {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                             _("Unsupported archive action"));
        goto done;
    }
    if (path == NULL && browse) {
        char *uri = g_file_get_uri (archive);
        result = g_app_info_launch_default_for_uri (uri, NULL, error);
        g_free (uri);
        goto done;
    }
    if (path == NULL) {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                             _("Archive manager requires a local archive"));
        goto done;
    }
    /* Explicit confirmation for extraction; the external manager must also
     * handle its own file conflicts, which Nemo must not bypass. */
    if (here || to) {
        GtkWidget *confirm = gtk_message_dialog_new (parent,
            GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
            GTK_MESSAGE_QUESTION, GTK_BUTTONS_NONE, "%s",
            here ? _("Extract this archive beside the original file?") :
                   _("Open the archive manager to choose an extraction destination?"));
        gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (confirm), "%s",
            _("Inspect any overwrite or conflict prompts from your archive manager."));
        gtk_dialog_add_buttons (GTK_DIALOG (confirm), _("Cancel"), GTK_RESPONSE_CANCEL,
                                _("Continue"), GTK_RESPONSE_ACCEPT, NULL);
        gint response = gtk_dialog_run (GTK_DIALOG (confirm));
        gtk_widget_destroy (confirm);
        if (response != GTK_RESPONSE_ACCEPT) {
            result = TRUE; /* user declined, not a launch error */
            goto done;
        }
    }
    /* Ark is the native KDE choice on Kubuntu. Its batch dialog gives the
     * user a last chance to confirm extraction options. File Roller and
     * Engrampa use their documented --extract-* interfaces. */
    program = g_find_program_in_path ("ark");
    if (program) {
        argv[n++] = program;
        if (here || to) {
            argv[n++] = "--batch";
            argv[n++] = "--dialog";
            if (here) {
                parent_dir = g_path_get_dirname (path);
                argv[n++] = "--destination";
                argv[n++] = parent_dir;
            }
        }
        argv[n++] = path;
    } else {
        program = g_find_program_in_path ("file-roller");
        if (!program) program = g_find_program_in_path ("engrampa");
        if (program) {
            argv[n++] = program;
            if (here) argv[n++] = "--extract-here";
            else if (to) argv[n++] = "--extract";
            argv[n++] = path;
        }
    }
    if (!program) {
        if (browse) {
            char *uri = g_file_get_uri (archive);
            result = g_app_info_launch_default_for_uri (uri, NULL, error);
            g_free (uri);
            goto done;
        }
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                             "Install Ark, File Roller, or Engrampa to open or extract archives");
        goto done;
    }
    /* Passing an argv vector prevents shell injection from archive names.
     * Dispatch success is NOT extraction success, so do not show a check. */
    result = g_spawn_async (NULL, (gchar **) argv, NULL,
                            G_SPAWN_SEARCH_PATH,
                            NULL, NULL, NULL, error);
done:
    g_free (program);
    g_free (parent_dir);
    g_free (path);
    return result;
}

const NemoQAAction *
nemo_quick_actions_catalog (gsize *n)
{
    if (n) *n = G_N_ELEMENTS (catalog);
    return catalog;
}

const NemoQAAction *
nemo_quick_actions_find (const gchar *id)
{
    gsize i;
    if (id == NULL) return NULL;
    for (i=0; i<G_N_ELEMENTS(catalog); i++)
        if (g_strcmp0 (catalog[i].id, id) == 0)
            return &catalog[i];
    return NULL;
}

/* Shared Quick Action visibility and responsive button logic.  Neither
 * renderer should invent separate rules for pins, width or action feedback. */
gboolean
nemo_qa_custom_context_matches (const gchar *contexts, unsigned context)
{
    return g_strcmp0 (contexts, "all") == 0 ||
           ((context & NEMO_QA_FILE) && g_strcmp0 (contexts, "file") == 0) ||
           ((context & NEMO_QA_DIRECTORY) && g_strcmp0 (contexts, "directory") == 0) ||
           ((context & NEMO_QA_MULTI) && g_strcmp0 (contexts, "multiple") == 0);
}

void
nemo_qa_button_register (GtkWidget *button, GtkWidget *icon,
                         GtkWidget *title, GtkWidget *subtitle,
                         gboolean force_icons)
{
    g_return_if_fail (GTK_IS_BUTTON (button));
    g_object_set_data (G_OBJECT (button), "dakkar-qa-icon", icon);
    if (GTK_IS_IMAGE (icon)) {
        const gchar *icon_name = NULL;
        gtk_image_get_icon_name (GTK_IMAGE (icon), &icon_name, NULL);
        if (icon_name != NULL)
            g_object_set_data_full (G_OBJECT (button),
                                    "dakkar-qa-original-icon",
                                    g_strdup (icon_name), g_free);
    }
    g_object_set_data (G_OBJECT (button), "dakkar-qa-title", title);
    g_object_set_data (G_OBJECT (button), "dakkar-qa-subtitle", subtitle);
    g_object_set_data (G_OBJECT (button), "dakkar-qa-force-icons",
                       GINT_TO_POINTER (force_icons));
}

void
nemo_qa_adapt_buttons (GtkWidget *container, gint panel_width)
{
    GList *children, *iter;
    gboolean icons_only = panel_width < 174;
    g_return_if_fail (GTK_IS_CONTAINER (container));
    children = gtk_container_get_children (GTK_CONTAINER (container));
    for (iter = children; iter; iter = iter->next) {
        GtkWidget *button = iter->data;
        GtkWidget *title, *subtitle;
        gboolean compact;
        if (!GTK_IS_BUTTON (button)) continue;
        title = g_object_get_data (G_OBJECT (button), "dakkar-qa-title");
        subtitle = g_object_get_data (G_OBJECT (button), "dakkar-qa-subtitle");
        compact = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (button),
                                                    "dakkar-qa-compact"));
        gboolean icon_mode = icons_only || GPOINTER_TO_INT (
             g_object_get_data (G_OBJECT (button), "dakkar-qa-force-icons"));
        if (title != NULL)
            gtk_widget_set_visible (title, !icon_mode);
        if (subtitle != NULL)
            gtk_widget_set_visible (subtitle, !icon_mode && !compact && panel_width >= 250);
        gint requested_width = icon_mode ?
                  MIN (46, MAX (34, panel_width - 16)) :
                  MIN (222, MAX (46, panel_width - 16));
        if (GPOINTER_TO_INT (g_object_get_data (G_OBJECT (button),
                        "dakkar-qa-requested-width")) != requested_width) {
            gtk_widget_set_size_request (button, requested_width, -1);
            g_object_set_data (G_OBJECT (button), "dakkar-qa-requested-width",
                               GINT_TO_POINTER (requested_width));
        }
        gtk_widget_set_halign (button, GTK_ALIGN_CENTER);
        if (icon_mode)
            gtk_style_context_add_class (gtk_widget_get_style_context (button),
                                         "dakkar-qa-icon-only");
        else
            gtk_style_context_remove_class (gtk_widget_get_style_context (button),
                                            "dakkar-qa-icon-only");
    }
    g_list_free (children);
}

typedef struct {
    GWeakRef button_ref;
    guint generation;
} NemoQAFeedback;

static gboolean
nemo_qa_feedback_timeout (gpointer user_data)
{
    NemoQAFeedback *data = user_data;
    GtkWidget *button = g_weak_ref_get (&data->button_ref);
    if (button != NULL) {
        if (GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (button),
                         "dakkar-qa-feedback-generation")) == data->generation &&
            !gtk_widget_in_destruction (button)) {
            GtkWidget *icon = g_object_get_data (G_OBJECT (button), "dakkar-qa-icon");
            const gchar *original = g_object_get_data (G_OBJECT (button),
                                                       "dakkar-qa-original-icon");
            if (GTK_IS_IMAGE (icon) && original != NULL)
                gtk_image_set_from_icon_name (GTK_IMAGE (icon), original,
                                              GTK_ICON_SIZE_BUTTON);
            gtk_style_context_remove_class (gtk_widget_get_style_context (button),
                                            "dakkar-qa-success");
            gtk_style_context_remove_class (gtk_widget_get_style_context (button),
                                            "dakkar-qa-error");
        }
        g_object_unref (button);
    }
    g_weak_ref_clear (&data->button_ref);
    g_free (data);
    return G_SOURCE_REMOVE;
}

void
nemo_qa_button_feedback (GtkButton *button, gboolean success)
{
    GtkWidget *widget = GTK_WIDGET (button);
    GtkWidget *icon = g_object_get_data (G_OBJECT (button), "dakkar-qa-icon");
    guint generation = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (button),
                                                 "dakkar-qa-feedback-generation")) + 1;
    NemoQAFeedback *data;
    g_object_set_data (G_OBJECT (button), "dakkar-qa-feedback-generation",
                       GUINT_TO_POINTER (generation));
    if (GTK_IS_IMAGE (icon))
        gtk_image_set_from_icon_name (GTK_IMAGE (icon), success ?
            "emblem-ok-symbolic" : "dialog-error-symbolic", GTK_ICON_SIZE_BUTTON);
    gtk_style_context_remove_class (gtk_widget_get_style_context (widget),
                                    success ? "dakkar-qa-error" : "dakkar-qa-success");
    gtk_style_context_add_class (gtk_widget_get_style_context (widget),
                                 success ? "dakkar-qa-success" : "dakkar-qa-error");
    data = g_new0 (NemoQAFeedback, 1);
    g_weak_ref_init (&data->button_ref, button);
    data->generation = generation;
    g_timeout_add (1500, nemo_qa_feedback_timeout, data);
}

static gboolean
strv_contains (const gchar * const *list, const gchar *value)
{
    guint i;
    for (i=0; list && list[i]; i++)
        if (g_strcmp0 (list[i], value) == 0) return TRUE;
    return FALSE;
}

/* add/remove a stable action id without touching other settings entries */
static void
update_strv (GSettings *settings, const gchar *key,
             const gchar *id, gboolean add)
{
    gchar **current = g_settings_get_strv (settings, key);
    GPtrArray *result = g_ptr_array_new_with_free_func (g_free);
    guint i;
    for (i=0; current[i]; i++)
        if (g_strcmp0 (current[i], id) != 0)
            g_ptr_array_add (result, g_strdup (current[i]));
    if (add) g_ptr_array_add (result, g_strdup (id));
    g_ptr_array_add (result, NULL);
    g_settings_set_strv (settings, key, (const gchar * const *)result->pdata);
    g_ptr_array_unref (result);
    g_strfreev (current);
}

typedef struct {
    GSettings *settings;
    gchar *id;
    gchar *key;
} QAChoice;

static void
qa_choice_free (gpointer data)
{
    QAChoice *choice = data;
    g_object_unref (choice->settings);
    g_free (choice->id);
    g_free (choice->key);
    g_free (choice);
}

static void
qa_toggled (GtkToggleButton *button, gpointer data)
{
    QAChoice *choice = data;
    update_strv (choice->settings, choice->key, choice->id,
                 gtk_toggle_button_get_active (button));
}

static void
qa_choice_setting_changed (GSettings *settings, gchar *key, gpointer user_data)
{
    GtkToggleButton *button = GTK_TOGGLE_BUTTON (user_data);
    QAChoice *choice = g_object_get_data (G_OBJECT (button), "qa-choice");
    gchar **ids;
    gboolean checked;
    if (choice == NULL) return;
    ids = g_settings_get_strv (settings, choice->key);
    checked = strv_contains ((const gchar * const *)ids, choice->id);
    if (gtk_toggle_button_get_active (button) != checked) {
        g_signal_handlers_block_by_func (button, qa_toggled, choice);
        gtk_toggle_button_set_active (button, checked);
        g_signal_handlers_unblock_by_func (button, qa_toggled, choice);
    }
    g_strfreev (ids);
    (void) key;
}

static GtkWidget *
qa_checkbox (GSettings *settings, const gchar *key,
             const gchar *id, const gchar *label)
{
    gchar **current = g_settings_get_strv (settings, key);
    GtkWidget *toggle = gtk_check_button_new_with_label (label);
    QAChoice *choice = g_new0 (QAChoice, 1);
    choice->settings = g_object_ref (settings);
    choice->id = g_strdup (id);
    choice->key = g_strdup (key);
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (toggle),
                                  strv_contains ((const gchar * const *)current, id));
    g_object_set_data_full (G_OBJECT (toggle), "qa-choice", choice, qa_choice_free);
    g_signal_connect (toggle, "toggled", G_CALLBACK (qa_toggled), choice);
    gchar *detailed_key = g_strdup_printf ("changed::%s", key);
    g_signal_connect_object (settings, detailed_key,
                             G_CALLBACK (qa_choice_setting_changed), toggle, 0);
    g_free (detailed_key);
    g_strfreev (current);
    return toggle;
}

static GtkWidget *
qa_heading (const gchar *title)
{
    GtkWidget *label = gtk_label_new (NULL);
    gchar *escaped = g_markup_escape_text (title, -1);
    gchar *markup = g_strdup_printf ("<b>%s</b>", escaped);
    gtk_label_set_markup (GTK_LABEL (label), markup);
    gtk_widget_set_halign (label, GTK_ALIGN_START);
    gtk_widget_set_margin_top (label, 14);
    g_free (escaped);
    g_free (markup);
    return label;
}

/* Stable order: move the chosen ID up/down in the selected list. */
static void
qa_move (GtkButton *button, gpointer data)
{
    QAChoice *choice = data;
    gboolean up = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (button), "qa-up"));
    gchar **list = g_settings_get_strv (choice->settings, choice->key);
    gint i, n = g_strv_length (list);
    for (i=0; i<n; i++) {
        if (g_strcmp0 (list[i], choice->id) == 0) {
            gint other = i + (up ? -1 : 1);
            if (other >= 0 && other < n) {
                gchar *tmp = list[i]; list[i] = list[other]; list[other] = tmp;
                g_settings_set_strv (choice->settings, choice->key,
                                     (const gchar * const *)list);
            }
            break;
        }
    }
    g_strfreev (list);
}

/* An arrow only does something when the action is pinned AND can move.
 * Keep sensitivity live across external changes and other preferences windows. */
static void
qa_reorder_sensitivity (GSettings *settings, gchar *key, gpointer user_data)
{
    GtkWidget *button = GTK_WIDGET (user_data);
    QAChoice *choice = g_object_get_data (G_OBJECT (button), "qa-choice");
    gchar **ids;
    gint i, n;
    gboolean enabled = FALSE;
    gboolean up = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (button), "qa-up"));
    if (!choice) return;
    ids = g_settings_get_strv (settings, choice->key);
    n = g_strv_length (ids);
    for (i=0; i<n; i++) {
        if (g_strcmp0 (ids[i], choice->id) == 0) {
            enabled = up ? i > 0 : i < n - 1;
            break;
        }
    }
    gtk_widget_set_sensitive (button, enabled);
    g_strfreev (ids);
    (void) key;
}

static GtkWidget *
qa_reorder_button (GSettings *settings, const gchar *key,
                   const gchar *id, gboolean up)
{
    GtkWidget *button = gtk_button_new_from_icon_name (
        up ? "go-up-symbolic" : "go-down-symbolic", GTK_ICON_SIZE_MENU);
    QAChoice *choice = g_new0 (QAChoice, 1);
    choice->settings = g_object_ref (settings);
    choice->id = g_strdup (id);
    choice->key = g_strdup (key);
    g_object_set_data_full (G_OBJECT (button), "qa-choice", choice, qa_choice_free);
    g_object_set_data (G_OBJECT (button), "qa-up", GINT_TO_POINTER (up));
    g_signal_connect (button, "clicked", G_CALLBACK (qa_move), choice);
    gtk_widget_set_tooltip_text (button, up ? _("Move pinned action up") : _("Move pinned action down"));
    gchar *detailed_key = g_strdup_printf ("changed::%s", key);
    g_signal_connect_object (settings, detailed_key,
                             G_CALLBACK (qa_reorder_sensitivity), button, 0);
    g_free (detailed_key);
    qa_reorder_sensitivity (settings, NULL, button);
    return button;
}

static void
qa_bind_combo (GSettings *settings, GtkWidget *parent, const char *title,
               const char *key, const char * const *ids, const char * const *labels)
{
    GtkWidget *row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *text = gtk_label_new (title);
    GtkWidget *combo = gtk_combo_box_text_new ();
    guint i;
    for (i=0; ids[i]; i++)
        gtk_combo_box_text_append (GTK_COMBO_BOX_TEXT (combo), ids[i], _(labels[i]));
    gtk_widget_set_halign (text, GTK_ALIGN_START);
    gtk_widget_set_hexpand (text, TRUE);
    gtk_box_pack_start (GTK_BOX (row), text, TRUE, TRUE, 0);
    gtk_box_pack_end (GTK_BOX (row), combo, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (parent), row, FALSE, FALSE, 0);
    g_settings_bind (settings, key, combo, "active-id", G_SETTINGS_BIND_DEFAULT);
}

/* The custom-action editor is stored as a typed GSettings array rather than
 * executable desktop files. Names/commands are editable but execution in
 * Miller stays gated by a real filesystem sandbox. */
typedef struct {
    GSettings *settings;
    GtkWidget *list;
    GtkWidget *edit_button;
    GtkWidget *remove_button;
} QACustomUI;

static void
qa_custom_selection_changed (GtkListBox *list, GtkListBoxRow *selected,
                             gpointer user_data)
{
    QACustomUI *ui = user_data;
    if (ui->edit_button) gtk_widget_set_sensitive (ui->edit_button, selected != NULL);
    if (ui->remove_button) gtk_widget_set_sensitive (ui->remove_button, selected != NULL);
    (void) list;
}

static void
qa_custom_ui_free (gpointer data)
{
    QACustomUI *ui = data;
    g_object_unref (ui->settings);
    g_free (ui);
}

static void
qa_custom_refresh (QACustomUI *ui)
{
    GVariant *items = g_settings_get_value (ui->settings, NEMO_QA_CUSTOM);
    GVariantIter iter;
    const char *id, *title, *icon, *kind, *command, *context;
    GList *children = gtk_container_get_children (GTK_CONTAINER (ui->list)), *l;
    for (l=children; l; l=l->next) gtk_widget_destroy (GTK_WIDGET (l->data));
    g_list_free (children);
    g_variant_iter_init (&iter, items);
    while (g_variant_iter_next (&iter, "(&s&s&s&s&s&s)", &id, &title,
                                &icon, &kind, &command, &context)) {
        GtkWidget *row = gtk_label_new (NULL);
        gchar *markup = g_markup_printf_escaped ("<b>%s</b>  · %s\n%s",
                                                  title, kind, command);
        gtk_label_set_markup (GTK_LABEL (row), markup);
        gtk_label_set_xalign (GTK_LABEL (row), 0);
        gtk_widget_set_margin_top (row, 5);
        gtk_widget_set_margin_bottom (row, 5);
        gtk_list_box_insert (GTK_LIST_BOX (ui->list), row, -1);
        g_object_set_data_full (G_OBJECT (row), "qa-custom-id", g_strdup (id), g_free);
        g_free (markup);
    }
    g_variant_unref (items);
    gtk_widget_show_all (ui->list);
}

static GtkWidget *
qa_edit_entry (GtkWidget *grid, const char *label, int row, const char *value)
{
    GtkWidget *text = gtk_label_new (label);
    GtkWidget *entry = gtk_entry_new ();
    gtk_label_set_xalign (GTK_LABEL (text), 0);
    gtk_entry_set_text (GTK_ENTRY (entry), value ? value : "");
    gtk_widget_set_hexpand (entry, TRUE);
    gtk_grid_attach (GTK_GRID (grid), text, 0, row, 1, 1);
    gtk_grid_attach (GTK_GRID (grid), entry, 1, row, 1, 1);
    return entry;
}

static const gchar *
qa_custom_selected (QACustomUI *ui)
{
    GtkListBoxRow *row = gtk_list_box_get_selected_row (GTK_LIST_BOX (ui->list));
    GList *children;
    const gchar *id = NULL;
    if (row == NULL) return NULL;
    children = gtk_container_get_children (GTK_CONTAINER (row));
    if (children) id = g_object_get_data (G_OBJECT (children->data), "qa-custom-id");
    g_list_free (children);
    return id;
}

static void
qa_custom_edit (GtkButton *button, gpointer data)
{
    QACustomUI *ui = data;
    const gchar *old_id = g_object_get_data (G_OBJECT (button), "qa-mode") ?
        qa_custom_selected (ui) : NULL;
    GVariant *old = g_settings_get_value (ui->settings, NEMO_QA_CUSTOM);
    GVariantIter iter;
    const char *id, *name, *icon, *kind, *command, *context;
    gchar *found_name=NULL, *found_icon=NULL, *found_kind=NULL,
          *found_cmd=NULL, *found_context=NULL;
    GtkWidget *dialog, *grid, *name_entry, *icon_entry, *cmd_entry,
              *ctx_entry, *kind_box, *pin_check, *manual_check;
    gchar **pins = g_settings_get_strv (ui->settings, NEMO_QA_PINNED);
    gchar **manual = g_settings_get_strv (ui->settings, NEMO_QA_MANUAL);
    GVariantBuilder builder;
    gboolean accepted;
    guint count = 0;

    g_variant_iter_init (&iter, old);
    while (g_variant_iter_next (&iter, "(&s&s&s&s&s&s)", &id, &name,
                                &icon, &kind, &command, &context)) {
        if (g_strcmp0 (id, old_id) == 0) {
            found_name=g_strdup(name); found_icon=g_strdup(icon);
            found_kind=g_strdup(kind); found_cmd=g_strdup(command);
            found_context=g_strdup(context);
        }
        count++;
    }
    dialog = gtk_dialog_new_with_buttons (old_id ? _("Edit Custom Action") : _("New Custom Action"),
            NULL, GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
            _("Cancel"), GTK_RESPONSE_CANCEL, _("Save"), GTK_RESPONSE_ACCEPT, NULL);
    gtk_window_set_default_size (GTK_WINDOW (dialog), 530, -1);
    grid = gtk_grid_new ();
    gtk_grid_set_column_spacing (GTK_GRID (grid), 12);
    gtk_grid_set_row_spacing (GTK_GRID (grid), 10);
    gtk_container_set_border_width (GTK_CONTAINER (grid), 16);
    name_entry = qa_edit_entry (grid, _("Name"), 0, found_name);
    icon_entry = qa_edit_entry (grid, _("Icon name"), 1, found_icon ? found_icon : "system-run-symbolic");
    cmd_entry = qa_edit_entry (grid, _("Command / script path"), 2, found_cmd);
    ctx_entry = qa_edit_entry (grid, _("Context: file, directory, multiple, all"), 3,
                               found_context ? found_context : "file");
    kind_box = gtk_combo_box_text_new ();
    gtk_combo_box_text_append (GTK_COMBO_BOX_TEXT (kind_box), "command", _("Command (no shell)"));
    gtk_combo_box_text_append (GTK_COMBO_BOX_TEXT (kind_box), "script", _("Script (executable path)"));
    gtk_combo_box_set_active_id (GTK_COMBO_BOX (kind_box), found_kind ? found_kind : "command");
    gtk_grid_attach (GTK_GRID (grid), gtk_label_new (_("Type")), 0, 4, 1, 1);
    gtk_grid_attach (GTK_GRID (grid), kind_box, 1, 4, 1, 1);
    pin_check = gtk_check_button_new_with_label (_("Always pinned in the detail pane"));
    manual_check = gtk_check_button_new_with_label (_("Show in Manual mode"));
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (pin_check),
        strv_contains ((const gchar * const *)pins, old_id));
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (manual_check),
        strv_contains ((const gchar * const *)manual, old_id));
    gtk_grid_attach (GTK_GRID (grid), pin_check, 0, 5, 2, 1);
    gtk_grid_attach (GTK_GRID (grid), manual_check, 0, 6, 2, 1);
    gtk_box_pack_start (GTK_BOX (gtk_dialog_get_content_area (GTK_DIALOG (dialog))),
                        grid, TRUE, TRUE, 0);
    gtk_widget_show_all (grid);
    accepted = gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_ACCEPT;
    if (accepted && *gtk_entry_get_text (GTK_ENTRY (name_entry)) &&
        *gtk_entry_get_text (GTK_ENTRY (cmd_entry))) {
        gchar *new_id = old_id ? g_strdup (old_id) : g_strdup_printf ("custom-%u-%u", (guint)g_get_real_time(), count);
        g_variant_builder_init (&builder, G_VARIANT_TYPE ("a(ssssss)"));
        g_variant_iter_init (&iter, old);
        while (g_variant_iter_next (&iter, "(&s&s&s&s&s&s)", &id, &name,
                                    &icon, &kind, &command, &context)) {
            if (g_strcmp0 (id, old_id) != 0)
                g_variant_builder_add (&builder, "(ssssss)", id, name, icon, kind, command, context);
        }
        g_variant_builder_add (&builder, "(ssssss)", new_id,
            gtk_entry_get_text (GTK_ENTRY (name_entry)),
            gtk_entry_get_text (GTK_ENTRY (icon_entry)),
            gtk_combo_box_get_active_id (GTK_COMBO_BOX (kind_box)),
            gtk_entry_get_text (GTK_ENTRY (cmd_entry)),
            gtk_entry_get_text (GTK_ENTRY (ctx_entry)));
        g_settings_set_value (ui->settings, NEMO_QA_CUSTOM,
                              g_variant_builder_end (&builder));
        update_strv (ui->settings, NEMO_QA_PINNED, new_id,
                     gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (pin_check)));
        update_strv (ui->settings, NEMO_QA_MANUAL, new_id,
                     gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (manual_check)));
        g_free (new_id);
        qa_custom_refresh (ui);
    }
    gtk_widget_destroy (dialog);
    g_variant_unref (old);
    g_free (found_name); g_free (found_icon); g_free (found_kind);
    g_free (found_cmd); g_free (found_context);
    g_strfreev (pins); g_strfreev (manual);
}

static void
qa_custom_remove (GtkButton *button, gpointer data)
{
    QACustomUI *ui = data;
    const char *remove_id = qa_custom_selected (ui);
    GVariant *items;
    GVariantIter iter;
    GVariantBuilder builder;
    const char *id, *title, *icon, *kind, *command, *context;
    if (!remove_id) return;
    items = g_settings_get_value (ui->settings, NEMO_QA_CUSTOM);
    g_variant_builder_init (&builder, G_VARIANT_TYPE ("a(ssssss)"));
    g_variant_iter_init (&iter, items);
    while (g_variant_iter_next (&iter, "(&s&s&s&s&s&s)", &id, &title,
                                &icon, &kind, &command, &context))
        if (g_strcmp0 (id, remove_id) != 0)
            g_variant_builder_add (&builder, "(ssssss)", id, title, icon, kind, command, context);
    g_settings_set_value (ui->settings, NEMO_QA_CUSTOM, g_variant_builder_end (&builder));
    update_strv (ui->settings, NEMO_QA_PINNED, remove_id, FALSE);
    update_strv (ui->settings, NEMO_QA_MANUAL, remove_id, FALSE);
    g_variant_unref (items);
    qa_custom_refresh (ui);
}

/* Import an existing Nemo Action or script as an editable custom entry.
 * Importing records a definition; it runs like a Nemo script (as the user,
 * in the selected folder). The original script/action is unchanged. */
static void
qa_custom_import (GtkButton *button, gpointer data)
{
    QACustomUI *ui = data;
    gboolean nemo_action = GPOINTER_TO_INT (
        g_object_get_data (G_OBJECT (button), "qa-nemo-action"));
    GtkWidget *chooser = gtk_file_chooser_dialog_new (
        nemo_action ? _("Import Nemo Action") : _("Import Nemo Script"),
        NULL, GTK_FILE_CHOOSER_ACTION_OPEN,
        _("Cancel"), GTK_RESPONSE_CANCEL,
        _("Import"), GTK_RESPONSE_ACCEPT, NULL);
    gchar *filename = NULL, *name = NULL, *command = NULL, *icon = NULL;
    GVariant *existing;
    GVariantBuilder builder;
    GVariantIter iter;
    const gchar *id0, *name0, *icon0, *kind0, *command0, *context0;
    gchar *id;
    if (gtk_dialog_run (GTK_DIALOG (chooser)) == GTK_RESPONSE_ACCEPT)
        filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (chooser));
    gtk_widget_destroy (chooser);
    if (!filename) return;
    if (nemo_action) {
        GKeyFile *keyfile = g_key_file_new ();
        GError *error = NULL;
        if (g_key_file_load_from_file (keyfile, filename, G_KEY_FILE_NONE, &error)) {
            name = g_key_file_get_locale_string (keyfile, "Nemo Action", "Name", NULL, NULL);
            command = g_key_file_get_string (keyfile, "Nemo Action", "Exec", NULL);
            icon = g_key_file_get_string (keyfile, "Nemo Action", "Icon-Name", NULL);
        }
        if (error) {
            GtkWidget *err = gtk_message_dialog_new (NULL, GTK_DIALOG_MODAL,
                GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE, "%s", error->message);
            gtk_dialog_run (GTK_DIALOG (err));
            gtk_widget_destroy (err);
            g_error_free (error);
        }
        g_key_file_unref (keyfile);
    } else {
        name = g_path_get_basename (filename);
        command = g_shell_quote (filename);
        icon = g_strdup ("system-run-symbolic");
    }
    if (!command || !*command) goto cleanup;
    if (!name) name = g_path_get_basename (filename);
    if (!icon) icon = g_strdup ("system-run-symbolic");
    /* A legacy Nemo Exec line may contain %F or %U expansion tokens.
     * It must be reviewed/edited before it can run; never execute blindly. */
    existing = g_settings_get_value (ui->settings, NEMO_QA_CUSTOM);
    g_variant_builder_init (&builder, G_VARIANT_TYPE ("a(ssssss)"));
    g_variant_iter_init (&iter, existing);
    while (g_variant_iter_next (&iter, "(&s&s&s&s&s&s)",
            &id0, &name0, &icon0, &kind0, &command0, &context0))
        g_variant_builder_add (&builder, "(ssssss)",
            id0, name0, icon0, kind0, command0, context0);
    id = g_strdup_printf ("custom-import-%" G_GINT64_FORMAT, g_get_real_time ());
    g_variant_builder_add (&builder, "(ssssss)", id, name, icon,
                            nemo_action ? "command" : "script", command,
                            nemo_action ? "disabled" : "file");
    g_settings_set_value (ui->settings, NEMO_QA_CUSTOM,
                          g_variant_builder_end (&builder));
    g_free (id);
    g_variant_unref (existing);
    qa_custom_refresh (ui);
cleanup:
    g_free (filename); g_free (name); g_free (command); g_free (icon);
}

GtkWidget *
nemo_quick_actions_preferences_page (GSettings *settings)
{
    GtkWidget *page = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
    GtkWidget *scroll = gtk_scrolled_window_new (NULL, NULL);
    GtkWidget *body = gtk_box_new (GTK_ORIENTATION_VERTICAL, 10);
    GtkWidget *toggle, *label, *row, *limit, *custom_list, *button;
    QACustomUI *ui;
    const gchar *last_category = NULL;
    const gchar *mode_ids[] = {"automatic", "manual", "hybrid", NULL};
    const gchar *mode_labels[] = {N_("Automatic"), N_("Manual"), N_("Hybrid"), NULL};
    const gchar *un_ids[] = {"hide", "disable", NULL};
    const gchar *un_labels[] = {N_("Hide unavailable"), N_("Show disabled"), NULL};
    const gchar *layout_ids[] = {"full", "compact", "icons", NULL};
    const gchar *layout_labels[] = {N_("Icon + title + subtitle"), N_("Icon + title"), N_("Icons only"), NULL};
    gsize i;
    gtk_container_set_border_width (GTK_CONTAINER (page), 14);
    label = qa_heading (_("Quick Actions"));
    gtk_box_pack_start (GTK_BOX (page), label, FALSE, FALSE, 0);
    toggle = gtk_check_button_new_with_label (_("Show Quick Actions in Files"));
    g_settings_bind (settings, NEMO_QA_ENABLED, toggle, "active", G_SETTINGS_BIND_DEFAULT);
    gtk_box_pack_start (GTK_BOX (page), toggle, FALSE, FALSE, 0);
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroll),
                                    GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_box_pack_start (GTK_BOX (page), scroll, TRUE, TRUE, 0);
    gtk_container_add (GTK_CONTAINER (scroll), body);
    g_object_bind_property (toggle, "active", body, "sensitive", G_BINDING_SYNC_CREATE);
    qa_bind_combo (settings, body, _("Behavior"), NEMO_QA_MODE,
                   mode_ids, mode_labels);
    qa_bind_combo (settings, body, _("Unavailable actions"), NEMO_QA_UNAVAILABLE,
                   un_ids, un_labels);
    qa_bind_combo (settings, body, _("Button appearance"), NEMO_QA_LAYOUT,
                   layout_ids, layout_labels);
    row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
    label = gtk_label_new (_("Maximum suggested actions (0 = unlimited)"));
    limit = gtk_spin_button_new_with_range (0, 100, 1);
    gtk_box_pack_start (GTK_BOX (row), label, TRUE, TRUE, 0);
    gtk_box_pack_end (GTK_BOX (row), limit, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (body), row, FALSE, FALSE, 0);
    g_settings_bind (settings, NEMO_QA_LIMIT, limit, "value", G_SETTINGS_BIND_DEFAULT);

    label = qa_heading (_("Action Library · select manually or pin permanently"));
    gtk_box_pack_start (GTK_BOX (body), label, FALSE, FALSE, 0);
    label = gtk_label_new (_("Pin = always visible; Show = included in Manual mode.\n"
                             "In Hybrid mode pinned actions appear first, then relevant suggestions.\n"
                             "Changes apply immediately across tabs and windows."));
    gtk_label_set_xalign (GTK_LABEL (label), 0);
    gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
    gtk_box_pack_start (GTK_BOX (body), label, FALSE, FALSE, 0);
    for (i=0; i<G_N_ELEMENTS(catalog); i++) {
        const NemoQAAction *action = &catalog[i];
        GtkWidget *row, *pin, *manual;
        if (g_strcmp0 (last_category, action->category) != 0) {
            label = qa_heading (_(action->category));
            gtk_box_pack_start (GTK_BOX (body), label, FALSE, FALSE, 0);
            last_category = action->category;
        }
        row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
        label = gtk_label_new (_(action->label));
        gtk_label_set_xalign (GTK_LABEL (label), 0);
        gtk_widget_set_hexpand (label, TRUE);
        if (action->safety == NEMO_QA_NOT_READY)
            gtk_widget_set_tooltip_text (label, _("Catalog entry; implementation not available yet"));
        pin = qa_checkbox (settings, NEMO_QA_PINNED, action->id, _("Pin"));
        manual = qa_checkbox (settings, NEMO_QA_MANUAL, action->id, _("Show"));
        gtk_box_pack_start (GTK_BOX (row), label, TRUE, TRUE, 0);
        gtk_box_pack_start (GTK_BOX (row), manual, FALSE, FALSE, 0);
        gtk_box_pack_start (GTK_BOX (row), pin, FALSE, FALSE, 0);
        gtk_box_pack_start (GTK_BOX (row), qa_reorder_button (settings, NEMO_QA_PINNED,
                                                              action->id, TRUE), FALSE, FALSE, 0);
        gtk_box_pack_start (GTK_BOX (row), qa_reorder_button (settings, NEMO_QA_PINNED,
                                                              action->id, FALSE), FALSE, FALSE, 0);
        gtk_box_pack_start (GTK_BOX (body), row, FALSE, FALSE, 0);
    }

    label = qa_heading (_("Custom Actions & Scripts"));
    gtk_box_pack_start (GTK_BOX (body), label, FALSE, FALSE, 0);
    label = gtk_label_new (_("Commands are argument vectors (not shell snippets).\n"
        "Use %f for one selected path, %d for its folder, or %F for all selected paths.\n"
        "Commands run as you, in the selected folder, like Nemo scripts.\n"
        "Imported Nemo Actions must be reviewed before use."));
    gtk_label_set_xalign (GTK_LABEL (label), 0);
    gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
    gtk_box_pack_start (GTK_BOX (body), label, FALSE, FALSE, 0);
    custom_list = gtk_list_box_new ();
    gtk_widget_set_size_request (custom_list, -1, 65);
    gtk_box_pack_start (GTK_BOX (body), custom_list, FALSE, FALSE, 0);
    ui = g_new0 (QACustomUI, 1);
    ui->settings = g_object_ref (settings);
    ui->list = custom_list;
    g_object_set_data_full (G_OBJECT (page), "quick-custom-ui", ui, qa_custom_ui_free);
    g_signal_connect (custom_list, "row-selected",
                      G_CALLBACK (qa_custom_selection_changed), ui);
    qa_custom_refresh (ui);
    row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 7);
    button = gtk_button_new_with_label (_("Add Custom Action…"));
    g_signal_connect (button, "clicked", G_CALLBACK (qa_custom_edit), ui);
    gtk_box_pack_start (GTK_BOX (row), button, FALSE, FALSE, 0);
    button = gtk_button_new_with_label (_("Edit Selected…"));
    ui->edit_button = button;
    gtk_widget_set_sensitive (button, FALSE);
    g_object_set_data (G_OBJECT (button), "qa-mode", GINT_TO_POINTER (1));
    g_signal_connect (button, "clicked", G_CALLBACK (qa_custom_edit), ui);
    gtk_box_pack_start (GTK_BOX (row), button, FALSE, FALSE, 0);
    button = gtk_button_new_with_label (_("Remove Selected"));
    ui->remove_button = button;
    gtk_widget_set_sensitive (button, FALSE);
    g_signal_connect (button, "clicked", G_CALLBACK (qa_custom_remove), ui);
    gtk_box_pack_start (GTK_BOX (row), button, FALSE, FALSE, 0);
    button = gtk_button_new_with_label (_("Import Nemo Action…"));
    g_object_set_data (G_OBJECT (button), "qa-nemo-action", GINT_TO_POINTER (1));
    g_signal_connect (button, "clicked", G_CALLBACK (qa_custom_import), ui);
    gtk_box_pack_start (GTK_BOX (row), button, FALSE, FALSE, 0);
    button = gtk_button_new_with_label (_("Import Nemo Script…"));
    g_signal_connect (button, "clicked", G_CALLBACK (qa_custom_import), ui);
    gtk_box_pack_start (GTK_BOX (row), button, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (body), row, FALSE, FALSE, 0);
    gtk_widget_show_all (page);
    return page;
}
