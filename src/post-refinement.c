/*
 * post-refinement.c
 *
 * Post refinement
 *
 * Copyright © 2012-2018 Deutsches Elektronen-Synchrotron DESY,
 *                       a research centre of the Helmholtz Association.
 *
 * Authors:
 *   2010-2018 Thomas White <taw@physics.org>
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
#include <assert.h>
#include <gsl/gsl_multimin.h>
#include <gsl/gsl_fit.h>

#include "image.h"
#include "post-refinement.h"
#include "peaks.h"
#include "symmetry.h"
#include "geometry.h"
#include "cell.h"
#include "cell-utils.h"
#include "reflist-utils.h"
#include "scaling.h"
#include "merge.h"


struct prdata
{
	int refined;
};

const char *str_prflag(enum prflag flag)
{
	switch ( flag ) {

		case PRFLAG_OK :
		return "OK";

		case PRFLAG_FEWREFL :
		return "not enough reflections";

		case PRFLAG_SOLVEFAIL :
		return "PR solve failed";

		case PRFLAG_EARLY :
		return "early rejection";

		case PRFLAG_DELTACCHALF :
		return "negative delta CC½";

		case PRFLAG_BIGB :
		return "B too big";

		case PRFLAG_SCALEBAD :
		return "bad scaling";

		default :
		return "Unknown flag";
	}
}


static void rotate_cell_xy(UnitCell *cell, double ang1, double ang2)
{
	double asx, asy, asz;
	double bsx, bsy, bsz;
	double csx, csy, csz;
	double xnew, ynew, znew;

	cell_get_reciprocal(cell, &asx, &asy, &asz,
	                       &bsx, &bsy, &bsz,
	                       &csx, &csy, &csz);

	/* "a" around x */
	xnew = asx;
	ynew = asy*cos(ang1) + asz*sin(ang1);
	znew = -asy*sin(ang1) + asz*cos(ang1);
	asx = xnew;  asy = ynew;  asz = znew;

	/* "b" around x */
	xnew = bsx;
	ynew = bsy*cos(ang1) + bsz*sin(ang1);
	znew = -bsy*sin(ang1) + bsz*cos(ang1);
	bsx = xnew;  bsy = ynew;  bsz = znew;

	/* "c" around x */
	xnew = csx;
	ynew = csy*cos(ang1) + csz*sin(ang1);
	znew = -csy*sin(ang1) + csz*cos(ang1);
	csx = xnew;  csy = ynew;  csz = znew;

	/* "a" around y */
	xnew = asx*cos(ang2) + asz*sin(ang2);
	ynew = asy;
	znew = -asx*sin(ang2) + asz*cos(ang2);
	asx = xnew;  asy = ynew;  asz = znew;

	/* "b" around y */
	xnew = bsx*cos(ang2) + bsz*sin(ang2);
	ynew = bsy;
	znew = -bsx*sin(ang2) + bsz*cos(ang2);
	bsx = xnew;  bsy = ynew;  bsz = znew;

	/* "c" around y */
	xnew = csx*cos(ang2) + csz*sin(ang2);
	ynew = csy;
	znew = -csx*sin(ang2) + csz*cos(ang2);
	csx = xnew;  csy = ynew;  csz = znew;

	cell_set_reciprocal(cell, asx, asy, asz, bsx, bsy, bsz, csx, csy, csz);
}


static const char *get_label(enum gparam p)
{
	switch ( p ) {
		case GPARAM_ANG1 : return "First angle (radians)";
		case GPARAM_ANG2 : return "Second angle (radians)";
		case GPARAM_R : return "Profile radius (m^-1)";
		case GPARAM_WAVELENGTH : return "Wavelength (Angstroms)";
		default : return "unknown";
	}
}


/* We set all the step sizes to 1, then scale them.
 * This way, the size of the simplex stays meaningful and we possibly also
 *  avoid some roundoff errors */
static double get_scale(enum gparam p)
{
	switch ( p ) {

		case GPARAM_ANG1 : return deg2rad(0.05);
		case GPARAM_ANG2 : return deg2rad(0.05);
		case GPARAM_R : return 0.0005e9;
		case GPARAM_WAVELENGTH : return 0.001e-10;

		default : return 0.0;

	}
}


