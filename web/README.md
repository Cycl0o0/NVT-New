# NVT Web

Glassmorphism Nuxt 3 frontend for the NVT transit backend, with Apple MapKit JS for the live map.

Mirrors the TUI's data: lines, stops, next passages, alerts, vehicles — across Bordeaux, Toulouse, IDFM, and SNCF networks.

## Setup

```bash
cd web
npm install
cp .env.example .env
# edit .env: set MAPKIT_JS_TOKEN and (optionally) NVT_BACKEND_URL
```

Start the backend in a separate terminal from the repo root:

```bash
make backend-run
```

Then run the Nuxt dev server:

```bash
npm run dev
```

The web app proxies all `/api/nvt/*` calls server-side to the C backend, so MapKit JS and the rest can run from the browser without CORS concerns.

## Configuration

| Variable | Default | Purpose |
| --- | --- | --- |
| `NVT_BACKEND_URL` | `http://127.0.0.1:8080` | NVT C backend base URL |
| `MAPKIT_JS_TOKEN` | _(empty)_ | Apple MapKit JS JWT token (server-issued) |
| `NVT_DEFAULT_NETWORK` | `bdx` | Default network: `bdx`, `tls`, `idfm`, `sncf` |
| `NVT_REFRESH_MS` | `15000` | Live refresh interval for passages and vehicles |

## Production build

```bash
npm run build
node .output/server/index.mjs
```
