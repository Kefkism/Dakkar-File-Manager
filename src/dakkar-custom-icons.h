/* Dakkar: Change Icon / Reset Icon / icon colour for items in the views.
 *
 * Storage
 *  - one folder or file:  GIO metadata  metadata::custom-icon (file URI of an
 *    image) or metadata::custom-icon-name (an icon name of the active icon
 *    theme).  Nemo, Nautilus and other GIO programs honour these keys.  An
 *    icon of an icon pack that is not the active theme is stored as the URI
 *    of the resolved image file.
 *  - a whole file type:  nemo_preferences "dakkar-type-icons" (MIME type ->
 *    icon), applied by Dakkar itself, and exported for other programs as a
 *    user shared-mime-info package ~/.local/share/mime/packages/
 *    dakkar-custom-icons.xml with <icon name="dakkar-custom-TYPE"/>, the image
 *    copied to ~/.local/share/icons/hicolor/{scalable,256x256}/mimetypes/,
 *    then update-mime-database ~/.local/share/mime (when installed).
 */
#ifndef DAKKAR_CUSTOM_ICONS_H
#define DAKKAR_CUSTOM_ICONS_H

#include <gtk/gtk.h>

/* @files: GList of NemoFile */
void     dakkar_custom_icons_change       (GtkWindow *parent, GList *files);
void     dakkar_custom_icons_reset        (GtkWindow *parent, GList *files);
gboolean dakkar_custom_icons_any          (GList *files);
void     dakkar_custom_icons_set_color    (GtkWindow *parent, GList *folders);
void     dakkar_custom_icons_reset_color  (GList *folders);
gboolean dakkar_custom_icons_any_color    (GList *folders);

/* The folder's own custom icon (metadata) as an icon-picker spec, or NULL.
 * Used by the sidebar for bookmarks of that folder. */
gchar   *dakkar_custom_icons_spec_for_metadata (const gchar *icon_uri, const gchar *icon_name);

/* TRUE when a per-type icon may be set for @mime (not unknown/octet-stream). */
gboolean dakkar_custom_icons_type_allowed (const gchar *mime);

/* Rewrites the exported mime package and icons from the settings map. */
void     dakkar_custom_icons_export       (void);

#endif
