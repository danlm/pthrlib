/* Test for a working setcontext implementation.
 * - by Richard W.M. Jones <rich@annexia.org>
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
 * $Id: test_setcontext.c,v 1.1 2003/02/02 18:05:30 rich Exp $
 */

#include <stdio.h>
#include <stdlib.h>

#include <ucontext.h>

#define MAGIC 0x14151617

static void fn (int);

int
main ()
{
  ucontext_t ucp;

  if (getcontext (&ucp) == -1)
    exit (1);

  makecontext (&ucp, fn, 1, MAGIC);

  setcontext (&ucp);
  exit (1);
}

static void
fn (int magic)
{
  if (magic != MAGIC)
    exit (1);

  /* Working implementation. */
  printf ("ok\n");
  exit (0);
}
