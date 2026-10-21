/* Dakkar: shared Quick Actions for every file view.  GPL-2.0-or-later.
 *
 * Formerly two implementations existed: a rich one inside the Miller view
 * and a reduced one in the window slot for Icons/List/Compact.  Both are
 * replaced by this file.  Commands run with the user's own permissions on
 * explicitly selected items (argv only, never through a shell). */

#include <config.h>
#include "nemo-corner-style.h"
#include <string.h>
#include <glib/gi18n.h>
#include <libnemo-private/nemo-file.h>
#include <libnemo-private/nemo-global-preferences.h>
#include <libnemo-private/nemo-terminal.h>

#include "dakkar-qa-run.h"
#include "nemo-compress-dialog.h"
#include "nemo-actions.h"

/* ------------------------------------------------------------------ */
/* context                                                             */
/* ------------------------------------------------------------------ */

DakkarQAContext *
dakkar_qa_context_new (GtkWidget *anchor, NemoView *view)
{
    DakkarQAContext *ctx = g_new0 (DakkarQAContext, 1);
    NemoFile *folder;
    ctx->anchor = anchor;
    if (view != NULL) {
        ctx->view = g_object_ref (view);
        ctx->selection = nemo_view_get_selection (view);
        folder = nemo_view_get_directory_as_file (view);
        ctx->folder = folder != NULL ? nemo_file_ref (folder) : NULL;
    }
    return ctx;
}

void
dakkar_qa_context_free (DakkarQAContext *ctx)
{
    if (ctx == NULL) return;
    nemo_file_list_free (ctx->selection);
    if (ctx->folder != NULL) nemo_file_unref (ctx->folder);
    g_clear_object (&ctx->view);
    g_free (ctx->terminal_subtitle);
    g_free (ctx);
}

static GtkWindow *
qa_parent (DakkarQAContext *ctx)
{
    GtkWidget *top = ctx->anchor != NULL ? gtk_widget_get_toplevel (ctx->anchor) : NULL;
    if (top == NULL && ctx->view != NULL)
        top = gtk_widget_get_toplevel (GTK_WIDGET (ctx->view));
    return GTK_IS_WINDOW (top) ? GTK_WINDOW (top) : NULL;
}

static NemoFile *
qa_single (DakkarQAContext *ctx)
{
    return ctx->selection != NULL && ctx->selection->next == NULL ?
        NEMO_FILE (ctx->selection->data) : NULL;
}

static gboolean
qa_single_regular_file (DakkarQAContext *ctx)
{
    NemoFile *file = qa_single (ctx);
    return file != NULL && !nemo_file_is_directory (file);
}

/* Local path of the single selected item, or NULL. */
static gchar *
qa_selected_path (DakkarQAContext *ctx)
{
    NemoFile *file = qa_single (ctx);
    GFile *location;
    gchar *path;
    if (file == NULL) return NULL;
    location = nemo_file_get_location (file);
    path = location != NULL ? g_file_get_path (location) : NULL;
    g_clear_object (&location);
    return path;
}

/* The folder an action works "in": a selected folder, the parent of a
 * selected file, or the folder being shown. */
static GFile *
qa_target_dir (DakkarQAContext *ctx)
{
    NemoFile *file = qa_single (ctx);
    if (file != NULL) {
        GFile *location = nemo_file_get_location (file);
        GFile *target = NULL;
        if (location != NULL) {
            target = nemo_file_is_directory (file) ? g_object_ref (location) :
                                                     g_file_get_parent (location);
            g_object_unref (location);
        }
        return target;
    }
    return ctx->folder != NULL ? nemo_file_get_location (ctx->folder) : NULL;
}

/* Target of the "in a folder" actions (terminals, new folder): exactly the
 * rule of the menus, nemo_view_resolve_target_folder.  NULL when the
 * selected items live in different folders. */
static GFile *
qa_action_dir (DakkarQAContext *ctx, gboolean *enclosing)
{
    if (enclosing != NULL) *enclosing = FALSE;
    if (ctx->view == NULL) return NULL;
    return nemo_view_resolve_target_folder (ctx->view, ctx->selection, enclosing);
}

static gboolean
qa_enclosing (DakkarQAContext *ctx)
{
    gboolean enclosing = FALSE;
    GFile *dir = qa_action_dir (ctx, &enclosing);
    g_clear_object (&dir);
    return enclosing;
}

/* Local folder a quick-action terminal opens in (same rule as the menus). */
static gchar *
qa_terminal_dir (DakkarQAContext *ctx)
{
    GFile *dir = qa_action_dir (ctx, NULL);
    gchar *path = dir != NULL && g_file_is_native (dir) ? g_file_get_path (dir) : NULL;
    g_clear_object (&dir);
    if (path != NULL && !g_file_test (path, G_FILE_TEST_IS_DIR))
        g_clear_pointer (&path, g_free);
    return path;
}

/* Title of a quick action button: the terminal actions say where they
 * open, exactly like the context menus and the File menu. */
static const gchar *
qa_title (DakkarQAContext *ctx, const NemoQAAction *action)
{
    if (g_str_equal (action->id, "terminal") || g_str_equal (action->id, "root-terminal"))
        return nemo_terminal_get_label (g_str_equal (action->id, "root-terminal"),
                                        qa_enclosing (ctx));
    if (g_str_equal (action->id, "new-folder"))
        return qa_enclosing (ctx) ? _("Create New Folder in Enclosing Folder")
                                  : _("Create New Folder Here");
    return _(action->label);
}

static gchar *
qa_selected_or_folder_path (DakkarQAContext *ctx)
{
    gchar *path = qa_selected_path (ctx);
    GFile *dir;
    if (path != NULL) return path;
    dir = qa_target_dir (ctx);
    path = dir != NULL ? g_file_get_path (dir) : NULL;
    g_clear_object (&dir);
    return path;
}

static gchar *
qa_git_root (DakkarQAContext *ctx)
{
    gchar *path = qa_selected_or_folder_path (ctx);
    gchar *dir;
    if (path == NULL) return NULL;
    dir = g_file_test (path, G_FILE_TEST_IS_DIR) ? g_strdup (path) :
                                                   g_path_get_dirname (path);
    g_free (path);
    while (dir != NULL && *dir) {
        gchar *dotgit = g_build_filename (dir, ".git", NULL);
        gboolean found = g_file_test (dotgit, G_FILE_TEST_EXISTS);
        gchar *parent;
        g_free (dotgit);
        if (found) return dir;
        parent = g_path_get_dirname (dir);
        if (g_strcmp0 (dir, parent) == 0) {
            g_free (parent);
            break;
        }
        g_free (dir);
        dir = parent;
    }
    g_free (dir);
    return NULL;
}

