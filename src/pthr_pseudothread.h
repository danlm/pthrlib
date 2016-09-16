/* Pseudothread handler.
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
 * $Id: pthr_pseudothread.h,v 1.13 2002/12/01 14:29:30 rich Exp $
 */

#ifndef PTHR_PSEUDOTHREAD_H
#define PTHR_PSEUDOTHREAD_H

#include <stdlib.h>

#include <sys/types.h>
#include <unistd.h>
#include <sys/socket.h>
#include <setjmp.h>
#include <time.h>

#include <pool.h>
#include <vector.h>

#include "pthr_reactor.h"
#include "pthr_context.h"

struct pseudothread;
typedef struct pseudothread *pseudothread;

/* Function: current_pth - current pseudothread
 *
 * @code{current_pth} is a global variable containing the currently
 * running thread.
 */
extern pseudothread current_pth;

/* Function: pseudothread_set_stack_size - set and get default stack size
 * Function: pseudothread_get_stack_size
 *
 * @code{pseudothread_set_stack_size} sets the stack
 * size for newly created threads. The default stack size
 * is 64 KBytes.
 *
 * @code{pseudothread_get_stack_size} returns the current
 * stack size setting.
 *
 * See also: @ref{new_pseudothread(3)}.
 */
extern int pseudothread_set_stack_size (int size);
extern int pseudothread_get_stack_size (void);

/* Function: new_pseudothread - lightweight "pseudothreads" library
 * Function: pth_start
 * Function: pseudothread_get_threads
 * Function: pseudothread_count_threads
 *
 * Pseudothreads are lightweight, cooperatively scheduled threads.
 *
 * @code{new_pseudothread} creates a new pseudothread. The thread
 * only starts running when you call @code{pth_start}. The @code{pool}
 * argument passed is used for all allocations within the thread.
 * This pool is automatically deleted when the thread exits.
 * The @code{name} argument is the name of the thread (used in
 * thread listings -- see @ref{pseudothread_get_threads(3)}). You
 * may change the name later. The @code{run} and @code{data}
 * arguments are the entry point into the thread. The entry
 * point is called as @code{run (data)}.
 *
 * @code{pseudothread_get_threads} returns a list of all the
 * currently running pseudothreads. This allows you to implement
 * a "process listing" for a program. The returned value
 * is a vector of pseudothread structures (not pointers). These
 * structures are opaque to you, but you can call the pth_get_*
 * functions on the address of each one.
 *
 * @code{pseudothread_count_threads} counts the number of currently
 * running threads.
 */
extern pseudothread new_pseudothread (pool, void (*run) (void *), void *data, const char *name);
extern void pth_start (pseudothread pth);
extern vector pseudothread_get_threads (pool);
extern int pseudothread_count_threads (void);

/* Function: pth_accept - pseudothread system calls
 * Function: pth_connect
 * Function: pth_read
 * Function: pth_write
 * Function: pth_sleep
 * Function: pth_millisleep
 * Function: pth_nanosleep
 * Function: pth_timeout
 *
 * @code{pth_accept}, @code{pth_connect}, @code{pth_read}, @code{pth_write},
 * @code{pth_sleep} and @code{pth_nanosleep} behave just like the
 * corresponding system calls. However these calls handle non-blocking
 * sockets and cause the thread to sleep on the reactor if it would
 * block. For general I/O you will probably wish to wrap up your
 * sockets in I/O handle objects, which give you a higher-level
 * buffered interface to sockets (see @ref{io_fdopen(3)}).
 *
 * @code{pth_millisleep} sleeps for a given number of milliseconds.
 *
 * @code{pth_timeout} is similar to the @ref{alarm(2)} system call: it
 * registers a timeout (in seconds). The thread will exit automatically
 * (even in the middle of a system call) if the timeout is reached.
 * To reset the timeout, call @code{pth_timeout} with a timeout of 0.
 *
 * See also: @ref{pth_poll(3)}, @ref{pth_select(3)}.
 */
extern int pth_accept (int, struct sockaddr *, int *);
extern int pth_connect (int, struct sockaddr *, int);
extern ssize_t pth_read (int, void *, size_t);
extern ssize_t pth_write (int, const void *, size_t);
extern int pth_sleep (int seconds);
extern int pth_millisleep (int millis);
extern int pth_nanosleep (const struct timespec *req, struct timespec *rem);
extern void pth_timeout (int seconds);

