#ifndef CHALLENGE_MODEL_H
#define CHALLENGE_MODEL_H

#include <stdint.h>
#include "util/hash.h"

#define SLUG_MAX 64
#define CHALLENGE_LIST_MAX 200
#define MAX_TITLE_LEN 201
#define MAX_CATEGORY_LEN 33

// the details of a challenge
typedef struct { 
    char slug[SLUG_MAX + 1]; 
    char title[MAX_TITLE_LEN]; 
    char category[MAX_CATEGORY_LEN]; 
    int points; 
} challenge_summary_t;

// the core challenge struct
typedef struct { 
    challenge_summary_t s; 
    char *description; /* malloc'd */
} challenge_t;    

// new challenge
typedef struct {
    const char *event_slug, *slug, *title, *category, *description, *flag;
    int points;
} challenge_new_t;

int challenge_list(const char *event_slug, challenge_summary_t *out, int max, int *count);
int challenge_get(const char *event_slug, const char *slug, challenge_t *out);
void challenge_free(challenge_t *c);

/* Records the attempt. Returns DB_OK (correct set), DB_NOT_FOUND, DB_RATE_LIMITED, or DB_ERROR. */
int challenge_check_flag(const char *event_slug, const char *slug, const char *flag, int *correct);

/* Used by tourneyctl: hashes the flag and inserts. DB_OK, DB_NOT_FOUND (unknown event), DB_CONFLICT. */
int challenge_create(const challenge_new_t *in);

#endif