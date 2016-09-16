/* Multiple reader / single writer locks for pthrlib.
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
 * $Id: pthr_rwlock.c,v 1.3 2002/12/01 14:29:30 rich Exp $
 */

#include "config.h"

#include <stdio.h>

#ifdef HAVE_ASSERT_H
#include <assert.h>
#endif

#include <pool.h>
#include <hash.h>

#include "pthr_pseudothread.h"
#include "pthr_wait_queue.h"
#include "pthr_rwlock.h"

/* I added this while I was trying to pin down a possible memory corruption
 * problem. It can be disabled in normal operations.
 */
/* #define RWLOCK_MEM_DEBUG 1 */
#define RWLOCK_MEM_DEBUG 0

#if RWLOCK_MEM_DEBUG
#define RWLOCK_MEM_MAGIC 0x99775533
#endif

struct rwlock
{
#if RWLOCK_MEM_DEBUG
  unsigned magic;
#endif
  int n;			/* If N == 0, lock is free.
				 * If N > 0, lock is held by N readers.
				 * If N == -1, lock is held by 1 writer.
				 */
  wait_queue writers_wq;	/* Writers wait on this queue. */
  wait_queue readers_wq;	/* Readers wait on this queue. */

  /* A hash from pth pointer -> subpool. The keys of this hash are
   * pseudothreads which are currently in the critical section. The
   * values are subpools of the appropriate pth pool. If a thread
   * exits without releasing the lock, then the subpool is deleted,
   * which causes our callback to run, releasing the lock.
   */
  hash pools;

  unsigned writers_have_priority:1;
};

static void _do_enter (rwlock);
static void _do_release (void *);
static void _delete_rwlock (void *);

rwlock
new_rwlock (pool p)
{
  rwlock rw = pmalloc (p, sizeof *rw);

#if RWLOCK_MEM_DEBUG
  rw->magic = RWLOCK_MEM_MAGIC;
#endif

  rw->n = 0;
  rw->readers_wq = new_wait_queue (p);
  rw->writers_wq = new_wait_queue (p);
  rw->writers_have_priority = 1;
  rw->pools = new_hash (p, pseudothread, pool);

  /* The purpose of this cleanup is just to check that the rwlock
   * isn't released with threads in the critical section.
   */
  pool_register_cleanup_fn (p, _delete_rwlock, rw);

  return rw;
}

static void
_delete_rwlock (void *vrw)
{
  rwlock rw = (rwlock) vrw;
#if RWLOCK_MEM_DEBUG
  assert (rw->magic == RWLOCK_MEM_MAGIC);
#endif
  assert (rw->n == 0);
}

/* Calling this function changes the nature of the lock so that
 * writers have priority over readers. If this is the case then
 * new readers will not be able to enter a critical section if
 * there are writers waiting to enter.
 * [NB: This is the default.]
 */
void
rwlock_writers_have_priority (rwlock rw)
{
#if RWLOCK_MEM_DEBUG
  assert (rw->magic == RWLOCK_MEM_MAGIC);
#endif
  rw->writers_have_priority = 1;
}

/* Calling this function changes the nature of the lock so that
 * readers have priority over writers. Note that if this is the case
 * then writers are likely to be starved if the lock is frequently
 * read.
 */
void
rwlock_readers_have_priority (rwlock rw)
{
#if RWLOCK_MEM_DEBUG
  assert (rw->magic == RWLOCK_MEM_MAGIC);
#endif
  rw->writers_have_priority = 0;
}

/* This function is identical to RWLOCK_ENTER_READ, but it
 * does not block. It returns TRUE if the lock was successfully
 * acquired, or FALSE if the operation would block.
 */
inline int
rwlock_try_enter_read (rwlock rw)
{
#if RWLOCK_MEM_DEBUG
  assert (rw->magic == RWLOCK_MEM_MAGIC);
#endif

  if (rw->n >= 0 &&
      (!rw->writers_have_priority ||
       wq_nr_sleepers (rw->writers_wq) == 0))
    {
      _do_enter (rw);
      rw->n++;
      return 1;
    }
  else
    return 0;
}

/* This function is identical to RWLOCK_ENTER_WRITE, but it
 * does not block. It returns TRUE if the lock was successfully
 * acquired, or FALSE if the operation would block.
 */
