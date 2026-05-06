#include "idfm_crosswalk.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include "cJSON.h"

/* Forward decls — public helper from api.c */
extern char *nvt_http_get_bulk(const char *url);
#define http_get nvt_http_get_bulk

#define BUCKETS 16384  /* power of two for fast modulo */

typedef struct Entry {
    IdfmStop      stop;
    struct Entry *next;
} Entry;

static Entry          *g_buckets[BUCKETS];
static int             g_loaded   = 0;
static int             g_n_loaded = 0;
static pthread_mutex_t g_lock     = PTHREAD_MUTEX_INITIALIZER;

/* Fast hash for short numeric/alphanumeric strings. */
static unsigned int hash_str(const char *s)
{
    unsigned int h = 2166136261u;
    while (*s) {
        h ^= (unsigned char)*s++;
        h *= 16777619u;
    }
    return h & (BUCKETS - 1);
}

static void insert_locked(const char *arrid, double lon, double lat,
                          const char *name, const char *type)
{
    unsigned int idx = hash_str(arrid);
    Entry *e;

    /* Skip duplicates (some arrids appear twice in the dataset). */
    for (e = g_buckets[idx]; e; e = e->next) {
        if (strcmp(e->stop.arrid, arrid) == 0) return;
    }
    e = malloc(sizeof(*e));
    if (!e) return;
    memset(e, 0, sizeof(*e));
    snprintf(e->stop.arrid, sizeof(e->stop.arrid), "%s", arrid);
    e->stop.lon = lon;
    e->stop.lat = lat;
    snprintf(e->stop.name, sizeof(e->stop.name), "%s", name ? name : "");
    snprintf(e->stop.type, sizeof(e->stop.type), "%s", type ? type : "");
    e->next = g_buckets[idx];
    g_buckets[idx] = e;
    g_n_loaded++;
}

int idfm_crosswalk_load(void)
{
    char *raw;
    cJSON *root, *item;
    int rc = 0;

    pthread_mutex_lock(&g_lock);
    if (g_loaded) {
        pthread_mutex_unlock(&g_lock);
        return 0;
    }

    /* Bulk export endpoint — Opendatasoft serves gzip with libcurl auto-decompression. */
    raw = http_get("https://data.iledefrance-mobilites.fr/api/explore/v2.1/catalog/datasets/arrets/exports/json");
    if (!raw) {
        pthread_mutex_unlock(&g_lock);
        return -1;
    }

    root = cJSON_Parse(raw);
    free(raw);
    if (!root || !cJSON_IsArray(root)) {
        if (root) cJSON_Delete(root);
        pthread_mutex_unlock(&g_lock);
        return -1;
    }

    cJSON_ArrayForEach(item, root) {
        cJSON *arrid_n = cJSON_GetObjectItemCaseSensitive(item, "arrid");
        cJSON *geo     = cJSON_GetObjectItemCaseSensitive(item, "arrgeopoint");
        cJSON *name_n  = cJSON_GetObjectItemCaseSensitive(item, "arrname");
        cJSON *type_n  = cJSON_GetObjectItemCaseSensitive(item, "arrtype");
        cJSON *lon_n, *lat_n;
        const char *arrid;

        if (!cJSON_IsString(arrid_n) || !geo) continue;
        lon_n = cJSON_GetObjectItemCaseSensitive(geo, "lon");
        lat_n = cJSON_GetObjectItemCaseSensitive(geo, "lat");
        if (!cJSON_IsNumber(lon_n) || !cJSON_IsNumber(lat_n)) continue;

        arrid = arrid_n->valuestring;
        if (!arrid || !arrid[0]) continue;

        insert_locked(arrid,
                      lon_n->valuedouble, lat_n->valuedouble,
                      cJSON_IsString(name_n) ? name_n->valuestring : "",
                      cJSON_IsString(type_n) ? type_n->valuestring : "");
    }

    cJSON_Delete(root);
    g_loaded = 1;
    pthread_mutex_unlock(&g_lock);
    fprintf(stderr, "[idfm_crosswalk] loaded %d stops\n", g_n_loaded);
    return rc;
}

const IdfmStop *idfm_crosswalk_lookup(const char *arrid)
{
    unsigned int idx;
    Entry *e;

    if (!arrid || !arrid[0]) return NULL;
    if (!g_loaded) {
        if (idfm_crosswalk_load() < 0) return NULL;
    }
    idx = hash_str(arrid);
    for (e = g_buckets[idx]; e; e = e->next) {
        if (strcmp(e->stop.arrid, arrid) == 0) return &e->stop;
    }
    return NULL;
}

int idfm_extract_arrid_from_siri(const char *siri_stop_ref, char *out, int out_sz)
{
    /* Format: "STIF:StopPoint:Q:22091:" — extract the digits between Q: and trailing : */
    const char *p;
    int n = 0;

    if (!siri_stop_ref || !out || out_sz <= 1) return 0;
    out[0] = '\0';

    p = strstr(siri_stop_ref, "Q:");
    if (!p) {
        /* Try generic last-numeric-segment fallback */
        const char *q = strrchr(siri_stop_ref, ':');
        if (!q || q == siri_stop_ref) return 0;
        const char *r = q - 1;
        while (r > siri_stop_ref && r[-1] != ':' && r[-1] != ' ') r--;
        for (; r < q && n < out_sz - 1; r++) {
            if (*r >= '0' && *r <= '9') out[n++] = *r;
        }
        out[n] = '\0';
        return n > 0;
    }
    p += 2;
    while (*p && *p != ':' && n < out_sz - 1) {
        if (*p >= '0' && *p <= '9') out[n++] = *p;
        p++;
    }
    out[n] = '\0';
    return n > 0;
}

int idfm_crosswalk_size(void)
{
    return g_loaded ? g_n_loaded : 0;
}
