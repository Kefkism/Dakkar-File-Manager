/* Shared appearance preferences: one CSS provider, never widget recreation. */
#include <config.h>
#include <stdarg.h>
#include "nemo-corner-style.h"
#include "nemo-actions.h"
#include <libnemo-private/nemo-global-preferences.h>

static GtkCssProvider *corner_provider;
/* The accent palette (named colour overrides, inactive selections, hover,
 * dialogs) sits above the user's gtk.css: KDE writes its colour scheme
 * there as @define-color, which would otherwise win over ours. */
static GtkCssProvider *accent_provider;
static void dakkar_transparency_update_all (void);

/* Optional palette colours: an empty setting follows another colour. */
static const struct { const gchar *key; const gchar *follows; } dakkar_derived[] = {
    {DAKKAR_THEME_QA_BUTTON, DAKKAR_THEME_BUTTONS},
    {DAKKAR_THEME_QA_TEXT, DAKKAR_THEME_TEXT},
    {DAKKAR_THEME_QA_HOVER, DAKKAR_THEME_ACCENT_SECONDARY},
    {DAKKAR_THEME_QA_BORDER, DAKKAR_THEME_BORDERS},
    {DAKKAR_THEME_STATUSBAR, DAKKAR_THEME_TOOLBAR},
};

static const struct { const gchar *key, *fallback; } dakkar_base_defaults[] = {
    {DAKKAR_THEME_BACKGROUND, "#303036"}, {DAKKAR_THEME_SIDEBAR, "#292a30"},
    {DAKKAR_THEME_INSPECTOR, "#303036"}, {DAKKAR_THEME_TEXT, "#f4f4f5"},
    {DAKKAR_THEME_ACCENT, "#4c94df"}, {DAKKAR_THEME_TOOLBAR, "#32353a"},
    {DAKKAR_THEME_TABS, "#2c2e33"}, {DAKKAR_THEME_BUTTONS, "#41444a"},
    {DAKKAR_THEME_BORDERS, "#53575f"}, {DAKKAR_THEME_HOVER, "#454a52"},
    {DAKKAR_THEME_MENUS, "#34373d"},
};

gboolean
dakkar_theme_color_is_automatic (const gchar *key)
{
    gchar *v;
    gboolean automatic;
    if (nemo_preferences == NULL)
        return TRUE;
    v = g_settings_get_string (nemo_preferences, key);
    automatic = v == NULL || v[0] == '\0';
    g_free (v);
    return automatic;
}

void
dakkar_theme_color (const gchar *key, GdkRGBA *out)
{
    guint i;
    gchar *v;

    gdk_rgba_parse (out, "#808080");
    if (nemo_preferences == NULL || key == NULL)
        return;
    v = g_settings_get_string (nemo_preferences, key);
    if (v != NULL && v[0] != '\0' && gdk_rgba_parse (out, v)) {
        out->alpha = 1.0;
        g_free (v);
        return;
    }
    g_free (v);
    if (g_strcmp0 (key, DAKKAR_THEME_ACCENT_SECONDARY) == 0) {
        GdkRGBA accent, bg;
        /* a dimmer accent: mixed towards the content background */
        dakkar_theme_color (DAKKAR_THEME_ACCENT, &accent);
        dakkar_theme_color (DAKKAR_THEME_BACKGROUND, &bg);
        out->red = accent.red * .55 + bg.red * .45;
        out->green = accent.green * .55 + bg.green * .45;
        out->blue = accent.blue * .55 + bg.blue * .45;
        out->alpha = 1.0;
        return;
    }
    for (i = 0; i < G_N_ELEMENTS (dakkar_derived); i++)
        if (g_strcmp0 (key, dakkar_derived[i].key) == 0) {
            dakkar_theme_color (dakkar_derived[i].follows, out);
            return;
        }
    for (i = 0; i < G_N_ELEMENTS (dakkar_base_defaults); i++)
        if (g_strcmp0 (key, dakkar_base_defaults[i].key) == 0) {
            gdk_rgba_parse (out, dakkar_base_defaults[i].fallback);
            return;
        }
}

void
dakkar_theme_secondary_accent (GdkRGBA *out)
{
    dakkar_theme_color (DAKKAR_THEME_ACCENT_SECONDARY, out);
}

void
dakkar_dialog_style_attach (GtkWidget *toplevel)
{
    if (toplevel == NULL || !GTK_IS_WIDGET (toplevel))
        return;
    nemo_corner_style_ensure ();
    gtk_style_context_add_class (gtk_widget_get_style_context (toplevel), "dakkar-dialog");
}

/* rgb()/rgba() text of a colour for the generated stylesheet (numeric
 * only, never user text). */
#define DK_RGB(c) (int)((c).red*255+.5), (int)((c).green*255+.5), (int)((c).blue*255+.5)

/* Alpha values in the generated CSS.  printf would use the locale's decimal
 * separator ("0,800" in nl_NL), which GTK rejects along with every colour
 * of the stylesheet. */
#define CSS_NUM_LEN G_ASCII_DTOSTR_BUF_SIZE
static const gchar *
css_num (gchar *buf, gdouble value)
{
    return g_ascii_formatd (buf, CSS_NUM_LEN, "%.3f", value);
}

/* Controls that must follow the palette in every Dakkar window, written
 * once with "$" for the scope (".nemo-window " or ".dakkar-dialog "). */
static const gchar dakkar_controls_tmpl[] =
    /* switches */
    "$switch {background-color:@dakkar_dlg_button; background-image:none; "
    "border-color:@dakkar_dlg_border; color:@dakkar_dlg_fg;}\n"
    "$switch:checked {background-color:@dakkar_accent_main; border-color:@dakkar_accent_main; "
    "color:@theme_selected_fg_color;}\n"
    "$switch slider {background-color:mix(@dakkar_dlg_fg,@dakkar_dlg_bg,0.15); "
    "background-image:none; border-color:@dakkar_dlg_border;}\n"
    "$switch:hover slider {border-color:@dakkar_accent_main;}\n"
    "$switch:disabled {opacity:0.55;}\n"
    /* check and radio buttons */
    "$check, $radio {background-color:@dakkar_dlg_button; background-image:none; "
    "border-color:@dakkar_dlg_border; color:@dakkar_dlg_fg;}\n"
    "$check:hover, $radio:hover {border-color:@dakkar_accent_main;}\n"
    "$check:checked, $radio:checked, $check:indeterminate, $radio:indeterminate "
    "{background-color:@dakkar_accent_main; border-color:@dakkar_accent_main; "
    "color:@theme_selected_fg_color;}\n"
    "$check:disabled, $radio:disabled {opacity:0.55;}\n"
    /* entries, spin buttons and their focus ring */
    "$entry:focus, $spinbutton:focus, $spinbutton entry:focus "
    "{border-color:@dakkar_accent_main; box-shadow:inset 0 0 0 1px @dakkar_accent_main;}\n"
    "$entry selection, $label selection, $textview text selection, $spinbutton selection "
    "{background-color:@dakkar_accent_main; color:@theme_selected_fg_color;}\n"
    "$entry progress {border-color:@dakkar_accent_main; background-color:transparent;}\n"
    "$spinbutton button:hover {background-color:@dakkar_secondary; color:@theme_selected_fg_color;}\n"
    "$spinbutton button:active {background-color:@dakkar_pressed;}\n"
    /* combo boxes: the button follows the button rules; its popup is a menu */
    "$combobox button:hover {border-color:@dakkar_accent_main;}\n"
    /* sliders and scales: accent fill and knob, trough = dimmed text */
    "$scale trough {background-color:alpha(@dakkar_dlg_fg,0.28); background-image:none; "
    "border-color:transparent; box-shadow:none;}\n"
    "$scale highlight, $scale fill {background-color:@dakkar_accent_main; "
    "background-image:none; border-color:@dakkar_accent_main;}\n"
    "$scale fill {background-color:alpha(@dakkar_accent_main,0.45);}\n"
    "$scale slider {background-color:@dakkar_accent_main; background-image:none; "
    "border-color:@dakkar_accent_main; box-shadow:none;}\n"
    "$scale slider:hover {background-color:mix(@dakkar_accent_main,@dakkar_dlg_fg,0.25); "
    "border-color:@dakkar_dlg_fg;}\n"
    "$scale slider:active {background-color:@dakkar_pressed; border-color:@dakkar_dlg_fg;}\n"
    "$scale:disabled {opacity:0.55;}\n"
    /* progress bars and level bars */
    "$progressbar trough, $levelbar trough {background-color:alpha(@dakkar_dlg_fg,0.20); "
    "background-image:none; border-color:transparent;}\n"
    "$progressbar progress, $levelbar block.filled {background-color:@dakkar_accent_main; "
    "background-image:none; border-color:@dakkar_accent_main;}\n"
    /* scrollbars */
    "$scrollbar slider {background-color:alpha(@dakkar_dlg_fg,0.35); border-color:transparent;}\n"
    "$scrollbar slider:hover {background-color:@dakkar_secondary;}\n"
    "$scrollbar slider:active {background-color:@dakkar_accent_main;}\n"
    /* tabs */
    "$notebook > header tab:hover:not(:checked) {background-color:alpha(@dakkar_secondary,0.60);}\n"
    "$notebook > header tab:checked {box-shadow:inset 0 -3px @dakkar_accent_main;}\n"
    /* popovers */
    "$popover, $popover.background {background-color:@dakkar_dlg_menu; color:@dakkar_dlg_fg; "
    "border-color:@dakkar_dlg_border;}\n"
    "$popover modelbutton:hover, $popover button.flat:hover {background-color:@dakkar_secondary; "
    "color:@theme_selected_fg_color;}\n"
    "$popover modelbutton:active {background-color:@dakkar_pressed;}\n";

