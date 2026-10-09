#include "http/parser.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h> 

// Helper Functions


// carriage read line feeed(\r\n) finder
static const char *find_crlf(const char *buf, size_t len){
    for(size_t i = 0; i + 1 < len; i++){
        if(buf[i] == '\r' && buf[i + 1] == '\n'){
            return buf + 1;
        }
    }
    return NULL;
}

void http_request_init(http_request_t *req){
    memset(req, 0, sizeof(*req)); // zero out everything
    req->state = PARSE_REQ_LINE; // start of parsing
    req->method = METHOD_UNKNOWN;
}

void http_request_free(http_request_t *req){
    free(req->body);
    req->body = NULL; // handle memory leak and dangling pointer
}

const char* http_request_header(const http_request_t *req, const char *name){
    for(int i = 0; i < req->header_count; i++){
        if(strcasecmp(req->headers[i].name, name) == 0){
            return req->headers[i].value;
        }
    }
    return NULL;
}

size_t http_parse(http_request_t *req, const char* buffer, size_t buffer_len){
    size_t consumed = 0; // how many bytes read

    // body will be processed in terms of raw bytes
    while(req->state != PARSE_DONE && req->state != PARSE_ERROR){
        const char* cur = buffer + consumed;
        size_t avail = buffer_len - consumed;

        if(req->state == PARSE_BODY) {
            size_t need = req->content_length - req->body_received;
            size_t take = (avail < need) ? avail : need; // check how much is available and take based on need

            // the first pointer arithmetic moves the pointer to append the data
            memcpy(req->body + req->body_received, cur, take); // cur is the current buffer start pointer, take is how much
            req->body_received += take;
            consumed += take;

            if(req->body_received == req->content_length){
                req->body[req->content_length] == '\0'; //appending the null character at the end of string
                req->state = PARSE_DONE; //done with parsing
            }

            break; // done, or need more bytes
        }

        // we move to request lines and headers. here we need complete line

        const char *crlf = find_crlf(cur, avail);

        if(!crlf){
            if(avail > MAX_LINE_LENGTH){ //ENDLESS LINE
                req->state = PARSE_ERROR;
                req->error_status = (req->state == PARSE_REQ_LINE) ? 414 : 431;
            }
            break; // NEED_MORE
        }
        
    }
}