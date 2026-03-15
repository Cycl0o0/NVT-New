#ifndef DATA_H
#define DATA_H

#include <stddef.h>

#include "app_state.h"

int nvt_data_init_bordeaux(AppState *app, int attempts, char *err, size_t err_sz);
int nvt_data_refresh_bordeaux_overview(AppState *app, int attempts, char *err, size_t err_sz);
int nvt_data_refresh_bordeaux_alerts(AppState *app, int attempts, char *err, size_t err_sz);
int nvt_data_refresh_bordeaux_vehicles(AppState *app, int attempts, char *err, size_t err_sz);
int nvt_data_load_bordeaux_passages(AppState *app, int attempts, char *err, size_t err_sz);
int nvt_data_refresh_toulouse_overview(AppState *app, int attempts, char *err, size_t err_sz);
int nvt_data_refresh_toulouse_alerts(AppState *app, int attempts, char *err, size_t err_sz);
int nvt_data_load_toulouse_passages(AppState *app, int attempts, char *err, size_t err_sz);
int nvt_data_load_toulouse_vehicles(AppState *app, int attempts, char *err, size_t err_sz);
int nvt_data_refresh_idfm_overview(AppState *app, int attempts, char *err, size_t err_sz);
int nvt_data_load_idfm_stops(AppState *app, int attempts, char *err, size_t err_sz);
int nvt_data_refresh_idfm_alerts(AppState *app, int attempts, char *err, size_t err_sz);
int nvt_data_load_idfm_passages(AppState *app, int attempts, char *err, size_t err_sz);
int nvt_data_load_idfm_vehicles(AppState *app, int attempts, char *err, size_t err_sz);
int nvt_data_refresh_sncf_overview(AppState *app, int attempts, char *err, size_t err_sz);
int nvt_data_load_sncf_stops(AppState *app, int attempts, char *err, size_t err_sz);
int nvt_data_refresh_sncf_alerts(AppState *app, int attempts, char *err, size_t err_sz);
int nvt_data_load_sncf_passages(AppState *app, int attempts, char *err, size_t err_sz);
int nvt_data_load_sncf_vehicles(AppState *app, int attempts, char *err, size_t err_sz);

#endif
