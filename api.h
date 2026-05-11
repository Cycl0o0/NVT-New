#ifndef API_H
#define API_H

#include <stdbool.h>
#include "config.h"

typedef struct {
    int gid;
    char libelle[64];
    int ident;
    char vehicule[16];
    bool active;
    bool sae;
} Line;

typedef struct {
    int gid;
    char libelle[64];
    double lon, lat;
} Stop;

typedef struct {
    int gid;
    double lon, lat;
    char etat[16];
    int retard;
    int vitesse;
    char vehicule[16];
    char statut[16];
    char sens[16];
    char terminus[64];
    bool arret;
    int arret_actu;
    int arret_suiv;
    int ligne_id;
} Vehicle;

typedef struct {
    int gid;
    char titre[128];
    char message[512];
    char severite[16];
    int ligne_id;
} Alert;

/* Hashmap for stop GID -> name lookup */
typedef struct StopEntry {
    int gid;
    char libelle[64];
    double lon, lat;
    struct StopEntry *next;
} StopEntry;

#define STOP_BUCKETS 4096

typedef struct {
    StopEntry *buckets[STOP_BUCKETS];
} StopMap;

/* ── Stop groups (arrêts groupés par nom) ─────────────────────────── */

typedef struct {
    char libelle[64];
    char groupe[32];
    int gids[16];
    int ngids;
} StopGroup;

/* ── Passages temps réel ──────────────────────────────────────────── */

typedef struct {
    char hor_estime[6];  /* "HH:MM" */
    char hor_theo[6];    /* "HH:MM" */
    int cours_id;
    int ligne_id;        /* résolu via sv_cours_a */
    int terminus_gid;    /* résolu via sv_cours_a */
} Passage;

typedef struct {
    int id;
    char ref[96];
    char code[16];
    char libelle[96];
    char mode[24];
    char couleur[32];
    char texte_couleur[16];
    int r, g, b;
    int terminus_count;
    char terminus_refs[8][96];
    char terminus_names[8][64];
} ToulouseLine;

typedef struct {
    int id;
    char ref[96];
    char libelle[96];
    char adresse[96];
    char commune[32];
    char lignes[128];
    char mode[32];
    double lon, lat;
} ToulouseStop;

typedef struct {
    char id[96];
    char titre[128];
    char message[2048];
    char importance[24];
    char scope[24];
    char lines[128];
} ToulouseAlert;

typedef struct {
    char line_code[16];
    char line_name[96];
    char destination[96];
    char stop_name[96];
    char datetime[20];
    char waiting_time[16];
    int realtime;
    int delayed;
} ToulousePassage;

typedef struct {
    char ref[96];
    char line_code[16];
    char line_name[96];
    char current_stop[96];
    char next_stop[96];
    char terminus[96];
    char sens[16];
    char datetime[20];
    char waiting_time[16];
    int realtime;
    int delayed;
    int vitesse;
    int arret;
    /* Real-time GPS — 0/0 means "not provided by upstream" */
    double lon, lat;
    int    bearing;     /* heading in degrees, -1 if unknown */
    int    has_position;
} ToulouseVehicle;

typedef struct {
    int total_lines;
    int total_stops;
    int sample_lines;
    int sample_stops;
    int live;
    char lines_url[192];
    char stops_url[192];
    char doc_ref[64];
    char doc_version[16];
    char doc_date[16];
} ToulouseSnapshot;

typedef struct {
    int total_lines;
    int total_stops;
    int total_alerts;
    int sample_lines;
    int sample_stops;
    int sample_alerts;
    int live;
    char lines_url[192];
    char stops_url[192];
    char alerts_url[192];
    char doc_ref[128];
    char doc_version[16];
    char doc_date[16];
} IdfmSnapshot;

/* ── Cache cours_id → (ligne_id, terminus_gid) ───────────────────── */

typedef struct CourseEntry {
    int gid;
    int ligne_id;
    int terminus_gid;
    struct CourseEntry *next;
} CourseEntry;

typedef struct {
    CourseEntry *buckets[COURSE_BUCKETS];
} CourseCache;

enum {
    MAP_KIND_BOUNDARY = 1,
    MAP_KIND_ROUTE_ALLER,
    MAP_KIND_ROUTE_RETOUR,
    MAP_KIND_ROAD,
    MAP_KIND_RAIL,
    MAP_KIND_WATER,
};

typedef struct {
    double lon, lat;
} MapPoint;

typedef struct {
    int start;
    int count;
    unsigned char kind;
} MapPath;

typedef struct {
    double lon, lat;
    char name[24];
    unsigned char rank;
} MapLabel;

typedef struct {
    double minlon, minlat;
    double maxlon, maxlat;
    MapPoint points[MAX_MAP_POINTS];
    MapPath paths[MAX_MAP_PATHS];
    MapLabel labels[MAX_MAP_LABELS];
    int npoints;
    int npaths;
    int nlabels;
} MetroMap;

