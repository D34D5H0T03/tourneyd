#include "core/config.h"
#include "http/parser.h"
#include "util/log.h"
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// Configuration Defaults
#define DEFAULT_CONFIG_PATH "config/server.conf"
#define DEFAULT_PORT "8080"
#define DEFAULT_BIND_ADDR "127.0.0.1"
#define DEFAULT_DOC_ROOT "public"
#define DEFAULT_BACKLOG 128
#define DEFAULT_MAX_CHILDREN 64
#define DEFAULT_IDLE_TIMEOUT 10
#define DEFAULT_REQ_TIMEOUT 15
#define DEFAULT_MAX_REQS 100
#define DEFAULT_MAX_BODY_SIZE (1024 * 1024)
#define DEFAULT_MAX_HEADER_BYTES (16 * 1024)
#define DEFAULT_SHUTDOWN_GRACE 10
#define DEFAULT_TRUST_PROXY 0

// Validation Boundaries
#define MIN_PORT 1
#define MAX_PORT 65535
#define MIN_BACKLOG 1
#define MAX_BACKLOG 4096
#define MIN_CHILDREN 1
#define MAX_CHILDREN 10000
#define MIN_TIMEOUT 1
#define MAX_TIMEOUT 3600
#define MIN_REQS_PER_CONN 1
#define MAX_REQS_PER_CONN 100000
#define MIN_GRACE_SEC 0
#define MAX_GRACE_SEC 600
#define MIN_BUSY_TIMEOUT_MS 0
#define MAX_BUSY_TIMEOUT_MS 60000

// Parsing Constants
#define CONFIG_LINE_BUF_SIZE 512
#define BASE_DECIMAL 10

/*
 * Initializes the config_t structure with safe baseline values.
 * Prevents uninitialized memory by zeroing the struct first.
 */
void config_defaults(config_t *cfg)
{
    memset(cfg, 0, sizeof *cfg);
    snprintf(cfg->port, sizeof(cfg->port), DEFAULT_PORT);
    snprintf(cfg->bind_addr, sizeof(cfg->bind_addr), DEFAULT_BIND_ADDR);
    snprintf(cfg->doc_root, sizeof(cfg->doc_root), DEFAULT_DOC_ROOT);
    cfg->backlog = DEFAULT_BACKLOG;
    cfg->max_children = DEFAULT_MAX_CHILDREN;
    cfg->idle_timeout_sec = DEFAULT_IDLE_TIMEOUT;
    cfg->request_timeout_sec = DEFAULT_REQ_TIMEOUT;
    cfg->max_requests_per_conn = DEFAULT_MAX_REQS;
    cfg->max_body_size = DEFAULT_MAX_BODY_SIZE;
    cfg->max_header_bytes = DEFAULT_MAX_HEADER_BYTES;
    cfg->shutdown_grace_sec = DEFAULT_SHUTDOWN_GRACE;
    cfg->trust_proxy = DEFAULT_TRUST_PROXY;
    snprintf(cfg->db_path, sizeof(cfg->db_path), "data/tourneyd.db");
    snprintf(cfg->migrations_dir, sizeof(cfg->migrations_dir), "migrations");
    cfg->db_busy_timeout_ms = 5000;
    cfg->lab_enabled = 0;
}

/*
 * Safely parses a string into a long integer, ensuring it falls within [lo, hi].
 * Returns 0 on success, -1 if the parsing fails, contains garbage, or exceeds bounds.
 */
static int parse_int(const char *s, long lo, long hi, long *out)
{
    char *end;
    errno = 0;
    long v = strtol(s, &end, BASE_DECIMAL);
    if (errno || end == s || *end != '\0' || v < lo || v > hi)
        return -1;
    *out = v;
    return 0;
}

/*
 * Converts common boolean string representations (on/true/1 or off/false/0) into integers.
 * Returns 0 on success, -1 on unrecognized strings.
 */
static int parse_bool(const char *s, int *out)
{
    if (!strcmp(s, "on") || !strcmp(s, "true") || !strcmp(s, "1"))
    {
        *out = 1;
        return 0;
    }
    if (!strcmp(s, "off") || !strcmp(s, "false") || !strcmp(s, "0"))
    {
        *out = 0;
        return 0;
    }
    return -1;
}

/*
 * Safely copies a string into a fixed-size character array.
 * Returns 0 on success, -1 if the source string is empty or exceeds the array capacity.
 */
