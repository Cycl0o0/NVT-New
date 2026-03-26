# NVT

NVT is first a TUI transport monitoring application written in C.

Around that TUI app, the project also provides a local backend API and a Home Assistant custom integration.
It now also provides a local MCP server that exposes the backend data to AI clients.

The project currently provides:

- a terminal user interface in C (`nvt`)
- a local backend API for transit data
- a local MCP server for lines, stops, next passages, alerts, and line monitoring
- a native SwiftUI Apple app for iOS, macOS, visionOS, and watchOS companion browsing
- support for Bordeaux, Toulouse, and Paris IDFM networks
- a Home Assistant integration available in `custom_components/nvt`
- line monitoring, next passages, alert details, thresholds, and automation-friendly entities

## Developer

- `Cycl0o0`

## Features

- Ncurses-based TUI app written in C
- Multi-network live support for Bordeaux, Toulouse, and Paris IDFM
- Local backend endpoints for lines, alerts, stop groups, passages, vehicles, and map boundaries
- A line-based itinerary calculator in the TUI for Bordeaux, Toulouse, IDFM, and SNCF
- One Home Assistant config entry per `network + stop + line + direction`
- User-defined threshold values such as `5`, `10`, `20`, or more minutes
- Alert detail display based on the alert message, not only the title
- Filtering of selectable lines based on the selected stop
- Line metadata exposure including type and colors when available from the backend

## Project Layout

```text
.
|-- backend.c
|-- Apple/
|-- mcp/
|-- api.c
|-- api.h
|-- nvt.c
|-- Makefile
`-- custom_components/
    `-- nvt/
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

Generate the Apple app project:

```bash
make apple-project
```

This creates `Apple/NVT.xcodeproj`.

Run the TUI directly:

```bash
./nvt
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

## Apple App

The native SwiftUI Apple client lives in `Apple/`.

It includes:

- one shared app target for `iOS`, `macOS`, and `visionOS`
- one `watchOS` companion app target
- direct backend integration for lines, alerts, stops, passages, vehicles, and route/boundary maps

Build notes live in `Apple/README.md`.

## Development Notes

- `nvt.c` is the main TUI application entry point
- `make test` runs the first unit tests for extracted filter and map helpers
- `make test` also runs the MCP server unit tests
- Press `i` or `6` on a selected line to open the itinerary calculator, then use `o`, `d`, and `x`
- Technical notes for the modularized TUI live in `docs/technical-readme.md`
- MCP usage notes live in `docs/mcp-server.md`
- IDFM support is configured directly in the C sources
- Local Home Assistant test data can be stored in `.ha-test/`
- Python cache files and compiled binaries should not be committed

## License

This project is licensed under the `GNU Affero General Public License v3.0 (AGPL-3.0)`.
See `LICENSE` for the official full text.
