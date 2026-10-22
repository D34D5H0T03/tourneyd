/* Turneyctl*/
/* tools/tourneyctl.c: content administration for tourneyd. It shares the model layer with the server. */
#define _GNU_SOURCE

#include "core/config.h"
#include "db/db.h"
#include "db/models/challenge_model.h"
#include "db/models/writeup_model.h"
#include "util/log.h"
#include "util/slug.h"
#include "util/utf8.h"

#include <ctype.h>
#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define EXIT_USAGE 2 // error with terminal usage rather than operational error
#define DEFAULT_EVENT "practice"
#define DEFAULT_CONFIG "config/server.conf"
#define FLAG_BUF (CHALLENGE_FLAG_MAX + 3) /* what slurp() allocates for a flag */

typedef struct
{
    const char *slug, *title, *author, *category, *event, *challenge, *source_url; // standard args
    const char *desc_file, *flag_file, *file;                                      // file path args
    int points, flag_stdin, yes;                                                   // ints
} opts_t;

static void usage(FILE *out)
{ // maunal
    fputs(
        "usage: tourneyctl [-c config] <command> [options]\n"
        "\n"
        "  migrate\n"
        "  challenge add   --slug S --title T --desc-file F (--flag-file F | --flag-stdin)\n"
        "                  [--event E] [--category C] [--points N]\n"
        "  challenge list  [--event E]\n"
        "  challenge hide|show|delete --slug S [--event E] [--yes]\n"
        "  writeup add|put --slug S --title T --file F.md [--author A] [--event E]\n"
        "                  [--challenge C] [--source-url U]\n"
        "  writeup list\n"
        "  writeup hide|show|delete --slug S [--yes]\n"
        "\n"
        "  'writeup add' fails if the slug exists, 'writeup put' replaces it.\n"
        "  Flags are never accepted on the command line (shell history, ps output).\n"
        "  Delete commands need --yes.\n",
        out);
}

// defines the options recognized by getopt_long()
static const struct option LONG_OPTS[] = {
    {"slug", required_argument, 0, 's'},
    {"title", required_argument, 0, 't'},
    {"author", required_argument, 0, 'a'},
    {"category", required_argument, 0, 'g'},
    {"event", required_argument, 0, 'e'},
    {"challenge", required_argument, 0, 'c'},
    {"source-url", required_argument, 0, 'u'},
    {"desc-file", required_argument, 0, 'd'},
    {"flag-file", required_argument, 0, 'f'},
    {"flag-stdin", no_argument, 0, 'i'},
    {"file", required_argument, 0, 'F'},
    {"points", required_argument, 0, 'p'},
    {"yes", no_argument, 0, 'y'},
    {0, 0, 0, 0}};

static int parse_opts(int argc, char **argv, opts_t *o)
{
    // zero out opts
    memset(o, 0, sizeof *o);
    o->points = 100; // default points
    optind = 1;      // index for getopt to track which arg is being processed
    int c;
    while ((c = getopt_long(argc, argv, "", LONG_OPTS, NULL)) != -1)
    {
        switch (c)
        {
        case 's':
            o->slug = optarg;
            break;
        case 't':
            o->title = optarg;
            break;
        case 'a':
            o->author = optarg;
            break;
        case 'g':
            o->category = optarg;
            break;
        case 'e':
            o->event = optarg;
            break;
        case 'c':
            o->challenge = optarg;
            break;
        case 'u':
            o->source_url = optarg;
            break;
        case 'd':
            o->desc_file = optarg;
            break;
        case 'f':
            o->flag_file = optarg;
            break;
        case 'i':
            o->flag_stdin = 1;
            break;
        case 'F':
            o->file = optarg;
            break;
        case 'y':
            o->yes = 1;
            break;
        case 'p':
        {
            char *end;
            errno = 0;
            long v = strtol(optarg, &end, 10); // convert to long. 10 for decimal notation
            if (errno || end == optarg || *end || v < 0 || v > 100000)
            { // points validation
                fprintf(stderr, "error: --points must be a number from 0 to 100000\n");
                return -1;
            }
            o->points = (int)v;
            break;
        }
        default:
            return -1;
        }
    }
    if (optind != argc)
    {
        fprintf(stderr, "error: unexpected argument '%s'\n", argv[optind]);
        return -1;
    }
    return 0;
}

