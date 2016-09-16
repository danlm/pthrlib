#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>

#include <pool.h>

#include <pthr_pseudothread.h>
#include <pthr_iolib.h>
#include <pthr_server.h>

static void start_processor (int sock, void *data);
static void run (void *);

typedef struct processor_thread
{
  pseudothread pth;		/* Pseudothread handle. */
  int sock;			/* Socket. */
} *processor_thread;

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
  pool pool;
  processor_thread p;

  pool = new_pool ();
  p = pmalloc (pool, sizeof *p);

  p->sock = sock;
  p->pth = new_pseudothread (pool, run, p, "processor thread");

  pth_start (p->pth);
}

static void
run (void *vp)
{
  processor_thread p = (processor_thread) vp;
  io_handle io;
  char buffer[256];

  io = io_fdopen (p->pth, p->sock);

  /* Sit in a loop reading strings and echoing them back. */
  while (io_fgets (buffer, sizeof buffer, io, 1))
    io_fputs (buffer, io);

  io_fclose (io);

  pth_exit (p->pth);
}
