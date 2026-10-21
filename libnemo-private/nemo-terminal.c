/* nemo-terminal.c - the one terminal launcher shared by every
 * "Open Terminal" / "Open Terminal as Root" entry point (context menus,
 * File menu, toolbar and quick actions, in every view).
 *
 * Copyright (C) 2026 Dakar contributors
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * Opens a terminal in a folder.  The terminal is resolved once and spawned
 * directly (no "sh -c 'cd DIR && exec ...'") with the folder as working
 * directory *and* with its own working-directory option: terminals that
 * hand the request to an already running instance (Konsole in
 * single-process mode, gnome-terminal-server, ...) ignore the working
 * directory of the launcher process.  Every failure is shown to the user.
 */

#include <config.h>

#include "nemo-terminal.h"
#include "nemo-global-preferences.h"

#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <glib/gi18n.h>
#include <gio/gio.h>

typedef enum {
    WORKDIR_NONE,       /* only the process working directory */
    WORKDIR_SEPARATE,   /* FLAG DIR */
    WORKDIR_JOINED      /* FLAGDIR (flag ends in '=') */
} WorkdirStyle;

typedef enum {
    EXEC_REST,          /* FLAG cmd arg arg ...  (FLAG may be NULL) */
    EXEC_STRING         /* FLAG "cmd arg arg"    (one shell string) */
} ExecStyle;

typedef struct {
    const gchar *name;
    const gchar *prefix;        /* extra first argument, e.g. wezterm "start" */
    WorkdirStyle workdir_style;
    const gchar *workdir_flag;
    const gchar *exec_flag;
    ExecStyle exec_style;
} KnownTerminal;

static const KnownTerminal known_terminals[] = {
    { "xdg-terminal-exec",   NULL,           WORKDIR_JOINED,   "--dir=",               "--", EXEC_REST },
    { "x-terminal-emulator", NULL,           WORKDIR_NONE,     NULL,                   "-e", EXEC_REST },
    { "konsole",             NULL,           WORKDIR_SEPARATE, "--workdir",            "-e", EXEC_REST },
    { "gnome-terminal",      NULL,           WORKDIR_JOINED,   "--working-directory=", "--", EXEC_REST },
    { "kgx",                 NULL,           WORKDIR_JOINED,   "--working-directory=", "-e", EXEC_REST },
    { "xfce4-terminal",      NULL,           WORKDIR_JOINED,   "--working-directory=", "-x", EXEC_REST },
    { "mate-terminal",       NULL,           WORKDIR_JOINED,   "--working-directory=", "-x", EXEC_REST },
    { "tilix",               NULL,           WORKDIR_JOINED,   "--working-directory=", "-e", EXEC_STRING },
    { "alacritty",           NULL,           WORKDIR_SEPARATE, "--working-directory",  "-e", EXEC_REST },
    { "kitty",               NULL,           WORKDIR_SEPARATE, "--directory",          NULL, EXEC_REST },
    { "ptyxis",              "--new-window", WORKDIR_JOINED,   "--working-directory=", "--", EXEC_REST },
    { "foot",                NULL,           WORKDIR_JOINED,   "--working-directory=", NULL, EXEC_REST },
    { "wezterm",             "start",        WORKDIR_SEPARATE, "--cwd",                "--", EXEC_REST },
    { "terminator",          NULL,           WORKDIR_JOINED,   "--working-directory=", "-x", EXEC_REST },
    { "lxterminal",          NULL,           WORKDIR_JOINED,   "--working-directory=", "-e", EXEC_STRING },
    { "qterminal",           NULL,           WORKDIR_SEPARATE, "--workdir",            "-e", EXEC_STRING },
    { "terminology",         NULL,           WORKDIR_SEPARATE, "-d",                   "-e", EXEC_STRING },
    { "blackbox",            NULL,           WORKDIR_SEPARATE, "-w",                   "-c", EXEC_STRING },
    { "blackbox-terminal",   NULL,           WORKDIR_SEPARATE, "-w",                   "-c", EXEC_STRING },
    { "urxvt",               NULL,           WORKDIR_SEPARATE, "-cd",                  "-e", EXEC_REST },
    { "xterm",               NULL,           WORKDIR_NONE,     NULL,                   "-e", EXEC_REST },
};