// reads a whole file into memory
static char *slurp(const char *path, size_t max, size_t *len)
{
    // file or stdin
    FILE *f = path ? fopen(path, "rb") : stdin;
    if (!f)
    {
        fprintf(stderr, "error: cannot open %s: %s\n", path, strerror(errno));
        return NULL;
    }
    char *buf = malloc(max + 1); // space to check max file size and append the null byte
    if (!buf)
    {             // if allocation failed and no path provided then close and fail
        if (path) // no file would have been opened if no path
            fclose(f);
        return NULL;
    }
    // read max + 1 bytes (1 is th esize of each individual element)
    size_t n = fread(buf, 1, max + 1, f);
    int bad = ferror(f);
    if (path)
        fclose(f); // if path provided then we are done with file
    if (bad)
    {
        fprintf(stderr, "error: read failed\n");
        free(buf);
        return NULL;
    }
    if (n > max) // read more than max bytes
    {
        fprintf(stderr, "error: input is larger than %zu bytes\n", max);
        free(buf);
        return NULL;
    }
    buf[n] = '\0'; // append null byte
    *len = n;      // bytes read
    return buf;
}
// checks text file content
static int check_text(const char *what, const char *s, size_t n)
{
    if (n == 0)
    { // if empty
        fprintf(stderr, "error: %s is empty\n", what);
        return -1;
    }
    if (memchr(s, '\0', n))
    { // if contains a null byte
        fprintf(stderr, "error: %s contains a NUL byte\n", what);
        return -1;
    }
    if (!utf8_valid(s, n))
    { // if not utf-8
        fprintf(stderr, "error: %s is not valid UTF-8\n", what);
        return -1;
    }
    return 0;
}

// validates ordinary string fields
static int check_field(const char *what, const char *s, size_t max_bytes)
{
    size_t n = strlen(s);
    if (n > max_bytes)
    { // size check
        fprintf(stderr, "error: %s is longer than %zu bytes\n", what, max_bytes);
        return -1;
    }
    return check_text(what, s, n);
}

// check if slug is supplied, if yes then checks validity of slug
static int require_slug(const opts_t *o)
{
    if (!o->slug)
    { // empty slug
        fprintf(stderr, "error: --slug is required\n");
        return -1;
    }
    if (!slug_valid(o->slug, strlen(o->slug)))
    { // invalid slug
        fprintf(stderr, "error: a slug is 1-%d characters from a-z, 0-9 and '-'\n", SLUG_MAX);
        return -1;
    }
    return 0;
}

// checks validity of provided url
static int valid_url(const char *u)
{
    size_t n = strlen(u);
    if (n == 0 || n > WRITEUP_URL_MAX) // url too long
        return 0;
    if (strncmp(u, "https://", 8) != 0 && strncmp(u, "http://", 7) != 0) // url not http or https
        return 0;
    for (size_t i = 0; i < n; i++) // does not contain whitespaces or nonprinitng characters
        if (!isgraph((unsigned char)u[i]))
            return 0;
    return 1;
}

// this reads flag from file or stdin
static char *read_flag(const opts_t *o)
{
    if ((o->flag_file != NULL) == (o->flag_stdin != 0)) // both flag-file and flag-stdin supplied
    {
        fprintf(stderr, "error: give exactly one of --flag-file or --flag-stdin\n");
        return NULL;
    }
    if (o->flag_stdin && isatty(STDIN_FILENO)) // if flag is being manually typed in, can be read from history
        fputs("Flag (it is visible while you type; prefer a file or a pipe): ", stderr);

    // read flag file
    size_t n = 0;
    char *flag = slurp(o->flag_stdin ? NULL : o->flag_file, CHALLENGE_FLAG_MAX + 2, &n);

    // checks for \n and \r and truncates them
    if (!flag)
        return NULL;
    if (n > 0 && flag[n - 1] == '\n')
        flag[--n] = '\0';
    if (n > 0 && flag[n - 1] == '\r')
        flag[--n] = '\0';

    // sanity check for flag content
    if (n == 0 || n > CHALLENGE_FLAG_MAX || memchr(flag, '\n', n) || memchr(flag, '\0', n))
    {
        fprintf(stderr, "error: the flag must be 1-%d characters on a single line\n", CHALLENGE_FLAG_MAX);
        explicit_bzero(flag, FLAG_BUF); // overwrite the flag buffer before releasing 
        free(flag);
        return NULL;
    }
    return flag;
}

