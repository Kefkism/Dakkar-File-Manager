/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* nemo-statusbar.c
 * 
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street - Suite 500,
 * Boston, MA 02110-1335, USA.
 */

#include "nemo-statusbar.h"
#include "nemo-transfer-window.h"

#include "nemo-actions.h"
#include "nemo-file-management-properties.h"
#include "nemo-miller-columns-view.h"
#include "nemo-progress-ui-handler.h"
#include <libnemo-private/nemo-global-preferences.h>

#include <config.h>
#include <math.h>
#include <glib/gi18n.h>

enum {
        LAST_SIGNAL
};

enum {
    PROP_WINDOW = 1,
    NUM_PROPERTIES
};

static GParamSpec *properties[NUM_PROPERTIES] = { NULL, };

G_DEFINE_TYPE (NemoStatusBar, nemo_status_bar, GTK_TYPE_BOX);

static void
nemo_status_bar_init (NemoStatusBar *bar)
{
    bar->window = NULL;
}

static void
nemo_status_bar_set_property (GObject        *object,
                              guint           arg_id,
                              const GValue   *value,
                              GParamSpec     *pspec)
{
    NemoStatusBar *self = NEMO_STATUS_BAR (object);

    switch (arg_id) {
        case PROP_WINDOW:
            self->window = g_value_get_object (value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, arg_id, pspec);
            break;
    }
}

static void
nemo_status_bar_get_property (GObject      *object,
                              guint         arg_id,
                              GValue       *value,
                              GParamSpec   *pspec)
{
    NemoStatusBar *self = NEMO_STATUS_BAR (object);

    switch (arg_id) {
        case PROP_WINDOW:
            g_value_set_object (value, self->window);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, arg_id, pspec);
            break;
    }
}

static void
nemo_status_bar_dispose (GObject *object)
{
    NemoStatusBar *bar = NEMO_STATUS_BAR (object);

    bar->window = NULL;
    g_clear_pointer (&bar->transfer_text, g_free);

    G_OBJECT_CLASS (nemo_status_bar_parent_class)->dispose (object);
}

static void
action_show_sidebar_callback (GtkButton *button, NemoStatusBar *bar)
{
    nemo_window_show_sidebar (bar->window);
}

static void
action_hide_sidebar_callback (GtkButton *button, NemoStatusBar *bar)
{
    nemo_window_hide_sidebar (bar->window);
}

static void
sidebar_state_changed_cb (gpointer pointer, gboolean state, gpointer user_data)
{
    nemo_status_bar_sync_button_states (NEMO_STATUS_BAR (user_data));
}

static void
sidebar_type_changed_cb (gpointer pointer, const gchar *sidebar_id, gpointer user_data)
{
    nemo_status_bar_sync_button_states (NEMO_STATUS_BAR (user_data));
}

/* One context-aware button: its icon shows which mode clicking will open,
 * NOT which mode is currently active. The window owns the persistent sidebar
 * mode and emits notify::sidebar-view-id for updates from other entry points. */
static void
nemo_status_bar_sync_sidebar_mode_button (NemoStatusBar *bar)
{
    const gboolean tree_active = g_strcmp0 (
        nemo_window_get_sidebar_id (bar->window), NEMO_WINDOW_SIDEBAR_TREE) == 0;
    const gchar *icon_name = tree_active ? "folder-symbolic" : "view-list-symbolic";
    const gchar *tip = tree_active ? _("Show Places") : _("Show Tree View");
    GtkWidget *image;
    if (bar->sidebar_mode_button == NULL) return;
    image = gtk_image_new_from_icon_name (icon_name,
                                         gtk_icon_size_from_name (NEMO_STATUSBAR_ICON_SIZE_NAME));
    gtk_button_set_image (GTK_BUTTON (bar->sidebar_mode_button), image);
    gtk_widget_set_tooltip_text (bar->sidebar_mode_button, tip);
}

/* The inspector switch mirrors the LEFT sidebar's footer button.
 * Unlike the old toolbar control it stays at the bottom-right edge. The
 * same Nemo supplied symbolic icons are used with their RTL mirror shapes. */
void
nemo_status_bar_sync_inspector_button (NemoStatusBar *bar)
{
    NemoWindowSlot *slot;
    gboolean visible = FALSE;
    const gchar *icon_name;
    GtkWidget *image;

    g_return_if_fail (NEMO_IS_STATUS_BAR (bar));
    if (bar->inspector_button == NULL || !NEMO_IS_WINDOW (bar->window))
        return;
    slot = nemo_window_get_active_slot (bar->window);
    if (!NEMO_IS_WINDOW_SLOT (slot)) {
        gtk_widget_set_sensitive (bar->inspector_button, FALSE);
        return;
    }
    /* One shared setting for every view. */
    visible = nemo_window_slot_get_inspector_visible (slot);

    /* sidebar-hide is left-facing; the -rtl asset is its exact horizontal
     * mirror, with the panel strip on the RIGHT. */
    icon_name = visible ? "sidebar-hide-symbolic-rtl" :
                          "sidebar-show-symbolic-rtl";
    image = gtk_image_new_from_icon_name (icon_name,
        gtk_icon_size_from_name (NEMO_STATUSBAR_ICON_SIZE_NAME));
    gtk_button_set_image (GTK_BUTTON (bar->inspector_button), image);
    gtk_widget_set_tooltip_text (bar->inspector_button,
        visible ? _("Hide Right Panel") : _("Show Right Panel"));
    gtk_widget_set_sensitive (bar->inspector_button, TRUE);
}

