/* Dakkar: the one details sidebar ("inspector") + Quick Actions panel used by
 * EVERY view (Icons, List, Compact and Miller Columns).  Included by
 * nemo-window-slot.c.  The widget is packed alongside (never over) the file
 * view.  Preview helpers live in dakkar-preview.c, actions in dakkar-qa-run.c.
 */

/* Keep the actual preview graphic responsive to the vertical divider, not
 * merely its containing GtkScrolledWindow. Preserve an unscaled pixbuf as
 * the source; repeated drags must not degrade thumbnail quality. */
static void
files_slot_refresh_preview_image_size (NemoWindowSlot *slot)
{
    GtkImage *image;
    GdkPixbuf *original;
    gint panel_width, preview_height, pixel_size, scale;
    if (slot->inspector_image == NULL ||
        slot->inspector_vertical_paned == NULL) return;
    image = GTK_IMAGE (slot->inspector_image);
    panel_width = gtk_widget_get_allocated_width (slot->quick_actions_panel);
    preview_height = gtk_paned_get_position (
        GTK_PANED (slot->inspector_vertical_paned));
    pixel_size = CLAMP (MIN (preview_height - 135, panel_width - 40), 40, 256);
    scale = MAX (1, gtk_widget_get_scale_factor (GTK_WIDGET (image)));
    /* Re-render only when the icon's actual size (or the HiDPI scale)
     * changes meaningfully. */
    if (GPOINTER_TO_INT (g_object_get_data (G_OBJECT (image),
                            "dakkar-preview-last-size")) == pixel_size * 16 + scale) return;
    original = g_object_get_data (G_OBJECT (image), "dakkar-original-preview");
    if (original != NULL) {
        gint source_width = gdk_pixbuf_get_width (original);
        gint source_height = gdk_pixbuf_get_height (original);
        gint target = pixel_size * scale;
        /* Render at device pixels and hand GTK a surface carrying the
         * scale factor: one crisp, centred image, never a 1x pixbuf that
         * GTK stretches on HiDPI.  Small sources keep their logical size. */
        gdouble factor = MIN ((gdouble) scale,
             MIN ((gdouble)target / MAX (1, source_width),
                  (gdouble)target / MAX (1, source_height)));
        GdkPixbuf *scaled = gdk_pixbuf_scale_simple (original,
             MAX (1, (gint)(source_width * factor + .5)),
             MAX (1, (gint)(source_height * factor + .5)), GDK_INTERP_BILINEAR);
        if (scaled != NULL) {
            cairo_surface_t *surface = gdk_cairo_surface_create_from_pixbuf (
                scaled, scale, gtk_widget_get_window (GTK_WIDGET (image)));
            gtk_image_set_from_surface (image, surface);
            cairo_surface_destroy (surface);
            g_object_unref (scaled);
        }
    } else {
        GtkImageType type = gtk_image_get_storage_type (image);
        if (type == GTK_IMAGE_ICON_NAME || type == GTK_IMAGE_GICON)
            gtk_image_set_pixel_size (image, pixel_size);
    }
    g_object_set_data (G_OBJECT (image), "dakkar-preview-last-size",
                       GINT_TO_POINTER (pixel_size * 16 + scale));
}

/* Shared classic-view inspector splitter controls. Save only on actual
 * pointer release, never on GTK's initial widget allocation. */
static void
files_slot_inspector_resize_allocation (GtkWidget *paned,
                                        GdkRectangle *allocation,
                                        NemoWindowSlot *slot)
{
    if (slot->inspector_width_ready || allocation->width < 400 ||
        slot->inspector_repositioning) return;
    slot->inspector_repositioning = TRUE;
    gtk_paned_set_position (GTK_PANED (paned), slot->dakkar_inspector_left ?
                            slot->inspector_width :
                            allocation->width - slot->inspector_width);
    slot->inspector_repositioning = FALSE;
    slot->inspector_width_ready = TRUE;
}

static void
files_slot_inspector_resize_notify (GObject *paned, GParamSpec *pspec,
                                    NemoWindowSlot *slot)
{
    /* The width is recorded after a drag, not on every size allocation. */
    (void) paned; (void) pspec; (void) slot;
}

