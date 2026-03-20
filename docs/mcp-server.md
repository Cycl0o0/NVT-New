# NVT MCP Server

## Overview

`mcp/nvt_mcp_server.py` is a `stdio` MCP server built on top of the existing NVT backend.

It exposes read-only tools for:

- checking whether a line is active
- listing lines and stop groups
- reading next passages for a stop
- listing network or line alerts
- combining line status, alerts, vehicles, and stop departures in one tool

## Run

Build the backend first:

```bash
make backend
```

Start the MCP server:

```bash
make mcp-run
```

By default the MCP server:

- starts `./nvt-backend` automatically
- picks a free localhost port
- waits for `/api/health`

To reuse an existing backend instead:

```bash
NVT_BACKEND_URL=http://127.0.0.1:8080 python3 ./mcp/nvt_mcp_server.py
```

## Environment

Supported environment variables:

- `NVT_BACKEND_URL`: use an already running backend
- `NVT_BACKEND_BIN`: override the `nvt-backend` binary path
- `NVT_BACKEND_PORT`: fixed port for auto-spawned backend
- `NVT_MCP_BACKEND_START_TIMEOUT`: backend startup timeout in seconds
- `NVT_MCP_HTTP_TIMEOUT`: per-request backend timeout in seconds

When the MCP server auto-spawns the backend, it also loads `.nvt-backend.env` if present.

## Exposed Tools

- `nvt_network_overview`: line and alert summary for one network
- `nvt_search_lines`: search a line by code, gid, ref, or name
- `nvt_search_stops`: search stop groups or stop areas
- `nvt_alerts`: list alerts, optionally filtered by line or severity
- `nvt_line_status`: show if a line is active and return alerts and vehicles
- `nvt_next_passages`: return upcoming departures for one stop
- `nvt_monitor_line`: composite monitor for one line, with optional stop departures

For `IDFM` and `SNCF`, stop search and passage lookup remain line-scoped, matching the existing backend behavior.

## MCP Client Config

Example JSON configuration:

```json
{
  "mcpServers": {
    "nvt": {
      "command": "python3",
      "args": [
        "/Users/cyclolysis/NVT-New/mcp/nvt_mcp_server.py"
      ]
    }
  }
}
```

If you already run the backend yourself:

```json
{
  "mcpServers": {
    "nvt": {
      "command": "python3",
      "args": [
        "/Users/cyclolysis/NVT-New/mcp/nvt_mcp_server.py"
      ],
      "env": {
        "NVT_BACKEND_URL": "http://127.0.0.1:8080"
      }
    }
  }
}
```

## Notes

- The server is read-only.
- Tool names are stable and based on the current backend endpoints.
- If a line or stop query is ambiguous, the server returns concrete suggestions instead of guessing.
