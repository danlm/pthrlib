/* Mutex locks.
 * by Richard W.M. Jones <rich@annexia.org>
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
 * $Id: pthr_mutex.c,v 1.4 2003/02/02 18:05:31 rich Exp $
 */

#include "config.h"

#ifdef HAVE_ASSERT_H
#include <assert.h>
#endif

#ifndef __sun__

#include <pool.h>

#include "pthr_pseudothread.h"
#include "pthr_wait_queue.h"

#else
/* SunOS annoyingly defines a 'struct mutex' type, and even worse defines
 * it as soon as you include <stdlib.h>.
 */
#define POOL_H
struct pool;
typedef struct pool *pool;
extern pool new_subpool (pool);
extern void delete_pool (pool);
extern void *pmalloc (pool, unsigned n);
extern void pool_register_cleanup_fn (pool, void (*fn) (void *), void *);

#define PTHR_PSEUDOTHREAD_H
struct pseudothread;
typedef struct pseudothread *pseudothread;
extern pseudothread current_pth;
extern pool pth_get_pool (pseudothread pth);

#define PTHR_WAIT_QUEUE_H
struct wait_queue;
typedef struct wait_queue *wait_queue;
extern wait_queue new_wait_queue (pool);
extern void wq_wake_up (wait_queue);
extern void wq_wake_up_one (wait_queue);
extern void wq_sleep_on (wait_queue);
extern int wq_nr_sleepers (wait_queue);
#endif

#include "pthr_mutex.h"

static void _do_release (void *);
static void _delete_mutex (void *);

struct mutex
{
  pseudothread pth;	/* Pseudothread which is holding the lock, or null. */
  wait_queue wq;        /* Queue of threads waiting to enter. */
  pool pool;		/* Subpool of pth pool which holds the lock. If the
			 * thread exits without releasing the lock, then
			 * the subpool is deleted, which causes our callback
			 * to run, releasing the lock.
			 */
};

mutex
new_mutex (pool p)
{
  mutex m = pmalloc (p, sizeof *m);

  m->pth = 0;
  m->pool = 0;
  m->wq = new_wait_queue (p);

  /* The purpose of this cleanup is just to check that the mutex
   * isn't released with threads in the critical section.
   */
  pool_register_cleanup_fn (p, _delete_mutex, m);

  return m;
}

static void
_delete_mutex (void *vm)
{
  mutex m = (mutex) vm;
  assert (m->pth == 0);
}

/* This function is identical to MUTEX_ENTER, except that it does not
 * block if the lock is held by another thread. The function
 * returns TRUE if the lock was successfully acquired, or FALSE
 * if another thread is holding it.
 */
inline int
mutex_try_enter (mutex m)
{
  if (m->pth == 0)
    {
      /* Create a subpool. If the thread exits early, then this subpool
       * with be deleted implicitly. If, on the other hand, we release
       * the lock in RWLOCK_LEAVE, then we will delete this pool
       * explicitly. Either way, _DO_RELEASE will be called.
       */
      pool pool = new_subpool (pth_get_pool (current_pth));

      /* Register _DO_RELEASE to run when the subpool is deleted. */
      pool_register_cleanup_fn (pool, _do_release, m);

      m->pth = current_pth;
      m->pool = pool;
      return 1;
    }
  else
    return 0;
}

/* Enter a critical section. This function blocks until the lock is
 * acquired.
 */
void
mutex_enter (mutex m)
{
  while (mutex_try_enter (m) == 0)
    wq_sleep_on (m->wq);
}

/* Leave a critical section.
 */
void
mutex_leave (mutex m)
{
  assert (m->pth == current_pth);

  /* Force _DO_RELEASE to run. */
  delete_pool (m->pool);
}

static void
_do_release (void *vm)
{
  mutex m = (mutex) vm;

  m->pth = 0;
  m->pool = 0;

  /* Anyone waiting to enter? */
  if (wq_nr_sleepers (m->wq) > 0) wq_wake_up_one (m->wq);
}

int
mutex_nr_sleepers (mutex m)
{
  return wq_nr_sleepers (m->wq);
}
