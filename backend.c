#include "api.h"
#include "config.h"
#include "cJSON.h"
#include "line_colors.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define BACKEND_VERSION "V2.0"
#define DEFAULT_PORT 8080
#define REQ_BUF_SIZE 8192

typedef struct {
    char method[8];
    char path[512];
    char query[1024];
} HttpRequest;

typedef enum {
    NETWORK_BDX = 0,
    NETWORK_TLS = 1,
    NETWORK_IDFM = 2,
    NETWORK_SNCF = 3,
    NETWORK_STAR = 4,
} NetworkKind;

static volatile sig_atomic_t g_running = 1;
static int g_server_fd = -1;
static StopMap g_stop_map;
static int g_stop_map_loaded = 0;
static StopGroup g_stop_groups[MAX_STOP_GROUPS];
static int g_nstop_groups = -1;
static CourseCache g_course_cache;
static int g_course_cache_ready = 0;
static MetroMap g_metro_map;
static int g_has_metro_map = 0;
static ToulouseSnapshot g_tls_snapshot;
static ToulouseLine g_tls_lines[MAX_LINES];
static int g_tls_nlines = -1;
static ToulouseStop g_tls_stops[MAX_STOPS];
static int g_tls_nstops = -1;
static MetroMap g_tls_metro_map;
static int g_tls_has_metro_map = 0;
static IdfmSnapshot g_idfm_snapshot;
static ToulouseLine g_idfm_lines[MAX_LINES];
static int g_idfm_nlines = -1;
static IdfmSnapshot g_sncf_snapshot;
static ToulouseLine g_sncf_lines[MAX_LINES];
static int g_sncf_nlines = -1;
static IdfmSnapshot g_star_snapshot;
static ToulouseLine g_star_lines[MAX_LINES];
static int g_star_nlines = -1;

static void on_signal(int signum)
{
    (void)signum;
    g_running = 0;
    if (g_server_fd >= 0) {
        close(g_server_fd);
        g_server_fd = -1;
    }
}

static void on_sigpipe(int signum)
{
    (void)signum;
}

static void iso_now(char *buf, size_t sz)
{
    time_t now = time(NULL);
    struct tm tmv;
    gmtime_r(&now, &tmv);
    strftime(buf, sz, "%Y-%m-%dT%H:%M:%SZ", &tmv);
}

static void log_msg(const char *fmt, ...)
{
    char stamp[32];
    va_list args;
    time_t now = time(NULL);
    struct tm tmv;

    localtime_r(&now, &tmv);
    strftime(stamp, sizeof(stamp), "%H:%M:%S", &tmv);
    fprintf(stderr, "[%s] ", stamp);
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fputc('\n', stderr);
}

static const char *network_slug(NetworkKind network)
{
    switch (network) {
    case NETWORK_TLS:
        return "toulouse";
    case NETWORK_IDFM:
        return "idfm";
    case NETWORK_SNCF:
        return "sncf";
    case NETWORK_STAR:
        return "star";
    case NETWORK_BDX:
    default:
        return "bordeaux";
    }
}

static int network_is_live(NetworkKind network)
{
    return network == NETWORK_IDFM
        || network == NETWORK_SNCF
        || network == NETWORK_STAR;
}

static int network_supports_boundaries(NetworkKind network)
{
    return network == NETWORK_BDX || network == NETWORK_TLS;
}

static int network_supports_route(NetworkKind network)
{
    return network == NETWORK_BDX || network == NETWORK_TLS;
}

static int query_param_get(const char *query, const char *key, char *out, size_t out_sz)
{
    size_t key_len;
    const char *p;

    if (!out_sz) return 0;
    out[0] = '\0';
    if (!query || !query[0] || !key || !key[0]) return 0;

    key_len = strlen(key);
    p = query;
    while (*p) {
        const char *amp = strchr(p, '&');
        size_t len = amp ? (size_t)(amp - p) : strlen(p);

        if (len > key_len && strncmp(p, key, key_len) == 0 && p[key_len] == '=') {
            size_t vlen = len - key_len - 1;
            if (vlen >= out_sz) vlen = out_sz - 1;
            memcpy(out, p + key_len + 1, vlen);
            out[vlen] = '\0';
            return 1;
        }

        if (!amp) break;
        p = amp + 1;
    }

    return 0;
}

static int request_network(const HttpRequest *req, NetworkKind *out)
{
    char value[32];

    if (!out) return -1;
    *out = NETWORK_BDX;
    if (!req) return 0;
    if (!query_param_get(req->query, "network", value, sizeof(value))) return 0;

    if (strcasecmp(value, "bordeaux") == 0 || strcasecmp(value, "bdx") == 0 ||
        strcasecmp(value, "tbm") == 0) {
        *out = NETWORK_BDX;
        return 0;
    }
    if (strcasecmp(value, "toulouse") == 0 || strcasecmp(value, "tls") == 0 ||
        strcasecmp(value, "tisseo") == 0) {
        *out = NETWORK_TLS;
        return 0;
    }
    if (strcasecmp(value, "idfm") == 0 || strcasecmp(value, "idf") == 0 ||
        strcasecmp(value, "paris") == 0 || strcasecmp(value, "paris-idfm") == 0 ||
        strcasecmp(value, "iledefrance") == 0 || strcasecmp(value, "ile-de-france") == 0) {
        *out = NETWORK_IDFM;
        return 0;
    }
    if (strcasecmp(value, "sncf") == 0) {
        *out = NETWORK_SNCF;
        return 0;
    }
    if (strcasecmp(value, "star") == 0 || strcasecmp(value, "rennes") == 0) {
        *out = NETWORK_STAR;
        return 0;
    }

    return -1;
}

static int query_param_get_int(const char *query, const char *key, int *out)
{
    char value[64];
    char *end = NULL;
    long parsed;

    if (!out) return -1;
    if (!query_param_get(query, key, value, sizeof(value))) return 0;

    errno = 0;
    parsed = strtol(value, &end, 10);
    if (errno || !end || *end != '\0' || parsed < 0 || parsed > INT_MAX) return -1;
    *out = (int)parsed;
    return 1;
}

static int request_line_gid(const HttpRequest *req, int *out)
{
    int rc;

    if (!req || !out) return -1;

    rc = query_param_get_int(req->query, "line", out);
    if (rc != 0) return rc;
    rc = query_param_get_int(req->query, "lineId", out);
    if (rc != 0) return rc;
    return query_param_get_int(req->query, "lineGid", out);
}

static int match_line_child_path(const char *path, const char *suffix, int *out_gid)
{
    static const char *prefix = "/api/lines/";
    const char *cursor;
    char *end = NULL;
    long parsed;

    if (!path || !suffix || !out_gid) return 0;
    if (strncmp(path, prefix, strlen(prefix)) != 0) return 0;

    cursor = path + strlen(prefix);
    if (!*cursor) return 0;

    errno = 0;
    parsed = strtol(cursor, &end, 10);
    if (errno || end == cursor || parsed < 0 || parsed > INT_MAX) return 0;
    if (strcmp(end, suffix) != 0) return 0;

    *out_gid = (int)parsed;
    return 1;
}

