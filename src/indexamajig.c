/*
 * indexamajig.c
 *
 * Index patterns, output hkl+intensity etc.
 *
 * Copyright © 2012-2018 Deutsches Elektronen-Synchrotron DESY,
 *                       a research centre of the Helmholtz Association.
 * Copyright © 2012 Richard Kirian
 * Copyright © 2012 Lorenzo Galli
 *
 * Authors:
 *   2010-2017 Thomas White <taw@physics.org>
 *   2011      Richard Kirian
 *   2012      Lorenzo Galli
 *   2012      Chunhong Yoon
 *   2017      Valerio Mariani <valerio.mariani@desy.de>
 *   2017-2018 Yaroslav Gevorkov <yaroslav.gevorkov@desy.de>
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

#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <hdf5.h>
#include <gsl/gsl_errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "utils.h"
#include "hdf5-file.h"
#include "index.h"
#include "peaks.h"
#include "detector.h"
#include "filters.h"
#include "thread-pool.h"
#include "geometry.h"
#include "stream.h"
#include "reflist-utils.h"
#include "cell-utils.h"
#include "integration.h"
#include "taketwo.h"
#include "im-sandbox.h"
#include "image.h"


static void show_help(const char *s)
{
	printf("Syntax: %s [options]\n\n", s);
	printf(
"Index and integrate snapshot diffraction images.\n\n"
" -h, --help                Display this help message\n"
"     --version             Print CrystFEL version number and exit\n"
"\nBasic options:\n\n"
" -i, --input=<filename>    List of images to process.\n"
" -o, --output=<filename>   Output stream filename\n"
" -g  --geometry=<file>     Detector geometry filename\n"
"     --basename            Remove the directory parts of the filenames\n"
" -x, --prefix=<p>          Prefix filenames from input file with <p>\n"
"     --no-check-prefix     Don't attempt to correct the --prefix\n"
" -j <n>                    Run <n> analyses in parallel  Default 1\n"
"     --highres=<n>         Absolute resolution cutoff in Angstroms\n"
"     --profile             Show timing data for performance monitoring\n"
"     --temp-dir=<path>     Put the temporary folder under <path>\n"
"     --wait-for-file=<n>   Time to wait for each file before processing\n"
"\nPeak search options:\n\n"
"     --peaks=<method>      Peak search method (zaef,peakfinder8,peakfinder9,hdf5,cxi)\n"
"                            Default: zaef\n"
"     --peak-radius=<r>     Integration radii for peak search\n"
"     --min-peaks=<n>       Minimum number of peaks for indexing\n"
"     --hdf5-peaks=<p>      Find peaks table in HDF5 file here\n"
"                            Default: /processing/hitfinder/peakinfo\n"
"     --median-filter=<n>   Apply a median filter to the image data\n"
"                            Default: 0 (no filter)\n"
"     --filter-noise        Apply noise filter to image data\n"
"     --threshold=<n>       Threshold for peak detection\n"
"                            (zaef,peakfinder8 only) Default: 800\n"
"     --min-squared-gradient=<n>\n"
"                           Minimum squared gradient\n"
"                            (zaef only) Default: 100,000\n"
"     --min-snr=<n>         Minimum signal/noise ratio for peaks\n"
"                            (zaef,peakfinder8, peakfinder9 only) Default: 5\n"
"     --min-pix-count=<n>   Minimum number of pixels per peak\n"
"                            (peakfinder8 only) Default: 2\n"
"     --max-pix-count=<n>   Maximum number of pixels per peak\n"
"                            (peakfinder8 only) Default: 200\n"
"     --local-bg-radius=<n> Radius (pixels) for local background estimation\n"
"                            (peakfinder8, peakfinder9 only) Default: 3\n"
"     --min-res=<n>         Minimum resolution for peak search (in pixels)\n"
"                            (peakfinder8 only) Default: 0\n"
"     --max-res=<n>         Maximum resolution for peak search (in pixels)\n"
"                            (peakfinder8 only) Default: 1200\n"
"     --min-snr-biggest-pix=<n>\n"
"                           Minimum snr of the biggest pixel in the peak\n"
"                            (peakfinder9 only)\n"
"     --min-snr-peak-pix=<n>\n"
"                           Minimum snr of a peak pixel (peakfinder9 only)\n"
"     --min-sig=<n>         Minimum standard deviation of the background\n"
"                            (peakfinder9 only)\n"
"     --min-peak-over-neighbour=<n>\n"
"                           Just for speed. Biggest pixel in peak must be n\n"
"                            higher than this (peakfinder9 only).\n"
"     --no-use-saturated    Reject saturated peaks\n"
"     --no-revalidate       Don't re-integrate and check HDF5 peaks\n"
"     --no-half-pixel-shift\n"
"                           Don't offset the HDF5 peak locations by 0.5 px\n"
"     --check-hdf5-snr      Check SNR for peaks from hdf5 or cxi (see --min-snr)\n"
"\nIndexing options:\n\n"
"     --indexing=<methods>  Indexing method list, comma separated\n"
" -p, --pdb=<file>          Unit cell file (PDB or CrystFEL unit cell format)\n"
"                             Default: 'molecule.pdb'\n"
"     --tolerance=<tol>     Tolerances for cell comparison\n"
"                              Default: 5,5,5,1.5\n"
"     --no-check-cell       Don't check lattice parameters against input cell\n"
"     --no-cell-combinations\n"
"                           Don't use axis combinations when checking cell\n"
"     --multi               Repeat indexing to index multiple hits\n"
"     --no-retry            Don't repeat indexing to increase indexing rate\n"
"     --no-refine           Skip the prediction refinement step\n"
"     --no-check-peaks      Don't check that most of the peaks can be accounted\n"
"                            for by the indexing solution\n"
"\n"
"     --taketwo-member-threshold\n"
"                           Minimum number of members in network\n"
"     --taketwo-len-tolerance\n"
"                           Reciprocal space length tolerance (1/A)\n"
"     --taketwo-angle-tolerance\n"
"                           Reciprocal space angle tolerance (in degrees)\n"
"     --taketwo-trace-tolerance\n"
"                           Rotation matrix equivalence tolerance (in degrees)\n"
"\n"
"     --felix-domega        Degree range of omega (moscaicity) to consider.\n"
"                            Default: 2\n"
"     --felix-fraction-max-visits\n"
"                           Cutoff for minimum fraction of the max visits.\n"
"                            Default: 0.75\n"
"     --felix-max-internal-angle\n"
"                           Cutoff for maximum internal angle between observed\n"
"                            spots and predicted spots. Default: 0.25\n"
"     --felix-max-uniqueness\n"
"                           Cutoff for maximum fraction of found spots which\n"
"                            can belong to other crystallites.  Default: 0.5\n"
"     --felix-min-completeness\n"
"                           Cutoff for minimum fraction of projected spots\n"
"                            found in the pattern. Default: 0.001\n"
"     --felix-min-visits\n"
"                           Cutoff for minimum number of voxel visits.\n"
"                            Default: 15\n"
"     --felix-num-voxels    Number of voxels for Rodrigues space search\n"
"                            Default: 100\n"
"     --felix-sigma         The sigma of the 2theta, eta and omega angles.\n"
"                            Default: 0.2\n"
"     --felix-tthrange-max  Maximum 2theta to consider for indexing (degrees)\n"
"                            Default: 30\n"
"     --felix-tthrange-min  Minimum 2theta to consider for indexing (degrees)\n"
"                            Default: 0\n"
"\n"
"     --xgandalf-sampling-pitch\n"
"                           Sampling pitch: 0 (loosest) to 4 (most dense)\n"
"                            or with secondary Miller indices: 5 (loosest) to\n"
"                            7 (most dense).  Default: 6\n"
"     --xgandalf-grad-desc-iterations\n"
"                           Gradient descent iterations: 0 (few) to 5 (many)\n"
"                            Default: 4\n"
"     --xgandalf-tolerance  Relative tolerance of the lattice vectors.\n"
"                            Default is 0.02\n"
"     --xgandalf-no-deviation-from-provided-cell\n"
"                           Force the fitted cell to have the same lattice\n"
"                            parameters as the provided one\n"
"     --xgandalf-min-lattice-vector-length\n"
"                           Minimum possible lattice vector length in A.\n"
"                            Default: 30 A\n"
"     --xgandalf-max-lattice-vector-length\n"
"                           Maximum possible lattice vector length in A.\n"
"                            Default: 250 A\n"
"\n"
"\nIntegration options:\n\n"
"     --integration=<meth>  Integration method (rings,prof2d)-(cen,nocen)\n"
"                            Default: rings-nocen\n"
"     --fix-profile-radius  Fix the reciprocal space profile radius for spot\n"
"                            prediction (default: automatically determine\n"
"     --fix-bandwidth       Set the bandwidth for spot prediction\n"
"     --fix-divergence      Set the divergence (full angle) for spot prediction\n"
"     --int-radius=<r>      Set the integration radii.  Default: 4,5,7.\n"
"     --int-diag=<cond>     Show debugging information about reflections\n"
"     --push-res=<n>        Integrate higher than apparent resolution cutoff\n"
"     --overpredict         Over-predict reflections (for post-refinement)\n"
"\nOutput options:\n\n"
"     --no-non-hits-in-stream\n"
"                           Do not include non-hit frames in the stream\n"
"                            (see --min-peaks)\n"
"     --copy-hdf5-field=<f> Copy the value of HDF5 field <f> into the stream\n"
"     --no-peaks-in-stream  Do not record peak search results in the stream\n"
"     --no-refls-in-stream  Do not record integrated reflections in the stream\n"
"     --serial-start        Start the serial numbers in the stream here\n"
"\nHistorical options:\n\n"
"     --no-sat-corr         Don't correct values of saturated peaks\n"
);
}


static void add_geom_beam_stuff_to_field_list(struct imagefile_field_list *copyme,
                                              struct detector *det,
                                              struct beam_params *beam)
{
	int i;

	for ( i=0; i<det->n_panels; i++ ) {

		struct panel *p = &det->panels[i];

		if ( p->clen_from != NULL ) {
			add_imagefile_field(copyme, p->clen_from);
		}
	}

	if ( beam->photon_energy_from != NULL ) {
		add_imagefile_field(copyme, beam->photon_energy_from);
	}
}


static struct spectrum *read_spectrum_fromfile(char *fn)
{
	FILE *f;
	struct spectrum *s;
	int i;
	double k, w;
	double w_sum = 0;

	f = fopen(fn, "r");
	if ( f == NULL ) {
		ERROR("Couldn't open '%s'\n", fn);
		return NULL;
	}

	s = malloc(sizeof(struct spectrum));
	if ( s == NULL ) return NULL;

	if ( fscanf(f, "%d", &s->n) == EOF ) {
		return NULL;
	}

	if ( s->n <= 0 ) {
		return NULL;
	}

	s->ks = malloc(s->n * sizeof(double));
	if ( s->ks == NULL ) {
		ERROR("Failed to allocate spectrum!\n");
		return NULL;
	}

	s->weights = malloc(s->n * sizeof(double));
	if ( s->weights == NULL ) {
		ERROR("Failed to allocate spectrum!\n");
		return NULL;
	}

	for ( i=0; i<s->n; i++ ) {
		if ( fscanf(f, "%lf %lf", &k, &w) != EOF ) {
			s->ks[i] = ph_eV_to_k(k);
			s->weights[i] = w;
			w_sum += w;
		} else {
			break;
		}
	}

	if ( i < s->n - 1 ) {
		ERROR("Failed to read %d lines from %s\n", s->n, fn);
		return NULL;
	}

	for ( i=0; i<s->n; i++ ) {
		s->weights[i] /= w_sum;
	}

	return s;
}


int main(int argc, char *argv[])
{
	int c;
	char *filename = NULL;
	char *outfile = NULL;
	FILE *fh;
	Stream *st;
	int config_checkprefix = 1;
	int config_basename = 0;
	int integrate_saturated = 0;
	char *indm_str = NULL;
	char *cellfile = NULL;
	char *prefix = NULL;
	char *speaks = NULL;
	char *toler = NULL;
	int n_proc = 1;
	struct index_args iargs;
	char *intrad = NULL;
	char *pkrad = NULL;
	char *int_str = NULL;
	char *temp_location = NULL;  /* e.g. /tmp */
	char *tmpdir;  /* e.g. /tmp/indexamajig.12345 */
	char *rn;  /* e.g. /home/taw/indexing */
	int r;
	char *int_diag = NULL;
	char *geom_filename = NULL;
	struct beam_params beam;
	int have_push_res = 0;
	char *command_line_peak_path = NULL;
	int if_refine = 1;
	int if_nocomb = 0;
	int if_nocheck = 0;
	int if_peaks = 1;
	int if_multi = 0;
	int if_retry = 1;
	int serial_start = 1;
	char *spectrum_fn = NULL;

	/* Defaults */
	iargs.cell = NULL;
	iargs.noisefilter = 0;
	iargs.median_filter = 0;
	iargs.satcorr = 1;
	iargs.tols[0] = 5.0;
	iargs.tols[1] = 5.0;
	iargs.tols[2] = 5.0;
	iargs.tols[3] = 1.5;
	iargs.threshold = 800.0;
	iargs.min_sq_gradient = 100000.0;
	iargs.min_snr = 5.0;
	iargs.min_pix_count = 2;
	iargs.max_pix_count = 200;
	iargs.min_res = 0;
	iargs.max_res = 1200;
	iargs.local_bg_radius = 3;
	iargs.min_snr_biggest_pix = 7.0;    /* peak finder 9  */
	iargs.min_snr_peak_pix = 6.0;
	iargs.min_sig = 11.0;
	iargs.min_peak_over_neighbour = -INFINITY;
	iargs.check_hdf5_snr = 0;
	iargs.det = NULL;
	iargs.peaks = PEAK_ZAEF;
	iargs.beam = &beam;
	iargs.hdf5_peak_path = NULL;
	iargs.half_pixel_shift = 1;
	iargs.copyme = NULL;
	iargs.pk_inn = -1.0;
	iargs.pk_mid = -1.0;
	iargs.pk_out = -1.0;
	iargs.ir_inn = 4.0;
	iargs.ir_mid = 5.0;
	iargs.ir_out = 7.0;
	iargs.use_saturated = 1;
	iargs.no_revalidate = 0;
	iargs.stream_peaks = 1;
	iargs.stream_refls = 1;
	iargs.stream_nonhits = 1;
	iargs.int_diag = INTDIAG_NONE;
	iargs.copyme = new_imagefile_field_list();
	iargs.min_peaks = 0;
	iargs.overpredict = 0;
	iargs.wait_for_file = 0;
	if ( iargs.copyme == NULL ) {
		ERROR("Couldn't allocate HDF5 field list.\n");
		return 1;
	}
	iargs.ipriv = NULL;  /* No default */
	iargs.int_meth = integration_method("rings-nocen-nosat-nograd", NULL);
	iargs.push_res = 0.0;
	iargs.highres = +INFINITY;
	iargs.fix_profile_r = -1.0;
	iargs.fix_bandwidth = -1.0;
	iargs.fix_divergence = -1.0;
	iargs.profile = 0;
	iargs.taketwo_opts.member_thresh = -1;
	iargs.taketwo_opts.len_tol = -1.0;
	iargs.taketwo_opts.angle_tol = -1.0;
	iargs.taketwo_opts.trace_tol = -1.0;
	iargs.xgandalf_opts.sampling_pitch = 6;
	iargs.xgandalf_opts.grad_desc_iterations = 4;
	iargs.xgandalf_opts.tolerance = 0.02;
	iargs.xgandalf_opts.no_deviation_from_provided_cell = 0;
	iargs.xgandalf_opts.minLatticeVectorLength_A = 30;
	iargs.xgandalf_opts.maxLatticeVectorLength_A = 250;
	iargs.felix_opts.ttmin = -1.0;
	iargs.felix_opts.ttmax = -1.0;
	iargs.felix_opts.min_visits = 0;
	iargs.felix_opts.min_completeness = -1.0;
	iargs.felix_opts.max_uniqueness = -1.0;
	iargs.felix_opts.n_voxels = 0;
	iargs.felix_opts.fraction_max_visits = -1.0;
	iargs.felix_opts.sigma = -1.0;
	iargs.felix_opts.domega = -1.0;
	iargs.felix_opts.max_internal_angle = -1.0;

	/* Long options */
	const struct option longopts[] = {

		/* Options with long and short versions */
		{"help",               0, NULL,               'h'},
		{"version",            0, NULL,               'v'},
		{"input",              1, NULL,               'i'},
		{"output",             1, NULL,               'o'},
		{"indexing",           1, NULL,               'z'},
		{"geometry",           1, NULL,               'g'},
		{"pdb",                1, NULL,               'p'},
		{"prefix",             1, NULL,               'x'},
		{"threshold",          1, NULL,               't'},
		{"beam",               1, NULL,               'b'},

		/* Long-only options with no arguments */
		{"filter-noise",       0, &iargs.noisefilter,        1},
		{"no-check-prefix",    0, &config_checkprefix,       0},
		{"basename",           0, &config_basename,          1},
		{"no-peaks-in-stream", 0, &iargs.stream_peaks,       0},
		{"no-refls-in-stream", 0, &iargs.stream_refls,       0},
		{"no-non-hits-in-stream", 0, &iargs.stream_nonhits,  0},
		{"integrate-saturated",0, &integrate_saturated,      1},
		{"no-use-saturated",   0, &iargs.use_saturated,      0},
		{"no-revalidate",      0, &iargs.no_revalidate,      1},
		{"check-hdf5-snr",     0, &iargs.check_hdf5_snr,     1},
		{"profile",            0, &iargs.profile,            1},
		{"no-half-pixel-shift",0, &iargs.half_pixel_shift,   0},
		{"no-refine",          0, &if_refine,                0},
		{"no-cell-combinations",0,&if_nocomb,                1},
		{"no-check-cell",      0, &if_nocheck,               1},
		{"no-cell-check",      0, &if_nocheck,               1},
		{"check-peaks",        0, &if_peaks,                 1},
		{"no-check-peaks",     0, &if_peaks,                 0},
		{"no-retry",           0, &if_retry,                 0},
		{"retry",              0, &if_retry,                 1},
		{"no-multi",           0, &if_multi,                 0},
		{"multi",              0, &if_multi,                 1},
		{"overpredict",        0, &iargs.overpredict,        1},

		/* Long-only options which don't actually do anything */
		{"no-sat-corr",        0, &iargs.satcorr,            0},
		{"sat-corr",           0, &iargs.satcorr,            1},
		{"no-check-hdf5-snr",  0, &iargs.check_hdf5_snr,     0},
		{"use-saturated",      0, &iargs.use_saturated,      1},

		/* Long-only options with arguments */
		{"peaks",              1, NULL,              302},
		{"cell-reduction",     1, NULL,              303},
		{"min-gradient",       1, NULL,              304},
		{"record",             1, NULL,              305},
		{"cpus",               1, NULL,              306},
		{"cpugroup",           1, NULL,              307},
		{"cpuoffset",          1, NULL,              308},
		{"hdf5-peaks",         1, NULL,              309},
		{"copy-hdf5-field",    1, NULL,              310},
		{"min-snr",            1, NULL,              311},
		{"tolerance",          1, NULL,              313},
		{"int-radius",         1, NULL,              314},
		{"median-filter",      1, NULL,              315},
		{"integration",        1, NULL,              316},
		{"temp-dir",           1, NULL,              317},
		{"int-diag",           1, NULL,              318},
		{"push-res",           1, NULL,              319},
		{"res-push",           1, NULL,              319}, /* compat */
		{"peak-radius",        1, NULL,              320},
		{"highres",            1, NULL,              321},
		{"fix-profile-radius", 1, NULL,              322},
		{"fix-bandwidth",      1, NULL,              323},
		{"fix-divergence",     1, NULL,              324},
		{"felix-options",      1, NULL,              325},
		{"min-pix-count",      1, NULL,              326},
		{"max-pix-count",      1, NULL,              327},
		{"local-bg-radius",    1, NULL,              328},
		{"min-res",            1, NULL,              329},
		{"max-res",            1, NULL,              330},
		{"min-peaks",          1, NULL,              331},
		{"taketwo-member-threshold", 1, NULL,        332},
		{"taketwo-member-thresh",    1, NULL,        332}, /* compat */
		{"taketwo-len-tolerance",    1, NULL,        333},
		{"taketwo-len-tol",          1, NULL,        333}, /* compat */
		{"taketwo-angle-tolerance",  1, NULL,        334},
		{"taketwo-angle-tol",        1, NULL,        334}, /* compat */
		{"taketwo-trace-tolerance",  1, NULL,        335},
		{"taketwo-trace-tol",        1, NULL,        335}, /* compat */
		{"felix-tthrange-min",       1, NULL,        336},
		{"felix-tthrange-max",       1, NULL,        337},
		{"felix-min-visits",         1, NULL,        338},
		{"felix-min-completeness",   1, NULL,        339},
		{"felix-max-uniqueness",     1, NULL,        340},
		{"felix-num-voxels",         1, NULL,        341},
		{"felix-fraction-max-visits",1, NULL,        342},
		{"felix-sigma",              1, NULL,        343},
		{"serial-start",             1, NULL,        344},
		{"felix-domega",             1, NULL,        345},
		{"felix-max-internal-angle", 1, NULL,        346},
		{"min-snr-biggest-pix",      1, NULL,        347},
		{"min-snr-peak-pix",         1, NULL,        348},
		{"min-sig",                  1, NULL,        349},
		{"min-peak-over-neighbour",  1, NULL,        350},
		{"xgandalf-sampling-pitch",                  1, NULL, 351},
		{"xgandalf-sps",                             1, NULL, 351},
		{"xgandalf-grad-desc-iterations",            1, NULL, 352},
		{"xgandalf-gdis",                            1, NULL, 352},
		{"xgandalf-tolerance",                       1, NULL, 353},
		{"xgandalf-tol",                             1, NULL, 353},
		{"xgandalf-no-deviation-from-provided-cell", 0, NULL, 354},
		{"xgandalf-ndfpc",                           0, NULL, 354},
		{"xgandalf-min-lattice-vector-length",       1, NULL, 355},
		{"xgandalf-min-lvl",                         1, NULL, 355},
		{"xgandalf-max-lattice-vector-length",       1, NULL, 356},
		{"xgandalf-max-lvl",                         1, NULL, 356},
	        {"spectrum-file",                            1, NULL, 357},
		{"wait-for-file",            1, NULL,        358},
		{"min-squared-gradient",1,NULL,              359},
		{"min-sq-gradient",    1, NULL,              359}, /* compat */

		{0, 0, NULL, 0}
	};

	/* Short options */
	while ((c = getopt_long(argc, argv, "hi:o:z:p:x:j:g:t:vb:",
	                        longopts, NULL)) != -1)
	{
		switch (c) {

			case 'h' :
			show_help(argv[0]);
			return 0;

			case 'v' :
			printf("CrystFEL: " CRYSTFEL_VERSIONSTRING "\n");
			printf(CRYSTFEL_BOILERPLATE"\n");
			return 0;

			case 'b' :
			ERROR("WARNING: This version of CrystFEL no longer "
			      "uses beam files.  Please remove the beam file "
			      "from your indexamajig command line.\n");
			return 1;

			case 'i' :
			filename = strdup(optarg);
			break;

			case 'o' :
			outfile = strdup(optarg);
			break;

			case 'z' :
			indm_str = strdup(optarg);
			break;

			case 'p' :
			cellfile = strdup(optarg);
			break;

			case 'x' :
			prefix = strdup(optarg);
			break;

			case 'j' :
			n_proc = atoi(optarg);
			break;

			case 'g' :
			geom_filename = optarg;
			break;

			case 't' :
			iargs.threshold = strtof(optarg, NULL);
			break;

			case 302 :
			speaks = strdup(optarg);
			break;

			case 303 :
			ERROR("The option '--cell-reduction' is no longer "
			      "used.\n"
			      "The complete indexing behaviour is now "
			      "controlled using '--indexing'.\n"
			      "See 'man indexamajig' for details of the "
			      "available methods.\n");
			return 1;

			case 304 :
			iargs.min_sq_gradient = strtof(optarg, NULL);
			ERROR("Recommend using --min-squared-gradient instead "
			      "of --min-gradient.\n");
			break;

			case 305 :
			ERROR("The option '--record' is no longer used.\n"
			      "Use '--no-peaks-in-stream' and"
			      "'--no-refls-in-stream' if you need to control"
			      "the contents of the stream.\n");
			return 1;

			case 306 :
			case 307 :
			case 308 :
			ERROR("The options --cpus, --cpugroup and --cpuoffset"
			      " are no longer used by indexamajig.\n");
			break;

			case 309 :
			free(command_line_peak_path);
			command_line_peak_path = strdup(optarg);
			break;

			case 310 :
			add_imagefile_field(iargs.copyme, optarg);
			break;

			case 311 :
			iargs.min_snr = strtof(optarg, NULL);
			break;

			case 313 :
			toler = strdup(optarg);
			break;

			case 314 :
			intrad = strdup(optarg);
			break;

			case 315 :
			iargs.median_filter = atoi(optarg);
			break;

			case 316 :
			int_str = strdup(optarg);
			break;

			case 317 :
			temp_location = strdup(optarg);
			break;

			case 318 :
			int_diag = strdup(optarg);
			break;

			case 319 :
			if ( sscanf(optarg, "%f", &iargs.push_res) != 1 ) {
				ERROR("Invalid value for --push-res\n");
				return 1;
			}
			iargs.push_res *= 1e9;  /* nm^-1 -> m^-1 */
			have_push_res = 1;
			break;

			case 320 :
			pkrad = strdup(optarg);
			break;

			case 321 :
			if ( sscanf(optarg, "%f", &iargs.highres) != 1 ) {
				ERROR("Invalid value for --highres\n");
				return 1;
			}
			/* A -> m^-1 */
			iargs.highres = 1.0 / (iargs.highres/1e10);
			break;

			case 322 :
			if ( sscanf(optarg, "%f", &iargs.fix_profile_r) != 1 ) {
				ERROR("Invalid value for "
				      "--fix-profile-radius\n");
				return 1;
			}
			break;

			case 323 :
			if ( sscanf(optarg, "%f", &iargs.fix_bandwidth) != 1 ) {
				ERROR("Invalid value for --fix-bandwidth\n");
				return 1;
			}
			break;

			case 324 :
			if ( sscanf(optarg, "%f", &iargs.fix_divergence) != 1 ) {
				ERROR("Invalid value for --fix-divergence\n");
				return 1;
			}
			break;

			case 325 :
			ERROR("--felix-options is no longer used.\n");
			ERROR("See --help for the new Felix options.\n");
			return 1;

			case 326:
			iargs.min_pix_count = atoi(optarg);
			break;

			case 327:
			iargs.max_pix_count = atoi(optarg);
			break;

			case 328:
			iargs.local_bg_radius = atoi(optarg);
			break;

			case 329:
			iargs.min_res = atoi(optarg);
			break;

			case 330:
			iargs.max_res = atoi(optarg);
			break;

			case 331:
			iargs.min_peaks = atoi(optarg);
			break;

			case 332:
			if ( sscanf(optarg, "%i", &iargs.taketwo_opts.member_thresh) != 1 )
			{
				ERROR("Invalid value for --taketwo-member-threshold\n");
				return 1;
			}
			break;

			case 333:
			if ( sscanf(optarg, "%lf", &iargs.taketwo_opts.len_tol) != 1 )
			{
				ERROR("Invalid value for --taketwo-len-tolerance\n");
				return 1;
			}
			/* Convert to m^-1 */
			iargs.taketwo_opts.len_tol *= 1e10;
			break;

			case 334:
			if ( sscanf(optarg, "%lf", &iargs.taketwo_opts.angle_tol) != 1 )
			{
				ERROR("Invalid value for --taketwo-angle-tolerance\n");
				return 1;
			}
			/* Convert to radians */
			iargs.taketwo_opts.angle_tol = deg2rad(iargs.taketwo_opts.angle_tol);
			break;

			case 335:
			if ( sscanf(optarg, "%lf", &iargs.taketwo_opts.trace_tol) != 1 )
			{
				ERROR("Invalid value for --taketwo-trace-tolerance\n");
				return 1;
			}
			/* Convert to radians */
			iargs.taketwo_opts.trace_tol = deg2rad(iargs.taketwo_opts.trace_tol);
			break;

			case 336:
			if ( sscanf(optarg, "%lf", &iargs.felix_opts.ttmin) != 1 )
			{
				ERROR("Invalid value for --felix-tthrange-min\n");
				return 1;
			}
			iargs.felix_opts.ttmin = deg2rad(iargs.felix_opts.ttmin);
			break;

			case 337:
			if ( sscanf(optarg, "%lf", &iargs.felix_opts.ttmax) != 1 )
			{
				ERROR("Invalid value for --felix-tthrange-max\n");
				return 1;
			}
			iargs.felix_opts.ttmax = deg2rad(iargs.felix_opts.ttmax);
			break;

			case 338:
			if ( sscanf(optarg, "%i", &iargs.felix_opts.min_visits) != 1 )
			{
				ERROR("Invalid value for --felix-min-visits\n");
				return 1;
			}
			break;

			case 339:
			if ( sscanf(optarg, "%lf", &iargs.felix_opts.min_completeness) != 1 )
			{
				ERROR("Invalid value for --felix-min-completeness\n");
				return 1;
			}
			break;

			case 340:
			if ( sscanf(optarg, "%lf", &iargs.felix_opts.max_uniqueness) != 1 )
			{
				ERROR("Invalid value for --felix-max-uniqueness\n");
				return 1;
			}
			break;

			case 341:
			if ( sscanf(optarg, "%i", &iargs.felix_opts.n_voxels) != 1 )
			{
				ERROR("Invalid value for --felix-num-voxels\n");
				return 1;
			}
			break;

			case 342:
			if ( sscanf(optarg, "%lf", &iargs.felix_opts.fraction_max_visits) != 1 )
			{
				ERROR("Invalid value for --felix-fraction-max-visits\n");
				return 1;
			}
			break;

			case 343:
			if ( sscanf(optarg, "%lf", &iargs.felix_opts.sigma) != 1 )
			{
				ERROR("Invalid value for --felix-sigma\n");
				return 1;
			}
			break;

			case 344:
			if ( sscanf(optarg, "%i", &serial_start) != 1 )
			{
				ERROR("Invalid value for --serial-start\n");
				return 1;
			}
			break;

			case 345:
			if ( sscanf(optarg, "%lf", &iargs.felix_opts.domega) != 1 )
			{
				ERROR("Invalid value for --felix-domega\n");
				return 1;
			}
			break;

			case 346:
			if ( sscanf(optarg, "%lf", &iargs.felix_opts.max_internal_angle) != 1 )
			{
				ERROR("Invalid value for --felix-max-internal-angle\n");
				return 1;
			}
			break;

			case 347:
			iargs.min_snr_biggest_pix = strtof(optarg, NULL);
			break;

			case 348:
			iargs.min_snr_peak_pix = strtof(optarg, NULL);
			break;

			case 349:
			iargs.min_sig = strtof(optarg, NULL);
			break;

			case 350:
			iargs.min_peak_over_neighbour = strtof(optarg, NULL);
			break;

			case 351:
			if (sscanf(optarg, "%u", &iargs.xgandalf_opts.sampling_pitch) != 1)
			{
				ERROR("Invalid value for --xgandalf-sampling-pitch\n");
				return 1;
			}
			break;

			case 352:
			if (sscanf(optarg, "%u", &iargs.xgandalf_opts.grad_desc_iterations) != 1)
			{
				ERROR("Invalid value for --xgandalf-grad-desc-iterations\n");
				return 1;
			}
			break;

			case 353:
			if (sscanf(optarg, "%f", &iargs.xgandalf_opts.tolerance) != 1)
			{
				ERROR("Invalid value for --xgandalf-tolerance\n");
				return 1;
			}
			break;

			case 354:
				iargs.xgandalf_opts.no_deviation_from_provided_cell = 1;
			break;

			case 355:
			if (sscanf(optarg, "%f",
					&iargs.xgandalf_opts.minLatticeVectorLength_A) != 1)
			{
				ERROR("Invalid value for "
						"--xgandalf-min-lattice-vector-length\n");
				return 1;
			}
			break;

			case 356:
			if (sscanf(optarg, "%f",
					&iargs.xgandalf_opts.maxLatticeVectorLength_A) != 1)
			{
				ERROR("Invalid value for "
						"--xgandalf-max-lattice-vector-length\n");
				return 1;
			}
			break;

			case 357:
			spectrum_fn = strdup(optarg);
			break;

			case 358:
			if (sscanf(optarg, "%d", &iargs.wait_for_file) != 1)
			{
				ERROR("Invalid value for --wait-for-file\n");
				return 1;
			}
			break;

			case 359 :
			iargs.min_sq_gradient = strtof(optarg, NULL);
			break;

			case 0 :
			break;

			case '?' :
			break;

			default :
			ERROR("Unhandled option '%c'\n", c);
			break;

		}

	}

	/* Check for minimal information */
	if ( filename == NULL ) {
		ERROR("You need to provide the input filename (use -i)\n");
		return 1;
	}
	if ( geom_filename == NULL ) {
		ERROR("You need to specify the geometry filename (use -g)\n");
		return 1;
	}
	if ( outfile == NULL ) {
		ERROR("You need to specify the output filename (use -o)\n");
		return 1;
	}

	if ( temp_location == NULL ) {
		temp_location = strdup(".");
	}

	/* Open input */
	if ( strcmp(filename, "-") == 0 ) {
		fh = stdin;
	} else {
		fh = fopen(filename, "r");
	}
	if ( fh == NULL ) {
		ERROR("Failed to open input file '%s'\n", filename);
		return 1;
	}
	free(filename);

	/* Parse peak detection method */
	if ( speaks == NULL ) {
		speaks = strdup("zaef");
		STATUS("You didn't specify a peak detection method.\n");
		STATUS("I'm using 'zaef' for you.\n");
	}
	if ( strcmp(speaks, "zaef") == 0 ) {
		iargs.peaks = PEAK_ZAEF;
	} else if ( strcmp(speaks, "peakfinder8") == 0 ) {
		iargs.peaks = PEAK_PEAKFINDER8;
	} else if ( strcmp(speaks, "hdf5") == 0 ) {
		iargs.peaks = PEAK_HDF5;
	} else if ( strcmp(speaks, "cxi") == 0 ) {
		iargs.peaks = PEAK_CXI;
	} else if ( strcmp(speaks, "peakfinder9") == 0 ) {
		iargs.peaks = PEAK_PEAKFINDER9;
	} else {
		ERROR("Unrecognised peak detection method '%s'\n", speaks);
		return 1;
	}
	free(speaks);

	/* Check prefix (if given) */
	if ( prefix == NULL ) {
		prefix = strdup("");
	} else {
		if ( config_checkprefix ) {
			prefix = check_prefix(prefix);
		}
	}

	/* Check number of processes */
	if ( n_proc == 0 ) {
		ERROR("Invalid number of processes.\n");
		return 1;
	}

	/* Load detector geometry */
	iargs.det = get_detector_geometry_2(geom_filename, iargs.beam,
	                                    &iargs.hdf5_peak_path);
	if ( iargs.det == NULL ) {
		ERROR("Failed to read detector geometry from  '%s'\n",
		      geom_filename);
		return 1;
	}
	add_geom_beam_stuff_to_field_list(iargs.copyme, iargs.det, iargs.beam);

	/* If no peak path from geometry file, use these (but see later) */
	if ( iargs.hdf5_peak_path == NULL ) {
		if ( iargs.peaks == PEAK_HDF5 ) {
			iargs.hdf5_peak_path = strdup("/processing/hitfinder/peakinfo");
		} else if ( iargs.peaks == PEAK_CXI ) {
			iargs.hdf5_peak_path = strdup("/entry_1/result_1");
		}
	}

	/* If an HDF5 peak path was given on the command line, use it */
	if ( command_line_peak_path != NULL ) {
		free(iargs.hdf5_peak_path);
		iargs.hdf5_peak_path = command_line_peak_path;
	}

	/* Parse integration method */
	if ( int_str != NULL ) {

		int err;

		iargs.int_meth = integration_method(int_str, &err);
		if ( err ) {
			ERROR("Invalid integration method '%s'\n", int_str);
			return 1;
		}
		free(int_str);
	}
	if ( integrate_saturated ) {
		/* Option provided for backwards compatibility */
		iargs.int_meth |= INTEGRATION_SATURATED;
	}
	if ( have_push_res && !(iargs.int_meth & INTEGRATION_RESCUT) ) {
		ERROR("WARNING: You used --push-res, but not -rescut, "
		      "therefore --push-res will have no effect.\n");
		ERROR("WARNING: Add --integration=rings-rescut or "
		      "--integration=prof2d-rescut.\n");
	}

	/* Parse unit cell tolerance */
	if ( toler != NULL ) {
		int ttt;
		ttt = sscanf(toler, "%f,%f,%f,%f",
		             &iargs.tols[0], &iargs.tols[1],
		             &iargs.tols[2], &iargs.tols[3]);
		if ( ttt != 4 ) {
			ERROR("Invalid parameters for '--tolerance'\n");
			return 1;
		}
		free(toler);
	}

	/* Parse integration radii */
	if ( intrad != NULL ) {
		int r;
		r = sscanf(intrad, "%f,%f,%f",
		           &iargs.ir_inn, &iargs.ir_mid, &iargs.ir_out);
		if ( r != 3 ) {
			ERROR("Invalid parameters for '--int-radius'\n");
			return 1;
		}
		free(intrad);
	} else {
		STATUS("WARNING: You did not specify --int-radius.\n");
		STATUS("WARNING: I will use the default values, which are"
		       " probably not appropriate for your patterns.\n");
	}

	/* Parse peak radii (used for peak detection) */
	if ( pkrad != NULL ) {
		int r;
		r = sscanf(pkrad, "%f,%f,%f",
		           &iargs.pk_inn, &iargs.pk_mid, &iargs.pk_out);
		if ( r != 3 ) {
			ERROR("Invalid parameters for '--peak-radius'\n");
			return 1;
		}
		free(pkrad);
	}
	if ( iargs.pk_inn < 0.0 ) {
		iargs.pk_inn = iargs.ir_inn;
		iargs.pk_mid = iargs.ir_mid;
		iargs.pk_out = iargs.ir_out;
	}

	/* Load unit cell (if given) */
	if ( cellfile != NULL ) {
		iargs.cell = load_cell_from_file(cellfile);
		if ( iargs.cell == NULL ) {
			ERROR("Couldn't read unit cell (from %s)\n", cellfile);
			return 1;
		}
		free(cellfile);
	} else {
		iargs.cell = NULL;
	}

	/* Load spectrum from file if given */
	if ( spectrum_fn != NULL ) {
		iargs.spectrum = read_spectrum_fromfile(spectrum_fn);
		if ( iargs.spectrum == NULL ) {
			ERROR("Couldn't read spectrum (from %s)\n", spectrum_fn);
			return 1;
		}
		free(spectrum_fn);
		STATUS("Read %d lines from %s\n", iargs.spectrum->n, spectrum_fn);
	} else {
		iargs.spectrum = NULL;
	}

	/* Parse integration diagnostic */
	if ( int_diag != NULL ) {

		int r;
		signed int h, k, l;

		if ( strcmp(int_diag, "random") == 0 ) {
			iargs.int_diag = INTDIAG_RANDOM;
		}

		if ( strcmp(int_diag, "all") == 0 ) {
			iargs.int_diag = INTDIAG_ALL;
		}

		if ( strcmp(int_diag, "negative") == 0 ) {
			iargs.int_diag = INTDIAG_NEGATIVE;
		}

		if ( strcmp(int_diag, "implausible") == 0 ) {
			iargs.int_diag = INTDIAG_IMPLAUSIBLE;
		}

		if ( strcmp(int_diag, "strong") == 0 ) {
			iargs.int_diag = INTDIAG_STRONG;
		}

		r = sscanf(int_diag, "%i,%i,%i", &h, &k, &l);
		if ( r == 3 ) {
			iargs.int_diag = INTDIAG_INDICES;
			iargs.int_diag_h = h;
			iargs.int_diag_k = k;
			iargs.int_diag_l = l;
		}

		if ( (iargs.int_diag == INTDIAG_NONE)
		  && (strcmp(int_diag, "none") != 0) ) {
			ERROR("Invalid value for --int-diag.\n");
			return 1;
		}

		free(int_diag);

	}

	tmpdir = create_tempdir(temp_location);
	if ( tmpdir == NULL ) return 1;

	/* Change into temporary folder, temporarily, to control the crap
	 * dropped by indexing programs during setup */
	rn = getcwd(NULL, 0);
	r = chdir(tmpdir);
	if ( r ) {
		ERROR("Failed to chdir to temporary folder: %s\n",
		      strerror(errno));
		return 1;
	}

	if ( indm_str == NULL ) {

		STATUS("No indexing methods specified.  I will try to ");
		STATUS("automatically detect the available methods.\n");
		STATUS("To disable auto-detection of indexing methods, specify ");
		STATUS("which methods to use with --indexing=<methods>.\n");
		STATUS("Use --indexing=none to disable indexing and integration.\n");

		indm_str = detect_indexing_methods(iargs.cell);

	}

	/* Prepare the indexing system */
	if ( indm_str == NULL ) {

		ERROR("No indexing method specified, and no usable indexing ");
		ERROR("methods auto-detected.\n");
		ERROR("Install some indexing programs (mosflm,dirax etc), or ");
		ERROR("try again with --indexing=none.\n");
		return 1;

	} else if ( strcmp(indm_str, "none") == 0 ) {

		STATUS("Indexing/integration disabled.\n");
		if ( iargs.cell != NULL ) {
			STATUS("Ignoring your unit cell.\n");
		}
		iargs.ipriv = NULL;

	} else {

		IndexingFlags flags = 0;

		if ( iargs.cell != NULL ) {
			STATUS("This is what I understood your unit cell to be:\n");
			cell_print(iargs.cell);
		} else {
			STATUS("No reference unit cell provided.\n");
		}

		if ( if_nocomb ) {
			flags |= INDEXING_CHECK_CELL_AXES;
		} else {
			flags |= INDEXING_CHECK_CELL_COMBINATIONS;
		}

		if ( if_nocheck ) {
			flags &= ~INDEXING_CHECK_CELL_AXES;
			flags &= ~INDEXING_CHECK_CELL_COMBINATIONS;
		}

		if ( if_refine ) {
			flags |= INDEXING_REFINE;
		}
		if ( if_peaks ) {
			flags |= INDEXING_CHECK_PEAKS;
		}
		if ( if_multi ) {
			flags |= INDEXING_MULTI;
		}
		if ( if_retry ) {
			flags |= INDEXING_RETRY;
		}

		iargs.ipriv = setup_indexing(indm_str, iargs.cell, iargs.det,
		                             iargs.tols, flags,
		                             &iargs.taketwo_opts,
		                             &iargs.xgandalf_opts,
		                             &iargs.felix_opts);
		if ( iargs.ipriv == NULL ) {
			ERROR("Failed to set up indexing system\n");
			return 1;
		}

	}

	/* Change back to where we were before.  Sandbox code will create
	 * worker subdirs inside the temporary folder, and process_image will
	 * change into them. */
	r = chdir(rn);
	if ( r ) {
		ERROR("Failed to chdir: %s\n", strerror(errno));
		return 1;
	}
	free(rn);

	/* Open output stream */
	st = open_stream_for_write_4(outfile, geom_filename, iargs.cell,
	                             argc, argv, indm_str);
	if ( st == NULL ) {
		ERROR("Failed to open stream '%s'\n", outfile);
		return 1;
	}
	free(outfile);
	free(indm_str);

	gsl_set_error_handler_off();

	r = create_sandbox(&iargs, n_proc, prefix, config_basename, fh,
	                   st, tmpdir, serial_start);

	free_imagefile_field_list(iargs.copyme);
	cell_free(iargs.cell);
	free(iargs.beam->photon_energy_from);
	free(prefix);
	free(temp_location);
	free(tmpdir);
	free_detector_geometry(iargs.det);
	free(iargs.hdf5_peak_path);
	close_stream(st);
	cleanup_indexing(iargs.ipriv);

	if ( r ) {
		return 0;
	} else {
		return 1;  /* No patterns processed */
	}
}
