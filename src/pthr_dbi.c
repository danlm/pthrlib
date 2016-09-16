/* Database interface library.
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
 * $Id: pthr_dbi.c,v 1.15 2003/01/26 13:17:16 rich Exp $
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <ctype.h>

#ifdef HAVE_ASSERT_H
#include <assert.h>
#endif

#ifdef HAVE_STRING_H
#include <string.h>
#endif

#ifdef HAVE_POSTGRESQL_LIBPQ_FE_H
#include <postgresql/libpq-fe.h>
#endif

#ifdef HAVE_LIBPQ_FE_H
#include <libpq-fe.h>
#endif

#ifndef HAVE_PQESCAPESTRING
size_t PQescapeString(char *to, const char *from, size_t length);
#endif

#include <pool.h>
#include <hash.h>
#include <vector.h>
#include <pstring.h>
#include <pre.h>

#include "pthr_pseudothread.h"
#include "pthr_dbi.h"

#define DEBUG(dbh,sth,fs,args...) do { if ((dbh)->flags & DBI_DEBUG) { if ((sth) == 0) fprintf (stderr, "dbi: dbh %p: " fs, (dbh) , ## args); else fprintf (stderr, "dbi: dbh %p sth %p: " fs, (dbh), (void *) (sth) , ## args); fputc ('\n', stderr); } } while (0)

/* Database handle. */
struct db_handle
{
  pool pool;			/* Pool for allocations. */
  const char *conninfo;		/* Connection string. */
  int flags;			/* Flags. */
  int in_transaction;		/* Are we in a transaction yet? */
  PGconn *conn;			/* The database connection object. */
};

/* Statement handle. */
struct st_handle
{
  pool pool;			/* Subpool used for allocations. */
  db_handle dbh;		/* Parent database connection. */
  const char *orig_query;	/* Original query string. */
  vector query;			/* Query, split into string, '?' and '@'. */
  vector intypes;		/* Placeholder types (vector of int). */
  PGresult *result;		/* Last result. */
  int fetch_allowed;		/* True if there are tuples to fetch. */
  int next_tuple;		/* Next row to fetch. */
  vector outtypes;		/* Output types (vector of struct otype). */
};

struct otype
{
  int type;			/* Type of this element. */
  void *varptr;			/* Pointer to variable for result. */
};

static void init_dbi (void) __attribute__((constructor));
static void free_dbi (void) __attribute__((destructor));
static void disconnect (void *vdbh);
static void parse_timestamp (st_handle sth, const char *str, struct dbi_timestamp *ts);
static void parse_interval (st_handle sth, const char *str, struct dbi_interval *inv);

/* Global variables. */
static pool dbi_pool;
static const pcre *re_qs, *re_timestamp, *re_interval;

/* Initialise the library. */
static void
init_dbi ()
{
#ifndef __OpenBSD__
  dbi_pool = new_subpool (global_pool);
#else
  dbi_pool = new_pool ();
#endif
  re_qs = precomp (dbi_pool, "\\?|@", 0);
  re_timestamp = precomp (dbi_pool,
" (?:(\\d\\d\\d\\d)-(\\d\\d)-(\\d\\d))     # date (YYYY-MM-DD)\n"
" \\s*                                     # space between date and time\n"
" (?:(\\d\\d):(\\d\\d)                     # HH:MM\n"
"    (?::(\\d\\d))?                        # optional :SS\n"
"    (?:\\.(\\d+))?                        # optional .microseconds\n"
"    (?:([+-])(\\d\\d))?                   # optional +/-OO offset from UTC\n"
" )?", PCRE_EXTENDED);
  re_interval = precomp (dbi_pool,
" (?:(\\d+)\\syears?)?                     # years\n"
" \\s*                                     # \n"
" (?:(\\d+)\\smons?)?                      # months\n"
" \\s*                                     # \n"
" (?:(\\d+)\\sdays?)?                      # days\n"
" \\s*                                     # \n"
" (?:(\\d\\d):(\\d\\d)                     # HH:MM\n"
"    (?::(\\d\\d))?                        # optional :SS\n"
" )?", PCRE_EXTENDED);
}