/* Tried in this order when the configured terminal is not installed. */
static const gchar *fallback_order[] = {
    "xdg-terminal-exec", "x-terminal-emulator", "konsole", "gnome-terminal",
    "kgx", "xfce4-terminal", "mate-terminal", "tilix", "alacritty", "kitty",
    "ptyxis", "foot", "wezterm", "terminator", "lxterminal", "qterminal",
    "terminology", "urxvt", "xterm",
};

const gchar *
nemo_terminal_get_label (gboolean as_root, gboolean enclosing_folder)
{
    if (as_root)
        return enclosing_folder ? _("Open Terminal as Root in Enclosing Folder")
                                : _("Open Terminal as Root Here");
    return enclosing_folder ? _("Open Terminal in Enclosing Folder")
                            : _("Open Terminal Here");
}

const gchar *
nemo_terminal_get_tooltip (gboolean as_root, gboolean enclosing_folder)
{
    if (as_root)
        return enclosing_folder ? _("Open a root terminal in the folder that contains the selected file")
                                : _("Open a root terminal in this folder");
    return enclosing_folder ? _("Open a terminal in the folder that contains the selected file")
                            : _("Open a terminal in this folder");
}

static const KnownTerminal *
lookup_known (const gchar *name)
{
    guint i;
    if (name == NULL) return NULL;
    for (i = 0; i < G_N_ELEMENTS (known_terminals); i++)
        if (g_strcmp0 (known_terminals[i].name, name) == 0)
            return &known_terminals[i];
    return NULL;
}

/* x-terminal-emulator (and other alternatives) are usually symlinks to a
 * real terminal: on Kubuntu x-terminal-emulator -> konsole.  Using the real
 * terminal's options lets us pass the folder explicitly.  Debian's
 * *.wrapper scripts only promise the generic -e contract, so they keep the
 * generic x-terminal-emulator syntax. */
static const KnownTerminal *
classify (const gchar *program, const gchar *full_path)
{
    const KnownTerminal *known = NULL;
    gchar *base = g_path_get_basename (program);

    if (full_path != NULL) {
        char *real = realpath (full_path, NULL);
        if (real != NULL) {
            gchar *real_base = g_path_get_basename (real);
            if (!g_str_has_suffix (real_base, ".wrapper") &&
                g_strcmp0 (real_base, "x-terminal-emulator") != 0)
                known = lookup_known (real_base);
            g_free (real_base);
            free (real);
        }
    }
    if (known == NULL)
        known = lookup_known (base);
    g_free (base);
    return known;
}

static gchar *
find_executable (const gchar *program)
{
    if (program == NULL || *program == '\0') return NULL;
    if (strchr (program, '/') != NULL)
        return g_file_test (program, G_FILE_TEST_IS_EXECUTABLE) &&
               !g_file_test (program, G_FILE_TEST_IS_DIR) ? g_strdup (program) : NULL;
    return g_find_program_in_path (program);
}

typedef struct {
    gchar **base_argv;           /* program + configured extra args */
    gchar *full_path;
    const KnownTerminal *known;
    gchar *exec_flag;            /* NULL: pass the command directly */
    ExecStyle exec_style;
} ResolvedTerminal;

static void
resolved_terminal_clear (ResolvedTerminal *t)
{
    g_strfreev (t->base_argv);
    g_free (t->full_path);
    g_free (t->exec_flag);
    memset (t, 0, sizeof *t);
}

static GSettings *
terminal_settings (void)
{
    if (gnome_terminal_preferences != NULL)
        return g_object_ref (gnome_terminal_preferences);
    return NULL;
}

