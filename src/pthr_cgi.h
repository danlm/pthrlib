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
 * $Id: pthr_cgi.h,v 1.3 2002/08/23 13:53:02 rich Exp $
 */

#ifndef PTHR_CGI_H
#define PTHR_CGI_H

#include <pool.h>

#include "pthr_http.h"
#include "pthr_iolib.h"

struct cgi;
typedef struct cgi *cgi;

/* Function: cgi_get_post_max - Get and set the internal POST_MAX parameter.
 * Function: cgi_set_post_max
 *
 * These functions get and set the internal POST_MAX parameter which
 * can be used to limit the size of @code{POST} method requests which
 * this library will handle. If set to a non-negative integer, then
 * POST requests will be limited to the number of bytes given. The
 * default is -1 (unlimited) which can leave the server open to denial
 * of service attacks.
 *
 * See also: @ref{new_cgi(3)}.
 */
extern int cgi_get_post_max (void);
extern int cgi_set_post_max (int new_post_max);

/* Function: new_cgi - Library for parsing CGI query strings.
 * Function: cgi_params
 * Function: cgi_param
 * Function: cgi_param_list
 * Function: cgi_erase
 * Function: copy_cgi
 *
 * @code{new_cgi} creates a new CGI object from an existing HTTP
 * request. It reads the query string or POSTed parameters and
 * parses them internally. CGI parameters are case sensitive, and
 * multiple parameters may be passed with the same name. Parameter
 * values are automatically unescaped by the library before you
 * get to see them.
 *
 * @code{cgi_params} returns a list of all the names of the parameters passed
 * to the script. The list is returned as a @code{vector} of
 * @code{char *}.
 *
 * @code{cgi_param} returns the value of a single named CGI parameter,
 * or @code{NULL} if there is no such parameter. If multiple parameters
 * were given with the same name, this returns one of them, essentially
 * at random.
 *
 * @code{cgi_param_list} returns the list of values of the named
 * CGI parameter. The list is returned as a @code{vector} of
 * @code{char *}.
 *
 * @code{cgi_erase} erases the named parameter. If a parameter
 * was erased, this returns true, else this returns false.
 *
 * @code{copy_cgi} copies @code{cgi} into pool @code{pool}.
 *
 * See also: @ref{cgi_get_post_max(3)}, @ref{cgi_escape(3)},
 * @ref{new_http_request(3)}.
 */
extern cgi new_cgi (pool, http_request, io_handle);
extern vector cgi_params (cgi);
extern const char *cgi_param (cgi, const char *name);
extern const vector cgi_param_list (cgi, const char *name);
extern int cgi_erase (cgi, const char *name);
extern cgi copy_cgi (pool, cgi);

/* Function: cgi_escape - %-escape and unescape CGI arguments.
 * Function: cgi_unescape
 *
 * These functions do %-escaping and unescaping on CGI arguments.
 * When %-escaping a string, @code{" "} is replaced by @code{"+"},
 * and non-printable
 * characters are replaced by @code{"%hh"} where @code{hh} is a
 * two-digit hex code. Unescaping a string reverses this process.
 *
 * See also: @ref{new_cgi(3)}, @ref{new_http_request(3)}.
 */
extern char *cgi_escape (pool, const char *str);
extern char *cgi_unescape (pool, const char *str);

#endif /* PTHR_CGI_H */
