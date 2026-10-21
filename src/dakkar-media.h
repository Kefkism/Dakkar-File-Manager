/* Dakkar: audio/video preview for the details sidebar of every view and
 * for Quick Look.  Metadata and album art are read with GstDiscoverer
 * (asynchronous, cancellable, no playback); the player is a playbin with
 * play/pause, seek bar, elapsed/total time and volume/mute.
 * GPL-2.0-or-later. */
#ifndef DAKKAR_MEDIA_H
#define DAKKAR_MEDIA_H

#include <gtk/gtk.h>

typedef enum {
    DAKKAR_PLAYER_VIDEO,   /* video frame, muted at start, speaker toggle */
    DAKKAR_PLAYER_AUDIO    /* controls only: play, seek, time, mute, volume */
} DakkarPlayerMode;

/* TRUE for audio files that the preview handles (not playlists). */
gboolean dakkar_media_is_audio_mime (const char *mime);
/* TRUE when playbin and the sinks the player needs are installed. */
gboolean dakkar_media_supported (void);

GtkWidget *dakkar_player_new (DakkarPlayerMode mode, gint width);
/* Loads (paused, first frame for video) or stops with NULL.  Every new
 * file starts muted for video and unmuted for audio. */
void       dakkar_player_set_uri (GtkWidget *player, const char *uri);
void       dakkar_player_toggle (GtkWidget *player);
gboolean   dakkar_player_is_active (GtkWidget *player);
gboolean   dakkar_player_is_playing (GtkWidget *player);
gboolean   dakkar_player_get_muted (GtkWidget *player);
void       dakkar_player_set_muted (GtkWidget *player, gboolean muted);
/* Duration from the metadata until the pipeline knows it (ns). */
void       dakkar_player_set_duration_hint (GtkWidget *player, gint64 duration);
/* Shows a message under the controls (e.g. missing codec); NULL hides. */
void       dakkar_player_set_message (GtkWidget *player, const char *message);

/* Metadata read without playback. */
typedef struct {
    GdkPixbuf *cover;          /* embedded art, else cover.jpg/folder.jpg, else NULL */
    gboolean cover_embedded;
    GPtrArray *keys;           /* translated labels, only rows that exist */
    GPtrArray *values;
    gchar *title;              /* tag title, or NULL */
    gchar *problem;            /* e.g. missing decoder; NULL when fine */
    gboolean has_video;
    gboolean has_audio;
    gint64 duration;           /* ns, 0 when unknown */
} DakkarMediaInfo;

typedef void (*DakkarMediaInfoFunc) (DakkarMediaInfo *info, gpointer user_data);

/* Calls func on the main loop unless cancel was cancelled first; info is
 * freed after func returns.  Never blocks the UI. */
void dakkar_media_info_load (const char *uri, gint cover_size, GCancellable *cancel,
                            DakkarMediaInfoFunc func, gpointer user_data,
                            GDestroyNotify notify);

/* Two-column key/value grid for the sidebar and Quick Look (rows only;
 * show info->problem with dakkar_player_set_message ()). */
GtkWidget *dakkar_media_grid_new (void);
void       dakkar_media_grid_fill (GtkWidget *grid, DakkarMediaInfo *info);  /* NULL clears */

#endif