static int hex_nibble(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static void url_decode_component(const char *src, char *dst, size_t dst_sz)
{
    size_t j = 0;

    if (!dst || !dst_sz) return;
    dst[0] = '\0';
    if (!src) return;

    for (size_t i = 0; src[i] && j + 1 < dst_sz; i++) {
        if (src[i] == '%' && src[i + 1] && src[i + 2]) {
            int hi = hex_nibble(src[i + 1]);
            int lo = hex_nibble(src[i + 2]);

            if (hi >= 0 && lo >= 0) {
                dst[j++] = (char)((hi << 4) | lo);
                i += 2;
                continue;
            }
        }

        dst[j++] = src[i] == '+' ? ' ' : src[i];
    }

    dst[j] = '\0';
}

static void add_network_to_root(cJSON *root, NetworkKind network)
{
    cJSON_AddStringToObject(root, "network", network_slug(network));
}

static int count_active_lines(const Line *lines, int n)
{
    int total = 0;
    for (int i = 0; i < n; i++) {
        if (lines[i].active) total++;
    }
    return total;
}

static int count_lines_type(const Line *lines, int n, const char *type)
{
    int total = 0;
    for (int i = 0; i < n; i++) {
        if (lines[i].active && strcmp(lines[i].vehicule, type) == 0) total++;
    }
    return total;
}

static int count_alert_prefix(const Alert *alerts, int n, const char *prefix)
{
    int total = 0;
    size_t prefix_len = strlen(prefix);
    for (int i = 0; i < n; i++) {
        if (strncmp(alerts[i].severite, prefix, prefix_len) == 0) total++;
    }
    return total;
}

static int count_alerted_lines(const Alert *alerts, int nalerts)
{
    int seen[MAX_LINES];
    int nseen = 0;

    for (int i = 0; i < nalerts; i++) {
        int dup = 0;
        if (!alerts[i].ligne_id) continue;
        for (int j = 0; j < nseen; j++) {
            if (seen[j] == alerts[i].ligne_id) {
                dup = 1;
                break;
            }
        }
        if (!dup && nseen < MAX_LINES) seen[nseen++] = alerts[i].ligne_id;
    }
    return nseen;
}

static int cmp_lines(const void *a, const void *b)
{
    const Line *la = a;
    const Line *lb = b;
    int ta = strcmp(la->vehicule, "TRAM") == 0 ? 0 : strcmp(la->vehicule, "BUS") == 0 ? 1 : 2;
    int tb = strcmp(lb->vehicule, "TRAM") == 0 ? 0 : strcmp(lb->vehicule, "BUS") == 0 ? 1 : 2;
    return ta != tb ? ta - tb : la->ident - lb->ident;
}

static void line_code(const Line *line, char *buf, size_t sz)
{
    if (strcmp(line->vehicule, "TRAM") == 0 && line->libelle[5]) {
        const char *p = strrchr(line->libelle, ' ');
        snprintf(buf, sz, "%s", p ? p + 1 : "?");
        return;
    }
    snprintf(buf, sz, "%d", line->ident);
}

static int cmp_toulouse_lines(const void *a, const void *b)
{
    const ToulouseLine *la = a;
    const ToulouseLine *lb = b;
    return strcasecmp(la->code, lb->code) != 0
         ? strcasecmp(la->code, lb->code)
         : strcasecmp(la->libelle, lb->libelle);
}

static int cmp_toulouse_stops(const void *a, const void *b)
{
    const ToulouseStop *sa = a;
    const ToulouseStop *sb = b;
    return strcasecmp(sa->libelle, sb->libelle);
}

static const ToulouseLine *find_toulouse_line_by_id(const ToulouseLine *lines, int n, int id)
{
    for (int i = 0; i < n; i++) {
        if (lines[i].id == id) return &lines[i];
    }
    return NULL;
}

static const ToulouseLine *find_toulouse_line_by_code(const ToulouseLine *lines, int n, const char *code)
{
    if (!code || !code[0]) return NULL;
    for (int i = 0; i < n; i++) {
        if (strcmp(lines[i].code, code) == 0) return &lines[i];
    }
    return NULL;
}

static const ToulouseStop *find_toulouse_stop_by_key(const char *key)
{
    if (!key || !key[0] || g_tls_nstops < 0) return NULL;
    for (int i = 0; i < g_tls_nstops; i++) {
        if (strcmp(g_tls_stops[i].ref, key) == 0) return &g_tls_stops[i];
    }
    return NULL;
}

static const ToulouseStop *find_live_stop_by_key(const ToulouseStop *stops, int n, const char *key)
{
    if (!key || !key[0] || !stops || n <= 0) return NULL;
    for (int i = 0; i < n; i++) {
        if (strcmp(stops[i].ref, key) == 0) return &stops[i];
    }
    return NULL;
}

static int count_toulouse_lines_type(const ToulouseLine *lines, int n, const char *type)
{
    int total = 0;
    for (int i = 0; i < n; i++) {
        if (strcasecmp(lines[i].mode, type) == 0) total++;
    }
    return total;
}

static int line_mode_is_bus(const char *mode)
{
    if (!mode || !mode[0]) return 0;
    return strcasestr(mode, "bus") != NULL || strcasestr(mode, "coach") != NULL;
}

static int line_mode_is_rail(const char *mode)
{
    if (!mode || !mode[0]) return 0;
    return strcasestr(mode, "metro") != NULL
        || strcasestr(mode, "tram") != NULL
        || strcasestr(mode, "train") != NULL
        || strcasestr(mode, "rer") != NULL
        || strcasestr(mode, "rail") != NULL
        || strcasestr(mode, "funicular") != NULL;
}

static int count_live_lines_bus(const ToulouseLine *lines, int n)
{
    int total = 0;
    for (int i = 0; i < n; i++) {
        if (line_mode_is_bus(lines[i].mode)) total++;
    }
    return total;
}

static int count_live_lines_rail(const ToulouseLine *lines, int n)
{
    int total = 0;
    for (int i = 0; i < n; i++) {
        if (line_mode_is_rail(lines[i].mode)) total++;
    }
    return total;
}

static int toulouse_alert_level(const ToulouseAlert *alert)
{
    const char *importance = alert ? alert->importance : "";

    if (!importance[0]) return 1;
    if (isdigit((unsigned char)importance[0])) return atoi(importance);
    if (strcasecmp(importance, "high") == 0 || strcasecmp(importance, "critical") == 0) return 3;
    if (strcasecmp(importance, "medium") == 0 || strcasecmp(importance, "warning") == 0) return 2;
    return 1;
}

static int count_toulouse_alert_level(const ToulouseAlert *alerts, int n, int level)
{
    int total = 0;
    for (int i = 0; i < n; i++) {
        if (toulouse_alert_level(&alerts[i]) == level) total++;
    }
    return total;
}

static int count_toulouse_alerted_lines(const ToulouseAlert *alerts, int nalerts)
{
    char seen[MAX_LINES][16];
    int nseen = 0;

    for (int i = 0; i < nalerts; i++) {
        char codes[128];
        char *token;
        char *saveptr = NULL;

        snprintf(codes, sizeof(codes), "%s", alerts[i].lines);
        token = strtok_r(codes, " ", &saveptr);
        while (token) {
            int dup = 0;
            for (int j = 0; j < nseen; j++) {
                if (strcmp(seen[j], token) == 0) {
                    dup = 1;
                    break;
                }
            }
            if (!dup && nseen < MAX_LINES) {
                snprintf(seen[nseen], sizeof(seen[nseen]), "%s", token);
                nseen++;
            }
            token = strtok_r(NULL, " ", &saveptr);
        }
    }

    return nseen;
}

static void add_line_codes_array(cJSON *obj, const char *codes)
{
    cJSON *items = cJSON_CreateArray();
    char copy[128];
    char *token;
    char *saveptr = NULL;

    snprintf(copy, sizeof(copy), "%s", codes ? codes : "");
    token = strtok_r(copy, " ", &saveptr);
    while (token) {
        cJSON_AddItemToArray(items, cJSON_CreateString(token));
        token = strtok_r(NULL, " ", &saveptr);
    }
    cJSON_AddItemToObject(obj, "lineCodes", items);
}

static const ToulouseLine *primary_alert_line(const ToulouseAlert *alert,
                                              const ToulouseLine *lines,
                                              int nlines)
{
    char copy[128];
    char *token;
    char *saveptr = NULL;

    if (!alert || !alert->lines[0] || !lines || nlines <= 0) return NULL;
    snprintf(copy, sizeof(copy), "%s", alert->lines);
    token = strtok_r(copy, " ", &saveptr);
    while (token) {
        const ToulouseLine *line = find_toulouse_line_by_code(lines, nlines, token);
        if (line) return line;
        token = strtok_r(NULL, " ", &saveptr);
    }
    return NULL;
}

static int toulouse_alert_has_line(const ToulouseAlert *alert, const ToulouseLine *line)
{
    char copy[128];
    char *token;
    char *saveptr = NULL;

    if (!alert || !line || !line->code[0] || !alert->lines[0]) return 0;
    snprintf(copy, sizeof(copy), "%s", alert->lines);
    token = strtok_r(copy, " ", &saveptr);
    while (token) {
        if (strcmp(token, line->code) == 0) return 1;
        token = strtok_r(NULL, " ", &saveptr);
    }
    return 0;
}

static void add_toulouse_line_color_fields(cJSON *obj, const ToulouseLine *line)
{
    if (!line) return;
    if (line->couleur[0]) cJSON_AddStringToObject(obj, "colorBg", line->couleur);
    if (line->texte_couleur[0]) cJSON_AddStringToObject(obj, "colorFg", line->texte_couleur);
}

static void extract_hhmm(const char *datetime, char *out, size_t sz)
{
    const char *space;

    if (!out || !sz) return;
    out[0] = '\0';
    if (!datetime || !datetime[0]) return;

    space = strchr(datetime, 'T');
    if (!space) space = strchr(datetime, ' ');
    if (!space) return;
    space++;
    if (strlen(space) >= 5) snprintf(out, sz, "%.5s", space);
}

static const LineColor *lookup_line_color(int ident)
{
    for (int i = 0; i < N_LINE_COLORS; i++) {
        if (line_colors[i].ident == ident) return &line_colors[i];
    }
    return NULL;
}

static const Line *find_line_by_gid(const Line *lines, int n, int gid)
{
    for (int i = 0; i < n; i++) {
        if (lines[i].gid == gid) return &lines[i];
    }
    return NULL;
}

static const StopGroup *find_stop_group_by_key(const char *key)
{
    char wanted[32];
    if (!key || !*key || g_nstop_groups < 0) return NULL;
    for (int i = 0; i < g_nstop_groups; i++) {
        snprintf(wanted, sizeof(wanted), "group-%d", g_stop_groups[i].gids[0]);
        if (strcmp(wanted, key) == 0) return &g_stop_groups[i];
    }
    return NULL;
}

static int ensure_stop_map(void)
{
    if (g_stop_map_loaded) return 0;
    stopmap_init(&g_stop_map);
    if (fetch_stops(&g_stop_map) < 0) return -1;
    g_stop_map_loaded = 1;
    return 0;
}

static int ensure_stop_groups(void)
{
    if (g_nstop_groups >= 0) return 0;
    g_nstop_groups = fetch_stop_groups(g_stop_groups, MAX_STOP_GROUPS);
    return g_nstop_groups >= 0 ? 0 : -1;
}

static int ensure_course_cache(void)
{
    if (g_course_cache_ready) return 0;
    course_cache_init(&g_course_cache);
    g_course_cache_ready = 1;
    return 0;
}

static int ensure_metro_map(void)
{
    if (g_has_metro_map) return 0;
    if (fetch_metro_map(&g_metro_map) > 0) {
        g_has_metro_map = 1;
        return 0;
    }
    return -1;
}

static int ensure_toulouse_snapshot(void)
{
    if (g_tls_nlines >= 0 && g_tls_nstops >= 0) return 0;
    if (fetch_toulouse_snapshot(&g_tls_snapshot, g_tls_lines, MAX_LINES, g_tls_stops, MAX_STOPS) < 0) {
        return -1;
    }
    g_tls_nlines = g_tls_snapshot.sample_lines;
    g_tls_nstops = g_tls_snapshot.sample_stops;
    if (g_tls_nlines > 1) qsort(g_tls_lines, (size_t)g_tls_nlines, sizeof(ToulouseLine), cmp_toulouse_lines);
    if (g_tls_nstops > 1) qsort(g_tls_stops, (size_t)g_tls_nstops, sizeof(ToulouseStop), cmp_toulouse_stops);
    /* Wire the cached stops into the vehicle synthesis path */
    nvt_set_toulouse_vehicle_stops(g_tls_stops, g_tls_nstops);
    return 0;
}

static int ensure_toulouse_metro_map(void)
{
    if (g_tls_has_metro_map) return 0;
    if (fetch_toulouse_metro_map(&g_tls_metro_map) > 0) {
        g_tls_has_metro_map = 1;
        return 0;
    }
    return -1;
}

static int ensure_idfm_snapshot(void)
{
    if (g_idfm_nlines >= 0) return 0;
    if (fetch_idfm_snapshot(&g_idfm_snapshot, g_idfm_lines, MAX_LINES) < 0) return -1;
    g_idfm_nlines = g_idfm_snapshot.sample_lines;
    if (g_idfm_nlines > 1) {
        qsort(g_idfm_lines, (size_t)g_idfm_nlines, sizeof(ToulouseLine), cmp_toulouse_lines);
    }
    return 0;
}

static int ensure_sncf_snapshot(void)
{
    if (g_sncf_nlines >= 0) return 0;
    if (fetch_sncf_snapshot(&g_sncf_snapshot, g_sncf_lines, MAX_LINES) < 0) return -1;
    g_sncf_nlines = g_sncf_snapshot.sample_lines;
    if (g_sncf_nlines > 1) {
        qsort(g_sncf_lines, (size_t)g_sncf_nlines, sizeof(ToulouseLine), cmp_toulouse_lines);
    }
    return 0;
}

static int ensure_star_snapshot(void)
{
    if (g_star_nlines >= 0) return 0;
    if (fetch_star_snapshot(&g_star_snapshot, g_star_lines, MAX_LINES) < 0) return -1;
    g_star_nlines = g_star_snapshot.sample_lines;
    if (g_star_nlines > 1) {
        qsort(g_star_lines, (size_t)g_star_nlines, sizeof(ToulouseLine), cmp_toulouse_lines);
    }
    return 0;
}

static const ToulouseLine *network_live_lines(NetworkKind network, int *out_count)
{
    if (out_count) *out_count = 0;

    switch (network) {
    case NETWORK_TLS:
        if (ensure_toulouse_snapshot() < 0) return NULL;
        if (out_count) *out_count = g_tls_nlines;
        return g_tls_lines;
    case NETWORK_IDFM:
        if (ensure_idfm_snapshot() < 0) return NULL;
        if (out_count) *out_count = g_idfm_nlines;
        return g_idfm_lines;
    case NETWORK_SNCF:
        if (ensure_sncf_snapshot() < 0) return NULL;
        if (out_count) *out_count = g_sncf_nlines;
        return g_sncf_lines;
    case NETWORK_STAR:
        if (ensure_star_snapshot() < 0) return NULL;
        if (out_count) *out_count = g_star_nlines;
        return g_star_lines;
    case NETWORK_BDX:
    default:
        return NULL;
    }
}

static const ToulouseLine *find_network_live_line_by_id(NetworkKind network, int line_gid)
{
    int nlines = 0;
    const ToulouseLine *lines = network_live_lines(network, &nlines);

    return lines ? find_toulouse_line_by_id(lines, nlines, line_gid) : NULL;
}

static int fetch_network_live_stops(NetworkKind network, const ToulouseLine *line,
                                    ToulouseStop *out, int max)
{
    if (network == NETWORK_TLS) {
        int count;

        if (ensure_toulouse_snapshot() < 0 || !line) return -1;
        count = g_tls_nstops > max ? max : g_tls_nstops;
        memcpy(out, g_tls_stops, (size_t)count * sizeof(ToulouseStop));
        return count;
    }
    if (network == NETWORK_IDFM) return fetch_idfm_line_stops(line, out, max);
    if (network == NETWORK_SNCF) return fetch_sncf_line_stops(line, out, max);
    if (network == NETWORK_STAR) return fetch_star_line_stops(line, out, max);
    return -1;
}

static int fetch_network_live_alerts(NetworkKind network, ToulouseAlert *out, int max)
{
    if (network == NETWORK_TLS) return fetch_toulouse_alerts(out, max);
    if (network == NETWORK_IDFM) return fetch_idfm_alerts(out, max);
    if (network == NETWORK_SNCF) return fetch_sncf_alerts(out, max);
    if (network == NETWORK_STAR) return fetch_star_alerts(out, max);
    return -1;
}

static int fetch_network_live_passages(NetworkKind network, const ToulouseLine *line,
                                       const ToulouseStop *stop, ToulousePassage *out, int max)
{
    if (network == NETWORK_TLS) return fetch_toulouse_passages(stop->ref, out, max);
    if (network == NETWORK_IDFM) return fetch_idfm_passages(line, stop, out, max);
    if (network == NETWORK_SNCF) return fetch_sncf_passages(line, stop, out, max);
    if (network == NETWORK_STAR) return fetch_star_passages(line, stop, out, max);
    return -1;
}

static int fetch_network_live_vehicles(NetworkKind network, const ToulouseLine *line,
                                       ToulouseVehicle *out, int max)
{
    if (network == NETWORK_TLS) return fetch_toulouse_vehicles(line, out, max);
    if (network == NETWORK_IDFM) return fetch_idfm_vehicles(line, out, max);
    if (network == NETWORK_SNCF) return fetch_sncf_vehicles(line, out, max);
    if (network == NETWORK_STAR) return fetch_star_vehicles(line, out, max);
    return -1;
}

static char *delay_label(int retard)
{
    char buf[24];
    int minutes;
    int seconds;

    if (!retard) return strdup("");
    minutes = retard / 60;
    seconds = abs(retard) % 60;
    snprintf(buf, sizeof(buf), retard > 0 ? "+%dm%02d" : "-%dm%02d",
             retard > 0 ? minutes : -minutes, seconds);
    return strdup(buf);
}

static const char *state_tone(const Vehicle *v)
{
    if (strcmp(v->etat, "AVANCE") == 0) return "ahead";
    if (strcmp(v->etat, "RETARD") == 0) return v->retard >= 180 ? "critical" : "warning";
    return "stable";
}

static int cmp_passages(const void *a, const void *b)
{
    const Passage *pa = a;
    const Passage *pb = b;
    const char *ta = pa->hor_estime[0] ? pa->hor_estime : pa->hor_theo;
    const char *tb = pb->hor_estime[0] ? pb->hor_estime : pb->hor_theo;
    return strcmp(ta, tb);
}

static int cmp_toulouse_passages(const void *a, const void *b)
{
    const ToulousePassage *pa = a;
    const ToulousePassage *pb = b;
    return strcmp(pa->datetime, pb->datetime);
}

static void add_generated_at(cJSON *root)
{
    char stamp[32];
    iso_now(stamp, sizeof(stamp));
    cJSON_AddStringToObject(root, "generatedAt", stamp);
}

static void add_line_color_fields(cJSON *obj, int ident)
{
    const LineColor *lc = lookup_line_color(ident);
    char buf[16];

    if (!lc) return;
    snprintf(buf, sizeof(buf), "#%06X", lc->bg);
    cJSON_AddStringToObject(obj, "colorBg", buf);
    snprintf(buf, sizeof(buf), "#%06X", lc->fg);
    cJSON_AddStringToObject(obj, "colorFg", buf);
}

static cJSON *line_to_json(const Line *line)
{
    cJSON *obj = cJSON_CreateObject();
    char code[16];

    line_code(line, code, sizeof(code));
    cJSON_AddNumberToObject(obj, "gid", line->gid);
    cJSON_AddNumberToObject(obj, "ident", line->ident);
    cJSON_AddStringToObject(obj, "code", code);
    cJSON_AddStringToObject(obj, "libelle", line->libelle);
    cJSON_AddStringToObject(obj, "vehicule", line->vehicule);
    cJSON_AddBoolToObject(obj, "active", line->active);
    cJSON_AddBoolToObject(obj, "sae", line->sae);
    add_line_color_fields(obj, line->ident);
    return obj;
}

static cJSON *alert_to_json(const Alert *alert, const Line *line)
{
    cJSON *obj = cJSON_CreateObject();

    cJSON_AddNumberToObject(obj, "gid", alert->gid);
    cJSON_AddStringToObject(obj, "id", "");
    cJSON_AddStringToObject(obj, "titre", alert->titre);
    cJSON_AddStringToObject(obj, "message", alert->message);
    cJSON_AddStringToObject(obj, "severite", alert->severite);
    cJSON_AddNumberToObject(obj, "ligneId", alert->ligne_id);
    if (line) {
        char code[16];
        line_code(line, code, sizeof(code));
        cJSON_AddStringToObject(obj, "lineName", line->libelle);
        cJSON_AddStringToObject(obj, "lineCode", code);
        cJSON_AddStringToObject(obj, "lineType", line->vehicule);
        add_line_codes_array(obj, code);
        add_line_color_fields(obj, line->ident);
    } else {
        cJSON_AddStringToObject(obj, "lineCode", "");
        cJSON_AddStringToObject(obj, "lineName", "");
        add_line_codes_array(obj, "");
    }
    return obj;
}

static cJSON *toulouse_line_to_json(const ToulouseLine *line)
{
    cJSON *obj = cJSON_CreateObject();

    cJSON_AddNumberToObject(obj, "gid", line->id);
    cJSON_AddNumberToObject(obj, "ident", line->id);
    cJSON_AddStringToObject(obj, "code", line->code);
    cJSON_AddStringToObject(obj, "libelle", line->libelle);
    cJSON_AddStringToObject(obj, "vehicule", line->mode);
    cJSON_AddBoolToObject(obj, "active", 1);
    cJSON_AddBoolToObject(obj, "sae", 1);
    cJSON_AddStringToObject(obj, "ref", line->ref);
    add_toulouse_line_color_fields(obj, line);
    return obj;
}

static cJSON *toulouse_alert_to_json(const ToulouseAlert *alert, const ToulouseLine *line)
{
    cJSON *obj = cJSON_CreateObject();

    cJSON_AddNumberToObject(obj, "gid", 0);
    cJSON_AddStringToObject(obj, "id", alert->id);
    cJSON_AddStringToObject(obj, "titre", alert->titre);
    cJSON_AddStringToObject(obj, "message", alert->message);
    cJSON_AddStringToObject(obj, "severite", alert->importance);
    cJSON_AddNumberToObject(obj, "ligneId", line ? line->id : 0);
    cJSON_AddStringToObject(obj, "scope", alert->scope);
    cJSON_AddStringToObject(obj, "lineCode", line ? line->code : "");
    cJSON_AddStringToObject(obj, "lineName", line ? line->libelle : "");
    cJSON_AddStringToObject(obj, "lineType", line ? line->mode : "");
    add_line_codes_array(obj, alert->lines);
    add_toulouse_line_color_fields(obj, line);
    return obj;
}

static cJSON *vehicle_to_json(const Vehicle *vehicle)
{
    cJSON *obj = cJSON_CreateObject();
    char *delay = delay_label(vehicle->retard);
    const char *current = stopmap_lookup(&g_stop_map, vehicle->arret_actu);
    const char *next = stopmap_lookup(&g_stop_map, vehicle->arret_suiv);

    cJSON_AddNumberToObject(obj, "gid", vehicle->gid);
    cJSON_AddNumberToObject(obj, "lon", vehicle->lon);
    cJSON_AddNumberToObject(obj, "lat", vehicle->lat);
    cJSON_AddStringToObject(obj, "etat", vehicle->etat);
    cJSON_AddNumberToObject(obj, "retard", vehicle->retard);
    cJSON_AddStringToObject(obj, "delayLabel", delay ? delay : "");
    cJSON_AddNumberToObject(obj, "vitesse", vehicle->vitesse);
    cJSON_AddStringToObject(obj, "vehicule", vehicle->vehicule);
    cJSON_AddStringToObject(obj, "statut", vehicle->statut);
    cJSON_AddStringToObject(obj, "sens", vehicle->sens);
    cJSON_AddStringToObject(obj, "terminus", vehicle->terminus);
    cJSON_AddBoolToObject(obj, "arret", vehicle->arret);
    cJSON_AddNumberToObject(obj, "arretActu", vehicle->arret_actu);
    cJSON_AddNumberToObject(obj, "arretSuiv", vehicle->arret_suiv);
    cJSON_AddStringToObject(obj, "currentStopName", current);
    cJSON_AddStringToObject(obj, "nextStopName", next);
    cJSON_AddStringToObject(obj, "tone", state_tone(vehicle));
    cJSON_AddBoolToObject(obj, "hasPosition", vehicle->lon != 0.0 || vehicle->lat != 0.0);
    free(delay);
    return obj;
}

static cJSON *stop_group_to_json(const StopGroup *group)
{
    cJSON *obj = cJSON_CreateObject();
    cJSON *gids = cJSON_CreateArray();
    char key[32];
    double lon_sum = 0.0;
    double lat_sum = 0.0;
    int coord_count = 0;

    snprintf(key, sizeof(key), "group-%d", group->gids[0]);
    cJSON_AddStringToObject(obj, "key", key);
    cJSON_AddStringToObject(obj, "libelle", group->libelle);
    cJSON_AddStringToObject(obj, "groupe", group->groupe);
    cJSON_AddNumberToObject(obj, "platformCount", group->ngids);
    for (int i = 0; i < group->ngids; i++) {
        double lon;
        double lat;

        cJSON_AddItemToArray(gids, cJSON_CreateNumber(group->gids[i]));
        if (stopmap_lookup_pos(&g_stop_map, group->gids[i], &lon, &lat)) {
            lon_sum += lon;
            lat_sum += lat;
            coord_count++;
        }
    }
    cJSON_AddItemToObject(obj, "gids", gids);
    if (coord_count > 0) {
        cJSON_AddNumberToObject(obj, "lon", lon_sum / coord_count);
        cJSON_AddNumberToObject(obj, "lat", lat_sum / coord_count);
    }
    return obj;
}

static cJSON *toulouse_stop_to_json(const ToulouseStop *stop)
{
    cJSON *obj = cJSON_CreateObject();
    cJSON *gids = cJSON_CreateArray();

    cJSON_AddStringToObject(obj, "key", stop->ref);
    cJSON_AddStringToObject(obj, "libelle", stop->libelle);
    cJSON_AddStringToObject(obj, "groupe", stop->commune[0] ? stop->commune : stop->libelle);
    cJSON_AddNumberToObject(obj, "platformCount", 1);
    cJSON_AddItemToArray(gids, cJSON_CreateNumber(stop->id));
    cJSON_AddItemToObject(obj, "gids", gids);
    cJSON_AddStringToObject(obj, "ref", stop->ref);
    cJSON_AddStringToObject(obj, "lines", stop->lignes);
    cJSON_AddStringToObject(obj, "mode", stop->mode);
    cJSON_AddNumberToObject(obj, "lon", stop->lon);
    cJSON_AddNumberToObject(obj, "lat", stop->lat);
    return obj;
}

static cJSON *passage_to_json(const Passage *passage, const Line *line)
{
    cJSON *obj = cJSON_CreateObject();
    const char *terminus = stopmap_lookup(&g_stop_map, passage->terminus_gid);
    char code[16];
    const char *line_name = "";

    cJSON_AddStringToObject(obj, "estimated", passage->hor_estime);
    cJSON_AddStringToObject(obj, "scheduled", passage->hor_theo);
    cJSON_AddNumberToObject(obj, "courseId", passage->cours_id);
    cJSON_AddNumberToObject(obj, "lineId", passage->ligne_id);
    cJSON_AddNumberToObject(obj, "terminusGid", passage->terminus_gid);
    cJSON_AddStringToObject(obj, "terminusName", terminus);
    cJSON_AddBoolToObject(obj, "live", passage->hor_estime[0] != '\0');
    cJSON_AddBoolToObject(obj, "delayed",
                          passage->hor_estime[0] && passage->hor_theo[0] &&
                          strcmp(passage->hor_estime, passage->hor_theo) > 0);

    if (line) {
        line_code(line, code, sizeof(code));
        line_name = line->libelle;
        cJSON_AddStringToObject(obj, "lineCode", code);
        cJSON_AddStringToObject(obj, "lineName", line_name);
        cJSON_AddStringToObject(obj, "lineType", line->vehicule);
        add_line_color_fields(obj, line->ident);
    } else {
        cJSON_AddStringToObject(obj, "lineCode", "");
        cJSON_AddStringToObject(obj, "lineName", line_name);
    }
    return obj;
}

static cJSON *toulouse_passage_to_json(const ToulousePassage *passage, const ToulouseLine *line)
{
    cJSON *obj = cJSON_CreateObject();
    char hhmm[6];

    extract_hhmm(passage->datetime, hhmm, sizeof(hhmm));
    cJSON_AddStringToObject(obj, "estimated", hhmm);
    cJSON_AddStringToObject(obj, "scheduled", "");
    cJSON_AddNumberToObject(obj, "courseId", 0);
    cJSON_AddNumberToObject(obj, "lineId", line ? line->id : 0);
    cJSON_AddNumberToObject(obj, "terminusGid", 0);
    cJSON_AddStringToObject(obj, "terminusName", passage->destination);
    cJSON_AddBoolToObject(obj, "live", passage->realtime);
    cJSON_AddBoolToObject(obj, "delayed", passage->delayed);
    cJSON_AddStringToObject(obj, "lineCode", passage->line_code);
    cJSON_AddStringToObject(obj, "lineName", passage->line_name);
    cJSON_AddStringToObject(obj, "lineType", line ? line->mode : "");
    cJSON_AddStringToObject(obj, "datetime", passage->datetime);
    cJSON_AddStringToObject(obj, "waitingTime", passage->waiting_time);
    cJSON_AddStringToObject(obj, "stopName", passage->stop_name);
    add_toulouse_line_color_fields(obj, line);
    return obj;
}

static cJSON *toulouse_vehicle_to_json(const ToulouseVehicle *vehicle, const ToulouseLine *line)
{
    cJSON *obj = cJSON_CreateObject();

    cJSON_AddNumberToObject(obj, "gid", 0);
    cJSON_AddNumberToObject(obj, "lon", vehicle->lon);
    cJSON_AddNumberToObject(obj, "lat", vehicle->lat);
    cJSON_AddStringToObject(obj, "etat", vehicle->delayed ? "RETARD" : "NORMAL");
    cJSON_AddNumberToObject(obj, "retard", vehicle->delayed ? 60 : 0);
    cJSON_AddStringToObject(obj, "delayLabel", vehicle->delayed ? "+1m00" : "");
    cJSON_AddNumberToObject(obj, "vitesse", vehicle->vitesse);
    cJSON_AddNumberToObject(obj, "bearing", vehicle->bearing);
    cJSON_AddStringToObject(obj, "vehicule", line ? line->mode : "");
    cJSON_AddStringToObject(obj, "statut", vehicle->realtime ? "REALTIME" : "SCHEDULED");
    cJSON_AddStringToObject(obj, "sens", vehicle->sens);
    cJSON_AddStringToObject(obj, "terminus", vehicle->terminus);
    cJSON_AddBoolToObject(obj, "arret", vehicle->arret);
    cJSON_AddNumberToObject(obj, "arretActu", 0);
    cJSON_AddNumberToObject(obj, "arretSuiv", 0);
    cJSON_AddStringToObject(obj, "currentStopName", vehicle->current_stop);
    cJSON_AddStringToObject(obj, "nextStopName", vehicle->next_stop);
    cJSON_AddStringToObject(obj, "tone", vehicle->delayed ? "warning" : "stable");
    cJSON_AddBoolToObject(obj, "live", vehicle->realtime);
    cJSON_AddStringToObject(obj, "datetime", vehicle->datetime);
    cJSON_AddStringToObject(obj, "waitingTime", vehicle->waiting_time);
    cJSON_AddBoolToObject(obj, "hasPosition", vehicle->has_position);
    add_toulouse_line_color_fields(obj, line);
    return obj;
}

static cJSON *metro_map_to_json(const MetroMap *map)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *bounds = cJSON_CreateObject();
    cJSON *paths = cJSON_CreateArray();
    cJSON *labels = cJSON_CreateArray();

    cJSON_AddNumberToObject(bounds, "minLon", map->minlon);
    cJSON_AddNumberToObject(bounds, "minLat", map->minlat);
    cJSON_AddNumberToObject(bounds, "maxLon", map->maxlon);
    cJSON_AddNumberToObject(bounds, "maxLat", map->maxlat);
    cJSON_AddItemToObject(root, "bounds", bounds);

    for (int i = 0; i < map->npaths; i++) {
        cJSON *path = cJSON_CreateObject();
        cJSON *points = cJSON_CreateArray();
        for (int j = 0; j < map->paths[i].count; j++) {
            int idx = map->paths[i].start + j;
            cJSON *point = cJSON_CreateObject();
            cJSON_AddNumberToObject(point, "lon", map->points[idx].lon);
            cJSON_AddNumberToObject(point, "lat", map->points[idx].lat);
            cJSON_AddItemToArray(points, point);
        }
        cJSON_AddNumberToObject(path, "kind", map->paths[i].kind);
        cJSON_AddItemToObject(path, "points", points);
        cJSON_AddItemToArray(paths, path);
    }

    for (int i = 0; i < map->nlabels; i++) {
        cJSON *label = cJSON_CreateObject();
        cJSON_AddNumberToObject(label, "lon", map->labels[i].lon);
        cJSON_AddNumberToObject(label, "lat", map->labels[i].lat);
        cJSON_AddStringToObject(label, "name", map->labels[i].name);
        cJSON_AddNumberToObject(label, "rank", map->labels[i].rank);
        cJSON_AddItemToArray(labels, label);
    }

    cJSON_AddItemToObject(root, "paths", paths);
    cJSON_AddItemToObject(root, "labels", labels);
    return root;
}