/* Function: pth_send - pseudothread network system calls
 * Function: pth_sendto
 * Function: pth_sendmsg
 * Function: pth_recv
 * Function: pth_recvfrom
 * Function: pth_recvmsg
 *
 * @code{pth_send}, @code{pth_sendto}, @code{pth_sendmsg},
 * @code{pth_recv}, @code{pth_recvfrom} and @code{pth_recvmsg}
 * behave just like the corresponding system calls. However
 * these calls handle non-blocking sockets and cause the
 * thread to sleep on the reactor if it would block.
 *
 * See also: @ref{pth_poll(3)}, @ref{pth_read(3)}, @ref{pth_write(3)}.
 */
extern int pth_send (int s, const void *msg, int len, unsigned int flags);
extern int pth_sendto (int s, const void *msg, int len, unsigned int flags, const struct sockaddr *to, int tolen);
extern int pth_sendmsg (int s, const struct msghdr *msg, unsigned int flags);
extern int pth_recv (int s, void *buf, int len, unsigned int flags);
extern int pth_recvfrom (int s, void *buf, int len, unsigned int flags, struct sockaddr *from, int *fromlen);
extern int pth_recvmsg (int s, struct msghdr *msg, unsigned int flags);

/* Function: pth_poll - pseudothread poll and select system calls
 * Function: pth_select
 *
 * @code{pth_poll} behaves like the @ref{poll(2)} system call. It
 * specifies an array @code{n} of @code{fds} file descriptor structures
 * each of which is checked for an I/O event. If @code{timeout} is
 * greater than or equal to zero, then the call will return after
 * this many milliseconds if no I/O events are detected. If @code{timeout}
 * is negative, then the timeout is infinite.
 *
 * @code{pth_select} behaves like the @ref{select(2)} system call.
 *
 * Note that @code{pth_select} is implemented as a library on top
 * of @code{pth_poll}, and is considerably less efficient than
 * @code{pth_poll}. It is recommended that you rewrite any code
 * which uses @code{pth_select}/@code{select} to use
 * @code{pth_poll}/@code{poll} instead.
 *
 * See also: @ref{pth_timeout(3)}, @ref{pth_wait_readable(3)}.
 */
extern int pth_poll (struct pollfd *fds, unsigned int n, int timeout);
extern int pth_select (int n, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout);

/* Function: pth_wait_readable - wait for a file descriptor to become readable or writable
 * Function: pth_wait_writable
 *
 * @code{pth_wait_readable} waits until a given file descriptor (or
 * socket) has data to read.
 *
 * @code{pth_wait_writable} waits until a given file descriptor (or
 * socket) can be written without blocking.
 *
 * The @code{fd} must be set to non-blocking.
 *
 * Returns: Both functions return 0 if OK, or -1 if there was an error.
 *
 * See also: @ref{pth_poll(3)}.
 */
extern int pth_wait_readable (int fd);
extern int pth_wait_writable (int fd);

/* Function: pth_exit - exit a pseudothread and exception handling
 * Function: pth_die
 * Function: pth_catch
 *
 * @code{pth_exit} causes the current thread to exit immediately.
 *
 * @code{pth_die} is similar in concept to @code{pth_exit}, except
 * that it throws an exception which may be caught by using the
 * @code{pth_catch} function. The distinction between @code{pth_die}
 * and @code{pth_exit} is the same as the distinction between
 * the Perl functions @code{die} and @code{exit}, in as much as
 * @code{exit} in Perl always exits the process immediately, and
 * @code{die} in Perl generally exits the process immediately
 * unless the programmer catches the exception with @code{eval}
 * and handles it appropriately.
 *
 * @code{pth_catch} is used to catch exceptions thrown by
 * @code{pth_die}. You give @code{fn} (function) and @code{data}
 * arguments, and the function calls @code{fn (data)}. If, during
 * this call, the code calls @code{pth_die}, then the exception
 * message is immediately returned from @code{pth_catch}. If
 * the code runs successfully to completion, then @code{pth_catch}
 * will return @code{NULL}.
 *
 * Exceptions may be nested.
 *
 * Note that @code{pth_catch} will not catch calls to @code{pth_exit}.
 */