static gboolean
files_slot_inspector_resize_release (GtkWidget *paned, GdkEventButton *event,
                                     NemoWindowSlot *slot)
{
    gint total, width;
    if (event->button != 1 || slot->inspector_repositioning ||
        !slot->inspector_width_ready || slot->quick_actions_collapsed) return FALSE;
    /* Only a drag of the divider itself, not a click that bubbles up from
     * a button inside the panel (e.g. the media player). */
    if (event->window != gtk_paned_get_handle_window (GTK_PANED (paned))) return FALSE;
    total = gtk_widget_get_allocated_width (paned);
    if (total < 400) return FALSE;
    width = slot->dakkar_inspector_left ? gtk_paned_get_position (GTK_PANED (paned)) :
                  total - gtk_paned_get_position (GTK_PANED (paned));
    if (width < 92 || width > 650) return FALSE;
    slot->inspector_width = width;
    slot->inspector_repositioning = TRUE;
    g_settings_set_int (nemo_preferences, "dakkar-inspector-width", width);
    slot->inspector_repositioning = FALSE;
    files_slot_refresh_preview_image_size (slot);
    return FALSE;
}

static void
files_slot_preview_allocation (GtkWidget *paned, GdkRectangle *allocation,
                               NemoWindowSlot *slot)
{
    if (slot->inspector_height_ready || allocation->height < 220 ||
        slot->inspector_height_repositioning) return;
    slot->inspector_height_repositioning = TRUE;
    gtk_paned_set_position (GTK_PANED (paned), MIN (allocation->height - 80,
        dakkar_inspector_saved_int ("dakkar-inspector-preview-height", 100, 1000)));
    slot->inspector_height_repositioning = FALSE;
    slot->inspector_height_ready = TRUE;
    files_slot_refresh_preview_image_size (slot);
}

static gboolean
files_slot_preview_release (GtkWidget *paned, GdkEventButton *event,
                            NemoWindowSlot *slot)
{
    gint pos;
    if (event->button != 1 || !slot->inspector_height_ready ||
        !gtk_widget_get_visible (slot->quick_actions_section)) return FALSE;
    if (event->window != gtk_paned_get_handle_window (GTK_PANED (paned))) return FALSE;
    pos = gtk_paned_get_position (GTK_PANED (paned));
    if (pos >= 100 && pos <= 1000) {
        slot->inspector_height_repositioning = TRUE;
        g_settings_set_int (nemo_preferences, "dakkar-inspector-preview-height", pos);
        slot->inspector_height_repositioning = FALSE;
    }
    /* Height growth can also request a wider preview, provided the file
     * browser retains at least 240px. Unlike a manual width drag, this is a
     * transient adjustment and never rewrites the shared saved width key. */
    if (slot->dakkar_content_row != NULL &&
        !slot->inspector_repositioning) {
        gint total = gtk_widget_get_allocated_width (slot->dakkar_content_row);
        gint current = gtk_widget_get_allocated_width (slot->quick_actions_panel);
        gint desired = MIN (MIN (pos - 90, 420), total - 240);
        if (total > 540 && desired > current + 12) {
            slot->inspector_repositioning = TRUE;
            gtk_paned_set_position (GTK_PANED (slot->dakkar_content_row),
                slot->dakkar_inspector_left ? desired : total - desired);
            slot->inspector_width = desired;
            slot->inspector_repositioning = FALSE;
        }
    }
    files_slot_refresh_preview_image_size (slot);
    return FALSE;
}

static void
files_slot_preview_live_position (GObject *paned, GParamSpec *param,
                                  NemoWindowSlot *slot)
{
    /* No widget reparenting or GSettings write during a handle drag. */
    (void)paned; (void)param;
    files_slot_refresh_preview_image_size (slot);
}

static void
files_slot_inspector_apply_fields (NemoWindowSlot *slot)
{
    GtkWidget *widgets[] = { slot->inspector_name, slot->inspector_type,
       slot->quick_actions_folder_size, slot->inspector_modified,
       slot->inspector_path, slot->inspector_owner };
    GtkWidget *audio = g_object_get_data (G_OBJECT (slot), "dakkar-inspector-audio");
    GtkWidget *grid = g_object_get_data (G_OBJECT (slot), "dakkar-inspector-media-grid");
    if (slot->inspector_metadata_box) {
        GtkBox *box = GTK_BOX (slot->inspector_metadata_box);
        GtkWidget *anchor = gtk_widget_get_visible (slot->inspector_type) ?
                            slot->inspector_type : slot->inspector_name;
        gint position = 0;
        dakkar_details_apply_fields (slot->inspector_metadata_box, widgets, 1);
        /* players directly under the picture, media tags under the type */
        if (slot->inspector_video != NULL) gtk_box_reorder_child (box, slot->inspector_video, 1);
        if (audio != NULL) gtk_box_reorder_child (box, audio, 2);
        if (grid != NULL && anchor != NULL) {
            gtk_container_child_get (GTK_CONTAINER (box), anchor, "position", &position, NULL);
            gtk_box_reorder_child (box, grid, position + 1);
        }
    }
}

