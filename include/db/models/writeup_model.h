#ifndef WRITEUP_MODEL_H
#define WRITEUP_MODEL_H

#define WRITEUP_SLUG_MAX 64
#define WRITEUP_TITLE_MAX 200
#define WRITEUP_AUTHOR_MAX 100

typedef struct {
    char slug[WRITEUP_SLUG_MAX + 1];
    char title[WRITEUP_TITLE_MAX + 1];
    char author[WRITEUP_AUTHOR_MAX + 1];
} writeup_summary_t;

typedef struct {
    writeup_summary_t s;
    char *body_md; /* malloc'd */
} writeup_t;

typedef struct {
    const char *slug, *title, *author, *body_md, *source_url;
    const char *event_slug;     /* optional */
    const char *challenge_slug; /* optional */
} writeup_new_t;

// get the lest for browsing page
int  writeup_list(writeup_summary_t *out, int limit, int offset, int *count);

//get specific writeup
int  writeup_get(const char *slug, writeup_t *out);
// free dynamically allocated space for body
void writeup_free(writeup_t *w);

// create new writeup
int  writeup_create(const writeup_new_t *in);

#endif