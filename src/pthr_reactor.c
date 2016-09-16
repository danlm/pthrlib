/* A specialized Reactor.
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
 * $Id: pthr_reactor.c,v 1.6 2002/08/21 10:42:19 rich Exp $
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>

#ifdef HAVE_SYS_POLL_H
#include <sys/poll.h>
#endif

#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif

#ifdef HAVE_SIGNAL_H
#include <signal.h>
#endif

#ifdef HAVE_SYSLOG_H
#include <syslog.h>
#endif

#ifdef HAVE_ASSERT_H
#include <assert.h>
#endif

#ifdef HAVE_STRING_H
#include <string.h>
#endif

#include "pthr_reactor.h"

#define REACTOR_DEBUG 0

struct reactor_handle
{
  int offset;			/* Points into internal poll fds array. */
  void (*fn) (int, int, void *);
  void *data;
};

struct reactor_timer
{
  pool pool;
  struct reactor_timer *prev, *next;
  unsigned long long delta;
  void (*fn) (void *);
  void *data;
};

struct reactor_prepoll
{
  pool pool;
  struct reactor_prepoll *next;
  void (*fn) (void *);
  void *data;
  int fired;
};

/* This is how HANDLES and POLL_ARRAY work:
 *
 * HANDLES is a straightforward list of reactor handle objects. When
 * a user registers an event handler in the reactor, they get back
 * an integer which is in fact an offset into the HANDLES array.
 *
 * POLL_ARRAY is the array which actually gets passed to the poll(2)
 * system call.
 *
 * HANDLES and POLL_ARRAY are related through the OFFSET field in
 * a reactor handle. Thus:
 *
 * HANDLES:     +------+------+------+------+------+
 *      offset: | 2    | 1    | 3    | 3    | 0    |
 *              |      |      |      |      |      |
 *              | |    | |    |  \   | |    | |    |
 *              | |    | |    |   \  | |    | |    |
 *              +-|----+-|----+----\-+-|----+-|----+
 *                 \_____|_____     \  |      |
 * POLL_ARRAY:  +------+-|----+\-----\-|----+ |
 *              |      | |    | \    |\     | |
 *              |      |      |      |      | |
 *              |     _______________________/
 *              |      |      |      |      |
 *              +------+------+------+------+
 *
 * Note that you may have two handles pointing to the same offset
 * in POLL_ARRAY: this happens when we share pollfds because they
 * both contain the same fd and event information.
 *
 * If the OFFSET field of a handle contains -1, then the handle
 * is unused. Notice that we never decrease the size of the HANDLES
 * array.
 *
 * Adding (registering) a handle is easy enough: firstly we look for
 * an unused (offset == -1) handle, and if not found we reallocate
 * the HANDLES array to make it larger. Then we search through the
 * POLL_ARRAY to see if it's possible to share. If not, we extend
 * the POLL_ARRAY by 1 and set the offset accordingly.
 *
 * Deleting (unregistering) a handle is not so easy: deleting the
 * handle is OK -- just set the offset to -1. But we must also
 * check the POLL_ARRAY entry, and if it is not shared, delete it,
 * shrink the POLL_ARRAY and update all handles which point to
 * an offset larger than the deleted element in POLL_ARRAY.
 */


/* The list of reactor handles registered. */
static struct reactor_handle *handles = 0;
static struct pollfd *poll_array = 0;
static int nr_handles_allocated = 0;
static int nr_array_allocated = 0;
static int nr_array_used = 0;

/* The list of timers, stored in time order (in a delta queue). */
static struct reactor_timer *head_timer = 0;

/* The list of prepoll handlers in no particular order. */
static struct reactor_prepoll *head_prepoll = 0;

/* The current time, or near as dammit, in milliseconds from Unix epoch. */
unsigned long long reactor_time;

/* Function prototypes. */
static void remove_timer (void *timerp);
static void remove_prepoll (void *timerp);

