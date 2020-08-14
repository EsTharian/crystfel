/*
 * crystfelindexingopts.h
 *
 * A GTK widget to set indexing options
 *
 * Copyright © 2020 Deutsches Elektronen-Synchrotron DESY,
 *                  a research centre of the Helmholtz Association.
 *
 * Authors:
 *  2020 Thomas White <taw@physics.org>
 *
 * This file is part of CrystFEL.
 *
 * CrystFEL is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * CrystFEL is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with CrystFEL.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <gtk/gtk.h>
#include <glib-object.h>
#include <errno.h>
#include <math.h>

#include <integration.h>

#include "crystfelindexingopts.h"


G_DEFINE_TYPE(CrystFELIndexingOpts,
              crystfel_indexing_opts,
              GTK_TYPE_NOTEBOOK)


static void crystfel_indexing_opts_class_init(CrystFELIndexingOptsClass *klass)
{
}


static void crystfel_indexing_opts_init(CrystFELIndexingOpts *io)
{
}


static void add_method(GtkListStore *store, const char *name)
{
	GtkTreeIter iter;
	gtk_list_store_append(store, &iter);
	gtk_list_store_set(store, &iter,
	                   0, FALSE,
	                   1, name,
	                   2, FALSE,
	                   3, FALSE,
	                   -1);
}


static GtkWidget *add_tol(GtkGrid *grid, const char *spec_t,
                          const char *unit_t, gint left, gint top)
{
	GtkWidget *spec;
	GtkWidget *entry;
	GtkWidget *unit;

	spec = gtk_label_new(spec_t);
	g_object_set(G_OBJECT(spec), "margin-left", 12, NULL);
	gtk_grid_attach(grid, spec, left, top, 1, 1);

	entry = gtk_entry_new();
	gtk_entry_set_width_chars(GTK_ENTRY(entry), 5);
	gtk_grid_attach(grid, entry, left+1, top, 1, 1);

	unit = gtk_label_new(unit_t);
	g_object_set(G_OBJECT(unit), "margin-right", 12, NULL);
	gtk_grid_attach(grid, unit, left+2, top, 1, 1);

	return entry;
}


static GtkWidget *make_tolerances(CrystFELIndexingOpts *io)
{
	GtkWidget *grid;

	grid = gtk_grid_new();
	gtk_grid_set_row_spacing(GTK_GRID(grid), 4);
	gtk_grid_set_column_spacing(GTK_GRID(grid), 4);
	gtk_container_set_border_width(GTK_CONTAINER(grid), 6);

	io->tols[0] = add_tol(GTK_GRID(grid), "a", "%", 0, 0);
	io->tols[1] = add_tol(GTK_GRID(grid), "b", "%", 4, 0);
	io->tols[2] = add_tol(GTK_GRID(grid), "c", "%", 8, 0);
	io->tols[3] = add_tol(GTK_GRID(grid), "α", "°", 0, 1);
	io->tols[4] = add_tol(GTK_GRID(grid), "β", "°", 4, 1);
	io->tols[5] = add_tol(GTK_GRID(grid), "ɣ", "°", 8, 1);

	return grid;
}


static GtkWidget *make_indexing_methods(CrystFELIndexingOpts *io)
{
	GtkWidget *treeview;
	GtkListStore *store;
	GtkCellRenderer *renderer;
	GtkTreeViewColumn *column;

	store = gtk_list_store_new(4,
	                           G_TYPE_BOOLEAN,  /* Algo on */
	                           G_TYPE_STRING,   /* Algo name */
	                           G_TYPE_BOOLEAN,  /* Prior cell */
	                           G_TYPE_BOOLEAN); /* Prior latt */

	add_method(store, "DirAx");
	add_method(store, "MOSFLM");
	add_method(store, "XDS");
	add_method(store, "XGANDALF");
	add_method(store, "PinkIndexer");
	add_method(store, "TakeTwo");
	add_method(store, "ASDF");
	add_method(store, "Felix");

	treeview = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));

	renderer = gtk_cell_renderer_toggle_new();
	column = gtk_tree_view_column_new_with_attributes(NULL,
	                                                  renderer,
	                                                  "active", 0,
	                                                  NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(treeview), column);

	renderer = gtk_cell_renderer_text_new();
	column = gtk_tree_view_column_new_with_attributes("Method",
	                                                  renderer,
	                                                  "text", 1,
	                                                  NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(treeview), column);

	renderer = gtk_cell_renderer_toggle_new();
	column = gtk_tree_view_column_new_with_attributes("Prior unit cell",
	                                                  renderer,
	                                                  "active", 2,
	                                                  NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(treeview), column);

	renderer = gtk_cell_renderer_toggle_new();
	column = gtk_tree_view_column_new_with_attributes("Prior lattice type",
	                                                  renderer,
	                                                  "active", 3,
	                                                  NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(treeview), column);

	return treeview;
}


