/* Test the exception handling.
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
 * $Id: test_except1.c,v 1.4 2002/12/01 14:29:31 rich Exp $
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#ifdef HAVE_STRING_H
#include <string.h>
#endif

#include <pool.h>

#include "pthr_pseudothread.h"

static pool test_pool;
static pseudothread test_pth;

static void
do_die (void *data)
{
  pth_die ("this is the message");
}

static void
do_test (void *data)
{
  const char *msg;

  /* Check that catch can correctly catch a die. */
  msg = pth_catch (do_die, 0);

  if (!msg || strcmp (msg, "this is the message") != 0)
    abort ();
}

int
main ()
{
  test_pool = new_pool ();
  test_pth = new_pseudothread (test_pool, do_test, 0, "testing thread");
  pth_start (test_pth);

  while (pseudothread_count_threads () > 0)
    reactor_invoke ();

  exit (0);
}
