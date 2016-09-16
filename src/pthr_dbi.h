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
 * $Id: pthr_dbi.h,v 1.8 2002/12/01 15:06:47 rich Exp $
 */

#ifndef PTHR_DBI_H
#define PTHR_DBI_H

struct db_handle;
typedef struct db_handle *db_handle;

struct st_handle;
typedef struct st_handle *st_handle;

#include <pool.h>
#include <vector.h>

/* Function: new_db_handle - database interface library
 * Function: db_commit
 * Function: db_rollback
 * Function: new_st_handle
 * Function: st_prepare
 * Function: st_prepare_cached
 * Function: st_execute
 * Function: st_serial
 * Function: _st_bind
 * Function: st_bind
 * Function: st_fetch
 * Function: st_fetch_all_rows
 * Function: st_finish
 * Function: db_set_debug
 * Function: db_get_debug
 *
 * @code{pthr_dbi} is a library for interfacing pthrlib programs
 * with the PostgreSQL database (see @code{http://www.postgresql.org/}).
 * This library is a low-level wrapper around the functions in
 * @code{libpq}. It does not do such things as connection pooling,
 * user authentication, etc., but this functionality is provided in
 * the higher-level layers, such as @code{monolith}.
 *
 * @code{new_db_handle} creates a new database handle, and connects
 * to the database. The connection structure is allocated in
 * @code{pool}. The connection is automatically closed and the memory
 * freed when the pool is deleted. The @code{conninfo} string is
 * the connection string passed to @code{libpq}. This string is
 * fully documented in the PostgreSQL Programmer's Guide, Section I
 * (Client Interfaces), libpq, 1.2 Database Connection Functions.
 * Commonly the string will contain:
 *
 * @code{"host=HOSTNAME dbname=DBNAME"}
 *
 * or:
 *
 * @code{"host=HOSTNAME dbname=DBNAME user=USER password=PASSWORD"}
 *
 * The @code{flags} parameter contains zero or more of the following flags:
 *
 * @code{DBI_THROW_ERRORS}: If set causes database errors to
 * call @code{pth_die} (this is the recommended behaviour).
 *
 * Normally this function returns a database handle. If the database
 * connection fails, this function returns @code{NULL}.
 *
 * @code{db_commit} commits the current database transaction and
 * begins a new one.
 *
 * @code{db_rollback} rolls back the current database transaction and
 * begins a new one.
 *
 * If a database connection is closed without issuing either a commit
 * or rollback (eg. the pool is deleted or the program exits), then
 * the database will rollback the transaction. Some of this functionality
 * relies on the database to do the right thing.
 *
 * @code{new_st_handle}, and the synonyms @code{st_prepare} and
 * @code{st_prepare_cached} create a new statement and return the
 * statement handle. The @code{query} parameter is the SQL query.
 * '?' and '@' characters in the query may be used as placeholders
 * (when they appear outside strings) for scalar and vector values
 * respectively. The final parameter(s) are a list of the types
 * of these placeholders, and must correspond exactly to the types
 * passed in the @code{st_execute} call.
 *
 * If the @code{st_prepare_cached} form of statement creation is
 * used, then the statement is cached in the database handle. At
 * the moment, this doesn't make a lot of difference to performance,
 * but when a future version of PostgreSQL properly supports prepared
 * statements, this will make a big difference in performance by
 * allowing query plans to be cached on the server. In practice it
 * is almost always best to use @code{st_prepare_cached}. The only
 * possible exception is when using statements which refer to
 * temporary tables.
 *
 * @code{st_execute} executes the query with the given parameter
 * list. The parameters are substituted for the '?' and '@' placeholders
 * in the query, in order. The tyes of the parameters must correspond
 * exactly to the types passed in the prepare call.
 *
 * @code{st_execute} may be called multiple times on the same
 * statement handle. You do not need to (and should not, if possible)
 * prepare the statement each time.
 *
 * @code{st_execute} returns the number of rows affected, for
 * @code{INSERT} and @code{UPDATE} statements.
 *
 * If the command was an @CODE{INSERT} statement, then you can use
 * @code{st_serial} as a convenience function to return the serial
 * number assigned to the new row. The argument passed is the
 * sequence name (usually @code{tablename_columnname_seq}).
 *
 * @code{st_bind} binds a local variable to a column in the
 * result. The arguments are the column number (starting at 0),
 * the local variable name, and the type of the variable.
 *
 * Unlike in Perl DBI, you may call @code{st_bind} at any point
 * after preparing the statement, and bindings are persistent
 * across executes.
 *
 * Possible types for the prepare, @code{st_execute} and
 * @code{st_bind} calls: @code{DBI_INT}, @code{DBI_INT_OR_NULL},
 * @code{DBI_STRING}, @code{DBI_BOOL}, @code{DBI_CHAR},
 * @code{DBI_TIMESTAMP}, @code{DBI_INTERVAL}, @code{DBI_VECTOR_INT},
 * @code{DBI_VECTOR_INT_OR_NULL}, @code{DBI_VECTOR_STRING},
 * @code{DBI_VECTOR_BOOL}, @code{DBI_VECTOR_CHAR},
 * @code{DBI_VECTOR_TIMESTAMP}, @code{DBI_VECTOR_INTERVAL}.
 *
 * @code{DBI_INT_OR_NULL} differs from an ordinary @code{DBI_INT}
 * in that the integer value of @code{0} is treated as a @code{null}
 * (useful when passed as a parameter to @code{st_execute}, not very
 * useful otherwise).
 *
 * The @code{DBI_TIMESTAMP}, @code{DBI_INTERVAL}, @code{DBI_VECTOR_TIMESTAMP}
 * and @code{DBI_VECTOR_INTERVAL} types refer respectively to the
 * PostgreSQL database types @code{timestamp} and @code{interval}
 * and the relevant structures @code{struct dbi_timestamp} and
 * @code{struct dbi_interval} defined in @code{<pthr_dbi.h>}.
 *
 * @code{st_fetch} fetches the next result row from the query. It
 * returns true if the result row was fetched, or false if there
 * are no more rows. @code{st_fetch} returns the actual results
 * in the variables bound to each column by @code{st_bind}. Any
 * unbound columns are ignored.
 *
 * @code{st_fetch_all_rows} fetches all of the result rows
 * in one go, returning a @code{vector} of @code{vector} of @code{char *}.
 *
 * @code{st_finish} is an optional step which you may use once you
 * have finished with a statement handle. It frees up the memory
 * used by the results held in the statement handle. (This memory
 * would otherwise not be freed up until another @code{st_execute}
 * or the pool containing the statement handle is deleted).
 *
 * The @code{db_(set|get)_debug} functions are used to update the
 * state of the debug flag on a database handle. When this handle
 * is set to true, then database statements which are executed are
 * also printed out to @code{stderr}. The default is no debugging.
 *
 * It is not likely that we will support other databases in future
 * unless something dramatic happens to PostgreSQL. Install and learn
 * PostgreSQL and I promise that your life will be happier.
 */
