/*
 * cell-utils.c
 *
 * Unit Cell utility functions
 *
 * Copyright © 2012-2019 Deutsches Elektronen-Synchrotron DESY,
 *                       a research centre of the Helmholtz Association.
 * Copyright © 2012 Lorenzo Galli
 *
 * Authors:
 *   2009-2019 Thomas White <taw@physics.org>
 *   2012      Lorenzo Galli
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

#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <gsl/gsl_matrix.h>
#include <gsl/gsl_blas.h>
#include <assert.h>

#include "cell.h"
#include "cell-utils.h"
#include "utils.h"
#include "image.h"


/**
 * SECTION:cell-utils
 * @short_description: Unit cell utilities
 * @title: Unit cell utilities
 * @section_id:
 * @see_also:
 * @include: "cell-utils.h"
 * @Image:
 *
 * There are some utility functions associated with the core %UnitCell.
 **/


/* Weighting factor of lengths relative to angles */
#define LWEIGHT (10.0e-9)


/**
 * cell_rotate:
 * @in: A %UnitCell to rotate
 * @quat: A %quaternion
 *
 * Rotate a %UnitCell using a %quaternion.
 *
 * Returns: a newly allocated rotated copy of @in.
 *
 */
UnitCell *cell_rotate(UnitCell *in, struct quaternion quat)
{
	struct rvec a, b, c;
	struct rvec an, bn, cn;
	UnitCell *out = cell_new_from_cell(in);

	cell_get_cartesian(in, &a.u, &a.v, &a.w,
	                       &b.u, &b.v, &b.w,
	                       &c.u, &c.v, &c.w);

	an = quat_rot(a, quat);
	bn = quat_rot(b, quat);
	cn = quat_rot(c, quat);

	cell_set_cartesian(out, an.u, an.v, an.w,
	                        bn.u, bn.v, bn.w,
	                        cn.u, cn.v, cn.w);

	return out;
}


const char *str_lattice(LatticeType l)
{
	switch ( l )
	{
		case L_TRICLINIC :    return "triclinic";
		case L_MONOCLINIC :   return "monoclinic";
		case L_ORTHORHOMBIC : return "orthorhombic";
		case L_TETRAGONAL :   return "tetragonal";
		case L_RHOMBOHEDRAL : return "rhombohedral";
		case L_HEXAGONAL :    return "hexagonal";
		case L_CUBIC :        return "cubic";
	}

	return "unknown lattice";
}


LatticeType lattice_from_str(const char *s)
{
	if ( strcmp(s, "triclinic") == 0 ) return L_TRICLINIC;
	if ( strcmp(s, "monoclinic") == 0 ) return L_MONOCLINIC;
	if ( strcmp(s, "orthorhombic") == 0 ) return L_ORTHORHOMBIC;
	if ( strcmp(s, "tetragonal") == 0 ) return L_TETRAGONAL;
	if ( strcmp(s, "rhombohedral") == 0 ) return L_RHOMBOHEDRAL;
	if ( strcmp(s, "hexagonal") == 0 ) return L_HEXAGONAL;
	if ( strcmp(s, "cubic") == 0 ) return L_CUBIC;

	ERROR("Unrecognised lattice type '%s'\n", s);
	return L_TRICLINIC;
}


static int check_centering(char cen)
{
	switch ( cen ) {

		case 'P' :
		case 'A' :
		case 'B' :
		case 'C' :
		case 'I' :
		case 'F' :
		case 'R' :
		case 'H' :
		return 0;

		default:
		return 1;

	}
}


static int check_unique_axis(char ua)
{
	switch ( ua ) {

		case 'a' :
		case 'b' :
		case 'c' :
		return 0;

		default:
		return 1;

	}
}


int right_handed(UnitCell *cell)
{
	double asx, asy, asz;
	double bsx, bsy, bsz;
	double csx, csy, csz;
	struct rvec aCb;
	double aCb_dot_c;
	int rh_reciprocal;
	int rh_direct;

	if ( cell_get_reciprocal(cell, &asx, &asy, &asz,
	                               &bsx, &bsy, &bsz,
	                               &csx, &csy, &csz) ) {
		ERROR("Couldn't get reciprocal cell.\n");
		return 0;
	}

	/* "a" cross "b" */
	aCb.u = asy*bsz - asz*bsy;
	aCb.v = - (asx*bsz - asz*bsx);
	aCb.w = asx*bsy - asy*bsx;

	/* "a cross b" dot "c" */
	aCb_dot_c = aCb.u*csx + aCb.v*csy + aCb.w*csz;

	rh_reciprocal = aCb_dot_c > 0.0;

	if ( cell_get_cartesian(cell, &asx, &asy, &asz,
	                              &bsx, &bsy, &bsz,
	                              &csx, &csy, &csz) ) {
		ERROR("Couldn't get direct cell.\n");
		return 0;
	}

	/* "a" cross "b" */
	aCb.u = asy*bsz - asz*bsy;
	aCb.v = - (asx*bsz - asz*bsx);
	aCb.w = asx*bsy - asy*bsx;

	/* "a cross b" dot "c" */
	aCb_dot_c = aCb.u*csx + aCb.v*csy + aCb.w*csz;

	rh_direct = aCb_dot_c > 0.0;

	if ( rh_reciprocal != rh_direct ) {
		ERROR("Whoops, reciprocal and real space handedness are "
		      "not the same!\n");
	}

	return rh_direct;
}


void cell_print(UnitCell *cell)
{
	LatticeType lt;
	char cen;

	lt = cell_get_lattice_type(cell);
	cen = cell_get_centering(cell);

	STATUS("%s %c", str_lattice(lt), cen);

	if ( (lt==L_MONOCLINIC) || (lt==L_TETRAGONAL) || ( lt==L_HEXAGONAL)
	  || ( (lt==L_ORTHORHOMBIC) && (cen=='A') )
	  || ( (lt==L_ORTHORHOMBIC) && (cen=='B') )
	  || ( (lt==L_ORTHORHOMBIC) && (cen=='C') ) )
	{
		STATUS(", unique axis %c", cell_get_unique_axis(cell));
	}

	if ( cell_has_parameters(cell) ) {
		if ( right_handed(cell) ) {
			STATUS(", right handed.\n");
		} else {
			STATUS(", left handed.\n");
		}
	} else {
		STATUS(".\n");
	}

	if ( cell_has_parameters(cell) ) {

		double a, b, c, alpha, beta, gamma;
		cell_get_parameters(cell, &a, &b, &c, &alpha, &beta, &gamma);

		STATUS("a      b      c            alpha   beta  gamma\n");
		STATUS("%6.2f %6.2f %6.2f A    %6.2f %6.2f %6.2f deg\n",
		       a*1e10, b*1e10, c*1e10,
		       rad2deg(alpha), rad2deg(beta), rad2deg(gamma));
	} else {
		STATUS("Unit cell parameters are not specified.\n");
	}
}


void cell_print_full(UnitCell *cell)
{

	cell_print(cell);

	if ( cell_has_parameters(cell) ) {

		double asx, asy, asz;
		double bsx, bsy, bsz;
		double csx, csy, csz;
		double ax, ay, az, bx, by, bz, cx, cy, cz;

		cell_get_cartesian(cell, &ax, &ay, &az, &bx, &by, &bz, &cx, &cy, &cz);

		STATUS("a = %10.3e %10.3e %10.3e m\n", ax, ay, az);
		STATUS("b = %10.3e %10.3e %10.3e m\n", bx, by, bz);
		STATUS("c = %10.3e %10.3e %10.3e m\n", cx, cy, cz);

		cell_get_reciprocal(cell, &asx, &asy, &asz,
			                  &bsx, &bsy, &bsz,
			                  &csx, &csy, &csz);

		STATUS("a* = %10.3e %10.3e %10.3e m^-1 (modulus %10.3e m^-1)\n",
		       asx, asy, asz, modulus(asx, asy, asz));
		STATUS("b* = %10.3e %10.3e %10.3e m^-1 (modulus %10.3e m^-1)\n",
		       bsx, bsy, bsz, modulus(bsx, bsy, bsz));
		STATUS("c* = %10.3e %10.3e %10.3e m^-1 (modulus %10.3e m^-1)\n",
		       csx, csy, csz, modulus(csx, csy, csz));

		STATUS("alpha* = %6.2f deg, beta* = %6.2f deg, "
		       "gamma* = %6.2f deg\n",
		       rad2deg(angle_between(bsx, bsy, bsz, csx, csy, csz)),
		       rad2deg(angle_between(asx, asy, asz, csx, csy, csz)),
		       rad2deg(angle_between(asx, asy, asz, bsx, bsy, bsz)));

		STATUS("Cell representation is %s.\n", cell_rep(cell));

	}
}


