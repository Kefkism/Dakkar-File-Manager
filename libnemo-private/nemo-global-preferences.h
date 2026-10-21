/* -*- Mode: C; indent-tabs-mode: f; c-basic-offset: 4; tab-width: 4 -*- */

/* nemo-global-preferences.h - Nemo specific preference keys and
                                   functions.

   Copyright (C) 1999, 2000, 2001 Eazel, Inc.

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public
   License along with this program; see the file COPYING.LIB.  If not,
   write to the Free Software Foundation, Inc., 51 Franklin Street - Suite 500,
   Boston, MA 02110-1335, USA.

   Authors: Ramiro Estrugo <ramiro@eazel.com>
*/

#ifndef NEMO_GLOBAL_PREFERENCES_H
#define NEMO_GLOBAL_PREFERENCES_H

#include <gio/gio.h>

G_BEGIN_DECLS

/* Trash options */
#define NEMO_PREFERENCES_CONFIRM_MOVE_TO_TRASH	"confirm-move-to-trash"
#define NEMO_PREFERENCES_CONFIRM_TRASH			"confirm-trash"
#define NEMO_PREFERENCES_ENABLE_DELETE			"enable-delete"
#define NEMO_PREFERENCES_SWAP_TRASH_DELETE      "swap-trash-delete"

/* Desktop options */
#define NEMO_PREFERENCES_DESKTOP_IS_HOME_DIR                "desktop-is-home-dir"

/* Display  */
#define NEMO_PREFERENCES_SHOW_HIDDEN_FILES			"show-hidden-files"
#define NEMO_PREFERENCES_SHOW_ADVANCED_PERMISSIONS		"show-advanced-permissions"
#define NEMO_PREFERENCES_DATE_FORMAT            "date-format"
#define NEMO_PREFERENCES_DATE_FONT_CHOICE  "date-font-choice"
#define NEMO_PREFERENCES_MONO_FONT_NAME "monospace-font-name"

/* Mouse */
#define NEMO_PREFERENCES_MOUSE_USE_EXTRA_BUTTONS		"mouse-use-extra-buttons"
#define NEMO_PREFERENCES_MOUSE_FORWARD_BUTTON		"mouse-forward-button"
#define NEMO_PREFERENCES_MOUSE_BACK_BUTTON			"mouse-back-button"

typedef enum
{
	NEMO_DATE_FORMAT_LOCALE,
	NEMO_DATE_FORMAT_ISO,
	NEMO_DATE_FORMAT_INFORMAL
} NemoDateFormat;

typedef enum
{
    NEMO_DATE_FONT_CHOICE_AUTO,
    NEMO_DATE_FONT_CHOICE_SYSTEM,
    NEMO_DATE_FONT_CHOICE_NONE
} NemoDateFontChoice;

typedef enum
{
	NEMO_NEW_TAB_POSITION_AFTER_CURRENT_TAB,
	NEMO_NEW_TAB_POSITION_END,
} NemoNewTabPosition;

/* Sidebar panels  */
#define NEMO_PREFERENCES_TREE_SHOW_ONLY_DIRECTORIES         "show-only-directories"

/* Single/Double click preference  */
#define NEMO_PREFERENCES_CLICK_POLICY			"click-policy"

/* Quick renames with two single clicks and pause in-between*/
#define NEMO_PREFERENCES_CLICK_TO_RENAME "quick-renames-with-pause-in-between"

/* Activating executable text files */
#define NEMO_PREFERENCES_EXECUTABLE_TEXT_ACTIVATION		"executable-text-activation"

/* Image viewers to pass nemo view sort order to */
#define NEMO_PREFERENCES_IMAGE_VIEWERS_WITH_EXTERNAL_SORT "image-viewers-with-external-sort"

/* Spatial or browser mode */
#define NEMO_PREFERENCES_ALWAYS_USE_BROWSER			"always-use-browser"
#define NEMO_PREFERENCES_NEW_TAB_POSITION			"tabs-open-position"

