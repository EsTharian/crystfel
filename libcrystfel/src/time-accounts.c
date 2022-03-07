/*
 * time-accounts.c
 *
 * Simple profiling according to wall clock time
 *
 * Copyright © 2016-2021 Deutsches Elektronen-Synchrotron DESY,
 *                       a research centre of the Helmholtz Association.
 *
 * Authors:
 *  2016-2018 Thomas White <taw@physics.org>
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

#include <libcrystfel-config.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <assert.h>

#include "time-accounts.h"

#define MAX_ACCOUNTS 256

#ifndef CLOCK_MONOTONIC_RAW
#define CLOCK_MONOTONIC_RAW (CLOCK_MONOTONIC)
#endif

struct _timeaccounts
{
	enum timeaccount accs[MAX_ACCOUNTS];
	time_t sec[MAX_ACCOUNTS];
	long nsec[MAX_ACCOUNTS];
	int n_accs;
	enum timeaccount cur_acc;
	time_t cur_sec;
	long cur_nsec;
};


TimeAccounts *time_accounts_init()
{
	TimeAccounts *accs = malloc(sizeof(struct _timeaccounts));
	if ( accs == NULL ) return NULL;

	accs->n_accs = 0;
	accs->cur_acc = TACC_NOTHING;

#ifndef HAVE_CLOCK_GETTIME
	printf("Profiling disabled because clock_gettime is not available\n");
#endif

	return accs;
}


void time_accounts_free(TimeAccounts *accs)
{
	free(accs);
}


static int find_account(TimeAccounts *accs, enum timeaccount acc)
{
	int i;

	for ( i=0; i<accs->n_accs; i++ ) {
		if ( accs->accs[i] == acc ) return i;
	}

	if ( i == MAX_ACCOUNTS ) {
		static int warned_toomany = 0;
		if ( !warned_toomany ) printf("Too many time accounts used!\n");
		warned_toomany = 1;
		return MAX_ACCOUNTS;
	}

	/* This is the first time the account is used */
	accs->accs[i] = acc;
	accs->sec[i] = 0;
	accs->nsec[i] = 0;
	accs->n_accs++;
	return i;
}


void time_accounts_reset(TimeAccounts *accs)
{
	accs->n_accs = 0;
	accs->cur_acc = TACC_NOTHING;
}


#ifdef HAVE_CLOCK_GETTIME

void time_accounts_set(TimeAccounts *accs, enum timeaccount new_acc)
{
	struct timespec tp;

	if ( accs == NULL ) return;

	clock_gettime(CLOCK_MONOTONIC_RAW, &tp);

	/* Record time used on the previous account */
	if ( accs->cur_acc != TACC_NOTHING ) {
		int i = find_account(accs, accs->cur_acc);
		if ( i == MAX_ACCOUNTS ) {
			printf("Too many time accounts!\n");
		} else {

			time_t sec = tp.tv_sec - accs->cur_sec;
			long nsec = tp.tv_nsec - accs->cur_nsec;

			if ( nsec < 0 ) {
				sec -= 1;
				nsec += 1000000000;
			}
			accs->sec[i] += sec;
			accs->nsec[i] += nsec;

			while ( accs->nsec[i] > 1000000000 ) {
				accs->sec[i] += 1;
				accs->nsec[i] -= 1000000000;
			}

		}
	}

	accs->cur_acc = new_acc;
	accs->cur_sec = tp.tv_sec;
	accs->cur_nsec = tp.tv_nsec;
}

#else

void time_accounts_set(TimeAccounts *accs, enum timeaccount new_acc)
{
	if ( accs == NULL ) return;

	/* Record time used on the previous account */
	if ( accs->cur_acc != TACC_NOTHING ) {
		int i = find_account(accs, accs->cur_acc);
		if ( i == MAX_ACCOUNTS ) {
			printf("Too many time accounts!\n");
		} else {
			/* Do nothing because we have no timer */
		}
	}

	accs->cur_acc = new_acc;
	accs->cur_sec = 0;
	accs->cur_nsec = 0;
}

