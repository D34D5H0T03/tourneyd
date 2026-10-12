#include "core/config.h"
#include "core/server.h"
#include <stdlib.h>

int main(int argc, char **argv) {
    config_t cfg;
    
    config_defaults(&cfg);
    
    if (config_parse_args(&cfg, argc, argv) < 0) {
        return EXIT_FAILURE;
    }
    
    if (server_init(&cfg) < 0) {
        return EXIT_FAILURE;
    }
    
    int rc = server_run();
    
    server_shutdown();
    
    return rc < 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}