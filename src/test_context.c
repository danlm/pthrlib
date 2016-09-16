/* Test context switching.
 * Copyright (C) 2001-2003 Richard W.M. Jones <rich@annexia.org>
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
 * $Id: test_context.c,v 1.4 2003/02/05 22:13:33 rich Exp $
 */

#include "config.h"

#include <stdlib.h>
#include <assert.h>

#ifdef HAVE_STRING_H
#include <string.h>
#endif

#ifdef HAVE_SETJMP_H
#include <setjmp.h>
#endif

#include "pthr_context.h"

#define STACK_SIZE 4096
#define MARKER_SIZE 16

static unsigned char stack [MARKER_SIZE+STACK_SIZE+MARKER_SIZE];
static mctx_t ctx, calling_ctx;
static int called = 0;
static void fn (void *);
static void setjmp_test (void *);
static jmp_buf jb;

#define DATA ((void *) 0x12546731)

int
main ()
{
  int i;

  /* Place magic numbers into the marker areas at each end of the stack, so
   * we can detect stack overrun.
   */
  memset (stack, 0xeb, MARKER_SIZE);
  memset (stack + MARKER_SIZE+STACK_SIZE, 0xeb, MARKER_SIZE);
  mctx_set (&ctx, fn, DATA, stack + MARKER_SIZE, STACK_SIZE);
  mctx_switch (&calling_ctx, &ctx);
  assert (called == 1);

  /* Verify the ends of the stack haven't been overrun. */
  for (i = 0; i < MARKER_SIZE; ++i)
    assert (stack[i] == 0xeb && stack[i + MARKER_SIZE+STACK_SIZE] == 0xeb);

  /* Check setjmp/longjmp work on the alternate stack. This was found to be
   * a problem on Solaris.
   */
  mctx_set (&ctx, setjmp_test, DATA, stack + MARKER_SIZE, STACK_SIZE);
  mctx_switch (&calling_ctx, &ctx);

  exit (0);
}

static void
fn (void *data)
{
  assert (data == DATA);
  called = 1;
  mctx_restore (&calling_ctx);
  abort ();
}

static void call_longjmp (void);

static void
setjmp_test (void *data)
{
  if (setjmp (jb) == 0)
    call_longjmp ();

  mctx_restore (&calling_ctx);
}

static void
call_longjmp ()
{
  longjmp (jb, 1);
}