unsigned
dakkar_qa_context_flags (DakkarQAContext *ctx)
{
    unsigned flags = 0;
    NemoFile *file;
    if (ctx->selection != NULL && ctx->selection->next != NULL) {
        GList *l;
        gboolean all_archives = TRUE;
        flags = NEMO_QA_MULTI;
        /* several archives: Extract Here / Extract To… apply too */
        for (l = ctx->selection; l != NULL && all_archives; l = l->next) {
            NemoFile *f = NEMO_FILE (l->data);
            gchar *mime = nemo_file_get_mime_type (f), *name = nemo_file_get_name (f);
            if (nemo_file_is_directory (f) || !nemo_qa_archive_matches (mime, name))
                all_archives = FALSE;
            g_free (mime);
            g_free (name);
        }
        if (all_archives) flags |= NEMO_QA_ARCHIVE;
    } else if (ctx->selection == NULL) {
        flags = NEMO_QA_EMPTY | NEMO_QA_DIRECTORY;
    } else {
        gchar *mime, *name;
        file = NEMO_FILE (ctx->selection->data);
        flags = nemo_file_is_directory (file) ? NEMO_QA_DIRECTORY : NEMO_QA_FILE;
        mime = nemo_file_get_mime_type (file);
        name = nemo_file_get_name (file);
        if (mime != NULL) {
            if (g_str_has_prefix (mime, "image/")) flags |= NEMO_QA_IMAGE;
            if (g_str_has_prefix (mime, "video/")) flags |= NEMO_QA_VIDEO;
            if (g_str_has_prefix (mime, "audio/")) flags |= NEMO_QA_AUDIO;
            if (g_str_has_prefix (mime, "text/") ||
                g_str_equal (mime, "application/pdf")) flags |= NEMO_QA_DOCUMENT;
        }
        if (!nemo_file_is_directory (file) && nemo_qa_archive_matches (mime, name))
            flags |= NEMO_QA_ARCHIVE;
        g_free (mime);
        g_free (name);
    }
    {
        gchar *root = qa_git_root (ctx);
        if (root != NULL) flags |= NEMO_QA_GIT;
        g_free (root);
    }
    return flags;
}

/* ------------------------------------------------------------------ */
/* dialogs and subprocess output                                       */
/* ------------------------------------------------------------------ */

typedef struct {
    GtkTextBuffer *buffer;
    GSubprocess *process;
} QAOutputJob;

static void
qa_output_finished (GObject *object, GAsyncResult *result, gpointer user_data)
{
    QAOutputJob *job = user_data;
    gchar *out = NULL, *err = NULL;
    GError *error = NULL;
    if (!g_subprocess_communicate_utf8_finish (G_SUBPROCESS (object), result,
                                               &out, &err, &error)) {
        gtk_text_buffer_set_text (job->buffer, error != NULL ? error->message :
                                  _("Unable to run command"), -1);
    } else {
        gint status = g_subprocess_get_exit_status (G_SUBPROCESS (object));
        gchar *text = g_strdup_printf ("%s%s%s%s", out ? out : "",
            (err && *err) ? "\n--- stderr ---\n" : "", err ? err : "",
            status != 0 ? _("\n(command finished with an error)") : "");
        gtk_text_buffer_set_text (job->buffer, *text ? text : _("(no output)"), -1);
        g_free (text);
    }
    g_clear_error (&error);
    g_free (out);
    g_free (err);
    g_object_unref (job->buffer);
    g_object_unref (job->process);
    g_free (job);
}

static void
qa_run_output (GtkTextBuffer *buffer, const gchar * const *argv, const gchar *cwd)
{
    GError *error = NULL;
    GSubprocessLauncher *launcher = g_subprocess_launcher_new (
        G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_PIPE);
    GSubprocess *proc;
    QAOutputJob *job;
    if (cwd != NULL) g_subprocess_launcher_set_cwd (launcher, cwd);
    proc = g_subprocess_launcher_spawnv (launcher, argv, &error);
    g_object_unref (launcher);
    if (proc == NULL) {
        gtk_text_buffer_set_text (buffer, error != NULL ? error->message :
                                  _("Cannot launch process"), -1);
        g_clear_error (&error);
        return;
    }
    gtk_text_buffer_set_text (buffer, _("Working…"), -1);
    job = g_new0 (QAOutputJob, 1);
    job->buffer = g_object_ref (buffer);
    job->process = proc;
    g_subprocess_communicate_utf8_async (proc, NULL, NULL, qa_output_finished, job);
}

static GtkWidget *
qa_dialog (DakkarQAContext *ctx, const gchar *title, const gchar *subtitle,
           GtkTextBuffer **buffer)
{
    GtkWidget *dialog = gtk_dialog_new_with_buttons (title, qa_parent (ctx),
        GTK_DIALOG_DESTROY_WITH_PARENT, _("Close"), GTK_RESPONSE_CLOSE, NULL);
    GtkWidget *content = gtk_dialog_get_content_area (GTK_DIALOG (dialog));
    GtkWidget *text = gtk_text_view_new ();
    GtkWidget *scroll = gtk_scrolled_window_new (NULL, NULL);
    GtkWidget *label = gtk_label_new (subtitle);
    gtk_window_set_default_size (GTK_WINDOW (dialog), 690, 480);
    gtk_container_set_border_width (GTK_CONTAINER (content), 14);
    gtk_widget_set_margin_bottom (label, 10);
    gtk_label_set_xalign (GTK_LABEL (label), 0);
    gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
    gtk_label_set_selectable (GTK_LABEL (label), TRUE);
    gtk_box_pack_start (GTK_BOX (content), label, FALSE, FALSE, 0);
    gtk_text_view_set_editable (GTK_TEXT_VIEW (text), FALSE);
    gtk_text_view_set_monospace (GTK_TEXT_VIEW (text), TRUE);
    gtk_text_view_set_wrap_mode (GTK_TEXT_VIEW (text), GTK_WRAP_WORD_CHAR);
    gtk_container_add (GTK_CONTAINER (scroll), text);
    gtk_box_pack_start (GTK_BOX (content), scroll, TRUE, TRUE, 0);
    if (buffer != NULL) *buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (text));
    g_signal_connect_swapped (dialog, "response", G_CALLBACK (gtk_widget_destroy), dialog);
    gtk_widget_show_all (dialog);
    return dialog;
}

static void
qa_message (DakkarQAContext *ctx, GtkMessageType type, const gchar *title,
            const gchar *message)
{
    GtkWidget *dialog = gtk_message_dialog_new (qa_parent (ctx),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, type,
        GTK_BUTTONS_CLOSE, "%s", message);
    gtk_window_set_title (GTK_WINDOW (dialog), title);
    g_signal_connect_swapped (dialog, "response", G_CALLBACK (gtk_widget_destroy), dialog);
    gtk_widget_show (dialog);
}

/* ------------------------------------------------------------------ */
/* native Nemo actions                                                 */
/* ------------------------------------------------------------------ */

static const char *
qa_native_name (const char *id)
{
    if (g_strcmp0 (id, "properties") == 0) return NEMO_ACTION_PROPERTIES;
    if (g_strcmp0 (id, "rename") == 0) return NEMO_ACTION_RENAME;
    if (g_strcmp0 (id, "batch-rename") == 0) return NEMO_ACTION_RENAME;
    if (g_strcmp0 (id, "duplicate") == 0) return NEMO_ACTION_DUPLICATE;
    if (g_strcmp0 (id, "make-symlink") == 0) return NEMO_ACTION_CREATE_LINK;
    return NULL;
}

