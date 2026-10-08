#ifndef ECHO_H
#define ECHO_H

#include "../net/conn.h"

// Read data from client, convert it to uppercase, and write it back.
void echo_handle(conn_t *client);

#endif // ECHO_H