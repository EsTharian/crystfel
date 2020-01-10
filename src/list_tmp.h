/*
 * Template for creating indexed 3D lists of a given type, usually indexed
 * as signed h,k,l values where -INDMAX<={h,k,l}<=+INDMAX.
 *
 * These are used, for example, for:
 *  - a list of 'double complex' values for storing structure factors,
 *  - a list of 'double' values for storing reflection intensities,
 *  - a list of 'unsigned int' values for counts of some sort.
 *
 * When LABEL and TYPE are #defined appropriately, including this header
 * defines functions such as:
 *  - new_list_<LABEL>(), for creating a new list of the given type,
 *  - set_<LABEL>(), for setting a value in a list,
 *  - lookup_<LABEL>(), for retrieving values from a list,
 * ..and so on.
 *
 * See src/utils.h for more information.
 *
 */

/*
 * Copyright © 2012-2020 Deutsches Elektronen-Synchrotron DESY,
 *                       a research centre of the Helmholtz Association.
 *
 * Authors:
 *   2009-2010,2012 Thomas White <taw@physics.org>
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

#include <stdio.h>

#define ERROR_T(...) fprintf(stderr, __VA_ARGS__)


static inline void LABEL(set_arr)(TYPE *ref, signed int h,
                              signed int k, signed int l,
                              TYPE i)
{
	int idx;

	if ( (abs(h) > INDMAX) || (abs(k) > INDMAX) || (abs(l) > INDMAX) ) {
		ERROR_T("\nReflection %i %i %i is out of range!\n", h, k, l);
		ERROR_T("You need to re-configure INDMAX and re-run.\n");
		exit(1);
	}

	if ( h < 0 ) h += IDIM;
	if ( k < 0 ) k += IDIM;
	if ( l < 0 ) l += IDIM;

	idx = h + (IDIM*k) + (IDIM*IDIM*l);
	ref[idx] = i;
}


static inline TYPE LABEL(lookup_arr)(const TYPE *ref, signed int h,
                                 signed int k, signed int l)
{
	int idx;

	if ( (abs(h) > INDMAX) || (abs(k) > INDMAX) || (abs(l) > INDMAX) ) {
		ERROR_T("\nReflection %i %i %i is out of range!\n", h, k, l);
		ERROR_T("You need to re-configure INDMAX and re-run.\n");
		exit(1);
	}

	if ( h < 0 ) h += IDIM;
	if ( k < 0 ) k += IDIM;
	if ( l < 0 ) l += IDIM;

	idx = h + (IDIM*k) + (IDIM*IDIM*l);
	return ref[idx];
}


static inline TYPE *LABEL(new_arr)(void)
{
	TYPE *r;
	size_t r_size;
	r_size = IDIM*IDIM*IDIM*sizeof(TYPE);
	r = malloc(r_size);
	memset(r, 0, r_size);
	return r;
}


#undef LABEL
#undef TYPE
#undef ERROR_T
