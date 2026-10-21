/* nemo-statusbar.h
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
 *
 * 
 */

#ifndef NEMO_STATUSBAR_H
#define NEMO_STATUSBAR_H

#include <gtk/gtk.h>
#include <gio/gio.h>
#include "nemo-window.h"
#include "nemo-window-slot.h"
#include "nemo-view.h"

typedef struct _NemoStatusBar      NemoStatusBar;
typedef struct _NemoStatusBarClass NemoStatusBarClass;


#define NEMO_TYPE_STATUS_BAR                 (nemo_status_bar_get_type ())
#define NEMO_STATUS_BAR(obj)                 (G_TYPE_CHECK_INSTANCE_CAST ((obj), NEMO_TYPE_STATUS_BAR, NemoStatusBar))
#define NEMO_STATUS_BAR_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST ((klass), NEMO_TYPE_STATUS_BAR, NemoStatusBarClass))
#define NEMO_IS_STATUS_BAR(obj)              (G_TYPE_CHECK_INSTANCE_TYPE ((obj), NEMO_TYPE_STATUS_BAR))
#define NEMO_IS_STATUS_BAR_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE ((klass), NEMO_TYPE_STATUS_BAR))
#define NEMO_STATUS_BAR_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS ((obj), NEMO_TYPE_STATUS_BAR, NemoStatusBarClass))

#define NEMO_STATUSBAR_ICON_SIZE_NAME "statusbar-icon"
#define NEMO_STATUSBAR_ICON_SIZE 11

/* Visual state of the minimized transfer bar in the status bar. */
typedef enum {
    NEMO_STATUS_BAR_TRANSFER_NORMAL,     /* copying / moving / deleting */
    NEMO_STATUS_BAR_TRANSFER_VERIFYING,  /* checksum read-back phase */
    NEMO_STATUS_BAR_TRANSFER_PAUSED,     /* every transfer is held */
    NEMO_STATUS_BAR_TRANSFER_ERROR       /* at least one file failed */
} NemoStatusBarTransferState;

struct _NemoStatusBar
{
    GtkBox parent;
    NemoWindow *window;
    GtkWidget *real_statusbar;
    GtkWidget *transfer_stack;
    GtkWidget *transfer_button;
    GtkWidget *transfer_progress; /* custom-drawn filled bar with caption */
    gchar *transfer_text;
    gdouble transfer_fraction;    /* < 0: indeterminate */
    gdouble transfer_secondary;   /* checksum progress of the current file */
    guint transfer_pulse;
    NemoStatusBarTransferState transfer_state;
    GtkWidget *transfer_box;    /* stack child: bar button + pause */
    GtkWidget *transfer_pause;

    GtkWidget *zoom_slider;
    GtkWidget *inspector_button; /* mirrored footer collapse, far right */

    GtkWidget *sidebar_mode_button; /* toggles Places/Tree; icon shows destination */
    GtkWidget *show_button;
    GtkWidget *hide_button;
    GtkWidget *separator;
};

struct _NemoStatusBarClass
{
    GtkBoxClass parent_class;
};

GType    nemo_status_bar_get_type (void) G_GNUC_CONST;

GtkWidget *nemo_status_bar_new (NemoWindow *window);

GtkWidget *nemo_status_bar_get_real_statusbar (NemoStatusBar *bar);

/* Set NULL label to restore the centered free-space/selection text. */
void       nemo_status_bar_set_transfer_paused (NemoStatusBar *bar,
                                               gboolean visible,
                                               gboolean paused);
void       nemo_status_bar_set_transfer (NemoStatusBar *bar,
                                          const char *label, double fraction,
                                          GObject *progress_handler);
/* Colour/state of the transfer bar; secondary is the checksum fraction of
 * the current file while verifying (ignored otherwise). */
void       nemo_status_bar_set_transfer_state (NemoStatusBar *bar,
                                                NemoStatusBarTransferState state,
                                                double secondary);
void       nemo_status_bar_sync_button_states (NemoStatusBar *bar);

void       nemo_status_bar_sync_zoom_widgets (NemoStatusBar *bar);
void       nemo_status_bar_sync_inspector_button (NemoStatusBar *bar);

#endif /* NEMO_STATUSBAR_H */
