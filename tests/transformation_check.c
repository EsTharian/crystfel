/*
 * transformation_check.c
 *
 * Check that unit cell transformations work
 *
 * Copyright © 2012-2014 Deutsches Elektronen-Synchrotron DESY,
 *                       a research centre of the Helmholtz Association.
 *
 * Authors:
 *   2012-2014 Thomas White <taw@physics.org>
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
#include <stdarg.h>

#include <cell.h>
#include <cell-utils.h>
#include <symmetry.h>


#define MAX_REFLS (10*1024)

static struct rvec *all_refls(UnitCell *cell, double max_r, int *n)
{
	double asx, asy, asz;
	double bsx, bsy, bsz;
	double csx, csy, csz;
	double ax, ay, az;
	double bx, by, bz;
	double cx, cy, cz;
	signed int h, k, l;
	int hmax, kmax, lmax;
	struct rvec *r;
	int i = 0;

	r = malloc(sizeof(struct rvec)*MAX_REFLS);
	if ( r == NULL ) return NULL;

	cell_get_cartesian(cell, &ax, &ay, &az, &bx, &by, &bz, &cx, &cy, &cz);

	hmax = max_r * modulus(ax, ay, az);
	kmax = max_r * modulus(bx, by, bz);
	lmax = max_r * modulus(cx, cy, cz);

	cell_get_reciprocal(cell, &asx, &asy, &asz,
	                          &bsx, &bsy, &bsz,
	                          &csx, &csy, &csz);

	for ( h=-hmax; h<=hmax; h++ ) {
	for ( k=-kmax; k<=kmax; k++ ) {
	for ( l=-lmax; l<=lmax; l++ ) {

		if ( (h==0) && (k==0) && (l==0) ) continue;
		if ( forbidden_reflection(cell, h, k, l) ) continue;
		if ( 2.0*resolution(cell, h, k, l) > max_r ) continue;

		r[i].u = h*asx + k*bsx + l*csx;
		r[i].v = h*asy + k*bsy + l*csy;
		r[i].w = h*asz + k*bsz + l*csz;
		i++;

		if ( i == MAX_REFLS ) {
			ERROR("Too many reflections.\n");
			return NULL;
		}
	}
	}
	}

	*n = i;
	return r;
}



static int tolerance(double a, double b)
{
	if ( fabs(a-b) < 1e6 ) return 1;
	return 0;
}


static int find_rvec(struct rvec *l, int n, struct rvec f)
{
	int i;
	for ( i=0; i<n; i++ ) {
		if  ( ( tolerance(l[i].u, f.u) )
		   && ( tolerance(l[i].v, f.v) )
		   && ( tolerance(l[i].w, f.w) ) ) return 1;
	}
	return 0;
}


static int compare_rvecs(struct rvec *a, int na, struct rvec *b, int nb)
{
	int i;
	int n_nf = 0;

	if ( (a==NULL) || (b==NULL) ) {
		ERROR("One of the lists if NULL!\n");
		return 1;
	}
	STATUS("Comparing %i and %i reflections\n", na, nb);

	for ( i=0; i<na; i++ ) {
		if ( !find_rvec(b, nb, a[i]) ) n_nf++;

	}
	STATUS("Found %i out of %i\n", na-n_nf, na);
	if ( 100*n_nf > na ) return 1;
	return 0;
}


static int check_same_reflections(UnitCell *cell, UnitCell *cnew)
{
	struct rvec *vecs;
	struct rvec *tvecs;
	int na, nb;

	/* Check that the two cells predict the same reflections */
	vecs = all_refls(cell, 1e9, &na);
	tvecs = all_refls(cnew, 1e9, &nb);
	if ( compare_rvecs(vecs, na, tvecs, nb)
	    || compare_rvecs(tvecs, nb, vecs, na) )
	{
		ERROR("********************************************** ");
		ERROR("Transformed cell didn't predict the same reflections\n");
		//printf("---\n");
		//for ( i=0; i<na; i++ ) {
		//	printf("%e %e %e\n", vecs[i].u, vecs[i].v, vecs[i].w);
		//}
		//printf("---\n");
		//for ( i=0; i<nb; i++ ) {
		//	printf("%e %e %e\n", tvecs[i].u, tvecs[i].v, tvecs[i].w);
		//}
		return 1;
	} else {
		STATUS("The cells predict the same reflections.\n");
	}
	free(vecs);
	free(tvecs);
	return 0;
}


