/* Dakkar: audio/video preview (see dakkar-media.h).  GPL-2.0-or-later. */

#include <config.h>
#include <string.h>
#include <glib/gi18n.h>
#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <gst/audio/streamvolume.h>
#include <gst/pbutils/pbutils.h>
#include <gst/tag/tag.h>

#include "dakkar-media.h"

#define MEDIA_DOMAIN "Dakkar-Media"
#define media_debug(...) g_log (MEDIA_DOMAIN, G_LOG_LEVEL_DEBUG, __VA_ARGS__)

/* playbin flags (GstPlayFlags is not public API) */
#define PLAY_FLAG_VIDEO       (1 << 0)
#define PLAY_FLAG_AUDIO       (1 << 1)
#define PLAY_FLAG_SOFT_VOLUME (1 << 4)

gboolean
dakkar_media_is_audio_mime (const char *mime)
{
    static const char *playlists[] = {
        "audio/x-mpegurl", "audio/mpegurl", "audio/x-scpls", "audio/x-ms-asx",
        "audio/x-iriver-pla", "audio/x-ms-wax", "audio/x-musepack-playlist",
        "audio/x-ape-playlist", NULL
    };
    guint i;
    if (mime == NULL || !g_str_has_prefix (mime, "audio/")) return FALSE;
    for (i = 0; playlists[i] != NULL; i++)
        if (g_str_equal (mime, playlists[i])) return FALSE;
    return TRUE;
}

gboolean
dakkar_media_supported (void)
{
    static gint supported = -1;
    if (supported < 0) {
        const gchar *needed[] = {"playbin", "appsink", "videoconvert", "videoscale",
                                 "fakesink", "uridecodebin"};
        guint i;
        gst_init_check (NULL, NULL, NULL);
        supported = 1;
        for (i = 0; i < G_N_ELEMENTS (needed); i++) {
            GstElementFactory *f = gst_element_factory_find (needed[i]);
            if (f == NULL) { supported = 0; break; }
            gst_object_unref (f);
        }
    }
    return supported == 1;
}

/* ------------------------------------------------------------------ */
/* audio output probe: without a sound device (sandbox, headless) the   */
/* player uses a clocked fakesink so the UI still behaves normally.     */
/* ------------------------------------------------------------------ */

static gint audio_output = -1;   /* -1 unknown, 0 none, 1 available */

static gpointer
audio_probe_thread (gpointer data)
{
    GstElement *sink = gst_element_factory_make ("autoaudiosink", NULL);
    gint found = 0;
    (void) data;
    if (sink != NULL) {
        if (gst_element_set_state (sink, GST_STATE_READY) != GST_STATE_CHANGE_FAILURE) {
            GstIterator *it = gst_bin_iterate_sinks (GST_BIN (sink));
            GValue item = G_VALUE_INIT;
            while (gst_iterator_next (it, &item) == GST_ITERATOR_OK) {
                GstElement *child = g_value_get_object (&item);
                GstElementFactory *f = gst_element_get_factory (child);
                const gchar *name = f ? gst_plugin_feature_get_name (GST_PLUGIN_FEATURE (f)) : "";
                if (g_strcmp0 (name, "fakesink") != 0 && g_strcmp0 (name, "fakeaudiosink") != 0)
                    found = 1;
                g_value_reset (&item);
            }
            g_value_unset (&item);
            gst_iterator_free (it);
        }
        gst_element_set_state (sink, GST_STATE_NULL);
        gst_object_unref (sink);
    }
    g_atomic_int_set (&audio_output, found);
    media_debug ("audio output %s", found ? "available" : "not available (silent fakesink)");
    return NULL;
}

static void
audio_probe_start (void)
{
    static gsize started = 0;
    if (g_once_init_enter (&started)) {
        GThread *t = g_thread_try_new ("dakkar-audio-probe", audio_probe_thread, NULL, NULL);
        if (t != NULL) g_thread_unref (t);
        else g_atomic_int_set (&audio_output, 1);
        g_once_init_leave (&started, 1);
    }
}

/* ------------------------------------------------------------------ */
/* player                                                              */
/* ------------------------------------------------------------------ */

typedef struct {
    DakkarPlayerMode mode;
    GtkWidget *root;          /* weak, owner */
    GtkWidget *image;         /* video frame */
    GtkWidget *big_button;    /* centred play button on the video */
    GtkWidget *big_icon;
    GtkWidget *speaker;       /* video: overlay speaker; audio: mute button */
    GtkWidget *speaker_icon;
    GtkWidget *play;
    GtkWidget *play_icon;
    GtkWidget *scale;
    GtkWidget *time_label;
    GtkWidget *volume;
    GtkWidget *message;
    GstElement *pipeline;
    GstElement *sink;         /* appsink (video) */
    GstElement *audio_sink;
    gulong sample_id;
    gulong preroll_id;
    guint bus_id;
    guint tick_id;
    gboolean playing;
    gboolean muted;
    gboolean silent;          /* no audio device: fakesink */
    gboolean retried;
    gboolean failed;
    gint64 duration;
    gint width;
    gchar *uri;
    gdouble volume_value;     /* cubic 0..1, kept while the widget lives */
} DakkarPlayer;

static DakkarPlayer *
player_get (GtkWidget *widget)
{
    return widget != NULL ? g_object_get_data (G_OBJECT (widget), "dakkar-player") : NULL;
}

static gchar *
format_time (gint64 ns)
{
    gint64 s = ns > 0 ? ns / GST_SECOND : 0;
    if (s >= 3600)
        return g_strdup_printf ("%d:%02d:%02d", (int) (s / 3600), (int) ((s / 60) % 60), (int) (s % 60));
    return g_strdup_printf ("%d:%02d", (int) (s / 60), (int) (s % 60));
}

static void
player_update_time (DakkarPlayer *p, gint64 position)
{
    gchar *a = format_time (position), *b = format_time (p->duration), *text;
    /* Translators: elapsed / total playing time, e.g. "0:12 / 3:45" */
    text = g_strdup_printf (_("%s / %s"), a, b);
    gtk_label_set_text (GTK_LABEL (p->time_label), text);
    g_free (text);
    g_free (a);
    g_free (b);
}

static void
player_update_icons (DakkarPlayer *p)
{
    const gchar *icon = p->playing ? "media-playback-pause-symbolic" : "media-playback-start-symbolic";
    gtk_image_set_from_icon_name (GTK_IMAGE (p->play_icon), icon, GTK_ICON_SIZE_BUTTON);
    gtk_widget_set_tooltip_text (p->play, p->playing ? _("Pause") : _("Play"));
    if (p->big_icon != NULL) {
        gtk_image_set_from_icon_name (GTK_IMAGE (p->big_icon), icon, GTK_ICON_SIZE_BUTTON);
        gtk_widget_set_opacity (p->big_button, p->playing ? 0.0 : 1.0);
    }
    gtk_image_set_from_icon_name (GTK_IMAGE (p->speaker_icon),
        p->muted ? "audio-volume-muted-symbolic" : "audio-volume-high-symbolic",
        GTK_ICON_SIZE_BUTTON);
    gtk_widget_set_tooltip_text (p->speaker, p->muted ? _("Sound off — click to turn it on")
                                                      : _("Sound on — click to mute"));
    gtk_widget_set_sensitive (p->play, p->pipeline != NULL && !p->failed);
    gtk_widget_set_sensitive (p->scale, p->pipeline != NULL && !p->failed && p->duration > 0);
    {
        AtkObject *a = gtk_widget_get_accessible (p->speaker);
        if (a != NULL) atk_object_set_name (a, p->muted ? _("Unmute") : _("Mute"));
    }
}

