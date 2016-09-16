/* Generic server process.
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
 * $Id: pthr_server.c,v 1.10 2003/02/05 22:13:33 rich Exp $
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

#ifdef HAVE_SYSLOG_H
#include <syslog.h>
#endif

#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif

#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif

#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif

#ifdef HAVE_ARPA_INET_H
#include <arpa/inet.h>
#endif

#ifdef HAVE_PWD_H
#include <pwd.h>
#endif

#ifdef HAVE_GRP_H
#include <grp.h>
#endif

#ifdef HAVE_STRING_H
#include <string.h>
#endif

#ifdef HAVE_EXECINFO_H
#include <execinfo.h>
#endif

#ifdef HAVE_SIGNAL_H
#include <signal.h>
#endif

#include "pthr_listener.h"
#include "pthr_server.h"

static int default_port = 80;
static char port_option_name = 'p';
static in_addr_t default_address = INADDR_ANY;
static char address_option_name = 'a';
static int disable_syslog = 0;
static const char *package_name = PACKAGE " " VERSION;
static int disable_fork = 0;
static int disable_chdir = 0;
static int disable_close = 0;
static const char *root = 0;
static const char *username = 0;
static const char *stderr_file = 0;
static void (*startup_fn)(int argc, char *argv[]) = 0;
static int enable_stack_trace_on_segv = 0;

#if defined(HAVE_EXECINFO_H) && defined(HAVE_BACKTRACE)
#define CAN_CATCH_SIGSEGV
#endif

#ifdef CAN_CATCH_SIGSEGV
static void catch_sigsegv (int);
#endif

extern char *optarg;
extern int optind;
extern int opterr;

#ifdef __OpenBSD__
extern int optreset;
#endif

#ifndef INADDR_NONE
#define INADDR_NONE ((in_addr_t) 0xffffffff)
#endif

void
pthr_server_main_loop (int argc, char *argv[],
		       void (*processor_fn) (int sock, void *))
{
  struct sockaddr_in addr;
  int port = default_port;
  in_addr_t address = default_address;
  int sock, one = 1;
  int c;
  char getopt_scr[10];

  /* Reset the getopt library. */
  optind = 1;
  opterr = 0;

#ifdef __OpenBSD__
  optreset = 1;
