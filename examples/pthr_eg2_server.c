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
 * $Id: pthr_eg2_server.c,v 1.7 2003/02/05 22:13:32 rich Exp $
 */

#include "config.h"

#include <stdio.h>

#ifdef HAVE_ALLOCA_H
#include <alloca.h>
#endif

#ifdef HAVE_STRING_H
#include <string.h>
#endif

#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif

#ifdef HAVE_DIRENT_H
#include <dirent.h>
#endif

#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif

#ifdef HAVE_SYS_SYSLIMITS_H
#include <sys/syslimits.h>
#endif

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include <pool.h>
#include <pstring.h>

#include "src/pthr_pseudothread.h"
#include "src/pthr_iolib.h"
#include "src/pthr_http.h"
#include "src/pthr_cgi.h"
#include "pthr_eg2_server.h"

struct eg2_server_processor
{
  /* Pseudothread handle. */
  pseudothread pth;

  /* Socket. */
  int sock;

  /* Pool for memory allocations. */
  struct pool *pool;

  /* HTTP request. */
  http_request http_request;

  /* IO handle. */
  io_handle io;
};

static int file_not_found_error (eg2_server_processor p);
static int moved_permanently (eg2_server_processor p, const char *);
static int serve_directory (eg2_server_processor p, const char *, const struct stat *);
static int serve_file (eg2_server_processor p, const char *, const struct stat *);
static void run (void *vp);

eg2_server_processor
new_eg2_server_processor (int sock)
{
  pool pool;
  eg2_server_processor p;

  pool = new_pool ();
  p = pmalloc (pool, sizeof *p);

  p->pool = pool;
  p->sock = sock;
  p->pth = new_pseudothread (pool, run, p, "eg2_server_processor");

  pth_start (p->pth);

  return p;
}

static void
run (void *vp)
{
  eg2_server_processor p = (eg2_server_processor) vp;
  int close = 0;
  const char *path;
  struct stat statbuf;

  p->io = io_fdopen (p->sock);

  /* Sit in a loop reading HTTP requests. */
  while (!close)
    {
      /* ----- HTTP request ----- */
      p->http_request = new_http_request (p->pool, p->io);
      if (p->http_request == 0)	/* Normal end of file. */
        break;

      /* Get the path and locate the file. */
      path = http_request_path (p->http_request);
      if (stat (path, &statbuf) == -1)
	{
	  close = file_not_found_error (p);
	  continue;
	}

      /* File or directory? */
      if (S_ISDIR (statbuf.st_mode))
	{
	  close = serve_directory (p, path, &statbuf);
	  continue;
	}
      else if (S_ISREG (statbuf.st_mode))
	{
	  close = serve_file (p, path, &statbuf);
	  continue;
	}
      else
	{
	  close = file_not_found_error (p);
	  continue;
	}
    }

  io_fclose (p->io);

  pth_exit ();
}

static int
file_not_found_error (eg2_server_processor p)
{
  http_response http_response;
  int close;

  http_response = new_http_response (p->pool, p->http_request, p->io,
				     404, "File or directory not found");
  http_response_send_headers (http_response,
			      /* Content type. */
			      "Content-Type", "text/html",
			      NO_CACHE_HEADERS,
			      /* End of headers. */
			      NULL);
  close = http_response_end_headers (http_response);

  if (http_request_is_HEAD (p->http_request)) return close;

  io_fprintf (p->io,
	      "<html><head><title>File or directory not found</title></head>" CRLF
	      "<body bgcolor=\"#ffffff\">" CRLF
	      "<h1>404 File or directory not found</h1>" CRLF
	      "The file you requested was not found on this server." CRLF
	      "</body></html>" CRLF);

  return close;
}

int
moved_permanently (eg2_server_processor p, const char *location)
{
  http_response http_response;
  int close;

  http_response = new_http_response (p->pool, p->http_request, p->io,
				     301, "Moved permanently");
  http_response_send_headers (http_response,
			      /* Content length. */
			      "Content-Length", "0",
			      /* Location. */
			      "Location", location,
			      /* End of headers. */
			      NULL);
  close = http_response_end_headers (http_response);

  if (http_request_is_HEAD (p->http_request)) return close;

  return close;
}

