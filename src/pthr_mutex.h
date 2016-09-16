/* Simple mutex locks for pthrlib.
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
 * $Id: pthr_mutex.h,v 1.4 2002/12/01 14:29:28 rich Exp $
 */

#ifndef PTHR_MUTEX_H
#define PTHR_MUTEX_H

#include <pool.h>

#include "pthr_pseudothread.h"
#include "pthr_wait_queue.h"

struct mutex;
typedef struct mutex *mutex;

/* Function: new_mutex - mutual exclusion (mutex) locks
 * Function: mutex_enter
 * Function: mutex_leave
 * Function: mutex_try_enter
 * Function: mutex_nr_sleepers
 *
 * Mutex locks are simple: at most one pseudothread may enter the
 * critical area protected by the lock at once. If instead you
 * wish multiple reader / single writer semantics, then please
 * see @ref{new_rwlock(3)}.
 *
 * Mutex locks are automatically released if they are being held when
 * the thread exits.
 *
 * Note that there are possible deadlocks when using locks. To
 * avoid deadlocks, always ensure every thread acquires locks
 * in the same order.
 *
 * @code{new_mutex} creates a new mutex object.
 *
 * @code{mutex_enter} and @code{mutex_leave} enter and leave
 * the critical section. Only one thread can run at a time
 * inside the critical section.
 *
 * @code{mutex_try_enter} is identical to @code{mutex_enter}
 * except that it does not block if the lock is held by another
 * thread. The function returns true if the lock was successfully
 * acquired, or false if another thread is currently holding it.
 *
 * @code{mutex_nr_sleepers} returns the number of threads which
 * are queued up waiting to enter the critical section.
 *
 * Bugs: A common mistake is to accidentally call @code{mutex_leave}
 * when you are not holding the lock. This generally causes the
 * library to crash.
 */
extern mutex new_mutex (pool);
extern void mutex_enter (mutex);
extern void mutex_leave (mutex);
extern int mutex_try_enter (mutex);
extern int mutex_nr_sleepers (mutex);

#endif /* PTHR_MUTEX_H */
