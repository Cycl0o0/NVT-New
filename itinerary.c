#include "itinerary.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "filter.h"

void nvt_itinerary_reset(NvtItineraryState *state)
{
    if (!state) return;
    memset(state, 0, sizeof(*state));
    state->network_kind = -1;
    state->origin = -1;
    state->destination = -1;
}

void nvt_itinerary_prepare(NvtItineraryState *state, int network_kind,
                           const char *line_ref, const char *line_code,
                           const char *line_name)
{
    if (!state) return;
    nvt_itinerary_reset(state);
    state->network_kind = network_kind;
    snprintf(state->line_ref, sizeof(state->line_ref), "%s", line_ref ? line_ref : "");
    snprintf(state->line_code, sizeof(state->line_code), "%s", line_code ? line_code : "");
    snprintf(state->line_name, sizeof(state->line_name), "%s", line_name ? line_name : "");
}

int nvt_itinerary_add_stop_unique(NvtItineraryState *state, const char *ref,
                                  const char *name, const char *meta,
                                  double lon, double lat, int order,
                                  int source_index)
{
    if (!state || !name || !name[0]) return 0;

    for (int i = 0; i < state->nstops; i++) {
        if (strcmp(state->stops[i].name, name) != 0) continue;
        if (order >= 0 && (state->stops[i].order < 0 || order < state->stops[i].order)) {
            state->stops[i].order = order;
            state->stops[i].source_index = source_index;
        }
        if (!state->stops[i].ref[0] && ref && ref[0]) {
            snprintf(state->stops[i].ref, sizeof(state->stops[i].ref), "%s", ref);
        }
        if (!state->stops[i].meta[0] && meta && meta[0]) {
            snprintf(state->stops[i].meta, sizeof(state->stops[i].meta), "%s", meta);
        }
        if ((state->stops[i].lon == 0.0 && state->stops[i].lat == 0.0) && (lon != 0.0 || lat != 0.0)) {
            state->stops[i].lon = lon;
            state->stops[i].lat = lat;
        }
        return i;
    }

    if (state->nstops >= MAX_STOPS) return -1;

    {
        NvtItineraryStop *stop = &state->stops[state->nstops];

        memset(stop, 0, sizeof(*stop));
        stop->source_index = source_index;
        stop->order = order;
        stop->lon = lon;
        stop->lat = lat;
        snprintf(stop->ref, sizeof(stop->ref), "%s", ref ? ref : "");
        snprintf(stop->name, sizeof(stop->name), "%s", name);
        snprintf(stop->meta, sizeof(stop->meta), "%s", meta ? meta : "");
        state->nstops++;
    }

    return state->nstops - 1;
}

static int cmp_itinerary_stops(const void *left, const void *right)
{
    const NvtItineraryStop *a = left;
    const NvtItineraryStop *b = right;

    if (a->order >= 0 && b->order >= 0 && a->order != b->order) return a->order - b->order;
    if (a->order >= 0 && b->order < 0) return -1;
    if (a->order < 0 && b->order >= 0) return 1;
    return strcmp(a->name, b->name);
}

void nvt_itinerary_sort_by_order(NvtItineraryState *state)
{
    if (!state || state->nstops <= 1) return;
    qsort(state->stops, (size_t)state->nstops, sizeof(state->stops[0]), cmp_itinerary_stops);
}

int nvt_itinerary_rebuild_filter(NvtItineraryState *state)
{
    int n = 0;

    if (!state) return 0;
    for (int i = 0; i < state->nstops && n < MAX_STOPS; i++) {
        if (state->search[0]) {
            if (!nvt_strcasestr_s(state->stops[i].name, state->search) &&
                !nvt_strcasestr_s(state->stops[i].meta, state->search) &&
                !nvt_strcasestr_s(state->stops[i].ref, state->search)) {
                continue;
            }
        }
        state->filtered[n++] = i;
    }

    state->nfiltered = n;
    if (state->cursor >= state->nfiltered) state->cursor = state->nfiltered > 0 ? state->nfiltered - 1 : 0;
    if (state->cursor < 0) state->cursor = 0;
    return n;
}

int nvt_itinerary_route_progress(const LineRouteMap *route, double lon, double lat)
{
    double best_dist = 1e18;
    int best_order = -1;
    int order = 0;
    int have_aller = 0;

    if (!route || route->npoints <= 0 || route->npaths <= 0) return -1;

    for (int i = 0; i < route->npaths; i++) {
        if (route->paths[i].kind == MAP_KIND_ROUTE_ALLER) {
            have_aller = 1;
            break;
        }
    }

    for (int i = 0; i < route->npaths; i++) {
        const MapPath *path = &route->paths[i];

        if (have_aller && path->kind != MAP_KIND_ROUTE_ALLER) continue;
        for (int j = 0; j < path->count; j++, order++) {
            int point_index = path->start + j;
            double dx;
            double dy;
            double dist;

            if (point_index < 0 || point_index >= route->npoints) continue;
            dx = route->points[point_index].lon - lon;
            dy = route->points[point_index].lat - lat;
            dist = dx * dx + dy * dy;
            if (dist < best_dist) {
                best_dist = dist;
                best_order = order;
            }
        }
    }

    return best_order;
}

int nvt_itinerary_direction(const NvtItineraryState *state)
{
    if (!state) return 0;
    if (state->origin < 0 || state->origin >= state->nstops) return 0;
    if (state->destination < 0 || state->destination >= state->nstops) return 0;
    if (state->origin == state->destination) return 0;
    return state->destination > state->origin ? 1 : -1;
}

int nvt_itinerary_hops(const NvtItineraryState *state)
{
    if (!state) return 0;
    if (state->origin < 0 || state->origin >= state->nstops) return 0;
    if (state->destination < 0 || state->destination >= state->nstops) return 0;
    return abs(state->destination - state->origin);
}