struct rf_priv {
	const Crystal *cr;
	const RefList *full;
	enum gparam rv[32];
	int verbose;
	int serial;
	gsl_vector *initial;
	int scaleflags;

	/* For freeing later */
	gsl_vector *vals;
	gsl_vector *step;

	/* So that it stays around until the end of minimisation */
	gsl_multimin_function f;
};


static double get_actual_val(const gsl_vector *v, const gsl_vector *initial,
                             enum gparam *rv, int i)
{
	return gsl_vector_get(v, i) * get_scale(rv[i])
	             + gsl_vector_get(initial, i);
}


static void apply_parameters(const gsl_vector *v, const gsl_vector *initial,
                             enum gparam *rv, Crystal *cr)
{
	int i;
	double ang1, ang2, R, lambda;

	/* Default parameters if not used in refinement */
	ang1 = 0.0;
	ang2 = 0.0;
	R = crystal_get_profile_radius(cr);
	lambda = crystal_get_image(cr)->lambda;

	for ( i=0; i<v->size; i++ ) {

		double val;

		val = get_actual_val(v, initial, rv, i);

		switch ( rv[i] ) {

			case GPARAM_ANG1 :
			ang1 = val;
			break;

			case GPARAM_ANG2 :
			ang2 = val;
			break;

			case GPARAM_R :
			R = val;
			break;

			case GPARAM_WAVELENGTH :
			lambda = val;
			break;

			default :
			ERROR("Don't understand parameter %i\n", rv[i]);
			break;

		}
	}

	rotate_cell_xy(crystal_get_cell(cr), ang1, ang2);
	crystal_set_profile_radius(cr, R);
	crystal_get_image(cr)->lambda = lambda;
}


static double residual_f(const gsl_vector *v, void *pp)
{
	struct rf_priv *pv = pp;
	RefList *list;
	struct image im;
	Crystal *cr;
	double res;
	int i;

	for ( i=0; i<v->size; i++ ) {
		if ( gsl_vector_get(v, i) > 100.0 ) return GSL_NAN;
	}

	cr = crystal_copy(pv->cr);
	im = *crystal_get_image(cr);
	crystal_set_image(cr, &im);
	crystal_set_cell(cr, cell_new_from_cell(crystal_get_cell(cr)));

	apply_parameters(v, pv->initial, pv->rv, cr);

	if ( fabs(crystal_get_profile_radius(cr)) > 5e9 ) {
		cell_free(crystal_get_cell(cr));
		crystal_free(cr);
		if ( pv->verbose ) STATUS("radius > 5e9\n");
		return GSL_NAN;
	}

	/* Can happen with grid scans and certain --force-radius values */
	if ( fabs(crystal_get_profile_radius(cr)) < 0.0000001e9 ) {
		cell_free(crystal_get_cell(cr));
		crystal_free(cr);
		if ( pv->verbose ) STATUS("radius very small\n");
		return GSL_NAN;
	}

	if ( im.lambda <= 0.0 ) {
		cell_free(crystal_get_cell(cr));
		crystal_free(cr);
		if ( pv->verbose ) STATUS("lambda < 0\n");
		return GSL_NAN;
	}

	list = copy_reflist(crystal_get_reflections(cr));
	crystal_set_reflections(cr, list);

	update_predictions(cr);
	calculate_partialities(cr, PMODEL_XSPHERE);

	if ( scale_one_crystal(cr, pv->full, pv->scaleflags) ) {
		cell_free(crystal_get_cell(cr));
		reflist_free(crystal_get_reflections(cr));
		crystal_free(cr);
		if ( pv->verbose ) STATUS("Bad scaling\n");
		return GSL_NAN;
	}
	res = residual(cr, pv->full, 0, NULL, NULL);

	cell_free(crystal_get_cell(cr));
	reflist_free(crystal_get_reflections(cr));
	crystal_free(cr);

	return res;
}


static double get_initial_param(Crystal *cr, enum gparam p)
{
	switch ( p ) {

		case GPARAM_ANG1 : return 0.0;
		case GPARAM_ANG2 : return 0.0;
		case GPARAM_R : return crystal_get_profile_radius(cr);
		case GPARAM_WAVELENGTH : return crystal_get_image(cr)->lambda;

		default: return 0.0;

	}
}


