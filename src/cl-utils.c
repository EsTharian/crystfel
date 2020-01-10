/*
 * cl-utils.c
 *
 * OpenCL utility functions
 *
 * Copyright © 2012-2020 Deutsches Elektronen-Synchrotron DESY,
 *                       a research centre of the Helmholtz Association.
 *
 * Authors:
 *   2010-2019 Thomas White <taw@physics.org>
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
#include <string.h>

#ifdef HAVE_CL_CL_H
#include <CL/cl.h>
#else
#include <cl.h>
#endif

#include "utils.h"


/* Return 1 if a GPU device is present, 0 if not, 2 on error. */
int have_gpu_device()
{
	cl_uint nplat;
	cl_platform_id platforms[8];
	cl_context_properties prop[3];
	cl_int err;
	int i;

	err = clGetPlatformIDs(8, platforms, &nplat);
	if ( err != CL_SUCCESS ) return 2;
	if ( nplat == 0 ) return 0;

	/* Find a GPU platform in the list */
	for ( i=0; i<nplat; i++ ) {

		prop[0] = CL_CONTEXT_PLATFORM;
		prop[1] = (cl_context_properties)platforms[i];
		prop[2] = 0;

		clCreateContextFromType(prop, CL_DEVICE_TYPE_GPU,
		                        NULL, NULL, &err);

		if ( err != CL_SUCCESS ) {
			if ( err != CL_DEVICE_NOT_FOUND ) return 2;
		} else {
			return 1;
		}
	}

	return 0;
}


const char *clError(cl_int err)
{
	switch ( err ) {

		case CL_SUCCESS :
		return "no error";

		case CL_DEVICE_NOT_AVAILABLE :
		return "device not available";

		case CL_DEVICE_NOT_FOUND :
		return "device not found";

		case CL_INVALID_DEVICE_TYPE :
		return "invalid device type";

		case CL_INVALID_PLATFORM :
		return "invalid platform";

		case CL_INVALID_KERNEL :
		return "invalid kernel";

		case CL_INVALID_ARG_INDEX :
		return "invalid argument index";

		case CL_INVALID_ARG_VALUE :
		return "invalid argument value";

		case CL_INVALID_MEM_OBJECT :
		return "invalid memory object";

		case CL_INVALID_SAMPLER :
		return "invalid sampler";

		case CL_INVALID_ARG_SIZE :
		return "invalid argument size";

		case CL_INVALID_COMMAND_QUEUE :
		return "invalid command queue";

		case CL_INVALID_CONTEXT :
		return "invalid context";

		case CL_INVALID_VALUE :
		return "invalid value";

		case CL_INVALID_EVENT_WAIT_LIST :
		return "invalid wait list";

		case CL_MAP_FAILURE :
		return "map failure";

		case CL_MEM_OBJECT_ALLOCATION_FAILURE :
		return "object allocation failure";

		case CL_OUT_OF_HOST_MEMORY :
		return "out of host memory";

		case CL_OUT_OF_RESOURCES :
		return "out of resources";

		case CL_INVALID_KERNEL_NAME :
		return "invalid kernel name";

		case CL_INVALID_KERNEL_ARGS :
		return "invalid kernel arguments";

		case CL_INVALID_WORK_GROUP_SIZE :
		return "invalid work group size";

		case CL_IMAGE_FORMAT_NOT_SUPPORTED :
		return "image format not supported";

		case CL_INVALID_WORK_DIMENSION :
		return "invalid work dimension";

		default :
		return "unknown error";
	}
}


static char *get_device_string(cl_device_id dev, cl_device_info info)
{
	int r;
	size_t size;
	char *val;

	r = clGetDeviceInfo(dev, info, 0, NULL, &size);
	if ( r != CL_SUCCESS ) {
		ERROR("Couldn't get device vendor size: %s\n",
		      clError(r));
		return NULL;
	}
	val = malloc(size);
	r = clGetDeviceInfo(dev, info, size, val, NULL);
	if ( r != CL_SUCCESS ) {
		ERROR("Couldn't get dev vendor: %s\n", clError(r));
		return NULL;
	}

	return val;
}


