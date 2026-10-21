/* nemo-compress-dialog.c - Dakkar "Compress…" dialog and archive actions.
 *
 * Shared by the context menu of every view and by the Quick Actions panel.
 * The archive itself is written by nemo_file_operations_compress() in
 * libnemo-private (libarchive, in process), which reports through the
 * TeraCopy-style transfer window and its status-bar bar.
 *
 * GPL-2.0-or-later.
 */
#include <config.h>
#include <string.h>
#include <unistd.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include "nemo-compress-dialog.h"
#include <libnemo-private/nemo-file-operations.h>
#include <libnemo-private/nemo-global-preferences.h>

typedef struct {
	GList *files;
	GtkWidget *dialog;
	GtkWidget *name_entry;
	GtkWidget *folder_button;
	GtkWidget *format_combo;
	GtkWidget *result_label;
	GtkWidget *create_button;
	/* password protection */
	GtkWidget *encrypt_check;
	GtkWidget *encrypt_note;
	GtkWidget *password_label;
	GtkWidget *password_entry;
	GtkWidget *confirm_label;
	GtkWidget *confirm_entry;
	GtkWidget *show_check;
	GtkWidget *strength_label;
	GtkWidget *names_check;
	gchar *default_folder;
} CompressDialog;

static const struct {
	NemoArchiveFormat format;
	const gchar *id;
	const gchar *label;
} formats[] = {
	{ NEMO_ARCHIVE_ZIP,     "zip",     N_("ZIP (.zip) — opens everywhere") },
	{ NEMO_ARCHIVE_TAR_GZ,  "tar.gz",  N_("TAR.GZ (.tar.gz)") },
	{ NEMO_ARCHIVE_TAR_XZ,  "tar.xz",  N_("TAR.XZ (.tar.xz) — smaller, slower") },
	{ NEMO_ARCHIVE_TAR_ZST, "tar.zst", N_("TAR.ZST (.tar.zst) — fast") },
	{ NEMO_ARCHIVE_7Z,      "7z",      N_("7-Zip (.7z)") },
};

gboolean
nemo_compress_available (void)
{
	return nemo_file_operations_archive_supported ();
}

gboolean
nemo_compress_can_compress (GList *files)
{
	GList *l;
	if (files == NULL || !nemo_compress_available ()) return FALSE;
	for (l = files; l != NULL; l = l->next)
		if (!g_file_is_native (l->data) || !g_file_has_parent (l->data, NULL))
			return FALSE;
	return TRUE;
}

gboolean
nemo_compress_can_extract (GList *files)
{
	GList *l;
	if (files == NULL || !nemo_compress_available ()) return FALSE;
	for (l = files; l != NULL; l = l->next) {
		gchar *name;
		gboolean ok;
		if (!g_file_is_native (l->data)) return FALSE;
		name = g_file_get_basename (l->data);
		ok = nemo_file_operations_is_extractable_name (name) &&
		     g_file_query_file_type (l->data, G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL) ==
		     G_FILE_TYPE_REGULAR;
		g_free (name);
		if (!ok) return FALSE;
	}
	return TRUE;
}

/* "report.pdf" -> "report", "site.tar.gz" -> "site"; folders keep their full name. */
static gchar *
default_archive_name (GList *files)
{
	gchar *name;
	if (files->next == NULL) {
		name = g_file_get_basename (files->data);
		if (g_file_query_file_type (files->data, G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL) !=
		    G_FILE_TYPE_DIRECTORY) {
			gchar *dot = strrchr (name, '.');
			if (dot != NULL && dot != name)
				*dot = '\0';
			/* "backup.tar.gz" -> "backup", not "backup.tar" */
			if (g_str_has_suffix (name, ".tar") && strlen (name) > 4)
				name[strlen (name) - 4] = '\0';
		}
	} else {
		GFile *parent = g_file_get_parent (files->data);
		GFile *grand = parent != NULL ? g_file_get_parent (parent) : NULL;
		/* the folder they are in, or "Archive" at the file-system root */
		name = grand != NULL ? g_file_get_basename (parent) : NULL;
		g_clear_object (&grand);
		g_clear_object (&parent);
		if (name == NULL || *name == '\0' || g_str_equal (name, "/")) {
			g_free (name);
			name = g_strdup (_("Archive"));
		}
	}
	return name;
}