/* Free up global memory used by the library. */
static void
free_dbi ()
{
  delete_pool (dbi_pool);
}

db_handle
new_db_handle (pool pool, const char *conninfo, int flags)
{
  db_handle dbh = pmalloc (pool, sizeof *dbh);
  int status, fd;

  dbh->pool = pool;
  dbh->conninfo = conninfo;
  dbh->flags = flags;
  dbh->in_transaction = 0;

  /* Begin the database connection. */
  dbh->conn = PQconnectStart (conninfo);
  if (dbh->conn == 0)		/* Failed. */
    return 0;

  /* See the PostgreSQL documentation for the libpq connect functions
   * for details about how the following loop works.
   */
  status = PQstatus (dbh->conn);
  if (status == CONNECTION_BAD)
    {
      PQfinish (dbh->conn);
      return 0;			/* Connection failed immediately. */
    }

  if (PQsetnonblocking (dbh->conn, 1) == -1) abort ();
  fd = PQsocket (dbh->conn);

  status = PGRES_POLLING_WRITING;

  while (status != PGRES_POLLING_OK &&
	 status != PGRES_POLLING_FAILED)
    {
      switch (status)
	{
	case PGRES_POLLING_WRITING:
	  pth_wait_writable (fd);
	  break;
	case PGRES_POLLING_READING:
	  pth_wait_readable (fd);
	  break;
	}
      status = PQconnectPoll (dbh->conn);
    }

  if (status == PGRES_POLLING_FAILED)
    {
      PQfinish (dbh->conn);
      return 0;			/* Connection failed. */
    }

  /*- Connected! -*/

  DEBUG (dbh, 0, "connected");

  /* Remember to clean up this connection when the pool gets deleted. */
  pool_register_cleanup_fn (dbh->pool, disconnect, dbh);

  return dbh;
}

static void
disconnect (void *vdbh)
{
  db_handle dbh = (db_handle) vdbh;

  PQfinish (dbh->conn);

  DEBUG (dbh, 0, "disconnected");
}

void
db_set_debug (db_handle dbh, int d)
{
  if (d)
    {
      dbh->flags |= DBI_DEBUG;
      DEBUG (dbh, 0, "debugging enabled");
    }
  else
    {
      DEBUG (dbh, 0, "debugging disabled");
      dbh->flags &= ~DBI_DEBUG;
    }
}

int
db_get_debug (db_handle dbh)
{
  return dbh->flags & DBI_DEBUG;
}

void
db_commit (db_handle dbh)
{
  st_handle sth;

  sth = st_prepare_cached (dbh, "commit work");
  st_execute (sth);

  dbh->in_transaction = 0;
}

void
db_rollback (db_handle dbh)
{
  st_handle sth;

  sth = st_prepare_cached (dbh, "rollback work");
  st_execute (sth);

  dbh->in_transaction = 0;
}

int
st_serial (st_handle sth, const char *seq_name)
{
  db_handle dbh = sth->dbh;
  st_handle sth2;
  int serial;

  /* In PostgreSQL, to fetch the serial number we need to issue another
   * command to the database.
   */
  sth2 = st_prepare_cached (dbh, "select currval (?)", DBI_STRING);
  st_execute (sth2, seq_name);

  st_bind (sth2, 0, serial, DBI_INT);

  if (!st_fetch (sth2))
    {
      if ((dbh->flags & DBI_THROW_ERRORS))
	pth_die ("dbi: st_serial: failed to fetch sequence value");
      else
	return -1;
    }

  return serial;
}

static void finish_handle (void *vsth);

