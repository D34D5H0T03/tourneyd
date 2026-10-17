#include "connections/http_connection.h"
#include "http/parser.h"
#include "http/response.h"
#include "http/status.h"
#include "net/conn.h"
#include "util/log.h"
#include "http/router.h"
#include "util/shutdown.h"
#include "util/clock.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <strings.h>
#include <arpa/inet.h> // for ipv4
#include <netinet/in.h>

#define READ_BUFFER_SIZE 8192

// access logging buffer limits
#define LOG_IP_BUFSIZE 64
#define LOG_REQLINE_BUFSIZE 300
#define LOG_TARGET_BUFSIZE 200
#define LOG_VERSION_BUFSIZE 32
#define LOG_UA_BUFSIZE 128

// Bit-shift value to isolate the first octet of an IPv4 address
#define IPV4_FIRST_OCTET_SHIFT 24

// The first octet value for the 127.0.0.0/8 loopback block
#define IPV4_LOOPBACK_NET_PREFIX 127

// The byte index where the IPv4 address begins in a v4-mapped IPv6 address
#define IPV6_V4MAPPED_OCTET_INDEX 127
#define IPV6_V4MAPPED_IP_OFFSET 12

_Static_assert(READ_BUFFER_SIZE >= 2 * MAX_LINE_LENGTH,
               "read buffer must hold a partial line plus new data");

typedef struct
{
    char buffer[READ_BUFFER_SIZE];
    size_t have;
} http_conn_state_t;

/* The read buffer must be larger than MAX_LINE_LENGTH (4096). The parser
   rejects an endless line once more than MAX_LINE_LENGTH bytes pile up, so the
   buffer can never fill completely without the parser reaching a verdict. */

// helper functions

// for access logs

// check if peer is localhost
static int peer_is_loopback(const conn_t *c)
{
    if (c->peer.ss_family == AF_INET)
    {
        const struct sockaddr_in *a = (const struct sockaddr_in *)&c->peer;
        return (ntohl(a->sin_addr.s_addr) >> IPV4_FIRST_OCTET_SHIFT) == IPV4_LOOPBACK_NET_PREFIX;
    }

    if (c->peer.ss_family == AF_INET6)
    {
        const struct sockaddr_in6 *a = (const struct sockaddr_in6 *)&c->peer;
        if (IN6_IS_ADDR_LOOPBACK(&a->sin6_addr))
        {
            return 1;
        }
        if (IN6_IS_ADDR_V4MAPPED(&a->sin6_addr))
        {
            return a->sin6_addr.s6_addr[IPV6_V4MAPPED_IP_OFFSET] == IPV6_V4MAPPED_OCTET_INDEX;
        }
    }

    return 0;
}

// check if valid ipv4 or ipv6 format or not
static int looks_like_ip(const char *s)
{
    unsigned char tmp[sizeof(struct in6_addr)];
    return inet_pton(AF_INET, s, tmp) == 1 || inet_pton(AF_INET6, s, tmp) == 1;
}

// extracts the real client ip from the header
static void client_ip(const conn_t *c, const http_request_t *req, int trust_proxy,
                      char *out, size_t cap)
{
    if (trust_proxy && peer_is_loopback(c))
    {
        const char *fwd = http_request_header(req, "CF-Connecting-IP");
        if (fwd && looks_like_ip(fwd))
        {
            snprintf(out, cap, "%s", fwd);
            return;
        }
    }
    conn_peer_str(c, out, cap);
}

// Copies src for use inside quotes: control chars, '"' and '\' become '_'; long input ends in "...".
// sanitizes log log parsers or log ingestion tools like splunk can easily work with it
static void log_clean(char *dst, size_t cap, const char *src)
{
    size_t i = 0;
    for (; src[i] && i + 4 < cap; i++)
    {
        unsigned char ch = (unsigned char)src[i];
        dst[i] = (ch < ASCII_MIN_PRINTABLE || ch == ASCII_DEL || ch == '"' || ch == '\\') ? '_' : (char)ch;
    }
    if (src[i])
    {
        memcpy(dst + i, "...", 3);
        i += 3;
    }
    dst[i] = '\0';
}

static void log_request(const conn_t *c, const http_request_t *req, const http_response_t *res,
                        const http_conn_opts_t *opts, long long us, int sent_ok)
{

    char ip[LOG_IP_BUFSIZE];
    client_ip(c, req, opts->trust_proxy, ip, sizeof(ip));

    char reqline[LOG_REQLINE_BUFSIZE] = "-";
    if (req->path[0] != '\0')
    {
        // +2 accounts for the '?' character and the null-terminator '\0'
        char raw[MAX_PATH_LENGTH + MAX_QUERY_LENGTH + 2];
        snprintf(raw, sizeof(raw), "%s%s%s", req->path, req->query[0] ? "?" : "", req->query);

        char target[LOG_TARGET_BUFSIZE];
        char ver[LOG_VERSION_BUFSIZE];

        log_clean(target, sizeof(target), raw);
        log_clean(ver, sizeof(ver), req->version);

        snprintf(reqline, sizeof(reqline), "%s %s %s", http_method_to_str(req->method), target, ver);
    }

    const char *ua_raw = http_request_header(req, "User-Agent");
    char ua[LOG_UA_BUFSIZE];
    log_clean(ua, sizeof(ua), ua_raw ? ua_raw : "-");

    size_t bytes = (req->method == METHOD_HEAD) ? 0 : res->body_len;
    log_access("%s \"%s\" %d %zu %lldus \"%s\"%s", ip, reqline, (int)res->status,
               bytes, us, ua, sent_ok ? "" : " aborted");
}