/* The classic views use the same preview field styling and ordering as
 * Miller. Keep these details independent of the Quick Actions toggle. */
static GtkWidget *
files_slot_inspector_label (GtkWidget *parent, const char *style, gboolean path)
{
    GtkWidget *label = gtk_label_new ("");
    gtk_label_set_xalign (GTK_LABEL (label), 0.5);
    /* Multi-line values (folder size: items / total) stay centred too. */
    gtk_label_set_justify (GTK_LABEL (label), GTK_JUSTIFY_CENTER);
    gtk_widget_set_halign (label, GTK_ALIGN_FILL);
    gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
    gtk_label_set_width_chars (GTK_LABEL (label), 1);
    gtk_label_set_max_width_chars (GTK_LABEL (label), 24);
    gtk_label_set_ellipsize (GTK_LABEL (label),
                            path ? PANGO_ELLIPSIZE_START : PANGO_ELLIPSIZE_END);
    gtk_label_set_selectable (GTK_LABEL (label), TRUE);
    gtk_widget_set_margin_start (label, 4);
    gtk_widget_set_margin_end (label, 4);
    gtk_label_set_line_wrap_mode (GTK_LABEL (label), PANGO_WRAP_WORD_CHAR);
    if (path) {
        gtk_label_set_ellipsize (GTK_LABEL (label), PANGO_ELLIPSIZE_START);
        gtk_label_set_max_width_chars (GTK_LABEL (label), 40);
        gtk_label_set_selectable (GTK_LABEL (label), TRUE);
    }
    gtk_style_context_add_class (gtk_widget_get_style_context (label), style);
    gtk_box_pack_start (GTK_BOX (parent), label, FALSE, FALSE, 0);
    return label;
}

static void
files_slot_inspector_clear (NemoWindowSlot *slot)
{
    gtk_label_set_text (GTK_LABEL (slot->inspector_name), "");
    gtk_label_set_text (GTK_LABEL (slot->inspector_type), "");
    gtk_label_set_text (GTK_LABEL (slot->inspector_modified), "");
    gtk_label_set_text (GTK_LABEL (slot->inspector_path), "");
    gtk_label_set_text (GTK_LABEL (slot->inspector_owner), "");
}

static void
files_slot_preview_cancel (NemoWindowSlot *slot)
{
    if (slot->inspector_preview_cancel != NULL)
        g_cancellable_cancel (slot->inspector_preview_cancel);
    g_clear_object (&slot->inspector_preview_cancel);
    if (slot->inspector_video != NULL) {
        dakkar_video_set_uri (slot->inspector_video, NULL);
        gtk_widget_hide (slot->inspector_video);
    }
    {
        /* audio stops on every selection change, view switch and close */
        GtkWidget *audio = g_object_get_data (G_OBJECT (slot), "dakkar-inspector-audio");
        GtkWidget *grid = g_object_get_data (G_OBJECT (slot), "dakkar-inspector-media-grid");
        if (audio != NULL) {
            dakkar_player_set_uri (audio, NULL);
            gtk_widget_hide (audio);
        }
        if (grid != NULL) dakkar_media_grid_fill (grid, NULL);
    }
}

static void
dakkar_slot_forget_audio (NemoWindowSlot *slot)
{
    g_object_set_data (G_OBJECT (slot), "dakkar-inspector-audio", NULL);
}

static void
dakkar_slot_forget_grid (NemoWindowSlot *slot)
{
    g_object_set_data (G_OBJECT (slot), "dakkar-inspector-media-grid", NULL);
}

/* Media tags + album art arrive asynchronously (GstDiscoverer). */
static void
files_slot_media_ready (DakkarMediaInfo *info, gpointer data)
{
    GWeakRef *ref = data;
    NemoWindowSlot *slot = g_weak_ref_get (ref);
    GtkWidget *grid, *audio;
    if (slot == NULL) return;
    grid = g_object_get_data (G_OBJECT (slot), "dakkar-inspector-media-grid");
    audio = g_object_get_data (G_OBJECT (slot), "dakkar-inspector-audio");
    if (grid != NULL) {
        dakkar_media_grid_fill (grid, info);
        files_slot_inspector_apply_fields (slot);
    }
    if (audio != NULL && gtk_widget_get_visible (audio)) {
        dakkar_player_set_duration_hint (audio, info->duration);
        if (info->problem != NULL) dakkar_player_set_message (audio, info->problem);
    }
    if (slot->inspector_video != NULL && gtk_widget_get_visible (slot->inspector_video)) {
        dakkar_player_set_duration_hint (slot->inspector_video, info->duration);
        if (info->problem != NULL) dakkar_player_set_message (slot->inspector_video, info->problem);
    }
    if (info->cover != NULL && audio != NULL && gtk_widget_get_visible (audio) &&
        slot->inspector_image != NULL) {
        g_object_set_data_full (G_OBJECT (slot->inspector_image), "dakkar-original-preview",
                                g_object_ref (info->cover), g_object_unref);
        g_object_set_data (G_OBJECT (slot->inspector_image), "dakkar-preview-last-size", NULL);
        gtk_image_set_from_pixbuf (GTK_IMAGE (slot->inspector_image), info->cover);
        gtk_widget_set_tooltip_text (slot->inspector_image, info->cover_embedded ?
                                     _("Album art (embedded)") : _("Album art (from the folder)"));
        files_slot_refresh_preview_image_size (slot);
    }
    g_object_unref (slot);
}

