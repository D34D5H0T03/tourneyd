#ifndef SERVER_H
#define SERVER_H

#include "core/config.h"

int server_init(const config_t *cfg);
int server_run(void);
void server_shutdown(void);

#endif // SERVER_H