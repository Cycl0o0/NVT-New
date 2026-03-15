#include "network.h"

#include "data.h"
#include "filter.h"

static int bordeaux_refresh_overview(AppState *app, char *err, size_t err_sz)
{
    return nvt_data_refresh_bordeaux_overview(app, 2, err, err_sz);
}

static int bordeaux_refresh_alerts(AppState *app, char *err, size_t err_sz)
{
    return nvt_data_refresh_bordeaux_alerts(app, 2, err, err_sz);
}

static int bordeaux_load_stops(AppState *app, char *err, size_t err_sz)
{
    (void)err;
    (void)err_sz;
    app->bdx.nstop_filtered = nvt_rebuild_stop_filter(
        app->bdx.stop_groups,
        app->bdx.nstop_groups,
        app->bdx.stop_search,
        app->bdx.stop_filtered,
        MAX_STOP_GROUPS
    );
    return app->bdx.nstop_groups;
}

static int bordeaux_load_passages(AppState *app, char *err, size_t err_sz)
{
    return nvt_data_load_bordeaux_passages(app, 2, err, err_sz);
}

static int bordeaux_load_vehicles(AppState *app, char *err, size_t err_sz)
{
    return nvt_data_refresh_bordeaux_vehicles(app, 2, err, err_sz);
}

static void bordeaux_reset_transient(AppState *app)
{
    app->bdx.nvehicles = 0;
    nvt_app_reset_line_route(app);
    nvt_app_reset_vehicle_detail_map(app);
}

static int toulouse_refresh_overview(AppState *app, char *err, size_t err_sz)
{
    return nvt_data_refresh_toulouse_overview(app, 2, err, err_sz);
}

static int toulouse_refresh_alerts(AppState *app, char *err, size_t err_sz)
{
    return nvt_data_refresh_toulouse_alerts(app, 2, err, err_sz);
}

static int toulouse_load_stops(AppState *app, char *err, size_t err_sz)
{
    (void)err;
    (void)err_sz;
    app->tls.nstop_filtered = nvt_rebuild_toulouse_stop_filter(
        app->tls.stops,
        app->tls.nstops,
        app->tls.stop_search,
        app->tls.stop_filtered,
        MAX_STOPS
    );
    return app->tls.nstops;
}

static int toulouse_load_passages(AppState *app, char *err, size_t err_sz)
{
    return nvt_data_load_toulouse_passages(app, 2, err, err_sz);
}

static int toulouse_load_vehicles(AppState *app, char *err, size_t err_sz)
{
    return nvt_data_load_toulouse_vehicles(app, 2, err, err_sz);
}

static void toulouse_reset_transient(AppState *app)
{
    app->tls.nvehicles = 0;
    nvt_app_reset_toulouse_line_route(app);
    nvt_app_reset_toulouse_metro_map_cache(app);
}

static int idfm_refresh_overview(AppState *app, char *err, size_t err_sz)
{
    return nvt_data_refresh_idfm_overview(app, 2, err, err_sz);
}

static int idfm_refresh_alerts(AppState *app, char *err, size_t err_sz)
{
    return nvt_data_refresh_idfm_alerts(app, 2, err, err_sz);
}

static int idfm_load_stops(AppState *app, char *err, size_t err_sz)
{
    return nvt_data_load_idfm_stops(app, 2, err, err_sz);
}

static int idfm_load_passages(AppState *app, char *err, size_t err_sz)
{
    return nvt_data_load_idfm_passages(app, 2, err, err_sz);
}

static int idfm_load_vehicles(AppState *app, char *err, size_t err_sz)
{
    return nvt_data_load_idfm_vehicles(app, 2, err, err_sz);
}

static void idfm_reset_transient(AppState *app)
{
    app->idf.nstops = 0;
    app->idf.nstop_filtered = 0;
    app->idf.npassages = 0;
    app->idf.nvehicles = 0;
    app->idf.sel_line = -1;
    app->idf.sel_stop = -1;
}

static int sncf_refresh_overview(AppState *app, char *err, size_t err_sz)
{
    return nvt_data_refresh_sncf_overview(app, 2, err, err_sz);
}

static int sncf_refresh_alerts(AppState *app, char *err, size_t err_sz)
{
    return nvt_data_refresh_sncf_alerts(app, 2, err, err_sz);
}

