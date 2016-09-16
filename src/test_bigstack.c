/* Test big stack usage.
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
 * $Id: test_bigstack.c,v 1.3 2002/08/21 10:42:20 rich Exp $
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>

#include "pthr_reactor.h"
#include "pthr_pseudothread.h"

static pool test_pool;
static pseudothread test_pth;

/* XXX I suspect that gcc will just turn this into tail recursion,
 * proving nothing.
 */
static void
recurse (int n)
{
  char s[1024];
  s[1023] = 'a';
  if (n > 0) recurse (n-1);
}

static void
do_test (void *data)
{
  recurse (100);
}

int
main ()
{
  pseudothread_set_stack_size (512 * 1024);

  test_pool = new_pool ();
  test_pth = new_pseudothread (test_pool, do_test, 0, "testing thread");
  pth_start (test_pth);

  while (pseudothread_count_threads () > 0)
    reactor_invoke ();

  exit (0);
}
