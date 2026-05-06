#include "data.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "filter.h"

typedef int (*NvtRetryFn)(void *ctx);

static int nvt_retry_operation(const char *label, int attempts, NvtRetryFn fn, void *ctx,
                               char *err, size_t err_sz)
{
    if (attempts < 1) attempts = 1;

    for (int attempt = 0; attempt < attempts; attempt++) {
        int rc = fn(ctx);

        if (rc >= 0) return rc;
    }

    snprintf(err, err_sz, "Unable to load %s after %d attempt%s",
             label, attempts, attempts > 1 ? "s" : "");
    return -1;
}

static int fetch_bordeaux_init_once(void *ctx)
{
    AppState *app = ctx;

    stopmap_free(&app->bdx.stops);
    course_cache_free(&app->map.course_cache);
    stopmap_init(&app->bdx.stops);
    course_cache_init(&app->map.course_cache);
    fetch_stops(&app->bdx.stops);

    app->bdx.nstop_groups = fetch_stop_groups(app->bdx.stop_groups, MAX_STOP_GROUPS);
    if (app->bdx.nstop_groups < 0) return -1;
    app->bdx.nstop_filtered = nvt_rebuild_stop_filter(
        app->bdx.stop_groups,
        app->bdx.nstop_groups,
        app->bdx.stop_search,
        app->bdx.stop_filtered,
        MAX_STOP_GROUPS
    );
    return app->bdx.nstop_groups;
}

static int fetch_bordeaux_overview_once(void *ctx)
{
    AppState *app = ctx;

    app->bdx.nlines = fetch_lines(app->bdx.lines, MAX_LINES);
    if (app->bdx.nlines < 0) return -1;
    qsort(app->bdx.lines, app->bdx.nlines, sizeof(Line), nvt_cmp_lines);
    app->bdx.nfiltered = nvt_rebuild_line_filter(
        app->bdx.lines,
        app->bdx.nlines,
        app->bdx.search,
        app->bdx.filtered,
        MAX_LINES
    );
    app->ui.natlas_filtered = nvt_rebuild_atlas_filter(
        app->bdx.lines,
        app->bdx.nlines,
        app->ui.atlas_search,
        app->ui.atlas_filtered,
        MAX_LINES
    );
    return app->bdx.nlines;
}

static int fetch_bordeaux_alerts_once(void *ctx)
{
    AppState *app = ctx;

    app->bdx.nalerts = fetch_alerts(app->bdx.alerts, MAX_ALERTS);
    if (app->bdx.nalerts < 0) return -1;
    return app->bdx.nalerts;
}

static int fetch_bordeaux_vehicles_once(void *ctx)
{
    AppState *app = ctx;
    int line_gid;

    if (app->bdx.sel_line < 0 || app->bdx.sel_line >= app->bdx.nlines) return 0;

    line_gid = app->bdx.lines[app->bdx.sel_line].gid;
    app->bdx.nvehicles = fetch_vehicles(line_gid, app->bdx.vehicles, MAX_VEHICLES);
    if (app->bdx.nvehicles < 0) return -1;
    return app->bdx.nvehicles;
}

static int fetch_bordeaux_passages_once(void *ctx)
{
    AppState *app = ctx;
    StopGroup *group;

    app->bdx.npassages = 0;
    if (app->bdx.sel_stop_group < 0 || app->bdx.sel_stop_group >= app->bdx.nstop_groups) return 0;

    group = &app->bdx.stop_groups[app->bdx.sel_stop_group];
    for (int i = 0; i < group->ngids; i++) {
        int count = fetch_passages(
            group->gids[i],
            app->bdx.passages + app->bdx.npassages,
            MAX_PASSAGES - app->bdx.npassages,
            &app->map.course_cache
        );
        if (count > 0) app->bdx.npassages += count;
    }
    return app->bdx.npassages;
}