static void
nemo_status_bar_toggle_inspector (GtkButton *button, NemoStatusBar *bar)
{
    NemoWindowSlot *slot = nemo_window_get_active_slot (bar->window);
    if (!NEMO_IS_WINDOW_SLOT (slot)) return;
    nemo_window_slot_set_inspector_visible (
        slot, !nemo_window_slot_get_inspector_visible (slot));
    nemo_status_bar_sync_inspector_button (bar);
    (void)button;
}

static void
nemo_status_bar_inspector_setting_changed (GSettings *settings, gchar *key,
                                            NemoStatusBar *bar)
{
    nemo_status_bar_sync_inspector_button (bar);
    (void)settings;
    (void)key;
}

static void
on_slider_changed_cb (GtkWidget *zoom_slider, gpointer user_data)
{
    NemoStatusBar *bar = NEMO_STATUS_BAR (user_data);
    gdouble val = gtk_range_get_value (GTK_RANGE (zoom_slider));

    NemoWindowSlot *slot = nemo_window_get_active_slot (bar->window);

    if (!NEMO_IS_WINDOW_SLOT (slot))
        return;

    NemoView *view = slot->content_view;

    if (!NEMO_IS_VIEW (view))
        return;

    nemo_view_zoom_to_level (view, (int) val);
}

static void files_sidebar_slider_value_changed (GtkRange *range, gpointer data);

/* The bottom-left slider ONLY scales sidebar icons, labels and headings.
 * Sidebar width remains resizeable with the normal paned divider.
 * The mode key from old experimental builds is intentionally ignored. */
static void
files_sidebar_slider_refresh (GtkWidget *slider)
{
    g_signal_handlers_block_by_func (slider,
              files_sidebar_slider_value_changed,
              g_object_get_data (G_OBJECT (slider), "files-sidebar-bar"));
    gtk_range_set_range (GTK_RANGE (slider), 0.65, 1.85);
    gtk_range_set_increments (GTK_RANGE (slider), 0.05, 0.15);
    gtk_range_set_value (GTK_RANGE (slider),
        CLAMP (g_settings_get_double (nemo_preferences,
                    NEMO_PREFERENCES_FILES_SIDEBAR_SCALE), 0.65, 1.85));
    g_signal_handlers_unblock_by_func (slider,
              files_sidebar_slider_value_changed,
              g_object_get_data (G_OBJECT (slider), "files-sidebar-bar"));
    gtk_widget_set_tooltip_text (slider, _("Sidebar icon, text and heading size"));
}

static void
files_sidebar_slider_setting_changed (GSettings *settings, gchar *key,
                                      gpointer slider)
{
    files_sidebar_slider_refresh (GTK_WIDGET (slider));
}

static void
files_sidebar_slider_value_changed (GtkRange *range, gpointer data)
{
    g_settings_set_double (nemo_preferences,
                           NEMO_PREFERENCES_FILES_SIDEBAR_SCALE,
                           gtk_range_get_value (range));
}

#define SLIDER_WIDTH 100

/* Clicking the centered compact bar reveals the original Nemo dialog. */
static void
dakkar_transfer_button_clicked (GtkButton *button, gpointer handler)
{
    nemo_progress_ui_handler_present (NEMO_PROGRESS_UI_HANDLER (handler));
    (void)button;
}

/* ---------- minimized transfer bar ----------
 * A custom-drawn bar (GtkDrawingArea inside the click-to-expand button):
 * visible trough, solid fill in the theme accent colour, caption centred on
 * top and painted twice (clipped) so it reads on filled and empty parts.
 * Colours come from the theme: the accent (@dakkar_transfer_fill from the
 * Dakkar stylesheet, else @accent_bg_color / @theme_selected_bg_color) for
 * copying and verifying alike, a desaturated accent while paused and
 * @error_color after a failure. A faint or missing accent falls back to a
 * solid blue. */

static gdouble
dakkar_luminance (const GdkRGBA *c)
{
    return 0.2126 * c->red + 0.7152 * c->green + 0.0722 * c->blue;
}