static void
dakkar_append_scoped (GString *css, const gchar *tmpl, const gchar *scope)
{
    gchar **parts = g_strsplit (tmpl, "$", -1);
    gchar *joined = g_strjoinv (scope, parts);
    g_string_append (css, joined);
    g_free (joined);
    g_strfreev (parts);
}

/* Theme colour names that resolve to the Dakkar accents, so theme rules and
 * our own rules (Miller columns, tabs, path bar, transfer bar, Breeze's KDE
 * names) follow the palette.  The provider sits above
 * the theme and the user's gtk.css, and only affects this process. */
static const gchar accent_alias_css[] =
    "@define-color theme_selected_bg_color @dakkar_accent_main;\n"
    "@define-color selected_bg_color @dakkar_accent_main;\n"
    "@define-color accent_bg_color @dakkar_accent_main;\n"
    "@define-color accent_color @dakkar_accent_main;\n"
    "@define-color theme_unfocused_selected_bg_color @dakkar_secondary;\n"
    "@define-color unfocused_selected_bg_color @dakkar_secondary;\n"
    "@define-color theme_selected_bg_color_breeze @dakkar_accent_main;\n"
    "@define-color theme_unfocused_selected_bg_color_breeze @dakkar_secondary;\n"
    "@define-color theme_hovering_selected_bg_color_breeze @dakkar_secondary;\n"
    "@define-color theme_button_decoration_hover_breeze @dakkar_secondary;\n"
    "@define-color theme_button_decoration_focus_breeze @dakkar_accent_main;\n"
    "@define-color theme_view_hover_decoration_color_breeze @dakkar_secondary;\n"
    "@define-color theme_view_active_decoration_color_breeze @dakkar_accent_main;\n";

/* Selections, rubber band, links, Quick Actions and the statusbar. */
#define QA(state) \
    ".nemo-window .files-corner-preview button" state ", " \
    ".nemo-window .files-general-quick-actions button" state ", " \
    ".nemo-window button.dakkar-qa-compact" state ", " \
    ".nemo-window button.miller-action-button" state ", " \
    ".nemo-window.dakkar-transparent .files-corner-preview button" state ", " \
    ".nemo-window.dakkar-transparent .files-general-quick-actions button" state
static const gchar accent_view_css[] =
    /* inactive selections: unfocused list/Miller columns and backdrop */
    ".nemo-window .files-corner-view treeview.view:selected:not(:focus), "
    ".nemo-window .files-corner-view treeview.view:selected:backdrop, "
    ".nemo-window .miller-columns-view treeview.view:selected:not(:focus), "
    ".nemo-window .miller-columns-view treeview.view:selected:backdrop, "
    ".nemo-window .files-corner-sidebar treeview.view:selected:backdrop, "
    ".nemo-window treeview.view.places-treeview:selected:backdrop "
    "{background-color:@dakkar_secondary; color:@theme_selected_fg_color;}\n"

    /* rubber band in every view: icon and compact views use the
     * .rubberband style, list and Miller are GtkTreeView rubber bands */
    ".nemo-window .rubberband, .nemo-window rubberband, "
    ".nemo-window treeview.view rubberband, .nemo-window iconview.view rubberband, "
    ".dakkar-dialog rubberband, .dakkar-dialog .rubberband "
    "{background-color:alpha(@dakkar_accent_main,0.22); "
    "border:1px solid @dakkar_accent_main;}\n"

    /* links (Preferences > About Dakkar, About dialog) take the accent */
    ".dakkar-dialog *:link, .dakkar-dialog button.link, .dakkar-dialog button.link label "
    "{color:@dakkar_accent_main;}\n"
    ".dakkar-dialog *:visited, .dakkar-dialog button.link:visited label "
    "{color:mix(@dakkar_accent_main,@theme_fg_color,0.35);}\n"
    /* link buttons are text links, not framed buttons */
    ".dakkar-dialog button.link, .dakkar-dialog button.link:hover, "
    ".dakkar-dialog button.link:active, .dakkar-dialog button.link:focus "
    "{background-image:none; background-color:transparent; border-color:transparent; "
    "box-shadow:none;}\n"

    /* Quick Actions buttons have their own colours; pressed mixes hover
     * and accent, disabled is dimmed */
    QA ("") " {background-color:@dakkar_qa_bg; background-image:none; "
    "border-color:@dakkar_qa_border; color:@dakkar_qa_fg; box-shadow:none; text-shadow:none;}\n"
    QA (" label") ", " QA (" image") " {color:@dakkar_qa_fg;}\n"
    QA (" .miller-action-subtitle") " {color:alpha(@dakkar_qa_fg,0.64);}\n"
    QA (":hover") " {background-color:@dakkar_qa_hover; background-image:none; "
    "border-color:@dakkar_accent_main;}\n"
    QA (":hover label") ", " QA (":hover image") " {color:@theme_selected_fg_color;}\n"
    QA (":active") ", " QA (":checked") " {background-color:@dakkar_qa_pressed; "
    "background-image:none; border-color:@dakkar_accent_main;}\n"
    QA (":focus") " {outline-color:@dakkar_accent_main;}\n"
    QA (":disabled") " {background-color:alpha(@dakkar_qa_bg,0.45); "
    "border-color:alpha(@dakkar_qa_border,0.45);}\n"
    QA (":disabled label") ", " QA (":disabled image") " {color:alpha(@dakkar_qa_fg,0.40);}\n"

    /* statusbar looks like the toolbar: own colour at toolbar opacity,
     * buttons like toolbar buttons, sliders via the scale rules */
    ".nemo-window .dakkar-statusbar, .nemo-window .dakkar-statusbar statusbar "
    "{background-color:@dakkar_statusbar; background-image:none; color:@dakkar_dlg_fg;}\n"
    ".nemo-window .dakkar-statusbar statusbar label {color:@dakkar_dlg_fg;}\n"
    ".nemo-window .dakkar-statusbar button {background-color:@dakkar_bar_button; "
    "background-image:none; border-color:@dakkar_dlg_border; color:@dakkar_dlg_fg; "
    "box-shadow:none; text-shadow:none;}\n"
    ".nemo-window .dakkar-statusbar button image {color:@dakkar_dlg_fg;}\n"
    ".nemo-window .dakkar-statusbar button:hover {background-color:@dakkar_secondary; "
    "border-color:@dakkar_accent_main;}\n"
    ".nemo-window .dakkar-statusbar button:hover image {color:@theme_selected_fg_color;}\n"
    ".nemo-window .dakkar-statusbar button:active, .nemo-window .dakkar-statusbar button:checked "
    "{background-color:@dakkar_pressed; border-color:@dakkar_accent_main;}\n";
#undef QA

/* Menus and tooltips are toplevels of their own; in this process they
 * follow the palette too. */
static const gchar accent_popup_css[] =
    "menu, .menu, .context-menu, menu.background {background-color:@dakkar_dlg_menu; "
    "color:@dakkar_dlg_fg; border-color:@dakkar_dlg_border;}\n"
    "menu menuitem, .menu menuitem {color:@dakkar_dlg_fg;}\n"
    "menu menuitem:hover, .menu menuitem:hover {background-color:@dakkar_secondary; "
    "color:@theme_selected_fg_color;}\n"
    "menu menuitem:hover label, menu menuitem:hover accelerator {color:@theme_selected_fg_color;}\n"
    "menu menuitem:disabled label {color:alpha(@dakkar_dlg_fg,0.45);}\n"
    "menu menuitem check:checked, menu menuitem radio:checked {color:@dakkar_accent_main;}\n"
    "tooltip, tooltip.background {background-color:@dakkar_dlg_menu; "
    "border-color:@dakkar_dlg_border; color:@dakkar_dlg_fg;}\n"
    "tooltip label {color:@dakkar_dlg_fg;}\n";

/* Dakkar dialogs: surfaces, lists (the Preferences page list) and their
 * selection, buttons with hover/pressed, entries. */
