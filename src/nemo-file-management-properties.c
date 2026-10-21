/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-file-management-properties.c - Functions to create and show the nemo preference dialog.

   Copyright (C) 2002 Jan Arne Petersen

   The Gnome Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   The Gnome Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public
   License along with the Gnome Library; see the file COPYING.LIB.  If not,
   write to the Free Software Foundation, Inc., 51 Franklin Street - Suite 500,
   Boston, MA 02110-1335, USA.

   Authors: Jan Arne Petersen <jpetersen@uni-bonn.de>
*/

#include <config.h>
#include "dakkar-favorites.h"
#include "nemo-corner-style.h"

#include "nemo-file-management-properties.h"

#include <string.h>
#include <time.h>
#include <gtk/gtk.h>
#include <gio/gio.h>

#include <glib/gi18n.h>

#include <eel/eel-glib-extensions.h>

#include <libnemo-private/nemo-column-chooser.h>
#include <libnemo-private/nemo-column-utilities.h>
#include <libnemo-private/nemo-global-preferences.h>
#include <libnemo-private/nemo-module.h>

#include "nemo-plugin-manager.h"
#include "nemo-actions.h"
#include "nemo-quick-actions.h"
#include "nemo-window-manage-views.h"
#include <libnemo-private/nemo-icon-names.h>
#include "nemo-window-menus.h"

/* string enum preferences */
#define NEMO_FILE_MANAGEMENT_PROPERTIES_DEFAULT_VIEW_WIDGET "default_view_combobox"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_SPLIT_VIEW_DEFAULT_VIEW_WIDGET "split_view_default_view_combobox"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_ICON_VIEW_ZOOM_WIDGET "icon_view_zoom_combobox"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_COMPACT_VIEW_ZOOM_WIDGET "compact_view_zoom_combobox"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_LIST_VIEW_ZOOM_WIDGET "list_view_zoom_combobox"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_SORT_ORDER_WIDGET "sort_order_combobox"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_DATE_FORMAT_WIDGET "date_format_combobox"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_DATE_FONT_CHOICE_WIDGET "date_font_choice_combobox"

#define NEMO_FILE_MANAGEMENT_PROPERTIES_PREVIEW_IMAGE_WIDGET "preview_image_combobox"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_PREVIEW_FOLDER_WIDGET "preview_folder_combobox"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_SIZE_PREFIXES_WIDGET "size_prefixes_combobox"

/* bool preferences */
#define NEMO_FILE_MANAGEMENT_PROPERTIES_INHERIT_VIEW_WIDGET "inherit_view_checkbox"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_REVERSE_SORT_WIDGET "reverse_sort_checkbox"
#define NEMO_FILE_MANAGEMENT_QUICK_RENAMES_WITH_PAUSE_IN_BETWEEN "quick_renames_with_pause_in_between"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_FAVORITES_FIRST_WIDGET "sort_favorites_first_checkbutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_FOLDERS_FIRST_WIDGET "sort_folders_first_checkbutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_COMPACT_LAYOUT_WIDGET "compact_layout_checkbutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_LABELS_BESIDE_ICONS_WIDGET "labels_beside_icons_checkbutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_ALL_COLUMNS_SAME_WIDTH "all_columns_same_width_checkbutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_ALWAYS_USE_BROWSER_WIDGET "always_use_browser_checkbutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_TRASH_CONFIRM_MOVE_WIDGET "trash_confirm_move_checkbutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_TRASH_CONFIRM_WIDGET "trash_confirm_checkbutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_TRASH_DELETE_WIDGET "trash_delete_checkbutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_SWAP_TRASH_DELETE "swap_trash_binding_checkbutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_OPEN_NEW_WINDOW_WIDGET "new_window_checkbutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_TREE_VIEW_FOLDERS_WIDGET "treeview_folders_checkbutton"

#define NEMO_FILE_MANAGEMENT_PROPERTIES_SHOW_PREVIOUS_ICON_TOOLBAR_WIDGET "show_previous_icon_toolbar_togglebutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_SHOW_NEXT_ICON_TOOLBAR_WIDGET "show_next_icon_toolbar_togglebutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_SHOW_UP_ICON_TOOLBAR_WIDGET "show_up_icon_toolbar_togglebutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_SHOW_RELOAD_ICON_TOOLBAR_WIDGET "show_reload_icon_toolbar_togglebutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_SHOW_EDIT_ICON_TOOLBAR_WIDGET "show_edit_icon_toolbar_togglebutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_SHOW_HOME_ICON_TOOLBAR_WIDGET "show_home_icon_toolbar_togglebutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_SHOW_COMPUTER_ICON_TOOLBAR_WIDGET "show_computer_icon_toolbar_togglebutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_SHOW_SEARCH_ICON_TOOLBAR_WIDGET "show_search_icon_toolbar_togglebutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_SHOW_NEW_FOLDER_ICON_TOOLBAR_WIDGET "show_new_folder_icon_toolbar_togglebutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_SHOW_OPEN_IN_TERMINAL_ICON_TOOLBAR_WIDGET "show_open_in_terminal_icon_toolbar_togglebutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_SHOW_ICON_VIEW_ICON_TOOLBAR_WIDGET "show_icon_view_icon_toolbar_togglebutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_SHOW_LIST_VIEW_ICON_TOOLBAR_WIDGET "show_list_view_icon_toolbar_togglebutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_SHOW_COMPACT_VIEW_ICON_TOOLBAR_WIDGET "show_compact_view_icon_toolbar_togglebutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_SHOW_SHOW_THUMBNAILS_ICON_TOOLBAR_WIDGET "show_show_thumbnails_icon_toolbar_togglebutton"

#define NEMO_FILE_MANAGEMENT_PROPERTIES_SHOW_FULL_PATH_IN_TITLE_BARS_WIDGET "show_full_path_in_title_bars_checkbutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_CLOSE_DEVICE_VIEW_ON_EJECT_WIDGET "close_device_view_on_eject_checkbutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_AUTOMOUNT_MEDIA_WIDGET "media_automount_checkbutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_AUTOOPEN_MEDIA_WIDGET "media_autoopen_checkbutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_AUTORUN_MEDIA_WIDGET "media_autorun_checkbutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_DETECT_CONTENT_MEDIA_WIDGET "media_detect_content_checkbutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_SHOW_ADVANCED_PERMISSIONS_WIDGET "show_advanced_permissions_checkbutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_START_WITH_DUAL_PANE_WIDGET "start_with_dual_pane_checkbutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_IGNORE_VIEW_METADATA_WIDGET "ignore_view_metadata_checkbutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_BOOKMARKS_IN_TO_MENUS_WIDGET "bookmarks_in_to_checkbutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_PLACES_IN_TO_MENUS_WIDGET "places_in_to_checkbutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_INHERIT_SHOW_THUMBNAILS_WIDGET "inherit_show_thumbnails_checkbutton"

#define NEMO_FILE_MANAGEMENT_PROPERTIES_BULK_RENAME_WIDGET "bulk_rename_entry"

#define NEMO_FILE_MANAGEMENT_PROPERTIES_TOOLTIPS_ON_ICON_VIEW_WIDGET "tooltips_on_icon_view_checkbutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_TOOLTIPS_ON_LIST_VIEW_WIDGET "tooltips_on_list_view_checkbutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_TOOLTIPS_ON_DESKTOP_WIDGET "tooltips_on_desktop_checkbutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_TOOLTIP_FILE_TYPE_WIDGET "tt_show_file_type_checkbutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_TOOLTIP_MOD_DATE_WIDGET "tt_show_modified_date_checkbutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_TOOLTIP_ACCESS_DATE_WIDGET "tt_show_accessed_date_checkbutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_TOOLTIP_CREATED_DATE_WIDGET "tt_show_created_date_checkbutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_TOOLTIP_FULL_PATH_WIDGET "tt_show_full_path_checkbutton"

#define NEMO_FILE_MANAGEMENT_PROPERTIES_NEMO_PREFERENCES_SKIP_FILE_OP_QUEUE_WIDGET "skip_file_op_queue_checkbutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_NEMO_PREFERENCES_CLICK_DBL_PARENT_FOLDER_WIDGET "click_double_parent_folder_checkbutton"
#define NEMO_FILE_MANAGEMENT_PROPERTIES_NEMO_PREFERENCES_EXPAND_ROW_ON_DND_DWELL_WIDGET "expand_row_on_dnd_dwell_checkbutton"

/* int enums */
#define NEMO_FILE_MANAGEMENT_PROPERTIES_THUMBNAIL_LIMIT_WIDGET "preview_image_size_combobox"

#define W(s) (gtk_builder_get_object (builder, s))

#define TOOLBAR_PADDING 20

static const char * const default_view_values[] = {
	"icon-view",
	"list-view",
	"compact-view",
	"miller-columns-view",
	NULL
};

static const char * const zoom_values[] = {
	"smallest",
	"smaller",
	"small",
	"standard",
	"large",
	"larger",
	"largest",
	NULL
};

static const char * const sort_order_values[] = {
	"name",
	"size",
	"type",
    "detailed_type",
	"mtime",
	"atime",
	"trash-time",
	NULL
};

static const char * const date_format_values[] = {
	"locale",
	"iso",
	"informal",
	NULL
};

static const char * const date_font_choice_values[] = {
    "auto-mono",
    "system-mono",
    "no-mono",
    NULL
};

static const char * const preview_image_values[] = {
    "always",
    "local-only",
    "never",
    NULL
};

static const char * const preview_folder_values[] = {
	"always",
	"local-only",
	"never",
	NULL
};

static const char * const click_behavior_components[] = {
	"single_click_radiobutton",
	"double_click_radiobutton",
	NULL
};

static const char * const click_behavior_values[] = {
	"single",
	"double",
	NULL
};

static const char * const executable_text_components[] = {
	"scripts_execute_radiobutton",
	"scripts_view_radiobutton",
	"scripts_confirm_radiobutton",
	NULL
};

static const char * const executable_text_values[] = {
	"launch",
	"display",
	"ask",
	NULL
};

static const char * const size_prefixes_values[] = {
	"base-10",
	"base-10-full",
	"base-2",
	"base-2-full",
	NULL
};

static const guint64 thumbnail_limit_values[] = {
	102400,
	512000,
	1048576,
	3145728,
	5242880,
	10485760,
	104857600,
	1073741824,
	2147483648U,
	4294967295U,
	8589934592U,
	17179869184U,
	34359738368U,
	68719476736U
};

static const char * const icon_captions_components[] = {
	"captions_0_combobox",
	"captions_1_combobox",
	"captions_2_combobox",
	NULL
};

static GtkWidget *preferences_dialog = NULL;

static void
nemo_file_management_properties_size_group_create (GtkBuilder *builder,
						       char *prefix,
						       int items)
{
	GtkSizeGroup *size_group;
	int i;
	char *item_name;
	GtkWidget *widget;

	size_group = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);

	for (i = 0; i < items; i++) {	
		item_name = g_strdup_printf ("%s_%d", prefix, i);
		widget = GTK_WIDGET (gtk_builder_get_object (builder, item_name));
		gtk_size_group_add_widget (size_group, widget);
		g_free (item_name);
	}
	g_object_unref (G_OBJECT (size_group));
}

static void
columns_changed_callback (NemoColumnChooser *chooser,
			  gpointer callback_data)
{
	char **visible_columns;
	char **column_order;

	nemo_column_chooser_get_settings (NEMO_COLUMN_CHOOSER (chooser),
					      &visible_columns,
					      &column_order);

	g_settings_set_strv (nemo_list_view_preferences,
			     NEMO_PREFERENCES_LIST_VIEW_DEFAULT_VISIBLE_COLUMNS,
			     (const char * const *)visible_columns);
	g_settings_set_strv (nemo_list_view_preferences,
			     NEMO_PREFERENCES_LIST_VIEW_DEFAULT_COLUMN_ORDER,
			     (const char * const *)column_order);

	g_strfreev (visible_columns);
	g_strfreev (column_order);
}

static void
free_column_names_array (GPtrArray *column_names)
{
	g_ptr_array_foreach (column_names, (GFunc) g_free, NULL);
	g_ptr_array_free (column_names, TRUE);
}

static void
create_icon_caption_combo_box_items (GtkComboBoxText *combo_box,
			             GList *columns)
{
	GList *l;
	GPtrArray *column_names;

	column_names = g_ptr_array_new ();

	/* Translators: this is referred to captions under icons. */
	gtk_combo_box_text_append_text (combo_box, _("None"));
	g_ptr_array_add (column_names, g_strdup ("none"));

	for (l = columns; l != NULL; l = l->next) {
		NemoColumn *column;
		char *name;
		char *label;

		column = NEMO_COLUMN (l->data);

		g_object_get (G_OBJECT (column), 
			      "name", &name, "label", &label, 
			      NULL);

		/* Don't show name here, it doesn't make sense */
		if (!strcmp (name, "name")) {
			g_free (name);
			g_free (label);
			continue;
		}

		gtk_combo_box_text_append_text (combo_box, label);
		g_ptr_array_add (column_names, name);

		g_free (label);
	}
	g_object_set_data_full (G_OBJECT (combo_box), "column_names",
			        column_names,
			        (GDestroyNotify) free_column_names_array);
}

static void
icon_captions_changed_callback (GtkComboBox *combo_box,
				gpointer user_data)
{
	GPtrArray *captions;
	GtkBuilder *builder;
	guint i;

	builder = GTK_BUILDER (user_data);

	captions = g_ptr_array_new ();

	for (i = 0; icon_captions_components[i] != NULL; i++) {
		int active;
		GPtrArray *column_names;
		char *name;
		GtkWidget *c_box;

		c_box = GTK_WIDGET (gtk_builder_get_object
					(builder, icon_captions_components[i]));
		active = gtk_combo_box_get_active (GTK_COMBO_BOX (c_box));

		column_names = g_object_get_data (G_OBJECT (c_box),
						  "column_names");

		name = g_ptr_array_index (column_names, active);
		g_ptr_array_add (captions, name);
	}
	g_ptr_array_add (captions, NULL);

	g_settings_set_strv (nemo_icon_view_preferences,
			     NEMO_PREFERENCES_ICON_VIEW_CAPTIONS,
			     (const char **)captions->pdata);
	g_ptr_array_free (captions, TRUE);
}

static void
update_caption_combo_box (GtkBuilder *builder,
			  const char *combo_box_name,
			  const char *name)
{
	GtkWidget *combo_box;
	guint i;
	GPtrArray *column_names;

	combo_box = GTK_WIDGET (gtk_builder_get_object (builder, combo_box_name));

	g_signal_handlers_block_by_func
		(combo_box,
		 G_CALLBACK (icon_captions_changed_callback),
		 builder);

	column_names = g_object_get_data (G_OBJECT (combo_box), 
					  "column_names");

	for (i = 0; i < column_names->len; ++i) {
		if (!strcmp (name, g_ptr_array_index (column_names, i))) {
			gtk_combo_box_set_active (GTK_COMBO_BOX (combo_box), i);
			break;
		}
	}

	g_signal_handlers_unblock_by_func
		(combo_box,
		 G_CALLBACK (icon_captions_changed_callback),
		 builder);
}

static void
update_icon_captions_from_settings (GtkBuilder *builder)
{
	char **captions;
	int i, j;

	captions = g_settings_get_strv (nemo_icon_view_preferences, NEMO_PREFERENCES_ICON_VIEW_CAPTIONS);
	if (captions == NULL)
		return;

	for (i = 0, j = 0; 
	     icon_captions_components[i] != NULL;
	     i++) {
		char *data;

		if (captions[j]) {
			data = captions[j];
			++j;
		} else {
			data = (char *)"none";
		}

		update_caption_combo_box (builder, 
					  icon_captions_components[i],
					  data);
	}

	g_strfreev (captions);
}

static void
nemo_file_management_properties_dialog_setup_icon_caption_page (GtkBuilder *builder)
{
	GList *columns;
	int i;
	gboolean writable;

	writable = g_settings_is_writable (nemo_icon_view_preferences,
					   NEMO_PREFERENCES_ICON_VIEW_CAPTIONS);

	columns = nemo_get_common_columns ();

	for (i = 0; icon_captions_components[i] != NULL; i++) {
		GtkWidget *combo_box;

		combo_box = GTK_WIDGET (gtk_builder_get_object (builder,
								icon_captions_components[i]));

		create_icon_caption_combo_box_items (GTK_COMBO_BOX_TEXT (combo_box), columns);
		gtk_widget_set_sensitive (combo_box, writable);

		g_signal_connect (combo_box, "changed",
				  G_CALLBACK (icon_captions_changed_callback),
				  builder);
	}

	nemo_column_list_free (columns);

	update_icon_captions_from_settings (builder);
}

/* "About Dakkar" page at the bottom of the Preferences sidebar, for
 * users without the menubar (Help > About).  Plain widgets only: colours
 * come from the theme / Dakkar custom palette like the other pages. */
static void
dakkar_about_page_open_dialog (GtkButton *button, gpointer user_data)
{
    GtkWidget *toplevel = gtk_widget_get_toplevel (GTK_WIDGET (button));
    dakkar_show_about_dialog (GTK_IS_WINDOW (toplevel) ? GTK_WINDOW (toplevel) : NULL);
    (void) user_data;
}

static GtkWidget *
dakkar_about_page_label (GtkWidget *box, const char *markup, gboolean dim)
{
    GtkWidget *label = gtk_label_new (NULL);
    gtk_label_set_markup (GTK_LABEL (label), markup);
    gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
    gtk_label_set_line_wrap_mode (GTK_LABEL (label), PANGO_WRAP_WORD_CHAR);
    gtk_label_set_justify (GTK_LABEL (label), GTK_JUSTIFY_CENTER);
    gtk_label_set_max_width_chars (GTK_LABEL (label), 52);
    gtk_label_set_width_chars (GTK_LABEL (label), 10);
    gtk_label_set_selectable (GTK_LABEL (label), FALSE);
    gtk_widget_set_halign (label, GTK_ALIGN_CENTER);
    if (dim)
        gtk_style_context_add_class (gtk_widget_get_style_context (label), "dim-label");
    gtk_box_pack_start (GTK_BOX (box), label, FALSE, FALSE, 0);
    return label;
}

static void
dakkar_preferences_setup_about_page (GtkBuilder *builder)
{
    GtkStack *stack = GTK_STACK (gtk_builder_get_object (builder, "page_stack"));
    GtkWidget *scroller, *page, *image, *button, *link;
    gchar *markup;

    page = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width (GTK_CONTAINER (page), 18);
    gtk_widget_set_name (page, "dakkar-prefs-about");

    image = gtk_image_new_from_icon_name (DAKKAR_APP_ICON, GTK_ICON_SIZE_DIALOG);
    gtk_image_set_pixel_size (GTK_IMAGE (image), 96);
    gtk_box_pack_start (GTK_BOX (page), image, FALSE, FALSE, 6);

    markup = g_markup_printf_escaped ("<span size=\"x-large\" weight=\"bold\">%s</span>", _("Dakkar"));
    dakkar_about_page_label (page, markup, FALSE);
    g_free (markup);
    markup = g_markup_printf_escaped (_("Version %s"), VERSION);
    dakkar_about_page_label (page, markup, TRUE);
    g_free (markup);

    markup = g_markup_escape_text (_("Made by Kefkism"), -1);
    dakkar_about_page_label (page, markup, FALSE);
    g_free (markup);
    link = gtk_link_button_new_with_label ("https://github.com/Kefkism", "github.com/Kefkism");
    gtk_widget_set_halign (link, GTK_ALIGN_CENTER);
    gtk_box_pack_start (GTK_BOX (page), link, FALSE, FALSE, 0);
    link = gtk_link_button_new_with_label (DAKKAR_WEBSITE, _("Official repository: github.com/Kefkism/Dakkar"));
    gtk_widget_set_halign (link, GTK_ALIGN_CENTER);
    gtk_label_set_ellipsize (GTK_LABEL (gtk_bin_get_child (GTK_BIN (link))), PANGO_ELLIPSIZE_END);
    gtk_box_pack_start (GTK_BOX (page), link, FALSE, FALSE, 0);

    markup = g_markup_escape_text (_("A file manager with Miller columns, custom colours and "
                                     "transfer tools."), -1);
    dakkar_about_page_label (page, markup, FALSE);
    g_free (markup);
    markup = g_markup_escape_text (_("Dakkar is a fork of Nemo, the Cinnamon file manager by "
                                     "Linux Mint, which is itself derived from GNOME Nautilus."), -1);
    dakkar_about_page_label (page, markup, FALSE);
    g_free (markup);

    markup = g_markup_escape_text (_("Miller columns are based on linux-nemo-miller-columns by "
                                     "Davide Minardi (davemin), MIT License."), -1);
    dakkar_about_page_label (page, markup, FALSE);
    g_free (markup);
    link = gtk_link_button_new_with_label ("https://github.com/davemin/linux-nemo-miller-columns",
                                           "github.com/davemin/linux-nemo-miller-columns");
    gtk_widget_set_halign (link, GTK_ALIGN_CENTER);
    gtk_label_set_ellipsize (GTK_LABEL (gtk_bin_get_child (GTK_BIN (link))), PANGO_ELLIPSIZE_MIDDLE);
    gtk_box_pack_start (GTK_BOX (page), link, FALSE, FALSE, 0);

    markup = g_markup_escape_text (_("This program comes with absolutely no warranty. See the GNU General "
                                     "Public License, version 2 or later, for details."), -1);
    dakkar_about_page_label (page, markup, TRUE);
    g_free (markup);

    button = gtk_button_new_with_mnemonic (_("_About Dakkar…"));
    gtk_widget_set_tooltip_text (button, _("Show the About dialog with credits and the full license"));
    gtk_widget_set_halign (button, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top (button, 10);
    gtk_widget_set_name (button, "dakkar-prefs-about-button");
    g_signal_connect (button, "clicked", G_CALLBACK (dakkar_about_page_open_dialog), NULL);
    gtk_box_pack_start (GTK_BOX (page), button, FALSE, FALSE, 0);

    scroller = gtk_scrolled_window_new (NULL, NULL);
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroller),
                                    GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_container_add (GTK_CONTAINER (scroller), page);
    gtk_widget_show_all (scroller);
    gtk_stack_add_titled (stack, scroller, "about", _("About Dakkar"));
}