static void
dakkar_bar_color (GtkStyleContext *ctx, const gchar *name, const gchar *alt,
                 const gchar *fallback, GdkRGBA *out)
{
    if (gtk_style_context_lookup_color (ctx, name, out))
        return;
    if (alt != NULL && gtk_style_context_lookup_color (ctx, alt, out))
        return;
    gdk_rgba_parse (out, fallback);
}

static void
dakkar_bar_rounded (cairo_t *cr, double x, double y, double w, double h, double r)
{
    r = MIN (r, MIN (w, h) / 2.0);
    cairo_new_sub_path (cr);
    cairo_arc (cr, x + w - r, y + r, r, -G_PI / 2, 0);
    cairo_arc (cr, x + w - r, y + h - r, r, 0, G_PI / 2);
    cairo_arc (cr, x + r, y + h - r, r, G_PI / 2, G_PI);
    cairo_arc (cr, x + r, y + r, r, G_PI, 3 * G_PI / 2);
    cairo_close_path (cr);
}

static gboolean
dakkar_transfer_bar_draw (GtkWidget *widget, cairo_t *cr, NemoStatusBar *bar)
{
    GtkStyleContext *ctx = gtk_widget_get_style_context (widget);
    GtkStateFlags flags = gtk_style_context_get_state (ctx);
    double w = gtk_widget_get_allocated_width (widget);
    double h = gtk_widget_get_allocated_height (widget);
    double fx = 0.0, fw, radius = 4.0;
    GdkRGBA fg, bg, fill, on_fill;
    PangoLayout *layout;
    int tw, th;
    gboolean pulse = bar->transfer_fraction < 0.0;

    gtk_style_context_get_color (ctx, flags, &fg);
    dakkar_bar_color (ctx, "theme_bg_color", NULL, "#f6f5f4", &bg);
    /* One fill colour for copying AND verifying: the theme accent (or the
     * Dakkar accent when custom colours are on). The verify phase is told
     * apart by its caption and the thin checksum strip only. */
    if (!gtk_style_context_lookup_color (ctx, "dakkar_transfer_fill", &fill) &&
        !gtk_style_context_lookup_color (ctx, "accent_bg_color", &fill))
        dakkar_bar_color (ctx, "theme_selected_bg_color", NULL, "#3584e4", &fill);
    /* A theme whose accent is (nearly) transparent or has almost the same
     * brightness as the window would give a faint bar: sensible fallback. */
    if (fill.alpha < 0.6 || fabs (dakkar_luminance (&fill) - dakkar_luminance (&bg)) < 0.12)
        gdk_rgba_parse (&fill, "#3584e4");
    if (bar->transfer_state == NEMO_STATUS_BAR_TRANSFER_ERROR) {
        GdkRGBA err;
        if (gtk_style_context_lookup_color (ctx, "error_color", &err) ||
            gtk_style_context_lookup_color (ctx, "error_bg_color", &err))
            fill = err;
        else
            gdk_rgba_parse (&fill, "#c01c28");
    } else if (bar->transfer_state == NEMO_STATUS_BAR_TRANSFER_PAUSED) {
        /* paused: the same accent, desaturated towards its own grey */
        gdouble l = dakkar_luminance (&fill);
        fill.red = l + (fill.red - l) * 0.25;
        fill.green = l + (fill.green - l) * 0.25;
        fill.blue = l + (fill.blue - l) * 0.25;
    }
    fill.alpha = 1.0;
    /* Caption over the fill: the theme's selected text colour when it
     * contrasts enough with the fill, otherwise black or white. */
    if (!gtk_style_context_lookup_color (ctx, "theme_selected_fg_color", &on_fill) ||
        fabs (dakkar_luminance (&on_fill) - dakkar_luminance (&fill)) < 0.4) {
        if (dakkar_luminance (&fill) > 0.6)
            gdk_rgba_parse (&on_fill, "#1e1e1e");
        else
            gdk_rgba_parse (&on_fill, "#ffffff");
    }
    on_fill.alpha = 1.0;

    /* trough */
    dakkar_bar_rounded (cr, 0.5, 0.5, w - 1.0, h - 1.0, radius);
    if (bar->transfer_state == NEMO_STATUS_BAR_TRANSFER_ERROR)
        cairo_set_source_rgba (cr, fill.red, fill.green, fill.blue, 0.16);
    else
        cairo_set_source_rgba (cr, fg.red, fg.green, fg.blue, 0.10);
    cairo_fill_preserve (cr);
    cairo_set_source_rgba (cr, fg.red, fg.green, fg.blue, 0.38);
    cairo_set_line_width (cr, 1.0);
    cairo_stroke (cr);

    /* fill */
    if (pulse) {
        fw = MAX (w * 0.22, 24.0);
        fx = fmod (bar->transfer_pulse * 0.04 * w, w + fw) - fw;
    } else
        fw = CLAMP (bar->transfer_fraction, 0.0, 1.0) * w;
    cairo_save (cr);
    dakkar_bar_rounded (cr, 0.5, 0.5, w - 1.0, h - 1.0, radius);
    cairo_clip (cr);
    cairo_rectangle (cr, fx, 0, fw, h);
    gdk_cairo_set_source_rgba (cr, &fill);
    cairo_fill (cr);
    if (bar->transfer_state == NEMO_STATUS_BAR_TRANSFER_PAUSED) {
        /* diagonal stripes over the paused fill */
        double x;
        cairo_rectangle (cr, fx, 0, fw, h);
        cairo_clip (cr);
        cairo_set_source_rgba (cr, 1, 1, 1, 0.28);
        cairo_set_line_width (cr, 4.0);
        for (x = -h; x < fw + h; x += 10.0) {
            cairo_move_to (cr, fx + x, h);
            cairo_line_to (cr, fx + x + h, 0);
        }
        cairo_stroke (cr);
    }
    cairo_restore (cr);

    /* checksum read-back of the current file: its own thin bar */
    if (bar->transfer_secondary >= 0.0) {
        double sh = MAX (3.0, h / 6.0);
        cairo_save (cr);
        dakkar_bar_rounded (cr, 0.5, 0.5, w - 1.0, h - 1.0, radius);
        cairo_clip (cr);
        cairo_rectangle (cr, 0, h - sh, w, sh);
        cairo_set_source_rgba (cr, 0, 0, 0, 0.25);
        cairo_fill (cr);
        cairo_rectangle (cr, 0, h - sh, CLAMP (bar->transfer_secondary, 0.0, 1.0) * w, sh);
        /* drawn in the text colour that contrasts with the fill */
        cairo_set_source_rgba (cr, on_fill.red, on_fill.green, on_fill.blue, 0.85);
        cairo_fill (cr);
        cairo_restore (cr);
    }

    /* caption: once in the normal text colour, once clipped to the fill */
    if (bar->transfer_text != NULL) {
        double tx, ty;
        layout = gtk_widget_create_pango_layout (widget, bar->transfer_text);
        {
            PangoAttrList *attrs = pango_attr_list_new ();
            pango_attr_list_insert (attrs, pango_attr_weight_new (PANGO_WEIGHT_BOLD));
            pango_layout_set_attributes (layout, attrs);
            pango_attr_list_unref (attrs);
        }
        pango_layout_set_ellipsize (layout, PANGO_ELLIPSIZE_END);
        pango_layout_set_width (layout, (int) MAX (0.0, w - 12.0) * PANGO_SCALE);
        pango_layout_get_pixel_size (layout, &tw, &th);
        tx = (w - tw) / 2.0;
        ty = (h - th) / 2.0 - (bar->transfer_secondary >= 0.0 ? 1.0 : 0.0);
        cairo_save (cr);
        cairo_rectangle (cr, 0, 0, w, h);
        cairo_rectangle (cr, fx + fw, 0, -fw, h);  /* everything except fill */
        cairo_set_fill_rule (cr, CAIRO_FILL_RULE_EVEN_ODD);
        cairo_clip (cr);
        gdk_cairo_set_source_rgba (cr, &fg);
        cairo_move_to (cr, tx, ty);
        pango_cairo_show_layout (cr, layout);
        cairo_restore (cr);
        cairo_save (cr);
        cairo_rectangle (cr, fx, 0, fw, h);
        cairo_clip (cr);
        gdk_cairo_set_source_rgba (cr, &on_fill);
        cairo_move_to (cr, tx, ty);
        pango_cairo_show_layout (cr, layout);
        cairo_restore (cr);
        g_object_unref (layout);
    }
    return TRUE;
}

