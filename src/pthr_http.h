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
 * $Id: pthr_http.h,v 1.15 2002/12/08 13:41:07 rich Exp $
 */

#ifndef PTHR_HTTP_H
#define PTHR_HTTP_H

#include <stdio.h>
#include <stdarg.h>

#include <setjmp.h>
#include <time.h>

#include <pool.h>
#include <vector.h>
#include <hash.h>

#include "pthr_pseudothread.h"
#include "pthr_iolib.h"

struct http_request;
typedef struct http_request *http_request;

struct http_response;
typedef struct http_response *http_response;

/* A vector of struct http_header is returned from the
 * HTTP_REQUEST_GET_HEADERS call.
 */
struct http_header { const char *key, *value; };

/* Supported methods. */
#define HTTP_METHOD_GET  1
#define HTTP_METHOD_HEAD 2
#define HTTP_METHOD_POST 3

/* Function: http_get_servername - get and set the server name string
 * Function: http_set_servername
 *
 * Get and set the server name (which is sent in the @code{Server})
 * header by the server when it responds to requests.
 *
 * The default string is @code{pthrlib-httpd/version}.
 *
 * See also: @ref{new_http_request(3)}, @ref{new_http_response(3)},
 * @ref{new_cgi(3)}.
 */
const char *http_get_servername (void);
const char *http_set_servername (const char *new_server_name);

/* Function: new_http_request - functions for parsing HTTP requests
 * Function: http_request_time
 * Function: http_request_get_url
 * Function: http_request_set_url
 * Function: http_request_path
 * Function: http_request_query_string
 * Function: http_request_method
 * Function: http_request_method_string
 * Function: http_request_is_HEAD
 * Function: http_request_version
 * Function: http_request_nr_headers
 * Function: http_request_get_headers
 * Function: http_request_get_header
 * Function: http_request_get_cookie
 *
 * These functions allow you to efficiently parse incoming
 * HTTP requests from conforming HTTP/0.9, HTTP/1.0 and HTTP/1.1
 * clients. The request parser understands GET, HEAD and POST
 * requests and conforms as far as possible to RFC 2616.
 *
 * @code{new_http_request} creates a new request object, parsing
 * the incoming request on the given @code{io_handle}. If the
 * stream closes at the beginning of the request the function
 * returns @code{NULL}. If the request is faulty, then the
 * library prints a message to syslog and throws an exception
 * by calling @ref{pth_die(3)}. Otherwise it initializes a complete
 * @code{http_request} object and returns it.
 *
 * @code{http_request_time} returns the timestamp of the incoming
 * request.
 *
 * @code{http_request_get_url} returns the complete URL of the request.
 *
 * @code{http_request_path} returns just the path component of
 * the URL (ie. without the query string if there was one).
 * @code{http_request_query_string} returns just the query string
 * (for GET requests only). Do not do your own parsing of query
 * strings: there is a CGI library built into pthrlib (see:
 * @ref{new_cgi(3)}).
 *
 * @code{http_request_set_url} updates the URL (and hence path
 * and query string). It is used by servers which support internal
 * redirects, such as @code{rws}.
 *
 * @code{http_request_method} returns the method, one of
 * @code{HTTP_METHOD_GET}, @code{HTTP_METHOD_HEAD} or
 * @code{HTTP_METHOD_POST}. @code{http_request_is_HEAD} is
 * just a quick way of testing if the method is a HEAD
 * request. @code{http_request_method_string} returns the
 * method as a string rather than a coded number.
 *
 * @code{http_request_version} returns the major and minor
 * numbers of the HTTP request (eg. major = 1, minor = 0 for
 * a HTTP/1.0 request).
 *
 * @code{http_request_nr_headers}, @code{http_request_get_headers}
 * and @code{http_request_get_header} return the number of
 * HTTP headers, the list of HTTP headers and a particular
 * HTTP header (if it exists). @code{http_request_get_headers}
 * returns a @code{vector} or @code{struct http_header}. This
 * structure contains at least two fields called @code{key} and
 * @code{value}. HTTP header keys are case insensitive when
 * searching, and you will find that the list of keys returned
 * by @code{http_request_get_headers} has been converted to
 * lowercase.
 *
 * @code{http_request_get_cookie} gets a named browser cookie
 * sent in the request. It returns the value of the cookie
 * or @code{NULL} if no such named cookie was sent by the browser.
 *
 * To send a cookie to the browser, you should generate and send
 * a @code{Set-Cookie} header with the appropriate content. Note
 * that not all browsers support cookies.
 *
 * See also: @ref{new_http_response(3)}, @ref{new_cgi(3)},
 * @ref{new_pseudothread(3)}, @ref{io_fdopen(3)}, RFC 2616.
 */