static int check_transformation(UnitCell *cell, IntegerMatrix *tfn,
                                int pred_test)
{
	UnitCell *cnew, *cback;
	double a[9], b[9];
	int i;
	int fail = 0;

	STATUS("-----------------------\n");
	cnew = cell_transform_intmat(cell, tfn);
	cback = cell_transform_intmat_inverse(cnew, tfn);

	STATUS("----> Before transformation:\n");
	cell_print(cell);
	STATUS("----> The transformation matrix:\n");
	intmat_print(tfn);
	STATUS("----> After transformation:\n");
	cell_print(cnew);
	STATUS("----> After back transformation:\n");
	cell_print(cback);

	if ( pred_test ) {
		check_same_reflections(cell, cnew);
	} else {
		STATUS("Cells not expected to predict the same reflections.\n");
	}

	/* Check we got the parameters back */
	cell_get_cartesian(cell, &a[0], &a[1], &a[2],
	                         &a[3], &a[4], &a[5],
	                         &a[6], &a[7], &a[8]);
	cell_get_cartesian(cback, &b[0], &b[1], &b[2],
	                          &b[3], &b[4], &b[5],
	                          &b[6], &b[7], &b[8]);
	for ( i=0; i<9; i++ ) {
		if ( !tolerance(a[i], b[i]) ) {
			//STATUS("%e %e\n", a[i], b[i]);
			fail = 1;
		}
	}

	if ( fail ) {
		ERROR("********************************************** ");
		ERROR("Original cell not recovered after transformation:\n");
		STATUS("----> After transformation and transformation back:\n");
		cell_print(cback);
	} else {
		STATUS("The original cell was recovered after inverse transform.\n");
	}

	return fail;
}


static int check_uncentering(UnitCell *cell)
{
	UnitCell *ct;
	IntegerMatrix *C;
	RationalMatrix *Ci;
	UnitCell *cback;
	double a[9], b[9];
	int i;
	int fail = 0;

	STATUS("-----------------------\n");

	STATUS("----> Before transformation:\n");
	cell_print_full(cell);

	ct = uncenter_cell(cell, &C, &Ci);
	if ( ct == NULL ) return 1;

	STATUS("----> The primitive unit cell:\n");
	cell_print(ct);

	STATUS("----> The matrix to put the centering back:\n");
	intmat_print(C);

	STATUS("----> The recovered centered cell:\n");
	cback = cell_transform_intmat(ct, C);
	cell_print(cback);

	cell_get_cartesian(cell, &a[0], &a[1], &a[2],
	                         &a[3], &a[4], &a[5],
	                         &a[6], &a[7], &a[8]);
	cell_get_cartesian(cback, &b[0], &b[1], &b[2],
	                          &b[3], &b[4], &b[5],
	                          &b[6], &b[7], &b[8]);
	for ( i=0; i<9; i++ ) {
		if ( fabs(a[i] - b[i]) > 1e-12 ) {
			fail = 1;
		}
	}

	if ( fail ) {
		ERROR("********************************************** ");
		ERROR("Original cell not recovered after back transformation\n");
	}

	fail += check_same_reflections(cell, ct);
	cell_free(ct);
	cell_free(cback);

	return fail;
}


static int check_identity(UnitCell *cell, IntegerMatrix *tfn)
{
	UnitCell *cnew;
	double a[9], b[9];
	int i;
	int fail = 0;

	STATUS("-----------------------\n");

	cnew = cell_transform_intmat(cell, tfn);

	STATUS("----> Before identity transformation:\n");
	cell_print(cell);
	STATUS("----> The identity transformation matrix:\n");
	intmat_print(tfn);
	STATUS("----> After identity transformation:\n");
	cell_print(cnew);

	cell_get_cartesian(cell, &a[0], &a[1], &a[2],
	                         &a[3], &a[4], &a[5],
	                         &a[6], &a[7], &a[8]);
	cell_get_cartesian(cnew, &b[0], &b[1], &b[2],
	                         &b[3], &b[4], &b[5],
	                         &b[6], &b[7], &b[8]);
	for ( i=0; i<9; i++ ) {
		if ( !within_tolerance(a[i], b[i], 0.1) ) {
			fail = 1;
			//STATUS("%e %e\n", a[i], b[i]);
		}
	}

	if ( fail ) {
		ERROR("********************************************** ");
		ERROR("Original cell not recovered after identity transformation:\n");
		cell_print(cell);
		intmat_print(tfn);
		cell_print(cnew);
	}

	return fail;
}