static int fetch_toulouse_overview_once(void *ctx)
{
    AppState *app = ctx;

    if (fetch_toulouse_snapshot(
            &app->tls.snapshot,
            app->tls.lines,
            MAX_LINES,
            app->tls.stops,
            MAX_STOPS
        ) < 0) {
        return -1;
    }

    app->tls.nlines = app->tls.snapshot.sample_lines;
    app->tls.nstops = app->tls.snapshot.sample_stops;
    /* Wire stops cache for vehicle synthesis. */
    nvt_set_toulouse_vehicle_stops(app->tls.stops, app->tls.nstops);
    app->tls.nfiltered = nvt_rebuild_toulouse_line_filter(
        app->tls.lines,
        app->tls.nlines,
        app->tls.search,
        app->tls.filtered,
        MAX_LINES
    );
    app->tls.nstop_filtered = nvt_rebuild_toulouse_stop_filter(
        app->tls.stops,
        app->tls.nstops,
        app->tls.stop_search,
        app->tls.stop_filtered,
        MAX_STOPS
    );
    return app->tls.nlines;
}

static int fetch_toulouse_alerts_once(void *ctx)
{
    AppState *app = ctx;

    app->tls.nalerts = fetch_toulouse_alerts(app->tls.alerts, MAX_ALERTS);
    if (app->tls.nalerts < 0) return -1;
    return app->tls.nalerts;
}

static int fetch_toulouse_passages_once(void *ctx)
{
    AppState *app = ctx;
    ToulouseStop *stop;

    app->tls.npassages = 0;
    if (app->tls.sel_stop < 0 || app->tls.sel_stop >= app->tls.nstops) return 0;

    stop = &app->tls.stops[app->tls.sel_stop];
    app->tls.npassages = fetch_toulouse_passages(stop->ref, app->tls.passages, MAX_PASSAGES);
    if (app->tls.npassages < 0) return -1;
    return app->tls.npassages;
}

static int cmp_toulouse_vehicles_by_waiting_time(const void *a, const void *b)
{
    const ToulouseVehicle *left = a;
    const ToulouseVehicle *right = b;
    int left_dir = strcmp(left->sens, "ALLER") == 0 ? 0 : 1;
    int right_dir = strcmp(right->sens, "ALLER") == 0 ? 0 : 1;
    int left_wait = nvt_toulouse_waiting_minutes(left->waiting_time);
    int right_wait = nvt_toulouse_waiting_minutes(right->waiting_time);

    if (left_dir != right_dir) return left_dir - right_dir;
    if (left_wait < 0) left_wait = 1 << 20;
    if (right_wait < 0) right_wait = 1 << 20;
    return left_wait - right_wait;
}

static int fetch_toulouse_vehicles_once(void *ctx)
{
    AppState *app = ctx;
    ToulouseLine *line;

    app->tls.nvehicles = 0;
    if (app->tls.sel_line < 0 || app->tls.sel_line >= app->tls.nlines) return 0;

    line = &app->tls.lines[app->tls.sel_line];
    app->tls.nvehicles = fetch_toulouse_vehicles(line, app->tls.vehicles, MAX_VEHICLES);
    if (app->tls.nvehicles < 0) return -1;
    if (app->tls.nvehicles > 1) {
        qsort(
            app->tls.vehicles,
            app->tls.nvehicles,
            sizeof(ToulouseVehicle),
            cmp_toulouse_vehicles_by_waiting_time
        );
    }
    return app->tls.nvehicles;
}

static int cmp_live_stops_by_name(const void *a, const void *b)
{
    const ToulouseStop *left = a;
    const ToulouseStop *right = b;

    return strcmp(left->libelle, right->libelle);
}

static int cmp_live_passages_by_waiting_time(const void *a, const void *b)
{
    const ToulousePassage *left = a;
    const ToulousePassage *right = b;
    int left_wait = nvt_toulouse_waiting_minutes(left->waiting_time);
    int right_wait = nvt_toulouse_waiting_minutes(right->waiting_time);

    if (left_wait < 0) left_wait = 1 << 20;
    if (right_wait < 0) right_wait = 1 << 20;
    return left_wait - right_wait;
}

