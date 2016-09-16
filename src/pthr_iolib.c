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
 * $Id: pthr_iolib.c,v 1.9 2003/02/02 18:05:31 rich Exp $
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#ifdef HAVE_STRING_H
#include <string.h>
#endif

#ifdef HAVE_SYS_TYPE_H
#include <sys/types.h>
#endif

#ifdef HAVE_SYS_WAIT_H
#include <sys/wait.h>
#endif

#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif

#ifdef HAVE_ALLOCA_H
#include <alloca.h>
#endif

#include <pool.h>

#include "pthr_iolib.h"
#include "pthr_pseudothread.h"

struct io_handle
{
  /* Pool for memory allocation, etc. */
  pool pool;

  /* The underlying socket. */
  int sock;

  /* The incoming buffer. */
  char *inbuf;			/* The actual buffer. */
  char *inbufpos;		/* Current position within buffer. */
  int inbuflen;			/* Number of bytes left to read. */

  int inbufcount;		/* Total number of bytes read. */

  /* The outgoing buffer. */
  char *outbuf;			/* The actual buffer. */
  char *outbufpos;		/* Current position within buffer. */
  int outbuffree;		/* Number of bytes free left in buffer. */

  int outbufcount;		/* Total number of bytes written. */

  int outbufmode;		/* Output buffer mode. */
};

static void _flush (io_handle io, int ignore_errors);
static void _err (io_handle io, const char *msg) __attribute__((noreturn));
static void _do_close (io_handle io);

io_handle
io_fdopen (int sock)
{
  io_handle io;
  pool pool = new_subpool (pth_get_pool (current_pth));

  io = pmalloc (pool, sizeof *io);

  io->inbuf = pmalloc (pool, IOLIB_INPUT_BUFFER_SIZE * sizeof (char));

  io->outbuf = pmalloc (pool, IOLIB_OUTPUT_BUFFER_SIZE * sizeof (char));

  io->pool = pool;
  io->sock = sock;

  io->inbufpos = &io->inbuf[IOLIB_INPUT_BUFFER_SIZE]; /* Let ungetc work. */
  io->inbuflen = 0;
  io->inbufcount = 0;
  io->outbufpos = io->outbuf;
  io->outbuffree = IOLIB_OUTPUT_BUFFER_SIZE;
  io->outbufcount = 0;
  io->outbufmode = IO_MODE_LINE_BUFFERED;

  /* Register to automagically close this I/O handle when we
   * exit this thread.
   */
  pool_register_cleanup_fn (pool, (void (*)(void *)) _do_close, io);

  return io;
}

inline int
io_fflush (io_handle io)
{
  if (io->outbufpos > io->outbuf)
    _flush (io, 0);

  return 0;
}

void
_do_close (io_handle io)
{
  /* Flush the buffer, but don't worry too much about errors. */
  if (io->outbufpos > io->outbuf)
    _flush (io, 1);

  /* Close underlying socket. */
  if (close (io->sock) < 0) _err (io, "close");
}

void
io_fclose (io_handle io)
{
  /* Deleting the subpool containing io causes the _do_close callback
   * handler to run which actually closes the socket. The memory
   * containing io is also freed.
   */
  delete_pool (io->pool);
}

int
io_fgetc (io_handle io)
{
  int r;

  io_fflush (io);

 next:
  /* Satify this from the input buffer? */
  if (io->inbuflen > 0)
    {
      int c = *io->inbufpos;
      io->inbufpos++;
      io->inbuflen--;
      return c;
    }

  /* Refill the input buffer from the socket. */
  r = pth_read (io->sock, io->inbuf, IOLIB_INPUT_BUFFER_SIZE * sizeof (char));
  if (r < 0) _err (io, "read");

  io->inbufpos = io->inbuf;
  io->inbuflen = r;
  io->inbufcount += r;

  /* End of buffer? */
  if (r == 0) return -1;

  /* Return the next character. */
  goto next;
}

