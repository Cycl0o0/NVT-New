#include "filter.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static int nvt_type_order(const char *vehicle_type)
{
    if (strcmp(vehicle_type, "TRAM") == 0) return 0;
    if (strcmp(vehicle_type, "BUS") == 0) return 1;
    return 2;
}

static int nvt_casecmp_s(const char *left, const char *right)
{
    while (*left && *right) {
        int delta = tolower((unsigned char)*left) - tolower((unsigned char)*right);

        if (delta) return delta;
        left++;
        right++;
    }
    return tolower((unsigned char)*left) - tolower((unsigned char)*right);
}

const char *nvt_idfm_line_type_label(const ToulouseLine *line)
{
    const char *mode = line ? line->mode : "";

    if (nvt_strcasestr_s(mode, "metro") || nvt_strcasestr_s(mode, "métro")) return "METRO";
    if (nvt_strcasestr_s(mode, "rer") || nvt_strcasestr_s(mode, "rapidtransit") || nvt_strcasestr_s(mode, "rapid transit")) return "RER";
    if (nvt_strcasestr_s(mode, "tram")) return "TRAM";
    if (nvt_strcasestr_s(mode, "train")
        || nvt_strcasestr_s(mode, "rail")
        || nvt_strcasestr_s(mode, "transilien")
        || nvt_strcasestr_s(mode, "ter")
        || nvt_strcasestr_s(mode, "intercite")
        || nvt_strcasestr_s(mode, "intercité")) {
        return "TRAIN";
    }
    if (nvt_strcasestr_s(mode, "coach") || nvt_strcasestr_s(mode, "car")) return "BUS";
    return "BUS";
}

static int nvt_idfm_type_order(const ToulouseLine *line)
{
    const char *type = nvt_idfm_line_type_label(line);

    if (strcmp(type, "METRO") == 0) return 0;
    if (strcmp(type, "RER") == 0) return 1;
    if (strcmp(type, "TRAM") == 0) return 2;
    if (strcmp(type, "TRAIN") == 0) return 3;
    if (strcmp(type, "BUS") == 0) return 4;
    return 5;
}

int nvt_cmp_lines(const void *a, const void *b)
{
    const Line *left = a;
    const Line *right = b;
    int delta = nvt_type_order(left->vehicule) - nvt_type_order(right->vehicule);

    return delta ? delta : left->ident - right->ident;
}

int nvt_strcasestr_s(const char *haystack, const char *needle)
{
    size_t needle_len = strlen(needle);
    size_t haystack_len = strlen(haystack);

    if (!needle_len) return 1;
    if (needle_len > haystack_len) return 0;

    for (size_t i = 0; i + needle_len <= haystack_len; i++) {
        size_t j = 0;

        for (; j < needle_len; j++) {
            if (tolower((unsigned char)haystack[i + j]) != tolower((unsigned char)needle[j])) break;
        }
        if (j == needle_len) return 1;
    }

    return 0;
}

int nvt_match_offset(const char *haystack, const char *needle)
{
    size_t needle_len = strlen(needle);
    size_t haystack_len = strlen(haystack);

    if (!needle_len) return -1;
    if (needle_len > haystack_len) return -1;

    for (size_t i = 0; i + needle_len <= haystack_len; i++) {
        size_t j = 0;

        for (; j < needle_len; j++) {
            if (tolower((unsigned char)haystack[i + j]) != tolower((unsigned char)needle[j])) break;
        }
        if (j == needle_len) return (int)i;
    }

    return -1;
}

int nvt_line_matches_search(const Line *line, const char *search)
{
    char id[16];

    if (!line->active) return 0;
    if (!search[0]) return 1;

    snprintf(id, sizeof(id), "%d", line->ident);
    return nvt_strcasestr_s(line->libelle, search)
        || nvt_strcasestr_s(line->vehicule, search)
        || nvt_strcasestr_s(id, search);
}

int nvt_line_matches_atlas(const Line *line, const char *search)
{
    char id[16];

    if (!search[0]) return 1;

    snprintf(id, sizeof(id), "%d", line->ident);
    return nvt_strcasestr_s(line->libelle, search)
        || nvt_strcasestr_s(line->vehicule, search)
        || nvt_strcasestr_s(id, search);
}

int nvt_find_line_index_by_gid(const Line *lines, int nlines, int gid)
{
    for (int i = 0; i < nlines; i++) {
        if (lines[i].gid == gid) return i;
    }
    return -1;
}

