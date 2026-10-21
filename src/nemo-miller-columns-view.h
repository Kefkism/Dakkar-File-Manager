/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*-
 *
 * nemo-miller-columns-view.h: Header for the Miller Columns view
 *
 * Copyright (C) 2026 Kefkism
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 */

#ifndef NEMO_MILLER_COLUMNS_VIEW_H
#define NEMO_MILLER_COLUMNS_VIEW_H

#include "nemo-view.h"

#define NEMO_TYPE_MILLER_COLUMNS_VIEW nemo_miller_columns_view_get_type()
#define NEMO_MILLER_COLUMNS_VIEW(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST ((obj), NEMO_TYPE_MILLER_COLUMNS_VIEW, NemoMillerColumnsView))
#define NEMO_MILLER_COLUMNS_VIEW_CLASS(klass) \
	(G_TYPE_CHECK_CLASS_CAST ((klass), NEMO_TYPE_MILLER_COLUMNS_VIEW, NemoMillerColumnsViewClass))
#define NEMO_IS_MILLER_COLUMNS_VIEW(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE ((obj), NEMO_TYPE_MILLER_COLUMNS_VIEW))
#define NEMO_IS_MILLER_COLUMNS_VIEW_CLASS(klass) \
	(G_TYPE_CHECK_CLASS_TYPE ((klass), NEMO_TYPE_MILLER_COLUMNS_VIEW))
#define NEMO_MILLER_COLUMNS_VIEW_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS ((obj), NEMO_TYPE_MILLER_COLUMNS_VIEW, NemoMillerColumnsViewClass))

#define NEMO_MILLER_VIEW_ID "OAFIID:Nemo_File_Manager_Miller_Columns_View"

typedef struct NemoMillerColumnsViewDetails NemoMillerColumnsViewDetails;

typedef struct NemoMillerColumnsView NemoMillerColumnsView;
typedef struct NemoMillerColumnsViewClass NemoMillerColumnsViewClass;

struct NemoMillerColumnsView {
	NemoView parent;
	NemoMillerColumnsViewDetails *details;
};

struct NemoMillerColumnsViewClass {
	NemoViewClass parent_class;
};

GType   nemo_miller_columns_view_get_type (void);

void    nemo_miller_columns_view_register (void);
void    nemo_miller_columns_view_resort_groups (NemoMillerColumnsView *view);
/* Borrowed reference to the active column's backing NemoFile. */
NemoFile *nemo_miller_columns_view_get_action_directory (NemoMillerColumnsView *view);
/* Newly allocated URI for the directory the user explicitly opened. */
char *nemo_miller_columns_view_get_paste_uri (NemoMillerColumnsView *view);
/* Toolbar sorting, shared with the View > Sort Items menu. */
NemoFileSortType nemo_miller_columns_view_get_sort_order (NemoMillerColumnsView *view);
gboolean nemo_miller_columns_view_get_sort_reversed (NemoMillerColumnsView *view);
void nemo_miller_columns_view_set_sort_order (NemoMillerColumnsView *view, NemoFileSortType order);
void nemo_miller_columns_view_set_sort_reversed (NemoMillerColumnsView *view, gboolean reversed);

#endif /* NEMO_MILLER_COLUMNS_VIEW_H */
