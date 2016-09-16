/* Pseudothread stacks.
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
 * $Id: pthr_stack.c,v 1.4 2003/02/05 22:13:33 rich Exp $
 */

/* The _PTH_GET_STACK and _PTH_RETURN_STACK functions are responsible
 * for allocating new stacks and returning them when they are finished.
 *
 * We are careful to avoid freeing up a stack while we are actually
 * using it. This means in particular that the _PTH_RETURN_STACK
 * function doesn't directly free up the current stack. It will
 * be freed up instead next time this function is called.
 *
 * Linux has the ability to allocate stacks from anonymous memory.
 * We use this ability if available. However, Linux also has the
 * ability to allocate "grows down" stack segments using the
 * non-portable MAP_GROWSDOWN flag. We *don't* use this feature
 * because (a) LinuxThreads itself doesn't use it any more and
 * (b) it can cause intermittent failures if something else
 * happens to allocate memory in the middle of our (as yet
 * unallocated) stack.
 *
 * On Linux we also allocate a guard page to protect against
 * stack overflow.
 */

#include "config.h"

#include <stdlib.h>

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#ifdef HAVE_SYS_MMAN_H
#include <sys/mman.h>
#endif

#include "pthr_stack.h"

#define GUARD_PAGE_SIZE 8192

#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS MAP_ANON
#endif

static inline void *
alloc_stack (int size)
{
  void *base;

  /* Allocate the actual stack memory. */
  base = mmap (0, size, PROT_READ|PROT_WRITE|PROT_EXEC,
	       MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
  if (base == MAP_FAILED) return 0;

  /* Allocate a guard page right at the bottom of the stack. */
  if (mprotect (base, GUARD_PAGE_SIZE, 0) == -1) abort ();

  return base;
}

static inline void
free_stack (void *base, int size)
{
  munmap (base, size);
}

/* Stack pending deallocation (see notes above). */
static void *pending_stack_base = 0;
static int pending_stack_size = 0;

void *
_pth_get_stack (int size)
{
  void *base;

  /* Is there a stack waiting to be freed up? If so, free it now. */
  if (pending_stack_base)
    {
      free_stack (pending_stack_base, pending_stack_size);
      pending_stack_base = 0;
    }

  /* Allocate a stack of the appropriate size, if available. */
  base = alloc_stack (size);
  if (!base) return 0;

  return base;
}

void
_pth_return_stack (void *base, int size)
{
  /* Is there a stack waiting to be freed up? If so, free it now. */
  if (pending_stack_base)
    {
      free_stack (pending_stack_base, pending_stack_size);
      pending_stack_base = 0;
    }

  /* Don't actually free the stack right now. We're still using it. */
  pending_stack_base = base;
  pending_stack_size = size;
}
