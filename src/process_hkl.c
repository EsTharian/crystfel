/*
 * process_hkl.c
 *
 * Assemble and process FEL Bragg intensities
 *
 * Copyright © 2012 Thomas White <taw@physics.org>
 * Copyright © 2012 Andrew Martin <andrew.martin@desy.de>
 * Copyright © 2012 Lorenzo Galli <lorenzo.galli@desy.de>
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

#include "utils.h"
#include "statistics.h"
#include "reflist-utils.h"
#include "symmetry.h"
#include "stream.h"
#include "reflist.h"
#include "image.h"


static void show_help(const char *s)
{
	printf("Syntax: %s [options]\n\n", s);
	printf(
"Assemble and process FEL Bragg intensities.\n"
"\n"
"  -h, --help                Display this help message.\n"
"  -i, --input=<filename>    Specify input filename (\"-\" for stdin).\n"
"  -o, --output=<filename>   Specify output filename for merged intensities\n"
"                             Default: processed.hkl).\n"
"\n"
"      --max-only            Take the integrated intensity to be equal to the\n"
"                             maximum intensity measured for that reflection.\n"
"                             The default is to use the mean value from all\n"
"                             measurements.\n"
"      --sum                 Sum (rather than average) the intensities for the\n"
"                             final output list.  This is useful for comparing\n"
"                             results to radially summed powder patterns, but\n"
"                             will break R-factor analysis.\n"
"      --start-after=<n>     Skip n patterns at the start of the stream.\n"
"      --stop-after=<n>      Stop after processing n patterns.  Zero means\n"
"                             keep going until the end of the input, and is\n"
"                             the default.\n"
"  -g, --histogram=<h,k,l>   Calculate the histogram of measurements for this\n"
"                             reflection.\n"
"  -z, --hist-parameters     Set the range for the histogram and the number of\n"
"          =<min,max,nbins>   bins. \n"
"\n"
"      --scale               Scale each pattern for best fit with the current\n"
"                             model.\n"
"  -y, --symmetry=<sym>      Merge according to point group <sym>.\n"
"      --reference=<file>    Compare against intensities from <file> when\n"
"                             scaling or resolving ambiguities.\n"
"                             The symmetry of the reference list must be the\n"
"                             same as that given with '-y'.\n"
);
}


static void plot_histogram(double *vals, int n, float hist_min, float hist_max, int nbins)
{
	int i;
	double max = -INFINITY;
	double min = +INFINITY;
	double step;
	int histo[nbins];
	FILE *fh;

	fh = fopen("histogram.dat", "w");
	if ( fh == NULL ) {
		ERROR("Couldn't open 'histogram.dat'\n");
		return;
	}

	if ( hist_min == hist_max ) {
		for ( i=0; i<n; i++ ) {
			if ( vals[i] > max ) max = vals[i];
			if ( vals[i] < min ) min = vals[i];
		}
	} else {
		min = hist_min;
		max = hist_max;
	}
	STATUS("min max nbins: %f %f %i\n", min, max, nbins);
	min--;  max++;

	for ( i=0; i<nbins; i++ ) {
		histo[i] = 0;
	}

	step = (max-min)/nbins;

	for ( i=0; i<n; i++ ) {
		int bin;
		if ( (vals[i] > min) && (vals[i] < max) ) {
			bin = (vals[i]-min)/step;
			histo[bin]++;
		}
	}

	for ( i=0; i<nbins; i++ ) {
		fprintf(fh, "%f %i\n", min+step*i, histo[i]);
	}

	fclose(fh);
}


static void merge_pattern(RefList *model, RefList *new, int max_only,
                          const SymOpList *sym,
                          double *hist_vals, signed int hist_h,
                          signed int hist_k, signed int hist_l, int *hist_n,
                          int pass)
{
	Reflection *refl;
	RefListIterator *iter;

	for ( refl = first_refl(new, &iter);
	      refl != NULL;
	      refl = next_refl(refl, iter) ) {

		double intensity;
		signed int h, k, l;
		Reflection *model_version;
		double model_int;

		get_indices(refl, &h, &k, &l);

		/* Put into the asymmetric unit for the target group */
		get_asymm(sym, h, k, l, &h, &k, &l);

		model_version = find_refl(model, h, k, l);
		if ( model_version == NULL ) {
			model_version = add_refl(model, h, k, l);
		}

		/* Read the intensity from the original location
		 * (i.e. before screwing around with symmetry) */
		intensity = get_intensity(refl);

		/* Get the current model intensity */
		model_int = get_intensity(model_version);

		if ( pass == 1 ) {

			/* User asked for max only? */
			if ( !max_only ) {
				set_int(model_version, model_int + intensity);
			} else {
				if ( intensity>get_intensity(model_version) ) {
					set_int(model_version, intensity);
				}
			}

			/* Increase redundancy */
			int cur_redundancy = get_redundancy(model_version);
			set_redundancy(model_version, cur_redundancy+1);


		} else if ( pass == 2 ) {

			double dev = get_temp1(model_version);

			/* Other ways of estimating the sigma are possible,
			 * choose from:
			 *    dev += pow(get_esd_intensity(refl), 2.0);
			 *    dev += pow(intensity, 2.0);
			 * But alter the other part of the calculation below
			 * as well. */
			dev += pow(intensity - model_int, 2.0);

			set_temp1(model_version, dev);

			if ( hist_vals != NULL ) {
				int p = *hist_n;
				if ( (h==hist_h) && (k==hist_k)
				  && (l==hist_l) )
				{
					hist_vals[p] = intensity;
					*hist_n = p+1;
				}

			}

		}

	}


}