static void
nemo_file_management_properties_dialog_setup_plugin_page (GtkBuilder *builder)
{
    GtkWidget *box;

    box = GTK_WIDGET (gtk_builder_get_object (builder, "plugin_box"));

    gtk_box_pack_start (GTK_BOX (box),
                        GTK_WIDGET (nemo_plugin_manager_new ()),
                        TRUE, TRUE, 0);
}

static void
create_date_format_menu (GtkBuilder *builder)
{
	GtkComboBoxText *combo_box;
	gchar *date_string;
	GDateTime *now;

	combo_box = GTK_COMBO_BOX_TEXT
		(gtk_builder_get_object (builder,
					 NEMO_FILE_MANAGEMENT_PROPERTIES_DATE_FORMAT_WIDGET));

	now = g_date_time_new_now_local ();

	date_string = g_date_time_format (now, "%c");
	gtk_combo_box_text_append_text (combo_box, date_string);
	g_free (date_string);

	date_string = g_date_time_format (now, "%Y-%m-%d %H:%M:%S");
	gtk_combo_box_text_append_text (combo_box, date_string);
	g_free (date_string);

	gtk_combo_box_text_append_text (combo_box, _("Yesterday"));

	g_date_time_unref (now);
}

static void
set_columns_from_settings (NemoColumnChooser *chooser)
{
	char **visible_columns;
	char **column_order;

	visible_columns = g_settings_get_strv (nemo_list_view_preferences,
					       NEMO_PREFERENCES_LIST_VIEW_DEFAULT_VISIBLE_COLUMNS);
	column_order = g_settings_get_strv (nemo_list_view_preferences,
					    NEMO_PREFERENCES_LIST_VIEW_DEFAULT_COLUMN_ORDER);

	nemo_column_chooser_set_settings (NEMO_COLUMN_CHOOSER (chooser),
					      visible_columns,
					      column_order);

	g_strfreev (visible_columns);
	g_strfreev (column_order);
}

static void
use_default_callback (NemoColumnChooser *chooser,
		      gpointer user_data)
{
	g_settings_reset (nemo_list_view_preferences,
			  NEMO_PREFERENCES_LIST_VIEW_DEFAULT_VISIBLE_COLUMNS);
	g_settings_reset (nemo_list_view_preferences,
			  NEMO_PREFERENCES_LIST_VIEW_DEFAULT_COLUMN_ORDER);
	set_columns_from_settings (chooser);
}

static void
nemo_file_management_properties_dialog_setup_list_column_page (GtkBuilder *builder)
{
	GtkWidget *chooser;
	GtkWidget *box;

	chooser = nemo_column_chooser_new (NULL);

	set_columns_from_settings (NEMO_COLUMN_CHOOSER (chooser));

	g_signal_connect (chooser, "changed",
			  G_CALLBACK (columns_changed_callback), chooser);
	g_signal_connect (chooser, "use_default",
			  G_CALLBACK (use_default_callback), chooser);

	gtk_widget_show (chooser);
	box = GTK_WIDGET (gtk_builder_get_object (builder, "list_columns_vbox"));

	gtk_box_pack_start (GTK_BOX (box), chooser, TRUE, TRUE, 0);
}

/* Looks up a Glade object before binding a preference and warns when it
 * is missing, instead of leaving an inert control behind. */
static GObject *
dakkar_preference_object (GtkBuilder *builder, const char *id)
{
    GObject *widget = gtk_builder_get_object (builder, id);
    if (widget == NULL)
        g_warning ("Dakkar Preferences: missing control '%s'", id);
    return widget;
}

static void
bind_builder_bool (GtkBuilder *builder,
		   GSettings *settings,
		   const char *widget_name,
		   const char *prefs)
{
	g_settings_bind (settings, prefs,
			 dakkar_preference_object (builder, widget_name),
			 "active", G_SETTINGS_BIND_DEFAULT);
}

static void
bind_builder_bool_inverted (GtkBuilder *builder,
			    GSettings *settings,
			    const char *widget_name,
			    const char *prefs)
{
	g_settings_bind (settings, prefs,
			 dakkar_preference_object (builder, widget_name),
			 "active", G_SETTINGS_BIND_INVERT_BOOLEAN);
}

static void
bind_builder_string_entry (GtkBuilder *builder,
                            GSettings *settings,
                           const char *widget_name,
                           const char *prefs)
{
    g_settings_bind (settings, prefs,
                     dakkar_preference_object (builder, widget_name),
                     "text", G_SETTINGS_BIND_DEFAULT);
}

static gboolean
enum_get_mapping (GValue             *value,
		  GVariant           *variant,
		  gpointer            user_data)
{
	const char **enum_values = user_data;
	const char *str;
	int i;

	str = g_variant_get_string (variant, NULL);
	for (i = 0; enum_values[i] != NULL; i++) {
		if (strcmp (enum_values[i], str) == 0) {
			g_value_set_int (value, i);
			return TRUE;
		}
	}

    /* Old Nemo profiles can contain enum values that no longer exist in
     * the current combobox. Show the first supported item, not an empty
     * dead-looking chooser. Do not rewrite the legacy value until edited. */
    g_value_set_int (value, 0);
    return TRUE;
}

static GVariant *
enum_set_mapping (const GValue       *value,
		  const GVariantType *expected_type,
		  gpointer            user_data)
{
	const char **enum_values = user_data;

    gint i = g_value_get_int (value);
    gint n = 0;
    while (enum_values[n] != NULL) n++;
    if (n == 0) return NULL;
    if (i < 0 || i >= n) return NULL;
    return g_variant_new_string (enum_values[i]);
}

/* Only a choice made in the Preferences window itself re-views the open
 * windows. GtkComboBox emits "changed" before "notify::active", so for a
 * user choice the bound key still holds the old value here; when the key
 * was changed elsewhere (toolbar, View menu, Ctrl+1..4) the binding updates
 * the combo after the key, and both already agree: nothing to apply. */
static gboolean
dakkar_apply_default_view_idle (gpointer user_data)
{
	nemo_window_manage_views_apply_default_view ();
	return G_SOURCE_REMOVE;
}

/* user_data: the settings key bound to the combo */
static void
dakkar_default_view_combo_changed (GtkComboBox *combo, gpointer user_data)
{
	gint active = gtk_combo_box_get_active (combo);
	gchar *current;

	if (active < 0 || active >= (gint) G_N_ELEMENTS (default_view_values) - 1) {
		return;
	}
	current = g_settings_get_string (nemo_preferences, (const gchar *) user_data);
	if (g_strcmp0 (current, default_view_values[active]) != 0) {
		g_idle_add (dakkar_apply_default_view_idle, NULL);
	}
	g_free (current);
}

static void
bind_builder_enum (GtkBuilder *builder,
		   GSettings *settings,
		   const char *widget_name,
		   const char *prefs,
		   const char **enum_values)
{
	g_settings_bind_with_mapping (settings, prefs,
				      dakkar_preference_object (builder, widget_name),
				      "active", G_SETTINGS_BIND_DEFAULT,
				      enum_get_mapping,
				      enum_set_mapping,
				      enum_values, NULL);
}


typedef struct {
	const guint64 *values;
	int n_values;
} UIntEnumBinding;

static gboolean
uint_enum_get_mapping (GValue             *value,
		       GVariant           *variant,
		       gpointer            user_data)
{
	UIntEnumBinding *binding = user_data;
	guint64 v;
	int i;

	v = g_variant_get_uint64 (variant);
	for (i = 0; i < binding->n_values; i++) {
		if (binding->values[i] >= v) {
			g_value_set_int (value, i);
			return TRUE;
		}
	}

    /* A previous release may have stored a higher limit than this
     * version supports. Show the largest option instead of no selection. */
    g_value_set_int (value, binding->n_values - 1);
    return TRUE;
}

static GVariant *
uint_enum_set_mapping (const GValue       *value,
		       const GVariantType *expected_type,
		       gpointer            user_data)
{
	UIntEnumBinding *binding = user_data;

    gint i = g_value_get_int (value);
    if (i < 0 || i >= binding->n_values) return NULL;
    return g_variant_new_uint64 (binding->values[i]);
}

static void
bind_builder_uint_enum (GtkBuilder *builder,
			GSettings *settings,
			const char *widget_name,
			const char *prefs,
			const guint64 *values,
			int n_values)
{
	UIntEnumBinding *binding;

	binding = g_new (UIntEnumBinding, 1);
	binding->values = values;
	binding->n_values = n_values;

	g_settings_bind_with_mapping (settings, prefs,
				      dakkar_preference_object (builder, widget_name),
				      "active", G_SETTINGS_BIND_DEFAULT,
				      uint_enum_get_mapping,
				      uint_enum_set_mapping,
				      binding, g_free);
}

static GVariant *
radio_mapping_set (const GValue *gvalue,
		   const GVariantType *expected_type,
		   gpointer user_data)
{
	const gchar *widget_value = user_data;
	GVariant *retval = NULL;

	if (g_value_get_boolean (gvalue)) {
		retval = g_variant_new_string (widget_value);
	}

	return retval;
}

static gboolean
radio_mapping_get (GValue *gvalue,
		   GVariant *variant,
		   gpointer user_data)
{
	const gchar *widget_value = user_data;
	const gchar *value;

	value = g_variant_get_string (variant, NULL);

	if (g_strcmp0 (value, widget_value) == 0) {
		g_value_set_boolean (gvalue, TRUE);
	} else {
		g_value_set_boolean (gvalue, FALSE);
	}

	return TRUE;
}

static void
bind_builder_radio (GtkBuilder *builder,
		    GSettings *settings,
		    const char **widget_names,
		    const char *prefs,
		    const char **values)
{
	GtkWidget *button;
	int i;

	for (i = 0; widget_names[i] != NULL; i++) {
		button = GTK_WIDGET (gtk_builder_get_object (builder, widget_names[i]));

		g_settings_bind_with_mapping (settings, prefs,
					      button, "active",
					      G_SETTINGS_BIND_DEFAULT,
					      radio_mapping_get, radio_mapping_set,
					      (gpointer) values[i], NULL);
	}
}

static void
setup_configurable_menu_items (GtkBuilder *builder)
{
    gint i;

    for (i = 0; i < CONFIGURABLE_MENU_ITEM_COUNT; i++) {
        if (CONFIGURABLE_MENU_ITEM_INFO[i].config_widget_name == NULL) {
            continue;
        }

        bind_builder_bool (builder,
                           nemo_menu_config_preferences,
                           CONFIGURABLE_MENU_ITEM_INFO[i].config_widget_name,
                           CONFIGURABLE_MENU_ITEM_INFO[i].settings_key);
    }
}

static void
setup_tooltip_items (GtkBuilder *builder)
{
    gboolean enabled = FALSE;

    enabled = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (W (NEMO_FILE_MANAGEMENT_PROPERTIES_TOOLTIPS_ON_ICON_VIEW_WIDGET))) ||
              gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (W (NEMO_FILE_MANAGEMENT_PROPERTIES_TOOLTIPS_ON_DESKTOP_WIDGET))) ||
              gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (W (NEMO_FILE_MANAGEMENT_PROPERTIES_TOOLTIPS_ON_LIST_VIEW_WIDGET)));

    gtk_widget_set_sensitive (GTK_WIDGET (W (NEMO_FILE_MANAGEMENT_PROPERTIES_TOOLTIP_FILE_TYPE_WIDGET)), enabled);
    gtk_widget_set_sensitive (GTK_WIDGET (W (NEMO_FILE_MANAGEMENT_PROPERTIES_TOOLTIP_MOD_DATE_WIDGET)), enabled);
    gtk_widget_set_sensitive (GTK_WIDGET (W (NEMO_FILE_MANAGEMENT_PROPERTIES_TOOLTIP_CREATED_DATE_WIDGET)), enabled);
    gtk_widget_set_sensitive (GTK_WIDGET (W (NEMO_FILE_MANAGEMENT_PROPERTIES_TOOLTIP_ACCESS_DATE_WIDGET)), enabled);
    gtk_widget_set_sensitive (GTK_WIDGET (W (NEMO_FILE_MANAGEMENT_PROPERTIES_TOOLTIP_FULL_PATH_WIDGET)), enabled);
}

static void
connect_tooltip_items (GtkBuilder *builder)
{
    GtkToggleButton *w;

    w = GTK_TOGGLE_BUTTON (W (NEMO_FILE_MANAGEMENT_PROPERTIES_TOOLTIPS_ON_ICON_VIEW_WIDGET));
    g_signal_connect_swapped (w, "toggled", G_CALLBACK (setup_tooltip_items), builder);

    w = GTK_TOGGLE_BUTTON (W (NEMO_FILE_MANAGEMENT_PROPERTIES_TOOLTIPS_ON_LIST_VIEW_WIDGET));
    g_signal_connect_swapped (w, "toggled", G_CALLBACK (setup_tooltip_items), builder);

    w = GTK_TOGGLE_BUTTON (W (NEMO_FILE_MANAGEMENT_PROPERTIES_TOOLTIPS_ON_DESKTOP_WIDGET));
    g_signal_connect_swapped (w, "toggled", G_CALLBACK (setup_tooltip_items), builder);

}

/* When single click radio button is selected, checkbox for quick renames should get unselected and disable to avoid annoying features */
static void
setup_quick_renames (GtkBuilder *builder)
{
	gboolean enabled = FALSE;
	enabled = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (W (click_behavior_components[1])));
    /* A disabled control is not a request to erase its persisted choice.
     * Keep the quick-rename preference for users who later re-enable double-click. */
	gtk_widget_set_sensitive (GTK_WIDGET (W (NEMO_FILE_MANAGEMENT_QUICK_RENAMES_WITH_PAUSE_IN_BETWEEN)), enabled);
}

static void
connect_quick_renames (GtkBuilder *builder)
{
	GtkRadioButton *w;
	w=GTK_RADIO_BUTTON(W(click_behavior_components[0]));
 		g_signal_connect_swapped (w, "toggled", G_CALLBACK (setup_quick_renames), builder);

	w=GTK_RADIO_BUTTON(W(click_behavior_components[1]));
		g_signal_connect_swapped (w, "toggled", G_CALLBACK (setup_quick_renames), builder);
}

static void
on_dialog_destroy (GtkWidget *widget,
                   gpointer   user_data)
{
    GtkBuilder *builder = GTK_BUILDER (user_data);

    g_object_unref (builder);
}

static void
on_date_format_combo_changed (GtkComboBox *widget,
                              gpointer     user_data)
{
    GtkBuilder *builder = GTK_BUILDER (user_data);
    gint active = gtk_combo_box_get_active (widget);

    switch (active) {
        case NEMO_DATE_FORMAT_LOCALE:
        case NEMO_DATE_FORMAT_ISO:
            gtk_widget_set_sensitive (GTK_WIDGET (gtk_builder_get_object (builder, NEMO_FILE_MANAGEMENT_PROPERTIES_DATE_FONT_CHOICE_WIDGET)), TRUE);
            break;
        case NEMO_DATE_FORMAT_INFORMAL:
        default:
            gtk_widget_set_sensitive (GTK_WIDGET (gtk_builder_get_object (builder, NEMO_FILE_MANAGEMENT_PROPERTIES_DATE_FONT_CHOICE_WIDGET)), FALSE);
            break;
    }
}

static void
set_gtk_filechooser_sort_first (GObject *object,
				GParamSpec *pspec)
{
	g_settings_set_boolean (gtk_filechooser_preferences,
				NEMO_PREFERENCES_SORT_DIRECTORIES_FIRST,
				gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (object)));
}

/* A read-only diagnostic of the theme GTK actually resolved. Preferences
 * don't choose a named desktop theme: System/Light/Dark remain per-app hints.
 * This updates in place on live KDE/GTK theme changes. */
static void
files_appearance_theme_description_changed (GObject *settings,
                                             GParamSpec *pspec,
                                             gpointer user_data)
{
    GtkWidget *label = GTK_WIDGET (user_data);
    gchar *theme = NULL;
    gboolean prefers_dark = FALSE;
    gchar *description;

    g_object_get (gtk_settings_get_default (),
                  "gtk-theme-name", &theme,
                  "gtk-application-prefer-dark-theme", &prefers_dark,
                  NULL);
    description = g_strdup_printf (_("Active GTK theme: %s · %s preference"),
                                   theme != NULL ? theme : _("unknown"),
                                   prefers_dark ? _("dark") : _("light"));
    gtk_label_set_text (GTK_LABEL (label), description);
    g_free (description);
    g_free (theme);
    (void)settings;
    (void)pspec;
}

/* Appearance settings are shared by Icon, List, Compact and Miller views.
 * GtkSpinButton also allows an exact pixel value (not just a slider preset). */
/* Dakkar Preferences: searchable controls and freedesktop-theme sidebar icons.
 * Search results take the user to the real setting page, not a dummy view. */
typedef struct {
    GtkStack *stack;              /* owned by the preferences dialog */
    GtkWidget *results;
    GtkWidget *list;
    GtkWidget *search;
} DakkarPreferencesSearch;

static void
dakkar_preferences_setup_sidebar_icons (GtkStack *stack)
{
    /* Every page gets an icon that exists in the active icon theme: the
     * glade pages ask for xapp-prefs-* (only present with XApp installed),
     * "Details & Preview" had no icon at all, and Breeze has no
     * preferences-desktop-theme-symbolic (GTK then fell back to a large
     * full-colour icon). */
    const struct { const char *page, *icon; } icons[] = {
        {"views", dakkar_view_icon_name (NEMO_ACTION_ICON_VIEW)},
        {"behavior", dakkar_icon_resolve ("xapp-prefs-behavior-symbolic")},
        {"display", dakkar_icon_resolve ("xapp-prefs-display-symbolic")},
        {"list-columns", dakkar_view_icon_name (NEMO_ACTION_LIST_VIEW)},
        {"preview", dakkar_icon_resolve ("xapp-prefs-preview-symbolic")},
        {"toolbar", dakkar_icon_resolve ("xapp-prefs-toolbar-symbolic")},
        {"page0", dakkar_icon_pick ("open-menu-symbolic", "application-menu-symbolic", NULL)},
        {"plugins", dakkar_icon_resolve ("xapp-prefs-plugins-symbolic")},
        {"startup", "go-home-symbolic"},
        {"appearance", dakkar_icon_resolve ("preferences-desktop-theme-symbolic")},
        {"miller", dakkar_view_icon_name (NEMO_ACTION_MILLER_VIEW)},
        {"details-preview", dakkar_icon_pick ("view-split-left-right-symbolic", "sidebar-show-right-symbolic",
                                             "view-preview-symbolic", "document-properties-symbolic", NULL)},
        {"performance", dakkar_icon_pick ("preferences-system-performance-symbolic", "speedometer-symbolic",
                                         "preferences-system-symbolic", NULL)},
        {"search", "system-search-symbolic"},
        {"grouping", dakkar_icon_pick ("view-group-symbolic", "view-list-symbolic", NULL)},
        {"file-transfers", dakkar_icon_pick ("folder-download-symbolic", "document-save-symbolic", NULL)},
        {"quick-actions", dakkar_icon_pick ("system-run-symbolic", "preferences-other-symbolic", NULL)},
        {"favorites", dakkar_icon_pick ("starred-symbolic", "emblem-favorite-symbolic", "bookmark-new-symbolic", NULL)},
        {"about", dakkar_icon_pick ("help-about-symbolic", "dialog-information-symbolic", NULL)},
    };
    guint i;
    for (i = 0; i < G_N_ELEMENTS (icons); i++) {
        GtkWidget *page = gtk_stack_get_child_by_name (stack, icons[i].page);
        if (page != NULL)
            gtk_container_child_set (GTK_CONTAINER (stack), page,
                                     "icon-name", icons[i].icon, NULL);
    }
}

static void
dakkar_preferences_result_clicked (GtkButton *button, DakkarPreferencesSearch *state)
{
    GtkWidget *page = g_object_get_data (G_OBJECT (button), "dakkar-search-page");
    if (page != NULL) {
        gtk_stack_set_visible_child (state->stack, page);
        gtk_entry_set_text (GTK_ENTRY (state->search), "");
    }
}

static void
dakkar_preferences_scan_widget (GtkWidget *widget, GtkWidget *page,
                               const char *title, const char *folded_query,
                               DakkarPreferencesSearch *state, guint *count)
{
    const char *label = NULL;
    if (*count >= 24) return;
    if (GTK_IS_LABEL (widget))
        label = gtk_label_get_text (GTK_LABEL (widget));
    else if (GTK_IS_BUTTON (widget))
        label = gtk_button_get_label (GTK_BUTTON (widget));
    if (label != NULL && *label != 0) {
        char *folded = g_utf8_casefold (label, -1);
        if (strstr (folded, folded_query) != NULL) {
            char *caption = g_strdup_printf ("%s  ›  %s", title, label);
            GtkWidget *button = gtk_button_new_with_label (caption);
            gtk_button_set_relief (GTK_BUTTON (button), GTK_RELIEF_NONE);
            gtk_widget_set_halign (button, GTK_ALIGN_FILL);
            gtk_widget_set_tooltip_text (button, _("Open the matching settings page"));
            g_object_set_data (G_OBJECT (button), "dakkar-search-page", page);
            g_signal_connect (button, "clicked",
                              G_CALLBACK (dakkar_preferences_result_clicked), state);
            gtk_box_pack_start (GTK_BOX (state->list), button, FALSE, FALSE, 1);
            (*count)++;
            g_free (caption);
        }
        g_free (folded);
    }
    if (GTK_IS_CONTAINER (widget)) {
        GList *children = gtk_container_get_children (GTK_CONTAINER (widget));
        GList *l;
        for (l = children; l != NULL && *count < 24; l = l->next)
            dakkar_preferences_scan_widget (GTK_WIDGET (l->data), page, title,
                                           folded_query, state, count);
        g_list_free (children);
    }
}

