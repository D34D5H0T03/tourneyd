#ifndef HTTP_CONNECTION_H
#define HTTP_CONNECTION_H

#include "net/conn.h"

// The main HTTP connection orchestrator
void http_connection_handle(conn_t *client);

#endif // HTTP_CONNECTION_H