static int set_str(char *dst, size_t cap, const char *v)
{
    size_t n = strlen(v);
    if (n == 0 || n >= cap)
        return -1;
    memcpy(dst, v, n + 1);
    return 0;
}

/*
 * Maps a configuration key to its corresponding field in the config_t struct.
 * Parses the string value into the correct data type using validation helpers.
 * Returns 0 on success, -1 for invalid values, and -2 for unknown keys.
 */
static int config_set(config_t *c, const char *key, const char *val)
{
    long n;
    if (!strcmp(key, "port"))
        return set_str(c->port, sizeof(c->port), val);
    if (!strcmp(key, "bind_addr"))
        return set_str(c->bind_addr, sizeof(c->bind_addr), val);
    if (!strcmp(key, "doc_root"))
        return set_str(c->doc_root, sizeof(c->doc_root), val);
    if (!strcmp(key, "db_path"))
        return set_str(c->db_path, sizeof(c->db_path), val);
    if (!strcmp(key, "migrations_dir"))
        return set_str(c->migrations_dir, sizeof(c->migrations_dir), val);

    if (!strcmp(key, "backlog"))
    {
        if (parse_int(val, MIN_BACKLOG, MAX_BACKLOG, &n))
            return -1;
        c->backlog = (int)n;
        return 0;
    }
    if (!strcmp(key, "max_children"))
    {
        if (parse_int(val, MIN_CHILDREN, MAX_CHILDREN, &n))
            return -1;
        c->max_children = (int)n;
        return 0;
    }
    if (!strcmp(key, "idle_timeout_sec"))
    {
        if (parse_int(val, MIN_TIMEOUT, MAX_TIMEOUT, &n))
            return -1;
        c->idle_timeout_sec = (int)n;
        return 0;
    }
    if (!strcmp(key, "request_timeout_sec"))
    {
        if (parse_int(val, MIN_TIMEOUT, MAX_TIMEOUT, &n))
            return -1;
        c->request_timeout_sec = (int)n;
        return 0;
    }
    if (!strcmp(key, "max_requests_per_conn"))
    {
        if (parse_int(val, MIN_REQS_PER_CONN, MAX_REQS_PER_CONN, &n))
            return -1;
        c->max_requests_per_conn = (int)n;
        return 0;
    }
    if (!strcmp(key, "shutdown_grace_sec"))
    {
        if (parse_int(val, MIN_GRACE_SEC, MAX_GRACE_SEC, &n))
            return -1;
        c->shutdown_grace_sec = (int)n;
        return 0;
    }
    if (!strcmp(key, "max_body_size"))
    {
        if (parse_int(val, 0, INT_MAX, &n))
            return -1;
        c->max_body_size = (size_t)n;
        return 0;
    }
    if (!strcmp(key, "max_header_bytes"))
    {
        if (parse_int(val, 1, INT_MAX, &n))
            return -1;
        c->max_header_bytes = (size_t)n;
        return 0;
    }
    if (!strcmp(key, "trust_proxy"))
    {
        int b;
        if (parse_bool(val, &b))
            return -1;
        c->trust_proxy = b;
        return 0;
    }
    if (!strcmp(key, "db_busy_timeout_ms"))
    {
        if (parse_int(val, MIN_BUSY_TIMEOUT_MS, MAX_BUSY_TIMEOUT_MS, &n))
            return -1;
        c->db_busy_timeout_ms = (int)n;
        return 0;
    }
    if (!strcmp(key, "lab_enabled"))
    {
        int b;
        if (parse_bool(val, &b))
            return -1;
        c->lab_enabled = b;
        return 0;
    }
    return -2;
}

/*
 * Removes leading and trailing whitespace from a mutable string in-place.
 * Returns a pointer to the first non-whitespace character.
 */
static char *trim(char *s)
{
    while (isspace((unsigned char)*s))
        s++;
    size_t n = strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1]))
        s[--n] = '\0';
    return s;
}

/*
 * Opens, reads, and parses a key=value configuration file line by line.
 * Strips comments, identifies syntax errors, and updates the config_t struct.
 * Returns 0 on success, -1 on any failure (missing file, line too long, typo, etc.).
 */