void
nemo_status_bar_set_transfer (NemoStatusBar *bar, const char *label,
                               double fraction, GObject *handler)
{
    if (!NEMO_IS_STATUS_BAR (bar) || !GTK_IS_STACK (bar->transfer_stack)) return;
    if (label == NULL) {
        gtk_stack_set_visible_child (GTK_STACK (bar->transfer_stack), bar->real_statusbar);
        gtk_widget_set_halign (bar->transfer_stack, GTK_ALIGN_CENTER);
        bar->transfer_state = NEMO_STATUS_BAR_TRANSFER_NORMAL;
        bar->transfer_secondary = -1.0;
        gtk_widget_queue_resize (gtk_widget_get_parent (bar->transfer_stack));
        return;
    }
    if (handler != NULL) {
        /* The handler can be recreated after a completed transfer. Do not
         * retain a stale owner pointer and leave a dead footer button. */
        g_signal_handlers_disconnect_matched (
            bar->transfer_button, G_SIGNAL_MATCH_FUNC, 0, 0, NULL,
            G_CALLBACK (dakkar_transfer_button_clicked), NULL);
        g_signal_connect_object (bar->transfer_button, "clicked",
                                 G_CALLBACK (dakkar_transfer_button_clicked), handler, 0);
    }
    g_free (bar->transfer_text);
    bar->transfer_text = g_strdup (label);
    /* Unknown totals pulse instead of inventing a percentage. */
    if (fraction < 0.0)
        bar->transfer_pulse++;
    bar->transfer_fraction = fraction < 0.0 ? -1.0 : CLAMP (fraction, 0.0, 1.0);
    gtk_widget_set_tooltip_text (bar->transfer_button, label);
    atk_object_set_name (gtk_widget_get_accessible (bar->transfer_button), label);
    if (gtk_stack_get_visible_child (GTK_STACK (bar->transfer_stack)) != bar->transfer_box) {
        gtk_stack_set_visible_child (GTK_STACK (bar->transfer_stack), bar->transfer_box);
        /* size_allocate honours halign: FILL lets the bar take the free width */
        gtk_widget_set_halign (bar->transfer_stack, GTK_ALIGN_FILL);
        gtk_widget_queue_resize (gtk_widget_get_parent (bar->transfer_stack));
    }
    gtk_widget_queue_draw (bar->transfer_progress);
}

