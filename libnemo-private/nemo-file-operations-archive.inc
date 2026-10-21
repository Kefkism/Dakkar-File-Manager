/* nemo-file-operations-archive.inc - Dakkar: Compress and Extract Here.
 *
 * Included at the end of nemo-file-operations.c so the archive jobs reuse
 * the CommonJob machinery (job queue, cancellable, power inhibit, progress
 * info) and the checksum helpers of the copy engine.
 *
 * Archives are written and read in process with libarchive, which gives
 * exact per-file and byte progress (an external zip/tar/7z only reports
 * file names).  Everything the TeraCopy-style window shows comes from the
 * job's NemoProgressInfo: the per-file manifest, byte counts, the hold
 * (pause) primitive, the per-file cancellable (Skip) and the job
 * cancellable (Cancel).
 *
 * Compress: the archive is written to a hidden ".<name>.dakkar-part" sibling
 * and only renamed to its final name when complete, so Cancel or a failure
 * never leaves a partial archive behind and never replaces an existing file.
 * Skipping a file whose data was already partly written cannot be undone in
 * a streaming archive; such entries are removed by one extra copy pass of
 * the archive at the end.  With checksum verification on, every file is
 * hashed while it is read for compression and the finished archive is read
 * back and decompressed: each entry must match its hash.
 *
 * Extract Here: each archive goes into a new folder named after it next to
 * the archive ("name", "name (2)", ...), so nothing existing is overwritten.
 * Absolute paths, ".." components and paths that would leave the folder
 * through a symbolic link are refused.  Cancel removes that new folder.
 * With verification on, every extracted file is hashed while it is written
 * and read back from disk (as for copies).
 */

static const gchar *dakkar_archive_extensions[] = {
	".tar.gz", ".tgz", ".tar.bz2", ".tbz2", ".tbz", ".tar.xz", ".txz",
	".tar.zst", ".tzst", ".tar.lz", ".tar.lzma", ".tlz", ".tar.lz4",
	".zip", ".jar", ".7z", ".tar", ".rar", ".iso", ".cpio", ".xar", ".cab",
	NULL
};

const gchar *
nemo_file_operations_archive_format_extension (NemoArchiveFormat format)
{
	switch (format) {
	case NEMO_ARCHIVE_TAR_GZ:  return ".tar.gz";
	case NEMO_ARCHIVE_TAR_XZ:  return ".tar.xz";
	case NEMO_ARCHIVE_TAR_ZST: return ".tar.zst";
	case NEMO_ARCHIVE_7Z:      return ".7z";
	case NEMO_ARCHIVE_ZIP:
	default:                   return ".zip";
	}
}

/* Length of the archive extension of @name, 0 when it is not an archive. */
static G_GNUC_UNUSED gsize
dakkar_archive_extension_length (const gchar *name)
{
	gchar *lower;
	gsize len = 0;
	guint i;
	if (name == NULL) return 0;
	lower = g_ascii_strdown (name, -1);
	for (i = 0; dakkar_archive_extensions[i] != NULL; i++) {
		if (g_str_has_suffix (lower, dakkar_archive_extensions[i]) &&
		    strlen (lower) > strlen (dakkar_archive_extensions[i])) {
			len = strlen (dakkar_archive_extensions[i]);
			break;
		}
	}
	g_free (lower);
	return len;
}

void
nemo_file_operations_secret_free (gchar *secret)
{
	volatile gchar *p = secret;
	if (secret == NULL) return;
	/* volatile: the compiler may not drop the wipe of memory it frees */
	while (*p != '\0') *p++ = '\0';
	g_free (secret);
}

gchar *
nemo_file_operations_archive_7z_program (void)
{
	static const gchar *names[] = { "7z", "7zz", "7za", NULL };
	guint i;
	for (i = 0; names[i] != NULL; i++) {
		gchar *path = g_find_program_in_path (names[i]);
		if (path != NULL) return path;
	}
	return NULL;
}

#ifdef HAVE_LIBARCHIVE
#include <signal.h>
#include <archive.h>
#include <archive_entry.h>
#include <sys/stat.h>

gboolean
nemo_file_operations_archive_supported (void)
{
	return TRUE;
}

gboolean
nemo_file_operations_is_extractable_name (const gchar *name)
{
	return dakkar_archive_extension_length (name) > 0;
}

/* Only built-in filters count: ARCHIVE_WARN means libarchive would fall back
 * to an external program, which this feature deliberately avoids. */
static gboolean
dakkar_archive_setup_writer (struct archive *a, NemoArchiveFormat format)
{
	int r;
	switch (format) {
	case NEMO_ARCHIVE_ZIP:
		r = archive_write_set_format_zip (a);
		break;
	case NEMO_ARCHIVE_7Z:
		r = archive_write_set_format_7zip (a);
		break;
	default:
		r = archive_write_set_format_pax_restricted (a);
		if (r == ARCHIVE_OK) {
			if (format == NEMO_ARCHIVE_TAR_GZ)
				r = archive_write_add_filter_gzip (a);
			else if (format == NEMO_ARCHIVE_TAR_XZ)
				r = archive_write_add_filter_xz (a);
			else
				r = archive_write_add_filter_zstd (a);
		}
		break;
	}
	return r == ARCHIVE_OK;
}

static gboolean
dakkar_zip_encryption_works (const gchar *method)
{
	struct archive *a = archive_write_new ();
	gchar *option = g_strdup_printf ("zip:encryption=%s", method);
	gboolean ok = archive_write_set_format_zip (a) == ARCHIVE_OK &&
	              archive_write_set_options (a, option) == ARCHIVE_OK;
	g_free (option);
	archive_write_free (a);
	return ok;
}

NemoArchiveEncryption
nemo_file_operations_archive_encryption (NemoArchiveFormat format, gboolean *names_possible)
{
	if (names_possible != NULL) *names_possible = FALSE;
	if (format == NEMO_ARCHIVE_ZIP) {
		if (dakkar_zip_encryption_works ("aes256")) return NEMO_ARCHIVE_ENCRYPTION_AES256;
		if (dakkar_zip_encryption_works ("zipcrypt")) return NEMO_ARCHIVE_ENCRYPTION_ZIPCRYPTO;
		return NEMO_ARCHIVE_ENCRYPTION_NONE;
	}
	if (format == NEMO_ARCHIVE_7Z) {
		/* libarchive cannot write encrypted 7z: the 7-Zip program does */
		gchar *program = nemo_file_operations_archive_7z_program ();
		gboolean ok = program != NULL;
		g_free (program);
		if (ok && names_possible != NULL) *names_possible = TRUE;
		return ok ? NEMO_ARCHIVE_ENCRYPTION_AES256 : NEMO_ARCHIVE_ENCRYPTION_NONE;
	}
	return NEMO_ARCHIVE_ENCRYPTION_NONE;
}

gboolean
nemo_file_operations_archive_format_available (NemoArchiveFormat format)
{
	struct archive *a = archive_write_new ();
	gboolean ok = dakkar_archive_setup_writer (a, format);
	archive_write_free (a);
	return ok;
}

typedef struct {
	GFile    *file;
	gchar    *path;          /* local path of the source */
	gchar    *name;          /* path inside the archive */
	GFileType type;
	goffset   size;
	guint32   mode;
	guint64   mtime;
	gchar    *link_target;
	gchar    *error;         /* could not be read while scanning */
	gchar    *digest;        /* hash taken while compressing */
	gboolean  dropped;       /* data partly written, must be removed */
	gboolean  listed;        /* shown in the transfer manifest */
	gboolean  ended;         /* manifest entry has a final state */
	gboolean  added;         /* completely written into the archive */
} DakkarArcItem;

static void
dakkar_arc_item_free (gpointer data)
{
	DakkarArcItem *it = data;
	g_clear_object (&it->file);
	g_free (it->path);
	g_free (it->name);
	g_free (it->link_target);
	g_free (it->error);
	g_free (it->digest);
	g_free (it);
}

typedef struct {
	CommonJob common;
	GList *files;
	GFile *target;          /* the archive (compress) or folder (extract) */
	NemoArchiveFormat format;
	gboolean verify;
	GChecksumType checksum_type;
	gchar *checksum_name;
	NemoCopyCallback done_callback;
	gpointer done_callback_data;
	GHashTable *debuting_files;
	NemoArchiveResult *result;
	/* password of the archive being written / read: never logged, wiped
	 * with nemo_file_operations_secret_free () when the job ends */
	gchar *password;
	gboolean encrypt_names;      /* 7z: also encrypt the file names */
	const gchar *zip_encryption; /* libarchive option value, or NULL */
	gboolean subfolders;         /* extract: one new folder per archive */
} DakkarArchiveJob;

/* The writer for this job: format, plus zip encryption when a password
 * was given (AES-256, or ZipCrypto only as the explicit fallback). */
static gboolean
dakkar_archive_setup_job_writer (DakkarArchiveJob *job, struct archive *a)
{
	gchar *option;
	gboolean ok;
	if (!dakkar_archive_setup_writer (a, job->format)) return FALSE;
	if (job->password == NULL || job->zip_encryption == NULL) return TRUE;
	option = g_strdup_printf ("zip:encryption=%s", job->zip_encryption);
	ok = archive_write_set_options (a, option) == ARCHIVE_OK &&
	     archive_write_set_passphrase (a, job->password) == ARCHIVE_OK;
	g_free (option);
	return ok;
}


static void
dakkar_archive_result_free (gpointer data)
{
	NemoArchiveResult *r = data;
	g_free (r->archive_path);
	g_free (r->error);
	g_free (r->source_label);
	g_free (r->encryption);
	g_free (r);
}

static gboolean
dakkar_archive_job_done (gpointer user_data)
{
	DakkarArchiveJob *job = user_data;
	if (job->done_callback)
		job->done_callback (job->debuting_files,
		                    !job_aborted ((CommonJob *) job),
		                    job->done_callback_data);
	g_list_free_full (job->files, g_object_unref);
	g_clear_object (&job->target);
	g_hash_table_unref (job->debuting_files);
	g_free (job->checksum_name);
	nemo_file_operations_secret_free (job->password);
	job->password = NULL;
	finalize_common ((CommonJob *) job);
	nemo_file_changes_consume_changes (TRUE);
	return FALSE;
}

/* Bytes for the transfer window plus the legacy fraction, whose
 * "progress-changed" signal also refreshes the status-bar bars. */
static void
dakkar_arc_report (CommonJob *common, goffset done, goffset total)
{
	nemo_progress_info_report_bytes (common->progress, done, total);
	if (total > 0)
		nemo_progress_info_set_progress (common->progress, (double) MIN (done, total), (double) total);
}

static void
dakkar_arc_end (CommonJob *common, DakkarArcItem *it, NemoTransferFileState state,
               const gchar *reason)
{
	if (!it->listed) return;
	nemo_progress_info_file_end (common->progress, it->file, state, reason);
	it->ended = TRUE;
	it->added = state == NEMO_TRANSFER_FILE_COMPLETED;
}

static void
dakkar_arc_scan (DakkarArchiveJob *job, GFile *file, const gchar *name,
                GFile *exclude_a, GFile *exclude_b, GPtrArray *items, goffset *total)
{
	CommonJob *common = &job->common;
	GError *error = NULL;
	GFileInfo *info;
	DakkarArcItem *it;

	if (job_aborted (common)) return;
	/* never put the archive being written into itself */
	if (g_file_equal (file, exclude_a) || g_file_equal (file, exclude_b)) return;
	it = g_new0 (DakkarArcItem, 1);
	it->file = g_object_ref (file);
	it->name = g_strdup (name);
	it->path = g_file_get_path (file);
	g_ptr_array_add (items, it);
	info = g_file_query_info (file,
	                          G_FILE_ATTRIBUTE_STANDARD_TYPE ","
	                          G_FILE_ATTRIBUTE_STANDARD_SIZE ","
	                          G_FILE_ATTRIBUTE_STANDARD_SYMLINK_TARGET ","
	                          G_FILE_ATTRIBUTE_UNIX_MODE ","
	                          G_FILE_ATTRIBUTE_TIME_MODIFIED,
	                          G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
	                          common->cancellable, &error);
	if (info == NULL) {
		it->type = G_FILE_TYPE_REGULAR;
		it->error = g_strdup (error->message);
		g_error_free (error);
		return;
	}
	it->type = g_file_info_get_file_type (info);
	it->size = g_file_info_get_size (info);
	it->mode = g_file_info_get_attribute_uint32 (info, G_FILE_ATTRIBUTE_UNIX_MODE);
	it->mtime = g_file_info_get_attribute_uint64 (info, G_FILE_ATTRIBUTE_TIME_MODIFIED);
	if (it->type == G_FILE_TYPE_SYMBOLIC_LINK)
		it->link_target = g_strdup (g_file_info_get_symlink_target (info));
	g_object_unref (info);

	if (it->path == NULL) {
		it->error = g_strdup (_("Only local files can be added to an archive"));
	} else if (it->type == G_FILE_TYPE_REGULAR) {
		*total += it->size;
	} else if (it->type == G_FILE_TYPE_DIRECTORY) {
		GFileEnumerator *en = g_file_enumerate_children (file,
			G_FILE_ATTRIBUTE_STANDARD_NAME, G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
			common->cancellable, &error);
		GFileInfo *child;
		if (en == NULL) {
			it->error = g_strdup (error->message);
			g_clear_error (&error);
			return;
		}
		while ((child = g_file_enumerator_next_file (en, common->cancellable, &error)) != NULL) {
			GFile *cf = g_file_get_child (file, g_file_info_get_name (child));
			gchar *cn = g_strconcat (name, "/", g_file_info_get_name (child), NULL);
			dakkar_arc_scan (job, cf, cn, exclude_a, exclude_b, items, total);
			g_free (cn);
			g_object_unref (cf);
			g_object_unref (child);
		}
		if (error != NULL) {
			it->error = g_strdup (error->message);
			g_clear_error (&error);
		}
		g_object_unref (en);
	} else if (it->type != G_FILE_TYPE_SYMBOLIC_LINK) {
		it->error = g_strdup (_("Special files cannot be added to an archive"));
	}
}

static struct archive *
dakkar_archive_reader_new (void)
{
	struct archive *r = archive_read_new ();
	archive_read_support_filter_all (r);
	archive_read_support_format_all (r);
	return r;
}

/* Copy the finished archive once more without the entries whose data was
 * only partly written (skipped or unreadable mid-file). */
