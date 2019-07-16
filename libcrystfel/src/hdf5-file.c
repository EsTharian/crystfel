/*
 * hdf5-file.c
 *
 * Read/write HDF5 data files
 *
 * Copyright © 2012-2016 Deutsches Elektronen-Synchrotron DESY,
 *                       a research centre of the Helmholtz Association.
 *
 * Authors:
 *   2009-2016 Thomas White <taw@physics.org>
 *   2014      Valerio Mariani
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
#include <stdint.h>
#include <hdf5.h>
#include <assert.h>
#include <unistd.h>

#include "events.h"
#include "image.h"
#include "hdf5-file.h"
#include "utils.h"

/** \file hdf5-file.h */

struct hdf5_write_location {

	const char      *location;
	int              n_panels;
	int             *panel_idxs;

	int              max_ss;
	int              max_fs;

};


int split_group_and_object(const char *path, char **group, char **object)
{
	const char *sep;
	const char *store;

	sep = path;
	store = sep;
	sep = strpbrk(sep + 1, "/");
	if ( sep != NULL ) {
		while ( 1 ) {
			store = sep;
			sep = strpbrk(sep + 1, "/");
			if ( sep == NULL ) {
				break;
			}
		}
	}
	if ( store == path ) {
		*group = NULL;
		*object = strdup(path);
	} else {
		*group = strndup(path, store - path);
		*object = strdup(store+1);
	}
	return 0;
};


struct hdfile {

	const char      *path;  /* Current data path */

	hid_t           fh;  /* HDF file handle */
	hid_t           dh;  /* Dataset handle */

	int             data_open;  /* True if dh is initialised */
};


struct hdfile *hdfile_open(const char *filename)
{
	struct hdfile *f;

	f = malloc(sizeof(struct hdfile));
	if ( f == NULL ) return NULL;

	if ( access( filename, R_OK ) == -1 ) {
		ERROR("File does not exist or cannot be read: %s\n",
		      filename);
		free(f);
		return NULL;
	}

	f->fh = H5Fopen(filename, H5F_ACC_RDONLY, H5P_DEFAULT);
	if ( f->fh < 0 ) {
		ERROR("Couldn't open file: %s\n", filename);
		free(f);
		return NULL;
	}

	f->data_open = 0;
	return f;
}


int hdfile_set_image(struct hdfile *f, const char *path)
{
	f->dh = H5Dopen2(f->fh, path, H5P_DEFAULT);
	if ( f->dh < 0 ) {
		ERROR("Couldn't open dataset\n");
		return -1;
	}
	f->data_open = 1;
	return 0;
}


static int read_peak_count(struct hdfile *f, char *path, int line,
                           int *num_peaks)
{

	hid_t dh, sh, mh;
	hsize_t size[1];
	hsize_t max_size[1];
	hsize_t offset[1], count[1];
	hsize_t m_offset[1], m_count[1], dimmh[1];


	int tw, r;

	dh = H5Dopen2(f->fh, path, H5P_DEFAULT);
	if ( dh < 0 ) {
		ERROR("Data block %s not found.\n", path);
		return 1;
	}

	sh = H5Dget_space(dh);
	if ( sh < 0 ) {
		H5Dclose(dh);
		ERROR("Couldn't get dataspace for data.\n");
		return 1;
	}

	if ( H5Sget_simple_extent_ndims(sh) != 1 ) {
		ERROR("Data block %s has the wrong dimensionality (%i).\n",
		      path, H5Sget_simple_extent_ndims(sh));
		H5Sclose(sh);
		H5Dclose(dh);
		return 1;
	}

	H5Sget_simple_extent_dims(sh, size, max_size);

	tw = size[0];

	if ( line > tw-1 ) {
		H5Sclose(sh);
		H5Dclose(dh);
		ERROR("Data block %s does not contain data for required event.\n",
		      path);
		return 1;
	}

	offset[0] = line;
	count[0] = 1;

	r = H5Sselect_hyperslab(sh, H5S_SELECT_SET,
	                        offset, NULL, count, NULL);
	if ( r < 0 ) {
		ERROR("Error selecting file dataspace "
		      "for data block %s\n", path);
		H5Dclose(dh);
		H5Sclose(sh);
		return 1;
	}

	m_offset[0] = 0;
	m_count[0] = 1;
	dimmh[0] = 1;
	mh = H5Screate_simple(1, dimmh, NULL);
	r = H5Sselect_hyperslab(mh, H5S_SELECT_SET,
	                        m_offset, NULL, m_count, NULL);
	if ( r < 0 ) {
		ERROR("Error selecting memory dataspace "
		      "for data block %s\n", path);
		H5Dclose(dh);
		H5Sclose(sh);
		H5Sclose(mh);
		return 1;
	}

	r = H5Dread(dh, H5T_NATIVE_INT, mh,
	            sh, H5P_DEFAULT, num_peaks);
	if ( r < 0 ) {
		ERROR("Couldn't read data for block %s, line %i\n", path, line);
		H5Dclose(dh);
		H5Sclose(sh);
		H5Sclose(mh);
		return 1;
	}

	H5Dclose(dh);
	H5Sclose(sh);
	H5Sclose(mh);
	return 0;
}



static float *read_hdf5_data(struct hdfile *f, char *path, int line)
{

	hid_t dh, sh, mh;
	hsize_t size[2];
	hsize_t max_size[2];
	hsize_t offset[2], count[2];
	hsize_t m_offset[2], m_count[2], dimmh[2];
	float *buf;
	int tw, r;

	dh = H5Dopen2(f->fh, path, H5P_DEFAULT);
	if ( dh < 0 ) {
		ERROR("Data block (%s) not found.\n", path);
		return NULL;
	}

	sh = H5Dget_space(dh);
	if ( sh < 0 ) {
		H5Dclose(dh);
		ERROR("Couldn't get dataspace for data.\n");
		return NULL;
	}

	if ( H5Sget_simple_extent_ndims(sh) != 2 ) {
		ERROR("Data block %s has the wrong dimensionality (%i).\n",
		      path, H5Sget_simple_extent_ndims(sh));
		H5Sclose(sh);
		H5Dclose(dh);
		return NULL;
	}

	H5Sget_simple_extent_dims(sh, size, max_size);

	tw = size[0];
	if ( line> tw-1 ) {
		H5Sclose(sh);
		H5Dclose(dh);
		ERROR("Data block %s does not contain data for required event.\n",
		      path);
		return NULL;
	}

	offset[0] = line;
	offset[1] = 0;
	count[0] = 1;
	count[1] = size[1];

	r = H5Sselect_hyperslab(sh, H5S_SELECT_SET, offset, NULL, count, NULL);
	if ( r < 0 ) {
	    ERROR("Error selecting file dataspace "
	          "for data block %s\n", path);
	    H5Dclose(dh);
	    H5Sclose(sh);
	    return NULL;
	}

	m_offset[0] = 0;
	m_offset[1] = 0;
	m_count[0] = 1;
	m_count[1] = size[1];
	dimmh[0] = 1;
	dimmh[1] = size[1];

	mh = H5Screate_simple(2, dimmh, NULL);
	r = H5Sselect_hyperslab(mh, H5S_SELECT_SET,
	                        m_offset, NULL, m_count, NULL);
	if ( r < 0 ) {
		ERROR("Error selecting memory dataspace "
		      "for data block %s\n", path);
		H5Dclose(dh);
		H5Sclose(sh);
		H5Sclose(mh);
		return NULL;
	}

	buf = malloc(size[1]*sizeof(float));
	if ( buf == NULL ) return NULL;
	r = H5Dread(dh, H5T_NATIVE_FLOAT, mh, sh, H5P_DEFAULT, buf);
	if ( r < 0 ) {
		ERROR("Couldn't read data for block %s, line %i\n", path, line);
		H5Dclose(dh);
		H5Sclose(sh);
		H5Sclose(mh);
		return NULL;
	}

	H5Dclose(dh);
	H5Sclose(sh);
	H5Sclose(mh);
	return buf;
}


/**
 * \param image: An \ref image structure
 * \param f: An \ref hdfile structure
 * \param p: The HDF5 path to the peak data
 * \param fpe: A \ref filename_plus_event structure specifying the event
 * \param half_pixel_shift: Non-zero if 0.5 should be added to all peak coordinates
 *
 * Get peaks from HDF5, in "CXI format" (as in "CXIDB").  The data should be in
 * a set of arrays under \p p.  The number of peaks should be in a 1D array at
 * \p p/nPeaks. The fast-scan and slow-scan coordinates should be in 2D arrays at
 * \p p/peakXPosRaw and \p p/peakYPosRaw respectively (sorry about the naming).  The
 * first dimension of these arrays should be the event number (as given by
 * \p fpe).  The intensities are expected to be at \p p/peakTotalIntensity in a
 * similar 2D array.
 *
 * CrystFEL considers all peak locations to be distances from the corner of the
 * detector panel, in pixel units, consistent with its description of detector
 * geometry (see 'man crystfel_geometry').  The software which generates the
 * CXI files, including Cheetah, may instead consider the peak locations to be
 * pixel indices in the data array.  In this case, the peak coordinates should
 * have 0.5 added to them.  This will be done if \p half_pixel_shift is non-zero.
 *
 * \returns Non-zero on error, zero otherwise.
 *
 */
