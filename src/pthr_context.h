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
 * $Id: pthr_context.h,v 1.7 2003/02/05 22:13:32 rich Exp $
 */

#ifndef PTHR_CONTEXT_H
#define PTHR_CONTEXT_H

#include <setjmp.h>

#ifdef HAVE_WORKING_SETCONTEXT

#include <ucontext.h>

typedef struct mctx_st {
  ucontext_t uc;
} mctx_t;

/* Save machine context. */
#define mctx_save(mctx) (void) getcontext (&(mctx)->uc)

/* Restore machine context. */
#define mctx_restore(mctx) (void) setcontext (&(mctx)->uc)

/* Switch machine context. */
#define mctx_switch(mctx_old, mctx_new) \
(void) swapcontext (&((mctx_old)->uc), &((mctx_new)->uc))

#else /* setjmp implementation */

#include <setjmp.h>

typedef struct mctx_st {
  jmp_buf jb;
} mctx_t;

/* Save machine context. */
#define mctx_save(mctx) (void) setjmp ((mctx)->jb)

/* Restore machine context. */
#define mctx_restore(mctx) longjmp ((mctx)->jb, 1)

/* Switch machine context. */
#define mctx_switch(mctx_old, mctx_new) \
do { if (setjmp ((mctx_old)->jb) == 0) longjmp ((mctx_new)->jb, 1); } while(0)

#endif /* ! USE_UCONTEXT */

/* Create machine context. */
extern void mctx_set (mctx_t *mctx,
		      void (*sf_addr) (void *), void *sf_arg,
		      void *sk_addr, int sk_size);

/* Return PC. */
extern unsigned long mctx_get_PC (mctx_t *mctx);

/* Return SP. */
extern unsigned long mctx_get_SP (mctx_t *mctx);

#endif /* PTHR_CONTEXT_H */
