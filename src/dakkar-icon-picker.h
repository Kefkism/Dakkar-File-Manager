/* Dakkar: icon picker for sidebar items (GTK 3).
 *
 * A spec string describes a chosen icon:
 *   "theme:<theme dir>|<icon name>|<resolved file>"  an icon of one installed
 *        icon theme; it renders even when that theme is not the active one
 *   "file:<absolute path>"                         a custom PNG/SVG image
 */
#ifndef DAKKAR_ICON_PICKER_H
#define DAKKAR_ICON_PICKER_H

#include <gtk/gtk.h>

/* Runs the modal picker. Returns a newly allocated spec, or NULL on Cancel. */
gchar *dakkar_icon_picker_run (GtkWindow *parent, const gchar *title,
                              const gchar *current_spec);

/* As above, with a scope choice for files when @type_choice and
 * @single_choice are set ("All PDF document files" / "Only this file").
 * *inout_for_type is the default on entry and the choice on Apply.
 * With !@type_allowed the type choice is shown insensitive. */
gchar *dakkar_icon_picker_run_scoped (GtkWindow *parent, const gchar *title,
                                     const gchar *current_spec,
                                     const gchar *type_choice, const gchar *single_choice,
                                     gboolean type_allowed, const gchar *type_blocked_reason,
                                     gboolean *inout_for_type);

/* Splits a spec; theme/name are NULL for "file:", path may be NULL for "theme:". */
gboolean dakkar_icon_spec_parse (const gchar *spec, gchar **theme, gchar **name, gchar **path);

/* TRUE when @spec is well-formed (it may still point to a missing file). */
gboolean dakkar_icon_spec_is_valid (const gchar *spec);

/* Loads @spec at @size logical pixels for @scale (pixbuf is size*scale).
 * Symbolic icons are drawn in @tint, else in @context's colours, else as is.
 * Full-colour icons ignore the tint. NULL when the icon cannot be found. */
GdkPixbuf *dakkar_icon_spec_load (const gchar *spec, gint size, gint scale,
                                 GtkStyleContext *context, const GdkRGBA *tint,
                                 gboolean *out_symbolic);


#endif