static cJSON *line_route_to_json(const LineRouteMap *map)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *bounds = cJSON_CreateObject();
    cJSON *paths = cJSON_CreateArray();
    cJSON *stats = cJSON_CreateObject();
    double minlon = 0.0;
    double minlat = 0.0;
    double maxlon = 0.0;
    double maxlat = 0.0;

    if (map->npoints > 0) {
        minlon = maxlon = map->points[0].lon;
        minlat = maxlat = map->points[0].lat;
        for (int i = 1; i < map->npoints; i++) {
            if (map->points[i].lon < minlon) minlon = map->points[i].lon;
            if (map->points[i].lon > maxlon) maxlon = map->points[i].lon;
            if (map->points[i].lat < minlat) minlat = map->points[i].lat;
            if (map->points[i].lat > maxlat) maxlat = map->points[i].lat;
        }
    }

    cJSON_AddNumberToObject(bounds, "minLon", minlon);
    cJSON_AddNumberToObject(bounds, "minLat", minlat);
    cJSON_AddNumberToObject(bounds, "maxLon", maxlon);
    cJSON_AddNumberToObject(bounds, "maxLat", maxlat);
    cJSON_AddItemToObject(root, "bounds", bounds);

    for (int i = 0; i < map->npaths; i++) {
        cJSON *path = cJSON_CreateObject();
        cJSON *points = cJSON_CreateArray();
        for (int j = 0; j < map->paths[i].count; j++) {
            int idx = map->paths[i].start + j;
            cJSON *point = cJSON_CreateObject();
            cJSON_AddNumberToObject(point, "lon", map->points[idx].lon);
            cJSON_AddNumberToObject(point, "lat", map->points[idx].lat);
            cJSON_AddItemToArray(points, point);
        }
        cJSON_AddNumberToObject(path, "kind", map->paths[i].kind);
        cJSON_AddItemToObject(path, "points", points);
        cJSON_AddItemToArray(paths, path);
    }

    cJSON_AddItemToObject(root, "paths", paths);
    cJSON_AddNumberToObject(stats, "total", map->npaths);
    cJSON_AddNumberToObject(stats, "aller", map->aller_paths);
    cJSON_AddNumberToObject(stats, "retour", map->retour_paths);
    cJSON_AddItemToObject(root, "stats", stats);
    return root;
}

