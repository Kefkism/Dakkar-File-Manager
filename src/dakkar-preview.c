/* Dakkar: shared preview helpers (see dakkar-preview.h).  GPL-2.0-or-later.
 * Formerly these lived only inside the Miller Columns view. */

#include <config.h>
#include <string.h>
#include <glib/gi18n.h>

#include "dakkar-preview.h"
#include "dakkar-media.h"

gchar *
dakkar_preview_format_date (NemoFile *file)
{
    time_t mtime;
    GDateTime *dt;
    gchar *text;
    if (file == NULL) return NULL;
    mtime = nemo_file_get_mtime (file);
    if (mtime <= 0) return NULL;
    dt = g_date_time_new_from_unix_local ((gint64) mtime);
    if (dt == NULL) return NULL;
    text = g_date_time_format (dt, "%Y-%m-%d %H:%M");
    g_date_time_unref (dt);
    return text;
}

void
dakkar_preview_multi_summary (GList *selection, gchar **title, gchar **size)
{
    GList *l;
    guint count = 0, folders = 0;
    guint64 bytes = 0;
    for (l = selection; l != NULL; l = l->next) {
        NemoFile *file = NEMO_FILE (l->data);
        count++;
        if (nemo_file_is_directory (file)) {
            folders++;
        } else {
            goffset s = nemo_file_get_size (file);
            if (s > 0) bytes += (guint64) s;
        }
    }
    if (title != NULL)
        *title = g_strdup_printf (ngettext ("%u item selected", "%u items selected", count),
                                  count);
    if (size != NULL) {
        gchar *formatted = g_format_size (bytes);
        if (folders > 0)
            *size = g_strdup_printf (ngettext ("%s (files only, %u folder not counted)",
                                               "%s (files only, %u folders not counted)",
                                               folders), formatted, folders);
        else
            *size = g_strdup (formatted);
        g_free (formatted);
    }
}

/* ------------------------------------------------------------------ */
/* video: the shared player of dakkar-media.c (muted at start, speaker   */
/* toggle, seek bar)                                                    */
/* ------------------------------------------------------------------ */

gboolean
dakkar_video_supported (void)
{
    return dakkar_media_supported ();
}

GtkWidget *
dakkar_video_new (gint width)
{
    return dakkar_player_new (DAKKAR_PLAYER_VIDEO, width);
}

void
dakkar_video_set_uri (GtkWidget *widget, const char *uri)
{
    dakkar_player_set_uri (widget, uri);
}

/* ------------------------------------------------------------------ */
/* Quick Look                                                          */
/* ------------------------------------------------------------------ */

static GtkWidget *quick_window = NULL;   /* one per process */

typedef struct {
    GtkWidget *window;     /* weak */
    GtkWidget *stack;
    GtkWidget *image;
    GtkWidget *text;
    GtkWidget *video;
    GtkWidget *audio;      /* audio player */
    GtkWidget *art;        /* album art / music icon */
    GtkWidget *grid;       /* media metadata */
    GtkWidget *caption;
    GCancellable *cancel;
    gint64 opened;         /* monotonic time the window opened */
    gboolean space_armed;  /* the Space press that opened it is over */
} QuickLook;

static void
quick_free (gpointer data)
{
    QuickLook *ql = data;
    if (ql->cancel != NULL) {
        g_cancellable_cancel (ql->cancel);
        g_object_unref (ql->cancel);
    }
    g_free (ql);
}

static void
quick_destroyed (GtkWidget *widget, gpointer data)
{
    (void) data;
    if (quick_window == widget) quick_window = NULL;
}