#define D ".dakkar-dialog "
static const gchar accent_dialog_css[] =
    "window.dakkar-dialog, dialog.dakkar-dialog, .dakkar-dialog .dialog-action-area, "
    D "notebook > stack, " D "headerbar "
    "{background-color:@dakkar_dlg_bg; background-image:none; color:@dakkar_dlg_fg;}\n"
    D "label, " D "image {color:@dakkar_dlg_fg;}\n"
    D "label:disabled, " D "image:disabled {color:alpha(@dakkar_dlg_fg,0.50);}\n"
    D "stacksidebar, " D "stacksidebar list, " D "list, " D "treeview.view, "
    D "iconview, " D "flowbox, " D "scrolledwindow, " D "viewport, " D "textview text "
    "{background-color:@dakkar_dlg_side; color:@dakkar_dlg_fg;}\n"
    D "list row, " D "flowbox flowboxchild {background-color:transparent; color:@dakkar_dlg_fg;}\n"
    D "list row:hover:not(:selected), " D "flowbox flowboxchild:hover:not(:selected) "
    "{background-color:alpha(@dakkar_secondary,0.60);}\n"
    D "list row:selected, " D "stacksidebar row:selected, " D "treeview.view:selected, "
    D "iconview:selected, " D "flowbox flowboxchild:selected "
    "{background-color:@dakkar_accent_main; color:@theme_selected_fg_color;}\n"
    D "list row:selected label, " D "stacksidebar row:selected label, "
    D "list row:selected image {color:@theme_selected_fg_color;}\n"
    D "list row:selected:backdrop, " D "treeview.view:selected:backdrop, "
    D "treeview.view:selected:not(:focus), " D "iconview:selected:backdrop "
    "{background-color:@dakkar_secondary;}\n"
    D "button {background-color:@dakkar_dlg_button; background-image:none; "
    "border-color:@dakkar_dlg_border; color:@dakkar_dlg_fg; box-shadow:none; text-shadow:none;}\n"
    D "button label, " D "button image {color:inherit;}\n"
    D "button:hover {background-color:@dakkar_secondary; border-color:@dakkar_accent_main; "
    "color:@theme_selected_fg_color;}\n"
    D "button:active, " D "button:checked {background-color:@dakkar_pressed; "
    "border-color:@dakkar_accent_main; color:@theme_selected_fg_color;}\n"
    D "button.suggested-action {background-color:@dakkar_accent_main; "
    "border-color:@dakkar_accent_main; color:@theme_selected_fg_color;}\n"
    D "button:disabled {background-color:alpha(@dakkar_dlg_button,0.55); "
    "color:alpha(@dakkar_dlg_fg,0.45);}\n"
    D "button:focus {outline-color:@dakkar_accent_main;}\n"
    D "list button, " D "treeview.view header button {background-color:transparent; border-color:transparent;}\n"
    D "list button:hover {background-color:@dakkar_secondary;}\n"
    D "entry, " D "spinbutton, " D "spinbutton entry {background-color:@dakkar_dlg_side; "
    "background-image:none; color:@dakkar_dlg_fg; border-color:@dakkar_dlg_border;}\n"
    D "spinbutton button {background-color:transparent; border-color:transparent;}\n"
    D "scrollbar, " D "scrollbar trough {background-color:transparent; border-color:transparent;}\n"
    D "notebook > header {background-color:@dakkar_dlg_side; border-color:@dakkar_dlg_border;}\n"
    D "separator {background-color:@dakkar_dlg_border;}\n"
    D "frame > border {border-color:@dakkar_dlg_border;}\n";
#undef D

/* Stylesheet for the accent provider: palette colour names, theme colour
 * aliases and the control rules for main windows and dialogs. */
static gchar *
dakkar_accent_css (void)
{
    GdkRGBA a, s, bg, fg, side, buttons, borders, menus,
            qa_bg, qa_fg, qa_hover, qa_border, status;
    gdouble opacity, details_opacity, toolbar_opacity, qa_alpha, bar_alpha;
    gchar n_qa[CSS_NUM_LEN], n_hover[CSS_NUM_LEN], n_pressed[CSS_NUM_LEN], n_bar[CSS_NUM_LEN];
    GString *css = g_string_new (NULL);

    dakkar_theme_color (DAKKAR_THEME_ACCENT, &a);
    dakkar_theme_color (DAKKAR_THEME_ACCENT_SECONDARY, &s);
    dakkar_theme_color (DAKKAR_THEME_BACKGROUND, &bg);
    dakkar_theme_color (DAKKAR_THEME_TEXT, &fg);
    dakkar_theme_color (DAKKAR_THEME_SIDEBAR, &side);
    dakkar_theme_color (DAKKAR_THEME_BUTTONS, &buttons);
    dakkar_theme_color (DAKKAR_THEME_BORDERS, &borders);
    dakkar_theme_color (DAKKAR_THEME_MENUS, &menus);
    dakkar_theme_color (DAKKAR_THEME_QA_BUTTON, &qa_bg);
    dakkar_theme_color (DAKKAR_THEME_QA_TEXT, &qa_fg);
    dakkar_theme_color (DAKKAR_THEME_QA_HOVER, &qa_hover);
    dakkar_theme_color (DAKKAR_THEME_QA_BORDER, &qa_border);
    dakkar_theme_color (DAKKAR_THEME_STATUSBAR, &status);
    opacity = CLAMP (g_settings_get_double (nemo_preferences, DAKKAR_THEME_OPACITY), .30, 1.0);
    details_opacity = CLAMP (g_settings_get_double (nemo_preferences, DAKKAR_THEME_DETAILS_OPACITY), .30, 1.0);
    toolbar_opacity = CLAMP (g_settings_get_double (nemo_preferences, DAKKAR_THEME_TOOLBAR_OPACITY), .30, 1.0);
    /* Quick Actions sit on the details pane and the statusbar is a toolbar,
     * so they take those surfaces' opacity */
    qa_alpha = opacity * details_opacity;
    bar_alpha = opacity * toolbar_opacity;

    /* named palette colours used by all the rules below */
    g_string_append_printf (css,
        "@define-color dakkar_accent_main rgb(%d,%d,%d);\n"
        "@define-color dakkar_secondary rgb(%d,%d,%d);\n"
        "@define-color dakkar_pressed mix(@dakkar_secondary,@dakkar_accent_main,0.5);\n"
        "@define-color dakkar_dlg_bg rgb(%d,%d,%d);\n"
        "@define-color dakkar_dlg_fg rgb(%d,%d,%d);\n"
        "@define-color dakkar_dlg_side rgb(%d,%d,%d);\n"
        "@define-color dakkar_dlg_button rgb(%d,%d,%d);\n"
        "@define-color dakkar_dlg_border rgb(%d,%d,%d);\n"
        "@define-color dakkar_dlg_menu rgb(%d,%d,%d);\n"
        "@define-color dakkar_qa_bg rgba(%d,%d,%d,%s);\n"
        "@define-color dakkar_qa_fg rgb(%d,%d,%d);\n"
        "@define-color dakkar_qa_hover rgba(%d,%d,%d,%s);\n"
        "@define-color dakkar_qa_pressed alpha(mix(rgb(%d,%d,%d),@dakkar_accent_main,0.5),%s);\n"
        "@define-color dakkar_qa_border rgb(%d,%d,%d);\n"
        "@define-color dakkar_statusbar rgba(%d,%d,%d,%s);\n"
        "@define-color dakkar_bar_button rgba(%d,%d,%d,%s);\n",
        DK_RGB (a), DK_RGB (s), DK_RGB (bg), DK_RGB (fg), DK_RGB (side),
        DK_RGB (buttons), DK_RGB (borders), DK_RGB (menus),
        DK_RGB (qa_bg), css_num (n_qa, qa_alpha), DK_RGB (qa_fg),
        DK_RGB (qa_hover), css_num (n_hover, MIN (1.0, qa_alpha + .25)),
        DK_RGB (qa_hover), css_num (n_pressed, MIN (1.0, qa_alpha + .30)),
        DK_RGB (qa_border),
        DK_RGB (status), css_num (n_bar, bar_alpha), DK_RGB (buttons), n_bar);
    g_string_append (css, accent_alias_css);
    g_string_append (css, accent_view_css);
    dakkar_append_scoped (css, dakkar_controls_tmpl, ".nemo-window ");
    dakkar_append_scoped (css, dakkar_controls_tmpl, ".dakkar-dialog ");
    g_string_append (css, accent_popup_css);
    g_string_append (css, accent_dialog_css);
    return g_string_free (css, FALSE);
}

/* Rules that do not depend on the custom palette: frame radius, pane
 * borders, tabs, toolbar buttons, Quick Actions and the preview sample. */