// translate DB results into CLI errors
static int report(int rc, const char *what)
{
    switch (rc)
    {
    case DB_OK:
        return 0;
    case DB_CONFLICT:
        fprintf(stderr, "error: %s: that slug already exists\n", what);
        return 1;
    case DB_NOT_FOUND:
        fprintf(stderr, "error: %s: not found (check the event, challenge or slug)\n", what);
        return 1;
    case DB_INVALID:
        fprintf(stderr, "error: %s: a value is invalid or out of range\n", what);
        return 1;
    default:
        fprintf(stderr, "error: %s: database error (see the log line above)\n", what);
        return 1;
    }
}

// guard against destructive operations like delete with a yes flag check
static int need_yes(const opts_t *o, const char *what)
{
    if (o->yes)
        return 0;
    fprintf(stderr, "error: %s cannot be undone; repeat the command with --yes\n", what);
    return -1;
}

// challenege command definitions
/*
It defines five operations:
challenge add
chellenge list
challenge hide
challenge show
challenge delete
*/
 // chellenge add
static int cmd_challenge_add(const opts_t *o)
{
    if (!o->title || !o->desc_file) // check title and desc
    {
        fprintf(stderr, "error: --slug, --title and --desc-file are required\n");
        return EXIT_USAGE;
    }
    if (require_slug(o) < 0) // validate slug
        return EXIT_USAGE;
    if (check_field("title", o->title, 200) < 0) // checks validity of title and category
        return EXIT_USAGE;
    if (o->category && check_field("category", o->category, 32) < 0)
        return EXIT_USAGE;
    
    // reads description from file
    size_t dl = 0;
    char *desc = slurp(o->desc_file, CHALLENGE_DESC_MAX, &dl);
    if (!desc)
        return 1;
    if (check_text("description", desc, dl) < 0)
    {
        free(desc);
        return 1;
    }

    // read the flag
    char *flag = read_flag(o);
    if (!flag)
    {
        free(desc); // if no flag free the desc
        return 1;
    }

    // define the new challenge
    challenge_new_t in = {
        .event_slug = o->event ? o->event : DEFAULT_EVENT,
        .slug = o->slug,
        .title = o->title,
        .category = o->category ? o->category : "misc",
        .description = desc,
        .flag = flag,
        .points = o->points,
    };
    //handoff to challenge_model
    int rc = challenge_create(&in);
    explicit_bzero(flag, FLAG_BUF); // zero out the flag buffer and free both flag and desc
    free(flag);
    free(desc);

    // report result and return
    int code = report(rc, "challenge add");
    if (code == 0)
        printf("added challenge %s/%s (%d points)\n", in.event_slug, in.slug, in.points);
    return code;
}


static int cmd_challenge_list(const opts_t *o)
{
    challenge_summary_t *rows = malloc(sizeof *rows * CHALLENGE_LIST_MAX); // row size x max row
    if (!rows) return 1; // malloc fail
    
    // call challenge_list
    int n = 0;
    int rc = challenge_list(o->event ? o->event : DEFAULT_EVENT, 1, rows, CHALLENGE_LIST_MAX, &n);
    if (rc == DB_OK)
    { // print rows if reuslts supplied from db
        printf("%-4s %-28s %-10s %6s  %s\n", "pub", "slug", "category", "points", "title");
        for (int i = 0; i < n; i++)
            printf("%-4s %-28s %-10s %6d  %s\n", rows[i].published ? "yes" : "no", rows[i].slug,
                   rows[i].category, rows[i].points, rows[i].title);
        printf("%d challenge(s)%s\n", n, n == CHALLENGE_LIST_MAX ? " (showing the first page only)" : "");
    }
    free(rows); // free and report
    return report(rc, "challenge list");
}

// challenge publish
static int challenge_publish(const opts_t *o, int published)
{
    if (require_slug(o) < 0) // validate slug
        return EXIT_USAGE;
    const char *ev = o->event ? o->event : DEFAULT_EVENT; // select event
    // update according to param
    int code = report(challenge_set_published(ev, o->slug, published), "challenge update");
    // print result
    if (code == 0)
        printf("%s %s/%s\n", published ? "published" : "hid", ev, o->slug);
    return code;
}
// hide = 0 and show = 1
static int cmd_challenge_hide(const opts_t *o){ 
    return challenge_publish(o, 0);  
}

static int cmd_challenge_show(const opts_t *o){ 
    return challenge_publish(o, 1); 
}