int bravais_lattice(UnitCell *cell)
{
	LatticeType lattice = cell_get_lattice_type(cell);
	char centering = cell_get_centering(cell);
	char ua = cell_get_unique_axis(cell);

	switch ( centering )
	{
		case 'P' :
		return 1;

		case 'A' :
		case 'B' :
		case 'C' :
		if ( lattice == L_MONOCLINIC ) {
			if ( (ua=='a') && (centering!='A') ) return 1;
			if ( (ua=='b') && (centering!='B') ) return 1;
			if ( (ua=='c') && (centering!='C') ) return 1;
		} else if ( lattice == L_ORTHORHOMBIC) {
			return 1;
		}
		return 0;

		case 'I' :
		/* We accept monoclinic I as "Bravais", even though it's
		 * unconventional */
		if ( (lattice == L_MONOCLINIC)
		  || (lattice == L_ORTHORHOMBIC)
		  || (lattice == L_TETRAGONAL)
		  || (lattice == L_CUBIC) )
		{
			return 1;
		}
		return 0;

		case 'F' :
		if ( (lattice == L_ORTHORHOMBIC) || (lattice == L_CUBIC) ) {
			return 1;
		}
		return 0;

		case 'H' :
		/* "Hexagonal H" is not a Bravais lattice, but rather something
		 * invented by the PDB to make life difficult for programmers.
		 * Accepting it as Bravais seems to be the least painful way to
		 * handle it correctly. Yuk. */
		if ( ua != 'c' ) return 0;
		if ( lattice == L_HEXAGONAL ) return 1;
		return 0;

		case 'R' :
		if ( lattice == L_RHOMBOHEDRAL ) return 1;
		return 0;

		default :
		return 0;
	}
}


static RationalMatrix *create_rtnl_mtx(signed int a1, signed int a2,
                                       signed int b1, signed int b2,
                                       signed int c1, signed int c2,
                                       signed int d1, signed int d2,
                                       signed int e1, signed int e2,
                                       signed int f1, signed int f2,
                                       signed int g1, signed int g2,
                                       signed int h1, signed int h2,
                                       signed int i1, signed int i2)
{
	RationalMatrix *m = rtnl_mtx_new(3, 3);
	if ( m == NULL ) return NULL;
	rtnl_mtx_set(m, 0, 0, rtnl(a1, a2));
	rtnl_mtx_set(m, 0, 1, rtnl(b1, b2));
	rtnl_mtx_set(m, 0, 2, rtnl(c1, c2));
	rtnl_mtx_set(m, 1, 0, rtnl(d1, d2));
	rtnl_mtx_set(m, 1, 1, rtnl(e1, e2));
	rtnl_mtx_set(m, 1, 2, rtnl(f1, f2));
	rtnl_mtx_set(m, 2, 0, rtnl(g1, g2));
	rtnl_mtx_set(m, 2, 1, rtnl(h1, h2));
	rtnl_mtx_set(m, 2, 2, rtnl(i1, i2));
	return m;
}


/* Given a centered cell @in, return the integer transformation matrix which
 * turns a primitive cell into @in. Set new_centering and new_latt to the
 * centering and lattice type of the primitive cell (usually aP, sometimes rR,
 * rarely mP).  Store the inverse matrix at pCi */
static IntegerMatrix *centering_transformation(UnitCell *in,
                                               char *new_centering,
                                               LatticeType *new_latt,
                                               char *new_ua,
                                               RationalMatrix **pCi)
{
	LatticeType lt;
	char ua, cen;
	IntegerMatrix *C = NULL;
	RationalMatrix *Ci = NULL;

	lt = cell_get_lattice_type(in);
	ua = cell_get_unique_axis(in);
	cen = cell_get_centering(in);

	/* Write the matrices exactly as they appear in ITA Table 5.1.3.1.
	 * C is "P", and Ci is "Q=P^-1".  Vice-versa if the transformation
	 * should go the opposite way to what's written in the first column. */

	if ( (cen=='P') || (cen=='R') ) {
		*new_centering = cen;
		*new_latt = lt;
		*new_ua = ua;
		C = intmat_identity(3);
		Ci = rtnl_mtx_identity(3);
	}

	if ( cen == 'I' ) {
		C = intmat_create_3x3(0, 1, 1,
		                      1, 0, 1,
		                      1, 1, 0);
		Ci = create_rtnl_mtx(-1,2,  1,2,  1,2,
		                      1,2, -1,2,  1,2,
		                      1,2,  1,2, -1,2);
		if ( lt == L_CUBIC ) {
			*new_latt = L_RHOMBOHEDRAL;
			*new_centering = 'R';
			*new_ua = '*';
		} else {
			*new_latt = L_TRICLINIC;
			*new_centering = 'P';
			*new_ua = '*';
		}
	}

	if ( cen == 'F' ) {
		C = intmat_create_3x3(-1,  1,  1,
		                       1, -1,  1,
		                       1,  1, -1);
		Ci = create_rtnl_mtx( 0,1,  1,2,  1,2,
		                      1,2,  0,1,  1,2,
		                      1,2,  1,2,  0,1);
		if ( lt == L_CUBIC ) {
			*new_latt = L_RHOMBOHEDRAL;
			*new_centering = 'R';
			*new_ua = '*';
		} else {
			*new_latt = L_TRICLINIC;
			*new_centering = 'P';
			*new_ua = '*';
		}
	}

	if ( (lt == L_HEXAGONAL) && (cen == 'H') && (ua == 'c') ) {
		/* Obverse setting */
		C = intmat_create_3x3( 1,  0,  1,
		                      -1,  1,  1,
		                       0, -1,  1);
		Ci = create_rtnl_mtx( 2,3, -1,3, -1,3,
		                      1,3,  1,3, -2,3,
		                      1,3,  1,3,  1,3);
		assert(lt == L_HEXAGONAL);
		assert(ua == 'c');
		*new_latt = L_RHOMBOHEDRAL;
		*new_centering = 'R';
		*new_ua = '*';
	}

	if ( cen == 'A' ) {
		C = intmat_create_3x3( 1,  0,  0,
		                       0,  1,  1,
		                       0, -1,  1);
		Ci = create_rtnl_mtx( 1,1,  0,1,  0,1,
		                      0,1,  1,2, -1,2,
		                      0,1,  1,2,  1,2);
		if ( lt == L_ORTHORHOMBIC ) {
			*new_latt = L_MONOCLINIC;
			*new_centering = 'P';
			*new_ua = 'a';
		} else {
			*new_latt = L_TRICLINIC;
			*new_centering = 'P';
			*new_ua = '*';
		}
	}

	if ( cen == 'B' ) {
		C = intmat_create_3x3( 1,  0,  1,
		                       0,  1,  0,
		                      -1,  0,  1);
		Ci = create_rtnl_mtx( 1,2,  0,1, -1,2,
		                      0,1,  1,1,  0,1,
		                      1,2,  0,1,  1,2);
		if ( lt == L_ORTHORHOMBIC ) {
			*new_latt = L_MONOCLINIC;
			*new_centering = 'P';
			*new_ua = 'b';
		} else {
			*new_latt = L_TRICLINIC;
			*new_centering = 'P';
			*new_ua = '*';
		}
	}

	if ( cen == 'C' ) {
		C = intmat_create_3x3( 1,  1,  0,
		                      -1,  1,  0,
		                       0,  0,  1);
		Ci = create_rtnl_mtx( 1,2, -1,2,  0,1,
		                      1,2,  1,2,  0,1,
		                      0,1,  0,1,  1,1);
		if ( lt == L_ORTHORHOMBIC ) {
			*new_latt = L_MONOCLINIC;
			*new_centering = 'P';
			*new_ua = 'c';
		} else {
			*new_latt = L_TRICLINIC;
			*new_centering = 'P';
			*new_ua = '*';
		}
	}

	*pCi = Ci;
	return C;
}


/**
 * uncenter_cell:
 * @in: A %UnitCell
 * @C: Location at which to store the centering transformation
 * @Ci: Location at which to store the inverse centering transformation
 *
 * Turns any cell into a primitive one, e.g. for comparison purposes.
 *
 * The transformation which was used is stored at @Ci. The centering
 * transformation, which is the transformation you should apply if you want to
 * get back the original cell, will be stored at @C.  Either or both of these
 * can be NULL if you don't need that information.
 *
 * Returns: a primitive version of @in.
 *
 */
UnitCell *uncenter_cell(UnitCell *in, IntegerMatrix **pC, RationalMatrix **pCi)
{
	IntegerMatrix *C;
	RationalMatrix *Ci;
	char new_centering;
	LatticeType new_latt;
	char new_ua;
	UnitCell *out;

	C = centering_transformation(in, &new_centering, &new_latt,
	                             &new_ua, &Ci);
	if ( C == NULL ) return NULL;

	out = cell_transform_rational(in, Ci);
	if ( out == NULL ) return NULL;

	cell_set_lattice_type(out, new_latt);
	cell_set_centering(out, new_centering);
	cell_set_unique_axis(out, new_ua);

	if ( pC != NULL ) {
		*pC = C;
	} else {
		intmat_free(C);
	}

	if ( pCi != NULL ) {
		*pCi = Ci;
	} else {
		rtnl_mtx_free(Ci);
	}

	return out;
}


