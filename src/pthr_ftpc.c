/* FTP client library.
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
 * $Id: pthr_ftpc.c,v 1.7 2002/12/01 14:29:27 rich Exp $
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>

#ifdef HAVE_STRING_H
#include <string.h>
#endif

#ifdef HAVE_NETDB_H
#include <netdb.h>
#endif

#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif

#ifdef HAVE_FCNTL_H
#include <fcntl.h>
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

#include <pool.h>
#include <vector.h>
#include <pstring.h>

#include "pthr_pseudothread.h"
#include "pthr_iolib.h"
#include "pthr_ftpc.h"

#define IS_1xx(c) ((c) >= 100 && (c) <= 199)
#define IS_2xx(c) ((c) >= 200 && (c) <= 299)
#define IS_3xx(c) ((c) >= 300 && (c) <= 399)
#define IS_4xx(c) ((c) >= 400 && (c) <= 499)
#define IS_5xx(c) ((c) >= 500 && (c) <= 599)
#define IS_NOT_1xx(c) (!(IS_1xx(c)))
#define IS_NOT_2xx(c) (!(IS_2xx(c)))
#define IS_NOT_3xx(c) (!(IS_3xx(c)))
#define IS_NOT_4xx(c) (!(IS_4xx(c)))
#define IS_NOT_5xx(c) (!(IS_5xx(c)))

#define REPLY_BUFFER_SIZE 2048

struct ftpc
{
  /* General information about the connection. */
  pool pool;
  io_handle io;

  int port;
  int passive_mode;
  int verbose;

  struct sockaddr_in addr;
  int pasv_data_port;

  /* Reply buffer - stores the last line read from the server. */
  char *reply;

  /* These are kept for information only. */
  char *server;
  const char *username;
  char *server_greeting;
};

static int eat_reply (ftpc);
static int do_command (ftpc, const char *cmd, const char *arg);
static io_handle open_data (ftpc, int data_sock);
static int issue_port_or_pasv (ftpc f);

