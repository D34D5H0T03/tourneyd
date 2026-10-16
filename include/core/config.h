#ifndef CONFIG_H
#define CONFIG_H

#include <stddef.h>

//Memory Boundaries 
#define CONFIG_MAX_PORT_LEN 8
#define CONFIG_MAX_ADDR_LEN 64
#define CONFIG_MAX_PATH_LEN 256

typedef struct {
    char   port[CONFIG_MAX_PORT_LEN];
    char   bind_addr[CONFIG_MAX_ADDR_LEN];
    char   doc_root[CONFIG_MAX_PATH_LEN];
    int    backlog;
    int    max_children;
    int    idle_timeout_sec;
    int    request_timeout_sec;
    int    max_requests_per_conn;
    size_t max_body_size;
    size_t max_header_bytes;
    int    shutdown_grace_sec;
    int    trust_proxy;
} config_t;

/* 
 * Initializes the config_t structure with safe, hardcoded baseline values.
 * Must be called before attempting to parse files or command-line arguments.
 */
void config_defaults(config_t *cfg);

/* 
 * Opens, reads, and parses a key=value configuration file line by line.
 * Returns 0 on success, or -1 if the file is unreadable or malformed.
 */
int config_load_file(config_t *cfg, const char *path);

/* 
 * The primary configuration orchestrator. Reads the default or CLI-specified
 * config file, applies CLI flag overrides, and executes final validation.
 * Returns 0 on success, or -1 on syntax errors or constraint violations.
 */
int config_parse_args(config_t *cfg, int argc, char **argv);

/* 
 * Executes final logical bounds checks on the configuration before server start,
 * ensuring dynamic configs do not exceed hardcoded compile-time macro limits.
 * Returns 0 if valid, -1 if constraints are violated.
 */
int config_validate(const config_t *cfg);

#endif // CONFIG_H