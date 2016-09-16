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
 * $Id: pthr_eg1_echo_main.c,v 1.2 2002/08/21 10:42:15 rich Exp $
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>

#include "src/pthr_server.h"
#include "pthr_eg1_echo.h"

static void start_processor (int sock, void *);

int
main (int argc, char *argv[])
{
  /* Start up the server. */
  pthr_server_main_loop (argc, argv, start_processor);

  exit (0);
}

static void
start_processor (int sock, void *data)
{
  (void) new_eg1_echo_processor (sock);
}