static int send_all(int fd, const char *buf, size_t len)
{
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(fd, buf + sent, len - sent, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        sent += (size_t)n;
    }
    return 0;
}

static int send_response(int fd, int status, const char *reason, const char *ctype,
                         const char *body, const char *cache_control)
{
    char header[1024];
    int body_len = body ? (int)strlen(body) : 0;
    int n = snprintf(header, sizeof(header),
                     "HTTP/1.1 %d %s\r\n"
                     "Content-Type: %s\r\n"
                     "Content-Length: %d\r\n"
                     "Connection: close\r\n"
                     "Access-Control-Allow-Origin: *\r\n"
                     "Access-Control-Allow-Headers: Content-Type\r\n"
                     "Access-Control-Allow-Methods: GET, OPTIONS\r\n"
                     "Cache-Control: %s\r\n"
                     "\r\n",
                     status, reason, ctype, body_len,
                     cache_control ? cache_control : "no-store");
    if (n < 0 || n >= (int)sizeof(header)) return -1;
    if (send_all(fd, header, (size_t)n) < 0) return -1;
    if (body_len > 0 && send_all(fd, body, (size_t)body_len) < 0) return -1;
    return 0;
}

static int send_json(int fd, int status, const char *reason, cJSON *json, const char *cache_control)
{
    char *body = cJSON_PrintUnformatted(json);
    int rc = send_response(fd, status, reason, "application/json; charset=utf-8",
                           body ? body : "{}", cache_control);
    free(body);
    return rc;
}

