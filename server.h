typedef struct Connection Connection;
typedef struct Server Server;
typedef struct Action Action;
typedef struct Keymap Keymap;

extern Server *new_server (void *h);
extern void server_set_timeout (Server *v, int timeout);
extern void server_select (Server * v);

/* Connection constructors */
extern Connection *new_connection (Server * v, int fd, char *id, void *h);
extern Connection *server_listenport (Server * v, int port, int backlog, void *h);
extern Connection *connection_port (Server * v, char *hostname, int port, void *h);

extern void connection_remove (Connection * n);

extern int connection_fd (Connection *n);

/* Write to connection */
extern void connection_write (Connection * n, const char *data, int count);

/* Modifiers */
extern void connection_set_can_read (Connection *n,
				     void (*can_read) (Connection * n, void *h));
extern void connection_set_can_write (Connection *n,
				      void (*can_write) (Connection * n, void *h));
extern void connection_set_except (Connection *n,
				   void (*except) (Connection * n, void *h));
extern void connection_set_timeout (Connection *n,
				    void (*except) (Connection * n, void *h));