static void
dakkar_preferences_search_changed (GtkEditable *editable, DakkarPreferencesSearch *state)
{
    const char *query = gtk_entry_get_text (GTK_ENTRY (editable));
    GList *items, *l;
    char *folded;
    guint count = 0;
    items = gtk_container_get_children (GTK_CONTAINER (state->list));
    for (l = items; l != NULL; l = l->next)
        gtk_widget_destroy (GTK_WIDGET (l->data));
    g_list_free (items);
    if (g_utf8_strlen (query, -1) < 2) {
        gtk_widget_hide (state->results);
        return;
    }
    folded = g_utf8_casefold (query, -1);
    items = gtk_container_get_children (GTK_CONTAINER (state->stack));
    for (l = items; l != NULL && count < 24; l = l->next) {
        GtkWidget *page = GTK_WIDGET (l->data);
        char *title = NULL;
        gtk_container_child_get (GTK_CONTAINER (state->stack), page,
                                 "title", &title, NULL);
        if (title != NULL) {
            char *folded_title = g_utf8_casefold (title, -1);
            if (strstr (folded_title, folded) != NULL) {
                GtkWidget *button = gtk_button_new_with_label (title);
                gtk_button_set_relief (GTK_BUTTON (button), GTK_RELIEF_NONE);
                g_object_set_data (G_OBJECT (button), "dakkar-search-page", page);
                g_signal_connect (button, "clicked",
                                  G_CALLBACK (dakkar_preferences_result_clicked), state);
                gtk_box_pack_start (GTK_BOX (state->list), button, FALSE, FALSE, 1);
                count++;
            }
            g_free (folded_title);
            dakkar_preferences_scan_widget (page, page, title, folded,
                                           state, &count);
        }
        g_free (title);
    }
    g_list_free (items);
    g_free (folded);
    if (count > 0) gtk_widget_show_all (state->results);
    else gtk_widget_hide (state->results);
}

static void
dakkar_preferences_setup_search (GtkWidget *dialog, GtkStack *stack)
{
    GtkWidget *pane = gtk_widget_get_parent (GTK_WIDGET (stack));
    GtkWidget *root = pane != NULL ? gtk_widget_get_parent (pane) : NULL;
    GtkWidget *bar;
    DakkarPreferencesSearch *state;
    if (!GTK_IS_BOX (root)) return;
    state = g_new0 (DakkarPreferencesSearch, 1);
    state->stack = stack;
    bar = gtk_box_new (GTK_ORIENTATION_VERTICAL, 4);
    gtk_container_set_border_width (GTK_CONTAINER (bar), 8);
    state->search = gtk_search_entry_new ();
    gtk_entry_set_placeholder_text (GTK_ENTRY (state->search),
                                    _("Search settings…"));
    gtk_box_pack_start (GTK_BOX (bar), state->search, FALSE, FALSE, 0);
    state->results = gtk_scrolled_window_new (NULL, NULL);
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (state->results),
                                    GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request (state->results, -1, 125);
    state->list = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add (GTK_CONTAINER (state->results), state->list);
    gtk_box_pack_start (GTK_BOX (bar), state->results, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (root), bar, FALSE, FALSE, 0);
    gtk_box_reorder_child (GTK_BOX (root), bar, 0);
    g_signal_connect (state->search, "changed",
                      G_CALLBACK (dakkar_preferences_search_changed), state);
    g_object_set_data_full (G_OBJECT (dialog), "dakkar-preferences-search",
                            state, g_free);
    gtk_widget_show_all (bar);
    gtk_widget_hide (state->results);
}

/* g_settings_bind() greys out every control whose key the GSettings backend
 * reports as not writable.  When that happens for a whole schema (a dconf
 * "null configuration" from a broken DCONF_PROFILE, dconf locks, a read-only
 * or "null" backend in the environment Files was started from) a page
 * would be all dead, greyed-out controls.  Count them so the page can say
 * what is going on and how to check it. */
static guint
dakkar_count_unwritable_keys (GSettings *settings, guint *total, gchar **first)
{
    GSettingsSchema *schema = NULL;
    gchar **keys;
    guint i, n = 0;

    if (settings == NULL)
        return 0;
    g_object_get (settings, "settings-schema", &schema, NULL);
    if (schema == NULL)
        return 0;
    keys = g_settings_schema_list_keys (schema);
    for (i = 0; keys[i] != NULL; i++) {
        (*total)++;
        if (!g_settings_is_writable (settings, keys[i])) {
            if (n == 0 && first != NULL && *first == NULL)
                *first = g_strdup_printf ("%s %s",
                                          g_settings_schema_get_id (schema),
                                          keys[i]);
            n++;
        }
    }
    g_strfreev (keys);
    g_settings_schema_unref (schema);
    return n;
}

static void
dakkar_preferences_setup_readonly_warning (GtkWidget *dialog, GtkStack *stack)
{
    GSettings *all[] = {
        nemo_preferences, nemo_icon_view_preferences,
        nemo_list_view_preferences, nemo_compact_view_preferences,
        nemo_tree_sidebar_preferences, nemo_menu_config_preferences,
        nemo_window_state, gnome_media_handling_preferences
    };
    GtkWidget *pane, *root, *bar, *label;
    GSettingsBackend *backend = NULL;
    gchar *first = NULL, *text;
    const gchar *profile;
    guint i, total = 0, readonly = 0;

    for (i = 0; i < G_N_ELEMENTS (all); i++)
        readonly += dakkar_count_unwritable_keys (all[i], &total, &first);
    if (readonly == 0)
        return;

    g_object_get (nemo_preferences, "backend", &backend, NULL);
    profile = g_getenv ("DCONF_PROFILE");
    g_warning ("Preferences: %u of %u settings are not writable (backend %s%s%s), "
               "so their controls are disabled. First one: %s. "
               "Check with: gsettings writable %s",
               readonly, total,
               backend != NULL ? G_OBJECT_TYPE_NAME (backend) : "?",
               profile != NULL ? ", DCONF_PROFILE=" : "",
               profile != NULL ? profile : "",
               first, first);

    pane = gtk_widget_get_parent (GTK_WIDGET (stack));
    root = pane != NULL ? gtk_widget_get_parent (pane) : NULL;
    if (GTK_IS_BOX (root)) {
        text = g_strdup_printf (
            _("%u of %u settings cannot be changed: the system settings "
              "storage (%s%s%s) reports them as read-only, so their controls "
              "are greyed out. This is not caused by the current view. Check "
              "in a terminal with:  gsettings writable %s"),
            readonly, total,
            backend != NULL ? G_OBJECT_TYPE_NAME (backend) : "?",
            profile != NULL ? ", DCONF_PROFILE=" : "",
            profile != NULL ? profile : "",
            first);
        bar = gtk_info_bar_new ();
        gtk_info_bar_set_message_type (GTK_INFO_BAR (bar), GTK_MESSAGE_WARNING);
        label = gtk_label_new (text);
        gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
        gtk_label_set_selectable (GTK_LABEL (label), TRUE);
        gtk_label_set_xalign (GTK_LABEL (label), 0);
        gtk_container_add (GTK_CONTAINER (gtk_info_bar_get_content_area (GTK_INFO_BAR (bar))),
                           label);
        gtk_widget_set_name (bar, "dakkar-preferences-readonly-warning");
        gtk_box_pack_start (GTK_BOX (root), bar, FALSE, FALSE, 0);
        gtk_box_reorder_child (GTK_BOX (root), bar, 0);
        gtk_widget_show_all (bar);
        g_free (text);
    }
    g_clear_object (&backend);
    g_free (first);
    (void) dialog;
}

/* Explicit widget <-> settings synchronization. In GTK3 a GtkRange uses a
 * double and the persisted corner radius is an integer; two g_settings_bind
 * conversions can otherwise update a slider/spin in an unexpected order. */
static void
dakkar_radius_from_settings (GSettings *settings, gchar *key, gpointer widget)
{
    gdouble value = g_settings_get_int (settings, NEMO_PREFERENCES_FILES_CORNER_RADIUS);
    if (GTK_IS_RANGE (widget)) {
        if (gtk_range_get_value (GTK_RANGE (widget)) != value)
            gtk_range_set_value (GTK_RANGE (widget), value);
    } else if (GTK_IS_SPIN_BUTTON (widget) &&
               gtk_spin_button_get_value (GTK_SPIN_BUTTON (widget)) != value)
        gtk_spin_button_set_value (GTK_SPIN_BUTTON (widget), value);
}
static void
dakkar_radius_scale_changed (GtkRange *range, gpointer unused)
{
    gint radius = (gint)(gtk_range_get_value (range) + .5);
    if (g_settings_get_int (nemo_preferences, NEMO_PREFERENCES_FILES_CORNER_RADIUS) != radius)
        g_settings_set_int (nemo_preferences, NEMO_PREFERENCES_FILES_CORNER_RADIUS, radius);
}
static void
dakkar_radius_spin_changed (GtkSpinButton *spin, gpointer unused)
{
    gint radius = gtk_spin_button_get_value_as_int (spin);
    if (g_settings_get_int (nemo_preferences, NEMO_PREFERENCES_FILES_CORNER_RADIUS) != radius)
        g_settings_set_int (nemo_preferences, NEMO_PREFERENCES_FILES_CORNER_RADIUS, radius);
}
static void
dakkar_color_button_changed (GtkColorButton *button, gpointer user_data)
{
    const gchar *key = user_data;
    GdkRGBA rgba;
    gchar *value;
    gtk_color_chooser_get_rgba (GTK_COLOR_CHOOSER (button), &rgba);
    value = gdk_rgba_to_string (&rgba);
    g_settings_set_string (nemo_preferences, key, value);
    g_free (value);
}
static void
dakkar_color_settings_changed (GSettings *settings, gchar *key, gpointer widget)
{
    GdkRGBA rgba;
    gchar *value = g_settings_get_string (settings, key);
    if (gdk_rgba_parse (&rgba, value))
        gtk_color_chooser_set_rgba (GTK_COLOR_CHOOSER (widget), &rgba);
    g_free (value);
}
static GtkWidget *
dakkar_color_preference (GtkWidget *box, const gchar *title, const gchar *key)
{
    GtkWidget *row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget *label = gtk_label_new (title);
    GtkWidget *picker = gtk_color_button_new ();
    gtk_label_set_xalign (GTK_LABEL (label), 0);
    /* The swatch must stay visible at narrow widths: the label wraps (and
     * may shrink) instead of pushing the swatch out of the dialog. */
    gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
    gtk_label_set_line_wrap_mode (GTK_LABEL (label), PANGO_WRAP_WORD_CHAR);
    gtk_label_set_width_chars (GTK_LABEL (label), 8);
    gtk_label_set_max_width_chars (GTK_LABEL (label), 40);
    gtk_widget_set_valign (picker, GTK_ALIGN_CENTER);
    gtk_widget_set_hexpand (label, TRUE);
    gtk_box_pack_start (GTK_BOX (row), label, TRUE, TRUE, 0);
    gtk_box_pack_end (GTK_BOX (row), picker, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (box), row, FALSE, FALSE, 0);
    dakkar_color_settings_changed (nemo_preferences, (gchar *)key, picker);
    g_signal_connect (picker, "color-set", G_CALLBACK (dakkar_color_button_changed), (gpointer)key);
    gchar *signal = g_strconcat ("changed::", key, NULL);
    g_signal_connect_object (nemo_preferences, signal,
                             G_CALLBACK (dakkar_color_settings_changed), picker, 0);
    g_free (signal);
    return picker;
}
/* Optional palette colours (secondary accent, Quick Actions, statusbar):
 * an empty setting is derived from another colour.  The swatch always
 * shows the colour in use; the reset button returns to the derived one. */
static void
dakkar_optional_sync (GSettings *settings, gchar *changed, gpointer row)
{
    const gchar *key = g_object_get_data (G_OBJECT (row), "dakkar-key");
    GtkWidget *picker = g_object_get_data (G_OBJECT (row), "dakkar-picker");
    GtkWidget *reset = g_object_get_data (G_OBJECT (row), "dakkar-reset");
    GtkWidget *hint = g_object_get_data (G_OBJECT (row), "dakkar-hint");
    gboolean automatic;
    GdkRGBA rgba;

    /* derived colours follow any palette change */
    if (changed != NULL && !g_str_has_prefix (changed, "dakkar-theme-"))
        return;
    automatic = dakkar_theme_color_is_automatic (key);
    dakkar_theme_color (key, &rgba);
    gtk_color_chooser_set_rgba (GTK_COLOR_CHOOSER (picker), &rgba);
    gtk_widget_set_sensitive (reset, !automatic);
    gtk_widget_set_visible (hint, automatic);
}
static void
dakkar_optional_set (GtkColorButton *button, gpointer key)
{
    GdkRGBA rgba;
    gchar *value;
    gtk_color_chooser_get_rgba (GTK_COLOR_CHOOSER (button), &rgba);
    rgba.alpha = 1.0;
    value = gdk_rgba_to_string (&rgba);
    g_settings_set_string (nemo_preferences, key, value);
    g_free (value);
}
static void
dakkar_optional_reset (GtkButton *button, gpointer key)
{
    g_settings_reset (nemo_preferences, key);
}
static void
dakkar_optional_color_preference (GtkWidget *box, const gchar *title, const gchar *automatic_hint,
                                  const gchar *tooltip, const gchar *key)
{
    GtkWidget *row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *texts = gtk_box_new (GTK_ORIENTATION_VERTICAL, 1);
    GtkWidget *label = gtk_label_new (title);
    GtkWidget *hint = gtk_label_new (automatic_hint);
    GtkWidget *picker = gtk_color_button_new ();
    GtkWidget *reset = gtk_button_new_from_icon_name ("edit-undo-symbolic", GTK_ICON_SIZE_BUTTON);
    guint i;

    if (tooltip != NULL)
        gtk_widget_set_tooltip_text (row, tooltip);
    gtk_widget_set_tooltip_text (reset, _("Back to the automatic color"));
    gtk_button_set_relief (GTK_BUTTON (reset), GTK_RELIEF_NONE);
    gtk_widget_set_valign (reset, GTK_ALIGN_CENTER);
    gtk_widget_set_valign (picker, GTK_ALIGN_CENTER);
    gtk_color_chooser_set_use_alpha (GTK_COLOR_CHOOSER (picker), FALSE);
    gtk_widget_set_no_show_all (hint, TRUE);
    gtk_style_context_add_class (gtk_widget_get_style_context (hint), "dim-label");
    for (i = 0; i < 2; i++) {
        GtkLabel *l = GTK_LABEL (i == 0 ? label : hint);
        gtk_label_set_xalign (l, 0);
        gtk_label_set_line_wrap (l, TRUE);
        gtk_label_set_line_wrap_mode (l, PANGO_WRAP_WORD_CHAR);
        gtk_label_set_width_chars (l, 8);
        gtk_label_set_max_width_chars (l, 40);
    }
    gtk_widget_set_hexpand (texts, TRUE);
    gtk_box_pack_start (GTK_BOX (texts), label, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (texts), hint, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (row), texts, TRUE, TRUE, 0);
    gtk_box_pack_end (GTK_BOX (row), picker, FALSE, FALSE, 0);
    gtk_box_pack_end (GTK_BOX (row), reset, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (box), row, FALSE, FALSE, 0);
    g_object_set_data (G_OBJECT (row), "dakkar-key", (gpointer) key);
    g_object_set_data (G_OBJECT (row), "dakkar-picker", picker);
    g_object_set_data (G_OBJECT (row), "dakkar-reset", reset);
    g_object_set_data (G_OBJECT (row), "dakkar-hint", hint);
    dakkar_optional_sync (nemo_preferences, NULL, row);
    g_signal_connect (picker, "color-set", G_CALLBACK (dakkar_optional_set), (gpointer) key);
    g_signal_connect (reset, "clicked", G_CALLBACK (dakkar_optional_reset), (gpointer) key);
    g_signal_connect_object (nemo_preferences, "changed", G_CALLBACK (dakkar_optional_sync), row, 0);
}
/* long check-button labels wrap at narrow dialog widths */
static void
dakkar_wrap_button_label (GtkWidget *button)
{
    GtkWidget *child = gtk_bin_get_child (GTK_BIN (button));
    if (GTK_IS_LABEL (child)) {
        gtk_label_set_line_wrap (GTK_LABEL (child), TRUE);
        gtk_label_set_line_wrap_mode (GTK_LABEL (child), PANGO_WRAP_WORD_CHAR);
        gtk_label_set_max_width_chars (GTK_LABEL (child), 50);
        gtk_label_set_xalign (GTK_LABEL (child), 0);
    }
}
static void
dakkar_appearance_allow_narrow (GtkWidget *widget)
{
    if (GTK_IS_LABEL (widget)) {
        GtkLabel *label = GTK_LABEL (widget);
        if (!gtk_label_get_line_wrap (label) &&
            gtk_label_get_ellipsize (label) == PANGO_ELLIPSIZE_NONE &&
            !gtk_style_context_has_class (gtk_widget_get_style_context (widget), "dakkar-keep-width")) {
            const gchar *text = gtk_label_get_text (label);
            gtk_label_set_ellipsize (label, PANGO_ELLIPSIZE_END);
            if (text != NULL && *text && gtk_widget_get_tooltip_text (widget) == NULL)
                gtk_widget_set_tooltip_text (widget, text);
        }
        return;
    }
    if (GTK_IS_CONTAINER (widget))
        gtk_container_foreach (GTK_CONTAINER (widget), (GtkCallback) dakkar_appearance_allow_narrow, NULL);
}
static void
dakkar_section_heading (GtkWidget *box, const gchar *markup)
{
    GtkWidget *section = gtk_label_new (NULL);
    gtk_label_set_markup (GTK_LABEL (section), markup);
    gtk_label_set_xalign (GTK_LABEL (section), 0);
    gtk_label_set_line_wrap (GTK_LABEL (section), TRUE);
    gtk_box_pack_start (GTK_BOX (box), section, FALSE, FALSE, 0);
}
static void
dakkar_custom_theme_reset (GtkButton *button, gpointer unused)
{
    const gchar *keys[] = {DAKKAR_THEME_BACKGROUND, DAKKAR_THEME_SIDEBAR,
        DAKKAR_THEME_INSPECTOR, DAKKAR_THEME_TEXT, DAKKAR_THEME_ACCENT,
        DAKKAR_THEME_ACCENT_SECONDARY, DAKKAR_THEME_QA_BUTTON, DAKKAR_THEME_QA_TEXT,
        DAKKAR_THEME_QA_HOVER, DAKKAR_THEME_QA_BORDER, DAKKAR_THEME_STATUSBAR, DAKKAR_THEME_TOOLBAR, DAKKAR_THEME_TABS, DAKKAR_THEME_BUTTONS,
        DAKKAR_THEME_BORDERS, DAKKAR_THEME_HOVER, DAKKAR_THEME_MENUS,
        DAKKAR_THEME_SIDEBAR_HEADING, DAKKAR_THEME_OPACITY, DAKKAR_THEME_SIDEBAR_OPACITY,
        DAKKAR_THEME_DETAILS_OPACITY, DAKKAR_THEME_TOOLBAR_OPACITY,
        DAKKAR_THEME_SEE_THROUGH, DAKKAR_THEME_WHOLE_WINDOW_OPACITY};
    guint i;
    for (i=0; i<G_N_ELEMENTS (keys); i++) g_settings_reset (nemo_preferences, keys[i]);
}
/* dakkar-sidebar-icon-size (int) <-> combo active-id ("0", "16", ...);
 * a value outside the offered steps shows as the nearest step. */
static gboolean
dakkar_icon_size_get_mapping (GValue *value, GVariant *variant, gpointer data)
{
    static const gint steps[] = {16, 20, 24, 32, 48};
    gint size = g_variant_get_int32 (variant), best = 0;
    guint k;
    if (size > 0) {
        best = steps[0];
        for (k = 0; k < G_N_ELEMENTS (steps); k++)
            if (ABS (steps[k] - size) < ABS (best - size)) best = steps[k];
    }
    g_value_take_string (value, g_strdup_printf ("%d", best));
    return TRUE;
}
static GVariant *
dakkar_icon_size_set_mapping (const GValue *value, const GVariantType *type, gpointer data)
{
    const gchar *id = g_value_get_string (value);
    return g_variant_new_int32 (id != NULL ? CLAMP (atoi (id), 0, 48) : 0);
}
/* The hint under "See-through windows" follows the compositor live. */
static void
dakkar_transparency_hint_update (GdkScreen *screen, GtkWidget *hint)
{
    gboolean supported = dakkar_transparency_supported (screen);
    gtk_label_set_text (GTK_LABEL (hint), supported ?
        _("Text and icons stay fully opaque; only the backgrounds become see-through.") :
        _("No compositing window manager is running, so Dakkar is drawn opaque. "
          "Transparency appears as soon as a compositor (such as KWin) is active."));
}
static void
dakkar_icon_pack_selected (GtkComboBox *combo, gpointer unused)
{
    const gchar *id = gtk_combo_box_get_active_id (combo);
    if (id != NULL) g_settings_set_string (nemo_preferences, DAKKAR_SIDEBAR_ICON_PACK, id);
}
static void
dakkar_icon_pack_refresh (GSettings *settings, gchar *key, gpointer combo)
{
    gchar *name = g_settings_get_string (settings, DAKKAR_SIDEBAR_ICON_PACK);
    if (!gtk_combo_box_set_active_id (GTK_COMBO_BOX (combo), name))
        gtk_combo_box_set_active_id (GTK_COMBO_BOX (combo), "system");
    g_free (name);
}
/* GtkRange::value is a double; g_settings_bind does not safely map every
 * GSettings integer schema key to a double GObject property. Keep integer
 * sliders in sync explicitly so their controls cannot appear to work while
 * silently rejecting the save. */
