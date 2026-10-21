#ifndef CHALLENGE_MODEL_H
#define CHALLENGE_MODEL_H

#include <stdint.h>
#include "util/hash.h"
#include "util/slug.h" 

#define CHALLENGE_LIST_MAX  200
#define CHALLENGE_FLAG_MAX  256
#define CHALLENGE_DESC_MAX  (64 * 1024)

typedef struct { 
    char slug[SLUG_MAX + 1]; 
    char title[201]; 
    char category[33]; 
    int points; 
    int published; 
} challenge_summary_t;

typedef struct { 
    challenge_summary_t s; 
    char *description; /* malloc'd */
} challenge_t;    

typedef struct {
    const char *event_slug, *slug, *title, *category, *description, *flag;
    int points;
} challenge_new_t;

// returns a list of challange summary for browsing page (now supports admin include_unpublished flag)
int challenge_list(const char *event_slug, int include_unpublished, challenge_summary_t *out, int max, int *count);

// returns a specific challenge for the description page
int challenge_get(const char *event_slug, const char *slug, challenge_t *out);

// challenge description cleanup
void challenge_free(challenge_t *c);

// check the flag from submission
int challenge_check_flag(const char *event_slug, const char *slug, const char *flag, int *correct);

// creates new challenfe and inserts it into the db
int challenge_create(const challenge_new_t *in);

// publish or hide a challenge (used by tourneyctl)
int challenge_set_published(const char *event_slug, const char *slug, int published);

// delete a challenge entirely (used by tourneyctl)
int challenge_delete(const char *event_slug, const char *slug);

#endif