extern db_handle new_db_handle (pool, const char *conninfo, int flags);
extern void db_commit (db_handle);
extern void db_rollback (db_handle);
extern st_handle new_st_handle (db_handle, const char *query, int flags, ...);
#define st_prepare(db,query,types...) new_st_handle ((db), (query), 0 , ## types)
#define st_prepare_cached(db,query,types...) new_st_handle ((db), (query), DBI_ST_CACHE , ## types)
extern int st_execute (st_handle, ...);
extern int st_serial (st_handle, const char *seq_name);
extern void _st_bind (st_handle, int colidx, void *varptr, int type);
#define st_bind(sth,colidx,var,type) _st_bind ((sth), (colidx), &(var), (type))
extern int st_fetch (st_handle);
extern vector st_fetch_all_rows (st_handle);
extern void st_finish (st_handle);
extern void db_set_debug (db_handle, int);
extern int db_get_debug (db_handle);

/* Flags for new_db_handle. */
#define DBI_THROW_ERRORS  0x0001
#define DBI_DEBUG         0x0002

/* Flags for new_st_handle. */
#define DBI_ST_CACHE      0x0001

/* Database types. */
/* NB. 0 must not be a valid type! */
#define DBI_MIN_TYPE           1001
#define DBI_INT                1001
#define DBI_STRING             1002
#define DBI_BOOL               1003
#define DBI_CHAR               1004
#define DBI_TIMESTAMP          1005
#define DBI_INTERVAL           1006
#define DBI_INT_OR_NULL        1007
#define DBI_MAX_TYPE           1007
#define DBI_VECTOR_INT         DBI_INT
#define DBI_VECTOR_STRING      DBI_STRING
#define DBI_VECTOR_BOOL        DBI_BOOL
#define DBI_VECTOR_CHAR        DBI_CHAR
#define DBI_VECTOR_TIMESTAMP   DBI_TIMESTAMP
#define DBI_VECTOR_INTERVAL    DBI_INTERVAL
#define DBI_VECTOR_INT_OR_NULL DBI_INT_OR_NULL

/* For the timestamp and interval types, these structures are used. */
struct dbi_timestamp
{
  int is_null;			/* NULL if true (other fields will be zero). */
  int year, month, day;
  int hour, min, sec;
  int microsecs;
  int utc_offset;
};

struct dbi_interval
{
  int is_null;			/* NULL if true (other fields will be zero). */
  int secs, mins, hours;
  int days, months, years;
};

#endif /* PTHR_DBI_H */
