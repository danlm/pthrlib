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
 * $Id: pthr_reactor.h,v 1.3 2002/08/21 10:42:20 rich Exp $
 */

#ifndef PTHR_REACTOR_H
#define PTHR_REACTOR_H

#include <sys/poll.h>

#include <pool.h>

/* A reactor handle. */
struct reactor_handle;
typedef int reactor_handle;	/* These are offsets into a private array
				 * of struct reactor_handle.
				 */

/* A timer. */
struct reactor_timer;
typedef struct reactor_timer *reactor_timer;

/* Pre-poll handlers. */
struct reactor_prepoll;
typedef struct reactor_prepoll *reactor_prepoll;

/* Reactor operations. */
#define REACTOR_READ  POLLIN
#define REACTOR_WRITE POLLOUT

/* Reactor time types. */
typedef unsigned long long reactor_time_t;
typedef signed long long reactor_timediff_t;

/* Reactor time in milliseconds from Unix epoch. */
extern reactor_time_t reactor_time;

/* Reactor functions. */
extern reactor_handle reactor_register (int socket, int operations,
					void (*fn) (int socket, int events,
						    void *data),
					void *data);
extern void reactor_unregister (reactor_handle handle);
extern reactor_timer reactor_set_timer (pool, int timeout,
					void (*fn) (void *data),
					void *data);
extern void reactor_unset_timer_early (reactor_timer timer);
extern reactor_prepoll reactor_register_prepoll (pool, void (*fn) (void *data),
						 void *data);
extern void reactor_unregister_prepoll (reactor_prepoll handle);
extern void reactor_invoke (void);

#endif /* PTHR_REACTOR_H */