st_handle
new_st_handle (db_handle dbh, const char *query, int flags, ...)
{
  st_handle sth;
  pool pool;
  int i;
  va_list args;

  /* XXX Ignore the caching flag at the moment. Statement handles cannot
   * be trivially cached, because the same handle might then be used
   * concurrently in two different threads, which would cause serious
   * problems. Also since PostgreSQL doesn't support PREPARE yet, we
   * don't get the particular performance boost by caching query plans
   * on the server side anyway.
   *
   * Actually the first statement isn't strictly true. We should never
   * be sharing database handles across threads, so as long as we only
   * cache statements in the database handle, we ought to be OK.
   */

  /* Allocating in a subpool isn't strictly necessary at the moment. However
   * in the future it will allow us to safely free up the memory associated
   * with the statement handle when the handle is 'finished'.
   */
  pool = new_subpool (dbh->pool);
  sth = pmalloc (pool, sizeof *sth);

  sth->pool = pool;
  sth->dbh = dbh;
  sth->orig_query = query;
  sth->result = 0;
  sth->fetch_allowed = 0;
  sth->outtypes = 0;

  /* Examine the query string looking for ? and @ placeholders which
   * don't occur inside strings.
   * XXX Haven't implemented the test for placeholders inside strings
   * yet, so avoid using ? and @ as anything but placeholders for the
   * moment XXX
   */
  sth->query = pstrresplit2 (pool, query, re_qs);

  /* Set up the array of input types. */
  sth->intypes = new_vector (pool, int);
  va_start (args, flags);

  for (i = 0; i < vector_size (sth->query); ++i)
    {
      char *q;

      vector_get (sth->query, i, q);

      if (strcmp (q, "?") == 0 || strcmp (q, "@") == 0)
	{
	  int type = va_arg (args, int);

	  assert (DBI_MIN_TYPE <= type && type <= DBI_MAX_TYPE);
	  vector_push_back (sth->intypes, type);
	}
    }

  va_end (args);

  /* Remember to clean up this handle when the pool gets deleted. */
  pool_register_cleanup_fn (pool, finish_handle, sth);

  DEBUG (dbh, sth, "handle created for query: %s", sth->orig_query);

  return sth;
}

static void
finish_handle (void *vsth)
{
  st_handle sth = (st_handle) vsth;

  if (sth->result)
    PQclear (sth->result);
  sth->result = 0;

  DEBUG (sth->dbh, sth, "finished (implicit)");
}

static int exec_error (st_handle sth, PGresult *result);
static char *escape_string (pool, const char *);