static void
files_slot_media_ref_free (gpointer data)
{
    g_weak_ref_clear (data);
    g_free (data);
}

static void
files_slot_media_load (NemoWindowSlot *slot, const char *uri)
{
    GWeakRef *ref = g_new0 (GWeakRef, 1);
    g_weak_ref_init (ref, slot);
    if (slot->inspector_preview_cancel == NULL)
        slot->inspector_preview_cancel = g_cancellable_new ();
    dakkar_media_info_load (uri, 512, slot->inspector_preview_cancel,
                           files_slot_media_ready, ref, files_slot_media_ref_free);
}

/* Image previews are decoded from the file itself (like Quick Look), so
 * they do not depend on the thumbnailer having run yet. */
typedef struct {
    GWeakRef image;
    GCancellable *cancel;
} FilesSlotImageJob;

static void
files_slot_image_job_free (FilesSlotImageJob *job)
{
    g_weak_ref_clear (&job->image);
    g_object_unref (job->cancel);
    g_free (job);
}

static void
files_slot_image_ready (GObject *source, GAsyncResult *res, gpointer data)
{
    FilesSlotImageJob *job = data;
    GdkPixbuf *pixbuf = gdk_pixbuf_new_from_stream_finish (res, NULL);
    GtkWidget *image = g_weak_ref_get (&job->image);
    (void) source;
    if (image != NULL && pixbuf != NULL && !g_cancellable_is_cancelled (job->cancel)) {
        NemoWindowSlot *slot = g_object_get_data (G_OBJECT (image), "dakkar-slot");
        g_object_set_data_full (G_OBJECT (image), "dakkar-original-preview",
                                g_object_ref (pixbuf), g_object_unref);
        g_object_set_data (G_OBJECT (image), "dakkar-preview-last-size", NULL);
        gtk_image_set_from_pixbuf (GTK_IMAGE (image), pixbuf);
        if (slot != NULL) files_slot_refresh_preview_image_size (slot);
    }
    g_clear_object (&pixbuf);
    g_clear_object (&image);
    files_slot_image_job_free (job);
}

static void
files_slot_image_opened (GObject *source, GAsyncResult *res, gpointer data)
{
    FilesSlotImageJob *job = data;
    GFileInputStream *stream = g_file_read_finish (G_FILE (source), res, NULL);
    if (stream != NULL && !g_cancellable_is_cancelled (job->cancel)) {
        gdk_pixbuf_new_from_stream_at_scale_async (G_INPUT_STREAM (stream), 512, 512, TRUE,
                                                   job->cancel, files_slot_image_ready, job);
        g_object_unref (stream);
        return;
    }
    g_clear_object (&stream);
    files_slot_image_job_free (job);
}