static gboolean
player_tick (gpointer data)
{
    DakkarPlayer *p = data;
    gint64 pos = 0, dur = 0;
    if (p->pipeline == NULL) { p->tick_id = 0; return G_SOURCE_REMOVE; }
    if (gst_element_query_duration (p->pipeline, GST_FORMAT_TIME, &dur) && dur > 0 &&
        dur != p->duration) {
        p->duration = dur;
        gtk_range_set_range (GTK_RANGE (p->scale), 0.0, (gdouble) dur / GST_SECOND);
        player_update_icons (p);
    }
    if (gst_element_query_position (p->pipeline, GST_FORMAT_TIME, &pos) && pos >= 0) {
        gtk_range_set_value (GTK_RANGE (p->scale), (gdouble) pos / GST_SECOND);
        player_update_time (p, pos);
    }
    return G_SOURCE_CONTINUE;
}

/* video frames ----------------------------------------------------- */

typedef struct {
    GtkWidget *image;
    GdkPixbuf *pixbuf;
} FrameUpdate;

static void
frame_update_free (gpointer data)
{
    FrameUpdate *u = data;
    g_clear_object (&u->image);
    g_clear_object (&u->pixbuf);
    g_free (u);
}

static gboolean
frame_update_idle (gpointer data)
{
    FrameUpdate *u = data;
    if (GTK_IS_IMAGE (u->image) && !gtk_widget_in_destruction (u->image) &&
        g_object_get_data (G_OBJECT (u->image), "dakkar-frame-live") != NULL)
        gtk_image_set_from_pixbuf (GTK_IMAGE (u->image), u->pixbuf);
    return G_SOURCE_REMOVE;
}

static GdkPixbuf *
sample_to_pixbuf (GstSample *sample)
{
    GstCaps *caps = gst_sample_get_caps (sample);
    const GstStructure *s;
    GstBuffer *buffer;
    GstMapInfo map;
    GdkPixbuf *pixbuf = NULL;
    gint width, height, stride, row;
    if (caps == NULL) return NULL;
    s = gst_caps_get_structure (caps, 0);
    if (!gst_structure_get_int (s, "width", &width) ||
        !gst_structure_get_int (s, "height", &height) ||
        width <= 0 || height <= 0 || width > 4096 || height > 4096)
        return NULL;
    buffer = gst_sample_get_buffer (sample);
    if (buffer == NULL || !gst_buffer_map (buffer, &map, GST_MAP_READ)) return NULL;
    stride = GST_ROUND_UP_4 (width * 3);
    if (map.size < (gsize) stride * height) {
        stride = width * 3;
        if (map.size < (gsize) stride * height) {
            gst_buffer_unmap (buffer, &map);
            return NULL;
        }
    }
    pixbuf = gdk_pixbuf_new (GDK_COLORSPACE_RGB, FALSE, 8, width, height);
    if (pixbuf != NULL) {
        guint8 *dst = gdk_pixbuf_get_pixels (pixbuf);
        gint dst_stride = gdk_pixbuf_get_rowstride (pixbuf);
        for (row = 0; row < height; row++)
            memcpy (dst + row * dst_stride, map.data + row * stride, width * 3);
    }
    gst_buffer_unmap (buffer, &map);
    return pixbuf;
}

static GstFlowReturn
player_frame (GstSample *sample, GtkWidget *image)
{
    GdkPixbuf *pixbuf;
    FrameUpdate *u;
    if (sample == NULL) return GST_FLOW_EOS;
    pixbuf = sample_to_pixbuf (sample);
    gst_sample_unref (sample);
    if (pixbuf == NULL) return GST_FLOW_OK;
    u = g_new0 (FrameUpdate, 1);
    u->image = g_object_ref (image);
    u->pixbuf = pixbuf;
    g_idle_add_full (G_PRIORITY_DEFAULT, frame_update_idle, u, frame_update_free);
    return GST_FLOW_OK;
}

static GstFlowReturn
player_new_sample (GstAppSink *sink, gpointer user_data)
{
    /* user_data: the image, referenced by the signal closure */
    return player_frame (gst_app_sink_pull_sample (sink), user_data);
}

static GstFlowReturn
player_new_preroll (GstAppSink *sink, gpointer user_data)
{
    /* first frame while paused (and after a seek while paused) */
    return player_frame (gst_app_sink_pull_preroll (sink), user_data);
}

/* stop / build ----------------------------------------------------- */

static gpointer
player_stop_thread (gpointer data)
{
    GstElement *pipeline = data;
    gst_element_set_state (pipeline, GST_STATE_NULL);
    gst_object_unref (pipeline);
    return NULL;
}

static void
player_stop (DakkarPlayer *p)
{
    GstElement *pipeline = p->pipeline;
    if (p->sink != NULL) {
        if (p->sample_id != 0) g_signal_handler_disconnect (p->sink, p->sample_id);
        if (p->preroll_id != 0) g_signal_handler_disconnect (p->sink, p->preroll_id);
        p->sample_id = p->preroll_id = 0;
        gst_object_unref (p->sink);
        p->sink = NULL;
    }
    g_clear_pointer (&p->audio_sink, gst_object_unref);
    if (p->bus_id != 0) { g_source_remove (p->bus_id); p->bus_id = 0; }
    if (p->tick_id != 0) { g_source_remove (p->tick_id); p->tick_id = 0; }
    p->pipeline = NULL;
    if (p->playing) media_debug ("stopped %s", p->uri ? p->uri : "");
    p->playing = FALSE;
    if (p->image != NULL)
        g_object_set_data (G_OBJECT (p->image), "dakkar-frame-live", NULL);
    if (pipeline != NULL) {
        /* Never block the GTK thread on the streaming thread. */
        GThread *t = g_thread_try_new ("dakkar-player-stop", player_stop_thread, pipeline, NULL);
        if (t != NULL) g_thread_unref (t);
        else player_stop_thread (pipeline);
    }
}

static void player_build (DakkarPlayer *p, gint64 start);
static void player_set_note (DakkarPlayer *p, const char *note);

static gboolean
player_bus (GstBus *bus, GstMessage *msg, gpointer data)
{
    DakkarPlayer *p = data;
    (void) bus;
    if (p->pipeline == NULL) return G_SOURCE_CONTINUE;
    switch (GST_MESSAGE_TYPE (msg)) {
    case GST_MESSAGE_EOS:
        p->playing = FALSE;
        gst_element_set_state (p->pipeline, GST_STATE_PAUSED);
        gst_element_seek_simple (p->pipeline, GST_FORMAT_TIME,
                                 GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT, 0);
        media_debug ("end of stream");
        player_update_icons (p);
        player_update_time (p, 0);
        gtk_range_set_value (GTK_RANGE (p->scale), 0.0);
        break;
    case GST_MESSAGE_ERROR: {
        GError *error = NULL;
        gchar *dbg = NULL;
        gboolean from_audio_sink = p->audio_sink != NULL &&
            (GST_MESSAGE_SRC (msg) == GST_OBJECT (p->audio_sink) ||
             gst_object_has_as_ancestor (GST_MESSAGE_SRC (msg), GST_OBJECT (p->audio_sink)));
        gst_message_parse_error (msg, &error, &dbg);
        media_debug ("error: %s (%s)", error ? error->message : "?", dbg ? dbg : "");
        if (from_audio_sink && !p->silent && !p->retried) {
            /* The sound device went away: keep playing silently. */
            gint64 pos = 0;
            gst_element_query_position (p->pipeline, GST_FORMAT_TIME, &pos);
            p->silent = TRUE;
            p->retried = TRUE;
            player_stop (p);
            player_build (p, pos);
        } else {
            gchar *text;
            if (error != NULL && error->domain == GST_CORE_ERROR &&
                error->code == GST_CORE_ERROR_MISSING_PLUGIN)
                text = g_strdup (_("Cannot play this file: the codec is not installed."));
            else
                text = g_strdup_printf (_("Cannot play this file: %s"),
                                        error ? error->message : _("unknown error"));
            p->failed = TRUE;
            player_stop (p);
            dakkar_player_set_message (p->root, text);
            player_update_icons (p);
            g_free (text);
        }
        g_clear_error (&error);
        g_free (dbg);
        break;
    }
    case GST_MESSAGE_ELEMENT:
        if (gst_is_missing_plugin_message (msg)) {
            gchar *desc = gst_missing_plugin_message_get_description (msg);
            gchar *text = g_strdup_printf (_("Missing codec: %s"), desc ? desc : "?");
            dakkar_player_set_message (p->root, text);
            g_free (text);
            g_free (desc);
        }
        break;
    default:
        break;
    }
    return G_SOURCE_CONTINUE;
}