static gboolean
quick_key (GtkWidget *widget, GdkEventKey *event, gpointer data)
{
    QuickLook *ql = data;
    GtkWidget *player = ql->video != NULL ? ql->video : ql->audio;
    /* Key mapping in Quick Look:
     *   audio/video: Space = play/pause, M = sound on/off, Esc = close;
     *   everything else: Space or Esc = close (as before).
     * The Space press that opened Quick Look (and its auto-repeat while the
     * key is still held) never starts playback: Space only plays after it
     * was released once, or after a second. */
    if (event->keyval == GDK_KEY_space && player != NULL && dakkar_player_is_active (player)) {
        if (!ql->space_armed && g_get_monotonic_time () - ql->opened < G_USEC_PER_SEC)
            return TRUE;
        ql->space_armed = TRUE;
        dakkar_player_toggle (player);
        return TRUE;
    }
    if ((event->keyval == GDK_KEY_m || event->keyval == GDK_KEY_M) && player != NULL &&
        (event->state & (GDK_CONTROL_MASK | GDK_MOD1_MASK)) == 0) {
        dakkar_player_set_muted (player, !dakkar_player_get_muted (player));
        return TRUE;
    }
    if (event->keyval == GDK_KEY_space || event->keyval == GDK_KEY_Escape) {
        gtk_widget_destroy (widget);
        return TRUE;
    }
    return FALSE;
}

static gboolean
quick_key_release (GtkWidget *widget, GdkEventKey *event, gpointer data)
{
    QuickLook *ql = data;
    (void) widget;
    if (event->keyval == GDK_KEY_space) ql->space_armed = TRUE;
    return FALSE;
}

/* Async jobs hold a weak reference to the window. */
typedef struct {
    GWeakRef window;
    GCancellable *cancel;
} QuickJob;

static QuickJob *
quick_job_new (QuickLook *ql)
{
    QuickJob *job = g_new0 (QuickJob, 1);
    g_weak_ref_init (&job->window, ql->window);
    job->cancel = g_object_ref (ql->cancel);
    return job;
}

static QuickLook *
quick_job_target (QuickJob *job, GtkWidget **window)
{
    *window = g_weak_ref_get (&job->window);
    if (*window == NULL || g_cancellable_is_cancelled (job->cancel)) return NULL;
    return g_object_get_data (G_OBJECT (*window), "dakkar-quick-look");
}

static void
quick_job_free (QuickJob *job)
{
    g_weak_ref_clear (&job->window);
    g_object_unref (job->cancel);
    g_free (job);
}

static void
quick_image_ready (GObject *source, GAsyncResult *res, gpointer data)
{
    QuickJob *job = data;
    GtkWidget *window = NULL;
    GdkPixbuf *pixbuf = gdk_pixbuf_new_from_stream_finish (res, NULL);
    QuickLook *ql = quick_job_target (job, &window);
    (void) source;
    if (ql != NULL && pixbuf != NULL) {
        gtk_image_set_from_pixbuf (GTK_IMAGE (ql->image), pixbuf);
        gtk_stack_set_visible_child_name (GTK_STACK (ql->stack), "image");
    }
    g_clear_object (&pixbuf);
    g_clear_object (&window);
    quick_job_free (job);
}

static void
quick_image_opened (GObject *source, GAsyncResult *res, gpointer data)
{
    QuickJob *job = data;
    GFileInputStream *stream = g_file_read_finish (G_FILE (source), res, NULL);
    if (stream != NULL && !g_cancellable_is_cancelled (job->cancel)) {
        gdk_pixbuf_new_from_stream_at_scale_async (G_INPUT_STREAM (stream), 900, 650, TRUE,
                                                   job->cancel, quick_image_ready, job);
        g_object_unref (stream);
        return;
    }
    g_clear_object (&stream);
    quick_job_free (job);
}

