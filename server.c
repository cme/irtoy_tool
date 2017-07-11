/* ------------------------------------------------------------
 * Server/Connection management
 * XXX TODO: handle timeouts better. Set time?
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <fcntl.h>
#include <stdbool.h>
#include <termios.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <signal.h>
#include <sys/time.h>
#include <time.h>

#include "error.h"
#include "server.h"

struct Connection
{
  Server *server;
  char *id;
  int fd;
  Connection *next;
  Connection *prev;
  void (*can_write) (Connection * n, void *h);
  void (*can_read) (Connection * n, void *h);
  void (*except) (Connection * n, void *h);
  void (*timeout) (Connection * n, void *h);

  void *h;
};


struct Server
{
  Connection *first;
  Connection *last;
  struct timeval timeout;
  void *h;
};

Server *
new_server (void *h)
{
  Server *v = malloc (sizeof *v);
  v->first = NULL;
  v->last = NULL;
  v->h = h;
  return v;
}

Connection *
new_connection (Server * v, int fd, char *id, void *h)
{
  Connection *n = malloc (sizeof *n);

  n->server = v;
  n->next = NULL;
  n->prev = v->last;
  if (v->last)
    v->last->next = n;
  n->fd = fd;
  n->id = strdup(id ? id : "(unnamed)");
  n->can_write = NULL;
  n->can_read = NULL;
  n->except = NULL;
  n->timeout = NULL;
  if (!v->first)
    v->first = n;
  v->last = n;
  v->h = h;

  return n;
}
void
connection_remove (Connection * n)
{
  Server *v = n->server;
  if (n == v->first)
    v->first = n->next;
  if (n == v->last)
    v->last = n->prev;
  if (n->next)
    n->next->prev = n->prev;
  if (n->prev)
    n->prev->next = n->next;
  free (n->id);
  free (n);
}

void
connection_write (Connection * n, const char *data, int count)
{
  /* Temporarily set the connection blocking. Blugh. */
  fcntl (n->fd, F_SETFL, fcntl (n->fd, F_GETFL) & ~O_NONBLOCK);
  for (;;)
    {
      int written;
      if (count <= 0)
        break;
      written = write (n->fd, data, count);
      if (written < 0)
        fatal (0, "Couldn't write to connection '%s'", n->id);
      count -= written;
      data += written;
    }
  /* Set non-blocking again */
  fcntl (n->fd, F_SETFL, fcntl (n->fd, F_GETFL) | O_NONBLOCK);
}

Connection *
server_listenport (Server * v, int port, int backlog, void *h)
{
  int fd;
  int reuseaddr = 1;
  int opts;
  struct sockaddr_in sa;
  char buffer[BUFSIZ];

  /* Set it up */
  fd = socket (AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (fd < 0)
    fatal (0, "Couldn't open socket");

  if (setsockopt (fd, SOL_SOCKET, SO_REUSEADDR,
                  &reuseaddr, sizeof reuseaddr) < 0)
    fatal (0, "Couldn't set socket options");
  opts = fcntl (fd, F_GETFL);
  if (opts < 0)
    fatal (0, "Couldn't get opts for socket");
  if (fcntl (fd, F_SETFL, opts | O_NONBLOCK) < 0)
    fatal (0, "Couldn't set socket non-blocking");

  opts = fcntl (fd, F_GETFL);

  /* Bind to the port */
  memset (&sa, '\0', sizeof sa);
  sa.sin_family = AF_INET;
  sa.sin_addr.s_addr = INADDR_ANY;
  sa.sin_port = htons (port);
  if (bind (fd, (struct sockaddr *) &sa, sizeof sa) < -1)
    fatal (0, "Couldn't bind port %d", port);

  if (listen (fd, backlog) < -1)
    fatal (0, "Couldn't listen on port %d", port);
  sprintf (buffer, "listen :%d", port);
  return new_connection (v, fd, buffer, h);
}

Connection *
connection_port (Server * v, char *hostname, int port, void *h)
{
  int socket_fd;
  struct hostent *hostinfo;
  struct sockaddr_in __hostaddr, *hostaddr = &__hostaddr;
  char buffer[BUFSIZ];

  memset (hostaddr, 0, sizeof (struct sockaddr));

  /* Look up address */
  hostinfo = gethostbyname (hostname);
  if (!hostinfo)
    /* fatal (0, "Can't resolve hostname '%s'", hostname); */
    return NULL;

  hostaddr->sin_family = hostinfo->h_addrtype;

  memcpy (&(hostaddr->sin_addr), hostinfo->h_addr, hostinfo->h_length);
  hostaddr->sin_port = htons (port);

  socket_fd = socket (AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (socket_fd == -1)
    fatal (0, "Can't create socket");

  if (connect (socket_fd, (struct sockaddr *) hostaddr, sizeof (*hostaddr)))
    return NULL;
  sprintf (buffer, "%s:%d", hostname, port);
  return new_connection (v, socket_fd, buffer, h);
}

void server_set_timeout (Server *v, int timeout)
{
  v->timeout.tv_sec = timeout / 1000000;
  v->timeout.tv_usec = timeout % 1000000;
}

void server_select (Server * v)
{
  Connection *n, *next_n;
  fd_set readfds, writefds, exceptfds;
  int high_fd;
  struct timeval timeout;
  int rv;
  int count_active = 0;

  FD_ZERO (&readfds);
  FD_ZERO (&writefds);
  FD_ZERO (&exceptfds);

  timeout = v->timeout;

  if (v->first == NULL)
    fatal (0, "Attempt to select on a Server with no connections");
  high_fd = v->first->fd;
  for (n = v->first; n; n = n->next)
    {
      if (!(n->can_write || n->can_read || n->except))
        continue;
      if (n->fd > high_fd)
        high_fd = n->fd;
      count_active++;

      if (n->can_write)
        FD_SET (n->fd, &writefds);
      if (n->can_read)
        FD_SET (n->fd, &readfds);
      if (n->except)
        FD_SET (n->fd, &exceptfds);
    }
  rv = select (high_fd + 1, &readfds, &writefds, &exceptfds, &timeout);

  if (rv == -1)
    fatal (0, "Error in select");
  if (rv == 0)
    {
      for (n = v->first; n; n = next_n)
        {
          next_n = n->next;
          if (n->timeout)
            n->timeout (n, n->h);
        }
    }
  else
    for (n = v->first; n; n = next_n)
      {
        next_n = n->next;
        if (FD_ISSET (n->fd, &exceptfds))
          n->except (n, n->h);
        else if (FD_ISSET (n->fd, &readfds))
          n->can_read (n, n->h);
        else if (FD_ISSET (n->fd, &writefds))
          n->can_write (n, n->h);
      }
}

int connection_fd(Connection *n)
{
  return n->fd;
}

void connection_set_can_read (Connection *n,
			      void (*can_read) (Connection * n, void *h))
{
  n->can_read = can_read;
}

void connection_set_can_write (Connection *n,
			       void (*can_write) (Connection * n, void *h))
{
  n->can_write = can_write;
}

void connection_set_except (Connection *n,
			    void (*except) (Connection * n, void *h))
{
  n->except = except;
}

void connection_set_timeout (Connection *n,
			     void (*timeout) (Connection * n, void *h))
{
  n->timeout = timeout;
}