void
nemo_status_bar_set_transfer_state (NemoStatusBar *bar, NemoStatusBarTransferState state,
                                    double secondary)
{
    if (!NEMO_IS_STATUS_BAR (bar) || bar->transfer_progress == NULL) return;
    if (bar->transfer_state == state && bar->transfer_secondary == secondary) return;
    bar->transfer_state = state;
    bar->transfer_secondary = secondary;
    gtk_widget_queue_draw (bar->transfer_progress);
}

/* The transfer bar spans the free width between the left and right footer
 * controls; the ordinary status text keeps its centred natural size. */
static gboolean
dakkar_footer_child_position (GtkOverlay *overlay, GtkWidget *child,
                             GdkRectangle *alloc, NemoStatusBar *bar)
{
    GtkWidget *controls = gtk_bin_get_child (GTK_BIN (overlay));
    GList *children, *l;
    GArray *edges;
    int left = 0, right, oh, min_h, nat_h, x, y, total;
    guint i;

    if (child != bar->transfer_stack ||
        gtk_stack_get_visible_child (GTK_STACK (child)) != bar->transfer_box ||
        controls == NULL)
        return FALSE;
    total = gtk_widget_get_allocated_width (GTK_WIDGET (overlay));
    oh = gtk_widget_get_allocated_height (GTK_WIDGET (overlay));
    /* The free area is the widest horizontal gap between visible controls
     * (left group, right group; spacers are invisible to this). */
    edges = g_array_new (FALSE, FALSE, sizeof (GdkRectangle));
    children = gtk_container_get_children (GTK_CONTAINER (controls));
    for (l = children; l != NULL; l = l->next) {
        GtkWidget *w = l->data;
        GdkRectangle r;
        if (!gtk_widget_get_visible (w) || !gtk_widget_get_child_visible (w)) continue;
        if (!gtk_widget_translate_coordinates (w, GTK_WIDGET (overlay), 0, 0, &x, &y))
            continue;
        r.x = x; r.width = gtk_widget_get_allocated_width (w);
        if (r.width <= 1) continue;
        g_array_append_val (edges, r);
    }
    g_list_free (children);
    {
        /* sort by x (few elements) */
        guint j;
        for (i = 0; i < edges->len; i++)
            for (j = i + 1; j < edges->len; j++)
                if (g_array_index (edges, GdkRectangle, j).x < g_array_index (edges, GdkRectangle, i).x) {
                    GdkRectangle tmp = g_array_index (edges, GdkRectangle, i);
                    g_array_index (edges, GdkRectangle, i) = g_array_index (edges, GdkRectangle, j);
                    g_array_index (edges, GdkRectangle, j) = tmp;
                }
    }
    right = total;
    {
        int best = -1, prev_end = 0;
        for (i = 0; i <= edges->len; i++) {
            int start = i < edges->len ? g_array_index (edges, GdkRectangle, i).x : total;
            if (start - prev_end > best) {
                best = start - prev_end;
                left = prev_end;
                right = start;
            }
            if (i < edges->len)
                prev_end = MAX (prev_end, g_array_index (edges, GdkRectangle, i).x +
                                          g_array_index (edges, GdkRectangle, i).width);
        }
    }
    g_array_free (edges, TRUE);
    gtk_widget_get_preferred_height (child, &min_h, &nat_h);
    alloc->x = left + 8;
    alloc->width = MAX (120, right - left - 16);
    alloc->height = MIN (oh, MAX (min_h, nat_h));
    alloc->y = (oh - alloc->height) / 2;
    return TRUE;
}

static void
dakkar_transfer_pause_clicked (GtkButton *button, gpointer user_data)
{
    nemo_transfer_window_toggle_pause_all ();
    (void) button; (void) user_data;
}