static void
dakkar_appearance_int_setting_changed (GSettings *settings, gchar *key, gpointer widget)
{
    gint value = g_settings_get_int (settings, key);
    if ((gint) (gtk_range_get_value (GTK_RANGE (widget)) + .5) != value)
        gtk_range_set_value (GTK_RANGE (widget), value);
}

static void
dakkar_appearance_int_range_changed (GtkRange *range, gpointer user_data)
{
    const gchar *key = user_data;
    gint value = (gint) (gtk_range_get_value (range) + .5);
    if (g_settings_get_int (nemo_preferences, key) != value)
        g_settings_set_int (nemo_preferences, key, value);
}

static GtkWidget *
dakkar_appearance_slider (GtkWidget *parent, const gchar *name, const gchar *key,
                         gdouble minimum, gdouble maximum, gdouble step, guint digits)
{
    GtkWidget *row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *title = gtk_label_new (name);
    GtkWidget *control = gtk_scale_new_with_range (GTK_ORIENTATION_HORIZONTAL,
                                                   minimum, maximum, step);
    gtk_label_set_xalign (GTK_LABEL (title), 0);
    gtk_widget_set_size_request (control, 185, -1);
    gtk_scale_set_draw_value (GTK_SCALE (control), TRUE);
    gtk_scale_set_digits (GTK_SCALE (control), digits);
    gtk_box_pack_start (GTK_BOX (row), title, TRUE, TRUE, 0);
    gtk_box_pack_end (GTK_BOX (row), control, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (parent), row, FALSE, FALSE, 0);
    /* Numeric settings with integer type require an explicit conversion. */
    GVariant *stored = g_settings_get_value (nemo_preferences, key);
    gboolean integer_setting = g_variant_is_of_type (stored, G_VARIANT_TYPE_INT32);
    g_variant_unref (stored);
    if (integer_setting) {
        gchar *changed_signal = g_strconcat ("changed::", key, NULL);
        g_signal_connect_object (nemo_preferences, changed_signal,
            G_CALLBACK (dakkar_appearance_int_setting_changed), control, 0);
        g_signal_connect (control, "value-changed",
            G_CALLBACK (dakkar_appearance_int_range_changed), (gpointer)key);
        dakkar_appearance_int_setting_changed (nemo_preferences, (gchar *)key, control);
        g_free (changed_signal);
    } else {
        /* GtkScale has no "value" property (it lives on the adjustment):
         * binding the scale itself failed with a critical and the slider
         * never loaded or saved its key. */
        g_settings_bind (nemo_preferences, key,
                         gtk_range_get_adjustment (GTK_RANGE (control)),
                         "value", G_SETTINGS_BIND_DEFAULT);
    }
    return control;
}

/* Color theme combo: follow the OS, or prefer a variant in Files only. */
static void
appearance_add_color_mode (GtkWidget *page)
{
    GtkWidget *label = gtk_label_new (_("Color theme"));
    gtk_widget_set_halign (label, GTK_ALIGN_START);
    gtk_box_pack_start (GTK_BOX (page), label, FALSE, FALSE, 0);
    GtkWidget *color_mode = gtk_combo_box_text_new ();
    gtk_combo_box_text_append (GTK_COMBO_BOX_TEXT (color_mode),
                               "system", _("Follow system theme"));
    gtk_combo_box_text_append (GTK_COMBO_BOX_TEXT (color_mode),
                               "light", _("Prefer light"));
    gtk_combo_box_text_append (GTK_COMBO_BOX_TEXT (color_mode),
                               "dark", _("Prefer dark"));
    gtk_widget_set_halign (color_mode, GTK_ALIGN_START);
    gtk_box_pack_start (GTK_BOX (page), color_mode, FALSE, FALSE, 0);
    g_settings_bind (nemo_preferences, NEMO_PREFERENCES_FILES_COLOR_MODE,
                     color_mode, "active-id", G_SETTINGS_BIND_DEFAULT);
}

/* The custom colors toggle and everything it unlocks: surface, toolbar
 * and Quick Actions colors, transparency and the reset button. */
static void
appearance_add_custom_theme (GtkWidget *page)
{
    GtkWidget *custom_toggle = gtk_check_button_new_with_label (
        _("Enable custom Dakkar colors and transparency"));
    dakkar_wrap_button_label (custom_toggle);
    gtk_box_pack_start (GTK_BOX (page), custom_toggle, FALSE, FALSE, 0);
    g_settings_bind (nemo_preferences, DAKKAR_THEME_CUSTOM,
                     custom_toggle, "active", G_SETTINGS_BIND_DEFAULT);
    GtkWidget *custom_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_start (custom_box, 14);
    gtk_box_pack_start (GTK_BOX (page), custom_box, FALSE, FALSE, 0);
    g_object_bind_property (custom_toggle, "active", custom_box, "sensitive", G_BINDING_SYNC_CREATE);
    GtkWidget *section = gtk_label_new (NULL);
    gtk_label_set_markup (GTK_LABEL (section), _("<b>Surfaces</b>"));
    gtk_label_set_xalign (GTK_LABEL (section), 0);
    gtk_box_pack_start (GTK_BOX (custom_box), section, FALSE, FALSE, 0);
    dakkar_color_preference (custom_box, _("Content background"), DAKKAR_THEME_BACKGROUND);
    dakkar_color_preference (custom_box, _("Sidebar background"), DAKKAR_THEME_SIDEBAR);
    dakkar_color_preference (custom_box, _("Details background"), DAKKAR_THEME_INSPECTOR);
    dakkar_color_preference (custom_box, _("Text color"), DAKKAR_THEME_TEXT);
    dakkar_color_preference (custom_box, _("Selection accent"), DAKKAR_THEME_ACCENT);
    dakkar_optional_color_preference (custom_box, _("Secondary selection and hover accent"),
        _("Automatic: a dimmer selection accent"),
        _("Used for selections in unfocused views and inactive Miller columns, and when the "
          "pointer is over or presses a button."), DAKKAR_THEME_ACCENT_SECONDARY);
    section = gtk_label_new (NULL);
    gtk_label_set_markup (GTK_LABEL (section), _("<b>Toolbar, tabs and menus</b>"));
    gtk_label_set_xalign (GTK_LABEL (section), 0);
    gtk_box_pack_start (GTK_BOX (custom_box), section, FALSE, FALSE, 0);
    dakkar_color_preference (custom_box, _("Toolbar background"), DAKKAR_THEME_TOOLBAR);
    dakkar_color_preference (custom_box, _("Tab strip background"), DAKKAR_THEME_TABS);
    dakkar_color_preference (custom_box, _("Toolbar button background"), DAKKAR_THEME_BUTTONS);
    dakkar_color_preference (custom_box, _("Panel and divider color"), DAKKAR_THEME_BORDERS);
    dakkar_color_preference (custom_box, _("Sidebar and tab hover"), DAKKAR_THEME_HOVER);
    dakkar_color_preference (custom_box, _("Sidebar section headings"), DAKKAR_THEME_SIDEBAR_HEADING);
    dakkar_color_preference (custom_box, _("Menus and popovers"), DAKKAR_THEME_MENUS);
    dakkar_optional_color_preference (custom_box, _("Statusbar background"),
        _("Automatic: the toolbar background"),
        _("The bar at the bottom of the window. Its buttons use the toolbar button "
          "background, its sliders the selection accent."), DAKKAR_THEME_STATUSBAR);
    dakkar_section_heading (custom_box, _("<b>Quick Actions</b>"));
    dakkar_optional_color_preference (custom_box, _("Button background"),
        _("Automatic: the toolbar button background"), NULL, DAKKAR_THEME_QA_BUTTON);
    dakkar_optional_color_preference (custom_box, _("Button text and icons"),
        _("Automatic: the text color"), NULL, DAKKAR_THEME_QA_TEXT);
    dakkar_optional_color_preference (custom_box, _("Button hover"),
        _("Automatic: the secondary selection and hover accent"),
        _("Pressed buttons mix this color with the selection accent."), DAKKAR_THEME_QA_HOVER);
    dakkar_optional_color_preference (custom_box, _("Button border"),
        _("Automatic: the panel and divider color"), NULL, DAKKAR_THEME_QA_BORDER);
    section = gtk_label_new (NULL);
    gtk_label_set_markup (GTK_LABEL (section), _("<b>Transparency</b>"));
    gtk_label_set_xalign (GTK_LABEL (section), 0);
    gtk_box_pack_start (GTK_BOX (custom_box), section, FALSE, FALSE, 0);
    GtkWidget *opacity_row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *see_through = gtk_check_button_new_with_mnemonic (
        _("_See-through windows (show the desktop and windows behind Dakkar)"));
    g_settings_bind (nemo_preferences, DAKKAR_THEME_SEE_THROUGH,
                     see_through, "active", G_SETTINGS_BIND_DEFAULT);
    dakkar_wrap_button_label (see_through);
    gtk_box_pack_start (GTK_BOX (custom_box), see_through, FALSE, FALSE, 0);
    GtkWidget *hint = gtk_label_new (NULL);
    gtk_label_set_xalign (GTK_LABEL (hint), 0);
    gtk_label_set_line_wrap (GTK_LABEL (hint), TRUE);
    gtk_label_set_max_width_chars (GTK_LABEL (hint), 60);
    gtk_style_context_add_class (gtk_widget_get_style_context (hint), "dim-label");
    gtk_widget_set_margin_start (hint, 24);
    gtk_box_pack_start (GTK_BOX (custom_box), hint, FALSE, FALSE, 0);
    dakkar_transparency_hint_update (gtk_widget_get_screen (hint), hint);
    g_signal_connect_object (gtk_widget_get_screen (hint), "composited-changed",
                             G_CALLBACK (dakkar_transparency_hint_update), hint, 0);
    GtkWidget *opacity_label = gtk_label_new (_("Window opacity"));
    gtk_widget_set_tooltip_text (opacity_label,
        _("Background opacity of the whole window; text and icons stay crisp"));
    GtkWidget *opacity_scale = gtk_scale_new_with_range (GTK_ORIENTATION_HORIZONTAL, .30, 1.0, .05);
    gtk_scale_set_draw_value (GTK_SCALE (opacity_scale), TRUE);
    gtk_scale_set_digits (GTK_SCALE (opacity_scale), 2);
    gtk_box_pack_start (GTK_BOX (opacity_row), opacity_label, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (opacity_row), opacity_scale, TRUE, TRUE, 0);
    gtk_box_pack_start (GTK_BOX (custom_box), opacity_row, FALSE, FALSE, 0);
    g_settings_bind (nemo_preferences, DAKKAR_THEME_OPACITY,
                     gtk_range_get_adjustment (GTK_RANGE (opacity_scale)),
                     "value", G_SETTINGS_BIND_DEFAULT);
    dakkar_appearance_slider (custom_box, _("Sidebar opacity"),
        DAKKAR_THEME_SIDEBAR_OPACITY, 0.30, 1.0, 0.05, 2);
    dakkar_appearance_slider (custom_box, _("Details opacity"),
        DAKKAR_THEME_DETAILS_OPACITY, 0.30, 1.0, 0.05, 2);
    dakkar_appearance_slider (custom_box, _("Toolbar opacity"),
        DAKKAR_THEME_TOOLBAR_OPACITY, 0.30, 1.0, 0.05, 2);
    dakkar_appearance_slider (custom_box, _("Whole-window opacity (dims text and icons too)"),
        DAKKAR_THEME_WHOLE_WINDOW_OPACITY, 0.30, 1.0, 0.05, 2);
    GtkWidget *reset_colors = gtk_button_new_with_label (_("Reset custom colors"));
    gtk_widget_set_halign (reset_colors, GTK_ALIGN_START);
    g_signal_connect (reset_colors, "clicked", G_CALLBACK (dakkar_custom_theme_reset), NULL);
    gtk_box_pack_start (GTK_BOX (custom_box), reset_colors, FALSE, FALSE, 0);
}

/* Combo with "follow system" plus every installed icon theme (a dir with
 * an index.theme), sorted and without duplicates across the search path.
 * Only installed themes are listed; Dakkar ships no third-party icons. */
static GtkWidget *
appearance_icon_pack_combo_new (void)
{
    GtkWidget *icon_pack = gtk_combo_box_text_new ();
    gtk_combo_box_text_append (GTK_COMBO_BOX_TEXT (icon_pack), "system", _("Follow system icon pack"));
    const gchar *system_dirs[] = {"/usr/share/icons", "/usr/local/share/icons", NULL};
    gchar *user_icons = g_build_filename (g_get_user_data_dir (), "icons", NULL);
    gchar *legacy_icons = g_build_filename (g_get_home_dir (), ".icons", NULL);
    const gchar *icon_dirs[] = {system_dirs[0], system_dirs[1], user_icons,
                                legacy_icons, NULL};
    GHashTable *seen_packs = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                    g_free, NULL);
    GList *installed_packs = NULL, *cursor;
    guint dir_index;
    for (dir_index = 0; icon_dirs[dir_index] != NULL; dir_index++) {
        GDir *icon_dir = g_dir_open (icon_dirs[dir_index], 0, NULL);
        if (icon_dir == NULL) continue;
        const gchar *entry;
        while ((entry = g_dir_read_name (icon_dir)) != NULL) {
            gchar *index_file = g_build_filename (icon_dirs[dir_index], entry,
                                                  "index.theme", NULL);
            if (g_file_test (index_file, G_FILE_TEST_IS_REGULAR) &&
                !g_hash_table_contains (seen_packs, entry)) {
                g_hash_table_add (seen_packs, g_strdup (entry));
                installed_packs = g_list_prepend (installed_packs, g_strdup (entry));
            }
            g_free (index_file);
        }
        g_dir_close (icon_dir);
    }
    installed_packs = g_list_sort (installed_packs, (GCompareFunc)g_strcmp0);
    for (cursor = installed_packs; cursor != NULL; cursor = cursor->next) {
        const gchar *theme_name = cursor->data;
        gtk_combo_box_text_append (GTK_COMBO_BOX_TEXT (icon_pack),
                                   theme_name, theme_name);
    }
    g_list_free_full (installed_packs, g_free);
    g_hash_table_unref (seen_packs);
    g_free (user_icons);
    g_free (legacy_icons);
    return icon_pack;
}

/* Icon pack, monochrome toggle and the per-category icon colors. */
static void
appearance_add_sidebar_icons (GtkWidget *page)
{
    GtkWidget *icon_heading = gtk_label_new (_("Places sidebar icons"));
    gtk_widget_set_halign (icon_heading, GTK_ALIGN_START);
    gtk_box_pack_start (GTK_BOX (page), icon_heading, FALSE, FALSE, 2);
    GtkWidget *icon_pack = appearance_icon_pack_combo_new ();
    gtk_widget_set_halign (icon_pack, GTK_ALIGN_START);
    gtk_box_pack_start (GTK_BOX (page), icon_pack, FALSE, FALSE, 0);
    dakkar_icon_pack_refresh (nemo_preferences, NULL, icon_pack);
    g_signal_connect (icon_pack, "changed", G_CALLBACK (dakkar_icon_pack_selected), NULL);
    g_signal_connect_object (nemo_preferences, "changed::" DAKKAR_SIDEBAR_ICON_PACK,
        G_CALLBACK (dakkar_icon_pack_refresh), icon_pack, 0);
    GtkWidget *mono = gtk_check_button_new_with_label (
        _("Monochrome symbolic icons (individual colors below)"));
    gtk_box_pack_start (GTK_BOX (page), mono, FALSE, FALSE, 0);
    g_settings_bind (nemo_preferences, DAKKAR_SIDEBAR_ICONS_MONO,
                     mono, "active", G_SETTINGS_BIND_DEFAULT);
    GtkWidget *colors_grid = gtk_grid_new ();
    gtk_grid_set_row_spacing (GTK_GRID (colors_grid), 5);
    gtk_grid_set_column_spacing (GTK_GRID (colors_grid), 12);
    const gchar *categories[] = {"home", "desktop", "recent", "filesystem", "trash", "devices", "network", "favorites", "bookmarks"};
    const gchar *category_names[] = {N_("Home"), N_("Desktop"), N_("Recent"), N_("File System"), N_("Trash"), N_("Devices"), N_("Network"), N_("Favorites"), N_("Bookmarks")};
    guint category;
    for (category=0; category<G_N_ELEMENTS (categories); category++) {
        gchar *key = g_strdup_printf ("dakkar-sidebar-color-%s", categories[category]);
        GtkWidget *row_color = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
        GtkWidget *name = gtk_label_new (_(category_names[category]));
        GtkWidget *picker = gtk_color_button_new ();
        dakkar_color_settings_changed (nemo_preferences, key, picker);
        g_object_set_data_full (G_OBJECT (picker), "dakkar-icon-color-key", key, g_free);
        g_signal_connect (picker, "color-set", G_CALLBACK (dakkar_color_button_changed), key);
        gtk_box_pack_start (GTK_BOX (row_color), name, TRUE, TRUE, 0);
        gtk_box_pack_end (GTK_BOX (row_color), picker, FALSE, FALSE, 0);
        gtk_grid_attach (GTK_GRID (colors_grid), row_color, category % 2, category / 2, 1, 1);
    }
    gtk_box_pack_start (GTK_BOX (page), colors_grid, FALSE, FALSE, 0);
    g_object_bind_property (mono, "active", colors_grid, "sensitive", G_BINDING_SYNC_CREATE);
}

/* Read-only line describing the GTK theme in use, so a theme without a
 * usable dark variant is easy to spot. Changes no OS preference. */
static void
appearance_add_theme_status (GtkWidget *page)
{
    GtkWidget *theme_status = gtk_label_new (NULL);
    gtk_widget_set_halign (theme_status, GTK_ALIGN_START);
    gtk_label_set_xalign (GTK_LABEL (theme_status), 0.0);
    gtk_label_set_line_wrap (GTK_LABEL (theme_status), TRUE);
    gtk_style_context_add_class (gtk_widget_get_style_context (theme_status),
                                 GTK_STYLE_CLASS_DIM_LABEL);
    gtk_box_pack_start (GTK_BOX (page), theme_status, FALSE, FALSE, 0);
    GtkSettings *appearance_settings = gtk_settings_get_default ();
    if (appearance_settings != NULL) {
        g_signal_connect_object (appearance_settings, "notify::gtk-theme-name",
                G_CALLBACK (files_appearance_theme_description_changed),
                theme_status, 0);
        g_signal_connect_object (appearance_settings,
                "notify::gtk-application-prefer-dark-theme",
                G_CALLBACK (files_appearance_theme_description_changed),
                theme_status, 0);
        files_appearance_theme_description_changed (G_OBJECT (appearance_settings),
                                                    NULL, theme_status);
    }
}

/* Square/rounded radio pair and the radius slider + spin button, kept in
 * sync through the setting. */
static void
appearance_add_corners (GtkWidget *page)
{
    GtkWidget *label, *square, *rounded, *row, *scale, *spin;

    label = gtk_label_new (_("Corner style"));
    gtk_widget_set_halign (label, GTK_ALIGN_START);
    gtk_box_pack_start (GTK_BOX (page), label, FALSE, FALSE, 0);

    square = gtk_radio_button_new_with_label (NULL, _("Square corners"));
    rounded = gtk_radio_button_new_with_label_from_widget (
                  GTK_RADIO_BUTTON (square), _("Rounded corners"));
    gtk_box_pack_start (GTK_BOX (page), square, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (page), rounded, FALSE, FALSE, 0);
    g_settings_bind (nemo_preferences, NEMO_PREFERENCES_FILES_ROUNDED_CORNERS,
                     rounded, "active", G_SETTINGS_BIND_DEFAULT);

    row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 10);
    label = gtk_label_new (_("Corner radius"));
    gtk_box_pack_start (GTK_BOX (row), label, FALSE, FALSE, 0);
    scale = gtk_scale_new_with_range (GTK_ORIENTATION_HORIZONTAL, 2, 32, 1);
    gtk_scale_set_draw_value (GTK_SCALE (scale), FALSE);
    gtk_widget_set_size_request (scale, 160, -1);
    gtk_box_pack_start (GTK_BOX (row), scale, TRUE, TRUE, 0);
    spin = gtk_spin_button_new_with_range (2, 32, 1);
    gtk_spin_button_set_digits (GTK_SPIN_BUTTON (spin), 0);
    gtk_box_pack_start (GTK_BOX (row), spin, FALSE, FALSE, 0);
    label = gtk_label_new (_("px"));
    gtk_box_pack_start (GTK_BOX (row), label, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (page), row, FALSE, FALSE, 0);

    g_signal_connect_object (nemo_preferences,
        "changed::" NEMO_PREFERENCES_FILES_CORNER_RADIUS,
        G_CALLBACK (dakkar_radius_from_settings), scale, 0);
    g_signal_connect_object (nemo_preferences,
        "changed::" NEMO_PREFERENCES_FILES_CORNER_RADIUS,
        G_CALLBACK (dakkar_radius_from_settings), spin, 0);
    g_signal_connect (scale, "value-changed", G_CALLBACK (dakkar_radius_scale_changed), NULL);
    g_signal_connect (spin, "value-changed", G_CALLBACK (dakkar_radius_spin_changed), NULL);
    dakkar_radius_from_settings (nemo_preferences, NULL, scale);
    dakkar_radius_from_settings (nemo_preferences, NULL, spin);
    g_object_bind_property (rounded, "active", row, "sensitive",
                            G_BINDING_SYNC_CREATE);
}

