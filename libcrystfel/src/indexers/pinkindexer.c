/*
 * pinkindexer.c
 *
 * Interface to PinkIndexer
 *
 * Copyright © 2017-2021 Deutsches Elektronen-Synchrotron DESY,
 *                       a research centre of the Helmholtz Association.
 *
 * Authors:
 *   2017-2019 Yaroslav Gevorkov <yaroslav.gevorkov@desy.de>
 *   2021 Thomas White <thomas.white@desy.de>
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

#include "pinkindexer.h"


#include <stdlib.h>
#include <sys/errno.h>
#include <argp.h>

#include "utils.h"
#include "cell-utils.h"
#include "peaks.h"

struct pinkIndexer_options {
	unsigned int considered_peaks_count;
	unsigned int angle_resolution;
	unsigned int refinement_type;
	float maxResolutionForIndexing_1_per_A;
	float tolerance;
	int thread_count;
	float reflectionRadius; /* In m^-1 */
	float customBandwidth;
	float maxRefinementDisbalance;
};

#ifdef HAVE_PINKINDEXER

#include <pinkIndexer/adaptions/crystfel/Lattice.h>
#include <pinkIndexer/adaptions/crystfel/ExperimentSettings.h>
#include <pinkIndexer/adaptions/crystfel/PinkIndexer.h>

struct pinkIndexer_private_data {
	PinkIndexer *pinkIndexer;
	reciprocalPeaks_1_per_A_t reciprocalPeaks_1_per_A;
	float *intensities;

	IndexingMethod indm;
	UnitCell *cellTemplate;
	int threadCount;

	float maxRefinementDisbalance;

	IntegerMatrix *centeringTransformation;
	LatticeTransform_t latticeReductionTransform;
};

//static void reduceCell(UnitCell* cell, LatticeTransform_t* appliedReductionTransform);
//static void restoreCell(UnitCell *cell, LatticeTransform_t* appliedReductionTransform);
static void reduceReciprocalCell(UnitCell* cell, LatticeTransform_t* appliedReductionTransform);
static void restoreReciprocalCell(UnitCell *cell, LatticeTransform_t* appliedReductionTransform);
static void makeRightHanded(UnitCell* cell);