int nvt_rebuild_line_filter(const Line *lines, int nlines, const char *search, int *filtered, int max_filtered)
{
    int count = 0;

    for (int i = 0; i < nlines && count < max_filtered; i++) {
        if (!nvt_line_matches_search(&lines[i], search)) continue;
        filtered[count++] = i;
    }

    return count;
}

int nvt_rebuild_atlas_filter(const Line *lines, int nlines, const char *search, int *filtered, int max_filtered)
{
    int count = 0;

    for (int i = 0; i < nlines && count < max_filtered; i++) {
        if (!nvt_line_matches_atlas(&lines[i], search)) continue;
        filtered[count++] = i;
    }

    return count;
}

int nvt_rebuild_stop_filter(const StopGroup *groups, int ngroups, const char *search, int *filtered, int max_filtered)
{
    int count = 0;

    for (int i = 0; i < ngroups && count < max_filtered; i++) {
        if (search[0] && !nvt_strcasestr_s(groups[i].libelle, search)) continue;
        filtered[count++] = i;
    }

    return count;
}

int nvt_toulouse_mode_is_rail(const char *mode)
{
    return nvt_strcasestr_s(mode, "tram")
        || nvt_strcasestr_s(mode, "metro")
        || nvt_strcasestr_s(mode, "métro");
}

int nvt_cmp_idfm_lines(const void *a, const void *b)
{
    const ToulouseLine *left = a;
    const ToulouseLine *right = b;
    int delta = nvt_idfm_type_order(left) - nvt_idfm_type_order(right);

    if (delta) return delta;
    delta = nvt_casecmp_s(left->code, right->code);
    if (delta) return delta;
    delta = nvt_casecmp_s(left->libelle, right->libelle);
    if (delta) return delta;
    return nvt_casecmp_s(left->ref, right->ref);
}

int nvt_toulouse_list_has_token(const char *list, const char *token)
{
    size_t token_len;
    const char *cursor;

    if (!list || !token || !token[0]) return 0;

    token_len = strlen(token);
    cursor = list;
    while (*cursor) {
        while (*cursor == ' ' || *cursor == ',' || *cursor == ';' || *cursor == '|') cursor++;
        if (!*cursor) break;
        if (strncmp(cursor, token, token_len) == 0) {
            char tail = cursor[token_len];
            if (tail == '\0' || tail == ' ' || tail == ',' || tail == ';' || tail == '|') return 1;
        }
        while (*cursor && *cursor != ' ' && *cursor != ',' && *cursor != ';' && *cursor != '|') cursor++;
    }

    return 0;
}

int nvt_toulouse_line_matches_search(const ToulouseLine *line, const char *search)
{
    if (!search[0]) return 1;
    return nvt_strcasestr_s(line->libelle, search)
        || nvt_strcasestr_s(line->code, search)
        || nvt_strcasestr_s(line->mode, search)
        || nvt_strcasestr_s(line->couleur, search);
}

int nvt_rebuild_toulouse_line_filter(const ToulouseLine *lines, int nlines, const char *search, int *filtered, int max_filtered)
{
    int count = 0;

    for (int i = 0; i < nlines && count < max_filtered; i++) {
        if (!nvt_toulouse_line_matches_search(&lines[i], search)) continue;
        filtered[count++] = i;
    }

    return count;
}

int nvt_rebuild_toulouse_stop_filter(const ToulouseStop *stops, int nstops, const char *search, int *filtered, int max_filtered)
{
    int count = 0;

    for (int i = 0; i < nstops && count < max_filtered; i++) {
        if (search[0]
            && !nvt_strcasestr_s(stops[i].libelle, search)
            && !nvt_strcasestr_s(stops[i].commune, search)
            && !nvt_strcasestr_s(stops[i].lignes, search)) {
            continue;
        }
        filtered[count++] = i;
    }

    return count;
}

int nvt_toulouse_waiting_minutes(const char *waiting_time)
{
    int hours = 0;
    int minutes = -1;
    int seconds = 0;

    if (!waiting_time || !waiting_time[0]) return -1;
    if (strcmp(waiting_time, "0") == 0) return 0;
    if (sscanf(waiting_time, "%d:%d:%d", &hours, &minutes, &seconds) == 3) {
        return hours * 60 + minutes + (seconds > 0 ? 1 : 0);
    }
    if (sscanf(waiting_time, "%d", &minutes) == 1) return minutes;
    return -1;
}

int nvt_hhmm_to_minutes(const char *hhmm)
{
    if (!hhmm || !hhmm[0]) return 0;
    return ((hhmm[0] - '0') * 10 + (hhmm[1] - '0')) * 60
        + (hhmm[3] - '0') * 10
        + (hhmm[4] - '0');
}
