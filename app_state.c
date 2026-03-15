#include "app_state.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

void nvt_app_init(AppState *app)
{
    memset(app, 0, sizeof(*app));
    app->ui.screen = SCR_LINES;
    app->ui.network = NET_BDX;
    app->ui.vehicle_zoom = 0;
    app->bdx.sel_line = -1;
    app->tls.sel_line = -1;
    app->tls.sel_stop = -1;
    app->idf.sel_line = -1;
    app->idf.sel_stop = -1;
    app->sncf.sel_line = -1;
    app->sncf.sel_stop = -1;
    app->map.line_route_gid = -1;
    app->map.vehicle_detail_map_zoom = -1;
    app->map.vehicle_detail_map_line_gid = -1;
}

void nvt_app_reset_line_route(AppState *app)
{
    memset(&app->map.line_route, 0, sizeof(app->map.line_route));
    app->map.has_line_route = 0;
    app->map.line_route_gid = -1;
}

void nvt_app_reset_vehicle_detail_map(AppState *app)
{
    memset(&app->map.vehicle_detail_map, 0, sizeof(app->map.vehicle_detail_map));
    app->map.has_vehicle_detail_map = 0;
    app->map.vehicle_detail_map_valid = 0;
    app->map.vehicle_detail_map_zoom = -1;
    app->map.vehicle_detail_map_line_gid = -1;
}

void nvt_app_reset_toulouse_line_route(AppState *app)
{
    memset(&app->map.tls_line_route, 0, sizeof(app->map.tls_line_route));
    app->map.has_tls_line_route = 0;
    app->map.tls_line_route_ref[0] = '\0';
}

void nvt_app_reset_atlas_map(AppState *app)
{
    memset(&app->map.atlas_map, 0, sizeof(app->map.atlas_map));
    app->map.has_atlas_map = 0;
    app->map.atlas_map_attempted = 0;
}

void nvt_app_reset_atlas_routes(AppState *app)
{
    memset(&app->map.atlas_routes, 0, sizeof(app->map.atlas_routes));
    app->map.has_atlas_routes = 0;
    app->map.atlas_routes_attempted = 0;
}

void nvt_app_reset_metro_map_cache(AppState *app)
{
    memset(&app->map.metro_map, 0, sizeof(app->map.metro_map));
    app->map.has_metro_map = 0;
    app->map.map_attempted = 0;
}

void nvt_app_reset_toulouse_metro_map_cache(AppState *app)
{
    memset(&app->map.tls_metro_map, 0, sizeof(app->map.tls_metro_map));
    app->map.has_tls_metro_map = 0;
    app->map.tls_map_attempted = 0;
}

void nvt_app_toast(AppState *app, const char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    vsnprintf(app->ui.toast, sizeof(app->ui.toast), fmt, args);
    va_end(args);
    app->ui.toast_time = time(NULL);
}

void nvt_app_toast_error(AppState *app, time_t now, const char *fmt, ...)
{
    va_list args;

    if (now - app->ui.last_error_time < 10) return;

    va_start(args, fmt);
    vsnprintf(app->ui.toast, sizeof(app->ui.toast), fmt, args);
    va_end(args);
    app->ui.toast_time = now;
    app->ui.last_error_time = now;
}