static gchar *
corner_base_css (gint radius)
{
    return g_strdup_printf (
        ".nemo-window .files-corner-view, "
        ".nemo-window .files-corner-sidebar, "
        ".nemo-window .files-corner-preview, "
        ".files-corner-sample { border-radius: %dpx; "
        "background-clip: padding-box; }\n"
        ".nemo-window .files-corner-sidebar, "
        ".nemo-window .files-corner-preview { "
        "background-color: @theme_bg_color; "
        "border-color: @theme_bg_color; }\n"
        ".nemo-window .files-corner-view { "
        "background-color: @theme_base_color; "
        "border-color: @theme_bg_color; }\n"
        /* no border-radius on treeview.view: GtkTreeView applies it to every
         * cell, which gives bumpy selections and gaps between sidebar rows.
         * The rounded frame comes from the corner mask instead. */
        ".nemo-window .files-corner-view iconview.view, "
        ".nemo-window .files-corner-preview scrolledwindow, "
        ".nemo-window .files-corner-preview viewport { "
        "border-radius:%dpx; background-clip:padding-box; }\n"
        ".nemo-window .miller-columns-view treeview.view, "
        ".nemo-window .miller-columns-view iconview.view {border-radius:0;}\n"
        ".nemo-window .files-corner-view > scrolledwindow, "
        ".nemo-window .files-corner-view viewport, "
        ".nemo-window .files-corner-preview scrolledwindow, "
        ".nemo-window .files-corner-sidebar scrolledwindow { "
        "box-shadow: none; border-color: transparent; }\n"
        /* pane borders use the theme's own contrast; Miller separators
         * stay square */
        ".nemo-window .files-corner-sidebar, "
        ".nemo-window .files-corner-preview, "
        ".nemo-window .files-corner-view { "
        "border: 1px solid alpha(@theme_fg_color, 0.10); }\n"
        /* compact tabs without the theme's thick underline */
        ".nemo-window notebook > header tab {"
        "min-width: 0; min-height: 22px; padding: 4px 11px;"
        "margin: 3px 4px 0 0; border-radius: 10px 10px 0 0;"
        "box-shadow: none; border-width: 1px;"
        "border-color: alpha(@theme_fg_color, 0.10);"
        "background-image: none; }\n"
        ".nemo-window notebook > header tab:checked {"
        "border-color: alpha(@theme_selected_bg_color, 0.35);"
        "background-color: alpha(@theme_selected_bg_color, 0.18);"
        "box-shadow: inset 0 2px 0 alpha(@theme_selected_bg_color,0.50); }\n"
        ".nemo-window notebook > header tab:hover:not(:checked) {"
        "background-color: alpha(@theme_fg_color, 0.07); }\n"
        ".nemo-window notebook > header tab button {"
        "min-width: 16px; min-height: 16px; padding: 0;"
        "margin: 0 0 0 5px; box-shadow: none; }\n"
        ".nemo-window .files-corner-view treeview.view, "
        ".nemo-window .files-corner-sidebar treeview.view, "
        ".nemo-window .files-corner-view treeview.view:selected, "
        ".nemo-window .files-corner-sidebar treeview.view:selected {"
        "border-radius:0; background-image:none;}\n"
        ".nemo-window .files-corner-sidebar treeview.view {"
        "-GtkTreeView-vertical-separator: 2;}\n"
        /* places sidebar cells stay transparent in every state;
         * NemoPlacesTreeView paints one rounded highlight per row */
        ".nemo-window treeview.view.places-treeview, "
        ".nemo-window treeview.view.places-treeview:hover, "
        ".nemo-window treeview.view.places-treeview:selected, "
        ".nemo-window treeview.view.places-treeview:selected:hover, "
        ".nemo-window treeview.view.places-treeview:selected:focus, "
        ".nemo-window treeview.view.places-treeview:selected:backdrop {"
        "background-color:transparent; background-image:none;"
        "border-radius:0; box-shadow:none;}\n"
        ".nemo-window treeview.view.places-treeview:selected {"
        "color:@theme_selected_fg_color;}\n"
        ".dakkar-preview-sidebar, .dakkar-preview-details, .dakkar-preview-toolbar, "
        ".dakkar-preview-content, .dakkar-preview-selection, .dakkar-preview-menu {"
        "border-radius:8px; padding:7px;}\n"
        ".dakkar-preview-selection {background-color:alpha(@theme_selected_bg_color,0.22);}"
        ".dakkar-preview-heading {opacity:0.82; font-weight:600;}\n"
        /* toolbar buttons: all 28x28 with the same radius.  Only the
         * right-hand group (dakkar-tool-framed) has a frame, the rest are
         * flat icons with a soft hover.  Hover and pressed go into
         * background-image so they still show over a custom button colour. */
        ".nemo-window toolbar button.dakkar-tool-button, "
        ".nemo-window .files-settings-picker.dakkar-tool-button { "
        "min-width:26px; min-height:26px; padding:0; margin:0; "
        "border-radius:7px; border:1px solid transparent; "
        "background-color:transparent; background-image:none; "
        "box-shadow:none; text-shadow:none; outline-offset:-3px; }\n"
        ".nemo-window toolbar button.dakkar-tool-button.dakkar-tool-framed { "
        "border-color:alpha(@theme_fg_color,0.14); "
        "background-color:alpha(@theme_fg_color,0.05); }\n"

        ".nemo-window toolbar button.dakkar-tool-button:hover { "
        "background-image:image(alpha(@theme_fg_color,0.08)); }\n"
        ".nemo-window toolbar button.dakkar-tool-button.dakkar-tool-framed:hover { "
        "border-color:alpha(@theme_fg_color,0.26); }\n"
        ".nemo-window toolbar button.dakkar-tool-button:active, "
        ".nemo-window toolbar button.dakkar-tool-button:checked { "
        "background-image:image(alpha(@theme_selected_bg_color,0.30)); }\n"
        ".nemo-window toolbar button.dakkar-tool-button:disabled { "
        "background-image:none; }\n"
        ".nemo-window toolbar button.dakkar-tool-button.dakkar-tool-framed:active, "
        ".nemo-window toolbar button.dakkar-tool-button.dakkar-tool-framed:checked { "
        "border-color:alpha(@theme_selected_bg_color,0.75); }\n"
        ".nemo-window toolbar button.dakkar-tool-button.dakkar-tool-framed:disabled { "
        "border-color:alpha(@theme_fg_color,0.07); }\n"
        ".nemo-window toolbar button.dakkar-tool-button.dakkar-tool-framed:backdrop { "
        "border-color:alpha(@theme_fg_color,0.10); }\n"
        ".nemo-window toolbar button.files-group-picker.dakkar-group-enabled {"
        "border-color:alpha(@theme_selected_bg_color,0.65);"
        "background-image:image(alpha(@theme_selected_bg_color,0.16)); }\n"
        /* Quick Actions buttons, the same in all views */
        ".nemo-window .dakkar-qa-compact {"
        "padding:3px 8px; min-height:26px; border-radius:8px;"
        "background-image:none;"
        "border:1px solid alpha(@theme_fg_color,0.11); }\n"
        ".nemo-window .dakkar-qa-compact:hover {"
        "background-color:alpha(@theme_selected_bg_color,0.10); }\n"
        /* paned separators: 6px to grab, one painted hairline */
        ".nemo-window .dakkar-preview-action-divider.vertical > separator {"
        "min-height:6px; background:transparent; border:0;"
        "box-shadow:inset 0 1px alpha(@theme_fg_color,0.17); }\n"
        ".nemo-window .dakkar-inspector-resize.horizontal > separator, "
        ".nemo-window .miller-inspector-resize.horizontal > separator {"
        /* no line here: the view and the details pane both draw a border */
        "min-width:6px; background:transparent; border:0; box-shadow:none; }\n"
        ".nemo-window .dakkar-preview-action-divider.vertical > separator:hover {"
        "box-shadow:inset 0 1px alpha(@theme_selected_bg_color,0.50); }\n"
        ".nemo-window .dakkar-inspector-resize.horizontal > separator:hover, "
        ".nemo-window .miller-inspector-resize.horizontal > separator:hover {"
        "box-shadow:inset 1px 0 alpha(@theme_selected_bg_color,0.50); }\n"
        ".nemo-window .dakkar-inspector-qa-title {"
        "font-weight:600; margin:3px 0 10px; font-size:95%%;"
        "color:alpha(@theme_fg_color,0.85); }\n"
        ".nemo-window .dakkar-qa-icon-only {padding:5px; min-width:32px;}\n"
        ".nemo-window .dakkar-qa-success {"
        "border:1px solid alpha(#38a169,0.90);"
        "box-shadow:inset 0 0 0 1px alpha(#38a169,0.15); }\n"
        ".nemo-window .dakkar-qa-error {"
        "border:1px solid alpha(#cc5544,0.85); }\n"
        ".files-corner-sample { "
        "background-color: @theme_base_color; color: @theme_text_color; "
        "border: 1px solid alpha(@theme_fg_color, 0.16); }\n"
        ".files-corner-sample label { "
        "color: @theme_text_color; }\n",
        radius, radius);
}

/* Colour setting as entered, or @fallback when it does not parse. */
static void
palette_read (const gchar *key, const gchar *fallback, GdkRGBA *out)
{
    gchar *v = g_settings_get_string (nemo_preferences, key);
    if (!gdk_rgba_parse (out, v))
        gdk_rgba_parse (out, fallback);
    g_free (v);
}

static gdouble
palette_opacity (const gchar *key)
{
    return CLAMP (g_settings_get_double (nemo_preferences, key), .30, 1.0);
}

/* The palette colours are only ever written as numbers, never as the
 * setting's text.  The first block historically truncates, the rest round. */
#define DK_RGB_TRUNC(c) (int)((c).red*255), (int)((c).green*255), (int)((c).blue*255)

/* Content, sidebar, details and toolbar surfaces in the custom colours,
 * each with its own opacity. */