// for keep-alive
// checks if token is in a comma separated header value
static int has_token(const char *list, const char *token)
{
    if (!list)
        return 0;

    size_t tlen = strlen(token);
    const char *p = list;
    while (*p)
    {
        while (*p == ' ' || *p == '\t' || *p == ',')
            p++;
        const char *start = p;
        while (*p && *p != ',')
            p++;
        const char *end = p;
        while (end > start && (end[-1] == ' ' || end[-1] == '\t'))
            end--;
        if ((size_t)(end - start) == tlen && strncasecmp(start, token, tlen) == 0)
        {
            return 1;
        }
    }
    return 0;
}

static int want_keep_alive(const http_request_t *req)
{
    // for HTTP/1.0
    const char *conn = http_request_header(req, "Connection");
    if (has_token(conn, "close"))
        return 0;

    if (strcmp(req->version, "HTTP/1.1") == 0)
        return 1; // HTTP/1.1 wants keep-alive by default
    // unless explicitly stated otherwise
    return has_token(conn, "keep-alive");
}

static int expects_continue(const http_request_t *req)
{
    const char *ex = http_request_header(req, "Expect");
    return ex && (strcasecmp(ex, "100-continue") == 0);
}

// Returns 1 = request complete, 0 = parse error (req->error_status set),
//  -1 = close silently (idle timeout, disconnect, shutdown).
static int read_request(conn_t *client, http_request_t *req, http_conn_state_t *st,
                        const http_conn_opts_t *opts)
{
    static const char cont[] = "HTTP/1.1 100 Continue\r\n\r\n";
    int started = (st->have > 0); // a pipelined request is already buffered
    int continue_sent = 0;

    conn_set_deadline(client, started ? opts->request_timeout_sec : opts->idle_timeout_sec);

    for (;;)
    {
        if (st->have > 0)
        { // parse what is already there BEFORE reading more
            size_t used = http_parse(req, st->buffer, st->have);
            memmove(st->buffer, st->buffer + used, st->have - used);
            st->have -= used;
            if (req->state == PARSE_DONE)
                return 1;
            if (req->state == PARSE_ERROR)
                return 0;

            if (req->state == PARSE_BODY && !continue_sent)
            {
                continue_sent = 1;
                if (expects_continue(req))
                    conn_write_all(client, cont, sizeof(cont) - 1);
            }
            if (st->have == sizeof(st->buffer))
            { // should be unreachable
                req->error_status = HTTP_STATUS_HEADER_TOO_LARGE;
                req->state = PARSE_ERROR;
                return 0;
            }
        }

        ssize_t n = conn_read(client, st->buffer + st->have, sizeof(st->buffer - st->have));
        if (n == CONN_TIMEOUT)
        {
            if (!started)
                return -1; // idle: close without a response
            req->error_status = HTTP_STATUS_REQUEST_TIMEOUT;
            req->state = PARSE_ERROR;
            return 0;
        }
        if (n <= 0)
            return -1;

        if (!started)
        { // first byte: switch to the request deadline
            started = 1;
            conn_set_deadline(client, opts->request_timeout_sec);
        }
        st->have += (size_t)n;
    }
}

// sends head then body, returns 0 on success -1 on failure
static int send_response(conn_t *client, const http_response_t *res,
                         const http_request_t *req, int keep_alive)
{
    size_t head_len = 0;
    char *head = http_response_serialize_head(res, keep_alive, &head_len);
    if (!head)
        return -1;

    int has_body = req->method != METHOD_HEAD && res->body_len > 0;
    int rc = (has_body) ? conn_write_more(client, head, head_len)
                        : conn_write_all(client, head, head_len);
    free(head);
    if (rc < 0)
        return -1;
    return (has_body) ? conn_write_all(client, res->body, res->body_len) : 0;
}

void http_connection_handle(conn_t *client, const void *ctx)
{
    const http_conn_opts_t *opts = ctx;
    if (!opts)
    {
        log_error("http: missing options");
        return;
    }

    // a zeroed request/response is safe to free even if init never ran
    http_conn_state_t *st = calloc(1, sizeof *st);
    http_request_t *req = calloc(1, sizeof *req);
    http_response_t *res = calloc(1, sizeof *res);
    if (!st || !req || !res)
    {
        log_error("http: out of memory");
        goto cleanup;
    }

    for (int served = 0; served < opts->max_requests_per_conn; served++)
    {
        http_request_init(req, &opts->limits);
        http_response_init(res);

        int result = read_request(client, req, st, opts);
        if (result < 0)
        {
            log_debug("connection closed after %d request(s)", served);
            break;
        }

        long long t0 = clock_us(); // Start timer, from request complete to response 

        if (result == 0)
            http_response_set_error(res, req->error_status);
        else
            router_dispatch(req, res);

        // http_response_add_security_headers(res); // TODO

        int keep = result == 1 && want_keep_alive(req) && res->status < 500 && served + 1 < opts->max_requests_per_conn && !shutdown_requested();

        conn_set_deadline(client, opts->request_timeout_sec);

        int sent_ok = send_response(client, res, req, keep) == 0;
        if (!sent_ok)
            keep = 0;

        // Log the completed request
        log_request(client, req, res, opts, clock_us() - t0, sent_ok);

        http_request_free(req);
        http_response_free(res);
        if (!keep)
            break;
    }

cleanup:
    if (req)
    {
        http_request_free(req);
        free(req);
    }
    if (res)
    {
        http_response_free(res);
        free(res);
    }
    free(st);
}