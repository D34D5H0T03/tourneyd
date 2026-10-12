#include "core/config.h"
#include <stddef.h>

void config_defaults(config_t *cfg) {
    cfg->port = "8080";
    cfg->backlog = 128;
    cfg->doc_root = "./public";
}

int config_parse_args(config_t *cfg, int argc, char **argv) {
    (void)cfg; (void)argc; (void)argv;
    // parse flags like -p 8080 or -d /var/www here later
    return 0; 
}