#define MAX_CAND (1024)

static int right_handed_vec(struct rvec a, struct rvec b, struct rvec c)
{
	struct rvec aCb;
	double aCb_dot_c;

	/* "a" cross "b" */
	aCb.u = a.v*b.w - a.w*b.v;
	aCb.v = - (a.u*b.w - a.w*b.u);
	aCb.w = a.u*b.v - a.v*b.u;

	/* "a cross b" dot "c" */
	aCb_dot_c = aCb.u*c.u + aCb.v*c.v + aCb.w*c.w;

	if ( aCb_dot_c > 0.0 ) return 1;
	return 0;
}


struct cvec {
	struct rvec vec;
	float na;
	float nb;
	float nc;
	float fom;
};


static int same_vector(struct cvec a, struct cvec b)
{
	if ( a.na != b.na ) return 0;
	if ( a.nb != b.nb ) return 0;
	if ( a.nc != b.nc ) return 0;
	return 1;
}


/* Attempt to make 'cell' fit into 'template' somehow */
UnitCell *match_cell(UnitCell *cell_in, UnitCell *template_in, int verbose,
                     const float *tols, int reduce)
{
	signed int n1l, n2l, n3l;
	double asx, asy, asz;
	double bsx, bsy, bsz;
	double csx, csy, csz;
	int i, j;
	double lengths[3];
	double angles[3];
	struct cvec *cand[3];
	UnitCell *new_cell = NULL;
	float best_fom = +999999999.9; /* Large number.. */
	int ncand[3] = {0,0,0};
	signed int ilow, ihigh;
	float angtol = deg2rad(tols[3]);
	UnitCell *cell;
	UnitCell *template;
	IntegerMatrix *centering;
	UnitCell *new_cell_trans;

	/* "Un-center" the template unit cell to make the comparison easier */
	template = uncenter_cell(template_in, &centering, NULL);
	if ( template == NULL ) return NULL;

	/* The candidate cell is also uncentered, because it might be centered
	 * if it came from (e.g.) MOSFLM */
	cell = uncenter_cell(cell_in, NULL, NULL);
	if ( cell == NULL ) return NULL;

	if ( cell_get_reciprocal(template, &asx, &asy, &asz,
	                         &bsx, &bsy, &bsz,
	                         &csx, &csy, &csz) ) {
		ERROR("Couldn't get reciprocal cell for template.\n");
		cell_free(template);
		cell_free(cell);
		intmat_free(centering);
		return NULL;
	}

	lengths[0] = modulus(asx, asy, asz);
	lengths[1] = modulus(bsx, bsy, bsz);
	lengths[2] = modulus(csx, csy, csz);

	angles[0] = angle_between(bsx, bsy, bsz, csx, csy, csz);
	angles[1] = angle_between(asx, asy, asz, csx, csy, csz);
	angles[2] = angle_between(asx, asy, asz, bsx, bsy, bsz);

	cand[0] = malloc(MAX_CAND*sizeof(struct cvec));
	cand[1] = malloc(MAX_CAND*sizeof(struct cvec));
	cand[2] = malloc(MAX_CAND*sizeof(struct cvec));

	if ( cell_get_reciprocal(cell, &asx, &asy, &asz,
	                         &bsx, &bsy, &bsz,
	                         &csx, &csy, &csz) ) {
		ERROR("Couldn't get reciprocal cell.\n");
		cell_free(template);
		cell_free(cell);
		intmat_free(centering);
		return NULL;
	}

	if ( reduce ) {
		ilow = -2;  ihigh = 4;
	} else {
		ilow = 0;  ihigh = 1;
	}

	/* Negative values mean 1/n, positive means n, zero means zero */
	for ( n1l=ilow; n1l<=ihigh; n1l++ ) {
	for ( n2l=ilow; n2l<=ihigh; n2l++ ) {
	for ( n3l=ilow; n3l<=ihigh; n3l++ ) {

		float n1, n2, n3;
		signed int b1, b2, b3;

		n1 = (n1l>=0) ? (n1l) : (1.0/n1l);
		n2 = (n2l>=0) ? (n2l) : (1.0/n2l);
		n3 = (n3l>=0) ? (n3l) : (1.0/n3l);

		if ( !reduce ) {
			if ( n1l + n2l + n3l > 1 ) continue;
		}

		/* 'bit' values can be +1 or -1 */
		for ( b1=-1; b1<=1; b1+=2 ) {
		for ( b2=-1; b2<=1; b2+=2 ) {
		for ( b3=-1; b3<=1; b3+=2 ) {

			double tx, ty, tz;
			double tlen;
			int i;

			n1 *= b1;  n2 *= b2;  n3 *= b3;

			tx = n1*asx + n2*bsx + n3*csx;
			ty = n1*asy + n2*bsy + n3*csy;
			tz = n1*asz + n2*bsz + n3*csz;
			tlen = modulus(tx, ty, tz);

			/* Test modulus for agreement with moduli of template */
			for ( i=0; i<3; i++ ) {

				if ( !within_tolerance(lengths[i], tlen,
				                       tols[i]) )
				{
					continue;
				}

				if ( ncand[i] == MAX_CAND ) {
					ERROR("Too many cell candidates - ");
					ERROR("consider tightening the unit ");
					ERROR("cell tolerances.\n");
				} else {

					double fom;

					fom = fabs(lengths[i] - tlen);

					cand[i][ncand[i]].vec.u = tx;
					cand[i][ncand[i]].vec.v = ty;
					cand[i][ncand[i]].vec.w = tz;
					cand[i][ncand[i]].na = n1;
					cand[i][ncand[i]].nb = n2;
					cand[i][ncand[i]].nc = n3;
					cand[i][ncand[i]].fom = fom;

					ncand[i]++;

				}
			}

		}
		}
		}

	}
	}
	}

	if ( verbose ) {
		STATUS("Candidates: %i %i %i\n", ncand[0], ncand[1], ncand[2]);
	}

	for ( i=0; i<ncand[0]; i++ ) {
	for ( j=0; j<ncand[1]; j++ ) {

		double ang;
		int k;
		float fom1;

		if ( same_vector(cand[0][i], cand[1][j]) ) continue;

		/* Measure the angle between the ith candidate for axis 0
		 * and the jth candidate for axis 1 */
		ang = angle_between(cand[0][i].vec.u, cand[0][i].vec.v,
		                    cand[0][i].vec.w, cand[1][j].vec.u,
		                    cand[1][j].vec.v, cand[1][j].vec.w);

		/* Angle between axes 0 and 1 should be angle 2 */
		if ( fabs(ang - angles[2]) > angtol ) continue;

		fom1 = fabs(ang - angles[2]);

		for ( k=0; k<ncand[2]; k++ ) {

			float fom2, fom3;

			if ( same_vector(cand[1][j], cand[2][k]) ) continue;

			/* Measure the angle between the current candidate for
			 * axis 0 and the kth candidate for axis 2 */
			ang = angle_between(cand[0][i].vec.u, cand[0][i].vec.v,
			                    cand[0][i].vec.w, cand[2][k].vec.u,
			                    cand[2][k].vec.v, cand[2][k].vec.w);

			/* ... it should be angle 1 ... */
			if ( fabs(ang - angles[1]) > angtol ) continue;

			fom2 = fom1 + fabs(ang - angles[1]);

			/* Finally, the angle between the current candidate for
			 * axis 1 and the kth candidate for axis 2 */
			ang = angle_between(cand[1][j].vec.u, cand[1][j].vec.v,
			                    cand[1][j].vec.w, cand[2][k].vec.u,
			                    cand[2][k].vec.v, cand[2][k].vec.w);

			/* ... it should be angle 0 ... */
			if ( fabs(ang - angles[0]) > angtol ) continue;

			/* Unit cell must be right-handed */
			if ( !right_handed_vec(cand[0][i].vec, cand[1][j].vec,
			                       cand[2][k].vec) ) continue;

			fom3 = fom2 + fabs(ang - angles[0]);
			fom3 += LWEIGHT * (cand[0][i].fom + cand[1][j].fom
			                   + cand[2][k].fom);

			if ( fom3 < best_fom ) {
				if ( new_cell != NULL ) free(new_cell);
				new_cell = cell_new_from_reciprocal_axes(
				                 cand[0][i].vec, cand[1][j].vec,
			                         cand[2][k].vec);
				best_fom = fom3;
			}

		}

	}
	}

	free(cand[0]);
	free(cand[1]);
	free(cand[2]);

	cell_free(cell);

	/* Reverse the de-centering transformation */
	if ( new_cell != NULL ) {

		new_cell_trans = cell_transform_intmat(new_cell, centering);
		cell_free(new_cell);
		cell_set_lattice_type(new_cell_trans,
		                      cell_get_lattice_type(template_in));
		cell_set_centering(new_cell_trans,
		                   cell_get_centering(template_in));
		cell_set_unique_axis(new_cell_trans,
		                     cell_get_unique_axis(template_in));

		cell_free(template);
		intmat_free(centering);

		return new_cell_trans;

	} else {
		cell_free(template);
		intmat_free(centering);
		return NULL;
	}
}


