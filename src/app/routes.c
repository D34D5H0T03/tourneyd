#include "app/routes.h"
#include "http/router.h"
#include "app/hello.h"

void app_register_routes(void) {
    router_add(METHOD_GET, "/", hello_handler);
    router_add(METHOD_POST, "/upper", upper_handler);
}