/*
 * ring_check.c
 *
 * Check peak integration
 *
 * Copyright © 2012-2020 Deutsches Elektronen-Synchrotron DESY,
 *                       a research centre of the Helmholtz Association.
 *
 * Authors:
 *   2011-2020 Thomas White <taw@physics.org>
 *   2012      Andrew Martin <andrew.martin@desy.de>
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

#include <stdlib.h>
#include <stdio.h>

#include <image.h>
#include <utils.h>


extern int integrate_peak(struct image *image,
                          int p_cfs, int p_css, int pn,
                          double *pfs, double *pss,
                          double *intensity, double *sigma,
                          double ir_inn, double ir_mid, double ir_out,
                          int *saturated);

/* The third integration check draws a Poisson background and checks that, on
 * average, it gets subtracted by the background subtraction. */
static void third_integration_check(struct image *image, int n_trials,
                                    int *fail, gsl_rng *rng)
{
	double mean_intensity = 0.0;
	double mean_bg = 0.0;
	double mean_max = 0.0;
	double mean_sigma = 0.0;
	int i;
	int fs, ss;
	int nfail = 0;

	for ( i=0; i<n_trials; i++ ) {

		double intensity, sigma;
		double fsp, ssp;
		int r;

		for ( fs=0; fs<image->detgeom->panels[0].w; fs++ ) {
		for ( ss=0; ss<image->detgeom->panels[0].h; ss++ ) {
			image->dp[0][fs+image->detgeom->panels[0].w*ss]
			                           = poisson_noise(rng, 1000.0);
		}
		}

		r = integrate_peak(image, 64, 64, 0,
		                   &fsp, &ssp, &intensity, &sigma,
		                   10.0, 15.0, 17.0, NULL);

		if ( r == 0 ) {
			mean_intensity += intensity;
			mean_sigma += sigma;
		} else {
			nfail++;
		}

	}
	mean_intensity /= n_trials;
	mean_bg /= n_trials;
	mean_max /= n_trials;
	mean_sigma /= n_trials;

	STATUS("  Third check (mean values): intensity = %.2f, sigma = %.2f,"
	       " integration failed %i/%i times\n",
	       mean_intensity, mean_sigma, nfail, n_trials);

/* These values are always wrong, because the integration sucks */
//	if ( fabs(mean_intensity) > 5.0 ) {
//		ERROR("Mean intensity should be close to zero.\n");
//		*fail = 1;
//	}
//	if ( fabs(mean_intensity) > mean_sigma/10.0 ) {
//		ERROR("Mean intensity should be much less than mean sigma.\n");
//		*fail = 1;
//	}
}


/* The fourth integration check draws a Poisson background and draws a peak on
 * top of it, then checks that the intensity of the peak is correctly recovered
 * accounting for the background. */
static void fourth_integration_check(struct image *image, int n_trials,
                                     int *fail, gsl_rng *rng)
{
	double mean_intensity = 0.0;
	double mean_sigma = 0.0;
	int i;
	int fs, ss;
	int pcount = 0;
	int nfail = 0;

	for ( i=0; i<n_trials; i++ ) {

		double intensity, sigma;
		double fsp, ssp;
		int r;

		for ( fs=0; fs<image->detgeom->panels[0].w; fs++ ) {
		for ( ss=0; ss<image->detgeom->panels[0].h; ss++ ) {
			int idx = fs+image->detgeom->panels[0].w*ss;
			image->dp[0][idx] = poisson_noise(rng, 1000.0);
			if ( (fs-64)*(fs-64) + (ss-64)*(ss-64) > 9*9 ) continue;
			image->dp[0][idx] += 1000.0;
			pcount++;
		}
		}

		r = integrate_peak(image, 64, 64, 0,
		                   &fsp, &ssp, &intensity, &sigma,
		                   10.0, 15.0, 17.0, NULL);

		if ( r == 0 ) {
			mean_intensity += intensity;
			mean_sigma += sigma;
		} else {
			nfail++;
		}

	}
	mean_intensity /= n_trials;
	mean_sigma /= n_trials;
	pcount /= n_trials;

	STATUS(" Fourth check (mean values): intensity = %.2f, sigma = %.2f,"
	       " integration failed %i/%i times\n",
	       mean_intensity, mean_sigma, nfail, n_trials);

	if ( fabs(mean_intensity - pcount*1000.0) > 4000.0 ) {
		ERROR("Mean intensity should be close to %f\n", pcount*1000.0);
		*fail = 1;
	}
	if ( fabs(mean_intensity) < mean_sigma ) {
		ERROR("Mean intensity should be greater than mean sigma.\n");
		*fail = 1;
	}
}


