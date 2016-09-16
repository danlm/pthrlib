/* Very Linux-specific context switching.
 * Written by RWMJ with lots of help from GNU pth library.
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
 * $Id: pthr_context.c,v 1.6 2003/02/09 17:02:57 rich Exp $
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>

#include "pthr_context.h"

#ifdef HAVE_WORKING_SETCONTEXT

void
mctx_set (mctx_t *mctx,
	  void (*sf_addr) (void *), void *sf_arg,
	  void *sk_addr, int sk_size)
{
  /* Fetch current context. */
  getcontext (&mctx->uc);

  /* Update to point to new stack. */
  mctx->uc.uc_link = 0;
  /* XXX I cheated and used code from pth. We should do proper
   * automatic detection here instead.
   */
#if defined(__i386__)
  mctx->uc.uc_stack.ss_sp = sk_addr + sk_size - 4;
  /* mctx->uc.uc_stack.ss_size = sk_size; */
#elif defined(__sparc__)
  mctx->uc.uc_stack.ss_sp = sk_addr + sk_size - 8;
  mctx->uc.uc_stack.ss_size = sk_size - 8;
#else
#error "Unsupported architecture"
#endif
  mctx->uc.uc_stack.ss_flags = 0;

  /* Make new context. */
  makecontext (&mctx->uc, sf_addr, 1, sf_arg);
}

unsigned long
mctx_get_PC (mctx_t *mctx)
{
#if defined(__i386__) || defined(__i386)
  return (unsigned long) mctx->uc.uc_mcontext.eip;
#elif defined(__sparc)
  return (unsigned long) mctx->uc.uc_mcontext.gregs[REG_PC];
#else
  return 0;			/* This is a non-essential function. */
#endif
}

unsigned long
mctx_get_SP (mctx_t *mctx)
{
  return (unsigned long) mctx->uc.uc_stack.ss_sp;
}

#else /* setjmp implementation */

#ifdef linux

#  if defined(__GLIBC__) && defined(__GLIBC_MINOR__) && __GLIBC__ >= 2 && __GLIBC_MINOR__ >= 0 && defined(JB_PC) && defined(JB_SP)
#    define SET_PC(mctx, v) mctx->jb[0].__jmpbuf[JB_PC] = (int) v
#    define SET_SP(mctx, v) mctx->jb[0].__jmpbuf[JB_SP] = (int) v
#    define GET_PC(mctx) mctx->jb[0].__jmpbuf[JB_PC]
#    define GET_SP(mctx) mctx->jb[0].__jmpbuf[JB_SP]
#  elif defined(__GLIBC__) && defined(__GLIBC_MINOR__) && __GLIBC__ >= 2 && __GLIBC_MINOR__ >= 0 && defined(__mc68000__)
#    define SET_PC(mctx, v) mctx->jb[0].__jmpbuf[0].__aregs[0] = (long int) v
#    define SET_SP(mctx, v) mctx->jb[0].__jmpbuf[0].__sp = (int *) v
#    define GET_PC(mctx) mctx->jb[0].__jmpbuf[0].__aregs[0]
#    define GET_SP(mctx) mctx->jb[0].__jmpbuf[0].__sp
#  elif defined(__GNU_LIBRARY__) && defined(__i386__)
#    define SET_PC(mctx, v) mctx->jb[0].__jmpbuf[0].__pc = (char *) v
#    define SET_SP(mctx, v) mctx->jb[0].__jmpbuf[0].__sp = (void *) v
#    define GET_PC(mctx) mctx->jb[0].__jmpbuf[0].__pc
#    define GET_SP(mctx) mctx->jb[0].__jmpbuf[0].__sp
#  else
#    error "Unsupported Linux (g)libc version and/or platform"
#  endif

#elif defined(__OpenBSD__) && defined(__i386__)

#  define GET_PC(mctx) mctx->jb[0]
#  define GET_SP(mctx) mctx->jb[2]
#  define SET_PC(mctx,v) GET_PC(mctx) = (long) v
#  define SET_SP(mctx,v) GET_SP(mctx) = (long) v

#else
#error "setjmp implementation has not been ported to your OS or architecture - please contact the maintainer"
#endif

void
mctx_set (mctx_t *mctx,
	  void (*sf_addr) (void *), void *sf_arg,
	  void *sk_addr, int sk_size)
{
  int *stack_ptr = (int *) (sk_addr + sk_size);

  /* Prevent gcc from allocating these variables in registers, to avoid
   * a warning about variables being clobbered by setjmp/longjmp.
   */
  (void) &sk_addr;
  (void) &stack_ptr;

#ifdef __i386__
  /* Push function argument onto the stack. */
  *--stack_ptr = (int) sf_arg;
  /* Push return address onto the stack. */
  *--stack_ptr = 0xfafafafa;
#ifdef __OpenBSD__
  /* OpenBSD needs an extra word here for what? Frame pointer? */
  *--stack_ptr = 0;
#endif
#else
#error "unsupported machine architecture"
#endif

  mctx_save (mctx);
  SET_PC (mctx, sf_addr);
  SET_SP (mctx, stack_ptr);
}

unsigned long
mctx_get_PC (mctx_t *mctx)
{
  return (unsigned) GET_PC (mctx);
}

unsigned long
mctx_get_SP (mctx_t *mctx)
{
  return (unsigned long) GET_SP (mctx);
}

#endif /* ! USE_UCONTEXT */
