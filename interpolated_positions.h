#ifndef INTERPOLATED_POSITIONS_H
#define INTERPOLATED_POSITIONS_H

#include "api.h"

/*
 * Synthesize "live" vehicle positions from real-time passage data.
 *
 * Used for transit networks whose APIs expose passage times per stop
 * (StopMonitoring / Tisséo stops_schedules / SIRI Lite ETT) but NOT
 * actual GPS positions. We approximate each vehicle's location by
 * placing it at the next stop it will reach soon.
 *
 * The output ToulouseVehicle entries get `has_position = 1` so the
 * frontend renders them on the map. Positions are best-guess (~stop
 * location), bearing is unset.
 *
 * `passages_at_stops` is parallel to `stops`: passages_at_stops[i] is
 * the list of upcoming passages observed at stops[i] for the active
 * line. Empty arrays are tolerated (just contribute nothing).
 */

typedef struct {
    /* All passages parallel to a stops[] array. */
    const ToulousePassage *passages;   /* count = n */
    int  count;
    int  stop_index;                   /* which stop in the parent stops[] array */
} StopPassages;

/*
 * Convert (stops, passages-per-stop, line) → vehicles.
 *
 * Algorithm:
 *   - For each stop, take the passage with the smallest non-negative
 *     waiting time as "next departure".
 *   - If that wait is below `imminent_seconds` (default 120), the
 *     vehicle is considered "approaching" → emit a synthetic vehicle
 *     at this stop's coordinates with the destination/headsign from
 *     the passage.
 *   - De-duplicate by (destination + departure_dt) so the same vehicle
 *     observed at consecutive stops doesn't render twice.
 *
 * Returns the number of vehicles written to `out` (0..max).
 */
int nvt_synthesize_vehicles_from_passages(
    const ToulouseLine     *line,
    const ToulouseStop     *stops,
    int                     nstops,
    const StopPassages     *passages_per_stop,
    int                     imminent_seconds,
    ToulouseVehicle        *out,
    int                     max
);

#endif
