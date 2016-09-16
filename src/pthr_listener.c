/* Listener thread.
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
 * $Id: pthr_listener.c,v 1.5 2002/12/08 13:41:07 rich Exp $
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>

#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif

#ifdef HAVE_UNISTD_H
#include <unistd.h>
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

#ifdef HAVE_SYSLOG_H
#include <syslog.h>
#endif

#include "pthr_pseudothread.h"
#include "pthr_listener.h"

struct listener
{
  pseudothread pth;
  int sock;
  void (*processor_fn) (int sock, void *data);
  void *data;
};

static void run (void *vp);

listener
new_listener (int sock,
	      void (*processor_fn) (int sock, void *data), void *data)
{
  pool pool;
  listener p;

  pool = new_pool ();
  p = pmalloc (pool, sizeof *p);

  p->sock = sock;
  p->processor_fn = processor_fn;
  p->data = data;
  p->pth = new_pseudothread (pool, run, p, "listener");

  pth_start (p->pth);

  return p;
}

static void
run (void *vp)
{
  listener p = (listener) vp;

  for (;;)
    {
      struct sockaddr_in addr;
      int sz, ns;

      /* Wait for the new connection. */
      sz = sizeof addr;
      ns = pth_accept (p->sock, (struct sockaddr *) &addr, &sz);

      if (ns < 0) { perror ("accept"); continue; }

      /* Set the new socket to non-blocking. */
      if (fcntl (ns, F_SETFL, O_NONBLOCK) < 0) abort ();

      /* Create a new processor thread to handle this connection. */
      p->processor_fn (ns, p->data);
    }
}
