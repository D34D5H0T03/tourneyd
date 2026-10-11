#ifndef HTTP_HANDLER_H
#define HTTP_HANDLER_H

#include "net/conn.h"

// The main HTTP connection orchestrator
void http_connection_handle(conn_t *client);

#endif // HTTP_HANDLER_H