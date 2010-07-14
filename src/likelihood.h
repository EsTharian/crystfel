/*
 * likelihood.h
 *
 * Likelihood maximisation
 *
 * (c) 2006-2010 Thomas White <taw@physics.org>
 *
 * Part of CrystFEL - crystallography with a FEL
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifndef LIKELIHOOD_H
#define LIKELIHOOD_H


#include "utils.h"


extern void scale_intensities(const double *model, double *new_pattern,
	                      const unsigned int *model_counts,
                              ReflItemList *items, double f0,
                              int f0_valid);


#endif	/* LIKELIHOOD_H */