#endif

static const char *taccname(enum timeaccount acc)
{
	switch ( acc ) {
		case TACC_NOTHING : return "Nothing";
		case TACC_SELECT : return "select()";
		case TACC_STREAMREAD : return "Stream read";
		case TACC_SIGNALS : return "Checking signals";
		case TACC_QUEUETOPUP : return "Topping up queue";
		case TACC_STATUS : return "Printing status";
		case TACC_ENDCHECK : return "Checking end";
		case TACC_WAKEUP : return "Waking up workers";
		case TACC_WAITPID : return "Waiting on workers";
		case TACC_WAITFILE : return "Waiting for image file";
		case TACC_IMAGE_DATA : return "Reading image data";
		case TACC_IMAGE_PARAMS : return "Reading image parameters";
		case TACC_CREATE_DETGEOM : return "Creating detgeom";
		case TACC_CREATE_BADMAP : return "Creating bad pixel map";
		case TACC_CREATE_SATMAP : return "Creating saturation map";
		case TACC_CACHE_HEADERS : return "Caching image headers";
		case TACC_FILTER : return "Image filters";
		case TACC_RESRANGE : return "Resolution range";
		case TACC_PEAKSEARCH : return "Peak search";
		case TACC_INDEXING : return "Indexing";
		case TACC_PREDPARAMS : return "Prediction parameters";
		case TACC_INTEGRATION : return "Integration";
		case TACC_TOTALS : return "Crystal totals";
		case TACC_WRITESTREAM : return "Writing stream";
		case TACC_CLEANUP : return "Image cleanup";
		case TACC_EVENTWAIT : return "Waiting for event";
		case TACC_FINALCLEANUP : return "Final cleanup";
		default : return "Unknown";
	}
}


static const char *taccname_short(enum timeaccount acc)
{
	switch ( acc ) {
		case TACC_NOTHING : return "?????";
		case TACC_SELECT : return "selct";
		case TACC_STREAMREAD : return "sread";
		case TACC_SIGNALS : return "signs";
		case TACC_QUEUETOPUP : return "qfill";
		case TACC_STATUS : return "print";
		case TACC_ENDCHECK : return "endch";
		case TACC_WAKEUP : return "wakew";
		case TACC_WAITPID : return "waitw";
		case TACC_WAITFILE : return "wfile";
		case TACC_IMAGE_DATA : return "idata";
		case TACC_IMAGE_PARAMS : return "iprms";
		case TACC_CREATE_DETGEOM : return "dgeom";
		case TACC_CREATE_BADMAP : return "bdmap";
		case TACC_CREATE_SATMAP : return "stmap";
		case TACC_CACHE_HEADERS : return "headc";
		case TACC_FILTER : return "filtr";
		case TACC_RESRANGE : return "rrnge";
		case TACC_PEAKSEARCH : return "peaks";
		case TACC_INDEXING : return "index";
		case TACC_PREDPARAMS : return "predp";
		case TACC_INTEGRATION : return "integ";
		case TACC_TOTALS : return "ctotl";
		case TACC_WRITESTREAM : return "swrte";
		case TACC_CLEANUP : return "clean";
		case TACC_EVENTWAIT : return "wevnt";
		case TACC_FINALCLEANUP : return "final";
		default : return "unkwn";
	}
}


void time_accounts_print_short(TimeAccounts *accs)
{
	int i;
	for ( i=0; i<accs->n_accs; i++ ) {
		printf("%s: %.3f ", taccname_short(accs->accs[i]),
		       (double)accs->sec[i] + accs->nsec[i]/1e9);
	}
	printf("\n");
	fflush(stdout);
}


void time_accounts_print(TimeAccounts *accs)
{
	int i;
	printf("Wall clock time budget:\n");
	printf("-----------------------\n");
	for ( i=0; i<accs->n_accs; i++ ) {
		printf("%25s: %10lli sec %10li nsec\n", taccname(accs->accs[i]),
		       (long long)accs->sec[i], accs->nsec[i]);
	}
}