static int send_error_json(int fd, int status, const char *code, const char *message)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "error", code);
    cJSON_AddStringToObject(root, "message", message);
    add_generated_at(root);
    int rc = send_json(fd, status,
                       status == 404 ? "Not Found" :
                       status == 405 ? "Method Not Allowed" :
                       status == 503 ? "Service Unavailable" : "Bad Request",
                       root, "no-store");
    cJSON_Delete(root);
    return rc;
}

static int parse_request(const char *raw, HttpRequest *req)
{
    char target[1024];
    const char *line_end = strstr(raw, "\r\n");
    const char *qs;
    size_t line_len;

    if (!line_end) return -1;
    line_len = (size_t)(line_end - raw);
    if (line_len >= 1536) return -1;
    if (sscanf(raw, "%7s %1023s", req->method, target) != 2) return -1;

    qs = strchr(target, '?');
    if (qs) {
        size_t path_len = (size_t)(qs - target);
        if (path_len >= sizeof(req->path)) return -1;
        memcpy(req->path, target, path_len);
        req->path[path_len] = '\0';
        snprintf(req->query, sizeof(req->query), "%s", qs + 1);
    } else {
        if (strlen(target) >= sizeof(req->path)) return -1;
        memcpy(req->path, target, strlen(target) + 1);
        req->query[0] = '\0';
    }
    return 0;
}

static int handle_health(int fd)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *networks = cJSON_CreateArray();
    add_generated_at(root);
    cJSON_AddStringToObject(root, "status", "ok");
    cJSON_AddStringToObject(root, "backend", "nvt-backend");
    cJSON_AddStringToObject(root, "version", BACKEND_VERSION);
    cJSON_AddStringToObject(root, "userAgent", api_user_agent());
    cJSON_AddStringToObject(root, "defaultNetwork", network_slug(NETWORK_BDX));
    cJSON_AddItemToObject(root, "supportedNetworks", networks);
    cJSON_AddItemToArray(networks, cJSON_CreateString(network_slug(NETWORK_BDX)));
    cJSON_AddItemToArray(networks, cJSON_CreateString(network_slug(NETWORK_TLS)));
    cJSON_AddItemToArray(networks, cJSON_CreateString(network_slug(NETWORK_IDFM)));
    cJSON_AddItemToArray(networks, cJSON_CreateString(network_slug(NETWORK_SNCF)));
    cJSON_AddItemToArray(networks, cJSON_CreateString(network_slug(NETWORK_STAR)));
    int rc = send_json(fd, 200, "OK", root, "no-store");
    cJSON_Delete(root);
    return rc;
}

static int handle_lines(int fd, NetworkKind network)
{
    cJSON *root;
    cJSON *items;
    cJSON *stats;

    if (network_is_live(network) || network == NETWORK_TLS) {
        int nlines = 0;
        const ToulouseLine *lines = network_live_lines(network, &nlines);

        if (!lines) {
            return send_error_json(fd, 503, "upstream_unavailable", "Unable to load lines.");
        }

        root = cJSON_CreateObject();
        items = cJSON_CreateArray();
        stats = cJSON_CreateObject();
        add_generated_at(root);
        add_network_to_root(root, network);
        cJSON_AddItemToObject(root, "items", items);
        cJSON_AddItemToObject(root, "stats", stats);
        cJSON_AddNumberToObject(stats, "total", nlines);
        cJSON_AddNumberToObject(stats, "active", nlines);
        cJSON_AddNumberToObject(stats, "tram",
                                network == NETWORK_TLS
                                    ? count_toulouse_lines_type(lines, nlines, "Metro")
                                        + count_toulouse_lines_type(lines, nlines, "Tram")
                                    : count_live_lines_rail(lines, nlines));
        cJSON_AddNumberToObject(stats, "bus",
                                network == NETWORK_TLS
                                    ? count_toulouse_lines_type(lines, nlines, "Bus")
                                    : count_live_lines_bus(lines, nlines));
        cJSON_AddNumberToObject(stats, "other",
                                nlines - (network == NETWORK_TLS
                                              ? count_toulouse_lines_type(lines, nlines, "Metro")
                                                    + count_toulouse_lines_type(lines, nlines, "Tram")
                                                    + count_toulouse_lines_type(lines, nlines, "Bus")
                                              : count_live_lines_rail(lines, nlines)
                                                    + count_live_lines_bus(lines, nlines)));
        for (int i = 0; i < nlines; i++) {
            cJSON_AddItemToArray(items, toulouse_line_to_json(&lines[i]));
        }

        int rc = send_json(fd, 200, "OK", root, "max-age=15");
        cJSON_Delete(root);
        return rc;
    } else {
        Line lines[MAX_LINES];
        int nlines = fetch_lines(lines, MAX_LINES);

        if (nlines < 0) return send_error_json(fd, 503, "upstream_unavailable", "Unable to load lines.");
        qsort(lines, (size_t)nlines, sizeof(Line), cmp_lines);

        root = cJSON_CreateObject();
        items = cJSON_CreateArray();
        stats = cJSON_CreateObject();
        add_generated_at(root);
        add_network_to_root(root, network);
        cJSON_AddItemToObject(root, "items", items);
        cJSON_AddItemToObject(root, "stats", stats);
        cJSON_AddNumberToObject(stats, "total", nlines);
        cJSON_AddNumberToObject(stats, "active", count_active_lines(lines, nlines));
        cJSON_AddNumberToObject(stats, "tram", count_lines_type(lines, nlines, "TRAM"));
        cJSON_AddNumberToObject(stats, "bus", count_lines_type(lines, nlines, "BUS"));
        for (int i = 0; i < nlines; i++) {
            cJSON_AddItemToArray(items, line_to_json(&lines[i]));
        }

        int rc = send_json(fd, 200, "OK", root, "max-age=15");
        cJSON_Delete(root);
        return rc;
    }
}