static NemoArchiveFormat
selected_format (CompressDialog *d)
{
	const gchar *id = gtk_combo_box_get_active_id (GTK_COMBO_BOX (d->format_combo));
	guint i;
	for (i = 0; i < G_N_ELEMENTS (formats); i++)
		if (g_strcmp0 (id, formats[i].id) == 0) return formats[i].format;
	return NEMO_ARCHIVE_ZIP;
}

static gchar *
selected_folder (CompressDialog *d)
{
	gchar *folder = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (d->folder_button));
	return folder != NULL ? folder : g_strdup (d->default_folder);
}

/* Can the chosen format be encrypted here? Updates the option and its
 * note; returns TRUE when a password will be used. */
static gboolean
compress_dialog_update_encryption (CompressDialog *d)
{
	NemoArchiveFormat format = selected_format (d);
	gboolean names = FALSE;
	NemoArchiveEncryption enc = nemo_file_operations_archive_encryption (format, &names);
	gboolean possible = enc != NEMO_ARCHIVE_ENCRYPTION_NONE;
	gboolean on = possible && gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (d->encrypt_check));
	const gchar *note = NULL;
	GtkStyleContext *style = gtk_widget_get_style_context (d->encrypt_note);

	gtk_widget_set_sensitive (d->encrypt_check, possible);
	gtk_style_context_remove_class (style, "error");
	if (!possible && format == NEMO_ARCHIVE_7Z)
		note = _("Encrypting 7z archives needs the 7z program (package 7zip or p7zip-full).");
	else if (!possible && format == NEMO_ARCHIVE_ZIP)
		note = _("This system’s libarchive cannot encrypt ZIP archives.");
	else if (!possible)
		note = _("Only ZIP and 7-Zip archives can be encrypted.");
	else if (on && enc == NEMO_ARCHIVE_ENCRYPTION_ZIPCRYPTO) {
		note = _("Warning: only the old ZipCrypto method is available here. It is easy to break; use 7-Zip for real protection.");
		gtk_style_context_add_class (style, "error");
	} else if (on && format == NEMO_ARCHIVE_ZIP)
		note = _("AES-256. Older unzip tools cannot open AES-encrypted ZIP files; 7-Zip and most archive managers can. File names stay visible.");
	else if (on && format == NEMO_ARCHIVE_7Z)
		note = _("AES-256, written by the 7z program.");
	gtk_label_set_text (GTK_LABEL (d->encrypt_note), note != NULL ? note : "");
	gtk_widget_set_visible (d->encrypt_note, note != NULL);
	gtk_widget_set_visible (d->password_label, on);
	gtk_widget_set_visible (d->password_entry, on);
	gtk_widget_set_visible (d->confirm_label, on);
	gtk_widget_set_visible (d->confirm_entry, on);
	gtk_widget_set_visible (d->show_check, on);
	gtk_widget_set_visible (d->strength_label, on);
	/* only 7z can hide the file names (header encryption) */
	gtk_widget_set_visible (d->names_check, on && names);
	return on;
}

/* A rough hint, nothing more: length and kinds of characters. */
static void
compress_dialog_update_strength (CompressDialog *d)
{
	const gchar *pw = gtk_entry_get_text (GTK_ENTRY (d->password_entry));
	gboolean lower = FALSE, upper = FALSE, digit = FALSE, other = FALSE;
	glong len = g_utf8_strlen (pw, -1);
	gint score;
	const gchar *p, *text;
	for (p = pw; *p != '\0'; p = g_utf8_next_char (p)) {
		gunichar c = g_utf8_get_char (p);
		if (g_unichar_islower (c)) lower = TRUE;
		else if (g_unichar_isupper (c)) upper = TRUE;
		else if (g_unichar_isdigit (c)) digit = TRUE;
		else other = TRUE;
	}
	score = (len >= 8) + (len >= 12) + (len >= 16) + lower + upper + digit + other;
	if (len == 0) text = "";
	else if (len < 8 || score <= 3) text = _("Strength: weak");
	else if (score <= 5) text = _("Strength: fair");
	else text = _("Strength: strong");
	gtk_label_set_text (GTK_LABEL (d->strength_label), text);
}

