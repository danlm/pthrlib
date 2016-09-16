/* HTTP library.
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
 * $Id: pthr_http.c,v 1.16 2003/02/02 18:05:31 rich Exp $
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <ctype.h>

#ifdef HAVE_SETJMP_H
#include <setjmp.h>
#endif

#ifdef HAVE_STRING_H
#include <string.h>
#endif

#ifdef HAVE_SYSLOG_H
#include <syslog.h>
#endif

#ifdef HAVE_TIME_H
#include <time.h>
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

#ifdef HAVE_ALLOCA_H
#include <alloca.h>
#endif

#include <pool.h>
#include <hash.h>
#include <vector.h>
#include <pstring.h>
#include <pre.h>

#include "pthr_reactor.h"
#include "pthr_iolib.h"
#include "pthr_cgi.h"
#include "pthr_http.h"

#define MAX_LINE_LENGTH 4096
#define CRLF "\r\n"

static const char *servername = PACKAGE "-httpd/" VERSION;
static FILE *log_fp = 0;

struct http_request
{
  pool pool;			/* Pool for memory allocations. */
  time_t t;			/* Request time. */
  int method;			/* Method. */
  const char *original_url;	/* Original request URL (used for logging). */
  const char *url;		/* The URL. */
  const char *path;		/* Path only. */
  const char *query_string;	/* Query string only. */
  int is_http09;		/* Is it an HTTP/0.9 request? */
  int major, minor;		/* Major/minor version numbers. */
  sash headers;			/* Headers. */
};

struct http_response
{
  pool pool;			/* Pool. */
  http_request request;		/* The original request. */
  int code;			/* Response code. */
  io_handle io;			/* IO handle. */
  unsigned extra_headers;	/* Bitmap of extra headers to send. */
#define _HTTP_XH_SERVER           1
#define _HTTP_XH_DATE             2
#define _HTTP_XH_CONTENT_TYPE     4
#define _HTTP_XH_CONNECTION       8
#define _HTTP_XH_CONTENT_LENGTH  16
#define _HTTP_XH_TRANSFER_ENCODING_CHUNKED 32
  int content_length;		/* Content length (if seen). */
};

#define _HTTP_XH_LENGTH_DEFINED (_HTTP_XH_CONTENT_LENGTH|_HTTP_XH_TRANSFER_ENCODING_CHUNKED)

static void parse_url (http_request h);
static void do_logging (http_response h);

const char *
http_get_servername (void)
{
  return servername;
}

const char *
http_set_servername (const char *new_server_name)
{
  return servername = new_server_name;
}