static int handle_alerts(int fd, NetworkKind network)
{
    cJSON *root;
    cJSON *items;
    cJSON *stats;

    if (network_is_live(network) || network == NETWORK_TLS) {
        ToulouseAlert alerts[MAX_ALERTS];
        int nlines = 0;
        const ToulouseLine *lines = network_live_lines(network, &nlines);
        int nalerts;

        if (!lines) {
            return send_error_json(fd, 503, "upstream_unavailable", "Unable to load alerts.");
        }
        nalerts = fetch_network_live_alerts(network, alerts, MAX_ALERTS);
        if (nalerts < 0) return send_error_json(fd, 503, "upstream_unavailable", "Unable to load alerts.");

        root = cJSON_CreateObject();
        items = cJSON_CreateArray();
        stats = cJSON_CreateObject();
        add_generated_at(root);
        add_network_to_root(root, network);
        cJSON_AddItemToObject(root, "items", items);
        cJSON_AddItemToObject(root, "stats", stats);
        cJSON_AddNumberToObject(stats, "total", nalerts);
        cJSON_AddNumberToObject(stats, "critical", count_toulouse_alert_level(alerts, nalerts, 3));
        cJSON_AddNumberToObject(stats, "warning", count_toulouse_alert_level(alerts, nalerts, 2));
        cJSON_AddNumberToObject(stats, "info", nalerts - count_toulouse_alert_level(alerts, nalerts, 3)
                                         - count_toulouse_alert_level(alerts, nalerts, 2));
        cJSON_AddNumberToObject(stats, "impactedLines", count_toulouse_alerted_lines(alerts, nalerts));

        for (int i = 0; i < nalerts; i++) {
            cJSON_AddItemToArray(items, toulouse_alert_to_json(&alerts[i], primary_alert_line(&alerts[i], lines, nlines)));
        }

        int rc = send_json(fd, 200, "OK", root, "max-age=15");
        cJSON_Delete(root);
        return rc;
    } else {
        Line lines[MAX_LINES];
        Alert alerts[MAX_ALERTS];
        int nlines = fetch_lines(lines, MAX_LINES);
        int nalerts = fetch_alerts(alerts, MAX_ALERTS);

        if (nalerts < 0) return send_error_json(fd, 503, "upstream_unavailable", "Unable to load alerts.");
        if (nlines < 0) nlines = 0;
        if (nlines > 0) qsort(lines, (size_t)nlines, sizeof(Line), cmp_lines);

        root = cJSON_CreateObject();
        items = cJSON_CreateArray();
        stats = cJSON_CreateObject();
        add_generated_at(root);
        add_network_to_root(root, network);
        cJSON_AddItemToObject(root, "items", items);
        cJSON_AddItemToObject(root, "stats", stats);
        cJSON_AddNumberToObject(stats, "total", nalerts);
        cJSON_AddNumberToObject(stats, "critical", count_alert_prefix(alerts, nalerts, "3_"));
        cJSON_AddNumberToObject(stats, "warning", count_alert_prefix(alerts, nalerts, "2_"));
        cJSON_AddNumberToObject(stats, "info", nalerts - count_alert_prefix(alerts, nalerts, "3_")
                                         - count_alert_prefix(alerts, nalerts, "2_"));
        cJSON_AddNumberToObject(stats, "impactedLines", count_alerted_lines(alerts, nalerts));

        for (int i = 0; i < nalerts; i++) {
            cJSON_AddItemToArray(items, alert_to_json(&alerts[i], find_line_by_gid(lines, nlines, alerts[i].ligne_id)));
        }

        int rc = send_json(fd, 200, "OK", root, "max-age=15");
        cJSON_Delete(root);
        return rc;
    }
}

static int handle_stop_groups(int fd, const HttpRequest *req, NetworkKind network)
{
    cJSON *root;
    cJSON *items;

    if (network == NETWORK_TLS) {
        if (ensure_toulouse_snapshot() < 0) {
            return send_error_json(fd, 503, "upstream_unavailable", "Unable to load stop groups.");
        }

        root = cJSON_CreateObject();
        items = cJSON_CreateArray();
        add_generated_at(root);
        add_network_to_root(root, network);
        cJSON_AddItemToObject(root, "items", items);
        cJSON_AddNumberToObject(root, "total", g_tls_nstops);
        for (int i = 0; i < g_tls_nstops; i++) {
            cJSON_AddItemToArray(items, toulouse_stop_to_json(&g_tls_stops[i]));
        }

        int rc = send_json(fd, 200, "OK", root, "max-age=300");
        cJSON_Delete(root);
        return rc;
    } else if (network == NETWORK_BDX) {
        if (ensure_stop_groups() < 0) {
            return send_error_json(fd, 503, "upstream_unavailable", "Unable to load stop groups.");
        }
        ensure_stop_map();

        root = cJSON_CreateObject();
        items = cJSON_CreateArray();
        add_generated_at(root);
        add_network_to_root(root, network);
        cJSON_AddItemToObject(root, "items", items);
        cJSON_AddNumberToObject(root, "total", g_nstop_groups);
        for (int i = 0; i < g_nstop_groups; i++) {
            cJSON_AddItemToArray(items, stop_group_to_json(&g_stop_groups[i]));
        }

        int rc = send_json(fd, 200, "OK", root, "max-age=300");
        cJSON_Delete(root);
        return rc;
    } else {
        ToulouseStop stops[MAX_STOPS];
        int nstops;
        int line_gid = 0;
        int line_rc = request_line_gid(req, &line_gid);
        const ToulouseLine *line;

        if (line_rc < 0) {
            return send_error_json(fd, 400, "invalid_line", "Use a numeric line query parameter.");
        }
        if (line_rc == 0) {
            return send_error_json(fd, 400, "missing_line", "Use line=<gid> for network=idfm or network=sncf.");
        }

        if (!network_live_lines(network, NULL)) {
            return send_error_json(fd, 503, "upstream_unavailable", "Unable to load line metadata.");
        }
        line = find_network_live_line_by_id(network, line_gid);
        if (!line) return send_error_json(fd, 404, "line_not_found", "Unknown line.");

        nstops = fetch_network_live_stops(network, line, stops, MAX_STOPS);
        if (nstops < 0) {
            return send_error_json(fd, 503, "upstream_unavailable", "Unable to load stop groups.");
        }
        if (nstops > 1) qsort(stops, (size_t)nstops, sizeof(ToulouseStop), cmp_toulouse_stops);

        root = cJSON_CreateObject();
        items = cJSON_CreateArray();
        add_generated_at(root);
        add_network_to_root(root, network);
        cJSON_AddItemToObject(root, "line", toulouse_line_to_json(line));
        cJSON_AddItemToObject(root, "items", items);
        cJSON_AddNumberToObject(root, "total", nstops);
        for (int i = 0; i < nstops; i++) {
            cJSON_AddItemToArray(items, toulouse_stop_to_json(&stops[i]));
        }

        int rc = send_json(fd, 200, "OK", root, "max-age=300");
        cJSON_Delete(root);
        return rc;
    }
}