static void
palette_surfaces_css (GString *css, gdouble opacity)
{
    GdkRGBA bg, side, inspector, fg, accent, toolbar;
    gchar n_op[CSS_NUM_LEN], n_side[CSS_NUM_LEN], n_details[CSS_NUM_LEN], n_tool[CSS_NUM_LEN];

    palette_read (DAKKAR_THEME_BACKGROUND, "#303036", &bg);
    palette_read (DAKKAR_THEME_SIDEBAR, "#292a30", &side);
    palette_read (DAKKAR_THEME_INSPECTOR, "#303036", &inspector);
    palette_read (DAKKAR_THEME_TEXT, "#f4f4f5", &fg);
    palette_read (DAKKAR_THEME_ACCENT, "#4c94df", &accent);
    palette_read (DAKKAR_THEME_TOOLBAR, "#32353a", &toolbar);

    g_string_append_printf (css,
        ".nemo-window .files-corner-view, .nemo-window .files-corner-view viewport "
        "{background-color:rgba(%d,%d,%d,%s);color:rgb(%d,%d,%d);}\n"
        ".nemo-window .files-corner-sidebar {background-color:rgba(%d,%d,%d,%s);}\n"
        ".nemo-window .files-corner-preview, .nemo-window .files-general-quick-actions "
        "{background-color:rgba(%d,%d,%d,%s);color:rgb(%d,%d,%d);}\n"
        ".nemo-window .files-corner-view:selected, "
        ".nemo-window .files-corner-view selection, "
        ".nemo-window .files-corner-sidebar selection {background-color:rgb(%d,%d,%d);}\n"
        ".files-corner-sample {background-color:rgba(%d,%d,%d,%s);color:rgb(%d,%d,%d);}\n",
        DK_RGB_TRUNC (bg), css_num (n_op, opacity), DK_RGB_TRUNC (fg),
        DK_RGB_TRUNC (side), n_op,
        DK_RGB_TRUNC (inspector), n_op, DK_RGB_TRUNC (fg),
        DK_RGB_TRUNC (accent),
        DK_RGB_TRUNC (bg), n_op, DK_RGB_TRUNC (fg));

    /* these descendants paint their own GDK windows, so colouring the
     * parent box is not enough */
    g_string_append_printf (css,
        ".nemo-window .files-corner-sidebar, "
        ".nemo-window .files-corner-sidebar treeview.view, "
        ".dakkar-preview-sidebar {background-color:rgba(%d,%d,%d,%s);}\n"
        ".nemo-window .files-corner-preview, "
        ".nemo-window .files-corner-preview scrolledwindow, "
        ".nemo-window .files-general-quick-actions, "
        ".dakkar-preview-details {background-color:rgba(%d,%d,%d,%s);}\n"
        ".nemo-window toolbar, .nemo-window headerbar, "
        ".dakkar-preview-toolbar {background-color:rgba(%d,%d,%d,%s);}\n",
        DK_RGB (side), css_num (n_side, opacity * palette_opacity (DAKKAR_THEME_SIDEBAR_OPACITY)),
        DK_RGB (inspector), css_num (n_details, opacity * palette_opacity (DAKKAR_THEME_DETAILS_OPACITY)),
        DK_RGB (toolbar), css_num (n_tool, opacity * palette_opacity (DAKKAR_THEME_TOOLBAR_OPACITY)));

    /* icon and list views paint an inner surface of their own.  Selected
     * rows get the accent explicitly, the surface rule would otherwise
     * paint them in the content colour too. */
    g_string_append_printf (css,
        ".nemo-window .files-corner-view treeview.view, "
        ".nemo-window .files-corner-view iconview.view, "
        ".nemo-window .files-corner-view scrolledwindow, "
        ".dakkar-preview-content {background-color:rgba(%d,%d,%d,%s); "
        "color:rgb(%d,%d,%d);}\n"
        ".nemo-window .miller-columns-view treeview.view, "
        ".nemo-window .miller-columns-view iconview.view {border-radius:0;}\n"
        ".nemo-window .files-corner-view treeview.view:selected, "
        ".nemo-window .files-corner-view treeview.view:selected:focus {"
        "background-color:rgb(%d,%d,%d); color:@theme_selected_fg_color;}\n"
        ".nemo-window .files-corner-view treeview.view:selected:backdrop {"
        "background-color:rgba(%d,%d,%d,0.55);}\n",
        DK_RGB (bg), n_op, DK_RGB (fg), DK_RGB (accent), DK_RGB (accent));

    /* dakkar_transfer_fill is read by the transfer bar in the statusbar */
    g_string_append_printf (css,
        "@define-color dakkar_transfer_fill rgb(%d,%d,%d);\n"
        "@define-color dakkar_accent rgb(%d,%d,%d);\n",
        DK_RGB (accent), DK_RGB (accent));
    g_string_append_printf (css,
        ".dakkar-preview-selection {background-color:rgba(%d,%d,%d,0.34);}" "\n",
        DK_RGB (accent));
    /* tree view cell text is drawn separately from the widget colour */
    g_string_append_printf (css,
        ".nemo-window .files-corner-sidebar treeview.view, "
        ".nemo-window .files-corner-sidebar treeview.view text, "
        ".dakkar-preview-sidebar label, .dakkar-preview-details label "
        "{color:rgb(%d,%d,%d); }\n",
        DK_RGB (fg));
}

/* Tabs, toolbar buttons, borders, hover and menus.  Borders only set
 * border-color, and the toolbar background is left to the surface rule
 * so its opacity slider keeps working. */
static void
palette_areas_css (GString *css, gdouble opacity)
{
    static const struct { const gchar *key, *selector, *fallback, *property; } areas[] = {
        {DAKKAR_THEME_TABS, ".nemo-window notebook > header", "#2c2e33", "background-color"},
        {DAKKAR_THEME_BUTTONS, ".nemo-window toolbar button, .nemo-window toolbar button.dakkar-tool-button.dakkar-tool-framed, .nemo-window headerbar button, .dakkar-preview-toolbar button", "#41444a", "background-color"},
        {DAKKAR_THEME_BORDERS, ".nemo-window paned > separator, .nemo-window .files-corner-sidebar, .nemo-window .files-corner-preview, .nemo-window .files-corner-view", "#53575f", "border-color"},
        {DAKKAR_THEME_HOVER, ".nemo-window notebook tab:hover, .nemo-window .files-corner-sidebar treeview.view row:hover", "#454a52", "background-color"},
        {DAKKAR_THEME_MENUS, ".nemo-window menu, .nemo-window popover, .dakkar-preview-menu", "#34373d", "background-color"},
    };
    GdkRGBA col;
    gchar n_alpha[CSS_NUM_LEN];
    guint i;

    for (i = 0; i < G_N_ELEMENTS (areas); i++) {
        palette_read (areas[i].key, areas[i].fallback, &col);
        g_string_append_printf (css,
            "%s { %s: rgba(%d,%d,%d,%s); }\n",
            areas[i].selector, areas[i].property, DK_RGB (col),
            /* menus and popovers stay opaque and readable */
            css_num (n_alpha, g_strcmp0 (areas[i].key, DAKKAR_THEME_MENUS) == 0 ? 1.0 : opacity));
    }
    palette_read (DAKKAR_THEME_SIDEBAR_HEADING, "#b8c0ce", &col);
    g_string_append_printf (css,
        ".dakkar-preview-heading {color:rgb(%d,%d,%d); }\n", DK_RGB (col));
}

/* See-through windows (class dakkar-transparent: custom colours and
 * see-through on, RGBA visual, compositor).  Only one layer per surface
 * may carry the colour: stacked translucent layers add up to opaque, so
 * the window and every container in between is cleared. */
#define T ".nemo-window.dakkar-transparent "
static void
palette_transparent_css (GString *css, gdouble opacity)
{
    GdkRGBA buttons;
    gchar a_tool[CSS_NUM_LEN];

    g_string_append (css,
        "window.nemo-window.dakkar-transparent, " T "paned, " T "notebook, "
        T "notebook > stack, " T "stack, " T "viewport, " T "layout, "
        T "scrolledwindow:not(.files-corner-view), "
        T ".view:not(:selected):not(.files-corner-view), "
        /* menubar and footer show the window background, painted once
         * around the surfaces by dakkar_transparency_window_draw() */
        T "menubar, " T "box.toolbar, " T "statusbar, " T "separator.horizontal "
        "{background-color:transparent; background-image:none; box-shadow:none;}\n"
        /* popovers inside the window stay opaque */
        T "popover, " T "popover.background {background-color:@theme_bg_color;}\n");

    /* controls on the see-through surfaces: no opaque theme gradients,
     * a light text-colour tint instead, stronger for hover and pressed.
     * Popovers and menus are separate CSS trees and keep the theme look. */
    palette_read (DAKKAR_THEME_BUTTONS, "#41444a", &buttons);
    css_num (a_tool, opacity * palette_opacity (DAKKAR_THEME_TOOLBAR_OPACITY));
#define TB T "toolbar button, " T "toolbar .path-bar button, " T "box.toolbar button, " \
           T "notebook > header button, " T ".dakkar-search-bar button, " \
           T ".files-corner-preview button, " T "button.dakkar-surface-button, " T "treeview.view header button"
    g_string_append_printf (css,
        TB " {background-image:none; box-shadow:none; text-shadow:none;"
        " background-color:alpha(@theme_fg_color,0.06); border-color:alpha(@theme_fg_color,0.12);}\n"
        T "toolbar button:hover, " T "toolbar .path-bar button:hover, " T "box.toolbar button:hover, "
        T "notebook > header button:hover, " T ".dakkar-search-bar button:hover, "
        T ".files-corner-preview button:hover, " T "button.dakkar-surface-button:hover, " T "treeview.view header button:hover"
        " {background-color:alpha(@theme_fg_color,0.13); border-color:alpha(@theme_fg_color,0.22);}\n"
        T "toolbar button:active, " T "toolbar button:checked, " T "toolbar .path-bar button:checked, "
        T "box.toolbar button:active, " T "box.toolbar button:checked, "
        T ".dakkar-search-bar button:checked, " T ".dakkar-search-bar button:active, "
        T ".files-corner-preview button:active, " T "button.dakkar-surface-button:active, " T "treeview.view header button:active"
        " {background-color:alpha(@dakkar_pressed,0.80); border-color:alpha(@dakkar_accent_main,0.70);}\n"
        T "toolbar button:disabled, " T "box.toolbar button:disabled"
        " {background-color:alpha(@theme_fg_color,0.03); border-color:alpha(@theme_fg_color,0.06);}\n"
        /* flat tool buttons have no box at rest */
        T "toolbar button.dakkar-tool-button:not(.dakkar-tool-framed):not(:hover):not(:active):not(:checked)"
        " {background-color:transparent; border-color:transparent;}\n"
        /* framed tool buttons: the custom button colour at the toolbar alpha */
        T "toolbar button.dakkar-tool-button.dakkar-tool-framed {background-color:rgba(%d,%d,%d,%s);}\n"
        /* list headers: one hairline between columns, no box */
        T "treeview.view header button {border-width:0 1px 1px 0; border-radius:0;"
        " background-color:alpha(@theme_fg_color,0.04);}\n"
        /* location and search entries take the surface they sit on */
        T "toolbar entry, " T ".dakkar-search-bar entry {background-image:none; box-shadow:none;"
        " background-color:alpha(@theme_base_color,%s); border-color:alpha(@theme_fg_color,0.18);}\n"
        T "toolbar entry:focus, " T ".dakkar-search-bar entry:focus {border-color:@theme_selected_bg_color;}\n"
        /* search bar and tab bar strips */
        T ".dakkar-search-bar infobar, " T ".dakkar-search-bar infobar > revealer > box, "
        T "notebook > header {background-color:alpha(@theme_fg_color,0.04); background-image:none;"
        " box-shadow:none; border-color:alpha(@theme_fg_color,0.10);}\n"
        T "notebook > header tab:not(:checked):not(:hover) {background-color:transparent;}\n"
        /* zoom sliders, scrollbars */
        T "scale trough {background-color:alpha(@theme_fg_color,0.18); background-image:none;"
        " border-color:transparent; box-shadow:none;}\n"
        T "scale slider {box-shadow:none;}\n"
        T "scrollbar, " T "scrollbar trough {background-color:transparent; background-image:none;"
        " border-color:transparent; box-shadow:none;}\n"
        T "scrollbar slider {background-color:alpha(@theme_fg_color,0.40); border-color:transparent;}\n"
        T "scrollbar slider:hover {background-color:alpha(@theme_fg_color,0.60);}\n"
        T "scrollbar slider:active {background-color:@theme_selected_bg_color;}\n",
        DK_RGB (buttons), a_tool, a_tool);
#undef TB
}
#undef T

