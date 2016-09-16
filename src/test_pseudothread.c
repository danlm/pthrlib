/* Test the pseudothreads.
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
 * $Id: test_pseudothread.c,v 1.5 2002/12/01 14:29:31 rich Exp $
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#ifdef HAVE_STRING_H
#include <string.h>
#endif

#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif

#include <pool.h>

#include "pthr_pseudothread.h"

static void set_flag (void *data) { int *flag = (int *) data; *flag = 1; }

static pool test_pool;
static pseudothread test_pth;

static pool pool1;
static pseudothread pth1;
static int pool_gone = 0;
static int thread_has_run = 0;

static void
test_exiting (void *data)
{
  assert (current_pth == pth1);
  thread_has_run = 1;
  pth_exit ();
  abort ();
}

static void
test_timeout (void *data)
{
  assert (current_pth == pth1);
  thread_has_run = 1;
  pth_timeout (1);
  pth_sleep (1000);
}

static void
do_test (void *data)
{
  /* Check current_pth set correctly on thread start. */
  assert (current_pth == test_pth);

  /* Launch a thread and check that the thread runs and the pool is
   * deleted at the end.
   */
  pool1 = new_pool ();
  pool_register_cleanup_fn (pool1, set_flag, &pool_gone);
  pth1 = new_pseudothread (pool1, set_flag, &thread_has_run, "pth1");
  assert (pool_gone == 0);
  assert (thread_has_run == 0);
  pth_start (pth1);		/* The thread actually runs and exits here. */
  assert (pool_gone == 1);
  assert (thread_has_run == 1);
  assert (current_pth == test_pth);

  /* Check pth_get_* functions. */
  assert (pth_get_pool (test_pth) == test_pool);
  assert (strcmp (pth_get_name (test_pth), "testing thread") == 0);
  assert (pth_get_thread_num (test_pth) == 0);
  assert (pth_get_run (test_pth) == do_test);
  assert (pth_get_data (test_pth) == 0);
  assert (pth_get_language (test_pth) == 0);

  /* Check pth_exit function. */
  pool1 = new_pool ();
  pth1 = new_pseudothread (pool1, test_exiting, 0, "exiting thread");
  thread_has_run = 0;
  pth_start (pth1);
  assert (thread_has_run == 1);
  assert (current_pth == test_pth);

  /* Check timeout for system calls. */
  pool1 = new_pool ();
  pool_register_cleanup_fn (pool1, set_flag, &pool_gone);
  pth1 = new_pseudothread (pool1, test_timeout, 0, "timeout thread");
  thread_has_run = 0;
  pool_gone = 0;
  pth_start (pth1);
  assert (thread_has_run == 1);
  assert (current_pth == test_pth);
  while (!pool_gone) { pth_millisleep (100); }
}

int
main ()
{
  test_pool = new_pool ();
  test_pth = new_pseudothread (test_pool, do_test, 0, "testing thread");

  /* Check that pth_start restores current_pth correctly. */
  current_pth = (pseudothread) 0x1234;
  pth_start (test_pth);

  assert (current_pth == (pseudothread) 0x1234);

  while (pseudothread_count_threads () > 0)
    reactor_invoke ();

  exit (0);
}