static int check_angle_shifts(gsl_vector *cur, const gsl_vector *initial,
                              enum gparam *rv, struct rf_priv *residual_f_priv)
{
	int i = 0;
	double ang = 0.0;

	while ( rv[i] != GPARAM_EOL ) {
		if ( (rv[i] == GPARAM_ANG1) || (rv[i] == GPARAM_ANG2) ) {
			ang += fabs(get_actual_val(cur, initial, rv, i));
		}
		rv++;
	}

	if ( rad2deg(ang) > 5.0 ) {
		ERROR("More than 5 degrees total rotation!\n");
		residual_f_priv->verbose = 1;
		double res = residual_f(cur, residual_f_priv);
		STATUS("residual after rotation = %e\n", res);
		residual_f_priv->verbose = 2;
		res = residual_f(initial, residual_f_priv);
		STATUS("residual before rotation = %e\n", res);
		return 1;
	}
	return 0;
}


static RefList *reindex_reflections(RefList *input, SymOpList *amb,
                                    SymOpList *sym, int idx)
{
	RefList *n;
	Reflection *refl;
	RefListIterator *iter;

	n = reflist_new();

	for ( refl = first_refl(input, &iter);
	      refl != NULL;
	      refl = next_refl(refl, iter) )
	{
		signed int h, k, l;
		Reflection *rn;

		get_indices(refl, &h, &k, &l);
		get_equiv(amb, NULL, idx, h, k, l, &h, &k, &l);
		get_asymm(sym, h, k, l, &h, &k, &l);
		rn = add_refl(n, h, k, l);
		copy_data(rn, refl);

		get_symmetric_indices(rn, &h, &k, &l);
		get_equiv(amb, NULL, idx, h, k, l, &h, &k, &l);
		set_symmetric_indices(rn, h, k, l);
	}

	return n;
}


static void reindex_cell(UnitCell *cell, SymOpList *amb, int idx)
{
	signed int h, k, l;
	struct rvec na, nb, nc;
	struct rvec as, bs, cs;

	cell_get_reciprocal(cell, &as.u, &as.v, &as.w,
	                          &bs.u, &bs.v, &bs.w,
	                          &cs.u, &cs.v, &cs.w);

	get_equiv(amb, NULL, idx, 1, 0, 0, &h, &k, &l);
	na.u = as.u*h + bs.u*k + cs.u*l;
	na.v = as.v*h + bs.v*k + cs.v*l;
	na.w = as.w*h + bs.w*k + cs.w*l;

	get_equiv(amb, NULL, idx, 0, 1, 0, &h, &k, &l);
	nb.u = as.u*h + bs.u*k + cs.u*l;
	nb.v = as.v*h + bs.v*k + cs.v*l;
	nb.w = as.w*h + bs.w*k + cs.w*l;

	get_equiv(amb, NULL, idx, 0, 0, 1, &h, &k, &l);
	nc.u = as.u*h + bs.u*k + cs.u*l;
	nc.v = as.v*h + bs.v*k + cs.v*l;
	nc.w = as.w*h + bs.w*k + cs.w*l;

	cell_set_reciprocal(cell, na.u, na.v, na.w,
	                          nb.u, nb.v, nb.w,
	                          nc.u, nc.v, nc.w);
}