int main(int argc, char *argv[])
{
	int fail = 0;
	UnitCell *cell, *cref;
	IntegerMatrix *tfn;
	IntegerMatrix *part1, *part2;
	gsl_rng *rng;

	rng = gsl_rng_alloc(gsl_rng_mt19937);

	cref = cell_new_from_parameters(50e-10, 55e-10, 70e-10,
	                                deg2rad(67.0),
	                                deg2rad(70.0),
	                                deg2rad(77.0));
	if ( cref == NULL ) return 1;

	cell = cell_rotate(cref, random_quaternion(rng));
	if ( cell == NULL ) return 1;
	cell_free(cref);

	tfn = intmat_identity(3);

	/* Permutation of axes */
	if ( tfn == NULL ) return 1;
	intmat_set_all_3x3(tfn, 0,0,1,
	                        1,0,0,
	                        0,1,0);
	fail += check_transformation(cell, tfn, 1);

	/* Doubling of cell in one direction */
	if ( tfn == NULL ) return 1;
	intmat_set_all_3x3(tfn, 2,0,0,
	                        0,1,0,
	                        0,0,1);
	fail += check_transformation(cell, tfn, 0);

	/* Shearing */
	if ( tfn == NULL ) return 1;
	intmat_set_all_3x3(tfn, 1,0,0,
	                        1,1,0,
	                        0,0,1);
	fail += check_transformation(cell, tfn, 1);

	/* Crazy */
	if ( tfn == NULL ) return 1;
	intmat_set_all_3x3(tfn, 1,0,0,
	                        1,1,1,
	                        0,0,1);
	fail += check_transformation(cell, tfn, 0);

	/* Identity in two parts */
	part1 = intmat_identity(3);
	part2 = intmat_identity(3);
	if ( tfn == NULL ) return 1;
	intmat_set_all_3x3(part1, 0,0,-1,
	                          0,1,0,
	                          1,0,0);
	intmat_set_all_3x3(part2,  0,0,1,
	                           0,1,0,
	                          -1,0,0);
	tfn = intmat_intmat_mult(part1, part2);
	fail += check_identity(cell, tfn);
	intmat_free(part1);
	intmat_free(part2);

	intmat_free(tfn);

	/* Check some uncentering transformations */
	cref = cell_new_from_parameters(50e-10, 50e-10, 50e-10,
	                                deg2rad(90.0),
	                                deg2rad(90.0),
	                                deg2rad(90.0));
	cell_set_lattice_type(cref, L_CUBIC);
	cell_set_centering(cref, 'F');
	fail += check_uncentering(cref);
	cell_set_centering(cref, 'I');
	fail += check_uncentering(cref);

	cref = cell_new_from_parameters(50e-10, 50e-10, 90e-10,
	                                deg2rad(90.0),
	                                deg2rad(90.0),
	                                deg2rad(90.0));
	cell_set_lattice_type(cref, L_TETRAGONAL);
	cell_set_centering(cref, 'I');
	cell_set_unique_axis(cref, 'c');
	fail += check_uncentering(cref);
	cref = cell_new_from_parameters(90e-10, 50e-10, 50e-10,
	                                deg2rad(90.0),
	                                deg2rad(90.0),
	                                deg2rad(90.0));
	cell_set_lattice_type(cref, L_TETRAGONAL);
	cell_set_centering(cref, 'I');
	cell_set_unique_axis(cref, 'a');
	fail += check_uncentering(cref);

	cref = cell_new_from_parameters(50e-10, 60e-10, 70e-10,
	                                deg2rad(90.0),
	                                deg2rad(90.0),
	                                deg2rad(90.0));
	cell_set_lattice_type(cref, L_ORTHORHOMBIC);
	cell_set_centering(cref, 'C');
	fail += check_uncentering(cref);
	cell_set_centering(cref, 'A');
	fail += check_uncentering(cref);
	cell_set_centering(cref, 'B');
	fail += check_uncentering(cref);

	cref = cell_new_from_parameters(50e-10, 60e-10, 70e-10,
	                                deg2rad(90.0),
	                                deg2rad(100.0),
	                                deg2rad(90.0));
	cell_set_lattice_type(cref, L_MONOCLINIC);
	cell_set_unique_axis(cref, 'b');
	cell_set_centering(cref, 'C');
	fail += check_uncentering(cref);
	cell_set_centering(cref, 'I');
	fail += check_uncentering(cref);
	cell_set_centering(cref, 'A');
	fail += check_uncentering(cref);

	cref = cell_new_from_parameters(50e-10, 50e-10, 70e-10,
	                                deg2rad(90.0),
	                                deg2rad(90.0),
	                                deg2rad(120.0));
	cell_set_lattice_type(cref, L_HEXAGONAL);
	cell_set_unique_axis(cref, 'c');
	cell_set_centering(cref, 'H');
	fail += check_uncentering(cref);

	cell_free(cell);
	gsl_rng_free(rng);

	return fail;
}