int get_peaks_cxi_2(struct image *image, struct hdfile *f, const char *p,
                    struct filename_plus_event *fpe, int half_pixel_shift)
{
	char path_n[1024];
	char path_x[1024];
	char path_y[1024];
	char path_i[1024];
	int r;
	int pk;

	int line = 0;
	int num_peaks;

	float *buf_x;
	float *buf_y;
	float *buf_i;

	double peak_offset = half_pixel_shift ? 0.5 : 0.0;

	if ( (fpe != NULL) && (fpe->ev != NULL)
	  && (fpe->ev->dim_entries != NULL) )
	{
		line = fpe->ev->dim_entries[0];
	} else {
		ERROR("CXI format peak list format selected,"
		      "but file has no event structure");
		return 1;
	}

	snprintf(path_n, 1024, "%s/nPeaks", p);
	snprintf(path_x, 1024, "%s/peakXPosRaw", p);
	snprintf(path_y, 1024, "%s/peakYPosRaw", p);
	snprintf(path_i, 1024, "%s/peakTotalIntensity", p);

	r = read_peak_count(f, path_n, line, &num_peaks);
	if ( r != 0 ) return 1;

	buf_x = read_hdf5_data(f, path_x, line);
	if ( r != 0 ) return 1;

	buf_y = read_hdf5_data(f, path_y, line);
	if ( r != 0 ) return 1;

	buf_i = read_hdf5_data(f, path_i, line);
	if ( r != 0 ) return 1;

	if ( image->features != NULL ) {
		image_feature_list_free(image->features);
	}
	image->features = image_feature_list_new();

	for ( pk=0; pk<num_peaks; pk++ ) {

		float fs, ss, val;
		struct panel *p;

		fs = buf_x[pk] + peak_offset;
		ss = buf_y[pk] + peak_offset;
		val = buf_i[pk];

		p = find_orig_panel(image->det, fs, ss);
		if ( p == NULL ) continue;
		if ( p->no_index ) continue;

		/* Convert coordinates to panel-relative */
		fs = fs - p->orig_min_fs;
		ss = ss - p->orig_min_ss;

		image_add_feature(image->features, fs, ss, p, image, val, NULL);

	}

	return 0;
}


/**
 * \param image: An \ref image structure
 * \param f: An \ref hdfile structure
 * \param p: The HDF5 path to the peak data
 * \param fpe: A \ref filename_plus_event structure specifying the event
 *
 * This is a wrapper function to preserve API compatibility with older CrystFEL
 * versions.  Use \ref get_peaks_cxi_2 instead.
 *
 * This function is equivalent to get_peaks_cxi_2(\p image, \p f, \p p, \p fpe, 1).
 *
 * \returns Non-zero on error, zero otherwise.
 *
 */
int get_peaks_cxi(struct image *image, struct hdfile *f, const char *p,
                  struct filename_plus_event *fpe)
{
	return get_peaks_cxi_2(image, f, p, fpe, 1);
}


/**
 * \param image: An \ref image structure
 * \param f: An \ref hdfile structure
 * \param p: The HDF5 path to the peak data
 * \param half_pixel_shift: Non-zero if 0.5 should be added to all peak coordinates
 *
 * Get peaks from HDF5.  The peak list should be located at \p p in the HDF5 file,
 * a 2D array where the first dimension equals the number of peaks and second
 * dimension is three.  The first two columns contain the fast scan and slow
 * scan coordinates, respectively, of the peaks.  The third column contains the
 * intensities.
 *
 * CrystFEL considers all peak locations to be distances from the corner of the
 * detector panel, in pixel units, consistent with its description of detector
 * geometry (see 'man crystfel_geometry').  The software which generates the
 * CXI files, including Cheetah, may instead consider the peak locations to be
 * pixel indices in the data array.  In this case, the peak coordinates should
 * have 0.5 added to them.  This will be done if \p half_pixel_shift is non-zero.
 *
 * \returns Non-zero on error, zero otherwise.
 *
 */
int get_peaks_2(struct image *image, struct hdfile *f, const char *p,
                int half_pixel_shift)
{
	hid_t dh, sh;
	hsize_t size[2];
	hsize_t max_size[2];
	int i;
	float *buf;
	herr_t r;
	int tw;
	char *np;
	double peak_offset = half_pixel_shift ? 0.5 : 0.0;

	if ( image->event != NULL ) {
		np = retrieve_full_path(image->event, p);
	} else {
		np = strdup(p);
	}

	dh = H5Dopen2(f->fh, np, H5P_DEFAULT);
	if ( dh < 0 ) {
		ERROR("Peak list (%s) not found.\n", np);
		return 1;
	}

	sh = H5Dget_space(dh);
	if ( sh < 0 ) {
		H5Dclose(dh);
		ERROR("Couldn't get dataspace for peak list.\n");
		free(np);
		return 1;
	}

	if ( H5Sget_simple_extent_ndims(sh) != 2 ) {
		ERROR("Peak list has the wrong dimensionality (%i).\n",
		H5Sget_simple_extent_ndims(sh));
		H5Sclose(sh);
		H5Dclose(dh);
		free(np);
		return 1;
	}

	H5Sget_simple_extent_dims(sh, size, max_size);

	tw = size[1];
	if ( (tw != 3) && (tw != 4) ) {
		H5Sclose(sh);
		H5Dclose(dh);
		ERROR("Peak list has the wrong dimensions.\n");
		free(np);
		return 1;
	}

	buf = malloc(sizeof(float)*size[0]*size[1]);
	if ( buf == NULL ) {
		H5Sclose(sh);
		H5Dclose(dh);
		ERROR("Couldn't reserve memory for the peak list.\n");
		free(np);
		return 1;
	}
	r = H5Dread(dh, H5T_NATIVE_FLOAT, H5S_ALL, H5S_ALL,
	            H5P_DEFAULT, buf);
	if ( r < 0 ) {
		ERROR("Couldn't read peak list.\n");
		free(buf);
		free(np);
		return 1;
	}

	if ( image->features != NULL ) {
		image_feature_list_free(image->features);
	}
	image->features = image_feature_list_new();

	for ( i=0; i<size[0]; i++ ) {

		float fs, ss, val;
		struct panel *p;

		fs = buf[tw*i+0] + peak_offset;
		ss = buf[tw*i+1] + peak_offset;
		val = buf[tw*i+2];

		p = find_orig_panel(image->det, fs, ss);
		if ( p == NULL ) continue;
		if ( p->no_index ) continue;

		/* Convert coordinates to panel-relative */
		fs = fs - p->orig_min_fs;
		ss = ss - p->orig_min_ss;

		image_add_feature(image->features, fs, ss, p, image, val,
		                  NULL);

	}

	free(buf);
	free(np);
	H5Sclose(sh);
	H5Dclose(dh);

	return 0;
}


/**
 * \param image: An \ref image structure
 * \param f: An \ref hdfile structure
 * \param p: The HDF5 path to the peak data
 *
 * This is a wrapper function to preserve API compatibility with older CrystFEL
 * versions.  Use \ref get_peaks_2 instead.
 *
 * This function is equivalent to \ref get_peaks_2(\p image, \p f, \p p, 1).
 *
 * \returns Non-zero on error, zero otherwise.
 *
 */
int get_peaks(struct image *image, struct hdfile *f, const char *p)
{
	return get_peaks_2(image, f, p, 1);
}


static void cleanup(hid_t fh)
{
	int n_ids, i;
	hid_t ids[2048];

	n_ids = H5Fget_obj_ids(fh, H5F_OBJ_ALL, 2048, ids);

	for ( i=0; i<n_ids; i++ ) {

		hid_t id;
		H5I_type_t type;

		id = ids[i];

		type = H5Iget_type(id);

		if ( type == H5I_GROUP ) H5Gclose(id);
		if ( type == H5I_DATASET ) H5Dclose(id);
		if ( type == H5I_DATATYPE ) H5Tclose(id);
		if ( type == H5I_DATASPACE ) H5Sclose(id);
		if ( type == H5I_ATTR ) H5Aclose(id);

	}

}


void hdfile_close(struct hdfile *f)
{

	if ( f->data_open ) {
		H5Dclose(f->dh);
	}

	cleanup(f->fh);

	H5Fclose(f->fh);

	free(f);
}


