/* Dakkar Favorites: one ordered list for the sidebar "Favorites" section.
 *
 * The section shows XApp favorites, the standard XDG folders and the GTK
 * bookmarks.  Each collection keeps its own storage; Dakkar only stores the
 * display order (org.dakkar.window-state dakkar-favorite-order, URIs) and the
 * standard folders the user removed (dakkar-favorite-hidden).
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef DAKKAR_FAVORITES_H
#define DAKKAR_FAVORITES_H

#include <glib.h>

typedef enum {
    DAKKAR_FAVORITE_XAPP,       /* XApp favorite (file or folder) */
    DAKKAR_FAVORITE_XDG,        /* standard folder: Desktop, Downloads, ... */
    DAKKAR_FAVORITE_BOOKMARK    /* GTK bookmark (~/.config/gtk-3.0/bookmarks) */
} DakkarFavoriteKind;

typedef struct {
    DakkarFavoriteKind kind;
    gchar *uri;
    gchar *name;
    gchar *icon_name;
    gchar *tooltip;
    gint   bookmark_index;      /* index in the bookmark list, else -1 */
} DakkarFavorite;

void    dakkar_favorite_free          (DakkarFavorite *favorite);
/* All Favorites in display order (deduplicated by URI). */
GList  *dakkar_favorites_collect      (void);
/* Store a new display order (NULL-terminated URI array). */
void    dakkar_favorites_set_order    (const gchar * const *uris);
/* Move @uri by @delta places (-1 up, +1 down).  Returns TRUE if moved. */
gboolean dakkar_favorites_move        (const gchar *uri, gint delta);
/* Move @uri so it sits before (or after) @target_uri. */
gboolean dakkar_favorites_move_to     (const gchar *uri, const gchar *target_uri,
                                       gboolean after);
/* Remove @uri from Favorites (XApp, bookmark, or hide a standard folder). */
void    dakkar_favorites_remove       (const gchar *uri);

/* Preferences > Favorites page: list (icon, name, path), drag and drop,
 * Move Up / Move Down / Remove.  Changes apply live and persist. */
#include <gtk/gtk.h>
GtkWidget *dakkar_favorites_preferences_page (void);

#endif