static void try_reindex(Crystal *crin, const RefList *full,
                        SymOpList *sym, SymOpList *amb, int scaleflags)
{
	RefList *list;
	Crystal *cr;
	UnitCell *cell;
	double residual_original, residual_flipped;
	int idx, n;

	if ( sym == NULL || amb == NULL ) return;

	if ( scale_one_crystal(crin, full, scaleflags) ) return;
	residual_original = residual(crin, full, 0, NULL, NULL);

	cr = crystal_copy(crin);

	n = num_equivs(amb, NULL);

	for ( idx=0; idx<n; idx++ ) {

		cell = cell_new_from_cell(crystal_get_cell(crin));
		if ( cell == NULL ) return;
		reindex_cell(cell, amb, idx);
		crystal_set_cell(cr, cell);

		list = reindex_reflections(crystal_get_reflections(crin),
		                           amb, sym, idx);
		crystal_set_reflections(cr, list);

		update_predictions(cr);
		calculate_partialities(cr, PMODEL_XSPHERE);

		if ( scale_one_crystal(cr, full, scaleflags) ) return;
		residual_flipped = residual(cr, full, 0, NULL, NULL);

		if ( residual_flipped < residual_original ) {
			crystal_set_cell(crin, cell);
			crystal_set_reflections(crin, list);
			residual_original = residual_flipped;
		} else {
			cell_free(crystal_get_cell(cr));
			reflist_free(crystal_get_reflections(cr));
		}
	}

	crystal_free(cr);
}


void write_test_logs(Crystal *crystal, const RefList *full,
                     signed int cycle, int serial)
{
	FILE *fh;
	struct image *image = crystal_get_image(crystal);
	char tmp[256];
	char ins[16];

	snprintf(tmp, 256, "pr-logs/parameters-crystal%i.dat", serial);

	if ( cycle == 0 ) {
		fh = fopen(tmp, "w");
	} else {
		fh = fopen(tmp, "a");
	}

	if ( fh == NULL ) {
		ERROR("Failed to open '%s'\n", tmp);
		return;
	}

	if ( cycle == 0 ) {
		char *evstr = get_event_string(image->event);
		fprintf(fh, "Image: %s %s\n", image->filename, evstr);
		free(evstr);
	}

	if ( cycle >= 0 ) {
		snprintf(ins, 16, "%i", cycle);
	} else {
		ins[0] = 'F';
		ins[1] = '\0';
	}

	fprintf(fh, "%s rlp_size = %e\n", ins, crystal_get_profile_radius(crystal)/1e10);
	fprintf(fh, "%s mosaicity = %e\n", ins, crystal_get_mosaicity(crystal));
	fprintf(fh, "%s wavelength = %f A\n", ins, image->lambda*1e10);
	fprintf(fh, "%s bandwidth = %f\n", ins, image->bw);
	fprintf(fh, "%s my scale factor = %e\n", ins, crystal_get_osf(crystal));

	double asx, asy, asz, bsx, bsy, bsz, csx, csy, csz;
	cell_get_reciprocal(crystal_get_cell(crystal), &asx, &asy, &asz,
	                                               &bsx, &bsy, &bsz,
	                                               &csx, &csy, &csz);
	fprintf(fh, "%s %f, %f, %f, %f, %f, %f, %f, %f, %f\n", ins,
	        asx/1e10, bsx/1e10, csx/1e10,
	        asy/1e10, bsy/1e10, csy/1e10,
	        asz/1e10, bsz/1e10, csz/1e10);
	fclose(fh);
}


void write_specgraph(Crystal *crystal, const RefList *full,
                     signed int cycle, int serial)
{
	FILE *fh;
	char tmp[256];
	Reflection *refl;
	RefListIterator *iter;
	double G = crystal_get_osf(crystal);
	double B = crystal_get_Bfac(crystal);
	UnitCell *cell;
	struct image *image = crystal_get_image(crystal);
	char ins[16];

	snprintf(tmp, 256, "pr-logs/specgraph-crystal%i.dat", serial);

	if ( cycle == 0 ) {
		fh = fopen(tmp, "w");
	} else {
		fh = fopen(tmp, "a");
	}

	if ( fh == NULL ) {
		ERROR("Failed to open '%s'\n", tmp);
		return;
	}

	if ( cycle == 0 ) {
		char *evstr = get_event_string(image->event);
		fprintf(fh, "Image: %s %s\n", image->filename, evstr);
		fprintf(fh, "khalf/m   1/d(m)  pcalc    pobs   iteration  h  k  l\n");
		free(evstr);
	}

	cell = crystal_get_cell(crystal);

	if ( cycle >= 0 ) {
		snprintf(ins, 16, "%i", cycle);
	} else {
		ins[0] = 'F';
		ins[1] = '\0';
	}

	for ( refl = first_refl(crystal_get_reflections(crystal), &iter);
	      refl != NULL;
	      refl = next_refl(refl, iter) )
	{
		double Ipart, Ifull, pobs, pcalc;
		double res;
		signed int h, k, l;
		Reflection *match;

		/* Strong reflections only */
		if ( get_intensity(refl) < 3.0*get_esd_intensity(refl) ) continue;

		get_indices(refl, &h, &k, &l);
		res = resolution(cell, h, k, l);

		match = find_refl(full, h, k, l);
		if ( match == NULL ) continue;

		/* Don't calculate pobs if reference reflection is weak */
		if ( fabs(get_intensity(match)) / get_esd_intensity(match) < 3.0 ) continue;

		Ipart = correct_reflection_nopart(get_intensity(refl), refl, G, B, res);
		Ifull = get_intensity(match);
		pobs = Ipart / Ifull;
		pcalc = get_partiality(refl);

		fprintf(fh, "%e   %e   %f   %f   %s  %4i %4i %4i\n",
		        get_khalf(refl), 2.0*res, pcalc, pobs, ins, h, k, l);

	}

	fclose(fh);
}


