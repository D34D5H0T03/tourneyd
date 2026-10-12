#ifndef CONFIG_H
#define CONFIG_H

typedef struct {
    const char *port;
    int backlog;
    const char *doc_root;
} config_t;

void config_defaults(config_t *cfg);
int config_parse_args(config_t *cfg, int argc, char **argv);

#endif // CONFIG_H