static gboolean
dakkar_arc_rewrite_without_dropped (DakkarArchiveJob *job, const gchar *part_path,
                                   GPtrArray *items, guint8 *buffer, gchar **error)
{
	GHashTable *drop = g_hash_table_new (g_str_hash, g_str_equal);
	gchar *tmp_path = g_strconcat (part_path, ".2", NULL);
	struct archive *r = dakkar_archive_reader_new ();
	struct archive *w = archive_write_new ();
	if (job->password != NULL) archive_read_add_passphrase (r, job->password);
	struct archive_entry *e;
	gboolean ok = FALSE;
	int rc;
	guint i;

	nemo_progress_info_take_details (job->common.progress,
		g_strdup (_("Removing skipped files from the archive…")));
	for (i = 0; i < items->len; i++) {
		DakkarArcItem *it = g_ptr_array_index (items, i);
		if (it->dropped) g_hash_table_add (drop, it->name);
	}
	if (archive_read_open_filename (r, part_path, 1 << 16) != ARCHIVE_OK ||
	    !dakkar_archive_setup_job_writer (job, w) ||
	    archive_write_open_filename (w, tmp_path) != ARCHIVE_OK) {
		*error = g_strdup (archive_error_string (r) ? archive_error_string (r) :
		                   archive_error_string (w));
		goto out;
	}
	while ((rc = archive_read_next_header (r, &e)) == ARCHIVE_OK || rc == ARCHIVE_WARN) {
		la_ssize_t n;
		if (g_hash_table_contains (drop, archive_entry_pathname (e))) {
			archive_read_data_skip (r);
			continue;
		}
		if (archive_write_header (w, e) < ARCHIVE_WARN) {
			*error = g_strdup (archive_error_string (w));
			goto out;
		}
		while ((n = archive_read_data (r, buffer, DAKKAR_COPY_CHUNK)) > 0) {
			if (archive_write_data (w, buffer, (size_t) n) < 0) {
				*error = g_strdup (archive_error_string (w));
				goto out;
			}
		}
		if (n < 0) {
			*error = g_strdup (archive_error_string (r));
			goto out;
		}
		archive_write_finish_entry (w);
	}
	if (rc != ARCHIVE_EOF) {
		*error = g_strdup (archive_error_string (r));
		goto out;
	}
	if (archive_write_close (w) != ARCHIVE_OK) {
		*error = g_strdup (archive_error_string (w));
		goto out;
	}
	ok = g_rename (tmp_path, part_path) == 0;
	if (!ok) *error = g_strdup (g_strerror (errno));
out:
	archive_read_free (r);
	archive_write_free (w);
	if (!ok) g_unlink (tmp_path);
	if (!ok && *error == NULL) *error = g_strdup (_("Could not write the archive"));
	g_free (tmp_path);
	g_hash_table_unref (drop);
	return ok;
}

/* Read the finished archive back (from disk, page cache dropped) and
 * compare every entry with the hash taken while it was compressed. */
static void
dakkar_arc_verify (DakkarArchiveJob *job, const gchar *path, GPtrArray *items,
                  guint8 *buffer, goffset *done_bytes, goffset grand_total)
{
	CommonJob *common = &job->common;
	GHashTable *pending = g_hash_table_new (g_str_hash, g_str_equal);
	struct archive *r;
	struct archive_entry *e;
	GHashTableIter iter;
	gpointer value;
	gchar *open_error = NULL;
	int rc, fd;
	guint i;

	nemo_progress_info_take_details (common->progress, g_strdup (_("Verifying archive…")));
	for (i = 0; i < items->len; i++) {
		DakkarArcItem *it = g_ptr_array_index (items, i);
		if (it->digest != NULL) g_hash_table_insert (pending, it->name, it);
	}
	fd = g_open (path, O_RDONLY | O_CLOEXEC, 0);
	if (fd >= 0) {
#ifdef POSIX_FADV_DONTNEED
		fdatasync (fd);
		posix_fadvise (fd, 0, 0, POSIX_FADV_DONTNEED);
#endif
		close (fd);
	}
	r = dakkar_archive_reader_new ();
	/* an encrypted archive is read back with its password */
	if (job->password != NULL) archive_read_add_passphrase (r, job->password);
	if (archive_read_open_filename (r, path, 1 << 16) != ARCHIVE_OK) {
		open_error = g_strdup (archive_error_string (r));
	} else {
		while ((rc = archive_read_next_header (r, &e)) == ARCHIVE_OK || rc == ARCHIVE_WARN) {
			DakkarArcItem *it = g_hash_table_lookup (pending, archive_entry_pathname (e));
			GChecksum *sum;
			goffset vd = 0;
			la_ssize_t n;
			if (it == NULL) {
				archive_read_data_skip (r);
				continue;
			}
			g_hash_table_remove (pending, it->name);
			sum = g_checksum_new (job->checksum_type);
			nemo_progress_info_file_verify_progress (common->progress, it->file, 0, it->size);
			while ((n = archive_read_data (r, buffer, DAKKAR_COPY_CHUNK)) > 0) {
				g_checksum_update (sum, buffer, (gsize) n);
				vd += n;
				*done_bytes += n;
				dakkar_test_throttle (n);
				if (!nemo_progress_info_wait_if_held (common->progress)) break;
				nemo_progress_info_file_verify_progress (common->progress, it->file, vd, it->size);
				dakkar_arc_report (common, *done_bytes, grand_total);
			}
			if (job_aborted (common)) {
				g_checksum_free (sum);
				break;
			}
			*done_bytes += MAX (it->size - vd, 0);
			if (n < 0)
				nemo_progress_info_file_verification (common->progress, it->file, FALSE, NULL,
					archive_error_string (r));
			else if (g_strcmp0 (g_checksum_get_string (sum), it->digest) == 0)
				nemo_progress_info_file_verification (common->progress, it->file, TRUE,
					it->digest, NULL);
			else
				nemo_progress_info_file_verification (common->progress, it->file, FALSE, NULL,
					_("checksum mismatch"));
			g_checksum_free (sum);
		}
		if (rc == ARCHIVE_FATAL)
			open_error = g_strdup (archive_error_string (r));
	}
	archive_read_free (r);
	if (!job_aborted (common)) {
		g_hash_table_iter_init (&iter, pending);
		while (g_hash_table_iter_next (&iter, NULL, &value)) {
			DakkarArcItem *it = value;
			nemo_progress_info_file_verification (common->progress, it->file, FALSE, NULL,
				open_error != NULL ? open_error : _("missing from the archive"));
			*done_bytes += it->size;
		}
	}
	dakkar_arc_report (common, *done_bytes, grand_total);
	g_free (open_error);
	g_hash_table_unref (pending);
}

/* Running totals of one compress job, shared with its helpers. */
typedef struct {
	goffset grand_total;     /* bytes to write, twice when verifying */
	goffset done_bytes;
	goffset content;         /* size of the files that made it in */
	guint listed;            /* entries shown in the transfer window */
	guint files_done;
	guint added;
	gboolean any_dropped;    /* some file was skipped after its header */
	gchar *job_error;        /* fatal: no archive is created */
} DakkarCompressRun;

/* Add one regular file. It is opened before the header is written, so an
 * unreadable file never leaves an empty entry behind. A file skipped or
 * cut short after its header is marked dropped and rewritten out later.
 * FALSE when writing the archive itself failed. */
static gboolean
dakkar_compress_add_regular (DakkarArchiveJob *job, DakkarCompressRun *run, struct archive *a,
                             DakkarArcItem *it, struct archive_entry *entry, guint8 *buffer)
{
	CommonJob *common = &job->common;
	GCancellable *fc;
	struct stat st;
	GChecksum *sum = NULL;
	goffset fdone = 0;
	gchar *err = NULL;
	int fd;

	nemo_progress_info_file_begin (common->progress, it->file, job->target);
	nemo_progress_info_take_details (common->progress, g_strdup (it->name));
	fc = nemo_progress_info_begin_file_cancellable (common->progress);
	fd = g_open (it->path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW, 0);
	if (fd < 0 || fstat (fd, &st) != 0) {
		err = g_strdup (g_strerror (errno));
		if (fd >= 0) close (fd);
		nemo_progress_info_end_file_cancellable (common->progress, fc);
		dakkar_arc_end (common, it, NEMO_TRANSFER_FILE_FAILED, err);
		g_free (err);
		run->done_bytes += job->verify ? it->size * 2 : it->size;
		dakkar_arc_report (common, run->done_bytes, run->grand_total);
		nemo_progress_info_report_files (common->progress, ++run->files_done, run->listed);
		archive_entry_free (entry);
		return TRUE;
	}
	if (st.st_size != it->size) {
		/* changed since the scan: keep the totals consistent */
		run->grand_total += (job->verify ? 2 : 1) * (st.st_size - it->size);
		it->size = st.st_size;
	}
	archive_entry_set_filetype (entry, AE_IFREG);
	archive_entry_set_perm (entry, it->mode ? (it->mode & 07777) : 0644);
	archive_entry_set_size (entry, st.st_size);
	if (archive_write_header (a, entry) < ARCHIVE_WARN) {
		run->job_error = g_strdup (archive_error_string (a));
		close (fd);
		archive_entry_free (entry);
		nemo_progress_info_end_file_cancellable (common->progress, fc);
		dakkar_arc_end (common, it, NEMO_TRANSFER_FILE_FAILED, run->job_error);
		return FALSE;
	}
	archive_entry_free (entry);
	if (job->verify) sum = g_checksum_new (job->checksum_type);
	nemo_progress_info_file_progress (common->progress, it->file, 0, st.st_size);
	while (fdone < st.st_size) {
		gssize n;
		if (g_cancellable_is_cancelled (fc)) break;
		if (!nemo_progress_info_wait_if_held (common->progress)) break;
		if (g_cancellable_is_cancelled (fc)) break;
		n = read (fd, buffer, (size_t) MIN ((goffset) DAKKAR_COPY_CHUNK, st.st_size - fdone));
		if (n < 0 && errno == EINTR) continue;
		if (n < 0) { err = g_strdup (g_strerror (errno)); break; }
		if (n == 0) { err = g_strdup (_("the file became shorter while it was read")); break; }
		if (archive_write_data (a, buffer, (size_t) n) < 0) {
			run->job_error = g_strdup (archive_error_string (a));
			break;
		}
		if (sum != NULL) g_checksum_update (sum, buffer, (gsize) n);
		fdone += n;
		run->done_bytes += n;
		dakkar_test_throttle (n);
		nemo_progress_info_file_progress (common->progress, it->file, fdone, st.st_size);
		dakkar_arc_report (common, run->done_bytes, run->grand_total);
	}
	close (fd);
	if (run->job_error == NULL && archive_write_finish_entry (a) < ARCHIVE_WARN)
		run->job_error = g_strdup (archive_error_string (a));
	{
		gboolean user_skip = nemo_progress_info_end_file_cancellable (common->progress, fc);
		if (run->job_error != NULL) {
			dakkar_arc_end (common, it, NEMO_TRANSFER_FILE_FAILED, run->job_error);
		} else if (job_aborted (common)) {
			dakkar_arc_end (common, it, NEMO_TRANSFER_FILE_CANCELLED, NULL);
		} else if (fdone < st.st_size) {
			it->dropped = TRUE;
			run->any_dropped = TRUE;
			run->done_bytes += st.st_size - fdone;
			if (err != NULL)
				dakkar_arc_end (common, it, NEMO_TRANSFER_FILE_FAILED, err);
			else
				dakkar_arc_end (common, it, NEMO_TRANSFER_FILE_SKIPPED,
				               user_skip ? _("skipped by user") : NULL);
			if (job->verify) run->done_bytes += st.st_size;
		} else {
			if (sum != NULL) it->digest = g_strdup (g_checksum_get_string (sum));
			else if (job->verify) run->done_bytes += st.st_size;
			dakkar_arc_end (common, it, NEMO_TRANSFER_FILE_COMPLETED, NULL);
			run->added++;
			run->content += st.st_size;
		}
	}
	if (sum != NULL) g_checksum_free (sum);
	g_free (err);
	dakkar_arc_report (common, run->done_bytes, run->grand_total);
	nemo_progress_info_report_files (common->progress, ++run->files_done, run->listed);
	return TRUE;
}

/* Close every listed entry that has no final state yet, and fail the ones
 * reported as added when no archive came out after all. */
static void
dakkar_compress_report_unfinished (DakkarArchiveJob *job, DakkarCompressRun *run, GPtrArray *items)
{
	CommonJob *common = &job->common;
	guint i;

for (i = 0; i < items->len; i++) {
	DakkarArcItem *it = g_ptr_array_index (items, i);
	if (!it->listed) continue;
	if (!it->ended)
		nemo_progress_info_file_end (common->progress, it->file,
			job_aborted (common) ? NEMO_TRANSFER_FILE_CANCELLED : NEMO_TRANSFER_FILE_FAILED,
			job_aborted (common) ? NULL : run->job_error);
	else if (it->added && !job->result->created)
		/* no archive was created: "run->added" entries are not in one either */
		nemo_progress_info_file_end (common->progress, it->file,
			job_aborted (common) ? NEMO_TRANSFER_FILE_CANCELLED : NEMO_TRANSFER_FILE_FAILED,
			job_aborted (common) ? NULL : run->job_error);
}
}