static gsl_multimin_fminimizer *setup_minimiser(Crystal *cr, const RefList *full,
                                                int verbose, int serial,
                                                int scaleflags,
                                                struct rf_priv *priv)
{
	gsl_multimin_fminimizer *min;
	int n_params = 0;
	int i, r;

	/* The parameters to be refined */
	priv->rv[n_params++] = GPARAM_ANG1;
	priv->rv[n_params++] = GPARAM_ANG2;
	priv->rv[n_params++] = GPARAM_R;
	priv->rv[n_params++] = GPARAM_WAVELENGTH;
	priv->rv[n_params] = GPARAM_EOL;  /* End of list */

	priv->cr = cr;
	priv->full = full;
	priv->verbose = verbose;
	priv->serial = serial;
	priv->scaleflags = scaleflags;

	priv->f.f = residual_f;
	priv->f.n = n_params;
	priv->f.params = priv;

	priv->initial = gsl_vector_alloc(n_params);
	priv->vals = gsl_vector_alloc(n_params);
	priv->step = gsl_vector_alloc(n_params);

	for ( i=0; i<n_params; i++ ) {
		gsl_vector_set(priv->initial, i, get_initial_param(cr, priv->rv[i]));
		gsl_vector_set(priv->vals, i, 0.0);
		gsl_vector_set(priv->step, i, 1.0);
	}

	min = gsl_multimin_fminimizer_alloc(gsl_multimin_fminimizer_nmsimplex2,
	                                    n_params);
	if ( min == NULL ) {
		ERROR("Failed to allocate minimiser\n");
		gsl_vector_free(priv->vals);
		gsl_vector_free(priv->step);
		gsl_vector_free(priv->initial);
		return NULL;
	}

	r = gsl_multimin_fminimizer_set(min, &priv->f, priv->vals, priv->step);
	if ( r != 0 ) {
		gsl_multimin_fminimizer_free(min);
		gsl_vector_free(priv->vals);
		gsl_vector_free(priv->step);
		gsl_vector_free(priv->initial);
		ERROR("Failed to set up minimiser: %s\n", gsl_strerror(r));
		return NULL;
	}

	return min;
}