char *
io_fgets (char *s, int max_size, io_handle io, int store_eol)
{
  int n = 0;

  io_fflush (io);

  while (n < max_size - 1)
    {
      int c = io_fgetc (io);

      /* End of file? */
      if (c == -1)
	{
	  s[n] = '\0';
	  return n > 0 ? s : 0;
	}

      s[n++] = c;

      /* End of line? */
      if (c == '\n')
	break;
    }

  /* Remove the trailing CR LF? */
  if (!store_eol)
    {
      while (n > 0 && (s[n-1] == '\n' || s[n-1] == '\r'))
	n--;
    }

  /* Terminate the line. */
  s[n] = '\0';
  return s;
}

int
io_ungetc (int c, io_handle io)
{
  if (io->inbufpos > io->inbuf)
    {
      io->inbufpos--;
      *io->inbufpos = c;
      io->inbuflen++;
      return c;
    }

  return -1;			/* No room left in input buffer. */
}

size_t
io_fread (void *ptr, size_t size, size_t nmemb, io_handle io)
{
  size_t n = size * nmemb, c = 0;
  int i;
  char *cptr = (char *) ptr;

  io_fflush (io);

  /* Satisfy as much as possible from the input buffer. */
  i = n > io->inbuflen ? io->inbuflen : n;
  memcpy (cptr, io->inbufpos, i * sizeof (char));
  n -= i;
  c += i;
  cptr += i;
  io->inbuflen -= i;
  io->inbufpos += i;

  /* Read the rest directly from the socket until we have either
   * satisfied the request or there is an EOF.
   */
  while (n > 0)
    {
      int r = pth_read (io->sock, cptr, n * sizeof (char));
      if (r < 0) _err (io, "read");

      if (r == 0)		/* End of file. */
	return c;

      n -= r;
      c += r;
      cptr += r;
      io->inbufcount += r;
    }

  return c;
}

inline int
io_fputc (int c, io_handle io)
{
 again:
  if (io->outbuffree > 0)
    {
      *io->outbufpos = c;
      io->outbufpos++;
      io->outbuffree--;

      /* Flush the buffer after each character or line. */
      if (io->outbufmode == IO_MODE_UNBUFFERED ||
	  (io->outbufmode == IO_MODE_LINE_BUFFERED && c == '\n'))
	_flush (io, 0);

      return c;
    }

  /* We need to flush the output buffer and try again. */
  _flush (io, 0);
  goto again;
}

int
io_fputs (const char *s, io_handle io)
{
  while (*s)
    {
      io_fputc (*s, io);
      s++;
    }

  /* According to the manual page, fputs returns a non-negative number
   * on success or EOF on error.
   */
  return 1;
}

int
io_fprintf (io_handle io, const char *fs, ...)
{
  va_list args;
  int r, n = 4096;
  char *s = alloca (n);

  if (s == 0) abort ();

  va_start (args, fs);
  r = vsnprintf (s, n, fs, args);
  va_end (args);

  /* Did the string get truncated? If so, we can allocate the
   * correct sized buffer directly from the pool.
   *
   * Note: according to the manual page, a return value of -1 indicates
   * that the string was truncated. We have found that this is not
   * actually true however. In fact, the library seems to return the
   * number of characters which would have been written into the string
   * (ie. r > n).
   */
  if (r > n)
    {
      n = r + 1;
      s = pmalloc (io->pool, n);

      va_start (args, fs);
      r = vsnprintf (s, n, fs, args);
      va_end (args);
    }

  io_fputs (s, io);

  return r;
}

size_t
io_fwrite (const void *ptr, size_t size, size_t nmemb, io_handle io)
{
  size_t n = size * nmemb, c = 0;
  char *cptr = (char *) ptr;

  /* Flush out any existing data. */
  io_fflush (io);

  /* Write the data directly to the socket. */
  while (n > 0)
    {
      int r = pth_write (io->sock, cptr, n * sizeof (char));
      if (r < 0) _err (io, "write");

      n -= r;
      cptr += r;
      c += r;
      io->outbufcount += r;
    }

  return c;
}