/* Validate the name and check for an existing file; returns the archive
 * path when it can be created. */
static gchar *
compress_dialog_check (CompressDialog *d)
{
	const gchar *name = gtk_entry_get_text (GTK_ENTRY (d->name_entry));
	gchar *folder = selected_folder (d);
	gchar *file_name, *path = NULL, *markup;
	GtkStyleContext *style = gtk_widget_get_style_context (d->result_label);

	gchar *trimmed = g_strstrip (g_strdup (name != NULL ? name : ""));
	gboolean empty = *trimmed == '\0';

	g_free (trimmed);
	gtk_style_context_remove_class (style, "error");
	gtk_style_context_remove_class (style, "dim-label");
	if (empty) {
		markup = g_strdup (_("Enter a name for the archive."));
		gtk_style_context_add_class (style, "error");
	} else if (strchr (name, '/') != NULL || g_str_equal (name, ".") || g_str_equal (name, "..")) {
		markup = g_strdup (_("The name cannot contain “/”."));
		gtk_style_context_add_class (style, "error");
	} else if (folder == NULL || access (folder, W_OK) != 0) {
		markup = g_strdup (_("You cannot create files in this folder."));
		gtk_style_context_add_class (style, "error");
	} else {
		file_name = g_strconcat (name, nemo_file_operations_archive_format_extension (selected_format (d)), NULL);
		path = g_build_filename (folder, file_name, NULL);
		if (g_file_test (path, G_FILE_TEST_EXISTS) || g_file_test (path, G_FILE_TEST_IS_SYMLINK)) {
			/* Translators: %s is a file name */
			markup = g_strdup_printf (_("“%s” already exists here. Choose another name."), file_name);
			gtk_style_context_add_class (style, "error");
			g_clear_pointer (&path, g_free);
		} else if (compress_dialog_update_encryption (d) &&
		           gtk_entry_get_text_length (GTK_ENTRY (d->password_entry)) == 0) {
			markup = g_strdup (_("Enter a password."));
			gtk_style_context_add_class (style, "error");
			g_clear_pointer (&path, g_free);
		} else if (gtk_widget_get_visible (d->password_entry) &&
		           g_strcmp0 (gtk_entry_get_text (GTK_ENTRY (d->password_entry)),
		                      gtk_entry_get_text (GTK_ENTRY (d->confirm_entry))) != 0) {
			markup = g_strdup (_("The passwords do not match."));
			gtk_style_context_add_class (style, "error");
			g_clear_pointer (&path, g_free);
		} else {
			/* Translators: %s is the archive file name that will be created */
			markup = g_strdup_printf (gtk_widget_get_visible (d->password_entry) ?
				_("Creates “%s”, protected with a password") : _("Creates “%s”"), file_name);
			gtk_style_context_add_class (style, "dim-label");
		}
		g_free (file_name);
	}
	if (path == NULL)
		compress_dialog_update_encryption (d);
	compress_dialog_update_strength (d);
	gtk_label_set_text (GTK_LABEL (d->result_label), markup);
	gtk_widget_set_sensitive (d->create_button, path != NULL);
	g_free (markup);
	g_free (folder);
	return path;
}

static void
compress_dialog_changed (GtkWidget *widget, CompressDialog *d)
{
	g_free (compress_dialog_check (d));
	(void) widget;
}

static void
compress_dialog_free (CompressDialog *d)
{
	g_list_free_full (d->files, g_object_unref);
	g_free (d->default_folder);
	g_free (d);
}

