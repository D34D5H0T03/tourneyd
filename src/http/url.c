#include "http/url.h"

#define HEX_LETTER_OFFSET 10   // The decimal value where hex letters (A/a) begin
#define BITS_PER_NIBBLE 4      // Number of bits to shift for the first hex digit
#define URL_ENCODING_LENGTH 3  // Length of a URL-encoded character (e.g., "%20")

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