/* Window rules for the custom palette.  Only applied in this process to
 * Dakkar's own tagged widgets; the system theme is left alone. */
static gchar *
palette_css (void)
{
    GString *css = g_string_new (NULL);
    gdouble opacity = palette_opacity (DAKKAR_THEME_OPACITY);

    palette_surfaces_css (css, opacity);
    palette_areas_css (css, opacity);
    palette_transparent_css (css, opacity);
    return g_string_free (css, FALSE);
}
#undef DK_RGB_TRUNC
#undef DK_RGB

/* DAKKAR_CSS_DUMP=prefix writes the generated stylesheets to prefix.main.css
 * and prefix.accent.css, for comparing them between builds */
static void
dakkar_css_dump (const gchar *suffix, const gchar *css)
{
    gchar *path;
    if (g_getenv ("DAKKAR_CSS_DUMP") == NULL)
        return;
    path = g_strconcat (g_getenv ("DAKKAR_CSS_DUMP"), suffix, NULL);
    g_file_set_contents (path, css, -1, NULL);
    g_free (path);
}

/* Rebuilds both stylesheets from the settings.  Called on every change of
 * a corner or palette key; widgets are restyled, never recreated. */
static void
nemo_corner_style_refresh (GSettings *settings, gchar *key, gpointer data)
{
    gboolean custom;
    gint radius = 0;
    gchar *base, *palette, *accent, *combined;
    GError *error = NULL;

    if (corner_provider == NULL || nemo_preferences == NULL)
        return;

    if (g_settings_get_boolean (nemo_preferences, NEMO_PREFERENCES_FILES_ROUNDED_CORNERS))
        radius = CLAMP (g_settings_get_int (nemo_preferences, NEMO_PREFERENCES_FILES_CORNER_RADIUS), 2, 32);
    custom = g_settings_get_boolean (nemo_preferences, DAKKAR_THEME_CUSTOM);

    base = corner_base_css (radius);
    palette = custom ? palette_css () : NULL;
    accent = custom ? dakkar_accent_css () : NULL;

    if (accent_provider != NULL &&
        !gtk_css_provider_load_from_data (accent_provider, accent != NULL ? accent : "", -1, &error)) {
        g_warning ("Dakkar accent style: %s", error->message);
        g_clear_error (&error);
    }
    dakkar_css_dump (".accent.css", accent != NULL ? accent : "");

    /* last word: flat toolbar icons never get a fill or border */
    combined = g_strconcat (base, palette != NULL ? palette : "",
        ".nemo-window toolbar button.dakkar-tool-button:not(.dakkar-tool-framed) { "
        "background-color:transparent; border-color:transparent; box-shadow:none; }\n",
        NULL);
    dakkar_css_dump (".main.css", combined);
    if (!gtk_css_provider_load_from_data (corner_provider, combined, -1, &error)) {
        g_warning ("Files corner style: %s", error->message);
        g_clear_error (&error);
    }
    g_free (combined);
    g_free (accent);
    g_free (palette);
    g_free (base);
    dakkar_transparency_update_all ();
}

/* ---------- real window transparency ---------- */

static GSList *dakkar_transparent_windows;

gboolean
dakkar_transparency_supported (GdkScreen *screen)
{
    if (screen == NULL) screen = gdk_screen_get_default ();
    return screen != NULL && gdk_screen_get_rgba_visual (screen) != NULL &&
           gdk_screen_is_composited (screen);
}

gboolean
dakkar_transparency_active (GtkWidget *window)
{
    GdkScreen *screen;
    if (nemo_preferences == NULL || window == NULL) return FALSE;
    screen = gtk_widget_get_screen (window);
    return g_settings_get_boolean (nemo_preferences, DAKKAR_THEME_CUSTOM) &&
           g_settings_get_boolean (nemo_preferences, DAKKAR_THEME_SEE_THROUGH) &&
           dakkar_transparency_supported (screen) &&
           gtk_widget_get_visual (window) == gdk_screen_get_rgba_visual (screen);
}

/* The window background around the surfaces: the Dakkar background
 * colour at the overall opacity. */
gboolean
dakkar_transparency_gap_color (GtkWidget *widget, GdkRGBA *color)
{
    GtkWidget *top = widget != NULL ? gtk_widget_get_toplevel (widget) : NULL;
    gchar *value;
    if (top == NULL || !GTK_IS_WINDOW (top) ||
        !gtk_style_context_has_class (gtk_widget_get_style_context (top), "dakkar-transparent"))
        return FALSE;
    value = g_settings_get_string (nemo_preferences, DAKKAR_THEME_BACKGROUND);
    if (!gdk_rgba_parse (color, value)) gdk_rgba_parse (color, "#303036");
    g_free (value);
    color->alpha = CLAMP (g_settings_get_double (nemo_preferences, DAKKAR_THEME_OPACITY), .30, 1.0);
    return TRUE;
}

static gboolean
dakkar_is_surface (GtkWidget *widget)
{
    GtkStyleContext *context = gtk_widget_get_style_context (widget);
    return GTK_IS_TOOLBAR (widget) ||
           gtk_style_context_has_class (context, "files-corner-view") ||
           gtk_style_context_has_class (context, "files-corner-sidebar") ||
           gtk_style_context_has_class (context, "files-corner-preview");
}

static void
dakkar_clear_surfaces (GtkWidget *widget, gpointer data)
{
    gpointer *ctx = data;
    if (!gtk_widget_is_drawable (widget)) return;
    if (dakkar_is_surface (widget)) {
        GtkAllocation a;
        gint x, y;
        gtk_widget_get_allocation (widget, &a);
        if (gtk_widget_translate_coordinates (widget, GTK_WIDGET (ctx[1]), 0, 0, &x, &y)) {
            cairo_t *surface_cr = ctx[0];
            gint r = 0;
            /* clear the surface's own rounded shape, so its corners keep
             * the window background instead of becoming holes */
            gtk_style_context_get (gtk_widget_get_style_context (widget),
                                   gtk_widget_get_state_flags (widget),
                                   GTK_STYLE_PROPERTY_BORDER_RADIUS, &r, NULL);
            /* one pixel tighter than the CSS shape: its antialiased edge
             * blends onto the window background, never onto a hole */
            r = CLAMP (r, 0, MIN (a.width, a.height) / 2);
            /* 1px inset: the frame's hairline border is drawn onto the
             * window background, so no edge pixel can become a hole */
            x += 1; y += 1; a.width -= 2; a.height -= 2;
            if (a.width <= 0 || a.height <= 0) return;
            cairo_new_sub_path (surface_cr);
            if (r <= 0)
                cairo_rectangle (surface_cr, x, y, a.width, a.height);
            else {
                cairo_arc (surface_cr, x + a.width - r, y + r, r, -G_PI / 2, 0);
                cairo_arc (surface_cr, x + a.width - r, y + a.height - r, r, 0, G_PI / 2);
                cairo_arc (surface_cr, x + r, y + a.height - r, r, G_PI / 2, G_PI);
                cairo_arc (surface_cr, x + r, y + r, r, G_PI, 3 * G_PI / 2);
                cairo_close_path (surface_cr);
            }
        }
        return;
    }
    if (GTK_IS_CONTAINER (widget))
        gtk_container_forall (GTK_CONTAINER (widget), dakkar_clear_surfaces, data);
}

/* One translucent layer per pixel: the window background is painted only
 * AROUND the surfaces (sidebar, view, details, toolbar), which paint their
 * own colour and alpha onto a cleared area.  Painting it everywhere would
 * stack two translucent layers under every surface. */
static gboolean
dakkar_transparency_window_draw (GtkWidget *window, cairo_t *cr, gpointer data)
{
    GdkRGBA gap;
    gpointer ctx[2];
    if (!dakkar_transparency_gap_color (window, &gap)) return FALSE;
    cairo_save (cr);
    cairo_set_operator (cr, CAIRO_OPERATOR_SOURCE);
    gdk_cairo_set_source_rgba (cr, &gap);
    cairo_paint (cr);
    cairo_new_path (cr);
    ctx[0] = cr; ctx[1] = window;
    gtk_container_forall (GTK_CONTAINER (window), dakkar_clear_surfaces, ctx);
    cairo_set_operator (cr, CAIRO_OPERATOR_CLEAR);
    cairo_fill (cr);
    cairo_restore (cr);
    return FALSE;
}

