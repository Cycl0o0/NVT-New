#include <assert.h>
#include <string.h>

#include "itinerary.h"

static void test_unique_order_and_filter(void)
{
    NvtItineraryState state;

    nvt_itinerary_prepare(&state, 2, "line:A", "A", "Tram A");
    assert(nvt_itinerary_add_stop_unique(&state, "1", "Alpha", "Centre", 0.0, 0.0, 30, 1) == 0);
    assert(nvt_itinerary_add_stop_unique(&state, "2", "Bravo", "Nord", 0.0, 0.0, 10, 2) == 1);
    assert(nvt_itinerary_add_stop_unique(&state, "3", "Alpha", "Centre", 0.0, 0.0, 5, 3) == 0);
    assert(state.nstops == 2);
    assert(state.stops[0].order == 5);

    nvt_itinerary_sort_by_order(&state);
    assert(strcmp(state.stops[0].name, "Alpha") == 0);
    assert(strcmp(state.stops[1].name, "Bravo") == 0);

    strcpy(state.search, "nord");
    assert(nvt_itinerary_rebuild_filter(&state) == 1);
    assert(state.filtered[0] == 1);
}

static void test_direction_and_hops(void)
{
    NvtItineraryState state;

    nvt_itinerary_prepare(&state, 0, "line:1", "1", "Lianes 1");
    nvt_itinerary_add_stop_unique(&state, "1", "A", "", 0.0, 0.0, 0, 0);
    nvt_itinerary_add_stop_unique(&state, "2", "B", "", 0.0, 0.0, 1, 1);
    nvt_itinerary_add_stop_unique(&state, "3", "C", "", 0.0, 0.0, 2, 2);
    state.origin = 0;
    state.destination = 2;
    assert(nvt_itinerary_direction(&state) == 1);
    assert(nvt_itinerary_hops(&state) == 2);
    state.origin = 2;
    state.destination = 0;
    assert(nvt_itinerary_direction(&state) == -1);
    assert(nvt_itinerary_hops(&state) == 2);
}

static void test_route_progress_prefers_aller(void)
{
    LineRouteMap route;

    memset(&route, 0, sizeof(route));
    route.points[0].lon = 0.0;
    route.points[0].lat = 0.0;
    route.points[1].lon = 1.0;
    route.points[1].lat = 0.0;
    route.points[2].lon = 2.0;
    route.points[2].lat = 0.0;
    route.points[3].lon = 2.0;
    route.points[3].lat = 1.0;
    route.paths[0].start = 0;
    route.paths[0].count = 3;
    route.paths[0].kind = MAP_KIND_ROUTE_RETOUR;
    route.paths[1].start = 1;
    route.paths[1].count = 3;
    route.paths[1].kind = MAP_KIND_ROUTE_ALLER;
    route.npoints = 4;
    route.npaths = 2;

    assert(nvt_itinerary_route_progress(&route, 2.0, 1.0) == 2);
}

int main(void)
{
    test_unique_order_and_filter();
    test_direction_and_hops();
    test_route_progress_prefers_aller();
    return 0;
}