static GtkWidget *indexing_parameters(CrystFELIndexingOpts *io)
{
	GtkWidget *box;
	GtkWidget *hbox;
	GtkWidget *label;
	GtkWidget *expander;
	GtkWidget *frame;
	GtkWidget *indexing_methods;
	GtkWidget *tolerances;

	box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
	gtk_container_set_border_width(GTK_CONTAINER(box), 8);

	/* Use unit cell / Cell file chooser */
	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	gtk_box_pack_start(GTK_BOX(box), GTK_WIDGET(hbox),
	                   FALSE, FALSE, 0);
	io->use_cell = gtk_check_button_new_with_label("Use unit cell");
	gtk_box_pack_start(GTK_BOX(hbox), GTK_WIDGET(io->use_cell),
	                   FALSE, FALSE, 0);
	io->cell_chooser = gtk_file_chooser_button_new("Unit cell file",
	                                               GTK_FILE_CHOOSER_ACTION_OPEN);
	gtk_file_chooser_set_local_only(GTK_FILE_CHOOSER(io->cell_chooser),
	                                TRUE);
	gtk_box_pack_start(GTK_BOX(hbox), GTK_WIDGET(io->cell_chooser),
	                   FALSE, FALSE, 0);

	/* Indexing method selector */
	io->auto_indm = gtk_check_button_new_with_label("Automatically choose the indexing methods");
	gtk_box_pack_start(GTK_BOX(box), GTK_WIDGET(io->auto_indm),
	                   FALSE, FALSE, 0);
	expander = gtk_expander_new("Select indexing methods and prior information");
	frame = gtk_frame_new(NULL);
	gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_IN);
	gtk_container_add(GTK_CONTAINER(expander),
	                  GTK_WIDGET(frame));
	indexing_methods = make_indexing_methods(io);
	gtk_container_add(GTK_CONTAINER(frame),
	                  GTK_WIDGET(indexing_methods));
	gtk_box_pack_start(GTK_BOX(box), GTK_WIDGET(expander),
	                   FALSE, FALSE, 0);
	gtk_container_set_border_width(GTK_CONTAINER(frame), 6);

	/* --multi */
	io->multi = gtk_check_button_new_with_label("Attempt to find multiple lattices per frame");
	gtk_box_pack_start(GTK_BOX(box), GTK_WIDGET(io->multi),
	                   FALSE, FALSE, 0);

	/* --no-refine (NB inverse) */
	io->refine = gtk_check_button_new_with_label("Refine the indexing solution");
	gtk_box_pack_start(GTK_BOX(box), GTK_WIDGET(io->refine),
	                   FALSE, FALSE, 0);

	/* --no-retry (NB inverse) */
	io->retry = gtk_check_button_new_with_label("Retry indexing if unsuccessful");
	gtk_box_pack_start(GTK_BOX(box), GTK_WIDGET(io->retry),
	                   FALSE, FALSE, 0);

	/* --no-check-peaks (NB inverse) */
	io->check_peaks = gtk_check_button_new_with_label("Check indexing solutions match peaks");
	gtk_box_pack_start(GTK_BOX(box), GTK_WIDGET(io->check_peaks),
	                   FALSE, FALSE, 0);

	/* --no-check-cell (NB inverse) and --tolerance */
	io->check_cell = gtk_check_button_new_with_label("Check indexing solutions against reference cell");
	gtk_box_pack_start(GTK_BOX(box), GTK_WIDGET(io->check_cell),
	                   FALSE, FALSE, 0);
	expander = gtk_expander_new("Unit cell tolerances");
	gtk_box_pack_start(GTK_BOX(box), GTK_WIDGET(expander),
	                   FALSE, FALSE, 0);
	tolerances = make_tolerances(io);
	gtk_container_add(GTK_CONTAINER(expander), tolerances);

	/* --min-peaks (NB add one) */
	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	gtk_box_pack_start(GTK_BOX(box), GTK_WIDGET(hbox),
	                   FALSE, FALSE, 0);
	io->enable_hitfind = gtk_check_button_new_with_label("Skip frames with fewer than");
	gtk_box_pack_start(GTK_BOX(hbox), GTK_WIDGET(io->enable_hitfind),
	                   FALSE, FALSE, 0);
	io->ignore_fewer_peaks = gtk_entry_new();
	gtk_entry_set_width_chars(GTK_ENTRY(io->ignore_fewer_peaks), 4);
	gtk_box_pack_start(GTK_BOX(hbox), GTK_WIDGET(io->ignore_fewer_peaks),
	                   FALSE, FALSE, 0);
	label = gtk_label_new("peaks");
	gtk_box_pack_start(GTK_BOX(hbox), GTK_WIDGET(label),
	                   FALSE, FALSE, 0);

	return box;
}


