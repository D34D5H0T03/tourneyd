#ifndef WRITEUP_MODEL_H
#define WRITEUP_MODEL_H

#include <stddef.h>
#include <stdint.h>
#include "util/slug.h"

#define WRITEUP_LIST_MAX     50
#define WRITEUP_BODY_MAX     (256 * 1024)
#define WRITEUP_TITLE_MAX    200       /* bytes; the CLI enforces bytes so a row always fits these buffers */
#define WRITEUP_AUTHOR_MAX   100
#define WRITEUP_URL_MAX      512

typedef struct {
    char    slug[SLUG_MAX + 1];
    char    title[WRITEUP_TITLE_MAX + 1];
    char    author[WRITEUP_AUTHOR_MAX + 1];
    char    event_slug[SLUG_MAX + 1];         /* "" if the writeup is not tied to an event */
    char    challenge_slug[SLUG_MAX + 1];     /* "" if not tied to a challenge */
    char    source_url[WRITEUP_URL_MAX + 1];  /* "" if it is hosted here */
    int64_t created_at;                       /* unix seconds, UTC */
    int     published;
} writeup_summary_t;

typedef struct {
    writeup_summary_t s;
    char  *body_md;                           /* malloc'd, NUL-terminated; release with writeup_free */
    size_t body_len;
} writeup_t;

typedef struct {
    const char *slug, *title;
    const char *author;                       /* NULL = "" */
    const char *event_slug;                   /* NULL = none */
    const char *challenge_slug;               /* NULL = none; requires event_slug */
    const char *source_url;                   /* NULL = none */
    const char *body_md;
    size_t      body_len;                     /* 1..WRITEUP_BODY_MAX, no NUL bytes */
    int         published;
} writeup_new_t;

/* Newest first. `out` must hold `max` entries (clamped to WRITEUP_LIST_MAX). `total` may be NULL.
   include_unpublished is for the admin tool; the public API passes 0. DB_OK or DB_ERROR. */
int  writeup_list(int offset, int include_unpublished, writeup_summary_t *out, int max,
                  int *count, int *total);

/* Published writeups only. DB_OK, DB_NOT_FOUND or DB_ERROR. */
int  writeup_get(const char *slug, writeup_t *out);

/* Frees the dynamically allocated markdown body. */
void writeup_free(writeup_t *w);

/* DB_OK, DB_CONFLICT (slug exists), DB_NOT_FOUND (unknown event or challenge), DB_INVALID, DB_ERROR. */
int  writeup_create(const writeup_new_t *in);

/* Insert, or replace everything but created_at when the slug exists. Same codes, never DB_CONFLICT. */
int  writeup_upsert(const writeup_new_t *in);

/* Admin tools for hiding/showing and deleting writeups. */
int  writeup_set_published(const char *slug, int published);    /* DB_OK / DB_NOT_FOUND / DB_ERROR */
int  writeup_delete(const char *slug);                          /* DB_OK / DB_NOT_FOUND / DB_ERROR */

#endif