static void
compress_dialog_response (GtkDialog *dialog, gint response, CompressDialog *d)
{
	if (response == GTK_RESPONSE_ACCEPT) {
		gchar *path = compress_dialog_check (d);
		if (path == NULL) return; /* keep the dialog open, the label says why */
		{
			GFile *archive = g_file_new_for_path (path);
			GtkWindow *parent = gtk_window_get_transient_for (GTK_WINDOW (dialog));
			g_settings_set_string (nemo_preferences, "dakkar-compress-format",
			                       gtk_combo_box_get_active_id (GTK_COMBO_BOX (d->format_combo)));
			gchar *password = gtk_widget_get_visible (d->password_entry) ?
				g_strdup (gtk_entry_get_text (GTK_ENTRY (d->password_entry))) : NULL;
			gboolean names = gtk_widget_get_visible (d->names_check) &&
				gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (d->names_check));
			/* the password goes to the job only: never into GSettings */
			nemo_file_operations_compress_full (d->files, archive, selected_format (d),
			                                    password, names, parent, NULL, NULL);
			nemo_file_operations_secret_free (password);
			g_object_unref (archive);
		}
		g_free (path);
	}
	/* do not leave the password behind in the entries' buffers */
	gtk_entry_set_text (GTK_ENTRY (d->password_entry), "");
	gtk_entry_set_text (GTK_ENTRY (d->confirm_entry), "");
	gtk_widget_destroy (GTK_WIDGET (dialog));
}

static void
compress_dialog_show_toggled (GtkToggleButton *button, CompressDialog *d)
{
	gboolean show = gtk_toggle_button_get_active (button);
	gtk_entry_set_visibility (GTK_ENTRY (d->password_entry), show);
	gtk_entry_set_visibility (GTK_ENTRY (d->confirm_entry), show);
}

static GtkWidget *
password_entry_new (void)
{
	GtkWidget *entry = gtk_entry_new ();
	gtk_entry_set_visibility (GTK_ENTRY (entry), FALSE);
	gtk_entry_set_input_purpose (GTK_ENTRY (entry), GTK_INPUT_PURPOSE_PASSWORD);
	gtk_entry_set_activates_default (GTK_ENTRY (entry), TRUE);
	gtk_widget_set_hexpand (entry, TRUE);
	gtk_widget_set_no_show_all (entry, TRUE);
	return entry;
}

static GtkWidget *
caption (const gchar *text)
{
	GtkWidget *label = gtk_label_new_with_mnemonic (text);
	gtk_label_set_xalign (GTK_LABEL (label), 1.0);
	return label;
}