int
st_execute (st_handle sth, ...)
{
  pool pool = sth->pool;
  int i, typeidx;
  va_list args;
  char *query;
  PGconn *conn;
  PGresult *result;
  int fd;
  ExecStatusType status;

  /* Formulate a query with the types substituted as appropriate. */
  query = pstrdup (pool, "");
  va_start (args, sth);

  for (i = 0, typeidx = 0; i < vector_size (sth->query); ++i)
    {
      char *q;
      int type;

      vector_get (sth->query, i, q);

      if (strcmp (q, "?") == 0)	/* Simple placeholder. */
	{
	  vector_get (sth->intypes, typeidx, type); typeidx++;

	  switch (type)
	    {
	    case DBI_INT:
	      query = pstrcat (pool, query, pitoa (pool, va_arg (args, int)));
	      break;

	    case DBI_INT_OR_NULL:
	      {
		int r = va_arg (args, int);

		if (r != 0)
		  query = pstrcat (pool, query, pitoa (pool, r));
		else
		  query = pstrcat (pool, query, "null");
	      }
	      break;

	    case DBI_STRING:
	      {
		const char *str = va_arg (args, const char *);

		if (str)
		  {
		    query = pstrcat (pool, query, "'");
		    query = pstrcat (pool, query, escape_string (pool, str));
		    query = pstrcat (pool, query, "'");
		  }
		else
		  query = pstrcat (pool, query, "null");
	      }
	      break;

	    case DBI_BOOL:
	      query =
		pstrcat (pool, query, va_arg (args, int) ? "'t'" : "'f'");
	      break;

	    case DBI_CHAR:
	      {
		char str[2] = { va_arg (args, int), '\0' }; /* sic */

		query = pstrcat (pool, query, "'");
		query = pstrcat (pool, query, escape_string (pool, str));
		query = pstrcat (pool, query, "'");
	      }
	      break;

	    case DBI_TIMESTAMP:
	    case DBI_INTERVAL:
	      abort ();		/* Not implemented yet! */

	    default:
	      abort ();
	    }
	}
      else if (strcmp (q, "@") == 0) /* List placeholder. */
	{
	  vector v;

	  vector_get (sth->intypes, typeidx, type); typeidx++;

	  /* We don't know yet if v is a vector of int or char *. */
	  v = va_arg (args, vector);

	  /* But we _do_ know that if the vector is empty, PG will fail.
	   * Stupid bug in PostgreSQL.
	   */
	  assert (vector_size (v) > 0);

	  switch (type)
	    {
	    case DBI_INT:
	      v = pvitostr (pool, v);
	      query = pstrcat (pool, query, pjoin (pool, v, ","));
	      break;

	    case DBI_STRING:
	      /* XXX Does not handle nulls correctly. */
	      query = pstrcat (pool, query, "'");
	      query = pstrcat (pool, query,
			       pjoin (pool,
				      pmap (pool, v, escape_string), "','"));
	      query = pstrcat (pool, query, "'");
	      break;

	    case DBI_BOOL:
	    case DBI_CHAR:
	    case DBI_INT_OR_NULL:
	    case DBI_TIMESTAMP:
	    case DBI_INTERVAL:
	      abort ();		/* Not implemented yet! */

	    default:
	      abort ();
	    }
	}
      else			/* String. */
	query = pstrcat (pool, query, q);
    }

  va_end (args);

  /* In transaction? If not, we need to issue a BEGIN WORK command. */
  if (!sth->dbh->in_transaction)
    {
      st_handle sth_bw;

      /* So we don't go into infinite recursion here ... */
      sth->dbh->in_transaction = 1;

      sth_bw = st_prepare_cached (sth->dbh, "begin work");
      if (st_execute (sth_bw) == -1)
	{
	  sth->dbh->in_transaction = 0;
	  return exec_error (sth, 0);
	}
    }

  DEBUG (sth->dbh, sth, "execute: %s", query);

  /* Get the connection. */
  conn = sth->dbh->conn;
  assert (PQisnonblocking (conn));
  fd = PQsocket (conn);

  /* Run it. */
  if (PQsendQuery (conn, query) != 1)
    return exec_error (sth, 0);

  /* Get all the command results. Ignore all but the last one. */
  do
    {
      /* Wait for the result. */
      while (PQisBusy (conn))
	{
	  /* Blocks ..? */
	  if (PQflush (conn) == EOF)
	    return exec_error (sth, 0);

          pth_wait_readable (fd);

	  if (PQconsumeInput (conn) != 1)
	    return exec_error (sth, 0);
	}

      result = PQgetResult (conn);
      if (result)
	{
	  if (sth->result) PQclear (sth->result);
	  sth->result = result;
	}
    }
  while (result);

  /* Get the result status. */
  status = PQresultStatus (sth->result);

  if (status == PGRES_COMMAND_OK) /* INSERT, UPDATE, DELETE, etc. */
    {
      char *s = PQcmdTuples (sth->result);
      int rv;

      sth->fetch_allowed = 0;

      if (s && strlen (s) > 0 && sscanf (s, "%d", &rv) == 1)
	return rv;		/* Return rows affected. */
      else
	return 0;		/* Command OK, unknown # rows affected. */
    }
  else if (status == PGRES_TUPLES_OK)
    {
      sth->fetch_allowed = 1;
      sth->next_tuple = 0;

      /* SELECT OK, return number of rows in the result. */
      return PQntuples (sth->result);
    }
  else
    /* Some other error. */
    return exec_error (sth, sth->result);
}

static char *
escape_string (pool pool, const char *s)
{
  int len = strlen (s);
  char *r = pmalloc (pool, len * 2 + 1);

  PQescapeString (r, s, len);
  return r;
}

