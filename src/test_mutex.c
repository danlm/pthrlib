/* Test mutexes.
 * Copyright (C) 2001 Richard W.M. Jones <rich@annexia.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the Free
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * $Id: test_mutex.c,v 1.3 2002/12/01 14:29:31 rich Exp $
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif

#include <pool.h>
#include <pstring.h>

#include "pthr_mutex.h"

#define NR_THREADS 50
#define NR_INCREMENTS 50

static int var = 0;		/* The contended variable. */
static mutex lock;		/* The lock. */
static int nr_threads = NR_THREADS;

static void
start_monitor_thread (void *data)
{
  int p = 0, v;

  printf ("[                                                                        ]\r[");

  while (nr_threads > 0)
    {
      /* Get value of contended variable and draw a scale. */
      v = 72 * var / (NR_THREADS * NR_INCREMENTS);
      while (v > p)
	{
	  printf (".");
	  fflush (stdout);
	  p++;
	}

      pth_millisleep (100);
    }

  printf ("\n");

  /* Check v is correct at the end. */
  assert (var == NR_THREADS * NR_INCREMENTS);
  exit (0);
}

static void
start_thread (void *data)
{
  int i;

  for (i = 0; i < NR_INCREMENTS; ++i)
    {
      int v;

      mutex_enter (lock);
      v = var;			/* Do a slow R/M/W. */
      pth_millisleep (1);
      var = v + 1;
      mutex_leave (lock);

      pth_millisleep (1);
    }

  nr_threads--;
}

int
main ()
{
  pseudothread pth[NR_THREADS];
  pseudothread monitor_pth;
  pool p;
  int i;

  /* Create the lock. */
  lock = new_mutex (global_pool);

  /* Create the monitoring thread. */
  p = new_subpool (global_pool);
  monitor_pth = new_pseudothread (p, start_monitor_thread, 0,
				  "monitor");
  pth_start (monitor_pth);

  /* Create the threads. */
  for (i = 0; i < NR_THREADS; ++i)
    {
      p = new_subpool (global_pool);
      pth[i] = new_pseudothread (p, start_thread, 0,
				 psprintf (p, "thread %d", i));
    }

  /* Start all the threads running. */
  for (i = 0; i < NR_THREADS; ++i)
    pth_start (pth[i]);

  for (;;) reactor_invoke ();
}
