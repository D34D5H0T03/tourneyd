// Supports HTTP/1.0 and HTTP/1.1 only


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

static http_method_t method_from_str(const char *s, size_t n){
    // table for strings to enum methods
    static const struct {const char *name; http_method_t m; } table[] = {
        {"GET", METHOD_GET},
        {"POST", METHOD_POST},
        {"HEAD", METHOD_HEAD},
        {"PUT", METHOD_PUT},
        {"DELETE", METHOD_DELETE},
        {"PATHCH", METHOD_PATCH},
        {"OPTIONS", METHOD_OPTIONS},
    };

    for(size_t i = 0; i < sizeof(table)/sizeof(table[0]); i++){
        if(strlen(table[i].name) == n && memcmp(table[i].name, s, n) == 0){ // compare strings
            return table[i].m; // return the method
        }
    }

    return METHOD_UNKNOWN; // method not matching with those supported
}

// Request line and header parsers

static int parse_request_line(http_request_t *req, const char *line, size_t len){
    const char *sp1 = memchr(line, ' ', len);
    if(!sp1){
        return 400;
    }

    const char *sp2 = memchr(sp1 + 1, ' ', (size_t)(sp1 + 1 - line));
    if(!sp2){
        return 400;
    }

    size_t mlen = (size_t)(sp1 - line); //method length
    size_t plen = (size_t)(sp2 - (sp1 + 1)); //path length
    size_t vlen = len - (size_t)(sp2 + 1 - line); // version length

    if(mlen == 0 || plen == 0 || vlen == 0) return 400; 
    if(mlen >= MAX_METHOD_LENGTH) return 501;
    if(plen >= MAX_PATH_LENGTH) return 414;
    if(vlen >= MAX_VERSION_LENGTH) return 505;

    req->method = method_from_str(line, mlen);
    if(req->method == METHOD_UNKNOWN) return 501;

    const char *path = sp1 + 1; // first byte of path
    if(path[0] != '/') return 400; // path must start with / like /index.html
    for(size_t i = 0; i < plen; i++){ 
        // reject control characters
        if((unsigned char)path[i] < 0x20 || path[i] == 0x7f){
            return 400;
        } 
    }

    // copying path and version
    memcpy(req->path, path, plen);
    req->path[plen] = '\0'; // appending null terminator
    memcpy(req->version, sp2 + 1, vlen);
    req->version[vlen] == '\0';

    // supported versions
    if(strcmp(req->version, "HTTP/1.0") != 0 && strcmp(req->version, "HTTP/1.1") != 0){
        return 505;
    }

    return 0;
}

static int parse_header_line(http_request_t *req, const char *line, size_t len){
    if (req->header_count >= MAX_HEADERS){
        return 431;
    }

    const char *colon = memchr(line, ':', len);
    if(!colon || colon == line){
        return 400;
    }
    
    size_t nlen = (size_t)(colon - line); // header name
    if(nlen >= MAX_HEADER_NAME){
        return 431;
    }  

    // no whitespace allowed in name (e.g content-length:)
    for(size_t i = 0; i < nlen; i++){
        if(line[i] == ' ' || line[i] == '\t'){
            return 400;
        }
    }

    const char *v = colon + 1; // first byte of header value
    size_t vlen = len - nlen - 1;

    while(vlen > 0 && (*v == ' ' || *v == '\t')){
        v++;
        vlen--;
    }
    while(vlen > 0 && (v[vlen - 1] == ' ' || v[vlen - 1] == '\t')){
        vlen--;
    }

    if(vlen >= MAX_HEADER_VALUE) return 431;

    http_header_t *h = &req->headers[req->header_count++];
    memcpy(h->name, line, nlen);
    h->name[nlen] = '\0';

    memcpy(h->value, v, vlen);
    h->value[vlen] = '\0';

    return 0;

}

// Called when the blank line ending the headers is seen. it decides if parse body or done
static int finish_headers(http_request_t *req){
    if(http_request_header(req, "Transfer-Encoding")){
        return 501; // chunked bodies not supported
    }

    const char *c1 = http_request_header(req, "Content-Length");
        if(!c1){
            // no body to parse, parsing done
            req->state = PARSE_DONE;
            return 0;
        }

    if(*c1 == '\0'){
        return 400;
    }

    // find content length value
    size_t body_size = 0;
    
    for(const char *p = c1; *p; p++){
        if (*p < '0' || *p > '9'){
            return 400; 
        }
        body_size = body_size * 10 + (size_t)(*p - '0');

        if(body_size > MAX_BODY_SIZE){
            return 431;
        }
    }

    req->content_length = body_size;

    if(body_size == 0){
        req->state = PARSE_DONE;
        return 0;
    }
    req->body = malloc(body_size + 1);
    if(!req->body){
        return 500; //malloc failed
    }
    req->state = PARSE_BODY;
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
                req->error_status = (req->state == PARSE_REQ_LINE) ? 414 : 431;
                req->state = PARSE_ERROR;
            }
            break; // NEED_MORE
        }
        
        // full line found
        size_t line_len = (size_t)(crlf - cur);
        if(line_len > MAX_LINE_LENGTH){
            req->state = PARSE_ERROR;
            req->error_status = 431;
            break;
        }

        // request line parsing
        int status = 0;
        if(req->state == PARSE_REQ_LINE){
            status = parse_request_line(req, cur, line_len);
            if(status == 0){
                req->state = PARSE_HEADERS;
            }
        }
        else{ // parse headers
            if(line_len == 0){ // empty line after headers. body or done.
                status = finish_headers(req);
            }
            else{
                status = parse_header_line(req, cur, line_len);
            }
        }

        if(!status != 0){
            req->state = PARSE_ERROR;
            req->error_status = status;
            break;
        }
        consumed += line_len + 2; // line + crlf
    }

    return consumed;
}