#define NEMO_PREFERENCES_SHOW_LOCATION_ENTRY		"show-location-entry"
#define NEMO_PREFERENCES_SHOW_PREVIOUS_ICON_TOOLBAR     "show-previous-icon-toolbar"
#define NEMO_PREFERENCES_SHOW_NEXT_ICON_TOOLBAR     "show-next-icon-toolbar"
#define NEMO_PREFERENCES_SHOW_UP_ICON_TOOLBAR		"show-up-icon-toolbar"
#define NEMO_PREFERENCES_SHOW_EDIT_ICON_TOOLBAR		"show-edit-icon-toolbar"
#define NEMO_PREFERENCES_SHOW_RELOAD_ICON_TOOLBAR		"show-reload-icon-toolbar"
#define NEMO_PREFERENCES_SHOW_HOME_ICON_TOOLBAR		"show-home-icon-toolbar"
#define NEMO_PREFERENCES_SHOW_COMPUTER_ICON_TOOLBAR		"show-computer-icon-toolbar"
#define NEMO_PREFERENCES_SHOW_SEARCH_ICON_TOOLBAR		"show-search-icon-toolbar"
#define NEMO_PREFERENCES_SHOW_NEW_FOLDER_ICON_TOOLBAR   "show-new-folder-icon-toolbar"
#define NEMO_PREFERENCES_SHOW_OPEN_IN_TERMINAL_TOOLBAR   "show-open-in-terminal-toolbar"
#define NEMO_PREFERENCES_SHOW_ICON_VIEW_ICON_TOOLBAR   "show-icon-view-icon-toolbar"
#define NEMO_PREFERENCES_SHOW_LIST_VIEW_ICON_TOOLBAR   "show-list-view-icon-toolbar"
#define NEMO_PREFERENCES_SHOW_COMPACT_VIEW_ICON_TOOLBAR   "show-compact-view-icon-toolbar"
#define NEMO_PREFERENCES_SHOW_ROOT_WARNING                "show-root-warning"
#define NEMO_PREFERENCES_SHOW_SHOW_THUMBNAILS_TOOLBAR     "show-show-thumbnails-toolbar"
#define DAKKAR_SIDEBAR_DISK_USAGE                          "dakkar-sidebar-disk-usage"
#define DAKKAR_TOOLBAR_BUTTON_ORDER                        "toolbar-button-order"
#define DAKKAR_SHOW_TRANSFERS_TOOL                         "dakkar-show-transfers-tool"
#define DAKKAR_SHOW_NEW_TAB_TOOL                           "dakkar-show-new-tab-tool"

/* Which views should be displayed for new windows */
#define NEMO_WINDOW_STATE_START_WITH_STATUS_BAR		"start-with-status-bar"
#define NEMO_WINDOW_STATE_START_WITH_SIDEBAR		"start-with-sidebar"
#define NEMO_WINDOW_STATE_START_WITH_TOOLBAR		"start-with-toolbar"
#define NEMO_WINDOW_STATE_START_WITH_MENU_BAR           "start-with-menu-bar"
#define NEMO_WINDOW_STATE_SIDE_PANE_VIEW                    "side-pane-view"
#define NEMO_WINDOW_STATE_GEOMETRY				"geometry"
#define NEMO_WINDOW_STATE_MAXIMIZED				"maximized"
#define NEMO_WINDOW_STATE_SIDEBAR_WIDTH			"sidebar-width"
#define NEMO_WINDOW_STATE_MY_COMPUTER_EXPANDED  "my-computer-expanded"
#define NEMO_WINDOW_STATE_BOOKMARKS_EXPANDED    "bookmarks-expanded"
#define NEMO_WINDOW_STATE_DEVICES_EXPANDED      "devices-expanded"
#define NEMO_WINDOW_STATE_NETWORK_EXPANDED      "network-expanded"

