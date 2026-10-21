/* Shared, live corner styling for Files widgets (GTK 3). */
#ifndef NEMO_CORNER_STYLE_H
#define NEMO_CORNER_STYLE_H

#include <gtk/gtk.h>

/* Installs exactly one provider per process and tracks GSettings live. */
void nemo_corner_style_ensure (void);

/* Rounded outer corners for GTK3 child surfaces (input-transparent mask).
 * background_color is a GTK theme CSS named color, e.g. "theme_bg_color". */
GtkWidget *nemo_corner_style_wrap (GtkWidget *child,
                                   const gchar *background_color);

/* The secondary selection / hover accent of the custom palette: the
 * dakkar-theme-accent-secondary setting, or when that is empty a dimmer
 * variant of the selection accent (mixed towards the content background).
 * Used for unfocused selections, inactive Miller columns and the hover and
 * pressed states of buttons.  Always fills @out. */
void dakkar_theme_secondary_accent (GdkRGBA *out);

/* Any palette colour, with the derivations of the optional ones (Quick
 * Actions button/text/hover/border, statusbar, secondary accent): an
 * empty setting follows the colour it is derived from. */
void dakkar_theme_color (const gchar *key, GdkRGBA *out);
gboolean dakkar_theme_color_is_automatic (const gchar *key);

/* Tags a Dakkar dialog (preferences, transfers, icon picker, properties)
 * so that it follows the custom Dakkar colours when they are enabled. */
void dakkar_dialog_style_attach (GtkWidget *toplevel);

/* Private icon theme for the Places sidebar icon pack (NULL = system). */
GtkIconTheme *dakkar_sidebar_icon_theme (void);

/* Icon names that exist in the user's icon theme.  Breeze, Adwaita,
 * Papirus ... do not share one naming set (Breeze has no
 * view-compact-symbolic, Adwaita no view-file-columns-symbolic), so a
 * single hardcoded name rendered a generic, oversized colour or "missing
 * image" icon in some themes.  dakkar_icon_pick returns the first candidate
 * the current default icon theme (including its inherited and fallback
 * themes) provides, else the last candidate.  The list ends with NULL.
 * The returned string is one of the arguments (not a copy). */
const gchar *dakkar_icon_pick (const gchar *first, ...) G_GNUC_NULL_TERMINATED;

/* Same, for one well-known name: maps names that are missing from common
 * themes (view-compare-symbolic, git-symbolic, ...) to themed equivalents.
 * Returns @name itself when it exists or no alternative is known. */
const gchar *dakkar_icon_resolve (const gchar *name);

/* Distinct themed icon per view mode (Icons/List/Compact/Miller), keyed by
 * the NEMO_ACTION_*_VIEW action name. */
const gchar *dakkar_view_icon_name (const gchar *view_action);

/* Real window transparency.  attach() gives a toplevel an RGBA visual
 * (before it is realized) and keeps its "dakkar-transparent" class and
 * whole-window opacity in sync with the settings and the compositor. */
void dakkar_transparency_attach (GtkWindow *window);
/* The screen has an RGBA visual and a running compositor. */
gboolean dakkar_transparency_supported (GdkScreen *screen);
/* This window is drawn see-through right now. */
gboolean dakkar_transparency_active (GtkWidget *window);
/* The see-through window background around the surfaces of @widget's
 * window; FALSE when that window is not see-through. */
gboolean dakkar_transparency_gap_color (GtkWidget *widget, GdkRGBA *color);

#endif /* NEMO_CORNER_STYLE_H */