UnitCell *match_cell_ab(UnitCell *cell_in, UnitCell *template_in)
{
	double ax, ay, az;
	double bx, by, bz;
	double cx, cy, cz;
	int i;
	double lengths[3];
	int used[3];
	struct rvec real_a, real_b, real_c;
	struct rvec params[3];
	double alen, blen;
	float ltl = 5.0;     /* percent */
	int have_real_a;
	int have_real_b;
	int have_real_c;
	UnitCell *cell;
	UnitCell *template;
	IntegerMatrix *to_given_cell;
	UnitCell *new_cell;
	UnitCell *new_cell_trans;

	/* "Un-center" the template unit cell to make the comparison easier */
	template = uncenter_cell(template_in, &to_given_cell, NULL);

	/* The candidate cell is also uncentered, because it might be centered
	 * if it came from (e.g.) MOSFLM */
	cell = uncenter_cell(cell_in, NULL, NULL);

	/* Get the lengths to match */
	if ( cell_get_cartesian(template, &ax, &ay, &az,
	                                  &bx, &by, &bz,
	                                  &cx, &cy, &cz) )
	{
		ERROR("Couldn't get cell for template.\n");
		return NULL;
	}
	alen = modulus(ax, ay, az);
	blen = modulus(bx, by, bz);

	/* Get the lengths from the cell and turn them into anonymous vectors */
	if ( cell_get_cartesian(cell, &ax, &ay, &az,
	                              &bx, &by, &bz,
	                              &cx, &cy, &cz) )
	{
		ERROR("Couldn't get cell.\n");
		return NULL;
	}
	lengths[0] = modulus(ax, ay, az);
	lengths[1] = modulus(bx, by, bz);
	lengths[2] = modulus(cx, cy, cz);
	used[0] = 0;  used[1] = 0;  used[2] = 0;
	params[0].u = ax;  params[0].v = ay;  params[0].w = az;
	params[1].u = bx;  params[1].v = by;  params[1].w = bz;
	params[2].u = cx;  params[2].v = cy;  params[2].w = cz;

	real_a.u = 0.0;  real_a.v = 0.0;  real_a.w = 0.0;
	real_b.u = 0.0;  real_b.v = 0.0;  real_b.w = 0.0;
	real_c.u = 0.0;  real_c.v = 0.0;  real_c.w = 0.0;

	/* Check each vector against a and b */
	have_real_a = 0;
	have_real_b = 0;
	for ( i=0; i<3; i++ ) {
		if ( within_tolerance(lengths[i], alen, ltl)
		     && !used[i] && !have_real_a )
		{
			used[i] = 1;
			memcpy(&real_a, &params[i], sizeof(struct rvec));
			have_real_a = 1;
		}
		if ( within_tolerance(lengths[i], blen, ltl)
		     && !used[i] && !have_real_b )
		{
			used[i] = 1;
			memcpy(&real_b, &params[i], sizeof(struct rvec));
			have_real_b = 1;
		}
	}

	/* Have we matched both a and b? */
	if ( !(have_real_a && have_real_b) ) return NULL;

	/* "c" is "the other one" */
	have_real_c = 0;
	for ( i=0; i<3; i++ ) {
		if ( !used[i] ) {
			memcpy(&real_c, &params[i], sizeof(struct rvec));
			have_real_c = 1;
		}
	}

	if ( !have_real_c ) {
		ERROR("Huh?  Couldn't find the third vector.\n");
		ERROR("Matches: %i %i %i\n", used[0], used[1], used[2]);
		return NULL;
	}

	/* Flip c if not right-handed */
	if ( !right_handed_vec(real_a, real_b, real_c) ) {
		real_c.u = -real_c.u;
		real_c.v = -real_c.v;
		real_c.w = -real_c.w;
	}

	new_cell = cell_new_from_direct_axes(real_a, real_b, real_c);

	 /* Reverse the de-centering transformation */
	new_cell_trans = cell_transform_intmat_inverse(new_cell, to_given_cell);
	cell_free(new_cell);
	cell_set_lattice_type(new_cell, cell_get_lattice_type(template_in));
	cell_set_centering(new_cell, cell_get_centering(template_in));
	cell_set_unique_axis(new_cell, cell_get_unique_axis(template_in));

	return new_cell_trans;
}


/* Return sin(theta)/lambda = 1/2d.  Multiply by two if you want 1/d */
double resolution(UnitCell *cell, signed int h, signed int k, signed int l)
{
	double a, b, c, alpha, beta, gamma;

	cell_get_parameters(cell, &a, &b, &c, &alpha, &beta, &gamma);

	const double Vsq = a*a*b*b*c*c*(1 - cos(alpha)*cos(alpha)
	                                  - cos(beta)*cos(beta)
	                                  - cos(gamma)*cos(gamma)
				          + 2*cos(alpha)*cos(beta)*cos(gamma) );

	const double S11 = b*b*c*c*sin(alpha)*sin(alpha);
	const double S22 = a*a*c*c*sin(beta)*sin(beta);
	const double S33 = a*a*b*b*sin(gamma)*sin(gamma);
	const double S12 = a*b*c*c*(cos(alpha)*cos(beta) - cos(gamma));
	const double S23 = a*a*b*c*(cos(beta)*cos(gamma) - cos(alpha));
	const double S13 = a*b*b*c*(cos(gamma)*cos(alpha) - cos(beta));

	const double brackets = S11*h*h + S22*k*k + S33*l*l
	                         + 2*S12*h*k + 2*S23*k*l + 2*S13*h*l;
	const double oneoverdsq = brackets / Vsq;
	const double oneoverd = sqrt(oneoverdsq);

	return oneoverd / 2;
}


static void determine_lattice(UnitCell *cell,
                              const char *as, const char *bs, const char *cs,
                              const char *als, const char *bes, const char *gas)
{
	int n_right;

	/* Rhombohedral or cubic? */
	if ( (strcmp(as, bs) == 0) && (strcmp(as, cs) == 0) ) {

		if ( (strcmp(als, "  90.00") == 0)
		  && (strcmp(bes, "  90.00") == 0)
		  && (strcmp(gas, "  90.00") == 0) )
		{
			/* Cubic.  Unique axis irrelevant. */
			cell_set_lattice_type(cell, L_CUBIC);
			return;
		}

		if ( (strcmp(als, bes) == 0) && (strcmp(als, gas) == 0) ) {
			/* Rhombohedral.  Unique axis irrelevant. */
			cell_set_lattice_type(cell, L_RHOMBOHEDRAL);
			return;
		}

	}

	if ( (strcmp(als, "  90.00") == 0)
	  && (strcmp(bes, "  90.00") == 0)
	  && (strcmp(gas, "  90.00") == 0) )
	{
		if ( strcmp(bs, cs) == 0 ) {
			/* Tetragonal, unique axis a */
			cell_set_lattice_type(cell, L_TETRAGONAL);
			cell_set_unique_axis(cell, 'a');
			return;
		}

		if ( strcmp(as, cs) == 0 ) {
			/* Tetragonal, unique axis b */
			cell_set_lattice_type(cell, L_TETRAGONAL);
			cell_set_unique_axis(cell, 'b');
			return;
		}

		if ( strcmp(as, bs) == 0 ) {
			/* Tetragonal, unique axis c */
			cell_set_lattice_type(cell, L_TETRAGONAL);
			cell_set_unique_axis(cell, 'c');
			return;
		}

		/* Orthorhombic.  Unique axis irrelevant, but point group
		 * can have different orientations. */
		cell_set_lattice_type(cell, L_ORTHORHOMBIC);
		cell_set_unique_axis(cell, '*');
		return;
	}

	n_right = 0;
	if ( strcmp(als, "  90.00") == 0 ) n_right++;
	if ( strcmp(bes, "  90.00") == 0 ) n_right++;
	if ( strcmp(gas, "  90.00") == 0 ) n_right++;

	/* Hexgonal or monoclinic? */
	if ( n_right == 2 ) {

		if ( (strcmp(als, " 120.00") == 0)
		  && (strcmp(bs, cs) == 0) )
		{
			/* Hexagonal, unique axis a */
			cell_set_lattice_type(cell, L_HEXAGONAL);
			cell_set_unique_axis(cell, 'a');
			return;
		}

		if ( (strcmp(bes, " 120.00") == 0)
		  && (strcmp(as, cs) == 0) )
		{
			/* Hexagonal, unique axis b */
			cell_set_lattice_type(cell, L_HEXAGONAL);
			cell_set_unique_axis(cell, 'b');
			return;
		}

		if ( (strcmp(gas, " 120.00") == 0)
		  && (strcmp(as, bs) == 0) )
		{
			/* Hexagonal, unique axis c */
			cell_set_lattice_type(cell, L_HEXAGONAL);
			cell_set_unique_axis(cell, 'c');
			return;
		}

		if ( strcmp(als, "  90.00") != 0 ) {
			/* Monoclinic, unique axis a */
			cell_set_lattice_type(cell, L_MONOCLINIC);
			cell_set_unique_axis(cell, 'a');
			return;
		}

		if ( strcmp(bes, "  90.00") != 0 ) {
			/* Monoclinic, unique axis b */
			cell_set_lattice_type(cell, L_MONOCLINIC);
			cell_set_unique_axis(cell, 'b');
			return;
		}

		if ( strcmp(gas, "  90.00") != 0 ) {
			/* Monoclinic, unique axis c */
			cell_set_lattice_type(cell, L_MONOCLINIC);
			cell_set_unique_axis(cell, 'c');
			return;
		}
	}

	/* Triclinic, unique axis irrelevant. */
	cell_set_lattice_type(cell, L_TRICLINIC);
}


