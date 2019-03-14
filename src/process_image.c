/*
 * process_image.c
 *
 * The processing pipeline for one image
 *
 * Copyright © 2012-2019 Deutsches Elektronen-Synchrotron DESY,
 *                       a research centre of the Helmholtz Association.
 *
 * Authors:
 *   2010-2019 Thomas White <taw@physics.org>
 *   2014-2017 Valerio Mariani <valerio.mariani@desy.de>
 *   2017      Stijn de Graaf
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
#include <hdf5.h>
#include <gsl/gsl_errno.h>
#include <gsl/gsl_statistics_double.h>
#include <gsl/gsl_sort.h>
#include <unistd.h>
#include <sys/stat.h>

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
#include "process_image.h"
#include "integration.h"
#include "predict-refine.h"
#include "im-sandbox.h"
#include "time-accounts.h"
#include "im-zmq.h"


static float **backup_image_data(float **dp, struct detector *det)
{
	float **bu;
	int i;

	bu = malloc(det->n_panels * sizeof(float *));
	if ( bu == NULL ) return NULL;

	for ( i=0; i<det->n_panels; i++ ) {

		size_t data_size;

		data_size = det->panels[i].w * det->panels[i].h * sizeof(float);
		bu[i] = malloc(data_size);
		if ( bu[i] == NULL ) {
			free(bu);
			ERROR("Failed to allocate pre-filter backup.\n");
			return NULL;
		}

		memcpy(bu[i], dp[i], data_size);

	}

	return bu;
}


static void restore_image_data(float **dp, struct detector *det, float **bu)
{
	int i;

	for ( i=0; i<det->n_panels; i++ ) {
		size_t data_size;
		data_size = det->panels[i].w * det->panels[i].h * sizeof(float);
		memcpy(dp[i], bu[i], data_size);
		free(bu[i]);
	}
	free(bu);
}


static int file_wait_open_read(struct sb_shm *sb_shared, struct image *image,
                               TimeAccounts *taccs, char *last_task,
                               signed int wait_for_file, int cookie,
                               struct imagefile **pimfile)
{
	signed int file_wait_time = wait_for_file;
	int wait_message_done = 0;
	int read_retry_done = 0;
	int r;
	struct imagefile *imfile;

	time_accounts_set(taccs, TACC_WAITFILE);
	set_last_task(last_task, "wait for file");

	do {

		struct stat statbuf;

		sb_shared->pings[cookie]++;
		r = stat(image->filename, &statbuf);
		if ( r ) {

			if ( (wait_for_file != 0) && (file_wait_time != 0) ) {

				if ( !wait_message_done ) {
					STATUS("Waiting for '%s'\n",
					       image->filename);
					wait_message_done = 1;
				}

				sleep(1);
				if ( wait_for_file != -1 ) {
					file_wait_time--;
				}
				continue;

			}

			ERROR("File %s not found\n", image->filename);
			return 1;
		}

	} while ( r );

	time_accounts_set(taccs, TACC_HDF5OPEN);
	set_last_task(last_task, "open file");
	sb_shared->pings[cookie]++;

	do {
		imfile = imagefile_open(image->filename);
		if ( imfile == NULL ) {
			if ( wait_for_file && !read_retry_done ) {
				read_retry_done = 1;
				r = 1;
				STATUS("File '%s' exists but could not be opened."
				       "  Trying again after 10 seconds.\n",
				       image->filename);
				sleep(10);
				continue;
			}
			ERROR("Couldn't open file: %s\n", image->filename);
			return 1;
		}

		time_accounts_set(taccs, TACC_HDF5READ);
		set_last_task(last_task, "read file");
		sb_shared->pings[cookie]++;

		r = imagefile_read(imfile, image, image->event);
		if ( r ) {
			if ( wait_for_file && !read_retry_done ) {
				read_retry_done = 1;
				imagefile_close(imfile);
				STATUS("File '%s' exists but could not be read."
				       "  Trying again after 10 seconds.\n",
				       image->filename);
				sleep(10);
				continue;
			}
			ERROR("Couldn't open file: %s\n", image->filename);
			return 1;
		}

	} while ( r );

	*pimfile = imfile;
	return 0;
}


void process_image(const struct index_args *iargs, struct pattern_args *pargs,
                   Stream *st, int cookie, const char *tmpdir,
                   int serial, struct sb_shm *sb_shared, TimeAccounts *taccs,
                   char *last_task)
{
	struct imagefile *imfile = NULL;
	struct image image;
	int i;
	int r;
	int ret;
	char *rn;
	float **prefilter;
	int any_crystals;

	image.features = NULL;
	image.copyme = iargs->copyme;
	image.id = cookie;
	image.beam = iargs->beam;
	image.det = copy_geom(iargs->det);
	image.crystals = NULL;
	image.n_crystals = 0;
	image.serial = serial;
	image.indexed_by = INDEXING_NONE;

	if ( pargs->msgpack_obj != NULL ) {
		STATUS("Msgpack!\n");
		if ( unpack_msgpack_data(pargs->msgpack_obj, &image) ) return;
	} else if ( pargs->filename_p_e != NULL ) {
		image.filename = pargs->filename_p_e->filename;
		image.event = pargs->filename_p_e->ev;
		if ( file_wait_open_read(sb_shared, &image, taccs, last_task,
		                         iargs->wait_for_file, cookie,
		                         &imfile) ) return;
	}

	/* Take snapshot of image before applying horrible noise filters */
	time_accounts_set(taccs, TACC_FILTER);
	set_last_task(last_task, "image filter");
	sb_shared->pings[cookie]++;
	prefilter = backup_image_data(image.dp, image.det);

	if ( iargs->median_filter > 0 ) {
		filter_median(&image, iargs->median_filter);
	}

	if ( iargs->noisefilter ) {
		filter_noise(&image);
	}

	time_accounts_set(taccs, TACC_RESRANGE);
	set_last_task(last_task, "resolution range");
	sb_shared->pings[cookie]++;
	mark_resolution_range_as_bad(&image, iargs->highres, +INFINITY);

	time_accounts_set(taccs, TACC_PEAKSEARCH);
	sb_shared->pings[cookie]++;
	switch ( iargs->peaks ) {

		struct hdfile *hdfile;

		case PEAK_HDF5:
		set_last_task(last_task, "peaksearch:hdf5");
		hdfile = imagefile_get_hdfile(imfile);
		if ( (hdfile == NULL)
		  || (get_peaks_2(&image, hdfile, iargs->hdf5_peak_path,
		                  iargs->half_pixel_shift)) )
		{
			ERROR("Failed to get peaks from HDF5 file.\n");
		}
		if ( !iargs->no_revalidate ) {
			validate_peaks(&image, iargs->min_snr,
				       iargs->pk_inn, iargs->pk_mid,
		                       iargs->pk_out, iargs->use_saturated,
				       iargs->check_hdf5_snr);
		}
		break;

		case PEAK_CXI:
		set_last_task(last_task, "peaksearch:cxi");
		hdfile = imagefile_get_hdfile(imfile);
		if ( (hdfile == NULL)
		 ||  (get_peaks_cxi_2(&image, hdfile, iargs->hdf5_peak_path,
		                      pargs->filename_p_e,
		                      iargs->half_pixel_shift)) )
		{
			ERROR("Failed to get peaks from CXI file.\n");
		}
		if ( !iargs->no_revalidate ) {
			validate_peaks(&image, iargs->min_snr,
				       iargs->pk_inn, iargs->pk_mid,
		                       iargs->pk_out, iargs->use_saturated,
				       iargs->check_hdf5_snr);
		}
		break;

		case PEAK_ZAEF:
		set_last_task(last_task, "peaksearch:zaef");
		search_peaks(&image, iargs->threshold,
		             iargs->min_sq_gradient, iargs->min_snr,
		             iargs->pk_inn, iargs->pk_mid, iargs->pk_out,
		             iargs->use_saturated);
		break;

		case PEAK_PEAKFINDER8:
		set_last_task(last_task, "peaksearch:pf8");
		if ( search_peaks_peakfinder8(&image, 2048,
		                               iargs->threshold,
		                               iargs->min_snr,
		                               iargs->min_pix_count,
		                               iargs->max_pix_count,
		                               iargs->local_bg_radius,
		                               iargs->min_res,
		                               iargs->max_res,
		                               iargs->use_saturated) ) {
			if ( image.event != NULL ) {
				ERROR("Failed to find peaks in image %s"
				      "(event %s).\n", image.filename,
				      get_event_string(image.event));
			} else {
				ERROR("Failed to find peaks in image %s.",
				      image.filename);
			}

		}
		break;

		case PEAK_PEAKFINDER9:
		set_last_task(last_task, "peaksearch:pf9");
		if ( search_peaks_peakfinder9(&image,
		                              iargs->min_snr_biggest_pix,
		                              iargs->min_snr_peak_pix,
		                              iargs->min_snr,
		                              iargs->min_sig,
		                              iargs->min_peak_over_neighbour,
		                              iargs->local_bg_radius) )
		{
			if ( image.event != NULL ) {
				ERROR("Failed to find peaks in image %s"
				      "(event %s).\n", image.filename,
				      get_event_string(image.event));
			} else {
				ERROR("Failed to find peaks in image %s.",
				      image.filename);
			}
		}
		break;

		case PEAK_MSGPACK:
		get_peaks_msgpack(pargs->msgpack_obj, &image,
		                  iargs->half_pixel_shift);
		break;

	}

	image.peak_resolution = estimate_peak_resolution(image.features,
	                                                 image.lambda);

	restore_image_data(image.dp, image.det, prefilter);

	rn = getcwd(NULL, 0);

	r = chdir(tmpdir);
	if ( r ) {
		ERROR("Failed to chdir to temporary folder: %s\n",
		      strerror(errno));
		imagefile_close(imfile);
		return;
	}

	/* Set beam parameters */
	if ( iargs->fix_divergence >= 0.0 ) {
		image.div = iargs->fix_divergence;
	} else {
		image.div = 0.0;
	}
	if ( iargs->fix_bandwidth >= 0.0 ) {
		image.bw = iargs->fix_bandwidth;
	} else {
		image.bw = 0.00000001;
	}

	/* Set beam spectrum for pink beam data */
	if ( iargs->spectrum != NULL ) {
		image.spectrum = iargs->spectrum;
	}

	if ( image_feature_count(image.features) < iargs->min_peaks ) {
		r = chdir(rn);
		if ( r ) {
			ERROR("Failed to chdir: %s\n", strerror(errno));
			imagefile_close(imfile);
			return;
		}
		free(rn);
		image.hit = 0;

		if ( iargs->stream_nonhits ) {
			goto streamwrite;
		} else {
			goto out;
		}
	}
	image.hit = 1;

	/* Index the pattern */
	time_accounts_set(taccs, TACC_INDEXING);
	set_last_task(last_task, "indexing");
	index_pattern_3(&image, iargs->ipriv, &sb_shared->pings[cookie],
	                last_task);

	r = chdir(rn);
	if ( r ) {
		ERROR("Failed to chdir: %s\n", strerror(errno));
		imagefile_close(imfile);
		return;
	}
	free(rn);

	/* Set beam/crystal parameters */
	time_accounts_set(taccs, TACC_PREDPARAMS);
	set_last_task(last_task, "prediction params");
	if ( iargs->fix_profile_r >= 0.0 ) {
		for ( i=0; i<image.n_crystals; i++ ) {
			crystal_set_profile_radius(image.crystals[i],
			                           iargs->fix_profile_r);
			crystal_set_mosaicity(image.crystals[i], 0.0);
		}
	} else {
		for ( i=0; i<image.n_crystals; i++ ) {
			crystal_set_profile_radius(image.crystals[i], 0.02e9);
			crystal_set_mosaicity(image.crystals[i], 0.0);
		}
	}

	if ( iargs->fix_profile_r < 0.0 ) {
		for ( i=0; i<image.n_crystals; i++ ) {
			if ( refine_radius(image.crystals[i], &image) ) {
				ERROR("WARNING: Radius determination failed\n");
			}
		}
	}

	/* Integrate! */
	time_accounts_set(taccs, TACC_INTEGRATION);
	set_last_task(last_task, "integration");
	sb_shared->pings[cookie]++;
	integrate_all_5(&image, iargs->int_meth, PMODEL_XSPHERE,
	                iargs->push_res,
	                iargs->ir_inn, iargs->ir_mid, iargs->ir_out,
	                iargs->int_diag, iargs->int_diag_h,
	                iargs->int_diag_k, iargs->int_diag_l,
	                &sb_shared->term_lock, iargs->overpredict);

