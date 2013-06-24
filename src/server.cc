/**
 * Implements a simple single-threaded TCP/IP server.
 */

#include "server_internal.h"

#include <event2/event.h>
#include <errno.h>
#include <sys/types.h>

#ifndef WIN32
#include <unistd.h>
#endif

// Include networking
#ifdef WIN32
#include <WS2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#endif

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifdef WIN32
#define close(x) closesocket(x)
#define read(s, buf, len) recv(s, buf, len, NULL)
#define strncpy(d, s, n) strncpy_s(d, n, s, n);
#endif


/**
 * Creates a new server socket
 */
int setup_server_socket(int port)
{
	int sock;
	int optval;
	sockaddr_in addr;

	// Open IPv4 TCP socket
	sock = socket(AF_INET, SOCK_STREAM, 0);

	if(sock == -1) {
		perror("socket()");
		return -1;
	}

	#ifndef WIN32
	// Disable blocking
	int flags = fcntl(sock, F_GETFL, 0);
	fcntl(sock, F_SETFL, flags | O_NONBLOCK);
	#endif

	// Disable nagling and allow reuse of address
	optval = 1;
	setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (char *) &optval, sizeof(optval));
	setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char *) &optval, sizeof(optval));

	memset(&addr, 0, sizeof(sockaddr_in));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	addr.sin_addr.s_addr = INADDR_ANY;

	if(bind(sock, (sockaddr *) &addr, sizeof(sockaddr_in)) == -1) {
		perror("bind()");
		close(sock);
		return -1;
	}

	if(listen(sock, 0) == -1) {
		perror("listen()");
		close(sock);
		return -1;
	}

	return sock;
}


/**
 * Accepts a pending client
 */
int accept_client(int sock)
{
	int client;
	int optval;
	sockaddr_in addr;
	socklen_t addr_size;

	addr_size = sizeof(sockaddr_in);

	// Accept connection
	client = accept(sock, (struct sockaddr *) &addr, &addr_size);

	if(client == -1) {
		perror("accept()");
		return -1;
	}

	// Disable blocking
	#ifndef WIN32
	int flags = fcntl(sock, F_GETFL, 0);
	fcntl(sock, F_SETFL, flags | O_NONBLOCK);
	#endif

	// Disable nagling
	optval = 1;
	setsockopt(client, IPPROTO_TCP, TCP_NODELAY, (char *) &optval, sizeof(optval));

	return client;
}


/**
 * Handles a read event.
 */
void net_on_read(evutil_socket_t fd, short events, void *server_v)
{
	net_server_t *server = (net_server_t *) server_v;

	char buffer[1024];
	size_t nread = read(fd, buffer, 1023);

	if(nread == -1) {
		perror("read()");
		disconnect(server->connection_data[fd]);
		return;
	}

	// Read did not succeed or connection was terminated
	if(nread == 0) {
		disconnect(server->connection_data[fd]);
	}

	// Read succeeded
	buffer[nread] = '\0';

	if(server->read_handler) {
		net_connection_t *conn = server->connection_data[fd];
		server->read_handler(conn, buffer, nread);
	}

	return;
}


/**
 * Called when a new connection is pending. Accepts the connection and starts
 * listening to incoming data.
 */
void net_on_new_connection(evutil_socket_t fd, short events, void *server_v)
{
	net_server_t *server = (net_server_t *) server_v;

	if(!server)
		return;

	int client = accept_client(server->sock);

    // Register handle
	event *event = event_new(server->event_base, client, EV_READ | EV_ET | EV_PERSIST, net_on_read, (void *) server);
	int result = event_add(event, NULL);

	if(result == -1) {
		perror("event_add()");
		event_free(event);
		close(client);
	}
    
    net_connection_t *conn = new net_connection_t();
	conn->fd = client;
	conn->server = server;
	conn->read_event = event;
	conn->write_event = NULL;
	conn->local = NULL;
	conn->frags_head = NULL;
	conn->frags_tail = NULL;

	if(server->connect_handler)
		conn->local = server->connect_handler(conn);

	server->connection_data[client] = conn;        
}