enum {
	SCALE_NONE,
	SCALE_CONSTINT,
	SCALE_INTPERBRAGG,
	SCALE_TWOPASS,
};


static void scale_intensities(RefList *model, RefList *new, const SymOpList *sym)
{
	double s;
	double top = 0.0;
	double bot = 0.0;
	const int scaling = SCALE_INTPERBRAGG;
	Reflection *refl;
	RefListIterator *iter;
	Reflection *model_version;

	for ( refl = first_refl(new, &iter);
	      refl != NULL;
	      refl = next_refl(refl, iter) ) {

		double i1, i2;
		signed int hu, ku, lu;
		signed int h, k, l;

		get_indices(refl, &h, &k, &l);

		switch ( scaling ) {
		case SCALE_TWOPASS :

			model_version = find_refl(model, h, k, l);
			if ( model_version == NULL ) continue;

			get_asymm(sym, h, k, l, &hu, &ku, &lu);

			i1 = get_intensity(model_version);
			i2 = get_intensity(refl);

			/* Calculate LSQ estimate of scaling factor */
			top += i1 * i2;
			bot += i2 * i2;

			break;

		case SCALE_CONSTINT :

			/* Sum up the intensity in the pattern */
			i2 = get_intensity(refl);
			top += i2;

			break;

		case SCALE_INTPERBRAGG :

			/* Sum up the intensity in the pattern */
			i2 = get_intensity(refl);
			top += i2;
			bot += 1.0;

			break;

		}

	}

	switch ( scaling ) {
	case SCALE_TWOPASS :
		s = top / bot;
		break;
	case SCALE_CONSTINT :
		s = 1000.0 / top;
		break;
	case SCALE_INTPERBRAGG :
		s = 1000.0 / (top/bot);
		break;
	}

	/* Multiply the new pattern up by "s" */
	for ( refl = first_refl(new, &iter);
	      refl != NULL;
	      refl = next_refl(refl, iter) ) {

		double intensity = get_intensity(refl);
		set_int(refl, intensity*s);

	}
}