io_handle
io_popen (const char *command, const char *type)
{
  int fd[2], s;
  int pid;
  int mode = 0;			/* 0 for read, 1 for write. */
  io_handle io;

  if (strcmp (type, "r") == 0)
    mode = 0;
  else if (strcmp (type, "w") == 0)
    mode = 1;
  else
    abort ();

  /* Create a pipe between parent and child. */
  if (pipe (fd) < 0) { perror ("pipe"); return 0; }

  /* Fork and connect up the pipe appropriately. */
  pid = fork ();
  if (pid < 0) { close (fd[0]); close (fd[1]); perror ("fork"); return 0; }
  else if (pid > 0)		/* Parent process. */
    {
      if (mode == 0)
	{
	  close (fd[1]);
	  s = fd[0];
	}
      else
	{
	  close (fd[0]);
	  s = fd[1];
	}

      /* Set the file descriptor to non-blocking. */
      if (fcntl (s, F_SETFL, O_NONBLOCK) < 0) abort ();

      io = io_fdopen (s);
      if (io == 0)
	{
	  close (s);
	  return 0;
	}

      return io;
    }
  else				/* Child process. */
    {
      if (mode == 0)
	{
	  close (fd[0]);
	  if (fd[1] != 1)
	    {
	      dup2 (fd[1], 1);
	      close (fd[1]);
	    }
	}
      else
	{
	  close (fd[1]);
	  if (fd[0] != 0)
	    {
	      dup2 (fd[0], 0);
	      close (fd[0]);
	    }
	}

      /* Run the child command. */
      _exit (system (command));

      /*NOTREACHED*/
      return 0;
    }
}

void
io_pclose (io_handle io)
{
  /* Close connection. */
  io_fclose (io);

  /* Wait for child to exit. */
  wait (NULL);
}

int
io_copy (io_handle from_io, io_handle to_io, int len)
{
  int written_len = 0;

  /* Synchronize the input handle. */
  io_fflush (from_io);

  while (len != 0)
    {
      int n;

      /* Read bytes directly from the input buffer. */
      if (from_io->inbuflen == 0)
	{
	  int c;

	  /* Need to refill the input buffer. We do this by reading a single
	   * character and the "ungetting" it back into the buffer. The
	   * io_ungetc call is guaranteed to work in this case.
	   */
	  c = io_fgetc (from_io);
	  if (c == -1) return written_len;
	  io_ungetc (c, from_io);
	}

      /* Decide how many bytes to read. */
      if (len == -1)
	n = from_io->inbuflen;
      else
	{
	  if (len >= from_io->inbuflen)
	    n = from_io->inbuflen;
	  else
	    n = len;
	}

      /* Write it. */
      io_fwrite (from_io->inbufpos, 1, n, to_io);
      written_len += n;
      if (len > 0) len -= n;
      from_io->inbufpos += n;
      from_io->inbuflen -= n;
    }

  return written_len;
}

void
io_setbufmode (io_handle io, int mode)
{
  io->outbufmode = mode;
}

static void
_flush (io_handle io, int ignore_errors)
{
  size_t n = io->outbufpos - io->outbuf;
  char *cptr = io->outbuf;

  /* Write the data to the socket. */
  while (n > 0)
    {
      int r = pth_write (io->sock, cptr, n * sizeof (char));
      if (r < 0)
	{
	  if (!ignore_errors)
	    _err (io, "write");
	  else
	    break;
	}

      n -= r;
      cptr += r;
      io->outbufcount += r;
    }

  /* Reset the output buffer. */
  io->outbufpos = io->outbuf;
  io->outbuffree = IOLIB_OUTPUT_BUFFER_SIZE;
}

int
io_fileno (io_handle io)
{
  return io->sock;
}

int
io_get_inbufcount (io_handle io)
{
  return io->inbufcount;
}

int
io_get_outbufcount (io_handle io)
{
  return io->outbufcount;
}

static void
_err (io_handle io, const char *msg)
{
  /* I commented out the following line because, strangely, it seems to
   * cause some sort of memory corruption bug. If there is a bug, it's
   * probably related to either stack overflow or the stack swapping /
   * context switching stuff. Whatever. Removed it for safety.
   * -- RWMJ 2000/12/05.
   */
#if 0
  perror (msg);
#endif

  /* Should errors be catchable? ie. Should we call pth_die here? And
   * if so, what are the consequences of doing it?
   * -- RWMJ 2001/05/30.
   */
  pth_exit ();
}