static void
files_slot_inspector_refresh (NemoWindowSlot *slot, GList *selection)
{
    NemoFile *file = NULL;
    gboolean directory = FALSE;
    char *name = NULL, *type = NULL, *modified = NULL;
    char *owner = NULL, *uri = NULL, *path = NULL, *mime = NULL;
    char *extra = NULL;
    GdkPixbuf *pixbuf;

    if (slot->inspector_image == NULL) return;
    files_slot_preview_cancel (slot);
    gtk_widget_show (slot->inspector_image);
    gtk_widget_set_tooltip_text (slot->inspector_image, NULL);
    if (selection != NULL && selection->next != NULL) {
        char *summary = NULL, *total = NULL;
        dakkar_preview_multi_summary (selection, &summary, &total);
        gtk_image_set_from_icon_name (GTK_IMAGE (slot->inspector_image),
                                      "edit-select-all-symbolic", GTK_ICON_SIZE_DIALOG);
        gtk_image_set_pixel_size (GTK_IMAGE (slot->inspector_image), 64);
        g_object_set_data (G_OBJECT (slot->inspector_image), "dakkar-original-preview", NULL);
        files_slot_inspector_clear (slot);
        gtk_label_set_text (GTK_LABEL (slot->inspector_name), summary);
        if (slot->quick_actions_folder_size != NULL) {
            gtk_label_set_text (GTK_LABEL (slot->quick_actions_folder_size), total);
            gtk_widget_show (slot->quick_actions_folder_size);
        }
        g_free (summary);
        g_free (total);
        return;
    }
    if (selection != NULL)
        file = NEMO_FILE (selection->data);
    else if (slot->content_view != NULL)
        file = nemo_view_get_directory_as_file (slot->content_view);

    if (file == NULL) {
        gtk_image_clear (GTK_IMAGE (slot->inspector_image));
        g_object_set_data (G_OBJECT (slot->inspector_image), "dakkar-original-preview", NULL);
        files_slot_inspector_clear (slot);
        return;
    }
    directory = nemo_file_is_directory (file);
    name = nemo_file_get_display_name (file);
    type = nemo_file_get_string_attribute (file, "type");
    modified = dakkar_preview_format_date (file);
    owner = nemo_file_get_owner_name (file);
    mime = nemo_file_get_mime_type (file);
    uri = nemo_file_get_uri (file);
    if (uri != NULL) {
        GFile *location = g_file_new_for_uri (uri);
        path = g_file_get_path (location);
        g_object_unref (location);
    }
    /* Folders always show their themed folder icon, never a (foreign,
     * e.g. Dolphin) folder thumbnail; fetch it at the device scale. */
    pixbuf = nemo_file_get_icon_pixbuf (file, 256, TRUE,
        MAX (1, gtk_widget_get_scale_factor (slot->inspector_image)),
        directory ? 0 : NEMO_FILE_ICON_FLAGS_USE_THUMBNAILS);
    g_object_set_data_full (G_OBJECT (slot->inspector_image),
        "dakkar-original-preview", pixbuf ? g_object_ref (pixbuf) : NULL,
        pixbuf ? (GDestroyNotify)g_object_unref : NULL);
    g_object_set_data (G_OBJECT (slot->inspector_image),
                       "dakkar-preview-last-size", NULL);
    if (pixbuf != NULL) {
        /* Replaced right below by the scale-aware surface. */
        gtk_image_set_from_pixbuf (GTK_IMAGE (slot->inspector_image), pixbuf);
        g_object_unref (pixbuf);
    } else {
        gtk_image_set_from_icon_name (GTK_IMAGE (slot->inspector_image),
                                      directory ? "folder" : "text-x-generic",
                                      GTK_ICON_SIZE_DIALOG);
        gtk_image_set_pixel_size (GTK_IMAGE (slot->inspector_image), 96);
    }
    files_slot_refresh_preview_image_size (slot);
    if (!directory && mime != NULL && uri != NULL) {
        /* Without the GStreamer plugins keep the thumbnail/icon instead of
         * an empty player. */
        if (g_str_has_prefix (mime, "video/") && slot->inspector_video != NULL &&
            dakkar_video_supported ()) {
            /* Same inline (muted) player for every view. */
            gtk_widget_hide (slot->inspector_image);
            gtk_widget_show (slot->inspector_video);
            dakkar_video_set_uri (slot->inspector_video, uri);
            files_slot_media_load (slot, uri);
        } else if (dakkar_media_is_audio_mime (mime) && dakkar_media_supported () &&
                   g_object_get_data (G_OBJECT (slot), "dakkar-inspector-audio") != NULL) {
            /* Music icon (the file's icon) until the album art arrives. */
            GtkWidget *audio = g_object_get_data (G_OBJECT (slot), "dakkar-inspector-audio");
            gtk_widget_show (audio);
            dakkar_player_set_uri (audio, uri);
            files_slot_media_load (slot, uri);
        } else if (g_str_has_prefix (mime, "image/")) {
            FilesSlotImageJob *job = g_new0 (FilesSlotImageJob, 1);
            GFile *location = g_file_new_for_uri (uri);
            slot->inspector_preview_cancel = g_cancellable_new ();
            g_weak_ref_init (&job->image, slot->inspector_image);
            job->cancel = g_object_ref (slot->inspector_preview_cancel);
            g_file_read_async (location, G_PRIORITY_DEFAULT, job->cancel,
                               files_slot_image_opened, job);
            g_object_unref (location);
        }
    }
    gtk_label_set_text (GTK_LABEL (slot->inspector_name), name ? name : "");
    if (directory) {
        gtk_label_set_text (GTK_LABEL (slot->inspector_type), _("Folder"));
    } else {
        gtk_label_set_text (GTK_LABEL (slot->inspector_type), type ? type : "");
    }
    gtk_label_set_text (GTK_LABEL (slot->inspector_modified),
                        modified ? modified : "");
    gtk_label_set_text (GTK_LABEL (slot->inspector_path),
                        path ? path : (uri ? uri : ""));
    if (!g_settings_get_boolean (nemo_preferences, NEMO_PREFERENCES_MILLER_PREVIEW_DETAILS))
        extra = NULL;   /* "Show file owner and permissions" is off */
    else if (owner != NULL && nemo_file_can_get_permissions (file))
        extra = g_strdup_printf (_("Owner: %s  ·  Permissions: %04o"),
                                 owner, nemo_file_get_permissions (file) & 07777);
    else if (owner != NULL)
        extra = g_strdup_printf (_("Owner: %s"), owner);
    gtk_label_set_text (GTK_LABEL (slot->inspector_owner), extra ? extra : "");
    g_free (name);
    g_free (type);
    g_free (modified);
    g_free (owner);
    g_free (mime);
    g_free (uri);
    g_free (path);
    g_free (extra);
}