ftpc
new_ftpc (pool pool, const char *server)
{
  ftpc f;
  char *t;
  struct hostent *h;
  int sock, code;

  f = pcalloc (pool, 1, sizeof *f);
  f->pool = pool;
  f->server = pstrdup (pool, server);
  f->reply = pmalloc (pool, REPLY_BUFFER_SIZE);

#if 0
  /* Just during testing, enable verbose mode always. */
  f->verbose = 1;
#endif

  /* Name contains a port number? If so, extract it out first. */
  t = strrchr (f->server, ':');
  if (t)
    {
      *t = 0;
      if (sscanf (t+1, "%d", &f->port) != 1)
	{
	  fprintf (stderr, "bad port number: %s\n", t+1);
	  return 0;
	}
    }
  else
    {
      struct servent *se;

      /* Try to determine the default port for FTP. */
      se = getservbyname ("ftp", "tcp");
      if (se)
	f->port = ntohs (se->s_port);
      else
	f->port = 21;		/* Default FTP control port. */
    }

  /* Resolve the name of the server, if necessary. */
  h = gethostbyname (f->server);
  if (!h)
    {
      herror (f->server);
      return 0;
    }

  f->addr.sin_family = AF_INET;
  memcpy (&f->addr.sin_addr, h->h_addr, sizeof f->addr.sin_addr);
  f->addr.sin_port = htons (f->port);

  /* Create a socket. */
  sock = socket (PF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (sock == -1)
    {
      perror ("socket");
      return 0;
    }

  /* Set non-blocking. */
  if (fcntl (sock, F_SETFL, O_NONBLOCK) == -1)
    {
      perror ("fcntl: O_NONBLOCK");
      return 0;
    }

  /* Attempt to connect to the server. */
  if (pth_connect (sock, (struct sockaddr *) &f->addr, sizeof f->addr)
      == -1)
    {
      perror (f->server);
      return 0;
    }

  /* Wrap up the socket in an IO handle. */
  f->io = io_fdopen (sock);
  if (!f->io) return 0;

  /* Expect a response string back immediately from the server. */
  code = eat_reply (f);

  /* Save the server greeting. */
  f->server_greeting = pstrdup (f->pool, f->reply+4);

  if (IS_NOT_2xx (code))
    {
      fprintf (stderr, "bad response from server %d\n", code);
      return 0;
    }

  return f;
}

int
ftpc_set_passive_mode (ftpc f, int flag)
{
  return f->passive_mode = flag;
}

int
ftpc_set_verbose (ftpc f, int flag)
{
  return f->verbose = flag;
}

void
ftpc_perror (ftpc f, const char *msg)
{
  fprintf (stderr, "%s: %s\n", msg, f->reply);
}

int
ftpc_login (ftpc f, const char *username, const char *password)
{
  int is_anonymous, code;

  is_anonymous = !username || strcmp (username, "ftp") == 0 ||
    strcmp (username, "anonymous") == 0;

  if (is_anonymous)
    {
      if (!username)
	username = "ftp";
      if (!password)
	{
	  char *logname = getenv ("LOGNAME");

	  if (!logname) logname = "nobody";
	  password = psprintf (f->pool, "%s@", logname);
	}
    }
  else
    {
      if (!password) password = "";
    }

  f->username = username;

  /* Send the USER command. */
  code = do_command (f, "USER", username);
  if (IS_NOT_3xx (code))
    return -1;

  /* Send the PASS command. */
  code = do_command (f, "PASS", password);
  if (IS_NOT_2xx (code))
    return -1;

  return 0;
}

int
ftpc_type (ftpc f, char type)
{
  int code;
  char t[2] = { type, 0 };

  code = do_command (f, "TYPE", t);
  return IS_2xx (code) ? 0 : -1;
}

int
ftpc_ascii (ftpc f)
{
  return ftpc_type (f, 'a');
}

int
ftpc_binary (ftpc f)
{
  return ftpc_type (f, 'i');
}

int
ftpc_cwd (ftpc f, const char *pathname)
{
  int code = do_command (f, "CWD", pathname);
  return IS_2xx (code) ? 0 : -1;
}

int
ftpc_cdup (ftpc f)
{
  int code = do_command (f, "CDUP", 0);
  return IS_2xx (code) ? 0 : -1;
}

char *
ftpc_pwd (ftpc f)
{
  int code, len;
  char *path;

  code = do_command (f, "PWD", 0);
  if (IS_NOT_2xx (code)) return 0;

  path = pstrdup (f->pool, &f->reply[4]);

  /* Strip quotes around the pathname, if there are any. */
  len = strlen (path);
  if (len >= 2 && path[0] == '"' && path[len-1] == '"')
    {
      path[len-1] = '\0';
      path++;
    }

  return path;
}

int
ftpc_mkdir (ftpc f, const char *pathname)
{
  int code = do_command (f, "MKD", pathname);
  return IS_2xx (code) ? 0 : -1;
}

int
ftpc_rmdir (ftpc f, const char *pathname)
{
  int code = do_command (f, "RMD", pathname);
  return IS_2xx (code) ? 0 : -1;
}

int
ftpc_delete (ftpc f, const char *pathname)
{
  int code = do_command (f, "DELE", pathname);
  return IS_2xx (code) ? 0 : -1;
}

vector
ftpc_ls (ftpc f, pool pool, const char *pathname)
{
  int code, data_sock;
  io_handle io;
  vector v;

  /* Issue PORT or PASV command and get a socket. */
  if ((data_sock = issue_port_or_pasv (f)) == -1)
    return 0;

  if (!pool) pool = f->pool;

  v = new_vector (pool, const char *);

  /* Issue the NLST command. */
  code = do_command (f, "NLST -a", pathname);
  if (IS_NOT_1xx (code)) { close (data_sock); return 0; }

  /* Open data connection to server. */
  io = open_data (f, data_sock);
  if (!io) return 0;

  /* Read the data, line at a time. */
  while (io_fgets (f->reply, REPLY_BUFFER_SIZE, io, 0))
    {
      char *s = pstrdup (pool, f->reply);
      vector_push_back (v, s);
    }

  /* Close the data connection. */
  io_fclose (io);

  /* Check return code. */
  code = eat_reply (f);
  return IS_2xx (code) ? v : 0;
}

vector
ftpc_dir (ftpc f, pool pool, const char *pathname)
{
  int code, data_sock;
  io_handle io;
  vector v;

  /* Issue PORT or PASV command and get a socket. */
  if ((data_sock = issue_port_or_pasv (f)) == -1)
    return 0;

  if (!pool) pool = f->pool;

  v = new_vector (pool, const char *);

  /* Issue the LIST command. */
  code = do_command (f, "LIST -a", pathname);
  if (IS_NOT_1xx (code)) { close (data_sock); return 0; }

  /* Open data connection to server. */
  io = open_data (f, data_sock);
  if (!io) return 0;

  /* Read the data, line at a time. */
  while (io_fgets (f->reply, REPLY_BUFFER_SIZE, io, 0))
    {
      char *s = pstrdup (pool, f->reply);
      vector_push_back (v, s);
    }

  /* Close the data connection. */
  io_fclose (io);

  /* Check return code. */
  code = eat_reply (f);
  return IS_2xx (code) ? v : 0;
}

/* XXX Only works for binary transfers. */
int
ftpc_get (ftpc f, const char *remote_file, const char *local_file)
{
  int code, data_sock, n;
  FILE *fp;
  io_handle io;
  char buf[1024];

  /* Issue PORT or PASV command and get a socket. */
  if ((data_sock = issue_port_or_pasv (f)) == -1)
    return -1;

  /* Issue the RETR command. */
  code = do_command (f, "RETR", remote_file);
  if (IS_NOT_1xx (code)) { close (data_sock); return -1; }

  /* Open the local file. */
  fp = fopen (local_file, "w");
  if (fp == 0) { perror (local_file); return -1; }

  /* Open data connection to server. */
  io = open_data (f, data_sock);
  if (!io) return -1;

  /* Read the data, block at a time. */
  while ((n = io_fread (buf, 1, sizeof buf, io)) > 0)
    {
      if (fwrite (buf, 1, n, fp) != n)
	{
	  perror (local_file);
	  fclose (fp);
	  io_fclose (io);
	  return -1;
	}
    }

  /* Close everything. */
  fclose (fp);
  io_fclose (io);

  /* Check return code. */
  code = eat_reply (f);
  return IS_2xx (code) ? 0 : -1;
}

/* XXX Only works for binary transfers. */
int
ftpc_put (ftpc f, const char *local_file, const char *remote_file)
{
  int code, data_sock, n;
  FILE *fp;
  io_handle io;
  char buf[1024];

  /* Issue PORT or PASV command and get a socket. */
  if ((data_sock = issue_port_or_pasv (f)) == -1)
    return -1;

  /* Issue the STOR command. */
  code = do_command (f, "STOR", remote_file);
  if (IS_NOT_1xx (code)) { close (data_sock); return -1; }

  /* Open the local file. */
  fp = fopen (local_file, "r");
  if (fp == 0) { perror (local_file); return -1; }

  /* Open data connection to server. */
  io = open_data (f, data_sock);
  if (!io) return -1;

  /* Read the data, block at a time. */
  while ((n = fread (buf, 1, sizeof buf, fp)) > 0)
    {
      if (io_fwrite (buf, 1, n, io) != n)
	{
	  perror (remote_file);
	  fclose (fp);
	  io_fclose (io);
	  return -1;
	}
    }

  /* Close everything. */
  fclose (fp);
  io_fclose (io);

  /* Check return code. */
  code = eat_reply (f);
  return IS_2xx (code) ? 0 : -1;
}

int
ftpc_quote (ftpc f, const char *cmd)
{
  int code;

  code = do_command (f, cmd, 0);
  return IS_2xx (code) ? 0 : -1;
}

int
ftpc_quit (ftpc f)
{
  int code;

  code = do_command (f, "QUIT", 0);
  io_fclose (f->io);
  return IS_2xx (code) ? 0 : -1;
}

/* Execute a command, get the reply and return the code. */
static int
do_command (ftpc f, const char *cmd, const char *arg)
{
  if (f->verbose)
    {
      if (f->username)
	fprintf (stderr, "%s@%s: ", f->username, f->server);
      else
	fprintf (stderr, "%s: ", f->server);

      if (arg == 0)
	fprintf (stderr, "%s\r\n", cmd);
      else
	fprintf (stderr, "%s %s\r\n", cmd, arg);
    }

  if (arg == 0)
    io_fprintf (f->io, "%s\r\n", cmd);
  else
    io_fprintf (f->io, "%s %s\r\n", cmd, arg);

  return eat_reply (f);
}

/* Eat the reply from the server, and throw it all away, except for
 * the code.
 */
static int
eat_reply (ftpc f)
{
  int code;

  while (io_fgets (f->reply, REPLY_BUFFER_SIZE, f->io, 0))
    {
      if (f->verbose)
	{
	  if (f->username)
	    fprintf (stderr, "%s@%s: %s\n", f->username, f->server, f->reply);
	  else
	    fprintf (stderr, "%s: %s\n", f->server, f->reply);
	}

      if (strlen (f->reply) < 4 ||
	  f->reply[0] < '1' || f->reply[0] > '5' ||
	  f->reply[1] < '0' || f->reply[1] > '9' ||
	  f->reply[2] < '0' || f->reply[2] > '9' ||
	  (f->reply[3] != ' ' && f->reply[3] != '-'))
	pth_die ("badly formatted reply from server");

      /* Is this the last line? */
      if (f->reply[3] == ' ')
	{
	  /* Yes: extract the code. */
	  code = 100 * (f->reply[0] - '0') +
	    10 * (f->reply[1] - '0') + (f->reply[2] - '0');

	  return code;
	}
    }

  pth_die ("server closed the connection unexpectedly");
}

/* Issue either PORT or PASV command to the server and get a socket. */
static int
issue_port_or_pasv (ftpc f)
{
  int code, a1, a2, a3, a4, p1, p2;
  char *t;
  struct sockaddr_in addr;
  int addr_len, data_sock;

  /* Create data socket. */
  data_sock = socket (PF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (data_sock == -1)
    {
      perror ("socket");
      return -1;
    }

  /* Set non-blocking. */
  if (fcntl (data_sock, F_SETFL, O_NONBLOCK) == -1) abort ();

  if (f->passive_mode)
    {
      /* Issue PASV command to get a port number. */
      code = do_command (f, "PASV", 0);
      if (IS_NOT_2xx (code)) return -1;

      /* The reply should say something like:
       * 227 Entering Passive Mode (A,A,A,A,P,P)
       * We ignore the address fields and concentrate on just
       * the port number.
       */
      t = strchr (&f->reply[4], '(');
      if (!t ||
	  sscanf (t, "( %d , %d , %d , %d , %d , %d )",
		  &a1, &a2, &a3, &a4, &p1, &p2) != 6)
	{
	  close (data_sock);
	  fprintf (stderr, "cannot parse reply from PASV command\n");
	  return -1;
	}

      f->pasv_data_port = p1 * 256 + p2;
    }
  else
    {
      /* Bind the socket. */
      addr.sin_family = AF_INET;
      addr.sin_port = 0;
      addr.sin_addr.s_addr = INADDR_ANY;
      if (bind (data_sock, (struct sockaddr *) &addr, sizeof addr) == -1)
	{
	  close (data_sock);
	  perror ("bind");
	  return -1;
	}

      /* Listen on socket. */
      if (listen (data_sock, 1) == -1)
	{
	  close (data_sock);
	  perror ("listen");
	  return -1;
	}

      addr_len = sizeof addr;
      if (getsockname (data_sock, (struct sockaddr *) &addr, &addr_len)
	  == -1)
	{
	  close (data_sock);
	  perror ("getsockname");
	  return -1;
	}

      /* Issue a PORT command to tell the server our port number. */
      a1 = (ntohl (f->addr.sin_addr.s_addr) >> 24) & 0xff;
      a2 = (ntohl (f->addr.sin_addr.s_addr) >> 16) & 0xff;
      a3 = (ntohl (f->addr.sin_addr.s_addr) >>  8) & 0xff;
      a4 =  ntohl (f->addr.sin_addr.s_addr)        & 0xff;
      p1 = ntohs (addr.sin_port) / 256;
      p2 = ntohs (addr.sin_port) % 256;
      t = psprintf (f->pool, "%d,%d,%d,%d,%d,%d", a1, a2, a3, a4, p1, p2);
      code = do_command (f, "PORT", t);
      if (IS_NOT_2xx (code)) return -1;
    }

  return data_sock;
}

/* Open a data connection to the server (which is expecting it). */
static io_handle
open_data (ftpc f, int data_sock)
{
  io_handle io;
  struct sockaddr_in addr;
  int addr_len, sock;

  if (f->passive_mode)
    {
      /* Connect to server. */
      f->addr.sin_port = htons (f->pasv_data_port);

      if (pth_connect (data_sock, (struct sockaddr *) &f->addr,
		       sizeof f->addr) == -1)
	{
	  close (data_sock);
	  perror (f->server);
	  return 0;
	}

      io = io_fdopen (data_sock);
      if (!io) return 0;
    }
  else
    {
      addr_len = sizeof addr;

      /* Accept connection from server. */
      if ((sock = pth_accept (data_sock,
			      (struct sockaddr *) &addr, &addr_len)) == -1)
	{
	  close (data_sock);
	  perror ("accept");
	  return 0;
	}

      close (data_sock);

      /* Verify this connection comes from the server. */
      if (addr.sin_addr.s_addr != f->addr.sin_addr.s_addr)
	{
	  fprintf (stderr, "connection accepted, but not from FTP server");
	  return 0;
	}

      io = io_fdopen (sock);
      if (!io) return 0;
    }

  return io;
}
