/*
 * index.h
 *
 * Perform indexing (somehow)
 *
 * Copyright © 2012-2016 Deutsches Elektronen-Synchrotron DESY,
 *                       a research centre of the Helmholtz Association.
 * Copyright © 2012 Richard Kirian
 * Copyright © 2012 Lorenzo Galli
 *
 * Authors:
 *   2010-2016 Thomas White <taw@physics.org>
 *   2010      Richard Kirian
 *   2012      Lorenzo Galli
 *   2015      Kenneth Beyerlein <kenneth.beyerlein@desy.de>
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

#ifndef INDEX_H
#define INDEX_H

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif


#define INDEXING_DEFAULTS_DIRAX (INDEXING_DIRAX | INDEXING_CHECK_PEAKS         \
                                     | INDEXING_CHECK_CELL_COMBINATIONS        \
                                     | INDEXING_RETRY)

#define INDEXING_DEFAULTS_ASDF (INDEXING_ASDF | INDEXING_CHECK_PEAKS           \
                                     | INDEXING_CHECK_CELL_COMBINATIONS        \
                                     | INDEXING_RETRY)

#define INDEXING_DEFAULTS_MOSFLM (INDEXING_MOSFLM | INDEXING_CHECK_PEAKS       \
                                     | INDEXING_CHECK_CELL_COMBINATIONS        \
                                     | INDEXING_USE_LATTICE_TYPE               \
                                     | INDEXING_USE_CELL_PARAMETERS            \
                                     | INDEXING_RETRY)

#define INDEXING_DEFAULTS_FELIX (INDEXING_FELIX                                \
                                     | INDEXING_USE_LATTICE_TYPE               \
                                     | INDEXING_USE_CELL_PARAMETERS            \
                                     | INDEXING_RETRY)

/* Axis check is needed for XDS, because it likes to permute the axes */
#define INDEXING_DEFAULTS_XDS (INDEXING_XDS | INDEXING_USE_LATTICE_TYPE        \
                                     | INDEXING_USE_CELL_PARAMETERS            \
                                     | INDEXING_CHECK_CELL_AXES                \
                                     | INDEXING_CHECK_PEAKS                    \
                                     | INDEXING_RETRY)

/**
 * IndexingMethod:
 * @INDEXING_NONE: No indexing to be performed
 * @INDEXING_DIRAX: Invoke DirAx
 * @INDEXING_MOSFLM: Invoke MOSFLM
 * @INDEXING_FELIX: Invoke Felix
 * @INDEXING_XDS: Invoke XDS
 * @INDEXING_SIMULATION: Dummy value
 * @INDEXING_DEBUG: Results injector for debugging
 * @INDEXING_ASDF: Use in-built "asdf" indexer
 * @INDEXING_CHECK_CELL_COMBINATIONS: Check linear combinations of unit cell
 *   axes for agreement with given cell.
 * @INDEXING_CHECK_CELL_AXES: Check unit cell axes for agreement with given
 *   cell, and permute them if necessary.
 * @INDEXING_CHECK_PEAKS: Check that the peaks can be explained by the indexing
 *   result.
 * @INDEXING_USE_LATTICE_TYPE: Use lattice type and centering information to
 *   guide the indexing process.
 * @INDEXING_USE_CELL_PARAMETERS: Use the unit cell parameters to guide the
 *   indexingprocess.
 * @INDEXING_RETRY: If the indexer doesn't succeed, delete the weakest peaks
 *   and try again.
 * @INDEXING_MULTI: If the indexer succeeds, delete the peaks explained by the
 *   lattice and try again in the hope of finding another crystal.
 *
 * An enumeration of all the available indexing methods.  The dummy value
 * @INDEXING_SIMULATION is used by partial_sim to indicate that no indexing was
 * performed, and that the indexing results are just from simulation.
 **/
typedef enum {

	INDEXING_NONE   = 0,

	/* The core indexing methods themselves */
	INDEXING_DIRAX  = 1,
	INDEXING_MOSFLM = 2,
	INDEXING_FELIX = 4,
	INDEXING_XDS = 5,
	INDEXING_SIMULATION = 6,
	INDEXING_DEBUG = 7,
	INDEXING_ASDF = 8,

	/* Bits at the top of the IndexingMethod are flags which modify the
	 * behaviour of the indexer. */
	INDEXING_CHECK_CELL_COMBINATIONS = 256,
	INDEXING_CHECK_CELL_AXES         = 512,
	INDEXING_CHECK_PEAKS             = 1024,
	INDEXING_USE_LATTICE_TYPE        = 2048,
	INDEXING_USE_CELL_PARAMETERS     = 4096,
	INDEXING_RETRY                   = 8192,
	INDEXING_MULTI                   = 16384

} IndexingMethod;

/* This defines the bits in "IndexingMethod" which are used to represent the
 * core of the indexing method */
#define INDEXING_METHOD_MASK (0xff)

#ifdef __cplusplus
extern "C" {
#endif

/**
 * IndexingPrivate:
 *
 * This is an opaque data structure containing information needed by the
 * indexing method.
 **/
typedef void *IndexingPrivate;

extern IndexingMethod *build_indexer_list(const char *str);
extern char *indexer_str(IndexingMethod indm);

#include "detector.h"
#include "cell.h"
#include "image.h"

extern IndexingPrivate **prepare_indexing(IndexingMethod *indm, UnitCell *cell,
                                          struct detector *det, float *ltl,
                                          const char *options);

extern void index_pattern(struct image *image,
                          IndexingMethod *indms, IndexingPrivate **iprivs);

extern void cleanup_indexing(IndexingMethod *indms, IndexingPrivate **privs);

#ifdef __cplusplus
}
#endif

#endif	/* INDEX_H */