int run_pinkIndexer(struct image *image, void *ipriv)
{
	struct pinkIndexer_private_data* pinkIndexer_private_data = (struct pinkIndexer_private_data*) ipriv;
	reciprocalPeaks_1_per_A_t* reciprocalPeaks_1_per_A = &(pinkIndexer_private_data->reciprocalPeaks_1_per_A);
	float *intensities = pinkIndexer_private_data->intensities;

	/* FIXME: Check if wavelength is too far from estimate */
	int peakCountMax = image_feature_count(image->features);
	if (peakCountMax < 5) {
		int goodLatticesCount = 0;
		return goodLatticesCount;
	}
	reciprocalPeaks_1_per_A->peakCount = 0;
	for (int i = 0; i < peakCountMax && i < MAX_PEAK_COUNT_FOR_INDEXER; i++) {

		struct imagefeature *f;
		double r[3];

		f = image_get_feature(image->features, i);
		if ( f == NULL ) continue;

		detgeom_transform_coords(&image->detgeom->panels[f->pn],
		                         f->fs, f->ss, image->lambda,
		                         0.0, 0.0, r);
		reciprocalPeaks_1_per_A->coordinates_x[reciprocalPeaks_1_per_A->peakCount] = r[2] * 1e-10;
		reciprocalPeaks_1_per_A->coordinates_y[reciprocalPeaks_1_per_A->peakCount] = r[0] * 1e-10;
		reciprocalPeaks_1_per_A->coordinates_z[reciprocalPeaks_1_per_A->peakCount] = r[1] * 1e-10;
		intensities[reciprocalPeaks_1_per_A->peakCount] = (float) (f->intensity);
		reciprocalPeaks_1_per_A->peakCount++;
	}
	int indexed = 0;

	float center_shift[2];
	Lattice_t indexedLattice;
	int matchedPeaksCount = PinkIndexer_indexPattern(pinkIndexer_private_data->pinkIndexer,
	                                    &indexedLattice,
	                                    center_shift,
	                                    reciprocalPeaks_1_per_A,
	                                    intensities,
	                                    pinkIndexer_private_data->maxRefinementDisbalance,
	                                    pinkIndexer_private_data->threadCount);

	if ( matchedPeaksCount == -1 ) {

		STATUS("WARNING: Indexing solution was rejected due to too "
		       "large imbalance of the refinement.\n"
		        "If you see this message often, check the documentation "
		        "for parameter --pinkIndexer-max-refinement-disbalance\n");

	} else {

		UnitCell *uc;
		UnitCell *new_cell_trans;

		uc = cell_new();

		cell_set_reciprocal(uc, indexedLattice.ay * 1e10,
		                        indexedLattice.az * 1e10,
		                        indexedLattice.ax * 1e10,
		                        indexedLattice.by * 1e10,
		                        indexedLattice.bz * 1e10,
		                        indexedLattice.bx * 1e10,
		                        indexedLattice.cy * 1e10,
		                        indexedLattice.cz * 1e10,
		                        indexedLattice.cx * 1e10);

		restoreReciprocalCell(uc, &pinkIndexer_private_data->latticeReductionTransform);

		new_cell_trans = cell_transform_intmat(uc, pinkIndexer_private_data->centeringTransformation);
		cell_free(uc);

		cell_set_lattice_type(new_cell_trans,
		                      cell_get_lattice_type(pinkIndexer_private_data->cellTemplate));
		cell_set_centering(new_cell_trans,
		                   cell_get_centering(pinkIndexer_private_data->cellTemplate));
		cell_set_unique_axis(new_cell_trans,
		                     cell_get_unique_axis(pinkIndexer_private_data->cellTemplate));

		if ( validate_cell(new_cell_trans) ) {
			ERROR("pinkIndexer: problem with returned cell!\n");
		} else {

			Crystal *cr = crystal_new();
			if ( cr == NULL ) {
				ERROR("Failed to allocate crystal.\n");
				return 0;
			}
			crystal_set_cell(cr, new_cell_trans);
			crystal_set_det_shift(cr, center_shift[0],
			                          center_shift[1]);
			image_add_crystal(image, cr);
			indexed++;

		}

	}

	return indexed;
}


static int want_center_adjustment(struct pinkIndexer_options *pinkIndexer_opts)
{
	return (pinkIndexer_opts->refinement_type ==
	        REFINEMENT_TYPE_firstFixedThenVariableLatticeParametersCenterAdjustmentMultiSeed);
}