static int
exec_error (st_handle sth, PGresult *result)
{
  if (!result)			/* Some sort of connection-related error. */
    {
      perror ("dbi: st_execute: database connection error");
      if ((sth->dbh->flags & DBI_THROW_ERRORS))
	pth_die ("dbi: st_execute: database execution error");
      else
	return -1;
    }
  else				/* Execution error. */
    {
      char *error = psprintf (sth->pool,
			      "dbi: st_execute: %s",
			      PQresultErrorMessage (result));

      fprintf (stderr, "%s\n", error);
      if ((sth->dbh->flags & DBI_THROW_ERRORS))
	pth_die (error);
      else
	return -1;
    }
}

void
_st_bind (st_handle sth, int colidx, void *varptr, int type)
{
  struct otype zero = { 0, 0 };
  struct otype ot;
  int extend_by;

  if (sth->outtypes == 0)
    sth->outtypes = new_vector (sth->pool, struct otype);

  /* Is the vector large enough? If not, extend it. */
  extend_by = colidx - vector_size (sth->outtypes) + 1;
  if (extend_by > 0)
    vector_fill (sth->outtypes, zero, extend_by);

  ot.type = type;
  ot.varptr = varptr;
  vector_replace (sth->outtypes, colidx, ot);
}

int
st_fetch (st_handle sth)
{
  int nr_rows, i;
  struct otype ot;

  if (!sth->result || !sth->fetch_allowed)
    {
      const char *error =
	"dbi: st_fetch: fetch without execute, or on a non-SELECT statement";

      fprintf (stderr, "%s\n", error);
      if ((sth->dbh->flags & DBI_THROW_ERRORS))
	pth_die (error);
      else
	return -1;
    }

  /* Get number of rows in the result. */
  nr_rows = PQntuples (sth->result);

  if (sth->next_tuple >= nr_rows)
    {
      DEBUG (sth->dbh, sth, "fetch: no more rows in query");
      return 0;			/* Finished. */
    }

  DEBUG (sth->dbh, sth, "fetch: starting row fetch");

  /* Fetch it. */
  if (sth->outtypes)
    for (i = 0; i < vector_size (sth->outtypes); ++i)
      {
	vector_get (sth->outtypes, i, ot);

	if (ot.type != 0 && ot.varptr != 0)
	  {
	    int is_null = PQgetisnull (sth->result, sth->next_tuple, i);
	    char *r = !is_null ? PQgetvalue (sth->result, sth->next_tuple, i)
	                       : 0;

	    DEBUG (sth->dbh, sth, "fetch: col %d: %s", i, r);

	    switch (ot.type)
	      {
	      case DBI_STRING:
		* (char **) ot.varptr = r;
		break;

	      case DBI_INT:
	      case DBI_INT_OR_NULL:
		if (is_null)
		  * (int *) ot.varptr = 0; /* Best we can do in C ! */
		else
		  sscanf (r, "%d", (int *) ot.varptr);
		break;

	      case DBI_BOOL:
		if (is_null)
		  * (int *) ot.varptr = 0; /* Best we can do! */
		else
		  * (int *) ot.varptr = strcmp (r, "t") == 0;
		break;

	      case DBI_CHAR:
		if (is_null)
		  * (char *) ot.varptr = 0; /* Best we can do! */
		else
		  * (char *) ot.varptr = r[0];
		break;

	      case DBI_TIMESTAMP:
		{
		  struct dbi_timestamp *ts =
		    (struct dbi_timestamp *) ot.varptr;

		  memset (ts, 0, sizeof *ts);
		  if (is_null)
		    ts->is_null = 1;
		  else
		    parse_timestamp (sth, r, ts);
		}
		break;

	      case DBI_INTERVAL:
		{
		  struct dbi_interval *inv =
		    (struct dbi_interval *) ot.varptr;

		  memset (inv, 0, sizeof *inv);
		  if (is_null)
		    inv->is_null = 1;
		  else
		    parse_interval (sth, r, inv);
		}
		break;

	      default:
		abort ();
	      }
	  }
      }

  DEBUG (sth->dbh, sth, "fetch: ended row fetch");

  sth->next_tuple++;

  return 1;			/* Row returned. */
}