static gboolean
dakkar_compress_job (GIOSchedulerJob *io_job, GCancellable *cancellable, gpointer user_data)
{
	DakkarArchiveJob *job = user_data;
	CommonJob *common = &job->common;
	GPtrArray *items = g_ptr_array_new_with_free_func (dakkar_arc_item_free);
	gchar *final_path = g_file_get_path (job->target);
	gchar *dir = g_path_get_dirname (final_path);
	gchar *base = g_path_get_basename (final_path);
	gchar *part_base = g_strdup_printf (".%s.dakkar-part", base);
	gchar *part_path = g_build_filename (dir, part_base, NULL);
	GFile *part_file = g_file_new_for_path (part_path);
	struct archive *a = NULL;
	guint8 *buffer = g_malloc (DAKKAR_COPY_CHUNK);
	goffset total = 0;
	guint wanted = 0, i;
	DakkarCompressRun run = { 0, 0, 0, 0, 0, 0, FALSE, NULL };
	GList *l;

	common->io_job = io_job;
	nemo_progress_info_start (common->progress);
	if (job->verify)
		nemo_progress_info_set_checksum_name (common->progress, job->checksum_name);
	/* Translators: %s is the name of the archive being created */
	nemo_progress_info_take_status (common->progress,
		g_strdup_printf (_("Compressing to “%s”"), base));
	nemo_progress_info_take_details (common->progress, g_strdup (_("Preparing")));

	for (l = job->files; l != NULL; l = l->next) {
		gchar *name = g_file_get_basename (l->data);
		dakkar_arc_scan (job, l->data, name, job->target, part_file, items, &total);
		g_free (name);
	}
	if (job_aborted (common)) goto cleanup;
	for (i = 0; i < items->len; i++) {
		DakkarArcItem *it = g_ptr_array_index (items, i);
		if (it->error != NULL || it->type == G_FILE_TYPE_REGULAR ||
		    it->type == G_FILE_TYPE_SYMBOLIC_LINK) {
			it->listed = TRUE;
			run.listed++;
			nemo_progress_info_file_queue (common->progress, it->file, job->target);
		}
		if (it->error == NULL && (it->type == G_FILE_TYPE_REGULAR ||
		                          it->type == G_FILE_TYPE_SYMBOLIC_LINK))
			wanted++;
	}
	run.grand_total = job->verify ? total * 2 : total;
	nemo_progress_info_report_files (common->progress, 0, run.listed);
	dakkar_arc_report (common, 0, run.grand_total);

	if (g_file_test (final_path, G_FILE_TEST_EXISTS) ||
	    g_file_test (final_path, G_FILE_TEST_IS_SYMLINK)) {
		/* Translators: %s is a file name */
		run.job_error = g_strdup_printf (_("“%s” already exists"), base);
		goto cleanup;
	}
	a = archive_write_new ();
	if (!dakkar_archive_setup_job_writer (job, a) ||
	    archive_write_open_filename (a, part_path) != ARCHIVE_OK) {
		run.job_error = g_strdup (archive_error_string (a) != NULL ? archive_error_string (a) :
		                      _("Could not create the archive"));
		goto cleanup;
	}
	g_timer_start (common->time);

	for (i = 0; i < items->len && run.job_error == NULL; i++) {
		DakkarArcItem *it = g_ptr_array_index (items, i);
		struct archive_entry *entry;

		if (!nemo_progress_info_wait_if_held (common->progress) || job_aborted (common))
			break;
		if (it->error != NULL) {
			nemo_progress_info_file_begin (common->progress, it->file, job->target);
			dakkar_arc_end (common, it, NEMO_TRANSFER_FILE_FAILED, it->error);
			run.done_bytes += job->verify ? it->size * 2 : it->size;
			nemo_progress_info_report_files (common->progress, ++run.files_done, run.listed);
			continue;
		}
		entry = archive_entry_new ();
		/* record the creating user, like tar/zip do, instead of root (0/0) */
		archive_entry_set_uid (entry, getuid ());
		archive_entry_set_gid (entry, getgid ());
		archive_entry_copy_uname (entry, g_get_user_name ());
		archive_entry_copy_pathname (entry, it->name);
		archive_entry_set_mtime (entry, (time_t) it->mtime, 0);
		if (it->type == G_FILE_TYPE_DIRECTORY) {
			archive_entry_set_filetype (entry, AE_IFDIR);
			archive_entry_set_perm (entry, it->mode ? (it->mode & 07777) : 0755);
			if (archive_write_header (a, entry) < ARCHIVE_WARN)
				run.job_error = g_strdup (archive_error_string (a));
			archive_entry_free (entry);
			continue;
		}
		if (it->type == G_FILE_TYPE_SYMBOLIC_LINK) {
			nemo_progress_info_file_begin (common->progress, it->file, job->target);
			archive_entry_set_filetype (entry, AE_IFLNK);
			archive_entry_set_perm (entry, 0777);
			archive_entry_copy_symlink (entry, it->link_target != NULL ? it->link_target : "");
			if (archive_write_header (a, entry) < ARCHIVE_WARN) {
				dakkar_arc_end (common, it, NEMO_TRANSFER_FILE_FAILED, archive_error_string (a));
			} else {
				dakkar_arc_end (common, it, NEMO_TRANSFER_FILE_COMPLETED, NULL);
				run.added++;
			}
			archive_entry_free (entry);
			nemo_progress_info_report_files (common->progress, ++run.files_done, run.listed);
			continue;
		}

		if (!dakkar_compress_add_regular (job, &run, a, it, entry, buffer))
			break;
	}
	if (job_aborted (common) || run.job_error != NULL) goto cleanup;

	nemo_progress_info_take_details (common->progress, g_strdup (_("Finishing archive…")));
	if (archive_write_close (a) != ARCHIVE_OK) {
		run.job_error = g_strdup (archive_error_string (a));
		goto cleanup;
	}
	archive_write_free (a);
	a = NULL;
	if (wanted > 0 && run.added == 0) {
		run.job_error = g_strdup (_("No file could be run.added, so no archive was created"));
		goto cleanup;
	}
	if (run.any_dropped &&
	    !dakkar_arc_rewrite_without_dropped (job, part_path, items, buffer, &run.job_error))
		goto cleanup;
	{
		int fd = g_open (part_path, O_RDONLY | O_CLOEXEC, 0);
		if (fd >= 0) {
			fsync (fd);
			close (fd);
		}
	}
	if (job->verify)
		dakkar_arc_verify (job, part_path, items, buffer, &run.done_bytes, run.grand_total);
	if (job_aborted (common)) goto cleanup;
	if (g_file_test (final_path, G_FILE_TEST_EXISTS) ||
	    g_file_test (final_path, G_FILE_TEST_IS_SYMLINK)) {
		run.job_error = g_strdup_printf (_("“%s” already exists"), base);
		goto cleanup;
	}
	if (g_rename (part_path, final_path) != 0) {
		run.job_error = g_strdup (g_strerror (errno));
		goto cleanup;
	}
	job->result->created = TRUE;
	if (job->password != NULL)
		job->result->encryption = g_strdup (g_strcmp0 (job->zip_encryption, "aes256") == 0 ?
			_("AES-256 (password)") : _("ZipCrypto (password, weak)"));
	nemo_file_changes_queue_file_added (job->target);
	g_hash_table_add (job->debuting_files, g_object_ref (job->target));

cleanup:
	if (a != NULL) archive_write_free (a);
	if (!job->result->created) {
		gchar *second = g_strconcat (part_path, ".2", NULL);
		g_unlink (part_path);
		g_unlink (second);
		g_free (second);
	}
	dakkar_compress_report_unfinished (job, &run, items);
	{
		GStatBuf st;
		job->result->archive_path = g_strdup (final_path);
		if (job->result->created && g_stat (final_path, &st) == 0)
			job->result->archive_size = st.st_size;
		job->result->content_size = run.content;
		job->result->error = job_aborted (common) ? NULL : g_strdup (run.job_error);
	}
	dakkar_arc_report (common,
		job->result->created ? MAX (run.done_bytes, run.grand_total) : run.done_bytes, run.grand_total);
	g_ptr_array_unref (items);
	g_free (buffer);
	g_free (final_path);
	g_free (dir);
	g_free (base);
	g_free (part_base);
	g_free (part_path);
	g_object_unref (part_file);
	g_free (run.job_error);
	g_io_scheduler_job_send_to_mainloop_async (io_job, dakkar_archive_job_done, job, NULL);
	(void) cancellable;
	return FALSE;
}

/* ---------- the 7-Zip program (encrypted 7z) ----------
 *
 * libarchive can neither write encrypted 7z archives nor read encrypted
 * 7z/RAR ones; those go through the 7z program when it is installed.
 * The password is written to its standard input (never on the command
 * line, where other users could see it in the process list), the
 * buffer is wiped afterwards, and the program runs in the C locale so
 * its messages can be recognised. */

typedef void (*Dakkar7zLineFunc) (const gchar *line, gpointer data);

typedef struct {
	gboolean started;
	gint     exit_status;     /* 0 ok, 1 warning, 2+ error, -1 killed */
	gboolean wrong_password;
	gchar   *errors;          /* "ERROR:" / "WARNING:" lines, or NULL */
	GPtrArray *error_lines;   /* the same, one per entry */
} Dakkar7zRun;

static void
dakkar_7z_run_clear (Dakkar7zRun *run)
{
	g_free (run->errors);
	if (run->error_lines != NULL) g_ptr_array_unref (run->error_lines);
	memset (run, 0, sizeof *run);
}

static void
dakkar_7z_force_exit (GCancellable *cancellable, gpointer proc)
{
	(void) cancellable;
	g_subprocess_force_exit (G_SUBPROCESS (proc));
}

static void
dakkar_7z_line (Dakkar7zRun *run, GString *line, gboolean *in_error,
               Dakkar7zLineFunc on_line, gpointer data)
{
	gchar *text = g_strstrip (g_strdup (line->str));
	if (*text != '\0') {
		if (strstr (text, "Wrong password") != NULL)
			run->wrong_password = TRUE;
		if (g_str_has_prefix (text, "ERROR:") || g_str_has_prefix (text, "WARNING:") ||
		    g_str_has_prefix (text, "Can not open") || g_str_has_prefix (text, "Cannot open") ||
		    *in_error) {
			if (run->error_lines->len < 200)
				g_ptr_array_add (run->error_lines, g_strdup (text));
			/* a bare "ERROR:" line is followed by its message */
			*in_error = g_str_equal (text, "ERROR:") || g_str_equal (text, "WARNING:");
		}
		if (on_line != NULL) on_line (text, data);
	}
	g_free (text);
	g_string_truncate (line, 0);
}

static void
dakkar_7z_run (DakkarArchiveJob *job, const gchar * const *argv, const gchar *cwd,
              const gchar *password, Dakkar7zLineFunc on_line, gpointer data, Dakkar7zRun *run)
{
	CommonJob *common = &job->common;
	GSubprocessLauncher *launcher;
	GSubprocess *proc;
	GError *error = NULL;
	GString *line;
	GInputStream *out;
	gboolean in_error = FALSE;
	gulong handler;
	gchar buf[4096];
	gssize n;
	const gchar *slow = g_getenv ("DAKKAR_TEST_7Z_PAUSE_MS");
	gint slow_ms = slow != NULL ? atoi (slow) : 0;

	memset (run, 0, sizeof *run);
	run->exit_status = -1;
	run->error_lines = g_ptr_array_new_with_free_func (g_free);
	launcher = g_subprocess_launcher_new (G_SUBPROCESS_FLAGS_STDIN_PIPE |
	                                      G_SUBPROCESS_FLAGS_STDOUT_PIPE |
	                                      G_SUBPROCESS_FLAGS_STDERR_MERGE);
	g_subprocess_launcher_setenv (launcher, "LC_ALL", "C.UTF-8", TRUE);
	g_subprocess_launcher_unsetenv (launcher, "LANGUAGE");
	if (cwd != NULL) g_subprocess_launcher_set_cwd (launcher, cwd);
	proc = g_subprocess_launcher_spawnv (launcher, argv, &error);
	g_object_unref (launcher);
	if (proc == NULL) {
		run->errors = g_strdup (error->message);
		g_error_free (error);
		return;
	}
	run->started = TRUE;
	{
		GOutputStream *in = g_subprocess_get_stdin_pipe (proc);
		if (password != NULL) {
			gchar *secret = g_strconcat (password, "\n", NULL);
			g_output_stream_write_all (in, secret, strlen (secret), NULL, NULL, NULL);
			nemo_file_operations_secret_free (secret);
		}
		g_output_stream_close (in, NULL, NULL);
	}
	handler = g_cancellable_connect (common->cancellable, G_CALLBACK (dakkar_7z_force_exit),
	                                 g_object_ref (proc), g_object_unref);
	line = g_string_new (NULL);
	out = g_subprocess_get_stdout_pipe (proc);
	while ((n = g_input_stream_read (out, buf, sizeof buf, NULL, NULL)) > 0) {
		gssize i;
		for (i = 0; i < n; i++) {
			if (buf[i] == '\b' || buf[i] == '\r' || buf[i] == '\n')
				dakkar_7z_line (run, line, &in_error, on_line, data);
			else
				g_string_append_c (line, buf[i]);
		}
		if (slow_ms > 0 && !job_aborted (common)) {
			/* test hook: slow the program down to test Pause/Cancel */
			const gchar *id = g_subprocess_get_identifier (proc);
			pid_t pid = id != NULL ? (pid_t) atoi (id) : 0;
			if (pid > 0) kill (pid, SIGSTOP);
			g_usleep ((gulong) slow_ms * 1000);
			if (pid > 0) kill (pid, SIGCONT);
		}
		if (nemo_progress_info_hold_requested (common->progress, NULL) &&
		    !job_aborted (common)) {
			/* Pause: stop the program itself while the transfer is held */
			const gchar *id = g_subprocess_get_identifier (proc);
			pid_t pid = id != NULL ? (pid_t) atoi (id) : 0;
			if (pid > 0) kill (pid, SIGSTOP);
			nemo_progress_info_wait_if_held (common->progress);
			if (pid > 0) kill (pid, SIGCONT);
		}
	}
	dakkar_7z_line (run, line, &in_error, on_line, data);
	g_string_free (line, TRUE);
	g_cancellable_disconnect (common->cancellable, handler);
	g_subprocess_wait (proc, NULL, NULL);
	if (g_subprocess_get_if_exited (proc))
		run->exit_status = g_subprocess_get_exit_status (proc);
	g_object_unref (proc);
	if (run->error_lines->len > 0) {
		GString *all = g_string_new (NULL);
		guint i;
		for (i = 0; i < run->error_lines->len && i < 6; i++) {
			const gchar *t = g_ptr_array_index (run->error_lines, i);
			if (g_str_equal (t, "ERROR:") || g_str_equal (t, "WARNING:")) continue;
			if (all->len > 0) g_string_append (all, "; ");
			g_string_append (all, t);
		}
		run->errors = g_string_free (all, all->len == 0);
	}
}

/* A short reason for the summary from a failed 7z run. */
static gchar *
dakkar_7z_reason (const Dakkar7zRun *run, const gchar *fallback)
{
	if (run->wrong_password) return g_strdup (_("wrong password"));
	if (run->errors != NULL) return g_strdup (run->errors);
	return g_strdup (fallback);
}

/* "  42% 7 + name" style progress: the percentage, or -1 */
static gint
dakkar_7z_percent (const gchar *line)
{
	const gchar *p = line;
	gint value = 0, digits = 0;
	while (g_ascii_isdigit (*p) && digits < 4) {
		value = value * 10 + (*p - '0');
		p++;
		digits++;
	}
	return (digits > 0 && *p == '%') ? MIN (value, 100) : -1;
}

typedef struct {
	DakkarArchiveJob *job;
	GHashTable *by_name;     /* name in archive → DakkarArcItem */
	DakkarArcItem *current;
	goffset current_start;
	goffset base, span, grand, done, content;
	guint files_done, listed, added;
} Dakkar7zCompress;

static void
dakkar_7z_compress_finish_current (Dakkar7zCompress *c)
{
	CommonJob *common = &c->job->common;
	if (c->current == NULL) return;
	if (!c->current->ended) {
		dakkar_arc_end (common, c->current, NEMO_TRANSFER_FILE_COMPLETED, NULL);
		c->added++;
		c->content += c->current->size;
		nemo_progress_info_report_files (common->progress, ++c->files_done, c->listed);
	}
	c->current = NULL;
}

static void
dakkar_7z_compress_line (const gchar *line, gpointer data)
{
	Dakkar7zCompress *c = data;
	CommonJob *common = &c->job->common;
	gint pct;
	if (g_str_has_prefix (line, "+ ")) {
		DakkarArcItem *it = g_hash_table_lookup (c->by_name, line + 2);
		dakkar_7z_compress_finish_current (c);
		if (it != NULL && it->listed && !it->ended) {
			c->current = it;
			c->current_start = c->done;
			nemo_progress_info_file_begin (common->progress, it->file, c->job->target);
			nemo_progress_info_take_details (common->progress, g_strdup (it->name));
			nemo_progress_info_file_progress (common->progress, it->file, 0, it->size);
		}
		return;
	}
	pct = dakkar_7z_percent (line);
	if (pct >= 0) {
		c->done = c->base + c->span * pct / 100;
		if (c->current != NULL)
			nemo_progress_info_file_progress (common->progress, c->current->file,
				CLAMP (c->done - c->current_start, 0, c->current->size), c->current->size);
		dakkar_arc_report (common, c->done, c->grand);
	}
}