#endif

  /* Handle command-line arguments. Get the correct port and address to
   * listen on.
   */
  snprintf (getopt_scr, sizeof getopt_scr,
	    "%c:%c:", port_option_name, address_option_name);

  while ((c = getopt (argc, argv, getopt_scr)) != -1)
    {
      if (port_option_name == c)
	{
	  if (sscanf (optarg, "%d", &port) != 1)
	    {
	      fprintf (stderr, "invalid port option: %s\n", optarg);
	      exit (1);
	    }
	}
      else if (address_option_name == c)
	{
	  /* Should really use inet_aton() (or even inet_pton())
	   * but inet_addr() is more widely supported.
	   */
	  address = inet_addr (optarg);
	  if (INADDR_NONE == address)
	    {
	      fprintf (stderr, "invalid address: %s\n", optarg);
	      exit(1);
	    }
	}
    }

  /* Bind a socket to the appropriate port. */
  sock = socket (PF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (sock < 0) abort ();

  setsockopt (sock, SOL_SOCKET, SO_REUSEADDR, &one, sizeof (one));

  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = address;
  addr.sin_port = htons (port);
  if (bind (sock, (struct sockaddr *) &addr, sizeof addr) < 0)
    {
      /* Generally this means that the port is already bound. */
      perror ("bind");
      exit (1);
    }

  /* Put the socket into listen mode. */
  if (listen (sock, 10) < 0) abort ();

  /* Set the new socket to non-blocking. */
  if (fcntl (sock, F_SETFL, O_NONBLOCK) < 0) abort ();

  /* If running as root, and asked to chroot, then chroot. */
  if (root && geteuid () == 0)
    {
      if (chroot (root) == -1)
	{
	  perror (root);
	  exit (1);
	}
    }

  /* If running as root, and asked to change user, do so now. */
  if (username && geteuid () == 0)
    {
      struct passwd *pw = getpwnam (username);

      if (pw == 0)
	{
	  fprintf (stderr, "username not found: %s", username);
	  exit (1);
	}

      if (initgroups (username, pw->pw_gid) == -1 ||
	  setgid (pw->pw_gid) == -1 ||
	  setuid (pw->pw_uid) == -1)
	{
	  perror ("setuid");
	  exit (1);
	}
    }

  if (!disable_chdir)
    chdir ("/");

  if (!disable_close)
    {
      /* Close connections to the terminal. */
      close (0);
      close (1);
      close (2);
      open ("/dev/null", O_RDWR);
      dup2 (0, 1);
      dup2 (0, 2);
      setsid ();
    }

  if (stderr_file)
    {
      /* Reopen stderr on a log file. */
      close (2);
      if (open (stderr_file, O_WRONLY|O_CREAT|O_APPEND, 0644) == -1)
	{
	  /* Hard to output an error message at this point ... */
	  abort ();
	}
    }

  if (!disable_fork)
    {
      /* Fork into background. */
      int pid = fork ();

      if (pid < 0) { perror ("fork"); exit (1); }
      else if (pid > 0)
	{
	  /* Parent process: exit normally. */
	  exit (0);
	}
    }

  if (!disable_syslog)
    {
      /* Open connection to syslog. */
      openlog (package_name, LOG_PID|LOG_NDELAY, LOG_USER);
      syslog (LOG_INFO,
	      "%s starting up on port %d", package_name, port);
    }

  if (enable_stack_trace_on_segv)
    {
#ifdef CAN_CATCH_SIGSEGV
      struct sigaction sa;

      /* Print a stack trace to stderr if we get SIGSEGV. */
      memset (&sa, 0, sizeof sa);
      sa.sa_handler = catch_sigsegv;
      sa.sa_flags = 0;
      sigaction (SIGSEGV, &sa, 0);
#endif
    }

  /* Run the startup function, if any. */
  if (startup_fn)
    startup_fn (argc, argv);

  /* Start the listener thread. */
  (void) new_listener (sock, processor_fn, 0);

  /* Run the reactor. */
  while (pseudothread_count_threads () > 0)
    reactor_invoke ();
}

#ifdef CAN_CATCH_SIGSEGV
static void
catch_sigsegv (int sig)
{
#define MAX_ADDRS 50
  const char msg[] = "** Segmentation fault **\n\nStack trace:\n\n";
  void *addr[MAX_ADDRS];
  int n;

  write (2, msg, sizeof (msg) - 1);

  /* Write the stack trace to stderr. */
  n = backtrace (addr, MAX_ADDRS);
  backtrace_symbols_fd (addr, n, 2);

  /* Really abort. */
  abort ();
#undef MAX_ADDRS
}
#endif

void
pthr_server_default_port (int _default_port)
{
  default_port = _default_port;
}

void
pthr_server_port_option_name (char _port_option_name)
{
  port_option_name = _port_option_name;
}

void
pthr_server_default_address (in_addr_t _default_address)
{
  default_address = _default_address;
}

void
pthr_server_address_option_name (char _address_option_name)
{
  address_option_name = _address_option_name;
}

void
pthr_server_disable_syslog (void)
{
  disable_syslog = 1;
}

void
pthr_server_package_name (const char *_package_name)
{
  package_name = _package_name;
}

void
pthr_server_disable_fork (void)
{
  disable_fork = 1;
}

void
pthr_server_disable_chdir (void)
{
  disable_chdir = 1;
}

void
pthr_server_disable_close (void)
{
  disable_close = 1;
}

void
pthr_server_chroot (const char *_root)
{
  root = _root;
}

void
pthr_server_username (const char *_username)
{
  username = _username;
}

void
pthr_server_stderr_file (const char *_pathname)
{
  stderr_file = _pathname;
}

void
pthr_server_startup_fn (void (*_startup_fn) (int argc, char *argv[]))
{
  startup_fn = _startup_fn;
}

void
pthr_server_enable_stack_trace_on_segv (void)
{
  enable_stack_trace_on_segv = 1;
}