/* Sidebar scale, icon size, spacing sliders, disk bars and places/tree. */
static void
appearance_add_sidebar_layout (GtkWidget *page)
{
    GtkWidget *label = gtk_label_new (_("Sidebar icon & text scale"));
    gtk_widget_set_halign (label, GTK_ALIGN_START);
    gtk_box_pack_start (GTK_BOX (page), label, FALSE, FALSE, 0);
    GtkWidget *sidebar_scale = gtk_scale_new_with_range (
        GTK_ORIENTATION_HORIZONTAL, 0.65, 1.85, 0.05);
    gtk_widget_set_size_request (sidebar_scale, 140, -1);
    gtk_widget_set_halign (sidebar_scale, GTK_ALIGN_START);
    gtk_scale_set_draw_value (GTK_SCALE (sidebar_scale), FALSE);
    gtk_box_pack_start (GTK_BOX (page), sidebar_scale, FALSE, FALSE, 0);
    g_settings_bind (nemo_preferences, NEMO_PREFERENCES_FILES_SIDEBAR_SCALE,
                     gtk_range_get_adjustment (GTK_RANGE (sidebar_scale)),
                     "value", G_SETTINGS_BIND_DEFAULT);

    {
        /* Icons only: the sidebar text keeps its size. */
        static const gint icon_sizes[] = {16, 20, 24, 32, 48};
        GtkWidget *size_row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 10);
        GtkWidget *size_label = gtk_label_new_with_mnemonic (_("Sidebar _icon size"));
        GtkWidget *size_combo = gtk_combo_box_text_new ();
        guint k;
        gtk_combo_box_text_append (GTK_COMBO_BOX_TEXT (size_combo), "0",
                                   _("Default (follows the scale above)"));
        for (k = 0; k < G_N_ELEMENTS (icon_sizes); k++) {
            gchar *id = g_strdup_printf ("%d", icon_sizes[k]);
            /* Translators: an icon size in pixels, "24 px" */
            gchar *text = g_strdup_printf (_("%d px"), icon_sizes[k]);
            gtk_combo_box_text_append (GTK_COMBO_BOX_TEXT (size_combo), id, text);
            g_free (id); g_free (text);
        }
        gtk_label_set_mnemonic_widget (GTK_LABEL (size_label), size_combo);
        gtk_widget_set_tooltip_text (size_combo,
            _("Enlarges only the sidebar icons; the text and the view zoom stay as they are"));
        gtk_box_pack_start (GTK_BOX (size_row), size_label, FALSE, FALSE, 0);
        gtk_box_pack_start (GTK_BOX (size_row), size_combo, FALSE, FALSE, 0);
        gtk_box_pack_start (GTK_BOX (page), size_row, FALSE, FALSE, 0);
        g_settings_bind_with_mapping (nemo_preferences, DAKKAR_SIDEBAR_ICON_SIZE,
                                      size_combo, "active-id", G_SETTINGS_BIND_DEFAULT,
                                      dakkar_icon_size_get_mapping, dakkar_icon_size_set_mapping,
                                      NULL, NULL);
    }
    dakkar_appearance_slider (page, _("Sidebar row spacing"),
        DAKKAR_SIDEBAR_ROW_PADDING, 1, 12, 1, 0);
    dakkar_appearance_slider (page, _("Sidebar selection rounding"),
        DAKKAR_SIDEBAR_SELECTION_RADIUS, 0, 16, 1, 0);
    dakkar_appearance_slider (page, _("Sidebar section spacing"),
        DAKKAR_SIDEBAR_HEADING_PADDING, 1, 16, 1, 0);
    {
        GtkWidget *disk = gtk_check_button_new_with_label (_("Show disk usage bars under drives in the sidebar"));
        gtk_widget_set_halign (disk, GTK_ALIGN_START);
        gtk_box_pack_start (GTK_BOX (page), disk, FALSE, FALSE, 0);
        g_settings_bind (nemo_preferences, DAKKAR_SIDEBAR_DISK_USAGE,
                         disk, "active", G_SETTINGS_BIND_DEFAULT);
    }
    label = gtk_label_new (_("Sidebar view"));
    gtk_widget_set_halign (label, GTK_ALIGN_START);
    gtk_box_pack_start (GTK_BOX (page), label, FALSE, FALSE, 0);
    GtkWidget *sidebar_mode = gtk_combo_box_text_new ();
    gtk_combo_box_text_append (GTK_COMBO_BOX_TEXT (sidebar_mode),
                               "places", _("Places"));
    gtk_combo_box_text_append (GTK_COMBO_BOX_TEXT (sidebar_mode),
                               "tree", _("Tree View"));
    gtk_widget_set_halign (sidebar_mode, GTK_ALIGN_START);
    gtk_box_pack_start (GTK_BOX (page), sidebar_mode, FALSE, FALSE, 0);
    g_settings_bind (nemo_window_state, NEMO_WINDOW_STATE_SIDE_PANE_VIEW,
                     sidebar_mode, "active-id", G_SETTINGS_BIND_DEFAULT);
}

/* Small mock window. nemo-corner-style.c styles the dakkar-preview-*
 * classes with the current colors and radius, so changes show up here. */
static void
appearance_add_preview (GtkWidget *page)
{
    GtkWidget *label, *sample, *sample_content, *sample_label;

    label = gtk_label_new (_("Live preview"));
    gtk_widget_set_halign (label, GTK_ALIGN_START);
    gtk_box_pack_start (GTK_BOX (page), label, FALSE, FALSE, 0);
    sample = gtk_frame_new (NULL);
    gtk_style_context_add_class (gtk_widget_get_style_context (sample),
                                 "files-corner-sample");
    gtk_widget_set_size_request (sample, 260, 135);
    gtk_widget_set_halign (sample, GTK_ALIGN_START);
    sample_content = gtk_box_new (GTK_ORIENTATION_VERTICAL, 4);
    gtk_container_set_border_width (GTK_CONTAINER (sample_content), 8);
    GtkWidget *preview_toolbar = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_style_context_add_class (gtk_widget_get_style_context (preview_toolbar),
                                 "dakkar-preview-toolbar");
    sample_label = gtk_label_new (_("Files    ‹    ›       Home / Documents"));
    gtk_box_pack_start (GTK_BOX (preview_toolbar), sample_label, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (sample_content), preview_toolbar, FALSE, FALSE, 0);
    GtkWidget *preview_columns = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 5);
    GtkWidget *preview_sidebar = gtk_box_new (GTK_ORIENTATION_VERTICAL, 5);
    GtkWidget *preview_content = gtk_box_new (GTK_ORIENTATION_VERTICAL, 5);
    GtkWidget *preview_details = gtk_box_new (GTK_ORIENTATION_VERTICAL, 5);
    gtk_style_context_add_class (gtk_widget_get_style_context (preview_sidebar), "dakkar-preview-sidebar");
    gtk_style_context_add_class (gtk_widget_get_style_context (preview_content), "dakkar-preview-content");
    gtk_style_context_add_class (gtk_widget_get_style_context (preview_details), "dakkar-preview-details");
    gtk_box_pack_start (GTK_BOX (preview_sidebar), gtk_label_new (_("Home")), FALSE, FALSE, 0);
    GtkWidget *preview_heading = gtk_label_new (_("Favorites"));
    gtk_style_context_add_class (gtk_widget_get_style_context (preview_heading),
                                 "dakkar-preview-heading");
    gtk_box_pack_start (GTK_BOX (preview_sidebar), preview_heading, FALSE, FALSE, 0);
    GtkWidget *preview_selection = gtk_label_new (_("Documents"));
    gtk_style_context_add_class (gtk_widget_get_style_context (preview_selection), "dakkar-preview-selection");
    gtk_box_pack_start (GTK_BOX (preview_content), preview_selection, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (preview_content), gtk_label_new (_("Projects")), FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (preview_details), gtk_label_new (_("Details")), FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (preview_columns), preview_sidebar, TRUE, TRUE, 0);
    gtk_box_pack_start (GTK_BOX (preview_columns), preview_content, TRUE, TRUE, 0);
    gtk_box_pack_start (GTK_BOX (preview_columns), preview_details, TRUE, TRUE, 0);
    gtk_box_pack_start (GTK_BOX (sample_content), preview_columns, TRUE, TRUE, 0);
    gtk_container_add (GTK_CONTAINER (sample), sample_content);
    gtk_box_pack_start (GTK_BOX (page), sample, FALSE, FALSE, 0);

    label = gtk_label_new (_("Applies to file views, sidebar and preview.\n"
                             "Miller's internal column dividers remain square."));
    gtk_widget_set_halign (label, GTK_ALIGN_START);
    gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
    gtk_box_pack_start (GTK_BOX (page), label, FALSE, FALSE, 0);
}

static void
nemo_file_management_properties_setup_appearance_page (GtkBuilder *builder)
{
    GtkStack *stack = GTK_STACK (gtk_builder_get_object (builder, "page_stack"));
    GtkWidget *page = gtk_box_new (GTK_ORIENTATION_VERTICAL, 14);
    GtkWidget *heading;

    gtk_container_set_border_width (GTK_CONTAINER (page), 18);
    heading = gtk_label_new (NULL);
    gtk_label_set_markup (GTK_LABEL (heading), _("<b>Appearance</b>"));
    gtk_widget_set_halign (heading, GTK_ALIGN_START);
    gtk_box_pack_start (GTK_BOX (page), heading, FALSE, FALSE, 0);

    appearance_add_color_mode (page);
    appearance_add_custom_theme (page);
    appearance_add_sidebar_icons (page);
    appearance_add_theme_status (page);
    appearance_add_corners (page);
    appearance_add_sidebar_layout (page);
    appearance_add_preview (page);

    /* The page is long, so it scrolls. No label may force it wider than
     * the window (that pushed the color swatches out of view): labels that
     * do not wrap ellipsize instead and keep their text as a tooltip. */
    dakkar_appearance_allow_narrow (page);
    GtkWidget *scroller = gtk_scrolled_window_new (NULL, NULL);
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroller),
        GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_container_add (GTK_CONTAINER (scroller), page);
    gtk_widget_show_all (scroller);
    gtk_stack_add_titled (stack, scroller, "appearance", _("Appearance"));
}

/* Reflect preference changes made from another open Preferences window.
 * Only user-initiated 'file-set' updates the setting, so a settings signal
 * cannot start a recursive save loop. */
static void
dakkar_startup_folder_setting_changed (GSettings *settings,
                                      gchar *key,
                                      gpointer chooser_widget)
{
    GtkFileChooser *chooser = GTK_FILE_CHOOSER (chooser_widget);
    gchar *uri = g_settings_get_string (settings,
                                       NEMO_PREFERENCES_DAKKAR_STARTUP_FOLDER);
    GFile *folder = *uri ? g_file_new_for_uri (uri) :
                           g_file_new_for_path (g_get_home_dir ());
    GFile *current = gtk_file_chooser_get_file (chooser);

    if (!g_file_is_native (folder)) {
        g_clear_object (&folder);
        folder = g_file_new_for_path (g_get_home_dir ());
    }
    if (current == NULL || !g_file_equal (folder, current))
        gtk_file_chooser_set_file (chooser, folder, NULL);

    g_clear_object (&current);
    g_object_unref (folder);
    g_free (uri);
    (void) key;
}

/* Dakkar's start folder is a local directory URI. File chooser rejects files
 * and does not modify already open tabs or explicitly specified locations. */
static void
dakkar_startup_folder_chosen (GtkFileChooserButton *chooser, gpointer unused)
{
    GFile *folder = gtk_file_chooser_get_file (GTK_FILE_CHOOSER (chooser));
    if (folder != NULL) {
        gchar *uri = g_file_get_uri (folder);
        g_settings_set_string (nemo_preferences,
                               NEMO_PREFERENCES_DAKKAR_STARTUP_FOLDER, uri);
        g_free (uri);
        g_object_unref (folder);
    }
    (void) unused;
}

static void
dakkar_startup_folder_reset (GtkButton *button, GtkFileChooserButton *chooser)
{
    GFile *home = g_file_new_for_path (g_get_home_dir ());
    g_settings_reset (nemo_preferences, NEMO_PREFERENCES_DAKKAR_STARTUP_FOLDER);
    gtk_file_chooser_set_file (GTK_FILE_CHOOSER (chooser), home, NULL);
    g_object_unref (home);
    (void) button;
}

static void
nemo_file_management_properties_setup_startup_page (GtkBuilder *builder)
{
    GtkStack *stack = GTK_STACK (gtk_builder_get_object (builder, "page_stack"));
    GtkWidget *page = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
    GtkWidget *label = gtk_label_new (_("Default folder when opening Files"));
    GtkWidget *chooser = gtk_file_chooser_button_new (
        _("Select default folder"), GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER);
    GtkWidget *reset = gtk_button_new_with_label (_("Use Home folder"));
    GtkWidget *help = gtk_label_new (_("Used only when opening Files normally. "
                                      "Explicit paths, bookmarks and tabs are unaffected. "
                                      "If the selected folder is unavailable, Home opens instead."));
    GFile *folder;
    gchar *uri;
    gtk_container_set_border_width (GTK_CONTAINER (page), 18);
    gtk_widget_set_halign (label, GTK_ALIGN_START);
    gtk_widget_set_halign (chooser, GTK_ALIGN_START);
    gtk_widget_set_halign (reset, GTK_ALIGN_START);
    gtk_widget_set_halign (help, GTK_ALIGN_START);
    gtk_label_set_line_wrap (GTK_LABEL (help), TRUE);
    gtk_file_chooser_set_local_only (GTK_FILE_CHOOSER (chooser), TRUE);
    gtk_widget_set_size_request (chooser, 300, -1);
    uri = g_settings_get_string (nemo_preferences, NEMO_PREFERENCES_DAKKAR_STARTUP_FOLDER);
    folder = *uri ? g_file_new_for_uri (uri) : g_file_new_for_path (g_get_home_dir ());
    if (!g_file_is_native (folder)) {
        g_clear_object (&folder);
        folder = g_file_new_for_path (g_get_home_dir ());
    }
    gtk_file_chooser_set_file (GTK_FILE_CHOOSER (chooser), folder, NULL);
    g_object_unref (folder);
    g_free (uri);
    gtk_box_pack_start (GTK_BOX (page), label, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (page), chooser, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (page), reset, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (page), help, FALSE, FALSE, 0);
    g_signal_connect_object (nemo_preferences,
        "changed::" NEMO_PREFERENCES_DAKKAR_STARTUP_FOLDER,
        G_CALLBACK (dakkar_startup_folder_setting_changed), chooser, 0);
    g_signal_connect (chooser, "file-set", G_CALLBACK (dakkar_startup_folder_chosen), NULL);
    g_signal_connect (reset, "clicked", G_CALLBACK (dakkar_startup_folder_reset), chooser);
    gtk_widget_show_all (page);
    gtk_stack_add_titled (stack, page, "startup", _("Startup"));
}

/* Shared Details page: the same field order and presence in all views.
 * Preferences writes GSettings; open inspectors observe changed:: and update
 * live. Disabled fields stay in the editor so they can be added back. */
static const char * const dakkar_details_field_labels[] = {
    N_("Name"), N_("File type"), N_("Size and item count"),
    N_("Last modified"), N_("Full path"), N_("Owner and permissions")
};
static const char * const dakkar_details_field_keys[] = {
    "name", "type", "size", "modified", "path", "owner"
};

static gboolean
dakkar_details_strv_contains (gchar **fields, const char *id)
{
    for (guint i = 0; fields && fields[i]; ++i)
        if (g_str_equal (fields[i], id)) return TRUE;
    return FALSE;
}

static void
dakkar_details_update_editor (GSettings *settings, gchar *key, GtkWidget *list)
{
    gchar **fields = g_settings_get_strv (nemo_preferences, "dakkar-inspector-fields");
    GList *rows = gtk_container_get_children (GTK_CONTAINER (list));
    guint place = 0;
    g_object_set_data (G_OBJECT (list), "dakkar-details-syncing", GINT_TO_POINTER (1));
    for (guint i = 0; fields[i]; ++i) {
        for (GList *r = rows; r; r = r->next) {
            GtkWidget *row = GTK_WIDGET (r->data);
            if (g_strcmp0 (g_object_get_data (G_OBJECT (row), "dakkar-field-id"),
                           fields[i]) == 0) {
                gtk_box_reorder_child (GTK_BOX (list), row, place++);
                break;
            }
        }
    }
    for (GList *r = rows; r; r = r->next) {
        GtkWidget *row = GTK_WIDGET (r->data);
        GtkWidget *toggle = g_object_get_data (G_OBJECT (row), "dakkar-field-toggle");
        const char *id = g_object_get_data (G_OBJECT (row), "dakkar-field-id");
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (toggle),
                                      dakkar_details_strv_contains (fields, id));
    }
    g_object_set_data (G_OBJECT (list), "dakkar-details-syncing", NULL);
    g_list_free (rows);
    g_strfreev (fields);
    (void)settings; (void)key;
}

static void
dakkar_details_toggle_field (GtkToggleButton *button, GtkWidget *list)
{
    GtkWidget *row = gtk_widget_get_parent (GTK_WIDGET (button));
    const char *id = g_object_get_data (G_OBJECT (row), "dakkar-field-id");
    gchar **saved, **next;
    guint n = 0, k = 0;
    gboolean active, found;
    if (g_object_get_data (G_OBJECT (list), "dakkar-details-syncing")) return;
    active = gtk_toggle_button_get_active (button);
    saved = g_settings_get_strv (nemo_preferences, "dakkar-inspector-fields");
    while (saved[n]) n++;
    found = dakkar_details_strv_contains (saved, id);
    if (found == active) { g_strfreev (saved); return; }
    next = g_new0 (gchar *, n + 2);
    for (guint i = 0; i < n; ++i)
        if (!g_str_equal (saved[i], id)) next[k++] = g_strdup (saved[i]);
    if (active) next[k++] = g_strdup (id);
    g_settings_set_strv (nemo_preferences, "dakkar-inspector-fields",
                         (const gchar * const *)next);
    g_strfreev (saved);
    g_strfreev (next);
}

static void
dakkar_details_move_field (GtkButton *button, GtkWidget *list)
{
    GtkWidget *row = gtk_widget_get_parent (GTK_WIDGET (button));
    const char *id = g_object_get_data (G_OBJECT (row), "dakkar-field-id");
    gboolean up = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (button),
                                                     "dakkar-move-up"));
    gchar **saved = g_settings_get_strv (nemo_preferences, "dakkar-inspector-fields");
    guint i = 0;
    while (saved[i] && g_strcmp0 (saved[i], id) != 0) i++;
    if (saved[i] && ((up && i > 0) || (!up && saved[i+1]))) {
        guint other = up ? i - 1 : i + 1;
        gchar *tmp = saved[i]; saved[i] = saved[other]; saved[other] = tmp;
        g_settings_set_strv (nemo_preferences, "dakkar-inspector-fields",
                             (const gchar * const *)saved);
    }
    g_strfreev (saved);
    (void)list;
}