static void
player_build (DakkarPlayer *p, gint64 start)
{
    GstElement *pipeline = gst_element_factory_make ("playbin", NULL);
    GstElement *asink = NULL;
    gint flags = 0;
    if (pipeline == NULL || p->uri == NULL) {
        if (pipeline) gst_object_unref (pipeline);
        p->failed = TRUE;
        dakkar_player_set_message (p->root, _("Playback is not available: GStreamer playbin is missing."));
        player_update_icons (p);
        return;
    }
    g_object_get (pipeline, "flags", &flags, NULL);
    if (p->mode == DAKKAR_PLAYER_VIDEO) {
        GstElement *bin = gst_bin_new (NULL);
        GstElement *convert = gst_element_factory_make ("videoconvert", NULL);
        GstElement *scale = gst_element_factory_make ("videoscale", NULL);
        GstElement *sink = gst_element_factory_make ("appsink", NULL);
        GstCaps *caps;
        GstPad *pad;
        gchar *s;
        if (bin == NULL || convert == NULL || scale == NULL || sink == NULL) {
            if (bin) gst_object_unref (bin);
            if (convert) gst_object_unref (convert);
            if (scale) gst_object_unref (scale);
            if (sink) gst_object_unref (sink);
            gst_object_unref (pipeline);
            p->failed = TRUE;
            player_update_icons (p);
            return;
        }
        /* Fixed width, height follows the display aspect ratio. */
        s = g_strdup_printf ("video/x-raw,format=RGB,width=%d,pixel-aspect-ratio=1/1", p->width);
        caps = gst_caps_from_string (s);
        g_free (s);
        g_object_set (sink, "caps", caps, "emit-signals", TRUE, "max-buffers", 2,
                      "drop", TRUE, "sync", TRUE, NULL);
        gst_caps_unref (caps);
        gst_bin_add_many (GST_BIN (bin), convert, scale, sink, NULL);
        gst_element_link_many (convert, scale, sink, NULL);
        pad = gst_element_get_static_pad (convert, "sink");
        gst_element_add_pad (bin, gst_ghost_pad_new ("sink", pad));
        gst_object_unref (pad);
        g_object_set (pipeline, "video-sink", bin, NULL);
        p->sink = gst_object_ref (sink);
        g_object_set_data (G_OBJECT (p->image), "dakkar-frame-live", GINT_TO_POINTER (1));
        p->sample_id = g_signal_connect_data (sink, "new-sample",
            G_CALLBACK (player_new_sample), g_object_ref (p->image),
            (GClosureNotify) (void (*)(void)) g_object_unref, 0);
        p->preroll_id = g_signal_connect_data (sink, "new-preroll",
            G_CALLBACK (player_new_preroll), g_object_ref (p->image),
            (GClosureNotify) (void (*)(void)) g_object_unref, 0);
        flags |= PLAY_FLAG_VIDEO | PLAY_FLAG_AUDIO | PLAY_FLAG_SOFT_VOLUME;
    } else {
        GstElement *vsink = gst_element_factory_make ("fakesink", NULL);
        if (vsink != NULL) g_object_set (pipeline, "video-sink", vsink, NULL);
        flags = (flags & ~PLAY_FLAG_VIDEO) | PLAY_FLAG_AUDIO | PLAY_FLAG_SOFT_VOLUME;
    }
    g_object_set (pipeline, "flags", flags, NULL);
    if (g_atomic_int_get (&audio_output) == 0) p->silent = TRUE;
    if (p->silent) {
        asink = gst_element_factory_make ("fakesink", NULL);
        if (asink != NULL) g_object_set (asink, "sync", TRUE, NULL);
    } else {
        asink = gst_element_factory_make ("autoaudiosink", NULL);
    }
    if (asink != NULL) {
        p->audio_sink = gst_object_ref (asink);
        g_object_set (pipeline, "audio-sink", asink, NULL);
    }
    g_object_set (pipeline, "uri", p->uri, "mute", p->muted,
                  "volume", gst_stream_volume_convert_volume (GST_STREAM_VOLUME_FORMAT_CUBIC,
                                                              GST_STREAM_VOLUME_FORMAT_LINEAR,
                                                              p->volume_value), NULL);
    p->pipeline = pipeline;
    {
        GstBus *bus = gst_element_get_bus (pipeline);
        p->bus_id = gst_bus_add_watch (bus, player_bus, p);
        gst_object_unref (bus);
    }
    p->tick_id = g_timeout_add (250, player_tick, p);
    if (p->silent && !p->muted)
        player_set_note (p, _("No sound device found; playing silently."));
    else
        dakkar_player_set_message (p->root, NULL);
    /* Preroll: first frame / duration; playback starts on demand. */
    if (gst_element_set_state (pipeline, GST_STATE_PAUSED) == GST_STATE_CHANGE_FAILURE) {
        p->failed = TRUE;
        player_stop (p);
        dakkar_player_set_message (p->root, _("Cannot play this file."));
    } else if (start > 0) {
        gst_element_get_state (pipeline, NULL, NULL, 2 * GST_SECOND);
        gst_element_seek_simple (pipeline, GST_FORMAT_TIME,
                                 GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE, start);
        if (p->playing) gst_element_set_state (pipeline, GST_STATE_PLAYING);
    }
    player_update_icons (p);
}

/* callbacks -------------------------------------------------------- */

static void
player_play_clicked (GtkButton *button, gpointer data)
{
    (void) button;
    dakkar_player_toggle (GTK_WIDGET (data));
}

static void
player_speaker_clicked (GtkButton *button, gpointer data)
{
    DakkarPlayer *p = player_get (GTK_WIDGET (data));
    (void) button;
    if (p != NULL) dakkar_player_set_muted (p->root, !p->muted);
}

static gboolean
player_image_pressed (GtkWidget *widget, GdkEventButton *event, gpointer data)
{
    (void) widget;
    if (event->button == GDK_BUTTON_PRIMARY && event->type == GDK_BUTTON_PRESS) {
        dakkar_player_toggle (GTK_WIDGET (data));
        return TRUE;
    }
    return FALSE;
}

static gboolean
player_seek (GtkRange *range, GtkScrollType scroll, gdouble value, gpointer data)
{
    DakkarPlayer *p = data;
    gint64 target;
    (void) range;
    (void) scroll;
    if (p->pipeline == NULL || p->duration <= 0) return TRUE;
    target = (gint64) (CLAMP (value, 0.0, (gdouble) p->duration / GST_SECOND) * GST_SECOND);
    gst_element_seek_simple (p->pipeline, GST_FORMAT_TIME,
        GST_SEEK_FLAG_FLUSH | (p->mode == DAKKAR_PLAYER_VIDEO ? GST_SEEK_FLAG_KEY_UNIT
                                                             : GST_SEEK_FLAG_ACCURATE), target);
    player_update_time (p, target);
    media_debug ("seek to %" G_GINT64_FORMAT " ms", target / GST_MSECOND);
    return FALSE;
}