http_request
new_http_request (pool pool, io_handle io)
{
  char line[MAX_LINE_LENGTH];
  char *start_url, *end_url, *end_key;
  char *key, *value;

  http_request h = pmalloc (pool, sizeof *h);

  memset (h, 0, sizeof *h);
  h->pool = pool;
  h->headers = new_sash (h->pool);
  h->t = reactor_time / 1000;

  /* Read the first line of the request. As a sop to Netscape 4, ignore
   * blank lines (see note below about Netscape generating extra CRLFs
   * after POST requests).
   */
 again:
  if (!io_fgets (line, sizeof line, io, 0))
    return 0;
  if (line[0] == '\0') goto again;

  /* Previous versions of the server supported only GET requests. We
   * now support GET and HEAD (as required by RFC 2616 section 5.1.1)
   * and POST (see: http://www.w3.org/MarkUp/html-spec/html-spec_8.html).
   * See RFC 2616 section 14.7 for a description of the Allow header.
   */
  if (strncmp (line, "GET ", 4) == 0)
    {
      h->method = HTTP_METHOD_GET;
      start_url = line + 4;
    }
  else if (strncmp (line, "HEAD ", 5) == 0)
    {
      h->method = HTTP_METHOD_HEAD;
      start_url = line + 5;
    }
  else if (strncmp (line, "POST ", 5) == 0)
    {
      h->method = HTTP_METHOD_POST;
      start_url = line + 5;
    }
  else
    {
      const char *msg = "bad method (not a GET, HEAD or POST request)";

      syslog (LOG_INFO, msg);
      io_fputs ("HTTP/1.1 405 Method not allowed" CRLF
		"Allow: GET, HEAD, POST" CRLF
		CRLF, io);
      pth_die (msg);
    }

  /* Parse out the URL. According to RFC 2616 section 5.1.2 we ought
   * to be able to support absolute URIs in requests. At the moment
   * we can't. Luckily no HTTP/1.1 clients should generate them.
   */
  end_url = strchr (start_url, ' ');
  if (end_url == 0)
    {
      /* It's an HTTP/0.9 request! */
      h->original_url = h->url = pstrdup (h->pool, start_url);
      parse_url (h);
      h->is_http09 = 1;
      h->major = 0;
      h->minor = 9;

      return h;
    }

  /* It's an HTTP > 0.9 request, so there must be headers following. */
  *end_url = '\0';
  h->original_url = h->url = pstrdup (h->pool, start_url);
  parse_url (h);

  /* Check HTTP version number. */
  if (strncmp (end_url+1, "HTTP/", 5) != 0 ||
      !isdigit ((int) *(end_url+6)) ||
      *(end_url+7) != '.' ||
      !isdigit ((int) *(end_url+8)))
    {
      const char *msg = "badly formed request -- no HTTP/x.y";

      syslog (LOG_INFO, msg);
      io_fputs ("HTTP/1.1 400 Badly formed request" CRLF, io);
      pth_die (msg);
    }

  h->is_http09 = 0;
  h->major = *(end_url+6) - '0';
  h->minor = *(end_url+8) - '0';

  /* Read the headers. */
  for (;;)
    {
      if (!io_fgets (line, sizeof line, io, 0))
	{
	  const char *msg = "unexpected EOF reading headers";

	  syslog (LOG_INFO, msg);
	  io_fputs ("HTTP/1.1 400 Unexpected EOF in request" CRLF, io);
	  pth_die (msg);
	}

      /* End of headers? */
      if (line[0] == '\0')
	break;

      /* Check that the header has the form Key: Value. */
      end_key = strchr (line, ':');
      if (end_key == 0)
	{
	  const char *msg = "badly formed header in request";

	  syslog (LOG_INFO, msg);
	  io_fputs ("HTTP/1.1 400 Badly formed header" CRLF, io);
	  pth_die (msg);
	}

      /* Split up the key and value and store them. */
      *end_key = '\0';

      /* Find the beginning of the value field.
       * RFC822 / RFC2616 does not require a space after the colon
       * and effectively says to ignore linear white space after the
       * colon. So remove that here.
       */
      end_key++;
      ptrim (end_key);

      key = pstrdup (h->pool, line);
      value = pstrdup (h->pool, end_key);

      /* Canonicalize the key (HTTP header keys are case insensitive). */
      pstrlwr (key);

      sash_insert (h->headers, key, value);
    }

  return h;
}

/* This function is called just after h->url has been set. It
 * parses out the path and query string parameters from the URL
 * and stores them separately.
 */
static void
parse_url (http_request h)
{
  if (h->method == HTTP_METHOD_POST)
    {
      h->path = h->url;
      h->query_string = 0;
    }
  else				/* GET or HEAD requests. */
    {
      char *p, *t;

      p = pstrdup (h->pool, h->url);
      t = strchr (p, '?');

      if (t == 0)		/* No query string. */
	{
	  h->path = p;
	  h->query_string = 0;
	  return;
	}

      *t = '\0';
      h->path = p;		/* Path is everything before '?' char. */
      h->query_string = t+1;	/* Query string is everything after it. */
    }
}

time_t
http_request_time (http_request h)
{
  return h->t;
}

const char *
http_request_get_url (http_request h)
{
  return h->url;
}

void
http_request_set_url (http_request h, const char *url)
{
  h->url = url;
  parse_url (h);
}

const char *
http_request_path (http_request h)
{
  return h->path;
}

const char *
http_request_query_string (http_request h)
{
  return h->query_string;
}

int
http_request_method (http_request h)
{
  return h->method;
}

const char *
http_request_method_string (http_request h)
{
  switch (h->method)
    {
    case HTTP_METHOD_GET: return "GET";
    case HTTP_METHOD_HEAD: return "HEAD";
    case HTTP_METHOD_POST: return "POST";
    }
  abort ();
}

int
http_request_is_HEAD (http_request h)
{
  return h->method == HTTP_METHOD_HEAD;
}

void
http_request_version (http_request h, int *major, int *minor)
{
  *major = h->major;
  *minor = h->minor;
}

int
http_request_nr_headers (http_request h)
{
  return sash_size (h->headers);
}

vector
http_request_get_headers (http_request h)
{
  vector keys = sash_keys (h->headers);
  vector r = new_vector (h->pool, struct http_header);
  int i;

  for (i = 0; i < vector_size (keys); ++i)
    {
      struct http_header header;
      char *key;

      vector_get (keys, i, key);
      header.key = key;
      sash_get (h->headers, key, header.value);

      vector_push_back (r, header);
    }

  return r;
}

