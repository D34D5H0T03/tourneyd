#ifndef SHUTDOWN_H
#define SHUTDOWN_H

void shutdown_request(void); // async-signal-safe: only sets a flag 
int  shutdown_requested(void);

#endif // SHUTDOWN_H