/* Deprecated */
int hdf5_write(const char *filename, const void *data,
               int width, int height, int type)
{
	hid_t fh, gh, sh, dh;  /* File, group, dataspace and data handles */
	herr_t r;
	hsize_t size[2];

	fh = H5Fcreate(filename, H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT);
	if ( fh < 0 ) {
		ERROR("Couldn't create file: %s\n", filename);
		return 1;
	}

	gh = H5Gcreate2(fh, "data", H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
	if ( gh < 0 ) {
		ERROR("Couldn't create group\n");
		H5Fclose(fh);
		return 1;
	}

	/* Note the "swap" here, according to section 3.2.5,
	 * "C versus Fortran Dataspaces", of the HDF5 user's guide. */
	size[0] = height;
	size[1] = width;
	sh = H5Screate_simple(2, size, NULL);

	dh = H5Dcreate2(gh, "data", type, sh,
	                H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
	if ( dh < 0 ) {
		ERROR("Couldn't create dataset\n");
		H5Fclose(fh);
		return 1;
	}

	/* Muppet check */
	H5Sget_simple_extent_dims(sh, size, NULL);

	r = H5Dwrite(dh, type, H5S_ALL,
	             H5S_ALL, H5P_DEFAULT, data);
	if ( r < 0 ) {
		ERROR("Couldn't write data\n");
		H5Dclose(dh);
		H5Fclose(fh);
		return 1;
	}
	H5Dclose(dh);
	H5Gclose(gh);
	H5Fclose(fh);

	return 0;
}


static void add_panel_to_location(struct hdf5_write_location *loc,
                                  struct panel *p, int pi)
{
	int *new_panel_idxs;

	new_panel_idxs = realloc(loc->panel_idxs,
	                         (loc->n_panels+1)*sizeof(int));
	if ( new_panel_idxs == NULL ) {
		ERROR("Error while managing write location list.\n");
		return;
	}
	loc->panel_idxs = new_panel_idxs;
	loc->panel_idxs[loc->n_panels] = pi;
	loc->n_panels += 1;
	if ( p->orig_max_fs > loc->max_fs ) {
		loc->max_fs = p->orig_max_fs;
	}
	if ( p->orig_max_ss > loc->max_ss ) {
		loc->max_ss = p->orig_max_ss;
	}
}


static void add_panel_location(struct panel *p, const char *p_location, int pi,
                               struct hdf5_write_location **plocations,
                               int *pnum_locations)
{
	int li;
	int num_locations = *pnum_locations;
	struct hdf5_write_location *locations = *plocations;
	int done = 0;

	/* Does this HDF5 path already exist in the location list?
	 * If so, add the new panel to it (with a unique index, we hope) */
	for ( li=0; li<num_locations; li++ ) {
		if ( strcmp(p_location, locations[li].location) == 0 ) {
			add_panel_to_location(&locations[li], p, pi);
			done = 1;
		}
	}

	/* If not, add a new location to ths list */
	if ( !done ) {

		struct hdf5_write_location *new_locations;
		size_t nsz;

		nsz = (num_locations+1)*sizeof(struct hdf5_write_location);
		new_locations = realloc(locations, nsz);
		if ( new_locations == NULL ) {
			ERROR("Failed to grow location list.\n");
			return;
		}
		locations = new_locations;

		locations[num_locations].max_ss = p->orig_max_ss;
		locations[num_locations].max_fs = p->orig_max_fs;
		locations[num_locations].location = p_location;
		locations[num_locations].panel_idxs = malloc(sizeof(int));
		if ( locations[num_locations].panel_idxs == NULL ) {
			ERROR("Failed to allocate single idx (!)\n");
			return;
		}
		locations[num_locations].panel_idxs[0] = pi;
		locations[num_locations].n_panels = 1;

		num_locations += 1;

	}

	*plocations = locations;
	*pnum_locations = num_locations;
}


static struct hdf5_write_location *make_location_list(struct detector *det,
                                                      const char *def_location,
                                                      int *pnum_locations)
{
	int pi;
	struct hdf5_write_location *locations = NULL;
	int num_locations = 0;

	for ( pi=0; pi<det->n_panels; pi++ ) {

		struct panel *p;
		const char *p_location;

		p = &det->panels[pi];

		if ( p->data == NULL ) {
			p_location = def_location;
		} else {
			p_location = p->data;
		}

		add_panel_location(p, p_location, pi,
		                   &locations, &num_locations);

	}

	*pnum_locations = num_locations;
	return locations;
}


static void write_location(hid_t fh, struct detector *det, float **dp,
                           struct hdf5_write_location *loc)
{
	hid_t sh, dh, ph;
	hid_t dh_dataspace;
	hsize_t size[2];
	int pi;

	/* Note the "swap" here, according to section 3.2.5,
	 * "C versus Fortran Dataspaces", of the HDF5 user's guide. */
	size[0] = loc->max_ss+1;
	size[1] = loc->max_fs+1;
	sh = H5Screate_simple(2, size, NULL);

	ph = H5Pcreate(H5P_LINK_CREATE);
	H5Pset_create_intermediate_group(ph, 1);

	dh = H5Dcreate2(fh, loc->location, H5T_NATIVE_FLOAT, sh,
	                ph, H5P_DEFAULT, H5P_DEFAULT);
	if ( dh < 0 ) {
		ERROR("Couldn't create dataset\n");
		H5Fclose(fh);
		return;
	}

	H5Sget_simple_extent_dims(sh, size, NULL);

	for ( pi=0; pi<loc->n_panels; pi++ ) {

		hsize_t f_offset[2], f_count[2], dims[2];
		hid_t memspace;
		struct panel p;
		int r;

		p = det->panels[loc->panel_idxs[pi]];

		f_offset[0] = p.orig_min_ss;
		f_offset[1] = p.orig_min_fs;
		f_count[0] = p.orig_max_ss - p.orig_min_ss +1;
		f_count[1] = p.orig_max_fs - p.orig_min_fs +1;

		dh_dataspace = H5Dget_space(dh);
		r = H5Sselect_hyperslab(dh_dataspace, H5S_SELECT_SET,
		                        f_offset, NULL, f_count, NULL);
		if ( r < 0 ) {
			ERROR("Error selecting file dataspace "
			      "for panel %s\n", p.name);
			H5Pclose(ph);
			H5Dclose(dh);
			H5Sclose(dh_dataspace);
			H5Sclose(sh);
			H5Fclose(fh);
			return;
		}

		dims[0] = p.h;
		dims[1] = p.w;
		memspace = H5Screate_simple(2, dims, NULL);

		r = H5Dwrite(dh, H5T_NATIVE_FLOAT, memspace, dh_dataspace,
		             H5P_DEFAULT, dp[loc->panel_idxs[pi]]);
		if ( r < 0 ) {
			ERROR("Couldn't write data\n");
			H5Pclose(ph);
			H5Dclose(dh);
			H5Sclose(dh_dataspace);
			H5Sclose(memspace);
			H5Sclose(sh);
			H5Fclose(fh);
			return;
		}

		H5Sclose(dh_dataspace);
		H5Sclose(memspace);
	}
	H5Pclose(ph);
	H5Sclose(sh);
	H5Dclose(dh);
}


static void write_photon_energy(hid_t fh, double eV, const char *ph_en_loc)
{
	hid_t ph, sh, dh;
	hsize_t size1d[1];
	int r;

	ph = H5Pcreate(H5P_LINK_CREATE);
	H5Pset_create_intermediate_group(ph, 1);

	size1d[0] = 1;
	sh = H5Screate_simple(1, size1d, NULL);

	dh = H5Dcreate2(fh, ph_en_loc, H5T_NATIVE_DOUBLE, sh,
	                ph, H5S_ALL, H5P_DEFAULT);
	if ( dh < 0 ) {
		ERROR("Couldn't create dataset for photon energy.\n");
		return;
	}
	r = H5Dwrite(dh, H5T_NATIVE_DOUBLE, H5S_ALL, H5S_ALL, H5P_DEFAULT, &eV);
	if ( r < 0 ) {
		ERROR("Couldn't write photon energy.\n");
		/* carry on */
	}

	H5Pclose(ph);
	H5Dclose(dh);
}


static void write_spectrum(hid_t fh, Spectrum *s)
{
	herr_t r;
	double *arr;
	int i;
	hid_t sh, dh, ph;
	double kmin, kmax, step;
	const hsize_t n = 1024;

	ph = H5Pcreate(H5P_LINK_CREATE);
	H5Pset_create_intermediate_group(ph, 1);

	arr = malloc(n*sizeof(double));
	if ( arr == NULL ) {
		ERROR("Failed to allocate memory for spectrum.\n");
		return;
	}

	/* Save the wavelength values */
	spectrum_get_range(s, &kmin, &kmax);
	step = (kmax-kmin)/n;
	for ( i=0; i<n; i++ ) {
		arr[i] = 1.0e10/(kmin+i*step);
	}

	sh = H5Screate_simple(1, &n, NULL);

	dh = H5Dcreate2(fh, "/spectrum/wavelengths_A", H5T_NATIVE_DOUBLE,
	                sh, ph, H5S_ALL, H5P_DEFAULT);
	if ( dh < 0 ) {
		ERROR("Failed to create dataset for spectrum wavelengths.\n");
		return;
	}
	r = H5Dwrite(dh, H5T_NATIVE_DOUBLE, H5S_ALL,
		     H5S_ALL, H5P_DEFAULT, arr);
	if ( r < 0 ) {
		ERROR("Failed to write spectrum wavelengths.\n");
		return;
	}
	H5Dclose(dh);

	/* Save the probability density values */
	for ( i=0; i<n; i++ ) {
		arr[i] = spectrum_get_density_at_k(s, kmin+i*step);
	}

	dh = H5Dcreate2(fh, "/spectrum/pdf", H5T_NATIVE_DOUBLE, sh,
		        H5P_DEFAULT, H5S_ALL, H5P_DEFAULT);
	if ( dh < 0 ) {
		ERROR("Failed to create dataset for spectrum p.d.f.\n");
		return;
	}
	r = H5Dwrite(dh, H5T_NATIVE_DOUBLE, H5S_ALL,
		     H5S_ALL, H5P_DEFAULT, arr);
	if ( r < 0 ) {
		ERROR("Failed to write spectrum p.d.f.\n");
		return;
	}

	H5Dclose(dh);
	H5Pclose(ph);
	free(arr);
}


int hdf5_write_image(const char *filename, const struct image *image,
                     char *element)
{
	hid_t fh;
	int li;
	char *default_location;
	struct hdf5_write_location *locations;
	int num_locations;
	const char *ph_en_loc;

	if ( image->det == NULL ) {
		ERROR("Geometry not available\n");
		return 1;
	}

	fh = H5Fcreate(filename, H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT);
	if ( fh < 0 ) {
		ERROR("Couldn't create file: %s\n", filename);
		return 1;
	}

	if ( element != NULL ) {
		default_location = strdup(element);
	} else {
		default_location = strdup("/data/data");
	}

	locations = make_location_list(image->det, default_location,
	                               &num_locations);

	for ( li=0; li<num_locations; li++ ) {
		write_location(fh, image->det, image->dp, &locations[li]);
	}

	if ( image->beam == NULL
	 || (image->beam != NULL && image->beam->photon_energy_from == NULL) ) {
		ph_en_loc = "photon_energy_eV";
	} else {
		ph_en_loc = image->beam->photon_energy_from;
	}

	write_photon_energy(fh, ph_lambda_to_eV(image->lambda), ph_en_loc);

	if ( image->spectrum != NULL ) {
		write_spectrum(fh, image->spectrum);
	}

	H5Fclose(fh);
	free(default_location);
	for ( li=0; li<num_locations; li ++ ) {
		free(locations[li].panel_idxs);
	}
	free(locations);
	return 0;
}


static void debodge_saturation(struct hdfile *f, struct image *image)
{
	hid_t dh, sh;
	hsize_t size[2];
	hsize_t max_size[2];
	int i;
	float *buf;
	herr_t r;

	dh = H5Dopen2(f->fh, "/processing/hitfinder/peakinfo_saturated",
	              H5P_DEFAULT);

	if ( dh < 0 ) {
		/* This isn't an error */
		return;
	}

	sh = H5Dget_space(dh);
	if ( sh < 0 ) {
		H5Dclose(dh);
		ERROR("Couldn't get dataspace for saturation table.\n");
		return;
	}

	if ( H5Sget_simple_extent_ndims(sh) != 2 ) {
		H5Sclose(sh);
		H5Dclose(dh);
		return;
	}

	H5Sget_simple_extent_dims(sh, size, max_size);

	if ( size[1] != 3 ) {
		H5Sclose(sh);
		H5Dclose(dh);
		ERROR("Saturation table has the wrong dimensions.\n");
		return;
	}

	buf = malloc(sizeof(float)*size[0]*size[1]);
	if ( buf == NULL ) {
		H5Sclose(sh);
		H5Dclose(dh);
		ERROR("Couldn't reserve memory for saturation table.\n");
		return;
	}
	r = H5Dread(dh, H5T_NATIVE_FLOAT, H5S_ALL, H5S_ALL, H5P_DEFAULT, buf);
	if ( r < 0 ) {
		ERROR("Couldn't read saturation table.\n");
		free(buf);
		return;
	}

	for ( i=0; i<size[0]; i++ ) {

		unsigned int fs, ss;
		float val;
		struct panel *p;
		signed int pn;

		fs = buf[3*i+0];
		ss = buf[3*i+1];
		val = buf[3*i+2];

		/* Turn "original" position into "panel" position */
		pn = find_orig_panel_number(image->det, fs, ss);
		if ( pn == -1 ) {
			ERROR("Failed to find panel!\n");
			continue;
		}
		p = &image->det->panels[pn];

		image->dp[pn][fs+p->w*ss] = val/5.0;
		image->dp[pn][fs+1+p->w*ss] = val/5.0;
		image->dp[pn][fs-1+p->w*ss] = val/5.0;
		image->dp[pn][fs+p->w*(ss-1)] = val/5.0;
		image->dp[pn][fs+p->w*(ss+1)] = val/5.0;

	}

	free(buf);
	H5Sclose(sh);
	H5Dclose(dh);
}


static int *make_badmask(int *flags, struct detector *det, float *data,
                         struct panel *p)
{
	int *badmap;
	int fs, ss;

	badmap = malloc(p->w*p->h*sizeof(int));
	if ( badmap == NULL ) {
		ERROR("Failed to allocate bad mask for panel %s\n",
		      p->name);
		return NULL;
	}

	/* Defaults, then bad pixels arising from bad regions or panels */
	for ( ss=0; ss<p->h; ss++ ) {
	for ( fs=0; fs<p->w; fs++ ) {

		int bad = 0;

		if ( p->no_index ) bad = 1;

		if ( in_bad_region(det, p, fs, ss) ) {
			bad = 1;
		}

		badmap[fs+p->w*ss] = bad;
	}
	}

	/* Bad pixels from mask */
	if ( flags != NULL ) {
		for ( ss=0; ss<p->h; ss++ ) {
		for ( fs=0; fs<p->w; fs++ ) {

			int f = flags[fs+p->w*ss];
			int bad = badmap[fs+p->w*ss];
			float val = data[fs+p->w*ss];

			/* Bad if it's missing any of the "good" bits */
			if ( (f & det->mask_good) != det->mask_good ) bad = 1;

			/* Bad if it has any of the "bad" bits. */
			if ( f & det->mask_bad ) bad = 1;

			/* Bad if pixel value is NaN or inf */
			if ( isnan(val) || isinf(val) ) bad = 1;

			badmap[fs+p->w*ss] = bad;

		}
		}
	}

	return badmap;
}


int hdfile_get_value(struct hdfile *f, const char *name, struct event *ev,
                     void *val, hid_t memtype)
{
	hid_t dh;
	hid_t type;
	hid_t class;
	hid_t sh;
	hid_t ms;
	hsize_t *f_offset = NULL;
	hsize_t *f_count = NULL;
	hsize_t m_offset[1];
	hsize_t m_count[1];
	hsize_t msdims[1];
	hsize_t size[3];
	herr_t r;
	herr_t check;
	int check_pe;
	int dim_flag;
	int ndims;
	int i;
	char *subst_name = NULL;

	if ( (ev != NULL) && (ev->path_length != 0) ) {
		subst_name = retrieve_full_path(ev, name);
	} else {
		subst_name = strdup(name);
	}

	check_pe = check_path_existence(f->fh, subst_name);
	if ( check_pe == 0 ) {
		ERROR("No such event-based float field '%s'\n", subst_name);
		return 1;
	}

	dh = H5Dopen2(f->fh, subst_name, H5P_DEFAULT);
	type = H5Dget_type(dh);
	class = H5Tget_class(type);

	if ( (class != H5T_FLOAT) && (class != H5T_INTEGER) ) {
		ERROR("Not a floating point or integer value.\n");
		H5Tclose(type);
		H5Dclose(dh);
		return 1;
	}

	/* Get the dimensionality.  We have to cope with scalars expressed as
	 * arrays with all dimensions 1, as well as zero-d arrays. */
	sh = H5Dget_space(dh);
	ndims = H5Sget_simple_extent_ndims(sh);
	if ( ndims > 3 ) {
		H5Tclose(type);
		H5Dclose(dh);
		return 1;
	}
	H5Sget_simple_extent_dims(sh, size, NULL);

	m_offset[0] = 0;
	m_count[0] = 1;
	msdims[0] = 1;
	ms = H5Screate_simple(1,msdims,NULL);

	/* Check that the size in all dimensions is 1
	 * or that one of the dimensions has the same
	 * size as the hyperplane events */

	dim_flag = 0;

	for ( i=0; i<ndims; i++ ) {
		if ( size[i] == 1 ) continue;
		if ( ( i==0 ) && (ev != NULL) && (size[i] > ev->dim_entries[0]) ) {
			dim_flag = 1;
		} else {
			H5Tclose(type);
			H5Dclose(dh);
			return 1;
		}
	}

	if ( dim_flag == 0 ) {

		if ( H5Dread(dh, memtype, H5S_ALL, H5S_ALL, H5P_DEFAULT, val) < 0 ) {
			ERROR("Couldn't read value.\n");
			H5Tclose(type);
			H5Dclose(dh);
			return 1;
		}

	} else {

		f_offset = malloc(ndims*sizeof(hsize_t));
		f_count = malloc(ndims*sizeof(hsize_t));

		for ( i=0; i<ndims; i++ ) {

			if ( i == 0 ) {
				f_offset[i] = ev->dim_entries[0];
				f_count[i] = 1;
			} else {
				f_offset[i] = 0;
				f_count[i] = 0;
			}

		}

		check = H5Sselect_hyperslab(sh, H5S_SELECT_SET,
		                            f_offset, NULL, f_count, NULL);
		if ( check <0 ) {
			ERROR("Error selecting dataspace for float value");
			free(f_offset);
			free(f_count);
			return 1;
		}

		ms = H5Screate_simple(1,msdims,NULL);
		check = H5Sselect_hyperslab(ms, H5S_SELECT_SET,
		                            m_offset, NULL, m_count, NULL);
		if ( check < 0 ) {
			ERROR("Error selecting memory dataspace for float value");
			free(f_offset);
			free(f_count);
			return 1;
		}

		r = H5Dread(dh, memtype, ms, sh, H5P_DEFAULT, val);
		if ( r < 0 )  {
			ERROR("Couldn't read value.\n");
			H5Tclose(type);
			H5Dclose(dh);
			return 1;
		}

	}

	free(subst_name);

	return 0;
}


static void hdfile_fill_in_beam_parameters(struct beam_params *beam,
                                           struct hdfile *f,
                                           struct event *ev,
                                           struct image *image)
{
	double eV;

	if ( beam->photon_energy_from == NULL ) {

		/* Explicit value given */
		eV = beam->photon_energy;

	} else {

		int r;

		r = hdfile_get_value(f, beam->photon_energy_from,
		                     ev, &eV, H5T_NATIVE_DOUBLE);
		if ( r ) {
			ERROR("Failed to read '%s'\n",
			      beam->photon_energy_from);
		}

	}

	image->lambda = ph_en_to_lambda(eV_to_J(eV))*beam->photon_energy_scale;
}


static void hdfile_fill_in_clen(struct detector *det, struct hdfile *f,
                                struct event *ev)
{
	int i;

	for ( i=0; i<det->n_panels; i++ ) {

		struct panel *p = &det->panels[i];

		if ( p->clen_from != NULL ) {

			double val;
			int r;

			r = hdfile_get_value(f, p->clen_from, ev, &val,
			                     H5T_NATIVE_DOUBLE);
			if ( r ) {
				ERROR("Failed to read '%s'\n", p->clen_from);
			} else {
				p->clen = val * 1.0e-3;
			}

		}

		adjust_centering_for_rail(p);

	}
}


int hdf5_read(struct hdfile *f, struct image *image, const char *element,
              int satcorr)
{
	herr_t r;
	float *buf;
	int fail;
	hsize_t *size;
	hsize_t *max_size;
	hid_t sh;
	int sh_dim;
	int w, h;

	if ( element == NULL ) {
		fail = hdfile_set_first_image(f, "/");
	} else {
		fail = hdfile_set_image(f, element);
	}

	if ( fail ) {
		ERROR("Couldn't select path\n");
		return 1;
	}

	sh = H5Dget_space(f->dh);
	sh_dim = H5Sget_simple_extent_ndims(sh);

	if ( sh_dim != 2 ) {
		ERROR("Dataset is not two-dimensional\n");
		return -1;
	}

	size = malloc(sh_dim*sizeof(hsize_t));
	max_size = malloc(sh_dim*sizeof(hsize_t));
	H5Sget_simple_extent_dims(sh, size, max_size);
	H5Sclose(sh);
	w = size[1];
	h = size[0];
	free(size);
	free(max_size);

	buf = malloc(sizeof(float)*w*h);
	r = H5Dread(f->dh, H5T_NATIVE_FLOAT, H5S_ALL, H5S_ALL,
	            H5P_DEFAULT, buf);
	if ( r < 0 ) {
		ERROR("Couldn't read data\n");
		free(buf);
		return 1;
	}

	if ( image->det != NULL ) {
		ERROR("WARNING: hdf5_read() called with geometry structure.\n");
	}
	image->det = simple_geometry(image, w, h);
	image->dp = malloc(sizeof(double *));
	if ( image->dp == NULL ) {
		ERROR("Failed to allocate memory for image data!\n");
		return 1;
	}
	image->dp[0] = buf;

	if ( satcorr ) debodge_saturation(f, image);

	if ( image->beam != NULL ) {

		hdfile_fill_in_beam_parameters(image->beam, f, NULL, image);

		if ( image->lambda > 1000 ) {
			/* Error message covers a silly value in the beam file
			 * or in the HDF5 file. */
			ERROR("WARNING: Missing or nonsensical wavelength "
			      "(%e m) for %s.\n",
			      image->lambda, image->filename);
		}

	}

	fill_in_adu(image);

	return 0;
}


static hsize_t *first_two_dims(hsize_t *in, struct dim_structure *ds)
{
	int i, j;
	hsize_t *out = malloc(2*sizeof(hsize_t));

	if ( out == NULL ) return NULL;

	j = 0;
	for ( i=0; i<ds->num_dims; i++ ) {
		if ( (ds->dims[i] == HYSL_FS) || (ds->dims[i] == HYSL_SS) ) {
			out[j++] = in[i];
		}
	}
	return out;
}


static int load_satmap(struct hdfile *f, struct event *ev, struct panel *p,
                       hsize_t *in_f_offset, hsize_t *in_f_count,
                       struct dim_structure *dim_struct,
                       float *satmap)
{
	char *loc;  /* Sat map location after possible substitution */
	hid_t satmap_dataspace, satmap_dh;
	int exists;
	int check, r;
	hid_t memspace;
	hsize_t dimsm[2];
	hid_t fh;
	hsize_t *f_offset, *f_count;

	if ( p->satmap_file != NULL ) {

		fh = H5Fopen(p->satmap_file, H5F_ACC_RDONLY, H5P_DEFAULT);
		if ( fh < 0 ) {
			ERROR("Couldn't open satmap file '%s'\n", p->satmap_file);
			return 1;
		}

		/* If we have an external map file, we assume it to be a simple
		 * 2D job */
		f_offset = first_two_dims(in_f_offset, dim_struct);
		f_count = first_two_dims(in_f_count, dim_struct);

	} else {

		/* Otherwise, we assume it has the same dimensions as the
		 * image data itself */
		fh = f->fh;
		f_offset = in_f_offset;
		f_count = in_f_count;
	}

	if ( ev != NULL ) {
		loc = retrieve_full_path(ev, p->satmap);
	} else {
		loc = strdup(p->satmap);
	}

	exists = check_path_existence(fh, loc);
	if ( !exists ) {
		ERROR("Cannot find satmap for panel %s\n", p->name);
		goto err;
	}

	satmap_dh = H5Dopen2(fh, loc, H5P_DEFAULT);
	if ( satmap_dh <= 0 ) {
		ERROR("Couldn't open satmap for panel %s\n", p->name);
		goto err;
	}

	satmap_dataspace = H5Dget_space(satmap_dh);
	check = H5Sselect_hyperslab(satmap_dataspace, H5S_SELECT_SET,
	                            f_offset, NULL, f_count, NULL);
	if ( check < 0 ) {
		ERROR("Error selecting satmap dataspace for panel %s\n",
		      p->name);
		goto err;
	}

	dimsm[0] = p->h;
	dimsm[1] = p->w;
	memspace = H5Screate_simple(2, dimsm, NULL);
	if ( check < 0 ) {
		ERROR("Error selecting satmap memory dataspace for panel %s\n",
		      p->name);
		goto err;
	}

	r = H5Dread(satmap_dh, H5T_NATIVE_FLOAT, memspace,
	            satmap_dataspace, H5P_DEFAULT, satmap);
	if ( r < 0 ) {
		ERROR("Couldn't read satmap for panel %s\n", p->name);
		goto err;
	}

	H5Sclose(satmap_dataspace);
	H5Dclose(satmap_dh);
	free(loc);

	return 0;

err:
	if ( p->satmap_file != NULL ) H5Fclose(fh);
	free(loc);
	return 1;
}



static int load_mask(struct hdfile *f, struct event *ev, struct panel *p,
                     int *flags,
                     hsize_t *in_f_offset, hsize_t *in_f_count,
                     struct dim_structure *dim_struct)
{
	char *mask;  /* Mask location after possible substitution */
	hid_t mask_dataspace, mask_dh;
	int exists;
	int check, r;
	hid_t memspace;
	hsize_t dimsm[2];
	hid_t fh;
	hsize_t *f_offset, *f_count;

	if ( p->mask_file != NULL ) {

		fh = H5Fopen(p->mask_file, H5F_ACC_RDONLY, H5P_DEFAULT);
		if ( fh < 0 ) {
			ERROR("Couldn't open mask file '%s'\n", p->mask_file);
			return 1;
		}

		/* If we have an external map file, we assume it to be a simple
		 * 2D job */
		f_offset = first_two_dims(in_f_offset, dim_struct);
		f_count = first_two_dims(in_f_count, dim_struct);

	} else {
		fh = f->fh;
		f_offset = in_f_offset;
		f_count = in_f_count;
	}

	if ( ev != NULL ) {
		mask = retrieve_full_path(ev, p->mask);
	} else {
		mask = strdup(p->mask);
	}

	exists = check_path_existence(fh, mask);
	if ( !exists ) {
		ERROR("Cannot find flags for panel %s\n", p->name);
		goto err;
	}

	mask_dh = H5Dopen2(fh, mask, H5P_DEFAULT);
	if ( mask_dh <= 0 ) {
		ERROR("Couldn't open flags for panel %s\n", p->name);
		goto err;
	}

	mask_dataspace = H5Dget_space(mask_dh);
	check = H5Sselect_hyperslab(mask_dataspace, H5S_SELECT_SET,
	                            f_offset, NULL, f_count, NULL);
	if ( check < 0 ) {
		ERROR("Error selecting mask dataspace for panel %s\n", p->name);
		goto err;
	}

	dimsm[0] = p->h;
	dimsm[1] = p->w;
	memspace = H5Screate_simple(2, dimsm, NULL);
	if ( check < 0 ) {
		ERROR("Error selecting memory dataspace for panel %s\n", p->name);
		goto err;
	}

	r = H5Dread(mask_dh, H5T_NATIVE_INT, memspace,
	            mask_dataspace, H5P_DEFAULT, flags);
	if ( r < 0 ) {
		ERROR("Couldn't read flags for panel %s\n", p->name);
		goto err;
	}

	H5Sclose(mask_dataspace);
	H5Dclose(mask_dh);
	free(mask);

	return 0;

err:
	if ( p->mask_file != NULL ) H5Fclose(fh);
	free(mask);
	return 1;
}


int hdf5_read2(struct hdfile *f, struct image *image, struct event *ev,
               int satcorr)
{
	herr_t r;
	int pi;
	int i;

	if ( image->det == NULL ) {
		ERROR("Geometry not available\n");
		return 1;
	}

	image->dp = malloc(image->det->n_panels*sizeof(float *));
	image->bad = malloc(image->det->n_panels*sizeof(int *));
	image->sat = malloc(image->det->n_panels*sizeof(float *));
	if ( (image->dp==NULL) || (image->bad==NULL) || (image->sat==NULL) ) {
		ERROR("Failed to allocate data arrays.\n");
		return 1;
	}

	for ( pi=0; pi<image->det->n_panels; pi++ ) {

		hsize_t *f_offset, *f_count;
		int hsi;
		struct dim_structure *hsd;
		herr_t check;
		hid_t dataspace, memspace;
		int fail;
		struct panel *p;
		hsize_t dims[2];

		p = &image->det->panels[pi];

		if ( ev != NULL ) {

			int exists;
			char *panel_full_path;

			panel_full_path = retrieve_full_path(ev, p->data);

			exists = check_path_existence(f->fh, panel_full_path);
			if ( !exists ) {
				ERROR("Cannot find data for panel %s\n",
				      p->name);
				free(image->dp);
				free(image->bad);
				free(image->sat);
				return 1;
			}

			fail = hdfile_set_image(f, panel_full_path);

			free(panel_full_path);

		} else {

			if ( p->data == NULL ) {

				fail = hdfile_set_first_image(f, "/");

			} else {

				int exists;
				exists = check_path_existence(f->fh, p->data);
				if ( !exists ) {
					ERROR("Cannot find data for panel %s\n",
					      p->name);
					free(image->dp);
					free(image->bad);
					free(image->sat);
					return 1;
				}
				fail = hdfile_set_image(f, p->data);

			}

		}
		if ( fail ) {
			ERROR("Couldn't select path for panel %s\n",
			      p->name);
			free(image->dp);
			free(image->bad);
			free(image->sat);
			return 1;
		}

		/* Determine where to read the data from in the file */
		hsd = image->det->panels[pi].dim_structure;
		f_offset = malloc(hsd->num_dims*sizeof(hsize_t));
		f_count = malloc(hsd->num_dims*sizeof(hsize_t));
		if ( (f_offset == NULL) || (f_count == NULL ) ) {
			ERROR("Failed to allocate offset or count.\n");
			free(image->dp);
			free(image->bad);
			free(image->sat);
			return 1;
		}
		for ( hsi=0; hsi<hsd->num_dims; hsi++ ) {

			if ( hsd->dims[hsi] == HYSL_FS ) {
				f_offset[hsi] = p->orig_min_fs;
				f_count[hsi] = p->orig_max_fs - p->orig_min_fs+1;
			} else if ( hsd->dims[hsi] == HYSL_SS ) {
				f_offset[hsi] = p->orig_min_ss;
				f_count[hsi] = p->orig_max_ss - p->orig_min_ss+1;
			} else if (hsd->dims[hsi] == HYSL_PLACEHOLDER ) {
				f_offset[hsi] = ev->dim_entries[0];
				f_count[hsi] = 1;
			} else {
				f_offset[hsi] = hsd->dims[hsi];
				f_count[hsi] = 1;
			}

		}

		/* Set up dataspace for file */
		dataspace = H5Dget_space(f->dh);
		check = H5Sselect_hyperslab(dataspace, H5S_SELECT_SET,
		                            f_offset, NULL, f_count, NULL);
		if ( check < 0 ) {
			ERROR("Error selecting file dataspace for panel %s\n",
			      p->name);
			free(image->dp);
			free(image->bad);
			free(image->sat);
			return 1;
		}

		dims[0] = p->h;
		dims[1] = p->w;
		memspace = H5Screate_simple(2, dims, NULL);

		image->dp[pi] = malloc(p->w*p->h*sizeof(float));
		image->sat[pi] = malloc(p->w*p->h*sizeof(float));
		if ( (image->dp[pi] == NULL) || (image->sat[pi] == NULL) ) {
			ERROR("Failed to allocate panel %s\n", p->name);
			free(f_offset);
			free(f_count);
			free(image->dp);
			free(image->bad);
			free(image->sat);
			return 1;
		}
		for ( i=0; i<p->w*p->h; i++ ) image->sat[pi][i] = INFINITY;

		r = H5Dread(f->dh, H5T_NATIVE_FLOAT, memspace, dataspace,
		            H5P_DEFAULT, image->dp[pi]);
		if ( r < 0 ) {
			ERROR("Couldn't read data for panel %s\n",
			      p->name);
			free(f_offset);
			free(f_count);
			for ( i=0; i<=pi; i++ ) {
				free(image->dp[i]);
				free(image->sat[i]);
			}
			free(image->dp);
			free(image->bad);
			free(image->sat);
			return 1;
		}

		if ( p->mask != NULL ) {
			int *flags = malloc(p->w*p->h*sizeof(int));
			if ( !load_mask(f, ev, p, flags, f_offset, f_count, hsd) ) {
				image->bad[pi] = make_badmask(flags, image->det,
				                              image->dp[pi], p);
			} else {
				image->bad[pi] = make_badmask(NULL, image->det,
				                              image->dp[pi], p);
			}
			free(flags);
		} else {
			image->bad[pi] = make_badmask(NULL, image->det,
			                              image->dp[pi], p);
		}

		if ( p->satmap != NULL ) {
			if ( load_satmap(f, ev, p, f_offset, f_count, hsd,
			                 image->sat[pi]) )
			{
				ERROR("Failed to load sat map for panel %s\n",
				      p->name);
			}
		}

		H5Sclose(dataspace);
		free(f_offset);
		free(f_count);

	}

	H5Dclose(f->dh);
	f->data_open = 0;
	hdfile_fill_in_clen(image->det, f, ev);

	if ( satcorr ) debodge_saturation(f, image);

	if ( image->beam != NULL ) {

		hdfile_fill_in_beam_parameters(image->beam, f, ev, image);

		if ( (image->lambda > 1.0) || (image->lambda < 1e-20) ) {

			ERROR("WARNING: Nonsensical wavelength (%e m) value "
			      "for file: %s, event: %s.\n",
			      image->lambda, image->filename,
			      get_event_string(image->event));
		}

	}

	fill_in_adu(image);

	return 0;
}


static int looks_like_image(hid_t h)
{
	hid_t sh;
	hsize_t size[2];
	hsize_t max_size[2];

	sh = H5Dget_space(h);
	if ( sh < 0 ) return 0;

	if ( H5Sget_simple_extent_ndims(sh) != 2 ) {
		H5Sclose(sh);
		return 0;
	}

	H5Sget_simple_extent_dims(sh, size, max_size);
	H5Sclose(sh);

	if ( ( size[0] > 64 ) && ( size[1] > 64 ) ) return 1;

	return 0;
}


int hdfile_is_scalar(struct hdfile *f, const char *name, int verbose)
{
	hid_t dh;
	hid_t sh;
	hsize_t size[3];
	hid_t type;
	int ndims;
	int i;
	int check;

	check = check_path_existence(f->fh, name);
	if ( check == 0 ) {
		ERROR("No such scalar field '%s'\n", name);
		return 0;
	}

	dh = H5Dopen2(f->fh, name, H5P_DEFAULT);
	type = H5Dget_type(dh);

	/* Get the dimensionality.  We have to cope with scalars expressed as
	 * arrays with all dimensions 1, as well as zero-d arrays. */
	sh = H5Dget_space(dh);
	ndims = H5Sget_simple_extent_ndims(sh);
	if ( ndims > 3 ) {
		if ( verbose ) {
			ERROR("Too many dimensions (%i).\n", ndims);
		}
		H5Sclose(sh);
		H5Tclose(type);
		H5Dclose(dh);
		return 0;
	}

	/* Check that the size in all dimensions is 1 */
	H5Sget_simple_extent_dims(sh, size, NULL);
	H5Sclose(sh);
	H5Tclose(type);
	H5Dclose(dh);
	for ( i=0; i<ndims; i++ ) {
		if ( size[i] != 1 ) {
			if ( verbose ) {
				ERROR("%s not a scalar value (ndims=%i,"
				      "size[%i]=%i)\n",
				      name, ndims, i, (int)size[i]);
			}
			return 0;
		}
	}

	return 1;
}


struct copy_hdf5_field
{
	char **fields;
	int n_fields;
	int max_fields;
};


struct copy_hdf5_field *new_copy_hdf5_field_list()
{
	struct copy_hdf5_field *n;

	n = calloc(1, sizeof(struct copy_hdf5_field));
	if ( n == NULL ) return NULL;

	n->max_fields = 32;
	n->fields = malloc(n->max_fields*sizeof(char *));
	if ( n->fields == NULL ) {
		free(n);
		return NULL;
	}

	return n;
}


void free_copy_hdf5_field_list(struct copy_hdf5_field *n)
{
	int i;
	for ( i=0; i<n->n_fields; i++ ) {
		free(n->fields[i]);
	}
	free(n->fields);
	free(n);
}


void add_copy_hdf5_field(struct copy_hdf5_field *copyme,
                         const char *name)
{
	int i;

	/* Already on the list?   Don't re-add if so. */
	for ( i=0; i<copyme->n_fields; i++ ) {
		if ( strcmp(copyme->fields[i], name) == 0 ) return;
	}

	/* Need more space? */
	if ( copyme->n_fields == copyme->max_fields ) {

		char **nfields;
		int nmax = copyme->max_fields + 32;

		nfields = realloc(copyme->fields, nmax*sizeof(char *));
		if ( nfields == NULL ) {
			ERROR("Failed to allocate space for new HDF5 field.\n");
			return;
		}

		copyme->max_fields = nmax;
		copyme->fields = nfields;

	}

	copyme->fields[copyme->n_fields] = strdup(name);
	if ( copyme->fields[copyme->n_fields] == NULL ) {
		ERROR("Failed to add field for copying '%s'\n", name);
		return;
	}

	copyme->n_fields++;
}


void copy_hdf5_fields(struct hdfile *f, const struct copy_hdf5_field *copyme,
                      FILE *fh, struct event *ev)
{
	int i;

	if ( copyme == NULL ) return;

	for ( i=0; i<copyme->n_fields; i++ ) {

		char *val;
		char *field;

		field = copyme->fields[i];
		val = hdfile_get_string_value(f, field, ev);

		if ( field[0] == '/' ) {
			fprintf(fh, "hdf5%s = %s\n", field, val);
		} else {
			fprintf(fh, "hdf5/%s = %s\n", field, val);
		}

		free(val);

	}
}


char *hdfile_get_string_value(struct hdfile *f, const char *name,
                              struct event *ev)
{
	hid_t dh;
	hsize_t size;
	hid_t type;
	hid_t class;
	int buf_i;
	double buf_f;
	char *tmp = NULL, *subst_name = NULL;

	if (ev != NULL && ev->path_length != 0 ) {
		subst_name = retrieve_full_path(ev, name);
	} else {
		subst_name = strdup(name);
	}

	dh = H5Dopen2(f->fh, subst_name, H5P_DEFAULT);
	if ( dh < 0 ) {
		free(subst_name);
		return NULL;
	}
	type = H5Dget_type(dh);
	class = H5Tget_class(type);

	if ( class == H5T_STRING ) {

		herr_t r;
		hid_t sh;
		htri_t v;

		v = H5Tis_variable_str(type);
		if ( v < 0 ) {
			H5Tclose(type);
			free(subst_name);
			return "WTF?";
		}

		if ( v && (v>0) ) {

			r = H5Dread(dh, type, H5S_ALL, H5S_ALL,
			            H5P_DEFAULT, &tmp);
			if ( r < 0 ) {
				H5Tclose(type);
				free(subst_name);
				return NULL;
			}

			return tmp;

		} else {

			size = H5Tget_size(type);
			tmp = malloc(size+1);

			sh = H5Screate(H5S_SCALAR);

			r = H5Dread(dh, type, sh, H5S_ALL, H5P_DEFAULT, tmp);
			H5Sclose(sh);
			if ( r < 0 ) {
				free(tmp);
				free(subst_name);
				return NULL;
			} else {

				/* Two possibilities:
				 *   String is already zero-terminated
				 *   String is not terminated.
				 * Make sure things are done properly... */
				tmp[size] = '\0';
				chomp(tmp);
				H5Dclose(dh);
				free(subst_name);
				return tmp;
			}

		}

	} else {

		int r;

		H5Dclose(dh);
		H5Tclose(type);

		switch ( class ) {

			case H5T_FLOAT :
			r = hdfile_get_value(f, subst_name, ev, &buf_f,
			                     H5T_NATIVE_DOUBLE);
			free(subst_name);
			if ( r == 0 ) {
				tmp = malloc(256);
				if ( tmp == NULL ) {
					ERROR("Failed to allocate float\n");
					return NULL;
				}
				snprintf(tmp, 255, "%f", buf_f);
				return tmp;
			} else {
				ERROR("Failed to read value\n");
				return NULL;
			}
			break;

			case H5T_INTEGER :
			r = hdfile_get_value(f, subst_name, ev, &buf_i,
			                     H5T_NATIVE_INT);
			free(subst_name);
			if ( r == 0 ) {
				tmp = malloc(256);
				if ( tmp == NULL ) {
					ERROR("Failed to allocate int buf!\n");
					return NULL;
				}
				snprintf(tmp, 255, "%d", buf_i);
				return tmp;

			} else {
				ERROR("Failed to read value\n");
				return NULL;
			}
			break;

			default :
			ERROR("Don't know what to do!\n");
			free(subst_name);
			return NULL;
		}

	}
}


char **hdfile_read_group(struct hdfile *f, int *n, const char *parent,
                         int **p_is_group, int **p_is_image)
{
	hid_t gh;
	hsize_t num;
	char **res;
	int i;
	int *is_group;
	int *is_image;
	H5G_info_t ginfo;

	gh = H5Gopen2(f->fh, parent, H5P_DEFAULT);
	if ( gh < 0 ) {
		*n = 0;
		return NULL;
	}

	if ( H5Gget_info(gh, &ginfo) < 0 ) {
		/* Whoopsie */
		*n = 0;
		return NULL;
	}
	num = ginfo.nlinks;
	*n = num;
	if ( num == 0 ) return NULL;

	res = malloc(num*sizeof(char *));
	is_image = malloc(num*sizeof(int));
	is_group = malloc(num*sizeof(int));
	*p_is_image = is_image;
	*p_is_group = is_group;

	for ( i=0; i<num; i++ ) {

		char buf[256];
		hid_t dh;
		H5I_type_t type;

		H5Lget_name_by_idx(gh, ".", H5_INDEX_NAME, H5_ITER_NATIVE,
		                   i, buf, 255, H5P_DEFAULT);
		res[i] = malloc(512);
		if ( strlen(parent) > 1 ) {
			snprintf(res[i], 511, "%s/%s", parent, buf);
		} else {
			snprintf(res[i], 511, "%s%s", parent, buf);
		} /* ick */

		is_image[i] = 0;
		is_group[i] = 0;
		dh = H5Oopen(gh, buf, H5P_DEFAULT);
		if ( dh < 0 ) continue;
		type = H5Iget_type(dh);

		if ( type == H5I_GROUP ) {
			is_group[i] = 1;
		} else if ( type == H5I_DATASET ) {
			is_image[i] = looks_like_image(dh);
		}
		H5Oclose(dh);

	}

	H5Gclose(gh);

	return res;
}


int hdfile_set_first_image(struct hdfile *f, const char *group)
{
	char **names;
	int *is_group;
	int *is_image;
	int n, i, j;

	names = hdfile_read_group(f, &n, group, &is_group, &is_image);
	if ( n == 0 ) return 1;

	for ( i=0; i<n; i++ ) {

		if ( is_image[i] ) {
			hdfile_set_image(f, names[i]);
			for ( j=0; j<n; j++ ) free(names[j]);
			free(is_image);
			free(is_group);
			free(names);
			return 0;
		} else if ( is_group[i] ) {
			if ( !hdfile_set_first_image(f, names[i]) ) {
				for ( j=0; j<n; j++ ) free(names[j]);
				free(is_image);
				free(is_group);
				free(names);
				return 0;
			}
		}

	}

	for ( j=0; j<n; j++ ) free(names[j]);
	free(is_image);
	free(is_group);
	free(names);

	return 1;
}


struct parse_params {
	struct hdfile *hdfile;
	int path_dim;
	const char *path;
	struct event *curr_event;
	struct event_list *ev_list;
	int top_level;
};


int check_path_existence(hid_t fh, const char *path)
{

	char buffer[256];
	char buffer_full_path[2048];
	herr_t herrt;
	struct H5O_info_t ob_info;
	char *path_copy = strdup(path);
	char *start = path_copy;
	char *sep = NULL;

	buffer[0] = '\0';
	buffer_full_path[0] = '\0';

	if ( strcmp(path_copy, "/" ) == 0 ) {
		return 1;
	}

	do {

		int check;

		sep = strstr(start, "/");
		if ( sep != NULL && strlen(sep) == 1 ) {
			ERROR("Error: Data path ends with a / symbol\n");
			free(path_copy);
			return 1;
		}

		if ( sep != NULL ) {

			if ( sep == start ) {
				start = sep+1;
				strcat(buffer_full_path, "/");
				continue;
			}

			strncpy(buffer, start, sep-start);
			buffer[sep-start] = '\0';
			strcat(buffer_full_path, buffer);

			check = H5Lexists(fh, buffer_full_path, H5P_DEFAULT);
			if ( check == 0 ) {
				return 0;
			} else {
				herrt = H5Oget_info_by_name(fh, buffer_full_path,
				                            &ob_info,
				                            H5P_DEFAULT);
				if ( herrt < 0 ) {
					return -1;
				}
				if ( ob_info.type != H5O_TYPE_GROUP ) {
					return 0;
				}

				start = sep+1;
				strcat(buffer_full_path, "/");

			}

		} else {

			strcpy(buffer, start);
			strcat(buffer_full_path, buffer);

			check = H5Lexists(fh, buffer_full_path, H5P_DEFAULT);
			if ( check == 0 ) {
				return 0;
			}

		}
	} while (sep);

	free(path_copy);
	return 1;

}


static herr_t parse_file_event_structure(hid_t loc_id, char *name,
                                         const H5L_info_t *info,
                                         struct parse_params *pp)

{
	char *substituted_path;
	char *ph_loc;
	char *truncated_path;
	htri_t check;
	herr_t herrt_iterate, herrt_info;
	struct H5O_info_t object_info;

	if ( !pp->top_level ) {

		int fail_push;

		fail_push = push_path_entry_to_event(pp->curr_event, name);
		if ( fail_push ) {
			return -1;
		}

		substituted_path = event_path_placeholder_subst(name, pp->path);

	} else {
		substituted_path = strdup(pp->path);
	}

	if ( pp->top_level == 1 ) {
		pp->top_level = 0;
	}

	truncated_path = strdup(substituted_path);
	ph_loc = strstr(substituted_path,"%");
	if ( ph_loc != NULL) {
		truncated_path[ph_loc-substituted_path] = '\0';
	}

	herrt_iterate = 0;
	herrt_info = 0;

	check = check_path_existence(pp->hdfile->fh, truncated_path);
	if ( check == 0 ) {
			pop_path_entry_from_event(pp->curr_event);
			return 0;
	} else {

		herrt_info = H5Oget_info_by_name(pp->hdfile->fh, truncated_path,
                                         &object_info, H5P_DEFAULT);
		if ( herrt_info < 0 ) {
			free(truncated_path);
			free(substituted_path);
			return -1;
		}

		if ( pp->curr_event->path_length == pp->path_dim
		 &&  object_info.type == H5O_TYPE_DATASET )
		{

			int fail_append;

			fail_append = append_event_to_event_list(pp->ev_list,
			                                         pp->curr_event);
			if ( fail_append ) {
				free(truncated_path);
				free(substituted_path);
				return -1;
			}

			pop_path_entry_from_event(pp->curr_event);
			return 0;

		} else {

			pp->path = substituted_path;

			if ( object_info.type == H5O_TYPE_GROUP ) {

				herrt_iterate = H5Literate_by_name(pp->hdfile->fh,
				      truncated_path, H5_INDEX_NAME,
				      H5_ITER_NATIVE, NULL,
				      (H5L_iterate_t)parse_file_event_structure,
				      (void *)pp, H5P_DEFAULT);
			}
		}
	}

	pop_path_entry_from_event(pp->curr_event);

	free(truncated_path);
	free(substituted_path);

	return herrt_iterate;
}


static int fill_paths(struct hdfile *hdfile, struct detector *det, int pi,
                      struct event_list *master_el)
{
	struct parse_params pparams;
	struct event *empty_event;
	struct event_list *panel_ev_list;
	int ei;
	int check;

	empty_event = initialize_event();
	panel_ev_list = initialize_event_list();
	if ( (empty_event == NULL) || (panel_ev_list == NULL) )
	{
		ERROR("Failed to allocate memory for event list.\n");
		return 1;
	}

	pparams.path = det->panels[pi].data;
	pparams.hdfile = hdfile;
	pparams.path_dim = det->path_dim;
	pparams.curr_event = empty_event;
	pparams.top_level = 1;
	pparams.ev_list = panel_ev_list;

	check = parse_file_event_structure(hdfile->fh, NULL, NULL, &pparams);
	if ( check < 0 ) {
		free_event(empty_event);
		free_event_list(panel_ev_list);
		return 1;
	}

	for ( ei=0; ei<panel_ev_list->num_events; ei++ ) {

		int fail_add;

		fail_add = add_non_existing_event_to_event_list(master_el,
		                                     panel_ev_list->events[ei]);
		if ( fail_add ) {
			free_event(empty_event);
			free_event_list(panel_ev_list);
			return 1;
		}

	}

	free_event(empty_event);
	free_event_list(panel_ev_list);

	return 0;
}


static int check_dims(struct hdfile *hdfile, struct panel *p, struct event *ev,
                      struct event_list *events, int *global_path_dim)
{
	char *full_panel_path;
	hid_t dh;
	hid_t sh;
	int dims;
	hsize_t *size;
	hsize_t *max_size;
	int hsdi;
	int panel_path_dim = 0;
	struct dim_structure *panel_dim_structure;

	/* Get the full path for this panel in this event */
	full_panel_path = retrieve_full_path(ev, p->data);

	dh = H5Dopen2(hdfile->fh, full_panel_path, H5P_DEFAULT);
	if ( dh < 0 ) {
		ERROR("Error opening '%s'\n", full_panel_path);
		ERROR("Failed to enumerate events.  "
		      "Check your geometry file.\n");
		return 1;
	}

	sh = H5Dget_space(dh);
	dims = H5Sget_simple_extent_ndims(sh);
	size = malloc(dims*sizeof(hsize_t));
	max_size = malloc(dims*sizeof(hsize_t));
	if ( (size==NULL) || (max_size==NULL) ) {
		ERROR("Failed to allocate memory for dimensions\n");
		return 1;
	}

	dims = H5Sget_simple_extent_dims(sh, size, max_size);

	panel_dim_structure = p->dim_structure;
	for ( hsdi=0; hsdi<panel_dim_structure->num_dims; hsdi++ ) {
		if ( panel_dim_structure->dims[hsdi] == HYSL_PLACEHOLDER ) {
			panel_path_dim = size[hsdi];
			break;
		}
	}

	if ( *global_path_dim == -1 ) {

		*global_path_dim = panel_path_dim;

	} else if ( panel_path_dim != *global_path_dim ) {

		ERROR("All panels must have the same number of frames\n");
		ERROR("Panel %s has %i frames in one dimension, but the first "
		      "panel has %i.\n",
		      p->name, panel_path_dim, *global_path_dim);
		free(size);
		free(max_size);
		return 1;
	}

	H5Sclose(sh);
	H5Dclose(dh);

	return 0;
}


struct event_list *fill_event_list(struct hdfile *hdfile, struct detector *det)
{
	struct event_list *master_el;

	master_el = initialize_event_list();
	if ( master_el == NULL ) {
		ERROR("Failed to allocate event list.\n");
		return NULL;
	}

	/* First expand any placeholders in the HDF5 paths */
	if ( det->path_dim != 0 ) {
		int pi;
		for ( pi=0; pi<det->n_panels; pi++ ) {
			if ( fill_paths(hdfile, det, pi, master_el) ) {
				ERROR("Failed to enumerate paths.\n");
				return NULL;
			}
		}
	}

	/* Now enumerate the placeholder dimensions */
	if ( det->dim_dim > 0 ) {

		struct event_list *master_el_with_dims;
		int evi;

		/* If there were no HDF5 path placeholders, add a dummy event */
		if ( master_el->num_events == 0 ) {
			struct event *empty_ev;
			empty_ev = initialize_event();
			append_event_to_event_list(master_el, empty_ev);
			free(empty_ev);
		}

		master_el_with_dims = initialize_event_list();

		/* For each event so far, expand the dimensions */
		for ( evi=0; evi<master_el->num_events; evi++ ) {

			int pi;
			int global_path_dim = -1;
			int mlwd;

			/* Check the dimensionality of each panel */
			for ( pi=0; pi<det->n_panels; pi++ ) {
				if ( check_dims(hdfile, &det->panels[pi],
				                master_el->events[evi],
				                master_el_with_dims,
				                &global_path_dim) )
				{
					ERROR("Failed to enumerate dims.\n");
					return NULL;
				}
			}

			/* Add this dimensionality to all events */
			for ( mlwd=0; mlwd<global_path_dim; mlwd++ ) {

				struct event *mlwd_ev;

				mlwd_ev = copy_event(master_el->events[evi]);
				push_dim_entry_to_event(mlwd_ev, mlwd);
				append_event_to_event_list(master_el_with_dims,
				                           mlwd_ev);
				free_event(mlwd_ev);
			}

		}

		free_event_list(master_el);
		return master_el_with_dims;

	} else {

		return master_el;

	}
}
