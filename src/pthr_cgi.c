/* CGI library.
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
 * $Id: pthr_cgi.c,v 1.6 2003/02/02 18:05:30 rich Exp $
 */

#include "config.h"

#include <stdio.h>
#include <stdarg.h>

#ifdef HAVE_STRING_H
#include <string.h>
#endif

#ifdef HAVE_CTYPE_H
#include <ctype.h>
#endif

#include <pool.h>
#include <vector.h>
#include <hash.h>
#include <pstring.h>

#include "pthr_http.h"
#include "pthr_cgi.h"

static int post_max = -1;

static void parse_qs (cgi, const char *qs);
static void insert_param (cgi, char *name, char *value);

struct cgi
{
  pool pool;			/* Pool for allocations. */
  shash params;			/* Parameters (hash of string -> vector). */
};

int
cgi_get_post_max (void)
{
  return post_max;
}

int
cgi_set_post_max (int new_post_max)
{
  return post_max = new_post_max;
}

cgi
new_cgi (struct pool *pool, http_request h, io_handle io)
{
  cgi c = pmalloc (pool, sizeof *c);

  c->pool = pool;
  c->params = new_shash (pool, vector);

  if (http_request_method (h) != HTTP_METHOD_POST)
    {
      const char *qs = http_request_query_string (h);

      parse_qs (c, qs);
    }
  else				/* Method POST. */
    {
      const char *content_length_s =
	http_request_get_header (h, "Content-Length");
      const char *content_type = http_request_get_header (h, "Content-Type");
      int content_length = -1;
      const char std_type[] = "application/x-www-form-urlencoded";
      struct pool *tmp = new_subpool (pool);
      char *content;

      if (content_length_s &&
	  sscanf (content_length_s, "%d", &content_length) != 1)
	return 0;		/* Error in field format. */

      /* Content length too long? */
      if (post_max >= 0 && content_length >= 0 && content_length > post_max)
	return 0;		/* Content too long. */

      /* Check content type. If missing, assume it defaults to the
       * standard application/x-www-form-urlencoded.
       */
      if (content_type &&
	  strncasecmp (content_type, std_type, strlen (std_type)) != 0)
	return 0;		/* Unexpected/unknown content type. */

      /* Read the content, either to the end of input of else to the
       * expected length. Note that Netscape 4 sends an extra CRLF
       * after the POST data with is explicitly forbidden (see:
       * RFC 2616, section 4.1). We ignore these next time around when
       * we are reading the next request (see code in new_http_request).
       */
      if (content_length >= 0)
	{
	  content = pmalloc (tmp, content_length + 1);
	  if (io_fread (content, 1, content_length, io) < content_length)
	    return 0;

	  content[content_length] = '\0';
	}
      else
	{
	  char t[1024];
	  int r, n = 0;

	  content = pstrdup (tmp, "");

	  while ((r = io_fread (t, 1, 1024, io)) > 0)
	    {
	      n += r;
	      if (post_max >= 0 && n > post_max)
		return 0;	/* Content too long. */

	      t[r] = '\0';
	      content = pstrcat (tmp, content, t);
	    }
	}

      parse_qs (c, content);

      delete_pool (tmp);
    }

  return c;
}

static void
parse_qs (cgi c, const char *qs)
{
  vector v;
  int i;
  static char *one = "1";

  if (qs && qs[0] != '\0')
    {
      /* Parse the query string. */
      v = pstrcsplit (c->pool, qs, '&');

      for (i = 0; i < vector_size (v); ++i)
	{
	  char *param, *t;

	  vector_get (v, i, param);
	  t = strchr (param, '=');

	  /* No '=' char found? Assume it's a parameter of the form name=1. */
	  if (!t) { insert_param (c, param, one); continue; }

	  /* Split the string on the '=' character and set name and value. */
	  *t = '\0';
	  insert_param (c, param, t+1);
	}
    }
}

static void
insert_param (cgi c, char *name, char *value)
{
  vector v;
  char *s;

  if (!shash_get (c->params, name, v))
    v = new_vector (c->pool, char *);

  s = cgi_unescape (c->pool, value);
  vector_push_back (v, s);
  shash_insert (c->params, name, v);
}

vector
cgi_params (cgi c)
{
  return shash_keys (c->params);
}

const char *
cgi_param (cgi c, const char *name)
{
  vector v;
  char *s;

  if (!shash_get (c->params, name, v))
    return 0;

  vector_get (v, 0, s);
  return s;
}

const vector
cgi_param_list (cgi c, const char *name)
{
  vector v;

  if (!shash_get (c->params, name, v))
    return 0;

  return v;
}

int
cgi_erase (cgi c, const char *name)
{
  return shash_erase (c->params, name);
}

cgi
copy_cgi (pool npool, cgi c)
{
  const char *key, *value;
  int i, j;
  vector keys, values, v;

  cgi nc = pmalloc (npool, sizeof *nc);

  nc->pool = npool;
  nc->params = new_shash (npool, vector);

  keys = shash_keys_in_pool (c->params, npool);

  for (i = 0; i < vector_size (keys); ++i)
    {
      vector_get (keys, i, key);
      values = cgi_param_list (c, key);

      for (j = 0; j < vector_size (values); ++j)
	{
	  vector_get (values, j, value);
	  value = pstrdup (npool, value);

	  if (!shash_get (nc->params, key, v))
	    v = new_vector (npool, char *);

	  vector_push_back (v, value);
	  shash_insert (nc->params, key, v);
	}
    }

  return nc;
}

char *
cgi_escape (pool pool, const char *str)
{
  int i, j;
  int len = strlen (str);
  int new_len = 0;
  char *new_str;
  static const char hexdigits[] = "0123456789abcdef";

  /* Work out how long the escaped string will be. Escaped strings
   * get bigger.
   */
  for (i = 0; i < len; ++i)
    {
      if (isalnum ((int) str[i]) ||
	  str[i] == ',' || str[i] == '-' || str[i] == ' ')
	new_len++;
      else
	new_len += 3;
    }

  new_str = pmalloc (pool, new_len + 1);

  /* Escape the string. */
  for (i = 0, j = 0; i < len; ++i)
    {
      if (isalnum ((int) str[i]) ||
	  str[i] == ',' || str[i] == '-')
	new_str[j++] = str[i];
      else if (str[i] == ' ')
	new_str[j++] = '+';
      else
	{
	  new_str[j++] = '%';
	  new_str[j++] = hexdigits [(str[i] >> 4) & 0xf];
	  new_str[j++] = hexdigits [str[i] & 0xf];
	}
    }

  new_str[j++] = '\0';

  return new_str;
}

static inline int
hex_to_int (char c)
{
  if (c >= '0' && c <= '9') return c - '0';
  else if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  else if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  else return -1;
}

char *
cgi_unescape (pool pool, const char *str)
{
  int i, j;
  int len = strlen (str);
  char *new_str;

  /* Unescaped strings always get smaller. */
  new_str = pmalloc (pool, len + 1);

  for (i = 0, j = 0; i < len; ++i)
    {
      if (str[i] == '%')
	{
	  if (i+2 < len)
	    {
	      int a = hex_to_int (str[i+1]);
	      int b = hex_to_int (str[i+2]);

	      if (a >= 0 && b >= 0)
		{
		  new_str[j++] = a << 4 | b;
		  i += 2;
		}
	      else
		new_str[j++] = str[i];
	    }
	  else
	    new_str[j++] = str[i];
	}
      else if (str[i] == '+')
	new_str[j++] = ' ';
      else
	new_str[j++] = str[i];
    }

  new_str[j++] = '\0';

  return new_str;
}