// delete a challenge
static int cmd_challenge_delete(const opts_t *o)
{
    // validate the input
    if (require_slug(o) < 0)
        return EXIT_USAGE;
    if (need_yes(o, "deleting a challenge (and its submissions)") < 0)
        return EXIT_USAGE;
    // check event
    const char *ev = o->event ? o->event : DEFAULT_EVENT;
    // call db and report
    int code = report(challenge_delete(ev, o->slug), "challenge delete");
    if (code == 0)
        printf("deleted %s/%s\n", ev, o->slug);
    return code;
}

// command for writeups
/*
It defines 6 operations:
writeup add
writeup put
writeup list
writeup hide
writeup show
writeup delete
*/
// saves writeups for add or put
static int writeup_save(const opts_t *o, int overwrite)
{
    // validate input
    if (!o->title || !o->file)
    {
        fprintf(stderr, "error: --slug, --title and --file are required\n");
        return EXIT_USAGE;
    }
    if (require_slug(o) < 0)
        return EXIT_USAGE;
    if (check_field("title", o->title, WRITEUP_TITLE_MAX) < 0)
        return EXIT_USAGE;
    if (o->author && check_field("author", o->author, WRITEUP_AUTHOR_MAX) < 0)
        return EXIT_USAGE;
    if (o->source_url && !valid_url(o->source_url))
    {
        fprintf(stderr, "error: --source-url must be an http:// or https:// address without spaces\n");
        return EXIT_USAGE;
    }
    // read the writeup body with slurp
    size_t bl = 0;
    char *body = slurp(o->file, WRITEUP_BODY_MAX, &bl);
    if (!body)
        return 1;
    // validat ethe writeup text
    if (check_text("writeup body", body, bl) < 0)
    {
        free(body);
        return 1;
    }

    // define the new writeup
    writeup_new_t in = {
        .slug = o->slug,
        .title = o->title,
        .author = o->author,
        .event_slug = o->event ? o->event : (o->challenge ? DEFAULT_EVENT : NULL),
        .challenge_slug = o->challenge,
        .source_url = o->source_url,
        .body_md = body,
        .body_len = bl,
        .published = 1,
    };
    // check if update or create new
    int rc = overwrite ? writeup_upsert(&in) : writeup_create(&in);
    free(body);

    // report and return
    int code = report(rc, overwrite ? "writeup put" : "writeup add");
    if (code == 0)
        printf("%s writeup %s (%zu bytes)\n", overwrite ? "saved" : "added", o->slug, bl);
    return code;
}

// add new = 1 & put = 1
static int cmd_writeup_add(const opts_t *o){ 
    return writeup_save(o, 0); 
}
static int cmd_writeup_put(const opts_t *o){ 
    return writeup_save(o, 1); 
}

static int cmd_writeup_list(const opts_t *o)
{
    (void)o; // no use, discard
    writeup_summary_t *rows = malloc(sizeof *rows * WRITEUP_LIST_MAX); // row x max row size
    if (!rows) // malloc fail
        return 1;
    int n = 0, total = 0;
    int rc = writeup_list(0, 1, rows, WRITEUP_LIST_MAX, &n, &total); // get list
    if (rc == DB_OK)
    { // print if success
        printf("%-4s %-10s %-28s %-16s %s\n", "pub", "date", "slug", "author", "title");
        for (int i = 0; i < n; i++)
        {
            char date[16]; // convert unixtime to yyyy-mm-dd format
            time_t t = (time_t)rows[i].created_at;
            struct tm tm;
            gmtime_r(&t, &tm);
            strftime(date, sizeof date, "%Y-%m-%d", &tm);
            printf("%-4s %-10s %-28s %-16s %s\n", rows[i].published ? "yes" : "no", date,
                   rows[i].slug, rows[i].author, rows[i].title);
        }
        printf("%d of %d writeup(s)\n", n, total);
    }
    free(rows); // clean and retrun
    return report(rc, "writeup list");
}

static int writeup_publish(const opts_t *o, int published)
{
    // check slug
    if (require_slug(o) < 0) return EXIT_USAGE;
        
    //update publish status and report
    int code = report(writeup_set_published(o->slug, published), "writeup update");

    if (code == 0) // success and return
        printf("%s %s\n", published ? "published" : "hid", o->slug);

    return code;
}