static int sncf_load_stops(AppState *app, char *err, size_t err_sz)
{
    return nvt_data_load_sncf_stops(app, 2, err, err_sz);
}

static int sncf_load_passages(AppState *app, char *err, size_t err_sz)
{
    return nvt_data_load_sncf_passages(app, 2, err, err_sz);
}

static int sncf_load_vehicles(AppState *app, char *err, size_t err_sz)
{
    return nvt_data_load_sncf_vehicles(app, 2, err, err_sz);
}

static void sncf_reset_transient(AppState *app)
{
    app->sncf.nstops = 0;
    app->sncf.nstop_filtered = 0;
    app->sncf.npassages = 0;
    app->sncf.nvehicles = 0;
    app->sncf.sel_line = -1;
    app->sncf.sel_stop = -1;
}

static const NvtNetworkAdapter NETWORKS[] = {
    {
        .kind = NET_BDX,
        .name = "Bordeaux",
        .refresh_overview = bordeaux_refresh_overview,
        .refresh_alerts = bordeaux_refresh_alerts,
        .load_stops = bordeaux_load_stops,
        .load_passages = bordeaux_load_passages,
        .load_vehicles = bordeaux_load_vehicles,
        .reset_transient = bordeaux_reset_transient,
    },
    {
        .kind = NET_TLS,
        .name = "Toulouse",
        .refresh_overview = toulouse_refresh_overview,
        .refresh_alerts = toulouse_refresh_alerts,
        .load_stops = toulouse_load_stops,
        .load_passages = toulouse_load_passages,
        .load_vehicles = toulouse_load_vehicles,
        .reset_transient = toulouse_reset_transient,
    },
    {
        .kind = NET_IDFM,
        .name = "Paris IDFM",
        .refresh_overview = idfm_refresh_overview,
        .refresh_alerts = idfm_refresh_alerts,
        .load_stops = idfm_load_stops,
        .load_passages = idfm_load_passages,
        .load_vehicles = idfm_load_vehicles,
        .reset_transient = idfm_reset_transient,
    },
    {
        .kind = NET_SNCF,
        .name = "SNCF",
        .refresh_overview = sncf_refresh_overview,
        .refresh_alerts = sncf_refresh_alerts,
        .load_stops = sncf_load_stops,
        .load_passages = sncf_load_passages,
        .load_vehicles = sncf_load_vehicles,
        .reset_transient = sncf_reset_transient,
    },
};

const char *nvt_network_name(NvtNetwork network)
{
    switch (network) {
    case NET_TLS:
        return "Toulouse";
    case NET_IDFM:
        return "Paris IDFM";
    case NET_SNCF:
        return "SNCF";
    case NET_BDX:
    default:
        return "Bordeaux";
    }
}

const NvtNetworkAdapter *nvt_network_adapter(NvtNetwork network)
{
    for (size_t i = 0; i < sizeof(NETWORKS) / sizeof(NETWORKS[0]); i++) {
        if (NETWORKS[i].kind == network) return &NETWORKS[i];
    }
    return &NETWORKS[0];
}

void nvt_switch_network(AppState *app, NvtNetwork network)
{
    const NvtNetworkAdapter *adapter;

    if (network == app->ui.network) return;

    app->ui.network = network;
    app->ui.screen = SCR_LINES;
    app->bdx.cursor = 0;
    app->bdx.scroll = 0;
    app->tls.cursor = 0;
    app->tls.scroll = 0;
    app->tls.stop_cursor = 0;
    app->tls.stop_scroll = 0;
    app->tls.sel_line = -1;
    app->tls.sel_stop = -1;
    app->idf.cursor = 0;
    app->idf.scroll = 0;
    app->idf.stop_cursor = 0;
    app->idf.stop_scroll = 0;
    app->idf.sel_line = -1;
    app->idf.sel_stop = -1;
    app->sncf.cursor = 0;
    app->sncf.scroll = 0;
    app->sncf.stop_cursor = 0;
    app->sncf.stop_scroll = 0;
    app->sncf.sel_line = -1;
    app->sncf.sel_stop = -1;
    app->ui.alert_scroll = 0;

    adapter = nvt_network_adapter(network);
    if (adapter->reset_transient) adapter->reset_transient(app);
}
