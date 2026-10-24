#include "app/routes.h"
#include "app/challenges_api.h"
#include "app/writeups_api.h"
#include "app/static_files.h"
#include "http/router.h"

void app_register_routes(void) {
    // Challenges API
    // Exact match for the list
    router_add       (METHOD_GET,  "/api/challenges",  challenge_list_handler,   ROUTE_BLOCKING);
    // Prefix match for details (e.g., /api/challenges/layers)
    router_add_prefix(METHOD_GET,  "/api/challenges/", challenge_get_handler,    ROUTE_BLOCKING);
    // Prefix match for submissions (e.g., /api/challenges/layers/submit)
    router_add_prefix(METHOD_POST, "/api/challenges/", challenge_submit_handler, ROUTE_BLOCKING);
    
    // Writeups API
    // Exact match for the paginated list
    router_add       (METHOD_GET,  "/api/writeups",    writeup_list_handler,     ROUTE_BLOCKING);
    // Prefix match for details (e.g., /api/writeups/layers-walkthrough)
    router_add_prefix(METHOD_GET,  "/api/writeups/",   writeup_get_handler,      ROUTE_BLOCKING);
    
    // Static Files & 404 Fallback
    // Any unmatched route (like /, /css/style.css, or unknown /api/ paths) falls through to the static handler.
    router_set_fallback(static_files_handler);
}