// hide = 0, show = 1
static int cmd_writeup_hide(const opts_t *o){ 
    return writeup_publish(o, 0); 
}
static int cmd_writeup_show(const opts_t *o){ 
    return writeup_publish(o, 1); 
}

static int cmd_writeup_delete(const opts_t *o)
{
    // check slug
    if (require_slug(o) < 0) return EXIT_USAGE;
    
    // check if yes provided
    if (need_yes(o, "deleting a writeup") < 0) return EXIT_USAGE;
    
    // delete and report
    int code = report(writeup_delete(o->slug), "writeup delete");

    // success and return
    if (code == 0)
        printf("deleted %s\n", o->slug);
    return code;
}

// the commands

typedef struct
{
    const char *group, *name; // command identity
    int (*fn)(const opts_t *); // the function pointer for operations
} command_t;

// defined commands
static const command_t COMMANDS[] = {
    {"challenge", "add", cmd_challenge_add},
    {"challenge", "list", cmd_challenge_list},
    {"challenge", "hide", cmd_challenge_hide},
    {"challenge", "show", cmd_challenge_show},
    {"challenge", "delete", cmd_challenge_delete},
    {"writeup", "add", cmd_writeup_add},
    {"writeup", "put", cmd_writeup_put},
    {"writeup", "list", cmd_writeup_list},
    {"writeup", "hide", cmd_writeup_hide},
    {"writeup", "show", cmd_writeup_show},
    {"writeup", "delete", cmd_writeup_delete},
};

// main function
int main(int argc, char **argv)
{
    // recognize the config path to know which db to connect to (if user provides a different db)
    // example: ./tourneyctl -c config/server.conf
    // here, ./tourneyctl is arg[0], -c is arg[1] and config/server.conf is arg[2]
    int i = 1;
    const char *config_path = NULL;
    if (i + 1 < argc && strcmp(argv[i], "-c") == 0)
    {
        config_path = argv[i + 1];
        i += 2;
    }

    // user provided no args, print usage
    if (i >= argc)
    {
        usage(stderr);
        return EXIT_USAGE;
    }
    // user for manual
    if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help"))
    {
        usage(stdout);
        return 0;
    }

    // migrate command
    int is_migrate = strcmp(argv[i], "migrate") == 0;
    const command_t *cmd = NULL;
    opts_t o;
    if (is_migrate)
    {
        if (i + 1 != argc) // migrate command cannot have args
        {
            usage(stderr);
            return EXIT_USAGE;
        }
    }
    else
    {
        if (i + 1 >= argc)
        {
            usage(stderr); // command had no args
            return EXIT_USAGE;
        }
        // iterate through commands and find the appropiate command
        for (size_t k = 0; k < sizeof COMMANDS / sizeof COMMANDS[0]; k++)
            if (!strcmp(COMMANDS[k].group, argv[i]) && !strcmp(COMMANDS[k].name, argv[i + 1]))
                cmd = &COMMANDS[k];
        if (!cmd)
        {
            fprintf(stderr, "error: unknown command '%s %s'\n\n", argv[i], argv[i + 1]);
            usage(stderr);
            return EXIT_USAGE;
        }
        // parse options. argc - (i + 1) shortens the total arg size
        // argv + (i + 1) shifts the pointer to the first options liek --title
        // the optind is set to 1
        if (parse_opts(argc - (i + 1), argv + (i + 1), &o) < 0)
            return EXIT_USAGE;
    }

    // load config with defaults
    config_t cfg;
    config_defaults(&cfg);
    // load the provided file
    if (config_path)
    {
        if (config_load_file(&cfg, config_path) < 0)
            return 1;
    }

    else if (access(DEFAULT_CONFIG, R_OK) == 0) // check if default config file is accessible
    {
        if (config_load_file(&cfg, DEFAULT_CONFIG) < 0)
            return 1;
    }
    // validate the config
    if (config_validate(&cfg) < 0)
        return 1;
    

    /* If new db path and migration is provided, this opens the db connection and runs
     * the migrations provided in the config files*/
    if (db_init(cfg.db_path, cfg.migrations_dir, cfg.db_busy_timeout_ms) != DB_OK)
    {
        fprintf(stderr, "error: cannot open or migrate the database\n");
        return 1;
    }
    // if is migrate then show this message
    if (is_migrate)
    {
        puts("database is up to date");
        return 0;
    }
    // execute the given command otherwise and return from there
    return cmd->fn(&o);
}