static inline int
parse_fixed_width_int (const char *str, int width)
{
  int r = 0, i;

  for (i = 0; i < width; ++i)
    {
      r *= 10;
      r += str[i] - '0';
    }

  return r;
}

/* Parse a timestamp field from a PostgreSQL database, and return it
 * broken out into the dbi_timestamp structure. This can also parse
 * dates and times.
 */
static void
parse_timestamp (st_handle sth, const char *str, struct dbi_timestamp *ts)
{
  vector v;
  const char *s, *sign;

  /* Parse the timestamp. */
  v = prematch (sth->pool, str, re_timestamp, 0);

  if (!v)
    pth_die (psprintf (sth->pool,
		       "dbi: parse_timestamp: invalid timestamp: %s",
		       str));

  if (vector_size (v) <= 1) return;
  vector_get (v, 1, s);
  if (s) ts->year = parse_fixed_width_int (s, 4);

  if (vector_size (v) <= 2) return;
  vector_get (v, 2, s);
  if (s) ts->month = parse_fixed_width_int (s, 2);

  if (vector_size (v) <= 3) return;
  vector_get (v, 3, s);
  if (s) ts->day = parse_fixed_width_int (s, 2);

  if (vector_size (v) <= 4) return;
  vector_get (v, 4, s);
  if (s) ts->hour = parse_fixed_width_int (s, 2);

  if (vector_size (v) <= 5) return;
  vector_get (v, 5, s);
  if (s) ts->min = parse_fixed_width_int (s, 2);

  if (vector_size (v) <= 6) return;
  vector_get (v, 6, s);
  if (s) ts->sec = parse_fixed_width_int (s, 2);

  if (vector_size (v) <= 7) return;
  vector_get (v, 7, s);
  if (s) sscanf (s, "%d", &ts->microsecs);

  if (vector_size (v) <= 9) return;
  vector_get (v, 8, sign);
  vector_get (v, 9, s);
  if (sign && s)
    {
      ts->utc_offset = parse_fixed_width_int (s, 2);
      if (sign[0] == '-') ts->utc_offset = -ts->utc_offset;
    }
}

/* Parse an interval field from a PostgreSQL database, and return it
 * broken out into the dbi_interval structure.
 */
static void
parse_interval (st_handle sth, const char *str, struct dbi_interval *inv)
{
  vector v;
  const char *s;

  /* Parse the interval. */
  v = prematch (sth->pool, str, re_interval, 0);

  if (!v)
    pth_die (psprintf (sth->pool,
		       "dbi: parse_interval: invalid interval: %s",
		       str));

  if (vector_size (v) <= 1) return;
  vector_get (v, 1, s);
  if (s) sscanf (s, "%d", &inv->years);

  if (vector_size (v) <= 2) return;
  vector_get (v, 2, s);
  if (s) sscanf (s, "%d", &inv->months);

  if (vector_size (v) <= 3) return;
  vector_get (v, 3, s);
  if (s) sscanf (s, "%d", &inv->days);

  if (vector_size (v) <= 4) return;
  vector_get (v, 4, s);
  if (s) inv->hours = parse_fixed_width_int (s, 2);

  if (vector_size (v) <= 5) return;
  vector_get (v, 5, s);
  if (s) inv->mins = parse_fixed_width_int (s, 2);

  if (vector_size (v) <= 6) return;
  vector_get (v, 6, s);
  if (s) inv->secs = parse_fixed_width_int (s, 2);
}