static int
serve_directory (eg2_server_processor p, const char *path,
		 const struct stat *statbuf)
{
  http_response http_response;
  int close;
  DIR *dir;
  struct dirent *d;

#ifndef NAME_MAX
  /* Solaris defines NAME_MAX on a per-filesystem basis.
   * See: http://lists.spine.cx/archives/everybuddy/2002-May/001419.html
   */
  long NAME_MAX = pathconf (path, _PC_NAME_MAX);
#endif

  /* If the path doesn't end with a "/", then we need to send
   * a redirect back to the client so it refetches the page
   * with "/" appended.
   */
  if (path[strlen (path)-1] != '/')
    {
      char *location = psprintf (p->pool, "%s/", path);
      return moved_permanently (p, location);
    }

  dir = opendir (path);
  if (dir == 0)
    return file_not_found_error (p);

  http_response = new_http_response (p->pool, p->http_request, p->io,
				     200, "OK");
  http_response_send_headers (http_response,
			      /* Content type. */
			      "Content-Type", "text/html",
			      NO_CACHE_HEADERS,
			      /* End of headers. */
			      NULL);
  close = http_response_end_headers (http_response);

  if (http_request_is_HEAD (p->http_request)) return close;

  io_fprintf (p->io,
	      "<html><head><title>Directory: %s</title></head>" CRLF
	      "<body bgcolor=\"#ffffff\">" CRLF
	      "<h1>Directory: %s</h1>" CRLF
	      "<table>" CRLF
	      "<tr><td></td><td></td>"
	      "<td><a href=\"..\">Parent directory</a></td></tr>" CRLF,
	      path, path);

  while ((d = readdir (dir)) != 0)
    {
      if (d->d_name[0] != '.')	/* Ignore hidden files. */
	{
	  const char *filename;
	  struct stat fstatbuf;

	  /* Generate the full pathname to this file. */
	  filename = psprintf (p->pool, "%s/%s", path, d->d_name);

	  /* Stat the file to find out what it is. */
	  if (lstat (filename, &fstatbuf) == 0)
	    {
	      const char *type;
	      int size;

	      if (S_ISDIR (fstatbuf.st_mode))
		type = "dir";
	      else if (S_ISREG (fstatbuf.st_mode))
		type = "file";
	      else if (S_ISLNK (fstatbuf.st_mode))
		type = "link";
	      else
		type = "special";

	      size = fstatbuf.st_size;

	      /* Print the details. */
	      io_fprintf (p->io,
			  "<tr><td>[ %s ]</td><td align=right>%d</td>"
			  "<td><a href=\"%s%s\">%s</a>",
			  type, size,
			  d->d_name,
			  S_ISDIR (fstatbuf.st_mode) ? "/" : "",
			  d->d_name);

	      if (S_ISLNK (fstatbuf.st_mode))
		{
		  char link[NAME_MAX+1];
		  int r;

		  r = readlink (filename, link, NAME_MAX);
		  if (r >= 0) link[r] = '\0';
		  else strcpy (link, "unknown");

		  io_fprintf (p->io, " -&gt; %s", link);
		}

	      io_fputs ("</td></tr>" CRLF, p->io);
	    }
	}
    }

  io_fprintf (p->io,
	      "</table></body></html>" CRLF);

  return close;
}

static int
serve_file (eg2_server_processor p, const char *path,
	    const struct stat *statbuf)
{
  http_response http_response;
  const int n = 4096;
  char *buffer = alloca (n);
  int cl, fd, r;
  char *content_length = pitoa (p->pool, statbuf->st_size);

  fd = open (path, O_RDONLY);
  if (fd < 0)
    return file_not_found_error (p);

  http_response = new_http_response (p->pool, p->http_request, p->io,
				     200, "OK");
  http_response_send_headers (http_response,
			      /* Content type. */
			      "Content-Type", "text/plain",
			      "Content-Length", content_length,
			      /* End of headers. */
			      NULL);
  cl = http_response_end_headers (http_response);

  if (http_request_is_HEAD (p->http_request)) return cl;

  while ((r = read (fd, buffer, n)) > 0)
    {
      io_fwrite (buffer, r, 1, p->io);
    }

  if (r < 0)
    perror ("read");

  close (fd);

  return cl;
}
