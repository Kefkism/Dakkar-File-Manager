/* Configurable Miller Columns Quick Actions. Copyright (C) 2026.
 * GPL-2.0-or-later. The action registry has stable IDs, not translated keys. */
#ifndef NEMO_QUICK_ACTIONS_H
#define NEMO_QUICK_ACTIONS_H
#include <gtk/gtk.h>
#include <gio/gio.h>

typedef enum {
    NEMO_QA_FILE = 1u << 0,
    NEMO_QA_DIRECTORY = 1u << 1,
    NEMO_QA_MULTI = 1u << 2,
    NEMO_QA_EMPTY = 1u << 3,
    NEMO_QA_IMAGE = 1u << 4,
    NEMO_QA_VIDEO = 1u << 5,
    NEMO_QA_AUDIO = 1u << 6,
    NEMO_QA_ARCHIVE = 1u << 7,
    NEMO_QA_DOCUMENT = 1u << 8,
    NEMO_QA_GIT = 1u << 9
} NemoQAContext;

typedef enum {
    NEMO_QA_SAFE,          /* no mutation, can run now */
    NEMO_QA_CONTROLLED,    /* implemented, requires Nemo's existing safety gate */
    NEMO_QA_NOT_READY     /* exposed for configuration, no working backend yet */
} NemoQASafety;

typedef struct {
    const char *id;
    const char *label;
    const char *icon;
    const char *category;
    unsigned contexts;
    NemoQASafety safety;
    gboolean recommended;
} NemoQAAction;

#define NEMO_QA_ENABLED "miller-quick-actions-enabled"
#define NEMO_QA_MODE "miller-quick-actions-mode"
#define NEMO_QA_UNAVAILABLE "miller-quick-actions-unavailable"
#define NEMO_QA_LAYOUT "miller-quick-actions-layout"
#define NEMO_QA_PINNED "miller-quick-actions-pinned"
#define NEMO_QA_MANUAL "miller-quick-actions-manual"
#define NEMO_QA_CUSTOM "miller-quick-actions-custom"
#define NEMO_QA_LIMIT "miller-quick-actions-limit"

const NemoQAAction *nemo_quick_actions_catalog (gsize *n);
const NemoQAAction *nemo_quick_actions_find (const gchar *id);
GtkWidget *nemo_quick_actions_preferences_page (GSettings *settings);
/* Presentation policy shared by Miller, List, Icons and Compact. */
gboolean nemo_qa_custom_context_matches (const gchar *contexts, unsigned context);
void nemo_qa_button_register (GtkWidget *button, GtkWidget *icon,
                              GtkWidget *title, GtkWidget *subtitle,
                              gboolean force_icons);
void nemo_qa_adapt_buttons (GtkWidget *container, gint panel_width);
void nemo_qa_button_feedback (GtkButton *button, gboolean success);
/* Archive manager dispatch shared by Miller and classic views. Supported
 * backends are explicitly detected, and no archive data is touched by Nemo. */
gboolean nemo_qa_archive_matches (const char *mime, const char *filename);
gboolean nemo_qa_archive_handler_available (const char *action_id);
gboolean nemo_qa_archive_launch (GtkWindow *parent, const char *action_id,
                                  GFile *archive, GError **error);

/* Configured custom actions: a(ssssss) = id, title, icon, kind,
 * command/script path, contexts (file/directory/multiple/all). */
#endif