extern http_request new_http_request (pool, io_handle);
extern time_t http_request_time (http_request);
extern const char *http_request_get_url (http_request);
extern void http_request_set_url (http_request, const char *new_url);
extern const char *http_request_path (http_request);
extern const char *http_request_query_string (http_request);
extern int http_request_method (http_request);
extern const char *http_request_method_string (http_request);
extern int http_request_is_HEAD (http_request);
extern void http_request_version (http_request, int *major, int *minor);
extern int http_request_nr_headers (http_request);
extern vector http_request_get_headers (http_request);
extern const char *http_request_get_header (http_request h, const char *key);
extern const char *http_request_get_cookie (http_request h, const char *key);

/* Function: new_http_response - functions for sending HTTP responses
 * Function: http_response_send_header
 * Function: http_response_send_headers
 * Function: http_response_end_headers
 * Function: http_response_write_chunk
 * Function: http_response_write_chunk_string
 * Function: http_response_write_chunk_end
 *
 * These functions allow you to efficiently generate outgoing HTTP
 * responses.
 *
 * @code{new_http_response} generates a new HTTP response object and
 * returns it. @code{code} is the HTTP response code (see RFC 2616
 * for a list of codes), and @code{msg} is the HTTP response message.
 *
 * @code{http_response_send_header} sends a single HTTP header back
 * to the client. The header is constructed by concatenating
 * @code{key}, @code{": "}, @code{value} and @code{CR LF}.
 *
 * @code{http_response_send_headers} sends back several headers in
 * a single call. The arguments to this function are a list of
 * @code{key}, @code{value} pairs followed by a single @code{NULL}
 * argument which terminates the list.
 *
 * @code{http_response_end_headers} ends the header list. It causes
 * the code to emit any missing-but-required headers and then send
 * the final @code{CR LF} characters.
 *
 * @code{http_response_write_chunk}, @code{http_response_write_chunk_string}
 * and @code{http_response_write_chunk_end}
 * allow the caller to use chunked encoding, which is an
 * alternative to sending the @code{Content-Length} header. To enable
 * chunked encoding, the application should first call
 * @code{http_response_send_header (h, "Transfer-Encoding", "chunked");}.
 * Then, instead of writing directly to @code{io} to send data,
 * the application should call @code{http_response_write_chunk}
 * or @code{http_response_write_chunk_string} to write each block
 * (or string) of data. At the end of the data, the application
 * should call @code{http_response_write_chunk_end}.
 * (Steve Atkins implemented chunked encoding).
 *
 * See also: @ref{new_http_request(3)}, @ref{new_cgi(3)},
 * @ref{new_pseudothread(3)}, @ref{io_fdopen(3)}, RFC 2616.
 */
extern http_response new_http_response (pool, http_request, io_handle, int code, const char *msg);
extern void http_response_send_header (http_response, const char *key, const char *value);
extern void http_response_send_headers (http_response, ...);
extern int http_response_end_headers (http_response h);
extern void http_response_write_chunk (http_response, const char *data, int length);
extern void http_response_write_chunk_string (http_response, const char *string);
extern void http_response_write_chunk_end (http_response);

/* Function: http_set_log_file - enable HTTP logs on file pointer
 * Function: http_get_log_file
 *
 * The @code{FILE *fp} argument to @code{http_set_log_file} sets
 * the file pointer on which HTTP logs are generated. To disable
 * logging, set @code{fp} to @code{NULL}. The function returns
 * @code{fp}.
 *
 * @code{http_get_log_file} returns the current file pointer
 * or @code{NULL} if logging is disabled.
 *
 * The default is that logging is disabled.
 *
 * Currently log messages are generated at the end of the
 * HTTP response headers and have the following fixed format:
 *
 * YYYY/MM/DD HH:MM ip:port - "METHOD URL HTTP/x.y" CODE length "Referer" "User Agent"
 *
 * The first "-" is intended to store the HTTP auth username, when
 * HTTP authorization is supported by the library. The "length"
 * field is only known if the caller sends back a "Content-Length"
 * header. Otherwise 0 is printed in that position.
 *
 * Bugs: Log format should be customizable. It should be possible
 * (optionally, of course) to look up the IP address and print
 * a hostname.
 *
 * See also: @ref{new_http_request(3)}, @ref{new_cgi(3)},
 * @ref{new_pseudothread(3)}, @ref{io_fdopen(3)}, RFC 2616.
 */
extern FILE *http_set_log_file (FILE *fp);
extern FILE *http_get_log_file (void);

#endif /* PTHR_HTTP_H */