/* Sorting order */
#define NEMO_PREFERENCES_SORT_DIRECTORIES_FIRST		"sort-directories-first"
#define NEMO_PREFERENCES_SORT_FAVORITES_FIRST		"sort-favorites-first"
#define NEMO_PREFERENCES_DEFAULT_SORT_ORDER			"default-sort-order"
#define NEMO_PREFERENCES_DEFAULT_SORT_IN_REVERSE_ORDER	"default-sort-in-reverse-order"

/* The default folder viewer - one of the two enums below */
#define NEMO_PREFERENCES_DAKKAR_STARTUP_FOLDER "dakkar-startup-folder"
#define NEMO_PREFERENCES_DEFAULT_FOLDER_VIEWER		"default-folder-viewer"
#define NEMO_PREFERENCES_SPLIT_VIEW_DEFAULT_VIEW	"dakkar-split-view-default-view"
#define NEMO_PREFERENCES_FILES_COLOR_MODE "files-color-mode"
#define NEMO_PREFERENCES_FILES_ROUNDED_CORNERS "files-rounded-corners"
#define NEMO_PREFERENCES_FILES_CORNER_RADIUS "files-corner-radius"
#define DAKKAR_THEME_CUSTOM "dakkar-custom-theme"
#define DAKKAR_THEME_BACKGROUND "dakkar-theme-background"
#define DAKKAR_THEME_SIDEBAR "dakkar-theme-sidebar"
#define DAKKAR_THEME_INSPECTOR "dakkar-theme-inspector"
#define DAKKAR_THEME_TEXT "dakkar-theme-text"
#define DAKKAR_THEME_ACCENT "dakkar-theme-accent"
/* "" = derived from the accent (dimmer); see dakkar_theme_secondary_accent */
#define DAKKAR_THEME_ACCENT_SECONDARY "dakkar-theme-accent-secondary"
/* Quick Actions buttons and the statusbar; "" = derived (see nemo-corner-style.h) */
#define DAKKAR_THEME_QA_BUTTON "dakkar-theme-qa-button"
#define DAKKAR_THEME_QA_TEXT "dakkar-theme-qa-text"
#define DAKKAR_THEME_QA_HOVER "dakkar-theme-qa-hover"
#define DAKKAR_THEME_QA_BORDER "dakkar-theme-qa-border"
#define DAKKAR_THEME_STATUSBAR "dakkar-theme-statusbar"
#define DAKKAR_THEME_TOOLBAR "dakkar-theme-toolbar"
#define DAKKAR_THEME_TABS "dakkar-theme-tabs"
#define DAKKAR_THEME_BUTTONS "dakkar-theme-buttons"
#define DAKKAR_THEME_BORDERS "dakkar-theme-borders"
#define DAKKAR_THEME_HOVER "dakkar-theme-hover"
#define DAKKAR_THEME_MENUS "dakkar-theme-menus"
#define DAKKAR_THEME_OPACITY "dakkar-theme-opacity"
#define DAKKAR_THEME_SIDEBAR_OPACITY "dakkar-theme-sidebar-opacity"
#define DAKKAR_THEME_DETAILS_OPACITY "dakkar-theme-details-opacity"
#define DAKKAR_THEME_TOOLBAR_OPACITY "dakkar-theme-toolbar-opacity"
#define DAKKAR_THEME_SEE_THROUGH "dakkar-theme-see-through"
#define DAKKAR_THEME_WHOLE_WINDOW_OPACITY "dakkar-theme-whole-window-opacity"
#define DAKKAR_SIDEBAR_ROW_PADDING "dakkar-sidebar-row-padding"
#define DAKKAR_SIDEBAR_ICON_SIZE "dakkar-sidebar-icon-size"
#define DAKKAR_TYPE_ICONS "dakkar-type-icons"
#define DAKKAR_MIGRATED "dakkar-migrated"
#define DAKKAR_MENU_SIDEBAR_CHANGE_ICON "sidebar-menu-change-icon"
#define DAKKAR_MENU_SIDEBAR_ICON_COLOR "sidebar-menu-icon-color"
#define DAKKAR_SIDEBAR_SELECTION_RADIUS "dakkar-sidebar-selection-radius"
#define DAKKAR_SIDEBAR_HEADING_PADDING "dakkar-sidebar-heading-padding"
#define DAKKAR_THEME_SIDEBAR_HEADING "dakkar-theme-sidebar-heading"
#define DAKKAR_SIDEBAR_ICON_PACK "dakkar-sidebar-icon-pack"
#define DAKKAR_SIDEBAR_ICONS_MONO "dakkar-sidebar-icons-monochrome"
#define NEMO_PREFERENCES_FILES_SIDEBAR_SCALE "files-sidebar-scale"
#define NEMO_PREFERENCES_FILES_SIDEBAR_SLIDER_MODE "files-sidebar-slider-mode"
#define NEMO_PREFERENCES_FILES_INSTANT_SEARCH "files-instant-search"
#define NEMO_PREFERENCES_FILES_SEARCH_DELAY_MS "files-search-delay-ms"
#define NEMO_PREFERENCES_MILLER_COLUMNS_COUNT		"miller-columns-count"
#define NEMO_PREFERENCES_MILLER_COLUMN_WIDTH	"miller-column-width"
#define NEMO_PREFERENCES_MILLER_COLUMN_WIDTHS	"miller-column-widths"
#define NEMO_PREFERENCES_MILLER_AUTO_FIT	"miller-auto-fit-columns"
#define NEMO_PREFERENCES_MILLER_SHOW_PREVIEW	"miller-show-preview"
#define NEMO_PREFERENCES_MILLER_DRAG_HOVER	"miller-drag-hover"
#define NEMO_PREFERENCES_MILLER_WHEEL_HORIZONTAL	"miller-wheel-horizontal"
#define NEMO_PREFERENCES_MILLER_QUICK_LOOK	"miller-quick-look"
#define NEMO_PREFERENCES_MILLER_PREVIEW_DETAILS	"miller-preview-details"

