/*
 * render.c
 *
 * Render image data to GdkPixbufs
 *
 * Copyright © 2012-2020 Deutsches Elektronen-Synchrotron DESY,
 *                       a research centre of the Helmholtz Association.
 *
 * Authors:
 *   2009-2020 Thomas White <taw@physics.org>
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
#include <math.h>
#include <stdint.h>

#ifdef HAVE_GDKPIXBUF
#include <gdk-pixbuf/gdk-pixbuf.h>
#endif


#include "render.h"
#include "peaks.h"
#include "colscale.h"
#include "detgeom.h"

/** \file render.h */

static float *get_binned_panel(struct image *image, int binning,
                               int pi, double *max, int *pw, int *ph)
{
	float *data;
	int x, y;
	int w, h;

	struct detgeom_panel *p = &image->detgeom->panels[pi];

	/* Some pixels might get discarded */
	w = p->w / binning;
	h = p->h / binning;
	*pw = w;
	*ph = h;

	data = malloc(w*h*sizeof(float));

	*max = 0.0;
	for ( x=0; x<w; x++ ) {
	for ( y=0; y<h; y++ ) {

		double total;
		size_t xb, yb;
		int bad = 0;
		double val;

		total = 0;
		for ( xb=0; xb<binning; xb++ ) {
		for ( yb=0; yb<binning; yb++ ) {

			double v;
			int fs, ss;

			fs = binning*x+xb;
			ss = binning*y+yb;
			v = image->dp[pi][fs+ss*p->w];
			total += v;

			if ( (image->bad != NULL)
			  && (image->bad[pi][fs+ss*p->w]) ) bad = 1;

		}
		}

		val = total / ((double)binning * (double)binning);

		if ( bad ) {
			data[x+w*y] = -INFINITY;
		} else {
			data[x+w*y] = val;
			if ( val > *max ) *max = val;
		}

	}
	}

	return data;
}


#ifdef HAVE_GDKPIXBUF

/* NB This function is shared between render_get_image() and
 * render_get_colour_scale() */
static void render_free_data(guchar *data, gpointer p)
{
	free(data);
}


static GdkPixbuf *render_panel(float *hdr, int scale, double max, int w, int h)
{
	guchar *data;
	int x, y;

	/* Rendered (colourful) version */
	data = malloc(3*w*h);
	if ( data == NULL ) return NULL;

	/* These x,y coordinates are measured relative to the bottom-left
	 * corner */
	for ( y=0; y<h; y++ ) {
	for ( x=0; x<w; x++ ) {

		double val;
		double r, g, b;

		val = hdr[x+w*y];

		if ( val > -INFINITY ) {

			render_scale(val, max, scale, &r, &g, &b);

			/* Stuff inside square brackets makes this pixel go to
			 * the expected location in the pixbuf (which measures
			 * from the top-left corner */
			data[3*( x+w*y )+0] = 255*r;
			data[3*( x+w*y )+1] = 255*g;
			data[3*( x+w*y )+2] = 255*b;

		} else {

			data[3*( x+w*y )+0] = 30;
			data[3*( x+w*y )+1] = 20;
			data[3*( x+w*y )+2] = 0;

		}

	}
	}

	/* Create the pixbuf from the 8-bit display data */
	return gdk_pixbuf_new_from_data(data, GDK_COLORSPACE_RGB, FALSE, 8,
					w, h, w*3, render_free_data, NULL);

}


/* Render an image into multiple pixbufs according to geometry */
GdkPixbuf **render_panels(struct image *image,
                          int binning, int scale, double boost,
                          int *n_pixbufs)
{
	int i;
	int np;
	GdkPixbuf **pixbufs;
	float **hdrs;
	double max;
	int *ws, *hs;

	np = image->detgeom->n_panels;

	hdrs = calloc(np, sizeof(float *));
	ws = calloc(np, sizeof(int));
	hs = calloc(np, sizeof(int));
	if ( (hdrs == NULL) || (ws == NULL) || (hs == NULL) ) {
		*n_pixbufs = 0;
		return NULL;
	}

	/* Find overall max value for whole image */
	max = 0.0;
	for ( i=0; i<np; i++ ) {
		double this_max = 0.0;
		hdrs[i] = get_binned_panel(image, binning, i, &this_max,
		                           &ws[i], &hs[i]);
		if ( this_max > max ) max = this_max;
	}

	max /= boost;
	if ( max <= 6 ) { max = 10; }

	pixbufs = calloc(np, sizeof(GdkPixbuf*));
	if ( pixbufs == NULL ) {
		*n_pixbufs = 0;
		return NULL;
	}

	for ( i=0; i<np; i++ ) {
		pixbufs[i] = render_panel(hdrs[i], scale, max, ws[i], hs[i]);
		free(hdrs[i]);
	}

	free(hdrs);
	free(ws);
	free(hs);
	*n_pixbufs = np;

	return pixbufs;
}


GdkPixbuf *render_get_colour_scale(size_t w, size_t h, int scale)
{
	guchar *data;
	size_t x, y;
	int max;

	data = malloc(3*w*h);
	if ( data == NULL ) return NULL;

	max = h-(h/6);

	for ( y=0; y<h; y++ ) {

		double r, g, b;
		int val;

		val = y-(h/6);

		render_scale(val, max, scale, &r, &g, &b);

		data[3*( 0+w*(h-1-y) )+0] = 0;
		data[3*( 0+w*(h-1-y) )+1] = 0;
		data[3*( 0+w*(h-1-y) )+2] = 0;
		for ( x=1; x<w; x++ ) {
			data[3*( x+w*(h-1-y) )+0] = 255*r;
			data[3*( x+w*(h-1-y) )+1] = 255*g;
			data[3*( x+w*(h-1-y) )+2] = 255*b;
		}

	}

	y = h/6;
	for ( x=1; x<w; x++ ) {
		data[3*( x+w*(h-1-y) )+0] = 255;
		data[3*( x+w*(h-1-y) )+1] = 255;
		data[3*( x+w*(h-1-y) )+2] = 255;
	}

	return gdk_pixbuf_new_from_data(data, GDK_COLORSPACE_RGB, FALSE, 8,
					w, h, w*3, render_free_data, NULL);
}

#endif /* HAVE_GDKPIXBUF */
