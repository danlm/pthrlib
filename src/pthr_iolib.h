/* A small buffered I/O library.
 * - by Richard W.M. Jones <rich@annexia.org>.
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
 * $Id: pthr_iolib.h,v 1.5 2002/12/01 14:29:28 rich Exp $
 */

#ifndef PTHR_IOLIB_H
#define PTHR_IOLIB_H

#include <stdlib.h>
#include <stdarg.h>

#include <setjmp.h>

#include <pool.h>

#include "pthr_pseudothread.h"

#define IOLIB_INPUT_BUFFER_SIZE 1024
#define IOLIB_OUTPUT_BUFFER_SIZE 1024

struct io_handle;
typedef struct io_handle *io_handle;

/* Buffer modes. */
#define IO_MODE_LINE_BUFFERED  0 /* Default. */
#define IO_MODE_UNBUFFERED     1
#define IO_MODE_FULLY_BUFFERED 2

/* Function: io_fdopen - A buffered I/O library
 * Function: io_fclose
 * Function: io_fgetc
 * Function: io_fgets
 * Function: io_ungetc
 * Function: io_fread
 * Function: io_fputc
 * Function: io_fputs
 * Function: io_fprintf
 * Function: io_fwrite
 * Function: io_fflush
 * Function: io_fileno
 * Function: io_popen
 * Function: io_pclose
 * Function: io_copy
 * Function: io_setbufmode
 * Function: io_get_inbufcount
 * Function: io_get_outbufcount
 *
 * The @code{io_*} functions replace the normal blocking C library
 * @code{f*} functions with equivalents which work on non-blocking
 * pseudothread file descriptors.
 *
 * All of the functions in the synopsis above work identically
 * to the C library equivalents, except where documented below.
 *
 * @code{io_fdopen} associates a socket @code{sock} with a
 * I/O handle. The association cannot be broken later (so use
 * @ref{dup(2)} if you wish to later take back control of the
 * underlying socket). If either the current thread exits
 * or @code{io_fclose} is called, the underlying socket is
 * closed (with @ref{close(2)}).
 *
 * @code{io_fclose} flushes all unwritten data out of the socket
 * and closes it.
 *
 * @code{io_fgets} operates similarly to the ordinary C library
 * function @ref{fgets(3)}, except that it contains a useful
 * fourth argument, @code{store_eol}. If this fourth argument is
 * false, then the end of line characters (@code{CR}, @code{CR LF}
 * or @code{LF}) are stripped from the string before it is stored.
 *
 * @code{io_copy} copies @code{len} bytes from @code{from_io}
 * to @code{to_io}. If @code{len} equals -1 then bytes are
 * copied from @code{from_io} until end of file is reached.
 * If @code{len} equals 0, then no bytes are copied. The
 * number of bytes actually copied is returned.
 *
 * @code{io_setbufmode} sets the output buffer mode, and works
 * completely differently to the ordinary C library function
 * @ref{setbufmode(3)}. The three mode arguments possible are:
 * @code{IO_MODE_LINE_BUFFERED}, @code{IO_MODE_UNBUFFERED} and
 * @code{IO_MODE_FULLY_BUFFERED}, and these correspond to line
 * buffering, no buffering and full (block) buffering.
 *
 * @code{io_get_inbufcount} and @code{io_get_outbufcount} return
 * the number of characters read and written on the socket since
 * the socket was associated with the I/O object.
 *
 * See also:
 * @ref{fgetc(3)},
 * @ref{fgets(3)},
 * @ref{ungetc(3)},
 * @ref{fread(3)},
 * @ref{fputc(3)},
 * @ref{fputs(3)},
 * @ref{fprintf(3)},
 * @ref{fwrite(3)},
 * @ref{fflush(3)},
 * @ref{fileno(3)},
 * @ref{popen(3)},
 * @ref{pclose(3)},
 * @ref{pth_exit(3)}.
 */
extern io_handle io_fdopen (int sock);
extern void io_fclose (io_handle);
extern int io_fgetc (io_handle);
extern char *io_fgets (char *s, int max_size, io_handle, int store_eol);
extern int io_ungetc (int c, io_handle);
extern size_t io_fread (void *ptr, size_t size, size_t nmemb, io_handle);
extern int io_fputc (int c, io_handle);
extern int io_fputs (const char *s, io_handle);
extern int io_fprintf (io_handle, const char *fs, ...) __attribute__ ((format (printf, 2, 3)));
extern size_t io_fwrite (const void *ptr, size_t size, size_t nmemb, io_handle);
extern int io_fflush (io_handle);
extern int io_fileno (io_handle);
extern io_handle io_popen (const char *command, const char *mode);
extern void io_pclose (io_handle);
extern int io_copy (io_handle from_io, io_handle to_io, int len);
extern void io_setbufmode (io_handle, int mode);
extern int io_get_inbufcount (io_handle);
extern int io_get_outbufcount (io_handle);

#endif /* PTHR_IOLIB_H */