static void write_grid(Crystal *cr, const RefList *full,
                       signed int cycle, int serial, int scaleflags,
                       const enum gparam par1, const enum gparam par2,
                       const char *name)
{
	FILE *fh;
	char fn[64];
	char ins[16];
	gsl_multimin_fminimizer *min;
	struct rf_priv priv;
	int idx1, idx2;
	int i;

	min = setup_minimiser(cr, full, 0, serial, scaleflags, &priv);
	if ( min == NULL ) return;

	idx1 = 99;
	idx2 = 99;
	for ( i=0; i<priv.f.n; i++ ) {
		if ( priv.rv[i] == par1 ) idx1 = i;
		if ( priv.rv[i] == par2 ) idx2 = i;
	}
	assert(idx1 != 99);
	assert(idx2 != 99);

	if ( cycle >= 0 ) {
		snprintf(ins, 16, "%i", cycle);
	} else {
		ins[0] = 'F';
		ins[1] = '\0';
	}

	snprintf(fn, 64, "pr-logs/grid-crystal%i-cycle%s-%s.dat",
	         serial, ins, name);
	fh = fopen(fn, "w");
	if ( fh != NULL ) {
		double v1, v2;
		fprintf(fh, "%e %e %e %s\n",
		        -5.0*get_scale(par1)+get_initial_param(cr, par1),
		         5.0*get_scale(par1)+get_initial_param(cr, par1),
		         get_initial_param(cr, par1),
		         get_label(par1));
		fprintf(fh, "%e %e %e %s\n",
		        -5.0*get_scale(par2)+get_initial_param(cr, par2),
		         5.0*get_scale(par2)+get_initial_param(cr, par2),
		         get_initial_param(cr, par2),
		        get_label(par2));
		for ( v2=-5.0; v2<=5.0; v2+=0.25 ) {
			int first=1;
			for ( v1=-5.0; v1<=5.0; v1+=0.25 ) {
				double res;
				gsl_vector_set(min->x, idx1, v1);
				gsl_vector_set(min->x, idx2, v2);
				res = residual_f(min->x, &priv);
				if ( !first ) fprintf(fh, " ");
				first = 0;
				fprintf(fh, "%e", res);
			}
			fprintf(fh, "\n");
		}
	}
	fclose(fh);

	gsl_multimin_fminimizer_free(min);
	gsl_vector_free(priv.initial);
	gsl_vector_free(priv.vals);
	gsl_vector_free(priv.step);
}


void write_gridscan(Crystal *cr, const RefList *full,
                    signed int cycle, int serial, int scaleflags)
{
	write_grid(cr, full, cycle, serial, scaleflags,
	           GPARAM_ANG1, GPARAM_ANG2, "ang1-ang2");
	write_grid(cr, full, cycle, serial, scaleflags,
	           GPARAM_ANG1, GPARAM_WAVELENGTH, "ang1-wave");
	write_grid(cr, full, cycle, serial, scaleflags,
	           GPARAM_R, GPARAM_WAVELENGTH, "R-wave");
}