/* Space opens Quick Look for the single selected item, in every view. */
gboolean
nemo_window_slot_toggle_quick_look (NemoWindowSlot *slot)
{
    GList *selection;
    gboolean handled = FALSE;
    g_return_val_if_fail (NEMO_IS_WINDOW_SLOT (slot), FALSE);
    if (dakkar_quick_look_is_open ()) {
        dakkar_quick_look_close ();
        return TRUE;
    }
    if (slot->content_view == NULL ||
        !g_settings_get_boolean (nemo_preferences, NEMO_PREFERENCES_MILLER_QUICK_LOOK))
        return FALSE;
    selection = nemo_view_get_selection (slot->content_view);
    if (selection != NULL && selection->next == NULL) {
        /* one player at a time: pause the sidebar preview */
        GtkWidget *audio = g_object_get_data (G_OBJECT (slot), "dakkar-inspector-audio");
        if (dakkar_player_is_playing (audio)) dakkar_player_toggle (audio);
        if (dakkar_player_is_playing (slot->inspector_video)) dakkar_player_toggle (slot->inspector_video);
        dakkar_quick_look_toggle (GTK_WIDGET (slot), NEMO_FILE (selection->data));
        handled = TRUE;
    }
    nemo_file_list_free (selection);
    return handled;
}

/* The same bounded GIO size worker is used in every file view.  Unlike
 * Miller, the classic views expose the result above their Quick Actions.
 * No extra overlay or blocking recursive file IO is added to the views. */
typedef struct {
    NemoWindowSlot *slot;
    GCancellable *cancel;
    GWeakRef label;
    char *uri;
} FilesSlotSizeJob;

static void
files_slot_size_cancel (NemoWindowSlot *slot)
{
    if (slot->quick_actions_size_cancel)
        g_cancellable_cancel (slot->quick_actions_size_cancel);
    g_clear_object (&slot->quick_actions_size_cancel);
    g_clear_pointer (&slot->quick_actions_size_uri, g_free);
    slot->quick_actions_size_time_us = 0;
}

static void
files_slot_size_done (GObject *source, GAsyncResult *res, gpointer data)
{
    FilesSlotSizeJob *job = data;
    GError *error = NULL;
    NemoV5FolderSize *result = nemo_v5_folder_size_finish (G_FILE (source),
                                                             res, &error);
    GtkWidget *label = g_weak_ref_get (&job->label);
    if (label != NULL && job->slot->quick_actions_size_cancel == job->cancel &&
        !g_cancellable_is_cancelled (job->cancel) &&
        g_strcmp0 (job->slot->quick_actions_size_uri, job->uri) == 0) {
        if (result != NULL) {
            /* Same item count as the status bar of every view: the folder's
             * direct children (Nemo's own directory count when known), plus
             * the recursive total size, explicitly labeled as such. */
            NemoFile *folder_file = nemo_file_get_existing_by_uri (job->uri);
            guint direct = result->direct;
            guint nemo_count = 0;
            gboolean unreadable = FALSE;
            char *size, *items, *formatted;
            if (folder_file != NULL &&
                nemo_file_get_directory_item_count (folder_file, &nemo_count,
                                                    &unreadable) &&
                !unreadable)
                direct = nemo_count;
            nemo_file_unref (folder_file);
            size = g_format_size (result->bytes);
            items = g_strdup_printf (ngettext ("%u item", "%u items", direct), direct);
            formatted = g_strdup_printf (result->partial ?
                /* Translators: two lines: "6 items" / "at least 1.2 GB in total (incl. subfolders; partial)" */
                _("%s\nat least %s in total (incl. subfolders; partial)") :
                /* Translators: two lines: "6 items" / "1.2 GB in total (incl. subfolders)" */
                _("%s\n%s in total (incl. subfolders)"),
                items, size);
            gtk_label_set_text (GTK_LABEL (label), formatted);
            job->slot->quick_actions_size_time_us = g_get_monotonic_time ();
            g_free (formatted);
            g_free (items);
            g_free (size);
        } else if (error != NULL &&
                   !g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
            gtk_label_set_text (GTK_LABEL (label), _("Folder size unavailable"));
        }
    }
    if (label != NULL) g_object_unref (label);
    g_free (result);
    g_clear_error (&error);
    g_weak_ref_clear (&job->label);
    g_object_unref (job->slot);
    g_object_unref (job->cancel);
    g_free (job->uri);
    g_free (job);
}