streamwrite:
	time_accounts_set(taccs, TACC_WRITESTREAM);
	set_last_task(last_task, "stream write");
	sb_shared->pings[cookie]++;
	ret = write_chunk(st, &image, imfile,
	                  iargs->stream_peaks, iargs->stream_refls,
	                  pargs->filename_p_e->ev);
	if ( ret != 0 ) {
		ERROR("Error writing stream file.\n");
	}

	int n = 0;
	for ( i=0; i<image.n_crystals; i++ ) {
		n += crystal_get_num_implausible_reflections(image.crystals[i]);
	}
	if ( n > 0 ) {
		STATUS("WARNING: %i implausibly negative reflection%s in %s "
		       "%s\n", n, n>1?"s":"", image.filename,
		       get_event_string(image.event));
	}

out:
	/* Count crystals which are still good */
	time_accounts_set(taccs, TACC_TOTALS);
	set_last_task(last_task, "process_image finalisation");
	sb_shared->pings[cookie]++;
	pthread_mutex_lock(&sb_shared->totals_lock);
	any_crystals = 0;
	for ( i=0; i<image.n_crystals; i++ ) {
		if ( crystal_get_user_flag(image.crystals[i]) == 0 ) {
			sb_shared->n_crystals++;
			any_crystals = 1;
		}
	}
	sb_shared->n_processed++;
	sb_shared->n_hits += image.hit;
	sb_shared->n_hadcrystals += any_crystals;
	pthread_mutex_unlock(&sb_shared->totals_lock);

	for ( i=0; i<image.n_crystals; i++ ) {
		cell_free(crystal_get_cell(image.crystals[i]));
		reflist_free(crystal_get_reflections(image.crystals[i]));
		crystal_free(image.crystals[i]);
	}
	free(image.crystals);

	for ( i=0; i<image.det->n_panels; i++ ) {
		free(image.dp[i]);
		free(image.bad[i]);
		free(image.sat[i]);
	}
	free(image.dp);
	free(image.bad);
	free(image.sat);

	image_feature_list_free(image.features);
	free_detector_geometry(image.det);
	imagefile_close(imfile);
}