static GtkAction *
qa_native_action (NemoView *view, const char *id)
{
    const char *name = qa_native_name (id);
    GtkUIManager *ui;
    GList *l;
    if (name == NULL || view == NULL) return NULL;
    ui = nemo_view_get_ui_manager (view);
    if (ui == NULL) return NULL;
    nemo_view_update_actions_and_extensions (view);
    for (l = gtk_ui_manager_get_action_groups (ui); l != NULL; l = l->next) {
        GtkAction *action = gtk_action_group_get_action (GTK_ACTION_GROUP (l->data), name);
        if (action != NULL) return action;
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* individual actions                                                  */
/* ------------------------------------------------------------------ */

typedef struct {
    gchar *repo;
    GtkTextBuffer *buffer;
} QAGitUI;

static void
qa_gitui_free (gpointer data)
{
    QAGitUI *git = data;
    g_free (git->repo);
    g_free (git);
}

static void
qa_git_query (GtkButton *button, gpointer data)
{
    QAGitUI *git = data;
    const gchar *op = g_object_get_data (G_OBJECT (button), "qa-git-op");
    const gchar *argv[9] = {"git", "-C", git->repo, NULL, NULL, NULL, NULL, NULL, NULL};
    if (g_str_equal (op, "status")) {
        argv[3] = "status"; argv[4] = "--short"; argv[5] = "--branch";
    } else if (g_str_equal (op, "diff")) {
        argv[3] = "diff"; argv[4] = "--stat"; argv[5] = "HEAD";
    } else if (g_str_equal (op, "log")) {
        argv[3] = "log"; argv[4] = "--max-count=40"; argv[5] = "--oneline";
    } else if (g_str_equal (op, "branch")) {
        argv[3] = "branch"; argv[4] = "--all"; argv[5] = "--verbose";
    } else if (g_str_equal (op, "stash")) {
        argv[3] = "stash"; argv[4] = "list";
    } else if (g_str_equal (op, "tag")) {
        argv[3] = "tag"; argv[4] = "--list";
    } else if (g_str_equal (op, "remote")) {
        argv[3] = "remote"; argv[4] = "-v";
    } else {
        return;
    }
    qa_run_output (git->buffer, argv, NULL);
}

static void
qa_git_dialog (DakkarQAContext *ctx)
{
    static const struct { const gchar *op; const gchar *label; } ops[] = {
        {"status", N_("Status")}, {"diff", N_("Diff")}, {"log", N_("History")},
        {"branch", N_("Branches")}, {"stash", N_("Stash")}, {"tag", N_("Tags")},
        {"remote", N_("Remotes")}
    };
    gchar *root = qa_git_root (ctx);
    GtkWidget *dialog, *content, *grid;
    GtkTextBuffer *buffer;
    QAGitUI *ui;
    guint i;
    if (root == NULL) {
        qa_message (ctx, GTK_MESSAGE_INFO, _("Git Actions"),
                    _("No Git repository found for the current folder."));
        return;
    }
    dialog = qa_dialog (ctx, _("Git Actions"), root, &buffer);
    content = gtk_dialog_get_content_area (GTK_DIALOG (dialog));
    grid = gtk_grid_new ();
    gtk_grid_set_column_spacing (GTK_GRID (grid), 6);
    gtk_grid_set_row_spacing (GTK_GRID (grid), 6);
    gtk_box_pack_start (GTK_BOX (content), grid, FALSE, FALSE, 0);
    gtk_box_reorder_child (GTK_BOX (content), grid, 1);
    ui = g_new0 (QAGitUI, 1);
    ui->repo = root;
    ui->buffer = buffer;
    g_object_set_data_full (G_OBJECT (dialog), "git-ui", ui, qa_gitui_free);
    for (i = 0; i < G_N_ELEMENTS (ops); i++) {
        GtkWidget *button = gtk_button_new_with_label (_(ops[i].label));
        g_object_set_data (G_OBJECT (button), "qa-git-op", (gpointer) ops[i].op);
        g_signal_connect (button, "clicked", G_CALLBACK (qa_git_query), ui);
        gtk_grid_attach (GTK_GRID (grid), button, i % 4, i / 4, 1, 1);
    }
    gtk_widget_show_all (dialog);
    {
        const gchar *argv[] = {"git", "-C", root, "status", "--short", "--branch", NULL};
        qa_run_output (buffer, argv, NULL);
    }
}

static GList *
qa_locations (DakkarQAContext *ctx)
{
    GList *l, *out = NULL;
    for (l = ctx->selection; l != NULL; l = l->next)
        out = g_list_prepend (out, nemo_file_get_location (NEMO_FILE (l->data)));
    return g_list_reverse (out);
}

/* Fallback when Dakkar is built without libarchive: create one new archive
 * next to the selected items with zip/tar/7z. Never overwrites. With
 * libarchive the shared Compress dialog and transfer window are used. */
static void
qa_compress_dialog (DakkarQAContext *ctx)
{
    GList *l;
    GPtrArray *names = g_ptr_array_new_with_free_func (g_free);
    gchar *parent = NULL, *archive = NULL, *program = NULL, *format = NULL;
    GtkWidget *dialog, *grid, *entry, *combo, *content, *notice;
    GtkTextBuffer *output;
    const gchar *argv[256] = {NULL};
    const gchar *ext, *chosen;
    gboolean ok = TRUE;
    guint i, n;

    if (ctx->selection == NULL) {
        qa_message (ctx, GTK_MESSAGE_INFO, _("Compress"),
                    _("Select the file(s) or folder(s) to archive first."));
        goto out;
    }
    for (l = ctx->selection; l != NULL && ok; l = l->next) {
        GFile *gf = nemo_file_get_location (NEMO_FILE (l->data));
        gchar *path = gf != NULL ? g_file_get_path (gf) : NULL;
        gchar *folder = path != NULL ? g_path_get_dirname (path) : NULL;
        if (path == NULL || folder == NULL ||
            (parent != NULL && g_strcmp0 (parent, folder) != 0)) ok = FALSE;
        if (parent == NULL && folder != NULL) parent = g_strdup (folder);
        if (ok) g_ptr_array_add (names, g_path_get_basename (path));
        g_free (folder);
        g_free (path);
        g_clear_object (&gf);
    }
    if (!ok || parent == NULL || names->len == 0 || names->len > 200) {
        qa_message (ctx, GTK_MESSAGE_WARNING, _("Cannot compress"),
            _("All selected items must be local files in the same folder (at most 200)."));
        goto out;
    }
    dialog = gtk_dialog_new_with_buttons (_("Compress to Archive"), qa_parent (ctx),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        _("Cancel"), GTK_RESPONSE_CANCEL, _("Create Archive"), GTK_RESPONSE_ACCEPT, NULL);
    gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_ACCEPT);
    content = gtk_dialog_get_content_area (GTK_DIALOG (dialog));
    grid = gtk_grid_new ();
    gtk_grid_set_row_spacing (GTK_GRID (grid), 10);
    gtk_grid_set_column_spacing (GTK_GRID (grid), 12);
    gtk_container_set_border_width (GTK_CONTAINER (grid), 14);
    entry = gtk_entry_new ();
    gtk_entry_set_activates_default (GTK_ENTRY (entry), TRUE);
    gtk_entry_set_text (GTK_ENTRY (entry), names->len == 1 ?
                        (const gchar *) names->pdata[0] : _("Archive"));
    combo = gtk_combo_box_text_new ();
    gtk_combo_box_text_append (GTK_COMBO_BOX_TEXT (combo), "zip", "ZIP");
    gtk_combo_box_text_append (GTK_COMBO_BOX_TEXT (combo), "targz", "TAR.GZ");
    gtk_combo_box_text_append (GTK_COMBO_BOX_TEXT (combo), "tarzst", "TAR.ZST");
    gtk_combo_box_text_append (GTK_COMBO_BOX_TEXT (combo), "7z", "7z");
    gtk_combo_box_set_active_id (GTK_COMBO_BOX (combo), "zip");
    gtk_grid_attach (GTK_GRID (grid), gtk_label_new (_("Archive name")), 0, 0, 1, 1);
    gtk_grid_attach (GTK_GRID (grid), entry, 1, 0, 1, 1);
    gtk_grid_attach (GTK_GRID (grid), gtk_label_new (_("Format")), 0, 1, 1, 1);
    gtk_grid_attach (GTK_GRID (grid), combo, 1, 1, 1, 1);
    notice = gtk_label_new (_("Creates a new archive only. Existing files are never overwritten."));
    gtk_label_set_line_wrap (GTK_LABEL (notice), TRUE);
    gtk_grid_attach (GTK_GRID (grid), notice, 0, 2, 2, 1);
    gtk_box_pack_start (GTK_BOX (content), grid, FALSE, FALSE, 0);
    gtk_widget_show_all (grid);
    if (gtk_dialog_run (GTK_DIALOG (dialog)) != GTK_RESPONSE_ACCEPT) {
        gtk_widget_destroy (dialog);
        goto out;
    }
    chosen = gtk_entry_get_text (GTK_ENTRY (entry));
    format = g_strdup (gtk_combo_box_get_active_id (GTK_COMBO_BOX (combo)));
    if (chosen == NULL || *chosen == '\0' || strchr (chosen, '/') != NULL ||
        g_str_equal (chosen, ".") || g_str_equal (chosen, "..")) ok = FALSE;
    ext = g_str_equal (format, "targz") ? ".tar.gz" :
          g_str_equal (format, "tarzst") ? ".tar.zst" :
          g_str_equal (format, "7z") ? ".7z" : ".zip";
    if (ok) {
        gchar *basename = g_strconcat (chosen, ext, NULL);
        archive = g_build_filename (parent, basename, NULL);
        g_free (basename);
    }
    gtk_widget_destroy (dialog);
    if (archive != NULL && (g_file_test (archive, G_FILE_TEST_EXISTS) ||
                            g_file_test (archive, G_FILE_TEST_IS_SYMLINK))) ok = FALSE;
    if (!ok || archive == NULL) {
        qa_message (ctx, GTK_MESSAGE_WARNING, _("Invalid archive name"),
            _("Use a new file name without a slash. Existing files are never overwritten."));
        goto out;
    }
    program = g_find_program_in_path (g_str_equal (format, "zip") ? "zip" :
                                      g_str_equal (format, "7z") ? "7z" : "tar");
    if (program == NULL) {
        qa_message (ctx, GTK_MESSAGE_WARNING, _("Archiver not installed"),
            _("Install the archiver for the chosen format or choose another format."));
        goto out;
    }
    n = 0;
    argv[n++] = program;
    if (g_str_equal (format, "zip")) {
        argv[n++] = "-qr"; argv[n++] = archive; argv[n++] = "--";
    } else if (g_str_equal (format, "7z")) {
        argv[n++] = "a"; argv[n++] = "-bd"; argv[n++] = archive; argv[n++] = "--";
    } else if (g_str_equal (format, "targz")) {
        argv[n++] = "-czf"; argv[n++] = archive; argv[n++] = "--";
    } else {
        argv[n++] = "--zstd"; argv[n++] = "-cf"; argv[n++] = archive; argv[n++] = "--";
    }
    for (i = 0; i < names->len && n < G_N_ELEMENTS (argv) - 1; i++)
        argv[n++] = names->pdata[i];
    argv[n] = NULL;
    qa_dialog (ctx, _("Compress to Archive"), archive, &output);
    qa_run_output (output, argv, parent);
out:
    g_ptr_array_unref (names);
    g_free (parent);
    g_free (archive);
    g_free (program);
    g_free (format);
}

/* User-configured custom command: an argument vector with %f (one selected
 * path), %d (its folder) and %F (all selected paths).  Runs with the user's
 * permissions in the target folder, like a Nemo script, and shows output. */
static gboolean
qa_custom_lookup (const char *id, gchar **title, gchar **command)
{
    GVariant *custom = g_settings_get_value (nemo_preferences, NEMO_QA_CUSTOM);
    GVariantIter iter;
    const char *item_id, *t, *icon, *kind, *cmd, *contexts;
    gboolean found = FALSE;
    g_variant_iter_init (&iter, custom);
    while (g_variant_iter_next (&iter, "(&s&s&s&s&s&s)",
                                &item_id, &t, &icon, &kind, &cmd, &contexts)) {
        if (g_strcmp0 (id, item_id) == 0) {
            if (title) *title = g_strdup (t);
            if (command) *command = g_strdup (cmd);
            found = TRUE;
            break;
        }
    }
    g_variant_unref (custom);
    return found;
}

static gboolean
qa_custom_can_run (DakkarQAContext *ctx)
{
    gchar *path = qa_selected_or_folder_path (ctx);
    gboolean ok = path != NULL;
    g_free (path);
    return ok;
}

static void
qa_custom_run (DakkarQAContext *ctx, const gchar *id)
{
    gchar *title = NULL, *command = NULL, **command_argv = NULL;
    gchar *target = qa_selected_or_folder_path (ctx);
    GFile *dir = qa_target_dir (ctx);
    gchar *dir_path = dir != NULL ? g_file_get_path (dir) : NULL;
    GError *error = NULL;
    GPtrArray *args;
    GtkTextBuffer *buffer;
    gboolean local = TRUE;
    gint i, argc = 0;

    if (!qa_custom_lookup (id, &title, &command)) goto out;
    if (target == NULL || dir_path == NULL) {
        qa_message (ctx, GTK_MESSAGE_WARNING, title,
                    _("Custom actions need a local file or folder."));
        goto out;
    }
    if (!g_shell_parse_argv (command, &argc, &command_argv, &error) || argc == 0) {
        qa_message (ctx, GTK_MESSAGE_ERROR, _("Invalid action command"),
                    error != NULL ? error->message : _("Missing command"));
        goto out;
    }
    args = g_ptr_array_new_with_free_func (g_free);
    for (i = 0; i < argc && local; i++) {
        if (g_strcmp0 (command_argv[i], "%f") == 0) {
            g_ptr_array_add (args, g_strdup (target));
        } else if (g_strcmp0 (command_argv[i], "%d") == 0) {
            g_ptr_array_add (args, g_strdup (dir_path));
        } else if (g_strcmp0 (command_argv[i], "%F") == 0) {
            GList *item;
            if (ctx->selection == NULL)
                g_ptr_array_add (args, g_strdup (target));
            for (item = ctx->selection; item != NULL && local; item = item->next) {
                GFile *gf = nemo_file_get_location (NEMO_FILE (item->data));
                gchar *p = gf != NULL ? g_file_get_path (gf) : NULL;
                if (p == NULL) local = FALSE;
                else g_ptr_array_add (args, p);
                g_clear_object (&gf);
            }
        } else {
            g_ptr_array_add (args, g_strdup (command_argv[i]));
        }
    }
    g_ptr_array_add (args, NULL);
    if (local) {
        qa_dialog (ctx, title, command, &buffer);
        qa_run_output (buffer, (const gchar * const *) args->pdata, dir_path);
    } else {
        qa_message (ctx, GTK_MESSAGE_WARNING, title,
                    _("All selected items must be local files."));
    }
    g_ptr_array_unref (args);
out:
    g_clear_error (&error);
    g_strfreev (command_argv);
    g_clear_object (&dir);
    g_free (dir_path);
    g_free (target);
    g_free (title);
    g_free (command);
}

static void
qa_compare_files (DakkarQAContext *ctx)
{
    GList *l = ctx->selection;
    GFile *first, *second;
    gchar *a, *b;
    const gchar *argv[] = {"diff", "-u", "--", NULL, NULL, NULL};
    GtkTextBuffer *buffer;
    if (l == NULL || l->next == NULL || l->next->next != NULL) {
        qa_message (ctx, GTK_MESSAGE_INFO, _("Compare Files"),
                    _("Select exactly two files to compare."));
        return;
    }
    first = nemo_file_get_location (NEMO_FILE (l->data));
    second = nemo_file_get_location (NEMO_FILE (l->next->data));
    a = first != NULL ? g_file_get_path (first) : NULL;
    b = second != NULL ? g_file_get_path (second) : NULL;
    g_clear_object (&first);
    g_clear_object (&second);
    if (a != NULL && b != NULL) {
        argv[3] = a;
        argv[4] = b;
        qa_dialog (ctx, _("Compare Files"),
                   _("Unified diff of the two selected files"), &buffer);
        qa_run_output (buffer, argv, NULL);
    }
    g_free (a);
    g_free (b);
}

static void
qa_open_with (DakkarQAContext *ctx)
{
    NemoFile *selected = qa_single (ctx);
    gchar *mime;
    GFile *file;
    GtkWidget *chooser;
    if (selected == NULL || nemo_file_is_directory (selected)) return;
    mime = nemo_file_get_mime_type (selected);
    file = nemo_file_get_location (selected);
    if (mime != NULL && file != NULL) {
        chooser = gtk_app_chooser_dialog_new_for_content_type (qa_parent (ctx),
            GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, mime);
        gtk_window_set_title (GTK_WINDOW (chooser), _("Open With…"));
        if (gtk_dialog_run (GTK_DIALOG (chooser)) == GTK_RESPONSE_OK) {
            GAppInfo *app = gtk_app_chooser_get_app_info (GTK_APP_CHOOSER (chooser));
            if (app != NULL) {
                GList *files = g_list_prepend (NULL, file);
                GError *error = NULL;
                if (!g_app_info_launch (app, files, NULL, &error)) {
                    qa_message (ctx, GTK_MESSAGE_ERROR, _("Open With…"),
                                error != NULL ? error->message : _("Unable to open file"));
                    g_clear_error (&error);
                }
                g_list_free (files);
                g_object_unref (app);
            }
        }
        gtk_widget_destroy (chooser);
    }
    g_clear_object (&file);
    g_free (mime);
}

static gboolean qa_has_program (const char *name);

static void
qa_info_dialog (DakkarQAContext *ctx, const char *id)
{
    NemoFile *file = qa_single (ctx) != NULL ? qa_single (ctx) :
                     (ctx->selection == NULL ? ctx->folder : NULL);
    GString *result;
    gchar *path;
    if (file == NULL) return;
    path = NULL;
    {
        GFile *location = nemo_file_get_location (file);
        path = location != NULL ? g_file_get_path (location) : NULL;
        g_clear_object (&location);
    }
    if (g_strcmp0 (id, "advanced-info") == 0 && path != NULL &&
        qa_has_program ("stat")) {
        GtkTextBuffer *buffer;
        const gchar *argv[] = {"stat", "--", path, NULL};
        qa_dialog (ctx, _("Advanced File Information"), path, &buffer);
        qa_run_output (buffer, argv, NULL);
        g_free (path);
        return;
    }
    result = g_string_new (NULL);
    {
        gchar *name = nemo_file_get_display_name (file);
        gchar *uri = nemo_file_get_uri (file);
        gchar *mime = nemo_file_get_mime_type (file);
        goffset size = nemo_file_get_size (file);
        gchar *size_text = size >= 0 && !nemo_file_is_directory (file) ?
                           g_format_size ((guint64) size) : NULL;
        if (g_strcmp0 (id, "inspect-symlink") == 0) {
            gchar *target = nemo_file_is_symbolic_link (file) ?
                            nemo_file_get_symbolic_link_target_path (file) : NULL;
            g_string_append_printf (result, "%s: %s\n%s: %s", _("Name"), name ? name : "",
                                    _("Symbolic link target"), target ? target : _("Unknown"));
            g_free (target);
        } else {
            g_string_append_printf (result, "%s: %s\n%s: %s\nMIME: %s\n%s: %s",
                                    _("Name"), name ? name : "",
                                    _("Location"), path ? path : (uri ? uri : ""),
                                    mime ? mime : "", _("Size"),
                                    size_text ? size_text : "—");
        }
        qa_message (ctx, GTK_MESSAGE_INFO,
                    g_strcmp0 (id, "inspect-symlink") == 0 ?
                    _("Symbolic Link Information") : _("File Information"),
                    result->str);
        g_free (name);
        g_free (uri);
        g_free (mime);
        g_free (size_text);
    }
    g_string_free (result, TRUE);
    g_free (path);
}

static gboolean
qa_copy_text (DakkarQAContext *ctx, const char *id)
{
    GList *items = ctx->selection, *l, *owned = NULL;
    GFile *base = NULL;
    GString *result = g_string_new (NULL);
    gboolean ok;
    if (items == NULL && ctx->folder != NULL) {
        owned = g_list_prepend (NULL, nemo_file_ref (ctx->folder));
        items = owned;
    }
    if (g_strcmp0 (id, "copy-relative-path") == 0 && ctx->folder != NULL)
        base = nemo_file_get_location (ctx->folder);
    for (l = items; l != NULL; l = l->next) {
        NemoFile *file = NEMO_FILE (l->data);
        GFile *location = nemo_file_get_location (file);
        gchar *value = NULL;
        if (g_strcmp0 (id, "copy-filename") == 0) {
            value = nemo_file_get_name (file);
        } else if (g_strcmp0 (id, "copy-relative-path") == 0) {
            value = base != NULL && location != NULL ?
                    g_file_get_relative_path (base, location) : NULL;
            if (value == NULL) value = nemo_file_get_name (file);
        } else if (location != NULL) {
            value = g_file_get_path (location);
            if (value == NULL) value = g_file_get_uri (location);
        }
        if (value != NULL) {
            if (result->len > 0) g_string_append_c (result, '\n');
            g_string_append (result, value);
            g_free (value);
        }
        g_clear_object (&location);
    }
    ok = result->len > 0;
    if (ok)
        gtk_clipboard_set_text (gtk_clipboard_get (GDK_SELECTION_CLIPBOARD),
                                result->str, -1);
    g_string_free (result, TRUE);
    g_clear_object (&base);
    nemo_file_list_free (owned);
    return ok;
}

static void
qa_archive_launch (DakkarQAContext *ctx, const char *id)
{
    NemoFile *file = qa_single (ctx);
    GFile *archive;
    GError *error = NULL;
    if (file == NULL || nemo_file_is_directory (file)) return;
    archive = nemo_file_get_location (file);
    if (archive != NULL &&
        !nemo_qa_archive_launch (qa_parent (ctx), id, archive, &error))
        qa_message (ctx, GTK_MESSAGE_ERROR, _("Archive Manager"),
                    error != NULL ? error->message : _("Unable to open archive manager"));
    g_clear_error (&error);
    g_clear_object (&archive);
}

/* ------------------------------------------------------------------ */
/* availability                                                        */
/* ------------------------------------------------------------------ */

static gboolean
qa_has_program (const char *name)
{
    gchar *found = g_find_program_in_path (name);
    gboolean ok = found != NULL;
    g_free (found);
    return ok;
}

/* Is this action runnable now? why receives a short reason if not. */
static gboolean
qa_executable (DakkarQAContext *ctx, const NemoQAAction *action, unsigned context,
               const char **why)
{
    const char *id = action->id;
    *why = NULL;
    if ((action->contexts & context) == 0) {
        *why = _("Not applicable to the current selection");
        return FALSE;
    }
    if (action->safety == NEMO_QA_NOT_READY) {
        *why = _("Not implemented yet");
        return FALSE;
    }
    if (qa_native_name (id) != NULL) {
        GtkAction *native = qa_native_action (ctx->view, id);
        if (native == NULL || !gtk_action_is_sensitive (native)) {
            *why = _("Unavailable for the current selection or permissions");
            return FALSE;
        }
        return TRUE;
    }
    if ((g_str_equal (id, "extract-here") || g_str_equal (id, "extract-to")) &&
        nemo_compress_available ()) {
        GList *files = qa_locations (ctx);
        gboolean ok = nemo_compress_can_extract (files);
        g_list_free_full (files, g_object_unref);
        if (!ok) *why = _("Select one or more local archives");
        return ok;
    }
    if (g_str_equal (id, "extract-here") || g_str_equal (id, "extract-to") ||
        g_str_equal (id, "archive-contents")) {
        if (!nemo_qa_archive_handler_available (id)) {
            *why = _("Install Ark, File Roller or Engrampa to open archives");
            return FALSE;
        }
        return qa_single_regular_file (ctx);
    }
    if (g_str_equal (id, "checksum")) {
        gchar *path = qa_selected_path (ctx);
        gboolean ok = path != NULL && qa_single_regular_file (ctx) && qa_has_program ("sha256sum");
        g_free (path);
        if (!ok) *why = _("Needs one local file and sha256sum");
        return ok;
    }
    if (g_str_equal (id, "inspect-media")) {
        gchar *path = qa_selected_path (ctx);
        gboolean ok = path != NULL && qa_has_program ("ffprobe");
        g_free (path);
        if (!ok) *why = _("Needs one local media file and ffprobe");
        return ok;
    }
    if (g_str_equal (id, "compress") && nemo_compress_available ()) {
        /* single and multiple selections, files and folders mixed */
        GList *files = qa_locations (ctx);
        gboolean ok = nemo_compress_can_compress (files);
        g_list_free_full (files, g_object_unref);
        if (!ok) *why = _("Select local files or folders first");
        return ok;
    }
    if (g_str_equal (id, "compress")) {
        gboolean ok = ctx->selection != NULL &&
            (qa_has_program ("zip") || qa_has_program ("tar") || qa_has_program ("7z"));
        if (!ok) *why = _("Select items first; needs zip, tar or 7z");
        return ok;
    }
    if (g_str_equal (id, "git")) {
        gchar *root = qa_git_root (ctx);
        gboolean ok = root != NULL && qa_has_program ("git");
        g_free (root);
        if (!ok) *why = _("No Git repository here");
        return ok;
    }
    if (g_str_equal (id, "compare-files")) {
        gboolean ok = ctx->selection != NULL && ctx->selection->next != NULL &&
                      ctx->selection->next->next == NULL && qa_has_program ("diff");
        if (!ok) *why = _("Select exactly two local files");
        return ok;
    }
    if (g_str_equal (id, "new-folder")) {
        GFile *dir = qa_action_dir (ctx, NULL);
        gboolean ok = dir != NULL;
        if (ok && g_file_is_native (dir)) {
            GFileInfo *info = g_file_query_info (dir, G_FILE_ATTRIBUTE_ACCESS_CAN_WRITE,
                                                 G_FILE_QUERY_INFO_NONE, NULL, NULL);
            ok = info == NULL ||
                 g_file_info_get_attribute_boolean (info, G_FILE_ATTRIBUTE_ACCESS_CAN_WRITE);
            g_clear_object (&info);
        } else if (ok) {
            GtkAction *native = NULL;
            GtkUIManager *ui = nemo_view_get_ui_manager (ctx->view);
            GList *l;
            for (l = ui != NULL ? gtk_ui_manager_get_action_groups (ui) : NULL;
                 l != NULL && native == NULL; l = l->next)
                native = gtk_action_group_get_action (GTK_ACTION_GROUP (l->data),
                                                      NEMO_ACTION_NEW_FOLDER);
            ok = native != NULL && gtk_action_is_sensitive (native);
        }
        g_clear_object (&dir);
        if (!ok) *why = dir == NULL && ctx->selection != NULL ?
                        _("The selected items are in different folders") :
                        _("You cannot create a folder here");
        return ok;
    }
    if (g_str_equal (id, "terminal")) {
        /* no terminal installed is reported by the launcher in a dialog */
        gchar *path = qa_terminal_dir (ctx);
        gboolean ok = path != NULL;
        g_free (path);
        if (!ok) *why = ctx->selection != NULL && ctx->selection->next != NULL ?
                        _("The selected items are in different folders") :
                        _("Needs a local folder");
        return ok;
    }
    if (g_str_equal (id, "root-terminal")) {
        gchar *path = qa_terminal_dir (ctx);
        gboolean ok = path != NULL && qa_has_program ("pkexec");
        g_free (path);
        if (!ok) *why = _("Needs a local folder and pkexec");
        return ok;
    }
    if (g_str_equal (id, "open-with"))
        return qa_single_regular_file (ctx);
    if (g_str_equal (id, "inspect-symlink")) {
        NemoFile *file = qa_single (ctx);
        gboolean ok = file != NULL && nemo_file_is_symbolic_link (file);
        if (!ok) *why = _("The selected item is not a symbolic link");
        return ok;
    }
    if (g_str_equal (id, "copy-path") || g_str_equal (id, "copy-filename") ||
        g_str_equal (id, "copy-relative-path") ||
        g_str_equal (id, "file-information") || g_str_equal (id, "advanced-info"))
        return ctx->selection != NULL || ctx->folder != NULL;
    *why = _("Not implemented yet");
    return FALSE;
}

/* ------------------------------------------------------------------ */
/* dispatch                                                            */
/* ------------------------------------------------------------------ */

gboolean
dakkar_qa_dispatch (DakkarQAContext *ctx, const char *id)
{
    const NemoQAAction *action = nemo_quick_actions_find (id);
    const char *why = NULL;

    if (action == NULL) {
        qa_custom_run (ctx, id);
        return FALSE;
    }
    if (!qa_executable (ctx, action, dakkar_qa_context_flags (ctx), &why)) {
        if (why != NULL)
            qa_message (ctx, GTK_MESSAGE_INFO, _(action->label), why);
        return FALSE;
    }
    if (qa_native_name (id) != NULL) {
        GtkAction *native = qa_native_action (ctx->view, id);
        if (native != NULL && gtk_action_is_sensitive (native))
            gtk_action_activate (native);
        return FALSE;
    }
    if (g_str_equal (id, "new-folder")) {
        /* create it where the label says: in the selected folder ("Here")
         * or in the enclosing folder of the selected file(s).  When that
         * is the folder on screen use the normal path (rename in place). */
        GFile *dir = qa_action_dir (ctx, NULL);
        gchar *uri = dir != NULL ? g_file_get_uri (dir) : NULL;
        gchar *backing = nemo_view_get_backing_uri (ctx->view);
        if (uri != NULL && g_strcmp0 (uri, backing) == 0)
            nemo_view_new_folder (ctx->view);
        else if (uri != NULL)
            nemo_view_new_folder_at_uri (ctx->view, uri);
        g_free (backing);
        g_free (uri);
        g_clear_object (&dir);
        return FALSE;
    }
    if (g_str_equal (id, "terminal") || g_str_equal (id, "root-terminal")) {
        /* straight to the shared launcher with the quick-action target, not
         * a GtkAction looked up by name: that could find an insensitive
         * OpenInTerminal and silently do nothing */
        gchar *path = qa_terminal_dir (ctx);
        if (path != NULL)
            nemo_view_open_terminal_at_path (ctx->view, path,
                                             g_str_equal (id, "root-terminal"));
        g_free (path);
    } else if (g_str_equal (id, "compress") && nemo_compress_available ()) {
        GList *files = qa_locations (ctx);
        nemo_compress_dialog_show (qa_parent (ctx), files);
        g_list_free_full (files, g_object_unref);
    } else if (g_str_equal (id, "extract-here") && nemo_compress_available ()) {
        GList *files = qa_locations (ctx);
        nemo_compress_extract_here (qa_parent (ctx), files);
        g_list_free_full (files, g_object_unref);
    } else if (g_str_equal (id, "extract-to") && nemo_compress_available ()) {
        GList *files = qa_locations (ctx);
        nemo_compress_extract_to (qa_parent (ctx), files);
        g_list_free_full (files, g_object_unref);
    } else if (g_str_equal (id, "compress")) {
        qa_compress_dialog (ctx);
    } else if (g_str_equal (id, "git")) {
        qa_git_dialog (ctx);
    } else if (g_str_equal (id, "archive-contents") ||
               g_str_equal (id, "extract-here") || g_str_equal (id, "extract-to")) {
        /* Launch is not completion: no success checkmark yet. */
        qa_archive_launch (ctx, id);
    } else if (g_str_equal (id, "compare-files")) {
        qa_compare_files (ctx);
    } else if (g_str_equal (id, "open-with")) {
        qa_open_with (ctx);
    } else if (g_str_equal (id, "copy-path") || g_str_equal (id, "copy-filename") ||
               g_str_equal (id, "copy-relative-path")) {
        return qa_copy_text (ctx, id);
    } else if (g_str_equal (id, "checksum") || g_str_equal (id, "inspect-media")) {
        gchar *path = qa_selected_path (ctx);
        GtkTextBuffer *buffer;
        const gchar *argv_checksum[] = {"sha256sum", "--", path, NULL};
        const gchar *argv_media[] = {"ffprobe", "-v", "error", "-show_format",
                                     "-show_streams", "--", path, NULL};
        if (path != NULL) {
            qa_dialog (ctx, _(action->label), path, &buffer);
            qa_run_output (buffer, g_str_equal (id, "checksum") ?
                           argv_checksum : argv_media, NULL);
        }
        g_free (path);
    } else if (g_str_equal (id, "file-information") ||
               g_str_equal (id, "advanced-info") ||
               g_str_equal (id, "inspect-symlink")) {
        qa_info_dialog (ctx, id);
    }
    return FALSE;
}

/* ------------------------------------------------------------------ */
/* panel                                                               */
/* ------------------------------------------------------------------ */

static const gchar *
qa_subtitle (DakkarQAContext *ctx, const gchar *id)
{
    if (g_str_equal (id, "new-folder") ||
        g_str_equal (id, "terminal") || g_str_equal (id, "root-terminal")) {
        /* the title already says Here / in Enclosing Folder; name the
         * folder itself (same target rule as the title) */
        gboolean enclosing = FALSE;
        GFile *dir = qa_action_dir (ctx, &enclosing);
        gchar *name = dir != NULL ? g_file_get_basename (dir) : NULL;
        const gchar *result = dir == NULL ? _("Items in different folders") :
                              enclosing ? _("in Enclosing Folder") : _("Here");
        if (name != NULL) {
            g_free (ctx->terminal_subtitle);
            ctx->terminal_subtitle = name;
            result = ctx->terminal_subtitle;
        }
        g_clear_object (&dir);
        return result;
    }
    if (g_str_equal (id, "copy-path")) return _("Absolute path");
    if (g_str_equal (id, "copy-filename")) return _("File name only");
    if (g_str_equal (id, "copy-relative-path")) return _("Relative to this folder");
    if (g_str_equal (id, "git")) return _("Repository tools");
    if (g_str_equal (id, "compress")) return _("Selected items");
    if (g_str_equal (id, "checksum")) return _("SHA-256");
    if (ctx->selection == NULL) return _("This folder");
    return ctx->selection->next != NULL ? _("Selected items") : _("Selected item");
}

static void
qa_button_clicked (GtkButton *button, gpointer data)
{
    NemoView *view = g_object_get_data (G_OBJECT (button), "dakkar-qa-view");
    const gchar *id = g_object_get_data (G_OBJECT (button), "quick-action-id");
    DakkarQAContext *ctx;
    (void) data;
    if (id == NULL || view == NULL) return;
    ctx = dakkar_qa_context_new (GTK_WIDGET (button), view);
    if (dakkar_qa_dispatch (ctx, id))
        nemo_qa_button_feedback (button, TRUE);
    dakkar_qa_context_free (ctx);
}

static void
qa_add_button (DakkarQAContext *ctx, GtkWidget *container, const gchar *id,
               const gchar *label, const gchar *icon, const gchar *subtitle,
               gboolean sensitive, const gchar *layout, const gchar *tooltip,
               gint panel_width)
{
    GtkWidget *button, *icon_widget = NULL, *title_label = NULL, *subtitle_label = NULL;
    const gchar *icon_name = icon != NULL && *icon ? dakkar_icon_resolve (icon) : "system-run-symbolic";
    if (g_str_equal (layout, "icons")) {
        button = gtk_button_new ();
        icon_widget = gtk_image_new_from_icon_name (icon_name, GTK_ICON_SIZE_BUTTON);
        gtk_container_add (GTK_CONTAINER (button), icon_widget);
        gtk_widget_set_size_request (button, 48, 42);
        gtk_widget_set_halign (button, GTK_ALIGN_CENTER);
    } else {
        GtkWidget *row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 10);
        GtkWidget *labels = gtk_box_new (GTK_ORIENTATION_VERTICAL, 1);
        button = gtk_button_new ();
        icon_widget = gtk_image_new_from_icon_name (icon_name, GTK_ICON_SIZE_BUTTON);
        title_label = gtk_label_new (label);
        subtitle_label = gtk_label_new (subtitle != NULL ? subtitle : "");
        gtk_label_set_xalign (GTK_LABEL (title_label), 0.0);
        gtk_label_set_xalign (GTK_LABEL (subtitle_label), 0.0);
        gtk_label_set_ellipsize (GTK_LABEL (title_label), PANGO_ELLIPSIZE_END);
        /* long titles ("Open Terminal as Root in Enclosing Folder") wrap
         * onto a second line instead of being cut off, and never widen
         * the button beyond the width every other button gets */
        gtk_label_set_line_wrap (GTK_LABEL (title_label), TRUE);
        gtk_label_set_line_wrap_mode (GTK_LABEL (title_label), PANGO_WRAP_WORD_CHAR);
        gtk_label_set_lines (GTK_LABEL (title_label), 2);
        gtk_label_set_max_width_chars (GTK_LABEL (title_label), 1);
        gtk_label_set_max_width_chars (GTK_LABEL (subtitle_label), 1);
        gtk_label_set_ellipsize (GTK_LABEL (subtitle_label), PANGO_ELLIPSIZE_END);
        gtk_style_context_add_class (gtk_widget_get_style_context (title_label),
                                     "miller-action-title");
        gtk_style_context_add_class (gtk_widget_get_style_context (subtitle_label),
                                     "miller-action-subtitle");
        gtk_style_context_add_class (gtk_widget_get_style_context (button),
                                     "miller-action-button");
        gtk_widget_set_margin_start (row, 6);
        gtk_widget_set_margin_end (row, 6);
        gtk_widget_set_margin_top (row, 4);
        gtk_widget_set_margin_bottom (row, 4);
        gtk_box_pack_start (GTK_BOX (row), icon_widget, FALSE, FALSE, 0);
        gtk_box_pack_start (GTK_BOX (labels), title_label, FALSE, FALSE, 0);
        gtk_box_pack_start (GTK_BOX (labels), subtitle_label, FALSE, FALSE, 0);
        gtk_box_pack_start (GTK_BOX (row), labels, TRUE, TRUE, 0);
        gtk_container_add (GTK_CONTAINER (button), row);
    }
    gtk_style_context_add_class (gtk_widget_get_style_context (button), "dakkar-qa-compact");
    nemo_qa_button_register (button, icon_widget, title_label, subtitle_label,
                             g_str_equal (layout, "icons"));
    g_object_set_data (G_OBJECT (button), "dakkar-qa-compact",
                       GINT_TO_POINTER (g_str_equal (layout, "compact")));
    gtk_widget_set_tooltip_text (button, tooltip != NULL ? tooltip : label);
    gtk_widget_set_sensitive (button, sensitive);
    g_object_set_data_full (G_OBJECT (button), "quick-action-id", g_strdup (id), g_free);
    g_object_set_data_full (G_OBJECT (button), "dakkar-qa-view",
                            g_object_ref (ctx->view), g_object_unref);
    g_signal_connect (button, "clicked", G_CALLBACK (qa_button_clicked), NULL);
    gtk_box_pack_start (GTK_BOX (container), button, FALSE, FALSE, 0);
    gtk_widget_show_all (button);
    if (subtitle_label != NULL && g_str_equal (layout, "compact"))
        gtk_widget_hide (subtitle_label);
    (void) panel_width;
}