static void
files_slot_size_refresh (NemoWindowSlot *slot, GList *selection)
{
    GtkWidget *label = slot->quick_actions_folder_size;
    GFile *folder;
    char *uri;
    FilesSlotSizeJob *job;
    if (label == NULL) return;
    NemoFile *file = selection != NULL && selection->next == NULL ?
        NEMO_FILE (selection->data) :
        (selection == NULL && slot->content_view != NULL ?
         nemo_view_get_directory_as_file (slot->content_view) : NULL);
    if (file != NULL && (selection == NULL || selection->next == NULL) &&
        !nemo_file_is_directory (file)) {
        /* Regular file: its own size, same field as a folder's size. */
        goffset size = nemo_file_get_size (file);
        files_slot_size_cancel (slot);
        if (size >= 0) {
            char *text = g_format_size ((guint64) size);
            gtk_label_set_text (GTK_LABEL (label), text);
            gtk_widget_show (label);
            g_free (text);
        } else {
            gtk_widget_hide (label);
        }
        return;
    }
    if (file == NULL || (selection != NULL && selection->next != NULL) ||
        !g_settings_get_boolean (nemo_preferences, "files-background-folder-size")) {
        files_slot_size_cancel (slot);
        if (selection == NULL || selection->next == NULL)
            gtk_widget_hide (label);
        return;
    }
    folder = nemo_file_get_location (file);
    if (folder == NULL) return;
    gtk_widget_show (label);
    if (!g_file_is_native (folder)) {
        files_slot_size_cancel (slot);
        gtk_label_set_text (GTK_LABEL (label),
                            _("Remote folder: automatic size scan disabled"));
        g_object_unref (folder);
        return;
    }
    uri = g_file_get_uri (folder);
    /* Do not restart the same in-flight worker on each selection refresh. */
    if (g_strcmp0 (uri, slot->quick_actions_size_uri) == 0 &&
        slot->quick_actions_size_cancel != NULL &&
        (slot->quick_actions_size_time_us == 0 ||
         g_get_monotonic_time () - slot->quick_actions_size_time_us <
           30 * G_USEC_PER_SEC)) {
        g_free (uri);
        g_object_unref (folder);
        return;
    }
    files_slot_size_cancel (slot);
    slot->quick_actions_size_uri = uri;
    slot->quick_actions_size_cancel = g_cancellable_new ();
    gtk_label_set_text (GTK_LABEL (label), _("Calculating folder size…"));
    job = g_new0 (FilesSlotSizeJob, 1);
    job->slot = g_object_ref (slot);
    job->cancel = g_object_ref (slot->quick_actions_size_cancel);
    job->uri = g_strdup (uri);
    g_weak_ref_init (&job->label, label);
    nemo_v5_folder_size_async (folder, job->cancel, files_slot_size_done, job);
    g_object_unref (folder);
}

static void
files_slot_size_destroy (GtkWidget *widget, NemoWindowSlot *slot)
{
    files_slot_size_cancel (slot);
    files_slot_preview_cancel (slot);
    dakkar_quick_look_close ();
}

/* One visibility setting for the details sidebar, shared by every view,
 * tab and window (the status-bar toggle writes it). */
static gboolean
files_slot_inspector_wanted (void)
{
    return g_settings_get_boolean (nemo_preferences, NEMO_PREFERENCES_MILLER_SHOW_PREVIEW);
}