int config_load_file(config_t *cfg, const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f)
    {
        log_error("config: cannot open %s", path);
        return -1;
    }

    char line[CONFIG_LINE_BUF_SIZE];
    int lineno = 0, rc = 0;
    while (fgets(line, sizeof(line), f))
    {
        lineno++;
        if (!strchr(line, '\n') && !feof(f))
        {
            log_error("config:%d: line too long", lineno);
            rc = -1;
            break;
        }
        char *hash = strchr(line, '#');
        if (hash)
            *hash = '\0';
        char *s = trim(line);
        if (*s == '\0')
            continue;

        char *eq = strchr(s, '=');
        if (!eq)
        {
            log_error("config:%d: expected key = value", lineno);
            rc = -1;
            break;
        }
        *eq = '\0';
        char *key = trim(s), *val = trim(eq + 1);

        int r = config_set(cfg, key, val);
        if (r == -2)
        {
            log_error("config:%d: unknown key '%s'", lineno, key);
            rc = -1;
            break;
        }
        if (r < 0)
        {
            log_error("config:%d: bad value for '%s'", lineno, key);
            rc = -1;
            break;
        }
    }
    fclose(f);
    return rc;
}

/*
 * Executes final logical bounds checks on the configuration before server start.
 * Ensures dynamic configs do not exceed hardcoded compile-time macro limits.
 * Returns 0 if valid, -1 if constraints are violated.
 */
int config_validate(const config_t *c)
{
    long port;
    if (parse_int(c->port, MIN_PORT, MAX_PORT, &port))
    {
        log_error("config: port must be %d-%d", MIN_PORT, MAX_PORT);
        return -1;
    }
    if (c->max_body_size > MAX_BODY_SIZE)
    {
        log_error("config: max_body_size exceeds the compile-time cap (%d)", (int)MAX_BODY_SIZE);
        return -1;
    }
    if (c->max_header_bytes > (size_t)(MAX_HEADERS * (MAX_HEADER_NAME + MAX_HEADER_VALUE)))
    {
        log_error("config: max_header_bytes exceeds what the header table can hold");
        return -1;
    }
    if (c->request_timeout_sec < c->idle_timeout_sec)
    {
        log_error("config: request_timeout_sec should not be below idle_timeout_sec");
        return -1;
    }
    if (c->db_path[0] == '\0')
    {
        log_error("config: db_path cannot be empty");
        return -1;
    }
    if (c->migrations_dir[0] == '\0')
    {
        log_error("config: migrations_dir cannot be empty");
        return -1;
    }
    if (c->db_busy_timeout_ms < MIN_BUSY_TIMEOUT_MS || c->db_busy_timeout_ms > MAX_BUSY_TIMEOUT_MS)
    {
        log_error("config: db_busy_timeout_ms must be %d-%d", MIN_BUSY_TIMEOUT_MS, MAX_BUSY_TIMEOUT_MS);
        return -1;
    }
    return 0;
}

/*
 * Prints the correct usage syntax to standard error if CLI flags are malformed.
 */
static void usage(const char *prog)
{
    fprintf(stderr, "usage: %s [-c config] [-p port] [-b addr] [-r docroot]\n", prog);
}

/*
 * The primary configuration entry point. Executes a two-pass sweep of CLI arguments.
 * Pass 1: Identifies the config file (-c) or falls back to the default file, parsing it.
 * Pass 2: Parses CLI flags (-p, -b, -r) overriding the file settings.
 * Applies final validation before returning. Returns 0 on success, -1 on failure.
 */
int config_parse_args(config_t *cfg, int argc, char **argv)
{
    const char *file = NULL;
    for (int i = 1; i + 1 < argc; i++)
        if (!strcmp(argv[i], "-c"))
            file = argv[i + 1];

    if (file)
    {
        if (config_load_file(cfg, file) < 0)
            return -1;
    }
    else if (access(DEFAULT_CONFIG_PATH, R_OK) == 0)
    {
        if (config_load_file(cfg, DEFAULT_CONFIG_PATH) < 0)
            return -1;
    }

    int opt;
    // reset getopt state in case this is called multiple times (e.g., in tests)
    optind = 1;
    while ((opt = getopt(argc, argv, "c:p:b:r:h")) != -1)
    {
        switch (opt)
        {
        case 'c':
            break;
        case 'p':
            if (config_set(cfg, "port", optarg))
            {
                usage(argv[0]);
                return -1;
            }
            break;
        case 'b':
            if (config_set(cfg, "bind_addr", optarg))
            {
                usage(argv[0]);
                return -1;
            }
            break;
        case 'r':
            if (config_set(cfg, "doc_root", optarg))
            {
                usage(argv[0]);
                return -1;
            }
            break;
        default:
            usage(argv[0]);
            return -1;
        }
    }
    return config_validate(cfg);
}