static void do_pr_refine(Crystal *cr, const RefList *full,
                         PartialityModel pmodel, int verbose, int serial,
                         int cycle, int write_logs,
                         SymOpList *sym, SymOpList *amb, int scaleflags)
{
	gsl_multimin_fminimizer *min;
	struct rf_priv priv;
	int n_iter = 0;
	int status;
	double residual_start, residual_free_start;
	FILE *fh = NULL;

	try_reindex(cr, full, sym, amb, scaleflags);

	if ( scale_one_crystal(cr, full, scaleflags | SCALE_VERBOSE_ERRORS) ) {
		ERROR("Bad scaling at start of refinement.\n");
		return;
	}
	residual_start = residual(cr, full, 0, NULL, NULL);
	residual_free_start = residual(cr, full, 1, NULL, NULL);

	if ( verbose ) {
		STATUS("\nPR initial: dev = %10.5e, free dev = %10.5e\n",
		       residual_start, residual_free_start);
	}

	min = setup_minimiser(cr, full, verbose, serial, scaleflags, &priv);
	if ( min == NULL ) return;

	if ( verbose ) {
		double res = residual_f(min->x, &priv);
		double size = gsl_multimin_fminimizer_size(min);
		STATUS("At start: %f %f %f %f ----> %f %f %e %f residual = %e size %f\n",
		       gsl_vector_get(min->x, 0),
		       gsl_vector_get(min->x, 1),
		       gsl_vector_get(min->x, 2),
		       gsl_vector_get(min->x, 3),
		       rad2deg(get_actual_val(min->x, priv.initial, priv.rv, 0)),
		       rad2deg(get_actual_val(min->x, priv.initial, priv.rv, 1)),
		       get_actual_val(min->x, priv.initial, priv.rv, 2),
		       get_actual_val(min->x, priv.initial, priv.rv, 3)*1e10,
		       res, size);
	}

	if ( write_logs ) {

		char fn[64];

		snprintf(fn, 63, "pr-logs/crystal%i-cycle%i.log", serial, cycle);
		fh = fopen(fn, "w");
		if ( fh != NULL ) {
			fprintf(fh, "iteration  RtoReference  CCtoReference  nref  "
			            "ang1     ang2    radius    wavelength\n");
			double res = residual_f(min->x, &priv);
			fprintf(fh, "%5i %10.8f  %10.8f  %5i  %10.8f %10.8f  %e  %e\n",
			        n_iter, res, 0.0, 0,
			        rad2deg(get_actual_val(min->x, priv.initial, priv.rv, 0)),
			        rad2deg(get_actual_val(min->x, priv.initial, priv.rv, 1)),
			        get_actual_val(min->x, priv.initial, priv.rv, 2),
			        get_actual_val(min->x, priv.initial, priv.rv, 3)*1e10);
		}

	}

	do {
		double res;

		n_iter++;

		status = gsl_multimin_fminimizer_iterate(min);
		if ( status ) break;

		res = residual_f(min->x, &priv);
		if ( isnan(res) ) {
			status = GSL_ENOPROG;
			break;
		}

		if ( verbose ) {
			double res = residual_f(min->x, &priv);
			double size = gsl_multimin_fminimizer_size(min);
			STATUS("%f %f %f %f ----> %f %f %e %f residual = %e size %f\n",
			       gsl_vector_get(min->x, 0),
			       gsl_vector_get(min->x, 1),
			       gsl_vector_get(min->x, 2),
			       gsl_vector_get(min->x, 3),
			       rad2deg(get_actual_val(min->x, priv.initial, priv.rv, 0)),
			       rad2deg(get_actual_val(min->x, priv.initial, priv.rv, 1)),
			       get_actual_val(min->x, priv.initial, priv.rv, 2),
			       get_actual_val(min->x, priv.initial, priv.rv, 3)*1e10,
			       res, size);
		}

		if ( fh != NULL ) {
			fprintf(fh, "%5i %10.8f  %10.8f  %5i  %10.8f %10.8f  %e  %e\n",
			        n_iter, res, 0.0, 0,
			        rad2deg(get_actual_val(min->x, priv.initial, priv.rv, 0)),
			        rad2deg(get_actual_val(min->x, priv.initial, priv.rv, 1)),
			        get_actual_val(min->x, priv.initial, priv.rv, 2),
			        get_actual_val(min->x, priv.initial, priv.rv, 3)*1e10);
		}

		status = gsl_multimin_test_size(min->size, 0.005);

	} while ( status == GSL_CONTINUE && n_iter < 1000 );

	if ( verbose ) {
		STATUS("Done with refinement after %i iter\n", n_iter);
		STATUS("status = %i (%s)\n", status, gsl_strerror(status));
	}

	if ( status == GSL_SUCCESS ) {

		if ( check_angle_shifts(min->x, priv.initial, priv.rv, &priv) ) return;

		if ( verbose ) {

			double res = residual_f(min->x, &priv);
			double size = gsl_multimin_fminimizer_size(min);
			STATUS("At end: %f %f %f %f ----> %f %f %e %f residual = %e size %f\n",
			       gsl_vector_get(min->x, 0),
			       gsl_vector_get(min->x, 1),
			       gsl_vector_get(min->x, 2),
			       gsl_vector_get(min->x, 3),
			       rad2deg(get_actual_val(min->x, priv.initial, priv.rv, 0)),
			       rad2deg(get_actual_val(min->x, priv.initial, priv.rv, 1)),
			       get_actual_val(min->x, priv.initial, priv.rv, 2),
			       get_actual_val(min->x, priv.initial, priv.rv, 3)*1e10,
			       res, size);

		}

		if ( fh != NULL ) {
			double res = residual_f(min->x, &priv);
			fprintf(fh, "%5i %10.8f  %10.8f  %5i  %10.8f %10.8f  %e  %e\n",
			        n_iter, res, 0.0, 0,
			        rad2deg(get_actual_val(min->x, priv.initial, priv.rv, 0)),
			        rad2deg(get_actual_val(min->x, priv.initial, priv.rv, 1)),
			        get_actual_val(min->x, priv.initial, priv.rv, 2),
			        get_actual_val(min->x, priv.initial, priv.rv, 3)*1e10);
		}

		/* Apply the final shifts */
		apply_parameters(min->x, priv.initial, priv.rv, cr);
		update_predictions(cr);
		calculate_partialities(cr, PMODEL_XSPHERE);
		scale_one_crystal(cr, full, scaleflags);

		if ( verbose ) {

			STATUS("After applying final shifts:\n");
			STATUS("PR final: dev = %10.5e, free dev = %10.5e\n",
			       residual(cr, full, 0, NULL, NULL),
			       residual(cr, full, 1, NULL, NULL));
			STATUS("Final R = %e m^-1\n", crystal_get_profile_radius(cr));

		}

	} else {
		ERROR("Bad refinement: crystal %i (%s) after %i iterations\n",
		      serial, gsl_strerror(status), n_iter);
	}

	if ( write_logs ) {
		write_gridscan(cr, full, cycle, serial, scaleflags);
		write_specgraph(cr, full, cycle, serial);
		write_test_logs(cr, full, cycle, serial);
	}

	if ( crystal_get_profile_radius(cr) > 5e9 ) {
		ERROR("Very large radius: crystal %i\n", serial);
	}

	if ( fh != NULL ) {
		fclose(fh);
	}

	gsl_multimin_fminimizer_free(min);
	gsl_vector_free(priv.initial);
	gsl_vector_free(priv.vals);
	gsl_vector_free(priv.step);
}


