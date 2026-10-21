/* Dakkar: one Quick Actions implementation for every file view
 * (Icons, List, Compact and Miller Columns).  GPL-2.0-or-later.
 *
 * The panel is owned by the window slot; the view only supplies its
 * selection and the folder it is showing.  Every action is resolved and
 * executed here, so all views offer exactly the same set. */
#ifndef DAKKAR_QA_RUN_H
#define DAKKAR_QA_RUN_H

#include <gtk/gtk.h>
#include "nemo-view.h"
#include "nemo-quick-actions.h"

typedef struct {
    GtkWidget *anchor;   /* any widget inside the window (dialog parent) */
    NemoView *view;      /* referenced */
    GList *selection;    /* NemoFile list, owned */
    NemoFile *folder;    /* owned; folder shown when nothing is selected */
    gchar *terminal_subtitle; /* owned; scratch for the terminal subtitles */
} DakkarQAContext;

DakkarQAContext *dakkar_qa_context_new (GtkWidget *anchor, NemoView *view);
void dakkar_qa_context_free (DakkarQAContext *ctx);

/* Context flags (NemoQAContext bits) for the current selection. */
unsigned dakkar_qa_context_flags (DakkarQAContext *ctx);

/* Rebuild the buttons in container (a vertical GtkBox) for view. */
void dakkar_qa_populate (GtkWidget *container, GtkWidget *anchor,
                        NemoView *view, gint panel_width);

/* Run one action (built-in id or custom id).  Returns TRUE when the action
 * completed synchronously and successfully (used for button feedback). */
gboolean dakkar_qa_dispatch (DakkarQAContext *ctx, const char *id);

#endif