#define NEMO_PREFERENCES_INHERIT_FOLDER_VIEWER		"inherit-folder-viewer"

#define NEMO_PREFERENCES_SHOW_FULL_PATH_TITLES      "show-full-path-titles"

#define NEMO_PREFERENCES_CLOSE_DEVICE_VIEW_ON_EJECT "close-device-view-on-device-eject"

#define NEMO_PREFERENCES_START_WITH_DUAL_PANE "start-with-dual-pane"
#define NEMO_PREFERENCES_IGNORE_VIEW_METADATA "ignore-view-metadata"
#define NEMO_PREFERENCES_SHOW_BOOKMARKS_IN_TO_MENUS "show-bookmarks-in-to-menus"
#define NEMO_PREFERENCES_SHOW_PLACES_IN_TO_MENUS "show-places-in-to-menus"

#define NEMO_PREFERENCES_RECENT_ENABLED "remember-recent-files"

#define NEMO_PREFERENCES_SIDEBAR_BOOKMARK_BREAKPOINT "sidebar-bookmark-breakpoint"

enum
{
	NEMO_DEFAULT_FOLDER_VIEWER_ICON_VIEW,
	NEMO_DEFAULT_FOLDER_VIEWER_COMPACT_VIEW,
	NEMO_DEFAULT_FOLDER_VIEWER_LIST_VIEW,
	NEMO_DEFAULT_FOLDER_VIEWER_MILLER_VIEW,
	NEMO_DEFAULT_FOLDER_VIEWER_OTHER
};

/* These IIDs are used by the preferences code and in nemo-application.c */
#define NEMO_ICON_VIEW_IID		"OAFIID:Nemo_File_Manager_Icon_View"
#define NEMO_COMPACT_VIEW_IID	"OAFIID:Nemo_File_Manager_Compact_View"
#define NEMO_LIST_VIEW_IID		"OAFIID:Nemo_File_Manager_List_View"
#define NEMO_DESKTOP_ICON_VIEW_IID  "OAFIID:Nemo_File_Manager_Desktop_Icon_View"
#define NEMO_DESKTOP_ICON_GRID_VIEW_IID  "OAFIID:Nemo_File_Manager_Desktop_Icon_Grid_View"