/* Cause reactor_init / reactor_stop to be called automatically. */
static void reactor_init (void) __attribute__ ((constructor));
static void reactor_stop (void) __attribute__ ((destructor));

static void
reactor_init ()
{
  struct timeval tv;
  struct sigaction sa;

  /* Catch EPIPE errors rather than sending a signal. */
  memset (&sa, 0, sizeof sa);
  sa.sa_handler = SIG_IGN;
  sa.sa_flags = SA_RESTART;
  sigaction (SIGPIPE, &sa, 0);

  /* Update the reactor time. */
  gettimeofday (&tv, 0);
  reactor_time = tv.tv_sec * 1000LL + tv.tv_usec / 1000;
}

static void
reactor_stop ()
{
  int i;
  reactor_timer p, p_next;
  reactor_prepoll prepoll, prepoll_next;

  /* There should be no prepoll handlers registered. Check it and free them.*/
  for (prepoll = head_prepoll; prepoll; prepoll = prepoll_next)
    {
      syslog (LOG_WARNING, "prepoll handler left registered in reactor: fn=%p, data=%p",
	      prepoll->fn, prepoll->data);
      prepoll_next = prepoll;
      delete_pool (prepoll->pool);
    }

  /* There should be no timers registered. Check it and free them up. */
  for (p = head_timer; p; p = p_next)
    {
      syslog (LOG_WARNING, "timer left registered in reactor: fn=%p, data=%p",
	      p->fn, p->data);
      p_next = p->next;
      delete_pool (p->pool);
    }

  /* There should be no handles registered. Check for this. */
  for (i = 0; i < nr_handles_allocated; ++i)
    if (handles[i].offset >= 0)
      syslog (LOG_WARNING, "handle left registered in reactor: fn=%p, data=%p",
	      handles[i].fn, handles[i].data);

  /* Free up memory used by handles. */
  if (handles) free (handles);
  if (poll_array) free (poll_array);
}

reactor_handle
reactor_register (int socket, int operations,
		  void (*fn) (int socket, int events, void *), void *data)
{
  int i, h, a;

  /* Find an unused handle. */
  for (i = 0; i < nr_handles_allocated; ++i)
    if (handles[i].offset == -1)
      {
	h = i;
	goto found_handle;
      }

  /* No free handles: allocate a new handle. */
  h = nr_handles_allocated;
  nr_handles_allocated += 8;
  handles = realloc (handles,
		     nr_handles_allocated * sizeof (struct reactor_handle));
  for (i = h; i < nr_handles_allocated; ++i)
    handles[i].offset = -1;

 found_handle:
  /* Examine the poll descriptors to see if we can share with an
   * existing descriptor.
   */
  for (a = 0; a < nr_array_used; ++a)
    if (poll_array[a].fd == socket &&
	poll_array[a].events == operations)
      goto found_poll_descriptor;

  /* Allocate space in the array of poll descriptors. */
  if (nr_array_used >= nr_array_allocated)
    {
      nr_array_allocated += 8;
      poll_array = realloc (poll_array,
			    nr_array_allocated * sizeof (struct pollfd));
    }
  a = nr_array_used++;

  /* Create the poll descriptor. */
  poll_array[a].fd = socket;
  poll_array[a].events = operations;

 found_poll_descriptor:
  /* Create the handle. */
  handles[h].offset = a;
  handles[h].fn = fn;
  handles[h].data = data;

#if REACTOR_DEBUG
  fprintf (stderr,
	   "reactor_register (fd=%d, ops=0x%x, fn=%p, data=%p) = %p\n",
	   socket, operations, fn, data, &handles[h]);
#endif

  /* Return the handle. */
  return h;
}