static void
dakkar_transparency_update (GtkWidget *window)
{
    GtkStyleContext *context = gtk_widget_get_style_context (window);
    gboolean active = dakkar_transparency_active (window);
    gdouble whole = 1.0;
    if (active)
        gtk_style_context_add_class (context, "dakkar-transparent");
    else
        gtk_style_context_remove_class (context, "dakkar-transparent");
    /* whole-window opacity also needs a compositor: never dim an opaque
     * window that cannot blend */
    if (nemo_preferences != NULL &&
        g_settings_get_boolean (nemo_preferences, DAKKAR_THEME_CUSTOM) &&
        dakkar_transparency_supported (gtk_widget_get_screen (window)))
        whole = CLAMP (g_settings_get_double (nemo_preferences,
                                              DAKKAR_THEME_WHOLE_WINDOW_OPACITY), .30, 1.0);
    if (ABS (gtk_widget_get_opacity (window) - whole) > 0.001)
        gtk_widget_set_opacity (window, whole);
    gtk_widget_queue_draw (window);
}

static void
dakkar_transparency_update_all (void)
{
    GSList *l;
    for (l = dakkar_transparent_windows; l != NULL; l = l->next)
        dakkar_transparency_update (GTK_WIDGET (l->data));
}

static void
dakkar_transparency_window_gone (gpointer data, GObject *where_the_object_was)
{
    dakkar_transparent_windows = g_slist_remove (dakkar_transparent_windows, where_the_object_was);
}

static void
dakkar_transparency_composited_changed (GdkScreen *screen, gpointer data)
{
    dakkar_transparency_update_all ();
}

void
dakkar_transparency_attach (GtkWindow *window)
{
    static gboolean screen_hooked;
    GdkScreen *screen;
    GdkVisual *visual;
    g_return_if_fail (GTK_IS_WINDOW (window));
    screen = gtk_widget_get_screen (GTK_WIDGET (window));
    visual = screen != NULL ? gdk_screen_get_rgba_visual (screen) : NULL;
    /* The visual can only be chosen before the window is realized.  It is
     * set whenever the screen has one, also without a compositor right
     * now, so a compositor started later makes the window see-through
     * live; until then every pixel is painted with alpha 1. */
    if (visual != NULL && !gtk_widget_get_realized (GTK_WIDGET (window)))
        gtk_widget_set_visual (GTK_WIDGET (window), visual);
    if (g_slist_find (dakkar_transparent_windows, window) == NULL) {
        dakkar_transparent_windows = g_slist_prepend (dakkar_transparent_windows, window);
        g_object_weak_ref (G_OBJECT (window), dakkar_transparency_window_gone, NULL);
        g_signal_connect (window, "draw", G_CALLBACK (dakkar_transparency_window_draw), NULL);
    }
    if (!screen_hooked && screen != NULL) {
        screen_hooked = TRUE;
        g_signal_connect (screen, "composited-changed",
                          G_CALLBACK (dakkar_transparency_composited_changed), NULL);
        if (nemo_preferences != NULL) {
            g_signal_connect_swapped (nemo_preferences, "changed::" DAKKAR_THEME_SEE_THROUGH,
                                      G_CALLBACK (dakkar_transparency_update_all), NULL);
            g_signal_connect_swapped (nemo_preferences, "changed::" DAKKAR_THEME_WHOLE_WINDOW_OPACITY,
                                      G_CALLBACK (dakkar_transparency_update_all), NULL);
        }
    }
    dakkar_transparency_update (GTK_WIDGET (window));
}

static gboolean
dakkar_icon_pack_available (const gchar *selected)
{
    const gchar *dirs[] = {"/usr/share/icons", "/usr/local/share/icons", NULL};
    gchar *index;
    gboolean available = FALSE;
    guint i;
    for (i = 0; dirs[i] != NULL && !available; i++) {
        index = g_build_filename (dirs[i], selected, "index.theme", NULL);
        available = g_file_test (index, G_FILE_TEST_IS_REGULAR);
        g_free (index);
    }
    if (!available) {
        index = g_build_filename (g_get_user_data_dir (), "icons", selected,
                                  "index.theme", NULL);
        available = g_file_test (index, G_FILE_TEST_IS_REGULAR);
        g_free (index);
    }
    if (!available) {
        index = g_build_filename (g_get_home_dir (), ".icons", selected,
                                  "index.theme", NULL);
        available = g_file_test (index, G_FILE_TEST_IS_REGULAR);
        g_free (index);
    }
    return available;
}

/* The sidebar icon pack only applies to the Places sidebar.  The default
 * GtkIconTheme is screen-specific and must not get a custom theme (that was
 * the startup CRITICAL "!priv->is_screen_specific"), so the sidebar uses its
 * own private GtkIconTheme.  NULL means: use the system theme. */
GtkIconTheme *
dakkar_sidebar_icon_theme (void)
{
    static GtkIconTheme *theme = NULL;
    static gchar *theme_name = NULL;
    gchar *selected;
    if (nemo_preferences == NULL) return NULL;
    selected = g_settings_get_string (nemo_preferences, DAKKAR_SIDEBAR_ICON_PACK);
    if (g_strcmp0 (selected, "system") == 0 || selected == NULL || *selected == '\0') {
        g_clear_object (&theme);
        g_clear_pointer (&theme_name, g_free);
        g_free (selected);
        return NULL;
    }
    if (theme != NULL && g_strcmp0 (selected, theme_name) == 0) {
        g_free (selected);
        return theme;
    }
    g_clear_object (&theme);
    g_clear_pointer (&theme_name, g_free);
    if (dakkar_icon_pack_available (selected)) {
        theme = gtk_icon_theme_new ();
        if (gdk_screen_get_default () != NULL)
            gtk_icon_theme_set_screen (theme, gdk_screen_get_default ());
        gtk_icon_theme_set_custom_theme (theme, selected);
        theme_name = selected;
        return theme;
    }
    g_free (selected);
    return NULL;
}

static gboolean
dakkar_icon_available (const gchar *name)
{
    GtkIconTheme *theme = gtk_icon_theme_get_default ();
    return name != NULL && theme != NULL && gtk_icon_theme_has_icon (theme, name);
}

const gchar *
dakkar_icon_pick (const gchar *first, ...)
{
    va_list args;
    const gchar *name, *last = first;

    if (first == NULL || dakkar_icon_available (first))
        return first;
    va_start (args, first);
    while ((name = va_arg (args, const gchar *)) != NULL) {
        last = name;
        if (dakkar_icon_available (name)) {
            va_end (args);
            return name;
        }
    }
    va_end (args);
    return last;
}

const gchar *
dakkar_icon_resolve (const gchar *name)
{
    /* requested name -> themed alternatives, best first */
    static const struct { const gchar *name; const gchar *alt[4]; } aliases[] = {
        {"view-compare-symbolic", {"kr_comparedirs-symbolic", "document-compareleft-symbolic", "edit-copy-symbolic", NULL}},
        {"git-symbolic", {"vcs-branch-symbolic", "vcs-commit-symbolic", "folder-git-symbolic", NULL}},
        {"view-compact-symbolic", {"view-list-text-symbolic", NULL}},
        {"xapp-prefs-preview-symbolic", {"nemo-prefs-preview-symbolic", "view-preview-symbolic", "image-x-generic-symbolic", NULL}},
        {"xapp-prefs-behavior-symbolic", {"nemo-prefs-behavior-symbolic", "configure-symbolic", "preferences-system-symbolic", NULL}},
        {"xapp-prefs-display-symbolic", {"nemo-prefs-display-symbolic", "view-visible-symbolic", NULL}},
        {"xapp-prefs-toolbar-symbolic", {"nemo-prefs-toolbar-symbolic", "configure-toolbars-symbolic", NULL}},
        {"xapp-prefs-plugins-symbolic", {"nemo-prefs-plugins-symbolic", "plugins-symbolic", "application-x-addon-symbolic", NULL}},
        {"preferences-desktop-theme-symbolic", {"preferences-desktop-theme-global-symbolic", "preferences-desktop-appearance-symbolic", "applications-graphics-symbolic", NULL}},
    };
    guint i, j;

    if (name == NULL || dakkar_icon_available (name))
        return name;
    for (i = 0; i < G_N_ELEMENTS (aliases); i++) {
        if (g_strcmp0 (aliases[i].name, name) != 0)
            continue;
        for (j = 0; j < G_N_ELEMENTS (aliases[i].alt) && aliases[i].alt[j] != NULL; j++)
            if (dakkar_icon_available (aliases[i].alt[j]))
                return aliases[i].alt[j];
    }
    return name;
}

const gchar *
dakkar_view_icon_name (const gchar *view_action)
{
    if (g_strcmp0 (view_action, NEMO_ACTION_LIST_VIEW) == 0)
        return dakkar_icon_pick ("view-list-details-symbolic", "view-list-symbolic", NULL);
    if (g_strcmp0 (view_action, NEMO_ACTION_COMPACT_VIEW) == 0)
        return dakkar_icon_pick ("view-list-text-symbolic", "view-compact-symbolic",
                                "view-list-symbolic", NULL);
    if (g_strcmp0 (view_action, NEMO_ACTION_MILLER_VIEW) == 0)
        return dakkar_icon_pick ("view-file-columns-symbolic", "view-columns-symbolic",
                                "view-column-symbolic", "view-dual-symbolic",
                                "view-list-symbolic", NULL);
    return dakkar_icon_pick ("view-list-icons-symbolic", "view-grid-symbolic", NULL);
}

