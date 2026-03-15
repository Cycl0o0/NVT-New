#ifndef APP_STATE_H
#define APP_STATE_H

#include <time.h>

#include "api.h"

typedef enum {
    SCR_LINES,
    SCR_VEHICLES,
    SCR_ALERTS,
    SCR_STOP_SEARCH,
    SCR_PASSAGES,
    SCR_ATLAS,
} NvtScreen;

typedef enum {
    NET_BDX,
    NET_TLS,
    NET_IDFM,
    NET_SNCF,
    NET_COUNT,
} NvtNetwork;

typedef struct {
    Line lines[MAX_LINES];
    int nlines;
    Vehicle vehicles[MAX_VEHICLES];
    int nvehicles;
    Alert alerts[MAX_ALERTS];
    int nalerts;
    StopMap stops;
    StopGroup stop_groups[MAX_STOP_GROUPS];
    int nstop_groups;
    Passage passages[MAX_PASSAGES];
    int npassages;
    int sel_stop_group;
    int filtered[MAX_LINES];
    int nfiltered;
    char search[64];
    int stop_filtered[MAX_STOP_GROUPS];
    int nstop_filtered;
    char stop_search[64];
    int cursor;
    int scroll;
    int sel_line;
} NvtBordeauxState;

typedef struct {
    ToulouseSnapshot snapshot;
    ToulouseLine lines[MAX_LINES];
    int nlines;
    ToulouseStop stops[MAX_STOPS];
    int nstops;
    ToulouseAlert alerts[MAX_ALERTS];
    int nalerts;
    ToulousePassage passages[MAX_PASSAGES];
    int npassages;
    ToulouseVehicle vehicles[MAX_VEHICLES];
    int nvehicles;
    int filtered[MAX_LINES];
    int nfiltered;
    char search[64];
    int stop_filtered[MAX_STOPS];
    int nstop_filtered;
    char stop_search[64];
    int cursor;
    int scroll;
    int sel_line;
    int stop_cursor;
    int stop_scroll;
    int sel_stop;
} NvtToulouseState;

typedef struct {
    IdfmSnapshot snapshot;
    ToulouseLine lines[MAX_LINES];
    int nlines;
    ToulouseStop stops[MAX_STOPS];
    int nstops;
    ToulouseAlert alerts[MAX_ALERTS];
    int nalerts;
    ToulousePassage passages[MAX_PASSAGES];
    int npassages;
    ToulouseVehicle vehicles[MAX_VEHICLES];
    int nvehicles;
    int filtered[MAX_LINES];
    int nfiltered;
    char search[64];
    int stop_filtered[MAX_STOPS];
    int nstop_filtered;
    char stop_search[64];
    int cursor;
    int scroll;
    int sel_line;
    int stop_cursor;
    int stop_scroll;
    int sel_stop;
} NvtIdfmState;

typedef NvtIdfmState NvtSncfState;

typedef struct {
    CourseCache course_cache;
    MetroMap metro_map;
    int has_metro_map;
    int map_attempted;
    MetroMap tls_metro_map;
    int has_tls_metro_map;
    int tls_map_attempted;
    LineRouteMap line_route;
    int has_line_route;
    int line_route_gid;
    LineRouteMap tls_line_route;
    int has_tls_line_route;
    char tls_line_route_ref[96];
    AtlasMap vehicle_detail_map;
    int has_vehicle_detail_map;
    int vehicle_detail_map_valid;
    int vehicle_detail_map_zoom;
    int vehicle_detail_map_line_gid;
    AtlasMap atlas_map;
    int has_atlas_map;
    int atlas_map_attempted;
    AtlasRoutes atlas_routes;
    int has_atlas_routes;
    int atlas_routes_attempted;
} NvtMapState;

typedef struct {
    int atlas_filtered[MAX_LINES];
    int natlas_filtered;
    char atlas_search[64];
    int atlas_cursor;
    int atlas_scroll;
    int atlas_focus_gid;
    NvtScreen screen;
    NvtNetwork network;
    int theme;
    int use_256;
    int vehicle_zoom;
    int show_help;
    int alert_scroll;
    int alert_total_h;
    int network_loaded[NET_COUNT];
    char toast[128];
    time_t toast_time;
    time_t last_error_time;
} NvtUiState;

typedef struct {
    NvtBordeauxState bdx;
    NvtToulouseState tls;
    NvtIdfmState idf;
    NvtSncfState sncf;
    NvtMapState map;
    NvtUiState ui;
} AppState;

void nvt_app_init(AppState *app);
void nvt_app_reset_line_route(AppState *app);
void nvt_app_reset_vehicle_detail_map(AppState *app);
void nvt_app_reset_toulouse_line_route(AppState *app);
void nvt_app_reset_atlas_map(AppState *app);
void nvt_app_reset_atlas_routes(AppState *app);
void nvt_app_reset_metro_map_cache(AppState *app);
void nvt_app_reset_toulouse_metro_map_cache(AppState *app);
void nvt_app_toast(AppState *app, const char *fmt, ...);
void nvt_app_toast_error(AppState *app, time_t now, const char *fmt, ...);

#endif