inline int
rwlock_try_enter_write (rwlock rw)
{
#if RWLOCK_MEM_DEBUG
  assert (rw->magic == RWLOCK_MEM_MAGIC);
#endif

  if (rw->n == 0)
    {
      _do_enter (rw);
      rw->n = -1;
      return 1;
    }
  else
    return 0;
}

/* Enter a critical section as a reader. Any number of readers
 * are allowed to enter a critical section at the same time. This
 * function may block.
 */
void
rwlock_enter_read (rwlock rw)
{
#if RWLOCK_MEM_DEBUG
  assert (rw->magic == RWLOCK_MEM_MAGIC);
#endif

  while (rwlock_try_enter_read (rw) == 0)
    wq_sleep_on (rw->readers_wq);
}

/* Enter a critical section as a writer. Only a single writer
 * is allowed to enter a critical section, and then only if
 * there are no readers. This function may block.
 */
void
rwlock_enter_write (rwlock rw)
{
#if RWLOCK_MEM_DEBUG
  assert (rw->magic == RWLOCK_MEM_MAGIC);
#endif

  while (rwlock_try_enter_write (rw) == 0)
    wq_sleep_on (rw->writers_wq);
}

/* Leave a critical section. */
void
rwlock_leave (rwlock rw)
{
  pool pool;

#if RWLOCK_MEM_DEBUG
  assert (rw->magic == RWLOCK_MEM_MAGIC);
#endif

  /* If this core dumps, it's probably because the pth didn't actually
   * hold a lock.
   */
  if (!hash_get (rw->pools, current_pth, pool)) abort ();

  /* Force _DO_RELEASE to run. */
  delete_pool (pool);
}

struct cleanup_data
{
  pseudothread pth;
  rwlock rw;
};

/* This function registers a clean-up function which deals with the
 * case when a thread exits early without releasing the lock.
 */
static void
_do_enter (rwlock rw)
{
  struct cleanup_data *data;
  pool pool;

#if RWLOCK_MEM_DEBUG
  assert (rw->magic == RWLOCK_MEM_MAGIC);
#endif

  /* Create a subpool. If the thread exits early, then this subpool
   * with be deleted implicitly. If, on the other hand, we release
   * the lock in RWLOCK_LEAVE, then we will delete this pool
   * explicitly. Either way, _DO_RELEASE will be called.
   */
  pool = new_subpool (pth_get_pool (current_pth));

  /* Save it in the hash. */
  hash_insert (rw->pools, current_pth, pool);

  /* Register a clean-up function in the subpool to call _DO_RELEASE. */
  data = pmalloc (pool, sizeof (struct cleanup_data));
  data->pth = current_pth;
  data->rw = rw;
  pool_register_cleanup_fn (pool, _do_release, data);
}

/* This function is called to do the actual work of releasing a lock. */
static void
_do_release (void *vdata)
{
  struct cleanup_data *data = (struct cleanup_data *)vdata;
  pseudothread pth = data->pth;
  rwlock rw = data->rw;

#if RWLOCK_MEM_DEBUG
  assert (rw->magic == RWLOCK_MEM_MAGIC);
#endif

  assert (rw->n != 0);

  /* Remove this pseudothread from rw->pools. */
  if (!hash_erase (rw->pools, pth)) abort ();

  if (rw->n > 0)		/* Reader leaving critical section? */
    {
      rw->n --;
      if (rw->n == 0)
	{
	  /* Any writers waiting? */
	  if (wq_nr_sleepers (rw->writers_wq) > 0)
	    wq_wake_up_one (rw->writers_wq);

	  /* This can't happen (probably). */
	  /* XXX It does happen -- but I believe it's not a mistake. */
	  /*assert (wq_nr_sleepers (rw->readers_wq) == 0);*/
	}
    }
  else				/* Writer leaving critical section? */
    {
      rw->n = 0;

      /* Any writers waiting? */
      if (wq_nr_sleepers (rw->writers_wq) > 0)
	wq_wake_up_one (rw->writers_wq);
      /* Any readers waiting? */
      else if (wq_nr_sleepers (rw->readers_wq) > 0)
	wq_wake_up_one (rw->readers_wq);
    }
}