/**
 * load_cell_from_pdb:
 * @filename: The filename from which to load the cell
 *
 * Loads a unit cell from the CRYST1 line of a PDB file.
 *
 * Returns: a newly allocated %UnitCell.
 *
 */
UnitCell *load_cell_from_pdb(const char *filename)
{
	FILE *fh;
	char *rval;
	UnitCell *cell = NULL;

	fh = fopen(filename, "r");
	if ( fh == NULL ) {
		ERROR("Couldn't open '%s'\n", filename);
		return NULL;
	}

	do {

		char line[1024];

		rval = fgets(line, 1023, fh);

		if ( strncmp(line, "CRYST1", 6) == 0 ) {

			float a, b, c, al, be, ga;
			char as[10], bs[10], cs[10];
			char als[8], bes[8], gas[8];
			int r;

			memcpy(as, line+6, 9);    as[9] = '\0';
			memcpy(bs, line+15, 9);   bs[9] = '\0';
			memcpy(cs, line+24, 9);   cs[9] = '\0';
			memcpy(als, line+33, 7);  als[7] = '\0';
			memcpy(bes, line+40, 7);  bes[7] = '\0';
			memcpy(gas, line+47, 7);  gas[7] = '\0';

			r = sscanf(as, "%f", &a);
			r += sscanf(bs, "%f", &b);
			r += sscanf(cs, "%f", &c);
			r += sscanf(als, "%f", &al);
			r += sscanf(bes, "%f", &be);
			r += sscanf(gas, "%f", &ga);

			if ( r != 6 ) {
				STATUS("Couldn't understand CRYST1 line.\n");
				continue;
			}

			cell = cell_new_from_parameters(a*1e-10,
			                                b*1e-10, c*1e-10,
	                                                deg2rad(al),
	                                                deg2rad(be),
	                                                deg2rad(ga));

			determine_lattice(cell, as, bs, cs, als, bes, gas);

			if ( strlen(line) > 55 ) {
				cell_set_centering(cell, line[55]);
			} else {
				ERROR("CRYST1 line without centering.\n");
			}

			break;  /* Done */
		}

	} while ( rval != NULL );

	fclose(fh);

	if ( cell != NULL ) {
		validate_cell(cell);
	} else {
		ERROR("Failed to load cell from %s\n", filename);
	}


	return cell;
}


static int get_length_m(char **bits, int nbits, double *pl)
{
	char *rval;

	if ( nbits < 4 ) {
		ERROR("No units specified for '%s'\n", bits[0]);
		return 1;
	}

	*pl = strtod(bits[2], &rval);
	if ( *rval != '\0' ) {
		ERROR("Invalid value '%s'.\n", bits[2]);
		return 1;
	}

	if ( strcmp(bits[3], "nm") == 0 ) {
		*pl *= 1e-9;
	} else if ( strcmp(bits[3], "A") == 0 ) {
		*pl *= 1e-10;
	} else {
		ERROR("Unrecognised length units '%s'\n", bits[3]);
		return 1;
	}

	return 0;
}


static int get_angle_rad(char **bits, int nbits, double *pl)
{
	char *rval;

	if ( nbits < 4 ) {
		ERROR("No units specified for '%s'\n", bits[0]);
		return 1;
	}

	*pl = strtod(bits[2], &rval);
	if ( *rval != '\0' ) {
		ERROR("Invalid value '%s'.\n", bits[2]);
		return 1;
	}

	if ( strcmp(bits[3], "rad") == 0 ) {
		/* Do nothing, already in rad */
	} else if ( strcmp(bits[3], "deg") == 0 ) {
		*pl = deg2rad(*pl);
	} else {
		ERROR("Unrecognised angle units '%s'\n", bits[3]);
		return 1;
	}

	return 0;
}

/**
 * write_cell:
 * @cell: a %UnitCell
 * @fh: a file handle
 *
 * Writes @cell to @fh, in CrystFEL unit cell file format
 *
 */
void write_cell(UnitCell *cell, FILE *fh)
{
	double a, b, c, al, be, ga;
	LatticeType lt;

	fprintf(fh, "CrystFEL unit cell file version 1.0\n\n");
	lt = cell_get_lattice_type(cell);
	fprintf(fh, "lattice_type = %s\n", str_lattice(lt));
	if ( (lt == L_MONOCLINIC)
	  || (lt == L_TETRAGONAL)
	  || (lt == L_HEXAGONAL) )
	{
		fprintf(fh, "unique_axis = %c\n", cell_get_unique_axis(cell));
	}
	fprintf(fh, "centering = %c\n", cell_get_centering(cell));

	if ( cell_has_parameters(cell) ) {
		cell_get_parameters(cell, &a, &b, &c, &al, &be, &ga);
		fprintf(fh, "a = %.2f A\n", a*1e10);
		fprintf(fh, "b = %.2f A\n", b*1e10);
		fprintf(fh, "c = %.2f A\n", c*1e10);
		fprintf(fh, "al = %.2f deg\n", rad2deg(al));
		fprintf(fh, "be = %.2f deg\n", rad2deg(be));
		fprintf(fh, "ga = %.2f deg\n", rad2deg(ga));
	}
}


/**
 * load_cell_from_file:
 * @filename: The filename from which to load the cell
 *
 * Loads a unit cell from a file of any type (PDB or CrystFEL format)
 *
 * Returns: a newly allocated %UnitCell.
 *
 */
