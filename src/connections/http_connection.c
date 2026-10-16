#include "connections/http_connection.h"
#include "http/parser.h"
#include "http/response.h"
#include "http/status.h"
#include "net/conn.h"
#include "util/log.h"
#include "http/router.h"

#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#define READ_BUFFER_SIZE 8192
#define CONN_DEADLINE 15

/* The read buffer must be larger than MAX_LINE_LENGTH (4096). The parser
   rejects an endless line once more than MAX_LINE_LENGTH bytes pile up, so the
   buffer can never fill completely without the parser reaching a verdict. */

// helper functions

// reads from client untill parser reaches DONE or ERROR
static int read_request(conn_t *client, http_request_t *req){
    char buffer[READ_BUFFER_SIZE];
    size_t have = 0;

    while(req->state != PARSE_DONE && req->state != PARSE_ERROR){
         // client, pointer increment and size reduction
        ssize_t n = conn_read(client, buffer + have, sizeof(buffer) - have);

        if(n == -2){ // case of timeout
            req->error_status = HTTP_STATUS_REQUEST_TIMEOUT;
            req->state = PARSE_ERROR;
            break;
        }

        if(n <= 0) return -1; // nothing read

        have += (size_t)n;

        size_t used = http_parse(req, buffer, have);
        memmove(buffer, buffer + used, have - used);
        have -= used;
    }

    return (req->state == PARSE_DONE) ? 1 : 0;
}

// sends head then body, returns 0 on success -1 on failure
static int send_response(conn_t *client, const http_response_t *res,
        const http_request_t *req, int keep_alive){
    size_t head_len = 0;

    // get the string
    char *head = http_response_serialize_head(res, keep_alive, &head_len);

    if(!head) return -1;

    // send headers
    int rc = conn_write_all(client, head, head_len);
    free(head);

    if(rc < 0){
        return -1;
    }

    //send body
    if(req->method != METHOD_HEAD && res->body_len > 0){
        rc = conn_write_all(client, res->body, res->body_len);
    }

    return rc;
}

void http_connection_handle(conn_t *client){

    conn_set_deadline(client, CONN_DEADLINE);
    http_request_t *req = malloc(sizeof(*req)); // around 30 KB, so need to keep it off stack
    http_response_t *res = malloc(sizeof(*res));

    if(!req || !res){ // malloc fail condition
        log_error("http: out of memory");
        goto cleanup;
    }

    http_request_init(req);
    http_response_init(res);

    int result = read_request(client, req);
    if(result < 0){
        goto cleanup;
    } 

    if(result == 0){
        http_response_set_error(res, req->error_status);
    }
    else{
        router_dispatch(req, res);
    }

    send_response(client, res, req, 0); // keep alive is 0 for now

    log_info("%s %s%s%s -> %d",
             http_method_to_str(req->method),
             req->path,
             req->query[0] ? "?" : "",
             req->query,
             (int)res->status);

cleanup:
    if(req){
        http_request_free(req);
        free(req);
    }
    if(res){
        http_response_free(res);
        free(res);  
    }

}