static int cmp_live_vehicles_by_terminus(const void *a, const void *b)
{
    const ToulouseVehicle *left = a;
    const ToulouseVehicle *right = b;
    int diff = strcmp(left->terminus, right->terminus);

    if (diff != 0) return diff;
    return strcmp(left->current_stop, right->current_stop);
}

static int cmp_live_vehicles_by_waiting_time(const void *a, const void *b)
{
    const ToulouseVehicle *left = a;
    const ToulouseVehicle *right = b;
    int left_wait = nvt_toulouse_waiting_minutes(left->waiting_time);
    int right_wait = nvt_toulouse_waiting_minutes(right->waiting_time);

    if (left_wait < 0) left_wait = 1 << 20;
    if (right_wait < 0) right_wait = 1 << 20;
    if (left_wait != right_wait) return left_wait - right_wait;
    return strcmp(left->terminus, right->terminus);
}

static int fetch_idfm_overview_once(void *ctx)
{
    AppState *app = ctx;

    if (fetch_idfm_snapshot(&app->idf.snapshot, app->idf.lines, MAX_LINES) < 0) return -1;

    app->idf.nlines = app->idf.snapshot.sample_lines;
    if (app->idf.nlines > 1) {
        qsort(app->idf.lines, app->idf.nlines, sizeof(ToulouseLine), nvt_cmp_idfm_lines);
    }
    app->idf.nfiltered = nvt_rebuild_toulouse_line_filter(
        app->idf.lines,
        app->idf.nlines,
        app->idf.search,
        app->idf.filtered,
        MAX_LINES
    );
    app->idf.nstops = 0;
    app->idf.nstop_filtered = 0;
    app->idf.npassages = 0;
    app->idf.nvehicles = 0;
    app->idf.sel_stop = -1;
    return app->idf.nlines;
}

static int fetch_idfm_stops_once(void *ctx)
{
    AppState *app = ctx;
    ToulouseLine *line;

    app->idf.nstop_filtered = 0;
    app->idf.npassages = 0;
    app->idf.sel_stop = -1;
    if (app->idf.sel_line < 0 || app->idf.sel_line >= app->idf.nlines) return 0;

    line = &app->idf.lines[app->idf.sel_line];
    app->idf.nstops = fetch_idfm_line_stops(line, app->idf.stops, MAX_STOPS);
    if (app->idf.nstops < 0) return -1;
    if (app->idf.nstops > 1) {
        qsort(app->idf.stops, app->idf.nstops, sizeof(ToulouseStop), cmp_live_stops_by_name);
    }
    if (app->idf.stop_search[0]) {
        app->idf.nstop_filtered = nvt_rebuild_toulouse_stop_filter(
            app->idf.stops,
            app->idf.nstops,
            app->idf.stop_search,
            app->idf.stop_filtered,
            MAX_STOPS
        );
    }
    return app->idf.nstops;
}

static int fetch_idfm_alerts_once(void *ctx)
{
    AppState *app = ctx;

    app->idf.nalerts = fetch_idfm_alerts(app->idf.alerts, MAX_ALERTS);
    if (app->idf.nalerts < 0) return -1;
    app->idf.snapshot.sample_alerts = app->idf.nalerts;
    return app->idf.nalerts;
}

static int fetch_idfm_passages_once(void *ctx)
{
    AppState *app = ctx;
    ToulouseLine *line;
    ToulouseStop *stop;

    app->idf.npassages = 0;
    if (app->idf.sel_line < 0 || app->idf.sel_line >= app->idf.nlines) return 0;
    if (app->idf.sel_stop < 0 || app->idf.sel_stop >= app->idf.nstops) return 0;

    line = &app->idf.lines[app->idf.sel_line];
    stop = &app->idf.stops[app->idf.sel_stop];
    app->idf.npassages = fetch_idfm_passages(line, stop, app->idf.passages, MAX_PASSAGES);
    if (app->idf.npassages < 0) return -1;
    if (app->idf.npassages > 1) {
        qsort(
            app->idf.passages,
            app->idf.npassages,
            sizeof(ToulousePassage),
            cmp_live_passages_by_waiting_time
        );
    }
    return app->idf.npassages;
}