void *pinkIndexer_prepare(IndexingMethod *indm,
                          UnitCell *cell,
                          struct pinkIndexer_options *pinkIndexer_opts,
                          double wavelength_estimate)
{
	float beamEenergy_eV;

	if ( isnan(wavelength_estimate) ) {
		ERROR("PinkIndexer requires a wavelength estimate.  "
		      "Try again with --wavelength-estimate=xx\n");
		return NULL;
	} else {
		beamEenergy_eV = J_to_eV(ph_lambda_to_en(wavelength_estimate));
	}

	if ( !data_template_has_fixed_geometry(dtempl)
	   && want_center_adjustment(pinkIndexer_opts) )
	{

		ERROR("Geometry file refers to image metadata for "
		      "detector position.\n");
		ERROR("To use PinkIndexer with image center "
		      "refinement, use a fixed detector position in "
		      "the geometry file.\n");
		return NULL;
	}

	if ( cell == NULL ) {
		ERROR("Unit cell information is required for "
		      "PinkIndexer.\n");
		return NULL;
	}

	struct pinkIndexer_private_data* pinkIndexer_private_data = malloc(sizeof(struct pinkIndexer_private_data));
	allocReciprocalPeaks(&(pinkIndexer_private_data->reciprocalPeaks_1_per_A));
	pinkIndexer_private_data->intensities = malloc(MAX_PEAK_COUNT_FOR_INDEXER * sizeof(float));
	pinkIndexer_private_data->indm = *indm;
	pinkIndexer_private_data->cellTemplate = cell;
	pinkIndexer_private_data->threadCount = pinkIndexer_opts->thread_count;
	pinkIndexer_private_data->maxRefinementDisbalance = pinkIndexer_opts->maxRefinementDisbalance;

	UnitCell* primitiveCell = uncenter_cell(cell, &pinkIndexer_private_data->centeringTransformation, NULL);

	//reduceCell(primitiveCell, &pinkIndexer_private_data->latticeReductionTransform);
	reduceReciprocalCell(primitiveCell, &pinkIndexer_private_data->latticeReductionTransform);

	double asx, asy, asz, bsx, bsy, bsz, csx, csy, csz;
	int ret = cell_get_reciprocal(primitiveCell, &asx, &asy, &asz, &bsx, &bsy, &bsz, &csx, &csy, &csz);
	if (ret != 0) {
		ERROR("cell_get_reciprocal did not finish properly!");
	}

	Lattice_t lattice = { .ax = asz * 1e-10, .ay = asx * 1e-10, .az = asy * 1e-10,
	        .bx = bsz * 1e-10, .by = bsx * 1e-10, .bz = bsy * 1e-10,
	        .cx = csz * 1e-10, .cy = csx * 1e-10, .cz = csy * 1e-10 };

	float detectorDistance_m;
	if ( data_template_has_fixed_geometry(dtempl) ) {
		detectorDistance_m =  0.25;  /* fake value */
	} else {
		/* FIXME: Cannot get clen here without violating abstraction */
		detectorDistance_m = det->panels[0].clen + det->panels[0].coffset;
	}

	/* FIXME: Beam gone */
	float nonMonochromaticity = beam->bandwidth*5;
	if(pinkIndexer_opts->customBandwidth >= 0){
		nonMonochromaticity = pinkIndexer_opts->customBandwidth;
	}

	float reflectionRadius_1_per_A;
	if (pinkIndexer_opts->reflectionRadius < 0) {
		reflectionRadius_1_per_A = 0.02
		        * sqrt(lattice.ax * lattice.ax + lattice.ay * lattice.ay + lattice.az * lattice.az);
	}
	else {
		reflectionRadius_1_per_A = pinkIndexer_opts->reflectionRadius * 1e10;  /* m^-1 to A^-1*/
	}

	if(beamEenergy_eV > 75000 && nonMonochromaticity < 0.02 && reflectionRadius_1_per_A < 0.0005){
		STATUS("Trying to index electron diffraction? It might be "
		       " helpful to set a higher reflection radius "
		       "(see documentation for --pinkIndexer-reflection-radius)");
	}

	float divergenceAngle_deg = 0.01; //fake

	float tolerance = pinkIndexer_opts->tolerance;
	Lattice_t sampleReciprocalLattice_1_per_A = lattice;
	float detectorRadius_m = 0.03; //fake, only for prediction
	ExperimentSettings* experimentSettings = ExperimentSettings_new(beamEenergy_eV, detectorDistance_m,
	        detectorRadius_m, divergenceAngle_deg, nonMonochromaticity, sampleReciprocalLattice_1_per_A, tolerance,
	        reflectionRadius_1_per_A);

	consideredPeaksCount_t consideredPeaksCount = pinkIndexer_opts->considered_peaks_count;
	angleResolution_t angleResolution = pinkIndexer_opts->angle_resolution;
	refinementType_t refinementType = pinkIndexer_opts->refinement_type;
	float maxResolutionForIndexing_1_per_A = pinkIndexer_opts->maxResolutionForIndexing_1_per_A;
	pinkIndexer_private_data->pinkIndexer = PinkIndexer_new(experimentSettings, consideredPeaksCount, angleResolution,
	        refinementType,
	        maxResolutionForIndexing_1_per_A);

	ExperimentSettings_delete(experimentSettings);
	cell_free(primitiveCell);

	/* Flags that pinkIndexer knows about */
	*indm &= INDEXING_METHOD_MASK
	        | INDEXING_USE_CELL_PARAMETERS;

	return pinkIndexer_private_data;
}