typedef struct {
    MapPoint points[MAX_ROUTE_POINTS];
    MapPath paths[MAX_ROUTE_PATHS];
    int npoints;
    int npaths;
    int aller_paths;
    int retour_paths;
} LineRouteMap;

typedef struct {
    double minlon, minlat;
    double maxlon, maxlat;
    MapPoint points[MAX_ATLAS_POINTS];
    MapPath paths[MAX_ATLAS_PATHS];
    int npoints;
    int npaths;
    int road_paths;
    int rail_paths;
    int water_paths;
} AtlasMap;

typedef struct {
    MapPoint points[MAX_ATLAS_ROUTE_POINTS];
    MapPath paths[MAX_ATLAS_ROUTE_PATHS];
    int line_gids[MAX_ATLAS_ROUTE_PATHS];
    int npoints;
    int npaths;
} AtlasRoutes;

void      api_init(void);
void      api_cleanup(void);
const char *api_user_agent(void);

int       fetch_lines(Line *out, int max);
int       fetch_stops(StopMap *map);
int       fetch_vehicles(int line_gid, Vehicle *out, int max);
int       fetch_alerts(Alert *out, int max);

void      stopmap_init(StopMap *m);
void      stopmap_free(StopMap *m);
const char *stopmap_lookup(const StopMap *m, int gid);
bool      stopmap_lookup_pos(const StopMap *m, int gid, double *lon, double *lat);

int       fetch_stop_groups(StopGroup *out, int max);
int       fetch_passages(int stop_gid, Passage *out, int max, CourseCache *cc);
void      course_cache_init(CourseCache *cc);
void      course_cache_free(CourseCache *cc);
int       fetch_toulouse_snapshot(ToulouseSnapshot *snap, ToulouseLine *lines, int max_lines,
                                  ToulouseStop *stops, int max_stops);
/* Provide the cached Tisseo stops list to fetch_toulouse_vehicles so it
   can synthesize positions from real-time stop schedules. Callers should
   set this once after fetch_toulouse_snapshot completes. */
void      nvt_set_toulouse_vehicle_stops(const ToulouseStop *stops, int count);
int       fetch_toulouse_alerts(ToulouseAlert *out, int max);
int       fetch_toulouse_passages(const char *stop_area_ref, ToulousePassage *out, int max);
int       fetch_toulouse_vehicles(const ToulouseLine *line, ToulouseVehicle *out, int max);
int       fetch_idfm_snapshot(IdfmSnapshot *snap, ToulouseLine *lines, int max_lines);
int       fetch_idfm_line_stops(const ToulouseLine *line, ToulouseStop *out, int max);
int       fetch_idfm_alerts(ToulouseAlert *out, int max);
int       fetch_idfm_passages(const ToulouseLine *line, const ToulouseStop *stop, ToulousePassage *out, int max);
int       fetch_idfm_vehicles(const ToulouseLine *line, ToulouseVehicle *out, int max);
/* Provide cached IDFM line stops so fetch_idfm_vehicles can synthesize
   vehicle positions from PRIM SIRI ETT. Set after fetch_idfm_line_stops. */
void      nvt_set_idfm_vehicle_stops(const ToulouseStop *stops, int count);
int       fetch_star_snapshot(IdfmSnapshot *snap, ToulouseLine *lines, int max_lines);
int       fetch_star_line_stops(const ToulouseLine *line, ToulouseStop *out, int max);
int       fetch_star_alerts(ToulouseAlert *out, int max);
int       fetch_star_passages(const ToulouseLine *line, const ToulouseStop *stop, ToulousePassage *out, int max);
int       fetch_star_vehicles(const ToulouseLine *line, ToulouseVehicle *out, int max);
int       fetch_sncf_snapshot(IdfmSnapshot *snap, ToulouseLine *lines, int max_lines);
int       fetch_sncf_line_stops(const ToulouseLine *line, ToulouseStop *out, int max);
int       fetch_sncf_alerts(ToulouseAlert *out, int max);
int       fetch_sncf_passages(const ToulouseLine *line, const ToulouseStop *stop, ToulousePassage *out, int max);
int       fetch_sncf_vehicles(const ToulouseLine *line, ToulouseVehicle *out, int max);
int       fetch_toulouse_line_route(const ToulouseLine *line, LineRouteMap *out);
int       fetch_toulouse_metro_map(MetroMap *out);
int       fetch_metro_map(MetroMap *out);
int       fetch_line_route(int line_gid, LineRouteMap *out);
int       fetch_atlas_map(AtlasMap *out);
int       fetch_detail_map(double minlon, double maxlon, double minlat, double maxlat, int zoom, AtlasMap *out);
int       fetch_atlas_routes(AtlasRoutes *out);

#endif
