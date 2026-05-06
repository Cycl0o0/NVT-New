#ifndef CONFIG_H
#define CONFIG_H

#define API_KEY       "0234ABEFGH"
#define API_BASE      "https://data.bordeaux-metropole.fr/geojson"
#define TISSEO_API_KEY "8ee7d45b-a219-4f42-93fa-6c098df58b87"
#define TISSEO_API_BASE "https://api.tisseo.fr/v2"
#define TISSEO_NETWORK_ENCODED "Tiss%C3%A9o"
#define IDFM_API_KEY "nAtkIh7qfelk1sUWawKqdigIKVY0mKvY"
#define IDFM_API_BASE "https://prim.iledefrance-mobilites.fr/marketplace/v2/navitia"
#define IDFM_LINE_REPORTS_BASE "https://prim.iledefrance-mobilites.fr/marketplace/v2/navitia/line_reports"
#define SNCF_API_KEY "538d62fe-af52-4ec5-9a90-97b20b7dd733"
#define SNCF_API_BASE "https://api.sncf.com/v1"
#define SNCF_COVERAGE "sncf"
/* STAR — Rennes / Métropole. Public Opendatasoft API, no auth required. */
#define STAR_API_BASE "https://data.explore.star.fr/api/explore/v2.1/catalog/datasets"
/* TCL — Lyon / Sytral. WFS GeoJSON for static lines/stops (no auth);
   datapusher REST API for real-time passages requires HTTP Basic auth
   with a free moncompte.grandlyon.com account. Set TCL_USER + TCL_PASS
   in .nvt-backend.env. */
#define TCL_WFS_BASE        "https://download.data.grandlyon.com/wfs/rdata"
#define TCL_DATAPUSHER_BASE "https://data.grandlyon.com/fr/datapusher/ws/rdata"
#define MAP_API_BASE  "https://geo.api.gouv.fr"
#define OVERPASS_API_BASE "https://overpass-api.de/api/interpreter"
#define MAP_BDX_EPCI  "243300316"
#define MAP_TLS_EPCI  "243100518"
#define NVT_VERSION   "V2.0"
#define USER_AGENT_FMT "NVT/%s  %s/%s %s/%s"
#define USER_AGENT_MAX 192

#define MAX_LINES     4096
#define MAX_STOPS     8192
#define MAX_VEHICLES  512
#define MAX_ALERTS    2048
#define MAX_PASSAGES  512
#define MAX_STOP_GROUPS 2048
#define COURSE_BUCKETS  512
#define MAX_MAP_POINTS   32768
#define MAX_MAP_PATHS    4096
#define MAX_MAP_LABELS   48
#define MAX_ROUTE_POINTS 32768
#define MAX_ROUTE_PATHS  4096
#define MAX_ATLAS_POINTS 262144
#define MAX_ATLAS_PATHS  16384
#define MAX_ATLAS_ROUTE_POINTS 262144
#define MAX_ATLAS_ROUTE_PATHS  16384

#define MAP_BDX_SOUTH 44.72
#define MAP_BDX_WEST  -0.82
#define MAP_BDX_NORTH 45.02
#define MAP_BDX_EAST  -0.36

#define VEHICLE_REFRESH_SEC  10
#define ALERT_REFRESH_SEC    60

#endif