static int fetch_idfm_vehicles_once(void *ctx)
{
    AppState *app = ctx;
    ToulouseLine *line;

    app->idf.nvehicles = 0;
    if (app->idf.sel_line < 0 || app->idf.sel_line >= app->idf.nlines) return 0;

    line = &app->idf.lines[app->idf.sel_line];
    /* Wire stops cache so SIRI ETT synthesis can compute positions. */
    if (app->idf.nstops > 0) {
        nvt_set_idfm_vehicle_stops(app->idf.stops, app->idf.nstops);
    }
    app->idf.nvehicles = fetch_idfm_vehicles(line, app->idf.vehicles, MAX_VEHICLES);
    nvt_set_idfm_vehicle_stops(NULL, 0);
    if (app->idf.nvehicles < 0) return -1;
    if (app->idf.nvehicles > 1) {
        qsort(
            app->idf.vehicles,
            app->idf.nvehicles,
            sizeof(ToulouseVehicle),
            cmp_live_vehicles_by_terminus
        );
    }
    return app->idf.nvehicles;
}

static int fetch_sncf_overview_once(void *ctx)
{
    AppState *app = ctx;

    if (fetch_sncf_snapshot(&app->sncf.snapshot, app->sncf.lines, MAX_LINES) < 0) return -1;

    app->sncf.nlines = app->sncf.snapshot.sample_lines;
    if (app->sncf.nlines > 1) {
        qsort(app->sncf.lines, app->sncf.nlines, sizeof(ToulouseLine), nvt_cmp_idfm_lines);
    }
    app->sncf.nfiltered = nvt_rebuild_toulouse_line_filter(
        app->sncf.lines,
        app->sncf.nlines,
        app->sncf.search,
        app->sncf.filtered,
        MAX_LINES
    );
    app->sncf.nstops = 0;
    app->sncf.nstop_filtered = 0;
    app->sncf.npassages = 0;
    app->sncf.nvehicles = 0;
    app->sncf.sel_stop = -1;
    return app->sncf.nlines;
}

static int fetch_sncf_stops_once(void *ctx)
{
    AppState *app = ctx;
    ToulouseLine *line;

    app->sncf.nstop_filtered = 0;
    app->sncf.npassages = 0;
    app->sncf.sel_stop = -1;
    if (app->sncf.sel_line < 0 || app->sncf.sel_line >= app->sncf.nlines) return 0;

    line = &app->sncf.lines[app->sncf.sel_line];
    app->sncf.nstops = fetch_sncf_line_stops(line, app->sncf.stops, MAX_STOPS);
    if (app->sncf.nstops < 0) return -1;
    if (app->sncf.nstops > 1) {
        qsort(app->sncf.stops, app->sncf.nstops, sizeof(ToulouseStop), cmp_live_stops_by_name);
    }
    if (app->sncf.stop_search[0]) {
        app->sncf.nstop_filtered = nvt_rebuild_toulouse_stop_filter(
            app->sncf.stops,
            app->sncf.nstops,
            app->sncf.stop_search,
            app->sncf.stop_filtered,
            MAX_STOPS
        );
    }
    return app->sncf.nstops;
}

static int fetch_sncf_alerts_once(void *ctx)
{
    AppState *app = ctx;

    app->sncf.nalerts = fetch_sncf_alerts(app->sncf.alerts, MAX_ALERTS);
    if (app->sncf.nalerts < 0) return -1;
    app->sncf.snapshot.sample_alerts = app->sncf.nalerts;
    return app->sncf.nalerts;
}

static int fetch_sncf_passages_once(void *ctx)
{
    AppState *app = ctx;
    ToulouseLine *line;
    ToulouseStop *stop;

    app->sncf.npassages = 0;
    if (app->sncf.sel_line < 0 || app->sncf.sel_line >= app->sncf.nlines) return 0;
    if (app->sncf.sel_stop < 0 || app->sncf.sel_stop >= app->sncf.nstops) return 0;

    line = &app->sncf.lines[app->sncf.sel_line];
    stop = &app->sncf.stops[app->sncf.sel_stop];
    app->sncf.npassages = fetch_sncf_passages(line, stop, app->sncf.passages, MAX_PASSAGES);
    if (app->sncf.npassages < 0) return -1;
    if (app->sncf.npassages > 1) {
        qsort(
            app->sncf.passages,
            app->sncf.npassages,
            sizeof(ToulousePassage),
            cmp_live_passages_by_waiting_time
        );
    }
    return app->sncf.npassages;
}