static void
player_volume_changed (GtkRange *range, gpointer data)
{
    DakkarPlayer *p = data;
    p->volume_value = gtk_range_get_value (range);
    if (p->pipeline != NULL)
        g_object_set (p->pipeline, "volume",
                      gst_stream_volume_convert_volume (GST_STREAM_VOLUME_FORMAT_CUBIC,
                                                        GST_STREAM_VOLUME_FORMAT_LINEAR,
                                                        p->volume_value), NULL);
    if (p->muted && p->volume_value > 0.0)
        dakkar_player_set_muted (p->root, FALSE);
}

static void
player_free (gpointer data)
{
    DakkarPlayer *p = data;
    player_stop (p);
    g_free (p->uri);
    g_free (p);
}

static void
player_css (GtkWidget *widget)
{
    static GtkCssProvider *provider = NULL;
    if (provider == NULL) {
        provider = gtk_css_provider_new ();
        gtk_css_provider_load_from_data (provider,
            ".dakkar-player-overlay-button { background: rgba(0,0,0,0.45); color: white;"
            " border: none; border-radius: 99px; min-width: 24px; min-height: 24px; padding: 4px; }\n"
            ".dakkar-player-time { font-size: 0.85em; font-feature-settings: \"tnum\"; }\n"
            ".dakkar-player-message { font-size: 0.85em; color: #c01c28; }\n"
            ".dakkar-player-message.dakkar-player-note { color: alpha(currentColor, 0.65); }\n", -1, NULL);
    }
    gtk_style_context_add_provider (gtk_widget_get_style_context (widget),
                                    GTK_STYLE_PROVIDER (provider),
                                    GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
}

static GtkWidget *
icon_button (const gchar *icon, GtkWidget **image_out, gboolean overlay)
{
    GtkWidget *button = gtk_button_new ();
    GtkWidget *image = gtk_image_new_from_icon_name (icon, GTK_ICON_SIZE_BUTTON);
    gtk_button_set_image (GTK_BUTTON (button), image);
    gtk_widget_set_focus_on_click (button, FALSE);
    if (overlay) {
        gtk_style_context_add_class (gtk_widget_get_style_context (button),
                                     "dakkar-player-overlay-button");
        player_css (button);
    } else {
        gtk_button_set_relief (GTK_BUTTON (button), GTK_RELIEF_NONE);
    }
    *image_out = image;
    return button;
}

GtkWidget *
dakkar_player_new (DakkarPlayerMode mode, gint width)
{
    DakkarPlayer *p = g_new0 (DakkarPlayer, 1);
    GtkWidget *root = gtk_box_new (GTK_ORIENTATION_VERTICAL, 2);
    GtkWidget *row, *row2;
    gst_init_check (NULL, NULL, NULL);
    audio_probe_start ();
    p->mode = mode;
    p->width = MAX (width, 64);
    p->root = root;
    p->volume_value = 1.0;
    p->muted = mode == DAKKAR_PLAYER_VIDEO;
    gtk_widget_set_name (root, mode == DAKKAR_PLAYER_VIDEO ? "dakkar-video-player"
                                                          : "dakkar-audio-player");
    if (mode == DAKKAR_PLAYER_VIDEO) {
        GtkWidget *overlay = gtk_overlay_new ();
        GtkWidget *events = gtk_event_box_new ();
        p->image = gtk_image_new ();
        gtk_widget_set_halign (p->image, GTK_ALIGN_CENTER);
        gtk_widget_set_valign (p->image, GTK_ALIGN_CENTER);
        gtk_container_add (GTK_CONTAINER (events), p->image);
        gtk_container_add (GTK_CONTAINER (overlay), events);
        /* keep the picture area while the first frame is decoded */
        gtk_widget_set_size_request (overlay, p->width, p->width * 9 / 16);
        g_signal_connect (events, "button-press-event", G_CALLBACK (player_image_pressed), root);
        p->big_button = icon_button ("media-playback-start-symbolic", &p->big_icon, TRUE);
        gtk_widget_set_halign (p->big_button, GTK_ALIGN_CENTER);
        gtk_widget_set_valign (p->big_button, GTK_ALIGN_CENTER);
        gtk_widget_set_tooltip_text (p->big_button, _("Play / Pause"));
        g_signal_connect (p->big_button, "clicked", G_CALLBACK (player_play_clicked), root);
        gtk_overlay_add_overlay (GTK_OVERLAY (overlay), p->big_button);
        /* Speaker on the picture: sound is off until the user turns it on. */
        p->speaker = icon_button ("audio-volume-muted-symbolic", &p->speaker_icon, TRUE);
        gtk_widget_set_halign (p->speaker, GTK_ALIGN_END);
        gtk_widget_set_valign (p->speaker, GTK_ALIGN_END);
        gtk_widget_set_margin_end (p->speaker, 6);
        gtk_widget_set_margin_bottom (p->speaker, 6);
        gtk_widget_set_name (p->speaker, "dakkar-player-speaker");
        g_signal_connect (p->speaker, "clicked", G_CALLBACK (player_speaker_clicked), root);
        gtk_overlay_add_overlay (GTK_OVERLAY (overlay), p->speaker);
        gtk_box_pack_start (GTK_BOX (root), overlay, FALSE, FALSE, 0);
    }
    row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 2);
    p->play = icon_button ("media-playback-start-symbolic", &p->play_icon, FALSE);
    gtk_widget_set_name (p->play, "dakkar-player-play");
    g_signal_connect (p->play, "clicked", G_CALLBACK (player_play_clicked), root);
    gtk_box_pack_start (GTK_BOX (row), p->play, FALSE, FALSE, 0);
    p->scale = gtk_scale_new_with_range (GTK_ORIENTATION_HORIZONTAL, 0.0, 1.0, 1.0);
    gtk_scale_set_draw_value (GTK_SCALE (p->scale), FALSE);
    gtk_widget_set_hexpand (p->scale, TRUE);
    gtk_widget_set_size_request (p->scale, 40, -1);
    gtk_widget_set_tooltip_text (p->scale, _("Position"));
    g_signal_connect (p->scale, "change-value", G_CALLBACK (player_seek), p);
    gtk_box_pack_start (GTK_BOX (row), p->scale, TRUE, TRUE, 0);
    gtk_box_pack_start (GTK_BOX (root), row, FALSE, FALSE, 0);

    row2 = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 2);
    p->time_label = gtk_label_new ("");
    gtk_style_context_add_class (gtk_widget_get_style_context (p->time_label), "dakkar-player-time");
    player_css (p->time_label);
    gtk_widget_set_halign (p->time_label, GTK_ALIGN_START);
    gtk_widget_set_margin_start (p->time_label, 6);
    gtk_box_pack_start (GTK_BOX (row2), p->time_label, TRUE, TRUE, 0);
    if (mode == DAKKAR_PLAYER_AUDIO) {
        p->speaker = icon_button ("audio-volume-high-symbolic", &p->speaker_icon, FALSE);
        gtk_widget_set_name (p->speaker, "dakkar-player-speaker");
        g_signal_connect (p->speaker, "clicked", G_CALLBACK (player_speaker_clicked), root);
        gtk_box_pack_start (GTK_BOX (row2), p->speaker, FALSE, FALSE, 0);
        p->volume = gtk_scale_new_with_range (GTK_ORIENTATION_HORIZONTAL, 0.0, 1.0, 0.05);
        gtk_scale_set_draw_value (GTK_SCALE (p->volume), FALSE);
        gtk_range_set_value (GTK_RANGE (p->volume), p->volume_value);
        gtk_widget_set_size_request (p->volume, 60, -1);
        gtk_widget_set_tooltip_text (p->volume, _("Volume"));
        g_signal_connect (p->volume, "value-changed", G_CALLBACK (player_volume_changed), p);
        gtk_box_pack_start (GTK_BOX (row2), p->volume, FALSE, FALSE, 0);
    }
    gtk_box_pack_start (GTK_BOX (root), row2, FALSE, FALSE, 0);
    p->message = gtk_label_new ("");
    gtk_label_set_line_wrap (GTK_LABEL (p->message), TRUE);
    gtk_label_set_max_width_chars (GTK_LABEL (p->message), 30);
    gtk_style_context_add_class (gtk_widget_get_style_context (p->message), "dakkar-player-message");
    player_css (p->message);
    gtk_widget_set_no_show_all (p->message, TRUE);
    gtk_box_pack_start (GTK_BOX (root), p->message, FALSE, FALSE, 0);
    g_object_set_data_full (G_OBJECT (root), "dakkar-player", p, player_free);
    player_update_icons (p);
    player_update_time (p, 0);
    gtk_widget_show_all (root);
    return root;
}