void
reactor_unregister (reactor_handle handle)
{
  int i, a = handles[handle].offset;

#if REACTOR_DEBUG
  fprintf (stderr,
	   "reactor_unregister (handle=%d [fd=%d, ops=0x%x])\n",
	   handle, poll_array[a].fd, poll_array[a].events);
#endif

  handles[handle].offset = -1;

  /* Does any other handle share this element? If so, leave POLL_ARRAY alone.
   */
  for (i = 0; i < nr_handles_allocated; ++i)
    if (handles[i].offset == a)
      return;

  /* Not shared. Remove this element from poll_array. */
  memcpy (&poll_array[a], &poll_array[a+1],
	  (nr_array_used - a - 1) * sizeof (struct pollfd));
  nr_array_used --;

  /* Any handles in used which use offset > a should be updated. */
  for (i = 0; i < nr_handles_allocated; ++i)
    if (handles[i].offset > a)
      handles[i].offset --;
}

reactor_timer
reactor_set_timer (pool pp,
		   int timeout,
		   void (*fn) (void *data),
		   void *data)
{
  pool sp;
  reactor_timer timer, p, last_p;
  unsigned long long trigger_time, this_time;

  sp = new_subpool (pp);

  timer = pmalloc (sp, sizeof *timer);

  timer->pool = sp;
  timer->fn = fn;
  timer->data = data;

  /* Register a function to clean up this timer when the subpool is deleted.*/
  pool_register_cleanup_fn (sp, remove_timer, timer);

  /* Calculate the trigger time. */
  trigger_time = reactor_time + timeout;

  if (head_timer == 0)		/* List is empty. */
    {
      timer->prev = timer->next = 0;
      timer->delta = trigger_time;
      return head_timer = timer;
    }

  /* Find out where to insert this handle in the delta queue. */
  this_time = 0;
  last_p = 0;
  for (p = head_timer; p; last_p = p, p = p->next)
    {
      this_time += p->delta;

      if (this_time >= trigger_time) /* Insert before element p. */
	{
	  timer->prev = p->prev;
	  timer->next = p;
	  if (p->prev)		/* Not the first element. */
	    p->prev->next = timer;
	  else			/* First element in list. */
	    head_timer = timer;
	  p->prev = timer;
	  timer->delta = trigger_time - (this_time - p->delta);
	  p->delta = this_time - trigger_time;
	  return timer;
	}
    }

  /* Insert at the end of the list. */
  last_p->next = timer;
  timer->prev = last_p;
  timer->next = 0;
  timer->delta = trigger_time - this_time;
  return timer;
}

static void
remove_timer (void *timerp)
{
  struct reactor_timer *timer = (struct reactor_timer *) timerp;

  /* Remove this timer from the list. */
  if (timer->prev != 0)
    timer->prev->next = timer->next;
  else
    head_timer = timer->next;

  if (timer->next != 0)
    {
      timer->next->prev = timer->prev;
      timer->next->delta += timer->delta;
    }
}

void
reactor_unset_timer_early (reactor_timer timer)
{
  delete_pool (timer->pool);
}

reactor_prepoll
reactor_register_prepoll (pool pp,
			  void (*fn) (void *data),
			  void *data)
{
  pool sp;
  reactor_prepoll p;

  sp = new_subpool (pp);

  p = pmalloc (sp, sizeof *p);

  p->pool = sp;
  p->fn = fn;
  p->data = data;

  pool_register_cleanup_fn (sp, remove_prepoll, p);

  p->next = head_prepoll;
  head_prepoll = p;

  return p;
}

static void
remove_prepoll (void *handlep)
{
  reactor_prepoll handle = (reactor_prepoll) handlep, prev = 0, this;

  /* Find this handle in the list. */
  for (this = head_prepoll;
       this && this != handle;
       prev = this, this = this->next)
    ;

  assert (this == handle);

  if (prev == 0) {		/* Remove head handler. */
    head_prepoll = head_prepoll->next;
  } else {			/* Remove inner handler. */
    prev->next = this->next;
  }
}

void
reactor_unregister_prepoll (reactor_prepoll handle)
{
  delete_pool (handle->pool);
}

