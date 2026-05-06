#ifndef IDFM_CROSSWALK_H
#define IDFM_CROSSWALK_H

/*
 * IDFM stop crosswalk:  SIRI StopPointRef (STIF:StopPoint:Q:XXXXX:)
 *                       ↔ {lat, lon, name} from IDFM "arrets" reference dataset.
 *
 * Why: PRIM Navitia and PRIM SIRI Lite use different identifier
 * namespaces. SIRI ETT publishes stops as STIF:StopPoint:Q:22091:
 * (operator code) but Navitia returns stop_area:IDFM:471348 (internal).
 * The "arrets" Opendatasoft dataset on data.iledefrance-mobilites.fr
 * publishes a 38k-entry table where `arrid` matches the SIRI stop
 * point ID and `arrgeopoint` gives the GPS coords directly. We index
 * it once at startup (lazy on first lookup) and use it to position
 * IDFM vehicles synthesized from SIRI ETT.
 */

typedef struct {
    char   arrid[16];
    double lon, lat;
    char   name[64];
    char   type[16];   /* "metro", "bus", "tram", "rail", ... */
} IdfmStop;

/* Lazy-load the entire crosswalk into memory (≈ 5MB resident).
   First call triggers an HTTP download (~3MB gzipped → 18MB JSON,
   ~3-5 seconds on a typical link). Subsequent calls are O(1). */
const IdfmStop *idfm_crosswalk_lookup(const char *arrid);

/* Force-load the crosswalk now. Returns 0 on success, -1 on failure.
   Useful to warm the cache during startup. Idempotent. */
int idfm_crosswalk_load(void);

/* Try to extract the arrid number from a SIRI Lite stop point ref like
   "STIF:StopPoint:Q:22091:" → "22091". Returns 1 if extracted, 0 otherwise. */
int idfm_extract_arrid_from_siri(const char *siri_stop_ref, char *out, int out_sz);

/* Number of entries currently loaded (0 if not loaded yet). */
int idfm_crosswalk_size(void);

#endif
