/* Test rwlocks.
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
 * $Id: test_rwlock.c,v 1.3 2002/12/01 14:29:31 rich Exp $
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

#include "pthr_rwlock.h"

#define NR_WRITER_THREADS 1
#define NR_READER_THREADS 500
#define NR_INCREMENTS 1000

static int var = 0;		/* The contended variable. */
static rwlock lock;		/* The lock. */
static int nr_writer_threads = NR_WRITER_THREADS;
static int nr_reader_threads = NR_READER_THREADS;

static void
start_monitor_thread (void *data)
{
  int p = 0, v;

  printf ("[                                                                        ]\r[");

  while (nr_writer_threads > 0 && nr_reader_threads > 0)
    {
      /* Get value of contended variable and draw a scale. */
      v = 72 * var / (NR_WRITER_THREADS * NR_INCREMENTS);
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
  assert (var == NR_WRITER_THREADS * NR_INCREMENTS);
  exit (0);
}

static void
start_writer_thread (void *data)
{
  int i;

  for (i = 0; i < NR_INCREMENTS; ++i)
    {
      int v;

      rwlock_enter_write (lock);
      v = var;			/* Do a slow R/M/W. */
      pth_millisleep (1);
      var = v + 1;
      rwlock_leave (lock);

      pth_millisleep (1);
    }

  nr_writer_threads--;
}

static void
start_reader_thread (void *data)
{
  while (nr_writer_threads > 0)
    {
      rwlock_enter_read (lock);
      pth_millisleep (1);
      rwlock_leave (lock);

      pth_millisleep (1);
    }

  nr_reader_threads--;
}

int
main ()
{
  pseudothread writer_pth[NR_WRITER_THREADS];
  pseudothread reader_pth[NR_READER_THREADS];
  pseudothread monitor_pth;
  pool p;
  int i;

  /* Create the lock. */
  lock = new_rwlock (global_pool);

  /* Create the monitoring thread. */
  p = new_subpool (global_pool);
  monitor_pth = new_pseudothread (p, start_monitor_thread, 0,
				  "monitor");
  pth_start (monitor_pth);

  /* Create the writer threads. */
  for (i = 0; i < NR_WRITER_THREADS; ++i)
    {
      p = new_subpool (global_pool);
      writer_pth[i] = new_pseudothread (p,
					start_writer_thread, 0,
					psprintf (p, "writer thread %d", i));
    }

  /* Create the reader threads. */
  for (i = 0; i < NR_READER_THREADS; ++i)
    {
      p = new_subpool (global_pool);
      reader_pth[i] = new_pseudothread (p,
					start_reader_thread, 0,
					psprintf (p, "reader thread %d", i));
    }

  /* Start all the threads running. */
  for (i = 0; i < NR_WRITER_THREADS; ++i)
    pth_start (writer_pth[i]);
  for (i = 0; i < NR_READER_THREADS; ++i)
    pth_start (reader_pth[i]);

  for (;;) reactor_invoke ();
}
