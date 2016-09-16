/* Wait queues.
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
 * $Id: pthr_wait_queue.c,v 1.5 2002/12/01 14:29:30 rich Exp $
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif

#ifdef HAVE_ASSERT_H
#include <assert.h>
#endif

#include <pool.h>
#include <vector.h>

#include "pthr_pseudothread.h"
#include "pthr_wait_queue.h"

/* See implementation notes in <pthr_wait_queue.h>. */
struct wait_queue
{
  /* List of threads currently sleeping on the queue. */
  vector sleepers;
};

wait_queue
new_wait_queue (pool pool)
{
  wait_queue wq = pmalloc (pool, sizeof *wq);

  wq->sleepers = new_vector (pool, pseudothread);
  return wq;
}

int
wq_nr_sleepers (wait_queue wq)
{
  return vector_size (wq->sleepers);
}

/* To sleep on the wait queue, we register ourselves, then we swap back
 * into the reactor context.
 */
void
wq_sleep_on (wait_queue wq)
{
  vector_push_back (wq->sleepers, current_pth);

  /* Swap context back to the calling context. */
  _pth_switch_thread_to_calling_context ();

  /* When we get here, we have been woken up ... */

  /* Have we been signalled? */
  if (_pth_alarm_received ())
    {
      int i;
      pseudothread p;

      /* Remove self from sleepers list. */
      for (i = 0; i < vector_size (wq->sleepers); ++i)
	{
	  vector_get (wq->sleepers, i, p);
	  if (p == current_pth)
	    {
	      vector_erase (wq->sleepers, i);
	      goto found;
	    }
	}

      /* Oops - not found on sleepers list. */
      abort ();

    found:
      /* Exit. */
      pth_exit ();
    }
}

/* This is the prepoll handler which actually wakes up the threads. */
struct wake_up_info
{
  pool pool;
  vector sleepers;		/* Pseudothreads to wake up. */
  reactor_prepoll handler;	/* Handler (must be unregistered at end). */
};

static void
do_wake_up (void *infop)
{
  struct wake_up_info *info = (struct wake_up_info *) infop;
  int i;

  for (i = 0; i < vector_size (info->sleepers); ++i)
    {
      pseudothread pth;

      vector_get (info->sleepers, i, pth);

      /* Swap into the thread context. */
      _pth_switch_calling_to_thread_context (pth);
    }

  reactor_unregister_prepoll (info->handler);
  delete_pool (info->pool);
}

/* To wake up we take a private copy of the wait queue, clear the
 * sleepers list, then register a prepoll handler which will eventually
 * run and wake up each sleeper in turn.
 */
static inline void
wake_up (wait_queue wq, int n)
{
  pool pool;
  vector v;
  reactor_prepoll handler;
  struct wake_up_info *wake_up_info;

  /* Added this experimentally to get around a bug when rws running monolith
   * apps which have database connections open is killed. It seems to be
   * something to do with having prepoll handlers registered when the
   * reactor exits. Avoid this entirely here - there is no need, as far as
   * I can see, to do anything in this function if no one is actually sleeping
   * on the queue.
   * - RWMJ 2002/10/15
   */
  if (vector_size (wq->sleepers) == 0) return;

  /* This will be freed up by the prepoll handler. */
  pool = new_subpool (global_pool);

  /* Take a private copy, either of the whole queue, or just part of it,
   * and also clear the list.
   */
  if (n == -1)
    {
      v = copy_vector (pool, wq->sleepers);
      vector_clear (wq->sleepers);
    }
  else
    {
      v = new_vector (pool, pseudothread);

      while (n > 0)
	{
	  pseudothread pth;

	  vector_pop_front (wq->sleepers, pth);
	  vector_push_back (v, pth);
	  n--;
	}
    }

  /* Register a prepoll handler to wake up these sleepin' bewts. */
  wake_up_info = pmalloc (pool, sizeof *wake_up_info);
  wake_up_info->pool = pool;
  wake_up_info->sleepers = v;
  handler = reactor_register_prepoll (pool, do_wake_up, wake_up_info);
  wake_up_info->handler = handler;
}

void
wq_wake_up (wait_queue wq)
{
  wake_up (wq, -1);
}

void
wq_wake_up_one (wait_queue wq)
{
  /* If there is nothing on the wait queue, but we were instructed to
   * wake one, then there is probably a bug in the code.
   */
  if (vector_size (wq->sleepers) < 1) abort ();

  wake_up (wq, 1);
}
