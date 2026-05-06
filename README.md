# NVT

NVT is first a TUI transport monitoring application written in C.

Around that TUI app, the project also provides a local backend API, a Nuxt 3 web frontend, and a Home Assistant custom integration. It also ships a local MCP server that exposes the backend data to AI clients.

The project currently provides:

- a terminal user interface in C (`nvt`)
- a local backend API for transit data
- a Nuxt 3 / Vue 3 web frontend with MapKit JS in `web/`
- a local MCP server for lines, stops, next passages, alerts, and line monitoring
- support for Bordeaux (TBM), Toulouse (Tisséo), Paris (IDFM), SNCF, Rennes (STAR), and Lyon (TCL Sytral)
- a Home Assistant integration available in `custom_components/nvt`
- line monitoring, next passages, alert details, thresholds, and automation-friendly entities

## Developer

- `Cycl0o0`

## Networks & live vehicle GPS

Every supported network reports lines, stops, next passages, alerts, and **live vehicle positions** with real GPS coordinates. The strategy varies per network because not every operator publishes a SIRI VehicleMonitoring feed publicly:

| Network | API | Live vehicle GPS source |
|---|---|---|
| **Bordeaux TBM** | data.bordeaux-metropole.fr | Native vehicle endpoint |
| **Toulouse Tisséo** | api.tisseo.fr v2 | Synthesized from real-time `stops_schedules` (a vehicle is rendered at every stop with an imminent passage) |
| **Paris IDFM** | PRIM Navitia + PRIM SIRI Lite | Synthesized from SIRI Estimated Timetable (`/marketplace/estimated-timetable`) using a 38 k-stop crosswalk fetched once from `data.iledefrance-mobilites.fr/datasets/arrets` |
| **SNCF** | api.sncf.com Navitia | Synthesized from `/lines/{id}/departures` using `stop_point.coord` returned in each departure |
| **Rennes STAR** | data.explore.star.fr Opendatasoft | Native `tco-bus-vehicules-position-tr` dataset |
| **Lyon TCL** | data.grandlyon.com WFS + datapusher | Static lines/stops via WFS GeoJSON (no auth); real-time passages via `tcl_sytral.tclpassagearret` (HTTP Basic auth, set `TCL_USER`/`TCL_PASS` in `.nvt-backend.env`); vehicles synthesized from passages |

GTFS-RT is intentionally not used. All sources are public JSON APIs.

### TCL credentials

The Lyon dataset requires a free account on [moncompte.grandlyon.com](https://moncompte.grandlyon.com). Once registered, add the credentials to `.nvt-backend.env`:

```bash
TCL_USER=your_login
TCL_PASS=your_password
```

Without credentials, TCL lines and stops still load but real-time passages and synthesized vehicles return empty.

## Features

- Ncurses-based TUI app written in C
- Multi-network live support across 6 networks
- Local backend endpoints for lines, alerts, stop groups, passages, vehicles, and map boundaries
- Real-time vehicle positions on every network (synthesized when no native GPS feed exists — see table above)
- Generic `interpolated_positions` module for synthesizing vehicle markers from passage timetables
- IDFM stop crosswalk (`idfm_crosswalk`) bridging SIRI `STIF:StopPoint:Q:XXX:` ↔ Navitia stops via `arrid` lookup
- A line-based itinerary calculator in the TUI for Bordeaux, Toulouse, IDFM, and SNCF
- One Home Assistant config entry per `network + stop + line + direction`
- User-defined threshold values such as `5`, `10`, `20`, or more minutes
- Alert detail display based on the alert message, not only the title
- Filtering of selectable lines based on the selected stop
- Line metadata exposure including type and colors when available from the backend

## Project Layout

```text
.
|-- backend.c                      HTTP backend
|-- nvt.c                          TUI entry point
|-- api.[ch]                       Network fetchers (TBM, Tisséo, IDFM, SNCF, STAR, TCL)
|-- interpolated_positions.[ch]    Generic synthesis: passages → vehicle markers
|-- idfm_crosswalk.[ch]            SIRI StopPointRef → coords lookup for IDFM
|-- data.c, network.c, ui.c, ...   TUI modules
|-- web/                           Nuxt 3 / Vue 3 frontend with MapKit JS
|-- mcp/                           MCP server (Python)
|-- custom_components/nvt/         Home Assistant integration
|-- Makefile
```

## Build

Build the terminal client and backend:

```bash
make
```

Build only the TUI:

```bash
make tui
```

Build only the backend:

```bash
make backend
```

Run the backend locally:

```bash
make backend-run
```

The backend runner prints the API URL to paste into the Home Assistant `NVT` config flow.

Run the MCP server locally:

```bash
make mcp-run
```

By default, the MCP server auto-starts `./nvt-backend` on a free local port.
To point it to an already running backend instead:

```bash
NVT_BACKEND_URL=http://127.0.0.1:8080 python3 ./mcp/nvt_mcp_server.py
```

Run the TUI directly:

```bash
./nvt
```

## Web frontend

The Nuxt 3 frontend lives in `web/`. It exposes the backend through a server proxy and renders lines, live passages, alerts and a fullscreen MapKit JS map with the selected line color tinting the basemap.

```bash
cd web
npm install
cp .env.example .env   # set MAPKIT_JS_TOKEN and (optionally) NVT_BACKEND_URL
npm run dev
```

## Home Assistant

The Home Assistant integration lives in `custom_components/nvt/`.

Typical flow:

1. Start the backend with `make backend-run`
2. Mount `custom_components` into a Home Assistant instance
3. Add the `NVT` integration
4. Select the network, stop, line, direction, threshold, and refresh interval

## MCP

The MCP server lives in `mcp/nvt_mcp_server.py`.

It exposes tools for:

- network overview
- line search
- stop search
- line status
- next passages
- alert listing
- composite line monitoring

Connection examples and environment details live in `docs/mcp-server.md`.

## Development Notes

- `nvt.c` is the main TUI application entry point
- `make test` runs the first unit tests for extracted filter and map helpers
- `make test` also runs the MCP server unit tests
- Press `i` or `6` on a selected line to open the itinerary calculator, then use `o`, `d`, and `x`
- Technical notes for the modularized TUI live in `docs/technical-readme.md`
- MCP usage notes live in `docs/mcp-server.md`
- IDFM, SNCF, STAR, and TCL support is configured directly in the C sources
- The IDFM crosswalk downloads ~3 MB gzipped on first vehicle request (lazy load), then is kept in memory
- Local Home Assistant test data can be stored in `.ha-test/`
- Python cache files and compiled binaries should not be committed

## License

This project is licensed under the `GNU Affero General Public License v3.0 (AGPL-3.0)`.
See `LICENSE` for the official full text.
