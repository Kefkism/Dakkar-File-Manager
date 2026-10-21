/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-file-operations: execute file operations.

   Copyright (C) 1999, 2000 Free Software Foundation
   Copyright (C) 2000, 2001 Eazel, Inc.

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.
   
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.
   
   You should have received a copy of the GNU General Public
   License along with this program; if not, write to the
   Free Software Foundation, Inc., 51 Franklin Street - Suite 500,
   Boston, MA 02110-1335, USA.
   
   Authors: Ettore Perazzoli <ettore@gnu.org>,
            Pavel Cisler <pavel@eazel.com>
*/

#ifndef NEMO_FILE_OPERATIONS_H
#define NEMO_FILE_OPERATIONS_H

#include <gtk/gtk.h>
#include <gio/gio.h>

typedef void (* NemoCopyCallback)      (GHashTable *debuting_uris,
					    gboolean    success,
					    gpointer    callback_data);
typedef void (* NemoCreateCallback)    (GFile      *new_file,
					    gboolean    success,
					    gpointer    callback_data);
typedef void (* NemoOpCallback)        (gboolean    success,
					    gpointer    callback_data);
typedef void (* NemoDeleteCallback)    (GHashTable *debuting_uris,
					    gboolean    user_cancel,
					    gpointer    callback_data);
typedef void (* NemoMountCallback)     (GVolume    *volume,
					    gboolean    success,
					    GObject    *callback_data_object);
typedef void (* NemoUnmountCallback)   (gpointer    callback_data);

/* FIXME: int copy_action should be an enum */

void nemo_file_operations_copy_move   (const GList               *item_uris,
					   GArray                    *relative_item_points,
					   const char                *target_dir_uri,
					   GdkDragAction              copy_action,
					   GtkWidget                 *parent_view,
					   NemoCopyCallback       done_callback,
					   gpointer                   done_callback_data);
void nemo_file_operations_copy_file (GFile *source_file,
					 GFile *target_dir,
					 const gchar *source_display_name,
					 const gchar *new_name,
					 GtkWindow *parent_window,
					 NemoCopyCallback done_callback,
					 gpointer done_callback_data);
void nemo_file_operations_empty_trash (GtkWidget                 *parent_view);
void nemo_file_operations_new_folder  (GtkWidget                 *parent_view,
					   GdkPoint                  *target_point,
					   const char                *parent_dir_uri,
					   NemoCreateCallback     done_callback,
					   gpointer                   done_callback_data);
void nemo_file_operations_new_file    (GtkWidget                 *parent_view,
					   GdkPoint                  *target_point,
					   const char                *parent_dir,
					   const char                *target_filename,
					   const char                *initial_contents,
					   int                        length,
					   NemoCreateCallback     done_callback,
					   gpointer                   data);
void nemo_file_operations_new_file_from_template (GtkWidget               *parent_view,
					   	      GdkPoint                *target_point,
						      const char              *parent_dir,
						      const char              *target_filename,
						      const char              *template_uri,
						      NemoCreateCallback   done_callback,
						      gpointer                 data);

void nemo_file_operations_delete          (GList                  *files,
					       GtkWindow              *parent_window,
					       NemoDeleteCallback  done_callback,
					       gpointer                done_callback_data);
void nemo_file_operations_trash_or_delete (GList                  *files,
					       GtkWindow              *parent_window,
					       NemoDeleteCallback  done_callback,
					       gpointer                done_callback_data);

void nemo_file_set_permissions_recursive (const char                     *directory,
					      guint32                         file_permissions,
					      guint32                         file_mask,
					      guint32                         folder_permissions,
					      guint32                         folder_mask,
					      NemoOpCallback              callback,
					      gpointer                        callback_data);

void nemo_file_operations_unmount_mount (GtkWindow                      *parent_window,
					     GMount                         *mount,
					     gboolean                        eject,
					     gboolean                        check_trash);
void nemo_file_operations_unmount_mount_full (GtkWindow                 *parent_window,
						  GMount                    *mount,
                          GMountOperation           *mount_operation,
						  gboolean                   eject,
						  gboolean                   check_trash,
						  NemoUnmountCallback    callback,
						  gpointer                   callback_data);
void nemo_file_operations_mount_volume  (GtkWindow                      *parent_window,
					     GVolume                        *volume);
void nemo_file_operations_mount_volume_full (GtkWindow                      *parent_window,
						 GVolume                        *volume,
						 NemoMountCallback           mount_callback,
						 GObject                        *mount_callback_data_object);

void nemo_file_operations_copy      (GList                *files,
					 GArray               *relative_item_points,
					 GFile                *target_dir,
					 GtkWindow            *parent_window,
					 NemoCopyCallback  done_callback,
					 gpointer              done_callback_data);
