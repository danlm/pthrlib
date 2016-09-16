/* Pseudothread server example.
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
 * $Id: pthr_eg2_server_main.c,v 1.4 2002/08/21 10:42:16 rich Exp $
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#ifdef HAVE_SIGNAL_H
#include <signal.h>
#endif

#ifdef HAVE_STRING_H
#include <string.h>
#endif

#include "src/pthr_server.h"
#include "pthr_eg2_server.h"

static void start_processor (int sock, void *data);
static void catch_quit_signal (int sig);

const char *root = "/tmp", *user = "nobody";

int
main (int argc, char *argv[])
{
  struct sigaction sa;

  /* Intercept signals. */
  memset (&sa, 0, sizeof sa);
  sa.sa_handler = catch_quit_signal;
  sa.sa_flags = SA_RESTART;
  sigaction (SIGINT, &sa, 0);
  sigaction (SIGQUIT, &sa, 0);
  sigaction (SIGTERM, &sa, 0);

  /* ... but ignore SIGPIPE errors. */
  sa.sa_handler = SIG_IGN;
  sa.sa_flags = SA_RESTART;
  sigaction (SIGPIPE, &sa, 0);

  /* Start up the server. */
  pthr_server_chroot (root);
  pthr_server_username (user);
  pthr_server_main_loop (argc, argv, start_processor);

  exit (0);
}

static void
start_processor (int sock, void *data)
{
  (void) new_eg2_server_processor (sock);
}

static void
catch_quit_signal (int sig)
{
  exit (0);
}