static void
player_set_note (DakkarPlayer *p, const char *note)
{
    /* informational (dim), unlike errors (red) */
    dakkar_player_set_message (p->root, note);
    gtk_style_context_add_class (gtk_widget_get_style_context (p->message), "dakkar-player-note");
}

void
dakkar_player_set_message (GtkWidget *widget, const char *message)
{
    DakkarPlayer *p = player_get (widget);
    if (p == NULL) return;
    gtk_style_context_remove_class (gtk_widget_get_style_context (p->message), "dakkar-player-note");
    gtk_label_set_text (GTK_LABEL (p->message), message ? message : "");
    gtk_widget_set_visible (p->message, message != NULL && *message != '\0');
}

void
dakkar_player_set_uri (GtkWidget *widget, const char *uri)
{
    DakkarPlayer *p = player_get (widget);
    if (p == NULL) return;
    player_stop (p);
    g_clear_pointer (&p->uri, g_free);
    if (p->image != NULL) gtk_image_clear (GTK_IMAGE (p->image));
    p->duration = 0;
    p->failed = FALSE;
    p->retried = FALSE;
    p->silent = FALSE;
    /* Every new file: video starts muted, audio with sound. */
    p->muted = p->mode == DAKKAR_PLAYER_VIDEO;
    gtk_range_set_range (GTK_RANGE (p->scale), 0.0, 1.0);
    gtk_range_set_value (GTK_RANGE (p->scale), 0.0);
    dakkar_player_set_message (widget, NULL);
    player_update_time (p, 0);
    if (uri != NULL && dakkar_media_supported ()) {
        p->uri = g_strdup (uri);
        player_build (p, 0);
    }
    player_update_icons (p);
}

void
dakkar_player_toggle (GtkWidget *widget)
{
    DakkarPlayer *p = player_get (widget);
    if (p == NULL || p->pipeline == NULL) return;
    p->playing = !p->playing;
    gst_element_set_state (p->pipeline, p->playing ? GST_STATE_PLAYING : GST_STATE_PAUSED);
    media_debug ("%s %s", p->playing ? "playing" : "paused", p->uri);
    player_update_icons (p);
}

void
dakkar_player_set_duration_hint (GtkWidget *widget, gint64 duration)
{
    DakkarPlayer *p = player_get (widget);
    gint64 pos = 0;
    if (p == NULL || duration <= 0 || p->duration > 0) return;
    p->duration = duration;
    gtk_range_set_range (GTK_RANGE (p->scale), 0.0, (gdouble) duration / GST_SECOND);
    if (p->pipeline != NULL)
        gst_element_query_position (p->pipeline, GST_FORMAT_TIME, &pos);
    player_update_time (p, pos);
    player_update_icons (p);
}

gboolean
dakkar_player_is_active (GtkWidget *widget)
{
    DakkarPlayer *p = player_get (widget);
    return p != NULL && p->pipeline != NULL;
}

gboolean
dakkar_player_is_playing (GtkWidget *widget)
{
    DakkarPlayer *p = player_get (widget);
    return p != NULL && p->pipeline != NULL && p->playing;
}

gboolean
dakkar_player_get_muted (GtkWidget *widget)
{
    DakkarPlayer *p = player_get (widget);
    return p == NULL || p->muted;
}

void
dakkar_player_set_muted (GtkWidget *widget, gboolean muted)
{
    DakkarPlayer *p = player_get (widget);
    if (p == NULL) return;
    p->muted = muted;
    if (p->pipeline != NULL) g_object_set (p->pipeline, "mute", muted, NULL);
    media_debug ("%s", muted ? "muted" : "sound on");
    if (!muted && p->silent)
        player_set_note (p, _("No sound device found; playing silently."));
    else if (p->silent && !p->failed)
        dakkar_player_set_message (widget, NULL);
    player_update_icons (p);
}

/* ------------------------------------------------------------------ */
/* metadata (GstDiscoverer) + album art                                */
/* ------------------------------------------------------------------ */

typedef struct {
    gint ref;
    gboolean done;
    gchar *uri;
    gint cover_size;
    GCancellable *cancel;
    gulong cancel_id;
    GstDiscoverer *discoverer;
    DakkarMediaInfoFunc func;
    gpointer data;
    GDestroyNotify notify;
    DakkarMediaInfo *info;
    GBytes *embedded;      /* raw embedded image to decode off the main thread */
    gboolean need_tags;    /* decoder missing: read tags with parsebin instead */
    GstTagList *fallback_tags;
    gint64 fallback_duration;
} InfoJob;

static void
info_free (DakkarMediaInfo *info)
{
    if (info == NULL) return;
    g_clear_object (&info->cover);
    g_ptr_array_unref (info->keys);
    g_ptr_array_unref (info->values);
    g_free (info->title);
    g_free (info->problem);
    g_free (info);
}

static InfoJob *
info_job_ref (InfoJob *job)
{
    job->ref++;
    return job;
}

static void
info_job_unref (InfoJob *job)
{
    if (--job->ref > 0) return;
    if (job->notify != NULL) job->notify (job->data);
    info_free (job->info);
    if (job->embedded) g_bytes_unref (job->embedded);
    if (job->fallback_tags) gst_tag_list_unref (job->fallback_tags);
    g_clear_object (&job->cancel);
    g_free (job->uri);
    g_free (job);
}

static void
info_job_stop (InfoJob *job)
{
    if (job->cancel_id != 0) {
        g_cancellable_disconnect (job->cancel, job->cancel_id);
        job->cancel_id = 0;
    }
    if (job->discoverer != NULL) {
        GstDiscoverer *d = job->discoverer;
        job->discoverer = NULL;
        g_signal_handlers_disconnect_by_data (d, job);
        gst_discoverer_stop (d);
        g_object_unref (d);
    }
}

static gboolean
info_cancel_idle (gpointer data)
{
    InfoJob *job = data;
    if (!job->done) {
        job->done = TRUE;
        info_job_stop (job);
        media_debug ("metadata cancelled: %s", job->uri);
        info_job_unref (job);   /* the discoverer's reference */
    }
    info_job_unref (job);       /* this idle's reference */
    return G_SOURCE_REMOVE;
}

static void
info_cancelled (GCancellable *cancel, gpointer data)
{
    (void) cancel;
    /* g_cancellable_disconnect () may not run inside this handler */
    g_idle_add (info_cancel_idle, info_job_ref (data));
}

static void
add_row (DakkarMediaInfo *info, const gchar *key, gchar *value)   /* takes value */
{
    if (value == NULL || *value == '\0') { g_free (value); return; }
    g_ptr_array_add (info->keys, g_strdup (key));
    g_ptr_array_add (info->values, value);
}

static gchar *
tag_string (const GstTagList *tags, const gchar *tag)
{
    gchar *value = NULL;
    if (tags != NULL && gst_tag_list_get_string (tags, tag, &value)) {
        g_strstrip (value);
        if (*value != '\0') return value;
    }
    g_free (value);
    return NULL;
}