/* Icon View */
#define NEMO_PREFERENCES_ICON_VIEW_DEFAULT_ZOOM_LEVEL		"default-zoom-level"
#define NEMO_PREFERENCES_ICON_VIEW_LABELS_BESIDE_ICONS		"labels-beside-icons"

/* Which text attributes appear beneath icon names */
#define NEMO_PREFERENCES_ICON_VIEW_CAPTIONS				"captions"

/* The default size for thumbnail icons */
#define NEMO_PREFERENCES_ICON_VIEW_THUMBNAIL_SIZE			"thumbnail-size"

/* ellipsization preferences */
#define NEMO_PREFERENCES_ICON_VIEW_TEXT_ELLIPSIS_LIMIT		"text-ellipsis-limit"
#define NEMO_PREFERENCES_DESKTOP_TEXT_ELLIPSIS_LIMIT		"text-ellipsis-limit"

/* Compact View */
#define NEMO_PREFERENCES_COMPACT_VIEW_DEFAULT_ZOOM_LEVEL		"default-zoom-level"
#define NEMO_PREFERENCES_COMPACT_VIEW_ALL_COLUMNS_SAME_WIDTH	"all-columns-have-same-width"

/* List View */
#define NEMO_PREFERENCES_LIST_VIEW_DEFAULT_ZOOM_LEVEL		"default-zoom-level"
#define NEMO_PREFERENCES_LIST_VIEW_DEFAULT_VISIBLE_COLUMNS		"default-visible-columns"
#define NEMO_PREFERENCES_LIST_VIEW_DEFAULT_COLUMN_ORDER		"default-column-order"

#define NEMO_PREFERENCES_MAX_THUMBNAIL_THREADS "thumbnail-threads"

enum
{
	NEMO_CLICK_POLICY_SINGLE,
	NEMO_CLICK_POLICY_DOUBLE
};

enum
{
	NEMO_EXECUTABLE_TEXT_LAUNCH,
	NEMO_EXECUTABLE_TEXT_DISPLAY,
	NEMO_EXECUTABLE_TEXT_ASK
};

typedef enum
{
	NEMO_SPEED_TRADEOFF_ALWAYS,
	NEMO_SPEED_TRADEOFF_LOCAL_ONLY,
    NEMO_SPEED_TRADEOFF_NEVER
} NemoSpeedTradeoffValue;

#define NEMO_PREFERENCES_SHOW_DIRECTORY_ITEM_COUNTS "show-directory-item-counts"
#define NEMO_PREFERENCES_SHOW_IMAGE_FILE_THUMBNAILS	"show-image-thumbnails"
#define NEMO_PREFERENCES_IMAGE_FILE_THUMBNAIL_LIMIT	"thumbnail-limit"
#define NEMO_PREFERENCES_INHERIT_SHOW_THUMBNAILS "inherit-show-thumbnails"

#define NEMO_PREFERENCES_DESKTOP_FONT		   "font"
#define NEMO_PREFERENCES_DESKTOP_HOME_VISIBLE          "home-icon-visible"
#define NEMO_PREFERENCES_DESKTOP_COMPUTER_VISIBLE      "computer-icon-visible"
#define NEMO_PREFERENCES_DESKTOP_TRASH_VISIBLE         "trash-icon-visible"
#define NEMO_PREFERENCES_DESKTOP_VOLUMES_VISIBLE	   "volumes-visible"
#define NEMO_PREFERENCES_DESKTOP_NETWORK_VISIBLE       "network-icon-visible"
#define NEMO_PREFERENCES_DESKTOP_BACKGROUND_FADE       "background-fade"
#define NEMO_PREFERENCES_DESKTOP_IGNORED_DESKTOP_HANDLERS "ignored-desktop-handlers"