UnitCell *load_cell_from_file(const char *filename)
{
	FILE *fh;
	char *rval;
	char line[1024];
	UnitCell *cell;
	int have_a = 0, have_b = 0, have_c = 0;
	int have_al = 0,  have_be = 0,  have_ga = 0;
	double a, b, c, al, be, ga;

	fh = fopen(filename, "r");
	if ( fh == NULL ) {
		ERROR("Couldn't open '%s'\n", filename);
		return NULL;
	}

	rval = fgets(line, 1023, fh);
	chomp(line);

	if ( strcmp(line, "CrystFEL unit cell file version 1.0") != 0 ) {
		fclose(fh);
		return load_cell_from_pdb(filename);
	}

	cell = cell_new();

	do {

		char line[1024];
		int n1;
		int i;
		char **bits;

		rval = fgets(line, 1023, fh);
		chomp(line);

		n1 = assplode(line, " \t", &bits, ASSPLODE_NONE);
		if ( n1 < 3 ) {
			for ( i=0; i<n1; i++ ) free(bits[i]);
			free(bits);
			continue;
		}

		if ( bits[0][0] == ';' ) {
			for ( i=0; i<n1; i++ ) free(bits[i]);
			free(bits);
			continue;
		}

		if ( strcmp(bits[0], "lattice_type") == 0 ) {
			LatticeType lt = lattice_from_str(bits[2]);
			cell_set_lattice_type(cell, lt);

		} else if ( strcmp(bits[0], "centering") == 0 ) {
			char cen = bits[2][0];
			if ( !check_centering(cen) ) {
				cell_set_centering(cell, cen);
			} else {
				ERROR("Unrecognised centering '%c'\n", cen);
			}

		} else if ( strcmp(bits[0], "unique_axis") == 0 ) {
			char ua = bits[2][0];
			if ( !check_unique_axis(ua) ) {
				cell_set_unique_axis(cell, ua);
			} else {
				ERROR("Unrecognised unique axis '%c'\n", ua);
			}

		} else if ( strcmp(bits[0], "a") == 0 ) {
			if ( !get_length_m(bits, n1, &a) ) {
				have_a = 1;
			}

		} else if ( strcmp(bits[0], "b") == 0 ) {
			if ( !get_length_m(bits, n1, &b) ) {
				have_b = 1;
			}

		} else if ( strcmp(bits[0], "c") == 0 ) {
			if ( !get_length_m(bits, n1, &c) ) {
				have_c = 1;
			}

		} else if ( strcmp(bits[0], "al") == 0 ) {
			if ( !get_angle_rad(bits, n1, &al) ) {
				have_al = 1;
			}

		} else if ( strcmp(bits[0], "be") == 0 ) {
			if ( !get_angle_rad(bits, n1, &be) ) {
				have_be = 1;
			}

		} else if ( strcmp(bits[0], "ga") == 0 ) {
			if ( !get_angle_rad(bits, n1, &ga) ) {
				have_ga = 1;
			}

		} else {
			ERROR("Unrecognised field '%s'\n", bits[0]);
		}

		for ( i=0; i<n1; i++ ) free(bits[i]);
		free(bits);

	} while ( rval != NULL );

	fclose(fh);

	if ( have_a && have_b && have_c && have_al && have_be && have_ga ) {
		cell_set_parameters(cell, a, b, c, al, be, ga);
	}

	switch ( cell_get_lattice_type(cell) ) {

		case L_TRICLINIC :
		case L_ORTHORHOMBIC :
		case L_CUBIC :
		case L_RHOMBOHEDRAL :
		if ( (cell_get_unique_axis(cell) != '?')
		  && (cell_get_unique_axis(cell) != '*') ) {
			ERROR("WARNING: Unique axis '%c' doesn't make sense "
			      "for lattice type %s.\n",
			      cell_get_unique_axis(cell),
			      str_lattice(cell_get_lattice_type(cell)));
		}
		break;

		case L_MONOCLINIC :
		case L_TETRAGONAL :
		case L_HEXAGONAL :
		if ( (cell_get_unique_axis(cell) == '?')
		  || (cell_get_unique_axis(cell) == '*') ) {
			ERROR("You must specify the unique axis for lattice "
			      "type %s.\n",
			      str_lattice(cell_get_lattice_type(cell)));
			return NULL;
		}
		break;

		default :
		ERROR("Unrecognised lattice type %i\n",
		      cell_get_lattice_type(cell));
		break;
	}

	validate_cell(cell);

	return cell;
}


/* Force the linker to bring in CBLAS to make GSL happy */
void cell_fudge_gslcblas()
{
        STATUS("%p\n", cblas_sgemm);
}


/**
 * rotate_cell:
 * @in: A %UnitCell to rotate
 * @omega: Euler angle about +z
 * @phi: Euler angle about +x
 * @rot: Euler angle about new +z
 *
 * Rotate a %UnitCell using Euler angles
 *
 * Returns: a newly allocated rotated copy of @in.
 *
 */
UnitCell *rotate_cell(UnitCell *in, double omega, double phi, double rot)
{
	UnitCell *out;
	double asx, asy, asz;
	double bsx, bsy, bsz;
	double csx, csy, csz;
	double xnew, ynew, znew;

	cell_get_reciprocal(in, &asx, &asy, &asz, &bsx, &bsy,
	                        &bsz, &csx, &csy, &csz);

	/* Rotate by "omega" about +z (parallel to c* and c unless triclinic) */
	xnew = asx*cos(omega) + asy*sin(omega);
	ynew = -asx*sin(omega) + asy*cos(omega);
	znew = asz;
	asx = xnew;  asy = ynew;  asz = znew;
	xnew = bsx*cos(omega) + bsy*sin(omega);
	ynew = -bsx*sin(omega) + bsy*cos(omega);
	znew = bsz;
	bsx = xnew;  bsy = ynew;  bsz = znew;
	xnew = csx*cos(omega) + csy*sin(omega);
	ynew = -csx*sin(omega) + csy*cos(omega);
	znew = csz;
	csx = xnew;  csy = ynew;  csz = znew;

	/* Rotate by "phi" about +x (not parallel to anything specific) */
	xnew = asx;
	ynew = asy*cos(phi) + asz*sin(phi);
	znew = -asy*sin(phi) + asz*cos(phi);
	asx = xnew;  asy = ynew;  asz = znew;
	xnew = bsx;
	ynew = bsy*cos(phi) + bsz*sin(phi);
	znew = -bsy*sin(phi) + bsz*cos(phi);
	bsx = xnew;  bsy = ynew;  bsz = znew;
	xnew = csx;
	ynew = csy*cos(phi) + csz*sin(phi);
	znew = -csy*sin(phi) + csz*cos(phi);
	csx = xnew;  csy = ynew;  csz = znew;

	/* Rotate by "rot" about the new +z (in-plane rotation) */
	xnew = asx*cos(rot) + asy*sin(rot);
	ynew = -asx*sin(rot) + asy*cos(rot);
	znew = asz;
	asx = xnew;  asy = ynew;  asz = znew;
	xnew = bsx*cos(rot) + bsy*sin(rot);
	ynew = -bsx*sin(rot) + bsy*cos(rot);
	znew = bsz;
	bsx = xnew;  bsy = ynew;  bsz = znew;
	xnew = csx*cos(rot) + csy*sin(rot);
	ynew = -csx*sin(rot) + csy*cos(rot);
	znew = csz;
	csx = xnew;  csy = ynew;  csz = znew;

	out = cell_new_from_cell(in);
	cell_set_reciprocal(out, asx, asy, asz, bsx, bsy, bsz, csx, csy, csz);

	return out;
}


int cell_is_sensible(UnitCell *cell)
{
	double a, b, c, al, be, ga;

	cell_get_parameters(cell, &a, &b, &c, &al, &be, &ga);
	if (   al + be + ga >= 2.0*M_PI ) return 0;
	if (   al + be - ga >= 2.0*M_PI ) return 0;
	if (   al - be + ga >= 2.0*M_PI ) return 0;
	if ( - al + be + ga >= 2.0*M_PI ) return 0;
	if (   al + be + ga <= 0.0 ) return 0;
	if (   al + be - ga <= 0.0 ) return 0;
	if (   al - be + ga <= 0.0 ) return 0;
	if ( - al + be + ga <= 0.0 ) return 0;
	if ( isnan(al) ) return 0;
	if ( isnan(be) ) return 0;
	if ( isnan(ga) ) return 0;
	return 1;
}


/**
 * validate_cell:
 * @cell: A %UnitCell to validate
 *
 * Perform some checks for crystallographic validity @cell, such as that the
 * lattice is a conventional Bravais lattice.
 * Warnings are printied if any of the checks are failed.
 *
 * Returns: zero if the cell is fine, 1 if it is unconventional but otherwise
 *  OK (e.g. left-handed or not a Bravais lattice), and 2 if there is a serious
 *  problem such as the parameters being physically impossible.
 *
 */
int validate_cell(UnitCell *cell)
{
	int err = 0;
	char cen, ua;

	if ( cell_has_parameters(cell) && !cell_is_sensible(cell) ) {
		ERROR("WARNING: Unit cell parameters are not sensible.\n");
		err = 2;
	}

	if ( !bravais_lattice(cell) ) {
		ERROR("WARNING: Unit cell is not a conventional Bravais"
		      " lattice.\n");
		err = 1;
	}

	if ( cell_has_parameters(cell) && !right_handed(cell) ) {
		ERROR("WARNING: Unit cell is not right handed.\n");
		err = 1;
	}

	/* For monoclinic A, B or C centering, the unique axis must be something
	 * other than the centering. */
	if ( cell_get_lattice_type(cell) == L_MONOCLINIC ) {
		cen = cell_get_centering(cell);
		ua = cell_get_unique_axis(cell);
		if ( ((cen == 'A') && (ua == 'a'))
		  || ((cen == 'B') && (ua == 'b'))
		  || ((cen == 'C') && (ua == 'c')) ) {
			ERROR("WARNING: A, B or C centering matches unique"
			      " axis.\n");
			err = 2;
		}
	}

	return err;
}


/**
 * forbidden_reflection:
 * @cell: A %UnitCell
 * @h: h index to check
 * @k: k index to check
 * @l: l index to check
 *
 * Returns: true if this reflection is forbidden.
 *
 */
int forbidden_reflection(UnitCell *cell,
                         signed int h, signed int k, signed int l)
{
	char cen;

	cen = cell_get_centering(cell);

	/* Reflection conditions here must match the transformation matrices
	 * in centering_transformation().  tests/centering_check verifies
	 * this (amongst other things). */

	if ( cen == 'P' ) return 0;
	if ( cen == 'R' ) return 0;

	if ( cen == 'A' ) return (k+l) % 2;
	if ( cen == 'B' ) return (h+l) % 2;
	if ( cen == 'C' ) return (h+k) % 2;

	if ( cen == 'I' ) return (h+k+l) % 2;
	if ( cen == 'F' ) return ((h+k) % 2) || ((h+l) % 2) || ((k+l) % 2);

	/* Obverse setting */
	if ( cen == 'H' ) return (-h+k+l) % 3;

	return 0;
}


