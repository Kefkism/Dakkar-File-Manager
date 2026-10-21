/* Shared Details metadata presentation. No file model or file operations. */
#ifndef DAKKAR_INSPECTOR_LAYOUT_H
#define DAKKAR_INSPECTOR_LAYOUT_H

/* Keep this reusable header independent of each caller's include order. */
#include <gtk/gtk.h>
#include <libnemo-private/nemo-global-preferences.h>

static const gchar * const dakkar_details_field_ids[] = {
    "name", "type", "size", "modified", "path", "owner", NULL
};

static gboolean
dakkar_details_has_field (gchar **saved, const gchar *id)
{
    guint i;
    for (i = 0; saved && saved[i]; ++i)
        if (g_str_equal (saved[i], id)) return TRUE;
    return FALSE;
}

/* widgets must be the six labels owned by the supplied metadata box.
 * The preview image/video is packed before them; sorting never reparents
 * any file item or alters the directory selection. */
static void
dakkar_details_apply_fields (GtkWidget *box, GtkWidget **widgets,
                            guint first_position)
{
    gchar **order = g_settings_get_strv (nemo_preferences,
                                        "dakkar-inspector-fields");
    guint i, placed = 0;
    for (i = 0; order[i]; ++i) {
        guint j;
        for (j = 0; dakkar_details_field_ids[j]; ++j) {
            if (g_str_equal (order[i], dakkar_details_field_ids[j]) &&
                widgets[j] && gtk_widget_get_parent (widgets[j]) == box) {
                gtk_box_reorder_child (GTK_BOX (box), widgets[j],
                                       (gint)(first_position + placed));
                ++placed;
                break;
            }
        }
    }
    for (i = 0; dakkar_details_field_ids[i]; ++i) {
        if (widgets[i]) {
            gboolean show = dakkar_details_has_field (order,
                                                     dakkar_details_field_ids[i]);
            if (!show) gtk_widget_hide (widgets[i]);
            else gtk_widget_show (widgets[i]);
        }
    }
    g_strfreev (order);
}

/* Miller and the other Nemo views must share the very same narrow-width
 * metadata behaviour.  GtkLabel's natural text width must never become a
 * minimum width for the inspector: it has to shrink and ellipsize inside the
 * currently allocated GtkPaned child instead of being horizontally clipped.
 * The path is displayed with its end visible, just as in the Miller inspector. */
static void
dakkar_details_adapt_metadata (GtkWidget **labels, guint count,
                              gint width, guint path_index)
{
    guint i;
    gboolean narrow = width < 160;

    if (width < 1) return;
    for (i = 0; i < count; i++) {
        GtkWidget *widget = labels[i];
        if (!GTK_IS_LABEL (widget)) continue;
        gtk_widget_set_halign (widget, GTK_ALIGN_FILL);
        gtk_widget_set_hexpand (widget, FALSE);
        gtk_label_set_xalign (GTK_LABEL (widget), 0.5f);
        /* width-chars, unlike max-width-chars, establishes the natural
         * requisition.  One char allows the surrounding panel to get down
         * to its 92px minimum even with very long paths/filenames. */
        gtk_label_set_width_chars (GTK_LABEL (widget), 1);
        gtk_label_set_max_width_chars (GTK_LABEL (widget), narrow ? 9 : 28);
        gtk_label_set_line_wrap (GTK_LABEL (widget), !narrow);
        gtk_label_set_ellipsize (GTK_LABEL (widget),
            i == path_index ? PANGO_ELLIPSIZE_START : PANGO_ELLIPSIZE_END);
    }
}

/* A GtkScrolledWindow with horizontal scrolling disabled can still request
 * its child's natural width; GTK will then *clip* the child rather than
 * relayout it.  These constraints are used by both inspectorial layouts. */
static void
dakkar_details_make_scroller_shrinkable (GtkWidget *scroller)
{
    g_return_if_fail (GTK_IS_SCROLLED_WINDOW (scroller));
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroller),
                                    GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_min_content_width (GTK_SCROLLED_WINDOW (scroller), 1);
    gtk_widget_set_hexpand (scroller, TRUE);
    gtk_widget_set_halign (scroller, GTK_ALIGN_FILL);
}

/* A paned widget can report position changes during its first allocation.
 * Ignore these so a hidden panel never overwrites saved preferences. */
static gint
dakkar_inspector_saved_int (const char *key, gint min, gint max)
{
    return CLAMP (g_settings_get_int (nemo_preferences, key), min, max);
}
#endif
