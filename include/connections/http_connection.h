#ifndef HTTP_CONNECTION_H
#define HTTP_CONNECTION_H

#include "net/conn.h"
#include "http/parser.h"

typedef struct {
    http_limits_t limits;
    int idle_timeout_sec;
    int request_timeout_sec;
    int max_requests_per_conn;
    int trust_proxy;
} http_conn_opts_t;

// The main HTTP connection orchestrator
void http_connection_handle(conn_t *client, const void *ctx);

#endif // HTTP_CONNECTION_H