static gchar *
tag_count (const GstTagList *tags, const gchar *number_tag, const gchar *count_tag)
{
    guint n = 0, total = 0;
    if (tags == NULL || !gst_tag_list_get_uint (tags, number_tag, &n) || n == 0) return NULL;
    if (gst_tag_list_get_uint (tags, count_tag, &total) && total >= n)
        /* Translators: track or disc number, e.g. "3 of 12" */
        return g_strdup_printf (_("%u of %u"), n, total);
    return g_strdup_printf ("%u", n);
}

static gchar *
tag_date (const GstTagList *tags)
{
    GstDateTime *dt = NULL;
    GDate *date = NULL;
    gchar *text = NULL;
    if (tags == NULL) return NULL;
    if (gst_tag_list_get_date_time (tags, GST_TAG_DATE_TIME, &dt) && dt != NULL) {
        if (gst_date_time_has_day (dt))
            text = g_strdup_printf ("%04d-%02d-%02d", gst_date_time_get_year (dt),
                                    gst_date_time_get_month (dt), gst_date_time_get_day (dt));
        else if (gst_date_time_has_year (dt))
            text = g_strdup_printf ("%04d", gst_date_time_get_year (dt));
        gst_date_time_unref (dt);
    } else if (gst_tag_list_get_date (tags, GST_TAG_DATE, &date) && date != NULL) {
        if (g_date_valid (date)) text = g_strdup_printf ("%04d", g_date_get_year (date));
        g_date_free (date);
    }
    return text;
}

static gchar *
caps_codec (GstDiscovererStreamInfo *stream)
{
    GstCaps *caps = gst_discoverer_stream_info_get_caps (stream);
    gchar *text = NULL;
    if (caps != NULL) {
        text = gst_pb_utils_get_codec_description (caps);
        gst_caps_unref (caps);
    }
    return text;
}

/* Embedded art: front cover first, then any image, then preview image. */
static GBytes *
tags_image (const GstTagList *tags)
{
    GstSample *best = NULL;
    guint i, n;
    if (tags == NULL) return NULL;
    n = gst_tag_list_get_tag_size (tags, GST_TAG_IMAGE);
    for (i = 0; i < n; i++) {
        GstSample *sample = NULL;
        const GstStructure *s;
        gint type = GST_TAG_IMAGE_TYPE_UNDEFINED;
        if (!gst_tag_list_get_sample_index (tags, GST_TAG_IMAGE, i, &sample)) continue;
        s = gst_sample_get_info (sample);
        if (s != NULL) gst_structure_get_enum (s, "image-type", GST_TYPE_TAG_IMAGE_TYPE, &type);
        if (best == NULL || type == GST_TAG_IMAGE_TYPE_FRONT_COVER) {
            if (best) gst_sample_unref (best);
            best = sample;
            if (type == GST_TAG_IMAGE_TYPE_FRONT_COVER) break;
        } else {
            gst_sample_unref (sample);
        }
    }
    if (best == NULL)
        gst_tag_list_get_sample (tags, GST_TAG_PREVIEW_IMAGE, &best);
    if (best != NULL) {
        GstBuffer *buffer = gst_sample_get_buffer (best);
        GBytes *bytes = NULL;
        GstMapInfo map;
        if (buffer != NULL && gst_buffer_map (buffer, &map, GST_MAP_READ)) {
            if (map.size > 0 && map.size < 32 * 1024 * 1024)
                bytes = g_bytes_new (map.data, map.size);
            gst_buffer_unmap (buffer, &map);
        }
        gst_sample_unref (best);
        return bytes;
    }
    return NULL;
}

static void
add_tag_rows (DakkarMediaInfo *info, const GstTagList *tags)
{
    add_row (info, _("Title:"), g_strdup (info->title));
    add_row (info, _("Artist:"), tag_string (tags, GST_TAG_ARTIST));
    add_row (info, _("Album:"), tag_string (tags, GST_TAG_ALBUM));
    add_row (info, _("Album artist:"), tag_string (tags, GST_TAG_ALBUM_ARTIST));
    add_row (info, _("Track:"), tag_count (tags, GST_TAG_TRACK_NUMBER, GST_TAG_TRACK_COUNT));
    add_row (info, _("Disc:"), tag_count (tags, GST_TAG_ALBUM_VOLUME_NUMBER, GST_TAG_ALBUM_VOLUME_COUNT));
    add_row (info, _("Year:"), tag_date (tags));
    add_row (info, _("Genre:"), tag_string (tags, GST_TAG_GENRE));
    add_row (info, _("Composer:"), tag_string (tags, GST_TAG_COMPOSER));
}

static void
info_fill (InfoJob *job, GstDiscovererInfo *dinfo, GstDiscovererResult result)
{
    DakkarMediaInfo *info = job->info;
    const GstTagList *global = gst_discoverer_info_get_tags (dinfo);
    GList *audio = gst_discoverer_info_get_audio_streams (dinfo);
    GList *video = gst_discoverer_info_get_video_streams (dinfo);
    GstTagList *tags = global ? gst_tag_list_copy (global) : gst_tag_list_new_empty ();
    GstClockTime duration = gst_discoverer_info_get_duration (dinfo);
    GList *l;

    /* stream tags fill what the container did not provide */
    for (l = audio; l != NULL; l = l->next) {
        const GstTagList *st = gst_discoverer_stream_info_get_tags (l->data);
        if (st != NULL) gst_tag_list_insert (tags, st, GST_TAG_MERGE_KEEP);
    }
    info->has_audio = audio != NULL;
    info->has_video = video != NULL;
    info->title = tag_string (tags, GST_TAG_TITLE);
    job->embedded = tags_image (tags);
    if (job->embedded == NULL) {
        for (l = video; l != NULL && job->embedded == NULL; l = l->next) {
            /* attached pictures of audio files show up as an image "video" stream */
            const GstTagList *st = gst_discoverer_stream_info_get_tags (l->data);
            job->embedded = tags_image (st);
        }
    }
    if (info->has_video) {
        /* A single still frame is album art, not a video. */
        GstDiscovererVideoInfo *v = video->data;
        if (gst_discoverer_video_info_is_image (v)) info->has_video = FALSE;
    }

    add_tag_rows (info, tags);
    if (GST_CLOCK_TIME_IS_VALID (duration) && duration > 0)
        info->duration = (gint64) duration;
    if (info->duration > 0)
        add_row (info, _("Duration:"), format_time ((gint64) duration));
    if (info->has_video) {
        GstDiscovererVideoInfo *v = video->data;
        guint w = gst_discoverer_video_info_get_width (v), h = gst_discoverer_video_info_get_height (v);
        guint fn = gst_discoverer_video_info_get_framerate_num (v);
        guint fd = gst_discoverer_video_info_get_framerate_denom (v);
        if (w > 0 && h > 0)
            add_row (info, _("Resolution:"), g_strdup_printf ("%u × %u", w, h));
        if (fn > 0 && fd > 0)
            /* Translators: frames per second */
            add_row (info, _("Frame rate:"), g_strdup_printf (_("%.3g fps"), (double) fn / fd));
        add_row (info, _("Video codec:"), caps_codec (video->data));
    }
    if (audio != NULL) {
        GstDiscovererAudioInfo *a = audio->data;
        guint bitrate = gst_discoverer_audio_info_get_bitrate (a);
        guint rate = gst_discoverer_audio_info_get_sample_rate (a);
        guint channels = gst_discoverer_audio_info_get_channels (a);
        gchar *codec = tag_string (tags, GST_TAG_AUDIO_CODEC);
        if (codec == NULL) codec = caps_codec (audio->data);
        add_row (info, info->has_video ? _("Audio codec:") : _("Codec:"), codec);
        if (bitrate == 0) gst_tag_list_get_uint (tags, GST_TAG_BITRATE, &bitrate);
        if (bitrate == 0) gst_tag_list_get_uint (tags, GST_TAG_NOMINAL_BITRATE, &bitrate);
        if (bitrate == 0) bitrate = gst_discoverer_audio_info_get_max_bitrate (a);
        if (bitrate > 0)
            /* Translators: bit rate in kilobits per second */
            add_row (info, _("Bitrate:"), g_strdup_printf (_("%u kbit/s"), (bitrate + 500) / 1000));
        if (rate > 0)
            add_row (info, _("Sample rate:"), g_strdup_printf (_("%.1f kHz"), rate / 1000.0));
        if (channels > 0)
            add_row (info, _("Channels:"),
                     channels == 1 ? g_strdup (_("Mono")) :
                     channels == 2 ? g_strdup (_("Stereo")) : g_strdup_printf ("%u", channels));
    }
    if (result == GST_DISCOVERER_MISSING_PLUGINS) {
        const gchar **details = gst_discoverer_info_get_missing_elements_installer_details (dinfo);
        gchar *what = NULL;
        if (details != NULL && details[0] != NULL) {
            /* "gstreamer|1.0|app|description|detail" */
            gchar **parts = g_strsplit (details[0], "|", 5);
            if (g_strv_length (parts) >= 4) what = g_strdup (parts[3]);
            g_strfreev (parts);
        }
        info->problem = what ? g_strdup_printf (_("Missing codec: %s"), what)
                             : g_strdup (_("A codec needed for this file is not installed."));
        g_free (what);
        /* Without the decoder the discoverer may return no tags at all. */
        if (info->keys->len == 0 || info->title == NULL) job->need_tags = TRUE;
    } else if (result != GST_DISCOVERER_OK && info->keys->len == 0) {
        info->problem = g_strdup (_("Could not read this media file."));
    }
    gst_tag_list_unref (tags);
    gst_discoverer_stream_info_list_free (audio);
    gst_discoverer_stream_info_list_free (video);
}