static void
files_slot_qa_refresh (NemoWindowSlot *slot)
{
    GList *selection = NULL;
    if (slot->quick_actions_panel == NULL) return;
    gboolean advanced_open = GPOINTER_TO_INT (
        g_object_get_data (G_OBJECT (slot), "dakkar-advanced-mode"));
    slot->quick_actions_collapsed = !files_slot_inspector_wanted ();
    if (slot->content_view == NULL ||
        (slot->quick_actions_collapsed && !advanced_open)) {
        files_slot_size_cancel (slot);
        files_slot_preview_cancel (slot);
        gtk_widget_hide (slot->quick_actions_panel);
        return;
    }
    gtk_widget_show (slot->quick_actions_panel);
    selection = nemo_view_get_selection (slot->content_view);
    files_slot_inspector_refresh (slot, selection);
    files_slot_size_refresh (slot, selection);
    files_slot_inspector_apply_fields (slot);
    nemo_file_list_free (selection);
    if (!g_settings_get_boolean (nemo_preferences, NEMO_QA_ENABLED)) {
        gtk_widget_hide (slot->quick_actions_section);
        return;
    }
    gtk_widget_show (slot->quick_actions_section);
    dakkar_qa_populate (slot->quick_actions_list, GTK_WIDGET (slot), slot->content_view,
                       gtk_widget_get_allocated_width (slot->quick_actions_panel));
}

gboolean
nemo_window_slot_get_inspector_visible (NemoWindowSlot *slot)
{
    g_return_val_if_fail (NEMO_IS_WINDOW_SLOT (slot), FALSE);
    return files_slot_inspector_wanted ();
}

void
nemo_window_slot_set_inspector_visible (NemoWindowSlot *slot, gboolean visible)
{
    g_return_if_fail (NEMO_IS_WINDOW_SLOT (slot));
    /* Writing the shared key refreshes every slot via "changed". */
    g_settings_set_boolean (nemo_preferences, NEMO_PREFERENCES_MILLER_SHOW_PREVIEW, visible);
    files_slot_qa_refresh (slot);
}

static void
files_slot_inspector_responsive (GtkWidget *panel, GdkRectangle *allocation,
                                 NemoWindowSlot *slot)
{
    GtkWidget *metadata[] = {slot->inspector_name, slot->inspector_type,
        slot->quick_actions_folder_size, slot->inspector_modified,
        slot->inspector_path, slot->inspector_owner};
    gint width = allocation->width;

    if (width < 1) return;
    dakkar_details_adapt_metadata (metadata, G_N_ELEMENTS (metadata), width, 4);
    /* As in Miller, the metadata and buttons must respond to the actual
     * allocated inspector width, never their GTK preferred/natural width. */
    if (slot->quick_actions_list != NULL)
        nemo_qa_adapt_buttons (slot->quick_actions_list, width);
    files_slot_refresh_preview_image_size (slot);
    (void) panel;
}

static void
files_slot_qa_selection_changed (NemoView *view, NemoWindowSlot *slot)
{
    if (view == slot->content_view) files_slot_qa_refresh (slot);
}

static void
files_slot_qa_settings_changed (GSettings *settings, gchar *key,
                                NemoWindowSlot *slot)
{
    if (g_str_has_prefix (key, "miller-quick-actions-") ||
        g_str_equal (key, NEMO_PREFERENCES_MILLER_SHOW_PREVIEW) ||
        g_str_equal (key, NEMO_PREFERENCES_MILLER_PREVIEW_DETAILS) ||
        g_str_equal (key, "files-background-folder-size") ||
        g_str_equal (key, "dakkar-inspector-fields"))
        files_slot_qa_refresh (slot);
    if (g_str_equal (key, "dakkar-inspector-width") &&
        !slot->inspector_repositioning && slot->dakkar_content_row != NULL) {
        slot->inspector_width = dakkar_inspector_saved_int (key, 92, 650);
        slot->inspector_width_ready = FALSE;
        files_slot_inspector_resize_allocation (slot->dakkar_content_row,
            &(GdkRectangle){0, 0,
              gtk_widget_get_allocated_width (slot->dakkar_content_row),
              gtk_widget_get_allocated_height (slot->dakkar_content_row)}, slot);
    }
    if (g_str_equal (key, "dakkar-inspector-preview-height") &&
        !slot->inspector_height_repositioning &&
        slot->inspector_vertical_paned != NULL) {
        slot->inspector_height_ready = FALSE;
        files_slot_preview_allocation (slot->inspector_vertical_paned,
            &(GdkRectangle){0, 0,
              gtk_widget_get_allocated_width (slot->inspector_vertical_paned),
              gtk_widget_get_allocated_height (slot->inspector_vertical_paned)}, slot);
    }
    (void) settings;
}