/* Writes the names to add, relative to the folder of the selection, to a
 * temporary list file for 7z's @listfile argument. Returns an error message
 * or NULL. */
static gchar *
dakkar_7z_write_list (GPtrArray *items, gchar **list_path)
{
	GString *list = g_string_new (NULL);
	GError *error = NULL;
	gchar *job_error;
	guint i;
	gint fd;

	for (i = 0; i < items->len; i++) {
		DakkarArcItem *it = g_ptr_array_index (items, i);
		gboolean add = FALSE;
		if (it->error != NULL) continue;
		if (it->type == G_FILE_TYPE_REGULAR || it->type == G_FILE_TYPE_SYMBOLIC_LINK)
			add = TRUE;
		else if (it->type == G_FILE_TYPE_DIRECTORY) {
			/* only empty folders: the others come with their contents */
			DakkarArcItem *next = i + 1 < items->len ? g_ptr_array_index (items, i + 1) : NULL;
			add = next == NULL || !(g_str_has_prefix (next->name, it->name) &&
			                        next->name[strlen (it->name)] == '/');
		}
		if (add) {
			g_string_append (list, it->name);
			g_string_append_c (list, '\n');
		}
	}
	fd = g_file_open_tmp ("dakkar-7z-XXXXXX.lst", list_path, &error);
	if (fd < 0 || write (fd, list->str, list->len) != (gssize) list->len) {
		job_error = g_strdup (error != NULL ? error->message : g_strerror (errno));
		g_clear_error (&error);
		if (fd >= 0) close (fd);
		g_string_free (list, TRUE);
		return job_error;
	}
	close (fd);
	g_string_free (list, TRUE);
	return NULL;
}

/* Runs "7z a" on the list file, writing the encrypted archive to part_path. */
static void
dakkar_7z_compress_run (DakkarArchiveJob *job, const gchar *program, const gchar *part_path,
                        const gchar *list_path, const gchar *cwd,
                        Dakkar7zCompress *c, Dakkar7zRun *run)
{
	gchar *listarg = g_strconcat ("@", list_path, NULL);
	gchar *twice;
	const gchar *argv[16];
	guint k = 0;

	argv[k++] = program;
	argv[k++] = "a";
	argv[k++] = "-t7z";
	argv[k++] = "-bsp1";
	argv[k++] = "-bb1";
	argv[k++] = "-snl";      /* store symbolic links as links */
	argv[k++] = "-spd";      /* names are names, not wildcards */
	argv[k++] = "-p";        /* ask for the password (read from stdin) */
	if (job->encrypt_names) argv[k++] = "-mhe=on";
	/* both paths are absolute, so neither can be taken for a switch
	 * ("--" would also turn the list file into a plain name) */
	argv[k++] = part_path;
	argv[k++] = listarg;
	argv[k] = NULL;
	/* "-p" without a value makes 7-Zip ask for the password; some
	 * versions ask a second time to verify it */
	twice = g_strconcat (job->password, "\n", job->password, NULL);
	dakkar_7z_run (job, argv, cwd, twice, dakkar_7z_compress_line, c, run);
	nemo_file_operations_secret_free (twice);
	g_free (listarg);
}

/* 7-Zip tests the new archive with the password: every file is decrypted
 * and its CRC32 compared. Returns an error message, or NULL when the test
 * passed or the job was cancelled. */
static gchar *
dakkar_7z_compress_test (DakkarArchiveJob *job, const gchar *program, const gchar *part_path,
                         Dakkar7zCompress *c, goffset total)
{
	CommonJob *common = &job->common;
	const gchar *argv[] = { program, "t", "-bsp1", part_path, NULL };
	gchar *job_error = NULL;
	Dakkar7zRun test;

	nemo_progress_info_take_details (common->progress, g_strdup (_("Testing the archive…")));
	c->base = total;
	c->current = NULL;
	dakkar_7z_run (job, argv, NULL, job->password, dakkar_7z_compress_line, c, &test);
	if (job_aborted (common)) {
		dakkar_7z_run_clear (&test);
		return NULL;
	}
	job->result->cli_checked = TRUE;
	job->result->cli_check_ok = test.exit_status == 0 && !test.wrong_password;
	if (!job->result->cli_check_ok) {
		gchar *why = dakkar_7z_reason (&test, _("the test failed"));
		job_error = g_strdup_printf (_("The new archive failed 7-Zip’s integrity test: %s"), why);
		g_free (why);
	}
	dakkar_7z_run_clear (&test);
	return job_error;
}

static gboolean
dakkar_compress_7z_job (GIOSchedulerJob *io_job, GCancellable *cancellable, gpointer user_data)
{
	DakkarArchiveJob *job = user_data;
	CommonJob *common = &job->common;
	GPtrArray *items = g_ptr_array_new_with_free_func (dakkar_arc_item_free);
	gchar *final_path = g_file_get_path (job->target);
	gchar *dir = g_path_get_dirname (final_path);
	gchar *base = g_path_get_basename (final_path);
	gchar *part_base = g_strdup_printf (".%s.dakkar-part.7z", base);
	gchar *part_path = g_build_filename (dir, part_base, NULL);
	GFile *part_file = g_file_new_for_path (part_path);
	gchar *program = nemo_file_operations_archive_7z_program ();
	gchar *list_path = NULL, *cwd = NULL, *job_error = NULL;
	Dakkar7zCompress c;
	Dakkar7zRun run;
	goffset total = 0;
	guint wanted = 0, i;
	GList *l;

	memset (&c, 0, sizeof c);
	memset (&run, 0, sizeof run);
	c.job = job;
	c.by_name = g_hash_table_new (g_str_hash, g_str_equal);
	common->io_job = io_job;
	nemo_progress_info_start (common->progress);
	nemo_progress_info_take_status (common->progress,
		g_strdup_printf (_("Compressing to “%s”"), base));
	nemo_progress_info_take_details (common->progress, g_strdup (_("Preparing")));

	for (l = job->files; l != NULL; l = l->next) {
		gchar *name = g_file_get_basename (l->data);
		dakkar_arc_scan (job, l->data, name, job->target, part_file, items, &total);
		g_free (name);
	}
	if (job_aborted (common)) goto cleanup;
	for (i = 0; i < items->len; i++) {
		DakkarArcItem *it = g_ptr_array_index (items, i);
		/* 7-Zip would stop on an unreadable file: leave those out */
		if (it->error == NULL && it->type == G_FILE_TYPE_REGULAR &&
		    g_access (it->path, R_OK) != 0)
			it->error = g_strdup (g_strerror (errno));
		if (it->error != NULL || it->type == G_FILE_TYPE_REGULAR ||
		    it->type == G_FILE_TYPE_SYMBOLIC_LINK) {
			it->listed = TRUE;
			c.listed++;
			nemo_progress_info_file_queue (common->progress, it->file, job->target);
		}
		if (it->error == NULL && (it->type == G_FILE_TYPE_REGULAR ||
		                          it->type == G_FILE_TYPE_SYMBOLIC_LINK))
			wanted++;
		g_hash_table_insert (c.by_name, it->name, it);
	}
	c.span = total;
	c.grand = job->verify ? total * 2 : total;
	nemo_progress_info_report_files (common->progress, 0, c.listed);
	dakkar_arc_report (common, 0, c.grand);

	if (g_file_test (final_path, G_FILE_TEST_EXISTS) ||
	    g_file_test (final_path, G_FILE_TEST_IS_SYMLINK)) {
		job_error = g_strdup_printf (_("“%s” already exists"), base);
		goto cleanup;
	}
	if (program == NULL) {
		job_error = g_strdup (_("Encrypted 7z archives need the 7z program (package 7zip or p7zip-full)"));
		goto cleanup;
	}
	/* the 7z program tests the archive (CRC), not a SHA-1 read-back */
	g_object_set_data (G_OBJECT (common->progress), "dakkar-verify-method",
	                   GINT_TO_POINTER (DAKKAR_VERIFY_METHOD_CRC7Z));
	cwd = g_file_get_path (job->files->data);
	{
		gchar *tmp = g_path_get_dirname (cwd);
		g_free (cwd);
		cwd = tmp;
	}
	job_error = dakkar_7z_write_list (items, &list_path);
	if (job_error != NULL)
		goto cleanup;
	g_unlink (part_path);
	g_timer_start (common->time);
	dakkar_7z_compress_run (job, program, part_path, list_path, cwd, &c, &run);
	dakkar_7z_compress_finish_current (&c);
	if (job_aborted (common)) goto cleanup;
	if (!run.started || run.exit_status < 0 || run.exit_status >= 2) {
		job_error = dakkar_7z_reason (&run, _("7-Zip could not create the archive"));
		goto cleanup;
	}
	/* anything 7-Zip did not report as added */
	for (i = 0; i < items->len; i++) {
		DakkarArcItem *it = g_ptr_array_index (items, i);
		if (it->listed && !it->ended) {
			nemo_progress_info_file_begin (common->progress, it->file, job->target);
			dakkar_arc_end (common, it, NEMO_TRANSFER_FILE_FAILED,
				it->error != NULL ? it->error :
				run.errors != NULL ? run.errors : _("not added by 7-Zip"));
			nemo_progress_info_report_files (common->progress, ++c.files_done, c.listed);
		}
	}
	if (wanted > 0 && c.added == 0) {
		job_error = g_strdup (_("No file could be added, so no archive was created"));
		goto cleanup;
	}
	{
		int fd = g_open (part_path, O_RDONLY | O_CLOEXEC, 0);
		if (fd >= 0) {
			fsync (fd);
			close (fd);
		}
	}
	if (job->verify) {
		job_error = dakkar_7z_compress_test (job, program, part_path, &c, total);
		if (job_aborted (common) || job_error != NULL)
			goto cleanup;
	}
	if (g_file_test (final_path, G_FILE_TEST_EXISTS) ||
	    g_file_test (final_path, G_FILE_TEST_IS_SYMLINK)) {
		job_error = g_strdup_printf (_("“%s” already exists"), base);
		goto cleanup;
	}
	if (g_rename (part_path, final_path) != 0) {
		job_error = g_strdup (g_strerror (errno));
		goto cleanup;
	}
	job->result->created = TRUE;
	job->result->encryption = g_strdup (job->encrypt_names ?
		_("AES-256 (password), file names encrypted") : _("AES-256 (password)"));
	nemo_file_changes_queue_file_added (job->target);
	g_hash_table_add (job->debuting_files, g_object_ref (job->target));

cleanup:
	if (!job->result->created)
		g_unlink (part_path);
	if (list_path != NULL) {
		g_unlink (list_path);
		g_free (list_path);
	}
	for (i = 0; i < items->len; i++) {
		DakkarArcItem *it = g_ptr_array_index (items, i);
		if (!it->listed) continue;
		if (!it->ended || (it->added && !job->result->created))
			nemo_progress_info_file_end (common->progress, it->file,
				job_aborted (common) ? NEMO_TRANSFER_FILE_CANCELLED : NEMO_TRANSFER_FILE_FAILED,
				job_aborted (common) ? NULL : job_error);
	}
	{
		GStatBuf st;
		job->result->archive_path = g_strdup (final_path);
		if (job->result->created && g_stat (final_path, &st) == 0)
			job->result->archive_size = st.st_size;
		job->result->content_size = c.content;
		job->result->error = job_aborted (common) ? NULL : g_strdup (job_error);
	}
	dakkar_arc_report (common, job->result->created ? c.grand : c.done, c.grand);
	dakkar_7z_run_clear (&run);
	g_hash_table_destroy (c.by_name);
	g_ptr_array_unref (items);
	g_free (program);
	g_free (cwd);
	g_free (final_path);
	g_free (dir);
	g_free (base);
	g_free (part_base);
	g_free (part_path);
	g_object_unref (part_file);
	g_free (job_error);
	g_io_scheduler_job_send_to_mainloop_async (io_job, dakkar_archive_job_done, job, NULL);
	(void) cancellable;
	return FALSE;
}

/* ---------- extraction ---------- */

/* A relative, normalised path inside the archive, or NULL when unsafe. */
static gchar *
dakkar_arc_sanitize (const gchar *pathname)
{
	gchar **parts;
	GPtrArray *keep;
	gchar *result = NULL;
	guint i;
	if (pathname == NULL || *pathname == '\0' || g_path_is_absolute (pathname))
		return NULL;
	parts = g_strsplit (pathname, "/", -1);
	keep = g_ptr_array_new ();
	for (i = 0; parts[i] != NULL; i++) {
		if (parts[i][0] == '\0' || g_str_equal (parts[i], ".")) continue;
		if (g_str_equal (parts[i], "..")) goto out;
		g_ptr_array_add (keep, parts[i]);
	}
	if (keep->len > 0) {
		g_ptr_array_add (keep, NULL);
		result = g_strjoinv ("/", (gchar **) keep->pdata);
	}
out:
	g_ptr_array_unref (keep);
	g_strfreev (parts);
	return result;
}

/* TRUE when @dir (an existing directory) really lies inside @root. */
static gboolean
dakkar_arc_inside (const gchar *root_real, const gchar *dir)
{
	char *real = realpath (dir, NULL);
	gboolean ok = real != NULL &&
		(g_str_equal (real, root_real) ||
		 (g_str_has_prefix (real, root_real) && real[strlen (root_real)] == '/'));
	free (real);
	return ok;
}

static void
dakkar_arc_remove_tree (const gchar *path)
{
	GDir *d = g_dir_open (path, 0, NULL);
	const gchar *name;
	if (d != NULL) {
		while ((name = g_dir_read_name (d)) != NULL) {
			gchar *child = g_build_filename (path, name, NULL);
			if (g_file_test (child, G_FILE_TEST_IS_DIR) &&
			    !g_file_test (child, G_FILE_TEST_IS_SYMLINK))
				dakkar_arc_remove_tree (child);
			else
				g_unlink (child);
			g_free (child);
		}
		g_dir_close (d);
	}
	g_rmdir (path);
}

/* An archive whose contents are one single item (typically "name/")
 * would otherwise land as "name/name". When that item's name is still
 * free next to the archive, move it up and drop the wrapper folder.
 * Returns the new path, or NULL when the wrapper is kept. */
static gboolean
dakkar_arc_move_path (const gchar *from, const gchar *to)
{
	/* without OVERWRITE g_file_move refuses an existing target; on one
	 * file system it is a plain rename */
	GFile *a = g_file_new_for_path (from), *b = g_file_new_for_path (to);
	gboolean ok = g_file_move (a, b, G_FILE_COPY_NOFOLLOW_SYMLINKS | G_FILE_COPY_NO_FALLBACK_FOR_MOVE,
	                           NULL, NULL, NULL, NULL);
	g_object_unref (a);
	g_object_unref (b);
	return ok;
}

