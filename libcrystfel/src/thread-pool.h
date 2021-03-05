/*
 * thread-pool.h
 *
 * A thread pool implementation
 *
 * Copyright © 2012-2021 Deutsches Elektronen-Synchrotron DESY,
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

#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

extern signed int get_status_label(void);

/**
 * \file thread-pool.h
 * Thread pool.
 */

/**
 * \p qargs: The queue_args pointer which was given to run_threads().
 * \returns a pointer which will be passed to the worker function.
 *
 * This function is called, non-reentrantly, to get a new work item to give to
 * your work function.  The stuff you need to generate the new work item should
 * have been stored in \p qargs which was passed to \ref run_threads.
 **/
typedef void *(*TPGetTaskFunc)(void *qargs);


/**
 * \param work The queue_args pointer which was given to \ref run_threads.
 * \param cookie A small integral number which is guaranteed to be unique among all
 * currently running threads.
 *
 * This function is called, reentrantly, for each work item.
 **/
typedef void (*TPWorkFunc)(void *work, int cookie);


/**
 * \param qargs The queue_args pointer which was given to \ref run_threads.
 * \param work The pointer which was returned by your get_task function.
 *
 * This function is called, non-reentrantly, after each work item has been
 * completed.  A typical use might be to update some counters inside \p qargs
 * according to fields withing \p work which were filled by your 'work' function.
 **/
typedef void (*TPFinalFunc)(void *qargs, void *work);


extern int run_threads(int n_threads, TPWorkFunc work,
                       TPGetTaskFunc get_task, TPFinalFunc final,
                       void *queue_args, int max,
                       int cpu_num, int cpu_groupsize, int cpu_offset);

#ifdef __cplusplus
}
#endif

#endif	/* THREAD_POOL_H */
