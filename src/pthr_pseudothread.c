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
 * $Id: pthr_pseudothread.c,v 1.22 2003/02/05 22:13:32 rich Exp $
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>

#ifdef HAVE_ASSERT_H
#include <assert.h>
#endif

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#ifdef HAVE_SETJMP_H
#include <setjmp.h>
#endif

#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif

#ifdef HAVE_STRING_H
#include <string.h>
#endif

#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif

#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif

#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif

#include <pool.h>
#include <vector.h>
#include <pstring.h>

#include "pthr_reactor.h"
#include "pthr_context.h"
#include "pthr_stack.h"
#include "pthr_pseudothread.h"

struct pseudothread
{
  /* Thread context and calling (reactor) context. */
  mctx_t thread_ctx, calling_ctx;

  /* Thread number. */
  int n;

  /* Pointer to thread stack and size. */
  void *stack;
  int stack_size;

  /* Alarm handling. */
  int alarm_received;
  reactor_timer alarm_timer;

  /* Pool for memory allocations in this thread. */
  pool pool;

  /* Name of the thread. */
  const char *name;

  /* Used to implement pth_exit. */
  jmp_buf exit_jmp;

  /* Used to implement pth_die. */
  vector exception_jmp_vec;
  const char *exception_msg;

  /* Used to implement pth_poll, pth_select. */
  int poll_timeout;

  /* Start point and data for thread. */
  void (*run) (void *);
  void *data;

  /* LANGUAGE environment variable for this thread. If null, then
   * the variable is unset in the thread.
   */
  const char *lang;

  /* TZ environment variable for this thread. If null, then the
   * variable is unset in the thread.
   */
  const char *tz;
};

/* Currently running pseudothread. */
pseudothread current_pth = 0;

/* Global list of threads. */
static vector threads = 0;

/* Default stack size, in bytes. */
static int default_stack_size = 65536;

static void block (int sock, int ops);
static void return_from_block (int sock, int events, void *);
static void _sleep (int timeout);
static void return_from_sleep (void *);
static void _poll (struct pollfd *fds, unsigned int n, int timeout);
static void return_from_poll (int sock, int events, void *);
static void return_from_poll_timeout (void *);
static void return_from_alarm (void *);

static void thread_trampoline (void *vpth);

static void pseudothread_init (void) __attribute__ ((constructor));

static void
pseudothread_init ()
{
#ifdef __OpenBSD__
  /* OpenBSD doesn't call constructors in the correct order. */
  pool global_pool = new_pool ();
#endif
  threads = new_vector (global_pool, struct pseudothread *);
}

int
pseudothread_set_stack_size (int size)
{
  return default_stack_size = size;
}

int
pseudothread_get_stack_size (void)
{
  return default_stack_size;
}

pseudothread
new_pseudothread (pool pool,
		  void (*run) (void *), void *data,
		  const char *name)
{
  pseudothread pth;
  void *stack_addr;
  int i;

  /* Allocate space for the pseudothread. */
  pth = pcalloc (pool, 1, sizeof *pth);

  pth->run = run;
  pth->data = data;
  pth->pool = pool;
  pth->name = name;

  /* Create a stack for this thread. */
  stack_addr = _pth_get_stack (default_stack_size);
  if (stack_addr == 0) abort ();
  pth->stack = stack_addr;
  pth->stack_size = default_stack_size;

  /* Create a new thread context. */
  mctx_set (&pth->thread_ctx,
	    thread_trampoline, pth,
	    stack_addr, default_stack_size);

  /* Allocate space in the global threads list for this thread. */
  for (i = 0; i < vector_size (threads); ++i)
    {
      pseudothread p;

      vector_get (threads, i, p);
      if (p == 0)
	{
	  pth->n = i;
	  vector_replace (threads, i, pth);
	  goto done;
	}
    }

  pth->n = i;
  vector_push_back (threads, pth);

 done:
  return pth;
}

static void
thread_trampoline (void *vpth)
{
  pseudothread pth = (pseudothread) vpth;
  mctx_t calling_ctx;
  void *stack;
  int stack_size;
  const struct pseudothread *null_thread = 0;

  /* Set up the current_pth before running user code. */
  current_pth = pth;

  if (setjmp (pth->exit_jmp) == 0)
    pth->run (pth->data);

  /* We return here either when "run" finishes normally or after
   * a longjmp caused by the pseudothread calling pth_exit.
   */

  calling_ctx = pth->calling_ctx;

  /* Remove the thread from the list of threads. */
  vector_replace (threads, pth->n, null_thread);

  /* Delete the pool and the stack. */
  stack = pth->stack;
  stack_size = pth->stack_size;
  delete_pool (pth->pool);
  _pth_return_stack (stack, stack_size);

  /* Restore calling context (this never returns ...). */
  mctx_restore (&calling_ctx);
}