static gboolean
qa_strv_contains (gchar **strv, const gchar *id)
{
    guint i;
    for (i = 0; strv != NULL && strv[i] != NULL; i++)
        if (g_strcmp0 (strv[i], id) == 0) return TRUE;
    return FALSE;
}

void
dakkar_qa_populate (GtkWidget *container, GtkWidget *anchor, NemoView *view,
                   gint panel_width)
{
    DakkarQAContext *ctx;
    const NemoQAAction *catalog;
    GList *children, *l;
    gchar **pins, **manual, *mode, *hide, *layout;
    GHashTable *shown;
    GVariant *custom;
    gsize i, n;
    guint limit, suggested = 0, displayed = 0;
    unsigned context;
    gint phase;

    g_return_if_fail (GTK_IS_BOX (container));
    children = gtk_container_get_children (GTK_CONTAINER (container));
    for (l = children; l != NULL; l = l->next) gtk_widget_destroy (GTK_WIDGET (l->data));
    g_list_free (children);
    if (view == NULL) return;

    ctx = dakkar_qa_context_new (anchor, view);
    context = dakkar_qa_context_flags (ctx);
    mode = g_settings_get_string (nemo_preferences, NEMO_QA_MODE);
    hide = g_settings_get_string (nemo_preferences, NEMO_QA_UNAVAILABLE);
    layout = g_settings_get_string (nemo_preferences, NEMO_QA_LAYOUT);
    pins = g_settings_get_strv (nemo_preferences, NEMO_QA_PINNED);
    manual = g_settings_get_strv (nemo_preferences, NEMO_QA_MANUAL);
    limit = MAX (0, g_settings_get_int (nemo_preferences, NEMO_QA_LIMIT));
    custom = g_settings_get_value (nemo_preferences, NEMO_QA_CUSTOM);
    shown = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
    catalog = nemo_quick_actions_catalog (&n);

    /* Phase 0: pinned, 1: explicitly shown, 2: automatic suggestions. */
    for (phase = 0; phase < 3; phase++) {
        gchar **wanted = phase == 0 ? pins : manual;
        gboolean list_phase = phase != 2;
        GVariantIter iter;
        const char *cid, *cname, *cicon, *ckind, *ccmd, *ccontexts;
        if (phase == 2 && g_str_equal (mode, "manual")) break;
        for (i = 0; list_phase ? wanted[i] != NULL : i < n; i++) {
            const NemoQAAction *action = list_phase ?
                nemo_quick_actions_find (wanted[i]) : &catalog[i];
            const char *why = NULL;
            gboolean executable;
            if (action == NULL || g_hash_table_contains (shown, action->id)) continue;
            if ((action->contexts & context) == 0) continue;
            /* Open With is only meaningful for one regular file. */
            if (g_str_equal (action->id, "open-with") && !qa_single_regular_file (ctx))
                continue;
            if (g_str_equal (action->id, "git") && !(context & NEMO_QA_GIT)) continue;
            if (phase == 2 && (!action->recommended ||
                               action->safety == NEMO_QA_NOT_READY ||
                               (limit > 0 && suggested >= limit))) continue;
            executable = qa_executable (ctx, action, context, &why);
            if (!executable && phase == 2 && g_str_equal (hide, "hide")) continue;
            qa_add_button (ctx, container, action->id, qa_title (ctx, action), action->icon,
                           qa_subtitle (ctx, action->id), executable, layout,
                           executable ? NULL : why, panel_width);
            g_hash_table_add (shown, g_strdup (action->id));
            displayed++;
            if (phase == 2) suggested++;
        }
        /* Custom actions in the same phase, so pinned ones come first. */
        g_variant_iter_init (&iter, custom);
        while (g_variant_iter_next (&iter, "(&s&s&s&s&s&s)",
                                    &cid, &cname, &cicon, &ckind, &ccmd, &ccontexts)) {
            gboolean pinned = qa_strv_contains (pins, cid);
            gboolean selected = qa_strv_contains (manual, cid);
            gboolean applies = nemo_qa_custom_context_matches (ccontexts, context);
            gboolean show = phase == 0 ? pinned :
                            phase == 1 ? (selected && !pinned) : (!pinned && !selected);
            gboolean can_run = applies && qa_custom_can_run (ctx);
            if (!show || !applies || g_hash_table_contains (shown, cid)) continue;
            if (phase == 2 && !can_run && g_str_equal (hide, "hide")) continue;
            qa_add_button (ctx, container, cid, cname, cicon, _("Custom action"),
                           can_run, layout,
                           can_run ? ccmd : _("Custom actions need a local file or folder"),
                           panel_width);
            g_hash_table_add (shown, g_strdup (cid));
            displayed++;
        }
    }
    if (displayed == 0) {
        GtkWidget *label = gtk_label_new (_("No applicable Quick Actions"));
        gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
        gtk_box_pack_start (GTK_BOX (container), label, FALSE, FALSE, 10);
        gtk_widget_show (label);
    }
    nemo_qa_adapt_buttons (container, panel_width);
    g_hash_table_unref (shown);
    g_variant_unref (custom);
    g_strfreev (pins);
    g_strfreev (manual);
    g_free (mode);
    g_free (hide);
    g_free (layout);
    dakkar_qa_context_free (ctx);
}