static gchar *
dakkar_arc_lift_single (const gchar *folder, const gchar *target_dir)
{
	GDir *dir = g_dir_open (folder, 0, NULL);
	const gchar *name;
	gchar *only = NULL, *src, *tmp, *dst, *result = NULL;
	guint n = 0;

	if (dir == NULL) return NULL;
	while ((name = g_dir_read_name (dir)) != NULL) {
		if (++n > 1) break;
		only = g_strdup (name);
	}
	g_dir_close (dir);
	if (n != 1) { g_free (only); return NULL; }
	src = g_build_filename (folder, only, NULL);
	dst = g_build_filename (target_dir, only, NULL);
	/* the wrapper usually carries the same name ("map/map"), so park the
	 * item next to it first, then remove the wrapper */
	tmp = g_strdup_printf ("%s.dakkar-lift", folder);
	if (!g_file_test (tmp, G_FILE_TEST_EXISTS | G_FILE_TEST_IS_SYMLINK)
	    && dakkar_arc_move_path (src, tmp)) {
		if (g_rmdir (folder) == 0) {
			if (!g_file_test (dst, G_FILE_TEST_EXISTS | G_FILE_TEST_IS_SYMLINK)
			    && dakkar_arc_move_path (tmp, dst)) {
				result = g_strdup (dst);
			} else if (g_mkdir (folder, 0755) != 0 || !dakkar_arc_move_path (tmp, src)) {
				/* cannot restore the wrapper: keep the item under the parked name */
				result = g_strdup (tmp);
			}
		} else {
			(void) dakkar_arc_move_path (tmp, src); /* keep things as they were */
		}
	}
	g_free (only);
	g_free (src);
	g_free (tmp);
	g_free (dst);
	return result;
}

static gchar *
dakkar_arc_unique_folder (const gchar *parent, const gchar *stem)
{
	guint n;
	for (n = 1; n < 1000; n++) {
		gchar *name = n == 1 ? g_strdup (stem) : g_strdup_printf ("%s (%u)", stem, n);
		gchar *path = g_build_filename (parent, name, NULL);
		g_free (name);
		if (g_mkdir (path, 0755) == 0) return path;
		g_free (path);
		if (errno != EEXIST) return NULL;
	}
	return NULL;
}

/* ---------- password-protected archives ---------- */

typedef enum {
	DAKKAR_ARC_PLAIN,
	DAKKAR_ARC_ENCRYPTED,      /* zip: libarchive decrypts with the password */
	DAKKAR_ARC_CLI_ENCRYPTED,  /* encrypted 7z/RAR: only the 7z program can */
	DAKKAR_ARC_BROKEN
} DakkarArcKind;

/* What kind of archive is this? For encrypted ones, @test_name is the
 * smallest encrypted file (to try a password on) and @header_encrypted
 * tells that even the list of names is encrypted (7z -mhe). */
static DakkarArcKind
dakkar_arc_probe (const gchar *path, gboolean *header_encrypted, gchar **test_name, gchar **error)
{
	const gchar *ext = strrchr (path, '.');
	struct archive *r;
	struct archive_entry *e;
	DakkarArcKind kind = DAKKAR_ARC_PLAIN;
	gboolean seven_or_rar;
	goffset best = -1;
	int rc, format;

	*header_encrypted = FALSE;
	*test_name = NULL;
	*error = NULL;
	/* only these formats can hold encrypted data; reading through a large
	 * tar.gz just to find out would double the work */
	if (ext == NULL || !(g_ascii_strcasecmp (ext, ".zip") == 0 || g_ascii_strcasecmp (ext, ".7z") == 0 ||
	                     g_ascii_strcasecmp (ext, ".rar") == 0 || g_ascii_strcasecmp (ext, ".jar") == 0))
		return DAKKAR_ARC_PLAIN;
	seven_or_rar = g_ascii_strcasecmp (ext, ".7z") == 0 || g_ascii_strcasecmp (ext, ".rar") == 0;
	r = dakkar_archive_reader_new ();
	if (archive_read_open_filename (r, path, 1 << 16) != ARCHIVE_OK) {
		const gchar *msg = archive_error_string (r);
		if (msg != NULL && seven_or_rar && strstr (msg, "ncrypt") != NULL) {
			*header_encrypted = TRUE;
			kind = DAKKAR_ARC_CLI_ENCRYPTED;
		} else {
			*error = g_strdup (msg != NULL ? msg : _("not an archive"));
			kind = DAKKAR_ARC_BROKEN;
		}
		archive_read_free (r);
		return kind;
	}
	while ((rc = archive_read_next_header (r, &e)) == ARCHIVE_OK || rc == ARCHIVE_WARN) {
		gboolean regular = archive_entry_filetype (e) == AE_IFREG;
		if (archive_entry_is_encrypted (e)) {
			kind = DAKKAR_ARC_ENCRYPTED;
			if (archive_entry_is_data_encrypted (e) && regular &&
			    (*test_name == NULL || archive_entry_size (e) < best)) {
				g_free (*test_name);
				*test_name = g_strdup (archive_entry_pathname (e));
				best = archive_entry_size (e);
			}
		}
		/* 7z/RAR: all files of one archive share the encryption, and
		 * skipping data there may mean decompressing it */
		if (seven_or_rar && regular) break;
		archive_read_data_skip (r);
	}
	if (rc == ARCHIVE_FATAL && kind == DAKKAR_ARC_PLAIN) {
		const gchar *msg = archive_error_string (r);
		if (archive_read_has_encrypted_entries (r) > 0 ||
		    (msg != NULL && strstr (msg, "ncrypt") != NULL)) {
			*header_encrypted = seven_or_rar;
			kind = seven_or_rar ? DAKKAR_ARC_CLI_ENCRYPTED : DAKKAR_ARC_ENCRYPTED;
		}
		/* other damage is reported by the extraction itself */
	}
	format = archive_format (r) & ARCHIVE_FORMAT_BASE_MASK;
	if (kind == DAKKAR_ARC_ENCRYPTED &&
	    (format == ARCHIVE_FORMAT_7ZIP || format == ARCHIVE_FORMAT_RAR ||
	     format == ARCHIVE_FORMAT_RAR_V5 || seven_or_rar))
		kind = DAKKAR_ARC_CLI_ENCRYPTED;
	archive_read_free (r);
	return kind;
}

typedef enum { DAKKAR_PW_OK, DAKKAR_PW_WRONG, DAKKAR_PW_UNKNOWN } DakkarPwCheck;

static DakkarPwCheck
dakkar_arc_check_password (DakkarArchiveJob *job, const gchar *path, DakkarArcKind kind,
                          gboolean header_encrypted, const gchar *test_name,
                          const gchar *program, const gchar *password)
{
	if (kind == DAKKAR_ARC_ENCRYPTED) {
		struct archive *r = dakkar_archive_reader_new ();
		struct archive_entry *e;
		DakkarPwCheck result = DAKKAR_PW_UNKNOWN;
		int rc;
		archive_read_add_passphrase (r, password);
		if (archive_read_open_filename (r, path, 1 << 16) == ARCHIVE_OK) {
			while ((rc = archive_read_next_header (r, &e)) == ARCHIVE_OK || rc == ARCHIVE_WARN) {
				guint8 probe[8192];
				la_ssize_t n;
				if (test_name != NULL && g_strcmp0 (archive_entry_pathname (e), test_name) != 0)
					continue;
				/* ZipCrypto checks only one byte of the password up
				 * front: read the whole (smallest) file so its CRC
				 * catches the rare wrong password that passes */
				while ((n = archive_read_data (r, probe, sizeof probe)) > 0)
					;
				memset (probe, 0, sizeof probe);   /* decrypted data */
				if (n == 0)
					result = DAKKAR_PW_OK;
				else {
					const gchar *msg = archive_error_string (r);
					result = (msg != NULL && (strstr (msg, "assphrase") != NULL ||
					                          strstr (msg, "CRC") != NULL ||
					                          strstr (msg, "ecrypt") != NULL))
						? DAKKAR_PW_WRONG : DAKKAR_PW_UNKNOWN;
				}
				break;
			}
			if (test_name == NULL && result == DAKKAR_PW_UNKNOWN)
				result = DAKKAR_PW_OK;
		}
		archive_read_free (r);
		return result;
	} else {
		/* header-encrypted: listing needs the password; otherwise test
		 * one small file */
		Dakkar7zRun run;
		DakkarPwCheck result;
		const gchar *argv[8];
		guint k = 0;
		argv[k++] = program;
		argv[k++] = header_encrypted ? "l" : "t";
		argv[k++] = path;
		if (!header_encrypted && test_name != NULL && test_name[0] != '-' &&
		    strpbrk (test_name, "*?[") == NULL)
			argv[k++] = test_name;
		argv[k] = NULL;
		dakkar_7z_run (job, argv, NULL, password, NULL, NULL, &run);
		result = run.wrong_password ? DAKKAR_PW_WRONG :
		         run.exit_status == 0 ? DAKKAR_PW_OK : DAKKAR_PW_UNKNOWN;
		dakkar_7z_run_clear (&run);
		return result;
	}
}

typedef struct {
	DakkarArchiveJob *job;
	GFile *archive;
	gboolean wrong;
	gboolean names_hidden;
	gint response;
	gchar *password;
} DakkarPwAsk;

static void
dakkar_pw_show_toggled (GtkToggleButton *button, GtkEntry *entry)
{
	gtk_entry_set_visibility (entry, gtk_toggle_button_get_active (button));
}

static void
dakkar_pw_changed (GtkEntry *entry, GtkDialog *dialog)
{
	gtk_dialog_set_response_sensitive (dialog, GTK_RESPONSE_ACCEPT,
	                                   gtk_entry_get_text_length (entry) > 0);
}

static gboolean
dakkar_pw_ask_main (gpointer user_data)
{
	DakkarPwAsk *ask = user_data;
	GtkWindow *parent = nemo_progress_info_present_window (ask->job->common.progress);
	GtkWidget *dialog, *area, *grid, *icon, *title, *text, *wrong, *entry, *show;
	gchar *name = g_file_get_basename (ask->archive), *markup;

	if (parent == NULL) parent = ask->job->common.parent_window;
	dialog = gtk_dialog_new_with_buttons (_("Password Required"), parent,
		GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
		_("_Cancel"), GTK_RESPONSE_CANCEL,
		_("_Skip Archive"), GTK_RESPONSE_REJECT,
		_("_Extract"), GTK_RESPONSE_ACCEPT,
		NULL);
	gtk_window_set_resizable (GTK_WINDOW (dialog), FALSE);
	gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_ACCEPT);
	gtk_style_context_add_class (gtk_widget_get_style_context (
		gtk_dialog_get_widget_for_response (GTK_DIALOG (dialog), GTK_RESPONSE_ACCEPT)),
		GTK_STYLE_CLASS_SUGGESTED_ACTION);
	area = gtk_dialog_get_content_area (GTK_DIALOG (dialog));
	grid = gtk_grid_new ();
	gtk_grid_set_row_spacing (GTK_GRID (grid), 8);
	gtk_grid_set_column_spacing (GTK_GRID (grid), 12);
	gtk_container_set_border_width (GTK_CONTAINER (grid), 12);
	icon = gtk_image_new_from_icon_name ("dialog-password", GTK_ICON_SIZE_DIALOG);
	gtk_widget_set_valign (icon, GTK_ALIGN_START);
	gtk_grid_attach (GTK_GRID (grid), icon, 0, 0, 1, 5);
	/* Translators: %s is an archive name */
	markup = g_markup_printf_escaped ("<b>%s</b>",
		ask->names_hidden ? _("The contents of this archive are encrypted") :
		                    _("This archive is password protected"));
	title = gtk_label_new (NULL);
	gtk_label_set_markup (GTK_LABEL (title), markup);
	gtk_label_set_xalign (GTK_LABEL (title), 0);
	gtk_grid_attach (GTK_GRID (grid), title, 1, 0, 1, 1);
	g_free (markup);
	{
		gchar *msg = g_strdup_printf (ask->names_hidden ?
			/* Translators: %s is an archive name */
			_("Even the file names in “%s” are encrypted. Enter the password to see and extract them.") :
			/* Translators: %s is an archive name */
			_("Enter the password for “%s”."), name);
		text = gtk_label_new (msg);
		g_free (msg);
	}
	gtk_label_set_xalign (GTK_LABEL (text), 0);
	gtk_label_set_line_wrap (GTK_LABEL (text), TRUE);
	gtk_label_set_max_width_chars (GTK_LABEL (text), 50);
	gtk_grid_attach (GTK_GRID (grid), text, 1, 1, 1, 1);
	wrong = gtk_label_new (NULL);
	gtk_label_set_markup (GTK_LABEL (wrong), "");
	{
		gchar *m = g_markup_printf_escaped ("<span foreground=\"#c01c28\">%s</span>",
		                                    _("Wrong password. Try again."));
		gtk_label_set_markup (GTK_LABEL (wrong), m);
		g_free (m);
	}
	gtk_label_set_xalign (GTK_LABEL (wrong), 0);
	gtk_grid_attach (GTK_GRID (grid), wrong, 1, 2, 1, 1);
	entry = gtk_entry_new ();
	gtk_entry_set_visibility (GTK_ENTRY (entry), FALSE);
	gtk_entry_set_input_purpose (GTK_ENTRY (entry), GTK_INPUT_PURPOSE_PASSWORD);
	gtk_entry_set_activates_default (GTK_ENTRY (entry), TRUE);
	gtk_entry_set_placeholder_text (GTK_ENTRY (entry), _("Password"));
	gtk_widget_set_hexpand (entry, TRUE);
	gtk_grid_attach (GTK_GRID (grid), entry, 1, 3, 1, 1);
	show = gtk_check_button_new_with_mnemonic (_("_Show password"));
	g_signal_connect (show, "toggled", G_CALLBACK (dakkar_pw_show_toggled), entry);
	gtk_grid_attach (GTK_GRID (grid), show, 1, 4, 1, 1);
	g_signal_connect (entry, "changed", G_CALLBACK (dakkar_pw_changed), dialog);
	gtk_dialog_set_response_sensitive (GTK_DIALOG (dialog), GTK_RESPONSE_ACCEPT, FALSE);
	gtk_box_pack_start (GTK_BOX (area), grid, TRUE, TRUE, 0);
	gtk_widget_show_all (grid);
	gtk_widget_set_visible (wrong, ask->wrong);
	gtk_widget_grab_focus (entry);

	ask->response = gtk_dialog_run (GTK_DIALOG (dialog));
	if (ask->response == GTK_RESPONSE_ACCEPT)
		ask->password = g_strdup (gtk_entry_get_text (GTK_ENTRY (entry)));
	/* do not leave the password in the entry's buffer */
	gtk_entry_set_text (GTK_ENTRY (entry), "");
	gtk_widget_destroy (dialog);
	g_free (name);
	return FALSE;
}

/* Ask on the main thread; returns ACCEPT (with *password), REJECT (skip
 * this archive) or CANCEL. */
static gint
dakkar_arc_ask_password (DakkarArchiveJob *job, GFile *archive, gboolean wrong,
                        gboolean names_hidden, gchar **password)
{
	CommonJob *common = &job->common;
	DakkarPwAsk ask = { job, archive, wrong, names_hidden, GTK_RESPONSE_CANCEL, NULL };
	g_timer_stop (common->time);
	nemo_progress_info_pause (common->progress);
	g_io_scheduler_job_send_to_mainloop (common->io_job, dakkar_pw_ask_main, &ask, NULL);
	nemo_progress_info_resume (common->progress);
	g_timer_continue (common->time);
	*password = ask.password;
	return ask.response;
}