static gboolean
resolve_terminal (ResolvedTerminal *t)
{
    GSettings *settings = terminal_settings ();
    gchar *configured = NULL;
    gchar **configured_argv = NULL;
    guint i;

    memset (t, 0, sizeof *t);
    if (settings != NULL)
        configured = g_settings_get_string (settings, GNOME_DESKTOP_TERMINAL_EXEC);

    if (configured != NULL && *g_strstrip (configured) != '\0' &&
        g_shell_parse_argv (configured, NULL, &configured_argv, NULL) &&
        configured_argv[0] != NULL) {
        gchar *path = find_executable (configured_argv[0]);
        if (path != NULL) {
            GVariant *user_arg = g_settings_get_user_value (settings,
                                                            GNOME_DESKTOP_TERMINAL_EXEC_ARG);
            t->base_argv = configured_argv;
            configured_argv = NULL;
            t->full_path = path;
            t->known = classify (t->base_argv[0], path);
            if (user_arg != NULL || t->known == NULL) {
                /* respect exec-arg when the user chose it, or when we know
                 * nothing better about this terminal */
                gchar *arg = g_settings_get_string (settings, GNOME_DESKTOP_TERMINAL_EXEC_ARG);
                g_strstrip (arg);
                if (*arg != '\0') {
                    t->exec_flag = arg;
                    t->exec_style = (t->known != NULL &&
                                     g_strcmp0 (t->known->exec_flag, arg) == 0) ?
                                    t->known->exec_style : EXEC_REST;
                } else {
                    g_free (arg);
                    t->exec_flag = NULL;
                    t->exec_style = EXEC_REST;
                }
            } else {
                t->exec_flag = g_strdup (t->known->exec_flag);
                t->exec_style = t->known->exec_style;
            }
            if (user_arg != NULL) g_variant_unref (user_arg);
        }
    }
    g_strfreev (configured_argv);
    g_free (configured);
    g_clear_object (&settings);

    for (i = 0; t->base_argv == NULL && i < G_N_ELEMENTS (fallback_order); i++) {
        gchar *path = find_executable (fallback_order[i]);
        if (path == NULL) continue;
        t->base_argv = g_new0 (gchar *, 2);
        t->base_argv[0] = g_strdup (fallback_order[i]);
        t->full_path = path;
        t->known = classify (fallback_order[i], path);
        t->exec_flag = g_strdup (t->known->exec_flag);
        t->exec_style = t->known->exec_style;
    }
    return t->base_argv != NULL;
}

gchar **
nemo_terminal_build_argv (const gchar *directory, gboolean as_root, GError **error)
{
    ResolvedTerminal t;
    GPtrArray *argv;
    gchar **p;

    if (!resolve_terminal (&t)) {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                             _("No terminal program was found. Install a terminal "
                               "such as Konsole, GNOME Terminal or xterm, or choose "
                               "one in the preferred applications settings."));
        return NULL;
    }

    argv = g_ptr_array_new ();
    for (p = t.base_argv; *p != NULL; p++)
        g_ptr_array_add (argv, g_strdup (*p));
    if (t.known != NULL && t.known->prefix != NULL)
        g_ptr_array_add (argv, g_strdup (t.known->prefix));
    if (t.known != NULL && directory != NULL) {
        switch (t.known->workdir_style) {
        case WORKDIR_SEPARATE:
            g_ptr_array_add (argv, g_strdup (t.known->workdir_flag));
            g_ptr_array_add (argv, g_strdup (directory));
            break;
        case WORKDIR_JOINED:
            g_ptr_array_add (argv, g_strconcat (t.known->workdir_flag, directory, NULL));
            break;
        case WORKDIR_NONE:
        default:
            break;
        }
    }

    if (as_root) {
        gchar *quoted = g_shell_quote (directory != NULL ? directory : "/");
        gchar *script = g_strdup_printf ("cd -- %s && exec /bin/bash", quoted);
        const gchar *cmd[] = { "pkexec", "/bin/bash", "-c", script, NULL };
        guint i;

        if (t.exec_style == EXEC_STRING && t.exec_flag != NULL) {
            GString *line = g_string_new (NULL);
            for (i = 0; cmd[i] != NULL; i++) {
                gchar *q = g_shell_quote (cmd[i]);
                if (i > 0) g_string_append_c (line, ' ');
                g_string_append (line, q);
                g_free (q);
            }
            g_ptr_array_add (argv, g_strdup (t.exec_flag));
            g_ptr_array_add (argv, g_string_free (line, FALSE));
        } else {
            if (t.exec_flag != NULL)
                g_ptr_array_add (argv, g_strdup (t.exec_flag));
            for (i = 0; cmd[i] != NULL; i++)
                g_ptr_array_add (argv, g_strdup (cmd[i]));
        }
        g_free (script);
        g_free (quoted);
    }

    g_ptr_array_add (argv, NULL);
    resolved_terminal_clear (&t);
    return (gchar **) g_ptr_array_free (argv, FALSE);
}

