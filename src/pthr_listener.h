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
 * $Id: pthr_listener.h,v 1.3 2002/08/21 10:42:19 rich Exp $
 */

#ifndef PTHR_LISTENER_H
#define PTHR_LISTENER_H

#include "pthr_reactor.h"
#include "pthr_pseudothread.h"

struct listener;
typedef struct listener *listener;

extern listener new_listener (int sock, void (*processor_fn) (int sock, void *data), void *data);

#endif /* PTHR_LISTENER_H */