void
pth_start (pseudothread pth)
{
  pseudothread old_pth = current_pth;

  /* Swap into the new context -- this actually calls thread_trampoline. */
  mctx_switch (&pth->calling_ctx, &pth->thread_ctx);

  /* Restore current_pth before returning into user code. */
  current_pth = old_pth;
}

void
pth_set_name (const char *name)
{
  current_pth->name = name;
}

const char *
pth_get_name (pseudothread pth)
{
  return pth->name;
}

int
pth_get_thread_num (pseudothread pth)
{
  return pth->n;
}

void
(*pth_get_run (pseudothread pth)) (void *)
{
  return pth->run;
}

void *
pth_get_data (pseudothread pth)
{
  return pth->data;
}

const char *
pth_get_language (pseudothread pth)
{
  return pth->lang;
}

const char *
pth_get_tz (pseudothread pth)
{
  return pth->tz;
}

void *
pth_get_stack (pseudothread pth)
{
  return pth->stack;
}

int
pth_get_stack_size (pseudothread pth)
{
  return pth->stack_size;
}

unsigned long
pth_get_PC (pseudothread pth)
{
  return mctx_get_PC (&pth->thread_ctx);
}

unsigned long
pth_get_SP (pseudothread pth)
{
  return mctx_get_SP (&pth->thread_ctx);
}

void
pth_exit ()
{
  longjmp (current_pth->exit_jmp, 1);
}

const char *
pth_catch (void (*fn) (void *), void *data)
{
  jmp_buf jb;

  if (setjmp (jb) == 0)
    {
      /* Register the exception handler. */
      if (current_pth->exception_jmp_vec == 0)
	current_pth->exception_jmp_vec
	  = new_vector (current_pth->pool, jmp_buf);
      vector_push_back (current_pth->exception_jmp_vec, jb);

      /* Run the function. */
      fn (data);

      /* No errors: pop the exception handler. */
      vector_pop_back (current_pth->exception_jmp_vec, jb);

      return 0;
    }

  /* Exception was fired off. Return the message. */
  return current_pth->exception_msg;
}

void
_pth_die (const char *msg, const char *filename, int lineno)
{
  /* If there is a surrounding exception handler registered, then
   * jump directly to it.
   */
  if (current_pth->exception_jmp_vec &&
      vector_size (current_pth->exception_jmp_vec) > 0)
    {
      jmp_buf jb;

      current_pth->exception_msg = msg;
      vector_pop_back (current_pth->exception_jmp_vec, jb);
      longjmp (jb, 1);
      /*NOTREACHED*/
    }

  /* Otherwise: print the message and exit the pseudothread immediately. */
  fprintf (stderr, "%s:%d: %s\n", filename, lineno, msg);
  pth_exit ();
}

pool
pth_get_pool (pseudothread pth)
{
  return pth->pool;
}

int
pth_accept (int s, struct sockaddr *addr, int *size)
{
  int r;

  do
    {
      block (s, REACTOR_READ);

      /* Accept the connection. */
      r = accept (s, addr, size);
    }
  while (r == -1 && errno == EWOULDBLOCK);

  return r;
}

int
pth_connect (int s, struct sockaddr *addr, int size)
{
  int r, sz;

  /* Connect (NB. The socket had better have been set to non-blocking) */
  r = connect (s, addr, size);

  if (r == 0) return 0;		/* Connected immediately. */
  else
    {
      if (errno == EINPROGRESS)
	{
	  /* Wait for the socket to connect. */
	  block (s, REACTOR_WRITE);

	  /* Read the error code (see connect(2) man page for details). */
	  sz = sizeof r;
	  if (getsockopt (s, SOL_SOCKET, SO_ERROR, &r, &sz) < 0)
	    return -1;

	  /* What is the error code? */
	  errno = r;
	  return r == 0 ? 0 : -1;
	}
      else
	{
	  /* Some other type of error before blocking. */
	  return -1;
	}
    }
}

ssize_t
pth_read (int s, void *buf, size_t count)
{
  int r;

 again:
  r = read (s, buf, count);
  if (r == -1 && errno == EWOULDBLOCK)
    {
      block (s, REACTOR_READ);
      goto again;
    }

  return r;
}