static void
quick_text_ready (GObject *source, GAsyncResult *res, gpointer data)
{
    QuickJob *job = data;
    GtkWidget *window = NULL;
    GBytes *bytes = g_input_stream_read_bytes_finish (G_INPUT_STREAM (source), res, NULL);
    QuickLook *ql = quick_job_target (job, &window);
    if (ql != NULL && bytes != NULL) {
        gsize size = 0;
        const gchar *text = g_bytes_get_data (bytes, &size);
        if (size > 0 && memchr (text, 0, size) == NULL) {
            gchar *valid = g_utf8_make_valid (text, size);
            gtk_text_buffer_set_text (gtk_text_view_get_buffer (GTK_TEXT_VIEW (ql->text)),
                                      valid, -1);
            gtk_stack_set_visible_child_name (GTK_STACK (ql->stack), "text");
            g_free (valid);
        }
    }
    if (bytes != NULL) g_bytes_unref (bytes);
    g_clear_object (&window);
    quick_job_free (job);
}

static void
quick_text_opened (GObject *source, GAsyncResult *res, gpointer data)
{
    QuickJob *job = data;
    GFileInputStream *stream = g_file_read_finish (G_FILE (source), res, NULL);
    if (stream != NULL && !g_cancellable_is_cancelled (job->cancel)) {
        g_input_stream_read_bytes_async (G_INPUT_STREAM (stream), 131072, G_PRIORITY_DEFAULT,
                                         job->cancel, quick_text_ready, job);
        g_object_unref (stream);
        return;
    }
    g_clear_object (&stream);
    quick_job_free (job);
}

/* PDF: first page rendered by pdftoppm (poppler-utils), as in the old
 * Miller-only Quick Look; without pdftoppm the file icon stays. */
static void
quick_pdf_ready (GObject *source, GAsyncResult *res, gpointer data)
{
    QuickJob *job = data;
    GtkWidget *window = NULL;
    GBytes *out = NULL;
    QuickLook *ql;
    if (!g_subprocess_communicate_finish (G_SUBPROCESS (source), res, &out, NULL, NULL))
        out = NULL;
    ql = quick_job_target (job, &window);
    if (ql != NULL && out != NULL && g_bytes_get_size (out) > 0) {
        GInputStream *mem = g_memory_input_stream_new_from_bytes (out);
        GdkPixbuf *pixbuf = gdk_pixbuf_new_from_stream_at_scale (mem, 900, 650, TRUE,
                                                                 NULL, NULL);
        if (pixbuf != NULL) {
            gtk_image_set_from_pixbuf (GTK_IMAGE (ql->image), pixbuf);
            gtk_stack_set_visible_child_name (GTK_STACK (ql->stack), "image");
            g_object_unref (pixbuf);
        }
        g_object_unref (mem);
    }
    if (out != NULL) g_bytes_unref (out);
    g_clear_object (&window);
    quick_job_free (job);
}

static void
quick_pdf_start (QuickLook *ql, GFile *location)
{
    gchar *path = g_file_get_path (location);
    gchar *program = g_find_program_in_path ("pdftoppm");
    if (path != NULL && program != NULL) {
        const gchar *argv[] = { program, "-f", "1", "-singlefile", "-scale-to",
                                "900", "-png", path, "-", NULL };
        GSubprocess *process = g_subprocess_newv (argv,
            G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_SILENCE, NULL);
        if (process != NULL) {
            g_subprocess_communicate_async (process, NULL, ql->cancel,
                                            quick_pdf_ready, quick_job_new (ql));
            g_object_unref (process);
        }
    }
    g_free (program);
    g_free (path);
}

static void
quick_media_ready (DakkarMediaInfo *info, gpointer data)
{
    QuickJob *job = data;
    GtkWidget *window = NULL;
    QuickLook *ql = quick_job_target (job, &window);
    if (ql != NULL) {
        if (ql->art != NULL && info->cover != NULL)
            gtk_image_set_from_pixbuf (GTK_IMAGE (ql->art), info->cover);
        if (ql->grid != NULL) dakkar_media_grid_fill (ql->grid, info);
        if (info->problem != NULL)
            dakkar_player_set_message (ql->audio != NULL ? ql->audio : ql->video, info->problem);
        if (ql->audio != NULL) dakkar_player_set_duration_hint (ql->audio, info->duration);
        if (ql->video != NULL) dakkar_player_set_duration_hint (ql->video, info->duration);
        if (info->title != NULL && ql->caption != NULL && ql->audio != NULL) {
            gchar *markup = g_markup_printf_escaped ("<b>%s</b>", info->title);
            gtk_label_set_markup (GTK_LABEL (ql->caption), markup);
            g_free (markup);
        }
    }
    g_clear_object (&window);
}