static void
nemo_file_management_properties_setup_details_page (GtkBuilder *builder)
{
    GtkStack *stack = GTK_STACK (gtk_builder_get_object (builder, "page_stack"));
    GtkWidget *page = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
    GtkWidget *title = gtk_label_new (NULL);
    GtkWidget *hint = gtk_label_new (_("Choose which preview details appear in every view. "
        "The order below is the display order; changes apply immediately."));
    GtkWidget *list = gtk_box_new (GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_set_border_width (GTK_CONTAINER (page), 18);
    gtk_label_set_markup (GTK_LABEL (title), _("<b>Details &amp; Preview</b>"));
    gtk_label_set_xalign (GTK_LABEL (title), 0.0);
    gtk_label_set_line_wrap (GTK_LABEL (hint), TRUE);
    gtk_label_set_xalign (GTK_LABEL (hint), 0.0);
    gtk_box_pack_start (GTK_BOX (page), title, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (page), hint, FALSE, FALSE, 0);
    for (guint i = 0; i < G_N_ELEMENTS (dakkar_details_field_keys); ++i) {
        GtkWidget *row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
        GtkWidget *check = gtk_check_button_new_with_label (_(dakkar_details_field_labels[i]));
        GtkWidget *up = gtk_button_new_from_icon_name ("go-up-symbolic", GTK_ICON_SIZE_BUTTON);
        GtkWidget *down = gtk_button_new_from_icon_name ("go-down-symbolic", GTK_ICON_SIZE_BUTTON);
        g_object_set_data (G_OBJECT (row), "dakkar-field-id",
                           (gpointer)dakkar_details_field_keys[i]);
        g_object_set_data (G_OBJECT (row), "dakkar-field-toggle", check);
        g_object_set_data (G_OBJECT (up), "dakkar-move-up", GINT_TO_POINTER (1));
        gtk_widget_set_hexpand (check, TRUE);
        gtk_widget_set_halign (check, GTK_ALIGN_START);
        gtk_widget_set_tooltip_text (up, _("Move this row up"));
        gtk_widget_set_tooltip_text (down, _("Move this row down"));
        gtk_box_pack_start (GTK_BOX (row), check, TRUE, TRUE, 0);
        gtk_box_pack_start (GTK_BOX (row), up, FALSE, FALSE, 0);
        gtk_box_pack_start (GTK_BOX (row), down, FALSE, FALSE, 0);
        gtk_box_pack_start (GTK_BOX (list), row, FALSE, FALSE, 0);
        g_signal_connect (check, "toggled", G_CALLBACK (dakkar_details_toggle_field), list);
        g_signal_connect (up, "clicked", G_CALLBACK (dakkar_details_move_field), list);
        g_signal_connect (down, "clicked", G_CALLBACK (dakkar_details_move_field), list);
    }
    gtk_box_pack_start (GTK_BOX (page), list, FALSE, FALSE, 0);
    g_signal_connect_object (nemo_preferences, "changed::dakkar-inspector-fields",
        G_CALLBACK (dakkar_details_update_editor), list, 0);
    dakkar_details_update_editor (nemo_preferences, NULL, list);
    GtkWidget *width = gtk_spin_button_new_with_range (180, 650, 10);
    GtkWidget *height = gtk_spin_button_new_with_range (100, 1000, 10);
    GtkWidget *width_row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *height_row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_pack_start (GTK_BOX (width_row), gtk_label_new (_("Detail panel width (px)")), FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (width_row), width, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (height_row), gtk_label_new (_("Preview height (px)")), FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (height_row), height, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (page), width_row, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (page), height_row, FALSE, FALSE, 0);
    g_settings_bind (nemo_preferences, "dakkar-inspector-width",
                     width, "value", G_SETTINGS_BIND_DEFAULT);
    g_settings_bind (nemo_preferences, "dakkar-inspector-preview-height",
                     height, "value", G_SETTINGS_BIND_DEFAULT);
    gtk_widget_show_all (page);
    gtk_stack_add_titled (stack, page, "details-preview", _("Details & Preview"));
}

/* The Miller settings live in Nemo Preferences (GtkStackSidebar creates
 * its entry automatically); no separate settings window to go out of sync. */
static void
miller_reset_saved_widths (GtkButton *button, gpointer data)
{
	g_settings_reset (nemo_preferences, NEMO_PREFERENCES_MILLER_COLUMN_WIDTHS);
}

static GtkWidget *
miller_pref_checkbox (GtkWidget *box, const char *label, const char *key)
{
	GtkWidget *widget = gtk_check_button_new_with_label (label);
	gtk_widget_set_halign (widget, GTK_ALIGN_START);
	gtk_box_pack_start (GTK_BOX (box), widget, FALSE, FALSE, 0);
	g_settings_bind (nemo_preferences, key, widget, "active", G_SETTINGS_BIND_DEFAULT);
	return widget;
}

static void
nemo_file_management_properties_setup_miller_page (GtkBuilder *builder)
{
	GtkStack *stack = GTK_STACK (gtk_builder_get_object (builder, "page_stack"));
	GtkWidget *page = gtk_box_new (GTK_ORIENTATION_VERTICAL, 10);
	GtkWidget *spin, *label, *row, *reset;
	gtk_container_set_border_width (GTK_CONTAINER (page), 18);
	label = gtk_label_new (NULL);
	gtk_label_set_markup (GTK_LABEL (label), _("<b>Miller Columns</b>"));
	gtk_widget_set_halign (label, GTK_ALIGN_START);
	gtk_box_pack_start (GTK_BOX (page), label, FALSE, FALSE, 4);
	row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 12);
	label = gtk_label_new (_("Default column width (px):"));
	spin = gtk_spin_button_new_with_range (120, 800, 10);
	gtk_box_pack_start (GTK_BOX (row), label, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (row), spin, FALSE, FALSE, 0);
	g_settings_bind (nemo_preferences, NEMO_PREFERENCES_MILLER_COLUMN_WIDTH,
	                 spin, "value", G_SETTINGS_BIND_DEFAULT);
	gtk_box_pack_start (GTK_BOX (page), row, FALSE, FALSE, 0);
	{
		GtkWidget *fit = miller_pref_checkbox (page,
			_("Fit each column to its names (up to 30 characters)"),
			NEMO_PREFERENCES_MILLER_AUTO_FIT);
		gtk_widget_set_tooltip_text (fit,
			_("Longer names are shortened with an ellipsis and shown in full in the tooltip. "
			  "Drag a divider to size one column by hand; double-click it to fit again."));
		g_object_bind_property (fit, "active", row, "sensitive",
		                        G_BINDING_SYNC_CREATE | G_BINDING_INVERT_BOOLEAN);
	}
    GtkWidget *preview_toggle = miller_pref_checkbox (
        page, _("Show the details pane (all views)"), NEMO_PREFERENCES_MILLER_SHOW_PREVIEW);
    GtkWidget *details_toggle = miller_pref_checkbox (
        page, _("Show file owner and permissions"), NEMO_PREFERENCES_MILLER_PREVIEW_DETAILS);
    g_object_bind_property (preview_toggle, "active", details_toggle, "sensitive",
                            G_BINDING_SYNC_CREATE);
	miller_pref_checkbox (page, _("Quick Look preview on Space (all views)"), NEMO_PREFERENCES_MILLER_QUICK_LOOK);
	miller_pref_checkbox (page, _("Open folders during drag hover"), NEMO_PREFERENCES_MILLER_DRAG_HOVER);
	miller_pref_checkbox (page, _("Horizontal trackpad/wheel scrolling"), NEMO_PREFERENCES_MILLER_WHEEL_HORIZONTAL);
    GtkWidget *prefetch_toggle = miller_pref_checkbox (
        page, _("Read ahead selected local Miller folders"), "miller-prefetch-navigation");
    GtkWidget *prefetch_row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget *prefetch_spin = gtk_spin_button_new_with_range (1, 24, 1);
    GtkWidget *prefetch_label = gtk_label_new (_("Prefetch up to this many items:"));
    gtk_box_pack_start (GTK_BOX (prefetch_row), prefetch_label, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (prefetch_row), prefetch_spin, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (page), prefetch_row, FALSE, FALSE, 0);
    g_settings_bind (nemo_preferences, "miller-prefetch-limit", prefetch_spin,
                     "value", G_SETTINGS_BIND_DEFAULT);
    g_object_bind_property (prefetch_toggle, "active", prefetch_row, "sensitive",
                            G_BINDING_SYNC_CREATE);
    label = gtk_label_new (_("Default width applies to new columns. Manually saved widths take priority.\n"
                             "Use Reset saved column widths to apply the default to all columns.\n"
                             "Double-click a divider to auto-fit."));
	gtk_widget_set_halign (label, GTK_ALIGN_START);
	gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
	gtk_box_pack_start (GTK_BOX (page), label, FALSE, FALSE, 10);
	reset = gtk_button_new_with_label (_("Reset saved column widths"));
	gtk_widget_set_halign (reset, GTK_ALIGN_START);
	g_signal_connect (reset, "clicked", G_CALLBACK (miller_reset_saved_widths), NULL);
	gtk_box_pack_start (GTK_BOX (page), reset, FALSE, FALSE, 0);
	gtk_widget_show_all (page);
	gtk_stack_add_titled (stack, page, "miller", _("Miller Columns"));
}

static void
nemo_file_management_properties_setup_performance_page (GtkBuilder *builder)
{
    GtkStack *stack = GTK_STACK (gtk_builder_get_object (builder, "page_stack"));
    GtkWidget *page = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
    GtkWidget *label;
    gtk_container_set_border_width (GTK_CONTAINER (page), 18);
    label = gtk_label_new (NULL);
    gtk_label_set_markup (GTK_LABEL (label), _("<b>Performance</b>"));
    gtk_widget_set_halign (label, GTK_ALIGN_START);
    gtk_box_pack_start (GTK_BOX (page), label, FALSE, FALSE, 4);
    miller_pref_checkbox (page, _("Calculate local folder sizes in the background"),
                          "files-background-folder-size");
    label = gtk_label_new (_("Size scans stop after 3 seconds, 20,000 entries or 24 levels.\n"
                             "Remote locations and symbolic-link targets are not scanned."));
    gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
    gtk_label_set_xalign (GTK_LABEL (label), 0.0);
    gtk_box_pack_start (GTK_BOX (page), label, FALSE, FALSE, 6);

    gtk_widget_show_all (page);
    gtk_stack_add_titled (stack, page, "performance", _("Performance"));
}

static void
nemo_file_management_properties_setup_search_page (GtkBuilder *builder)
{
    GtkStack *stack = GTK_STACK (gtk_builder_get_object (builder, "page_stack"));
    GtkWidget *page = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
    GtkWidget *title = gtk_label_new (NULL);
    GtkWidget *description;
    gtk_container_set_border_width (GTK_CONTAINER (page), 18);
    gtk_label_set_markup (GTK_LABEL (title), _("<b>Search</b>"));
    gtk_label_set_xalign (GTK_LABEL (title), 0.0);
    gtk_box_pack_start (GTK_BOX (page), title, FALSE, FALSE, 4);
    GtkWidget *instant_toggle = miller_pref_checkbox (
        page, _("Instant Search while typing"), NEMO_PREFERENCES_FILES_INSTANT_SEARCH);
    GtkWidget *delay_row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget *delay_label = gtk_label_new (_("Delay after typing (ms)"));
    GtkWidget *delay = gtk_spin_button_new_with_range (150, 1500, 50);
    gtk_box_pack_start (GTK_BOX (delay_row), delay_label, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (delay_row), delay, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (page), delay_row, FALSE, FALSE, 0);
    g_settings_bind (nemo_preferences, NEMO_PREFERENCES_FILES_SEARCH_DELAY_MS,
                     delay, "value", G_SETTINGS_BIND_DEFAULT);
    g_object_bind_property (instant_toggle, "active", delay_row, "sensitive",
                            G_BINDING_SYNC_CREATE);
    description = gtk_label_new (_("Search results update after a short pause. Press Enter to search immediately.\n"
                                    "Content search is limited to supported local files and installed text extractors."));
    gtk_label_set_xalign (GTK_LABEL (description), 0.0);
    gtk_label_set_line_wrap (GTK_LABEL (description), TRUE);
    gtk_box_pack_start (GTK_BOX (page), description, FALSE, FALSE, 6);
    gtk_widget_show_all (page);
    gtk_stack_add_titled (stack, page, "search", _("Search"));
}

/* New tabs inherit this default; changing it does not forcibly regroup
 * already-open tabs or overwrite their independent collapsed-group state. */
static void
dakkar_preferences_setup_grouping (GtkBuilder *builder)
{
    GtkStack *stack = GTK_STACK (gtk_builder_get_object (builder, "page_stack"));
    GtkWidget *page = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
    GtkWidget *heading = gtk_label_new (_("Default Group By for new tabs"));
    GtkWidget *description = gtk_label_new (_(
        "Each open tab can independently override this default with its Group By toolbar button. "
        "Changing the default affects only newly opened tabs."));
    GtkWidget *combo = gtk_combo_box_text_new ();
    gtk_container_set_border_width (GTK_CONTAINER (page), 18);
    gtk_label_set_xalign (GTK_LABEL (heading), 0.0);
    gtk_label_set_xalign (GTK_LABEL (description), 0.0);
    gtk_label_set_line_wrap (GTK_LABEL (description), TRUE);
    gtk_combo_box_text_append (GTK_COMBO_BOX_TEXT (combo), "none", _("None"));
    gtk_combo_box_text_append (GTK_COMBO_BOX_TEXT (combo), "type", _("Type"));
    gtk_combo_box_text_append (GTK_COMBO_BOX_TEXT (combo), "name", _("Name"));
    gtk_combo_box_text_append (GTK_COMBO_BOX_TEXT (combo), "size", _("Size"));
    gtk_combo_box_text_append (GTK_COMBO_BOX_TEXT (combo), "created", _("Created"));
    gtk_combo_box_text_append (GTK_COMBO_BOX_TEXT (combo), "modified", _("Modified"));
    gtk_combo_box_text_append (GTK_COMBO_BOX_TEXT (combo), "owner", _("Owner"));
    gtk_combo_box_text_append (GTK_COMBO_BOX_TEXT (combo), "permissions", _("Permissions"));
    gtk_widget_set_halign (combo, GTK_ALIGN_START);
    gtk_box_pack_start (GTK_BOX (page), heading, FALSE, FALSE, 2);
    gtk_box_pack_start (GTK_BOX (page), combo, FALSE, FALSE, 2);
    gtk_box_pack_start (GTK_BOX (page), description, FALSE, FALSE, 2);
    g_settings_bind (nemo_preferences, "dakkar-group-by",
                     combo, "active-id", G_SETTINGS_BIND_DEFAULT);
    gtk_widget_show_all (page);
    gtk_stack_add_titled (stack, page, "grouping", _("Grouping"));
}

/* Only implemented transfer options are exposed.  There is deliberately no
 * 'mid-file Resume' or 'Recover from crash' toggle: Nemo cannot provide those
 * guarantees in the current backend. */
static void
dakkar_open_transfer_history (GtkButton *button, gpointer unused)
{
    gchar *folder = g_build_filename (g_get_user_data_dir (), "dakkar", "transfer-history", NULL);
    GFile *file;
    gchar *uri;
    GError *error = NULL;
    if (g_mkdir_with_parents (folder, 0700) != 0) {
        g_warning ("Could not create private transfer-history directory: %s", folder);
        g_free (folder);
        return;
    }
    file = g_file_new_for_path (folder);
    uri = g_file_get_uri (file);
    if (!gtk_show_uri_on_window (GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (button))),
                                 uri, GDK_CURRENT_TIME, &error)) {
        g_warning ("Could not open transfer history: %s", error->message);
        g_clear_error (&error);
    }
    g_free (uri);
    g_object_unref (file);
    g_free (folder);
    (void)unused;
}

static void
dakkar_preferences_setup_transfers (GtkBuilder *builder)
{
    GtkStack *stack = GTK_STACK (gtk_builder_get_object (builder, "page_stack"));
    GtkWidget *page = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
    GtkWidget *show_on_start = gtk_check_button_new_with_label (
        _("Automatically show the full transfer manager when a transfer starts"));
    GtkWidget *notify_on_success = gtk_check_button_new_with_label (
        _("Show a system notification only for confirmed successful transfers"));
    GtkWidget *speed_graph = gtk_check_button_new_with_label (
        _("Show the measured transfer speed graph"));
    GtkWidget *verify = gtk_check_button_new_with_label (
        _("Verify copied files with a checksum (recommended)"));
    GtkWidget *algo_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *algo_label = gtk_label_new (_("Checksum algorithm:"));
    GtkWidget *algo = gtk_combo_box_text_new ();
    GtkWidget *history = gtk_check_button_new_with_label (
        _("Keep a private transfer history (up to 100 jobs)"));
    GtkWidget *info = gtk_label_new (
        _("While a file is copied its contents are hashed; afterwards the copy is "
          "flushed to disk, dropped from the page cache and read back to compare the "
          "checksums. A mismatch marks the file as failed so it can be retried; when "
          "moving, the source is removed only after the copy verified. Folders and "
          "symbolic links are not hashed, and moves within one file system are renames "
          "that do not copy data.\n"
          "Pause takes effect immediately, also inside a large file. Transfers are not "
          "resumed after restarting Files."));
    GtkWidget *open = gtk_button_new_with_label (_("Open transfer history folder"));
    gtk_container_set_border_width (GTK_CONTAINER (page), 18);
    gtk_label_set_xalign (GTK_LABEL (info), 0.0);
    gtk_label_set_line_wrap (GTK_LABEL (info), TRUE);
    gtk_widget_set_halign (open, GTK_ALIGN_START);
    gtk_box_pack_start (GTK_BOX (page), show_on_start, FALSE, FALSE, 2);
    gtk_box_pack_start (GTK_BOX (page), notify_on_success, FALSE, FALSE, 2);
    gtk_box_pack_start (GTK_BOX (page), speed_graph, FALSE, FALSE, 2);
    gtk_box_pack_start (GTK_BOX (page), verify, FALSE, FALSE, 2);
    /* Translators: SHA-1, SHA-256 and MD5 are algorithm names. */
    gtk_combo_box_text_append (GTK_COMBO_BOX_TEXT (algo), "sha1", _("SHA-1 (fast, default)"));
    gtk_combo_box_text_append (GTK_COMBO_BOX_TEXT (algo), "sha256", _("SHA-256 (slower)"));
    gtk_combo_box_text_append (GTK_COMBO_BOX_TEXT (algo), "md5", _("MD5 (fast, weak)"));
    gtk_widget_set_margin_start (algo_box, 24);
    gtk_box_pack_start (GTK_BOX (algo_box), algo_label, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (algo_box), algo, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (page), algo_box, FALSE, FALSE, 2);
    g_settings_bind (nemo_preferences, "dakkar-transfer-verify-algorithm",
                     algo, "active-id", G_SETTINGS_BIND_DEFAULT);
    g_object_bind_property (verify, "active", algo_box, "sensitive", G_BINDING_SYNC_CREATE);
    gtk_box_pack_start (GTK_BOX (page), history, FALSE, FALSE, 2);
    gtk_box_pack_start (GTK_BOX (page), info, FALSE, FALSE, 6);
    gtk_box_pack_start (GTK_BOX (page), open, FALSE, FALSE, 4);
    g_settings_bind (nemo_preferences, "dakkar-transfer-show-window-on-start",
                     show_on_start, "active", G_SETTINGS_BIND_DEFAULT);
    g_settings_bind (nemo_preferences, "dakkar-transfer-completion-notifications",
                     notify_on_success, "active", G_SETTINGS_BIND_DEFAULT);
    g_settings_bind (nemo_preferences, "dakkar-transfer-expanded",
                     speed_graph, "active", G_SETTINGS_BIND_DEFAULT);
    g_settings_bind (nemo_preferences, "dakkar-transfer-verify-after-copy",
                     verify, "active", G_SETTINGS_BIND_DEFAULT);
    g_settings_bind (nemo_preferences, "dakkar-transfer-retain-history",
                     history, "active", G_SETTINGS_BIND_DEFAULT);
    g_signal_connect (open, "clicked", G_CALLBACK (dakkar_open_transfer_history), NULL);
    gtk_widget_show_all (page);
    gtk_stack_add_titled (stack, page, "file-transfers", _("File Transfers"));
}

/* These two tools are optional; Group By remains permanently present in
 * the main toolbar and cannot be hidden via these settings. */
/* ---- Preferences > Toolbar: one reorderable list per toolbar group ----
 * Each row: drag handle, the button's icon, its name and a switch bound to
 * the button's visibility key.  The order lives in toolbar-button-order;
 * the toolbar listens to the same keys, so everything applies live. */

typedef struct {
    const gchar *id;
    const gchar *key;      /* NULL: always shown */
    const gchar *label;
    const gchar *icon;
    gboolean     left;
} DakkarToolDef;

static const DakkarToolDef dakkar_tool_defs[] = {
    { "previous",    NEMO_PREFERENCES_SHOW_PREVIOUS_ICON_TOOLBAR, N_("Back"),             "go-previous-symbolic",        TRUE },
    { "next",        NEMO_PREFERENCES_SHOW_NEXT_ICON_TOOLBAR,     N_("Forward"),          "go-next-symbolic",            TRUE },
    { "up",          NEMO_PREFERENCES_SHOW_UP_ICON_TOOLBAR,       N_("Parent folder"),    "go-up-symbolic",              TRUE },
    { "refresh",     NEMO_PREFERENCES_SHOW_RELOAD_ICON_TOOLBAR,   N_("Reload"),           "view-refresh-symbolic",       TRUE },
    { "home",        NEMO_PREFERENCES_SHOW_HOME_ICON_TOOLBAR,     N_("Home"),             "go-home-symbolic",            TRUE },
    { "computer",    NEMO_PREFERENCES_SHOW_COMPUTER_ICON_TOOLBAR, N_("Computer"),         "computer-symbolic",           TRUE },
    { "location",    NEMO_PREFERENCES_SHOW_EDIT_ICON_TOOLBAR,     N_("Location entry toggle"), "location-symbolic",      FALSE },
    { "terminal",    NEMO_PREFERENCES_SHOW_OPEN_IN_TERMINAL_TOOLBAR, N_("Open in Terminal"), "utilities-terminal-symbolic", FALSE },
    { "new-folder",  NEMO_PREFERENCES_SHOW_NEW_FOLDER_ICON_TOOLBAR, N_("New Folder"),     "folder-new-symbolic",         FALSE },
    { "search",      NEMO_PREFERENCES_SHOW_SEARCH_ICON_TOOLBAR,   N_("Search"),           "edit-find-symbolic",          FALSE },
    { "thumbnails",  NEMO_PREFERENCES_SHOW_SHOW_THUMBNAILS_TOOLBAR, N_("Show Thumbnails"), "xapp-prefs-preview-symbolic", FALSE },
    { "transfers",   "dakkar-show-transfers-tool",                 N_("File Transfers"),   "folder-download-symbolic",    FALSE },
    { "new-tab",     "dakkar-show-new-tab-tool",                   N_("New Tab"),          "tab-new-symbolic",            FALSE },
    { "places-tree", "dakkar-show-places-tree-tool",               N_("Places / Tree switch"), "view-list-tree-symbolic", FALSE },
    { "view",        NULL,                                        N_("View mode"),        "view-grid-symbolic",          FALSE },
    { "sort",        NULL,                                        N_("Sort"),             "view-sort-ascending-symbolic", FALSE },
    { "group",       NULL,                                        N_("Group By"),         "view-group-symbolic",         FALSE },
    { "extra-pane",  "dakkar-show-extra-pane-tool",                N_("Extra Pane (F3)"),  "view-dual-symbolic",          FALSE },
    { "settings",    NULL,                                        N_("Menu"),             "emblem-system-symbolic",      FALSE },
};

