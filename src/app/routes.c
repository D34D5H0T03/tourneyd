#include "app/routes.h"
#include "http/router.h"
#include "app/hello.h"
#include "app/static_files.h"

void app_register_routes(void) {
    router_add(METHOD_GET, "/", hello_handler);
    router_add(METHOD_POST, "/upper", upper_handler);

    router_set_fallback(static_files_handler);
}