const char *
http_request_get_header (http_request h, const char *key)
{
  char *k = alloca (strlen (key) + 1);
  const char *v;

  /* Canonicalize the key. */
  strcpy (k, key);
  pstrlwr (k);

  sash_get (h->headers, k, v);

  return v;
}

const char *
http_request_get_cookie (http_request h, const char *key)
{
  const char *cookie_hdr;
  static pcre *re = 0;
  vector v;
  int keylen = strlen (key);
  int i;

  cookie_hdr = http_request_get_header (h, "Cookie");
  if (!cookie_hdr) return 0;

  /* Split it into pieces at whitespace, commas or semi-colons. */
  if (!re) re = precomp (global_pool, "[ \t\n,;]+", 0);
  v = pstrresplit (h->pool, cookie_hdr, re);

  for (i = 0; i < vector_size (v); ++i)
    {
      char *str;

      vector_get (v, i, str);

      /* It'll either be NAME=VALUE or $Path="?VALUE"?. We actually
       * don't care too much about the Path or Domain, so ignore them.
       */
      if (str[0] != '$')
	{
	  /* Matching name? */
	  if (strncasecmp (str, key, keylen) == 0
	      && str[keylen] == '=')
	    {
	      return cgi_unescape (h->pool, &str[keylen+1]);
	    }
	}
    }

  return 0;
}

http_response
new_http_response (pool pool, http_request request,
		   io_handle io,
		   int code, const char *msg)
{
  http_response h = pmalloc (pool, sizeof *h);

  memset (h, 0, sizeof *h);

  h->pool = pool;
  h->request = request;
  h->code = code;
  h->io = io;
  h->extra_headers = ~0;
  h->content_length = 0;

  /* Set the output mode to fully buffered for efficiency. */
  io_setbufmode (h->io, IO_MODE_FULLY_BUFFERED);

  /* HTTP/0.9? No response line or headers. */
  if (request->is_http09) return h;

  /* Write the response line. */
  io_fprintf (io, "HTTP/1.1 %d %s" CRLF, code, msg);

  return h;
}

void
http_response_send_header (http_response h,
			   const char *key, const char *value)
{
  /* HTTP/0.9? No response line or headers. */
  if (h->request->is_http09) return;

  io_fputs (key, h->io);
  io_fputs (": ", h->io);
  io_fputs (value, h->io);
  io_fputs (CRLF, h->io);

  /* Check for caller sending known header key and remove that
   * from the bitmap so we don't overwrite caller's header
   * in http_response_end_headers function.
   */
  if (strcasecmp (key, "Server") == 0)
    h->extra_headers &= ~_HTTP_XH_SERVER;
  if (strcasecmp (key, "Date") == 0)
    h->extra_headers &= ~_HTTP_XH_DATE;
  if (strcasecmp (key, "Content-Type") == 0)
    h->extra_headers &= ~_HTTP_XH_CONTENT_TYPE;
  if (strcasecmp (key, "Connection") == 0)
    h->extra_headers &= ~_HTTP_XH_CONNECTION;
  if (strcasecmp (key, "Content-Length") == 0)
    {
      h->extra_headers &= ~_HTTP_XH_CONTENT_LENGTH;
      sscanf (value, "%d", &h->content_length);
    }
  if (strcasecmp (key, "Transfer-Encoding") == 0 &&
      strcasecmp (value, "chunked") == 0)
    h->extra_headers &= ~_HTTP_XH_TRANSFER_ENCODING_CHUNKED;
}

void
http_response_send_headers (http_response h, ...)
{
  va_list args;
  const char *k, *v;

  /* HTTP/0.9? No response line or headers. */
  if (h->request->is_http09) return;

  va_start (args, h);
  while ((k = va_arg (args, const char *)) != 0)
    {
      v = va_arg (args, const char *);
      http_response_send_header (h, k, v);
    }
  va_end (args);
}