/* Returns cell volume in A^3 */
double cell_get_volume(UnitCell *cell)
{
	double asx, asy, asz;
	double bsx, bsy, bsz;
	double csx, csy, csz;
	struct rvec aCb;
	double rec_volume;

	if ( cell_get_reciprocal(cell, &asx, &asy, &asz,
	                               &bsx, &bsy, &bsz,
	                               &csx, &csy, &csz) ) {
		ERROR("Couldn't get reciprocal cell.\n");
		return 0;
	}

	/* "a" cross "b" */
	aCb.u = asy*bsz - asz*bsy;
	aCb.v = - (asx*bsz - asz*bsx);
	aCb.w = asx*bsy - asy*bsx;

	/* "a cross b" dot "c" */
	rec_volume = (aCb.u*csx + aCb.v*csy + aCb.w*csz)/1e30;

	return 1/rec_volume;
}


/**
 * compare_cell_parameters:
 * @a: A %UnitCell
 * @b: Another %UnitCell
 * @ltl: Maximum allowable fractional difference in axis lengths
 * @atl: Maximum allowable difference in reciprocal angles (in radians)
 *
 * Compare the two unit cells.  If the real space parameters match to within
 * fractional difference @ltl, and the inter-axial angles match within @atl,
 * and the centering matches, this function returns 1.  Otherwise 0.
 *
 * This function considers the cell parameters and centering, but ignores the
 * orientation of the cell.  If you want to compare the orientation as well,
 * use compare_cell_parameters_and_orientation() instead.
 *
 * Returns: non-zero if the cells match.
 *
 */
int compare_cell_parameters(UnitCell *cell1, UnitCell *cell2,
                            float ltl, float atl)
{
	double a1, b1, c1, al1, be1, ga1;
	double a2, b2, c2, al2, be2, ga2;

	/* Centering must match: we don't arbitrarte primitive vs centered,
	 * different cell choices etc */
	if ( cell_get_centering(cell1) != cell_get_centering(cell2) ) return 0;

	cell_get_parameters(cell1, &a1, &b1, &c1, &al1, &be1, &ga1);
	cell_get_parameters(cell2, &a2, &b2, &c2, &al2, &be2, &ga2);

	if ( !within_tolerance(a1, a2, ltl*100.0) ) return 0;
	if ( !within_tolerance(b1, b2, ltl*100.0) ) return 0;
	if ( !within_tolerance(c1, c2, ltl*100.0) ) return 0;
	if ( fabs(al1-al2) > atl ) return 0;
	if ( fabs(be1-be2) > atl ) return 0;
	if ( fabs(ga1-ga2) > atl ) return 0;

	return 1;
}


static double moduli_check(double ax, double ay, double az,
                           double bx, double by, double bz)
{
	double ma = modulus(ax, ay, az);
	double mb = modulus(bx, by, bz);
	return fabs(ma-mb)/ma;
}


/**
 * compare_cell_parameters_and_orientation:
 * @a: A %UnitCell
 * @b: Another %UnitCell
 * @ltl: Maximum allowable fractional difference in reciprocal axis lengths
 * @atl: Maximum allowable difference in reciprocal angles (in radians)
 *
 * Compare the two unit cells.  If the axes match in length (to within
 * fractional difference @ltl) and the axes are aligned to within @atl radians,
 * this function returns non-zero.
 *
 * This function compares the orientation of the cell as well as the parameters.
 * If you just want to see if the parameters are the same, use
 * compare_cell_parameters() instead.
 *
 * The cells @a and @b must have the same centering.  Otherwise, this function
 * always returns zero.
 *
 * Returns: non-zero if the cells match.
 *
 */
int compare_cell_parameters_and_orientation(UnitCell *cell1, UnitCell *cell2,
                                            const double ltl, const double atl)
{
	double asx1, asy1, asz1, bsx1, bsy1, bsz1, csx1, csy1, csz1;
	double asx2, asy2, asz2, bsx2, bsy2, bsz2, csx2, csy2, csz2;

	if ( cell_get_centering(cell1) != cell_get_centering(cell2) ) return 0;

	cell_get_cartesian(cell1, &asx1, &asy1, &asz1,
	                          &bsx1, &bsy1, &bsz1,
	                          &csx1, &csy1, &csz1);

	cell_get_cartesian(cell2, &asx2, &asy2, &asz2,
	                          &bsx2, &bsy2, &bsz2,
	                          &csx2, &csy2, &csz2);

	if ( angle_between(asx1, asy1, asz1, asx2, asy2, asz2) > atl ) return 0;
	if ( angle_between(bsx1, bsy1, bsz1, bsx2, bsy2, bsz2) > atl ) return 0;
	if ( angle_between(csx1, csy1, csz1, csx2, csy2, csz2) > atl ) return 0;

	if ( moduli_check(asx1, asy1, asz1, asx2, asy2, asz2) > ltl ) return 0;
	if ( moduli_check(bsx1, bsy1, bsz1, bsx2, bsy2, bsz2) > ltl ) return 0;
	if ( moduli_check(csx1, csy1, csz1, csx2, csy2, csz2) > ltl ) return 0;

	return 1;
}


/**
 * compare_reindexed_cell_parameters_and_orientation:
 * @a: A %UnitCell
 * @b: Another %UnitCell
 * @ltl: Maximum allowable fractional difference in reciprocal axis lengths
 * @atl: Maximum allowable difference in reciprocal angles (in radians)
 * @pmb: Place to store pointer to matrix
 *
 * Compare the two unit cells.  If, using any permutation of the axes, the
 * axes can be made to match in length (to within fractional difference @ltl)
 * and the axes aligned to within @atl radians, this function returns non-zero
 * and stores the transformation to map @b onto @a.
 *
 * Note that the orientations of the cells must match, not just the parameters.
 * The comparison is done after reindexing using
 * compare_cell_parameters_and_orientation().
 *
 * The cells @a and @b must have the same centering.  Otherwise, this function
 * always returns zero.
 *
 * Returns: non-zero if the cells match.
 *
 */
int compare_reindexed_cell_parameters_and_orientation(UnitCell *a, UnitCell *b,
                                                      double ltl, double atl,
                                                      IntegerMatrix **pmb)
{
	IntegerMatrix *m;
	int i[9];

	if ( cell_get_centering(a) != cell_get_centering(b) ) return 0;

	m = intmat_new(3, 3);

	for ( i[0]=-1; i[0]<=+1; i[0]++ ) {
	for ( i[1]=-1; i[1]<=+1; i[1]++ ) {
	for ( i[2]=-1; i[2]<=+1; i[2]++ ) {
	for ( i[3]=-1; i[3]<=+1; i[3]++ ) {
	for ( i[4]=-1; i[4]<=+1; i[4]++ ) {
	for ( i[5]=-1; i[5]<=+1; i[5]++ ) {
	for ( i[6]=-1; i[6]<=+1; i[6]++ ) {
	for ( i[7]=-1; i[7]<=+1; i[7]++ ) {
	for ( i[8]=-1; i[8]<=+1; i[8]++ ) {

		UnitCell *nc;
		int j, k;
		int l = 0;

		for ( j=0; j<3; j++ )
			for ( k=0; k<3; k++ )
				intmat_set(m, j, k, i[l++]);

		if ( intmat_det(m) != +1 ) continue;

		nc = cell_transform_intmat(b, m);

		if ( compare_cell_parameters_and_orientation(a, nc, ltl, atl) ) {
			if ( pmb != NULL ) *pmb = m;
			cell_free(nc);
			return 1;
		}

		cell_free(nc);

	}
	}
	}
	}
	}
	}
	}
	}
	}

	intmat_free(m);
	return 0;
}


struct cand
{
	Rational abc[3];
	double fom;
};


static int cmpcand(const void *av, const void *bv)
{
	const struct cand *a = av;
	const struct cand *b = bv;
	return a->fom > b->fom;
}