static void merge_all(FILE *fh, RefList *model,
                      int config_maxonly, int config_scale, int config_sum,
                      int config_startafter, int config_stopafter,
                      const SymOpList *sym,
                      int n_total_patterns,
                      double *hist_vals, signed int hist_h,
                      signed int hist_k, signed int hist_l,
                      int *hist_i, int pass)
{
	int rval;
	int n_patterns = 0;
	int n_used = 0;
	Reflection *refl;
	RefListIterator *iter;

	if ( skip_some_files(fh, config_startafter) ) {
		ERROR("Failed to skip first %i files.\n", config_startafter);
		return;
	}

	do {

		struct image image;

		image.det = NULL;

		/* Get data from next chunk */
		rval = read_chunk(fh, &image);
		if ( rval ) break;

		n_patterns++;

		if ( (image.reflections != NULL) && (image.indexed_cell) ) {

			/* Scale if requested */
			if ( config_scale ) {
				scale_intensities(model, image.reflections,
				                  sym);
			}

			merge_pattern(model, image.reflections, config_maxonly,
			              sym, hist_vals, hist_h, hist_k, hist_l,
			              hist_i, pass);

			n_used++;

		}

		free(image.filename);
		reflist_free(image.reflections);
		image_feature_list_free(image.features);
		cell_free(image.indexed_cell);

		progress_bar(n_patterns, n_total_patterns-config_startafter,
		             "Merging");

	} while ( rval == 0 );

	/* Divide by counts to get mean intensity if necessary */
	if ( (pass == 1) && !config_sum && !config_maxonly ) {

		Reflection *refl;
		RefListIterator *iter;

		for ( refl = first_refl(model, &iter);
		      refl != NULL;
		      refl = next_refl(refl, iter) ) {

			double intensity = get_intensity(refl);
			int red = get_redundancy(refl);

			set_int(refl, intensity / (double)red);

		}

	}

	/* Calculate ESDs */
	if ( pass == 2 ) {
		for ( refl = first_refl(model, &iter);
		      refl != NULL;
		      refl = next_refl(refl, iter) ) {

			double sum_squared_dev = get_temp1(refl);
			int red = get_redundancy(refl);
			int h, k, l;
			double esd;
			get_indices(refl,&h,&k,&l);

			/* Other ways of estimating the sigma are possible,
			 * such as:
			 *
			 *    double intensity = get_intensity(refl);
			 *    esd = sqrt( (sum_squared_dev/(double)red)
			 *              - pow(intensity,2.0) ) );
			 *
			 * But alter the other part of the calculation above
			 * as well. */
			esd = sqrt(sum_squared_dev)/(double)red;

			set_esd_intensity(refl, esd);

		}
	}

	if ( pass == 1 ) {
		STATUS("%i of the patterns could be used.\n", n_used);
	}
}