static int handle_stop_group_passages(int fd, const char *key, const HttpRequest *req, NetworkKind network)
{
    int delayed = 0;
    int live = 0;
    int unique_lines = 0;
    cJSON *root;
    cJSON *items;
    cJSON *stats;

    if (network == NETWORK_TLS) {
        const ToulouseStop *stop;
        ToulousePassage passages[MAX_PASSAGES];
        int npassages;

        if (ensure_toulouse_snapshot() < 0) {
            return send_error_json(fd, 503, "upstream_unavailable", "Unable to prepare passage data.");
        }

        stop = find_toulouse_stop_by_key(key);
        if (!stop) return send_error_json(fd, 404, "stop_group_not_found", "Unknown stop group.");

        npassages = fetch_toulouse_passages(stop->ref, passages, MAX_PASSAGES);
        if (npassages < 0) {
            return send_error_json(fd, 503, "upstream_unavailable", "Unable to prepare passage data.");
        }
        if (npassages > 1) qsort(passages, (size_t)npassages, sizeof(ToulousePassage), cmp_toulouse_passages);

        root = cJSON_CreateObject();
        items = cJSON_CreateArray();
        stats = cJSON_CreateObject();
        add_generated_at(root);
        add_network_to_root(root, network);
        cJSON_AddItemToObject(root, "items", items);
        cJSON_AddItemToObject(root, "stats", stats);
        cJSON_AddItemToObject(root, "group", toulouse_stop_to_json(stop));

        for (int i = 0; i < npassages; i++) {
            const ToulouseLine *line = find_toulouse_line_by_code(g_tls_lines, g_tls_nlines, passages[i].line_code);
            int dup = 0;

            if (passages[i].realtime) live++;
            if (passages[i].delayed) delayed++;
            if (passages[i].line_code[0]) {
                for (int j = 0; j < i; j++) {
                    if (strcmp(passages[j].line_code, passages[i].line_code) == 0) {
                        dup = 1;
                        break;
                    }
                }
                if (!dup) unique_lines++;
            }
            cJSON_AddItemToArray(items, toulouse_passage_to_json(&passages[i], line));
        }

        cJSON_AddNumberToObject(stats, "total", npassages);
        cJSON_AddNumberToObject(stats, "live", live);
        cJSON_AddNumberToObject(stats, "delayed", delayed);
        cJSON_AddNumberToObject(stats, "lines", unique_lines);

        int rc = send_json(fd, 200, "OK", root, "max-age=15");
        cJSON_Delete(root);
        return rc;
    } else if (network == NETWORK_BDX) {
        const StopGroup *group;
        Passage passages[MAX_PASSAGES];
        Line lines[MAX_LINES];
        int nlines;
        int npassages = 0;

        if (ensure_stop_groups() < 0 || ensure_course_cache() < 0) {
            return send_error_json(fd, 503, "upstream_unavailable", "Unable to prepare passage data.");
        }
        ensure_stop_map();

        group = find_stop_group_by_key(key);
        if (!group) return send_error_json(fd, 404, "stop_group_not_found", "Unknown stop group.");

        for (int i = 0; i < group->ngids && npassages < MAX_PASSAGES; i++) {
            int n = fetch_passages(group->gids[i], passages + npassages, MAX_PASSAGES - npassages, &g_course_cache);
            if (n > 0) npassages += n;
        }
        if (npassages > 1) qsort(passages, (size_t)npassages, sizeof(Passage), cmp_passages);

        nlines = fetch_lines(lines, MAX_LINES);
        if (nlines < 0) nlines = 0;
        if (nlines > 0) qsort(lines, (size_t)nlines, sizeof(Line), cmp_lines);

        root = cJSON_CreateObject();
        items = cJSON_CreateArray();
        stats = cJSON_CreateObject();
        add_generated_at(root);
        add_network_to_root(root, network);
        cJSON_AddItemToObject(root, "items", items);
        cJSON_AddItemToObject(root, "stats", stats);
        cJSON_AddItemToObject(root, "group", stop_group_to_json(group));

        for (int i = 0; i < npassages; i++) {
            int dup = 0;
            if (passages[i].hor_estime[0]) live++;
            if (passages[i].hor_estime[0] && passages[i].hor_theo[0] &&
                strcmp(passages[i].hor_estime, passages[i].hor_theo) > 0) delayed++;
            if (passages[i].ligne_id) {
                for (int j = 0; j < i; j++) {
                    if (passages[j].ligne_id == passages[i].ligne_id) {
                        dup = 1;
                        break;
                    }
                }
                if (!dup) unique_lines++;
            }
            cJSON_AddItemToArray(items, passage_to_json(&passages[i], find_line_by_gid(lines, nlines, passages[i].ligne_id)));
        }

        cJSON_AddNumberToObject(stats, "total", npassages);
        cJSON_AddNumberToObject(stats, "live", live);
        cJSON_AddNumberToObject(stats, "delayed", delayed);
        cJSON_AddNumberToObject(stats, "lines", unique_lines);

        int rc = send_json(fd, 200, "OK", root, "max-age=15");
        cJSON_Delete(root);
        return rc;
    } else {
        ToulouseStop stops[MAX_STOPS];
        ToulousePassage passages[MAX_PASSAGES];
        int nstops;
        int npassages;
        int line_gid = 0;
        int line_rc = request_line_gid(req, &line_gid);
        const ToulouseLine *line;
        const ToulouseStop *stop;

        if (line_rc < 0) {
            return send_error_json(fd, 400, "invalid_line", "Use a numeric line query parameter.");
        }
        if (line_rc == 0) {
            return send_error_json(fd, 400, "missing_line", "Use line=<gid> for network=idfm or network=sncf.");
        }

        if (!network_live_lines(network, NULL)) {
            return send_error_json(fd, 503, "upstream_unavailable", "Unable to load line metadata.");
        }
        line = find_network_live_line_by_id(network, line_gid);
        if (!line) return send_error_json(fd, 404, "line_not_found", "Unknown line.");

        nstops = fetch_network_live_stops(network, line, stops, MAX_STOPS);
        if (nstops < 0) {
            return send_error_json(fd, 503, "upstream_unavailable", "Unable to prepare passage data.");
        }

        stop = find_live_stop_by_key(stops, nstops, key);
        if (!stop) return send_error_json(fd, 404, "stop_group_not_found", "Unknown stop group.");

        npassages = fetch_network_live_passages(network, line, stop, passages, MAX_PASSAGES);
        if (npassages < 0) {
            return send_error_json(fd, 503, "upstream_unavailable", "Unable to prepare passage data.");
        }
        if (npassages > 1) qsort(passages, (size_t)npassages, sizeof(ToulousePassage), cmp_toulouse_passages);

        root = cJSON_CreateObject();
        items = cJSON_CreateArray();
        stats = cJSON_CreateObject();
        add_generated_at(root);
        add_network_to_root(root, network);
        cJSON_AddItemToObject(root, "line", toulouse_line_to_json(line));
        cJSON_AddItemToObject(root, "items", items);
        cJSON_AddItemToObject(root, "stats", stats);
        cJSON_AddItemToObject(root, "group", toulouse_stop_to_json(stop));

        for (int i = 0; i < npassages; i++) {
            int dup = 0;

            if (passages[i].realtime) live++;
            if (passages[i].delayed) delayed++;
            if (passages[i].line_code[0]) {
                for (int j = 0; j < i; j++) {
                    if (strcmp(passages[j].line_code, passages[i].line_code) == 0) {
                        dup = 1;
                        break;
                    }
                }
                if (!dup) unique_lines++;
            }
            cJSON_AddItemToArray(items, toulouse_passage_to_json(&passages[i], line));
        }

        cJSON_AddNumberToObject(stats, "total", npassages);
        cJSON_AddNumberToObject(stats, "live", live);
        cJSON_AddNumberToObject(stats, "delayed", delayed);
        cJSON_AddNumberToObject(stats, "lines", unique_lines);

        int rc = send_json(fd, 200, "OK", root, "max-age=15");
        cJSON_Delete(root);
        return rc;
    }
}

static int handle_vehicles(int fd, int line_gid, NetworkKind network)
{
    int delayed = 0;
    int stopped = 0;
    int speed_sum = 0;
    int aller = 0;
    int retour = 0;
    int line_alerts = 0;
    cJSON *root;
    cJSON *items;
    cJSON *stats;

    if (network_is_live(network) || network == NETWORK_TLS) {
        ToulouseVehicle vehicles[MAX_VEHICLES];
        ToulouseAlert alerts[MAX_ALERTS];
        int nvehicles;
        int nalerts;
        const ToulouseLine *line;

        if (!network_live_lines(network, NULL)) {
            return send_error_json(fd, 503, "upstream_unavailable", "Unable to load line metadata.");
        }
        line = find_network_live_line_by_id(network, line_gid);
        if (!line) {
            return send_error_json(fd, 404, "line_not_found", "Unknown line.");
        }
        /* For IDFM: fetch the line's stops first and feed them into the
         * vehicle synthesis cache so fetch_idfm_vehicles can use SIRI ETT. */
        if (network == NETWORK_IDFM) {
            static ToulouseStop idfm_stops_for_synth[MAX_STOPS];
            int n_stops = fetch_network_live_stops(network, line, idfm_stops_for_synth, MAX_STOPS);
            if (n_stops > 0) nvt_set_idfm_vehicle_stops(idfm_stops_for_synth, n_stops);
            else             nvt_set_idfm_vehicle_stops(NULL, 0);
        }
        nvehicles = fetch_network_live_vehicles(network, line, vehicles, MAX_VEHICLES);
        if (network == NETWORK_IDFM) nvt_set_idfm_vehicle_stops(NULL, 0);  /* clear */
        if (nvehicles < 0) return send_error_json(fd, 503, "upstream_unavailable", "Unable to load vehicles.");

        nalerts = fetch_network_live_alerts(network, alerts, MAX_ALERTS);
        if (nalerts < 0) nalerts = 0;

        root = cJSON_CreateObject();
        items = cJSON_CreateArray();
        stats = cJSON_CreateObject();
        add_generated_at(root);
        add_network_to_root(root, network);
        cJSON_AddItemToObject(root, "line", toulouse_line_to_json(line));
        cJSON_AddItemToObject(root, "items", items);
        cJSON_AddItemToObject(root, "stats", stats);

        for (int i = 0; i < nvehicles; i++) {
            if (vehicles[i].delayed) delayed++;
            if (vehicles[i].arret) stopped++;
            if (strcmp(vehicles[i].sens, "ALLER") == 0) aller++;
            if (strcmp(vehicles[i].sens, "RETOUR") == 0) retour++;
            speed_sum += vehicles[i].vitesse;
            cJSON_AddItemToArray(items, toulouse_vehicle_to_json(&vehicles[i], line));
        }

        for (int i = 0; i < nalerts; i++) {
            if (toulouse_alert_has_line(&alerts[i], line)) line_alerts++;
        }

        cJSON_AddNumberToObject(stats, "total", nvehicles);
        cJSON_AddNumberToObject(stats, "delayed", delayed);
        cJSON_AddNumberToObject(stats, "stopped", stopped);
        cJSON_AddNumberToObject(stats, "avgSpeed", nvehicles ? speed_sum / nvehicles : 0);
        cJSON_AddNumberToObject(stats, "aller", aller);
        cJSON_AddNumberToObject(stats, "retour", retour);
        cJSON_AddNumberToObject(stats, "alerts", line_alerts);

        int rc = send_json(fd, 200, "OK", root, "max-age=10");
        cJSON_Delete(root);
        return rc;
    } else {
        Line lines[MAX_LINES];
        Vehicle vehicles[MAX_VEHICLES];
        Alert alerts[MAX_ALERTS];
        int nlines;
        int nvehicles;
        int nalerts;
        const Line *line;

        ensure_stop_map();

        nlines = fetch_lines(lines, MAX_LINES);
        if (nlines < 0) return send_error_json(fd, 503, "upstream_unavailable", "Unable to load line metadata.");
        qsort(lines, (size_t)nlines, sizeof(Line), cmp_lines);
        line = find_line_by_gid(lines, nlines, line_gid);
        if (!line) return send_error_json(fd, 404, "line_not_found", "Unknown line.");

        nvehicles = fetch_vehicles(line_gid, vehicles, MAX_VEHICLES);
        if (nvehicles < 0) return send_error_json(fd, 503, "upstream_unavailable", "Unable to load vehicles.");

        nalerts = fetch_alerts(alerts, MAX_ALERTS);
        if (nalerts < 0) nalerts = 0;

        root = cJSON_CreateObject();
        items = cJSON_CreateArray();
        stats = cJSON_CreateObject();
        add_generated_at(root);
        add_network_to_root(root, network);
        cJSON_AddItemToObject(root, "line", line_to_json(line));
        cJSON_AddItemToObject(root, "items", items);
        cJSON_AddItemToObject(root, "stats", stats);

        for (int i = 0; i < nvehicles; i++) {
            if (strcmp(vehicles[i].etat, "RETARD") == 0 && vehicles[i].retard >= 60) delayed++;
            if (vehicles[i].arret) stopped++;
            if (strcmp(vehicles[i].sens, "ALLER") == 0) aller++;
            if (strcmp(vehicles[i].sens, "RETOUR") == 0) retour++;
            speed_sum += vehicles[i].vitesse;
            cJSON_AddItemToArray(items, vehicle_to_json(&vehicles[i]));
        }

        for (int i = 0; i < nalerts; i++) {
            if (alerts[i].ligne_id == line_gid) line_alerts++;
        }

        cJSON_AddNumberToObject(stats, "total", nvehicles);
        cJSON_AddNumberToObject(stats, "delayed", delayed);
        cJSON_AddNumberToObject(stats, "stopped", stopped);
        cJSON_AddNumberToObject(stats, "avgSpeed", nvehicles ? speed_sum / nvehicles : 0);
        cJSON_AddNumberToObject(stats, "aller", aller);
        cJSON_AddNumberToObject(stats, "retour", retour);
        cJSON_AddNumberToObject(stats, "alerts", line_alerts);

        int rc = send_json(fd, 200, "OK", root, "max-age=10");
        cJSON_Delete(root);
        return rc;
    }
}