ssize_t
pth_write (int s, const void *buf, size_t count)
{
  int r;

 again:
  r = write (s, buf, count);
  if (r == -1 && errno == EWOULDBLOCK)
    {
      block (s, REACTOR_WRITE);
      goto again;
    }

  return r;
}

int
pth_sleep (int seconds)
{
  _sleep (seconds * 1000);
  return seconds;
}

int
pth_nanosleep (const struct timespec *req,
	       struct timespec *rem)
{
  int timeout = req->tv_sec * 1000 + req->tv_nsec / 1000000;

  _sleep (timeout);
  return 0;
}

int
pth_millisleep (int millis)
{
  _sleep (millis);
  return 0;
}

void
pth_timeout (int seconds)
{
  /* Unregister any previously registered alarm. */
  if (current_pth->alarm_timer)
    reactor_unset_timer_early (current_pth->alarm_timer);
  current_pth->alarm_timer = 0;

  if (seconds != 0)
    current_pth->alarm_timer =
      reactor_set_timer (current_pth->pool, seconds * 1000,
			 return_from_alarm, current_pth);
}

int
pth_send (int s, const void *msg, int len, unsigned int flags)
{
  int r;

 again:
  r = send (s, msg, len, flags);
  if (r == -1 && errno == EWOULDBLOCK)
    {
      block (s, REACTOR_WRITE);
      goto again;
    }

  return r;
}

int
pth_sendto (int s, const void *msg, int len, unsigned int flags,
	    const struct sockaddr *to, int tolen)
{
  int r;

 again:
  r = sendto (s, msg, len, flags, to, tolen);
  if (r == -1 && errno == EWOULDBLOCK)
    {
      block (s, REACTOR_WRITE);
      goto again;
    }

  return r;
}

int
pth_sendmsg (int s, const struct msghdr *msg, unsigned int flags)
{
  int r;

 again:
  r = sendmsg (s, msg, flags);
  if (r == -1 && errno == EWOULDBLOCK)
    {
      block (s, REACTOR_WRITE);
      goto again;
    }

  return r;
}

int
pth_recv (int s, void *buf, int len, unsigned int flags)
{
  int r;

 again:
  r = recv (s, buf, len, flags);
  if (r == -1 && errno == EWOULDBLOCK)
    {
      block (s, REACTOR_READ);
      goto again;
    }

  return r;
}

int
pth_recvfrom (int s, void *buf, int len, unsigned int flags,
	      struct sockaddr *from, int *fromlen)
{
  int r;

 again:
  r = recvfrom (s, buf, len, flags, from, fromlen);
  if (r == -1 && errno == EWOULDBLOCK)
    {
      block (s, REACTOR_READ);
      goto again;
    }

  return r;
}

int
pth_recvmsg (int s, struct msghdr *msg, unsigned int flags)
{
  int r;

 again:
  r = recvmsg (s, msg, flags);
  if (r == -1 && errno == EWOULDBLOCK)
    {
      block (s, REACTOR_READ);
      goto again;
    }

  return r;
}

/* NB. Although it may appear that this version of poll is
 * inefficient because it makes extra real poll(2) system calls,
 * in the case where there are many fds being polled, and they
 * are frequently ready (ie. high load cases ...), this will
 * be much more efficient than alternatives.
 */
int
pth_poll (struct pollfd *fds, unsigned int n, int timeout)
{
  int r;

 again:
  r = poll (fds, n, 0);
  if (r == 0)
    {
      _poll (fds, n, timeout);
      if (current_pth->poll_timeout) return 0;
      goto again;
    }

  return r;
}

/* Select is inefficiently implemented just as a library on top of
 * the pth_poll call. Rewrite your code so it doesn't use pth_select.
 */
int
pth_select (int n, fd_set *readfds, fd_set *writefds, fd_set *exceptfds,
	    struct timeval *timeout)
{
  pool p = new_subpool (current_pth->pool);
  vector v = new_vector (p, struct pollfd);
  int i, to, r;
  struct pollfd pfd, *fds;

  /* Convert the sets into an array of poll descriptors. */
  for (i = 0; i < n; ++i)
    {
      if (readfds && FD_ISSET (i, readfds))
	{
	  pfd.fd = i;
	  pfd.events = POLLIN;
	  pfd.revents = 0;
	  vector_push_back (v, pfd);
	}
      if (writefds && FD_ISSET (i, writefds))
	{
	  pfd.fd = i;
	  pfd.events = POLLOUT;
	  pfd.revents = 0;
	  vector_push_back (v, pfd);
	}
      if (exceptfds && FD_ISSET (i, exceptfds))
	{
	  pfd.fd = i;
	  pfd.events = POLLERR;
	  pfd.revents = 0;
	  vector_push_back (v, pfd);
	}
    }