void
nemo_compress_dialog_show (GtkWindow *parent, GList *files)
{
	CompressDialog *d;
	GtkWidget *grid, *content, *label, *summary;
	GFile *dir;
	gchar *name, *text, *remembered;
	guint i, n = g_list_length (files);
	gboolean have_active = FALSE;

	g_return_if_fail (files != NULL);
	d = g_new0 (CompressDialog, 1);
	d->files = g_list_copy_deep (files, (GCopyFunc) g_object_ref, NULL);
	dir = g_file_get_parent (files->data);
	d->default_folder = dir != NULL ? g_file_get_path (dir) : g_strdup (g_get_home_dir ());
	g_clear_object (&dir);

	d->dialog = gtk_dialog_new_with_buttons (_("Compress"), parent,
		GTK_DIALOG_DESTROY_WITH_PARENT,
		_("_Cancel"), GTK_RESPONSE_CANCEL, NULL);
	d->create_button = gtk_dialog_add_button (GTK_DIALOG (d->dialog), _("C_reate"), GTK_RESPONSE_ACCEPT);
	gtk_style_context_add_class (gtk_widget_get_style_context (d->create_button), "suggested-action");
	gtk_dialog_set_default_response (GTK_DIALOG (d->dialog), GTK_RESPONSE_ACCEPT);
	gtk_window_set_default_size (GTK_WINDOW (d->dialog), 480, -1);
	gtk_widget_set_name (d->dialog, "dakkar-compress-dialog");

	content = gtk_dialog_get_content_area (GTK_DIALOG (d->dialog));
	grid = gtk_grid_new ();
	gtk_grid_set_row_spacing (GTK_GRID (grid), 8);
	gtk_grid_set_column_spacing (GTK_GRID (grid), 12);
	gtk_container_set_border_width (GTK_CONTAINER (grid), 14);

	if (n == 1) {
		gchar *base = g_file_get_basename (files->data);
		/* Translators: %s is the name of the selected item */
		text = g_strdup_printf (_("Compress “%s”"), base);
		g_free (base);
	} else {
		text = g_strdup_printf (ngettext ("Compress %u item", "Compress %u items", n), n);
	}
	summary = gtk_label_new (text);
	g_free (text);
	gtk_label_set_xalign (GTK_LABEL (summary), 0.0);
	gtk_label_set_ellipsize (GTK_LABEL (summary), PANGO_ELLIPSIZE_MIDDLE);
	{
		PangoAttrList *attrs = pango_attr_list_new ();
		pango_attr_list_insert (attrs, pango_attr_weight_new (PANGO_WEIGHT_BOLD));
		gtk_label_set_attributes (GTK_LABEL (summary), attrs);
		pango_attr_list_unref (attrs);
	}
	gtk_grid_attach (GTK_GRID (grid), summary, 0, 0, 2, 1);

	label = caption (_("Archive _name:"));
	d->name_entry = gtk_entry_new ();
	gtk_widget_set_hexpand (d->name_entry, TRUE);
	gtk_entry_set_activates_default (GTK_ENTRY (d->name_entry), TRUE);
	name = default_archive_name (files);
	gtk_entry_set_text (GTK_ENTRY (d->name_entry), name);
	g_free (name);
	gtk_label_set_mnemonic_widget (GTK_LABEL (label), d->name_entry);
	gtk_grid_attach (GTK_GRID (grid), label, 0, 1, 1, 1);
	gtk_grid_attach (GTK_GRID (grid), d->name_entry, 1, 1, 1, 1);

	label = caption (_("_Format:"));
	d->format_combo = gtk_combo_box_text_new ();
	remembered = g_settings_get_string (nemo_preferences, "dakkar-compress-format");
	for (i = 0; i < G_N_ELEMENTS (formats); i++) {
		if (!nemo_file_operations_archive_format_available (formats[i].format)) continue;
		gtk_combo_box_text_append (GTK_COMBO_BOX_TEXT (d->format_combo), formats[i].id,
		                           _(formats[i].label));
		if (g_strcmp0 (remembered, formats[i].id) == 0) have_active = TRUE;
	}
	gtk_combo_box_set_active_id (GTK_COMBO_BOX (d->format_combo), have_active ? remembered : "zip");
	g_free (remembered);
	gtk_label_set_mnemonic_widget (GTK_LABEL (label), d->format_combo);
	gtk_grid_attach (GTK_GRID (grid), label, 0, 2, 1, 1);
	gtk_grid_attach (GTK_GRID (grid), d->format_combo, 1, 2, 1, 1);

	label = caption (_("_Location:"));
	d->folder_button = gtk_file_chooser_button_new (_("Select a folder for the archive"),
	                                                GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER);
	gtk_file_chooser_set_filename (GTK_FILE_CHOOSER (d->folder_button), d->default_folder);
	gtk_label_set_mnemonic_widget (GTK_LABEL (label), d->folder_button);
	gtk_grid_attach (GTK_GRID (grid), label, 0, 3, 1, 1);
	gtk_grid_attach (GTK_GRID (grid), d->folder_button, 1, 3, 1, 1);

	d->encrypt_check = gtk_check_button_new_with_mnemonic (_("_Encrypt with password"));
	gtk_grid_attach (GTK_GRID (grid), d->encrypt_check, 1, 4, 1, 1);
	d->encrypt_note = gtk_label_new ("");
	gtk_label_set_xalign (GTK_LABEL (d->encrypt_note), 0.0);
	gtk_label_set_line_wrap (GTK_LABEL (d->encrypt_note), TRUE);
	gtk_label_set_max_width_chars (GTK_LABEL (d->encrypt_note), 52);
	gtk_style_context_add_class (gtk_widget_get_style_context (d->encrypt_note), "dim-label");
	gtk_widget_set_no_show_all (d->encrypt_note, TRUE);
	gtk_grid_attach (GTK_GRID (grid), d->encrypt_note, 1, 5, 1, 1);

	d->password_label = caption (_("_Password:"));
	gtk_widget_set_no_show_all (d->password_label, TRUE);
	d->password_entry = password_entry_new ();
	gtk_label_set_mnemonic_widget (GTK_LABEL (d->password_label), d->password_entry);
	gtk_grid_attach (GTK_GRID (grid), d->password_label, 0, 6, 1, 1);
	gtk_grid_attach (GTK_GRID (grid), d->password_entry, 1, 6, 1, 1);
	d->confirm_label = caption (_("Con_firm password:"));
	gtk_widget_set_no_show_all (d->confirm_label, TRUE);
	d->confirm_entry = password_entry_new ();
	gtk_label_set_mnemonic_widget (GTK_LABEL (d->confirm_label), d->confirm_entry);
	gtk_grid_attach (GTK_GRID (grid), d->confirm_label, 0, 7, 1, 1);
	gtk_grid_attach (GTK_GRID (grid), d->confirm_entry, 1, 7, 1, 1);
	{
		GtkWidget *box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 12);
		d->show_check = gtk_check_button_new_with_mnemonic (_("_Show password"));
		gtk_widget_set_no_show_all (d->show_check, TRUE);
		d->strength_label = gtk_label_new ("");
		gtk_widget_set_no_show_all (d->strength_label, TRUE);
		gtk_style_context_add_class (gtk_widget_get_style_context (d->strength_label), "dim-label");
		gtk_box_pack_start (GTK_BOX (box), d->show_check, FALSE, FALSE, 0);
		gtk_box_pack_end (GTK_BOX (box), d->strength_label, FALSE, FALSE, 0);
		gtk_grid_attach (GTK_GRID (grid), box, 1, 8, 1, 1);
	}
	d->names_check = gtk_check_button_new_with_mnemonic (_("Encrypt file _names too (nothing can be listed without the password)"));
	gtk_widget_set_no_show_all (d->names_check, TRUE);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (d->names_check), TRUE);
	gtk_grid_attach (GTK_GRID (grid), d->names_check, 1, 9, 1, 1);

	d->result_label = gtk_label_new ("");
	gtk_label_set_xalign (GTK_LABEL (d->result_label), 0.0);
	gtk_label_set_line_wrap (GTK_LABEL (d->result_label), TRUE);
	gtk_grid_attach (GTK_GRID (grid), d->result_label, 1, 10, 1, 1);

	gtk_box_pack_start (GTK_BOX (content), grid, TRUE, TRUE, 0);
	gtk_widget_show_all (grid);

	g_signal_connect (d->name_entry, "changed", G_CALLBACK (compress_dialog_changed), d);
	g_signal_connect (d->format_combo, "changed", G_CALLBACK (compress_dialog_changed), d);
	g_signal_connect (d->folder_button, "selection-changed", G_CALLBACK (compress_dialog_changed), d);
	g_signal_connect (d->encrypt_check, "toggled", G_CALLBACK (compress_dialog_changed), d);
	g_signal_connect (d->password_entry, "changed", G_CALLBACK (compress_dialog_changed), d);
	g_signal_connect (d->confirm_entry, "changed", G_CALLBACK (compress_dialog_changed), d);
	g_signal_connect (d->show_check, "toggled", G_CALLBACK (compress_dialog_show_toggled), d);
	g_signal_connect (d->dialog, "response", G_CALLBACK (compress_dialog_response), d);
	g_object_set_data_full (G_OBJECT (d->dialog), "dakkar-compress", d,
	                        (GDestroyNotify) compress_dialog_free);
	g_free (compress_dialog_check (d));
	gtk_widget_grab_focus (d->name_entry);
	gtk_editable_select_region (GTK_EDITABLE (d->name_entry), 0, -1);
	gtk_window_present (GTK_WINDOW (d->dialog));
}