static void
loader_size_prepared (GdkPixbufLoader *loader, gint width, gint height, gpointer data)
{
    gint size = GPOINTER_TO_INT (data);
    if (width > size || height > size) {
        gdouble scale = MIN ((gdouble) size / width, (gdouble) size / height);
        gdk_pixbuf_loader_set_size (loader, MAX (1, (gint) (width * scale)),
                                    MAX (1, (gint) (height * scale)));
    }
}

static GdkPixbuf *
decode_image (const guint8 *data, gsize size, gint max)
{
    GdkPixbufLoader *loader = gdk_pixbuf_loader_new ();
    GdkPixbuf *pixbuf = NULL;
    g_signal_connect (loader, "size-prepared", G_CALLBACK (loader_size_prepared), GINT_TO_POINTER (max));
    if (gdk_pixbuf_loader_write (loader, data, size, NULL) && gdk_pixbuf_loader_close (loader, NULL)) {
        pixbuf = gdk_pixbuf_loader_get_pixbuf (loader);
        if (pixbuf) {
            GdkPixbuf *rotated = gdk_pixbuf_apply_embedded_orientation (pixbuf);
            pixbuf = rotated ? rotated : g_object_ref (pixbuf);
        }
    } else {
        gdk_pixbuf_loader_close (loader, NULL);
    }
    g_object_unref (loader);
    return pixbuf;
}

static void
parse_pad_added (GstElement *parse, GstPad *pad, gpointer data)
{
    GstElement *pipeline = data;
    GstElement *sink = gst_element_factory_make ("fakesink", NULL);
    GstPad *sinkpad;
    (void) parse;
    if (sink == NULL) return;
    gst_bin_add (GST_BIN (pipeline), sink);
    gst_element_sync_state_with_parent (sink);
    sinkpad = gst_element_get_static_pad (sink, "sink");
    gst_pad_link (pad, sinkpad);
    gst_object_unref (sinkpad);
}

/* Tags (and embedded art) through parsers only: works without decoders. */
static GstTagList *
parse_tags (const gchar *uri, gint64 *duration, GCancellable *cancel)
{
    GstElement *pipeline = gst_pipeline_new (NULL);
    GstElement *src = gst_element_make_from_uri (GST_URI_SRC, uri, NULL, NULL);
    GstElement *parse = gst_element_factory_make ("parsebin", NULL);
    GstTagList *tags = NULL;
    GstBus *bus;
    gint64 end = g_get_monotonic_time () + 4 * G_USEC_PER_SEC;
    if (pipeline == NULL || src == NULL || parse == NULL) {
        if (pipeline) gst_object_unref (pipeline);
        if (src) gst_object_unref (src);
        if (parse) gst_object_unref (parse);
        return NULL;
    }
    gst_bin_add_many (GST_BIN (pipeline), src, parse, NULL);
    gst_element_link (src, parse);
    g_signal_connect (parse, "pad-added", G_CALLBACK (parse_pad_added), pipeline);
    tags = gst_tag_list_new_empty ();
    bus = gst_element_get_bus (pipeline);
    if (gst_element_set_state (pipeline, GST_STATE_PAUSED) != GST_STATE_CHANGE_FAILURE) {
        while (g_get_monotonic_time () < end && !g_cancellable_is_cancelled (cancel)) {
            GstMessage *msg = gst_bus_timed_pop_filtered (bus, 200 * GST_MSECOND,
                GST_MESSAGE_TAG | GST_MESSAGE_ASYNC_DONE | GST_MESSAGE_ERROR | GST_MESSAGE_EOS);
            gboolean stop = FALSE;
            if (msg == NULL) continue;
            if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_TAG) {
                GstTagList *t = NULL;
                gst_message_parse_tag (msg, &t);
                if (t != NULL) {
                    gst_tag_list_insert (tags, t, GST_TAG_MERGE_KEEP);
                    gst_tag_list_unref (t);
                }
            } else {
                if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_ASYNC_DONE && duration != NULL)
                    gst_element_query_duration (pipeline, GST_FORMAT_TIME, duration);
                stop = TRUE;
            }
            gst_message_unref (msg);
            if (stop) break;
        }
    }
    gst_element_set_state (pipeline, GST_STATE_NULL);
    gst_object_unref (bus);
    gst_object_unref (pipeline);
    return tags;
}

/* Worker thread: decode the embedded image, or look for folder art. */
static void
cover_thread (GTask *task, gpointer source, gpointer data, GCancellable *cancel)
{
    InfoJob *job = data;
    GdkPixbuf *pixbuf = NULL;
    (void) source;
    if (job->need_tags && !g_cancellable_is_cancelled (cancel)) {
        job->fallback_tags = parse_tags (job->uri, &job->fallback_duration, cancel);
        if (job->embedded == NULL && job->fallback_tags != NULL)
            job->embedded = tags_image (job->fallback_tags);
    }
    if (job->embedded != NULL) {
        gsize size = 0;
        const guint8 *bytes = g_bytes_get_data (job->embedded, &size);
        pixbuf = decode_image (bytes, size, job->cover_size);
        if (pixbuf != NULL) job->info->cover_embedded = TRUE;
    }
    if (pixbuf == NULL && !g_cancellable_is_cancelled (cancel)) {
        static const gchar *names[] = { "cover.jpg", "folder.jpg", "front.jpg", "cover.png",
                                        "folder.png", "Cover.jpg", "Folder.jpg", "AlbumArt.jpg",
                                        "COVER.JPG", "FOLDER.JPG", NULL };
        GFile *file = g_file_new_for_uri (job->uri);
        gchar *path = g_file_get_path (file);
        gchar *dir = path ? g_path_get_dirname (path) : NULL;
        guint i;
        for (i = 0; dir != NULL && names[i] != NULL && pixbuf == NULL; i++) {
            gchar *candidate = g_build_filename (dir, names[i], NULL);
            if (g_file_test (candidate, G_FILE_TEST_IS_REGULAR))
                pixbuf = gdk_pixbuf_new_from_file_at_scale (candidate, job->cover_size,
                                                            job->cover_size, TRUE, NULL);
            g_free (candidate);
        }
        g_free (dir);
        g_free (path);
        g_object_unref (file);
    }
    g_task_return_pointer (task, pixbuf, pixbuf ? g_object_unref : NULL);
}