/* Make job->password open this archive: the password of the previous
 * archive is tried silently first, then the user is asked until it is
 * right. FALSE: skipped (or the job was cancelled). */
static gboolean
dakkar_arc_unlock (DakkarArchiveJob *job, GFile *archive_file, const gchar *path,
                  DakkarArcKind kind, gboolean header_encrypted, const gchar *test_name,
                  const gchar *program)
{
	gboolean wrong = FALSE;
	if (job->password != NULL &&
	    dakkar_arc_check_password (job, path, kind, header_encrypted, test_name,
	                              program, job->password) == DAKKAR_PW_OK)
		return TRUE;
	while (!job_aborted (&job->common)) {
		gchar *password = NULL;
		gint response = dakkar_arc_ask_password (job, archive_file, wrong, header_encrypted, &password);
		DakkarPwCheck check;
		if (response == GTK_RESPONSE_REJECT) return FALSE;
		if (response != GTK_RESPONSE_ACCEPT || password == NULL) {
			nemo_file_operations_secret_free (password);
			abort_job (&job->common);
			return FALSE;
		}
		check = dakkar_arc_check_password (job, path, kind, header_encrypted, test_name,
		                                  program, password);
		if (job_aborted (&job->common)) {
			nemo_file_operations_secret_free (password);
			return FALSE;
		}
		if (check != DAKKAR_PW_WRONG) {
			/* UNKNOWN (damage, ...) is left to the extraction to report */
			nemo_file_operations_secret_free (job->password);
			job->password = password;
			return TRUE;
		}
		nemo_file_operations_secret_free (password);
		wrong = TRUE;
	}
	return FALSE;
}

/* ---------- Extract To… into an existing folder ----------
 *
 * Without a new folder per archive the contents meet what is already
 * there. They are first extracted into a hidden folder on the same file
 * system, then moved in one by one; a name that exists goes through the
 * usual conflict dialog (with its "apply to all" choices). */

static gchar *
dakkar_arc_staging_folder (const gchar *target_dir)
{
	gchar *path = g_build_filename (target_dir, ".dakkar-extract-XXXXXX", NULL);
	if (g_mkdtemp_full (path, 0700) == NULL) {
		g_free (path);
		return NULL;
	}
	return path;
}

static gchar *
dakkar_arc_unique_name (const gchar *path)
{
	gchar *dir = g_path_get_dirname (path), *base = g_path_get_basename (path);
	const gchar *dot = strrchr (base, '.');
	gsize stem_len = (dot != NULL && dot != base) ? (gsize) (dot - base) : strlen (base);
	gchar *stem = g_strndup (base, stem_len), *result = NULL;
	guint n;
	for (n = 2; n < 10000 && result == NULL; n++) {
		gchar *name = g_strdup_printf ("%s (%u)%s", stem, n, base + stem_len);
		gchar *candidate = g_build_filename (dir, name, NULL);
		g_free (name);
		if (!g_file_test (candidate, G_FILE_TEST_EXISTS) &&
		    !g_file_test (candidate, G_FILE_TEST_IS_SYMLINK))
			result = candidate;
		else
			g_free (candidate);
	}
	g_free (dir);
	g_free (base);
	g_free (stem);
	return result != NULL ? result : g_strdup (path);
}

typedef struct {
	DakkarArchiveJob *job;
	GFile *archive_file;
	const gchar *stage;
	const gchar *target;
	GPtrArray *moved;        /* paths placed in the target (removed on Cancel) */
	GPtrArray *top;          /* top-level GFiles placed (selected afterwards) */
	GHashTable *skipped;     /* GFile (in archive) → reason, reported later */
} DakkarArcMerge;

/* Report every file under a staged path that is not moved in. */
static void
dakkar_arc_merge_skip_tree (DakkarArcMerge *m, const gchar *rel, const gchar *reason)
{
	gchar *src = g_build_filename (m->stage, rel, NULL);
	GStatBuf st;
	if (g_lstat (src, &st) == 0 && S_ISDIR (st.st_mode)) {
		GDir *d = g_dir_open (src, 0, NULL);
		const gchar *name;
		while (d != NULL && (name = g_dir_read_name (d)) != NULL) {
			gchar *child = g_build_filename (rel, name, NULL);
			dakkar_arc_merge_skip_tree (m, child, reason);
			g_free (child);
		}
		if (d != NULL) g_dir_close (d);
	} else {
		g_hash_table_replace (m->skipped, g_file_resolve_relative_path (m->archive_file, rel),
		                      g_strdup (reason));
	}
	g_free (src);
}

static void dakkar_arc_merge_dir (DakkarArcMerge *m, const gchar *rel);

/* Point the manifest entries of what was moved in (a file, or a whole
 * folder) at their final place. */
static void
dakkar_arc_merge_retarget (DakkarArcMerge *m, const gchar *rel, const gchar *dst)
{
	GStatBuf st;
	if (g_lstat (dst, &st) == 0 && S_ISDIR (st.st_mode)) {
		GDir *d = g_dir_open (dst, 0, NULL);
		const gchar *name;
		while (d != NULL && (name = g_dir_read_name (d)) != NULL) {
			gchar *crel = g_build_filename (rel, name, NULL);
			gchar *cdst = g_build_filename (dst, name, NULL);
			dakkar_arc_merge_retarget (m, crel, cdst);
			g_free (crel);
			g_free (cdst);
		}
		if (d != NULL) g_dir_close (d);
	} else {
		GFile *vsrc = g_file_resolve_relative_path (m->archive_file, rel);
		GFile *dest = g_file_new_for_path (dst);
		nemo_progress_info_file_set_destination (m->job->common.progress, vsrc, dest);
		g_object_unref (vsrc);
		g_object_unref (dest);
	}
}

static void
dakkar_arc_merge_one (DakkarArcMerge *m, const gchar *rel)
{
	CommonJob *common = &m->job->common;
	gchar *src = g_build_filename (m->stage, rel, NULL);
	gchar *dst = g_build_filename (m->target, rel, NULL);
	gboolean replaced = FALSE;
	GStatBuf ss, ds;

	if (g_lstat (src, &ss) != 0) goto out;
	while (!job_aborted (common)) {
		GFile *sf, *df, *dd;
		gint id;
		gchar *new_name = NULL;

		if (g_lstat (dst, &ds) != 0) {
			if (g_rename (src, dst) == 0) {
				g_ptr_array_add (m->moved, g_strdup (dst));
				dakkar_arc_merge_retarget (m, rel, dst);
				if (strchr (rel, '/') == NULL)
					g_ptr_array_add (m->top, g_file_new_for_path (dst));
				if (replaced) {
					GFile *vsrc = g_file_resolve_relative_path (m->archive_file, rel);
					nemo_progress_info_file_set_overwritten (common->progress, vsrc, TRUE);
					g_object_unref (vsrc);
				}
			} else {
				dakkar_arc_merge_skip_tree (m, rel, g_strerror (errno));
			}
			break;
		}
		if (S_ISDIR (ss.st_mode) && S_ISDIR (ds.st_mode) && !replaced) {
			/* both are folders: merge their contents */
			dakkar_arc_merge_dir (m, rel);
			break;
		}
		sf = g_file_new_for_path (src);
		df = g_file_new_for_path (dst);
		dd = g_file_get_parent (df);
		if (common->replace_all) id = CONFLICT_RESPONSE_REPLACE;
		else if (common->skip_all_conflict) id = CONFLICT_RESPONSE_SKIP;
		else if (common->auto_rename_all) id = CONFLICT_RESPONSE_AUTO_RENAME;
		else if (common->conditional_replace_all != 0) id = common->conditional_replace_all;
		else {
			ConflictResponseData *response = run_conflict_dialog (common, sf, df, dd);
			id = response->id;
			new_name = g_strdup (response->new_name);
			if (response->apply_to_all) {
				if (id == CONFLICT_RESPONSE_REPLACE) common->replace_all = TRUE;
				else if (id == CONFLICT_RESPONSE_SKIP) common->skip_all_conflict = TRUE;
				else if (id == CONFLICT_RESPONSE_AUTO_RENAME) common->auto_rename_all = TRUE;
				else if (id == CONFLICT_RESPONSE_REPLACE_IF_NEWER ||
				         id == CONFLICT_RESPONSE_REPLACE_IF_DIFFERENT_SIZE)
					common->conditional_replace_all = id;
			}
			conflict_response_data_free (response);
		}
		if (id == CONFLICT_RESPONSE_SKIP) {
			dakkar_arc_merge_skip_tree (m, rel, _("Already exists; skipped"));
			id = -100;
		} else if (id == CONFLICT_RESPONSE_REPLACE) {
			if (S_ISDIR (ds.st_mode)) dakkar_arc_remove_tree (dst);
			else g_unlink (dst);
			replaced = TRUE;
		} else if (id == CONFLICT_RESPONSE_REPLACE_IF_NEWER ||
		           id == CONFLICT_RESPONSE_REPLACE_IF_DIFFERENT_SIZE) {
			if (S_ISREG (ss.st_mode) && S_ISREG (ds.st_mode) &&
			    dakkar_conditional_replace_matches (sf, df, id, common->cancellable)) {
				g_unlink (dst);
				replaced = TRUE;
			} else {
				dakkar_arc_merge_skip_tree (m, rel, _("Existing file kept (condition not met)"));
				id = -100;
			}
		} else if (id == CONFLICT_RESPONSE_RENAME && new_name != NULL && *new_name != '\0') {
			gchar *dir = g_path_get_dirname (dst);
			g_free (dst);
			dst = g_build_filename (dir, new_name, NULL);
			g_free (dir);
		} else if (id == CONFLICT_RESPONSE_AUTO_RENAME) {
			gchar *unique = dakkar_arc_unique_name (dst);
			g_free (dst);
			dst = unique;
		} else {
			abort_job (common);   /* Cancel */
		}
		g_free (new_name);
		g_object_unref (sf);
		g_object_unref (df);
		g_clear_object (&dd);
		if (id == -100) break;
	}
out:
	g_free (src);
	g_free (dst);
}

static void
dakkar_arc_merge_dir (DakkarArcMerge *m, const gchar *rel)
{
	gchar *dir = rel != NULL ? g_build_filename (m->stage, rel, NULL) : g_strdup (m->stage);
	GDir *d = g_dir_open (dir, 0, NULL);
	GPtrArray *names = g_ptr_array_new_with_free_func (g_free);
	const gchar *name;
	guint i;
	while (d != NULL && (name = g_dir_read_name (d)) != NULL)
		g_ptr_array_add (names, g_strdup (name));
	if (d != NULL) g_dir_close (d);
	g_ptr_array_sort (names, (GCompareFunc) g_strcmp0);
	for (i = 0; i < names->len && !job_aborted (&m->job->common); i++) {
		gchar *child = rel != NULL ? g_build_filename (rel, g_ptr_array_index (names, i), NULL)
		                           : g_strdup (g_ptr_array_index (names, i));
		dakkar_arc_merge_one (m, child);
		g_free (child);
	}
	g_ptr_array_unref (names);
	g_free (dir);
}

/* ---------- extracting through the 7z program ---------- */

typedef struct {
	DakkarArchiveJob *job;
	GFile *archive_file;
	const gchar *folder;
	GHashTable *sizes;       /* name → size, -1 for folders */
	GPtrArray *kept;
	GFile *current;
	gchar *current_path;
	goffset current_size;
	goffset base, span, total, done;
	goffset *content;
	guint *files_done;
} Dakkar7zExtract;

static void
dakkar_7z_list_line (const gchar *line, gpointer data)
{
	/* "-slt" lists "Path = …", "Size = …", "Attributes = D…" per entry,
	 * after a "----------" line (before it: the archive itself) */
	gpointer *state = data;
	GHashTable *sizes = state[0];
	gchar **path = (gchar **) &state[1];
	gboolean *started = state[2];
	if (g_str_has_prefix (line, "----------")) { *started = TRUE; return; }
	if (!*started) return;
	if (g_str_has_prefix (line, "Path = ")) {
		g_free (*path);
		*path = g_strdup (line + 7);
		g_hash_table_replace (sizes, g_strdup (*path), g_new0 (goffset, 1));
	} else if (*path != NULL && g_str_has_prefix (line, "Size = ")) {
		goffset *v = g_hash_table_lookup (sizes, *path);
		if (v != NULL && *v >= 0) *v = g_ascii_strtoll (line + 7, NULL, 10);
	} else if (*path != NULL && g_str_has_prefix (line, "Attributes = D")) {
		goffset *v = g_hash_table_lookup (sizes, *path);
		if (v != NULL) *v = -1;
	}
}

static void
dakkar_7z_extract_finish_current (Dakkar7zExtract *x)
{
	CommonJob *common = &x->job->common;
	if (x->current == NULL) return;
	nemo_progress_info_file_progress (common->progress, x->current, x->current_size, x->current_size);
	nemo_progress_info_file_end (common->progress, x->current, NEMO_TRANSFER_FILE_COMPLETED, NULL);
	g_ptr_array_add (x->kept, g_object_ref (x->current));
	*x->content += x->current_size;
	(*x->files_done)++;
	nemo_progress_info_report_files (common->progress, *x->files_done, *x->files_done);
	g_clear_object (&x->current);
	g_clear_pointer (&x->current_path, g_free);
}

static void
dakkar_7z_extract_line (const gchar *line, gpointer data)
{
	Dakkar7zExtract *x = data;
	CommonJob *common = &x->job->common;
	gint pct;
	if (g_str_has_prefix (line, "- ")) {
		const gchar *name = line + 2;
		goffset *size = g_hash_table_lookup (x->sizes, name);
		dakkar_7z_extract_finish_current (x);
		/* folders: "- name/" and "Attributes = D…" in the listing */
		if (g_str_has_suffix (name, "/") || (size != NULL && *size < 0)) return;
		{
			gchar *dest_path = g_build_filename (x->folder, name, NULL);
			GFile *dest = g_file_new_for_path (dest_path);
			x->current = g_file_resolve_relative_path (x->archive_file, name);
			x->current_path = dest_path;
			x->current_size = size != NULL ? *size : 0;
			nemo_progress_info_file_begin (common->progress, x->current, dest);
			nemo_progress_info_take_details (common->progress, g_strdup (name));
			g_object_unref (dest);
		}
		return;
	}
	pct = dakkar_7z_percent (line);
	if (pct >= 0) {
		x->done = x->base + x->span * pct / 100;
		dakkar_arc_report (common, x->done, x->total);
	}
}

/* Extract an encrypted 7z/RAR archive (job->password set) into @folder.
 * 7-Zip refuses names that would leave the folder ("..", absolute paths)
 * and checks each file's CRC; files it reports as broken are removed. */
