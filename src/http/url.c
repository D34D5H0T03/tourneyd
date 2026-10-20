#include "http/url.h"

#include <string.h>

#define HEX_LETTER_OFFSET 10   // The decimal value where hex letters (A/a) begin
#define BITS_PER_NIBBLE 4      // Number of bits to shift for the first hex digit
#define URL_ENCODING_LENGTH 3  // Length of a URL-encoded character (e.g., "%20")
#define MAX_VALUE_LENGTH 1024

// returns hexadecimal value on success, -1 on fail
static int hex_val(char c){
    if(c >= '0' && c <= '9'){
        return c - '0';
    }
    if(c >= 'a' && c <= 'f'){
        return c - 'a' + HEX_LETTER_OFFSET;
    }
    if(c >= 'A' && c <= 'F'){
        return c - 'A' + HEX_LETTER_OFFSET;
    }
    return -1;
}

int http_url_decode(const char *in, char *out, size_t cap){
    size_t i = 0, j = 0;

    while(in[i] && j + 1 < cap){
        if(in[i] == '%'){
            if(!in[i + 1] || !in[i + 2]) return -1; // truncated
            int h1 = hex_val(in[i + 1]);
            int h2 = hex_val(in[i + 2]);
            if(h1 < 0 || h2 < 0) return -1; //invalid hex

            char c = (char)((h1 << BITS_PER_NIBBLE) | h2); // get the asci code with bitwise operations and cast into character
            if(c == '\0') return -1; // %00 null injection defence

            out[j++] = c; // append the decoded char to out
            i += URL_ENCODING_LENGTH; // increment by 3 for next character
        }else {
            out[j++] = in[i++]; 
        }
    }

    if(in[i]) return -1; // buffer overflow (cap exceeded)
    out[j] = '\0';
    return (int)j;
}

int http_query_get(const char *query, const char *key, char *out, size_t cap) {
    if (!query || !key || !out || cap == 0) return -1; // null check
    size_t klen = strlen(key);
    const char *p = query; // iterator

    while (*p) {
        // checks where the ampersand is and uses it as a delimiter
        const char *amp = strchr(p, '&');
        size_t pair_len = amp ? (size_t)(amp - p) : strlen(p); // key value pair len
        const char *eq = memchr(p, '=', pair_len); //seperate key and value
        
        if (eq) {
            size_t curr_klen = (size_t)(eq - p); // key length
            if (curr_klen == klen && strncmp(p, key, klen) == 0) {
                // Key matched. Extract and decode the value.
                size_t vlen = pair_len - curr_klen - 1;
                if (vlen >= cap) return -1; // Won't fit
                
                char temp[MAX_VALUE_LENGTH];
                if (vlen >= sizeof(temp)) return -1;
                memcpy(temp, eq + 1, vlen);
                temp[vlen] = '\0';
                
                // Form submissions often encode spaces as '+'
                for (size_t i = 0; i < vlen; i++) {
                    if (temp[i] == '+') temp[i] = ' ';
                }
                
                return http_url_decode(temp, out, cap);
            }
        }
        if (!amp) break; // no more key value pairs to check
        p = amp + 1; // next byte after ampersand
    }
    return -1;
}