static int fetch_sncf_vehicles_once(void *ctx)
{
    AppState *app = ctx;
    ToulouseLine *line;

    app->sncf.nvehicles = 0;
    if (app->sncf.sel_line < 0 || app->sncf.sel_line >= app->sncf.nlines) return 0;

    line = &app->sncf.lines[app->sncf.sel_line];
    app->sncf.nvehicles = fetch_sncf_vehicles(line, app->sncf.vehicles, MAX_VEHICLES);
    if (app->sncf.nvehicles < 0) return -1;
    if (app->sncf.nvehicles > 1) {
        qsort(
            app->sncf.vehicles,
            app->sncf.nvehicles,
            sizeof(ToulouseVehicle),
            cmp_live_vehicles_by_waiting_time
        );
    }
    return app->sncf.nvehicles;
}

int nvt_data_init_bordeaux(AppState *app, int attempts, char *err, size_t err_sz)
{
    return nvt_retry_operation("Bordeaux stops", attempts, fetch_bordeaux_init_once, app, err, err_sz);
}

int nvt_data_refresh_bordeaux_overview(AppState *app, int attempts, char *err, size_t err_sz)
{
    return nvt_retry_operation("Bordeaux lines", attempts, fetch_bordeaux_overview_once, app, err, err_sz);
}

int nvt_data_refresh_bordeaux_alerts(AppState *app, int attempts, char *err, size_t err_sz)
{
    return nvt_retry_operation("Bordeaux alerts", attempts, fetch_bordeaux_alerts_once, app, err, err_sz);
}

int nvt_data_refresh_bordeaux_vehicles(AppState *app, int attempts, char *err, size_t err_sz)
{
    return nvt_retry_operation("Bordeaux vehicles", attempts, fetch_bordeaux_vehicles_once, app, err, err_sz);
}

int nvt_data_load_bordeaux_passages(AppState *app, int attempts, char *err, size_t err_sz)
{
    return nvt_retry_operation("Bordeaux passages", attempts, fetch_bordeaux_passages_once, app, err, err_sz);
}

int nvt_data_refresh_toulouse_overview(AppState *app, int attempts, char *err, size_t err_sz)
{
    return nvt_retry_operation("Toulouse snapshot", attempts, fetch_toulouse_overview_once, app, err, err_sz);
}

int nvt_data_refresh_toulouse_alerts(AppState *app, int attempts, char *err, size_t err_sz)
{
    return nvt_retry_operation("Toulouse alerts", attempts, fetch_toulouse_alerts_once, app, err, err_sz);
}

int nvt_data_load_toulouse_passages(AppState *app, int attempts, char *err, size_t err_sz)
{
    return nvt_retry_operation("Toulouse passages", attempts, fetch_toulouse_passages_once, app, err, err_sz);
}

int nvt_data_load_toulouse_vehicles(AppState *app, int attempts, char *err, size_t err_sz)
{
    return nvt_retry_operation("Toulouse vehicles", attempts, fetch_toulouse_vehicles_once, app, err, err_sz);
}

int nvt_data_refresh_idfm_overview(AppState *app, int attempts, char *err, size_t err_sz)
{
    return nvt_retry_operation("IDFM lines", attempts, fetch_idfm_overview_once, app, err, err_sz);
}

int nvt_data_load_idfm_stops(AppState *app, int attempts, char *err, size_t err_sz)
{
    return nvt_retry_operation("IDFM line stops", attempts, fetch_idfm_stops_once, app, err, err_sz);
}

int nvt_data_refresh_idfm_alerts(AppState *app, int attempts, char *err, size_t err_sz)
{
    return nvt_retry_operation("IDFM alerts", attempts, fetch_idfm_alerts_once, app, err, err_sz);
}