void nemo_file_operations_move      (GList                *files,
					 GArray               *relative_item_points,
					 GFile                *target_dir,
					 GtkWindow            *parent_window,
					 NemoCopyCallback  done_callback,
					 gpointer              done_callback_data);
void nemo_file_operations_duplicate (GList                *files,
					 GArray               *relative_item_points,
					 GtkWindow            *parent_window,
					 NemoCopyCallback  done_callback,
					 gpointer              done_callback_data);
void nemo_file_operations_link      (GList                *files,
					 GArray               *relative_item_points,
					 GFile                *target_dir,
					 GtkWindow            *parent_window,
					 NemoCopyCallback  done_callback,
					 gpointer              done_callback_data);
void nemo_file_mark_desktop_file_trusted (GFile           *file,
					      GtkWindow        *parent_window,
					      gboolean          interactive,
					      NemoOpCallback done_callback,
					      gpointer          done_callback_data);


/* ---- Dakkar: archives (libarchive, in process, TeraCopy-style progress) ---- */
typedef enum {
	NEMO_ARCHIVE_ZIP,
	NEMO_ARCHIVE_TAR_GZ,
	NEMO_ARCHIVE_TAR_XZ,
	NEMO_ARCHIVE_TAR_ZST,
	NEMO_ARCHIVE_7Z,
	NEMO_ARCHIVE_N_FORMATS
} NemoArchiveFormat;

/* Attached to the job's NemoProgressInfo as "dakkar-archive-result"
 * (written by the worker before "finished"); read by the summary. */
typedef struct {
	gchar   *archive_path;   /* final archive, or extraction folder */
	goffset  archive_size;   /* compressed size (compress and extract) */
	goffset  content_size;   /* uncompressed bytes added / extracted */
	gboolean created;        /* the archive / folder exists afterwards */
	gchar   *error;          /* job-level failure, if any */
	gchar   *source_label;   /* folder of the selection / the archive(s) */
	gchar   *encryption;     /* compress: method used ("AES-256", ...), else NULL */
	guint    n_protected;    /* extract: password-protected archives opened */
	gboolean cli_checked;    /* 7-Zip program tested the archive / its CRCs */
	gboolean cli_check_ok;
} NemoArchiveResult;

/* Encryption a format can offer when compressing. */
typedef enum {
	NEMO_ARCHIVE_ENCRYPTION_NONE,      /* format cannot be encrypted here */
	NEMO_ARCHIVE_ENCRYPTION_AES256,    /* zip via libarchive, 7z via the 7z program */
	NEMO_ARCHIVE_ENCRYPTION_ZIPCRYPTO  /* weak legacy zip encryption only */
} NemoArchiveEncryption;

gboolean     nemo_file_operations_archive_supported        (void);
gboolean     nemo_file_operations_archive_format_available (NemoArchiveFormat format);
const gchar *nemo_file_operations_archive_format_extension (NemoArchiveFormat format);
gboolean     nemo_file_operations_is_extractable_name      (const gchar *name);
void nemo_file_operations_compress (GList                *files,
				     GFile                *archive,
				     NemoArchiveFormat     format,
				     GtkWindow            *parent_window,
				     NemoCopyCallback      done_callback,
				     gpointer              done_callback_data);
void nemo_file_operations_extract  (GList                *archives,
				     GFile                *target_dir,
				     GtkWindow            *parent_window,
				     NemoCopyCallback      done_callback,
				     gpointer              done_callback_data);
/* Best encryption for @format; @names_possible: file names can be
 * encrypted too (7z header encryption). */
NemoArchiveEncryption nemo_file_operations_archive_encryption (NemoArchiveFormat format,
                                                               gboolean         *names_possible);
/* Path of the 7-Zip command line program (7z, 7zz or 7za), or NULL. */
gchar *nemo_file_operations_archive_7z_program (void);
/* @password: NULL for no encryption. It is copied, never stored or logged,
 * and the copy is wiped when the job ends. */
void nemo_file_operations_compress_full (GList                *files,
					  GFile                *archive,
					  NemoArchiveFormat     format,
					  const gchar          *password,
					  gboolean              encrypt_names,
					  GtkWindow            *parent_window,
					  NemoCopyCallback      done_callback,
					  gpointer              done_callback_data);
/* @subfolders: each archive into its own new folder (as Extract Here);
 * otherwise straight into @target_dir with the conflict dialog. */
void nemo_file_operations_extract_full (GList                *archives,
					 GFile                *target_dir,
					 gboolean              subfolders,
					 GtkWindow            *parent_window,
					 NemoCopyCallback      done_callback,
					 gpointer              done_callback_data);
/* Overwrite and free a string that held a password. */
void nemo_file_operations_secret_free (gchar *secret);


#endif /* NEMO_FILE_OPERATIONS_H */