static GtkWidget *integration_parameters(CrystFELIndexingOpts *io)
{
	GtkWidget *box;
	GtkWidget *label;
	GtkWidget *hbox;

	box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
	gtk_container_set_border_width(GTK_CONTAINER(box), 8);

	/* --integration=method */
	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	gtk_box_pack_start(GTK_BOX(box), GTK_WIDGET(hbox),
	                   FALSE, FALSE, 0);
	label = gtk_label_new("Integration method:");
	gtk_box_pack_start(GTK_BOX(hbox), GTK_WIDGET(label),
	                   FALSE, FALSE, 0);
	io->integration_combo = gtk_combo_box_text_new();
	gtk_box_pack_start(GTK_BOX(hbox), GTK_WIDGET(io->integration_combo),
	                   FALSE, FALSE, 0);
	gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(io->integration_combo), "none",
	                "No integration (only spot prediction)");
	gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(io->integration_combo), "rings",
	                "Ring summation");
	gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(io->integration_combo), "prof2d",
	                "Two dimensional profile fitting");

	/* -cen */
	io->centering = gtk_check_button_new_with_label("Center integration boxes on observed reflections");
	gtk_box_pack_start(GTK_BOX(box), GTK_WIDGET(io->centering),
	                   FALSE, FALSE, 0);

	/* --overpredict */
	io->overpredict = gtk_check_button_new_with_label("Over-predict reflections (for post-refinement)");
	gtk_box_pack_start(GTK_BOX(box), GTK_WIDGET(io->overpredict),
	                   FALSE, FALSE, 0);

	/* --push-res */
	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	gtk_box_pack_start(GTK_BOX(box), GTK_WIDGET(hbox),
	                   FALSE, FALSE, 0);
	io->limit_res = gtk_check_button_new_with_label("Limit prediction to");
	gtk_box_pack_start(GTK_BOX(hbox), GTK_WIDGET(io->limit_res),
	                   FALSE, FALSE, 0);
	io->push_res = gtk_entry_new();
	gtk_entry_set_width_chars(GTK_ENTRY(io->push_res), 4);
	gtk_box_pack_start(GTK_BOX(hbox), GTK_WIDGET(io->push_res),
	                   FALSE, FALSE, 0);
	label = gtk_label_new("nm-1 above apparent resolution limit");
	gtk_label_set_markup(GTK_LABEL(label),
	                     "nm<sup>-1</sup> above apparent resolution limit");
	gtk_box_pack_start(GTK_BOX(hbox), GTK_WIDGET(label),
	                   FALSE, FALSE, 0);

	/* FIXME: fix-bandwidth, divergence, profile-radius */
	/* FIXME: --int-radius */

	return box;
}