void
nemo_corner_style_ensure (void)
{
    GdkScreen *screen;

    if (corner_provider != NULL)
        return;

    screen = gdk_screen_get_default ();
    if (screen == NULL || nemo_preferences == NULL)
        return;

    corner_provider = gtk_css_provider_new ();
    /* Overrides the older Miller-specific hardcoded border radii only on
     * explicitly tagged outer frames, leaving GTK theme button styling. */
    gtk_style_context_add_provider_for_screen (
        screen, GTK_STYLE_PROVIDER (corner_provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 2);
    accent_provider = gtk_css_provider_new ();
    gtk_style_context_add_provider_for_screen (
        screen, GTK_STYLE_PROVIDER (accent_provider),
        GTK_STYLE_PROVIDER_PRIORITY_USER + 10);
    g_signal_connect (nemo_preferences,
                      "changed::" NEMO_PREFERENCES_FILES_ROUNDED_CORNERS,
                      G_CALLBACK (nemo_corner_style_refresh), NULL);
    g_signal_connect (nemo_preferences,
                      "changed::" NEMO_PREFERENCES_FILES_CORNER_RADIUS,
                      G_CALLBACK (nemo_corner_style_refresh), NULL);
    const char *custom_keys[] = {DAKKAR_THEME_CUSTOM, DAKKAR_THEME_BACKGROUND,
        DAKKAR_THEME_SIDEBAR, DAKKAR_THEME_INSPECTOR, DAKKAR_THEME_TEXT,
        DAKKAR_THEME_ACCENT, DAKKAR_THEME_ACCENT_SECONDARY, DAKKAR_THEME_QA_BUTTON, DAKKAR_THEME_QA_TEXT, DAKKAR_THEME_QA_HOVER, DAKKAR_THEME_QA_BORDER, DAKKAR_THEME_STATUSBAR, DAKKAR_THEME_TOOLBAR, DAKKAR_THEME_TABS, DAKKAR_THEME_BUTTONS, DAKKAR_THEME_BORDERS, DAKKAR_THEME_HOVER, DAKKAR_THEME_MENUS, DAKKAR_THEME_SIDEBAR_HEADING, DAKKAR_THEME_OPACITY,
        DAKKAR_THEME_SIDEBAR_OPACITY, DAKKAR_THEME_DETAILS_OPACITY,
        DAKKAR_THEME_TOOLBAR_OPACITY, DAKKAR_SIDEBAR_SELECTION_RADIUS};
    guint i;
    for (i = 0; i < G_N_ELEMENTS (custom_keys); i++) {
        gchar *signal = g_strdup_printf ("changed::%s", custom_keys[i]);
        g_signal_connect (nemo_preferences, signal,
                          G_CALLBACK (nemo_corner_style_refresh), NULL);
        g_free (signal);
    }
    nemo_corner_style_refresh (NULL, NULL, NULL);
}

/* GTK3 does not clip arbitrary container children to a CSS border radius.
 * Never use GtkOverlay for this mask: even a nominally pass-through overlay
 * can intercept events and break clicks in Miller AND in the places sidebar.
 * Draw the mask on a no-window GtkBox AFTER its children have been painted.
 * That box has no overlay/input surface and preserves the original child
 * event windows and pointer/focus behavior.
 */
static gboolean
files_corner_mask_draw (GtkWidget *area, cairo_t *cr, gpointer user_data)
{
    GtkAllocation allocation;
    GdkRGBA background;
    const gchar *color_name;
    gint radius, w, h;
    if (nemo_preferences == NULL ||
        !g_settings_get_boolean (nemo_preferences,
                                 NEMO_PREFERENCES_FILES_ROUNDED_CORNERS))
        return FALSE;
    radius = CLAMP (g_settings_get_int (nemo_preferences,
                        NEMO_PREFERENCES_FILES_CORNER_RADIUS), 2, 32);
    gtk_widget_get_allocation (area, &allocation);
    w = allocation.width;
    h = allocation.height;
    radius = MIN (radius, MIN (w / 2, h / 2));
    if (radius < 1)
        return FALSE;

    color_name = g_object_get_data (G_OBJECT (area), "files-mask-bg-color");
    if (color_name == NULL) color_name = "theme_bg_color";
    /* Sample color from the enclosing frame, not the inner content: this
     * prevents a dark square outline around a light rounded view. */
    GtkWidget *parent = gtk_widget_get_parent (area);
    GtkStyleContext *paint_context = GTK_IS_WIDGET (parent) ?
        gtk_widget_get_style_context (parent) : gtk_widget_get_style_context (area);
    if (!gtk_style_context_lookup_color (paint_context, color_name, &background)) {
        /* Theme fallback, using the current GTK root background if needed. */
        GtkWidget *top = gtk_widget_get_toplevel (area);
        if (GTK_IS_WIDGET (top))
            gtk_style_context_get_background_color (
                gtk_widget_get_style_context (top),
                GTK_STATE_FLAG_NORMAL, &background);
        else {
            /* No arbitrary dark fill when no toplevel is available (e.g.
             * sample/theme switch). A transparent paint is safer. */
            background.red = 0.0;
            background.green = 0.0;
            background.blue = 0.0;
            background.alpha = 0.0;
        }
    }
    /* Draw the FOUR exterior corner cutouts. The previous implementation
     * combined a rectangle and an arc in the same Cairo subpath; it didn't
     * form a rounded rectangular hole and could leave straight corners.
     * A full rectangular contour minus a closed rounded-rectangle contour
     * is deterministic for all corner radii and window sizes. */
    /* See-through window: the corners must show exactly the window
     * background around the surface (replace, never blend on top). */
    if (dakkar_transparency_gap_color (area, &background))
        cairo_set_operator (cr, CAIRO_OPERATOR_SOURCE);
    gdk_cairo_set_source_rgba (cr, &background);
    cairo_new_path (cr);
    cairo_rectangle (cr, 0, 0, w, h);
    cairo_new_sub_path (cr);
    cairo_arc (cr, radius, radius, radius, G_PI, 3.0 * G_PI / 2.0);
    cairo_arc (cr, w - radius, radius, radius, 3.0 * G_PI / 2.0, 2.0 * G_PI);
    cairo_arc (cr, w - radius, h - radius, radius, 0, G_PI / 2.0);
    cairo_arc (cr, radius, h - radius, radius, G_PI / 2.0, G_PI);
    cairo_close_path (cr);
    cairo_set_fill_rule (cr, CAIRO_FILL_RULE_EVEN_ODD);
    cairo_fill (cr);
    cairo_set_operator (cr, CAIRO_OPERATOR_OVER);
    cairo_set_fill_rule (cr, CAIRO_FILL_RULE_WINDING);
    return FALSE;
}

static void
files_corner_mask_changed (GSettings *settings, gchar *key, gpointer data)
{
    gtk_widget_queue_draw (GTK_WIDGET (data));
}

/* Color variants and GTK themes can change without either rounded-corner
 * preference changing. Repaint the four cutouts on the existing wrappers;
 * reconnect with g_signal_connect_object so the widget owns the handler's
 * lifetime and closed tabs never receive a stale draw request. */
static void
files_corner_gtk_theme_changed (GObject *settings, GParamSpec *pspec,
                                gpointer data)
{
    gtk_widget_queue_draw (GTK_WIDGET (data));
}

GtkWidget *
nemo_corner_style_wrap (GtkWidget *child, const gchar *background_color)
{
    GtkWidget *wrapper;
    g_return_val_if_fail (GTK_IS_WIDGET (child), NULL);
    g_return_val_if_fail (gtk_widget_get_parent (child) == NULL, NULL);

    /* GtkBox has no input window and cannot occlude pointer events on child
     * GtkTreeViews.  Unlike GtkOverlay this is safe for Nemo's view hierarchy. */
    wrapper = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_hexpand (wrapper, TRUE);
    gtk_widget_set_vexpand (wrapper, TRUE);
    gtk_box_pack_start (GTK_BOX (wrapper), child, TRUE, TRUE, 0);
    g_object_set_data_full (G_OBJECT (wrapper), "files-mask-bg-color",
                            g_strdup (background_color ? background_color : "theme_bg_color"),
                            g_free);
    /* Connect AFTER GtkContainer's default draw, so the mask covers corners
     * of the rendered child but never catches an input event. */
    g_signal_connect_after (wrapper, "draw", G_CALLBACK (files_corner_mask_draw), NULL);
    if (nemo_preferences != NULL) {
        g_signal_connect_object (nemo_preferences,
                    "changed::" NEMO_PREFERENCES_FILES_ROUNDED_CORNERS,
                    G_CALLBACK (files_corner_mask_changed), wrapper, 0);
        g_signal_connect_object (nemo_preferences,
                    "changed::" NEMO_PREFERENCES_FILES_CORNER_RADIUS,
                    G_CALLBACK (files_corner_mask_changed), wrapper, 0);
        /* The cutout is a drawn mask; re-render when live custom colors change. */
        const gchar *mask_keys[] = {DAKKAR_THEME_CUSTOM, DAKKAR_THEME_BACKGROUND,
            DAKKAR_THEME_SIDEBAR, DAKKAR_THEME_INSPECTOR, DAKKAR_THEME_OPACITY,
            DAKKAR_THEME_SIDEBAR_OPACITY, DAKKAR_THEME_DETAILS_OPACITY};
        guint mask_index;
        for (mask_index=0; mask_index < G_N_ELEMENTS (mask_keys); mask_index++) {
            gchar *signal = g_strconcat ("changed::", mask_keys[mask_index], NULL);
            g_signal_connect_object (nemo_preferences, signal,
                G_CALLBACK (files_corner_mask_changed), wrapper, 0);
            g_free (signal);
        }
    }
    GtkSettings *gtk_settings = gtk_settings_get_default ();
    if (gtk_settings != NULL) {
        g_signal_connect_object (gtk_settings, "notify::gtk-theme-name",
                    G_CALLBACK (files_corner_gtk_theme_changed), wrapper, 0);
        g_signal_connect_object (gtk_settings,
                    "notify::gtk-application-prefer-dark-theme",
                    G_CALLBACK (files_corner_gtk_theme_changed), wrapper, 0);
    }
    gtk_widget_show (wrapper);
    return wrapper;
}