struct refine_args
{
	RefList *full;
	Crystal *crystal;
	PartialityModel pmodel;
	int serial;
	struct prdata prdata;
	int verbose;
	int cycle;
	int no_logs;
	SymOpList *sym;
	SymOpList *amb;
	int scaleflags;
};


struct queue_args
{
	int n_started;
	int n_done;
	Crystal **crystals;
	int n_crystals;
	struct refine_args task_defaults;
};


static void refine_image(void *task, int id)
{
	struct refine_args *pargs = task;
	Crystal *cr = pargs->crystal;
	int write_logs = 0;

	write_logs = !pargs->no_logs && (pargs->serial % 20 == 0);
	pargs->prdata.refined = 0;

	do_pr_refine(cr, pargs->full, pargs->pmodel, pargs->verbose,
	             pargs->serial, pargs->cycle, write_logs,
	             pargs->sym, pargs->amb, pargs->scaleflags);

	if ( crystal_get_user_flag(cr) == 0 ) {
		pargs->prdata.refined = 1;
	}
}


static void *get_image(void *vqargs)
{
	struct refine_args *task;
	struct queue_args *qargs = vqargs;

	task = malloc(sizeof(struct refine_args));
	memcpy(task, &qargs->task_defaults, sizeof(struct refine_args));

	task->crystal = qargs->crystals[qargs->n_started];
	task->serial = qargs->n_started;

	qargs->n_started++;

	return task;
}


static void done_image(void *vqargs, void *task)
{
	struct queue_args *qa = vqargs;

	qa->n_done++;

	progress_bar(qa->n_done, qa->n_crystals, "Refining");
	free(task);
}


void refine_all(Crystal **crystals, int n_crystals,
                RefList *full, int nthreads, PartialityModel pmodel,
                int verbose, int cycle, int no_logs,
                SymOpList *sym, SymOpList *amb, int scaleflags)
{
	struct refine_args task_defaults;
	struct queue_args qargs;

	task_defaults.full = full;
	task_defaults.crystal = NULL;
	task_defaults.pmodel = pmodel;
	task_defaults.prdata.refined = 0;
	task_defaults.verbose = verbose;
	task_defaults.cycle = cycle;
	task_defaults.no_logs = no_logs;
	task_defaults.sym = sym;
	task_defaults.amb = amb;
	task_defaults.scaleflags = scaleflags;

	qargs.task_defaults = task_defaults;
	qargs.n_started = 0;
	qargs.n_done = 0;
	qargs.n_crystals = n_crystals;
	qargs.crystals = crystals;

	/* Don't have threads which are doing nothing */
	if ( n_crystals < nthreads ) nthreads = n_crystals;

	run_threads(nthreads, refine_image, get_image, done_image,
	            &qargs, n_crystals, 0, 0, 0);
}