void
nemo_status_bar_set_transfer_paused (NemoStatusBar *bar, gboolean visible, gboolean paused)
{
    if (!NEMO_IS_STATUS_BAR (bar) || bar->transfer_pause == NULL) return;
    gtk_widget_set_visible (bar->transfer_pause, visible);
    gtk_button_set_image (GTK_BUTTON (bar->transfer_pause),
        gtk_image_new_from_icon_name (paused ? "media-playback-start-symbolic" :
                                      "media-playback-pause-symbolic", GTK_ICON_SIZE_MENU));
    gtk_widget_set_tooltip_text (bar->transfer_pause,
        paused ? _("Resume all transfers") : _("Pause all transfers"));
}

static void
nemo_status_bar_constructed (GObject *object)
{
    NemoStatusBar *bar = NEMO_STATUS_BAR (object);
    G_OBJECT_CLASS (nemo_status_bar_parent_class)->constructed (object);

    GtkWidget *statusbar = gtk_statusbar_new ();
    GtkWidget *overlay = gtk_overlay_new ();
    GtkWidget *controls = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
    GtkWidget *stack = gtk_stack_new ();
    GtkWidget *progress_button = gtk_button_new ();
    GtkWidget *progress = gtk_drawing_area_new ();
    GtkWidget *message_area;
    GList *message_children;
    /* A real bar: at least 20 px high, as wide as the free footer space. */
    gtk_widget_set_size_request (progress, 200, 20);
    gtk_widget_set_hexpand (progress, TRUE);
    gtk_style_context_add_class (gtk_widget_get_style_context (progress), "dakkar-transfer-bar");
    g_signal_connect (progress, "draw", G_CALLBACK (dakkar_transfer_bar_draw), bar);
    bar->transfer_fraction = 0.0;
    bar->transfer_secondary = -1.0;
    gtk_widget_set_halign (stack, GTK_ALIGN_CENTER);
    gtk_widget_set_valign (stack, GTK_ALIGN_CENTER);
    gtk_widget_set_hexpand (stack, FALSE);
    gtk_button_set_relief (GTK_BUTTON (progress_button), GTK_RELIEF_NONE);
    gtk_style_context_add_class (gtk_widget_get_style_context (progress_button),
                                 "dakkar-transfer-button");
    gtk_widget_set_tooltip_text (progress_button, _("Open file operation details"));
    gtk_container_add (GTK_CONTAINER (progress_button), progress);
    gtk_container_add (GTK_CONTAINER (stack), statusbar);
    {
        GtkWidget *transfer_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 2);
        GtkWidget *pause = gtk_button_new_from_icon_name ("media-playback-pause-symbolic",
                                                          GTK_ICON_SIZE_MENU);
        gtk_button_set_relief (GTK_BUTTON (pause), GTK_RELIEF_NONE);
        gtk_widget_set_tooltip_text (pause, _("Pause all transfers"));
        g_signal_connect (pause, "clicked", G_CALLBACK (dakkar_transfer_pause_clicked), NULL);
        gtk_box_pack_start (GTK_BOX (transfer_box), progress_button, TRUE, TRUE, 0);
        gtk_box_pack_start (GTK_BOX (transfer_box), pause, FALSE, FALSE, 0);
        gtk_widget_set_no_show_all (pause, TRUE);
        gtk_container_add (GTK_CONTAINER (stack), transfer_box);
        bar->transfer_box = transfer_box;
        bar->transfer_pause = pause;
    }
    gtk_style_context_add_class (gtk_widget_get_style_context (overlay), "dakkar-footer");
    gtk_container_add (GTK_CONTAINER (overlay), controls);
    gtk_overlay_add_overlay (GTK_OVERLAY (overlay), stack);
    g_signal_connect (overlay, "get-child-position",
                      G_CALLBACK (dakkar_footer_child_position), bar);
    {
        /* Compact click target around the bar; the bar itself draws the
         * visible frame, so the flat button only adds hover feedback. */
        static GtkCssProvider *provider = NULL;
        if (provider == NULL) {
            provider = gtk_css_provider_new ();
            gtk_css_provider_load_from_data (provider,
                "button.dakkar-transfer-button { padding: 1px 2px; min-height: 0; }\n",
                -1, NULL);
        }
        gtk_style_context_add_provider (gtk_widget_get_style_context (progress_button),
                                        GTK_STYLE_PROVIDER (provider),
                                        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    }
    gtk_box_pack_start (GTK_BOX (bar), overlay, TRUE, TRUE, 0);
    bar->transfer_stack = stack;
    bar->transfer_button = progress_button;
    bar->transfer_progress = progress;
    gtk_stack_set_visible_child (GTK_STACK (stack), statusbar);
    message_area = gtk_statusbar_get_message_area (GTK_STATUSBAR (statusbar));
    message_children = gtk_container_get_children (GTK_CONTAINER (message_area));
    if (message_children != NULL && GTK_IS_LABEL (message_children->data)) {
        gtk_label_set_ellipsize (GTK_LABEL (message_children->data), PANGO_ELLIPSIZE_MIDDLE);
        gtk_label_set_justify (GTK_LABEL (message_children->data), GTK_JUSTIFY_CENTER);
        gtk_label_set_max_width_chars (GTK_LABEL (message_children->data), 58);
    }
    g_list_free (message_children);
    GtkStyleContext *context;

    bar->real_statusbar = statusbar;

    GtkIconSize size = gtk_icon_size_from_name (NEMO_STATUSBAR_ICON_SIZE_NAME);

    context = gtk_widget_get_style_context (GTK_WIDGET (bar));
    gtk_style_context_add_class (context, GTK_STYLE_CLASS_TOOLBAR);
    /* custom palette: background, toggle buttons and zoom sliders */
    gtk_style_context_add_class (context, "dakkar-statusbar");
    gtk_container_set_border_width (GTK_CONTAINER (bar), 2);

    GtkWidget *button, *icon;

    /* Sidebar slider controls either the pane WIDTH or icon/text SCALE.
     * Places, Tree, Hide Sidebar and Settings remain separate buttons. */
    GtkWidget *sidebar_slider = gtk_scale_new_with_range (
        GTK_ORIENTATION_HORIZONTAL, 0.65, 1.85, 0.05);
    gtk_widget_set_size_request (sidebar_slider, 115, -1);
    gtk_scale_set_draw_value (GTK_SCALE (sidebar_slider), FALSE);
    gtk_box_pack_start (GTK_BOX (controls), sidebar_slider, FALSE, FALSE, 5);
    g_object_set_data (G_OBJECT (sidebar_slider), "files-sidebar-bar", bar);
    g_signal_connect (sidebar_slider, "value-changed",
                      G_CALLBACK (files_sidebar_slider_value_changed), bar);
    g_signal_connect_object (nemo_preferences,
                             "changed::" NEMO_PREFERENCES_FILES_SIDEBAR_SCALE,
                             G_CALLBACK (files_sidebar_slider_setting_changed),
                             sidebar_slider, 0);
    files_sidebar_slider_refresh (sidebar_slider);

    /* the Places/Tree mode switch is at the bottom-right of the sidebar
     * itself, not among the footer controls */

    button = gtk_button_new ();
    icon = gtk_image_new_from_icon_name ("sidebar-hide-symbolic", size);
    gtk_button_set_image (GTK_BUTTON (button), icon);
    gtk_widget_set_tooltip_text (GTK_WIDGET (button), _("Hide the Sidebar (F9)"));
    bar->hide_button = button;
    gtk_box_pack_start (GTK_BOX (controls), button, FALSE, FALSE, 2);
    g_signal_connect (GTK_BUTTON (button), "clicked",
                      G_CALLBACK (action_hide_sidebar_callback), bar);

    button = gtk_button_new ();
    icon = gtk_image_new_from_icon_name ("sidebar-show-symbolic", size);
    gtk_button_set_image (GTK_BUTTON (button), icon);
    gtk_widget_set_tooltip_text (GTK_WIDGET (button), _("Show the Sidebar (F9)"));
    bar->show_button = button;
    gtk_box_pack_start (GTK_BOX (controls), button, FALSE, FALSE, 2);
    g_signal_connect (GTK_BUTTON (button), "clicked",
                      G_CALLBACK (action_show_sidebar_callback), bar);


    /* The stack is an overlay aligned to the WHOLE footer; controls' widths
     * never affect the center of the status or transfer bar. */

    GtkWidget *zoom_slider = gtk_scale_new_with_range (GTK_ORIENTATION_HORIZONTAL,
                                                       (gdouble) NEMO_ZOOM_LEVEL_SMALLEST,
                                                       (gdouble) NEMO_ZOOM_LEVEL_LARGEST,
                                                       1.0);
    gtk_widget_set_tooltip_text (GTK_WIDGET (zoom_slider), _("Adjust zoom level"));
    bar->zoom_slider = zoom_slider;

    /* Footer is an overlay: zoom belongs at the far right, not beside
     * the left sidebar controls. It is packed after the rightmost inspector. */

    gtk_widget_set_size_request (GTK_WIDGET (zoom_slider), SLIDER_WIDTH, 0);
    gtk_scale_set_draw_value (GTK_SCALE (zoom_slider), FALSE);
    gtk_range_set_increments (GTK_RANGE (zoom_slider), 1.0, 1.0);
    gtk_range_set_round_digits (GTK_RANGE (zoom_slider), 0);

    /* Mirror the left collapse control along the footer's far right.
     * Pack it at the END, not among the toolbar/navigation buttons. */
    button = gtk_button_new ();
    bar->inspector_button = button;
    icon = gtk_image_new_from_icon_name ("sidebar-hide-symbolic-rtl", size);
    gtk_button_set_image (GTK_BUTTON (button), icon);
    gtk_widget_set_tooltip_text (button, _("Hide Right Panel"));
    gtk_widget_set_can_focus (button, FALSE);
    /* gtk_box_pack_end places the first child nearest the outer edge.
     * The mirrored collapse must be directly BEFORE the right zoom slider. */
    gtk_box_pack_end (GTK_BOX (controls), zoom_slider, FALSE, FALSE, 2);
    gtk_box_pack_end (GTK_BOX (controls), button, FALSE, FALSE, 2);
    g_signal_connect (button, "clicked",
                      G_CALLBACK (nemo_status_bar_toggle_inspector), bar);
    /* Re-sync when a panel is switched off in Preferences. */
    g_signal_connect_object (nemo_preferences,
        "changed::miller-show-preview",
        G_CALLBACK (nemo_status_bar_inspector_setting_changed), bar, 0);
    g_signal_connect_object (nemo_preferences,
        "changed::miller-quick-actions-enabled",
        G_CALLBACK (nemo_status_bar_inspector_setting_changed), bar, 0);

    gtk_widget_show_all (GTK_WIDGET (bar));

    g_signal_connect_object (NEMO_WINDOW (bar->window), "notify::show-sidebar",
                             G_CALLBACK (sidebar_state_changed_cb), bar, G_CONNECT_AFTER);

    g_signal_connect_object (NEMO_WINDOW (bar->window), "notify::sidebar-view-id",
                           G_CALLBACK (sidebar_type_changed_cb), bar, G_CONNECT_AFTER);

    g_signal_connect (GTK_RANGE (zoom_slider), "value-changed",
                      G_CALLBACK (on_slider_changed_cb), bar);

    GtkWidget *cont = gtk_statusbar_get_message_area (GTK_STATUSBAR (statusbar));

    GList *children = gtk_container_get_children (GTK_CONTAINER (cont));

    gtk_box_set_child_packing (GTK_BOX (cont),
                               GTK_WIDGET (children->data),
                               TRUE, FALSE, 10, GTK_PACK_START);

    g_list_free (children);

    nemo_status_bar_sync_button_states (bar);
}