/* ------------------------------------------------------------------ */

typedef struct {
    GtkWindow *parent;          /* weak */
    gchar *directory;
    gchar *program;
    gboolean as_root;
    gint64 started;
} LaunchData;

static void
launch_data_free (LaunchData *data)
{
    if (data->parent != NULL)
        g_object_remove_weak_pointer (G_OBJECT (data->parent), (gpointer *) &data->parent);
    g_free (data->directory);
    g_free (data->program);
    g_free (data);
}

static LaunchData *
launch_data_new (GtkWindow *parent, const gchar *directory, gboolean as_root)
{
    LaunchData *data = g_new0 (LaunchData, 1);
    data->parent = parent;
    if (parent != NULL)
        g_object_add_weak_pointer (G_OBJECT (parent), (gpointer *) &data->parent);
    data->directory = g_strdup (directory);
    data->as_root = as_root;
    return data;
}

static void
show_error (GtkWindow *parent, gboolean as_root, const gchar *detail)
{
    GtkWidget *dialog;

    g_warning ("Could not open terminal: %s", detail);
    if (g_getenv ("NEMO_TERMINAL_NO_ERROR_DIALOG") != NULL)
        return;
    dialog = gtk_message_dialog_new (parent,
                                     GTK_DIALOG_DESTROY_WITH_PARENT,
                                     GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE,
                                     "%s", as_root ? _("Could not open a terminal as root")
                                                   : _("Could not open a terminal"));
    gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog), "%s", detail);
    gtk_window_set_title (GTK_WINDOW (dialog), _("Open Terminal"));
    g_signal_connect (dialog, "response", G_CALLBACK (gtk_widget_destroy), NULL);
    gtk_widget_show (dialog);
}

/* A terminal that dies with an error right away (for instance a
 * gnome-terminal client without a working server) would otherwise look
 * like "nothing happens". */
static void
child_exited (GPid pid, gint status, gpointer user_data)
{
    LaunchData *data = user_data;
    gint64 elapsed = g_get_monotonic_time () - data->started;
    GError *error = NULL;

#if GLIB_CHECK_VERSION (2, 70, 0)
    gboolean ok = g_spawn_check_wait_status (status, &error);
#else
    gboolean ok = g_spawn_check_exit_status (status, &error);
#endif
    if (elapsed < 3 * G_USEC_PER_SEC && !ok) {
        gchar *detail = g_strdup_printf (_("The terminal “%s” stopped right away (%s)."),
                                         data->program, error->message);
        show_error (data->parent, data->as_root, detail);
        g_free (detail);
    }
    g_clear_error (&error);
    g_spawn_close_pid (pid);
    launch_data_free (data);
}

/* Give the terminal a startup-notification id (X11) or an activation token
 * (Wayland) so the compositor raises and focuses the new window instead of
 * opening it behind the file manager. */