static const DakkarToolDef *
dakkar_tool_def (const gchar *id)
{
    guint i;
    for (i = 0; id != NULL && i < G_N_ELEMENTS (dakkar_tool_defs); i++)
        if (g_strcmp0 (dakkar_tool_defs[i].id, id) == 0)
            return &dakkar_tool_defs[i];
    return NULL;
}

/* Effective order of all buttons, exactly as nemo-toolbar.c applies it:
 * stored ids first (unknown ids and duplicates dropped), an id missing from
 * the stored order inserted right after its nearest default predecessor,
 * Settings always last.  Returns borrowed id strings. */
static GPtrArray *
dakkar_tool_effective_order (void)
{
    gchar **order = g_settings_get_strv (nemo_preferences, DAKKAR_TOOLBAR_BUTTON_ORDER);
    GPtrArray *all = g_ptr_array_new ();
    gint pass;
    for (pass = 0; pass < 2; pass++) {
        gboolean left = pass == 0;
        GPtrArray *group = g_ptr_array_new ();
        guint i, k;
        for (i = 0; order != NULL && order[i] != NULL; i++) {
            const DakkarToolDef *d = dakkar_tool_def (order[i]);
            if (d != NULL && d->left == left && !g_ptr_array_find (group, d->id, NULL))
                g_ptr_array_add (group, (gpointer) d->id);
        }
        for (i = 0; i < G_N_ELEMENTS (dakkar_tool_defs); i++) {
            const DakkarToolDef *d = &dakkar_tool_defs[i];
            guint at = 0;
            gint p;
            if (d->left != left || g_ptr_array_find (group, d->id, NULL)) continue;
            for (p = (gint) i - 1; p >= 0; p--) {
                if (dakkar_tool_defs[p].left == left &&
                    g_ptr_array_find (group, dakkar_tool_defs[p].id, &k)) {
                    at = k + 1;
                    break;
                }
            }
            g_ptr_array_insert (group, (gint) at, (gpointer) d->id);
        }
        for (i = 0; i < group->len; i++)
            if (g_strcmp0 (g_ptr_array_index (group, i), "settings") != 0)
                g_ptr_array_add (all, g_ptr_array_index (group, i));
        if (!left)
            g_ptr_array_add (all, (gpointer) "settings");
        g_ptr_array_free (group, TRUE);
    }
    g_strfreev (order);
    return all;
}

static gint
dakkar_tool_rank_in (GPtrArray *effective, const gchar *id)
{
    guint i;
    for (i = 0; i < effective->len; i++)
        if (g_strcmp0 (g_ptr_array_index (effective, i), id) == 0)
            return (gint) i;
    return 2000;
}

static gint
dakkar_tool_row_sort (GtkListBoxRow *a, GtkListBoxRow *b, gpointer user_data)
{
    GPtrArray *effective = dakkar_tool_effective_order ();
    gint r = dakkar_tool_rank_in (effective, g_object_get_data (G_OBJECT (a), "dakkar-tool-id")) -
             dakkar_tool_rank_in (effective, g_object_get_data (G_OBJECT (b), "dakkar-tool-id"));
    g_ptr_array_free (effective, TRUE);
    (void) user_data;
    return r;
}

/* Effective order of one group, as a list of ids (borrowed strings). */
static GList *
dakkar_tool_group_order (gboolean left)
{
    GPtrArray *effective = dakkar_tool_effective_order ();
    GList *ids = NULL;
    guint i;
    for (i = 0; i < effective->len; i++) {
        const DakkarToolDef *d = dakkar_tool_def (g_ptr_array_index (effective, i));
        if (d != NULL && d->left == left)
            ids = g_list_append (ids, (gpointer) d->id);
    }
    g_ptr_array_free (effective, TRUE);
    return ids;
}

/* Move @id inside its own group to index @to (clamped) and store the
 * complete order (left group first, then right group). */
static void
dakkar_tool_move (const gchar *id, gint to)
{
    const DakkarToolDef *def = dakkar_tool_def (id);
    GList *left, *right, *group, *l;
    GPtrArray *out;
    if (def == NULL || g_strcmp0 (id, "settings") == 0)
        return;
    left = dakkar_tool_group_order (TRUE);
    right = dakkar_tool_group_order (FALSE);
    group = def->left ? left : right;
    for (l = group; l != NULL; l = l->next)
        if (g_strcmp0 (l->data, id) == 0)
            break;
    if (l == NULL) {
        g_list_free (left);
        g_list_free (right);
        return;
    }
    group = g_list_delete_link (group, l);
    /* never past the Settings button, which stays last */
    to = CLAMP (to, 0, (gint) g_list_length (group) -
                       (g_list_find_custom (group, "settings", (GCompareFunc) g_strcmp0) ? 1 : 0));
    group = g_list_insert (group, (gpointer) def->id, to);
    if (def->left) left = group; else right = group;
    out = g_ptr_array_new ();
    for (l = left; l != NULL; l = l->next) g_ptr_array_add (out, l->data);
    for (l = right; l != NULL; l = l->next) g_ptr_array_add (out, l->data);
    g_ptr_array_add (out, NULL);
    g_settings_set_strv (nemo_preferences, DAKKAR_TOOLBAR_BUTTON_ORDER,
                         (const gchar * const *) out->pdata);
    g_ptr_array_free (out, TRUE);
    g_list_free (left);
    g_list_free (right);
}

static const GtkTargetEntry dakkar_tool_row_targets[] = {
    { (gchar *) "DAKKAR_TOOLBAR_ROW", GTK_TARGET_SAME_APP, 0 }
};

static void
dakkar_tool_row_drag_begin (GtkWidget *handle, GdkDragContext *context, gpointer user_data)
{
    GtkWidget *row = gtk_widget_get_ancestor (handle, GTK_TYPE_LIST_BOX_ROW);
    GtkAllocation alloc;
    cairo_surface_t *surface;
    cairo_t *cr;
    gint x = 0, y = 0;
    if (row == NULL)
        return;
    gtk_widget_get_allocation (row, &alloc);
    surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, alloc.width, alloc.height);
    cr = cairo_create (surface);
    gtk_style_context_add_class (gtk_widget_get_style_context (row), "dakkar-drag-icon");
    gtk_widget_draw (row, cr);
    gtk_style_context_remove_class (gtk_widget_get_style_context (row), "dakkar-drag-icon");
    cairo_destroy (cr);
    gtk_widget_translate_coordinates (handle, row, 0, 0, &x, &y);
    cairo_surface_set_device_offset (surface, -x - 8, -y - 8);
    gtk_drag_set_icon_surface (context, surface);
    cairo_surface_destroy (surface);
    (void) user_data;
}

static void
dakkar_tool_row_drag_data_get (GtkWidget *handle, GdkDragContext *context,
                              GtkSelectionData *data, guint info, guint time_,
                              gpointer user_data)
{
    GtkWidget *row = gtk_widget_get_ancestor (handle, GTK_TYPE_LIST_BOX_ROW);
    gtk_selection_data_set (data, gdk_atom_intern_static_string ("DAKKAR_TOOLBAR_ROW"),
                            8, (const guchar *) &row, sizeof (gpointer));
    (void) context; (void) info; (void) time_; (void) user_data;
}

static void
dakkar_tool_row_drag_data_received (GtkWidget *target, GdkDragContext *context,
                                   gint x, gint y, GtkSelectionData *data,
                                   guint info, guint time_, gpointer user_data)
{
    GtkWidget *source;
    gint index;
    if (gtk_selection_data_get_length (data) != sizeof (gpointer)) {
        gtk_drag_finish (context, FALSE, FALSE, time_);
        return;
    }
    source = *(GtkWidget * const *) gtk_selection_data_get_data (data);
    if (!GTK_IS_LIST_BOX_ROW (source) || source == target ||
        gtk_widget_get_parent (source) != gtk_widget_get_parent (target)) {
        /* Only reorder inside the same group. */
        gtk_drag_finish (context, FALSE, FALSE, time_);
        return;
    }
    index = gtk_list_box_row_get_index (GTK_LIST_BOX_ROW (target));
    if (y > gtk_widget_get_allocated_height (target) / 2)
        index++;
    if (gtk_list_box_row_get_index (GTK_LIST_BOX_ROW (source)) < index)
        index--;
    dakkar_tool_move (g_object_get_data (G_OBJECT (source), "dakkar-tool-id"), index);
    gtk_drag_finish (context, TRUE, FALSE, time_);
    (void) x; (void) info; (void) user_data;
}

static gboolean
dakkar_tool_row_key_press (GtkWidget *row, GdkEventKey *event, gpointer user_data)
{
    gint index;
    if (!(event->state & GDK_MOD1_MASK))
        return FALSE;
    if (event->keyval != GDK_KEY_Up && event->keyval != GDK_KEY_Down &&
        event->keyval != GDK_KEY_KP_Up && event->keyval != GDK_KEY_KP_Down)
        return FALSE;
    index = gtk_list_box_row_get_index (GTK_LIST_BOX_ROW (row));
    index += (event->keyval == GDK_KEY_Up || event->keyval == GDK_KEY_KP_Up) ? -1 : 1;
    if (index < 0)
        return TRUE;
    dakkar_tool_move (g_object_get_data (G_OBJECT (row), "dakkar-tool-id"), index);
    gtk_widget_grab_focus (row);
    (void) user_data;
    return TRUE;
}

static void
dakkar_tool_row_sync_state (GObject *sw, GParamSpec *pspec, GtkWidget *content)
{
    gboolean on = gtk_switch_get_active (GTK_SWITCH (sw));
    GtkStyleContext *ctx = gtk_widget_get_style_context (content);
    gtk_widget_set_opacity (content, on ? 1.0 : 0.45);
    if (on) gtk_style_context_remove_class (ctx, "dakkar-row-off");
    else    gtk_style_context_add_class (ctx, "dakkar-row-off");
    (void) pspec;
}

static GtkWidget *
dakkar_tool_row_new (const DakkarToolDef *def)
{
    GtkWidget *row = gtk_list_box_row_new ();
    GtkWidget *hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *content = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *handle = gtk_event_box_new ();
    GtkWidget *grip, *icon, *label, *state;

    g_object_set_data (G_OBJECT (row), "dakkar-tool-id", (gpointer) def->id);
    gtk_style_context_add_class (gtk_widget_get_style_context (row), "dakkar-toolbar-row");
    gtk_container_set_border_width (GTK_CONTAINER (hbox), 4);

    grip = gtk_image_new_from_icon_name (
        dakkar_icon_pick ("list-drag-handle-symbolic", "drag-handle-symbolic",
                         "open-menu-symbolic", NULL), GTK_ICON_SIZE_MENU);
    gtk_style_context_add_class (gtk_widget_get_style_context (grip), "dim-label");
    gtk_container_add (GTK_CONTAINER (handle), grip);
    if (g_strcmp0 (def->id, "settings") == 0) {
        /* fixed: always the last button */
        gtk_widget_set_opacity (grip, 0.0);
    } else {
    gtk_widget_set_tooltip_text (handle, _("Drag to reorder (or Alt+Up / Alt+Down)"));
    gtk_drag_source_set (handle, GDK_BUTTON1_MASK, dakkar_tool_row_targets, 1, GDK_ACTION_MOVE);
    }
    g_signal_connect (handle, "drag-begin", G_CALLBACK (dakkar_tool_row_drag_begin), NULL);
    g_signal_connect (handle, "drag-data-get", G_CALLBACK (dakkar_tool_row_drag_data_get), NULL);
    gtk_drag_dest_set (row, GTK_DEST_DEFAULT_ALL, dakkar_tool_row_targets, 1, GDK_ACTION_MOVE);
    g_signal_connect (row, "drag-data-received", G_CALLBACK (dakkar_tool_row_drag_data_received), NULL);
    g_signal_connect (row, "key-press-event", G_CALLBACK (dakkar_tool_row_key_press), NULL);

    icon = gtk_image_new_from_icon_name (dakkar_icon_resolve (def->icon), GTK_ICON_SIZE_BUTTON);
    label = gtk_label_new (_(def->label));
    gtk_widget_set_halign (label, GTK_ALIGN_START);
    gtk_box_pack_start (GTK_BOX (content), icon, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (content), label, TRUE, TRUE, 0);

    gtk_box_pack_start (GTK_BOX (hbox), handle, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (hbox), content, TRUE, TRUE, 0);

    if (def->key != NULL) {
        state = gtk_switch_new ();
        gtk_widget_set_valign (state, GTK_ALIGN_CENTER);
        gtk_widget_set_tooltip_text (state, _("Show this button in the toolbar"));
        g_settings_bind (nemo_preferences, def->key, state, "active", G_SETTINGS_BIND_DEFAULT);
        g_signal_connect (state, "notify::active", G_CALLBACK (dakkar_tool_row_sync_state), content);
        dakkar_tool_row_sync_state (G_OBJECT (state), NULL, content);
        atk_object_set_name (gtk_widget_get_accessible (state), _(def->label));
    } else {
        state = gtk_label_new (g_strcmp0 (def->id, "settings") == 0 ?
                               _("Always shown, always last") : _("Always shown"));
        gtk_style_context_add_class (gtk_widget_get_style_context (state), "dim-label");
    }
    gtk_box_pack_end (GTK_BOX (hbox), state, FALSE, FALSE, 0);
    gtk_container_add (GTK_CONTAINER (row), hbox);
    gtk_widget_show_all (row);
    return row;
}

static GtkWidget *
dakkar_tool_group_new (const gchar *title, gboolean left)
{
    GtkWidget *vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 4);
    GtkWidget *label = gtk_label_new (title);
    GtkWidget *frame = gtk_frame_new (NULL);
    GtkWidget *list = gtk_list_box_new ();
    guint i;

    gtk_widget_set_halign (label, GTK_ALIGN_START);
    gtk_style_context_add_class (gtk_widget_get_style_context (label), "dim-label");
    gtk_list_box_set_selection_mode (GTK_LIST_BOX (list), GTK_SELECTION_NONE);
    gtk_style_context_add_class (gtk_widget_get_style_context (list), "dakkar-toolbar-order");
    gtk_style_context_add_class (gtk_widget_get_style_context (list), "frame");
    for (i = 0; i < G_N_ELEMENTS (dakkar_tool_defs); i++)
        if (dakkar_tool_defs[i].left == left)
            gtk_container_add (GTK_CONTAINER (list), dakkar_tool_row_new (&dakkar_tool_defs[i]));
    gtk_list_box_set_sort_func (GTK_LIST_BOX (list), dakkar_tool_row_sort, NULL, NULL);
    g_signal_connect_object (nemo_preferences, "changed::" DAKKAR_TOOLBAR_BUTTON_ORDER,
                             G_CALLBACK (gtk_list_box_invalidate_sort), list, G_CONNECT_SWAPPED);
    gtk_container_add (GTK_CONTAINER (frame), list);
    gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_NONE);
    gtk_box_pack_start (GTK_BOX (vbox), label, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (vbox), frame, FALSE, FALSE, 0);
    return vbox;
}

static void
dakkar_tool_reset_order (GtkButton *button, gpointer user_data)
{
    g_settings_reset (nemo_preferences, DAKKAR_TOOLBAR_BUTTON_ORDER);
    (void) button; (void) user_data;
}

static void
dakkar_toolbar_optional_tools_setup (GtkBuilder *builder)
{
    GtkWidget *box = GTK_WIDGET (gtk_builder_get_object (builder, "vbox38"));
    GtkWidget *heading, *hint, *columns, *reset;
    GList *children, *l;
    if (!GTK_IS_BOX (box)) return;

    /* The glade grid of toggle buttons stays (its bindings are harmless)
     * but is replaced visually by the lists below: a pressed/unpressed
     * toggle button did not show clearly whether a button was on. */
    children = gtk_container_get_children (GTK_CONTAINER (box));
    for (l = children; l != NULL; l = l->next) {
        gtk_widget_set_no_show_all (l->data, TRUE);
        gtk_widget_hide (l->data);
    }
    g_list_free (children);

    heading = gtk_label_new (_("Toolbar buttons"));
    gtk_widget_set_halign (heading, GTK_ALIGN_START);
    gtk_style_context_add_class (gtk_widget_get_style_context (heading), "dakkar-preview-heading");
    hint = gtk_label_new (_("Use the switches to show or hide a button. Drag a row by its handle to change the order; it applies to the toolbar right away."));
    gtk_label_set_line_wrap (GTK_LABEL (hint), TRUE);
    gtk_label_set_xalign (GTK_LABEL (hint), 0.0);
    gtk_label_set_max_width_chars (GTK_LABEL (hint), 70);
    gtk_style_context_add_class (gtk_widget_get_style_context (hint), "dim-label");

    columns = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 18);
    gtk_box_set_homogeneous (GTK_BOX (columns), TRUE);
    gtk_box_pack_start (GTK_BOX (columns),
                        dakkar_tool_group_new (_("Left of the location bar"), TRUE), TRUE, TRUE, 0);
    gtk_box_pack_start (GTK_BOX (columns),
                        dakkar_tool_group_new (_("Right of the location bar"), FALSE), TRUE, TRUE, 0);

    reset = gtk_button_new_with_label (_("Reset order"));
    gtk_widget_set_halign (reset, GTK_ALIGN_START);
    g_signal_connect (reset, "clicked", G_CALLBACK (dakkar_tool_reset_order), NULL);

    gtk_box_pack_start (GTK_BOX (box), heading, FALSE, FALSE, 2);
    gtk_box_pack_start (GTK_BOX (box), hint, FALSE, FALSE, 2);
    gtk_box_pack_start (GTK_BOX (box), columns, FALSE, FALSE, 6);
    gtk_box_pack_start (GTK_BOX (box), reset, FALSE, FALSE, 2);
    gtk_widget_show_all (heading);
    gtk_widget_show_all (hint);
    gtk_widget_show_all (columns);
    gtk_widget_show_all (reset);
}