GtkWidget *crystfel_indexing_opts_new()
{
	CrystFELIndexingOpts *io;

	io = g_object_new(CRYSTFEL_TYPE_INDEXING_OPTS, NULL);

	gtk_notebook_append_page(GTK_NOTEBOOK(io),
	                         indexing_parameters(io),
	                         gtk_label_new("Indexing"));

	gtk_notebook_append_page(GTK_NOTEBOOK(io),
	                         integration_parameters(io),
	                         gtk_label_new("Integration"));

	gtk_widget_show_all(GTK_WIDGET(io));
	return GTK_WIDGET(io);
}


char *crystfel_indexing_opts_get_cell_file(CrystFELIndexingOpts *opts)
{
	if ( gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(opts->use_cell)) ) {
		return gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(opts->cell_chooser));
	} else {
		return NULL;
	}
}


char *crystfel_indexing_opts_get_indexing_method_string(CrystFELIndexingOpts *opts)
{
	return strdup("dirax");  /* FIXME! */
}


int crystfel_indexing_opts_get_multi_lattice(CrystFELIndexingOpts *opts)
{
	return gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(opts->multi));
}


int crystfel_indexing_opts_get_refine(CrystFELIndexingOpts *opts)
{
	return gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(opts->refine));
}


int crystfel_indexing_opts_get_retry(CrystFELIndexingOpts *opts)
{
	return gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(opts->retry));
}


int crystfel_indexing_opts_get_peak_check(CrystFELIndexingOpts *opts)
{
	return gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(opts->check_peaks));
}


int crystfel_indexing_opts_get_cell_check(CrystFELIndexingOpts *opts)
{
	return gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(opts->check_cell));
}


void crystfel_indexing_opts_get_tolerances(CrystFELIndexingOpts *opts,
                                           float *tols)
{
	int i;
	for ( i=0; i<3; i++ ) {
		float tol;
		char *rval;
		const gchar *text = gtk_entry_get_text(GTK_ENTRY(opts->tols[i]));
		errno = 0;
		tol = strtod(text, &rval);
		if ( *rval != '\0' ) {
			printf("Invalid tolerance '%s'\n", text);
		} else {
			tols[i] = tol / 100.0;
		}
	}
	for ( i=3; i<6; i++ ) {
		float tol;
		char *rval;
		const gchar *text = gtk_entry_get_text(GTK_ENTRY(opts->tols[i]));
		errno = 0;
		tol = strtod(text, &rval);
		if ( *rval != '\0' ) {
			printf("Invalid tolerance '%s'\n", text);
		} else {
			tols[i] = deg2rad(tol);
		}
	}
}


int crystfel_indexing_opts_get_min_peaks(CrystFELIndexingOpts *opts)
{
	if ( gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(opts->enable_hitfind)) ) {
		const gchar *text;
		int fewer_peaks;
		char *rval;
		text = gtk_entry_get_text(GTK_ENTRY(opts->ignore_fewer_peaks));
		errno = 0;
		fewer_peaks = strtod(text, &rval);
		if ( *rval != '\0' ) {
			printf("Invalid value for minimum number of peaks (%s)\n",
			      rval);
			return 0;
		}
		/* Subtract one because the dialog box says to skip
		 * frames with "FEWER THAN" this number */
		return fewer_peaks - 1;
	} else {
		return 0;
	}
}


char *crystfel_indexing_opts_get_integration_method_string(CrystFELIndexingOpts *opts)
{
	const gchar *id;
	char method[64];

	id = gtk_combo_box_get_active_id(GTK_COMBO_BOX(opts->integration_combo));
	if ( id == NULL ) return strdup("none");

	strcpy(method, id);
	if ( gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(opts->centering)) ) {
		strcat(method, "-cen");
	}

	return strdup(method);
}


int crystfel_indexing_opts_get_overpredict(CrystFELIndexingOpts *opts)
{
	return gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(opts->overpredict));
}


