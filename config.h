#ifndef CONFIG_H
#define CONFIG_H

#define API_KEY       "0234ABEFGH"
#define API_BASE      "https://data.bordeaux-metropole.fr/geojson"
#define TISSEO_API_KEY "8ee7d45b-a219-4f42-93fa-6c098df58b87"
#define TISSEO_API_BASE "https://api.tisseo.fr/v2"
#define TISSEO_NETWORK_ENCODED "Tiss%C3%A9o"
#define MAP_API_BASE  "https://geo.api.gouv.fr"
#define OVERPASS_API_BASE "https://overpass-api.de/api/interpreter"
#define MAP_BDX_EPCI  "243300316"
#define MAP_TLS_EPCI  "243100518"
#define NVT_VERSION   "1.0"
#define USER_AGENT_FMT "NVT/%s  %s/%s %s/%s"
#define USER_AGENT_MAX 192

#define MAX_LINES     256
#define MAX_STOPS     8192
#define MAX_VEHICLES  512
#define MAX_ALERTS    128
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
