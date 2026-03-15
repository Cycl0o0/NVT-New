#ifndef NETWORK_H
#define NETWORK_H

#include <stddef.h>

#include "app_state.h"

typedef struct {
    NvtNetwork kind;
    const char *name;
    int (*refresh_overview)(AppState *app, char *err, size_t err_sz);
    int (*refresh_alerts)(AppState *app, char *err, size_t err_sz);
    int (*load_stops)(AppState *app, char *err, size_t err_sz);
    int (*load_passages)(AppState *app, char *err, size_t err_sz);
    int (*load_vehicles)(AppState *app, char *err, size_t err_sz);
    void (*reset_transient)(AppState *app);
} NvtNetworkAdapter;

const char *nvt_network_name(NvtNetwork network);
const NvtNetworkAdapter *nvt_network_adapter(NvtNetwork network);
void nvt_switch_network(AppState *app, NvtNetwork network);

#endif
