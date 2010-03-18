/*
 * get_hkl.c
 *
 * Small program to write out a list of h,k,l,I values given a structure
 *
 * (c) 2006-2010 Thomas White <taw@physics.org>
 *
 * Part of CrystFEL - crystallography with a FEL
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
#include "sfac.h"
#include "reflections.h"


static void show_help(const char *s)
{
	printf("Syntax: %s [options]\n\n", s);
	printf(
"Write idealised intensity lists.\n"
"\n"
"  -h, --help                 Display this help message.\n"
"\n"
"  -t, --template=<filename>  Only include reflections mentioned in file.\n"
"      --poisson              Simulate Poisson samples.\n"
"      --twin                 Generate twinned data.\n"
"  -o  --output=<filename>    Output filename (default: stdout).\n");
}


static double *template_reflections(double *ref, const char *filename,
                                    unsigned int *counts)
{
	char *rval;
	double *out;
	FILE *fh;

	fh = fopen(filename, "r");
	if ( fh == NULL ) {
		return NULL;
	}

	out = new_list_intensity();

	do {

		char line[1024];
		double val;
		int r;
		signed int h, k, l;

		rval = fgets(line, 1023, fh);

		r = sscanf(line, "%i %i %i", &h, &k, &l);
		if ( r != 3 ) continue;

		val = lookup_intensity(ref, h, k, l);
		set_intensity(out, h, k, l, val);
		if ( counts != NULL ) {
			set_count(counts, h, k, l, 1);
		}

	} while ( rval != NULL );

	fclose(fh);

	return out;
}


/* Apply Poisson noise to all reflections */
static void noisify_reflections(double *ref)
{
	signed int h, k, l;

	for ( h=-INDMAX; h<INDMAX; h++ ) {
	for ( k=-INDMAX; k<INDMAX; k++ ) {
	for ( l=-INDMAX; l<INDMAX; l++ ) {

		double val;
		int c;

		val = lookup_intensity(ref, h, k, l);
		c = poisson_noise(val);
		set_intensity(ref, h, k, l, c);

	}
	}
	progress_bar(h+INDMAX, 2*INDMAX, "Simulating noise");
	}
}


int main(int argc, char *argv[])
{
	int c;
	double *ref;
	double *ideal_ref;
	struct molecule *mol;
	char *template = NULL;
	int config_noisify = 0;
	int config_twin = 0;
	char *output = NULL;
	unsigned int *counts;
	signed int h, k, l;

	/* Long options */
	const struct option longopts[] = {
		{"help",               0, NULL,               'h'},
		{"template",           1, NULL,               't'},
		{"poisson",            0, &config_noisify,     1},
		{"output",             1, NULL,               'o'},
		{"twin",               0, &config_twin,        1},
		{0, 0, NULL, 0}
	};

	/* Short options */
	while ((c = getopt_long(argc, argv, "ht:o:", longopts, NULL)) != -1) {

		switch (c) {
		case 'h' : {
			show_help(argv[0]);
			return 0;
		}

		case 't' : {
			template = strdup(optarg);
			break;
		}

		case 'o' : {
			output = strdup(optarg);
			break;
		}

		case 0 : {
			break;
		}

		default : {
			return 1;
		}
		}

	}

	mol = load_molecule();
	get_reflections_cached(mol, eV_to_J(1.8e3));
	ideal_ref = ideal_intensities(mol->reflections);

	counts = new_list_count();

	if ( template != NULL ) {

		ref = template_reflections(ideal_ref, template, counts);
		if ( ref == NULL ) {
			ERROR("Couldn't read template file!\n");
			return 1;
		}

	} else {

		ref = ideal_ref;
		for ( h=-INDMAX; h<=INDMAX; h++ ) {
		for ( k=-INDMAX; k<=INDMAX; k++ ) {
		for ( l=-INDMAX; l<=INDMAX; l++ ) {
			set_count(counts, h, k, l, 1);
		}
		}
		}

	}

	if ( config_noisify ) noisify_reflections(ref);

	if ( config_twin ) {

		STATUS("Twinning...\n");

		for ( h=-INDMAX; h<=INDMAX; h++ ) {
		for ( k=-INDMAX; k<=INDMAX; k++ ) {
		for ( l=-INDMAX; l<=INDMAX; l++ ) {

			if ( lookup_count(counts, h, k, l) != 0 ) {

				double a, b;

				a = lookup_intensity(ideal_ref, h, k, l);
				b = lookup_intensity(ideal_ref, k, h, -l);

				set_intensity(ref, h, k, l, (a+b)/2.0);

				STATUS("%i %i %i\n", h, k, l);

			}

		}
		}
		}

	}

	write_reflections(output, NULL, ref, 0, mol->cell);

	return 0;
}