int nvt_data_load_idfm_passages(AppState *app, int attempts, char *err, size_t err_sz)
{
    return nvt_retry_operation("IDFM departures", attempts, fetch_idfm_passages_once, app, err, err_sz);
}

int nvt_data_load_idfm_vehicles(AppState *app, int attempts, char *err, size_t err_sz)
{
    return nvt_retry_operation("IDFM vehicles", attempts, fetch_idfm_vehicles_once, app, err, err_sz);
}

int nvt_data_refresh_sncf_overview(AppState *app, int attempts, char *err, size_t err_sz)
{
    return nvt_retry_operation("SNCF lines", attempts, fetch_sncf_overview_once, app, err, err_sz);
}

int nvt_data_load_sncf_stops(AppState *app, int attempts, char *err, size_t err_sz)
{
    return nvt_retry_operation("SNCF line stops", attempts, fetch_sncf_stops_once, app, err, err_sz);
}

int nvt_data_refresh_sncf_alerts(AppState *app, int attempts, char *err, size_t err_sz)
{
    return nvt_retry_operation("SNCF alerts", attempts, fetch_sncf_alerts_once, app, err, err_sz);
}

int nvt_data_load_sncf_passages(AppState *app, int attempts, char *err, size_t err_sz)
{
    return nvt_retry_operation("SNCF departures", attempts, fetch_sncf_passages_once, app, err, err_sz);
}

int nvt_data_load_sncf_vehicles(AppState *app, int attempts, char *err, size_t err_sz)
{
    return nvt_retry_operation("SNCF journeys", attempts, fetch_sncf_vehicles_once, app, err, err_sz);
}

/* ── STAR (Rennes Métropole) ───────────────────────────────────── */

static int fetch_star_overview_once(void *ctx)
{
    AppState *app = ctx;

    if (fetch_star_snapshot(&app->star.snapshot, app->star.lines, MAX_LINES) < 0) return -1;

    app->star.nlines = app->star.snapshot.sample_lines;
    if (app->star.nlines > 1) {
        qsort(app->star.lines, app->star.nlines, sizeof(ToulouseLine), nvt_cmp_idfm_lines);
    }
    app->star.nfiltered = nvt_rebuild_toulouse_line_filter(
        app->star.lines, app->star.nlines, app->star.search,
        app->star.filtered, MAX_LINES
    );
    app->star.nstops = 0;
    app->star.nstop_filtered = 0;
    app->star.npassages = 0;
    app->star.nvehicles = 0;
    app->star.sel_stop = -1;
    return app->star.nlines;
}

static int fetch_star_stops_once(void *ctx)
{
    AppState *app = ctx;
    ToulouseLine *line;

    app->star.nstop_filtered = 0;
    app->star.npassages = 0;
    app->star.sel_stop = -1;
    if (app->star.sel_line < 0 || app->star.sel_line >= app->star.nlines) return 0;

    line = &app->star.lines[app->star.sel_line];
    app->star.nstops = fetch_star_line_stops(line, app->star.stops, MAX_STOPS);
    if (app->star.nstops < 0) return -1;
    if (app->star.nstops > 1) {
        qsort(app->star.stops, app->star.nstops, sizeof(ToulouseStop), cmp_live_stops_by_name);
    }
    if (app->star.stop_search[0]) {
        app->star.nstop_filtered = nvt_rebuild_toulouse_stop_filter(
            app->star.stops, app->star.nstops, app->star.stop_search,
            app->star.stop_filtered, MAX_STOPS
        );
    }
    return app->star.nstops;
}

static int fetch_star_alerts_once(void *ctx)
{
    AppState *app = ctx;

    app->star.nalerts = fetch_star_alerts(app->star.alerts, MAX_ALERTS);
    if (app->star.nalerts < 0) return -1;
    app->star.snapshot.sample_alerts = app->star.nalerts;
    return app->star.nalerts;
}

