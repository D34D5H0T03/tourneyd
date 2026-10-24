#include "util/utf8.h"
#include <stdint.h>
#include <stddef.h>

// UTF-8 Structural Masks and Prefixes
#define ASCII_THRESHOLD         0x80

#define MASK_2_BYTE             0xE0
#define PREFIX_2_BYTE           0xC0
#define PAYLOAD_2_BYTE          0x1F

#define MASK_3_BYTE             0xF0
#define PREFIX_3_BYTE           0xE0
#define PAYLOAD_3_BYTE          0x0F

#define MASK_4_BYTE             0xF8
#define PREFIX_4_BYTE           0xF0
#define PAYLOAD_4_BYTE          0x07

// Continuation Byte Masks
#define CONT_MASK               0xC0
#define CONT_PREFIX             0x80
#define CONT_PAYLOAD            0x3F
#define SHIFT_BITS              6

// Minimum Valid Code Points (prevents overlong encoding)
#define MIN_CP_2_BYTE           0x80
#define MIN_CP_3_BYTE           0x800
#define MIN_CP_4_BYTE           0x10000

// Strict Unicode Boundaries
#define MAX_VALID_CP            0x10FFFF
#define SURROGATE_START         0xD800
#define SURROGATE_END           0xDFFF

/* Length (1-4) of the valid UTF-8 sequence at s, or 0 if the bytes are not valid UTF-8. */
size_t utf8_seq_len(const char *s, size_t n) {
    if (n == 0) return 0;
    
    const unsigned char *p = (const unsigned char *)s;
    unsigned char c = p[0];
    
    // ASCII Fast Path
    if (c < ASCII_THRESHOLD) return 1;
    
    size_t need;
    uint32_t cp;
    uint32_t min;
    
    // Leading Byte Analysis
    if ((c & MASK_2_BYTE) == PREFIX_2_BYTE) { 
        need = 1; 
        cp = c & PAYLOAD_2_BYTE; 
        min = MIN_CP_2_BYTE; 
    } 
    else if ((c & MASK_3_BYTE) == PREFIX_3_BYTE) { 
        need = 2; 
        cp = c & PAYLOAD_3_BYTE; 
        min = MIN_CP_3_BYTE; 
    } 
    else if ((c & MASK_4_BYTE) == PREFIX_4_BYTE) { 
        need = 3; 
        cp = c & PAYLOAD_4_BYTE; 
        min = MIN_CP_4_BYTE; 
    } 
    else {
        return 0; // Invalid leading byte
    }
    
    // Truncation Check 
    if (n <= need) return 0; 
    
    // Continuation Validation & Payload Assembly
    for (size_t i = 1; i <= need; i++) {
        if ((p[i] & CONT_MASK) != CONT_PREFIX) return 0;
        cp = (cp << SHIFT_BITS) | (p[i] & CONT_PAYLOAD);
    }
    
    // Code Point Validation
    if (cp < min || cp > MAX_VALID_CP || (cp >= SURROGATE_START && cp <= SURROGATE_END)) {
        return 0;
    }
    
    return need + 1;
}

int utf8_valid(const char *s, size_t n) {
    for (size_t i = 0; i < n; ) {
        size_t k = utf8_seq_len(s + i, n - i);
        if (k == 0) return 0;
        i += k;
    }
    return 1;
}