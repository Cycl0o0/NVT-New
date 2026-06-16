# NVT Web

Glassmorphism Nuxt 3 frontend for the NVT transit backend, with Apple MapKit JS for the live map.

Mirrors the TUI's data: lines, stops, next passages, alerts, vehicles — across Bordeaux, Toulouse, IDFM, SNCF, and STAR (Rennes) networks.

## Setup

```bash
cd web
npm install
cp .env.example .env
# edit .env: set MapKit credentials (see below) and (optionally) NVT_BACKEND_URL
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
| `MAPKIT_PRIVATE_KEY` | _(empty)_ | Apple MapKit `.p8` private key contents (enables auto-refresh) |
| `MAPKIT_KEY_ID` | _(empty)_ | MapKit key ID (the `kid` JWT header) |
| `MAPKIT_TEAM_ID` | _(empty)_ | Apple developer team ID (the `iss` JWT claim) |
| `MAPKIT_TOKEN_TTL` | `1800` | Minted token lifetime in seconds |
| `MAPKIT_JS_TOKEN` | _(empty)_ | Pre-issued static JWT — fallback only, does not auto-refresh |
| `NVT_DEFAULT_NETWORK` | `bdx` | Default network: `bdx`, `tls`, `idfm`, `sncf`, `star` |
| `NVT_REFRESH_MS` | `15000` | Live refresh interval for passages and vehicles |

### MapKit tokens

The browser never holds a long-lived token. Instead the Nuxt server route
`/api/mapkit-token` mints a short-lived ES256 JWT on demand, and the client
re-fetches it every time MapKit's `authorizationCallback` fires (on init and
again before expiry). Set `MAPKIT_PRIVATE_KEY` + `MAPKIT_KEY_ID` +
`MAPKIT_TEAM_ID` for this self-refreshing behaviour. If you only have a
pre-issued `MAPKIT_JS_TOKEN`, it is served as-is but will stop working once it
expires.

## Production build

```bash
npm run build
node .output/server/index.mjs
```