float crystfel_indexing_opts_get_push_res(CrystFELIndexingOpts *opts)
{
	if ( gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(opts->limit_res)) ) {
		return INFINITY;
	} else {
		const gchar *text;
		float push_res;
		char *rval;
		text = gtk_entry_get_text(GTK_ENTRY(opts->ignore_fewer_peaks));
		errno = 0;
		push_res = strtof(text, &rval);
		if ( *rval != '\0' ) {
			printf("Invalid value for push-res (%s)\n",
			      rval);
			return INFINITY;
		}
		return push_res;
	}
}


void crystfel_indexing_opts_set_cell_file(CrystFELIndexingOpts *opts,
                                          const char *cell_file)
{
	if ( cell_file != NULL ) {
		gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(opts->cell_chooser),
		                              cell_file);
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(opts->use_cell),
		                             TRUE);
	} else {
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(opts->use_cell),
		                             FALSE);
	}
}


static const char *integration_method_id(IntegrationMethod meth)
{
	switch ( meth ) {
		case INTEGRATION_NONE : return "none";
		case INTEGRATION_RINGS : return "rings";
		case INTEGRATION_PROF2D : return "prof2d";
		default : return "none";
	}
}


void crystfel_indexing_opts_set_indexing_method_string(CrystFELIndexingOpts *opts,
                                                       const char *indm_str)
{
	/* FIXME */
}


void crystfel_indexing_opts_set_multi_lattice(CrystFELIndexingOpts *opts,
                                              int multi)
{
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(opts->multi),
	                             multi);
}


void crystfel_indexing_opts_set_refine(CrystFELIndexingOpts *opts,
                                       int refine)
{
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(opts->refine),
	                             refine);
}


void crystfel_indexing_opts_set_retry(CrystFELIndexingOpts *opts,
                                      int retry)
{
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(opts->retry),
	                             retry);
}


void crystfel_indexing_opts_set_peak_check(CrystFELIndexingOpts *opts,
                                           int peak_check)

{
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(opts->check_peaks),
	                             peak_check);
}


void crystfel_indexing_opts_set_cell_check(CrystFELIndexingOpts *opts,
                                           int cell_check)
{
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(opts->check_cell),
	                             cell_check);
}


void crystfel_indexing_opts_set_tolerances(CrystFELIndexingOpts *opts,
                                           float *tols)
{
	int i;
	for ( i=0; i<3; i++ ) {
		char tmp[64];
		snprintf(tmp, 63, "%f", tols[i]*100.0);
		gtk_entry_set_text(GTK_ENTRY(opts->tols[i]), tmp);
	}
	for ( i=3; i<6; i++ ) {
		char tmp[64];
		snprintf(tmp, 63, "%f", rad2deg(tols[i]));
		gtk_entry_set_text(GTK_ENTRY(opts->tols[i]), tmp);
	}
}


void crystfel_indexing_opts_set_min_peaks(CrystFELIndexingOpts *opts,
                                          int min_peaks)
{
	char tmp[64];

	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(opts->enable_hitfind),
	                             (min_peaks > 0));

	/* Plus one because dialog says skip when "fewer than" X peaks */
	snprintf(tmp, 63, "%i", min_peaks+1);
	gtk_entry_set_text(GTK_ENTRY(opts->ignore_fewer_peaks), tmp);
}


void crystfel_indexing_opts_set_integration_method_string(CrystFELIndexingOpts *opts,
                                                          const char *integr_str)
{
	IntegrationMethod meth;
	int err;

	meth = integration_method(integr_str, &err);
	if ( !err ) {
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(opts->centering),
		                             meth & INTEGRATION_CENTER);
		gtk_combo_box_set_active_id(GTK_COMBO_BOX(opts->integration_combo),
		                            integration_method_id(meth & INTEGRATION_METHOD_MASK));
	}
}


void crystfel_indexing_opts_set_overpredict(CrystFELIndexingOpts *opts,
                                            int overpredict)
{
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(opts->overpredict),
	                             overpredict);
}


void crystfel_indexing_opts_set_push_res(CrystFELIndexingOpts *opts,
                                         float push_res)
{
	char tmp[64];

	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(opts->limit_res),
	                             !isinf(push_res));

	snprintf(tmp, 63, "%f", push_res);
	gtk_entry_set_text(GTK_ENTRY(opts->push_res), tmp);
}
