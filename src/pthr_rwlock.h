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
 * $Id: pthr_rwlock.h,v 1.3 2002/12/01 14:29:30 rich Exp $
 */

#ifndef PTHR_RWLOCK_H
#define PTHR_RWLOCK_H

#include <pool.h>
#include <hash.h>

#include "pthr_pseudothread.h"
#include "pthr_wait_queue.h"

struct rwlock;
typedef struct rwlock *rwlock;

/* Function: new_rwlock - multiple reader / single writer locks (rwlocks)
 * Function: rwlock_writers_have_priority
 * Function: rwlock_readers_have_priority
 * Function: rwlock_enter_read
 * Function: rwlock_enter_write
 * Function: rwlock_try_enter_read
 * Function: rwlock_try_enter_write
 * Function: rwlock_leave
 *
 * Multiple reader / single writer locks (rwlocks) do what they
 * say. They allow either many readers to access a critical section
 * or a single writer (but not both). If instead you require simple
 * mutex semantics, then please see <pthr_mutex.h>.
 *
 * RWlocks are automatically released if they are being held when
 * the thread exits.
 *
 * RWlocks are not ``upgradable''. The implementation is too
 * complex to justify that.
 *
 * Note that there are possible deadlocks when using locks. To
 * avoid deadlocks, always ensure every thread acquires locks
 * in the same order.
 *
 * @code{new_rwlock} creates a new rwlock object.
 *
 * @code{rwlock_writers_have_priority} changes the nature of the lock so that
 * writers have priority over readers. If this is the case then
 * new readers will not be able to enter a critical section if
 * there are writers waiting to enter.
 * [NB: This is the default.]
 *
 * @code{rwlock_readers_have_priority} changes the nature of the lock so that
 * readers have priority over writers. Note that if this is the case
 * then writers are likely to be starved if the lock is frequently
 * read.
 *
 * @code{rwlock_enter_read} enters the critical section as a reader.
 * Any number of readers are allowed to enter a critical section at
 * the same time. This function may block.
 *
 * @code{rwlock_enter_write} enters the critical section as a writer.
 * Only a single writer is allowed to enter a critical section, and
 * then only if there are no readers. This function may block.
 *
 * @code{rwlock_try_enter_read} is identical to
 * @code{rwlock_enter_read}, but it
 * does not block. It returns true if the lock was successfully
 * acquired, or false if the operation would block.
 *
 * @code{rwlock_try_enter_write} is identical to
 * @code{rwlock_enter_write}, but it
 * does not block. It returns true if the lock was successfully
 * acquired, or false if the operation would block.
 *
 * @code{rwlock_leave} leaves the critical section.
 *
 * Bugs: A common mistake is to accidentally call @code{rwlock_leave}
 * when you are not holding the lock. This generally causes the
 * library to crash.
 */
extern rwlock new_rwlock (pool);
extern void rwlock_writers_have_priority (rwlock);
extern void rwlock_readers_have_priority (rwlock);
extern void rwlock_enter_read (rwlock);
extern void rwlock_enter_write (rwlock);
extern int rwlock_try_enter_read (rwlock);
extern int rwlock_try_enter_write (rwlock);
extern void rwlock_leave (rwlock);

#endif /* PTHR_RWLOCK_H */
