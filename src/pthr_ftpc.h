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
 * $Id: pthr_ftpc.h,v 1.5 2002/12/01 14:29:27 rich Exp $
 */

#ifndef PTHR_FTPC_H
#define PTHR_FTPC_H

#include <pool.h>
#include <vector.h>

#include <pthr_pseudothread.h>
#include <pthr_iolib.h>

struct ftpc;
typedef struct ftpc *ftpc;

/* Function: new_ftpc - Create a new FTP client object.
 *
 * Create a new FTP client object, connected to the FTP server
 * called @code{server}. The @code{server} may be an IP address or a hostname.
 * If the @code{server} name ends with @code{:port} then @code{port}
 * is the port number to connect to.
 *
 * The default mode for new connections is active mode. Call
 * @ref{ftpc_set_mode(3)} to change the mode.
 *
 * See also: @ref{ftpc_login(3)}, @ref{ftpc_set_mode(3)}.
 */
extern ftpc new_ftpc (pool, const char *server);

/* Function: ftpc_set_passive_mode - Change to/from active or passive mode.
 *
 * If @code{flag} is true, all future connections on this @code{ftpc}
 * object will be in passive mode. If @code{flag} is false, all
 * future connections will be in active mode.
 *
 * Passive mode is required by a few servers and some firewalls.
 *
 * Returns: 0 if successful, -1 if the attempt failed.
 * If a fatal error occurs with the connection, an exception
 * is thrown.
 */
extern int ftpc_set_passive_mode (ftpc ftpc, int flag);

extern int ftpc_set_verbose (ftpc ftpc, int flag);

extern void ftpc_perror (ftpc f, const char *msg);

/* Function: ftpc_login - Log onto the FTP server.
 *
 * Attempt to log onto the FTP server as user @code{username} with
 * password @code{password}. If @code{username} is @code{NULL},
 * @code{"ftp"} or @code{"anonymous"}, then log in anonymously.
 * For anonymous logins, the @code{password} may be @code{NULL},
 * in which case the environment variable @code{LOGNAME} followed
 * by a single @code{@} character is used as the password.
 *
 * Returns: 0 if successful, -1 if the attempt failed.
 * If a fatal error occurs with the connection, an exception
 * is thrown.
 */
extern int ftpc_login (ftpc ftpc, const char *username, const char *password);

/* Function: ftpc_type - Set connection type.
 * Function: ftpc_ascii
 * Function: ftpc_binary
 *
 * @code{ftpc_type} sets the connection type. Most FTP servers only
 * support type 'a' (ASCII) or type 'i' (bInary), although esoteric
 * FTP servers might support 'e' (EBCDIC).
 *
 * @code{ftpc_ascii} sets the type to ASCII.
 *
 * @code{ftpc_binary} sets the type to bInary.
 *
 * Returns: 0 if successful, -1 if the attempt failed.
 * If a fatal error occurs with the connection, an exception
 * is thrown.
 */
extern int ftpc_type (ftpc ftpc, char type);
extern int ftpc_ascii (ftpc ftpc);
extern int ftpc_binary (ftpc ftpc);

/* Function: ftpc_cwd - Change directory on the server.
 * Function: ftpc_cdup
 *
 * @code{ftpc_cwd} changes the directory to @code{pathname}.
 *
 * @code{ftpc_cdup} moves to the parent directory. On most Unix-like
 * FTP servers this is equivalent to doing @code{CWD ..}
 *
 * Returns: 0 if successful, -1 if the attempt failed.
 * If a fatal error occurs with the connection, an exception
 * is thrown.
 */
extern int ftpc_cwd (ftpc ftpc, const char *pathname);
extern int ftpc_cdup (ftpc ftpc);

/* Function: ftpc_pwd - Return current directory on the server.
 *
 * @code{ftpc_pwd} returns the current directory on the server.
 *
 * Returns: The current directory, as a string allocated in the
 * pool, or NULL if the command fails. If a fatal error occurs
 * with the connection, an exception is thrown.
 */
extern char *ftpc_pwd (ftpc ftpc);

/* Function: ftpc_mkdir - Create or remove directories on the server.
 * Function: ftpc_rmdir
 *
 * @code{ftpc_mkdir} creates a directory called @code{pathname}
 * on the server. @code{ftpc_rmdir} removes a directory called
 * @code{pathname} on the server.
 *
 * Returns: 0 if successful, -1 if the attempt failed.
 * If a fatal error occurs with the connection, an exception
 * is thrown.
 */
extern int ftpc_mkdir (ftpc ftpc, const char *pathname);
extern int ftpc_rmdir (ftpc ftpc, const char *pathname);

/* Function: ftpc_delete - Delete a file on the server.
 *
 * @code{ftpc_delete} deletes a file called @code{pathname}
 * on the server.
 *
 * Returns: 0 if successful, -1 if the attempt failed.
 * If a fatal error occurs with the connection, an exception
 * is thrown.
 */
extern int ftpc_delete (ftpc ftpc, const char *pathname);

/* Function: ftpc_ls - List the contents of a directory on the server.
 * Function: ftpc_dir
 *
 * @code{ftpc_ls} and @code{ftpc_dir} list the contents of either
 * the current directory (if @code{pathname} is @code{NULL}) or
 * else another directory @code{pathname}.
 *
 * @code{ftpc_ls} issues the command @code{NLST -a}, returning a
 * vector of strings giving the name of each file.
 *
 * @code{ftpc_dir} issues the command @code{LIST -a}, returning
 * a human-readable list of filenames, similar to issuing the
 * @code{ls -al} command in Unix.
 *
 * Returns: 0 if successful, -1 if the attempt failed.
 * If a fatal error occurs with the connection, an exception
 * is thrown.
 */
extern vector ftpc_ls (ftpc ftpc, pool, const char *pathname);
extern vector ftpc_dir (ftpc ftpc, pool, const char *pathname);

/* Function: ftpc_get - Download or upload a file.
 * Function: ftpc_put
 *
 * @code{ftpc_get} attempts to download @code{remote_file} from the
 * server and store it in a local file called @code{local_file}.
 *
 * @code{ftpc_put} attempts to upload a file called @code{local_file}
 * to the server and store it in a file on the server called
 * @code{remote_file}.
 *
 * Returns: 0 if successful, -1 if the attempt failed.
 * If a fatal error occurs with the connection, an exception
 * is thrown.
 */
extern int ftpc_get (ftpc ftpc, const char *remote_file, const char *local_file);
extern int ftpc_put (ftpc ftpc, const char *local_file, const char *remote_file);

/* Function: ftpc_quote - Issue a command to the FTP server.
 *
 * @code{ftpc_quote} issues a command directly to the FTP server.
 * This function may be used for issuing @code{SITE} commands for
 * example.
 *
 * Returns: 0 if successful, -1 if the attempt failed.
 * If a fatal error occurs with the connection, an exception
 * is thrown.
 */
extern int ftpc_quote (ftpc ftpc, const char *cmd);

/* Function: ftpc_quit - Nicely disconnect from the FTP server.
 *
 * @code{ftpc_quit} sends a @code{QUIT} command to the FTP server
 * and then drops the network connection. After using this function,
 * the @code{ftpc} object associated with the connection is
 * invalid and should not be used.
 *
 * Returns: 0 if successful, -1 if the attempt failed.
 * If a fatal error occurs with the connection, an exception
 * is thrown.
 */
extern int ftpc_quit (ftpc ftpc);

#endif /* PTHR_FTPC_H */