static void
dakkar_extract_7z (DakkarArchiveJob *job, GFile *archive_file, const gchar *archive_path,
                  const gchar *folder, const gchar *program, GPtrArray *kept,
                  goffset *content, guint *files_done, goffset base, goffset span, goffset total)
{
	CommonJob *common = &job->common;
	GHashTable *sizes = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
	Dakkar7zExtract x;
	Dakkar7zRun run;
	guint i;

	/* sizes and folders first ("l" also needs the password when the
	 * names are encrypted) */
	{
		const gchar *argv[] = { program, "l", "-slt", archive_path, NULL };
		gboolean started = FALSE;
		gpointer state[3] = { sizes, NULL, &started };
		nemo_progress_info_take_details (common->progress, g_strdup (_("Reading the archive…")));
		dakkar_7z_run (job, argv, NULL, job->password, dakkar_7z_list_line, state, &run);
		g_free (state[1]);
		dakkar_7z_run_clear (&run);
	}
	if (job_aborted (common)) goto out;
	memset (&x, 0, sizeof x);
	x.job = job;
	x.archive_file = archive_file;
	x.folder = folder;
	x.sizes = sizes;
	x.kept = kept;
	x.base = base;
	x.span = span;
	x.total = total;
	x.content = content;
	x.files_done = files_done;
	{
		gchar *out = g_strconcat ("-o", folder, NULL);
		const gchar *argv[] = { program, "x", "-y", "-bsp1", "-bb1", "-snl", out, archive_path, NULL };
		dakkar_7z_run (job, argv, NULL, job->password, dakkar_7z_extract_line, &x, &run);
		g_free (out);
	}
	if (job_aborted (common)) {
		if (x.current != NULL) {
			nemo_progress_info_file_end (common->progress, x.current, NEMO_TRANSFER_FILE_CANCELLED, NULL);
			g_clear_object (&x.current);
			g_clear_pointer (&x.current_path, g_free);
		}
		dakkar_7z_run_clear (&run);
		goto out;
	}
	dakkar_7z_extract_finish_current (&x);
	/* "ERROR: Data Error in encrypted file. Wrong password? : name" */
	for (i = 0; i < run.error_lines->len; i++) {
		const gchar *t = g_ptr_array_index (run.error_lines, i);
		const gchar *sep = g_strrstr (t, " : ");
		if (sep != NULL && g_hash_table_contains (sizes, sep + 3)) {
			gchar *reason = g_strndup (t, sep - t);
			gchar *dest_path = g_build_filename (folder, sep + 3, NULL);
			GFile *vsrc = g_file_resolve_relative_path (archive_file, sep + 3);
			GStatBuf st;
			if (g_str_has_prefix (reason, "ERROR: ")) memmove (reason, reason + 7, strlen (reason + 7) + 1);
			if (g_lstat (dest_path, &st) == 0 && !S_ISDIR (st.st_mode)) g_unlink (dest_path);
			nemo_progress_info_file_end (common->progress, vsrc, NEMO_TRANSFER_FILE_FAILED, reason);
			g_free (reason);
			g_free (dest_path);
			g_object_unref (vsrc);
		}
	}
	if (job->verify) {
		/* 7-Zip compares the CRC of every file it extracts */
		if (!job->result->cli_checked) job->result->cli_check_ok = TRUE;
		job->result->cli_checked = TRUE;
		if (run.exit_status != 0) job->result->cli_check_ok = FALSE;
	}
	if (!run.started || run.exit_status >= 2 || run.exit_status < 0) {
		gchar *why = dakkar_7z_reason (&run, _("7-Zip could not extract the archive"));
		nemo_progress_info_file_begin (common->progress, archive_file, job->target);
		nemo_progress_info_file_end (common->progress, archive_file, NEMO_TRANSFER_FILE_FAILED, why);
		g_free (why);
	}
	dakkar_7z_run_clear (&run);
out:
	g_hash_table_destroy (sizes);
}

/* One more archive entry handled, failed or skipped. */
static void
dakkar_arc_count_file (CommonJob *common, guint *files_done)
{
	(*files_done)++;
	nemo_progress_info_report_files (common->progress, *files_done, *files_done);
}

/* Write regular file entry @e to @dest_path (never over an existing file,
 * never through a symlink), hash it on the way when verifying and read it
 * back afterwards. A failed, skipped or cancelled file is removed again. */