static void
cover_done (GObject *source, GAsyncResult *res, gpointer data)
{
    InfoJob *job = data;
    GdkPixbuf *pixbuf = g_task_propagate_pointer (G_TASK (res), NULL);
    (void) source;
    job->info->cover = pixbuf;
    if (job->fallback_tags != NULL && !gst_tag_list_is_empty (job->fallback_tags)) {
        /* tag rows first, then whatever the discoverer had */
        DakkarMediaInfo *info = job->info;
        GPtrArray *keys = info->keys, *values = info->values;
        guint i;
        info->keys = g_ptr_array_new_with_free_func (g_free);
        info->values = g_ptr_array_new_with_free_func (g_free);
        if (info->title == NULL) info->title = tag_string (job->fallback_tags, GST_TAG_TITLE);
        add_tag_rows (info, job->fallback_tags);
        if (info->duration <= 0 && job->fallback_duration > 0) {
            info->duration = job->fallback_duration;
            add_row (info, _("Duration:"), format_time (info->duration));
        }
        if (keys->len == 0) {
            gchar *codec = tag_string (job->fallback_tags, GST_TAG_AUDIO_CODEC);
            guint bitrate = 0;
            add_row (info, _("Codec:"), codec);
            if (gst_tag_list_get_uint (job->fallback_tags, GST_TAG_BITRATE, &bitrate) && bitrate > 0)
                add_row (info, _("Bitrate:"), g_strdup_printf (_("%u kbit/s"), (bitrate + 500) / 1000));
        }
        for (i = 0; i < keys->len; i++) {
            g_ptr_array_add (info->keys, g_strdup (g_ptr_array_index (keys, i)));
            g_ptr_array_add (info->values, g_strdup (g_ptr_array_index (values, i)));
        }
        g_ptr_array_unref (keys);
        g_ptr_array_unref (values);
    }
    if (!g_cancellable_is_cancelled (job->cancel)) {
        media_debug ("metadata ready: %s (%u rows, cover %s)", job->uri, job->info->keys->len,
                     pixbuf == NULL ? "none" : job->info->cover_embedded ? "embedded" : "folder");
        job->func (job->info, job->data);
    }
    info_job_unref (job);
}

static void
info_discovered (GstDiscoverer *d, GstDiscovererInfo *dinfo, GError *error, gpointer data)
{
    InfoJob *job = data;
    GstDiscovererResult result = dinfo ? gst_discoverer_info_get_result (dinfo) : GST_DISCOVERER_ERROR;
    (void) d;
    (void) error;
    if (job->done) return;
    job->done = TRUE;
    if (!g_cancellable_is_cancelled (job->cancel) && dinfo != NULL)
        info_fill (job, dinfo, result);
    else if (dinfo == NULL)
        job->info->problem = g_strdup (_("Could not read this media file."));
    info_job_stop (job);
    if (g_cancellable_is_cancelled (job->cancel)) {
        info_job_unref (job);
        return;
    }
    {
        /* the discoverer's reference moves to the task */
        GTask *task = g_task_new (NULL, job->cancel, cover_done, job);
        g_task_set_task_data (task, job, NULL);
        g_task_run_in_thread (task, cover_thread);
        g_object_unref (task);
    }
}

void
dakkar_media_info_load (const char *uri, gint cover_size, GCancellable *cancel,
                       DakkarMediaInfoFunc func, gpointer user_data, GDestroyNotify notify)
{
    InfoJob *job;
    GError *error = NULL;
    g_return_if_fail (uri != NULL && func != NULL);
    gst_init_check (NULL, NULL, NULL);
    job = g_new0 (InfoJob, 1);
    job->ref = 1;
    job->uri = g_strdup (uri);
    job->cover_size = MAX (cover_size, 32);
    job->cancel = cancel ? g_object_ref (cancel) : g_cancellable_new ();
    job->func = func;
    job->data = user_data;
    job->notify = notify;
    job->info = g_new0 (DakkarMediaInfo, 1);
    job->info->keys = g_ptr_array_new_with_free_func (g_free);
    job->info->values = g_ptr_array_new_with_free_func (g_free);
    job->discoverer = gst_discoverer_new (10 * GST_SECOND, &error);
    if (job->discoverer == NULL) {
        g_clear_error (&error);
        job->done = TRUE;
        job->info->problem = g_strdup (_("Could not read this media file."));
        if (!g_cancellable_is_cancelled (job->cancel)) func (job->info, user_data);
        info_job_unref (job);
        return;
    }
    g_signal_connect (job->discoverer, "discovered", G_CALLBACK (info_discovered), job);
    job->cancel_id = g_cancellable_connect (job->cancel, G_CALLBACK (info_cancelled), job, NULL);
    if (g_cancellable_is_cancelled (job->cancel)) return;   /* the idle cleans up */
    gst_discoverer_start (job->discoverer);
    if (!gst_discoverer_discover_uri_async (job->discoverer, uri)) {
        job->done = TRUE;
        info_job_stop (job);
        info_job_unref (job);
    }
}

/* ------------------------------------------------------------------ */
/* key/value grid                                                       */
/* ------------------------------------------------------------------ */

GtkWidget *
dakkar_media_grid_new (void)
{
    GtkWidget *grid = gtk_grid_new ();
    gtk_grid_set_column_spacing (GTK_GRID (grid), 6);
    gtk_grid_set_row_spacing (GTK_GRID (grid), 1);
    gtk_widget_set_halign (grid, GTK_ALIGN_CENTER);
    gtk_widget_set_name (grid, "dakkar-media-info");
    gtk_widget_set_no_show_all (grid, TRUE);
    return grid;
}

void
dakkar_media_grid_fill (GtkWidget *grid, DakkarMediaInfo *info)
{
    GList *children = gtk_container_get_children (GTK_CONTAINER (grid)), *l;
    guint i, row = 0;
    for (l = children; l != NULL; l = l->next) gtk_widget_destroy (l->data);
    g_list_free (children);
    if (info == NULL) { gtk_widget_hide (grid); return; }
    for (i = 0; i < info->keys->len; i++) {
        GtkWidget *key = gtk_label_new (g_ptr_array_index (info->keys, i));
        GtkWidget *value = gtk_label_new (g_ptr_array_index (info->values, i));
        gtk_label_set_xalign (GTK_LABEL (key), 1.0);
        gtk_label_set_yalign (GTK_LABEL (key), 0.0);
        gtk_style_context_add_class (gtk_widget_get_style_context (key), "dim-label");
        gtk_style_context_add_class (gtk_widget_get_style_context (key), "miller-preview-meta");
        gtk_style_context_add_class (gtk_widget_get_style_context (value), "miller-preview-meta");
        gtk_label_set_xalign (GTK_LABEL (value), 0.0);
        gtk_label_set_line_wrap (GTK_LABEL (value), TRUE);
        gtk_label_set_line_wrap_mode (GTK_LABEL (value), PANGO_WRAP_WORD_CHAR);
        gtk_label_set_max_width_chars (GTK_LABEL (value), 22);
        gtk_label_set_width_chars (GTK_LABEL (value), 1);
        gtk_label_set_selectable (GTK_LABEL (value), TRUE);
        gtk_widget_set_hexpand (value, TRUE);
        gtk_grid_attach (GTK_GRID (grid), key, 0, row, 1, 1);
        gtk_grid_attach (GTK_GRID (grid), value, 1, row, 1, 1);
        gtk_widget_show (key);
        gtk_widget_show (value);
        row++;
    }
    gtk_widget_set_visible (grid, row > 0);
}