//static void reduceCell(UnitCell *cell, LatticeTransform_t* appliedReductionTransform)
//{
//	double ax, ay, az, bx, by, bz, cx, cy, cz;
//	cell_get_cartesian(cell, &ax, &ay, &az, &bx, &by, &bz, &cx, &cy, &cz);
//
//	Lattice_t l = { ax, ay, az, bx, by, bz, cx, cy, cz };
//
//	reduceLattice(&l, appliedReductionTransform);
//
//	cell_set_cartesian(cell, l.ax, l.ay, l.az,
//	        l.bx, l.by, l.bz,
//	        l.cx, l.cy, l.cz);
//
//	makeRightHanded(cell);
//}
//
//static void restoreCell(UnitCell *cell, LatticeTransform_t* appliedReductionTransform)
//{
//
//	double ax, ay, az, bx, by, bz, cx, cy, cz;
//	cell_get_cartesian(cell, &ax, &ay, &az, &bx, &by, &bz, &cx, &cy, &cz);
//
//	Lattice_t l = { ax, ay, az, bx, by, bz, cx, cy, cz };
//
//	restoreLattice(&l, appliedReductionTransform);
//
//	cell_set_cartesian(cell, l.ax, l.ay, l.az,
//	        l.bx, l.by, l.bz,
//	        l.cx, l.cy, l.cz);
//
//	makeRightHanded(cell);
//}

static void reduceReciprocalCell(UnitCell *cell, LatticeTransform_t* appliedReductionTransform)
{
	double ax, ay, az, bx, by, bz, cx, cy, cz;
	cell_get_reciprocal(cell, &ax, &ay, &az, &bx, &by, &bz, &cx, &cy, &cz);

	Lattice_t l = { ax, ay, az, bx, by, bz, cx, cy, cz };

	reduceLattice(&l, appliedReductionTransform);

	cell_set_reciprocal(cell, l.ax, l.ay, l.az,
	        l.bx, l.by, l.bz,
	        l.cx, l.cy, l.cz);

	makeRightHanded(cell);
}

static void restoreReciprocalCell(UnitCell *cell, LatticeTransform_t* appliedReductionTransform)
{

	double ax, ay, az, bx, by, bz, cx, cy, cz;
	cell_get_reciprocal(cell, &ax, &ay, &az, &bx, &by, &bz, &cx, &cy, &cz);

	Lattice_t l = { ax, ay, az, bx, by, bz, cx, cy, cz };

	restoreLattice(&l, appliedReductionTransform);

	cell_set_reciprocal(cell, l.ax, l.ay, l.az,
	        l.bx, l.by, l.bz,
	        l.cx, l.cy, l.cz);

	makeRightHanded(cell);
}

static void makeRightHanded(UnitCell *cell)
{
	double ax, ay, az, bx, by, bz, cx, cy, cz;
	cell_get_cartesian(cell, &ax, &ay, &az, &bx, &by, &bz, &cx, &cy, &cz);

	if (!right_handed(cell)) {
		cell_set_cartesian(cell, -ax, -ay, -az, -bx, -by, -bz, -cx, -cy, -cz);
	}
}

void pinkIndexer_cleanup(void *pp)
{
	struct pinkIndexer_private_data* pinkIndexer_private_data = (struct pinkIndexer_private_data*) pp;

	freeReciprocalPeaks(pinkIndexer_private_data->reciprocalPeaks_1_per_A);
	free(pinkIndexer_private_data->intensities);
	intmat_free(pinkIndexer_private_data->centeringTransformation);
	PinkIndexer_delete(pinkIndexer_private_data->pinkIndexer);
}

const char *pinkIndexer_probe(UnitCell *cell)
{
	return "pinkIndexer";
}

#else /* HAVE_PINKINDEXER */

int run_pinkIndexer(struct image *image, void *ipriv)
{
	ERROR("This copy of CrystFEL was compiled without PINKINDEXER support.\n");
	return 0;
}

extern void *pinkIndexer_prepare(IndexingMethod *indm,
                                 UnitCell *cell,
                                 struct pinkIndexer_options *pinkIndexer_opts,
                                 double wavelength_estimate)
{
	ERROR("This copy of CrystFEL was compiled without PINKINDEXER support.\n");
	ERROR("To use PINKINDEXER indexing, recompile with PINKINDEXER.\n");
	return NULL;
}

void pinkIndexer_cleanup(void *pp)
{
}

const char *pinkIndexer_probe(UnitCell *cell)
{
	return NULL;
}

#endif /* HAVE_PINKINDEXER */