extern void pth_exit (void) __attribute__((noreturn));
#define pth_die(msg) _pth_die ((msg), __FILE__, __LINE__)
extern const char *pth_catch (void (*fn) (void *), void *data);

extern void _pth_die (const char *msg,
		      const char *filename, int lineno)
     __attribute__((noreturn));

/* Function: pth_get_pool - get and set status of pseudothreads
 * Function: pth_get_name
 * Function: pth_get_thread_num
 * Function: pth_get_run
 * Function: pth_get_data
 * Function: pth_get_language
 * Function: pth_get_tz
 * Function: pth_get_stack
 * Function: pth_get_stack_size
 * Function: pth_get_PC
 * Function: pth_get_SP
 * Function: pth_set_name
 * Function: pth_set_language
 * Function: pth_set_tz
 *
 * @code{pth_get_pool} returns the pool associated with this thread.
 * The thread should use this pool for all allocations, ensuring that
 * they are cleaned up correctly when the thread is deleted. See
 * @ref{new_pool(3)}.
 *
 * @code{pth_get_name} returns the name of the thread. Note that
 * this string is normally stored in thread's local pool, and therefore
 * the string memory may be unexpected deallocated if the thread
 * exits. Callers should take a copy of the string in their own pool
 * if they are likely to need the string for a long period of time.
 *
 * @code{pth_get_thread_num} returns the thread number (roughly
 * the equivalent of a process ID).
 *
 * @code{pth_get_run} and @code{pth_get_data} return the original
 * entry point information of the thread.
 *
 * @code{pth_get_language} returns the value of the @code{LANGUAGE}
 * environment variable for this thread. See the discussion
 * of @code{pth_set_language} below.
 *
 * @code{pth_get_language} returns the value of the @code{TZ}
 * environment variable for this thread. See the discussion
 * of @code{pth_set_tz} below.
 *
 * @code{pth_get_stack} returns the top of the stack for this
 * thread.
 *
 * @code{pth_get_stack_size} returns the maximum size of the stack for
 * this thread.
 *
 * @code{pth_get_PC} returns the current program counter (PC) for
 * this thread. Obviously it only makes sense to call this from
 * another thread.
 *
 * @code{pth_get_SP} returns the current stack pointer (SP) for
 * this thread. Obviously it only makes sense to call this from
 * another thread.
 *
 * @code{pth_set_name} changes the name of the thread. The string
 * @code{name} argument passed must be either statically allocated,
 * or allocated in the thread's own pool (the normal case), or else
 * allocated in another pool with a longer life than the current
 * thread.
 *
 * @code{pth_set_language} changes the per-thread @code{LANGUAGE}
 * environment variable. This is useful when serving clients from
 * multiple locales, and using GNU gettext for translation.
 *
 * @code{pth_set_tz} changes the per-thread @code{TZ}
 * environment variable. This is useful when serving clients from
 * multiple timezones, and using @code{localtime} and related
 * functions.
 */
extern pool pth_get_pool (pseudothread pth);
extern const char *pth_get_name (pseudothread pth);
extern int pth_get_thread_num (pseudothread pth);
extern void (*pth_get_run (pseudothread pth)) (void *);
extern void *pth_get_data (pseudothread pth);
extern const char *pth_get_language (pseudothread pth);
extern const char *pth_get_tz (pseudothread pth);
extern void *pth_get_stack (pseudothread pth);
extern int pth_get_stack_size (pseudothread pth);
extern unsigned long pth_get_PC (pseudothread pth);
extern unsigned long pth_get_SP (pseudothread pth);
extern void pth_set_name (const char *name);
extern void pth_set_language (const char *lang);
extern void pth_set_tz (const char *tz);

/* These low-level functions are used by other parts of the pthrlib library.
 * Do not use them from user programs. They switch thread context with the
 * calling context and v.v.
 */
extern void _pth_switch_thread_to_calling_context (void);
extern void _pth_switch_calling_to_thread_context (pseudothread new_pth);
extern int  _pth_alarm_received (void);

#endif /* PTHR_PSEUDOTHREAD_H */