void
nemo_compress_extract_here (GtkWindow *parent, GList *files)
{
	GHashTable *by_dir;
	GHashTableIter it;
	gpointer key, value;
	GList *l;
	g_return_if_fail (files != NULL);
	/* one job per folder: each archive is unpacked next to itself */
	by_dir = g_hash_table_new_full (g_file_hash, (GEqualFunc) g_file_equal, g_object_unref, NULL);
	for (l = files; l != NULL; l = l->next) {
		GFile *dir = g_file_get_parent (l->data);
		GList *list;
		if (dir == NULL) continue;
		list = g_hash_table_lookup (by_dir, dir);
		list = g_list_append (list, l->data);
		g_hash_table_replace (by_dir, dir, list);
	}
	g_hash_table_iter_init (&it, by_dir);
	while (g_hash_table_iter_next (&it, &key, &value)) {
		nemo_file_operations_extract (value, key, parent, NULL, NULL);
		g_list_free (value);
	}
	g_hash_table_unref (by_dir);
}

/* ---------- Extract To… ---------- */

void
nemo_compress_extract_to (GtkWindow *parent, GList *files)
{
	GtkWidget *chooser, *subfolders;
	GFile *dir;
	guint n;

	g_return_if_fail (files != NULL);
	n = g_list_length (files);
	chooser = gtk_file_chooser_dialog_new (
		n == 1 ? _("Extract To") : _("Extract Archives To"), parent,
		GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
		_("_Cancel"), GTK_RESPONSE_CANCEL,
		_("_Extract"), GTK_RESPONSE_ACCEPT,
		NULL);
	gtk_dialog_set_default_response (GTK_DIALOG (chooser), GTK_RESPONSE_ACCEPT);
	gtk_file_chooser_set_create_folders (GTK_FILE_CHOOSER (chooser), TRUE);
	gtk_file_chooser_set_local_only (GTK_FILE_CHOOSER (chooser), TRUE);
	gtk_widget_set_name (chooser, "dakkar-extract-to-dialog");
	/* start in the folder being viewed (where the archive is) */
	dir = g_file_get_parent (files->data);
	if (dir != NULL)
		gtk_file_chooser_set_current_folder_file (GTK_FILE_CHOOSER (chooser), dir, NULL);
	g_clear_object (&dir);

	subfolders = gtk_check_button_new_with_mnemonic (n == 1 ?
		_("Create a _folder for the archive (not when it contains just one item)") :
		_("Create a _folder for each archive (not when it contains just one item)"));
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (subfolders),
		g_settings_get_boolean (nemo_preferences, "dakkar-extract-subfolder"));
	gtk_widget_set_tooltip_text (subfolders,
		_("When off, the contents go straight into the chosen folder; names that already exist there are asked about."));
	gtk_widget_show (subfolders);
	gtk_file_chooser_set_extra_widget (GTK_FILE_CHOOSER (chooser), subfolders);

	if (gtk_dialog_run (GTK_DIALOG (chooser)) == GTK_RESPONSE_ACCEPT) {
		GFile *target = gtk_file_chooser_get_file (GTK_FILE_CHOOSER (chooser));
		gboolean per_archive = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (subfolders));
		g_settings_set_boolean (nemo_preferences, "dakkar-extract-subfolder", per_archive);
		if (target != NULL && g_file_is_native (target))
			nemo_file_operations_extract_full (files, target, per_archive, parent, NULL, NULL);
		g_clear_object (&target);
	}
	gtk_widget_destroy (chooser);
}