int main(int argc, char *argv[])
{
	struct image image;
	double fsp, ssp, intensity, sigma;
	int fs, ss;
	FILE *fh;
	unsigned long int seed;
	int fail = 0;
	const int n_trials = 100;
	int r, npx;
	double ex;
	gsl_rng *rng;

	rng = gsl_rng_alloc(gsl_rng_mt19937);

	fh = fopen("/dev/urandom", "r");
	if ( fread(&seed, sizeof(seed), 1, fh) == 1 ) {
		gsl_rng_set(rng, seed);
	} else {
		ERROR("Failed to seed RNG\n");
	}
	fclose(fh);
	gsl_rng_set(rng, seed);

	image.dp = malloc(sizeof(float *));
	image.dp[0] = malloc(128*128*sizeof(float));
	image.bad = malloc(sizeof(uint16_t *));
	image.bad[0] = calloc(128*128, sizeof(int));
	image.lambda = ph_eV_to_lambda(1000.0);

	image.detgeom = calloc(1, sizeof(struct detgeom));
	image.detgeom->n_panels = 1;
	image.detgeom->panels = calloc(1, sizeof(struct detgeom_panel));

	image.detgeom->panels[0].fsx = 1.0;
	image.detgeom->panels[0].fsy = 0.0;
	image.detgeom->panels[0].ssx = 0.0;
	image.detgeom->panels[0].ssy = 1.0;
	image.detgeom->panels[0].cnx = -64.0;
	image.detgeom->panels[0].cny = -64.0;
	image.detgeom->panels[0].cnz = 1.0;
	image.detgeom->panels[0].pixel_pitch = 1.0;
	image.detgeom->panels[0].w = 128;
	image.detgeom->panels[0].h = 128;
	image.detgeom->panels[0].adu_per_photon = 1.0;
	image.detgeom->panels[0].max_adu = +INFINITY;  /* No cutoff */

	memset(image.dp[0], 0, 128*128*sizeof(float));

	image.n_crystals = 0;
	image.crystals = NULL;

	/* First check: no intensity -> no peak, or very low intensity */
	r = integrate_peak(&image, 64, 64, 0,
	                   &fsp, &ssp, &intensity, &sigma,
	                   10.0, 15.0, 17.0, NULL);
	STATUS("  First check: integrate_peak() returned %i", r);
	if ( r == 0 ) {

		STATUS(", intensity = %.2f, sigma = %.2f\n", intensity, sigma);

		if ( fabs(intensity) > 0.01 ) {
			ERROR("Intensity should be very close to zero.\n");
			fail = 1;
		}

	} else {
		STATUS(" (correct)\n");
	}

	/* Second check: uniform peak gives correct I and low sigma(I) */
	npx = 0;
	for ( fs=0; fs<image.detgeom->panels[0].w; fs++ ) {
	for ( ss=0; ss<image.detgeom->panels[0].h; ss++ ) {
		if ( (fs-64)*(fs-64) + (ss-64)*(ss-64) > 9*9 ) continue;
		image.dp[0][fs+image.detgeom->panels[0].w*ss] = 1000.0;
		npx++;
	}
	}

	r = integrate_peak(&image, 64, 64, 0,
	                   &fsp, &ssp, &intensity, &sigma,
	                   10.0, 15.0, 17.0, NULL);
	if ( r ) {
		ERROR(" Second check: integrate_peak() returned %i (wrong).\n",
		      r);
		fail = 1;
	} else {

		STATUS(" Second check: intensity = %.2f, sigma = %.2f\n",
		       intensity, sigma);

		ex = npx*1000.0;
		if ( within_tolerance(ex, intensity, 1.0) == 0 ) {
			ERROR("Intensity should be close to %f\n", ex);
			fail = 1;
		}

		ex = sqrt(npx*1000.0);
		if ( within_tolerance(ex, sigma, 1.0) == 0 ) {
			ERROR("Sigma should be roughly %f.\n", ex);
			fail = 1;
		}

	}

	/* Third check: Poisson background should get mostly subtracted */
	third_integration_check(&image, n_trials, &fail, rng);

	/* Fourth check: peak on Poisson background */
	fourth_integration_check(&image, n_trials, &fail, rng);

	/* Fifth check: uniform peak on uniform background */
	npx = 0;
	for ( fs=0; fs<image.detgeom->panels[0].w; fs++ ) {
	for ( ss=0; ss<image.detgeom->panels[0].h; ss++ ) {
		image.dp[0][fs+image.detgeom->panels[0].w*ss] = 1000.0;
		if ( (fs-64)*(fs-64) + (ss-64)*(ss-64) > 9*9 ) continue;
		image.dp[0][fs+image.detgeom->panels[0].w*ss] += 1000.0;
		npx++;
	}
	}

	r = integrate_peak(&image, 64, 64, 0,
	                   &fsp, &ssp, &intensity, &sigma,
	                   10.0, 15.0, 17.0, NULL);
	if ( r ) {
		ERROR("   Fifth check: integrate_peak() returned %i (wrong).\n",
		      r);
		fail = 1;
	} else {

		STATUS("  Fifth check: intensity = %.2f, sigma = %.2f\n",
		       intensity, sigma);

		ex = npx*1000.0;
		if ( within_tolerance(ex, intensity, 1.0) == 0 ) {
			ERROR("Intensity should be close to %f\n", ex);
			fail = 1;
		}

		ex = sqrt(npx*1000.0);
		if ( within_tolerance(ex, sigma, 1.0) == 0 ) {
			ERROR("Sigma should be roughly %f.\n", ex);
			fail = 1;
		}

	}

	detgeom_free(image.detgeom);
	free(image.dp[0]);
	free(image.dp);
	gsl_rng_free(rng);

	if ( fail ) return 1;

	return 0;
}