void
reactor_invoke ()
{
  int i, r, a;
  reactor_prepoll prepoll;
  struct timeval tv;

#if 0
  /* Update the reactor time. */
  gettimeofday (&tv, 0);
  reactor_time = tv.tv_sec * 1000LL + tv.tv_usec / 1000;
#endif

  /* Fire any timers which are ready. */
  while (head_timer != 0 && head_timer->delta <= reactor_time)
    {
      reactor_timer timer;
      void (*fn) (void *);
      void *data;

      /* Calling fn might change head_timer. */
      timer = head_timer;
      fn = timer->fn;
      data = timer->data;

      /* Remove the timer from the queue now (this avoids a rare race
       * condition exposed if code calls pth_sleep followed immediately
       * by pth_exit).
       */
      reactor_unset_timer_early (timer);

      fn (data);
    }

  /* Run the prepoll handlers. This is tricky -- we have to check
   * (a) that we run every prepoll handler, even if new ones are
   * added while we are running them, and (b) that we don't accidentally
   * hit a prepoll handler which has been removed. The only thing we
   * can be sure of is that HEAD_PREPOLL is always valid. Anything it
   * points to can change any time we call a handler. Therefore this
   * is how we do it: (1) go through the list, marked all of the handlers
   * as not fired (ie. clearing the FIRED flag); (2) go through the list
   * looking for the first non-fired handle, mark it as fired and run
   * it; (3) repeat step (2) until there are no more non-fired handles.
   */
  for (prepoll = head_prepoll; prepoll; prepoll = prepoll->next)
    prepoll->fired = 0;

 prepoll_again:
  for (prepoll = head_prepoll;
       prepoll && prepoll->fired;
       prepoll = prepoll->next)
    ;

  if (prepoll)
    {
      prepoll->fired = 1;
      prepoll->fn (prepoll->data);
      goto prepoll_again;
    }

  /* Poll file descriptors. */
  if (nr_array_used >= 0)
    {
#if REACTOR_DEBUG
      fprintf (stderr, "reactor_invoke: poll [");
      for (i = 0; i < nr_array_used; ++i)
	fprintf (stderr, "(fd=%d, ops=0x%x)",
		 poll_array[i].fd, poll_array[i].events);
      fprintf (stderr, "]\n");
#endif

      r = poll (poll_array, nr_array_used,
		head_timer ? head_timer->delta - reactor_time : -1);

      /* Update the reactor time. */
      gettimeofday (&tv, 0);
      reactor_time = tv.tv_sec * 1000LL + tv.tv_usec / 1000;

#if REACTOR_DEBUG
      fprintf (stderr, "reactor_invoke: poll returned %d [", r);
      for (i = 0; i < nr_array_used; ++i)
	if (poll_array[i].revents != 0)
	  fprintf (stderr, "(fd=%d, ops=0x%x)",
		   poll_array[i].fd, poll_array[i].revents);
      fprintf (stderr, "]\n");
#endif

      if (r > 0)		/* Some descriptors are ready. */
	{
	  /* Check for events happening in the array, but go via the
	   * handles because there is no back pointer back to the
	   * handles from the array. Surprisingly enough, this code
	   * appears to be free of race conditions (note that calling
	   * fn can register/unregister handles).
	   */
	  for (i = 0; i < nr_handles_allocated; ++i)
	    {
	      a = handles[i].offset;

	      if (a >= 0 && poll_array[a].revents != 0)
		{
		  handles[i].fn (poll_array[a].fd, poll_array[a].revents,
				 handles[i].data);
		}
	    }
	}
      else if (r == 0)		/* The head timer has fired. */
	{
	  reactor_timer timer;
	  void (*fn) (void *);
	  void *data;

	  /* Calling fn might change head_timer. */
	  timer = head_timer;
	  fn = timer->fn;
	  data = timer->data;

	  /* Remove the timer from the queue now (this avoids a rare race
	   * condition exposed if code calls pth_sleep followed immediately
	   * by pth_exit).
	   */
	  reactor_unset_timer_early (timer);

	  fn (data);
	}
    }
}