static int fetch_star_passages_once(void *ctx)
{
    AppState *app = ctx;
    ToulouseLine *line;
    ToulouseStop *stop;

    app->star.npassages = 0;
    if (app->star.sel_line < 0 || app->star.sel_line >= app->star.nlines) return 0;
    if (app->star.sel_stop < 0 || app->star.sel_stop >= app->star.nstops) return 0;

    line = &app->star.lines[app->star.sel_line];
    stop = &app->star.stops[app->star.sel_stop];
    app->star.npassages = fetch_star_passages(line, stop, app->star.passages, MAX_PASSAGES);
    if (app->star.npassages < 0) return -1;
    if (app->star.npassages > 1) {
        qsort(app->star.passages, app->star.npassages, sizeof(ToulousePassage),
              cmp_live_passages_by_waiting_time);
    }
    return app->star.npassages;
}

static int fetch_star_vehicles_once(void *ctx)
{
    AppState *app = ctx;
    ToulouseLine *line;

    app->star.nvehicles = 0;
    if (app->star.sel_line < 0 || app->star.sel_line >= app->star.nlines) return 0;

    line = &app->star.lines[app->star.sel_line];
    app->star.nvehicles = fetch_star_vehicles(line, app->star.vehicles, MAX_VEHICLES);
    if (app->star.nvehicles < 0) return -1;
    if (app->star.nvehicles > 1) {
        qsort(app->star.vehicles, app->star.nvehicles, sizeof(ToulouseVehicle),
              cmp_live_vehicles_by_terminus);
    }
    return app->star.nvehicles;
}

int nvt_data_refresh_star_overview(AppState *app, int attempts, char *err, size_t err_sz)
{
    return nvt_retry_operation("STAR lines", attempts, fetch_star_overview_once, app, err, err_sz);
}

int nvt_data_load_star_stops(AppState *app, int attempts, char *err, size_t err_sz)
{
    return nvt_retry_operation("STAR line stops", attempts, fetch_star_stops_once, app, err, err_sz);
}

int nvt_data_refresh_star_alerts(AppState *app, int attempts, char *err, size_t err_sz)
{
    return nvt_retry_operation("STAR alerts", attempts, fetch_star_alerts_once, app, err, err_sz);
}

int nvt_data_load_star_passages(AppState *app, int attempts, char *err, size_t err_sz)
{
    return nvt_retry_operation("STAR departures", attempts, fetch_star_passages_once, app, err, err_sz);
}

int nvt_data_load_star_vehicles(AppState *app, int attempts, char *err, size_t err_sz)
{
    return nvt_retry_operation("STAR vehicles", attempts, fetch_star_vehicles_once, app, err, err_sz);
}

/* ── TCL (Lyon Sytral) ─────────────────────────────────────────── */

static int fetch_tcl_overview_once(void *ctx)
{
    AppState *app = ctx;

    if (fetch_tcl_snapshot(&app->tcl.snapshot, app->tcl.lines, MAX_LINES,
                           app->tcl.stops, MAX_STOPS) < 0) return -1;

    app->tcl.nlines = app->tcl.snapshot.sample_lines;
    app->tcl.nstops = app->tcl.snapshot.sample_stops;
    nvt_set_tcl_vehicle_stops(app->tcl.stops, app->tcl.nstops);
    if (app->tcl.nlines > 1) {
        qsort(app->tcl.lines, app->tcl.nlines, sizeof(ToulouseLine), nvt_cmp_idfm_lines);
    }
    app->tcl.nfiltered = nvt_rebuild_toulouse_line_filter(
        app->tcl.lines, app->tcl.nlines, app->tcl.search,
        app->tcl.filtered, MAX_LINES
    );
    app->tcl.nstop_filtered = 0;
    app->tcl.npassages = 0;
    app->tcl.nvehicles = 0;
    app->tcl.sel_stop = -1;
    return app->tcl.nlines;
}

