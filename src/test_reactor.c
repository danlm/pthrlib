/* Test the reactor.
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
 * $Id: test_reactor.c,v 1.2 2002/08/21 10:42:21 rich Exp $
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include <pool.h>

#include "pthr_reactor.h"

static void set_flag (void *data) { int *flag = (int *) data; *flag = 1; }
static void set_flag_h (int s, int e, void *data) { int *flag = (int *) data; *flag = 1; }

int
main ()
{
  int p1[2], p2[2];
  reactor_handle h1, h2;
  int flag1 = 0, flag2 = 0, flag3 = 0;
  char c = '\0';
  reactor_timer t1;
  reactor_prepoll pre1;

  /* Create some pipes. */
  if (pipe (p1) < 0) { perror ("pipe"); exit (1); }
  if (pipe (p2) < 0) { perror ("pipe"); exit (1); }

  if (fcntl (p1[0], F_SETFL, O_NONBLOCK) < 0) { perror ("fcntl"); exit (1); }
  if (fcntl (p1[1], F_SETFL, O_NONBLOCK) < 0) { perror ("fcntl"); exit (1); }
  if (fcntl (p2[0], F_SETFL, O_NONBLOCK) < 0) { perror ("fcntl"); exit (1); }
  if (fcntl (p2[1], F_SETFL, O_NONBLOCK) < 0) { perror ("fcntl"); exit (1); }

  /* Register read handlers. */
  h1 = reactor_register (p1[0], REACTOR_READ, set_flag_h, &flag1);
  h2 = reactor_register (p2[0], REACTOR_READ, set_flag_h, &flag2);

  /* Register a prepoll handler. */
  pre1 = reactor_register_prepoll (global_pool, set_flag, &flag3);

  /* Write something and check the flags. */
  write (p1[1], &c, 1);
  reactor_invoke ();
  assert (flag1 == 1);
  assert (flag2 == 0);
  assert (flag3 == 1);
  flag1 = flag3 = 0;
  read (p1[0], &c, 1);
  write (p2[1], &c, 1);
  reactor_invoke ();
  assert (flag1 == 0);
  assert (flag2 == 1);
  assert (flag3 == 1);
  flag2 = flag3 = 0;
  read (p1[0], &c, 1);

  reactor_unregister (h1);
  reactor_unregister (h2);
  reactor_unregister_prepoll (pre1);

  /* Register a timer function. */
  t1 = reactor_set_timer (global_pool, 1000, set_flag, &flag1);
  sleep (2);
  reactor_invoke ();
  assert (flag1 == 1);
  assert (flag3 == 0);
  flag1 = 0;

  exit (0);
}
