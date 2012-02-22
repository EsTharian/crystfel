/*
 * hrs-scaling.h
 *
 * Intensity scaling using generalised HRS target function
 *
 * Copyright © 2012 Thomas White <taw@physics.org>
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

#ifndef HRS_SCALING_H
#define HRS_SCALING_H


#ifdef HAVE_CONFIG_H
#include <config.h>
#endif


#include "image.h"

extern RefList *scale_intensities(struct image *images, int n,
                                  RefList *reference, int n_threads,
                                  int noscale);


#endif	/* HRS_SCALING_H */