gboolean
dakkar_quick_look_is_open (void)
{
    return quick_window != NULL;
}

void
dakkar_quick_look_close (void)
{
    if (quick_window != NULL) gtk_widget_destroy (quick_window);
}

void
dakkar_quick_look_toggle (GtkWidget *anchor, NemoFile *file)
{
    QuickLook *ql;
    GtkWidget *box, *scroll, *caption, *top;
    GFile *location;
    gchar *mime, *name, *uri;
    if (quick_window != NULL) {
        gtk_widget_destroy (quick_window);
        return;
    }
    if (file == NULL) return;
    location = nemo_file_get_location (file);
    if (location == NULL) return;
    mime = nemo_file_get_mime_type (file);
    name = nemo_file_get_display_name (file);
    uri = g_file_get_uri (location);

    ql = g_new0 (QuickLook, 1);
    ql->cancel = g_cancellable_new ();
    quick_window = ql->window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title (GTK_WINDOW (ql->window), name);
    gtk_window_set_default_size (GTK_WINDOW (ql->window), 920, 720);
    top = anchor != NULL ? gtk_widget_get_toplevel (anchor) : NULL;
    if (GTK_IS_WINDOW (top))
        gtk_window_set_transient_for (GTK_WINDOW (ql->window), GTK_WINDOW (top));
    g_object_set_data_full (G_OBJECT (ql->window), "dakkar-quick-look", ql, quick_free);
    ql->opened = g_get_monotonic_time ();
    g_signal_connect (ql->window, "key-press-event", G_CALLBACK (quick_key), ql);
    g_signal_connect (ql->window, "key-release-event", G_CALLBACK (quick_key_release), ql);
    g_signal_connect (ql->window, "destroy", G_CALLBACK (quick_destroyed), NULL);

    box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width (GTK_CONTAINER (box), 12);
    gtk_container_add (GTK_CONTAINER (ql->window), box);
    ql->caption = caption = gtk_label_new (name);
    gtk_widget_set_halign (caption, GTK_ALIGN_START);
    gtk_box_pack_start (GTK_BOX (box), caption, FALSE, FALSE, 0);
    ql->stack = gtk_stack_new ();
    gtk_box_pack_start (GTK_BOX (box), ql->stack, TRUE, TRUE, 0);
    ql->image = gtk_image_new ();
    gtk_stack_add_named (GTK_STACK (ql->stack), ql->image, "image");
    ql->text = gtk_text_view_new ();
    gtk_text_view_set_editable (GTK_TEXT_VIEW (ql->text), FALSE);
    gtk_text_view_set_monospace (GTK_TEXT_VIEW (ql->text), TRUE);
    gtk_text_view_set_wrap_mode (GTK_TEXT_VIEW (ql->text), GTK_WRAP_WORD_CHAR);
    scroll = gtk_scrolled_window_new (NULL, NULL);
    gtk_container_add (GTK_CONTAINER (scroll), ql->text);
    gtk_stack_add_named (GTK_STACK (ql->stack), scroll, "text");
    {
        GdkPixbuf *icon = nemo_file_get_icon_pixbuf (file, 256, TRUE, 1,
                                                     NEMO_FILE_ICON_FLAGS_USE_THUMBNAILS);
        if (icon != NULL) {
            gtk_image_set_from_pixbuf (GTK_IMAGE (ql->image), icon);
            g_object_unref (icon);
        } else {
            gtk_image_set_from_icon_name (GTK_IMAGE (ql->image), "text-x-generic",
                                          GTK_ICON_SIZE_DIALOG);
        }
    }
    if (mime != NULL && g_str_has_prefix (mime, "video/") && dakkar_media_supported ()) {
        GtkWidget *page = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
        ql->video = dakkar_player_new (DAKKAR_PLAYER_VIDEO, 860);
        ql->grid = dakkar_media_grid_new ();
        gtk_box_pack_start (GTK_BOX (page), ql->video, FALSE, FALSE, 0);
        gtk_box_pack_start (GTK_BOX (page), ql->grid, FALSE, FALSE, 0);
        gtk_stack_add_named (GTK_STACK (ql->stack), page, "video");
    } else if (dakkar_media_is_audio_mime (mime) && dakkar_media_supported ()) {
        GtkWidget *page = gtk_box_new (GTK_ORIENTATION_VERTICAL, 10);
        GtkWidget *holder = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
        ql->art = gtk_image_new_from_icon_name ("audio-x-generic", GTK_ICON_SIZE_DIALOG);
        gtk_image_set_pixel_size (GTK_IMAGE (ql->art), 256);
        gtk_widget_set_size_request (ql->art, -1, 380);
        ql->grid = dakkar_media_grid_new ();
        ql->audio = dakkar_player_new (DAKKAR_PLAYER_AUDIO, 480);
        gtk_widget_set_size_request (ql->audio, 480, -1);
        gtk_widget_set_halign (ql->audio, GTK_ALIGN_CENTER);
        gtk_widget_set_valign (page, GTK_ALIGN_CENTER);
        gtk_box_pack_start (GTK_BOX (page), ql->art, FALSE, FALSE, 0);
        gtk_box_pack_start (GTK_BOX (page), ql->grid, FALSE, FALSE, 0);
        gtk_box_pack_start (GTK_BOX (holder), ql->audio, FALSE, FALSE, 0);
        gtk_box_pack_start (GTK_BOX (page), holder, FALSE, FALSE, 0);
        gtk_stack_add_named (GTK_STACK (ql->stack), page, "audio");
    }
    if (ql->video != NULL || ql->audio != NULL) {
        GtkWidget *hint = gtk_label_new (_("Space: play / pause  ·  M: sound on / off  ·  Esc: close"));
        gtk_style_context_add_class (gtk_widget_get_style_context (hint), "dim-label");
        gtk_box_pack_end (GTK_BOX (box), hint, FALSE, FALSE, 0);
    }
    gtk_widget_show_all (ql->window);
    if (ql->grid != NULL)
        dakkar_media_info_load (uri, 380, ql->cancel, quick_media_ready, quick_job_new (ql),
                               (GDestroyNotify) quick_job_free);
    if (ql->video != NULL) {
        /* first frame + Play button; nothing plays until Play or Space */
        gtk_stack_set_visible_child_name (GTK_STACK (ql->stack), "video");
        dakkar_player_set_uri (ql->video, uri);
    } else if (ql->audio != NULL) {
        /* Sound starts only on Play (or Space). */
        gtk_stack_set_visible_child_name (GTK_STACK (ql->stack), "audio");
        dakkar_player_set_uri (ql->audio, uri);
    } else if (mime != NULL && g_str_has_prefix (mime, "image/")) {
        g_file_read_async (location, G_PRIORITY_DEFAULT, ql->cancel,
                           quick_image_opened, quick_job_new (ql));
    } else if (mime != NULL && (g_str_has_prefix (mime, "text/") ||
                                g_content_type_is_a (mime, "text/plain"))) {
        g_file_read_async (location, G_PRIORITY_DEFAULT, ql->cancel,
                           quick_text_opened, quick_job_new (ql));
    } else if (mime != NULL && g_str_equal (mime, "application/pdf")) {
        quick_pdf_start (ql, location);
    }
    g_free (uri);
    g_free (name);
    g_free (mime);
    g_object_unref (location);
}
