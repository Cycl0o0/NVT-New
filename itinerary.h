#ifndef ITINERARY_H
#define ITINERARY_H

#include "api.h"

typedef struct {
    int source_index;
    int order;
    char ref[96];
    char name[96];
    char meta[64];
    double lon;
    double lat;
} NvtItineraryStop;

typedef struct {
    int network_kind;
    int ready;
    char line_ref[96];
    char line_code[16];
    char line_name[96];
    char direction_forward[96];
    char direction_backward[96];
    char search[64];
    NvtItineraryStop stops[MAX_STOPS];
    int filtered[MAX_STOPS];
    int nstops;
    int nfiltered;
    int cursor;
    int scroll;
    int origin;
    int destination;
} NvtItineraryState;

void nvt_itinerary_reset(NvtItineraryState *state);
void nvt_itinerary_prepare(NvtItineraryState *state, int network_kind,
                           const char *line_ref, const char *line_code,
                           const char *line_name);
int nvt_itinerary_add_stop_unique(NvtItineraryState *state, const char *ref,
                                  const char *name, const char *meta,
                                  double lon, double lat, int order,
                                  int source_index);
void nvt_itinerary_sort_by_order(NvtItineraryState *state);
int nvt_itinerary_rebuild_filter(NvtItineraryState *state);
int nvt_itinerary_route_progress(const LineRouteMap *route, double lon, double lat);
int nvt_itinerary_direction(const NvtItineraryState *state);
int nvt_itinerary_hops(const NvtItineraryState *state);

#endif
