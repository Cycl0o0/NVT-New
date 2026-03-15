# NVT Technical Notes

## Overview

The `nvt` TUI is still the main executable, but the codebase now has clearer module boundaries:

- `nvt.c`: screen composition, input loop, and ncurses orchestration
- `app_state.[ch]`: runtime state containers and cache reset helpers
- `data.[ch]`: data loading, refresh flows, and simple retry wrappers
- `network.[ch]`: Bordeaux/Toulouse/IDFM adapters through function pointers
- `filter.[ch]`: filter, search, and sortable pure logic
- `map_math.[ch]`: map projection and clipping logic
- `ui.[ch]`: reusable drawing primitives for cards, panels, scrollbars, and wrapped text

## Runtime State

The previous sea of globals has been collapsed into a single `AppState` with focused sub-structures:

- `NvtBordeauxState`
- `NvtToulouseState`
- `NvtIdfmState`
- `NvtMapState`
- `NvtUiState`

`nvt.c` still uses compatibility macros for now, but the state is centralized and can be passed directly to more functions over time.

## Network Abstraction

`network.c` introduces `NvtNetworkAdapter`, which groups per-network behavior:

- overview refresh
- alert refresh
- passage loading
- vehicle loading
- transient cache reset

This keeps Bordeaux/Toulouse/IDFM branching out of some control paths and makes future networks easier to slot in.

IDFM currently uses the official PRIM/Navitia endpoints for:

- paginated `lines`
- line-scoped `stop_areas`
- paginated `line_reports`
- `departures` for a selected stop area
- `vehicle_positions` for a selected line

The current IDFM vehicle screen is intentionally text-first: the endpoint exposes active journeys but not map-ready coordinates in the payloads used here, so the TUI does not pretend to render a live vehicle map for Paris.
When an IDFM stop has already been selected, the live screen also reuses cached departures so the operator keeps a Bordeaux-like passage view alongside the active journey feed.

## Error Handling

`data.c` adds retryable load wrappers around the raw `fetch_*` API functions. The TUI now routes more manual refresh and load paths through these wrappers, which gives:

- one place to adjust retry counts
- clearer user-facing toast messages on failure
- easier testing of load flows

## Testing

`make test` currently covers:

- filter/search/sort helpers in `tests/test_filter.c`
- projection and clipping helpers in `tests/test_map_math.c`

This is intentionally small, but it gives a starting point for expanding tests around atlas routing, passage ordering, and line matching behavior.

## Next Refactor Targets

- Move more screen-specific state transitions out of the main event loop
- Replace remaining compatibility macros in `nvt.c` with explicit `AppState *` parameters
- Extract atlas rendering and route rendering into dedicated files
- Add tests for passage sorting and route visibility rules