/* bulk rename utility */
#define NEMO_PREFERENCES_BULK_RENAME_TOOL              "bulk-rename-tool"

/* Lockdown */
#define NEMO_PREFERENCES_LOCKDOWN_COMMAND_LINE         "disable-command-line"

/* Desktop background */
#define NEMO_PREFERENCES_DESKTOP_LAYOUT "desktop-layout"
#define NEMO_PREFERENCES_SHOW_ORPHANED_DESKTOP_ICONS "show-orphaned-desktop-icons"
#define NEMO_PREFERENCES_SHOW_DESKTOP   "show-desktop-icons"    /* DEPRECATED */
#define NEMO_PREFERENCES_USE_DESKTOP_GRID "use-desktop-grid"
#define NEMO_PREFERENCES_DESKTOP_HORIZONTAL_GRID_ADJUST "horizontal-grid-adjust"
#define NEMO_PREFERENCES_DESKTOP_VERTICAL_GRID_ADJUST "vertical-grid-adjust"

/* File size unit prefix */
#define NEMO_PREFERENCES_SIZE_PREFIXES			"size-prefixes"

/* media handling */

#define GNOME_DESKTOP_MEDIA_HANDLING_AUTOMOUNT            "automount"
#define GNOME_DESKTOP_MEDIA_HANDLING_AUTOMOUNT_OPEN       "automount-open"
#define GNOME_DESKTOP_MEDIA_HANDLING_AUTORUN              "autorun-never"
#define NEMO_PREFERENCES_MEDIA_HANDLING_DETECT_CONTENT    "detect-content"

/* Terminal */
#define GNOME_DESKTOP_TERMINAL_EXEC        "exec"
#define GNOME_DESKTOP_TERMINAL_EXEC_ARG    "exec-arg"

/* Tooltips */
#define NEMO_PREFERENCES_TOOLTIPS_DESKTOP              "tooltips-on-desktop"
#define NEMO_PREFERENCES_TOOLTIPS_ICON_VIEW            "tooltips-in-icon-view"
#define NEMO_PREFERENCES_TOOLTIPS_LIST_VIEW            "tooltips-in-list-view"
#define NEMO_PREFERENCES_TOOLTIP_FILE_TYPE             "tooltips-show-file-type"
#define NEMO_PREFERENCES_TOOLTIP_MOD_DATE              "tooltips-show-mod-date"
#define NEMO_PREFERENCES_TOOLTIP_ACCESS_DATE           "tooltips-show-access-date"
#define NEMO_PREFERENCES_TOOLTIP_CREATED_DATE          "tooltips-show-birth-date"
#define NEMO_PREFERENCES_TOOLTIP_FULL_PATH             "tooltips-show-path"

#define NEMO_PREFERENCES_DISABLE_MENU_WARNING          "disable-menu-warning"

/* Plugins */
#define NEMO_PLUGIN_PREFERENCES_DISABLED_EXTENSIONS    "disabled-extensions"
#define NEMO_PLUGIN_PREFERENCES_DISABLED_ACTIONS       "disabled-actions"
#define NEMO_PLUGIN_PREFERENCES_DISABLED_SCRIPTS       "disabled-scripts"

/* Connect-to server dialog last-used method */
#define NEMO_PREFERENCES_LAST_SERVER_CONNECT_METHOD "last-server-connect-method"

/* File operations queue */
#define NEMO_PREFERENCES_NEVER_QUEUE_FILE_OPS          "never-queue-file-ops"

#define NEMO_PREFERENCES_CLICK_DOUBLE_PARENT_FOLDER    "click-double-parent-folder"
#define NEMO_PREFERENCES_EXPAND_ROW_ON_DND_DWELL       "expand-row-on-dnd-dwell"

#define NEMO_PREFERENCES_SHOW_MIME_MAKE_EXECUTABLE     "enable-mime-actions-make-executable"
#define NEMO_PREFERENCES_DEFERRED_ATTR_PRELOAD_LIMIT   "deferred-attribute-preload-limit"

