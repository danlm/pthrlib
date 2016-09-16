/* Test the database interface.
 * Copyright (C) 2002 Richard W.M. Jones <rich@annexia.org>
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
 * $Id: test_dbi.c,v 1.6 2002/12/09 10:43:27 rich Exp $
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#ifdef HAVE_STRING_H
#include <string.h>
#endif

#include <pool.h>

#include "pthr_pseudothread.h"
#include "pthr_dbi.h"

static pool test_pool;
static pseudothread test_pth;

static void
do_test (void *data)
{
  db_handle dbh;
  st_handle sth;
  int userid, rownum;
  char *alias, *username;
  struct dbi_timestamp ts;
  struct dbi_interval inv;

  /* Open a connection to the database. */
  dbh = new_db_handle (test_pool, "", DBI_THROW_ERRORS);
  if (!dbh)
    pth_die ("failed to connect to the database, check PGHOST, etc.");

  /* Create some tables and some data. */
  sth = st_prepare_cached
    (dbh,
     "create temporary table tdbi_users "
     "  (userid int4, "
     "   username text not null, "
     "   age int2 not null, "
     "   last_login date, "
     "   unique (userid), "
     "   unique (username))");
  st_execute (sth);

  sth = st_prepare_cached
    (dbh,
     "create temporary table tdbi_aliases "
     "  (userid int4 references tdbi_users (userid), "
     "   alias text not null)");
  st_execute (sth);

  sth = st_prepare_cached
    (dbh,
     "insert into tdbi_users (userid, username, age) values (?, ?, ?)",
     DBI_INT, DBI_STRING, DBI_INT);
  st_execute (sth, 1, "rich", 30);
  st_execute (sth, 2, "anna", 45);
  st_execute (sth, 3, "bob", 55);
  st_execute (sth, 4, "dan", 24);

  sth = st_prepare_cached
    (dbh,
     "insert into tdbi_aliases (userid, alias) values (?, ?)",
     DBI_INT, DBI_STRING);
  st_execute (sth, 1, "richard");
  st_execute (sth, 1, "richie");
  st_execute (sth, 1, "richy");
  st_execute (sth, 2, "ann");
  st_execute (sth, 2, "annie");
  st_execute (sth, 3, "robert");
  st_execute (sth, 3, "bobbie");
  st_execute (sth, 3, "bobby");

  /* Select out some results. */
  sth = st_prepare_cached
    (dbh,
     "select u.userid, u.username, a.alias "
     "from tdbi_users u, tdbi_aliases a "
     "where u.userid = a.userid "
     "order by 3");
  st_execute (sth);

  st_bind (sth, 0, userid, DBI_INT);
  st_bind (sth, 1, username, DBI_STRING);
  st_bind (sth, 2, alias, DBI_STRING);

  rownum = 0;
  while (st_fetch (sth))
    {
      switch (rownum)
	{
	case 0:
	  assert (userid == 2 &&
		  strcmp (username, "anna") == 0 &&
		  strcmp (alias, "ann") == 0);
	  break;
	case 1:
	  assert (userid == 2 &&
		  strcmp (username, "anna") == 0 &&
		  strcmp (alias, "annie") == 0);
	  break;
	case 2:
	  assert (userid == 3 &&
		  strcmp (username, "bob") == 0 &&
		  strcmp (alias, "bobbie") == 0);
	  break;
	case 3:
	  assert (userid == 3 &&
		  strcmp (username, "bob") == 0 &&
		  strcmp (alias, "bobby") == 0);
	  break;
	case 4:
	  assert (userid == 1 &&
		  strcmp (username, "rich") == 0 &&
		  strcmp (alias, "richard") == 0);
	  break;
	case 5:
	  assert (userid == 1 &&
		  strcmp (username, "rich") == 0 &&
		  strcmp (alias, "richie") == 0);
	  break;
	case 6:
	  assert (userid == 1 &&
		  strcmp (username, "rich") == 0 &&
		  strcmp (alias, "richy") == 0);
	  break;
	case 7:
	  assert (userid == 3 &&
		  strcmp (username, "bob") == 0 &&
		  strcmp (alias, "robert") == 0);
	  break;
	default:
	  abort ();
	}

      rownum++;
    }

  sth = st_prepare_cached
    (dbh,
     "select username from tdbi_users where age > 40 order by 1");
  st_execute (sth);

  st_bind (sth, 0, username, DBI_STRING);

  rownum = 0;
  while (st_fetch (sth))
    {
      switch (rownum)
	{
	case 0:
	  assert (strcmp (username, "anna") == 0);
	  break;
	case 1:
	  assert (strcmp (username, "bob") == 0);
	  break;
	default:
	  abort ();
	}

      rownum++;
    }

  /* Select out one row, no rows. */
  sth = st_prepare_cached
    (dbh,
     "select userid from tdbi_users where username = ?", DBI_STRING);
  st_execute (sth, "rich");

  st_bind (sth, 0, userid, DBI_INT);

  assert (st_fetch (sth) != 0);
  assert (userid == 1);
  assert (st_fetch (sth) == 0);
  assert (userid == 1);		/* Hasn't splatted userid. */

  st_execute (sth, "fred");

  assert (st_fetch (sth) == 0);

  /* Check the st_finish function does nothing bad. */
  st_finish (sth);

  /* Drop the tables. */
  sth = st_prepare_cached
    (dbh,
     "drop table tdbi_aliases; drop table tdbi_users");
  st_execute (sth);

  /* Test timestamps and intervals.
   * XXX Retrieval only tested/supported at present.
   */
  sth = st_prepare_cached
    (dbh,
     "create temporary table tdbi_times "
     "  (ord int2 not null, ts timestamp, inv interval)");
  st_execute (sth);

  sth = st_prepare_cached
    (dbh,
     "insert into tdbi_times (ord, ts, inv) values (?, ?, ?)",
     DBI_INT, DBI_STRING, DBI_STRING);
  st_execute (sth, 0, "2002/11/09 01:02", 0);
  st_execute (sth, 1, "2002/10/07 03:04:05", "1 year 1 day");
  st_execute (sth, 2, "2002/09/04 06:07:08.999", "01:00");
  st_execute (sth, 3, 0, "30 mins");
  st_execute (sth, 4, 0, "1 year 2 months 6 days 8 hours 9 mins");

  sth = st_prepare_cached
    (dbh, "select ord, ts, inv from tdbi_times order by 1");
  st_execute (sth);

  st_bind (sth, 1, ts, DBI_TIMESTAMP);
  st_bind (sth, 2, inv, DBI_INTERVAL);

  assert (st_fetch (sth));
  assert (!ts.is_null);
  assert (ts.year == 2002 && ts.month == 11 && ts.day == 9 &&
	  ts.hour == 1 && ts.min == 2 && ts.sec == 0 &&
	  ts.microsecs == 0);
  assert (inv.is_null);

  assert (st_fetch (sth));
  assert (!ts.is_null);
  assert (ts.year == 2002 && ts.month == 10 && ts.day == 7 &&
	  ts.hour == 3 && ts.min == 4 && ts.sec == 5 &&
	  ts.microsecs == 0);
  assert (!inv.is_null);
  assert (inv.years == 1 && inv.months == 0 &&
	  inv.days == 1 && inv.hours == 0 && inv.mins == 0 &&
	  inv.secs == 0);

  assert (st_fetch (sth));
  assert (!ts.is_null);
  assert (ts.year == 2002 && ts.month == 9 && ts.day == 4 &&
	  ts.hour == 6 && ts.min == 7 && ts.sec == 8 &&
	  ts.microsecs == 999);
  assert (!inv.is_null);
  assert (inv.years == 0 && inv.months == 0 &&
	  inv.days == 0 && inv.hours == 1 && inv.mins == 0 &&
	  inv.secs == 0);

  assert (st_fetch (sth));
  assert (ts.is_null);
  assert (!inv.is_null);
  assert (inv.years == 0 && inv.months == 0 &&
	  inv.days == 0 && inv.hours == 0 && inv.mins == 30 &&
	  inv.secs == 0);

  assert (st_fetch (sth));
  assert (ts.is_null);
  assert (!inv.is_null);
  assert (inv.years == 1 && inv.months == 2 &&
	  inv.days == 6 && inv.hours == 8 && inv.mins == 9 &&
	  inv.secs == 0);

  /* Drop the table. */
  sth = st_prepare_cached
    (dbh,
     "drop table tdbi_times");
  st_execute (sth);

  /* Try rolling back the database. */
  db_rollback (dbh);
}

int
main ()
{
  char *env = getenv ("TEST_DBI");

  /* Skip the test unless the 'TEST_DBI' environment variable is set. */
  if (!env || strcmp (env, "1") != 0)
    {
      fprintf (stderr,
"WARNING: DBI test skipped. If you want to run the DBI test, then you must\n"
"have:\n"
"  (a) A working PostgreSQL >= 7.1 database.\n"
"  (b) postgresql-devel packages installed (ie. libpq, header files).\n"
"  (c) PGHOST, etc., set up to provide access to a database where I can\n"
"      create temporary tables.\n"
"Set the TEST_DBI environment variable to 1 and run this test again.\n");

      exit (0);
    }

  test_pool = new_pool ();
  test_pth = new_pseudothread (test_pool, do_test, 0, "testing thread");
  pth_start (test_pth);

  while (pseudothread_count_threads () > 0)
    reactor_invoke ();

  exit (0);
}