static void pinkIndexer_show_help()
{
	printf(
"Parameters for the PinkIndexer indexing algorithm:\n"
"     --pinkIndexer-considered-peaks-count=n\n"
"                           Considered peaks count, 0 (fewest) to 4 (most)\n"
"                            Default: 4\n"
"     --pinkIndexer-angle-resolution=n\n"
"                           Angle resolution, 0 (loosest) to 4 (most dense)\n"
"                            Default: 2\n"
"     --pinkIndexer-refinement-type=n\n"
"                           Refinement type, 0 (none) to 5 (most accurate)\n"
"                            Default: 1\n"
"     --pinkIndexer-tolerance=n\n"
"                           Relative tolerance of the lattice vectors.\n"
"                            Default 0.06\n"
"     --pinkIndexer-reflection-radius=n\n"
"                           Radius of the reflections in reciprocal space.\n"
"                            Specified in 1/A.  Default is 2%% of a*.\n"
"     --pinkIndexer-max-resolution-for-indexing=n\n"
"                           Measured in 1/A\n"
"     --pinkIndexer-thread-count=n\n"
"                           Thread count for internal parallelization \n"
"                            Default: 1\n"
"     --pinkIndexer-max-refinement-disbalance=n\n"
"                           Maximum disbalance after refinement:\n"
"                            0 (no disbalance) to 2 (extreme disbalance), default 0.4\n"
"     --pinkIndexer-override-bandwidth=n\n"
"                           Bandwidth in (delta energy)/(mean energy) to use for indexing.\n"
"     --pinkIndexer-override-visible-energy-range=min-max\n"
"                           Overrides photon energy and bandwidth according to a range of \n"
"                           energies that have high enough intensity to produce \"visible\" \n"
"                           Bragg spots on the detector.\n"
"                           Min and max range borders are separated by a minus sign (no whitespace).\n"
	);
}


int pinkIndexer_default_options(PinkIndexerOptions **opts_ptr)
{
	PinkIndexerOptions *opts;

	opts = malloc(sizeof(struct pinkIndexer_options));
	if ( opts == NULL ) return ENOMEM;

	opts->considered_peaks_count = 4;
	opts->angle_resolution = 2;
	opts->refinement_type = 1;
	opts->tolerance = 0.06;
	opts->maxResolutionForIndexing_1_per_A = +INFINITY;
	opts->thread_count = 1;
	opts->reflectionRadius = -1;
	opts->customBandwidth = -1;
	opts->maxRefinementDisbalance = 0.4;

	*opts_ptr = opts;
	return 0;
}


static error_t pinkindexer_parse_arg(int key, char *arg,
                                     struct argp_state *state)
{
	float tmp, tmp2;
	int r;
	struct pinkIndexer_options **opts_ptr = state->input;