  /* Call the underlying poll function. */
  to = timeout ? timeout->tv_sec * 1000 + timeout->tv_usec / 1000 : -1;
  vector_get_ptr (v, 0, fds);
  n = vector_size (v);

  r = pth_poll (fds, n, to);

  /* Error. */
  if (r == -1)
    {
      delete_pool (p);
      return -1;
    }

  if (readfds) FD_ZERO (readfds);
  if (writefds) FD_ZERO (writefds);
  if (exceptfds) FD_ZERO (exceptfds);

  /* Timeout. */
  if (r == 0)
    {
      delete_pool (p);
      return 0;
    }

  /* Convert the returned events into sets. */
  for (i = 0; i < n; ++i)
    {
      if (fds[i].revents & POLLIN)
	FD_SET (fds[i].fd, readfds);
      if (fds[i].revents & POLLOUT)
	FD_SET (fds[i].fd, writefds);
      if (fds[i].revents & POLLERR)
	FD_SET (fds[i].fd, exceptfds);
    }

  delete_pool (p);
  return r;
}

int
pth_wait_readable (int fd)
{
  int r;
  struct pollfd fds[1];

  fds[0].fd = fd;
  fds[0].events = POLLIN;
  fds[0].revents = 0;		/* Don't care. */

 again:
  r = poll (fds, 1, -1);
  if (r == 0)
    {
      block (fd, REACTOR_READ);
      goto again;
    }

  return r >= 0 ? 1 : -1;
}

int
pth_wait_writable (int fd)
{
  int r;
  struct pollfd fds[1];

  fds[0].fd = fd;
  fds[0].events = POLLOUT;
  fds[0].revents = 0;		/* Don't care. */

 again:
  r = poll (fds, 1, -1);
  if (r == 0)
    {
      block (fd, REACTOR_WRITE);
      goto again;
    }

  return r >= 0 ? 1 : -1;
}

#if defined(HAVE_SETENV) && defined(HAVE_UNSETENV)
#define do_setenv(name,value) setenv ((name), (value), 1)
#define do_unsetenv(name) unsetenv ((name))
#elif defined(HAVE_PUTENV)
/* This implementation is ugly, but then putenv is an ugly function. */

/* This array maps environment variable names to the 'putenv'-ed env
 * string. First time round we add a new env string to this array and
 * putenv it into the environment. Subsequently we modify the env
 * string.
 */
static struct putenv_map {
  struct putenv_map *next;
  const char *name;
  char env[256];
} *putenv_map = 0;

static inline void
do_setenv (const char *name, const char *value)
{
  struct putenv_map *m;

  for (m = putenv_map; m; m = m->next)
    if (strcmp (m->name, name) == 0)
      {
	/* Modify the env in-place. */
      finish:
	snprintf (m->env, sizeof m->env, "%s=%s", name, value);
	putenv (m->env);
	return;
      }

  /* Create a new entry. */
  m = pmalloc (global_pool, sizeof *m);
  m->next = putenv_map;
  m->name = name;
  goto finish;
}

#define do_unsetenv(name) putenv ((name))

#else
#error "no setenv/unsetenv or putenv in your libc"
#endif

static inline void
_restore_lang ()
{
  if (current_pth->lang == 0)
    do_unsetenv ("LANGUAGE");
  else
    do_setenv ("LANGUAGE", current_pth->lang);

#ifdef __GLIBC__
  {
    /* Please see gettext info file, node ``Being a `gettext' grok'', section
     * ``Changing the language at runtime''.
     */
    extern int _nl_msg_cat_cntr;
    _nl_msg_cat_cntr++;
  }
#endif
}

static inline void
_restore_tz ()
{
  if (current_pth->tz == 0)
    do_unsetenv ("TZ");
  else
    do_setenv ("TZ", current_pth->tz);
}

void
pth_set_language (const char *lang)
{
  current_pth->lang = pstrdup (current_pth->pool, lang);
  _restore_lang ();
}

void
pth_set_tz (const char *tz)
{
  current_pth->tz = pstrdup (current_pth->pool, tz);
  _restore_tz ();
}

inline void
_pth_switch_thread_to_calling_context ()
{
  mctx_switch (&current_pth->thread_ctx, &current_pth->calling_ctx);
}

inline void
_pth_switch_calling_to_thread_context (pseudothread pth)
{
  current_pth = pth;		/* Set current thread. */
  mctx_switch (&current_pth->calling_ctx, &current_pth->thread_ctx);
}

