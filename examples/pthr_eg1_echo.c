/* Pseudothread echo example.
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
 * $Id: pthr_eg1_echo.c,v 1.6 2002/12/01 14:57:36 rich Exp $
 */

#include "config.h"

#include <stdio.h>

#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif

#ifdef HAVE_SYSLOG_H
#include <syslog.h>
#endif

#include <pool.h>

#include "src/pthr_pseudothread.h"
#include "src/pthr_iolib.h"
#include "src/pthr_http.h"
#include "src/pthr_cgi.h"
#include "pthr_eg1_echo.h"

struct eg1_echo_processor
{
  /* Pseudothread handle. */
  pseudothread pth;

  /* Socket. */
  int sock;
};

static void run (void *vp);

eg1_echo_processor
new_eg1_echo_processor (int sock)
{
  pool pool;
  eg1_echo_processor p;

  pool = new_pool ();
  p = pmalloc (pool, sizeof *p);

  p->sock = sock;
  p->pth = new_pseudothread (pool, run, p, "eg1_echo_processor");

  pth_start (p->pth);

  return p;
}

static void
run (void *vp)
{
  eg1_echo_processor p = (eg1_echo_processor) vp;
  int close = 0;
  http_request http_request;
  cgi cgi;
  http_response http_response;
  pool pool = pth_get_pool (p->pth);
  io_handle io;
  int i;

  io = io_fdopen (p->sock);

  /* Sit in a loop reading HTTP requests. */
  while (!close)
    {
      vector headers, params;
      struct http_header header;
      const char *name, *value;

      /* ----- HTTP request ----- */
      http_request = new_http_request (pool, io);
      if (http_request == 0)	/* Normal end of file. */
        break;

      cgi = new_cgi (pool, http_request, io);
      if (cgi == 0)		/* XXX Should send an error here. */
	break;

      /* ----- HTTP response ----- */
      http_response = new_http_response (pool, http_request,
					 io,
					 200, "OK");
      http_response_send_header (http_response,
                                 "Content-Type", "text/plain");
      close = http_response_end_headers (http_response);

      if (!http_request_is_HEAD (http_request))
	{
	  io_fprintf (io, "Hello. This is your server.\r\n\r\n");
	  io_fprintf (io, "Your browser sent the following headers:\r\n");

	  headers = http_request_get_headers (http_request);
	  for (i = 0; i < vector_size (headers); ++i)
	    {
	      vector_get (headers, i, header);
	      io_fprintf (io, "\t%s: %s\r\n", header.key, header.value);
	    }

	  io_fprintf (io, "----- end of headers -----\r\n");

	  io_fprintf (io, "The URL was: %s\r\n",
		      http_request_get_url (http_request));
	  io_fprintf (io, "The path component was: %s\r\n",
		      http_request_path (http_request));
	  io_fprintf (io, "The query string was: %s\r\n",
		      http_request_query_string (http_request));
	  io_fprintf (io, "The query arguments were:\r\n");

	  params = cgi_params (cgi);
	  for (i = 0; i < vector_size (params); ++i)
	    {
	      vector_get (params, i, name);
	      value = cgi_param (cgi, name);
	      io_fprintf (io, "\t%s=%s\r\n", name, value);
	    }

	  io_fprintf (io, "----- end of parameters -----\r\n");
	}
    }

  io_fclose (io);

  pth_exit ();
}