static void
nemo_status_bar_class_init (NemoStatusBarClass *status_bar_class)
{
    GObjectClass *oclass;

    oclass = G_OBJECT_CLASS (status_bar_class);

    oclass->set_property = nemo_status_bar_set_property;
    oclass->get_property = nemo_status_bar_get_property;

    oclass->dispose = nemo_status_bar_dispose;
    oclass->constructed = nemo_status_bar_constructed;

    properties[PROP_WINDOW] = g_param_spec_object ("window",
                                                   "The NemoWindow",
                                                   "The parent NemoWindow",
                                                   NEMO_TYPE_WINDOW,
                                                   G_PARAM_READWRITE |
                                                   G_PARAM_CONSTRUCT_ONLY |
                                                   G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (oclass, NUM_PROPERTIES, properties);
}

GtkWidget *
nemo_status_bar_new (NemoWindow *window)
{
    return g_object_new (NEMO_TYPE_STATUS_BAR,
                         "orientation", GTK_ORIENTATION_HORIZONTAL,
                         "spacing", 0,
                         "window", window,
                         NULL);
}

GtkWidget *
nemo_status_bar_get_real_statusbar (NemoStatusBar *bar)
{
    return bar->real_statusbar;
}

void
nemo_status_bar_sync_button_states (NemoStatusBar *bar)
{
    gboolean sidebar_visible = nemo_window_get_show_sidebar (NEMO_WINDOW (bar->window));

    if (sidebar_visible) {
        gtk_widget_show (bar->hide_button);
        gtk_widget_hide (bar->show_button);
    } else {
        gtk_widget_hide (bar->hide_button);
        gtk_widget_show (bar->show_button);
    }
    nemo_status_bar_sync_sidebar_mode_button (bar);
    nemo_status_bar_sync_inspector_button (bar);
}

void
nemo_status_bar_sync_zoom_widgets (NemoStatusBar *bar)
{
    /* A transfer may have a new destination window after switching tabs. */
    nemo_progress_ui_handler_refresh_global ();


    NemoWindowSlot *slot = nemo_window_get_active_slot (bar->window);

    nemo_status_bar_sync_inspector_button (bar);
    if (!NEMO_IS_WINDOW_SLOT (slot))
        return;

    NemoView *view = slot->content_view;

    if (!NEMO_IS_VIEW (view))
        return;

    NemoZoomLevel zoom_level = nemo_view_get_zoom_level (NEMO_VIEW (view));

    g_signal_handlers_block_by_func (GTK_RANGE (bar->zoom_slider), on_slider_changed_cb, bar);

    gtk_range_set_value (GTK_RANGE (bar->zoom_slider), (double) zoom_level);

    g_signal_handlers_unblock_by_func (GTK_RANGE (bar->zoom_slider), on_slider_changed_cb, bar);
}