static void
dakkar_extract_regular (DakkarArchiveJob *job, struct archive *r, struct archive_entry *e,
                        GFile *vsrc, GFile *dest, const gchar *dest_path, const gchar *clean,
                        mode_t perm, guint8 *buffer, GPtrArray *kept, goffset *content,
                        goffset done_base, goffset archive_size, goffset total)
{
	CommonJob *common = &job->common;
	goffset size = archive_entry_size_is_set (e) ? archive_entry_size (e) : -1;
	GCancellable *fc;
	GChecksum *sum = NULL;
	goffset fdone = 0;
	gchar *err = NULL;
	gboolean user_skip;
	int fd;

	nemo_progress_info_file_begin (common->progress, vsrc, dest);
	nemo_progress_info_take_details (common->progress, g_strdup (clean));
	fc = nemo_progress_info_begin_file_cancellable (common->progress);
	fd = g_open (dest_path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
	if (fd < 0)
		err = g_strdup (g_strerror (errno));
	if (fd >= 0 && job->verify)
		sum = g_checksum_new (job->checksum_type);
	while (fd >= 0) {
		la_ssize_t n;
		if (g_cancellable_is_cancelled (fc)) break;
		if (!nemo_progress_info_wait_if_held (common->progress)) break;
		if (g_cancellable_is_cancelled (fc)) break;
		n = archive_read_data (r, buffer, DAKKAR_COPY_CHUNK);
		if (n < 0) { err = g_strdup (archive_error_string (r)); break; }
		if (n == 0) { size = fdone; break; }
		{
			gssize off = 0;
			while (off < n) {
				gssize w = write (fd, buffer + off, (size_t) (n - off));
				if (w < 0 && errno == EINTR) continue;
				if (w < 0) { err = g_strdup (g_strerror (errno)); break; }
				off += w;
			}
			if (err != NULL) break;
		}
		if (sum != NULL) g_checksum_update (sum, buffer, (gsize) n);
		fdone += n;
		dakkar_test_throttle (n);
		nemo_progress_info_file_progress (common->progress, vsrc, fdone,
			size >= fdone ? size : fdone);
		dakkar_arc_report (common,
			MIN (done_base + archive_filter_bytes (r, -1), done_base + archive_size),
			total);
	}
	user_skip = nemo_progress_info_end_file_cancellable (common->progress, fc);
	if (fd >= 0 && (err != NULL || job_aborted (common) || fdone != size)) {
		close (fd);
		g_unlink (dest_path);
		fd = -1;
		if (job_aborted (common))
			nemo_progress_info_file_end (common->progress, vsrc,
				NEMO_TRANSFER_FILE_CANCELLED, NULL);
		else if (err != NULL)
			nemo_progress_info_file_end (common->progress, vsrc,
				NEMO_TRANSFER_FILE_FAILED, err);
		else
			nemo_progress_info_file_end (common->progress, vsrc,
				NEMO_TRANSFER_FILE_SKIPPED, user_skip ? _("skipped by user") : NULL);
	} else if (fd < 0) {
		nemo_progress_info_file_end (common->progress, vsrc,
			NEMO_TRANSFER_FILE_FAILED, err);
	} else {
		struct timespec times[2];
		times[0].tv_sec = times[1].tv_sec = archive_entry_mtime (e);
		times[0].tv_nsec = times[1].tv_nsec = 0;
		if (fchmod (fd, perm ? perm : 0644) != 0) { /* keep 0600 */ }
		futimens (fd, times);
		close (fd);
		nemo_progress_info_file_end (common->progress, vsrc,
			NEMO_TRANSFER_FILE_COMPLETED, NULL);
		g_ptr_array_add (kept, g_object_ref (vsrc));
		*content += fdone;
		if (sum != NULL) {
			gchar *diag = NULL;
			gchar *digest = g_strdup (g_checksum_get_string (sum));
			gboolean ok = dakkar_readback_matches (common, vsrc, dest,
				job->checksum_type, digest, common->cancellable, &diag);
			if (!job_aborted (common))
				nemo_progress_info_file_verification (common->progress, vsrc,
					ok, ok ? digest : NULL, diag);
			g_free (diag);
			g_free (digest);
		}
	}
	if (sum != NULL) g_checksum_free (sum);
	g_free (err);
}

/* Extract every entry of the libarchive reader @r into @folder. Paths are
 * sanitized and must stay inside @root_real; links that point out, hard
 * links and special files are refused. Returns the last libarchive code. */
static int
dakkar_extract_entries (DakkarArchiveJob *job, struct archive *r, GFile *archive_file,
                        const gchar *folder, const gchar *root_real, GPtrArray *kept,
                        guint8 *buffer, goffset *content, guint *files_done,
                        goffset done_base, goffset archive_size, goffset total)
{
	CommonJob *common = &job->common;
	struct archive_entry *e;
	int rc = ARCHIVE_OK;

	while (!job_aborted (common) &&
	       ((rc = archive_read_next_header (r, &e)) == ARCHIVE_OK || rc == ARCHIVE_WARN)) {
		gchar *clean = dakkar_arc_sanitize (archive_entry_pathname (e));
		gchar *dest_path = NULL, *parent = NULL;
		GFile *vsrc, *dest = NULL;
		mode_t type = archive_entry_filetype (e);
		mode_t perm = archive_entry_perm (e) & 0777;

		if (!nemo_progress_info_wait_if_held (common->progress)) {
			g_free (clean);
			break;
		}
		if (clean == NULL) {
			const gchar *raw = archive_entry_pathname (e);
			gchar *shown = raw != NULL ? g_path_get_basename (raw) : g_strdup ("?");
			vsrc = g_file_get_child (archive_file, shown);
			g_free (shown);
			nemo_progress_info_file_begin (common->progress, vsrc, NULL);
			nemo_progress_info_file_end (common->progress, vsrc, NEMO_TRANSFER_FILE_FAILED,
				_("unsafe path in the archive (absolute or containing “..”)"));
			g_object_unref (vsrc);
			dakkar_arc_count_file (common, files_done);
			continue;
		}
		vsrc = g_file_resolve_relative_path (archive_file, clean);
		dest_path = g_build_filename (folder, clean, NULL);
		dest = g_file_new_for_path (dest_path);
		parent = g_path_get_dirname (dest_path);
		g_mkdir_with_parents (parent, 0755);
		if (type == AE_IFDIR) {
			if (dakkar_arc_inside (root_real, parent))
				g_mkdir_with_parents (dest_path, perm ? (perm | 0700) : 0755);
		} else if (!dakkar_arc_inside (root_real, parent)) {
			nemo_progress_info_file_begin (common->progress, vsrc, dest);
			nemo_progress_info_file_end (common->progress, vsrc, NEMO_TRANSFER_FILE_FAILED,
				_("the path leaves the destination folder through a symbolic link"));
			dakkar_arc_count_file (common, files_done);
		} else if (type == AE_IFLNK) {
			nemo_progress_info_file_begin (common->progress, vsrc, dest);
			if (symlink (archive_entry_symlink (e) ? archive_entry_symlink (e) : "", dest_path) == 0) {
				nemo_progress_info_file_end (common->progress, vsrc, NEMO_TRANSFER_FILE_COMPLETED, NULL);
				g_ptr_array_add (kept, g_object_ref (vsrc));
			} else
				nemo_progress_info_file_end (common->progress, vsrc, NEMO_TRANSFER_FILE_FAILED,
					g_strerror (errno));
			dakkar_arc_count_file (common, files_done);
		} else if (type != AE_IFREG || archive_entry_hardlink (e) != NULL) {
			nemo_progress_info_file_begin (common->progress, vsrc, dest);
			nemo_progress_info_file_end (common->progress, vsrc, NEMO_TRANSFER_FILE_SKIPPED,
				_("hard links and special files are not extracted"));
			dakkar_arc_count_file (common, files_done);
		} else {
			dakkar_extract_regular (job, r, e, vsrc, dest, dest_path, clean, perm, buffer,
			                        kept, content, done_base, archive_size, total);
			dakkar_arc_count_file (common, files_done);
		}
		g_free (clean);
		g_free (dest_path);
		g_free (parent);
		g_object_unref (vsrc);
		g_clear_object (&dest);
	}
	if (rc == ARCHIVE_FATAL && !job_aborted (common)) {
		nemo_progress_info_file_begin (common->progress, archive_file, job->target);
		nemo_progress_info_file_end (common->progress, archive_file, NEMO_TRANSFER_FILE_FAILED,
			archive_error_string (r));
	}
	return rc;
}

/* After an archive: merge the hidden staging folder into place (Extract
 * To without a new folder), undo everything on cancel, or announce the
 * new folder, lifting a lone top-level item out of it. */
static void
dakkar_extract_settle (DakkarArchiveJob *job, DakkarArcMerge *merge, gchar **folder,
                       gchar **last_folder, const gchar *target_dir, GPtrArray *kept,
                       goffset *content, goffset content_before)
{
	CommonJob *common = &job->common;

	if (!job->subfolders && !job_aborted (common)) {
		/* Extract To… without a new folder: move the contents in,
		 * asking about names that already exist */
		GHashTableIter iter;
		gpointer key, value;
		merge->stage = *folder;
		merge->moved = g_ptr_array_new_with_free_func (g_free);
		merge->top = g_ptr_array_new_with_free_func (g_object_unref);
		merge->skipped = g_hash_table_new_full (g_file_hash, (GEqualFunc) g_file_equal,
		                                       g_object_unref, g_free);
		nemo_progress_info_take_details (common->progress, g_strdup (_("Moving into place…")));
		dakkar_arc_merge_dir (merge, NULL);
		g_hash_table_iter_init (&iter, merge->skipped);
		while (g_hash_table_iter_next (&iter, &key, &value))
			nemo_progress_info_file_end (common->progress, key,
				NEMO_TRANSFER_FILE_SKIPPED, value);
	}
	if (job_aborted (common)) {
		/* Cancel: the new (or hidden) folder only holds what this job
		 * wrote, and so does what was already moved in */
		guint k;
		dakkar_arc_remove_tree (*folder);
		for (k = 0; merge->moved != NULL && k < merge->moved->len; k++) {
			const gchar *moved = g_ptr_array_index (merge->moved, k);
			GStatBuf ms;
			if (g_lstat (moved, &ms) == 0 && S_ISDIR (ms.st_mode))
				dakkar_arc_remove_tree (moved);
			else
				g_unlink (moved);
		}
		for (k = 0; k < kept->len; k++)
			nemo_progress_info_file_end (common->progress, g_ptr_array_index (kept, k),
				NEMO_TRANSFER_FILE_CANCELLED, NULL);
		*content = content_before;
	} else if (!job->subfolders) {
		guint k;
		/* what was not moved in (skipped) goes with the hidden folder */
		dakkar_arc_remove_tree (*folder);
		for (k = 0; k < merge->top->len; k++) {
			GFile *f = g_ptr_array_index (merge->top, k);
			nemo_file_changes_queue_file_added (f);
			g_hash_table_add (job->debuting_files, g_object_ref (f));
		}
		job->result->created = TRUE;
	} else {
		GFile *f;
		gchar *lifted = dakkar_arc_lift_single (*folder, target_dir);
		if (lifted != NULL) {
			/* the archive held one top-level item: no wrapper folder */
			g_free (*folder);
			*folder = lifted;
			g_free (*last_folder);
			*last_folder = g_strdup (*folder);
		}
		f = g_file_new_for_path (*folder);
		nemo_file_changes_queue_file_added (f);
		g_hash_table_add (job->debuting_files, f);
		job->result->created = TRUE;
	}
}

static gboolean
dakkar_extract_job (GIOSchedulerJob *io_job, GCancellable *cancellable, gpointer user_data)
{
	DakkarArchiveJob *job = user_data;
	CommonJob *common = &job->common;
	gchar *target_dir = g_file_get_path (job->target);
	guint8 *buffer = g_malloc (DAKKAR_COPY_CHUNK);
	goffset total = 0, done_base = 0, content = 0;
	guint files_done = 0, n_archives = g_list_length (job->files);
	gchar *last_folder = NULL;
	gchar *program = nemo_file_operations_archive_7z_program ();
	GList *l;

	common->io_job = io_job;
	nemo_progress_info_start (common->progress);
	if (job->verify)
		nemo_progress_info_set_checksum_name (common->progress, job->checksum_name);
	{
		gchar *first = g_file_get_basename (job->files->data);
		if (n_archives == 1)
			/* Translators: %s is an archive name */
			nemo_progress_info_take_status (common->progress,
				g_strdup_printf (_("Extracting “%s”"), first));
		else
			nemo_progress_info_take_status (common->progress,
				g_strdup_printf (ngettext ("Extracting %u archive", "Extracting %u archives",
				                           n_archives), n_archives));
		g_free (first);
	}
	for (l = job->files; l != NULL; l = l->next) {
		GFileInfo *info = g_file_query_info (l->data, G_FILE_ATTRIBUTE_STANDARD_SIZE,
		                                     0, NULL, NULL);
		if (info != NULL) {
			total += g_file_info_get_size (info);
			g_object_unref (info);
		}
	}
	dakkar_arc_report (common, 0, total);
	g_timer_start (common->time);

	for (l = job->files; l != NULL && !job_aborted (common); l = l->next) {
		GFile *archive_file = l->data;
		gchar *archive_path = g_file_get_path (archive_file);
		/* entries written for this archive; a cancel removes them again */
		GPtrArray *kept = g_ptr_array_new_with_free_func (g_object_unref);
		goffset content_before = content;
		gchar *archive_name = g_file_get_basename (archive_file);
		gchar *stem, *folder = NULL, *root_real = NULL;
		struct archive *r = NULL;
		goffset archive_size = 0;
		GStatBuf st;
		DakkarArcKind kind;
		gboolean header_encrypted = FALSE;
		gchar *test_name = NULL, *probe_error = NULL;
		DakkarArcMerge merge = { job, archive_file, NULL, target_dir, NULL, NULL, NULL };

		if (archive_path != NULL && g_stat (archive_path, &st) == 0)
			archive_size = st.st_size;
		stem = g_strndup (archive_name,
			strlen (archive_name) - dakkar_archive_extension_length (archive_name));
		if (archive_path == NULL || target_dir == NULL) {
			nemo_progress_info_file_begin (common->progress, archive_file, job->target);
			nemo_progress_info_file_end (common->progress, archive_file, NEMO_TRANSFER_FILE_FAILED,
				_("Only local archives can be extracted"));
			goto next_archive;
		}
		/* password-protected? then the password comes first: for a 7z
		 * with encrypted names nothing can even be listed without it */
		kind = dakkar_arc_probe (archive_path, &header_encrypted, &test_name, &probe_error);
		if (kind == DAKKAR_ARC_BROKEN) {
			nemo_progress_info_file_begin (common->progress, archive_file, job->target);
			nemo_progress_info_file_end (common->progress, archive_file, NEMO_TRANSFER_FILE_FAILED,
				probe_error);
			goto next_archive;
		}
		if (kind == DAKKAR_ARC_CLI_ENCRYPTED && program == NULL) {
			nemo_progress_info_file_begin (common->progress, archive_file, job->target);
			nemo_progress_info_file_end (common->progress, archive_file, NEMO_TRANSFER_FILE_FAILED,
				_("encrypted 7z and RAR archives need the 7z program (package 7zip or p7zip-full)"));
			goto next_archive;
		}
		if (kind != DAKKAR_ARC_PLAIN) {
			if (!dakkar_arc_unlock (job, archive_file, archive_path, kind, header_encrypted,
			                       test_name, program)) {
				if (!job_aborted (common)) {
					nemo_progress_info_file_begin (common->progress, archive_file, job->target);
					nemo_progress_info_file_end (common->progress, archive_file,
						NEMO_TRANSFER_FILE_SKIPPED, _("password protected; no password given"));
				}
				goto next_archive;
			}
			job->result->n_protected++;
		}
		if (kind != DAKKAR_ARC_CLI_ENCRYPTED) {
			r = dakkar_archive_reader_new ();
			if (kind == DAKKAR_ARC_ENCRYPTED)
				archive_read_add_passphrase (r, job->password);
			if (archive_read_open_filename (r, archive_path, 1 << 16) != ARCHIVE_OK) {
				nemo_progress_info_file_begin (common->progress, archive_file, job->target);
				nemo_progress_info_file_end (common->progress, archive_file, NEMO_TRANSFER_FILE_FAILED,
					archive_error_string (r));
				goto next_archive;
			}
		}
		/* a new folder per archive, or a hidden one to merge from */
		folder = job->subfolders ? dakkar_arc_unique_folder (target_dir, stem)
		                         : dakkar_arc_staging_folder (target_dir);
		if (folder == NULL) {
			nemo_progress_info_file_begin (common->progress, archive_file, job->target);
			nemo_progress_info_file_end (common->progress, archive_file, NEMO_TRANSFER_FILE_FAILED,
				g_strerror (errno));
			goto next_archive;
		}
		root_real = realpath (folder, NULL);
		g_free (last_folder);
		last_folder = g_strdup (job->subfolders ? folder : target_dir);

		/* which checks this job uses, for the window's labels */
		g_object_set_data (G_OBJECT (common->progress), "dakkar-verify-method",
			GINT_TO_POINTER (GPOINTER_TO_INT (g_object_get_data (G_OBJECT (common->progress),
			                                                     "dakkar-verify-method")) |
			                 (kind == DAKKAR_ARC_CLI_ENCRYPTED ? DAKKAR_VERIFY_METHOD_CRC7Z
			                                                  : DAKKAR_VERIFY_METHOD_HASH)));
		if (kind == DAKKAR_ARC_CLI_ENCRYPTED) {
			dakkar_extract_7z (job, archive_file, archive_path, folder, program, kept,
			                  &content, &files_done, done_base, archive_size, total);
		} else {
			dakkar_extract_entries (job, r, archive_file, folder, root_real, kept, buffer,
			                        &content, &files_done, done_base, archive_size, total);
		}
		dakkar_extract_settle (job, &merge, &folder, &last_folder, target_dir, kept,
		                       &content, content_before);
next_archive:
		g_ptr_array_unref (kept);
		if (merge.moved != NULL) g_ptr_array_unref (merge.moved);
		if (merge.top != NULL) g_ptr_array_unref (merge.top);
		if (merge.skipped != NULL) g_hash_table_destroy (merge.skipped);
		g_free (test_name);
		g_free (probe_error);
		done_base += archive_size;
		dakkar_arc_report (common, done_base, total);
		if (r != NULL) archive_read_free (r);
		free (root_real);
		g_free (folder);
		g_free (stem);
		g_free (archive_name);
		g_free (archive_path);
	}
	if (n_archives > 1) {
		/* several archives: name the folder they were extracted into */
		g_free (last_folder);
		last_folder = g_strdup (target_dir);
	}
	job->result->archive_path = last_folder;
	job->result->archive_size = total;
	job->result->content_size = content;
	g_free (buffer);
	g_free (target_dir);
	g_free (program);
	g_io_scheduler_job_send_to_mainloop_async (io_job, dakkar_archive_job_done, job, NULL);
	(void) cancellable;
	return FALSE;
}

static DakkarArchiveJob *
dakkar_archive_job_new (GList *files, GFile *target, GFile *destination_dir,
                       NemoTransferOperation op, GtkWindow *parent_window,
                       NemoCopyCallback done_callback, gpointer done_callback_data)
{
	DakkarArchiveJob *job = op_job_new (DakkarArchiveJob, parent_window);
	const gchar *name = NULL;
	job->files = eel_g_object_list_copy (files);
	job->target = g_object_ref (target);
	job->done_callback = done_callback;
	job->done_callback_data = done_callback_data;
	job->debuting_files = g_hash_table_new_full (g_file_hash, (GEqualFunc) g_file_equal,
	                                             g_object_unref, NULL);
	/* settings are read here, on the main thread */
	job->verify = g_settings_get_boolean (nemo_preferences, "dakkar-transfer-verify-after-copy");
	job->checksum_type = dakkar_verify_checksum_type (&name);
	job->checksum_name = g_strdup (name);
	job->result = g_new0 (NemoArchiveResult, 1);
	{
		/* "From:" in the window: the folder of the selection, or the
		 * archive itself when a single archive is extracted */
		GFile *from = (op == NEMO_TRANSFER_OP_EXTRACT && files->next == NULL) ?
			g_object_ref (files->data) : g_file_get_parent (files->data);
		job->result->source_label = from != NULL ? g_file_get_parse_name (from) : NULL;
		g_clear_object (&from);
	}
	g_object_set_data_full (G_OBJECT (job->common.progress), "dakkar-archive-result",
	                        job->result, dakkar_archive_result_free);
	nemo_progress_info_set_destination (job->common.progress, destination_dir);
	nemo_progress_info_set_operation (job->common.progress, op);
	return job;
}

void
nemo_file_operations_compress (GList *files, GFile *archive, NemoArchiveFormat format,
                               GtkWindow *parent_window,
                               NemoCopyCallback done_callback, gpointer done_callback_data)
{
	nemo_file_operations_compress_full (files, archive, format, NULL, FALSE, parent_window,
	                                    done_callback, done_callback_data);
}

void
nemo_file_operations_compress_full (GList *files, GFile *archive, NemoArchiveFormat format,
                                    const gchar *password, gboolean encrypt_names,
                                    GtkWindow *parent_window,
                                    NemoCopyCallback done_callback, gpointer done_callback_data)
{
	DakkarArchiveJob *job;
	GIOSchedulerJobFunc func = dakkar_compress_job;
	GFile *dir = g_file_get_parent (archive);
	gchar *name = g_file_get_basename (archive);
	g_return_if_fail (files != NULL && dir != NULL);
	job = dakkar_archive_job_new (files, archive, dir, NEMO_TRANSFER_OP_COMPRESS,
	                             parent_window, done_callback, done_callback_data);
	job->format = format;
	if (password != NULL && *password != '\0') {
		NemoArchiveEncryption enc = nemo_file_operations_archive_encryption (format, NULL);
		job->password = g_strdup (password);
		if (format == NEMO_ARCHIVE_7Z) {
			job->encrypt_names = encrypt_names;
			func = dakkar_compress_7z_job;
		} else {
			job->zip_encryption = enc == NEMO_ARCHIVE_ENCRYPTION_ZIPCRYPTO ? "zipcrypt" : "aes256";
		}
	}
	/* Translators: %s is the name of the archive to create */
	nemo_progress_info_take_initial_details (job->common.progress,
		g_strdup_printf (_("Waiting to compress to “%s”"), name));
	inhibit_power_manager ((CommonJob *) job, _("Compressing Files"));
	add_job_to_job_queue (func, job, job->common.cancellable,
	                      job->common.progress, OP_KIND_CREATE);
	g_free (name);
	g_object_unref (dir);
}

void
nemo_file_operations_extract (GList *archives, GFile *target_dir, GtkWindow *parent_window,
                              NemoCopyCallback done_callback, gpointer done_callback_data)
{
	nemo_file_operations_extract_full (archives, target_dir, TRUE, parent_window,
	                                   done_callback, done_callback_data);
}

void
nemo_file_operations_extract_full (GList *archives, GFile *target_dir, gboolean subfolders,
                                   GtkWindow *parent_window,
                                   NemoCopyCallback done_callback, gpointer done_callback_data)
{
	DakkarArchiveJob *job;
	g_return_if_fail (archives != NULL && target_dir != NULL);
	job = dakkar_archive_job_new (archives, target_dir, target_dir, NEMO_TRANSFER_OP_EXTRACT,
	                             parent_window, done_callback, done_callback_data);
	job->subfolders = subfolders;
	nemo_progress_info_take_initial_details (job->common.progress,
		g_strdup (_("Waiting to extract")));
	inhibit_power_manager ((CommonJob *) job, _("Extracting Files"));
	add_job_to_job_queue (dakkar_extract_job, job, job->common.cancellable,
	                      job->common.progress, OP_KIND_CREATE);
}

#else /* !HAVE_LIBARCHIVE */

gboolean nemo_file_operations_archive_supported (void) { return FALSE; }
gboolean nemo_file_operations_archive_format_available (NemoArchiveFormat f) { (void) f; return FALSE; }
gboolean nemo_file_operations_is_extractable_name (const gchar *name) { (void) name; return FALSE; }
void
nemo_file_operations_compress (GList *files, GFile *archive, NemoArchiveFormat format,
                               GtkWindow *parent_window,
                               NemoCopyCallback done_callback, gpointer done_callback_data)
{
	g_warning ("Dakkar was built without libarchive: Compress is handled by the quick action");
	(void) files; (void) archive; (void) format; (void) parent_window;
	(void) done_callback; (void) done_callback_data;
}
void
nemo_file_operations_extract (GList *archives, GFile *target_dir, GtkWindow *parent_window,
                              NemoCopyCallback done_callback, gpointer done_callback_data)
{
	g_warning ("Dakkar was built without libarchive: Extract Here is unavailable");
	(void) archives; (void) target_dir; (void) parent_window;
	(void) done_callback; (void) done_callback_data;
}
void
nemo_file_operations_extract_full (GList *archives, GFile *target_dir, gboolean subfolders,
                                   GtkWindow *parent_window,
                                   NemoCopyCallback done_callback, gpointer done_callback_data)
{
	(void) subfolders;
	nemo_file_operations_extract (archives, target_dir, parent_window, done_callback, done_callback_data);
}
void
nemo_file_operations_compress_full (GList *files, GFile *archive, NemoArchiveFormat format,
                                    const gchar *password, gboolean encrypt_names,
                                    GtkWindow *parent_window,
                                    NemoCopyCallback done_callback, gpointer done_callback_data)
{
	(void) password; (void) encrypt_names;
	nemo_file_operations_compress (files, archive, format, parent_window, done_callback, done_callback_data);
}
NemoArchiveEncryption
nemo_file_operations_archive_encryption (NemoArchiveFormat format, gboolean *names_possible)
{
	(void) format;
	if (names_possible != NULL) *names_possible = FALSE;
	return NEMO_ARCHIVE_ENCRYPTION_NONE;
}
#endif /* HAVE_LIBARCHIVE */
