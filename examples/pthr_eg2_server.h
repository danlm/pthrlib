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
 * $Id: pthr_eg2_server.h,v 1.2 2002/08/21 10:42:16 rich Exp $
 */

#ifndef PTHR_EG2_SERVER_H
#define PTHR_EG2_SERVER_H

#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif

#include <pool.h>

#include "src/pthr_pseudothread.h"
#include "src/pthr_http.h"
#include "src/pthr_iolib.h"

/* Define some RFC-compliant dates to represent past and future. */
#define DISTANT_PAST   "Thu, 01 Dec 1994 16:00:00 GMT"
#define DISTANT_FUTURE "Sun, 01 Dec 2030 16:00:00 GMT"

/* Headers which are sent to defeat caches. */
#define NO_CACHE_HEADERS "Cache-Control", "must-revalidate", "Expires", DISTANT_PAST, "Pragma", "no-cache"

#define CRLF "\r\n"

struct eg2_server_processor;
typedef struct eg2_server_processor *eg2_server_processor;

extern eg2_server_processor new_eg2_server_processor (int sock);

#endif /* PTHR_EG2_SERVER_H */