static  void
nemo_file_management_properties_dialog_setup (GtkBuilder  *builder,
                                              GtkWindow   *window,
                                              const gchar *initial_page)
{
	GtkWidget *dialog;

	/* setup UI */
	nemo_file_management_properties_size_group_create (builder,
							       (char *)"views_label",
							       5);
	nemo_file_management_properties_size_group_create (builder,
							       (char *)"captions_label",
							       3);
	nemo_file_management_properties_size_group_create (builder,
							       (char *)"preview_label",
							       3);
	create_date_format_menu (builder);


	/* nemo patch */
	bind_builder_bool (builder, nemo_preferences,
			   NEMO_FILE_MANAGEMENT_PROPERTIES_SHOW_PREVIOUS_ICON_TOOLBAR_WIDGET,
			   NEMO_PREFERENCES_SHOW_PREVIOUS_ICON_TOOLBAR);
	bind_builder_bool (builder, nemo_preferences,
			   NEMO_FILE_MANAGEMENT_PROPERTIES_SHOW_NEXT_ICON_TOOLBAR_WIDGET,
			   NEMO_PREFERENCES_SHOW_NEXT_ICON_TOOLBAR);
	bind_builder_bool (builder, nemo_preferences,
			   NEMO_FILE_MANAGEMENT_PROPERTIES_SHOW_UP_ICON_TOOLBAR_WIDGET,
			   NEMO_PREFERENCES_SHOW_UP_ICON_TOOLBAR);
	bind_builder_bool (builder, nemo_preferences,
			   NEMO_FILE_MANAGEMENT_PROPERTIES_SHOW_RELOAD_ICON_TOOLBAR_WIDGET,
			   NEMO_PREFERENCES_SHOW_RELOAD_ICON_TOOLBAR);
	bind_builder_bool (builder, nemo_preferences,
			   NEMO_FILE_MANAGEMENT_PROPERTIES_SHOW_EDIT_ICON_TOOLBAR_WIDGET,
			   NEMO_PREFERENCES_SHOW_EDIT_ICON_TOOLBAR);
	bind_builder_bool (builder, nemo_preferences,
			   NEMO_FILE_MANAGEMENT_PROPERTIES_SHOW_HOME_ICON_TOOLBAR_WIDGET,
			   NEMO_PREFERENCES_SHOW_HOME_ICON_TOOLBAR);
	bind_builder_bool (builder, nemo_preferences,
			   NEMO_FILE_MANAGEMENT_PROPERTIES_SHOW_COMPUTER_ICON_TOOLBAR_WIDGET,
			   NEMO_PREFERENCES_SHOW_COMPUTER_ICON_TOOLBAR);
	bind_builder_bool (builder, nemo_preferences,
			   NEMO_FILE_MANAGEMENT_PROPERTIES_SHOW_SEARCH_ICON_TOOLBAR_WIDGET,
			   NEMO_PREFERENCES_SHOW_SEARCH_ICON_TOOLBAR);
    bind_builder_bool (builder, nemo_preferences,
        NEMO_FILE_MANAGEMENT_PROPERTIES_SHOW_NEW_FOLDER_ICON_TOOLBAR_WIDGET,
        NEMO_PREFERENCES_SHOW_NEW_FOLDER_ICON_TOOLBAR);
    bind_builder_bool (builder, nemo_preferences,
        NEMO_FILE_MANAGEMENT_PROPERTIES_SHOW_OPEN_IN_TERMINAL_ICON_TOOLBAR_WIDGET,
        NEMO_PREFERENCES_SHOW_OPEN_IN_TERMINAL_TOOLBAR);
    bind_builder_bool (builder, nemo_preferences,
			   NEMO_FILE_MANAGEMENT_PROPERTIES_SHOW_ICON_VIEW_ICON_TOOLBAR_WIDGET,
			   NEMO_PREFERENCES_SHOW_ICON_VIEW_ICON_TOOLBAR);
    bind_builder_bool (builder, nemo_preferences,
			   NEMO_FILE_MANAGEMENT_PROPERTIES_SHOW_LIST_VIEW_ICON_TOOLBAR_WIDGET,
			   NEMO_PREFERENCES_SHOW_LIST_VIEW_ICON_TOOLBAR);
    bind_builder_bool (builder, nemo_preferences,
			   NEMO_FILE_MANAGEMENT_PROPERTIES_SHOW_COMPACT_VIEW_ICON_TOOLBAR_WIDGET,
			   NEMO_PREFERENCES_SHOW_COMPACT_VIEW_ICON_TOOLBAR);
    bind_builder_bool (builder, nemo_preferences,
			   NEMO_FILE_MANAGEMENT_PROPERTIES_SHOW_SHOW_THUMBNAILS_ICON_TOOLBAR_WIDGET,
			   NEMO_PREFERENCES_SHOW_SHOW_THUMBNAILS_TOOLBAR);

	/* setup preferences */
	bind_builder_bool (builder, nemo_icon_view_preferences,
			   NEMO_FILE_MANAGEMENT_PROPERTIES_LABELS_BESIDE_ICONS_WIDGET,
			   NEMO_PREFERENCES_ICON_VIEW_LABELS_BESIDE_ICONS);
	bind_builder_bool (builder, nemo_compact_view_preferences,
			   NEMO_FILE_MANAGEMENT_PROPERTIES_ALL_COLUMNS_SAME_WIDTH,
			   NEMO_PREFERENCES_COMPACT_VIEW_ALL_COLUMNS_SAME_WIDTH);
	bind_builder_bool (builder, nemo_preferences,
			   NEMO_FILE_MANAGEMENT_PROPERTIES_FOLDERS_FIRST_WIDGET,
			   NEMO_PREFERENCES_SORT_DIRECTORIES_FIRST);
	g_signal_connect (gtk_builder_get_object (builder, NEMO_FILE_MANAGEMENT_PROPERTIES_FOLDERS_FIRST_WIDGET),
                          "notify::active",
                          G_CALLBACK (set_gtk_filechooser_sort_first), NULL);
	bind_builder_bool(builder, nemo_preferences,
			    NEMO_FILE_MANAGEMENT_QUICK_RENAMES_WITH_PAUSE_IN_BETWEEN,
			    NEMO_PREFERENCES_CLICK_TO_RENAME);
	bind_builder_bool_inverted (builder, nemo_preferences,
				    NEMO_FILE_MANAGEMENT_PROPERTIES_ALWAYS_USE_BROWSER_WIDGET,
				    NEMO_PREFERENCES_ALWAYS_USE_BROWSER);
	bind_builder_bool (builder, nemo_preferences,
			   NEMO_FILE_MANAGEMENT_PROPERTIES_TRASH_CONFIRM_MOVE_WIDGET,
			   NEMO_PREFERENCES_CONFIRM_MOVE_TO_TRASH);
	bind_builder_bool (builder, nemo_preferences,
			   NEMO_FILE_MANAGEMENT_PROPERTIES_TRASH_CONFIRM_WIDGET,
			   NEMO_PREFERENCES_CONFIRM_TRASH);
	bind_builder_bool (builder, nemo_preferences,
			   NEMO_FILE_MANAGEMENT_PROPERTIES_TRASH_DELETE_WIDGET,
			   NEMO_PREFERENCES_ENABLE_DELETE);
    bind_builder_bool (builder, nemo_preferences,
               NEMO_FILE_MANAGEMENT_PROPERTIES_SWAP_TRASH_DELETE,
               NEMO_PREFERENCES_SWAP_TRASH_DELETE);
	bind_builder_bool (builder, nemo_preferences,
			   NEMO_FILE_MANAGEMENT_PROPERTIES_SHOW_FULL_PATH_IN_TITLE_BARS_WIDGET,
			   NEMO_PREFERENCES_SHOW_FULL_PATH_TITLES);
	bind_builder_bool (builder, nemo_tree_sidebar_preferences,
			   NEMO_FILE_MANAGEMENT_PROPERTIES_TREE_VIEW_FOLDERS_WIDGET,
			   NEMO_PREFERENCES_TREE_SHOW_ONLY_DIRECTORIES);
  bind_builder_bool (builder, nemo_preferences,
			   NEMO_FILE_MANAGEMENT_PROPERTIES_INHERIT_VIEW_WIDGET,
			   NEMO_PREFERENCES_INHERIT_FOLDER_VIEWER);
  bind_builder_bool (builder, nemo_preferences,
               NEMO_FILE_MANAGEMENT_PROPERTIES_REVERSE_SORT_WIDGET,
               NEMO_PREFERENCES_DEFAULT_SORT_IN_REVERSE_ORDER);
  bind_builder_bool (builder, nemo_preferences,
               NEMO_FILE_MANAGEMENT_PROPERTIES_FAVORITES_FIRST_WIDGET,
               NEMO_PREFERENCES_SORT_FAVORITES_FIRST);
	bind_builder_enum (builder, nemo_preferences,
			   NEMO_FILE_MANAGEMENT_PROPERTIES_DEFAULT_VIEW_WIDGET,
			   NEMO_PREFERENCES_DEFAULT_FOLDER_VIEWER,
			   (const char **) default_view_values);
	/* Dakkar: the default view is the one global remembered view; picking it
	 * here also switches the open windows. Toolbar/menu switches update the
	 * same key, so the combo follows them through the binding above. The
	 * per-folder "inherit" option no longer applies. */
	g_signal_connect_after (dakkar_preference_object (builder,
				NEMO_FILE_MANAGEMENT_PROPERTIES_DEFAULT_VIEW_WIDGET),
				"changed", G_CALLBACK (dakkar_default_view_combo_changed),
				(gpointer) NEMO_PREFERENCES_DEFAULT_FOLDER_VIEWER);
	/* the extra pane of the split view has its own remembered view */
	bind_builder_enum (builder, nemo_preferences,
			   NEMO_FILE_MANAGEMENT_PROPERTIES_SPLIT_VIEW_DEFAULT_VIEW_WIDGET,
			   NEMO_PREFERENCES_SPLIT_VIEW_DEFAULT_VIEW,
			   (const char **) default_view_values);
	g_signal_connect_after (dakkar_preference_object (builder,
				NEMO_FILE_MANAGEMENT_PROPERTIES_SPLIT_VIEW_DEFAULT_VIEW_WIDGET),
				"changed", G_CALLBACK (dakkar_default_view_combo_changed),
				(gpointer) NEMO_PREFERENCES_SPLIT_VIEW_DEFAULT_VIEW);
	{
		GtkWidget *inherit = GTK_WIDGET (dakkar_preference_object (builder,
				NEMO_FILE_MANAGEMENT_PROPERTIES_INHERIT_VIEW_WIDGET));
		gtk_widget_set_no_show_all (inherit, TRUE);
		gtk_widget_hide (inherit);
	}
	bind_builder_enum (builder, nemo_icon_view_preferences,
			   NEMO_FILE_MANAGEMENT_PROPERTIES_ICON_VIEW_ZOOM_WIDGET,
			   NEMO_PREFERENCES_ICON_VIEW_DEFAULT_ZOOM_LEVEL,
			   (const char **) zoom_values);
	bind_builder_enum (builder, nemo_compact_view_preferences,
			   NEMO_FILE_MANAGEMENT_PROPERTIES_COMPACT_VIEW_ZOOM_WIDGET,
			   NEMO_PREFERENCES_COMPACT_VIEW_DEFAULT_ZOOM_LEVEL,
			   (const char **) zoom_values);
	bind_builder_enum (builder, nemo_list_view_preferences,
			   NEMO_FILE_MANAGEMENT_PROPERTIES_LIST_VIEW_ZOOM_WIDGET,
			   NEMO_PREFERENCES_LIST_VIEW_DEFAULT_ZOOM_LEVEL,
			   (const char **) zoom_values);
	bind_builder_enum (builder, nemo_preferences,
			   NEMO_FILE_MANAGEMENT_PROPERTIES_SORT_ORDER_WIDGET,
			   NEMO_PREFERENCES_DEFAULT_SORT_ORDER,
			   (const char **) sort_order_values);
	bind_builder_enum (builder, nemo_preferences,
			   NEMO_FILE_MANAGEMENT_PROPERTIES_PREVIEW_IMAGE_WIDGET,
			   NEMO_PREFERENCES_SHOW_IMAGE_FILE_THUMBNAILS,
			   (const char **) preview_image_values);
    bind_builder_bool (builder, nemo_preferences,
               NEMO_FILE_MANAGEMENT_PROPERTIES_INHERIT_SHOW_THUMBNAILS_WIDGET,
               NEMO_PREFERENCES_INHERIT_SHOW_THUMBNAILS);
	bind_builder_enum (builder, nemo_preferences,
			   NEMO_FILE_MANAGEMENT_PROPERTIES_PREVIEW_FOLDER_WIDGET,
			   NEMO_PREFERENCES_SHOW_DIRECTORY_ITEM_COUNTS,
			   (const char **) preview_folder_values);
	bind_builder_enum (builder, nemo_preferences,
			   NEMO_FILE_MANAGEMENT_PROPERTIES_SIZE_PREFIXES_WIDGET,
			   NEMO_PREFERENCES_SIZE_PREFIXES,
			   (const char **) size_prefixes_values);
	bind_builder_enum (builder, nemo_preferences,
			   NEMO_FILE_MANAGEMENT_PROPERTIES_DATE_FORMAT_WIDGET,
			   NEMO_PREFERENCES_DATE_FORMAT,
			   (const char **) date_format_values);
    bind_builder_enum (builder, nemo_preferences,
               NEMO_FILE_MANAGEMENT_PROPERTIES_DATE_FONT_CHOICE_WIDGET,
               NEMO_PREFERENCES_DATE_FONT_CHOICE,
               (const char **) date_font_choice_values);
	bind_builder_radio (builder, nemo_preferences,
			    (const char **) click_behavior_components,
			    NEMO_PREFERENCES_CLICK_POLICY,
			    (const char **) click_behavior_values);
	bind_builder_radio (builder, nemo_preferences,
			    (const char **) executable_text_components,
			    NEMO_PREFERENCES_EXECUTABLE_TEXT_ACTIVATION,
			    (const char **) executable_text_values);

	bind_builder_uint_enum (builder, nemo_preferences,
				NEMO_FILE_MANAGEMENT_PROPERTIES_THUMBNAIL_LIMIT_WIDGET,
				NEMO_PREFERENCES_IMAGE_FILE_THUMBNAIL_LIMIT,
				thumbnail_limit_values,
				G_N_ELEMENTS (thumbnail_limit_values));

    bind_builder_bool (builder, gnome_media_handling_preferences,
               NEMO_FILE_MANAGEMENT_PROPERTIES_AUTOMOUNT_MEDIA_WIDGET,
               GNOME_DESKTOP_MEDIA_HANDLING_AUTOMOUNT);

    bind_builder_bool (builder, gnome_media_handling_preferences,
               NEMO_FILE_MANAGEMENT_PROPERTIES_AUTOOPEN_MEDIA_WIDGET,
               GNOME_DESKTOP_MEDIA_HANDLING_AUTOMOUNT_OPEN);

    bind_builder_bool_inverted (builder, gnome_media_handling_preferences,
               NEMO_FILE_MANAGEMENT_PROPERTIES_AUTORUN_MEDIA_WIDGET,
               GNOME_DESKTOP_MEDIA_HANDLING_AUTORUN);

    bind_builder_bool (builder, nemo_preferences,
               NEMO_FILE_MANAGEMENT_PROPERTIES_DETECT_CONTENT_MEDIA_WIDGET,
               NEMO_PREFERENCES_MEDIA_HANDLING_DETECT_CONTENT);

    bind_builder_bool (builder, nemo_preferences,
                       NEMO_FILE_MANAGEMENT_PROPERTIES_CLOSE_DEVICE_VIEW_ON_EJECT_WIDGET,
                       NEMO_PREFERENCES_CLOSE_DEVICE_VIEW_ON_EJECT);

    bind_builder_bool (builder, nemo_preferences,
                       NEMO_FILE_MANAGEMENT_PROPERTIES_SHOW_ADVANCED_PERMISSIONS_WIDGET,
                       NEMO_PREFERENCES_SHOW_ADVANCED_PERMISSIONS);

    bind_builder_string_entry (builder, nemo_preferences,
                         NEMO_FILE_MANAGEMENT_PROPERTIES_BULK_RENAME_WIDGET,
                         NEMO_PREFERENCES_BULK_RENAME_TOOL);

    bind_builder_bool (builder, nemo_preferences,
                       NEMO_FILE_MANAGEMENT_PROPERTIES_START_WITH_DUAL_PANE_WIDGET,
                       NEMO_PREFERENCES_START_WITH_DUAL_PANE);

    bind_builder_bool (builder, nemo_preferences,
                       NEMO_FILE_MANAGEMENT_PROPERTIES_IGNORE_VIEW_METADATA_WIDGET,
                       NEMO_PREFERENCES_IGNORE_VIEW_METADATA);

    bind_builder_bool (builder, nemo_preferences,
                       NEMO_FILE_MANAGEMENT_PROPERTIES_BOOKMARKS_IN_TO_MENUS_WIDGET,
                       NEMO_PREFERENCES_SHOW_BOOKMARKS_IN_TO_MENUS);

    bind_builder_bool (builder, nemo_preferences,
                       NEMO_FILE_MANAGEMENT_PROPERTIES_PLACES_IN_TO_MENUS_WIDGET,
                       NEMO_PREFERENCES_SHOW_PLACES_IN_TO_MENUS);

    bind_builder_bool (builder, nemo_preferences,
                       NEMO_FILE_MANAGEMENT_PROPERTIES_TOOLTIPS_ON_DESKTOP_WIDGET,
                       NEMO_PREFERENCES_TOOLTIPS_DESKTOP);

    bind_builder_bool (builder, nemo_preferences,
                       NEMO_FILE_MANAGEMENT_PROPERTIES_TOOLTIPS_ON_ICON_VIEW_WIDGET,
                       NEMO_PREFERENCES_TOOLTIPS_ICON_VIEW);

    bind_builder_bool (builder, nemo_preferences,
                       NEMO_FILE_MANAGEMENT_PROPERTIES_TOOLTIPS_ON_LIST_VIEW_WIDGET,
                       NEMO_PREFERENCES_TOOLTIPS_LIST_VIEW);

    bind_builder_bool (builder, nemo_preferences,
                       NEMO_FILE_MANAGEMENT_PROPERTIES_TOOLTIP_FILE_TYPE_WIDGET,
                       NEMO_PREFERENCES_TOOLTIP_FILE_TYPE);

    bind_builder_bool (builder, nemo_preferences,
                       NEMO_FILE_MANAGEMENT_PROPERTIES_TOOLTIP_MOD_DATE_WIDGET,
                       NEMO_PREFERENCES_TOOLTIP_MOD_DATE);
    
    bind_builder_bool (builder, nemo_preferences,
                       NEMO_FILE_MANAGEMENT_PROPERTIES_TOOLTIP_ACCESS_DATE_WIDGET,
                       NEMO_PREFERENCES_TOOLTIP_ACCESS_DATE);

    bind_builder_bool (builder, nemo_preferences,
                       NEMO_FILE_MANAGEMENT_PROPERTIES_TOOLTIP_CREATED_DATE_WIDGET,
                       NEMO_PREFERENCES_TOOLTIP_CREATED_DATE);

    bind_builder_bool (builder, nemo_preferences,
                       NEMO_FILE_MANAGEMENT_PROPERTIES_TOOLTIP_FULL_PATH_WIDGET,
                       NEMO_PREFERENCES_TOOLTIP_FULL_PATH);

    bind_builder_bool (builder, nemo_preferences,
                       NEMO_FILE_MANAGEMENT_PROPERTIES_NEMO_PREFERENCES_SKIP_FILE_OP_QUEUE_WIDGET,
                       NEMO_PREFERENCES_NEVER_QUEUE_FILE_OPS);

    bind_builder_bool (builder, nemo_preferences,
                       NEMO_FILE_MANAGEMENT_PROPERTIES_NEMO_PREFERENCES_CLICK_DBL_PARENT_FOLDER_WIDGET,
                       NEMO_PREFERENCES_CLICK_DOUBLE_PARENT_FOLDER);

    bind_builder_bool (builder, nemo_preferences,
                       NEMO_FILE_MANAGEMENT_PROPERTIES_NEMO_PREFERENCES_EXPAND_ROW_ON_DND_DWELL_WIDGET,
                       NEMO_PREFERENCES_EXPAND_ROW_ON_DND_DWELL);

    setup_tooltip_items (builder);
    connect_tooltip_items (builder);

    /* to make checkbox for quickrenames get disabled when single click is selected */ 
    setup_quick_renames(builder);
    connect_quick_renames(builder);

    g_signal_connect (gtk_builder_get_object (builder, NEMO_FILE_MANAGEMENT_PROPERTIES_DATE_FORMAT_WIDGET), "changed",
                      G_CALLBACK (on_date_format_combo_changed), builder);

    on_date_format_combo_changed (GTK_COMBO_BOX (gtk_builder_get_object (builder, NEMO_FILE_MANAGEMENT_PROPERTIES_DATE_FORMAT_WIDGET)),
                                  builder);

	dakkar_toolbar_optional_tools_setup (builder);
	nemo_file_management_properties_setup_startup_page (builder);
	nemo_file_management_properties_setup_appearance_page (builder);
	nemo_file_management_properties_setup_miller_page (builder);
    nemo_file_management_properties_setup_details_page (builder);
    nemo_file_management_properties_setup_performance_page (builder);
    nemo_file_management_properties_setup_search_page (builder);
    dakkar_preferences_setup_grouping (builder);
    dakkar_preferences_setup_transfers (builder);
    gtk_stack_add_titled (GTK_STACK (gtk_builder_get_object (builder, "page_stack")),
                          dakkar_favorites_preferences_page (),
                          "favorites", _("Favorites"));
	gtk_stack_add_titled (GTK_STACK (gtk_builder_get_object (builder, "page_stack")),
	                        nemo_quick_actions_preferences_page (nemo_preferences),
	                        "quick-actions", _("Quick Actions"));
	nemo_file_management_properties_dialog_setup_icon_caption_page (builder);
	nemo_file_management_properties_dialog_setup_list_column_page (builder);
    nemo_file_management_properties_dialog_setup_plugin_page (builder);
    /* Last, so it sits at the bottom of the sidebar. */
    dakkar_preferences_setup_about_page (builder);

    setup_configurable_menu_items (builder);

    /* sidebar icons for the pages, also the ones added in code; freedesktop
     * theme names, not bundled artwork */
    dakkar_preferences_setup_sidebar_icons (GTK_STACK (gtk_builder_get_object (builder, "page_stack")));
    /* a homogeneous stack is as wide as its widest page, which cuts off
     * the right edge (the colour swatches) on a narrow dialog */
    gtk_stack_set_hhomogeneous (GTK_STACK (gtk_builder_get_object (builder, "page_stack")), FALSE);

    dialog = GTK_WIDGET (gtk_builder_get_object (builder, "file_management_dialog"));
    dakkar_dialog_style_attach (dialog);

	g_signal_connect (dialog, "delete-event",
			  G_CALLBACK (gtk_widget_destroy), NULL);

    g_signal_connect (dialog, "destroy",
                      G_CALLBACK (on_dialog_destroy), builder);

    dakkar_preferences_setup_search (dialog, GTK_STACK (gtk_builder_get_object (builder, "page_stack")));
    dakkar_preferences_setup_readonly_warning (dialog, GTK_STACK (gtk_builder_get_object (builder, "page_stack")));

	gtk_window_set_icon_name (GTK_WINDOW (dialog), DAKKAR_APP_ICON);

	if (window) {
		gtk_window_set_transient_for (GTK_WINDOW (dialog), window);
	}

    preferences_dialog = dialog;
    g_object_set_data (G_OBJECT (dialog), "dakkar-preferences-page-stack",
                       gtk_builder_get_object (builder, "page_stack"));
	g_object_add_weak_pointer (G_OBJECT (dialog), (gpointer *) &preferences_dialog);

    if (initial_page != NULL) {
        GtkStack *stack;

        stack = GTK_STACK (gtk_builder_get_object (builder, "page_stack"));

        if (gtk_stack_get_child_by_name (stack, initial_page) != NULL)
            gtk_stack_set_visible_child_name (stack, initial_page);
    }

	gtk_widget_show (dialog);
}

void
nemo_file_management_properties_dialog_show (GtkWindow   *window,
                                             const gchar *initial_page)
{
	GtkBuilder *builder;

    if (preferences_dialog != NULL) {
        GtkStack *stack = g_object_get_data (G_OBJECT (preferences_dialog),
                                             "dakkar-preferences-page-stack");
        if (initial_page && GTK_IS_STACK (stack) &&
            gtk_stack_get_child_by_name (stack, initial_page))
            gtk_stack_set_visible_child_name (stack, initial_page);
        gtk_window_present (GTK_WINDOW (preferences_dialog));
        return;
    }

	builder = gtk_builder_new ();
    /* no gtk_builder_set_translation_domain() here.  The .glade
     * file already declares <interface domain="nemo">; GTK 3.24's parser
     * keeps a borrowed pointer to the builder's old domain, frees it when it
     * applies the interface domain and then restores the freed pointer
     * (heap-use-after-free). */
	gtk_builder_add_from_resource (builder,
				       "/org/dakkar/nemo-file-management-properties.glade",
				       NULL);

	nemo_file_management_properties_dialog_setup (builder, window, initial_page);
}