#define NEMO_PREFERENCES_SEARCH_CONTENT_REGEX          "search-content-use-regex"
#define NEMO_PREFERENCES_SEARCH_REGEX_FORMAT           "search-regex-format"
#define NEMO_PREFERENCES_SEARCH_USE_RAW                "search-content-use-raw"
#define NEMO_PREFERENCES_SEARCH_FILE_CASE              "search-file-case-sensitive"
#define NEMO_PREFERENCES_SEARCH_CONTENT_CASE           "search-content-case-sensitive"
#define NEMO_PREFERENCES_SEARCH_SKIP_FOLDERS           "search-skip-folders"
#define NEMO_PREFERENCES_SEARCH_FILES_RECURSIVELY      "search-files-recursively"
#define NEMO_PREFERENCES_SEARCH_VISIBLE_COLUMNS        "search-visible-columns"
#define NEMO_PREFERENCES_SEARCH_SORT_COLUMN            "search-sort-column"
#define NEMO_PREFERENCES_SEARCH_REVERSE_SORT           "search-reverse-sort"
#define NEMO_PREFERENCES_SEARCH_FILE_HISTORY           "search-file-history"
#define NEMO_PREFERENCES_SEARCH_CONTENT_HISTORY        "search-content-history"
#define NEMO_PREFERENCES_SEARCH_CONTENT_HISTORY_LENGTH "search-content-history-length"

void nemo_global_preferences_init                      (void);
void nemo_global_preferences_finalize                  (void);
char *nemo_global_preferences_get_default_folder_viewer_preference_as_iid (void);
void  nemo_global_preferences_set_default_folder_viewer_from_iid (const char *iid);
char *nemo_global_preferences_get_split_view_viewer_as_iid (void);
void  nemo_global_preferences_set_split_view_viewer_from_iid (const char *iid);
gboolean nemo_global_preferences_get_inherit_folder_viewer_preference (void);
gboolean nemo_global_preferences_get_inherit_show_thumbnails_preference (void);
gboolean nemo_global_preferences_get_ignore_view_metadata (void);
int nemo_global_preferences_get_size_prefix_preference (void);
char *nemo_global_preferences_get_desktop_iid (void);
gint nemo_global_preferences_get_tooltip_flags (void);
gboolean nemo_global_preferences_should_load_plugin (const gchar *name, const gchar *key);
gchar **nemo_global_preferences_get_fileroller_mimetypes (void);

gchar *nemo_global_preferences_get_mono_system_font (void);
gchar *nemo_global_preferences_get_mono_font_family_match (const gchar *in_family);

extern GSettings *nemo_preferences;
extern GSettings *nemo_icon_view_preferences;
extern GSettings *nemo_list_view_preferences;
extern GSettings *nemo_compact_view_preferences;
extern GSettings *nemo_desktop_preferences;
extern GSettings *nemo_tree_sidebar_preferences;
extern GSettings *nemo_window_state;
extern GSettings *gtk_filechooser_preferences;
extern GSettings *nemo_plugin_preferences;
extern GSettings *nemo_menu_config_preferences;

/* Dakkar: one-time copy of pre-rename dakar-* settings (called by init). */

extern GSettings *nemo_search_preferences;
extern GSettings *gnome_lockdown_preferences;
extern GSettings *gnome_background_preferences;
extern GSettings *gnome_media_handling_preferences;
extern GSettings *gnome_terminal_preferences;
extern GSettings *cinnamon_privacy_preferences;
extern GSettings *cinnamon_interface_preferences;
extern GSettings *gnome_interface_preferences;

/* Cached for fast access and used in nemo-file.c for constructing date/time strings */
extern GTimeZone      *prefs_current_timezone;
extern gboolean        prefs_current_24h_time_format;
extern NemoDateFormat  prefs_current_date_format;

extern GTimer    *nemo_startup_timer;

G_END_DECLS

#endif /* NEMO_GLOBAL_PREFERENCES_H */