int
http_response_end_headers (http_response h)
{
  int close = 1;

  /* HTTP/0.9? No response line or headers. */
  if (h->request->is_http09)
    goto out;

  /* Send any remaining headers. */
  if (h->extra_headers & _HTTP_XH_SERVER)
    http_response_send_header (h, "Server", servername);

#if HAVE_STRFTIME && HAVE_GMTIME
  /* See RFC 2616 section 3.3.1. */
  if (h->extra_headers & _HTTP_XH_DATE)
    {
      time_t t;
      char s[64];

      t = http_request_time (h->request);
      strftime (s, sizeof s, "%a, %d %b %Y %H:%M:%S GMT", gmtime (&t));
      http_response_send_header (h, "Date", s);
    }
#endif

  /* This is not correct: see RFC 2616 section 3.4.1 for more details. */
  if (h->extra_headers & _HTTP_XH_CONTENT_TYPE)
    http_response_send_header (h, "Content-Type", "text/plain");

  if (h->extra_headers & _HTTP_XH_CONNECTION)
    {
      /* Deal with persistent connections (see RFC 2616 section 8.1). */

      /* Server app must have sent a Content-Length header, otherwise
       * the connection must close anyway. (RFC 2616 sections 4.4, 8.1.2.1)
       */
      if ((h->extra_headers & _HTTP_XH_LENGTH_DEFINED) ==
	  _HTTP_XH_LENGTH_DEFINED)
	{
	  close = 1;
	  http_response_send_header (h, "Connection", "close");
	}
      else
	{
	  /* Otherwise look for the Connection: header in the request. */
	  const char *conn_hdr
	    = http_request_get_header (h->request, "Connection");

	  if (conn_hdr)
	    {
	      if (strcasecmp (conn_hdr, "close") == 0)
		{
		  close = 1;
		  http_response_send_header (h, "Connection", "close");
		}
	      else if (strcasecmp (conn_hdr, "keep-alive") == 0)
		{
		  close = 0;
		  http_response_send_header (h, "Connection", "keep-alive");
		}
	      else
		{
		  close = 1;
		  http_response_send_header (h, "Connection", "close");
		}
	    }
	  else
	    {
	      /* Assume close for HTTP/1.0 clients, keep-alive for HTTP/1.1
	       * clients.
	       */
	      if (h->request->major == 1 && h->request->minor >= 1)
		{
		  close = 0;
		  http_response_send_header (h, "Connection", "keep-alive");
		}
	      else
		{
		  close = 1;
		  http_response_send_header (h, "Connection", "close");
		}
	    }
	}
    }

  io_fputs (CRLF, h->io);

 out:
  if (log_fp) do_logging (h);

  return close;
}

void
http_response_write_chunk (http_response h, const char *data, int length)
{
  io_fprintf (h->io, "%X" CRLF, length);
  io_fwrite (data, 1, length, h->io);
  io_fprintf (h->io, CRLF);
}

void
http_response_write_chunk_string (http_response h, const char *string)
{
  io_fprintf (h->io, "%X" CRLF "%s" CRLF, strlen (string), string);
}

void
http_response_write_chunk_end (http_response h)
{
  io_fputs ("0" CRLF, h->io);
}

FILE *
http_set_log_file (FILE *fp)
{
  return log_fp = fp;
}

FILE *
http_get_log_file (void)
{
  return log_fp;
}

static void
do_logging (http_response h)
{
#if HAVE_STRFTIME && HAVE_GMTIME
  char time_str[64];
  struct tm *tm;
  time_t t;
#else
  const char *time_str;
#endif
  const char *referer;
  const char *method;
  const char *url;
  const char *user_agent;
  int major, minor;
  struct sockaddr_in addr;
  socklen_t addrlen;
  const char *addr_str;
  int port;

  /* Get the request time. */
#if HAVE_STRFTIME && HAVE_GMTIME
  t = http_request_time (h->request);
  tm = gmtime (&t);
  strftime (time_str, sizeof time_str, "%Y/%m/%d %H:%M:%S", tm);
#else
  time_str = "- -";
#endif

  /* Get the referer (sic) header. */
  if ((referer = http_request_get_header (h->request, "Referer")) == 0)
    referer = "-";

  /* Get the user agent header. */
  if ((user_agent = http_request_get_header (h->request, "User-Agent")) == 0)
    user_agent = "-";

  /* Get the URL. */
  method = http_request_method_string (h->request);
  url = h->request->original_url;
  http_request_version (h->request, &major, &minor);

  /* Get the address and port number of the peer (client). */
  addrlen = sizeof addr;
  getpeername (io_fileno (h->io), (struct sockaddr *) &addr, &addrlen);
  addr_str = inet_ntoa (addr.sin_addr);
  port = ntohs (addr.sin_port);

  fprintf (log_fp,
	   "%s %s:%d \"%s %s HTTP/%d.%d\" %d %d \"%s\" \"%s\"\n",
	   time_str, addr_str, port, method, url, major, minor,
	   h->code, h->content_length, referer, user_agent);
  fflush (log_fp);
}