/**
 * Setup server state struct
 */
net_server_t *net_setup_server(event_base *event_base, void *context, int port)
{
	 net_server_t *server = NULL;

	// Initialize windows sockets
	#ifdef WIN32
	WSADATA wsa_data;
	if(WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
		perror("WSAStartup()");
		return NULL;
	}
	#endif

	try {
		server = new net_server_t();
	} catch(std::bad_alloc e) {
		return NULL;
	}

	server->context = context;
	server->event_base = event_base;

	// Create server socket
	server->sock = setup_server_socket(port);

	if(server->sock == -1) {
		delete server;
		return NULL;
	}

	// Add server socket to libevent
	event *event = event_new(server->event_base, server->sock, EV_READ | EV_ET | EV_PERSIST, net_on_new_connection, (void *) server);
	int result = event_add(event, NULL);

	if(result == -1) {
		perror("event_add()");
		event_free(event);
		delete server;
		return NULL;
	}

	// Set all handler to NULL
	server->connect_handler = NULL;
	server->disconnect_handler = NULL;
	server->read_handler = NULL;

	return server;
}


/**
 * Terminate a connection.
 */
int disconnect(net_connection_t *conn)
{
	net_server_t *server = conn->server;

	if(server->disconnect_handler)
		server->disconnect_handler(conn, &(conn->local));
	server->connection_data.erase(conn->fd);

	// Cancel monitoring of read events
	if(conn->read_event) {
		event_del(conn->read_event);
		event_free(conn->read_event);
		conn->read_event = NULL;
	}

	// Cancel monitoring of write events
	if(conn->write_event) {
		event_del(conn->write_event);
		event_free(conn->write_event);
		conn->write_event = NULL;
	}

	/* Free buffer */
	fragment_t *frag = conn->frags_head;

	while(frag) {
		free(frag);
		frag = frag->next;
	}

	conn->frags_head = NULL;
	conn->frags_tail = NULL;

	close(conn->fd);
	delete conn;

	return 0;
}


int net_disconnect(net_connection_t *conn)
{
  return disconnect(conn);
}


/**
 * Shuts down the server
 */
int net_teardown_server(net_server_t **s) {
  if(!s) 
    return -1;

  net_server_t *server = *s;

  if(!server)
    return -1;

  while(!server->connection_data.empty()) {
    std::map<int, net_connection_t *>::iterator it = server->connection_data.begin();
    printf("Closing (%d)\n", it->first);
    disconnect(it->second);
  }

  server->connection_data.clear();

  // Close socket and libevent
  event_base_free(server->event_base);
  server->event_base = NULL;
  close(server->sock);

  // Free memory
  delete server;
  *s = NULL;

  return 0;
}


/**
 * Attempts to write a single fragment from the buffer.
 *
 * Returns:
 * -1 - Error sending fragment
 *  0 - Partial fragment has been sent (terminate)
 *  1 - Entire fragment has been sent (possibly ready for more)
 */
int write_single_fragment(net_connection_t *conn)
{
  fragment_t *frag = conn->frags_head;

  char *buf = &(frag->data[frag->offset]);
  size_t size = frag->size - frag->offset;

  int retval = send(conn->fd, buf, size, 0);
  //printf("Wrote %d of %d bytes to %d\n", retval, size, conn->fd);

  if(retval >= 0) {
    frag->offset += retval;

    if(frag->offset < frag->size)
      return 0;

    conn->frags_head = frag->next;

    if(conn->frags_head == NULL)
      conn->frags_tail = NULL;

    free(frag->data);
    free(frag);

    return 1;
  }

  // Processing failed...
  if(retval == -1) {
    // Socket was not ready, try again later
    if(errno == EAGAIN || errno == EWOULDBLOCK)
      return 0;

    // Other error, disconnect
    perror("send()");
    disconnect(conn);
    return -1;
  }

  return 0;
}