static Rational *find_candidates(double len, double *a, double *b, double *c,
                                 double ltl, int *pncand)
{
	Rational *r;
	struct cand *cands;
	const int max_cand = 1024;
	int ncand = 0;
	Rational *rat;
	int nrat;
	int nrej = 0;
	int ia, ib, ic;
	int i;

	cands = malloc(max_cand * sizeof(struct cand));
	if ( cands == NULL ) return NULL;

	rat = rtnl_list(-5, 5, 1, 4, &nrat);
	if ( rat == NULL ) return NULL;

	for ( ia=0; ia<nrat; ia++ ) {
	for ( ib=0; ib<nrat; ib++ ) {
	for ( ic=0; ic<nrat; ic++ ) {
		double vec[3];
		double abc[3];
		double veclen;
		abc[0] = rtnl_as_double(rat[ia]);
		abc[1] = rtnl_as_double(rat[ib]);
		abc[2] = rtnl_as_double(rat[ic]);
		vec[0] = a[0]*abc[0] + b[0]*abc[1] + c[0]*abc[2];
		vec[1] = a[1]*abc[0] + b[1]*abc[1] + c[1]*abc[2];
		vec[2] = a[2]*abc[0] + b[2]*abc[1] + c[2]*abc[2];
		veclen = modulus(vec[0], vec[1], vec[2]);
		if ( within_tolerance(len, veclen, ltl*100.0) ) {
			if ( ncand == max_cand ) {
				nrej++;
			} else {
				cands[ncand].abc[0] = rat[ia];
				cands[ncand].abc[1] = rat[ib];
				cands[ncand].abc[2] = rat[ic];
				cands[ncand].fom = fabs(veclen - len);
				ncand++;
			}
		}
	}
	}
	}

	if ( nrej ) {
		ERROR("WARNING: Too many vector candidates (%i rejected)\n", nrej);
	}

	/* Sort by difference from reference vector length */
	qsort(cands, ncand, sizeof(struct cand), cmpcand);

	r = malloc(ncand * 3 * sizeof(Rational));
	if ( r == 0 ) return NULL;

	for ( i=0; i<ncand; i++ ) {
		r[3*i+0] = cands[i].abc[0];
		r[3*i+1] = cands[i].abc[1];
		r[3*i+2] = cands[i].abc[2];
	}
	free(cands);

	*pncand = ncand;
	return r;
}


static void g6_components(double *g6, double a, double b, double c,
                                      double al, double be, double ga)
{
	g6[0] = a*a;
	g6[1] = b*b;
	g6[2] = c*c;
	g6[3] = 2.0*b*c*cos(al);
	g6[4] = 2.0*a*c*cos(be);
	g6[5] = 2.0*a*b*cos(ga);
}


static double g6_distance(double a1, double b1, double c1,
                          double al1, double be1, double ga1,
                          double a2, double b2, double c2,
                          double al2, double be2, double ga2)
{
	double g1[6], g2[6];
	int i;
	double total = 0.0;
	g6_components(g1, a1, b1, c1, al1, be1, ga1);
	g6_components(g2, a2, b2, c2, al2, be2, ga2);
	for ( i=0; i<6; i++ ) {
		total += (g1[i]-g2[i])*(g1[i]-g2[i]);
	}
	return sqrt(total);
}


/**
 * compare_reindexed_cell_parameters:
 * @cell_in: A %UnitCell
 * @reference_in: Another %UnitCell
 * @ltl: Maximum allowable fractional difference in direct-space axis lengths
 * @atl: Maximum allowable difference in direct-space angles (in radians)
 * @pmb: Place to store pointer to matrix
 *
 * Compare the @cell_in with @reference_in.  If @cell is a derivative lattice
 * of @reference, within fractional axis length difference @ltl and absolute angle
 * difference @atl (in radians), this function returns non-zero and stores the
 * transformation which needs to be applied to @cell_in at @pmb.
 *
 * Note that the tolerances will be applied to the primitive unit cell.  If
 * the reference cell is centered, a primitive unit cell will first be calculated.
 *
 * Subject to the tolerances, this function will find the transformation which
 * gives the best match to the reference cell, using the Euclidian norm in
 * G6 [see e.g. Andrews and Bernstein, Acta Cryst. A44 (1988) p1009].
 *
 * Only the cell parameters will be compared.  The relative orientations are
 * irrelevant.
 *
 * Returns: non-zero if the cells match, zero for no match or error.
 *
 */
int compare_reindexed_cell_parameters(UnitCell *cell_in, UnitCell *reference_in,
                                      double ltl, double atl,
                                      RationalMatrix **pmb)
{
	UnitCell *cell;
	UnitCell *reference;
	IntegerMatrix *CBint;
	RationalMatrix *CiA;
	RationalMatrix *CB;
	RationalMatrix *m;
	double a, b, c, al, be, ga;
	double av[3], bv[3], cv[3];
	Rational *cand_a;
	Rational *cand_b;
	Rational *cand_c;
	int ncand_a, ncand_b, ncand_c;
	int ia, ib;
	RationalMatrix *MCiA;
	RationalMatrix *CBMCiA;
	double min_dist = +INFINITY;

	/* Actually compare against primitive version of reference */
	reference = uncenter_cell(reference_in, &CBint, NULL);
	if ( reference == NULL ) return 0;
	CB = rtnl_mtx_from_intmat(CBint);
	intmat_free(CBint);

	/* Actually compare primitive version of cell */
	cell = uncenter_cell(cell_in, NULL, &CiA);
	if ( cell == NULL ) return 0;

	/* Get target parameters */
	cell_get_parameters(reference, &a, &b, &c, &al, &be, &ga);
	cell_get_cartesian(cell, &av[0], &av[1], &av[2],
	                         &bv[0], &bv[1], &bv[2],
	                         &cv[0], &cv[1], &cv[2]);

	/* Find vectors in 'cell' with lengths close to a, b and c */
	cand_a = find_candidates(a, av, bv, cv, ltl, &ncand_a);
	cand_b = find_candidates(b, av, bv, cv, ltl, &ncand_b);
	cand_c = find_candidates(c, av, bv, cv, ltl, &ncand_c);

	m = rtnl_mtx_new(3, 3);
	MCiA = rtnl_mtx_new(3, 3);
	CBMCiA = rtnl_mtx_new(3, 3);
	for ( ia=0; ia<ncand_a; ia++ ) {
		for ( ib=0; ib<ncand_b; ib++ ) {

			UnitCell *test;
			double at, bt, ct, alt, bet, gat;
			double dist;
			int ic = 0;

			/* Form the matrix using the first candidate for c */
			rtnl_mtx_set(m, 0, 0, cand_a[3*ia+0]);
			rtnl_mtx_set(m, 1, 0, cand_a[3*ia+1]);
			rtnl_mtx_set(m, 2, 0, cand_a[3*ia+2]);
			rtnl_mtx_set(m, 0, 1, cand_b[3*ib+0]);
			rtnl_mtx_set(m, 1, 1, cand_b[3*ib+1]);
			rtnl_mtx_set(m, 2, 1, cand_b[3*ib+2]);
			rtnl_mtx_set(m, 0, 2, cand_c[3*ic+0]);
			rtnl_mtx_set(m, 1, 2, cand_c[3*ic+1]);
			rtnl_mtx_set(m, 2, 2, cand_c[3*ic+2]);

			/* Check angle between a and b */
			test = cell_transform_rational(cell, m);
			cell_get_parameters(test, &at, &bt, &ct, &alt, &bet, &gat);
			cell_free(test);
			if ( fabs(gat - ga) > atl ) continue;

			/* Gamma OK, now look for place for c axis */
			for ( ic=0; ic<ncand_c; ic++ ) {

				rtnl_mtx_set(m, 0, 0, cand_a[3*ia+0]);
				rtnl_mtx_set(m, 1, 0, cand_a[3*ia+1]);
				rtnl_mtx_set(m, 2, 0, cand_a[3*ia+2]);
				rtnl_mtx_set(m, 0, 1, cand_b[3*ib+0]);
				rtnl_mtx_set(m, 1, 1, cand_b[3*ib+1]);
				rtnl_mtx_set(m, 2, 1, cand_b[3*ib+2]);
				rtnl_mtx_set(m, 0, 2, cand_c[3*ic+0]);
				rtnl_mtx_set(m, 1, 2, cand_c[3*ic+1]);
				rtnl_mtx_set(m, 2, 2, cand_c[3*ic+2]);

				if ( rtnl_cmp(rtnl_mtx_det(m),rtnl_zero()) == 0 ) continue;

				test = cell_transform_rational(cell, m);
				cell_get_parameters(test, &at, &bt, &ct, &alt, &bet, &gat);
				if ( !right_handed(test) ) {
					cell_free(test);
					continue;
				}
				if ( fabs(alt - al) > atl ) {
					cell_free(test);
					continue;
				}
				if ( fabs(bet - be) > atl ) {
					cell_free(test);
					continue;
				}

				dist = g6_distance(at, bt, ct, alt, bet, gat,
				                   a, b, c, al, be, ga);
				if ( dist < min_dist ) {
					min_dist = dist;
					rtnl_mtx_mtxmult(m, CiA, MCiA);
				}

				cell_free(test);

			}
		}
	}

	rtnl_mtx_free(m);
	free(cand_a);
	free(cand_b);
	free(cand_c);

	if ( isinf(min_dist) ) {
		rtnl_mtx_free(CBMCiA);
		rtnl_mtx_free(MCiA);
		*pmb = NULL;
		return 0;
	}

	/* Solution found */
	rtnl_mtx_mtxmult(CB, MCiA, CBMCiA);
	rtnl_mtx_free(MCiA);
	*pmb = CBMCiA;
	return 1;
}