	switch ( key ) {

		case ARGP_KEY_INIT :
		r = pinkIndexer_default_options(opts_ptr);
		if ( r ) return r;
		break;

		case 1 :
		pinkIndexer_show_help();
		return EINVAL;

		case 2 :
		if (sscanf(arg, "%u", &(*opts_ptr)->considered_peaks_count) != 1)
		{
			ERROR("Invalid value for "
			      "--pinkIndexer-considered-peaks-count\n");
			return EINVAL;
		}
		break;

		case 3 :
		if (sscanf(arg, "%u", &(*opts_ptr)->angle_resolution) != 1)
		{
			ERROR("Invalid value for "
			      "--pinkIndexer-angle_resolution\n");
			return EINVAL;
		}
		break;

		case 4 :
		if (sscanf(arg, "%u", &(*opts_ptr)->refinement_type) != 1)
		{
			ERROR("Invalid value for "
			      "--pinkIndexer-refinement-type\n");
			return EINVAL;
		}
		break;

		case 5 :
		if (sscanf(arg, "%d", &(*opts_ptr)->thread_count) != 1)
		{
			ERROR("Invalid value for --pinkIndexer-thread-count\n");
			return EINVAL;
		}
		break;

		case 6 :
		if (sscanf(arg, "%f", &(*opts_ptr)->maxResolutionForIndexing_1_per_A) != 1)
		{
			ERROR("Invalid value for "
			      "--pinkIndexer-max-resolution-for-indexing\n");
			return EINVAL;
		}
		break;

		case 7 :
		if (sscanf(arg, "%f", &(*opts_ptr)->tolerance) != 1)
		{
			ERROR("Invalid value for --pinkIndexer-tolerance\n");
			return EINVAL;
		}
		break;

		case 8 :
		ERROR("WARNING: --pinkIndexer-multi is ignored.\n");
		break;

		case 9 :
		ERROR("WARNING: --pinkIndexer-no-check-indexed is ignored.\n");
		break;

		case 10 :
		if (sscanf(arg, "%f", &tmp) != 1) {
			ERROR("Invalid value for --pinkIndexer-reflection-radius\n");
			return EINVAL;
		}
		(*opts_ptr)->reflectionRadius = tmp / 1e10; /* A^-1 to m^-1 */
		break;

		case 11 :
		ERROR("Please use --wavelength-estimate instead of "
		      "--pinkIndexer-override-photon-energy.\n");
		return EINVAL;

		case 12 :
		if (sscanf(arg, "%f", &(*opts_ptr)->customBandwidth) != 1)
		{
			ERROR("Invalid value for --pinkIndexer-override-bandwidth\n");
			return EINVAL;
		}
		break;
		case 13 :
		if (sscanf(arg, "%f-%f", &tmp, &tmp2) != 2)
		{
			ERROR("Invalid value for --pinkIndexer-override-visible-energy-range\n");
			return EINVAL;
		}
		(*opts_ptr)->customPhotonEnergy = (tmp + tmp2)/2;
		(*opts_ptr)->customBandwidth = (tmp2 - tmp)/(*opts_ptr)->customPhotonEnergy;
		if((*opts_ptr)->customBandwidth < 0){
			(*opts_ptr)->customBandwidth *= -1;
		}
		break;
		case 14 :
		if (sscanf(arg, "%f", &(*opts_ptr)->maxRefinementDisbalance) != 1)
		{
			ERROR("Invalid value for --pinkIndexer-max-refinement-disbalance\n");
			return EINVAL;
		}
	}

	return 0;
}


static struct argp_option pinkindexer_options[] = {

	{"help-pinkindexer", 1, NULL, OPTION_NO_USAGE,
	 "Show options for PinkIndexer indexing algorithm", 99},

	{"pinkIndexer-considered-peaks-count", 2, "n", OPTION_HIDDEN, NULL},
	{"pinkIndexer-cpc", 2, "n", OPTION_HIDDEN, NULL},

	{"pinkIndexer-angle-resolution", 3, "ang", OPTION_HIDDEN, NULL},
	{"pinkIndexer-ar", 3, "ang", OPTION_HIDDEN, NULL},

	{"pinkIndexer-refinement-type", 4, "t", OPTION_HIDDEN, NULL},
	{"pinkIndexer-rt", 4, "t", OPTION_HIDDEN, NULL},

	{"pinkIndexer-thread-count", 5, "n", OPTION_HIDDEN, NULL},
	{"pinkIndexer-tc", 5, "n", OPTION_HIDDEN, NULL},

	{"pinkIndexer-max-resolution-for-indexing", 6, "res", OPTION_HIDDEN, NULL},
	{"pinkIndexer-mrfi", 6, "res", OPTION_HIDDEN, NULL},

	{"pinkIndexer-tolerance", 7, "tol", OPTION_HIDDEN, NULL},
	{"pinkIndexer-tol", 7, "tol", OPTION_HIDDEN, NULL},

	{"pinkIndexer-multi", 8, NULL, OPTION_HIDDEN, NULL},

	{"pinkIndexer-no-check-indexed", 9, NULL, OPTION_HIDDEN, NULL},

	{"pinkIndexer-reflection-radius", 10, "r", OPTION_HIDDEN, NULL},

	{"pinkIndexer-override-photon-energy", 11, "ev", OPTION_HIDDEN, NULL},

	{"pinkIndexer-override-bandwidth", 12, "bw", OPTION_HIDDEN, NULL},

	{"pinkIndexer-override-visible-energy-range", 13, "overridenVisibleEnergyRange", OPTION_HIDDEN, NULL},

	{"pinkIndexer-max-refinement-disbalance", 14, "maxDisbalance", OPTION_HIDDEN, NULL},

	{0}
};


struct argp pinkIndexer_argp = { pinkindexer_options,
                                 pinkindexer_parse_arg,
                                 NULL, NULL, NULL, NULL, NULL };