static int handle_map_boundaries(int fd, NetworkKind network)
{
    cJSON *root;

    if (!network_supports_boundaries(network)) {
        return send_error_json(fd, 404, "map_not_available",
                               "Boundary data is only available for Bordeaux and Toulouse.");
    }

    if (network == NETWORK_TLS) {
        if (ensure_toulouse_metro_map() < 0) {
            return send_error_json(fd, 503, "upstream_unavailable", "Unable to load Toulouse boundary data.");
        }
        root = metro_map_to_json(&g_tls_metro_map);
    } else {
        if (ensure_metro_map() < 0) {
            return send_error_json(fd, 503, "upstream_unavailable", "Unable to load Bordeaux boundary data.");
        }
        root = metro_map_to_json(&g_metro_map);
    }
    add_generated_at(root);
    add_network_to_root(root, network);
    int rc = send_json(fd, 200, "OK", root, "max-age=3600");
    cJSON_Delete(root);
    return rc;
}

static int handle_line_route(int fd, int line_gid, NetworkKind network)
{
    LineRouteMap route;
    cJSON *root;
    int rc;

    if (!network_supports_route(network)) {
        return send_error_json(fd, 404, "route_not_available",
                               "Line route geometry is only available for Bordeaux and Toulouse.");
    }

    if (network == NETWORK_TLS) {
        const ToulouseLine *line;

        if (ensure_toulouse_snapshot() < 0) {
            return send_error_json(fd, 503, "upstream_unavailable", "Unable to load line metadata.");
        }
        line = find_toulouse_line_by_id(g_tls_lines, g_tls_nlines, line_gid);
        if (!line) return send_error_json(fd, 404, "line_not_found", "Unknown line.");
        if (fetch_toulouse_line_route(line, &route) < 0) {
            return send_error_json(fd, 503, "upstream_unavailable", "Unable to load route geometry.");
        }

        root = line_route_to_json(&route);
        add_generated_at(root);
        add_network_to_root(root, network);
        cJSON_AddItemToObject(root, "line", toulouse_line_to_json(line));
        rc = send_json(fd, 200, "OK", root, "max-age=300");
        cJSON_Delete(root);
        return rc;
    } else {
        Line lines[MAX_LINES];
        int nlines = fetch_lines(lines, MAX_LINES);
        const Line *line;

        if (nlines < 0) {
            return send_error_json(fd, 503, "upstream_unavailable", "Unable to load line metadata.");
        }
        qsort(lines, (size_t)nlines, sizeof(Line), cmp_lines);
        line = find_line_by_gid(lines, nlines, line_gid);
        if (!line) return send_error_json(fd, 404, "line_not_found", "Unknown line.");
        if (fetch_line_route(line_gid, &route) < 0) {
            return send_error_json(fd, 503, "upstream_unavailable", "Unable to load route geometry.");
        }

        root = line_route_to_json(&route);
        add_generated_at(root);
        add_network_to_root(root, network);
        cJSON_AddItemToObject(root, "line", line_to_json(line));
        rc = send_json(fd, 200, "OK", root, "max-age=300");
        cJSON_Delete(root);
        return rc;
    }
}

static int handle_api_root(int fd)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *endpoints = cJSON_CreateArray();
    cJSON *networks = cJSON_CreateArray();
    add_generated_at(root);
    cJSON_AddStringToObject(root, "name", "nvt-backend");
    cJSON_AddStringToObject(root, "version", BACKEND_VERSION);
    cJSON_AddStringToObject(root, "defaultNetwork", network_slug(NETWORK_BDX));
    cJSON_AddItemToObject(root, "endpoints", endpoints);
    cJSON_AddItemToObject(root, "supportedNetworks", networks);
    cJSON_AddItemToArray(networks, cJSON_CreateString(network_slug(NETWORK_BDX)));
    cJSON_AddItemToArray(networks, cJSON_CreateString(network_slug(NETWORK_TLS)));
    cJSON_AddItemToArray(networks, cJSON_CreateString(network_slug(NETWORK_IDFM)));
    cJSON_AddItemToArray(networks, cJSON_CreateString(network_slug(NETWORK_SNCF)));
    cJSON_AddItemToArray(networks, cJSON_CreateString(network_slug(NETWORK_STAR)));
    cJSON_AddItemToArray(endpoints, cJSON_CreateString("/api/health"));
    cJSON_AddItemToArray(endpoints, cJSON_CreateString("/api/lines?network=bdx|tls|idfm|sncf|star"));
    cJSON_AddItemToArray(endpoints, cJSON_CreateString("/api/alerts?network=bdx|tls|idfm|sncf|star"));
    cJSON_AddItemToArray(endpoints, cJSON_CreateString("/api/lines/:gid/vehicles?network=bdx|tls|idfm|sncf"));
    cJSON_AddItemToArray(endpoints, cJSON_CreateString("/api/lines/:gid/route?network=bdx|tls"));
    cJSON_AddItemToArray(endpoints, cJSON_CreateString("/api/stop-groups?network=bdx|tls"));
    cJSON_AddItemToArray(endpoints, cJSON_CreateString("/api/stop-groups?network=idfm|sncf&line=:gid"));
    cJSON_AddItemToArray(endpoints, cJSON_CreateString("/api/stop-groups/:key/passages?network=bdx|tls"));
    cJSON_AddItemToArray(endpoints, cJSON_CreateString("/api/stop-groups/:key/passages?network=idfm|sncf&line=:gid"));
    cJSON_AddItemToArray(endpoints, cJSON_CreateString("/api/map/boundaries?network=bdx|tls"));
    int rc = send_json(fd, 200, "OK", root, "no-store");
    cJSON_Delete(root);
    return rc;
}

static int route_request(int fd, const HttpRequest *req)
{
    int gid;
    const char *prefix;
    NetworkKind network;

    if (strcmp(req->method, "OPTIONS") == 0) return send_response(fd, 204, "No Content", "text/plain", "", "no-store");
    if (strcmp(req->method, "GET") != 0) return send_error_json(fd, 405, "method_not_allowed", "Only GET and OPTIONS are supported.");
    if (strcmp(req->path, "/") == 0) return handle_api_root(fd);
    if (strcmp(req->path, "/api") == 0) return handle_api_root(fd);
    if (strcmp(req->path, "/api/health") == 0) return handle_health(fd);

    if (request_network(req, &network) < 0) {
        return send_error_json(fd, 400, "invalid_network", "Use network=bdx, network=tls, network=idfm, or network=sncf.");
    }

    if (strcmp(req->path, "/api/lines") == 0) return handle_lines(fd, network);
    if (strcmp(req->path, "/api/alerts") == 0) return handle_alerts(fd, network);
    if (strcmp(req->path, "/api/stop-groups") == 0) return handle_stop_groups(fd, req, network);
    if (strcmp(req->path, "/api/map/boundaries") == 0) return handle_map_boundaries(fd, network);
    if (match_line_child_path(req->path, "/route", &gid)) return handle_line_route(fd, gid, network);
    if (match_line_child_path(req->path, "/vehicles", &gid)) return handle_vehicles(fd, gid, network);

    prefix = "/api/stop-groups/";
    if (strncmp(req->path, prefix, strlen(prefix)) == 0) {
        const char *rest = req->path + strlen(prefix);
        const char *slash = strstr(rest, "/passages");
        char key[128];
        char encoded_key[128];
        if (slash && strcmp(slash, "/passages") == 0) {
            size_t len = (size_t)(slash - rest);
            if (len < sizeof(encoded_key)) {
                memcpy(encoded_key, rest, len);
                encoded_key[len] = '\0';
                url_decode_component(encoded_key, key, sizeof(key));
                return handle_stop_group_passages(fd, key, req, network);
            }
        }
    }

    return send_error_json(fd, 404, "not_found", "Unknown endpoint.");
}

static void handle_client(int client_fd, const struct sockaddr_in *peer)
{
    char buf[REQ_BUF_SIZE + 1];
    ssize_t nread;
    HttpRequest req;

    nread = recv(client_fd, buf, REQ_BUF_SIZE, 0);
    if (nread <= 0) return;
    buf[nread] = '\0';
    if (parse_request(buf, &req) < 0) {
        send_error_json(client_fd, 400, "bad_request", "Malformed request line.");
        return;
    }

    log_msg("%s %s from %s", req.method, req.path, inet_ntoa(peer->sin_addr));
    route_request(client_fd, &req);
}

int main(int argc, char **argv)
{
    int server_fd;
    int port = DEFAULT_PORT;
    int opt = 1;
    struct sockaddr_in addr;

    if (argc > 1) {
        port = atoi(argv[1]);
    } else if (getenv("NVT_BACKEND_PORT")) {
        port = atoi(getenv("NVT_BACKEND_PORT"));
    }
    if (port <= 0 || port > 65535) {
        fprintf(stderr, "Invalid port: %d\n", port);
        return 1;
    }

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGPIPE, on_sigpipe);

    api_init();

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        api_cleanup();
        return 1;
    }
    g_server_fd = server_fd;

    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        close(server_fd);
        api_cleanup();
        return 1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)port);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(server_fd);
        api_cleanup();
        return 1;
    }

    if (listen(server_fd, 16) < 0) {
        perror("listen");
        close(server_fd);
        api_cleanup();
        return 1;
    }

    log_msg("nvt-backend listening on http://127.0.0.1:%d", port);

    while (g_running) {
        struct sockaddr_in peer;
        socklen_t peer_len = sizeof(peer);
        int client_fd = accept(server_fd, (struct sockaddr *)&peer, &peer_len);
        if (client_fd < 0) {
            if (!g_running) break;
            if (errno == EINTR) continue;
            perror("accept");
            break;
        }
        handle_client(client_fd, &peer);
        close(client_fd);
    }

    if (g_course_cache_ready) course_cache_free(&g_course_cache);
    if (g_stop_map_loaded) stopmap_free(&g_stop_map);
    api_cleanup();
    if (server_fd >= 0) close(server_fd);
    g_server_fd = -1;
    return 0;
}