/**
 * Attempts to write data to the socket.
 */
int handle_write_event(evutil_socket_t fd, short events, void *server_v)
{
  net_server_t *server = (net_server_t *) server_v;
  net_connection_t *conn = server->connection_data[fd];

  while(conn->frags_head) {
    int retval = write_single_fragment(conn);

    if(retval == 0)
      break;

    if(retval == -1)
      break;
  }

  // If buffer empty, remove flag
  if(conn->frags_head == NULL) {
	// FIXME!
    //event.events = EPOLLIN | EPOLLET;
    //epoll_ctl(server->epoll_set, EPOLL_CTL_MOD, event.data.fd, &event);
  }

  return 0;
}


/*int net_get_epoll_fd(net_server_t *server)
{
  return server->epoll_set;
}*/


/*int net_handle_epoll_events(net_server_t *server)
{
  epoll_event events[MAX_EVENTS];

  int nfds = epoll_wait(server->epoll_set, events, MAX_EVENTS, -1);

  printf("[tick] ");

  if(nfds == -1) {
    perror("epoll_pwait");
    return -1;
  }

  for(int n = 0; n < nfds; n++) {
    if(handle_single_event(server, events[n]) == -1) {
      fprintf(stderr, "handle_single_event() failed\n");
      //return -1;
    }
  }

  return 0;
}*/


/**
 * Returns global (server-wide) context for a given connection.
 */
void *net_get_global_data(net_connection_t *conn)
{
  return conn->server->context;
}


/**
 * Returns local (connection-only) context.
 */
void *net_get_local_data(net_connection_t *conn)
{
  return conn->local;
}


/**
 * Sends data to the connection specified
 */
int net_send(net_connection_t *conn, char *buf, size_t size, int flags)
{
  if(conn == NULL) {
    if(flags | FADOPTBUFFER)
      free(buf);
    return -1;
  }

  if(conn->server == NULL) {
    fprintf(stderr, "Server not set in connection structure\n"); 
    if(flags | FADOPTBUFFER)
      free(buf);
    return -1;
  }

  // Setup fragment
  fragment_t *frag = (fragment_t *) malloc(sizeof(fragment_t));
  if(frag == NULL) {
    perror("malloc()");
    if(flags | FADOPTBUFFER)
      free(buf);
    return -1;
  }

  if(flags | FADOPTBUFFER) {
    frag->data = buf;
  } else {
    frag->data = (char *) malloc(size);
    if(frag->data == NULL) {
      perror("malloc()");
      free(frag);
      return -1;
    }
    strncpy(frag->data, buf, size);
  }

  frag->offset = 0;
  frag->size = size;
  frag->next = NULL;

  // Add fragment to list
  if(conn->frags_head == NULL) {
    conn->frags_head = frag;
    conn->frags_tail = frag;
  } else {   
    conn->frags_tail->next = frag;
    conn->frags_tail = frag;
  }

  // If buffer not empty, add flag
  if(conn->frags_head != NULL) {
    /*epoll_event event;
    event.data.fd = conn->fd;    
    event.events = EPOLLIN | EPOLLOUT | EPOLLET;

    if(epoll_ctl(conn->server->epoll_set, EPOLL_CTL_MOD, conn->fd, &event) == -1) {
      perror("epoll_ctl()");
      return -1;
    }*/
  }

  return 0;
}


void net_set_connect_handler(net_server_t *server, connect_handler_t handler) {
  if(!server)
    return;
  server->connect_handler = handler;
}


void net_set_disconnect_handler(net_server_t *server, disconnect_handler_t handler) {
  if(!server)
    return;
  server->disconnect_handler = handler;
}


void net_set_read_handler(net_server_t *server, read_handler_t handler) {
  if(!server)
    return;
  server->read_handler = handler;
}