vector
st_fetch_all_rows (st_handle sth)
{
  vector result;
  int row, nr_rows;
  int col, nr_cols;

  if (!sth->result || !sth->fetch_allowed)
    {
      const char *error =
	"dbi: st_fetch_all_rows: fetch without execute, or on a non-SELECT statement";

      fprintf (stderr, "%s\n", error);
      if ((sth->dbh->flags & DBI_THROW_ERRORS))
	pth_die (error);
      else
	return 0;
    }

  DEBUG (sth->dbh, sth, "fetch_all_rows");

  result = new_vector (sth->pool, vector);

  /* Get number of rows, columns in the result. */
  nr_rows = PQntuples (sth->result);
  nr_cols = PQnfields (sth->result);

  /* Fetch it. */
  for (row = 0; row < nr_rows; ++row)
    {
      vector v = new_vector (sth->pool, char *);

      for (col = 0; col < nr_cols; ++col)
	{
	  char *s = PQgetisnull (sth->result, row, col)
	    ? 0 : PQgetvalue (sth->result, row, col);

	  vector_push_back (v, s);
	}

      vector_push_back (result, v);
    }

  return result;
}

void
st_finish (st_handle sth)
{
  if (sth->result)
    PQclear (sth->result);
  sth->result = 0;

  DEBUG (sth->dbh, sth, "finished (explicit)");
}

#ifndef HAVE_PQESCAPESTRING
/* This is taken from the PostgreSQL source code. */

/* ---------------
 * Escaping arbitrary strings to get valid SQL strings/identifiers.
 *
 * Replaces "\\" with "\\\\" and "'" with "''".
 * length is the length of the buffer pointed to by
 * from.  The buffer at to must be at least 2*length + 1 characters
 * long.  A terminating NUL character is written.
 * ---------------
 */

size_t
PQescapeString(char *to, const char *from, size_t length)
{
	const char *source = from;
	char	   *target = to;
	unsigned int remaining = length;

	while (remaining > 0)
	{
		switch (*source)
		{
			case '\\':
				*target = '\\';
				target++;
				*target = '\\';
				/* target and remaining are updated below. */
				break;

			case '\'':
				*target = '\'';
				target++;
				*target = '\'';
				/* target and remaining are updated below. */
				break;

			default:
				*target = *source;
				/* target and remaining are updated below. */
		}
		source++;
		target++;
		remaining--;
	}

	/* Write the terminating NUL character. */
	*target = '\0';

	return target - to;
}
#endif

#if 0
/*
 *		PQescapeBytea	- converts from binary string to the
 *		minimal encoding necessary to include the string in an SQL
 *		INSERT statement with a bytea type column as the target.
 *
 *		The following transformations are applied
 *		'\0' == ASCII  0 == \\000
 *		'\'' == ASCII 39 == \'
 *		'\\' == ASCII 92 == \\\\
 *		anything >= 0x80 ---> \\ooo (where ooo is an octal expression)
 */
unsigned char *
PQescapeBytea(unsigned char *bintext, size_t binlen, size_t *bytealen)
{
	unsigned char *vp;
	unsigned char *rp;
	unsigned char *result;
	size_t		i;
	size_t		len;

	/*
	 * empty string has 1 char ('\0')
	 */
	len = 1;

	vp = bintext;
	for (i = binlen; i > 0; i--, vp++)
	{
		if (*vp == 0 || *vp >= 0x80)
			len += 5;			/* '5' is for '\\ooo' */
		else if (*vp == '\'')
			len += 2;
		else if (*vp == '\\')
			len += 4;
		else
			len++;
	}

	rp = result = (unsigned char *) malloc(len);
	if (rp == NULL)
		return NULL;

	vp = bintext;
	*bytealen = len;

	for (i = binlen; i > 0; i--, vp++)
	{
		if (*vp == 0 || *vp >= 0x80)
		{
			(void) sprintf(rp, "\\\\%03o", *vp);
			rp += 5;
		}
		else if (*vp == '\'')
		{
			rp[0] = '\\';
			rp[1] = '\'';
			rp += 2;
		}
		else if (*vp == '\\')
		{
			rp[0] = '\\';
			rp[1] = '\\';
			rp[2] = '\\';
			rp[3] = '\\';
			rp += 4;
		}
		else
			*rp++ = *vp;
	}
	*rp = '\0';

	return result;
}
#endif