int main(int argc, char *argv[])
{
	int c;
	char *filename = NULL;
	char *output = NULL;
	FILE *fh;
	RefList *model;
	int config_maxonly = 0;
	int config_startafter = 0;
	int config_stopafter = 0;
	int config_sum = 0;
	int config_scale = 0;
	unsigned int n_total_patterns;
	char *sym_str = NULL;
	SymOpList *sym;
	char *pdb = NULL;
	char *histo = NULL;
	signed int hist_h, hist_k, hist_l;
	signed int hist_nbins=50;
	float hist_min=0.0, hist_max=0.0;
	double *hist_vals = NULL;
	int hist_i;
	int space_for_hist = 0;
	char *histo_params = NULL;

	/* Long options */
	const struct option longopts[] = {
		{"help",               0, NULL,               'h'},
		{"input",              1, NULL,               'i'},
		{"output",             1, NULL,               'o'},
		{"max-only",           0, &config_maxonly,     1},
		{"output-every",       1, NULL,               'e'},
		{"stop-after",         1, NULL,               's'},
		{"start-after",        1, NULL,               'f'},
		{"sum",                0, &config_sum,         1},
		{"scale",              0, &config_scale,       1},
		{"symmetry",           1, NULL,               'y'},
		{"histogram",          1, NULL,               'g'},
		{"hist-parameters",    1, NULL,               'z'},
		{0, 0, NULL, 0}
	};

	/* Short options */
	while ((c = getopt_long(argc, argv, "hi:e:o:p:y:g:f:b:z:",
	                        longopts, NULL)) != -1) {

		switch (c) {
		case 'h' :
			show_help(argv[0]);
			return 0;

		case 'i' :
			filename = strdup(optarg);
			break;

		case 'o' :
			output = strdup(optarg);
			break;

		case 's' :
			config_stopafter = atoi(optarg);
			break;

		case 'f' :
			config_startafter = atoi(optarg);
			break;

		case 'p' :
			pdb = strdup(optarg);
			break;

		case 'y' :
			sym_str = strdup(optarg);
			break;

		case 'g' :
			histo = strdup(optarg);
			break;

		case 'z' :
			histo_params = strdup(optarg);
			break;

		case 0 :
			break;

		default :
			return 1;
		}

	}

	if ( filename == NULL ) {
		ERROR("Please specify filename using the -i option\n");
		return 1;
	}

	if ( output == NULL ) {
		output = strdup("processed.hkl");
	}

	if ( sym_str == NULL ) sym_str = strdup("1");
	sym = get_pointgroup(sym_str);
	free(sym_str);

	/* Open the data stream */
	if ( strcmp(filename, "-") == 0 ) {
		fh = stdin;
	} else {
		fh = fopen(filename, "r");
	}
	free(filename);
	if ( fh == NULL ) {
		ERROR("Failed to open input file\n");
		return 1;
	}

	/* Count the number of patterns in the file */
	n_total_patterns = count_patterns(fh);
	if ( n_total_patterns == 0 ) {
		ERROR("No patterns to process.\n");
		return 1;
	}
	STATUS("There are %i patterns to process\n", n_total_patterns);
	rewind(fh);

	model = reflist_new();

	if ( histo != NULL ) {

		int r;

		r = sscanf(histo, "%i,%i,%i", &hist_h, &hist_k, &hist_l);
		if ( r != 3 ) {
			ERROR("Invalid indices for '--histogram'\n");
			return 1;
		}

		space_for_hist = n_total_patterns * num_equivs(sym, NULL);
		hist_vals = malloc(space_for_hist * sizeof(double));
		free(histo);
		STATUS("Histogramming %i %i %i -> ", hist_h, hist_k, hist_l);

		/* Put into the asymmetric cell for the target group */
		get_asymm(sym, hist_h, hist_k, hist_l,
		          &hist_h, &hist_k, &hist_l);
		STATUS("%i %i %i\n", hist_h, hist_k, hist_l);

	}

	if ( histo_params != NULL ) {

		int rr;

		rr = sscanf(histo_params, "%f,%f,%i", &hist_min, &hist_max,
		                                      &hist_nbins);
		if ( rr != 3 ) {
			ERROR("Invalid parameters for '--hist-parameters'\n");
			return 1;
		}
		free(histo_params);
		if ( hist_max <= hist_min ) {
			ERROR("Invalid range for '--hist-parameters'. "
			      "Make sure that 'max' is greater than 'min'.\n");
			return 1;
		}

	}

	hist_i = 0;
	merge_all(fh, model, config_maxonly, config_scale, config_sum,
	          config_startafter, config_stopafter,
                  sym, n_total_patterns,
                  NULL, 0, 0, 0, NULL, 1);
	if ( ferror(fh) ) {
		ERROR("Stream read error.\n");
		return 1;
	}
	rewind(fh);

	STATUS("Extra pass to calculate ESDs...\n");
	rewind(fh);
	merge_all(fh, model, config_maxonly, config_scale, 0,
	          config_startafter, config_stopafter, sym, n_total_patterns,
	          hist_vals, hist_h, hist_k, hist_l, &hist_i, 2);
	if ( ferror(fh) ) {
		ERROR("Stream read error.\n");
		return 1;
	}

	if ( space_for_hist && (hist_i >= space_for_hist) ) {
		ERROR("Histogram array was too small!\n");
	}

	if ( hist_vals != NULL ) {
		STATUS("%i %i %i was seen %i times.\n", hist_h, hist_k, hist_l,
		                                        hist_i);
		plot_histogram(hist_vals, hist_i, hist_min, hist_max,
		               hist_nbins);
	}

	write_reflist(output, model, NULL);

	fclose(fh);

	free(sym);
	reflist_free(model);
	free(output);

	return 0;
}
