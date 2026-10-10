#ifndef HTTP_HANDLER_H
#define HTTP_HANDLER_H

#include "../net/conn.h"

// Temporary handler to test the HTTP incremental parser
void http_test_handle(conn_t *client);

#endif // HTTP_HANDLER_H