inline int
_pth_alarm_received ()
{
  return current_pth->alarm_received;
}

static void
block (int sock, int operations)
{
  /* Register a read event in the reactor. */
  reactor_handle handle
    = reactor_register (sock, operations, return_from_block, current_pth);

  /* Swap context back to the calling context. */
  _pth_switch_thread_to_calling_context ();

  /* Unregister the handle. */
  reactor_unregister (handle);

  /* Received alarm signal? - Exit. */
  if (_pth_alarm_received ())
    pth_exit ();

  /* Restore environment. */
  _restore_lang ();
  _restore_tz ();
}

static void
return_from_block (int sock, int events, void *vpth)
{
  /* Swap back to the thread context. */
  _pth_switch_calling_to_thread_context ((pseudothread) vpth);
}

static void
_sleep (int timeout)
{
  /* Register a timer in the reactor. */
  reactor_timer timer
    = reactor_set_timer (current_pth->pool, timeout,
			 return_from_sleep, current_pth);

  /* Swap context back to the calling context. */
  _pth_switch_thread_to_calling_context ();

  /* Received alarm signal? - Exit. */
  if (_pth_alarm_received ())
    {
      reactor_unset_timer_early (timer);
      pth_exit ();
    }

  /* Note: no need to unregister the timer, since it has fired. */

  /* Restore environment. */
  _restore_lang ();
  _restore_tz ();
}

static void
return_from_sleep (void *vpth)
{
  /* Swap back to the thread context. */
  _pth_switch_calling_to_thread_context ((pseudothread) vpth);
}

static void
_poll (struct pollfd *fds, unsigned int n, int timeout)
{
  reactor_timer timer = 0;
  reactor_handle handle[n];
  int i;

  (void) &timer;		/* Tell gcc not to put it in a register. */
  current_pth->poll_timeout = 0;

  /* Register all events in the reactor. */
  for (i = 0; i < n; ++i)
    /* NB: Poll operations == reactor operations. */
    handle[i] = reactor_register (fds[i].fd, fds[i].events,
				  return_from_poll, current_pth);

  /* Timeout? */
  if (timeout >= 0)
    timer = reactor_set_timer (current_pth->pool, timeout,
			       return_from_poll_timeout, current_pth);

  /* Swap context back to calling context. */
  _pth_switch_thread_to_calling_context ();

  /* Unregister all the handles. */
  for (i = 0; i < n; ++i)
    reactor_unregister (handle[i]);

  /* Delete the timer, if it exists but hasn't fired. */
  if (timer && !current_pth->poll_timeout)
    reactor_unset_timer_early (timer);

  /* Received alarm signal? Exit. */
  if (_pth_alarm_received ())
    pth_exit ();

  /* Restore environment. */
  _restore_lang ();
  _restore_tz ();
}

static void
return_from_poll (int sock, int events, void *vpth)
{
  /* Swap back to the thread context. */
  _pth_switch_calling_to_thread_context ((pseudothread) vpth);
}

static void
return_from_poll_timeout (void *vpth)
{
  pseudothread pth = (pseudothread) vpth;

  pth->poll_timeout = 1;

  /* Swap back to the thread context. */
  _pth_switch_calling_to_thread_context (pth);
}

static void
return_from_alarm (void *vpth)
{
  pseudothread pth = (pseudothread) vpth;

  pth->alarm_received = 1;
  pth->alarm_timer = 0;

  /* Swap back to the thread context. */
  _pth_switch_calling_to_thread_context (pth);
}

vector
pseudothread_get_threads (pool pool)
{
  vector v = new_vector (pool, struct pseudothread);
  int i;

  for (i = 0; i < vector_size (threads); ++i)
    {
      pseudothread pth;
      struct pseudothread pth_copy;

      vector_get (threads, i, pth);

      if (pth)
	{
	  /* Perform a deep copy of the structure. */
	  memcpy (&pth_copy, pth, sizeof pth_copy);
	  if (pth_copy.name)
	    pth_copy.name = pstrdup (pool, pth_copy.name);
	  if (pth_copy.lang)
	    pth_copy.lang = pstrdup (pool, pth_copy.lang);
	  if (pth_copy.tz)
	    pth_copy.tz = pstrdup (pool, pth_copy.tz);

	  vector_push_back (v, pth_copy);
	}
    }

  return v;
}

int
pseudothread_count_threads (void)
{
  int count = 0;
  int i;

  for (i = 0; i < vector_size (threads); ++i)
    {
      pseudothread pth;

      vector_get (threads, i, pth);

      if (pth) count++;
    }

  return count;
}