cl_device_id get_cl_dev(cl_context ctx, int n)
{
	cl_device_id *dev;
	cl_int r;
	size_t size;
	int i, num_devs;

	/* Get the required size of the array */
	r = clGetContextInfo(ctx, CL_CONTEXT_DEVICES, 0, NULL, &size);
	if ( r != CL_SUCCESS ) {
		ERROR("Couldn't get array size for devices: %s\n", clError(r));
		return 0;
	}

	dev = malloc(size);
	r = clGetContextInfo(ctx, CL_CONTEXT_DEVICES, size, dev, NULL);
	if ( r != CL_SUCCESS ) {
		ERROR("Couldn't get device: %s\n", clError(r));
		return 0;
	}
	num_devs = size/sizeof(cl_device_id);

	if ( n >= num_devs ) {
		ERROR("Device ID out of range\n");
		return 0;
	}

	if ( n < 0 ) {

		STATUS("Available devices:\n");
		for ( i=0; i<num_devs; i++ ) {

			char *vendor;
			char *name;

			vendor = get_device_string(dev[i], CL_DEVICE_VENDOR);
			name = get_device_string(dev[i], CL_DEVICE_NAME);

			STATUS("Device %i: %s %s\n", i, vendor, name);

		}
		n = 0;

		STATUS("Using device 0.  Use --gpu-dev to choose another.\n");

	} else {

		char *vendor;
		char *name;

		vendor = get_device_string(dev[n], CL_DEVICE_VENDOR);
		name = get_device_string(dev[n], CL_DEVICE_NAME);

		STATUS("Using device %i: %s %s\n", n, vendor, name);

	}

	return dev[n];
}


static void show_build_log(cl_program prog, cl_device_id dev)
{
	cl_int r;
	char log[4096];
	size_t s;

	r = clGetProgramBuildInfo(prog, dev, CL_PROGRAM_BUILD_LOG, 4096, log,
	                          &s);

	STATUS("Status: %i\n", r);
	STATUS("%s\n", log);
}


cl_program load_program_from_string(const char *source_in, size_t len,
                                    cl_context ctx, cl_device_id dev,
                                    cl_int *err, const char *extra_cflags,
                                    const char *insert_stuff)
{
	cl_program prog;
	cl_int r;
	char cflags[1024] = "";
	char *insert_pos;
	size_t il;
	char *source;

	/* Copy the code because we need to zero-terminate it */
	source = malloc(len+1);
	if ( source == NULL ) return 0;
	memcpy(source, source_in, len);
	source[len] = '\0';

	if ( insert_stuff != NULL ) {
		insert_pos = strstr(source, "INSERT_HERE");

		if ( insert_pos != NULL ) {

			char *source2;
			source2 = malloc(strlen(source)+strlen(insert_stuff)+1);
			if ( source2 == NULL ) return 0;

			il = insert_pos - source;
			memcpy(source2, source, il);
			memcpy(source2+il, insert_stuff, strlen(insert_stuff)+1);
			memcpy(source2+il+strlen(insert_stuff),
			       source+il+11, strlen(source+il+11)+1);
			free(source);
			source = source2;

		}
	}

	prog = clCreateProgramWithSource(ctx, 1, (const char **)&source,
	                                 NULL, err);
	if ( *err != CL_SUCCESS ) {
		ERROR("Couldn't load source\n");
		return 0;
	}

	cflags[0] = '\0';
	strncat(cflags, "-cl-no-signed-zeros ", 1023-strlen(cflags));
	strncat(cflags, extra_cflags, 1023-strlen(cflags));

	r = clBuildProgram(prog, 0, NULL, cflags, NULL, NULL);
	if ( r != CL_SUCCESS ) {
		ERROR("Couldn't build program\n");
		show_build_log(prog, dev);
		*err = r;
		return 0;
	}

	free(source);
	*err = CL_SUCCESS;
	return prog;
}


cl_program load_program(const char *filename, cl_context ctx,
                        cl_device_id dev, cl_int *err, const char *extra_cflags,
                        const char *insert_stuff)
{
	FILE *fh;
	char *source;
	size_t len;

	fh = fopen(filename, "r");
	if ( fh == NULL ) {
		ERROR("Couldn't open '%s'\n", filename);
		*err = CL_INVALID_PROGRAM;
		return 0;
	}
	source = malloc(16384);
	if ( source == NULL ) return 0;
	len = fread(source, 1, 16383, fh);
	fclose(fh);

	return load_program_from_string(source, len, ctx, dev,err, extra_cflags,
	                                insert_stuff);
}
