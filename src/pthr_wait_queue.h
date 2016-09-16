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
 * $Id: pthr_wait_queue.h,v 1.3 2002/12/01 14:29:31 rich Exp $
 */

#ifndef PTHR_WAIT_QUEUE_H
#define PTHR_WAIT_QUEUE_H

#include <pool.h>

#include "pthr_pseudothread.h"

struct wait_queue;
typedef struct wait_queue *wait_queue;

/* Function: new_wait_queue - wait queues
 * Function: wq_wake_up
 * Function: wq_wake_up_one
 * Function: wq_sleep_on
 * Function: wq_nr_sleepers
 *
 * @code{new_wait_queue} creates a wait queue object.
 *
 * @code{wq_wake_up} wakes up all the threads which are currently
 * sleeping on the wait queue. Note that this function does not block,
 * and because pseudothreads are non-preemptive, none of the sleeping threads
 * will actually begin running until at least the current thread
 * blocks somewhere else.
 *
 * @code{wq_wake_up_one} wakes up just the first thread at the head
 * of the wait queue (the one which has been waiting the longest).
 *
 * @code{wq_sleep_on} sends the current thread to sleep on the wait
 * queue. This call blocks (obviously).
 *
 * @code{wq_nr_sleepers} returns the number of threads which are
 * currently asleep on the wait queue.
 *
 * Please read the HISTORY section below for some background into
 * how wait queues are implemented. This may help if you find there
 * are tricky race conditions in your code.
 *
 * History:
 *
 * Originally, wait queues were implemented using underlying Unix
 * pipes. This worked (to some extent) but the overhead of requiring
 * one pipe (ie. one inode, two file descriptors) per wait queue
 * made this implementation unacceptably heavyweight.
 *
 * Wait queues are now implemented using a simple hack in the reactor
 * which will be described below.
 *
 * Wait queues are subtle. Consider this example: Threads 1, 2 and 3 are
 * sleeping on a wait queue. Now thread 4 wakes up the queue. You would
 * expect (probably) threads 1, 2 and 3 to each be woken up and allowed
 * to start running. However, since this is a cooperatively multitasking
 * environment, it may happen that thread 1 wakes up first, does some
 * work and then goes back to sleep on the wait queue, all before threads
 * 2 and 3 have woken up. With a naive implementation of wait queues,
 * thread 4 might end up waking up thread 1 *again* (and even again after
 * that), never waking up threads 2 and 3 and ultimately starving those
 * threads.
 *
 * To avoid this situation, we might consider two possible alternatives:
 * either when thread 1 goes back to sleep, it goes to sleep on a
 * 'different' queue, or else thread 4 might take a copy of the wait
 * queue and delete the queue before it wakes any of the threads up.
 *
 * Another nasty situation which might arise in real life is this:
 * Again, threads 1, 2 and 3 are sleeping. Thread 4 wakes them up.
 * Thread 1, while processing its work, happens also to wake up the
 * same wait queue. What should happen to this second wake-up event?
 * Should it be ignored? Should it wake up threads 2 and 3? Should
 * it wake up any other threads which happen to have gone to sleep
 * on the queue after 1, 2 and 3? Or perhaps some combination of
 * these?
 *
 * The solution that we have come up with is as follows. A wait queue
 * consists of a simple list of threads which are sleeping on it. When
 * a thread wishes to sleep on the wait queue, it is added to this list,
 * and it switches back into the reactor context. When a thread wishes
 * to wake up all sleepers, it:
 * (a) copies the list of sleeping pseudothreads into its own private
 *     space
 * (b) clears the list of sleeping pseudothreads
 * (c) registers a prepoll handler to run which will wake up (ie. switch
 *     into the context of) each of these threads in turn
 * (d) continues to run to completion.
 * A thread which wishes to wake just one pseudothread works similarly
 * except that it only copies (and removes) a single item off the list.
 *
 * Note various invariant conditions: A thread cannot be entered on the
 * wait queue sleeping list more than once (because it cannot call
 * sleep_on when it is already sleeping). For similar reasons, a thread
 * cannot be entered on any of the multiple lists at the same time.
 * This implies that a thread cannot be woken up multiple times.
 *
 * The reader should satisfy themselves that this algorithm is free
 * of races, and solves all the problems outlined above. In addition, it
 * has the desirable property that wake_up* never sleeps.
 *
 */
extern wait_queue new_wait_queue (pool);
extern void wq_wake_up (wait_queue);
extern void wq_wake_up_one (wait_queue);
extern void wq_sleep_on (wait_queue);
extern int wq_nr_sleepers (wait_queue);

#endif /* PTHR_WAIT_QUEUE_H */
