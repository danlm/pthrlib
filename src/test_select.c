/* Test pth_select call (this also tests pth_poll, implicitly).
 * Copyright (C) 2001 Richard W.M. Jones <rich@annexia.org>
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
 * $Id: test_select.c,v 1.4 2003/02/05 22:13:33 rich Exp $
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

#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif

#ifdef HAVE_STRING_H
#include <string.h>
#endif

#include "pthr_reactor.h"
#include "pthr_pseudothread.h"

#define NR_WRITERS   4
#define NR_CHARS   100

static pool reader_pool;
static pseudothread reader_pth;
static pool writer_pool[NR_WRITERS];
static pseudothread writer_pth[NR_WRITERS];

static int fds[NR_WRITERS][2];

static void
writer (void *vp)
{
  int id = *(int *)vp;
  int i, fd = fds[id][1];
  char c[1] = { '0' + id };

  for (i = 0; i < NR_CHARS; ++i)
    {
      pth_write (fd, c, 1);
      pth_millisleep (3);
    }

  c[0] = '\xff';
  pth_write (fd, c, 1);
  close (fd);
}

static void
reader (void *vp)
{
  int i, running = NR_WRITERS, r, max_fd = -1;
  fd_set readfds, returnfds;
  struct timeval tv;

  FD_ZERO (&readfds);

  for (i = 0; i < NR_WRITERS; ++i)
    {
      int fd = fds[i][0];

      if (fd > max_fd) max_fd = fd;
      FD_SET (fd, &readfds);
    }

  while (running)
    {
      tv.tv_sec = 0;
      tv.tv_usec = 1000;
      returnfds = readfds;
      r = pth_select (max_fd+1, &returnfds, 0, 0, &tv);

      if (r == -1) abort ();
      if (r > 0)
	{
	  for (i = 0; i <= max_fd; ++i)
	    {
	      if (FD_ISSET (i, &returnfds))
		{
		  char c[1];

		  pth_read (i, c, 1);
		  if (c[0] == '\xff')
		    {
		      running--;
		      close (i);
		    }
		  else
		    {
		      putchar (c[0]); putchar ('\r'); fflush (stdout);
		    }
		}
	    }
	}
    }
}

int
main ()
{
  int i;

  for (i = 0; i < NR_WRITERS; ++i)
    {
      if (pipe (fds[i]) == -1) abort ();

      if (fcntl (fds[i][0], F_SETFL, O_NONBLOCK) == -1) abort ();
      if (fcntl (fds[i][1], F_SETFL, O_NONBLOCK) == -1) abort ();

      writer_pool[i] = new_subpool (global_pool);
      writer_pth[i] = new_pseudothread (writer_pool[i], writer, &i,
					"writer");
      pth_start (writer_pth[i]);
    }

  reader_pool = new_subpool (global_pool);
  reader_pth = new_pseudothread (reader_pool, reader, 0, "reader");
  pth_start (reader_pth);

  while (pseudothread_count_threads () > 0)
    reactor_invoke ();

  exit (0);
}