static gchar **
launch_environment (GtkWindow *parent, const gchar *program, GAppLaunchContext **context_out)
{
    gchar **envp = g_get_environ ();
    GdkDisplay *display = parent != NULL ? gtk_widget_get_display (GTK_WIDGET (parent))
                                         : gdk_display_get_default ();
    GAppInfo *info;
    GdkAppLaunchContext *context;
    gchar *id;

    *context_out = NULL;
    envp = g_environ_unsetenv (envp, "DESKTOP_STARTUP_ID");
    envp = g_environ_unsetenv (envp, "XDG_ACTIVATION_TOKEN");
    if (display == NULL)
        return envp;
    info = g_app_info_create_from_commandline (program, NULL,
                                               G_APP_INFO_CREATE_SUPPORTS_STARTUP_NOTIFICATION,
                                               NULL);
    if (info == NULL)
        return envp;
    context = gdk_display_get_app_launch_context (display);
    gdk_app_launch_context_set_timestamp (context, gtk_get_current_event_time ());
    id = g_app_launch_context_get_startup_notify_id (G_APP_LAUNCH_CONTEXT (context), info, NULL);
    if (id != NULL) {
        envp = g_environ_setenv (envp, "DESKTOP_STARTUP_ID", id, TRUE);
        envp = g_environ_setenv (envp, "XDG_ACTIVATION_TOKEN", id, TRUE);
        g_object_set_data_full (G_OBJECT (context), "nemo-terminal-startup-id", id, g_free);
        g_object_set_data_full (G_OBJECT (context), "nemo-terminal-app-info",
                                g_object_ref (info), g_object_unref);
        *context_out = G_APP_LAUNCH_CONTEXT (context);
    } else {
        g_object_unref (context);
    }
    g_object_unref (info);
    return envp;
}

static gboolean
spawn_now (LaunchData *data)
{
    GError *error = NULL;
    gchar **argv;
    gchar **envp;
    GAppLaunchContext *context = NULL;
    GPid pid;
    gchar *message = NULL;

    if (data->directory == NULL || !g_path_is_absolute (data->directory) ||
        !g_file_test (data->directory, G_FILE_TEST_IS_DIR)) {
        message = g_strdup_printf (_("The folder “%s” is not a local folder."),
                                   data->directory != NULL ? data->directory : "");
        show_error (data->parent, data->as_root, message);
        g_free (message);
        launch_data_free (data);
        return FALSE;
    }
    if (data->as_root) {
        gchar *pkexec = g_find_program_in_path ("pkexec");
        if (pkexec == NULL) {
            show_error (data->parent, TRUE,
                        _("pkexec was not found. Install polkit to open a terminal as root."));
            launch_data_free (data);
            return FALSE;
        }
        g_free (pkexec);
    }

    argv = nemo_terminal_build_argv (data->directory, data->as_root, &error);
    if (argv == NULL) {
        show_error (data->parent, data->as_root, error->message);
        g_clear_error (&error);
        launch_data_free (data);
        return FALSE;
    }

    data->program = g_strdup (argv[0]);
    envp = launch_environment (data->parent, argv[0], &context);
    g_message ("Opening %sterminal with %s", data->as_root ? "root " : "", argv[0]);

    if (!g_spawn_async (data->directory, argv, envp,
                        G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD,
                        NULL, NULL, &pid, &error)) {
        if (context != NULL)
            g_app_launch_context_launch_failed (context,
                g_object_get_data (G_OBJECT (context), "nemo-terminal-startup-id"));
        message = g_strdup_printf (_("Could not start “%s”: %s"), argv[0], error->message);
        show_error (data->parent, data->as_root, message);
        g_free (message);
        g_clear_error (&error);
        g_clear_object (&context);
        g_strfreev (envp);
        g_strfreev (argv);
        launch_data_free (data);
        return FALSE;
    }

    data->started = g_get_monotonic_time ();
    g_child_watch_add (pid, child_exited, data);   /* owns data now */
    g_clear_object (&context);
    g_strfreev (envp);
    g_strfreev (argv);
    return TRUE;
}

static gboolean
spawn_idle (gpointer user_data)
{
    spawn_now (user_data);
    return G_SOURCE_REMOVE;
}

gboolean
nemo_terminal_open (GtkWindow *parent, const gchar *directory, gboolean as_root, gboolean defer)
{
    LaunchData *data = launch_data_new (parent, directory, as_root);

    if (defer) {
        /* Never spawn while a (Miller) context menu is still being
         * dismissed: do it on a fresh main-loop turn; data owns copies. */
        g_idle_add_full (G_PRIORITY_DEFAULT_IDLE, spawn_idle, data, NULL);
        return TRUE;
    }
    return spawn_now (data);
}
