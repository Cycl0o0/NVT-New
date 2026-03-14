#include "api.h"
#include "config.h"
#include "cJSON.h"
#include "line_colors.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
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

#define BACKEND_VERSION "1.0"
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
static int g_map_attempted = 0;
static ToulouseSnapshot g_tls_snapshot;
static ToulouseLine g_tls_lines[MAX_LINES];
static int g_tls_nlines = -1;
static ToulouseStop g_tls_stops[MAX_STOPS];
static int g_tls_nstops = -1;
static MetroMap g_tls_metro_map;
static int g_tls_has_metro_map = 0;
static int g_tls_map_attempted = 0;

static void on_signal(int signum)
{
    (void)signum;
    g_running = 0;
    if (g_server_fd >= 0) {
        close(g_server_fd);
        g_server_fd = -1;
    }
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
    return network == NETWORK_TLS ? "toulouse" : "bordeaux";
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

    return -1;
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

static int count_toulouse_lines_type(const ToulouseLine *lines, int n, const char *type)
{
    int total = 0;
    for (int i = 0; i < n; i++) {
        if (strcasecmp(lines[i].mode, type) == 0) total++;
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

static const ToulouseLine *toulouse_primary_alert_line(const ToulouseAlert *alert)
{
    char copy[128];
    char *token;
    char *saveptr = NULL;

    if (!alert || !alert->lines[0] || g_tls_nlines <= 0) return NULL;
    snprintf(copy, sizeof(copy), "%s", alert->lines);
    token = strtok_r(copy, " ", &saveptr);
    while (token) {
        const ToulouseLine *line = find_toulouse_line_by_code(g_tls_lines, g_tls_nlines, token);
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
    if (g_map_attempted) return g_has_metro_map ? 0 : -1;
    g_map_attempted = 1;
    g_has_metro_map = fetch_metro_map(&g_metro_map) > 0;
    return g_has_metro_map ? 0 : -1;
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
    return 0;
}

static int ensure_toulouse_metro_map(void)
{
    if (g_tls_map_attempted) return g_tls_has_metro_map ? 0 : -1;
    g_tls_map_attempted = 1;
    g_tls_has_metro_map = fetch_toulouse_metro_map(&g_tls_metro_map) > 0;
    return g_tls_has_metro_map ? 0 : -1;
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
    free(delay);
    return obj;
}

static cJSON *stop_group_to_json(const StopGroup *group)
{
    cJSON *obj = cJSON_CreateObject();
    cJSON *gids = cJSON_CreateArray();
    char key[32];

    snprintf(key, sizeof(key), "group-%d", group->gids[0]);
    cJSON_AddStringToObject(obj, "key", key);
    cJSON_AddStringToObject(obj, "libelle", group->libelle);
    cJSON_AddStringToObject(obj, "groupe", group->groupe);
    cJSON_AddNumberToObject(obj, "platformCount", group->ngids);
    for (int i = 0; i < group->ngids; i++) {
        cJSON_AddItemToArray(gids, cJSON_CreateNumber(group->gids[i]));
    }
    cJSON_AddItemToObject(obj, "gids", gids);
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
    cJSON_AddNumberToObject(obj, "lon", 0);
    cJSON_AddNumberToObject(obj, "lat", 0);
    cJSON_AddStringToObject(obj, "etat", vehicle->delayed ? "RETARD" : "NORMAL");
    cJSON_AddNumberToObject(obj, "retard", vehicle->delayed ? 60 : 0);
    cJSON_AddStringToObject(obj, "delayLabel", vehicle->delayed ? "+1m00" : "");
    cJSON_AddNumberToObject(obj, "vitesse", vehicle->vitesse);
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
    cJSON_AddBoolToObject(root, "mapkitConfigured",
                          getenv("MAPKIT_JS_TOKEN") && getenv("MAPKIT_JS_TOKEN")[0]);
    int rc = send_json(fd, 200, "OK", root, "no-store");
    cJSON_Delete(root);
    return rc;
}

static int handle_lines(int fd, NetworkKind network)
{
    cJSON *root;
    cJSON *items;
    cJSON *stats;

    if (network == NETWORK_TLS) {
        if (ensure_toulouse_snapshot() < 0) {
            return send_error_json(fd, 503, "upstream_unavailable", "Unable to load lines.");
        }

        root = cJSON_CreateObject();
        items = cJSON_CreateArray();
        stats = cJSON_CreateObject();
        add_generated_at(root);
        add_network_to_root(root, network);
        cJSON_AddItemToObject(root, "items", items);
        cJSON_AddItemToObject(root, "stats", stats);
        cJSON_AddNumberToObject(stats, "total", g_tls_nlines);
        cJSON_AddNumberToObject(stats, "active", g_tls_nlines);
        cJSON_AddNumberToObject(stats, "tram",
                                count_toulouse_lines_type(g_tls_lines, g_tls_nlines, "Metro") +
                                count_toulouse_lines_type(g_tls_lines, g_tls_nlines, "Tram"));
        cJSON_AddNumberToObject(stats, "bus",
                                count_toulouse_lines_type(g_tls_lines, g_tls_nlines, "Bus"));
        for (int i = 0; i < g_tls_nlines; i++) {
            cJSON_AddItemToArray(items, toulouse_line_to_json(&g_tls_lines[i]));
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

    if (network == NETWORK_TLS) {
        ToulouseAlert alerts[MAX_ALERTS];
        int nalerts;

        if (ensure_toulouse_snapshot() < 0) {
            return send_error_json(fd, 503, "upstream_unavailable", "Unable to load alerts.");
        }
        nalerts = fetch_toulouse_alerts(alerts, MAX_ALERTS);
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
            cJSON_AddItemToArray(items, toulouse_alert_to_json(&alerts[i], toulouse_primary_alert_line(&alerts[i])));
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

static int handle_stop_groups(int fd, NetworkKind network)
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
    } else {
        if (ensure_stop_groups() < 0) return send_error_json(fd, 503, "upstream_unavailable", "Unable to load stop groups.");

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
    }
}

static int handle_stop_group_passages(int fd, const char *key, NetworkKind network)
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
    } else {
        const StopGroup *group;
        Passage passages[MAX_PASSAGES];
        Line lines[MAX_LINES];
        int nlines;
        int npassages = 0;

        if (ensure_stop_groups() < 0 || ensure_stop_map() < 0 || ensure_course_cache() < 0) {
            return send_error_json(fd, 503, "upstream_unavailable", "Unable to prepare passage data.");
        }

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

    if (network == NETWORK_TLS) {
        ToulouseVehicle vehicles[MAX_VEHICLES];
        ToulouseAlert alerts[MAX_ALERTS];
        int nvehicles;
        int nalerts;
        const ToulouseLine *line;

        if (ensure_toulouse_snapshot() < 0) {
            return send_error_json(fd, 503, "upstream_unavailable", "Unable to load line metadata.");
        }
        line = find_toulouse_line_by_id(g_tls_lines, g_tls_nlines, line_gid);
        if (!line) return send_error_json(fd, 404, "line_not_found", "Unknown line.");

        nvehicles = fetch_toulouse_vehicles(line, vehicles, MAX_VEHICLES);
        if (nvehicles < 0) return send_error_json(fd, 503, "upstream_unavailable", "Unable to load vehicles.");

        nalerts = fetch_toulouse_alerts(alerts, MAX_ALERTS);
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

        if (ensure_stop_map() < 0) return send_error_json(fd, 503, "upstream_unavailable", "Unable to load stop lookup data.");

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

static int handle_mapkit_token(int fd)
{
    cJSON *root;
    const char *token = getenv("MAPKIT_JS_TOKEN");
    if (!token || !token[0]) {
        return send_error_json(fd, 503, "mapkit_not_configured",
                               "Set MAPKIT_JS_TOKEN before requesting a MapKit token.");
    }
    root = cJSON_CreateObject();
    add_generated_at(root);
    cJSON_AddStringToObject(root, "token", token);
    int rc = send_json(fd, 200, "OK", root, "no-store");
    cJSON_Delete(root);
    return rc;
}

static int handle_root(int fd)
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
    cJSON_AddItemToArray(endpoints, cJSON_CreateString("/api/health"));
    cJSON_AddItemToArray(endpoints, cJSON_CreateString("/api/lines?network=bdx|tls"));
    cJSON_AddItemToArray(endpoints, cJSON_CreateString("/api/alerts?network=bdx|tls"));
    cJSON_AddItemToArray(endpoints, cJSON_CreateString("/api/lines/:gid/vehicles?network=bdx|tls"));
    cJSON_AddItemToArray(endpoints, cJSON_CreateString("/api/stop-groups?network=bdx|tls"));
    cJSON_AddItemToArray(endpoints, cJSON_CreateString("/api/stop-groups/:key/passages?network=bdx|tls"));
    cJSON_AddItemToArray(endpoints, cJSON_CreateString("/api/map/boundaries?network=bdx|tls"));
    cJSON_AddItemToArray(endpoints, cJSON_CreateString("/api/mapkit/token"));
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
    if (request_network(req, &network) < 0) {
        return send_error_json(fd, 400, "invalid_network", "Use network=bdx or network=tls.");
    }

    if (strcmp(req->path, "/") == 0) return handle_root(fd);
    if (strcmp(req->path, "/api/health") == 0) return handle_health(fd);
    if (strcmp(req->path, "/api/lines") == 0) return handle_lines(fd, network);
    if (strcmp(req->path, "/api/alerts") == 0) return handle_alerts(fd, network);
    if (strcmp(req->path, "/api/stop-groups") == 0) return handle_stop_groups(fd, network);
    if (strcmp(req->path, "/api/map/boundaries") == 0) return handle_map_boundaries(fd, network);
    if (strcmp(req->path, "/api/mapkit/token") == 0) return handle_mapkit_token(fd);
    if (sscanf(req->path, "/api/lines/%d/vehicles", &gid) == 1) return handle_vehicles(fd, gid, network);

    prefix = "/api/stop-groups/";
    if (strncmp(req->path, prefix, strlen(prefix)) == 0) {
        const char *rest = req->path + strlen(prefix);
        const char *slash = strstr(rest, "/passages");
        char key[128];
        if (slash && strcmp(slash, "/passages") == 0) {
            size_t len = (size_t)(slash - rest);
            if (len < sizeof(key)) {
                memcpy(key, rest, len);
                key[len] = '\0';
                return handle_stop_group_passages(fd, key, network);
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