static int fetch_tcl_stops_once(void *ctx)
{
    AppState *app = ctx;
    ToulouseLine *line;
    static ToulouseStop tmp[MAX_STOPS];

    app->tcl.nstop_filtered = 0;
    app->tcl.npassages = 0;
    app->tcl.sel_stop = -1;
    if (app->tcl.sel_line < 0 || app->tcl.sel_line >= app->tcl.nlines) return 0;

    line = &app->tcl.lines[app->tcl.sel_line];
    int n = fetch_tcl_line_stops(line, tmp, MAX_STOPS);
    if (n < 0) return -1;
    if (n > MAX_STOPS) n = MAX_STOPS;
    memcpy(app->tcl.stops, tmp, (size_t)n * sizeof(ToulouseStop));
    app->tcl.nstops = n;
    if (n > 1) qsort(app->tcl.stops, n, sizeof(ToulouseStop), cmp_live_stops_by_name);
    if (app->tcl.stop_search[0]) {
        app->tcl.nstop_filtered = nvt_rebuild_toulouse_stop_filter(
            app->tcl.stops, n, app->tcl.stop_search,
            app->tcl.stop_filtered, MAX_STOPS
        );
    }
    return n;
}

static int fetch_tcl_alerts_once(void *ctx)
{
    AppState *app = ctx;
    app->tcl.nalerts = fetch_tcl_alerts(app->tcl.alerts, MAX_ALERTS);
    if (app->tcl.nalerts < 0) return -1;
    app->tcl.snapshot.sample_alerts = app->tcl.nalerts;
    return app->tcl.nalerts;
}

static int fetch_tcl_passages_once(void *ctx)
{
    AppState *app = ctx;
    ToulouseLine *line;
    ToulouseStop *stop;

    app->tcl.npassages = 0;
    if (app->tcl.sel_line < 0 || app->tcl.sel_line >= app->tcl.nlines) return 0;
    if (app->tcl.sel_stop < 0 || app->tcl.sel_stop >= app->tcl.nstops) return 0;
    line = &app->tcl.lines[app->tcl.sel_line];
    stop = &app->tcl.stops[app->tcl.sel_stop];
    app->tcl.npassages = fetch_tcl_passages(line, stop, app->tcl.passages, MAX_PASSAGES);
    if (app->tcl.npassages < 0) return -1;
    if (app->tcl.npassages > 1) {
        qsort(app->tcl.passages, app->tcl.npassages, sizeof(ToulousePassage),
              cmp_live_passages_by_waiting_time);
    }
    return app->tcl.npassages;
}

static int fetch_tcl_vehicles_once(void *ctx)
{
    AppState *app = ctx;
    ToulouseLine *line;

    app->tcl.nvehicles = 0;
    if (app->tcl.sel_line < 0 || app->tcl.sel_line >= app->tcl.nlines) return 0;
    line = &app->tcl.lines[app->tcl.sel_line];
    app->tcl.nvehicles = fetch_tcl_vehicles(line, app->tcl.vehicles, MAX_VEHICLES);
    if (app->tcl.nvehicles < 0) return -1;
    return app->tcl.nvehicles;
}

int nvt_data_refresh_tcl_overview(AppState *app, int attempts, char *err, size_t err_sz)
{
    return nvt_retry_operation("TCL lines", attempts, fetch_tcl_overview_once, app, err, err_sz);
}
int nvt_data_load_tcl_stops(AppState *app, int attempts, char *err, size_t err_sz)
{
    return nvt_retry_operation("TCL line stops", attempts, fetch_tcl_stops_once, app, err, err_sz);
}
int nvt_data_refresh_tcl_alerts(AppState *app, int attempts, char *err, size_t err_sz)
{
    return nvt_retry_operation("TCL alerts", attempts, fetch_tcl_alerts_once, app, err, err_sz);
}
int nvt_data_load_tcl_passages(AppState *app, int attempts, char *err, size_t err_sz)
{
    return nvt_retry_operation("TCL departures", attempts, fetch_tcl_passages_once, app, err, err_sz);
}
int nvt_data_load_tcl_vehicles(AppState *app, int attempts, char *err, size_t err_sz)
{
    return nvt_retry_operation("TCL vehicles", attempts, fetch_tcl_vehicles_once, app, err, err_sz);
}
