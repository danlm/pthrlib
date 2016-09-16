/* Generic server process.
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
 * $Id: pthr_server.h,v 1.4 2002/11/20 14:22:20 rich Exp $
 */

#ifndef PTHR_SERVER_H
#define PTHR_SERVER_H

#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif

/* Function: pthr_server_main_loop - Enter server main loop.
 * Function: pthr_server_default_port
 * Function: pthr_server_port_option_name
 * Function: pthr_server_default_address
 * Function: pthr_server_address_option_name
 * Function: pthr_server_disable_syslog
 * Function: pthr_server_package_name
 * Function: pthr_server_disable_fork
 * Function: pthr_server_disable_chdir
 * Function: pthr_server_disable_close
 * Function: pthr_server_chroot
 * Function: pthr_server_username
 * Function: pthr_server_stderr_file
 * Function: pthr_server_startup_fn
 * Function: pthr_server_enable_stack_trace_on_segv
 *
 * The function @code{pthr_server_main_loop} is a helper function which
 * allows you to write very simple servers quickly using @code{pthrlib}.
 * Normally your @code{main} should just contain a call to
 * @code{pthr_server_main_loop (argc, argv, processor)}
 * and you would include a function called @code{processor} which is
 * called on every connection.
 *
 * The other functions allow you to customise the behaviour of
 * @code{pthr_server_main_loop}. You should call any of these once
 * in your @code{main} before calling @code{pthr_server_main_loop}.
 *
 * By default the server listens on port 80 and allows you to
 * override this on the command line using the @code{-p} option.
 * To change this, call @code{pthr_server_default_port} and/or
 * @code{pthr_server_port_option_name}.
 *
 * By default the server listens on address INADDR_ANY (all local
 * addresses) and allows you to override this on the command line
 * using the @code{-a} option.  To change this, call
 * @code{pthr_server_default_address} and/or
 * @code{pthr_server_address_option_name}.
 *
 * By default the server writes package and version information to
 * syslog (although it will not be able to correctly determine the
 * package). Use @code{pthr_server_disable_syslog} to disable syslogging
 * entirely, or @code{pthr_server_package_name} to change the name displayed
 * in the logs.
 *
 * By default the server forks into the background, changes to the
 * root directory and disconnects entirely from the terminal. Use
 * @code{pthr_server_disable_fork}, @code{pthr_server_disable_chdir} and
 * @code{pthr_server_disable_close} to change these respectively.
 *
 * If you wish to have the server chroot after start up, then call
 * @code{pthr_server_chroot}. This is only possible if the server
 * is run as root, otherwise the chroot is silently ignored.
 *
 * If you wish to have the server change user and group after start up,
 * then call @code{pthr_server_username} (the group is taken from
 * the password file and @code{initgroups(3)} is also called). This
 * is only possible if the server is run as root, otherwise the
 * change user is silently ignored.
 *
 * If you wish to have @code{stderr} connected to a file (this is done after
 * changing user, so make sure the file is accessible or owned by the
 * user, not by root), then call @code{pthr_server_stderr_file}. Note
 * that unless you call this function or @code{pthr_server_disable_close}
 * then @code{stderr} is sent to @code{/dev/null}!
 *
 * The @code{startup_fn} is called after all of the above operations
 * have completed, but before the listener thread is created. You
 * do miscellaneous start-of-day functions from here, in particular
 * starting up other global threads. The command line arguments are also
 * passed to this function.
 *
 * If @code{pthr_server_enable_stack_trace_on_segv} is called, then
 * the server will attempt to dump a full stack trace to @code{stderr}
 * if it receives a @code{SIGSEGV} (segmentation fault). This is useful
 * for diagnosis of errors in production systems, but relies on some
 * esoteric GLIBC functions (if these functions don't exist, then this
 * setting does nothing). If the executable is linked with @code{-rdynamic}
 * then symbolic names will be given in the stack trace, if available.
 */
extern void pthr_server_main_loop (int argc, char *argv[], void (*processor_fn) (int sock, void *));
extern void pthr_server_default_port (int default_port);
extern void pthr_server_port_option_name (char port_option_name);
extern void pthr_server_default_address (in_addr_t default_address);
extern void pthr_server_address_option_name (char address_option_name);
extern void pthr_server_disable_syslog (void);
extern void pthr_server_package_name (const char *package_name);
extern void pthr_server_disable_fork (void);
extern void pthr_server_disable_chdir (void);
extern void pthr_server_disable_close (void);
extern void pthr_server_chroot (const char *root);
extern void pthr_server_username (const char *username);
extern void pthr_server_stderr_file (const char *pathname);
extern void pthr_server_startup_fn (void (*startup_fn) (int argc, char *argv[]));
extern void pthr_server_enable_stack_trace_on_segv (void);

#endif /* PTHR_SERVER_H */
