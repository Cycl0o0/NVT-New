#include "api.h"
#include "app_state.h"
#include "config.h"
#include "data.h"
#include "filter.h"
#include "line_colors.h"
#include "itinerary.h"
#include "map_math.h"
#include "network.h"
#include "ui.h"

#include <ncurses.h>
#include <locale.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include <stdarg.h>
#include <wchar.h>
#include <wctype.h>

/* ── Unicode symbols ─────────────────────────────────────────────── */

#define U_BLOCK     "\xe2\x96\x88"   /* █ */
#define U_BLIGHT    "\xe2\x96\x91"   /* ░ */
#define U_CBAR      "\xe2\x96\x8c"   /* ▌ */
#define U_BULLET    "\xe2\x97\x8f"   /* ● */
#define U_BULLETO   "\xe2\x97\x8b"   /* ○ */
#define U_ARROW     "\xe2\x86\x92"   /* → */
#define U_CHECK     "\xe2\x9c\x93"   /* ✓ */
#define U_WARN      "\xe2\x9a\xa0"   /* ⚠ */
#define U_INFO      "\xe2\x84\xb9"   /* ℹ */
#define U_DIAMOND   "\xe2\x97\x86"   /* ◆ */
#define U_MDOT      "\xc2\xb7"       /* · */
#define U_ELLIP     "\xe2\x80\xa6"   /* … */
#define U_EMDASH    "\xe2\x80\x94"   /* — */

/* Powerline glyphs (Nerd Font required for best look) */
#define PL_R        "\xee\x82\xb0"   /*  */
#define PL_RS       "\xee\x82\xb1"   /*  */
#define PL_L        "\xee\x82\xb2"   /*  */

/* Rounded box drawing */
#define RB_TL       "\xe2\x95\xad"   /* ╭ */
#define RB_TR       "\xe2\x95\xae"   /* ╮ */
#define RB_BL       "\xe2\x95\xb0"   /* ╰ */
#define RB_BR       "\xe2\x95\xaf"   /* ╯ */
#define RB_H        "\xe2\x94\x80"   /* ─ */
#define RB_V        "\xe2\x94\x82"   /* │ */

/* ── Theme system ────────────────────────────────────────────────── */

typedef struct {
    const char *name;
    int accent;      /* 256-color fg/bg accent */
    int header_bg;   /* dark bg for header/tabs/status */
    int sel_bg;      /* selected row bg */
    int dim_fg;      /* dimmed text fg on header_bg */
} Theme;

static const Theme themes[] = {
    { "Lagoon",     51,  236, 237, 245 },
    { "Nord",       110, 236, 238, 245 },
    { "Rose",       211, 235, 237, 246 },
    { "Ayu",        214, 235, 237, 246 },
    { "Jade",       84,  236, 238, 245 },
    { "Matrix",     47,  232, 236, 242 },
    { "Mono",       250, 237, 240, 246 },
};
#define N_THEMES ((int)(sizeof(themes)/sizeof(themes[0])))
#define MAX_VEHICLE_ZOOM 5

/* ── runtime state ───────────────────────────────────────────────── */

static AppState g_app;
static NvtItineraryState g_itinerary;

#define g_lines (g_app.bdx.lines)
#define g_nlines (g_app.bdx.nlines)
#define g_vehicles (g_app.bdx.vehicles)
#define g_nvehicles (g_app.bdx.nvehicles)
#define g_alerts (g_app.bdx.alerts)
#define g_nalerts (g_app.bdx.nalerts)
#define g_stops (g_app.bdx.stops)
#define g_stop_groups (g_app.bdx.stop_groups)
#define g_nstop_groups (g_app.bdx.nstop_groups)
#define g_passages (g_app.bdx.passages)
#define g_npassages (g_app.bdx.npassages)
#define g_sel_stop_group (g_app.bdx.sel_stop_group)
#define g_stop_filtered (g_app.bdx.stop_filtered)
#define g_nstop_filtered (g_app.bdx.nstop_filtered)
#define g_stop_search (g_app.bdx.stop_search)
#define g_filtered (g_app.bdx.filtered)
#define g_nfiltered (g_app.bdx.nfiltered)
#define g_search (g_app.bdx.search)
#define g_cursor (g_app.bdx.cursor)
#define g_scroll (g_app.bdx.scroll)
#define g_sel_line (g_app.bdx.sel_line)

#define g_tls_snapshot (g_app.tls.snapshot)
#define g_tls_lines (g_app.tls.lines)
#define g_ntls_lines (g_app.tls.nlines)
#define g_tls_stops (g_app.tls.stops)
#define g_ntls_stops (g_app.tls.nstops)
#define g_tls_alerts (g_app.tls.alerts)
#define g_ntls_alerts (g_app.tls.nalerts)
#define g_tls_passages (g_app.tls.passages)
#define g_ntls_passages (g_app.tls.npassages)
#define g_tls_vehicles (g_app.tls.vehicles)
#define g_ntls_vehicles (g_app.tls.nvehicles)
#define g_tls_filtered (g_app.tls.filtered)
#define g_ntls_filtered (g_app.tls.nfiltered)
#define g_tls_search (g_app.tls.search)
#define g_tls_cursor (g_app.tls.cursor)
#define g_tls_scroll (g_app.tls.scroll)
#define g_tls_sel_line (g_app.tls.sel_line)
#define g_tls_stop_filtered (g_app.tls.stop_filtered)
#define g_ntls_stop_filtered (g_app.tls.nstop_filtered)
#define g_tls_stop_search (g_app.tls.stop_search)
#define g_tls_stop_cursor (g_app.tls.stop_cursor)
#define g_tls_stop_scroll (g_app.tls.stop_scroll)
#define g_tls_sel_stop (g_app.tls.sel_stop)

#define g_idf_snapshot (g_app.idf.snapshot)
#define g_idf_lines (g_app.idf.lines)
#define g_nidf_lines (g_app.idf.nlines)
#define g_idf_stops (g_app.idf.stops)
#define g_nidf_stops (g_app.idf.nstops)
#define g_idf_alerts (g_app.idf.alerts)
#define g_nidf_alerts (g_app.idf.nalerts)
#define g_idf_passages (g_app.idf.passages)
#define g_nidf_passages (g_app.idf.npassages)
#define g_idf_vehicles (g_app.idf.vehicles)
#define g_nidf_vehicles (g_app.idf.nvehicles)
#define g_idf_filtered (g_app.idf.filtered)
#define g_nidf_filtered (g_app.idf.nfiltered)
#define g_idf_search (g_app.idf.search)
#define g_idf_cursor (g_app.idf.cursor)
#define g_idf_scroll (g_app.idf.scroll)
#define g_idf_sel_line (g_app.idf.sel_line)
#define g_idf_stop_filtered (g_app.idf.stop_filtered)
#define g_nidf_stop_filtered (g_app.idf.nstop_filtered)
#define g_idf_stop_search (g_app.idf.stop_search)
#define g_idf_stop_cursor (g_app.idf.stop_cursor)
#define g_idf_stop_scroll (g_app.idf.stop_scroll)
#define g_idf_sel_stop (g_app.idf.sel_stop)

#define g_course_cache (g_app.map.course_cache)
#define g_metro_map (g_app.map.metro_map)
#define g_has_metro_map (g_app.map.has_metro_map)
#define g_map_attempted (g_app.map.map_attempted)
#define g_tls_metro_map (g_app.map.tls_metro_map)
#define g_has_tls_metro_map (g_app.map.has_tls_metro_map)
#define g_tls_map_attempted (g_app.map.tls_map_attempted)
#define g_line_route (g_app.map.line_route)
#define g_has_line_route (g_app.map.has_line_route)
#define g_line_route_gid (g_app.map.line_route_gid)
#define g_tls_line_route (g_app.map.tls_line_route)
#define g_has_tls_line_route (g_app.map.has_tls_line_route)
#define g_tls_line_route_ref (g_app.map.tls_line_route_ref)
#define g_vehicle_detail_map (g_app.map.vehicle_detail_map)
#define g_has_vehicle_detail_map (g_app.map.has_vehicle_detail_map)
#define g_vehicle_detail_map_valid (g_app.map.vehicle_detail_map_valid)
#define g_vehicle_detail_map_zoom (g_app.map.vehicle_detail_map_zoom)
#define g_vehicle_detail_map_line_gid (g_app.map.vehicle_detail_map_line_gid)
#define g_atlas_map (g_app.map.atlas_map)
#define g_has_atlas_map (g_app.map.has_atlas_map)
#define g_atlas_map_attempted (g_app.map.atlas_map_attempted)
#define g_atlas_routes (g_app.map.atlas_routes)
#define g_has_atlas_routes (g_app.map.has_atlas_routes)
#define g_atlas_routes_attempted (g_app.map.atlas_routes_attempted)

#define g_atlas_filtered (g_app.ui.atlas_filtered)
#define g_natlas_filtered (g_app.ui.natlas_filtered)
#define g_atlas_search (g_app.ui.atlas_search)
#define g_atlas_cursor (g_app.ui.atlas_cursor)
#define g_atlas_scroll (g_app.ui.atlas_scroll)
#define g_atlas_focus_gid (g_app.ui.atlas_focus_gid)
#define g_screen (g_app.ui.screen)
#define g_network (g_app.ui.network)
#define g_theme (g_app.ui.theme)
#define g_256 (g_app.ui.use_256)
#define g_vehicle_zoom (g_app.ui.vehicle_zoom)
#define g_show_help (g_app.ui.show_help)
#define g_alert_scroll (g_app.ui.alert_scroll)
#define g_alert_total_h (g_app.ui.alert_total_h)
#define g_network_loaded (g_app.ui.network_loaded)
#define g_toast (g_app.ui.toast)
#define g_toast_time (g_app.ui.toast_time)

/* ── sorting / filtering ─────────────────────────────────────────── */

static int strcasestr_s(const char *h, const char *n)
{
    return nvt_strcasestr_s(h, n);
}

static int line_matches_atlas(const Line *l, const char *search)
{
    return nvt_line_matches_atlas(l, search);
}

static int line_idx_by_gid(int gid)
{
    return nvt_find_line_index_by_gid(g_lines, g_nlines, gid);
}

static void rebuild_filter(void)
{
    g_nfiltered = nvt_rebuild_line_filter(g_lines, g_nlines, g_search, g_filtered, MAX_LINES);
}

static void rebuild_atlas_filter(void)
{
    g_natlas_filtered = nvt_rebuild_atlas_filter(
        g_lines,
        g_nlines,
        g_atlas_search,
        g_atlas_filtered,
        MAX_LINES
    );
    if (g_atlas_focus_gid && line_idx_by_gid(g_atlas_focus_gid) < 0) g_atlas_focus_gid = 0;
    if (g_atlas_cursor >= g_natlas_filtered) g_atlas_cursor = g_natlas_filtered > 0 ? g_natlas_filtered - 1 : 0;
    if (g_atlas_cursor < 0) g_atlas_cursor = 0;
}

static void rebuild_stop_filter(void)
{
    g_nstop_filtered = nvt_rebuild_stop_filter(
        g_stop_groups,
        g_nstop_groups,
        g_stop_search,
        g_stop_filtered,
        MAX_STOP_GROUPS
    );
}

static int count_active_lines_all(void)
{
    int n=0;
    for(int i=0;i<g_nlines;i++) if(g_lines[i].active) n++;
    return n;
}

static int count_lines_type(const char *type)
{
    int n=0;
    for(int i=0;i<g_nlines;i++) if(g_lines[i].active&&strcmp(g_lines[i].vehicule,type)==0) n++;
    return n;
}

static int count_alerts_prefix(const char *prefix)
{
    int n=0;
    size_t pl=strlen(prefix);
    for(int i=0;i<g_nalerts;i++) if(strncmp(g_alerts[i].severite,prefix,pl)==0) n++;
    return n;
}

static int count_line_alerts(int line_gid)
{
    int n=0;
    for(int i=0;i<g_nalerts;i++) if(g_alerts[i].ligne_id==line_gid) n++;
    return n;
}

static int count_alerted_lines(void)
{
    int n=0;
    for(int i=0;i<g_nlines;i++) if(g_lines[i].active&&count_line_alerts(g_lines[i].gid)>0) n++;
    return n;
}

static int atlas_route_visible_gid(int gid)
{
    int idx = line_idx_by_gid(gid);
    if (g_atlas_focus_gid) return gid == g_atlas_focus_gid;
    if (idx < 0) return !g_atlas_search[0];
    return line_matches_atlas(&g_lines[idx], g_atlas_search);
}

static void line_code(const Line *l, char *buf, size_t sz)
{
    if(strcmp(l->vehicule,"TRAM")==0&&l->libelle[5]){
        const char *p=strrchr(l->libelle,' ');
        snprintf(buf,sz,"%s",p?p+1:"?");
        return;
    }
    snprintf(buf,sz,"%d",l->ident);
}

static const Line *line_by_gid(int gid)
{
    int idx = line_idx_by_gid(gid);
    return idx >= 0 ? &g_lines[idx] : NULL;
}

static int count_vehicles_dir(const char *dir)
{
    int n=0;
    for(int i=0;i<g_nvehicles;i++) if(strcmp(g_vehicles[i].sens,dir)==0) n++;
    return n;
}

static int count_delayed_vehicles_dir(const char *dir, int min_delay)
{
    int n=0;
    for(int i=0;i<g_nvehicles;i++)
        if(strcmp(g_vehicles[i].sens,dir)==0&&strcmp(g_vehicles[i].etat,"RETARD")==0&&g_vehicles[i].retard>=min_delay) n++;
    return n;
}

static int count_stopped_vehicles_dir(const char *dir)
{
    int n=0;
    for(int i=0;i<g_nvehicles;i++) if(strcmp(g_vehicles[i].sens,dir)==0&&g_vehicles[i].arret) n++;
    return n;
}

static int avg_speed_dir(const char *dir)
{
    int n=0, sum=0;
    for(int i=0;i<g_nvehicles;i++){
        if(strcmp(g_vehicles[i].sens,dir)!=0) continue;
        sum+=g_vehicles[i].vitesse;
        n++;
    }
    return n?sum/n:0;
}

static int count_live_passages(void)
{
    int n=0;
    for(int i=0;i<g_npassages;i++) if(g_passages[i].hor_estime[0]) n++;
    return n;
}

static int count_delayed_passages(void)
{
    int n=0;
    for(int i=0;i<g_npassages;i++){
        const Passage *p=&g_passages[i];
        if(!p->hor_estime[0]||!p->hor_theo[0]) continue;
        if(strcmp(p->hor_estime,p->hor_theo)>0) n++;
    }
    return n;
}

static int count_unique_passage_lines(void)
{
    int n=0;
    for(int i=0;i<g_npassages;i++){
        int dup=0;
        for(int j=0;j<i;j++) if(g_passages[j].ligne_id==g_passages[i].ligne_id){dup=1;break;}
        if(!dup&&g_passages[i].ligne_id) n++;
    }
    return n;
}

static int toulouse_mode_is_rail(const char *mode)
{
    return nvt_toulouse_mode_is_rail(mode);
}

static int count_toulouse_active_lines_all(void)
{
    return g_ntls_lines;
}

static int count_toulouse_lines_rail(void)
{
    int n = 0;
    for (int i = 0; i < g_ntls_lines; i++) if (toulouse_mode_is_rail(g_tls_lines[i].mode)) n++;
    return n;
}

static int count_toulouse_lines_busish(void)
{
    int n = 0;
    for (int i = 0; i < g_ntls_lines; i++) if (!toulouse_mode_is_rail(g_tls_lines[i].mode)) n++;
    return n;
}

static int toulouse_list_has_token(const char *list, const char *token);

static const char *toulouse_mode_label(const ToulouseLine *line)
{
    if (!line) return "BUS";
    if (strcasestr_s(line->mode, "metro") || strcasestr_s(line->mode, "métro")) return "METRO";
    if (strcasestr_s(line->mode, "tram")) return "TRAM";
    if (strcasestr_s(line->mode, "lin")) return "BUS";
    if (strcasestr_s(line->mode, "demande")) return "TAD";
    if (strcasestr_s(line->mode, "tele") || strcasestr_s(line->mode, "télé")) return "TELE";
    return "BUS";
}

static int toulouse_line_realtime_capable(const ToulouseLine *line)
{
    if (!line) return 0;
    if (strcasestr_s(line->mode, "metro") || strcasestr_s(line->mode, "métro")) return 0;
    if (strcasestr_s(line->mode, "demande")) return 0;
    return 1;
}

static int toulouse_alert_applies_to_line(const ToulouseAlert *alert, const ToulouseLine *line)
{
    if (!alert || !line || !alert->lines[0]) return 0;
    return toulouse_list_has_token(alert->lines, line->code);
}

static void rebuild_toulouse_filter(void)
{
    g_ntls_filtered = nvt_rebuild_toulouse_line_filter(
        g_tls_lines,
        g_ntls_lines,
        g_tls_search,
        g_tls_filtered,
        MAX_LINES
    );
    if (g_tls_cursor >= g_ntls_filtered) g_tls_cursor = g_ntls_filtered > 0 ? g_ntls_filtered - 1 : 0;
    if (g_tls_cursor < 0) g_tls_cursor = 0;
}

static void rebuild_toulouse_stop_filter(void)
{
    g_ntls_stop_filtered = nvt_rebuild_toulouse_stop_filter(
        g_tls_stops,
        g_ntls_stops,
        g_tls_stop_search,
        g_tls_stop_filtered,
        MAX_STOPS
    );
    if (g_tls_stop_cursor >= g_ntls_stop_filtered) g_tls_stop_cursor = g_ntls_stop_filtered > 0 ? g_ntls_stop_filtered - 1 : 0;
    if (g_tls_stop_cursor < 0) g_tls_stop_cursor = 0;
}

static int toulouse_list_has_token(const char *list, const char *token)
{
    return nvt_toulouse_list_has_token(list, token);
}

static int toulouse_token_count(const char *list)
{
    int n = 0;
    const char *p = list;
    while (*p) {
        while (*p == ' ') p++;
        if (!*p) break;
        while (*p && *p != ' ') p++;
        n++;
    }
    return n;
}

static int count_toulouse_line_alerts(const ToulouseLine *line)
{
    int n = 0;
    if (!line) return 0;
    for (int i = 0; i < g_ntls_alerts; i++) {
        if (toulouse_alert_applies_to_line(&g_tls_alerts[i], line)) n++;
    }
    return n;
}

static int count_toulouse_global_alerts(void)
{
    int n = 0;
    for (int i = 0; i < g_ntls_alerts; i++) {
        if (!g_tls_alerts[i].lines[0]) n++;
    }
    return n;
}

static int count_toulouse_impacted_lines(void)
{
    int n = 0;
    for (int i = 0; i < g_ntls_lines; i++) {
        if (count_toulouse_line_alerts(&g_tls_lines[i]) > 0) n++;
    }
    return n;
}

static int count_toulouse_live_passages(void)
{
    int n = 0;
    for (int i = 0; i < g_ntls_passages; i++) if (g_tls_passages[i].realtime) n++;
    return n;
}

static int count_toulouse_unique_passage_lines(void)
{
    int n = 0;
    for (int i = 0; i < g_ntls_passages; i++) {
        int dup = 0;
        for (int j = 0; j < i; j++) {
            if (strcmp(g_tls_passages[i].line_code, g_tls_passages[j].line_code) == 0) {
                dup = 1;
                break;
            }
        }
        if (!dup && g_tls_passages[i].line_code[0]) n++;
    }
    return n;
}

static int toulouse_waiting_minutes(const char *waiting_time)
{
    return nvt_toulouse_waiting_minutes(waiting_time);
}

static void toulouse_waiting_eta(const char *waiting_time, char *buf, size_t sz)
{
    int mins = toulouse_waiting_minutes(waiting_time);
    if (mins < 0) snprintf(buf, sz, "--");
    else if (mins <= 1) snprintf(buf, sz, "NOW");
    else snprintf(buf, sz, "%dmin", mins);
}

static void toulouse_passage_clock(const ToulousePassage *p, char *buf, size_t sz)
{
    if (!p || !p->datetime[0] || strlen(p->datetime) < 16) {
        snprintf(buf, sz, "--:--");
        return;
    }
    snprintf(buf, sz, "%.*s", 5, p->datetime + 11);
}

static int count_toulouse_delayed_passages(void)
{
    int n = 0;
    for (int i = 0; i < g_ntls_passages; i++) if (!g_tls_passages[i].realtime) n++;
    return n;
}

static ToulouseLine *selected_toulouse_line(void)
{
    if (g_tls_sel_line >= 0 && g_tls_sel_line < g_ntls_lines) return &g_tls_lines[g_tls_sel_line];
    if (g_ntls_filtered <= 0) return NULL;
    return &g_tls_lines[g_tls_filtered[g_tls_cursor]];
}

static const ToulouseLine *toulouse_line_by_code(const char *code)
{
    if (!code || !code[0]) return NULL;
    for (int i = 0; i < g_ntls_lines; i++) if (strcmp(g_tls_lines[i].code, code) == 0) return &g_tls_lines[i];
    return NULL;
}
static void draw_toulouse_badge_by_code(int y, int x, const char *code);

static int count_toulouse_vehicles_dir(const char *dir)
{
    int n = 0;
    for (int i = 0; i < g_ntls_vehicles; i++) if (strcmp(g_tls_vehicles[i].sens, dir) == 0) n++;
    return n;
}

static int count_toulouse_delayed_vehicles_dir(const char *dir)
{
    int n = 0;
    for (int i = 0; i < g_ntls_vehicles; i++) if (strcmp(g_tls_vehicles[i].sens, dir) == 0 && g_tls_vehicles[i].delayed) n++;
    return n;
}

static int count_toulouse_stopped_vehicles_dir(const char *dir)
{
    int n = 0;
    for (int i = 0; i < g_ntls_vehicles; i++) if (strcmp(g_tls_vehicles[i].sens, dir) == 0 && g_tls_vehicles[i].arret) n++;
    return n;
}

static int avg_toulouse_speed_dir(const char *dir)
{
    int n = 0, sum = 0;
    for (int i = 0; i < g_ntls_vehicles; i++) {
        if (strcmp(g_tls_vehicles[i].sens, dir) != 0) continue;
        sum += g_tls_vehicles[i].vitesse;
        n++;
    }
    return n ? sum / n : 0;
}

static ToulouseStop *selected_toulouse_stop(void)
{
    if (g_ntls_stop_filtered <= 0) return NULL;
    return &g_tls_stops[g_tls_stop_filtered[g_tls_stop_cursor]];
}

static int live_network_is_sncf(void)
{
    return g_network == NET_SNCF;
}

static NvtIdfmState *current_live_state(void)
{
    return live_network_is_sncf() ? (NvtIdfmState *)&g_app.sncf : &g_app.idf;
}

static const char *current_live_network_short(void)
{
    return live_network_is_sncf() ? "SNCF" : "IDFM";
}

#define g_live_snapshot (current_live_state()->snapshot)
#define g_live_lines (current_live_state()->lines)
#define g_nlive_lines (current_live_state()->nlines)
#define g_live_stops (current_live_state()->stops)
#define g_nlive_stops (current_live_state()->nstops)
#define g_live_alerts (current_live_state()->alerts)
#define g_nlive_alerts (current_live_state()->nalerts)
#define g_live_passages (current_live_state()->passages)
#define g_nlive_passages (current_live_state()->npassages)
#define g_live_vehicles (current_live_state()->vehicles)
#define g_nlive_vehicles (current_live_state()->nvehicles)
#define g_live_filtered (current_live_state()->filtered)
#define g_nlive_filtered (current_live_state()->nfiltered)
#define g_live_search (current_live_state()->search)
#define g_live_cursor (current_live_state()->cursor)
#define g_live_scroll (current_live_state()->scroll)
#define g_live_sel_line (current_live_state()->sel_line)
#define g_live_stop_filtered (current_live_state()->stop_filtered)
#define g_nlive_stop_filtered (current_live_state()->nstop_filtered)
#define g_live_stop_search (current_live_state()->stop_search)
#define g_live_stop_cursor (current_live_state()->stop_cursor)
#define g_live_stop_scroll (current_live_state()->stop_scroll)
#define g_live_sel_stop (current_live_state()->sel_stop)

static void rebuild_idfm_filter(void)
{
    NvtIdfmState *st = current_live_state();

    st->nfiltered = nvt_rebuild_toulouse_line_filter(
        st->lines,
        st->nlines,
        st->search,
        st->filtered,
        MAX_LINES
    );
    if (st->cursor >= st->nfiltered) st->cursor = st->nfiltered > 0 ? st->nfiltered - 1 : 0;
    if (st->cursor < 0) st->cursor = 0;
}

static void toast(const char *fmt, ...);

static void reset_idfm_stop_view(int clear_query)
{
    NvtIdfmState *st = current_live_state();

    st->nstop_filtered = 0;
    st->npassages = 0;
    st->sel_stop = -1;
    st->stop_cursor = 0;
    st->stop_scroll = 0;
    if (clear_query) memset(st->stop_search, 0, sizeof(st->stop_search));
}

static void reset_idfm_stop_results(int clear_query)
{
    current_live_state()->nstops = 0;
    reset_idfm_stop_view(clear_query);
}

static void open_idfm_stop_search(int line_index, int clear_query)
{
    NvtIdfmState *st = current_live_state();

    if (line_index < 0 || line_index >= st->nlines) {
        toast("Selectionnez d'abord une ligne");
        return;
    }
    st->sel_line = line_index;
    reset_idfm_stop_results(clear_query);
    g_screen = SCR_STOP_SEARCH;
}

static ToulouseLine *selected_idfm_line(void)
{
    NvtIdfmState *st = current_live_state();

    if (st->sel_line >= 0 && st->sel_line < st->nlines) return &st->lines[st->sel_line];
    if (st->nfiltered <= 0) return NULL;
    return &st->lines[st->filtered[st->cursor]];
}

static const ToulouseLine *idfm_line_by_code(const char *code)
{
    NvtIdfmState *st = current_live_state();

    if (!code || !code[0]) return NULL;
    for (int i = 0; i < st->nlines; i++) {
        if (strcmp(st->lines[i].code, code) == 0) return &st->lines[i];
    }
    return NULL;
}

static ToulouseStop *selected_idfm_stop(void)
{
    NvtIdfmState *st = current_live_state();

    if (st->nstop_filtered <= 0) return NULL;
    return &st->stops[st->stop_filtered[st->stop_cursor]];
}

static int count_idfm_active_lines_all(void)
{
    return current_live_state()->nlines;
}

static int count_idfm_lines_rail(void)
{
    NvtIdfmState *st = current_live_state();
    int n = 0;
    for (int i = 0; i < st->nlines; i++) {
        const char *type = nvt_idfm_line_type_label(&st->lines[i]);
        if (strcmp(type, "METRO") == 0 || strcmp(type, "RER") == 0 || strcmp(type, "TRAM") == 0 || strcmp(type, "TRAIN") == 0) n++;
    }
    return n;
}

static int count_idfm_lines_busish(void)
{
    NvtIdfmState *st = current_live_state();
    int n = 0;
    for (int i = 0; i < st->nlines; i++) {
        if (strcmp(nvt_idfm_line_type_label(&st->lines[i]), "BUS") == 0) n++;
    }
    return n;
}

static int count_idfm_line_alerts(const ToulouseLine *line)
{
    NvtIdfmState *st = current_live_state();
    int n = 0;
    if (!line) return 0;
    for (int i = 0; i < st->nalerts; i++) {
        if (toulouse_alert_applies_to_line(&st->alerts[i], line)) n++;
    }
    return n;
}

static int count_idfm_global_alerts(void)
{
    NvtIdfmState *st = current_live_state();
    int n = 0;
    for (int i = 0; i < st->nalerts; i++) if (!st->alerts[i].lines[0]) n++;
    return n;
}

static int count_idfm_impacted_lines(void)
{
    NvtIdfmState *st = current_live_state();
    int n = 0;
    for (int i = 0; i < st->nlines; i++) if (count_idfm_line_alerts(&st->lines[i]) > 0) n++;
    return n;
}

static int count_idfm_live_passages(void)
{
    NvtIdfmState *st = current_live_state();
    int n = 0;
    for (int i = 0; i < st->npassages; i++) if (st->passages[i].realtime) n++;
    return n;
}

static int count_idfm_delayed_passages(void)
{
    NvtIdfmState *st = current_live_state();
    int n = 0;
    for (int i = 0; i < st->npassages; i++) if (st->passages[i].delayed) n++;
    return n;
}

static int count_idfm_unique_passage_lines(void)
{
    NvtIdfmState *st = current_live_state();
    int n = 0;
    for (int i = 0; i < st->npassages; i++) {
        int dup = 0;
        for (int j = 0; j < i; j++) {
            if (strcmp(st->passages[i].line_code, st->passages[j].line_code) == 0) {
                dup = 1;
                break;
            }
        }
        if (!dup && st->passages[i].line_code[0]) n++;
    }
    return n;
}

static int idfm_alert_cp(const ToulouseAlert *alert);

static const char *network_name(void)
{
    return nvt_network_name(g_network);
}

static int cmpp(const void *a,const void *b);
static void reset_toulouse_line_route(void);
static void switch_network(NvtNetwork net)
{
    if (net == g_network) return;
    nvt_switch_network(&g_app, net);
    nvt_itinerary_reset(&g_itinerary);
}

static int refresh_current_network_overview(const char *success_msg)
{
    char err[128];
    const NvtNetworkAdapter *adapter = nvt_network_adapter(g_network);

    if (adapter->refresh_overview(&g_app, err, sizeof(err)) < 0) {
        toast("%s", err);
        return -1;
    }
    if (adapter->refresh_alerts(&g_app, err, sizeof(err)) < 0) {
        toast("%s", err);
        return -1;
    }
    if (success_msg && success_msg[0]) toast("%s", success_msg);
    return 0;
}

static int refresh_current_network_alerts(const char *success_fmt)
{
    char err[128];
    const NvtNetworkAdapter *adapter = nvt_network_adapter(g_network);
    int count;

    if (adapter->refresh_alerts(&g_app, err, sizeof(err)) < 0) {
        toast("%s", err);
        return -1;
    }
    if (g_network == NET_TLS) count = g_ntls_alerts;
    else if (g_network == NET_IDFM || g_network == NET_SNCF) count = current_live_state()->nalerts;
    else count = g_nalerts;
    if (success_fmt && success_fmt[0]) toast(success_fmt, count);
    return count;
}

static int load_current_network_stops(const char *success_fmt)
{
    char err[128];
    const NvtNetworkAdapter *adapter = nvt_network_adapter(g_network);
    int count;

    if (!adapter->load_stops) return 0;
    if (adapter->load_stops(&g_app, err, sizeof(err)) < 0) {
        toast("%s", err);
        return -1;
    }
    if (g_network == NET_TLS) count = g_ntls_stops;
    else if (g_network == NET_IDFM || g_network == NET_SNCF) count = current_live_state()->nstops;
    else count = g_nstop_groups;
    if (success_fmt && success_fmt[0]) toast(success_fmt, count);
    return count;
}

static int load_current_network_passages(const char *success_fmt)
{
    char err[128];
    const NvtNetworkAdapter *adapter = nvt_network_adapter(g_network);
    int count;

    if (adapter->load_passages(&g_app, err, sizeof(err)) < 0) {
        toast("%s", err);
        return -1;
    }
    if (g_network == NET_BDX && g_npassages > 1) qsort(g_passages, g_npassages, sizeof(Passage), cmpp);
    if (g_network == NET_TLS) count = g_ntls_passages;
    else if (g_network == NET_IDFM || g_network == NET_SNCF) count = current_live_state()->npassages;
    else count = g_npassages;
    if (success_fmt && success_fmt[0]) toast(success_fmt, count);
    return count;
}

static int load_current_network_vehicles(const char *success_fmt)
{
    char err[128];
    const NvtNetworkAdapter *adapter = nvt_network_adapter(g_network);
    int count;

    if (adapter->load_vehicles(&g_app, err, sizeof(err)) < 0) {
        toast("%s", err);
        return -1;
    }
    if (g_network == NET_TLS) count = g_ntls_vehicles;
    else if (g_network == NET_IDFM || g_network == NET_SNCF) count = current_live_state()->nvehicles;
    else count = g_nvehicles;
    if (success_fmt && success_fmt[0]) toast(success_fmt, count);
    return count;
}

/* ── toast ────────────────────────────────────────────────────────── */

static void toast(const char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    vsnprintf(g_toast, sizeof(g_toast), fmt, args);
    va_end(args);
    g_toast_time = time(NULL);
}

static void reset_line_route(void)
{
    nvt_app_reset_line_route(&g_app);
}

static void reset_vehicle_detail_map(void)
{
    nvt_app_reset_vehicle_detail_map(&g_app);
}

static void ensure_line_route(void)
{
    int line_gid = g_lines[g_sel_line].gid;

    if (g_line_route_gid == line_gid) return;
    g_line_route_gid = line_gid;
    g_has_line_route = fetch_line_route(line_gid, &g_line_route) > 0;
}

static void reset_toulouse_line_route(void)
{
    nvt_app_reset_toulouse_line_route(&g_app);
}

static void ensure_toulouse_line_route(void)
{
    ToulouseLine *line = selected_toulouse_line();

    if (!line || !line->ref[0]) return;
    if (strcmp(g_tls_line_route_ref, line->ref) == 0) return;
    snprintf(g_tls_line_route_ref, sizeof(g_tls_line_route_ref), "%s", line->ref);
    g_has_tls_line_route = fetch_toulouse_line_route(line, &g_tls_line_route) > 0;
}

static void xml_decode_basic_entities(const char *src, char *dst, size_t dst_sz)
{
    size_t di = 0;

    if (!dst_sz) return;
    dst[0] = '\0';
    if (!src) return;

    for (size_t si = 0; src[si] && di + 1 < dst_sz; ) {
        if (strncmp(src + si, "&amp;", 5) == 0) {
            dst[di++] = '&';
            si += 5;
        } else if (strncmp(src + si, "&apos;", 6) == 0) {
            dst[di++] = '\'';
            si += 6;
        } else if (strncmp(src + si, "&quot;", 6) == 0) {
            dst[di++] = '"';
            si += 6;
        } else if (strncmp(src + si, "&lt;", 4) == 0) {
            dst[di++] = '<';
            si += 4;
        } else if (strncmp(src + si, "&gt;", 4) == 0) {
            dst[di++] = '>';
            si += 4;
        } else {
            dst[di++] = src[si++];
        }
    }
    dst[di] = '\0';
}

static int xml_extract_tag_value(const char *line, const char *tag, char *out, size_t out_sz)
{
    char open[64];
    char close[64];
    const char *start;
    const char *end;
    size_t len;
    char raw[192];

    if (!line || !tag || !out || !out_sz) return 0;
    snprintf(open, sizeof(open), "<%s>", tag);
    snprintf(close, sizeof(close), "</%s>", tag);
    start = strstr(line, open);
    if (!start) return 0;
    start += strlen(open);
    end = strstr(start, close);
    if (!end || end <= start) return 0;
    len = (size_t)(end - start);
    if (len >= sizeof(raw)) len = sizeof(raw) - 1;
    memcpy(raw, start, len);
    raw[len] = '\0';
    xml_decode_basic_entities(raw, out, out_sz);
    return 1;
}

static void itinerary_set_terminal_labels(void)
{
    if (g_itinerary.nstops <= 0) return;
    snprintf(g_itinerary.direction_backward, sizeof(g_itinerary.direction_backward),
             "%s", g_itinerary.stops[0].name);
    snprintf(g_itinerary.direction_forward, sizeof(g_itinerary.direction_forward),
             "%s", g_itinerary.stops[g_itinerary.nstops - 1].name);
}

static void itinerary_finalize_load(void)
{
    nvt_itinerary_sort_by_order(&g_itinerary);
    itinerary_set_terminal_labels();
    g_itinerary.ready = g_itinerary.nstops > 0;
    g_itinerary.origin = g_itinerary.nstops > 0 ? 0 : -1;
    g_itinerary.destination = g_itinerary.nstops > 1 ? g_itinerary.nstops - 1 : g_itinerary.origin;
    g_itinerary.cursor = 0;
    g_itinerary.scroll = 0;
    nvt_itinerary_rebuild_filter(&g_itinerary);
}

static int bordeaux_line_file_matches(const char *path, const Line *line, const char *wanted_code)
{
    FILE *fp;
    char row[512];
    char short_name[32] = "";
    char public_code[32] = "";
    char line_name[96] = "";
    int in_line = 0;

    fp = fopen(path, "r");
    if (!fp) return 0;

    while (fgets(row, sizeof(row), fp)) {
        if (!in_line) {
            if (strstr(row, "<Line ")) in_line = 1;
            continue;
        }
        if (xml_extract_tag_value(row, "ShortName", short_name, sizeof(short_name))) continue;
        if (xml_extract_tag_value(row, "PublicCode", public_code, sizeof(public_code))) continue;
        if (!line_name[0]) xml_extract_tag_value(row, "Name", line_name, sizeof(line_name));
        if (strstr(row, "</Line>")) break;
    }

    fclose(fp);
    if (wanted_code[0] && (strcmp(short_name, wanted_code) == 0 || strcmp(public_code, wanted_code) == 0)) return 1;
    if (line && line->libelle[0] && strcmp(line_name, line->libelle) == 0) return 1;
    return 0;
}

static int find_bordeaux_line_file(const Line *line, char *path, size_t path_sz)
{
    char code[16];
    char compact[16];
    size_t ci = 0;
    DIR *dir;
    struct dirent *entry;

    if (!line || !path || !path_sz) return -1;
    line_code(line, code, sizeof(code));

    snprintf(path, path_sz, "lines/line_%s.xml", code);
    {
        FILE *probe = fopen(path, "r");
        if (probe) {
            fclose(probe);
            return 0;
        }
    }

    for (size_t i = 0; code[i] && ci + 1 < sizeof(compact); i++) {
        if (code[i] == ' ' || code[i] == '/') continue;
        compact[ci++] = code[i];
    }
    compact[ci] = '\0';
    if (compact[0]) {
        snprintf(path, path_sz, "lines/line_%s.xml", compact);
        {
            FILE *probe = fopen(path, "r");
            if (probe) {
                fclose(probe);
                return 0;
            }
        }
    }

    dir = opendir("lines");
    if (!dir) return -1;

    while ((entry = readdir(dir)) != NULL) {
        if (strncmp(entry->d_name, "line_", 5) != 0) continue;
        if (!strstr(entry->d_name, ".xml")) continue;
        snprintf(path, path_sz, "lines/%s", entry->d_name);
        if (bordeaux_line_file_matches(path, line, code)) {
            closedir(dir);
            return 0;
        }
    }

    closedir(dir);
    path[0] = '\0';
    return -1;
}

static int select_line_for_itinerary(void)
{
    switch (g_network) {
    case NET_TLS:
        if (g_screen == SCR_LINES && g_ntls_filtered > 0) g_tls_sel_line = g_tls_filtered[g_tls_cursor];
        else if (g_tls_sel_line < 0 && g_ntls_filtered > 0) g_tls_sel_line = g_tls_filtered[g_tls_cursor];
        return (g_tls_sel_line >= 0 && g_tls_sel_line < g_ntls_lines) ? 0 : -1;
    case NET_IDFM:
    case NET_SNCF:
        if (g_screen == SCR_LINES && g_nlive_filtered > 0) g_live_sel_line = g_live_filtered[g_live_cursor];
        else if (g_live_sel_line < 0 && g_nlive_filtered > 0) g_live_sel_line = g_live_filtered[g_live_cursor];
        return (g_live_sel_line >= 0 && g_live_sel_line < g_nlive_lines) ? 0 : -1;
    case NET_BDX:
    default:
        if (g_screen == SCR_LINES && g_nfiltered > 0) g_sel_line = g_filtered[g_cursor];
        return (g_sel_line >= 0 && g_sel_line < g_nlines) ? 0 : -1;
    }
}

static int itinerary_matches_current_line(void)
{
    char code[16];

    if (!g_itinerary.ready || g_itinerary.network_kind != (int)g_network) return 0;

    switch (g_network) {
    case NET_TLS: {
        ToulouseLine *line = selected_toulouse_line();
        return line && strcmp(g_itinerary.line_ref, line->ref) == 0;
    }
    case NET_IDFM:
    case NET_SNCF: {
        ToulouseLine *line = selected_idfm_line();
        return line && strcmp(g_itinerary.line_ref, line->ref) == 0;
    }
    case NET_BDX:
    default: {
        const Line *line = (g_sel_line >= 0 && g_sel_line < g_nlines) ? &g_lines[g_sel_line] : NULL;
        if (!line) return 0;
        line_code(line, code, sizeof(code));
        return strcmp(g_itinerary.line_code, code) == 0 && strcmp(g_itinerary.line_name, line->libelle) == 0;
    }
    }
}

static int load_bordeaux_itinerary(void)
{
    const Line *line = (g_sel_line >= 0 && g_sel_line < g_nlines) ? &g_lines[g_sel_line] : NULL;
    FILE *fp;
    char path[256];
    char row[512];
    char name[96] = "";
    char value[64];
    char code[16];
    double lon = 0.0;
    double lat = 0.0;
    int in_stop = 0;
    int fallback = 0;

    if (!line) return -1;
    if (find_bordeaux_line_file(line, path, sizeof(path)) < 0) return -1;

    line_code(line, code, sizeof(code));
    nvt_itinerary_prepare(&g_itinerary, g_network, "", code, line->libelle);
    ensure_line_route();

    fp = fopen(path, "r");
    if (!fp) return -1;

    while (fgets(row, sizeof(row), fp)) {
        if (strstr(row, "<ScheduledStopPoint ")) {
            in_stop = 1;
            name[0] = '\0';
            lon = 0.0;
            lat = 0.0;
            continue;
        }
        if (!in_stop) continue;
        if (xml_extract_tag_value(row, "Name", name, sizeof(name))) continue;
        if (xml_extract_tag_value(row, "Longitude", value, sizeof(value))) {
            lon = atof(value);
            continue;
        }
        if (xml_extract_tag_value(row, "Latitude", value, sizeof(value))) {
            lat = atof(value);
            continue;
        }
        if (strstr(row, "</ScheduledStopPoint>")) {
            int order = g_has_line_route ? nvt_itinerary_route_progress(&g_line_route, lon, lat) : -1;

            if (order < 0) order = fallback;
            nvt_itinerary_add_stop_unique(&g_itinerary, "", name, line->vehicule, lon, lat, order, fallback);
            fallback++;
            in_stop = 0;
        }
    }

    fclose(fp);
    itinerary_finalize_load();
    return g_itinerary.nstops;
}

static int load_toulouse_itinerary(void)
{
    ToulouseLine *line = selected_toulouse_line();
    int fallback = 0;

    if (!line) return -1;

    nvt_itinerary_prepare(&g_itinerary, g_network, line->ref, line->code, line->libelle);
    ensure_toulouse_line_route();

    for (int i = 0; i < g_ntls_stops; i++) {
        int order;

        if (!nvt_toulouse_list_has_token(g_tls_stops[i].lignes, line->code)) continue;
        order = g_has_tls_line_route ? nvt_itinerary_route_progress(&g_tls_line_route, g_tls_stops[i].lon, g_tls_stops[i].lat) : -1;
        if (order < 0) order = fallback;
        nvt_itinerary_add_stop_unique(
            &g_itinerary,
            g_tls_stops[i].ref,
            g_tls_stops[i].libelle,
            g_tls_stops[i].commune,
            g_tls_stops[i].lon,
            g_tls_stops[i].lat,
            order,
            i
        );
        fallback++;
    }

    itinerary_finalize_load();
    return g_itinerary.nstops;
}

static int load_live_itinerary(void)
{
    static ToulouseStop loaded_stops[MAX_STOPS];
    ToulouseLine *line = selected_idfm_line();
    int nstops;

    if (!line) return -1;

    nvt_itinerary_prepare(&g_itinerary, g_network, line->ref, line->code, line->libelle);
    nstops = live_network_is_sncf()
           ? fetch_sncf_line_stops(line, loaded_stops, MAX_STOPS)
           : fetch_idfm_line_stops(line, loaded_stops, MAX_STOPS);
    if (nstops < 0) return -1;

    for (int i = 0; i < nstops; i++) {
        nvt_itinerary_add_stop_unique(
            &g_itinerary,
            loaded_stops[i].ref,
            loaded_stops[i].libelle,
            loaded_stops[i].commune[0] ? loaded_stops[i].commune : loaded_stops[i].adresse,
            loaded_stops[i].lon,
            loaded_stops[i].lat,
            i,
            i
        );
    }

    itinerary_finalize_load();
    return g_itinerary.nstops;
}

static int load_current_network_itinerary(int force_reload)
{
    int count;

    if (select_line_for_itinerary() < 0) {
        toast("Selectionnez d'abord une ligne");
        return -1;
    }
    if (!force_reload && itinerary_matches_current_line()) return g_itinerary.nstops;

    switch (g_network) {
    case NET_TLS:
        count = load_toulouse_itinerary();
        break;
    case NET_IDFM:
    case NET_SNCF:
        count = load_live_itinerary();
        break;
    case NET_BDX:
    default:
        count = load_bordeaux_itinerary();
        break;
    }

    if (count <= 0) {
        nvt_itinerary_reset(&g_itinerary);
        toast("Impossible de calculer l'itineraire pour cette ligne");
        return -1;
    }

    toast("%d arrets charges pour %s", count,
          g_itinerary.line_code[0] ? g_itinerary.line_code : g_itinerary.line_name);
    return count;
}

static int open_current_network_itinerary(int force_reload)
{
    if (load_current_network_itinerary(force_reload) < 0) return -1;
    g_screen = SCR_ITINERARY;
    return 0;
}

static void swap_itinerary_endpoints(void)
{
    int tmp = g_itinerary.origin;
    g_itinerary.origin = g_itinerary.destination;
    g_itinerary.destination = tmp;
}

static void reset_atlas_map(void)
{
    nvt_app_reset_atlas_map(&g_app);
}

static void reset_atlas_routes(void)
{
    nvt_app_reset_atlas_routes(&g_app);
}

static void reset_metro_map_cache(void)
{
    nvt_app_reset_metro_map_cache(&g_app);
}

/* ── color pairs ─────────────────────────────────────────────────── */

enum {
    /* fixed */
    CP_GREEN=1, CP_YELLOW, CP_RED, CP_CYAN_T, CP_BLUE, CP_MAGENTA,
    CP_SEARCH, CP_ALERT_HI, CP_ALERT_MED, CP_ALERT_LO,
    CP_HELP_KEY, CP_TOAST,
    /* themed (reinit on theme change) */
    CP_HDR, CP_TAB_ACT, CP_TAB_INACT, CP_TAB_BG,
    CP_STAT_MID, CP_STAT_ACC,
    CP_SEL, CP_ACCENT, CP_BORDER, CP_CURSOR, CP_SECTION,
    /* powerline transitions */
    CP_PL_DA, CP_PL_AD, CP_PL_DN, CP_PL_AN,
    /* line colors base */
    CP_LINE_BASE
};

static int lcp_map[1024];
static unsigned tls_bg[256], tls_fg[256];
static int tls_pair[256];
static int g_ntls_pairs, g_tls_pair_next;

static int hex256(unsigned c)
{
    int r=(c>>16)&0xFF, g=(c>>8)&0xFF, b=c&0xFF;
    if (r==g&&g==b) { if(r<4) return 16; if(r>238) return 231; return 232+(r-8)/10; }
    int ri=r<48?0:r<115?1:(r-35)/40, gi=g<48?0:g<115?1:(g-35)/40, bi=b<48?0:b<115?1:(b-35)/40;
    if(ri>5)ri=5; if(gi>5)gi=5; if(bi>5)bi=5;
    return 16+36*ri+6*gi+bi;
}

static void apply_theme(void)
{
    const Theme *t = &themes[g_theme];
    if (g_256) {
        init_pair(CP_HDR,       255, t->header_bg);
        init_pair(CP_TAB_ACT,   16,  t->accent);
        init_pair(CP_TAB_INACT, t->dim_fg, t->header_bg);
        init_pair(CP_TAB_BG,    -1,  t->header_bg);
        init_pair(CP_STAT_MID,  255, t->header_bg);
        init_pair(CP_STAT_ACC,  16,  t->accent);
        init_pair(CP_SEL,       255, t->sel_bg);
        init_pair(CP_ACCENT,    t->accent, -1);
        init_pair(CP_BORDER,    t->accent, -1);
        init_pair(CP_CURSOR,    t->accent, -1);
        init_pair(CP_SECTION,   t->accent, -1);
        init_pair(CP_PL_DA,     t->header_bg, t->accent);
        init_pair(CP_PL_AD,     t->accent, t->header_bg);
        init_pair(CP_PL_DN,     t->header_bg, -1);
        init_pair(CP_PL_AN,     t->accent, -1);
    } else {
        init_pair(CP_HDR,       COLOR_BLACK, COLOR_WHITE);
        init_pair(CP_TAB_ACT,   COLOR_BLACK, COLOR_CYAN);
        init_pair(CP_TAB_INACT, COLOR_WHITE, -1);
        init_pair(CP_TAB_BG,    -1, -1);
        init_pair(CP_STAT_MID,  COLOR_BLACK, COLOR_WHITE);
        init_pair(CP_STAT_ACC,  COLOR_BLACK, COLOR_CYAN);
        init_pair(CP_SEL,       COLOR_WHITE, COLOR_BLUE);
        init_pair(CP_ACCENT,    COLOR_CYAN, -1);
        init_pair(CP_BORDER,    COLOR_CYAN, -1);
        init_pair(CP_CURSOR,    COLOR_CYAN, -1);
        init_pair(CP_SECTION,   COLOR_CYAN, -1);
        init_pair(CP_PL_DA,     -1, COLOR_CYAN);
        init_pair(CP_PL_AD,     COLOR_CYAN, -1);
        init_pair(CP_PL_DN,     -1, -1);
        init_pair(CP_PL_AN,     COLOR_CYAN, -1);
    }
}

static void init_colors(void)
{
    start_color(); use_default_colors();
    g_256 = (COLORS >= 256);
    init_pair(CP_GREEN,     COLOR_GREEN,-1);
    init_pair(CP_YELLOW,    COLOR_YELLOW,-1);
    init_pair(CP_RED,       COLOR_RED,-1);
    init_pair(CP_CYAN_T,    COLOR_CYAN,-1);
    init_pair(CP_BLUE,      COLOR_BLUE,-1);
    init_pair(CP_MAGENTA,   COLOR_MAGENTA,-1);
    init_pair(CP_SEARCH,    COLOR_BLACK,COLOR_YELLOW);
    init_pair(CP_ALERT_HI,  COLOR_RED,-1);
    init_pair(CP_ALERT_MED, COLOR_YELLOW,-1);
    init_pair(CP_ALERT_LO,  COLOR_WHITE,-1);
    init_pair(CP_HELP_KEY,  COLOR_YELLOW,-1);
    init_pair(CP_TOAST,     COLOR_BLACK,COLOR_GREEN);
    apply_theme();

    memset(lcp_map,0,sizeof(lcp_map));
    if (g_256) {
        unsigned sb[64],sf[64]; int sp[64], ns=0, pi=CP_LINE_BASE;
        for (int i=0; i<N_LINE_COLORS; i++) {
            int id=line_colors[i].ident;
            if(id<0||id>=1024||lcp_map[id]) continue;
            unsigned bg=line_colors[i].bg, fg=line_colors[i].fg;
            int ex=0;
            for(int j=0;j<ns;j++) if(sb[j]==bg&&sf[j]==fg){lcp_map[id]=sp[j];ex=1;break;}
            if(ex) continue; if(pi>=COLOR_PAIRS) break;
            init_pair(pi,hex256(fg),hex256(bg)); lcp_map[id]=pi;
            if(ns<64){sb[ns]=bg;sf[ns]=fg;sp[ns]=pi;ns++;} pi++;
        }
        g_tls_pair_next = pi;
    }
    g_ntls_pairs = 0;
}

static int get_lcp(int id) { return (id>=0&&id<1024)?lcp_map[id]:0; }

static int toulouse_badge_fallback_cp(const ToulouseLine *line)
{
    int r = line ? line->r : 0;
    int g = line ? line->g : 0;
    int b = line ? line->b : 0;

    if (r >= g && r >= b) {
        if (b > r * 2 / 3) return CP_MAGENTA;
        if (g > r / 2) return CP_YELLOW;
        return CP_RED;
    }
    if (g >= r && g >= b) {
        if (b > g / 2) return CP_CYAN_T;
        return CP_GREEN;
    }
    if (r > b / 2) return CP_MAGENTA;
    return CP_BLUE;
}

static unsigned parse_hex_rgb(const char *hex, unsigned fallback)
{
    unsigned value = fallback;

    if (!hex || !hex[0]) return fallback;
    if (*hex == '#') hex++;
    if (strlen(hex) < 6) return fallback;
    if (sscanf(hex, "%x", &value) != 1) return fallback;
    return value & 0xFFFFFFu;
}

static int toulouse_line_pair(const ToulouseLine *line)
{
    if (!line) return CP_ACCENT;
    if (!g_256 || g_tls_pair_next <= 0) return 0;

    {
        unsigned bg = parse_hex_rgb(line->couleur, 0);
        unsigned fg = parse_hex_rgb(line->texte_couleur, 0xFFFFFFu);

        for (int i = 0; i < g_ntls_pairs; i++) {
            if (tls_bg[i] == bg && tls_fg[i] == fg) return tls_pair[i];
        }
        if (g_ntls_pairs < (int)(sizeof(tls_pair) / sizeof(tls_pair[0])) && g_tls_pair_next < COLOR_PAIRS) {
            init_pair(g_tls_pair_next, hex256(fg), hex256(bg));
            tls_bg[g_ntls_pairs] = bg;
            tls_fg[g_ntls_pairs] = fg;
            tls_pair[g_ntls_pairs] = g_tls_pair_next;
            g_ntls_pairs++;
            return g_tls_pair_next++;
        }
    }

    return 0;
}

static int toulouse_badge_cp(const ToulouseLine *line)
{
    int cp = toulouse_line_pair(line);
    return cp ? cp : toulouse_badge_fallback_cp(line);
}

static void draw_toulouse_badge(int y, int x, const ToulouseLine *line)
{
    int cp = toulouse_badge_cp(line);
    attron(COLOR_PAIR(cp) | A_BOLD);
    mvprintw(y, x, " %3s ", line ? line->code : "---");
    attroff(COLOR_PAIR(cp) | A_BOLD);
}

static void draw_toulouse_badge_by_code(int y, int x, const char *code)
{
    const ToulouseLine *line = toulouse_line_by_code(code);
    if (line) {
        draw_toulouse_badge(y, x, line);
        return;
    }
    attron(COLOR_PAIR(CP_ACCENT) | A_BOLD);
    mvprintw(y, x, " %3.3s ", code && code[0] ? code : "---");
    attroff(COLOR_PAIR(CP_ACCENT) | A_BOLD);
}

static void draw_idfm_badge(int y, int x, const ToulouseLine *line)
{
    int cp = toulouse_line_pair(line);

    if (!cp) cp = toulouse_badge_fallback_cp(line);
    attron(COLOR_PAIR(cp) | A_BOLD);
    mvprintw(y, x, " %-5.5s ", line && line->code[0] ? line->code : "-----");
    attroff(COLOR_PAIR(cp) | A_BOLD);
}

static void draw_idfm_badge_by_code(int y, int x, const char *code)
{
    const ToulouseLine *line = idfm_line_by_code(code);

    if (line) {
        draw_idfm_badge(y, x, line);
        return;
    }
    attron(COLOR_PAIR(CP_ACCENT) | A_BOLD);
    mvprintw(y, x, " %-5.5s ", code && code[0] ? code : "-----");
    attroff(COLOR_PAIR(CP_ACCENT) | A_BOLD);
}

/* ── UI primitives ───────────────────────────────────────────────── */

static void rbox(int y1,int x1,int y2,int x2)
{
    nvt_ui_rbox(y1, x1, y2, x2, CP_BORDER);
}

static void hbar(int y,int x1,int x2)
{
    nvt_ui_hbar(y, x1, x2, CP_BORDER);
}

static void scrollbar(int ys,int h,int pos,int total)
{
    nvt_ui_scrollbar(ys, h, pos, total, CP_ACCENT, CP_BORDER);
}

static void progress_bar(int y,int x,int w,int pct)
{
    nvt_ui_progress_bar(y, x, w, pct, g_256, CP_GREEN, CP_YELLOW, CP_RED);
}

static void print_hl(int y,int x,const char *t,const char *s,int mw)
{
    nvt_ui_print_highlight(y, x, t, s, mw);
}

static void fill_span(int y,int x1,int x2,int cp,int attr)
{
    nvt_ui_fill_span(y, x1, x2, cp, attr);
}

static void panel_box(int y1,int x1,int y2,int x2,const char *title,const char *meta)
{
    nvt_ui_panel_box(y1, x1, y2, x2, title, meta, CP_BORDER, CP_SECTION);
}

static void print_fit(int y,int x,int w,const char *text)
{
    nvt_ui_print_fit(y, x, w, text);
}

static void stat_card(int y,int x,int w,const char *label,const char *value,const char *detail,int cp)
{
    nvt_ui_stat_card(y, x, w, label, value, detail, cp, CP_BORDER, CP_SECTION);
}

static void kv_line(int y,int x,int label_w,const char *label,const char *value,int cp)
{
    nvt_ui_kv_line(y, x, label_w, label, value, cp);
}

static void draw_line_badge(int y,int x,const Line *l)
{
    char idbuf[16];
    int lcp=get_lcp(l->ident);
    line_code(l,idbuf,sizeof(idbuf));
    if(lcp){
        attron(COLOR_PAIR(lcp)|A_BOLD);
        mvprintw(y,x," %3s ",idbuf);
        attroff(COLOR_PAIR(lcp)|A_BOLD);
    } else {
        attron(COLOR_PAIR(CP_ACCENT)|A_BOLD);
        mvprintw(y,x," %3s ",idbuf);
        attroff(COLOR_PAIR(CP_ACCENT)|A_BOLD);
    }
}

static int draw_wrapped_block(int y,int x,int w,int max_lines,const char *text,int cp,int attr)
{
    return nvt_ui_draw_wrapped_block(y, x, w, max_lines, text, cp, attr);
}

/* ── Powerline header / tabs / status ────────────────────────────── */

static void draw_header(const char *title, const char *bc)
{
    /* left accent segment */
    attron(COLOR_PAIR(CP_STAT_ACC)|A_BOLD);
    mvhline(0,0,' ',COLS);
    mvprintw(0,1," %s ",title);
    attroff(COLOR_PAIR(CP_STAT_ACC)|A_BOLD);
    int lx=getcurx(stdscr);

    /* powerline: accent → dark */
    attron(COLOR_PAIR(CP_PL_AD)); mvaddstr(0,lx,PL_R); attroff(COLOR_PAIR(CP_PL_AD));

    /* middle dark segment */
    attron(COLOR_PAIR(CP_HDR));
    int mx=lx+1;
    mvhline(0,mx,' ',COLS-mx);
    if(bc&&bc[0]) mvprintw(0,mx+1," %s",bc);

    /* clock on right */
    time_t now=time(NULL); struct tm *tm=localtime(&now);
    char clk[16]; snprintf(clk,sizeof(clk),"%02d:%02d:%02d",tm->tm_hour,tm->tm_min,tm->tm_sec);
    int cx=COLS-(int)strlen(clk)-4;

    /* powerline: dark → accent (right segment) */
    mvaddstr(0,cx-1," "); attroff(COLOR_PAIR(CP_HDR));
    attron(COLOR_PAIR(CP_PL_DA)); mvaddstr(0,cx,PL_R); attroff(COLOR_PAIR(CP_PL_DA));
    attron(COLOR_PAIR(CP_STAT_ACC)|A_BOLD);
    mvprintw(0,cx+1," %s ",clk);
    attroff(COLOR_PAIR(CP_STAT_ACC)|A_BOLD);
}

static void draw_tabs(void)
{
    static const char *lb[]={"Lignes","Vehicules","Alertes","Arrets","Passages","Itineraire"};
    static const NvtScreen sc[]={SCR_LINES,SCR_VEHICLES,SCR_ALERTS,SCR_STOP_SEARCH,SCR_PASSAGES,SCR_ITINERARY};
    int alert_count;

    if (g_network == NET_TLS) alert_count = g_ntls_alerts;
    else if (g_network == NET_IDFM || g_network == NET_SNCF) alert_count = g_nlive_alerts;
    else alert_count = g_nalerts;

    /* fill tab bar background */
    attron(COLOR_PAIR(CP_TAB_BG)); mvhline(1,0,' ',COLS); attroff(COLOR_PAIR(CP_TAB_BG));

    int x=0;
    for(int i=0;i<6;i++){
        int act=(sc[i]==g_screen);
        if(act){
            /* dark→accent transition */
            if(g_256){attron(COLOR_PAIR(CP_PL_DA));mvaddstr(1,x,PL_R);attroff(COLOR_PAIR(CP_PL_DA));x++;}
            attron(COLOR_PAIR(CP_TAB_ACT)|A_BOLD);
            mvprintw(1,x," %d"U_MDOT"%s ",i+1,lb[i]);
            if(i==2&&alert_count>0) printw("(%d) ",alert_count);
            x=getcurx(stdscr);
            attroff(COLOR_PAIR(CP_TAB_ACT)|A_BOLD);
            /* accent→dark transition */
            if(g_256){attron(COLOR_PAIR(CP_PL_AD));mvaddstr(1,x,PL_R);attroff(COLOR_PAIR(CP_PL_AD));x++;}
        } else {
            attron(COLOR_PAIR(CP_TAB_INACT));
            mvprintw(1,x," %d"U_MDOT"%s",i+1,lb[i]);
            if(i==2&&alert_count>0){
                attroff(COLOR_PAIR(CP_TAB_INACT));
                attron(COLOR_PAIR(CP_ALERT_HI)|A_BOLD);
                printw(" %d",alert_count);
                attroff(COLOR_PAIR(CP_ALERT_HI)|A_BOLD);
                attron(COLOR_PAIR(CP_TAB_INACT));
            }
            printw(" ");
            x=getcurx(stdscr);
            attroff(COLOR_PAIR(CP_TAB_INACT));
        }
    }
    /* theme indicator on far right */
    if(g_256){
        attron(COLOR_PAIR(CP_TAB_INACT));
        mvprintw(1,COLS-(int)strlen(themes[g_theme].name)-(int)strlen(network_name())-8,
                 " [%s/%s]", network_name(), themes[g_theme].name);
        attroff(COLOR_PAIR(CP_TAB_INACT));
    }
    hbar(2,0,COLS);
}

static void draw_status(const char *mid, const char *right)
{
    int y=LINES-1;
    /* left accent: NVT */
    attron(COLOR_PAIR(CP_STAT_ACC)|A_BOLD);
    mvhline(y,0,' ',COLS);
    mvprintw(y,0," NVT ");
    attroff(COLOR_PAIR(CP_STAT_ACC)|A_BOLD);
    int lx=getcurx(stdscr);

    if(g_256){attron(COLOR_PAIR(CP_PL_AD));mvaddstr(y,lx,PL_R);attroff(COLOR_PAIR(CP_PL_AD));lx++;}

    /* middle dark segment */
    attron(COLOR_PAIR(CP_STAT_MID));
    mvhline(y,lx,' ',COLS-lx);
    mvprintw(y,lx+1,"%s",mid);
    attroff(COLOR_PAIR(CP_STAT_MID));

    /* right accent segment */
    if(right&&right[0]){
        int rl=(int)strlen(right)+2;
        int rx=COLS-rl-1;
        if(g_256){attron(COLOR_PAIR(CP_PL_DA));mvaddstr(y,rx,PL_R);attroff(COLOR_PAIR(CP_PL_DA));rx++;}
        attron(COLOR_PAIR(CP_STAT_ACC)|A_BOLD);
        mvprintw(y,rx," %s ",right);
        attroff(COLOR_PAIR(CP_STAT_ACC)|A_BOLD);
    }
}

static void draw_toast_msg(void)
{
    if(!g_toast[0]) return;
    if(time(NULL)-g_toast_time>2){g_toast[0]='\0';return;}
    int l=(int)strlen(g_toast), w=l+6, x=(COLS-w)/2, y=LINES-2;
    if(x<0)x=0;
    attron(COLOR_PAIR(CP_TOAST)|A_BOLD);
    mvhline(y,x,' ',w);
    mvprintw(y,x+1," "U_CHECK" %s ",g_toast);
    attroff(COLOR_PAIR(CP_TOAST)|A_BOLD);
}

/* ── help overlay ────────────────────────────────────────────────── */

static void draw_help(void)
{
    int h=24,w=54,y0=(LINES-h)/2,x0=(COLS-w)/2;
    if(y0<0)y0=0; if(x0<0)x0=0;
    for(int y=y0;y<=y0+h;y++) mvhline(y,x0,' ',w+1);
    rbox(y0,x0,y0+h,x0+w);

    attron(COLOR_PAIR(CP_BORDER)|A_BOLD);
    mvprintw(y0,x0+(w-22)/2," "U_INFO" Raccourcis clavier ");
    attroff(COLOR_PAIR(CP_BORDER)|A_BOLD);

    int y=y0+2;
    struct{const char*k,*d;}
    nav[]={{"j / k","Naviguer haut/bas"},{"PgUp / PgDn","Page haut/bas"},
           {"Ctrl+U / Ctrl+D","Demi-page"},{"g / G","Debut / Fin"}},
    act[]={{"Enter","Selectionner / ouvrir ligne"},{"/ ","Rechercher"},
           {"Esc","Effacer filtre / Retour"},{"r / F5","Rafraichir"},
           {"+ / - / 0","Zoom carte + detail"},{"i / 6","Calculateur d'itineraire"},
           {"o / d / x","Depart, arrivee, inversion"},{"n","Menu reseaux"}},
    scr[]={{"1","Lignes"},{"2","Vehicules"},{"3 / a","Alertes"},
           {"4 / p","Arrets"},{"5","Passages"},{"6 / i","Itineraire"},
           {"q","Retour / Quitter"}};
    int nnav=(int)(sizeof(nav)/sizeof(nav[0]));
    int nact=(int)(sizeof(act)/sizeof(act[0]));
    int nscr=(int)(sizeof(scr)/sizeof(scr[0]));

    attron(COLOR_PAIR(CP_SECTION)|A_BOLD); mvprintw(y++,x0+3,"Navigation"); attroff(COLOR_PAIR(CP_SECTION)|A_BOLD);
    for(int i=0;i<nnav;i++){attron(COLOR_PAIR(CP_HELP_KEY)|A_BOLD);mvprintw(y,x0+4,"%-18s",nav[i].k);attroff(COLOR_PAIR(CP_HELP_KEY)|A_BOLD);printw(" %s",nav[i].d);y++;}
    y++;
    attron(COLOR_PAIR(CP_SECTION)|A_BOLD); mvprintw(y++,x0+3,"Actions"); attroff(COLOR_PAIR(CP_SECTION)|A_BOLD);
    for(int i=0;i<nact;i++){attron(COLOR_PAIR(CP_HELP_KEY)|A_BOLD);mvprintw(y,x0+4,"%-18s",act[i].k);attroff(COLOR_PAIR(CP_HELP_KEY)|A_BOLD);printw(" %s",act[i].d);y++;}
    y++;
    attron(COLOR_PAIR(CP_SECTION)|A_BOLD); mvprintw(y++,x0+3,"Ecrans"); attroff(COLOR_PAIR(CP_SECTION)|A_BOLD);
    for(int i=0;i<nscr;i++){attron(COLOR_PAIR(CP_HELP_KEY)|A_BOLD);mvprintw(y,x0+4,"%-18s",scr[i].k);attroff(COLOR_PAIR(CP_HELP_KEY)|A_BOLD);printw(" %s",scr[i].d);y++;}

    attron(A_DIM); mvprintw(y0+h-1,x0+(w-28)/2,"Appuyer sur une touche"U_ELLIP); attroff(A_DIM);
}

/* ── Screen: Lines ───────────────────────────────────────────────── */

static void draw_lines(void)
{
    int total=count_active_lines_all();
    int trams=count_lines_type("TRAM");
    int buses=count_lines_type("BUS");
    int alert_lines=count_alerted_lines();
    int top=3;

    draw_header("NVT // Bordeaux",g_search[0]?"reseau filtre":"reseau live");
    draw_tabs();

    if(COLS>=94&&LINES>=24){
        int gap=1;
        int cw=(COLS-5-gap*3)/4;
        char buf[32];
        snprintf(buf,sizeof(buf),"%d",total);
        stat_card(top,2,cw,"ACTIVE",buf,"lignes actives",CP_ACCENT);
        snprintf(buf,sizeof(buf),"%d",trams);
	stat_card(top,2+cw+gap,cw,"TRAM",buf,"couloirs ferres",CP_CYAN_T);
        snprintf(buf,sizeof(buf),"%d",buses);
        stat_card(top,2+(cw+gap)*2,cw,"BUS",buf,"reseau bus",CP_GREEN);
        snprintf(buf,sizeof(buf),"%d",alert_lines);
        stat_card(top,2+(cw+gap)*3,cw,"ALERTS",buf,"lignes impactees",alert_lines?CP_ALERT_MED:CP_GREEN);
        top+=5;
    }

    if(COLS>=110&&LINES-top>=16){
        int y1=top, y2=LINES-3;
        int left_w=COLS*58/100;
        if(left_w<56) left_w=56;
        if(left_w>COLS-34) left_w=COLS-34;
        int lx1=1, lx2=lx1+left_w-1, rx1=lx2+1, rx2=COLS-2;
        int head_y=y1+1, list_y=head_y+2;
        int name_w=lx2-lx1-28;
        int mr=y2-list_y;
        if(name_w<18) name_w=18;
        if(mr<1) mr=1;

        panel_box(y1,lx1,y2,lx2,"Network Index",g_search[0]?g_search:"all active");
        panel_box(y1,rx1,y2,rx2,"Line Focus",g_nfiltered>0?g_lines[g_filtered[g_cursor]].vehicule:"idle");

        if(g_cursor<g_scroll) g_scroll=g_cursor;
        if(g_cursor>=g_scroll+mr) g_scroll=g_cursor-mr+1;

        attron(A_DIM);
        mvprintw(head_y,lx1+3,"LINE");
        mvprintw(head_y,lx1+10,"NAME");
        mvprintw(head_y,lx2-16,"TYPE");
        mvprintw(head_y,lx2-9,"SAE");
        mvprintw(head_y,lx2-5,"ALT");
        attroff(A_DIM);

        for(int i=0;i<mr&&g_scroll+i<g_nfiltered;i++){
            int idx=g_filtered[g_scroll+i];
            Line *l=&g_lines[idx];
            int row=list_y+i;
            int alerts=count_line_alerts(l->gid);
            int selected=(g_scroll+i==g_cursor);

            if(selected){
                fill_span(row,lx1+1,lx2-1,CP_SEL,0);
                attron(COLOR_PAIR(CP_CURSOR)|A_BOLD);
                mvaddstr(row,lx1+1,U_CBAR);
                attroff(COLOR_PAIR(CP_CURSOR)|A_BOLD);
                attron(COLOR_PAIR(CP_SEL));
            } else if(i%2){
                attron(A_DIM);
                mvhline(row,lx1+1,' ',lx2-lx1-1);
                attroff(A_DIM);
                attron(A_DIM);
            }

            draw_line_badge(row,lx1+3,l);
            if(selected) attron(COLOR_PAIR(CP_SEL));
            print_hl(row,lx1+10,l->libelle,g_search,name_w);
            mvprintw(row,lx2-16,"%-5s",l->vehicule);
            mvprintw(row,lx2-9," %s ",l->sae?U_CHECK:U_MDOT);
            if(alerts>0){
                attroff(selected?COLOR_PAIR(CP_SEL):0);
                attron(COLOR_PAIR(CP_ALERT_MED)|A_BOLD);
                mvprintw(row,lx2-5,"%2d",alerts);
                attroff(COLOR_PAIR(CP_ALERT_MED)|A_BOLD);
                if(selected) attron(COLOR_PAIR(CP_SEL));
                else if(i%2) attron(A_DIM);
            } else mvprintw(row,lx2-5," -");

            if(selected) attroff(COLOR_PAIR(CP_SEL));
            else if(i%2) attroff(A_DIM);
        }
        scrollbar(list_y,mr,g_scroll,g_nfiltered);

        if(g_nfiltered>0){
            Line *sel=&g_lines[g_filtered[g_cursor]];
            int alerts=count_line_alerts(sel->gid);
            int px=rx1+2, pw=rx2-rx1-3, yy=y1+2;
            char idbuf[16], buf[64];
            line_code(sel,idbuf,sizeof(idbuf));
            draw_line_badge(yy,px,sel);
            attron(A_BOLD);
            print_fit(yy,px+7,pw-8,sel->libelle);
            attroff(A_BOLD);
            attron(A_DIM);
            mvprintw(yy+1,px,"%s corridor%s",sel->vehicule,alerts?" with incidents":"");
            attroff(A_DIM);

            if(pw>=28){
                int sw=(pw-1)/2;
                snprintf(buf,sizeof(buf),"%s",sel->sae?U_CHECK" live":"-");
                stat_card(yy+3,px,sw,"SAE",buf,sel->sae?"temps reel et prediction":"telemetrie limitee",sel->sae?CP_GREEN:CP_YELLOW);
                snprintf(buf,sizeof(buf),"%d",alerts);
                stat_card(yy+3,px+sw+1,pw-sw,"ALERTS",buf,alerts?"messages actifs":"ligne stable",alerts?CP_ALERT_MED:CP_GREEN);
            }

            yy+=9;
            attron(COLOR_PAIR(CP_SECTION)|A_BOLD);
            mvprintw(yy,px,"Telemetry");
            attroff(COLOR_PAIR(CP_SECTION)|A_BOLD);
            yy++;
            snprintf(buf,sizeof(buf),"%d",sel->gid);
            kv_line(yy++,px,10,"gid",buf,CP_ACCENT);
            kv_line(yy++,px,10,"type",sel->vehicule,CP_ACCENT);
            kv_line(yy++,px,10,"filter",g_search[0]?g_search:"none",CP_ACCENT);
            snprintf(buf,sizeof(buf),"%d / %d",g_nfiltered?g_cursor+1:0,g_nfiltered);
            kv_line(yy++,px,10,"focus",buf,CP_ACCENT);

            yy++;
            attron(COLOR_PAIR(CP_SECTION)|A_BOLD);
            mvprintw(yy++,px,"Signal");
            attroff(COLOR_PAIR(CP_SECTION)|A_BOLD);
            if(alerts){
                int shown=0;
                for(int i=0;i<g_nalerts&&yy<y2-3&&shown<2;i++){
                    if(g_alerts[i].ligne_id!=sel->gid) continue;
                    attron(COLOR_PAIR(CP_ALERT_MED)|A_BOLD);
                    mvprintw(yy,px,"%s ",shown==0?U_WARN:U_DIAMOND);
                    attroff(COLOR_PAIR(CP_ALERT_MED)|A_BOLD);
                    attron(A_BOLD);
                    print_fit(yy,px+2,pw-4,g_alerts[i].titre[0]?g_alerts[i].titre:"Message reseau");
                    attroff(A_BOLD);
                    yy++;
                    yy+=draw_wrapped_block(yy,px+2,pw-4,2,g_alerts[i].message,CP_ALERT_MED,0);
                    yy++;
                    shown++;
                }
            } else {
                attron(A_DIM);
                mvprintw(yy++,px,U_CHECK" aucune alerte sur cette ligne");
                mvprintw(yy++,px,"Enter ouvre le board vehicules en direct.");
                mvprintw(yy++,px,"p bascule vers la recherche d'arret.");
                attroff(A_DIM);
            }
        } else {
            attron(A_DIM);
            mvprintw(y1+3,rx1+3,U_INFO" aucun resultat");
            mvprintw(y1+5,rx1+3,"Essayez un identifiant, un type ou une ligne.");
            attroff(A_DIM);
        }
    } else {
        int y1=top, y2=LINES-3;
        int head_y=y1+1, list_y=head_y+2;
        int mr=y2-list_y;
        int cw=COLS>80?COLS-28:COLS-22;
        if(cw<18) cw=18;
        if(mr<1) mr=1;

        panel_box(y1,1,y2,COLS-2,"Network Index",g_search[0]?g_search:"all active");
        attron(A_DIM);
        mvprintw(head_y,3,"LINE");
        mvprintw(head_y,10,"NAME");
        mvprintw(head_y,COLS-17,"TYPE");
        mvprintw(head_y,COLS-10,"SAE");
        mvprintw(head_y,COLS-6,"ALT");
        attroff(A_DIM);

        if(g_cursor<g_scroll) g_scroll=g_cursor;
        if(g_cursor>=g_scroll+mr) g_scroll=g_cursor-mr+1;

        for(int i=0;i<mr&&g_scroll+i<g_nfiltered;i++){
            int idx=g_filtered[g_scroll+i];
            Line *l=&g_lines[idx];
            int row=list_y+i;
            int alerts=count_line_alerts(l->gid);
            int selected=(g_scroll+i==g_cursor);

            if(selected){
                fill_span(row,2,COLS-3,CP_SEL,0);
                attron(COLOR_PAIR(CP_CURSOR)|A_BOLD);
                mvaddstr(row,2,U_CBAR);
                attroff(COLOR_PAIR(CP_CURSOR)|A_BOLD);
                attron(COLOR_PAIR(CP_SEL));
            } else if(i%2) attron(A_DIM);

            draw_line_badge(row,4,l);
            if(selected) attron(COLOR_PAIR(CP_SEL));
            print_hl(row,11,l->libelle,g_search,cw);
            mvprintw(row,COLS-17,"%-5s",l->vehicule);
            mvprintw(row,COLS-10," %s ",l->sae?U_CHECK:U_MDOT);
            if(alerts>0){
                if(selected) attroff(COLOR_PAIR(CP_SEL));
                else if(i%2) attroff(A_DIM);
                attron(COLOR_PAIR(CP_ALERT_MED)|A_BOLD);
                mvprintw(row,COLS-6,"%2d",alerts);
                attroff(COLOR_PAIR(CP_ALERT_MED)|A_BOLD);
                if(selected) attron(COLOR_PAIR(CP_SEL));
                else if(i%2) attron(A_DIM);
            } else mvprintw(row,COLS-6," -");

            if(selected) attroff(COLOR_PAIR(CP_SEL));
            else if(i%2) attroff(A_DIM);
        }
        scrollbar(list_y,mr,g_scroll,g_nfiltered);
    }

    if(g_search[0]){
        attron(COLOR_PAIR(CP_SEARCH)|A_BOLD);
        mvhline(LINES-2,0,' ',COLS);
        mvprintw(LINES-2,1," / %s   %d hits",g_search,g_nfiltered);
        attroff(COLOR_PAIR(CP_SEARCH)|A_BOLD);
    }
    draw_toast_msg();

    char r[32];
    snprintf(r,sizeof(r),"%d/%d",g_nfiltered>0?g_cursor+1:0,g_nfiltered);
    draw_status(" j/k"U_MDOT"Enter"U_MDOT"/"U_MDOT"a:alertes"U_MDOT"p:arrets"U_MDOT"t:theme",r);
}

/* ── Screen: Vehicles ────────────────────────────────────────────── */

static int vcol(const Vehicle *v)
{ return strcmp(v->etat,"AVANCE")==0?CP_CYAN_T:strcmp(v->etat,"RETARD")==0?(v->retard>=180?CP_RED:CP_YELLOW):CP_GREEN; }

static void fmtdly(int r,char *b,size_t s)
{ if(!r){b[0]=0;return;} int m=r/60,sc=abs(r)%60; snprintf(b,s,r>0?"+%dm%02d":"-%dm%02d",r>0?m:-m,sc); }

static void draw_vehicle_lane_panel(int y1,int x1,int y2,int x2,const char *dir)
{
    char meta[32], buf[64], title[96];
    const char *terminus="?";
    int nterm=0;
    int cnt=count_vehicles_dir(dir);
    int delayed=count_delayed_vehicles_dir(dir,60);
    int stopped=count_stopped_vehicles_dir(dir);
    int avg=avg_speed_dir(dir);
    int y=y1+2;
    int inner=x2-x1-3;
    int term_w=inner>=48?14:inner>=40?10:inner>=32?8:0;
    int route_w=inner-18-(term_w?term_w+2:0);

    for(int i=0;i<g_nvehicles;i++){
        int dup=0;
        if(strcmp(g_vehicles[i].sens,dir)!=0) continue;
        if(nterm==0) terminus=g_vehicles[i].terminus;
        for(int j=0;j<i;j++){
            if(strcmp(g_vehicles[j].sens,dir)==0&&strcmp(g_vehicles[j].terminus,g_vehicles[i].terminus)==0){
                dup=1;
                break;
            }
        }
        if(!dup) nterm++;
    }
    snprintf(meta,sizeof(meta),"%d live",cnt);
    panel_box(y1,x1,y2,x2,dir,meta);
    if(route_w<16) route_w=16;

    if(nterm<=1) snprintf(title,sizeof(title),"%s",terminus);
    else snprintf(title,sizeof(title),"%s  +%d partial",terminus,nterm-1);
    attron(A_BOLD);
    print_fit(y,x1+2,inner,title);
    attroff(A_BOLD);
    attron(A_DIM);
    snprintf(buf,sizeof(buf),"%dkm/h avg  %d delayed  %d stopped",avg,delayed,stopped);
    print_fit(y+1,x1+2,inner,buf);
    mvprintw(y+2,x1+2,"now");
    if(term_w) mvprintw(y+2,x1+5+route_w,"term");
    mvprintw(y+2,x2-13,"delay");
    mvprintw(y+2,x2-6,"spd");
    attroff(A_DIM);
    y+=3;

    if(!cnt){
        attron(A_DIM);
        mvprintw(y,x1+2,U_INFO" aucun vehicule sur ce sens");
        attroff(A_DIM);
        return;
    }

    for(int i=0, row=0;i<g_nvehicles&&y<=y2-1;i++){
        Vehicle *v=&g_vehicles[i];
        char route[192], dly[16];
        const char *sn, *nn, *ind;
        int cp;
        if(strcmp(v->sens,dir)!=0) continue;
        cp=vcol(v);
        sn=stopmap_lookup(&g_stops,v->arret_actu);
        nn=stopmap_lookup(&g_stops,v->arret_suiv);
        snprintf(route,sizeof(route),"%s "U_ARROW" %s",sn,nn);
        fmtdly(v->retard,dly,sizeof(dly));
        ind=strcmp(v->etat,"AVANCE")==0?U_BULLETO:strcmp(v->etat,"RETARD")==0&&v->retard>=180?U_DIAMOND:strcmp(v->etat,"RETARD")==0?U_WARN:U_BULLET;

        if(y>y2-1) break;
        if(row%2){
            attron(A_DIM);
            mvhline(y,x1+1,' ',x2-x1-1);
            attroff(A_DIM);
            attron(A_DIM);
        }
        attron(COLOR_PAIR(cp)|A_BOLD);
        mvprintw(y,x1+2,"%s",ind);
        attroff(COLOR_PAIR(cp)|A_BOLD);
        if(row%2) attron(A_DIM);
        print_fit(y,x1+4,route_w,route);
        if(term_w){
            attron(A_DIM);
            print_fit(y,x1+5+route_w,term_w,v->terminus);
            attroff(A_DIM);
            if(row%2) attron(A_DIM);
        }
        if(dly[0]){
            attron(COLOR_PAIR(cp)|A_BOLD);
            mvprintw(y,x2-13,"%6s",dly);
            attroff(COLOR_PAIR(cp)|A_BOLD);
        } else mvprintw(y,x2-13,"   -- ");
        mvprintw(y,x2-6,"%3d",v->vitesse);
        if(v->arret){
            attron(COLOR_PAIR(CP_YELLOW)|A_BOLD);
            mvprintw(y,x2-3,"P");
            attroff(COLOR_PAIR(CP_YELLOW)|A_BOLD);
        }
        if(row%2) attroff(A_DIM);
        y++;
        row++;
    }
}

static void map_project(double lon,double lat,double minlon,double maxlon,double minlat,double maxlat,int w,int h,int *px,int *py)
{
    nvt_map_project(lon, lat, minlon, maxlon, minlat, maxlat, w, h, px, py);
}

static int map_clip_segment(double *lon0,double *lat0,double *lon1,double *lat1,
                            double minlon,double maxlon,double minlat,double maxlat)
{
    return nvt_map_clip_segment(lon0, lat0, lon1, lat1, minlon, maxlon, minlat, maxlat);
}

enum { MAP_N=1, MAP_E=2, MAP_S=4, MAP_W=8 };

static const char *map_mask_glyph(unsigned char mask)
{
    if(mask==(MAP_E|MAP_W)) return "\xe2\x94\x80";
    if(mask==(MAP_N|MAP_S)) return "\xe2\x94\x82";
    if(mask==(MAP_E|MAP_S)) return "\xe2\x94\x8c";
    if(mask==(MAP_W|MAP_S)) return "\xe2\x94\x90";
    if(mask==(MAP_E|MAP_N)) return "\xe2\x94\x94";
    if(mask==(MAP_W|MAP_N)) return "\xe2\x94\x98";
    if(mask==(MAP_N|MAP_E|MAP_S)) return "\xe2\x94\x9c";
    if(mask==(MAP_N|MAP_W|MAP_S)) return "\xe2\x94\xa4";
    if(mask==(MAP_E|MAP_S|MAP_W)) return "\xe2\x94\xac";
    if(mask==(MAP_E|MAP_N|MAP_W)) return "\xe2\x94\xb4";
    if(mask==(MAP_N|MAP_E|MAP_S|MAP_W)) return "\xe2\x94\xbc";
    if(mask&MAP_N || mask&MAP_S) return "\xe2\x94\x82";
    if(mask&MAP_E || mask&MAP_W) return "\xe2\x94\x80";
    return "\xe2\x94\x80";
}

static void map_set_cell(char *glyph,unsigned char *colors,int *attrs,unsigned char *prio,int w,int h,int x,int y,char ch,int cp,int attr,unsigned char z)
{
    int idx;
    if(x<0||x>=w||y<0||y>=h) return;
    idx=y*w+x;
    if(z<prio[idx]) return;
    prio[idx]=z;
    glyph[idx]=ch;
    colors[idx]=(unsigned char)cp;
    attrs[idx]=attr;
}

static void map_add_link(unsigned char *mask,int w,int h,int x0,int y0,int x1,int y1)
{
    int idx0, idx1;
    if(x0<0||x0>=w||y0<0||y0>=h||x1<0||x1>=w||y1<0||y1>=h) return;
    idx0=y0*w+x0;
    idx1=y1*w+x1;
    if(x1>x0){mask[idx0]|=MAP_E; mask[idx1]|=MAP_W;}
    else if(x1<x0){mask[idx0]|=MAP_W; mask[idx1]|=MAP_E;}
    else if(y1>y0){mask[idx0]|=MAP_S; mask[idx1]|=MAP_N;}
    else if(y1<y0){mask[idx0]|=MAP_N; mask[idx1]|=MAP_S;}
}

static void map_mark_cell(unsigned char *marks,int w,int h,int x,int y,unsigned char flag)
{
    if(!marks||x<0||x>=w||y<0||y>=h) return;
    marks[y*w+x]|=flag;
}

static void map_draw_mask_segment(unsigned char *mask,int w,int h,int x0,int y0,int x1,int y1)
{
    int steps=abs(x1-x0)>abs(y1-y0)?abs(x1-x0):abs(y1-y0);
    int px=x0, py=y0;
    if(steps<1) return;
    for(int i=1;i<=steps;i++){
        int nx=(int)(x0+((x1-x0)*(double)i)/steps+0.5);
        int ny=(int)(y0+((y1-y0)*(double)i)/steps+0.5);
        while(px!=nx||py!=ny){
            int stepx=px==nx?0:(nx>px?1:-1);
            int stepy=py==ny?0:(ny>py?1:-1);
            if(stepx&&stepy){
                map_add_link(mask,w,h,px,py,px+stepx,py);
                px+=stepx;
            } else {
                map_add_link(mask,w,h,px,py,px+stepx,py+stepy);
                px+=stepx;
                py+=stepy;
            }
        }
    }
}

static void map_draw_mask_segment_marked(unsigned char *mask,unsigned char *marks,int w,int h,
                                         int x0,int y0,int x1,int y1,unsigned char flag)
{
    int steps=abs(x1-x0)>abs(y1-y0)?abs(x1-x0):abs(y1-y0);
    int px=x0, py=y0;
    map_mark_cell(marks,w,h,px,py,flag);
    if(steps<1) return;
    for(int i=1;i<=steps;i++){
        int nx=(int)(x0+((x1-x0)*(double)i)/steps+0.5);
        int ny=(int)(y0+((y1-y0)*(double)i)/steps+0.5);
        while(px!=nx||py!=ny){
            int stepx=px==nx?0:(nx>px?1:-1);
            int stepy=py==ny?0:(ny>py?1:-1);
            if(stepx&&stepy){
                map_add_link(mask,w,h,px,py,px+stepx,py);
                px+=stepx;
            } else {
                map_add_link(mask,w,h,px,py,px+stepx,py+stepy);
                px+=stepx;
                py+=stepy;
            }
            map_mark_cell(marks,w,h,px,py,flag);
        }
    }
}

static void map_merge_style(int *colors,int *attrs,unsigned char *multi,int idx,int cp,int attr)
{
    if(!colors||!attrs||!multi||idx<0) return;
    if(!colors[idx]){
        colors[idx]=cp;
        attrs[idx]=attr;
        return;
    }
    if(colors[idx]!=cp){
        multi[idx]=1;
        colors[idx]=CP_ACCENT;
        attrs[idx]=A_BOLD;
        return;
    }
    if((attrs[idx]&A_BOLD)||attr&A_BOLD) attrs[idx]=A_BOLD;
    else attrs[idx]=attr;
}

static void map_add_link_layer(unsigned char *mask,int *colors,int *attrs,unsigned char *multi,
                               int w,int h,int x0,int y0,int x1,int y1,int cp,int attr)
{
    int idx0, idx1;
    if(x0<0||x0>=w||y0<0||y0>=h||x1<0||x1>=w||y1<0||y1>=h) return;
    idx0=y0*w+x0;
    idx1=y1*w+x1;
    map_merge_style(colors,attrs,multi,idx0,cp,attr);
    map_merge_style(colors,attrs,multi,idx1,cp,attr);
    if(x1>x0){mask[idx0]|=MAP_E; mask[idx1]|=MAP_W;}
    else if(x1<x0){mask[idx0]|=MAP_W; mask[idx1]|=MAP_E;}
    else if(y1>y0){mask[idx0]|=MAP_S; mask[idx1]|=MAP_N;}
    else if(y1<y0){mask[idx0]|=MAP_N; mask[idx1]|=MAP_S;}
}

static void map_draw_mask_segment_layer(unsigned char *mask,int *colors,int *attrs,unsigned char *multi,
                                        int w,int h,int x0,int y0,int x1,int y1,int cp,int attr)
{
    int steps=abs(x1-x0)>abs(y1-y0)?abs(x1-x0):abs(y1-y0);
    int px=x0, py=y0;
    map_merge_style(colors,attrs,multi,py*w+px,cp,attr);
    if(steps<1) return;
    for(int i=1;i<=steps;i++){
        int nx=(int)(x0+((x1-x0)*(double)i)/steps+0.5);
        int ny=(int)(y0+((y1-y0)*(double)i)/steps+0.5);
        while(px!=nx||py!=ny){
            int stepx=px==nx?0:(nx>px?1:-1);
            int stepy=py==ny?0:(ny>py?1:-1);
            if(stepx&&stepy){
                map_add_link_layer(mask,colors,attrs,multi,w,h,px,py,px+stepx,py,cp,attr);
                px+=stepx;
            } else {
                map_add_link_layer(mask,colors,attrs,multi,w,h,px,py,px+stepx,py+stepy,cp,attr);
                px+=stepx;
                py+=stepy;
            }
        }
    }
}

static void map_draw_label(int y,int x,int maxx,const char *text,int cp)
{
    int len=(int)strlen(text);
    if(y<0||y>=LINES||x>=maxx) return;
    if(x<0){
        text+=-x;
        len=(int)strlen(text);
        x=0;
    }
    if(len<=0) return;
    if(x+len>maxx) len=maxx-x;
    if(len<=1) return;
    attron(COLOR_PAIR(cp)|A_BOLD);
    mvprintw(y,x,"%.*s",len,text);
    attroff(COLOR_PAIR(cp)|A_BOLD);
}

static void map_draw_vehicle_glyph(int y,int x,char glyph,int cp,int attr)
{
    attron(COLOR_PAIR(cp)|attr);
    if(glyph=='s') mvaddstr(y,x,U_BULLETO);
    else if(glyph=='v') mvaddstr(y,x,U_BULLET);
    else mvaddch(y,x,glyph);
    attroff(COLOR_PAIR(cp)|attr);
}

static int map_label_slot_ok(const unsigned char *occ,int w,int h,int x,int y,int len)
{
    if(len<=0||x<0||y<0||y>=h||x+len>w) return 0;
    for(int i=0;i<len;i++) if(occ[y*w+x+i]) return 0;
    return 1;
}

static void map_label_slot_mark(unsigned char *occ,int w,int h,int x,int y,int len)
{
    int y0=y>0?y-1:y;
    int y1=y+1<h?y+1:y;
    int x0=x>0?x-1:x;
    int x1=x+len<w?x+len:w-1;
    for(int yy=y0;yy<=y1;yy++) for(int xx=x0;xx<=x1;xx++) occ[yy*w+xx]=1;
}

static int map_find_label_slot(const unsigned char *occ,int w,int h,int mx,int my,int len,int *outx,int *outy)
{
    int base=mx-(len/2);
    const int cand[10][2]={
        {0,0},{0,-1},{0,1},{len/2+2,0},{-(len/2+2),0},
        {len/2+2,-1},{-(len/2+2),-1},{len/2+2,1},{-(len/2+2),1},{0,2}
    };
    for(int i=0;i<10;i++){
        int x=base+cand[i][0];
        int y=my+cand[i][1];
        if(map_label_slot_ok(occ,w,h,x,y,len)){*outx=x;*outy=y;return 1;}
    }
    return 0;
}

static void format_commune_label(const char *name, char *out, size_t out_sz)
{
    wchar_t letters[96];
    wchar_t initials[8];
    mbstate_t st;
    size_t nletters=0;
    int ninitials=0;
    int token_len=0;
    wchar_t token_first=0;
    const char *p=name;

    memset(&st,0,sizeof(st));
    while(*p&&nletters<sizeof(letters)/sizeof(letters[0])){
        wchar_t wc;
        size_t n=mbrtowc(&wc,p,MB_CUR_MAX,&st);
        if(n==(size_t)-1||n==(size_t)-2){
            memset(&st,0,sizeof(st));
            p++;
            continue;
        }
        if(n==0) break;
        if(iswalpha(wc)){
            letters[nletters++]=wc;
            if(token_len==0) token_first=wc;
            token_len++;
        } else if(token_len>0){
            wchar_t lower=towlower(token_first);
            if(!(token_len==1&&(lower==L'd'||lower==L'l'))&&ninitials<(int)(sizeof(initials)/sizeof(initials[0])))
                initials[ninitials++]=towupper(token_first);
            token_len=0;
            token_first=0;
        }
        p+=n;
    }
    if(token_len>0){
        wchar_t lower=towlower(token_first);
        if(!(token_len==1&&(lower==L'd'||lower==L'l'))&&ninitials<(int)(sizeof(initials)/sizeof(initials[0])))
            initials[ninitials++]=towupper(token_first);
    }

    if(!nletters){
        snprintf(out,out_sz,"?");
        return;
    }

    {
        wchar_t chosen[3];
        int nchosen;
        char *dst=out;
        size_t rem=out_sz;
        mbstate_t out_st;

        if(ninitials>=2){
            nchosen=ninitials>=3?3:ninitials;
            for(int i=0;i<nchosen;i++) chosen[i]=towupper(initials[i]);
        } else {
            nchosen=(nletters>=3)?3:(int)nletters;
            chosen[0]=towupper(letters[0]);
            if(nchosen==1){
                chosen[0]=towupper(letters[0]);
            } else if(nchosen==2){
                chosen[1]=towlower(letters[1]);
            } else {
                chosen[1]=towlower(letters[(nletters-1)/2]);
                chosen[2]=towlower(letters[nletters-1]);
            }
        }

        memset(&out_st,0,sizeof(out_st));
        if(rem) *dst='\0';
        for(int i=0;i<nchosen;i++){
            char mb[MB_CUR_MAX];
            size_t wn=wcrtomb(mb,chosen[i],&out_st);
            if(wn==(size_t)-1||wn>=rem){
                if(i==0) snprintf(out,out_sz,"?");
                break;
            }
            memcpy(dst,mb,wn);
            dst+=wn;
            rem-=wn;
            *dst='\0';
        }
    }
}

static int line_route_bounds(double *minlon,double *maxlon,double *minlat,double *maxlat)
{
    if(!g_has_line_route||g_line_route.npoints<=0) return 0;

    *minlon=*maxlon=g_line_route.points[0].lon;
    *minlat=*maxlat=g_line_route.points[0].lat;
    for(int i=1;i<g_line_route.npoints;i++){
        MapPoint *p=&g_line_route.points[i];
        if(p->lon<*minlon) *minlon=p->lon;
        if(p->lon>*maxlon) *maxlon=p->lon;
        if(p->lat<*minlat) *minlat=p->lat;
        if(p->lat>*maxlat) *maxlat=p->lat;
    }
    return 1;
}

static int toulouse_line_route_bounds(double *minlon,double *maxlon,double *minlat,double *maxlat)
{
    if(!g_has_tls_line_route||g_tls_line_route.npoints<=0) return 0;

    *minlon=*maxlon=g_tls_line_route.points[0].lon;
    *minlat=*maxlat=g_tls_line_route.points[0].lat;
    for(int i=1;i<g_tls_line_route.npoints;i++){
        MapPoint *p=&g_tls_line_route.points[i];
        if(p->lon<*minlon) *minlon=p->lon;
        if(p->lon>*maxlon) *maxlon=p->lon;
        if(p->lat<*minlat) *minlat=p->lat;
        if(p->lat>*maxlat) *maxlat=p->lat;
    }
    return 1;
}

static double clampd(double v,double lo,double hi)
{
    if(v<lo) return lo;
    if(v>hi) return hi;
    return v;
}

static void vehicle_focus_center(double fallback_minlon,double fallback_maxlon,
                                 double fallback_minlat,double fallback_maxlat,
                                 double *out_lon,double *out_lat)
{
    double sum_lon=0.0, sum_lat=0.0;
    int n=0;

    for(int i=0;i<g_nvehicles;i++){
        if(g_vehicles[i].lon==0&&g_vehicles[i].lat==0) continue;
        sum_lon+=g_vehicles[i].lon;
        sum_lat+=g_vehicles[i].lat;
        n++;
    }
    if(n>0){
        *out_lon=sum_lon/n;
        *out_lat=sum_lat/n;
        return;
    }

    if(line_route_bounds(&fallback_minlon,&fallback_maxlon,&fallback_minlat,&fallback_maxlat)){
        *out_lon=(fallback_minlon+fallback_maxlon)*0.5;
        *out_lat=(fallback_minlat+fallback_maxlat)*0.5;
        return;
    }

    *out_lon=(fallback_minlon+fallback_maxlon)*0.5;
    *out_lat=(fallback_minlat+fallback_maxlat)*0.5;
}

static void apply_vehicle_zoom(double base_minlon,double base_maxlon,double base_minlat,double base_maxlat,
                               int zoom,double *out_minlon,double *out_maxlon,double *out_minlat,double *out_maxlat)
{
    double center_lon, center_lat;
    double span_lon=base_maxlon-base_minlon;
    double span_lat=base_maxlat-base_minlat;
    double scale=(double)(1<<zoom);
    double half_lon, half_lat;

    if(zoom<=0||span_lon<=0.0||span_lat<=0.0){
        *out_minlon=base_minlon; *out_maxlon=base_maxlon;
        *out_minlat=base_minlat; *out_maxlat=base_maxlat;
        return;
    }

    vehicle_focus_center(base_minlon,base_maxlon,base_minlat,base_maxlat,&center_lon,&center_lat);
    half_lon=span_lon/(scale*2.0);
    half_lat=span_lat/(scale*2.0);
    center_lon=clampd(center_lon,base_minlon+half_lon,base_maxlon-half_lon);
    center_lat=clampd(center_lat,base_minlat+half_lat,base_maxlat-half_lat);

    *out_minlon=center_lon-half_lon;
    *out_maxlon=center_lon+half_lon;
    *out_minlat=center_lat-half_lat;
    *out_maxlat=center_lat+half_lat;
}

/* Select the best available bounds by combining basemap, route and live vehicle extents. */
static int compute_vehicle_map_bounds(int *out_has_basemap,int *out_has_route,
                                      double *out_base_minlon,double *out_base_maxlon,
                                      double *out_base_minlat,double *out_base_maxlat,
                                      double *out_minlon,double *out_maxlon,
                                      double *out_minlat,double *out_maxlat)
{
    int has_basemap = g_has_metro_map&&g_metro_map.npaths>0;
    int has_route = g_has_line_route&&g_line_route.npaths>0;
    double base_minlon, base_maxlon, base_minlat, base_maxlat;

    if(!has_basemap&&!has_route) return 0;

    if(has_basemap){
        base_minlon=g_metro_map.minlon; base_maxlon=g_metro_map.maxlon;
        base_minlat=g_metro_map.minlat; base_maxlat=g_metro_map.maxlat;
    } else if(line_route_bounds(&base_minlon,&base_maxlon,&base_minlat,&base_maxlat)){
        double pad_lon=(base_maxlon-base_minlon)*0.08;
        double pad_lat=(base_maxlat-base_minlat)*0.08;
        if(pad_lon<0.0025) pad_lon=0.0025;
        if(pad_lat<0.0025) pad_lat=0.0025;
        base_minlon-=pad_lon; base_maxlon+=pad_lon;
        base_minlat-=pad_lat; base_maxlat+=pad_lat;
    } else {
        base_minlon=MAP_BDX_WEST; base_maxlon=MAP_BDX_EAST;
        base_minlat=MAP_BDX_SOUTH; base_maxlat=MAP_BDX_NORTH;
    }

    apply_vehicle_zoom(base_minlon,base_maxlon,base_minlat,base_maxlat,
                       g_vehicle_zoom,out_minlon,out_maxlon,out_minlat,out_maxlat);

    if(out_has_basemap) *out_has_basemap=has_basemap;
    if(out_has_route) *out_has_route=has_route;
    if(out_base_minlon) *out_base_minlon=base_minlon;
    if(out_base_maxlon) *out_base_maxlon=base_maxlon;
    if(out_base_minlat) *out_base_minlat=base_minlat;
    if(out_base_maxlat) *out_base_maxlat=base_maxlat;
    return 1;
}

static int vehicle_detail_map_covers(double minlon,double maxlon,double minlat,double maxlat)
{
    if(!g_vehicle_detail_map_valid) return 0;
    if(g_vehicle_detail_map_line_gid!=g_lines[g_sel_line].gid) return 0;
    if(g_vehicle_detail_map_zoom!=g_vehicle_zoom) return 0;
    return minlon>=g_vehicle_detail_map.minlon && maxlon<=g_vehicle_detail_map.maxlon
        && minlat>=g_vehicle_detail_map.minlat && maxlat<=g_vehicle_detail_map.maxlat;
}

static void expand_vehicle_detail_bounds(double base_minlon,double base_maxlon,double base_minlat,double base_maxlat,
                                         double minlon,double maxlon,double minlat,double maxlat,
                                         double *out_minlon,double *out_maxlon,double *out_minlat,double *out_maxlat)
{
    double pad_lon=(maxlon-minlon)*0.35;
    double pad_lat=(maxlat-minlat)*0.35;

    *out_minlon=minlon-pad_lon;
    *out_maxlon=maxlon+pad_lon;
    *out_minlat=minlat-pad_lat;
    *out_maxlat=maxlat+pad_lat;

    if(*out_minlon<base_minlon) *out_minlon=base_minlon;
    if(*out_maxlon>base_maxlon) *out_maxlon=base_maxlon;
    if(*out_minlat<base_minlat) *out_minlat=base_minlat;
    if(*out_maxlat>base_maxlat) *out_maxlat=base_maxlat;
}

static void ensure_vehicle_detail_map(void)
{
    double base_minlon, base_maxlon, base_minlat, base_maxlat;
    double minlon, maxlon, minlat, maxlat;
    double req_minlon, req_maxlon, req_minlat, req_maxlat;
    int line_gid=g_lines[g_sel_line].gid;

    if(g_vehicle_zoom<=0) return;
    if(!compute_vehicle_map_bounds(NULL,NULL,&base_minlon,&base_maxlon,&base_minlat,&base_maxlat,
                                   &minlon,&maxlon,&minlat,&maxlat)) return;
    if(vehicle_detail_map_covers(minlon,maxlon,minlat,maxlat)) return;

    expand_vehicle_detail_bounds(base_minlon,base_maxlon,base_minlat,base_maxlat,
                                 minlon,maxlon,minlat,maxlat,
                                 &req_minlon,&req_maxlon,&req_minlat,&req_maxlat);
    reset_vehicle_detail_map();
    g_vehicle_detail_map_valid=1;
    g_vehicle_detail_map_zoom=g_vehicle_zoom;
    g_vehicle_detail_map_line_gid=line_gid;
    g_has_vehicle_detail_map=fetch_detail_map(req_minlon,req_maxlon,req_minlat,req_maxlat,
                                              g_vehicle_zoom,&g_vehicle_detail_map)>0;
}

static void draw_vehicle_map_panel(int y1,int x1,int y2,int x2)
{
    int legend_y=y1+1, py=y1+3, px=x1+2;
    int w=x2-x1-3, h=y2-py-1;
    double minlon, maxlon, minlat, maxlat;
    int line_cp;
    int zoom_scale=1<<g_vehicle_zoom;
    int has_basemap, has_route;
    AtlasMap *detail_map=NULL;
    char meta[48], legend[160], footer[160];

    if(!compute_vehicle_map_bounds(&has_basemap,&has_route,NULL,NULL,NULL,NULL,
                                   &minlon,&maxlon,&minlat,&maxlat)) return;
    line_cp=get_lcp(g_lines[g_sel_line].ident);
    if(!line_cp) line_cp=CP_ACCENT;
    if(g_vehicle_zoom>0&&g_has_vehicle_detail_map
    && g_vehicle_detail_map_line_gid==g_lines[g_sel_line].gid
    && g_vehicle_detail_map_zoom==g_vehicle_zoom) detail_map=&g_vehicle_detail_map;

    snprintf(meta,sizeof(meta),"zoom x%d",zoom_scale);
    panel_box(y1,x1,y2,x2,"ASCII Map",meta);
    if(w<18||h<6){
        attron(A_DIM);
        mvprintw(y1+2,x1+2,"map area too small");
        attroff(A_DIM);
        return;
    }

    attron(A_DIM);
    snprintf(legend,sizeof(legend),
             "%s%s%s"U_BULLET" vehicule "U_BULLETO" arret #=superposes",
             has_basemap?"communes":"fond metro",
             detail_map?" | roads . rail = rivers ~":"",
             has_route?" | chemin (gras=aller dim=retour) | ":" | ");
    print_fit(legend_y,x1+2,x2-x1-3,legend);
    attroff(A_DIM);

    {
        char glyph[h*w];
        unsigned char colors[h*w], prio[h*w];
        unsigned char boundary_mask[h*w], road_mask[h*w], rail_mask[h*w], water_mask[h*w];
        unsigned char route_mask[h*w], route_dir[h*w];
        int attrs[h*w];
        memset(glyph,' ',sizeof(glyph));
        memset(colors,0,sizeof(colors));
        memset(prio,0,sizeof(prio));
        memset(boundary_mask,0,sizeof(boundary_mask));
        memset(road_mask,0,sizeof(road_mask));
        memset(rail_mask,0,sizeof(rail_mask));
        memset(water_mask,0,sizeof(water_mask));
        memset(route_mask,0,sizeof(route_mask));
        memset(route_dir,0,sizeof(route_dir));
        memset(attrs,0,sizeof(attrs));

        if(has_basemap){
            for(int i=0;i<g_metro_map.npaths;i++){
                MapPath *path=&g_metro_map.paths[i];
                for(int j=1;j<path->count;j++){
                    MapPoint *a=&g_metro_map.points[path->start+j-1];
                    MapPoint *b=&g_metro_map.points[path->start+j];
                    double lon0=a->lon, lat0=a->lat, lon1=b->lon, lat1=b->lat;
                    int x0,y0,x1p,y1p;
                    if(!map_clip_segment(&lon0,&lat0,&lon1,&lat1,minlon,maxlon,minlat,maxlat)) continue;
                    map_project(lon0,lat0,minlon,maxlon,minlat,maxlat,w,h,&x0,&y0);
                    map_project(lon1,lat1,minlon,maxlon,minlat,maxlat,w,h,&x1p,&y1p);
                    map_draw_mask_segment(boundary_mask,w,h,x0,y0,x1p,y1p);
                }
            }
        }

        if(detail_map){
            for(int i=0;i<detail_map->npaths;i++){
                MapPath *path=&detail_map->paths[i];
                unsigned char *target = path->kind==MAP_KIND_ROAD ? road_mask
                                       : path->kind==MAP_KIND_RAIL ? rail_mask
                                       : water_mask;
                for(int j=1;j<path->count;j++){
                    MapPoint *a=&detail_map->points[path->start+j-1];
                    MapPoint *b=&detail_map->points[path->start+j];
                    double lon0=a->lon, lat0=a->lat, lon1=b->lon, lat1=b->lat;
                    int x0,y0,x1p,y1p;
                    if(!map_clip_segment(&lon0,&lat0,&lon1,&lat1,minlon,maxlon,minlat,maxlat)) continue;
                    map_project(lon0,lat0,minlon,maxlon,minlat,maxlat,w,h,&x0,&y0);
                    map_project(lon1,lat1,minlon,maxlon,minlat,maxlat,w,h,&x1p,&y1p);
                    map_draw_mask_segment(target,w,h,x0,y0,x1p,y1p);
                }
            }
        }

        if(has_route){
            for(int i=0;i<g_line_route.npaths;i++){
                MapPath *path=&g_line_route.paths[i];
                unsigned char flag=path->kind==MAP_KIND_ROUTE_RETOUR?2:1;
                for(int j=1;j<path->count;j++){
                    MapPoint *a=&g_line_route.points[path->start+j-1];
                    MapPoint *b=&g_line_route.points[path->start+j];
                    double lon0=a->lon, lat0=a->lat, lon1=b->lon, lat1=b->lat;
                    int x0,y0,x1p,y1p;
                    if(!map_clip_segment(&lon0,&lat0,&lon1,&lat1,minlon,maxlon,minlat,maxlat)) continue;
                    map_project(lon0,lat0,minlon,maxlon,minlat,maxlat,w,h,&x0,&y0);
                    map_project(lon1,lat1,minlon,maxlon,minlat,maxlat,w,h,&x1p,&y1p);
                    map_draw_mask_segment_marked(route_mask,route_dir,w,h,x0,y0,x1p,y1p,flag);
                }
            }
        }

        for(int i=0;i<g_nvehicles;i++){
            Vehicle *v=&g_vehicles[i];
            int mx, my, idx;
            char mark=v->arret?'s':'v';
            if(v->lon==0&&v->lat==0) continue;
            if(v->lon<minlon||v->lon>maxlon||v->lat<minlat||v->lat>maxlat) continue;
            map_project(v->lon,v->lat,minlon,maxlon,minlat,maxlat,w,h,&mx,&my);
            idx=my*w+mx;
            if(prio[idx]>=4) map_set_cell(glyph,colors,attrs,prio,w,h,mx,my,'#',line_cp,A_BOLD,5);
            else map_set_cell(glyph,colors,attrs,prio,w,h,mx,my,mark,line_cp,A_BOLD,4);
        }

        for(int y=0;y<h;y++){
            move(py+y,px);
            for(int x=0;x<w;x++){
                int idx=y*w+x;
                if(prio[idx]>=4){
                    addch(' ');
                    continue;
                }
                if(route_mask[idx]){
                    int attr=(route_dir[idx]&1)?A_BOLD:A_DIM;
                    attron(COLOR_PAIR(line_cp)|attr);
                    addstr(map_mask_glyph(route_mask[idx]));
                    attroff(COLOR_PAIR(line_cp)|attr);
                } else if(water_mask[idx]){
                    attron(COLOR_PAIR(CP_BLUE)|A_BOLD);
                    addch('~');
                    attroff(COLOR_PAIR(CP_BLUE)|A_BOLD);
                } else if(rail_mask[idx]){
                    attron(COLOR_PAIR(CP_MAGENTA));
                    addch('=');
                    attroff(COLOR_PAIR(CP_MAGENTA));
                } else if(boundary_mask[idx]){
                    attron(COLOR_PAIR(CP_SECTION)|A_BOLD);
                    addstr(map_mask_glyph(boundary_mask[idx]));
                    attroff(COLOR_PAIR(CP_SECTION)|A_BOLD);
                } else if(road_mask[idx]){
                    attron(A_DIM);
                    addch('.');
                    attroff(A_DIM);
                } else if(glyph[idx]!=' '){
                    attron(COLOR_PAIR(colors[idx])|attrs[idx]);
                    addch(glyph[idx]);
                    attroff(COLOR_PAIR(colors[idx])|attrs[idx]);
                } else {
                    addch(' ');
                }
            }
        }

        if(has_basemap&&w>=28){
            unsigned char label_occ[h*w];
            memset(label_occ,0,sizeof(label_occ));
            for(int i=0;i<g_nvehicles;i++){
                Vehicle *v=&g_vehicles[i];
                int mx, my;
                if(v->lon==0&&v->lat==0) continue;
                if(v->lon<minlon||v->lon>maxlon||v->lat<minlat||v->lat>maxlat) continue;
                map_project(v->lon,v->lat,minlon,maxlon,minlat,maxlat,w,h,&mx,&my);
                if(mx>=0&&mx<w&&my>=0&&my<h) label_occ[my*w+mx]=1;
            }
            if(detail_map){
                for(int i=0;i<h*w;i++) if(road_mask[i]||rail_mask[i]||water_mask[i]) label_occ[i]=1;
            }
            for(int i=0;i<g_metro_map.nlabels;i++){
                char lbl[16];
                int mx, my;
                int lx, ly;
                int len;
                if(g_metro_map.labels[i].rank<3) continue;
                format_commune_label(g_metro_map.labels[i].name,lbl,sizeof(lbl));
                len=(int)strlen(lbl);
                if(g_metro_map.labels[i].lon<minlon||g_metro_map.labels[i].lon>maxlon
                || g_metro_map.labels[i].lat<minlat||g_metro_map.labels[i].lat>maxlat) continue;
                map_project(g_metro_map.labels[i].lon,g_metro_map.labels[i].lat,minlon,maxlon,minlat,maxlat,w,h,&mx,&my);
                if(!map_find_label_slot(label_occ,w,h,mx,my,len,&lx,&ly)) continue;
                map_draw_label(py+ly,px+lx,px+w,lbl,CP_SECTION);
                map_label_slot_mark(label_occ,w,h,lx,ly,len);
            }
        }

        for(int i=0;i<g_nvehicles;i++){
            Vehicle *v=&g_vehicles[i];
            int mx, my, idx;
            if(v->lon==0&&v->lat==0) continue;
            if(v->lon<minlon||v->lon>maxlon||v->lat<minlat||v->lat>maxlat) continue;
            map_project(v->lon,v->lat,minlon,maxlon,minlat,maxlat,w,h,&mx,&my);
            idx=my*w+mx;
            map_draw_vehicle_glyph(py+my,px+mx,glyph[idx],colors[idx],attrs[idx]);
        }
    }

    attron(A_DIM);
    if(g_vehicle_zoom>0){
        if(detail_map){
            snprintf(footer,sizeof(footer),
                     "%s%s%d veh.  %d names  zoom=x%d  detail roads=%d rail=%d rivers=%d",
                     has_basemap?"geo.api.gouv.fr communes":"metropole fallback",
                     has_route?"  sv_chem_l overlay  ":"  ",
                     g_nvehicles,
                     has_basemap?g_metro_map.nlabels:0,
                     zoom_scale,
                     detail_map->road_paths,
                     detail_map->rail_paths,
                     detail_map->water_paths);
        } else {
            snprintf(footer,sizeof(footer),
                     "%s%s%d veh.  %d names  zoom=x%d  detail unavailable",
                     has_basemap?"geo.api.gouv.fr communes":"metropole fallback",
                     has_route?"  sv_chem_l overlay  ":"  ",
                     g_nvehicles,
                     has_basemap?g_metro_map.nlabels:0,
                     zoom_scale);
        }
    } else {
        snprintf(footer,sizeof(footer),
                 "%s%s%d veh.  %d names  zoom=x%d",
                 has_basemap?"geo.api.gouv.fr communes":"metropole fallback",
                 has_route?"  sv_chem_l overlay  ":"  ",
                 g_nvehicles,
                 has_basemap?g_metro_map.nlabels:0,
                 zoom_scale);
    }
    print_fit(y2-1,x1+2,x2-x1-3,footer);
    attroff(A_DIM);
}

static void draw_vehicles(time_t lr)
{
    Line *ln=&g_lines[g_sel_line];
    char t[128],bc[128], code[16];
    int total_delayed=0, total_stopped=0, total_speed=0;
    int line_alerts=count_line_alerts(ln->gid);
    int cards_top=3, top=3;
    int has_map;
    line_code(ln,code,sizeof(code));
    snprintf(t,sizeof(t),"%s / %s",code,ln->libelle);
    snprintf(bc,sizeof(bc),"Lignes "U_ARROW" live telemetry");
    if(!g_map_attempted){
        g_map_attempted=1;
        g_has_metro_map=fetch_metro_map(&g_metro_map)>0;
        toast(g_has_metro_map?"Carte communes chargee":"Carte communes indisponible");
    }
    ensure_line_route();
    ensure_vehicle_detail_map();
    has_map=(g_has_metro_map&&g_metro_map.npaths>0)||(g_has_line_route&&g_line_route.npaths>0);
    int lcp=get_lcp(ln->ident);
    if(lcp){
        attron(COLOR_PAIR(lcp)|A_BOLD); mvhline(0,0,' ',COLS);
        mvprintw(0,2," %s ",t);
        time_t now=time(NULL); struct tm *tm=localtime(&now);
        mvprintw(0,COLS-10,"%02d:%02d:%02d",tm->tm_hour,tm->tm_min,tm->tm_sec);
        attroff(COLOR_PAIR(lcp)|A_BOLD);
    } else draw_header(t,bc);
    draw_tabs();
    for(int i=0;i<g_nvehicles;i++){
        total_speed+=g_vehicles[i].vitesse;
        if(strcmp(g_vehicles[i].etat,"RETARD")==0&&g_vehicles[i].retard>=60) total_delayed++;
        if(g_vehicles[i].arret) total_stopped++;
    }

    if(COLS>=92&&LINES>=24){
        int gap=1;
        int cw=(COLS-5-gap*3)/4;
        char buf[32];
        snprintf(buf,sizeof(buf),"%d",g_nvehicles);
        stat_card(cards_top,2,cw,"LIVE",buf,"vehicules suivis",CP_ACCENT);
        snprintf(buf,sizeof(buf),"%d",total_delayed);
        stat_card(cards_top,2+cw+gap,cw,"DELAY",buf,"retards > 1 min",total_delayed?CP_ALERT_MED:CP_GREEN);
        snprintf(buf,sizeof(buf),"%dkm/h",g_nvehicles?total_speed/g_nvehicles:0);
        stat_card(cards_top,2+(cw+gap)*2,cw,"AVG SPD",buf,"vitesse moyenne",CP_CYAN_T);
        snprintf(buf,sizeof(buf),"%d",line_alerts);
        stat_card(cards_top,2+(cw+gap)*3,cw,"LINE SIG",buf,line_alerts?"messages actifs":"aucun incident",line_alerts?CP_ALERT_MED:CP_GREEN);
        top+=5;
    }

    {
        int area_top=top;
        int area_bottom=LINES-3;
        int alert_h=(line_alerts&&LINES-area_top>=16)?6:0;
        int body_bottom=area_bottom-alert_h;

        if(has_map&&COLS>=146&&body_bottom-area_top>=12){
            int map_x2=COLS*44/100;
            int rx1, split;
            if(map_x2<54) map_x2=54;
            if(map_x2>COLS-46) map_x2=COLS-46;
            rx1=map_x2+1;
            split=area_top+(body_bottom-area_top)/2;
            draw_vehicle_map_panel(area_top,1,body_bottom,map_x2);
            draw_vehicle_lane_panel(area_top,rx1,split,COLS-2,"ALLER");
            draw_vehicle_lane_panel(split+1,rx1,body_bottom,COLS-2,"RETOUR");
        } else if(has_map&&body_bottom-area_top>=18){
            int map_h=8;
            int map_bottom, lanes_top;
            if(map_h>body_bottom-area_top-10) map_h=(body_bottom-area_top)/3;
            if(map_h<6) map_h=6;
            map_bottom=area_top+map_h;
            lanes_top=map_bottom+1;
            draw_vehicle_map_panel(area_top,1,map_bottom,COLS-2);
            if(COLS>=122&&body_bottom-lanes_top>=10){
                int mid=COLS/2;
                draw_vehicle_lane_panel(lanes_top,1,body_bottom,mid-1,"ALLER");
                draw_vehicle_lane_panel(lanes_top,mid,body_bottom,COLS-2,"RETOUR");
            } else {
                int split=lanes_top+(body_bottom-lanes_top)/2;
                draw_vehicle_lane_panel(lanes_top,1,split,COLS-2,"ALLER");
                draw_vehicle_lane_panel(split+1,1,body_bottom,COLS-2,"RETOUR");
            }
        } else if(COLS>=122&&body_bottom-area_top>=10){
            int mid=COLS/2;
            draw_vehicle_lane_panel(area_top,1,body_bottom,mid-1,"ALLER");
            draw_vehicle_lane_panel(area_top,mid,body_bottom,COLS-2,"RETOUR");
        } else {
            int split=area_top+(body_bottom-area_top)/2;
            draw_vehicle_lane_panel(area_top,1,split,COLS-2,"ALLER");
            draw_vehicle_lane_panel(split+1,1,body_bottom,COLS-2,"RETOUR");
        }

        if(alert_h){
            int y1=body_bottom+1, y2=area_bottom, y=y1+2;
            char meta[32];
            snprintf(meta,sizeof(meta),"%d alert%s",line_alerts,line_alerts>1?"s":"");
            panel_box(y1,1,y2,COLS-2,"Line Alerts",meta);
            for(int i=0;i<g_nalerts&&y<y2;i++){
                int cp;
                if(g_alerts[i].ligne_id!=ln->gid) continue;
                cp=strncmp(g_alerts[i].severite,"3_",2)==0?CP_ALERT_HI:strncmp(g_alerts[i].severite,"2_",2)==0?CP_ALERT_MED:CP_ALERT_LO;
                attron(COLOR_PAIR(cp)|A_BOLD);
                mvprintw(y,3,"%s ",cp==CP_ALERT_HI?U_DIAMOND:cp==CP_ALERT_MED?U_WARN:U_INFO);
                attroff(COLOR_PAIR(cp)|A_BOLD);
                attron(A_BOLD);
                print_fit(y,5,COLS-12,g_alerts[i].titre[0]?g_alerts[i].titre:"Message reseau");
                attroff(A_BOLD);
                y++;
                if(y<y2) y+=draw_wrapped_block(y,5,COLS-12,1,g_alerts[i].message,cp,0);
                if(y<y2) y++;
            }
        }
    }

    draw_toast_msg();
    int rem=VEHICLE_REFRESH_SEC-(int)(time(NULL)-lr); if(rem<0)rem=0;
    char mid[96],ri[32];
    snprintf(mid,sizeof(mid)," q:back"U_MDOT"r:refresh"U_MDOT"+/-/0:zoom"U_MDOT"a:alertes"U_MDOT"p:arrets"U_MDOT"t:theme   %ds",rem);
    snprintf(ri,sizeof(ri),"%d veh.",g_nvehicles);
    draw_status(mid,ri);

    /* mini progress in status */
    int pct=(VEHICLE_REFRESH_SEC-rem)*100/VEHICLE_REFRESH_SEC;
    int bx=COLS-20; if(bx>40) progress_bar(LINES-1,bx,6,pct);
}

static int count_atlas_visible_paths(void)
{
    int n=0;
    for(int i=0;i<g_atlas_routes.npaths;i++) if(atlas_route_visible_gid(g_atlas_routes.line_gids[i])) n++;
    return n;
}

/* Compose the atlas canvas by layering geography, focus routes and labels on one ncurses grid. */
static void draw_atlas_map_panel(int y1,int x1,int y2,int x2)
{
    int legend_y=y1+1, py=y1+3, px=x1+2;
    int w=x2-x1-3, h=y2-py-1;
    int route_emphasis = g_atlas_focus_gid || g_atlas_search[0];
    double minlon=MAP_BDX_WEST, maxlon=MAP_BDX_EAST, minlat=MAP_BDX_SOUTH, maxlat=MAP_BDX_NORTH;
    int has_any=(g_has_metro_map&&g_metro_map.npaths>0)||(g_has_atlas_map&&g_atlas_map.npaths>0)||(g_has_atlas_routes&&g_atlas_routes.npaths>0);
    char legend[192], footer[192];

    panel_box(y1,x1,y2,x2,"Whole Network","communes + roads + rail + rivers + chemins");
    if(w<28||h<8){
        attron(A_DIM);
        mvprintw(y1+2,x1+2,"map area too small");
        attroff(A_DIM);
        return;
    }
    if(!has_any){
        attron(A_DIM);
        mvprintw(y1+2,x1+2,U_WARN" aucun fond de carte charge");
        attroff(A_DIM);
        return;
    }

    if(g_has_metro_map&&g_metro_map.npaths>0){
        minlon=g_metro_map.minlon; maxlon=g_metro_map.maxlon;
        minlat=g_metro_map.minlat; maxlat=g_metro_map.maxlat;
    }

    attron(A_DIM);
    snprintf(legend,sizeof(legend),"communes | roads . | rail = | rivers ~ | chemin %s  /=%s",
             route_emphasis?"front":"bg",
             g_atlas_search[0]?g_atlas_search:(g_atlas_focus_gid?"focus":"all"));
    print_fit(legend_y,x1+2,x2-x1-3,legend);
    attroff(A_DIM);

    {
        unsigned char boundary_mask[h*w], road_mask[h*w], rail_mask[h*w], water_mask[h*w], route_mask[h*w];
        unsigned char route_multi[h*w], label_occ[h*w];
        int route_colors[h*w], route_attrs[h*w];
        memset(boundary_mask,0,sizeof(boundary_mask));
        memset(road_mask,0,sizeof(road_mask));
        memset(rail_mask,0,sizeof(rail_mask));
        memset(water_mask,0,sizeof(water_mask));
        memset(route_mask,0,sizeof(route_mask));
        memset(route_multi,0,sizeof(route_multi));
        memset(route_colors,0,sizeof(route_colors));
        memset(route_attrs,0,sizeof(route_attrs));
        memset(label_occ,0,sizeof(label_occ));

        if(g_has_metro_map&&g_metro_map.npaths>0){
            for(int i=0;i<g_metro_map.npaths;i++){
                MapPath *path=&g_metro_map.paths[i];
                for(int j=1;j<path->count;j++){
                    MapPoint *a=&g_metro_map.points[path->start+j-1];
                    MapPoint *b=&g_metro_map.points[path->start+j];
                    int x0,y0,x1p,y1p;
                    map_project(a->lon,a->lat,minlon,maxlon,minlat,maxlat,w,h,&x0,&y0);
                    map_project(b->lon,b->lat,minlon,maxlon,minlat,maxlat,w,h,&x1p,&y1p);
                    map_draw_mask_segment(boundary_mask,w,h,x0,y0,x1p,y1p);
                }
            }
        }

        if(g_has_atlas_map&&g_atlas_map.npaths>0){
            for(int i=0;i<g_atlas_map.npaths;i++){
                MapPath *path=&g_atlas_map.paths[i];
                unsigned char *target = path->kind==MAP_KIND_ROAD ? road_mask
                                       : path->kind==MAP_KIND_RAIL ? rail_mask
                                       : water_mask;
                for(int j=1;j<path->count;j++){
                    MapPoint *a=&g_atlas_map.points[path->start+j-1];
                    MapPoint *b=&g_atlas_map.points[path->start+j];
                    int x0,y0,x1p,y1p;
                    map_project(a->lon,a->lat,minlon,maxlon,minlat,maxlat,w,h,&x0,&y0);
                    map_project(b->lon,b->lat,minlon,maxlon,minlat,maxlat,w,h,&x1p,&y1p);
                    map_draw_mask_segment(target,w,h,x0,y0,x1p,y1p);
                }
            }
        }

        if(g_has_atlas_routes&&g_atlas_routes.npaths>0){
            for(int i=0;i<g_atlas_routes.npaths;i++){
                const Line *ln;
                MapPath *path;
                int cp;
                int attr;
                if(!atlas_route_visible_gid(g_atlas_routes.line_gids[i])) continue;
                ln=line_by_gid(g_atlas_routes.line_gids[i]);
                path=&g_atlas_routes.paths[i];
                cp=ln?get_lcp(ln->ident):0;
                if(!cp) cp=CP_ACCENT;
                attr=path->kind==MAP_KIND_ROUTE_RETOUR?A_DIM:A_BOLD;
                for(int j=1;j<path->count;j++){
                    MapPoint *a=&g_atlas_routes.points[path->start+j-1];
                    MapPoint *b=&g_atlas_routes.points[path->start+j];
                    int x0,y0,x1p,y1p;
                    map_project(a->lon,a->lat,minlon,maxlon,minlat,maxlat,w,h,&x0,&y0);
                    map_project(b->lon,b->lat,minlon,maxlon,minlat,maxlat,w,h,&x1p,&y1p);
                    map_draw_mask_segment_layer(route_mask,route_colors,route_attrs,route_multi,w,h,x0,y0,x1p,y1p,cp,attr);
                }
            }
        }

        for(int y=0;y<h;y++){
            move(py+y,px);
            for(int x=0;x<w;x++){
                int idx=y*w+x;
                if(water_mask[idx]){
                    attron(COLOR_PAIR(CP_BLUE)|A_BOLD);
                    addch('~');
                    attroff(COLOR_PAIR(CP_BLUE)|A_BOLD);
                    label_occ[idx]=1;
                } else if(rail_mask[idx]){
                    attron(COLOR_PAIR(CP_MAGENTA));
                    addch('=');
                    attroff(COLOR_PAIR(CP_MAGENTA));
                    label_occ[idx]=1;
                } else if(route_mask[idx] && route_emphasis){
                    if(route_multi[idx]){
                        attron(COLOR_PAIR(CP_ACCENT)|A_BOLD);
                        addch('#');
                        attroff(COLOR_PAIR(CP_ACCENT)|A_BOLD);
                    } else {
                        attron(COLOR_PAIR(route_colors[idx])|route_attrs[idx]);
                        addstr(map_mask_glyph(route_mask[idx]));
                        attroff(COLOR_PAIR(route_colors[idx])|route_attrs[idx]);
                    }
                    label_occ[idx]=1;
                } else if(road_mask[idx]){
                    attron(A_DIM);
                    addch('.');
                    attroff(A_DIM);
                } else if(route_mask[idx]){
                    if(route_multi[idx]){
                        attron(COLOR_PAIR(CP_ACCENT)|A_BOLD);
                        addch('#');
                        attroff(COLOR_PAIR(CP_ACCENT)|A_BOLD);
                    } else {
                        attron(COLOR_PAIR(route_colors[idx])|A_DIM);
                        addch(':');
                        attroff(COLOR_PAIR(route_colors[idx])|A_DIM);
                    }
                } else if(boundary_mask[idx]){
                    attron(COLOR_PAIR(CP_SECTION)|A_DIM);
                    addstr(map_mask_glyph(boundary_mask[idx]));
                    attroff(COLOR_PAIR(CP_SECTION)|A_DIM);
                } else {
                    addch(' ');
                }
            }
        }

        if(g_has_metro_map&&g_metro_map.nlabels>0&&w>=48){
            for(int i=0;i<g_metro_map.nlabels;i++){
                const char *lbl=g_metro_map.labels[i].name;
                int mx,my,lx,ly,len;
                if(!lbl[0]) continue;
                len=(int)strlen(lbl);
                map_project(g_metro_map.labels[i].lon,g_metro_map.labels[i].lat,minlon,maxlon,minlat,maxlat,w,h,&mx,&my);
                if(!map_find_label_slot(label_occ,w,h,mx,my,len,&lx,&ly)) continue;
                map_draw_label(py+ly,px+lx,px+w,lbl,CP_SECTION);
                map_label_slot_mark(label_occ,w,h,lx,ly,len);
            }
        }
    }

    attron(A_DIM);
    snprintf(footer,sizeof(footer),"geo.api.gouv.fr communes  overpass roads=%d rail=%d rivers=%d  chemin=%d",
             g_atlas_map.road_paths,g_atlas_map.rail_paths,g_atlas_map.water_paths,count_atlas_visible_paths());
    print_fit(y2-1,x1+2,x2-x1-3,footer);
    attroff(A_DIM);
}

static void draw_atlas(void)
{
    char bc[128], right[48], mid[128];
    int top=3;
    int visible_lines=g_atlas_focus_gid?1:g_natlas_filtered;
    int visible_paths;

    if(!g_map_attempted){
        g_map_attempted=1;
        g_has_metro_map=fetch_metro_map(&g_metro_map)>0;
    }
    if(!g_atlas_map_attempted){
        g_atlas_map_attempted=1;
        g_has_atlas_map=fetch_atlas_map(&g_atlas_map)>0;
        toast(g_has_atlas_map?"Road/rail/rivers charges":"Road/rail/rivers indisponibles");
    }
    if(!g_atlas_routes_attempted){
        g_atlas_routes_attempted=1;
        g_has_atlas_routes=fetch_atlas_routes(&g_atlas_routes)>0;
        toast(g_has_atlas_routes?"Chemins reseau charges":"Chemins reseau indisponibles");
    }

    if(g_atlas_focus_gid){
        const Line *ln=line_by_gid(g_atlas_focus_gid);
        char code[16];
        if(ln){
            line_code(ln,code,sizeof(code));
            snprintf(bc,sizeof(bc),"focus ligne %s / %s",code,ln->libelle);
        } else snprintf(bc,sizeof(bc),"focus ligne");
    } else if(g_atlas_search[0]) snprintf(bc,sizeof(bc),"filtre %s",g_atlas_search);
    else snprintf(bc,sizeof(bc),"all lines, whole metropole");

    draw_header("Atlas // Whole Map",bc);
    draw_tabs();

    visible_paths=count_atlas_visible_paths();
    if(COLS>=134&&LINES-top>=14){
        int sx1=1, sx2=34, mx1=sx2+1, mx2=COLS-2;
        int y1=top, y2=LINES-3;
        int head_y=y1+1, list_y=head_y+3;
        int rows=y2-list_y;

        panel_box(y1,sx1,y2,sx2,"Line Filter",g_atlas_focus_gid?"focus":"matches");
        panel_box(y1,mx1,y2,mx2,"Whole Network",g_has_atlas_routes?"atlas online":"atlas partial");

        attron(A_BOLD);
        mvprintw(head_y,sx1+2,"/ %s",g_atlas_search[0]?g_atlas_search:"all active lines");
        attroff(A_BOLD);
        attron(A_DIM);
        mvprintw(head_y+1,sx1+2,"%d visible lines  %d visible chemins",visible_lines,visible_paths);
        attroff(A_DIM);

        if(rows<1) rows=1;
        if(g_atlas_cursor<g_atlas_scroll) g_atlas_scroll=g_atlas_cursor;
        if(g_atlas_cursor>=g_atlas_scroll+rows) g_atlas_scroll=g_atlas_cursor-rows+1;

        for(int i=0;i<rows&&g_atlas_scroll+i<g_natlas_filtered;i++){
            int idx=g_atlas_filtered[g_atlas_scroll+i];
            Line *l=&g_lines[idx];
            int row=list_y+i;
            int selected=(g_atlas_scroll+i==g_atlas_cursor);
            int focused=(g_atlas_focus_gid==l->gid);
            if(selected) fill_span(row,sx1+1,sx2-1,CP_SEL,0);
            draw_line_badge(row,sx1+2,l);
            if(focused){attron(COLOR_PAIR(CP_ACCENT)|A_BOLD);mvprintw(row,sx1+8,U_DIAMOND);attroff(COLOR_PAIR(CP_ACCENT)|A_BOLD);}
            print_fit(row,sx1+10,sx2-sx1-11,l->libelle);
        }
        if(g_natlas_filtered>rows) scrollbar(list_y,rows,g_atlas_scroll,g_natlas_filtered);
        draw_atlas_map_panel(y1,mx1,y2,mx2);
    } else {
        draw_atlas_map_panel(top,1,LINES-3,COLS-2);
        if(LINES-top>=8){
            attron(A_DIM);
            mvprintw(top+1,3,"filter: %s",g_atlas_search[0]?g_atlas_search:"all active lines");
            mvprintw(top+2,3,"visible lines: %d  visible chemins: %d",visible_lines,visible_paths);
            attroff(A_DIM);
        }
    }

    draw_toast_msg();
    snprintf(mid,sizeof(mid)," /:filter"U_MDOT"Enter:focus"U_MDOT"r:reload"U_MDOT"q:back"U_MDOT"t:theme");
    snprintf(right,sizeof(right),"%d paths",visible_paths);
    draw_status(mid,right);
}

/* ── Screen: Alerts ──────────────────────────────────────────────── */

static const char *fln(int id){for(int i=0;i<g_nlines;i++) if(g_lines[i].gid==id) return g_lines[i].libelle; return NULL;}
static int fli(int id){for(int i=0;i<g_nlines;i++) if(g_lines[i].gid==id) return g_lines[i].ident; return -1;}

static void draw_alerts(void)
{
    int hi=count_alerts_prefix("3_");
    int med=count_alerts_prefix("2_");
    int low=g_nalerts-hi-med;
    int impacted=count_alerted_lines();
    int top=3;

    draw_header("Operations // Alerts",g_nalerts?"network pressure":"quiet network");
    draw_tabs();

    if(COLS>=94&&LINES>=24){
        int gap=1;
        int cw=(COLS-5-gap*3)/4;
        char buf[32];
        snprintf(buf,sizeof(buf),"%d",g_nalerts);
        stat_card(top,2,cw,"TOTAL",buf,"messages reseau",g_nalerts?CP_ALERT_MED:CP_GREEN);
        snprintf(buf,sizeof(buf),"%d",hi);
        stat_card(top,2+cw+gap,cw,"CRIT",buf,"severity 3",hi?CP_ALERT_HI:CP_GREEN);
        snprintf(buf,sizeof(buf),"%d",med);
        stat_card(top,2+(cw+gap)*2,cw,"WARN",buf,"severity 2",med?CP_ALERT_MED:CP_GREEN);
        snprintf(buf,sizeof(buf),"%d",impacted);
        stat_card(top,2+(cw+gap)*3,cw,"LINES",buf,"corridors impacted",impacted?CP_ALERT_MED:CP_GREEN);
        top+=5;
    }

    {
        int y1=top, y2=LINES-3;
        int cs=y1+2, ch=y2-cs;
        int vy=0;
        char meta[64];
        snprintf(meta,sizeof(meta),"%d crit / %d warn / %d info",hi,med,low);
        panel_box(y1,1,y2,COLS-2,"Alert Feed",meta);
        g_alert_total_h=0;

        if(!g_nalerts){
            attron(A_DIM);
            mvprintw(y1+3,3,U_CHECK" aucune alerte active");
            mvprintw(y1+5,3,"Le reseau est calme pour le moment.");
            attroff(A_DIM);
        }

        for(int i=0;i<g_nalerts;i++){
            Alert *a=&g_alerts[i];
            const char *ln_name=fln(a->ligne_id);
            int cp=strncmp(a->severite,"3_",2)==0?CP_ALERT_HI:strncmp(a->severite,"2_",2)==0?CP_ALERT_MED:CP_ALERT_LO;
            const char *ic=cp==CP_ALERT_HI?U_DIAMOND:cp==CP_ALERT_MED?U_WARN:U_INFO;
            int lcp=get_lcp(fli(a->ligne_id));
            int mw=COLS-14, po=0, ml=(int)strlen(a->message);
            int sy=cs+vy-g_alert_scroll;

            if(sy>=cs&&sy<cs+ch){
                attron(COLOR_PAIR(cp)|A_BOLD);
                mvprintw(sy,3,"%s",ic);
                attroff(COLOR_PAIR(cp)|A_BOLD);
                attron(A_BOLD);
                print_fit(sy,6,COLS-18,a->titre[0]?a->titre:"Message reseau");
                attroff(A_BOLD);
            }
            vy++;

            sy=cs+vy-g_alert_scroll;
            if(sy>=cs&&sy<cs+ch){
                if(ln_name){
                    if(lcp){
                        attron(COLOR_PAIR(lcp)|A_BOLD);
                        mvprintw(sy,6," %s ",ln_name);
                        attroff(COLOR_PAIR(lcp)|A_BOLD);
                    } else {
                        attron(COLOR_PAIR(cp)|A_BOLD);
                        mvprintw(sy,6," %s ",ln_name);
                        attroff(COLOR_PAIR(cp)|A_BOLD);
                    }
                    attron(A_DIM);
                    mvprintw(sy,8+(int)strlen(ln_name)," severity %s",a->severite);
                    attroff(A_DIM);
                } else {
                    attron(A_DIM);
                    mvprintw(sy,6,"network-wide severity %s",a->severite);
                    attroff(A_DIM);
                }
            }
            vy++;

            if(mw<20) mw=20;
            while(po<ml){
                int e=po, c=0, cut=0, last_space=-1;
                while(a->message[e]&&a->message[e]!='\n'&&c<mw){
                    if(a->message[e]==' ') last_space=e-po;
                    e++; c++;
                }
                cut=e-po;
                if(a->message[e]&&a->message[e]!='\n'&&c==mw&&last_space>0) cut=last_space;
                if(cut<=0) cut=e-po;
                sy=cs+vy-g_alert_scroll;
                if(sy>=cs&&sy<cs+ch) mvprintw(sy,8,"%.*s",cut,a->message+po);
                vy++;
                po+=cut;
                while(a->message[po]==' ') po++;
                if(a->message[po]=='\n') po++;
            }
            sy=cs+vy-g_alert_scroll;
            if(i<g_nalerts-1&&sy>=cs&&sy<cs+ch){
                attron(A_DIM);
                mvhline(sy,4,ACS_HLINE,COLS-10);
                attroff(A_DIM);
            }
            vy++;
        }
        g_alert_total_h=vy;
        if(g_alert_total_h>ch) scrollbar(cs,ch,g_alert_scroll,g_alert_total_h);
    }

    draw_toast_msg();
    char ri[32];
    if(g_alert_total_h>0) snprintf(ri,sizeof(ri),"%d/%d",g_alert_scroll+1,g_alert_total_h);
    else ri[0]=0;
    draw_status(" j/k:scroll"U_MDOT"r:refresh"U_MDOT"q:back"U_MDOT"t:theme",ri);
}

/* ── Screen: Stop Search ─────────────────────────────────────────── */

static int pdcol(const Passage *p);

static void draw_stops(void)
{
    int top=3;
    int preview_idx=g_nstop_filtered>0?g_stop_filtered[g_cursor]:-1;
    int cached=(preview_idx>=0&&preview_idx==g_sel_stop_group)?g_npassages:0;

    draw_header("Stations // Search",g_stop_search[0]?"filter active":"station browser");
    draw_tabs();

    if(COLS>=94&&LINES>=24){
        int gap=1;
        int cw=(COLS-5-gap*3)/4;
        char buf[32];
        snprintf(buf,sizeof(buf),"%d",g_nstop_groups);
        stat_card(top,2,cw,"GROUPS",buf,"arrets agreges",CP_ACCENT);
        snprintf(buf,sizeof(buf),"%d",g_nstop_filtered);
        stat_card(top,2+cw+gap,cw,"VISIBLE",buf,"hits courants",CP_CYAN_T);
        snprintf(buf,sizeof(buf),"%d",preview_idx>=0?g_stop_groups[preview_idx].ngids:0);
        stat_card(top,2+(cw+gap)*2,cw,"PLATFORMS",buf,"quais selectionnes",CP_GREEN);
        snprintf(buf,sizeof(buf),"%d",cached);
        stat_card(top,2+(cw+gap)*3,cw,"CACHE",buf,cached?"passages memorises":"ouvrez un board",cached?CP_GREEN:CP_YELLOW);
        top+=5;
    }

    if(COLS>=108&&LINES-top>=16){
        int y1=top, y2=LINES-3;
        int left_w=COLS*55/100;
        if(left_w<48) left_w=48;
        if(left_w>COLS-34) left_w=COLS-34;
        int lx1=1, lx2=lx1+left_w-1, rx1=lx2+1, rx2=COLS-2;
        int head_y=y1+1, list_y=head_y+2;
        int mr=y2-list_y, name_w=lx2-lx1-12;

        if(name_w<18) name_w=18;
        if(mr<1) mr=1;
        panel_box(y1,lx1,y2,lx2,"Station Index",g_stop_search[0]?g_stop_search:"all stops");
        panel_box(y1,rx1,y2,rx2,"Platform Preview",preview_idx>=0?g_stop_groups[preview_idx].libelle:"idle");

        attron(A_DIM);
        mvprintw(head_y,lx1+3,"STOP");
        mvprintw(head_y,lx2-8,"QUAIS");
        attroff(A_DIM);

        if(g_cursor<g_scroll)g_scroll=g_cursor;
        if(g_cursor>=g_scroll+mr)g_scroll=g_cursor-mr+1;

        for(int i=0;i<mr&&g_scroll+i<g_nstop_filtered;i++){
            int idx=g_stop_filtered[g_scroll+i];
            StopGroup *sg=&g_stop_groups[idx];
            int row=list_y+i;
            int selected=(g_scroll+i==g_cursor);
            if(selected){
                fill_span(row,lx1+1,lx2-1,CP_SEL,0);
                attron(COLOR_PAIR(CP_CURSOR)|A_BOLD);
                mvaddstr(row,lx1+1,U_CBAR);
                attroff(COLOR_PAIR(CP_CURSOR)|A_BOLD);
                attron(COLOR_PAIR(CP_SEL));
            } else if(i%2) attron(A_DIM);
            print_hl(row,lx1+4,sg->libelle,g_stop_search,name_w);
            mvprintw(row,lx2-8,"%3d",sg->ngids);
            if(selected) attroff(COLOR_PAIR(CP_SEL));
            else if(i%2) attroff(A_DIM);
        }
        scrollbar(list_y,mr,g_scroll,g_nstop_filtered);

        if(preview_idx>=0){
            StopGroup *sg=&g_stop_groups[preview_idx];
            int y=y1+2, w=rx2-rx1-3;
            char buf[64];
            attron(A_BOLD);
            print_fit(y,rx1+2,w,sg->libelle);
            attroff(A_BOLD);
            attron(A_DIM);
            mvprintw(y+1,rx1+2,"%d platforms in this cluster",sg->ngids);
            attroff(A_DIM);

            snprintf(buf,sizeof(buf),"%d",sg->ngids);
            stat_card(y+3,rx1+2,(w-1)/2,"QUAIS",buf,"physical stop ids",CP_GREEN);
            snprintf(buf,sizeof(buf),"%d",cached);
            stat_card(y+3,rx1+3+(w-1)/2,w-(w-1)/2,"CACHE",buf,cached?"board already loaded":"press Enter to load",cached?CP_GREEN:CP_YELLOW);

            y+=9;
            attron(COLOR_PAIR(CP_SECTION)|A_BOLD);
            mvprintw(y++,rx1+2,"Stop IDs");
            attroff(COLOR_PAIR(CP_SECTION)|A_BOLD);
            for(int i=0;i<sg->ngids&&y<y2-2;i++){
                attron(COLOR_PAIR(CP_ACCENT)|A_BOLD);
                mvprintw(y,rx1+2,U_BULLET);
                attroff(COLOR_PAIR(CP_ACCENT)|A_BOLD);
                mvprintw(y,rx1+5,"%d",sg->gids[i]);
                y++;
            }

            if(cached&&y<y2-3){
                y++;
                attron(COLOR_PAIR(CP_SECTION)|A_BOLD);
                mvprintw(y++,rx1+2,"Cached Departures");
                attroff(COLOR_PAIR(CP_SECTION)|A_BOLD);
                for(int i=0;i<g_npassages&&i<4&&y<y2-1;i++){
                    const char *hr=g_passages[i].hor_estime[0]?g_passages[i].hor_estime:g_passages[i].hor_theo;
                    const char *ln=fln(g_passages[i].ligne_id);
                    attron(COLOR_PAIR(pdcol(&g_passages[i]))|A_BOLD);
                    mvprintw(y,rx1+2,"%5s",hr);
                    attroff(COLOR_PAIR(pdcol(&g_passages[i]))|A_BOLD);
                    mvprintw(y,rx1+10,"%s",ln?ln:"?");
                    y++;
                }
            } else if(y<y2-3){
                attron(A_DIM);
                mvprintw(y+1,rx1+2,"Enter ouvre le board temps reel.");
                mvprintw(y+2,rx1+2,"/: change le filtre, q: retour.");
                attroff(A_DIM);
            }
        }
    } else {
        int y1=top, y2=LINES-3, head_y=y1+1, list_y=head_y+2;
        int mr=y2-list_y, cw=COLS-16;
        if(cw<20) cw=20;
        if(mr<1) mr=1;
        panel_box(y1,1,y2,COLS-2,"Station Index",g_stop_search[0]?g_stop_search:"all stops");
        attron(A_DIM);
        mvprintw(head_y,3,"STOP");
        mvprintw(head_y,COLS-8,"Q");
        attroff(A_DIM);

        if(g_cursor<g_scroll)g_scroll=g_cursor;
        if(g_cursor>=g_scroll+mr)g_scroll=g_cursor-mr+1;

        for(int i=0;i<mr&&g_scroll+i<g_nstop_filtered;i++){
            int idx=g_stop_filtered[g_scroll+i];
            StopGroup *sg=&g_stop_groups[idx];
            int row=list_y+i;
            int selected=(g_scroll+i==g_cursor);
            if(selected){
                fill_span(row,2,COLS-3,CP_SEL,0);
                attron(COLOR_PAIR(CP_CURSOR)|A_BOLD);
                mvaddstr(row,2,U_CBAR);
                attroff(COLOR_PAIR(CP_CURSOR)|A_BOLD);
                attron(COLOR_PAIR(CP_SEL));
            } else if(i%2) attron(A_DIM);
            print_hl(row,4,sg->libelle,g_stop_search,cw);
            mvprintw(row,COLS-8,"%3d",sg->ngids);
            if(selected) attroff(COLOR_PAIR(CP_SEL));
            else if(i%2) attroff(A_DIM);
        }
        scrollbar(list_y,mr,g_scroll,g_nstop_filtered);
    }

    if(g_stop_search[0]){
        attron(COLOR_PAIR(CP_SEARCH)|A_BOLD);
        mvhline(LINES-2,0,' ',COLS);
        mvprintw(LINES-2,1," / %s   %d hits",g_stop_search,g_nstop_filtered);
        attroff(COLOR_PAIR(CP_SEARCH)|A_BOLD);
    }
    draw_toast_msg();
    char r[32]; snprintf(r,sizeof(r),"%d/%d",g_nstop_filtered>0?g_cursor+1:0,g_nstop_filtered);
    draw_status(" j/k"U_MDOT"Enter"U_MDOT"/"U_MDOT"q:back"U_MDOT"t:theme",r);
}

/* ── Screen: Passages ────────────────────────────────────────────── */

static int hm2m(const char *h){return nvt_hhmm_to_minutes(h);}
static int nowm(void){time_t t=time(NULL);struct tm *m=localtime(&t);return m->tm_hour*60+m->tm_min;}
static int pskey(const char *h){int m=hm2m(h),b=nowm()-60;return((m-b)+1440)%1440;}
static int cmpp(const void *a,const void *b){const Passage *pa=a,*pb=b;
    const char *ha=pa->hor_estime[0]?pa->hor_estime:pa->hor_theo,*hb=pb->hor_estime[0]?pb->hor_estime:pb->hor_theo;
    return pskey(ha)-pskey(hb);}

static int pdcol(const Passage *p){
    if(!p->hor_estime[0]||!p->hor_theo[0]) return CP_GREEN;
    if(strcmp(p->hor_estime,p->hor_theo)<=0) return CP_GREEN;
    int em=(p->hor_estime[3]-'0')*10+(p->hor_estime[4]-'0'),tm2=(p->hor_theo[3]-'0')*10+(p->hor_theo[4]-'0');
    int eh=(p->hor_estime[0]-'0')*10+(p->hor_estime[1]-'0'),th=(p->hor_theo[0]-'0')*10+(p->hor_theo[1]-'0');
    return((eh*60+em)-(th*60+tm2))>=5?CP_RED:CP_YELLOW;
}

static void draw_passages(void)
{
    StopGroup *sg=&g_stop_groups[g_sel_stop_group];
    char t[128],bc[128], buf[32];
    int top=3;
    int nm=nowm();
    int live=count_live_passages();
    int delayed=count_delayed_passages();
    int uniq=count_unique_passage_lines();
    int next_min=-1;
    snprintf(t,sizeof(t),"Departures // %s",sg->libelle);
    snprintf(bc,sizeof(bc),"Arrets "U_ARROW" %s",sg->libelle);
    draw_header(t,bc); draw_tabs();

    if(g_npassages>0){
        const char *hr=g_passages[0].hor_estime[0]?g_passages[0].hor_estime:g_passages[0].hor_theo;
        next_min=((hm2m(hr)-nm)+1440)%1440;
    }

    if(COLS>=94&&LINES>=24){
        int gap=1;
        int cw=(COLS-5-gap*3)/4;
        if(next_min<0) snprintf(buf,sizeof(buf),"--");
        else if(next_min<=1) snprintf(buf,sizeof(buf),"NOW");
        else snprintf(buf,sizeof(buf),"%dmin",next_min);
        stat_card(top,2,cw,"NEXT",buf,"prochain passage",next_min>=0?CP_GREEN:CP_YELLOW);
        snprintf(buf,sizeof(buf),"%d",live);
        stat_card(top,2+cw+gap,cw,"LIVE",buf,"estimations live",live?CP_GREEN:CP_YELLOW);
        snprintf(buf,sizeof(buf),"%d",uniq);
        stat_card(top,2+(cw+gap)*2,cw,"LINES",buf,"lignes a venir",CP_ACCENT);
        snprintf(buf,sizeof(buf),"%d",delayed);
        stat_card(top,2+(cw+gap)*3,cw,"DELAY",buf,"depassements",delayed?CP_ALERT_MED:CP_GREEN);
        top+=5;
    }

    if(COLS>=112&&LINES-top>=16){
        int y1=top, y2=LINES-3;
        int left_w=COLS*64/100;
        if(left_w<58) left_w=58;
        if(left_w>COLS-30) left_w=COLS-30;
        int lx1=1, lx2=lx1+left_w-1, rx1=lx2+1, rx2=COLS-2;
        int head_y=y1+1, row_y=head_y+2, max_rows=y2-row_y;

        if(max_rows<1) max_rows=1;
        panel_box(y1,lx1,y2,lx2,"Departure Board",sg->libelle);
        panel_box(y1,rx1,y2,rx2,"Next Wave",g_npassages?"live board":"idle");

        attron(A_DIM);
        mvprintw(head_y,lx1+3,"TIME");
        mvprintw(head_y,lx1+12,"ETA");
        mvprintw(head_y,lx1+21,"LINE");
        mvprintw(head_y,lx1+41,"TERMINUS");
        attroff(A_DIM);

        for(int i=0;i<g_npassages&&i<max_rows;i++){
            Passage *p=&g_passages[i];
            const char *ln=fln(p->ligne_id), *tn=stopmap_lookup(&g_stops,p->terminus_gid);
            const char *hr=p->hor_estime[0]?p->hor_estime:p->hor_theo;
            int dm=((hm2m(hr)-nm)+1440)%1440, cp=pdcol(p), lv=get_lcp(fli(p->ligne_id));
            int row=row_y+i;

            if(i%2) attron(A_DIM);
            mvhline(row,lx1+1,' ',lx2-lx1-1);
            if(i%2) attroff(A_DIM);
            if(i%2) attron(A_DIM);

            attron(COLOR_PAIR(cp)|A_BOLD);
            mvprintw(row,lx1+3,"%5s",hr);
            attroff(COLOR_PAIR(cp)|A_BOLD);
            if(dm<=1){attron(COLOR_PAIR(CP_GREEN)|A_BOLD);mvprintw(row,lx1+12,"NOW   ");attroff(COLOR_PAIR(CP_GREEN)|A_BOLD);}
            else if(dm<=10){attron(COLOR_PAIR(cp)|A_BOLD);mvprintw(row,lx1+12,"%3dmin",dm);attroff(COLOR_PAIR(cp)|A_BOLD);}
            else mvprintw(row,lx1+12,"%3dmin",dm);
            if(lv){
                attron(COLOR_PAIR(lv)|A_BOLD);
                mvprintw(row,lx1+20," %-16.16s ",ln?ln:"?");
                attroff(COLOR_PAIR(lv)|A_BOLD);
            } else mvprintw(row,lx1+21,"%-18.18s",ln?ln:"?");
            print_fit(row,lx1+41,lx2-lx1-43,tn);
            if(i%2) attroff(A_DIM);
        }

        {
            int y=y1+2, inner=rx2-rx1-3;
            attron(A_BOLD);
            print_fit(y,rx1+2,inner,sg->libelle);
            attroff(A_BOLD);
            attron(A_DIM);
            mvprintw(y+1,rx1+2,"%d departures loaded",g_npassages);
            attroff(A_DIM);
            y+=3;
            for(int i=0;i<g_npassages&&i<6&&y<y2-1;i++){
                Passage *p=&g_passages[i];
                const char *ln=fln(p->ligne_id);
                const char *hr=p->hor_estime[0]?p->hor_estime:p->hor_theo;
                int dm=((hm2m(hr)-nm)+1440)%1440, cp=pdcol(p), lv=get_lcp(fli(p->ligne_id));
                attron(COLOR_PAIR(cp)|A_BOLD);
                mvprintw(y,rx1+2,"%5s",hr);
                attroff(COLOR_PAIR(cp)|A_BOLD);
                if(dm<=1){attron(COLOR_PAIR(CP_GREEN)|A_BOLD);mvprintw(y,rx1+10,"NOW ");attroff(COLOR_PAIR(CP_GREEN)|A_BOLD);}
                else mvprintw(y,rx1+10,"%3d",dm);
                if(lv){
                    attron(COLOR_PAIR(lv)|A_BOLD);
                    mvprintw(y,rx1+15," %-10.10s ",ln?ln:"?");
                    attroff(COLOR_PAIR(lv)|A_BOLD);
                } else mvprintw(y,rx1+16,"%-12.12s",ln?ln:"?");
                y++;
            }
            if(!g_npassages&&y<y2-2){
                attron(A_DIM);
                mvprintw(y,rx1+2,U_INFO" aucun passage prevu");
                mvprintw(y+2,rx1+2,"r recharge le board");
                mvprintw(y+3,rx1+2,"/ retourne a la recherche");
                attroff(A_DIM);
            }
        }
    } else {
        int y1=top, y2=LINES-3, head_y=y1+1, row_y=head_y+2;
        panel_box(y1,1,y2,COLS-2,"Departure Board",sg->libelle);
        attron(A_DIM);
        mvprintw(head_y,3,"TIME");
        mvprintw(head_y,12,"ETA");
        mvprintw(head_y,21,"LINE");
        mvprintw(head_y,39,"TERMINUS");
        attroff(A_DIM);
        for(int i=0;i<g_npassages&&row_y+i<y2;i++){
            Passage *p=&g_passages[i];
            const char *ln=fln(p->ligne_id), *tn=stopmap_lookup(&g_stops,p->terminus_gid);
            const char *hr=p->hor_estime[0]?p->hor_estime:p->hor_theo;
            int dm=((hm2m(hr)-nm)+1440)%1440, cp=pdcol(p), lv=get_lcp(fli(p->ligne_id));
            int row=row_y+i;
            if(i%2) attron(A_DIM);
            mvhline(row,2,' ',COLS-4);
            if(i%2) attroff(A_DIM);
            if(i%2) attron(A_DIM);
            attron(COLOR_PAIR(cp)|A_BOLD);
            mvprintw(row,3,"%5s",hr);
            attroff(COLOR_PAIR(cp)|A_BOLD);
            if(dm<=1){attron(COLOR_PAIR(CP_GREEN)|A_BOLD);mvprintw(row,12,"NOW ");attroff(COLOR_PAIR(CP_GREEN)|A_BOLD);}
            else mvprintw(row,12,"%3d",dm);
            if(lv){
                attron(COLOR_PAIR(lv)|A_BOLD);
                mvprintw(row,20," %-14.14s ",ln?ln:"?");
                attroff(COLOR_PAIR(lv)|A_BOLD);
            } else mvprintw(row,21,"%-16.16s",ln?ln:"?");
            print_fit(row,39,COLS-42,tn);
            if(i%2) attroff(A_DIM);
        }
        if(!g_npassages){
            attron(A_DIM);
            mvprintw(y1+4,3,U_INFO" aucun passage prevu");
            attroff(A_DIM);
        }
    }

    draw_toast_msg();
    char r[32]; snprintf(r,sizeof(r),"%d pass.",g_npassages);
    draw_status(" q:back"U_MDOT"r:refresh"U_MDOT"/:arret"U_MDOT"t:theme",r);
}

/* ── Screen: Itinerary ───────────────────────────────────────────── */

static void draw_itinerary_line_badge(int y, int x)
{
    if (g_network == NET_TLS) {
        ToulouseLine *line = selected_toulouse_line();
        if (line) draw_toulouse_badge(y, x, line);
    } else if (g_network == NET_IDFM || g_network == NET_SNCF) {
        ToulouseLine *line = selected_idfm_line();
        if (line) draw_idfm_badge(y, x, line);
    } else if (g_sel_line >= 0 && g_sel_line < g_nlines) {
        draw_line_badge(y, x, &g_lines[g_sel_line]);
    }
}

static const char *itinerary_direction_target(void)
{
    int dir = nvt_itinerary_direction(&g_itinerary);

    if (dir > 0) return g_itinerary.direction_forward[0] ? g_itinerary.direction_forward : "--";
    if (dir < 0) return g_itinerary.direction_backward[0] ? g_itinerary.direction_backward : "--";
    return "--";
}

static void draw_itinerary(void)
{
    const NvtItineraryStop *origin = (g_itinerary.origin >= 0 && g_itinerary.origin < g_itinerary.nstops)
                                   ? &g_itinerary.stops[g_itinerary.origin] : NULL;
    const NvtItineraryStop *destination = (g_itinerary.destination >= 0 && g_itinerary.destination < g_itinerary.nstops)
                                        ? &g_itinerary.stops[g_itinerary.destination] : NULL;
    int hops = nvt_itinerary_hops(&g_itinerary);
    int top = 3;
    char title[128];
    char crumb[128];
    char buf[32];

    snprintf(title, sizeof(title), "Itinerary // %s", network_name());
    if (g_itinerary.line_name[0]) {
        snprintf(crumb, sizeof(crumb), "%s%s%s",
                 g_itinerary.line_code[0] ? g_itinerary.line_code : "",
                 g_itinerary.line_code[0] && g_itinerary.line_name[0] ? " " U_ARROW " " : "",
                 g_itinerary.line_name);
    } else {
        snprintf(crumb, sizeof(crumb), "selectionnez une ligne");
    }

    draw_header(title, crumb);
    draw_tabs();

    if (COLS >= 94 && LINES >= 24) {
        int gap = 1;
        int cw = (COLS - 5 - gap * 3) / 4;

        snprintf(buf, sizeof(buf), "%d", g_itinerary.nstops);
        stat_card(top, 2, cw, "STOPS", buf, "sequence chargee", g_itinerary.ready ? CP_ACCENT : CP_YELLOW);
        snprintf(buf, sizeof(buf), "%s", origin ? "SET" : "--");
        stat_card(top, 2 + cw + gap, cw, "FROM", buf, origin ? origin->name : "choisissez avec o", origin ? CP_GREEN : CP_YELLOW);
        snprintf(buf, sizeof(buf), "%s", destination ? "SET" : "--");
        stat_card(top, 2 + (cw + gap) * 2, cw, "TO", buf, destination ? destination->name : "choisissez avec d", destination ? CP_RED : CP_YELLOW);
        if (!origin || !destination) snprintf(buf, sizeof(buf), "--");
        else if (hops == 0) snprintf(buf, sizeof(buf), "same");
        else snprintf(buf, sizeof(buf), "%d", hops);
        stat_card(top, 2 + (cw + gap) * 3, cw, "HOPS", buf, "nombre d'arrets", hops ? CP_CYAN_T : CP_GREEN);
        top += 5;
    }

    if (!g_itinerary.ready) {
        panel_box(top, 1, LINES - 3, COLS - 2, "Route Calculator", "idle");
        attron(A_DIM);
        mvprintw(top + 3, 4, U_INFO" aucune ligne chargee pour l'itineraire");
        mvprintw(top + 5, 4, "Ouvrez d'abord une ligne puis appuyez sur i ou 6.");
        attroff(A_DIM);
        draw_toast_msg();
        draw_status(" 6/i:ouvrir"U_MDOT"1:lignes"U_MDOT"n:reseaux"U_MDOT"t:theme", "n/a");
        return;
    }

    if (COLS >= 108 && LINES - top >= 16) {
        int y1 = top;
        int y2 = LINES - 3;
        int left_w = COLS * 52 / 100;
        int lx1 = 1;
        int lx2;
        int rx1;
        int rx2 = COLS - 2;
        int head_y = y1 + 1;
        int list_y = head_y + 2;
        int mr;
        int name_w;

        if (left_w < 48) left_w = 48;
        if (left_w > COLS - 34) left_w = COLS - 34;
        lx2 = lx1 + left_w - 1;
        rx1 = lx2 + 1;
        mr = y2 - list_y;
        name_w = lx2 - lx1 - 13;
        if (mr < 1) mr = 1;
        if (name_w < 18) name_w = 18;

        panel_box(y1, lx1, y2, lx2, "Stop Sequence", g_itinerary.search[0] ? g_itinerary.search : "line order");
        panel_box(y1, rx1, y2, rx2, "Trip Preview", g_itinerary.line_name);

        attron(A_DIM);
        mvprintw(head_y, lx1 + 3, "TAG");
        mvprintw(head_y, lx1 + 9, "STOP");
        attroff(A_DIM);

        if (g_itinerary.cursor < g_itinerary.scroll) g_itinerary.scroll = g_itinerary.cursor;
        if (g_itinerary.cursor >= g_itinerary.scroll + mr) g_itinerary.scroll = g_itinerary.cursor - mr + 1;

        for (int i = 0; i < mr && g_itinerary.scroll + i < g_itinerary.nfiltered; i++) {
            int idx = g_itinerary.filtered[g_itinerary.scroll + i];
            int row = list_y + i;
            int selected = (g_itinerary.scroll + i == g_itinerary.cursor);
            const char *tag = "   ";

            if (selected) {
                fill_span(row, lx1 + 1, lx2 - 1, CP_SEL, 0);
                attron(COLOR_PAIR(CP_CURSOR) | A_BOLD);
                mvaddstr(row, lx1 + 1, U_CBAR);
                attroff(COLOR_PAIR(CP_CURSOR) | A_BOLD);
            }
            if (idx == g_itinerary.origin && idx == g_itinerary.destination) tag = "OD ";
            else if (idx == g_itinerary.origin) tag = "DEP";
            else if (idx == g_itinerary.destination) tag = "ARR";

            if (idx == g_itinerary.origin) attron(COLOR_PAIR(CP_GREEN) | A_BOLD);
            else if (idx == g_itinerary.destination) attron(COLOR_PAIR(CP_RED) | A_BOLD);
            mvprintw(row, lx1 + 3, "%-3s", tag);
            if (idx == g_itinerary.origin) attroff(COLOR_PAIR(CP_GREEN) | A_BOLD);
            else if (idx == g_itinerary.destination) attroff(COLOR_PAIR(CP_RED) | A_BOLD);
            print_hl(row, lx1 + 9, g_itinerary.stops[idx].name, g_itinerary.search, name_w);
            if (g_itinerary.stops[idx].meta[0]) {
                attron(A_DIM);
                print_fit(row, lx2 - 18, 16, g_itinerary.stops[idx].meta);
                attroff(A_DIM);
            }
        }
        scrollbar(list_y, mr, g_itinerary.scroll, g_itinerary.nfiltered);

        {
            int y = y1 + 2;
            int px = rx1 + 2;
            int pw = rx2 - rx1 - 3;

            draw_itinerary_line_badge(y, px);
            attron(A_BOLD);
            print_fit(y, px + 8, pw - 9, g_itinerary.line_name[0] ? g_itinerary.line_name : g_itinerary.line_code);
            attroff(A_BOLD);
            attron(A_DIM);
            mvprintw(y + 1, px, "Ligne %s", g_itinerary.line_code[0] ? g_itinerary.line_code : "--");
            attroff(A_DIM);

            kv_line(y + 3, px, 12, "depart", origin ? origin->name : "--", origin ? CP_GREEN : CP_YELLOW);
            kv_line(y + 4, px, 12, "arrivee", destination ? destination->name : "--", destination ? CP_RED : CP_YELLOW);
            kv_line(y + 5, px, 12, "direction", itinerary_direction_target(), CP_ACCENT);
            if (!origin || !destination) snprintf(buf, sizeof(buf), "--");
            else snprintf(buf, sizeof(buf), "%d", hops);
            kv_line(y + 6, px, 12, "arrets", buf, CP_ACCENT);

            y += 8;
            attron(COLOR_PAIR(CP_SECTION) | A_BOLD);
            mvprintw(y++, px, "Segment");
            attroff(COLOR_PAIR(CP_SECTION) | A_BOLD);

            if (!origin || !destination) {
                attron(A_DIM);
                mvprintw(y++, px, "o definit le depart, d definit l'arrivee.");
                mvprintw(y++, px, "x inverse les deux bornes.");
                attroff(A_DIM);
            } else {
                int step = g_itinerary.destination >= g_itinerary.origin ? 1 : -1;
                int idx = g_itinerary.origin;

                while (y < y2 - 1) {
                    const char *tag = idx == g_itinerary.origin ? "DEP" : (idx == g_itinerary.destination ? "ARR" : U_ARROW);

                    if (idx == g_itinerary.origin) attron(COLOR_PAIR(CP_GREEN) | A_BOLD);
                    else if (idx == g_itinerary.destination) attron(COLOR_PAIR(CP_RED) | A_BOLD);
                    else attron(COLOR_PAIR(CP_ACCENT) | A_BOLD);
                    mvprintw(y, px, "%-3s", tag);
                    if (idx == g_itinerary.origin) attroff(COLOR_PAIR(CP_GREEN) | A_BOLD);
                    else if (idx == g_itinerary.destination) attroff(COLOR_PAIR(CP_RED) | A_BOLD);
                    else attroff(COLOR_PAIR(CP_ACCENT) | A_BOLD);
                    print_fit(y, px + 5, pw - 6, g_itinerary.stops[idx].name);
                    y++;
                    if (idx == g_itinerary.destination) break;
                    idx += step;
                }
            }
        }
    } else {
        int y1 = top;
        int y2 = LINES - 3;
        int head_y = y1 + 1;
        int list_y = head_y + 2;
        int mr = y2 - list_y;
        int cw = COLS - 14;

        if (mr < 1) mr = 1;
        if (cw < 18) cw = 18;
        panel_box(y1, 1, y2, COLS - 2, "Stop Sequence", g_itinerary.search[0] ? g_itinerary.search : g_itinerary.line_name);
        attron(A_DIM);
        mvprintw(head_y, 3, "TAG");
        mvprintw(head_y, 9, "STOP");
        attroff(A_DIM);

        if (g_itinerary.cursor < g_itinerary.scroll) g_itinerary.scroll = g_itinerary.cursor;
        if (g_itinerary.cursor >= g_itinerary.scroll + mr) g_itinerary.scroll = g_itinerary.cursor - mr + 1;

        for (int i = 0; i < mr && g_itinerary.scroll + i < g_itinerary.nfiltered; i++) {
            int idx = g_itinerary.filtered[g_itinerary.scroll + i];
            int row = list_y + i;

            if (idx == g_itinerary.origin) {
                attron(COLOR_PAIR(CP_GREEN) | A_BOLD);
                mvprintw(row, 3, "DEP");
                attroff(COLOR_PAIR(CP_GREEN) | A_BOLD);
            } else if (idx == g_itinerary.destination) {
                attron(COLOR_PAIR(CP_RED) | A_BOLD);
                mvprintw(row, 3, "ARR");
                attroff(COLOR_PAIR(CP_RED) | A_BOLD);
            }
            print_hl(row, 9, g_itinerary.stops[idx].name, g_itinerary.search, cw);
        }
        scrollbar(list_y, mr, g_itinerary.scroll, g_itinerary.nfiltered);
    }

    if (g_itinerary.search[0]) {
        attron(COLOR_PAIR(CP_SEARCH) | A_BOLD);
        mvhline(LINES - 2, 0, ' ', COLS);
        mvprintw(LINES - 2, 1, " / %s   %d hits", g_itinerary.search, g_itinerary.nfiltered);
        attroff(COLOR_PAIR(CP_SEARCH) | A_BOLD);
    }
    draw_toast_msg();
    snprintf(buf, sizeof(buf), "%d/%d", g_itinerary.nfiltered > 0 ? g_itinerary.cursor + 1 : 0, g_itinerary.nfiltered);
    draw_status(" j/k"U_MDOT"o:depart"U_MDOT"d:arrivee"U_MDOT"x:swap"U_MDOT"/:filtre"U_MDOT"q:back"U_MDOT"t:theme", buf);
}

/* ── Screen: Toulouse ────────────────────────────────────────────── */

static int toulouse_alert_cp(const ToulouseAlert *alert);

static void draw_toulouse(void)
{
    ToulouseLine *sel = g_ntls_filtered > 0 ? &g_tls_lines[g_tls_filtered[g_tls_cursor]] : NULL;
    int total = count_toulouse_active_lines_all();
    int rails = count_toulouse_lines_rail();
    int buses = count_toulouse_lines_busish();
    int alert_lines = count_toulouse_impacted_lines();
    int top = 3;

    draw_header("NVT // Toulouse", g_tls_search[0] ? "reseau filtre" : "reseau live");
    draw_tabs();

    if(COLS>=94&&LINES>=24){
        int gap=1;
        int cw=(COLS-5-gap*3)/4;
        char buf[32];
        snprintf(buf,sizeof(buf),"%d",total);
        stat_card(top,2,cw,"ACTIVE",buf,"lignes actives",CP_ACCENT);
        snprintf(buf,sizeof(buf),"%d",rails);
	stat_card(top,2+cw+gap,cw,"METRO+TRAM",buf,"metros + trams",CP_CYAN_T);
        snprintf(buf,sizeof(buf),"%d",buses);
        stat_card(top,2+(cw+gap)*2,cw,"BUS",buf,"reseau bus",CP_GREEN);
        snprintf(buf,sizeof(buf),"%d",alert_lines);
        stat_card(top,2+(cw+gap)*3,cw,"ALERTS",buf,"lignes impactees",alert_lines?CP_ALERT_MED:CP_GREEN);
        top+=5;
    }

    if(COLS>=110&&LINES-top>=16){
        int y1=top, y2=LINES-3;
        int left_w=COLS*58/100;
        if(left_w<56) left_w=56;
        if(left_w>COLS-34) left_w=COLS-34;
        int lx1=1, lx2=lx1+left_w-1, rx1=lx2+1, rx2=COLS-2;
        int head_y=y1+1, list_y=head_y+2;
        int name_w=lx2-lx1-28;
        int mr=y2-list_y;
        if(name_w<18) name_w=18;
        if(mr<1) mr=1;

        panel_box(y1,lx1,y2,lx2,"Network Index",g_tls_search[0]?g_tls_search:"all active");
        panel_box(y1,rx1,y2,rx2,"Line Focus",sel?toulouse_mode_label(sel):"idle");

        if(g_tls_cursor<g_tls_scroll) g_tls_scroll=g_tls_cursor;
        if(g_tls_cursor>=g_tls_scroll+mr) g_tls_scroll=g_tls_cursor-mr+1;

        attron(A_DIM);
        mvprintw(head_y,lx1+3,"LINE");
        mvprintw(head_y,lx1+10,"NAME");
        mvprintw(head_y,lx2-16,"TYPE");
        mvprintw(head_y,lx2-9,"SAE");
        mvprintw(head_y,lx2-5,"ALT");
        attroff(A_DIM);

        for(int i=0;i<mr&&g_tls_scroll+i<g_ntls_filtered;i++){
            int idx=g_tls_filtered[g_tls_scroll+i];
            ToulouseLine *line=&g_tls_lines[idx];
            int row=list_y+i;
            int alerts=count_toulouse_line_alerts(line);
            int selected=(g_tls_scroll+i==g_tls_cursor);

            if(selected){
                fill_span(row,lx1+1,lx2-1,CP_SEL,0);
                attron(COLOR_PAIR(CP_CURSOR)|A_BOLD);
                mvaddstr(row,lx1+1,U_CBAR);
                attroff(COLOR_PAIR(CP_CURSOR)|A_BOLD);
                attron(COLOR_PAIR(CP_SEL));
            } else if(i%2){
                attron(A_DIM);
                mvhline(row,lx1+1,' ',lx2-lx1-1);
                attroff(A_DIM);
                attron(A_DIM);
            }

            draw_toulouse_badge(row,lx1+3,line);
            if(selected) attron(COLOR_PAIR(CP_SEL));
            print_hl(row,lx1+10,line->libelle,g_tls_search,name_w);
            mvprintw(row,lx2-16,"%-5s",toulouse_mode_label(line));
            mvprintw(row,lx2-9," %s ",toulouse_line_realtime_capable(line)?U_CHECK:U_MDOT);
            if(alerts>0){
                attroff(selected?COLOR_PAIR(CP_SEL):0);
                attron(COLOR_PAIR(CP_ALERT_MED)|A_BOLD);
                mvprintw(row,lx2-5,"%2d",alerts);
                attroff(COLOR_PAIR(CP_ALERT_MED)|A_BOLD);
                if(selected) attron(COLOR_PAIR(CP_SEL));
                else if(i%2) attron(A_DIM);
            } else mvprintw(row,lx2-5," -");

            if(selected) attroff(COLOR_PAIR(CP_SEL));
            else if(i%2) attroff(A_DIM);
        }
        scrollbar(list_y,mr,g_tls_scroll,g_ntls_filtered);

        if(sel){
            int alerts=count_toulouse_line_alerts(sel);
            int px=rx1+2, pw=rx2-rx1-3, yy=y1+2;
            char buf[128];
            draw_toulouse_badge(yy,px,sel);
            attron(A_BOLD);
            print_fit(yy,px+7,pw-8,sel->libelle);
            attroff(A_BOLD);
            attron(A_DIM);
            mvprintw(yy+1,px,"%s corridor%s",toulouse_mode_label(sel),alerts?" with incidents":"");
            attroff(A_DIM);

            if(pw>=28){
                int sw=(pw-1)/2;
                snprintf(buf,sizeof(buf),"%s",toulouse_line_realtime_capable(sel)?U_CHECK" live":"-");
                stat_card(yy+3,px,sw,"SAE",buf,toulouse_line_realtime_capable(sel)?"temps reel via 4.7":"telemetrie limitee",toulouse_line_realtime_capable(sel)?CP_GREEN:CP_YELLOW);
                snprintf(buf,sizeof(buf),"%d",alerts);
                stat_card(yy+3,px+sw+1,pw-sw,"ALERTS",buf,alerts?"messages actifs":"ligne stable",alerts?CP_ALERT_MED:CP_GREEN);
            }

            yy+=9;
            attron(COLOR_PAIR(CP_SECTION)|A_BOLD);
            mvprintw(yy,px,"Telemetry");
            attroff(COLOR_PAIR(CP_SECTION)|A_BOLD);
            yy++;
            snprintf(buf,sizeof(buf),"%d",sel->id);
            kv_line(yy++,px,10,"line id",buf,CP_ACCENT);
            kv_line(yy++,px,10,"api ref",sel->ref,CP_ACCENT);
            kv_line(yy++,px,10,"type",toulouse_mode_label(sel),CP_ACCENT);
            snprintf(buf,sizeof(buf),"%d / %d",g_ntls_filtered?g_tls_cursor+1:0,g_ntls_filtered);
            kv_line(yy++,px,10,"focus",buf,CP_ACCENT);

            yy++;
            attron(COLOR_PAIR(CP_SECTION)|A_BOLD);
            mvprintw(yy++,px,"Signal");
            attroff(COLOR_PAIR(CP_SECTION)|A_BOLD);
            if(alerts){
                int shown=0;
                for(int i=0;i<g_ntls_alerts&&yy<y2-3&&shown<2;i++){
                    if(!toulouse_alert_applies_to_line(&g_tls_alerts[i], sel)) continue;
                    attron(COLOR_PAIR(toulouse_alert_cp(&g_tls_alerts[i]))|A_BOLD);
                    mvprintw(yy,px,"%s ",shown==0?U_WARN:U_DIAMOND);
                    attroff(COLOR_PAIR(toulouse_alert_cp(&g_tls_alerts[i]))|A_BOLD);
                    attron(A_BOLD);
                    print_fit(yy,px+2,pw-4,g_tls_alerts[i].titre[0]?g_tls_alerts[i].titre:"Message reseau");
                    attroff(A_BOLD);
                    yy++;
                    yy+=draw_wrapped_block(yy,px+2,pw-4,2,g_tls_alerts[i].message,toulouse_alert_cp(&g_tls_alerts[i]),0);
                    yy++;
                    shown++;
                }
            } else if(count_toulouse_global_alerts()){
                int shown=0;
                for(int i=0;i<g_ntls_alerts&&yy<y2-3&&shown<2;i++){
                    if(g_tls_alerts[i].lines[0]) continue;
                    attron(COLOR_PAIR(toulouse_alert_cp(&g_tls_alerts[i]))|A_BOLD);
                    mvprintw(yy,px,"%s ",U_INFO);
                    attroff(COLOR_PAIR(toulouse_alert_cp(&g_tls_alerts[i]))|A_BOLD);
                    attron(A_BOLD);
                    print_fit(yy,px+2,pw-4,g_tls_alerts[i].titre[0]?g_tls_alerts[i].titre:"Message reseau");
                    attroff(A_BOLD);
                    yy++;
                    yy+=draw_wrapped_block(yy,px+2,pw-4,2,g_tls_alerts[i].message,toulouse_alert_cp(&g_tls_alerts[i]),0);
                    yy++;
                    shown++;
                }
            } else {
                attron(A_DIM);
                mvprintw(yy++,px,U_CHECK" aucune alerte sur cette ligne");
                mvprintw(yy++,px,"Enter ouvre le board vehicules en direct.");
                mvprintw(yy++,px,"p bascule vers la recherche d'arret.");
                attroff(A_DIM);
            }
        } else {
            attron(A_DIM);
            mvprintw(y1+3,rx1+3,U_INFO" aucun resultat");
            mvprintw(y1+5,rx1+3,"Essayez un identifiant, un type ou une ligne.");
            attroff(A_DIM);
        }
    } else {
        int y1=top, y2=LINES-3;
        int head_y=y1+1, list_y=head_y+2;
        int mr=y2-list_y;
        int cw=COLS>80?COLS-28:COLS-22;
        if(cw<18) cw=18;
        if(mr<1) mr=1;

        panel_box(y1,1,y2,COLS-2,"Network Index",g_tls_search[0]?g_tls_search:"all active");
        attron(A_DIM);
        mvprintw(head_y,3,"LINE");
        mvprintw(head_y,10,"NAME");
        mvprintw(head_y,COLS-17,"TYPE");
        mvprintw(head_y,COLS-10,"SAE");
        mvprintw(head_y,COLS-6,"ALT");
        attroff(A_DIM);

        if(g_tls_cursor<g_tls_scroll) g_tls_scroll=g_tls_cursor;
        if(g_tls_cursor>=g_tls_scroll+mr) g_tls_scroll=g_tls_cursor-mr+1;

        for(int i=0;i<mr&&g_tls_scroll+i<g_ntls_filtered;i++){
            int idx=g_tls_filtered[g_tls_scroll+i];
            ToulouseLine *line=&g_tls_lines[idx];
            int row=list_y+i;
            int alerts=count_toulouse_line_alerts(line);
            int selected=(g_tls_scroll+i==g_tls_cursor);

            if(selected){
                fill_span(row,2,COLS-3,CP_SEL,0);
                attron(COLOR_PAIR(CP_CURSOR)|A_BOLD);
                mvaddstr(row,2,U_CBAR);
                attroff(COLOR_PAIR(CP_CURSOR)|A_BOLD);
                attron(COLOR_PAIR(CP_SEL));
            } else if(i%2) attron(A_DIM);

            draw_toulouse_badge(row,4,line);
            if(selected) attron(COLOR_PAIR(CP_SEL));
            print_hl(row,11,line->libelle,g_tls_search,cw);
            mvprintw(row,COLS-17,"%-5s",toulouse_mode_label(line));
            mvprintw(row,COLS-10," %s ",toulouse_line_realtime_capable(line)?U_CHECK:U_MDOT);
            if(alerts>0){
                if(selected) attroff(COLOR_PAIR(CP_SEL));
                else if(i%2) attroff(A_DIM);
                attron(COLOR_PAIR(CP_ALERT_MED)|A_BOLD);
                mvprintw(row,COLS-6,"%2d",alerts);
                attroff(COLOR_PAIR(CP_ALERT_MED)|A_BOLD);
                if(selected) attron(COLOR_PAIR(CP_SEL));
                else if(i%2) attron(A_DIM);
            } else mvprintw(row,COLS-6," -");

            if(selected) attroff(COLOR_PAIR(CP_SEL));
            else if(i%2) attroff(A_DIM);
        }
        scrollbar(list_y,mr,g_tls_scroll,g_ntls_filtered);
    }

    if(g_tls_search[0]){
        attron(COLOR_PAIR(CP_SEARCH)|A_BOLD);
        mvhline(LINES-2,0,' ',COLS);
        mvprintw(LINES-2,1," / %s   %d hits",g_tls_search,g_ntls_filtered);
        attroff(COLOR_PAIR(CP_SEARCH)|A_BOLD);
    }
    draw_toast_msg();

    {
        char r[32];
        snprintf(r,sizeof(r),"%d/%d",g_ntls_filtered>0?g_tls_cursor+1:0,g_ntls_filtered);
        draw_status(" j/k"U_MDOT"Enter"U_MDOT"/"U_MDOT"a:alertes"U_MDOT"p:arrets"U_MDOT"t:theme",r);
    }
}

static int toulouse_alert_cp(const ToulouseAlert *alert)
{
    if (strcasestr_s(alert->importance, "important")) return CP_ALERT_HI;
    if (alert->lines[0]) return CP_ALERT_MED;
    return CP_CYAN_T;
}

static int idfm_alert_cp(const ToulouseAlert *alert)
{
    if (!alert) return CP_CYAN_T;
    if (strcasestr_s(alert->importance, "perturb") || strcasestr_s(alert->importance, "critical")) return CP_ALERT_HI;
    if (strcasestr_s(alert->importance, "significant")) return CP_ALERT_MED;
    if (strcasestr_s(alert->importance, "information")) return CP_CYAN_T;
    return alert->lines[0] ? CP_ALERT_MED : CP_CYAN_T;
}

static void draw_toulouse_alerts(void)
{
    int total=g_ntls_alerts;
    int hi=0, med=0, low=0;
    int impacted=count_toulouse_impacted_lines();
    int top=3;

    for(int i=0;i<g_ntls_alerts;i++){
        int cp=toulouse_alert_cp(&g_tls_alerts[i]);
        if(cp==CP_ALERT_HI) hi++;
        else if(cp==CP_ALERT_MED) med++;
        else low++;
    }

    draw_header("Operations // Alerts",total?"network pressure":"quiet network");
    draw_tabs();

    if(COLS>=94&&LINES>=24){
        int gap=1;
        int cw=(COLS-5-gap*3)/4;
        char buf[32];
        snprintf(buf,sizeof(buf),"%d",total);
        stat_card(top,2,cw,"TOTAL",buf,"messages reseau",total?CP_ALERT_MED:CP_GREEN);
        snprintf(buf,sizeof(buf),"%d",hi);
        stat_card(top,2+cw+gap,cw,"CRIT",buf,"severity 3",hi?CP_ALERT_HI:CP_GREEN);
        snprintf(buf,sizeof(buf),"%d",med);
        stat_card(top,2+(cw+gap)*2,cw,"WARN",buf,"severity 2",med?CP_ALERT_MED:CP_GREEN);
        snprintf(buf,sizeof(buf),"%d",impacted);
        stat_card(top,2+(cw+gap)*3,cw,"LINES",buf,"corridors impacted",impacted?CP_ALERT_MED:CP_GREEN);
        top+=5;
    }

    {
        int y1=top, y2=LINES-3;
        int cs=y1+2, ch=y2-cs;
        int vy=0;
        char meta[64];
        snprintf(meta,sizeof(meta),"%d crit / %d warn / %d info",hi,med,low);
        panel_box(y1,1,y2,COLS-2,"Alert Feed",meta);
        g_alert_total_h=0;

        if(!total){
            attron(A_DIM);
            mvprintw(y1+3,3,U_CHECK" aucune alerte active");
            mvprintw(y1+5,3,"Le reseau est calme pour le moment.");
            attroff(A_DIM);
        }

        for(int i=0;i<g_ntls_alerts;i++){
            ToulouseAlert *a=&g_tls_alerts[i];
            int cp=toulouse_alert_cp(a);
            const char *ic=cp==CP_ALERT_HI?U_DIAMOND:cp==CP_ALERT_MED?U_WARN:U_INFO;
            int mw=COLS-14, po=0, ml=(int)strlen(a->message);
            int sy=cs+vy-g_alert_scroll;

            if(sy>=cs&&sy<cs+ch){
                attron(COLOR_PAIR(cp)|A_BOLD);
                mvprintw(sy,3,"%s",ic);
                attroff(COLOR_PAIR(cp)|A_BOLD);
                attron(A_BOLD);
                print_fit(sy,6,COLS-18,a->titre[0]?a->titre:"Message reseau");
                attroff(A_BOLD);
            }
            vy++;

            sy=cs+vy-g_alert_scroll;
            if(sy>=cs&&sy<cs+ch){
                attron(A_DIM);
                if(a->lines[0]) mvprintw(sy,6,"%s  severity %s",a->lines,a->importance[0]?a->importance:"2");
                else mvprintw(sy,6,"network-wide severity %s",a->importance[0]?a->importance:"1");
                attroff(A_DIM);
            }
            vy++;

            if(mw<20) mw=20;
            while(po<ml){
                int e=po, c=0, cut=0, last_space=-1;
                while(a->message[e]&&a->message[e]!='\n'&&c<mw){
                    if(a->message[e]==' ') last_space=e-po;
                    e++; c++;
                }
                cut=e-po;
                if(a->message[e]&&a->message[e]!='\n'&&c==mw&&last_space>0) cut=last_space;
                if(cut<=0) cut=e-po;
                sy=cs+vy-g_alert_scroll;
                if(sy>=cs&&sy<cs+ch) mvprintw(sy,8,"%.*s",cut,a->message+po);
                vy++;
                po+=cut;
                while(a->message[po]==' ') po++;
                if(a->message[po]=='\n') po++;
            }
            sy=cs+vy-g_alert_scroll;
            if(i<g_ntls_alerts-1&&sy>=cs&&sy<cs+ch){
                attron(A_DIM);
                mvhline(sy,4,ACS_HLINE,COLS-10);
                attroff(A_DIM);
            }
            vy++;
        }
        g_alert_total_h=vy;
        if(g_alert_total_h>ch) scrollbar(cs,ch,g_alert_scroll,g_alert_total_h);
    }

    draw_toast_msg();
    {
        char ri[32];
        if(g_alert_total_h>0) snprintf(ri,sizeof(ri),"%d/%d",g_alert_scroll+1,g_alert_total_h);
        else ri[0]=0;
        draw_status(" j/k:scroll"U_MDOT"r:refresh"U_MDOT"q:back"U_MDOT"t:theme",ri);
    }
}

static void draw_toulouse_stops(void)
{
    ToulouseStop *sel = selected_toulouse_stop();
    int cached = (sel && g_tls_sel_stop == g_tls_stop_filtered[g_tls_stop_cursor]) ? g_ntls_passages : 0;
    int top = 3;
    int preview_idx = g_ntls_stop_filtered > 0 ? g_tls_stop_filtered[g_tls_stop_cursor] : -1;

    draw_header("Stations // Search",g_tls_stop_search[0]?"filter active":"station browser");
    draw_tabs();

    if(COLS>=94&&LINES>=24){
        int gap=1;
        int cw=(COLS-5-gap*3)/4;
        char buf[32];
        snprintf(buf,sizeof(buf),"%d",g_ntls_stops);
        stat_card(top,2,cw,"GROUPS",buf,"arrets agreges",CP_ACCENT);
        snprintf(buf,sizeof(buf),"%d",g_ntls_stop_filtered);
        stat_card(top,2+cw+gap,cw,"VISIBLE",buf,"hits courants",CP_CYAN_T);
        snprintf(buf,sizeof(buf),"%d",sel?toulouse_token_count(sel->lignes):0);
        stat_card(top,2+(cw+gap)*2,cw,"PLATFORMS",buf,"quais selectionnes",CP_GREEN);
        snprintf(buf,sizeof(buf),"%d",cached);
        stat_card(top,2+(cw+gap)*3,cw,"CACHE",buf,cached?"passages memorises":"ouvrez un board",cached?CP_GREEN:CP_YELLOW);
        top+=5;
    }

    if(COLS>=108&&LINES-top>=16){
        int y1=top, y2=LINES-3;
        int left_w=COLS*55/100;
        if(left_w<48) left_w=48;
        if(left_w>COLS-34) left_w=COLS-34;
        int lx1=1, lx2=lx1+left_w-1, rx1=lx2+1, rx2=COLS-2;
        int head_y=y1+1, list_y=head_y+2;
        int mr=y2-list_y, name_w=lx2-lx1-12;

        if(name_w<18) name_w=18;
        if(mr<1) mr=1;
        panel_box(y1,lx1,y2,lx2,"Station Index",g_tls_stop_search[0]?g_tls_stop_search:"all stops");
        panel_box(y1,rx1,y2,rx2,"Platform Preview",preview_idx>=0?g_tls_stops[preview_idx].libelle:"idle");

        attron(A_DIM);
        mvprintw(head_y,lx1+3,"STOP");
        mvprintw(head_y,lx2-8,"QUAIS");
        attroff(A_DIM);

        if(g_tls_stop_cursor<g_tls_stop_scroll)g_tls_stop_scroll=g_tls_stop_cursor;
        if(g_tls_stop_cursor>=g_tls_stop_scroll+mr)g_tls_stop_scroll=g_tls_stop_cursor-mr+1;

        for(int i=0;i<mr&&g_tls_stop_scroll+i<g_ntls_stop_filtered;i++){
            int idx=g_tls_stop_filtered[g_tls_stop_scroll+i];
            ToulouseStop *stop=&g_tls_stops[idx];
            int row=list_y+i;
            int selected=(g_tls_stop_scroll+i==g_tls_stop_cursor);
            if(selected){
                fill_span(row,lx1+1,lx2-1,CP_SEL,0);
                attron(COLOR_PAIR(CP_CURSOR)|A_BOLD);
                mvaddstr(row,lx1+1,U_CBAR);
                attroff(COLOR_PAIR(CP_CURSOR)|A_BOLD);
                attron(COLOR_PAIR(CP_SEL));
            } else if(i%2) attron(A_DIM);
            print_hl(row,lx1+4,stop->libelle,g_tls_stop_search,name_w);
            mvprintw(row,lx2-8,"%3d",toulouse_token_count(stop->lignes));
            if(selected) attroff(COLOR_PAIR(CP_SEL));
            else if(i%2) attroff(A_DIM);
        }
        scrollbar(list_y,mr,g_tls_stop_scroll,g_ntls_stop_filtered);

        if(preview_idx>=0){
            ToulouseStop *stop=&g_tls_stops[preview_idx];
            int y=y1+2, w=rx2-rx1-3;
            char buf[64];
            attron(A_BOLD);
            print_fit(y,rx1+2,w,stop->libelle);
            attroff(A_BOLD);
            attron(A_DIM);
            mvprintw(y+1,rx1+2,"%s",stop->commune[0]?stop->commune:"stop area");
            attroff(A_DIM);

            snprintf(buf,sizeof(buf),"%d",toulouse_token_count(stop->lignes));
            stat_card(y+3,rx1+2,(w-1)/2,"QUAIS",buf,"physical stop ids",CP_GREEN);
            snprintf(buf,sizeof(buf),"%d",cached);
            stat_card(y+3,rx1+3+(w-1)/2,w-(w-1)/2,"CACHE",buf,cached?"board already loaded":"press Enter to load",cached?CP_GREEN:CP_YELLOW);

            y+=9;
            attron(COLOR_PAIR(CP_SECTION)|A_BOLD);
            mvprintw(y++,rx1+2,"Stop IDs");
            attroff(COLOR_PAIR(CP_SECTION)|A_BOLD);
            attron(COLOR_PAIR(CP_ACCENT)|A_BOLD);
            mvprintw(y,rx1+2,U_BULLET);
            attroff(COLOR_PAIR(CP_ACCENT)|A_BOLD);
            mvprintw(y++,rx1+5,"%s",stop->ref);
            if(stop->adresse[0]&&y<y2-2){
                attron(COLOR_PAIR(CP_ACCENT)|A_BOLD);
                mvprintw(y,rx1+2,U_BULLET);
                attroff(COLOR_PAIR(CP_ACCENT)|A_BOLD);
                print_fit(y++,rx1+5,w-4,stop->adresse);
            }

            if(cached&&y<y2-3){
                y++;
                attron(COLOR_PAIR(CP_SECTION)|A_BOLD);
                mvprintw(y++,rx1+2,"Cached Departures");
                attroff(COLOR_PAIR(CP_SECTION)|A_BOLD);
                for(int i=0;i<g_ntls_passages&&i<4&&y<y2-1;i++){
                    char hr[8];
                    toulouse_passage_clock(&g_tls_passages[i],hr,sizeof(hr));
                    attron(COLOR_PAIR(g_tls_passages[i].realtime?CP_GREEN:CP_YELLOW)|A_BOLD);
                    mvprintw(y,rx1+2,"%5s",hr);
                    attroff(COLOR_PAIR(g_tls_passages[i].realtime?CP_GREEN:CP_YELLOW)|A_BOLD);
                    mvprintw(y,rx1+10,"%s",g_tls_passages[i].line_code[0]?g_tls_passages[i].line_code:"?");
                    y++;
                }
            } else if(y<y2-3){
                attron(A_DIM);
                mvprintw(y+1,rx1+2,"Enter ouvre le board temps reel.");
                mvprintw(y+2,rx1+2,"/: change le filtre, q: retour.");
                attroff(A_DIM);
            }
        }
    } else {
        int y1=top, y2=LINES-3, head_y=y1+1, list_y=head_y+2;
        int mr=y2-list_y, cw=COLS-16;
        if(cw<20) cw=20;
        if(mr<1) mr=1;
        panel_box(y1,1,y2,COLS-2,"Station Index",g_tls_stop_search[0]?g_tls_stop_search:"all stops");
        attron(A_DIM);
        mvprintw(head_y,3,"STOP");
        mvprintw(head_y,COLS-8,"Q");
        attroff(A_DIM);

        if(g_tls_stop_cursor<g_tls_stop_scroll)g_tls_stop_scroll=g_tls_stop_cursor;
        if(g_tls_stop_cursor>=g_tls_stop_scroll+mr)g_tls_stop_scroll=g_tls_stop_cursor-mr+1;

        for(int i=0;i<mr&&g_tls_stop_scroll+i<g_ntls_stop_filtered;i++){
            int idx=g_tls_stop_filtered[g_tls_stop_scroll+i];
            ToulouseStop *stop=&g_tls_stops[idx];
            int row=list_y+i;
            int selected=(g_tls_stop_scroll+i==g_tls_stop_cursor);
            if(selected){
                fill_span(row,2,COLS-3,CP_SEL,0);
                attron(COLOR_PAIR(CP_CURSOR)|A_BOLD);
                mvaddstr(row,2,U_CBAR);
                attroff(COLOR_PAIR(CP_CURSOR)|A_BOLD);
                attron(COLOR_PAIR(CP_SEL));
            } else if(i%2) attron(A_DIM);
            print_hl(row,4,stop->libelle,g_tls_stop_search,cw);
            mvprintw(row,COLS-8,"%3d",toulouse_token_count(stop->lignes));
            if(selected) attroff(COLOR_PAIR(CP_SEL));
            else if(i%2) attroff(A_DIM);
        }
        scrollbar(list_y,mr,g_tls_stop_scroll,g_ntls_stop_filtered);
    }

    if(g_tls_stop_search[0]){
        attron(COLOR_PAIR(CP_SEARCH)|A_BOLD);
        mvhline(LINES-2,0,' ',COLS);
        mvprintw(LINES-2,1," / %s   %d hits",g_tls_stop_search,g_ntls_stop_filtered);
        attroff(COLOR_PAIR(CP_SEARCH)|A_BOLD);
    }
    draw_toast_msg();
    {
        char r[32];
        snprintf(r,sizeof(r),"%d/%d",g_ntls_stop_filtered>0?g_tls_stop_cursor+1:0,g_ntls_stop_filtered);
        draw_status(" j/k"U_MDOT"Enter"U_MDOT"/"U_MDOT"q:back"U_MDOT"t:theme",r);
    }
}

static void draw_toulouse_passages(void)
{
    ToulouseStop *stop = (g_tls_sel_stop >= 0 && g_tls_sel_stop < g_ntls_stops) ? &g_tls_stops[g_tls_sel_stop] : NULL;
    char t[128], bc[128], buf[32];
    int top=3;
    int live=count_toulouse_live_passages();
    int delayed=count_toulouse_delayed_passages();
    int uniq=count_toulouse_unique_passage_lines();
    int next_min=g_ntls_passages?toulouse_waiting_minutes(g_tls_passages[0].waiting_time):-1;
    snprintf(t,sizeof(t),"Departures // %s",stop?stop->libelle:"stop area");
    snprintf(bc,sizeof(bc),"Arrets "U_ARROW" %s",stop?stop->libelle:"selection");
    draw_header(t,bc); draw_tabs();

    if(COLS>=94&&LINES>=24){
        int gap=1;
        int cw=(COLS-5-gap*3)/4;
        if(next_min<0) snprintf(buf,sizeof(buf),"--");
        else if(next_min<=1) snprintf(buf,sizeof(buf),"NOW");
        else snprintf(buf,sizeof(buf),"%dmin",next_min);
        stat_card(top,2,cw,"NEXT",buf,"prochain passage",next_min>=0?CP_GREEN:CP_YELLOW);
        snprintf(buf,sizeof(buf),"%d",live);
        stat_card(top,2+cw+gap,cw,"LIVE",buf,"estimations live",live?CP_GREEN:CP_YELLOW);
        snprintf(buf,sizeof(buf),"%d",uniq);
        stat_card(top,2+(cw+gap)*2,cw,"LINES",buf,"lignes a venir",CP_ACCENT);
        snprintf(buf,sizeof(buf),"%d",delayed);
        stat_card(top,2+(cw+gap)*3,cw,"DELAY",buf,"depassements",delayed?CP_ALERT_MED:CP_GREEN);
        top+=5;
    }

    if(COLS>=112&&LINES-top>=16){
        int y1=top, y2=LINES-3;
        int left_w=COLS*64/100;
        if(left_w<58) left_w=58;
        if(left_w>COLS-30) left_w=COLS-30;
        int lx1=1, lx2=lx1+left_w-1, rx1=lx2+1, rx2=COLS-2;
        int head_y=y1+1, row_y=head_y+2, max_rows=y2-row_y;

        if(max_rows<1) max_rows=1;
        panel_box(y1,lx1,y2,lx2,"Departure Board",stop?stop->libelle:"idle");
        panel_box(y1,rx1,y2,rx2,"Next Wave",g_ntls_passages?"live board":"idle");

        attron(A_DIM);
        mvprintw(head_y,lx1+3,"TIME");
        mvprintw(head_y,lx1+12,"ETA");
        mvprintw(head_y,lx1+21,"LINE");
        mvprintw(head_y,lx1+41,"TERMINUS");
        attroff(A_DIM);

        for(int i=0;i<g_ntls_passages&&i<max_rows;i++){
            ToulousePassage *p=&g_tls_passages[i];
            char hr[8], eta[16];
            int cp=p->realtime?CP_GREEN:CP_YELLOW;
            int row=row_y+i;

            toulouse_passage_clock(p,hr,sizeof(hr));
            toulouse_waiting_eta(p->waiting_time,eta,sizeof(eta));
            if(i%2) attron(A_DIM);
            mvhline(row,lx1+1,' ',lx2-lx1-1);
            if(i%2) attroff(A_DIM);
            if(i%2) attron(A_DIM);

            attron(COLOR_PAIR(cp)|A_BOLD);
            mvprintw(row,lx1+3,"%5s",hr);
            attroff(COLOR_PAIR(cp)|A_BOLD);
            if(strcmp(eta,"NOW")==0){attron(COLOR_PAIR(CP_GREEN)|A_BOLD);mvprintw(row,lx1+12,"NOW   ");attroff(COLOR_PAIR(CP_GREEN)|A_BOLD);}
            else if(strchr(eta,'m')){attron(COLOR_PAIR(cp)|A_BOLD);mvprintw(row,lx1+12,"%6s",eta);attroff(COLOR_PAIR(cp)|A_BOLD);}
            else mvprintw(row,lx1+12,"%6s",eta);
            draw_toulouse_badge_by_code(row,lx1+20,p->line_code);
            print_fit(row,lx1+41,lx2-lx1-43,p->destination);
            if(i%2) attroff(A_DIM);
        }

        {
            int y=y1+2, inner=rx2-rx1-3;
            attron(A_BOLD);
            print_fit(y,rx1+2,inner,stop?stop->libelle:"stop area");
            attroff(A_BOLD);
            attron(A_DIM);
            mvprintw(y+1,rx1+2,"%d departures loaded",g_ntls_passages);
            attroff(A_DIM);
            y+=3;
            for(int i=0;i<g_ntls_passages&&i<6&&y<y2-1;i++){
                ToulousePassage *p=&g_tls_passages[i];
                char hr[8], eta[16];
                int cp=p->realtime?CP_GREEN:CP_YELLOW;
                toulouse_passage_clock(p,hr,sizeof(hr));
                toulouse_waiting_eta(p->waiting_time,eta,sizeof(eta));
                attron(COLOR_PAIR(cp)|A_BOLD);
                mvprintw(y,rx1+2,"%5s",hr);
                attroff(COLOR_PAIR(cp)|A_BOLD);
                mvprintw(y,rx1+10,"%6s",eta);
                draw_toulouse_badge_by_code(y,rx1+17,p->line_code);
                y++;
            }
            if(!g_ntls_passages&&y<y2-2){
                attron(A_DIM);
                mvprintw(y,rx1+2,U_INFO" aucun passage prevu");
                mvprintw(y+2,rx1+2,"r recharge le board");
                mvprintw(y+3,rx1+2,"/ retourne a la recherche");
                attroff(A_DIM);
            }
        }
    } else {
        int y1=top, y2=LINES-3, head_y=y1+1, row_y=head_y+2;
        panel_box(y1,1,y2,COLS-2,"Departure Board",stop?stop->libelle:"idle");
        attron(A_DIM);
        mvprintw(head_y,3,"TIME");
        mvprintw(head_y,12,"ETA");
        mvprintw(head_y,21,"LINE");
        mvprintw(head_y,39,"TERMINUS");
        attroff(A_DIM);
        for(int i=0;i<g_ntls_passages&&row_y+i<y2;i++){
            ToulousePassage *p=&g_tls_passages[i];
            char hr[8], eta[16];
            int cp=p->realtime?CP_GREEN:CP_YELLOW;
            int row=row_y+i;
            toulouse_passage_clock(p,hr,sizeof(hr));
            toulouse_waiting_eta(p->waiting_time,eta,sizeof(eta));
            if(i%2) attron(A_DIM);
            mvhline(row,2,' ',COLS-4);
            if(i%2) attroff(A_DIM);
            if(i%2) attron(A_DIM);
            attron(COLOR_PAIR(cp)|A_BOLD);
            mvprintw(row,3,"%5s",hr);
            attroff(COLOR_PAIR(cp)|A_BOLD);
            mvprintw(row,12,"%6s",eta);
            draw_toulouse_badge_by_code(row,20,p->line_code);
            print_fit(row,39,COLS-42,p->destination);
            if(i%2) attroff(A_DIM);
        }
        if(!g_ntls_passages){
            attron(A_DIM);
            mvprintw(y1+4,3,U_INFO" aucun passage prevu");
            attroff(A_DIM);
        }
    }

    draw_toast_msg();
    {
        char r[32];
        snprintf(r,sizeof(r),"%d pass.",g_ntls_passages);
        draw_status(" q:back"U_MDOT"r:refresh"U_MDOT"/:arret"U_MDOT"t:theme",r);
    }
}

static void draw_toulouse_vehicle_map_panel(int y1,int x1,int y2,int x2)
{
    ToulouseLine *line=selected_toulouse_line();
    int legend_y=y1+1, py=y1+3, px=x1+2;
    int w=x2-x1-3, h=y2-py-1;
    double base_minlon, base_maxlon, base_minlat, base_maxlat;
    double minlon, maxlon, minlat, maxlat;
    char meta[48], legend[160];
    int line_cp=line?toulouse_badge_cp(line):CP_ACCENT;
    int has_basemap=g_has_tls_metro_map&&g_tls_metro_map.npaths>0;
    int has_route=g_has_tls_line_route&&g_tls_line_route.npaths>0;

    panel_box(y1,x1,y2,x2,"ASCII Map",line&&(has_basemap||has_route)?"route":"idle");
    if(!line||(!has_basemap&&!has_route)){
        attron(A_DIM);
        mvprintw(y1+2,x1+2,"route unavailable");
        attroff(A_DIM);
        return;
    }
    if(w<18||h<6){
        attron(A_DIM);
        mvprintw(y1+2,x1+2,"map area too small");
        attroff(A_DIM);
        return;
    }

    if(has_basemap){
        base_minlon=g_tls_metro_map.minlon; base_maxlon=g_tls_metro_map.maxlon;
        base_minlat=g_tls_metro_map.minlat; base_maxlat=g_tls_metro_map.maxlat;
    } else if(toulouse_line_route_bounds(&base_minlon,&base_maxlon,&base_minlat,&base_maxlat)){
        double pad_lon=(base_maxlon-base_minlon)*0.08;
        double pad_lat=(base_maxlat-base_minlat)*0.08;
        if(pad_lon<0.0025) pad_lon=0.0025;
        if(pad_lat<0.0025) pad_lat=0.0025;
        base_minlon-=pad_lon; base_maxlon+=pad_lon;
        base_minlat-=pad_lat; base_maxlat+=pad_lat;
    } else {
        attron(A_DIM);
        mvprintw(y1+2,x1+2,"route unavailable");
        attroff(A_DIM);
        return;
    }
    apply_vehicle_zoom(base_minlon,base_maxlon,base_minlat,base_maxlat,
                       g_vehicle_zoom,&minlon,&maxlon,&minlat,&maxlat);

    snprintf(meta,sizeof(meta),"zoom x%d",1<<g_vehicle_zoom);
    panel_box(y1,x1,y2,x2,"ASCII Map",meta);
    attron(A_DIM);
    snprintf(legend,sizeof(legend),"%s | route only | no vehicle positions",
             has_basemap?"communes":"route");
    print_fit(legend_y,x1+2,x2-x1-3,legend);
    attroff(A_DIM);

    {
        unsigned char boundary_mask[h*w], route_mask[h*w], route_multi[h*w];
        int route_colors[h*w], route_attrs[h*w];
        memset(boundary_mask,0,sizeof(boundary_mask));
        memset(route_mask,0,sizeof(route_mask));
        memset(route_multi,0,sizeof(route_multi));
        memset(route_colors,0,sizeof(route_colors));
        memset(route_attrs,0,sizeof(route_attrs));

        if(has_basemap){
            for(int i=0;i<g_tls_metro_map.npaths;i++){
                MapPath *path=&g_tls_metro_map.paths[i];
                for(int j=1;j<path->count;j++){
                    MapPoint *a=&g_tls_metro_map.points[path->start+j-1];
                    MapPoint *b=&g_tls_metro_map.points[path->start+j];
                    double lon0=a->lon, lat0=a->lat, lon1=b->lon, lat1=b->lat;
                    int x0,y0,x1p,y1p;
                    if(!map_clip_segment(&lon0,&lat0,&lon1,&lat1,minlon,maxlon,minlat,maxlat)) continue;
                    map_project(lon0,lat0,minlon,maxlon,minlat,maxlat,w,h,&x0,&y0);
                    map_project(lon1,lat1,minlon,maxlon,minlat,maxlat,w,h,&x1p,&y1p);
                    map_draw_mask_segment(boundary_mask,w,h,x0,y0,x1p,y1p);
                }
            }
        }

        if(has_route){
            for(int i=0;i<g_tls_line_route.npaths;i++){
                MapPath *path=&g_tls_line_route.paths[i];
                for(int j=1;j<path->count;j++){
                    MapPoint *a=&g_tls_line_route.points[path->start+j-1];
                    MapPoint *b=&g_tls_line_route.points[path->start+j];
                    double lon0=a->lon, lat0=a->lat, lon1=b->lon, lat1=b->lat;
                    int x0,y0,x1p,y1p;
                    if(!map_clip_segment(&lon0,&lat0,&lon1,&lat1,minlon,maxlon,minlat,maxlat)) continue;
                    map_project(lon0,lat0,minlon,maxlon,minlat,maxlat,w,h,&x0,&y0);
                    map_project(lon1,lat1,minlon,maxlon,minlat,maxlat,w,h,&x1p,&y1p);
                    map_draw_mask_segment_layer(route_mask,route_colors,route_attrs,route_multi,w,h,x0,y0,x1p,y1p,line_cp,A_BOLD);
                }
            }
        }

        for(int my=0;my<h;my++){
            move(py+my,px);
            for(int mx=0;mx<w;mx++){
                int idx=my*w+mx;
                if(route_mask[idx]){
                    if(route_multi[idx]) attron(COLOR_PAIR(CP_ACCENT)|A_BOLD);
                    else attron(COLOR_PAIR(route_colors[idx])|route_attrs[idx]);
                    addstr(map_mask_glyph(route_mask[idx]));
                    if(route_multi[idx]) attroff(COLOR_PAIR(CP_ACCENT)|A_BOLD);
                    else attroff(COLOR_PAIR(route_colors[idx])|route_attrs[idx]);
                } else if(boundary_mask[idx]){
                    attron(COLOR_PAIR(CP_SECTION)|A_BOLD);
                    addstr(map_mask_glyph(boundary_mask[idx]));
                    attroff(COLOR_PAIR(CP_SECTION)|A_BOLD);
                } else addch(' ');
            }
        }

        if(has_basemap&&w>=28){
            unsigned char label_occ[h*w];
            memset(label_occ,0,sizeof(label_occ));
            for(int i=0;i<g_tls_line_route.npaths;i++){
                MapPath *path=&g_tls_line_route.paths[i];
                for(int j=0;j<path->count;j++){
                    MapPoint *p=&g_tls_line_route.points[path->start+j];
                    int mx,my;
                    if(p->lon<minlon||p->lon>maxlon||p->lat<minlat||p->lat>maxlat) continue;
                    map_project(p->lon,p->lat,minlon,maxlon,minlat,maxlat,w,h,&mx,&my);
                    if(mx>=0&&mx<w&&my>=0&&my<h) label_occ[my*w+mx]=1;
                }
            }
            for(int i=0;i<g_tls_metro_map.nlabels;i++){
                char lbl[16];
                int mx,my,lx,ly,len;
                if(g_tls_metro_map.labels[i].rank<3) continue;
                format_commune_label(g_tls_metro_map.labels[i].name,lbl,sizeof(lbl));
                len=(int)strlen(lbl);
                if(g_tls_metro_map.labels[i].lon<minlon||g_tls_metro_map.labels[i].lon>maxlon
                || g_tls_metro_map.labels[i].lat<minlat||g_tls_metro_map.labels[i].lat>maxlat) continue;
                map_project(g_tls_metro_map.labels[i].lon,g_tls_metro_map.labels[i].lat,minlon,maxlon,minlat,maxlat,w,h,&mx,&my);
                if(!map_find_label_slot(label_occ,w,h,mx,my,len,&lx,&ly)) continue;
                map_draw_label(py+ly,px+lx,px+w,lbl,CP_SECTION);
                map_label_slot_mark(label_occ,w,h,lx,ly,len);
            }
        }
    }
}

static void draw_toulouse_vehicle_lane_panel(int y1,int x1,int y2,int x2,const char *dir)
{
    char meta[32], buf[64], title[96];
    const char *terminus="?";
    int nterm=0;
    int cnt=count_toulouse_vehicles_dir(dir);
    int delayed=count_toulouse_delayed_vehicles_dir(dir);
    int stopped=count_toulouse_stopped_vehicles_dir(dir);
    int avg=avg_toulouse_speed_dir(dir);
    int y=y1+2;
    int inner=x2-x1-3;
    int term_w=inner>=48?14:inner>=40?10:inner>=32?8:0;
    int route_w=inner-18-(term_w?term_w+2:0);

    for(int i=0;i<g_ntls_vehicles;i++){
        int dup=0;
        if(strcmp(g_tls_vehicles[i].sens,dir)!=0) continue;
        if(nterm==0) terminus=g_tls_vehicles[i].terminus;
        for(int j=0;j<i;j++){
            if(strcmp(g_tls_vehicles[j].sens,dir)==0&&strcmp(g_tls_vehicles[j].terminus,g_tls_vehicles[i].terminus)==0){
                dup=1;
                break;
            }
        }
        if(!dup) nterm++;
    }
    snprintf(meta,sizeof(meta),"%d live",cnt);
    panel_box(y1,x1,y2,x2,dir,meta);
    if(route_w<16) route_w=16;

    if(nterm<=1) snprintf(title,sizeof(title),"%s",terminus);
    else snprintf(title,sizeof(title),"%s  +%d partial",terminus,nterm-1);
    attron(A_BOLD);
    print_fit(y,x1+2,inner,title);
    attroff(A_BOLD);
    attron(A_DIM);
    snprintf(buf,sizeof(buf),"%dkm/h avg  %d delayed  %d stopped",avg,delayed,stopped);
    print_fit(y+1,x1+2,inner,buf);
    mvprintw(y+2,x1+2,"now");
    if(term_w) mvprintw(y+2,x1+5+route_w,"term");
    mvprintw(y+2,x2-13,"delay");
    mvprintw(y+2,x2-6,"spd");
    attroff(A_DIM);
    y+=3;

    if(!cnt){
        attron(A_DIM);
        mvprintw(y,x1+2,U_INFO" aucun vehicule sur ce sens");
        attroff(A_DIM);
        return;
    }

    for(int i=0, row=0;i<g_ntls_vehicles&&y<=y2-1;i++){
        ToulouseVehicle *v=&g_tls_vehicles[i];
        char route[192], dly[16], when[16];
        const char *ind;
        int cp=v->realtime?CP_GREEN:CP_YELLOW;
        if(strcmp(v->sens,dir)!=0) continue;
        toulouse_waiting_eta(v->waiting_time,when,sizeof(when));
        snprintf(route,sizeof(route),"%s %s "U_ARROW" %s",when,v->current_stop,v->next_stop);
        snprintf(dly,sizeof(dly),"%s",v->realtime?"live":"sched");
        ind=v->realtime?U_BULLET:U_WARN;

        if(y>y2-1) break;
        if(row%2){
            attron(A_DIM);
            mvhline(y,x1+1,' ',x2-x1-1);
            attroff(A_DIM);
            attron(A_DIM);
        }
        attron(COLOR_PAIR(cp)|A_BOLD);
        mvprintw(y,x1+2,"%s",ind);
        attroff(COLOR_PAIR(cp)|A_BOLD);
        if(row%2) attron(A_DIM);
        print_fit(y,x1+4,route_w,route);
        if(term_w){
            attron(A_DIM);
            print_fit(y,x1+5+route_w,term_w,v->terminus);
            attroff(A_DIM);
            if(row%2) attron(A_DIM);
        }
        attron(COLOR_PAIR(cp)|A_BOLD);
        mvprintw(y,x2-13,"%6s",dly);
        attroff(COLOR_PAIR(cp)|A_BOLD);
        mvprintw(y,x2-6,"%3d",v->vitesse);
        if(v->arret){
            attron(COLOR_PAIR(CP_YELLOW)|A_BOLD);
            mvprintw(y,x2-3,"P");
            attroff(COLOR_PAIR(CP_YELLOW)|A_BOLD);
        }
        if(row%2) attroff(A_DIM);
        y++;
        row++;
    }
}

static void draw_toulouse_vehicles(void)
{
    ToulouseLine *ln=selected_toulouse_line();
    char t[128],bc[128];
    int total_delayed=0, total_stopped=0, total_speed=0;
    int line_alerts=count_toulouse_line_alerts(ln);
    int cards_top=3, top=3;
    int lcp=ln?toulouse_line_pair(ln):0;
    if(!ln){
        draw_header("Vehicles // Toulouse","no line selected");
        draw_tabs();
        panel_box(3,1,LINES-3,COLS-2,"Live Vehicles","idle");
        attron(A_DIM);
        mvprintw(6,4,U_INFO" aucune ligne selectionnee");
        attroff(A_DIM);
        draw_toast_msg();
        draw_status(" n:menu"U_MDOT"1:lignes"U_MDOT"t:theme","n/a");
        return;
    }

    snprintf(t,sizeof(t),"%s / %s",ln->code,ln->libelle);
    snprintf(bc,sizeof(bc),"Lignes "U_ARROW" live telemetry");
    if(!g_tls_map_attempted){
        g_tls_map_attempted=1;
        g_has_tls_metro_map=fetch_toulouse_metro_map(&g_tls_metro_map)>0;
        toast(g_has_tls_metro_map?"Carte communes chargee":"Carte communes indisponible");
    }
    ensure_toulouse_line_route();
    if(lcp){
        attron(COLOR_PAIR(lcp)|A_BOLD); mvhline(0,0,' ',COLS);
        mvprintw(0,2," %s ",t);
        time_t now=time(NULL); struct tm *tm=localtime(&now);
        mvprintw(0,COLS-10,"%02d:%02d:%02d",tm->tm_hour,tm->tm_min,tm->tm_sec);
        attroff(COLOR_PAIR(lcp)|A_BOLD);
    } else draw_header(t,bc);
    draw_tabs();
    for(int i=0;i<g_ntls_vehicles;i++){
        total_speed+=g_tls_vehicles[i].vitesse;
        if(g_tls_vehicles[i].delayed) total_delayed++;
        if(g_tls_vehicles[i].arret) total_stopped++;
    }

    if(COLS>=92&&LINES>=24){
        int gap=1;
        int cw=(COLS-5-gap*3)/4;
        char buf[32];
        snprintf(buf,sizeof(buf),"%d",g_ntls_vehicles);
        stat_card(cards_top,2,cw,"LIVE",buf,"vehicules suivis",CP_ACCENT);
        snprintf(buf,sizeof(buf),"%d",total_delayed);
        stat_card(cards_top,2+cw+gap,cw,"DELAY",buf,"retards > 1 min",total_delayed?CP_ALERT_MED:CP_GREEN);
        snprintf(buf,sizeof(buf),"%dkm/h",g_ntls_vehicles?total_speed/g_ntls_vehicles:0);
        stat_card(cards_top,2+(cw+gap)*2,cw,"AVG SPD",buf,"vitesse moyenne",CP_CYAN_T);
        snprintf(buf,sizeof(buf),"%d",line_alerts);
        stat_card(cards_top,2+(cw+gap)*3,cw,"LINE SIG",buf,line_alerts?"messages actifs":"aucun incident",line_alerts?CP_ALERT_MED:CP_GREEN);
        top+=5;
    }

    {
        int area_top=top;
        int area_bottom=LINES-3;
        int alert_h=(line_alerts&&LINES-area_top>=16)?6:0;
        int body_bottom=area_bottom-alert_h;

        if(g_has_tls_line_route&&COLS>=146&&body_bottom-area_top>=12){
            int map_x2=COLS*44/100;
            int rx1, split;
            if(map_x2<54) map_x2=54;
            if(map_x2>COLS-46) map_x2=COLS-46;
            rx1=map_x2+1;
            split=area_top+(body_bottom-area_top)/2;
            draw_toulouse_vehicle_map_panel(area_top,1,body_bottom,map_x2);
            draw_toulouse_vehicle_lane_panel(area_top,rx1,split,COLS-2,"ALLER");
            draw_toulouse_vehicle_lane_panel(split+1,rx1,body_bottom,COLS-2,"RETOUR");
        } else if(COLS>=122&&body_bottom-area_top>=10){
            int mid=COLS/2;
            draw_toulouse_vehicle_lane_panel(area_top,1,body_bottom,mid-1,"ALLER");
            draw_toulouse_vehicle_lane_panel(area_top,mid,body_bottom,COLS-2,"RETOUR");
        } else {
            int split=area_top+(body_bottom-area_top)/2;
            draw_toulouse_vehicle_lane_panel(area_top,1,split,COLS-2,"ALLER");
            draw_toulouse_vehicle_lane_panel(split+1,1,body_bottom,COLS-2,"RETOUR");
        }

        if(alert_h){
            int y1=body_bottom+1, y2=area_bottom, y=y1+2;
            char meta[32];
            snprintf(meta,sizeof(meta),"%d alert%s",line_alerts,line_alerts>1?"s":"");
            panel_box(y1,1,y2,COLS-2,"Line Alerts",meta);
            for(int i=0;i<g_ntls_alerts&&y<y2;i++){
                int cp;
                if(!toulouse_alert_applies_to_line(&g_tls_alerts[i],ln)) continue;
                cp=toulouse_alert_cp(&g_tls_alerts[i]);
                attron(COLOR_PAIR(cp)|A_BOLD);
                mvprintw(y,3,"%s ",cp==CP_ALERT_HI?U_DIAMOND:cp==CP_ALERT_MED?U_WARN:U_INFO);
                attroff(COLOR_PAIR(cp)|A_BOLD);
                attron(A_BOLD);
                print_fit(y,5,COLS-12,g_tls_alerts[i].titre[0]?g_tls_alerts[i].titre:"Message reseau");
                attroff(A_BOLD);
                y++;
                if(y<y2) y+=draw_wrapped_block(y,5,COLS-12,1,g_tls_alerts[i].message,cp,0);
                if(y<y2) y++;
            }
        }
    }

    draw_toast_msg();
    {
        char mid[96],ri[32];
        snprintf(mid,sizeof(mid)," q:back"U_MDOT"r:refresh"U_MDOT"+/-/0:zoom"U_MDOT"a:alertes"U_MDOT"p:arrets"U_MDOT"t:theme");
        snprintf(ri,sizeof(ri),"%d veh.",g_ntls_vehicles);
        draw_status(mid,ri);
    }
}

static void draw_idfm(void)
{
    ToulouseLine *sel = g_nlive_filtered > 0 ? &g_live_lines[g_live_filtered[g_live_cursor]] : NULL;
    int total = count_idfm_active_lines_all();
    int rails = count_idfm_lines_rail();
    int buses = count_idfm_lines_busish();
    int alert_lines = count_idfm_impacted_lines();
    int top = 3;

    draw_header(live_network_is_sncf() ? "NVT // SNCF" : "NVT // Paris IDFM",
                g_live_search[0] ? "reseau filtre" : "navitia live");
    draw_tabs();

    if (COLS >= 94 && LINES >= 24) {
        int gap = 1;
        int cw = (COLS - 5 - gap * 3) / 4;
        char buf[32];

        snprintf(buf, sizeof(buf), "%d", total);
        stat_card(top, 2, cw, "LINES", buf, "referentiel charge", CP_ACCENT);
        snprintf(buf, sizeof(buf), "%d", rails);
        stat_card(top, 2 + cw + gap, cw, "RAIL", buf, "metro rer tram train", CP_CYAN_T);
        snprintf(buf, sizeof(buf), "%d", buses);
        stat_card(top, 2 + (cw + gap) * 2, cw, "BUS", buf, "bus et cars", CP_GREEN);
        snprintf(buf, sizeof(buf), "%d", alert_lines);
        stat_card(top, 2 + (cw + gap) * 3, cw, "ALERTS", buf, "lignes impactees", alert_lines ? CP_ALERT_MED : CP_GREEN);
        top += 5;
    }

    if (COLS >= 112 && LINES - top >= 16) {
        int y1 = top;
        int y2 = LINES - 3;
        int left_w = COLS * 58 / 100;
        int lx1 = 1;
        int lx2;
        int rx1;
        int rx2 = COLS - 2;
        int head_y = y1 + 1;
        int list_y = head_y + 2;
        int mr;
        int name_w;

        if (left_w < 60) left_w = 60;
        if (left_w > COLS - 34) left_w = COLS - 34;
        lx2 = lx1 + left_w - 1;
        rx1 = lx2 + 1;
        mr = y2 - list_y;
        name_w = lx2 - lx1 - 28;
        if (mr < 1) mr = 1;
        if (name_w < 18) name_w = 18;

        panel_box(y1, lx1, y2, lx2, "Line Index", g_live_search[0] ? g_live_search : "all lines");
        panel_box(y1, rx1, y2, rx2, "Line Focus", sel ? nvt_idfm_line_type_label(sel) : "idle");

        if (g_live_cursor < g_live_scroll) g_live_scroll = g_live_cursor;
        if (g_live_cursor >= g_live_scroll + mr) g_live_scroll = g_live_cursor - mr + 1;

        attron(A_DIM);
        mvprintw(head_y, lx1 + 3, "LINE");
        mvprintw(head_y, lx1 + 12, "NAME");
        mvprintw(head_y, lx2 - 16, "TYPE");
        mvprintw(head_y, lx2 - 6, "ALT");
        attroff(A_DIM);

        for (int i = 0; i < mr && g_live_scroll + i < g_nlive_filtered; i++) {
            int idx = g_live_filtered[g_live_scroll + i];
            ToulouseLine *line = &g_live_lines[idx];
            int row = list_y + i;
            int alerts = count_idfm_line_alerts(line);
            int selected = (g_live_scroll + i == g_live_cursor);

            if (selected) {
                fill_span(row, lx1 + 1, lx2 - 1, CP_SEL, 0);
                attron(COLOR_PAIR(CP_CURSOR) | A_BOLD);
                mvaddstr(row, lx1 + 1, U_CBAR);
                attroff(COLOR_PAIR(CP_CURSOR) | A_BOLD);
                attron(COLOR_PAIR(CP_SEL));
            } else if (i % 2) {
                attron(A_DIM);
                mvhline(row, lx1 + 1, ' ', lx2 - lx1 - 1);
                attroff(A_DIM);
                attron(A_DIM);
            }

            draw_idfm_badge(row, lx1 + 3, line);
            if (selected) attron(COLOR_PAIR(CP_SEL));
            print_hl(row, lx1 + 12, line->libelle, g_live_search, name_w);
            mvprintw(row, lx2 - 16, "%-6s", nvt_idfm_line_type_label(line));
            if (alerts > 0) {
                attron(COLOR_PAIR(CP_ALERT_MED) | A_BOLD);
                mvprintw(row, lx2 - 6, "%2d", alerts);
                attroff(COLOR_PAIR(CP_ALERT_MED) | A_BOLD);
            } else {
                mvprintw(row, lx2 - 6, " -");
            }

            if (selected) attroff(COLOR_PAIR(CP_SEL));
            else if (i % 2) attroff(A_DIM);
        }
        scrollbar(list_y, mr, g_live_scroll, g_nlive_filtered);

        if (sel) {
            int alerts = count_idfm_line_alerts(sel);
            int y = y1 + 2;
            int px = rx1 + 2;
            int pw = rx2 - rx1 - 3;
            char buf[96];

            draw_idfm_badge(y, px, sel);
            attron(A_BOLD);
            print_fit(y, px + 9, pw - 10, sel->libelle);
            attroff(A_BOLD);
            attron(A_DIM);
            mvprintw(y + 1, px, "%s", nvt_idfm_line_type_label(sel));
            attroff(A_DIM);

            snprintf(buf, sizeof(buf), "%d", sel->terminus_count);
            stat_card(y + 3, px, (pw - 1) / 2, "ROUTES", buf, "directions exposees", CP_GREEN);
            snprintf(buf, sizeof(buf), "%d", alerts);
            stat_card(y + 3, px + (pw - 1) / 2 + 1, pw - (pw - 1) / 2, "ALERTS", buf, alerts ? "perturbations en cours" : "ligne stable", alerts ? CP_ALERT_MED : CP_GREEN);

            y += 9;
            attron(COLOR_PAIR(CP_SECTION) | A_BOLD);
            mvprintw(y++, px, "Telemetry");
            attroff(COLOR_PAIR(CP_SECTION) | A_BOLD);
            kv_line(y++, px, 10, "api ref", sel->ref, CP_ACCENT);
            kv_line(y++, px, 10, "type", nvt_idfm_line_type_label(sel), CP_ACCENT);
            kv_line(y++, px, 10, "mode", sel->mode[0] ? sel->mode : "--", CP_ACCENT);
            snprintf(buf, sizeof(buf), "%d / %d", g_nlive_filtered ? g_live_cursor + 1 : 0, g_nlive_filtered);
            kv_line(y++, px, 10, "focus", buf, CP_ACCENT);

            y++;
            attron(COLOR_PAIR(CP_SECTION) | A_BOLD);
            mvprintw(y++, px, "Signal");
            attroff(COLOR_PAIR(CP_SECTION) | A_BOLD);
            if (alerts) {
                int shown = 0;
                for (int i = 0; i < g_nlive_alerts && y < y2 - 2 && shown < 2; i++) {
                    int cp = idfm_alert_cp(&g_live_alerts[i]);

                    if (!toulouse_alert_applies_to_line(&g_live_alerts[i], sel)) continue;
                    attron(COLOR_PAIR(cp) | A_BOLD);
                    mvprintw(y, px, "%s ", cp == CP_ALERT_HI ? U_DIAMOND : U_WARN);
                    attroff(COLOR_PAIR(cp) | A_BOLD);
                    attron(A_BOLD);
                    print_fit(y, px + 2, pw - 4, g_live_alerts[i].titre[0] ? g_live_alerts[i].titre : "Message reseau");
                    attroff(A_BOLD);
                    y++;
                    y += draw_wrapped_block(y, px + 2, pw - 4, 3, g_live_alerts[i].message, cp, 0);
                    y++;
                    shown++;
                }
            } else if (count_idfm_global_alerts()) {
                int shown = 0;
                for (int i = 0; i < g_nlive_alerts && y < y2 - 2 && shown < 2; i++) {
                    int cp = idfm_alert_cp(&g_live_alerts[i]);

                    if (g_live_alerts[i].lines[0]) continue;
                    attron(COLOR_PAIR(cp) | A_BOLD);
                    mvprintw(y, px, "%s ", U_INFO);
                    attroff(COLOR_PAIR(cp) | A_BOLD);
                    attron(A_BOLD);
                    print_fit(y, px + 2, pw - 4, g_live_alerts[i].titre[0] ? g_live_alerts[i].titre : "Information reseau");
                    attroff(A_BOLD);
                    y++;
                    y += draw_wrapped_block(y, px + 2, pw - 4, 2, g_live_alerts[i].message, cp, 0);
                    y++;
                    shown++;
                }
            } else {
                attron(A_DIM);
                mvprintw(y++, px, U_CHECK" aucune alerte sur cette ligne");
                mvprintw(y++, px, "Enter: courses actives, p: arrets desservis.");
                attroff(A_DIM);
            }
        }
    } else {
        int y1 = top;
        int y2 = LINES - 3;
        int head_y = y1 + 1;
        int list_y = head_y + 2;
        int mr = y2 - list_y;
        int name_w = COLS - 28;

        if (mr < 1) mr = 1;
        if (name_w < 16) name_w = 16;
        panel_box(y1, 1, y2, COLS - 2, "Line Index", g_live_search[0] ? g_live_search : "all lines");
        attron(A_DIM);
        mvprintw(head_y, 3, "LINE");
        mvprintw(head_y, 12, "NAME");
        mvprintw(head_y, COLS - 14, "TYPE");
        attroff(A_DIM);

        if (g_live_cursor < g_live_scroll) g_live_scroll = g_live_cursor;
        if (g_live_cursor >= g_live_scroll + mr) g_live_scroll = g_live_cursor - mr + 1;

        for (int i = 0; i < mr && g_live_scroll + i < g_nlive_filtered; i++) {
            int idx = g_live_filtered[g_live_scroll + i];
            int row = list_y + i;
            int selected = (g_live_scroll + i == g_live_cursor);

            if (selected) {
                fill_span(row, 2, COLS - 3, CP_SEL, 0);
                attron(COLOR_PAIR(CP_CURSOR) | A_BOLD);
                mvaddstr(row, 2, U_CBAR);
                attroff(COLOR_PAIR(CP_CURSOR) | A_BOLD);
            }
            draw_idfm_badge(row, 4, &g_live_lines[idx]);
            print_hl(row, 13, g_live_lines[idx].libelle, g_live_search, name_w);
            mvprintw(row, COLS - 14, "%-6s", nvt_idfm_line_type_label(&g_live_lines[idx]));
        }
        scrollbar(list_y, mr, g_live_scroll, g_nlive_filtered);
    }

    if (g_live_search[0]) {
        attron(COLOR_PAIR(CP_SEARCH) | A_BOLD);
        mvhline(LINES - 2, 0, ' ', COLS);
        mvprintw(LINES - 2, 1, " / %s   %d hits", g_live_search, g_nlive_filtered);
        attroff(COLOR_PAIR(CP_SEARCH) | A_BOLD);
    }
    draw_toast_msg();

    {
        char r[32];
        snprintf(r, sizeof(r), "%d/%d", g_nlive_filtered > 0 ? g_live_cursor + 1 : 0, g_nlive_filtered);
        draw_status(" j/k"U_MDOT"Enter"U_MDOT"/"U_MDOT"a:alertes"U_MDOT"p:arrets"U_MDOT"t:theme", r);
    }
}

static void draw_idfm_alerts(void)
{
    int total = g_nlive_alerts;
    int hi = 0;
    int med = 0;
    int low = 0;
    int impacted = count_idfm_impacted_lines();
    int top = 3;

    for (int i = 0; i < g_nlive_alerts; i++) {
        int cp = idfm_alert_cp(&g_live_alerts[i]);
        if (cp == CP_ALERT_HI) hi++;
        else if (cp == CP_ALERT_MED) med++;
        else low++;
    }

    draw_header(live_network_is_sncf() ? "Operations // SNCF Alerts" : "Operations // IDFM Alerts",
                total ? "messages navitia" : "quiet network");
    draw_tabs();

    if (COLS >= 94 && LINES >= 24) {
        int gap = 1;
        int cw = (COLS - 5 - gap * 3) / 4;
        char buf[32];

        snprintf(buf, sizeof(buf), "%d", total);
        stat_card(top, 2, cw, "TOTAL", buf, "perturbations chargees", total ? CP_ALERT_MED : CP_GREEN);
        snprintf(buf, sizeof(buf), "%d", hi);
        stat_card(top, 2 + cw + gap, cw, "CRIT", buf, "fort impact", hi ? CP_ALERT_HI : CP_GREEN);
        snprintf(buf, sizeof(buf), "%d", med);
        stat_card(top, 2 + (cw + gap) * 2, cw, "WARN", buf, "impacts ligne", med ? CP_ALERT_MED : CP_GREEN);
        snprintf(buf, sizeof(buf), "%d", impacted);
        stat_card(top, 2 + (cw + gap) * 3, cw, "LINES", buf, "lignes impactees", impacted ? CP_ALERT_MED : CP_GREEN);
        top += 5;
    }

    {
        int y1 = top;
        int y2 = LINES - 3;
        int cs = y1 + 2;
        int ch = y2 - cs;
        int vy = 0;
        char meta[64];

        snprintf(meta, sizeof(meta), "%d crit / %d warn / %d info", hi, med, low);
        panel_box(y1, 1, y2, COLS - 2, "Alert Feed", meta);
        g_alert_total_h = 0;

        if (!total) {
            attron(A_DIM);
            mvprintw(y1 + 3, 3, U_CHECK" aucune alerte active");
            attroff(A_DIM);
        }

        for (int i = 0; i < g_nlive_alerts; i++) {
            ToulouseAlert *a = &g_live_alerts[i];
            int cp = idfm_alert_cp(a);
            int sy = cs + vy - g_alert_scroll;

            if (sy >= cs && sy < cs + ch) {
                attron(COLOR_PAIR(cp) | A_BOLD);
                mvprintw(sy, 3, "%s", cp == CP_ALERT_HI ? U_DIAMOND : cp == CP_ALERT_MED ? U_WARN : U_INFO);
                attroff(COLOR_PAIR(cp) | A_BOLD);
                attron(A_BOLD);
                print_fit(sy, 6, COLS - 18, a->titre[0] ? a->titre : "Information reseau");
                attroff(A_BOLD);
            }
            vy++;

            sy = cs + vy - g_alert_scroll;
            if (sy >= cs && sy < cs + ch) {
                attron(A_DIM);
                if (a->lines[0]) mvprintw(sy, 6, "%s  severity %s", a->lines, a->importance[0] ? a->importance : "--");
                else mvprintw(sy, 6, "network-wide  severity %s", a->importance[0] ? a->importance : "--");
                attroff(A_DIM);
            }
            vy++;

            sy = cs + vy - g_alert_scroll;
            if (sy >= cs && sy < cs + ch) {
                vy += draw_wrapped_block(sy, 8, COLS - 16, 5, a->message, cp, 0);
            } else {
                vy += 3;
            }

            sy = cs + vy - g_alert_scroll;
            if (i < g_nlive_alerts - 1 && sy >= cs && sy < cs + ch) {
                attron(A_DIM);
                mvhline(sy, 4, ACS_HLINE, COLS - 10);
                attroff(A_DIM);
            }
            vy++;
        }
        g_alert_total_h = vy;
        if (g_alert_total_h > ch) scrollbar(cs, ch, g_alert_scroll, g_alert_total_h);
    }

    draw_toast_msg();
    {
        char ri[32];
        if (g_alert_total_h > 0) snprintf(ri, sizeof(ri), "%d/%d", g_alert_scroll + 1, g_alert_total_h);
        else ri[0] = '\0';
        draw_status(" j/k:scroll"U_MDOT"r:refresh"U_MDOT"q:back"U_MDOT"t:theme", ri);
    }
}

static void draw_idfm_stops(void)
{
    ToulouseLine *line = selected_idfm_line();
    ToulouseStop *sel = selected_idfm_stop();
    int cached = (sel && g_live_sel_stop == g_live_stop_filtered[g_live_stop_cursor]) ? g_nlive_passages : 0;
    int top = 3;

    draw_header(live_network_is_sncf() ? "Stop Areas // SNCF" : "Stop Areas // IDFM",
                g_live_stop_search[0] ? "recherche active" : "recherche requise");
    draw_tabs();

    if (COLS >= 94 && LINES >= 24) {
        int gap = 1;
        int cw = (COLS - 5 - gap * 3) / 4;
        char buf[32];

        snprintf(buf, sizeof(buf), "%d", g_nlive_stops);
        stat_card(top, 2, cw, "STOPS", buf, "resultats charges", CP_ACCENT);
        snprintf(buf, sizeof(buf), "%d", g_nlive_stop_filtered);
        stat_card(top, 2 + cw + gap, cw, "VISIBLE", buf, "hits courants", CP_CYAN_T);
        snprintf(buf, sizeof(buf), "%s", line ? line->code : "--");
        stat_card(top, 2 + (cw + gap) * 2, cw, "LINE", buf, line ? line->libelle : "selectionnez une ligne", CP_GREEN);
        snprintf(buf, sizeof(buf), "%d", cached);
        stat_card(top, 2 + (cw + gap) * 3, cw, "CACHE", buf, cached ? "departs memorises" : "ouvrez un board", cached ? CP_GREEN : CP_YELLOW);
        top += 5;
    }

    if (COLS >= 108 && LINES - top >= 16) {
        int y1 = top;
        int y2 = LINES - 3;
        int left_w = COLS * 55 / 100;
        int lx1 = 1;
        int lx2;
        int rx1;
        int rx2 = COLS - 2;
        int head_y = y1 + 1;
        int list_y = head_y + 2;
        int mr;
        int name_w;

        if (left_w < 48) left_w = 48;
        if (left_w > COLS - 34) left_w = COLS - 34;
        lx2 = lx1 + left_w - 1;
        rx1 = lx2 + 1;
        mr = y2 - list_y;
        name_w = lx2 - lx1 - 14;
        if (mr < 1) mr = 1;
        if (name_w < 18) name_w = 18;

        panel_box(y1, lx1, y2, lx2, "Stop Index", g_live_stop_search[0] ? g_live_stop_search : "search by stop name");
        panel_box(y1, rx1, y2, rx2, "Stop Preview", sel ? sel->libelle : "idle");

        attron(A_DIM);
        mvprintw(head_y, lx1 + 3, "STOP");
        mvprintw(head_y, lx2 - 10, "CITY");
        attroff(A_DIM);

        if (g_live_stop_cursor < g_live_stop_scroll) g_live_stop_scroll = g_live_stop_cursor;
        if (g_live_stop_cursor >= g_live_stop_scroll + mr) g_live_stop_scroll = g_live_stop_cursor - mr + 1;

        for (int i = 0; i < mr && g_live_stop_scroll + i < g_nlive_stop_filtered; i++) {
            int idx = g_live_stop_filtered[g_live_stop_scroll + i];
            int row = list_y + i;
            int selected = (g_live_stop_scroll + i == g_live_stop_cursor);

            if (selected) {
                fill_span(row, lx1 + 1, lx2 - 1, CP_SEL, 0);
                attron(COLOR_PAIR(CP_CURSOR) | A_BOLD);
                mvaddstr(row, lx1 + 1, U_CBAR);
                attroff(COLOR_PAIR(CP_CURSOR) | A_BOLD);
            }
            print_hl(row, lx1 + 4, g_live_stops[idx].libelle, g_live_stop_search, name_w);
            print_fit(row, lx2 - 18, 16, g_live_stops[idx].commune[0] ? g_live_stops[idx].commune : "--");
        }
        scrollbar(list_y, mr, g_live_stop_scroll, g_nlive_stop_filtered);

        if (!line) {
            attron(A_DIM);
            mvprintw(list_y, lx1 + 3, "Selectionnez d'abord une ligne %s.", current_live_network_short());
            mvprintw(y1 + 3, rx1 + 2, "Aucune ligne active.");
            attroff(A_DIM);
        } else if (!g_live_stop_search[0]) {
            draw_idfm_badge(y1 + 2, rx1 + 2, line);
            attron(A_BOLD);
            print_fit(y1 + 2, rx1 + 11, rx2 - rx1 - 12, line->libelle);
            attroff(A_BOLD);
            attron(A_DIM);
            mvprintw(list_y, lx1 + 3, "Appuyez sur / puis validez avec Entree pour rechercher un arret.");
            mvprintw(y1 + 4, rx1 + 2, "Recherche limitee a la ligne choisie pour eviter le chargement massif.");
            mvprintw(y1 + 5, rx1 + 2, "Ligne: %s (%s)", line->code, nvt_idfm_line_type_label(line));
            attroff(A_DIM);
        } else if (!g_nlive_stop_filtered) {
            attron(A_DIM);
            mvprintw(list_y, lx1 + 3, "Aucun arret trouve pour \"%s\".", g_live_stop_search);
            mvprintw(y1 + 3, rx1 + 2, "Essayez un nom d'arret plus court ou une commune.");
            attroff(A_DIM);
        } else if (sel) {
            int y = y1 + 2;
            int px = rx1 + 2;
            int pw = rx2 - rx1 - 3;
            char buf[64];

            attron(A_BOLD);
            print_fit(y, px, pw, sel->libelle);
            attroff(A_BOLD);
            attron(A_DIM);
            mvprintw(y + 1, px, "%s", sel->commune[0] ? sel->commune : (live_network_is_sncf() ? "France" : "Ile-de-France"));
            attroff(A_DIM);

            snprintf(buf, sizeof(buf), "%s", line ? line->code : "--");
            stat_card(y + 3, px, (pw - 1) / 2, "LINE", buf, line ? line->libelle : "aucune ligne", CP_GREEN);
            snprintf(buf, sizeof(buf), "%d", cached);
            stat_card(y + 3, px + (pw - 1) / 2 + 1, pw - (pw - 1) / 2, "CACHE", buf, cached ? "board charge" : "Enter pour charger", cached ? CP_GREEN : CP_YELLOW);

            y += 9;
            attron(COLOR_PAIR(CP_SECTION) | A_BOLD);
            mvprintw(y++, px, "Stop IDs");
            attroff(COLOR_PAIR(CP_SECTION) | A_BOLD);
            kv_line(y++, px, 10, "stop area", sel->ref, CP_ACCENT);
            if (sel->adresse[0]) kv_line(y++, px, 10, "label", sel->adresse, CP_ACCENT);

            if (cached && y < y2 - 2) {
                y++;
                attron(COLOR_PAIR(CP_SECTION) | A_BOLD);
                mvprintw(y++, px, "Cached Departures");
                attroff(COLOR_PAIR(CP_SECTION) | A_BOLD);
                for (int i = 0; i < g_nlive_passages && i < 4 && y < y2 - 1; i++) {
                    char hr[8];

                    toulouse_passage_clock(&g_live_passages[i], hr, sizeof(hr));
                    attron(COLOR_PAIR(g_live_passages[i].realtime ? CP_GREEN : CP_YELLOW) | A_BOLD);
                    mvprintw(y, px, "%5s", hr);
                    attroff(COLOR_PAIR(g_live_passages[i].realtime ? CP_GREEN : CP_YELLOW) | A_BOLD);
                    mvprintw(y, px + 8, "%s", g_live_passages[i].line_code);
                    y++;
                }
            }
        }
    } else {
        int y1 = top;
        int y2 = LINES - 3;
        int head_y = y1 + 1;
        int list_y = head_y + 2;
        int mr = y2 - list_y;
        int cw = COLS - 22;

        if (mr < 1) mr = 1;
        if (cw < 18) cw = 18;
        panel_box(y1, 1, y2, COLS - 2, "Stop Index", g_live_stop_search[0] ? g_live_stop_search : "search by stop name");
        attron(A_DIM);
        mvprintw(head_y, 3, "STOP");
        mvprintw(head_y, COLS - 10, "CITY");
        attroff(A_DIM);

        if (g_live_stop_cursor < g_live_stop_scroll) g_live_stop_scroll = g_live_stop_cursor;
        if (g_live_stop_cursor >= g_live_stop_scroll + mr) g_live_stop_scroll = g_live_stop_cursor - mr + 1;

        for (int i = 0; i < mr && g_live_stop_scroll + i < g_nlive_stop_filtered; i++) {
            int idx = g_live_stop_filtered[g_live_stop_scroll + i];
            int row = list_y + i;

            print_hl(row, 4, g_live_stops[idx].libelle, g_live_stop_search, cw);
            print_fit(row, COLS - 18, 16, g_live_stops[idx].commune);
        }
        scrollbar(list_y, mr, g_live_stop_scroll, g_nlive_stop_filtered);

        if (!line) {
            attron(A_DIM);
            mvprintw(list_y, 4, "Selectionnez d'abord une ligne %s.", current_live_network_short());
            attroff(A_DIM);
        } else if (!g_live_stop_search[0]) {
            attron(A_DIM);
            mvprintw(list_y, 4, "Appuyez sur / puis Entree pour rechercher un arret sur %s.", line->code);
            attroff(A_DIM);
        } else if (!g_nlive_stop_filtered) {
            attron(A_DIM);
            mvprintw(list_y, 4, "Aucun arret trouve pour \"%s\".", g_live_stop_search);
            attroff(A_DIM);
        }
    }

    if (g_live_stop_search[0]) {
        attron(COLOR_PAIR(CP_SEARCH) | A_BOLD);
        mvhline(LINES - 2, 0, ' ', COLS);
        mvprintw(LINES - 2, 1, " / %s   %d hits", g_live_stop_search, g_nlive_stop_filtered);
        attroff(COLOR_PAIR(CP_SEARCH) | A_BOLD);
    }
    draw_toast_msg();
    {
        char r[32];
        snprintf(r, sizeof(r), "%d/%d", g_nlive_stop_filtered > 0 ? g_live_stop_cursor + 1 : 0, g_nlive_stop_filtered);
        draw_status(" j/k"U_MDOT"Enter"U_MDOT"/"U_MDOT"q:back"U_MDOT"t:theme", r);
    }
}

static void draw_idfm_passages(void)
{
    ToulouseLine *line = selected_idfm_line();
    ToulouseStop *stop = (g_live_sel_stop >= 0 && g_live_sel_stop < g_nlive_stops) ? &g_live_stops[g_live_sel_stop] : NULL;
    char title[128];
    char crumb[128];
    char buf[32];
    int top = 3;
    int live = count_idfm_live_passages();
    int delayed = count_idfm_delayed_passages();
    int uniq = count_idfm_unique_passage_lines();
    int next_min = g_nlive_passages ? toulouse_waiting_minutes(g_live_passages[0].waiting_time) : -1;

    snprintf(title, sizeof(title), "Departures // %s", stop ? stop->libelle : "stop area");
    snprintf(crumb, sizeof(crumb), "%s "U_ARROW" %s", line ? line->code : "ligne", stop ? stop->libelle : "selection");
    draw_header(title, crumb);
    draw_tabs();

    if (COLS >= 94 && LINES >= 24) {
        int gap = 1;
        int cw = (COLS - 5 - gap * 3) / 4;

        if (next_min < 0) snprintf(buf, sizeof(buf), "--");
        else if (next_min <= 1) snprintf(buf, sizeof(buf), "NOW");
        else snprintf(buf, sizeof(buf), "%dmin", next_min);
        stat_card(top, 2, cw, "NEXT", buf, "prochain depart", next_min >= 0 ? CP_GREEN : CP_YELLOW);
        snprintf(buf, sizeof(buf), "%d", live);
        stat_card(top, 2 + cw + gap, cw, "LIVE", buf, "estimations live", live ? CP_GREEN : CP_YELLOW);
        snprintf(buf, sizeof(buf), "%d", uniq);
        stat_card(top, 2 + (cw + gap) * 2, cw, "LINES", buf, "lignes a venir", CP_ACCENT);
        snprintf(buf, sizeof(buf), "%d", delayed);
        stat_card(top, 2 + (cw + gap) * 3, cw, "DELAY", buf, "ecarts theorique/reel", delayed ? CP_ALERT_MED : CP_GREEN);
        top += 5;
    }

    {
        int y1 = top;
        int y2 = LINES - 3;
        int head_y = y1 + 1;
        int row_y = head_y + 2;

        panel_box(y1, 1, y2, COLS - 2, "Departure Board", stop ? stop->libelle : "idle");
        attron(A_DIM);
        mvprintw(head_y, 3, "TIME");
        mvprintw(head_y, 12, "ETA");
        mvprintw(head_y, 22, "LINE");
        mvprintw(head_y, 33, "DIRECTION");
        attroff(A_DIM);

        for (int i = 0; i < g_nlive_passages && row_y + i < y2; i++) {
            ToulousePassage *p = &g_live_passages[i];
            char hr[8];
            char eta[16];
            int cp = p->realtime ? CP_GREEN : CP_YELLOW;
            int row = row_y + i;

            toulouse_passage_clock(p, hr, sizeof(hr));
            toulouse_waiting_eta(p->waiting_time, eta, sizeof(eta));
            if (i % 2) attron(A_DIM);
            mvhline(row, 2, ' ', COLS - 4);
            if (i % 2) attroff(A_DIM);
            attron(COLOR_PAIR(cp) | A_BOLD);
            mvprintw(row, 3, "%5s", hr);
            attroff(COLOR_PAIR(cp) | A_BOLD);
            mvprintw(row, 12, "%6s", eta);
            draw_idfm_badge_by_code(row, 21, p->line_code);
            print_fit(row, 33, COLS - 36, p->destination);
        }

        if (!g_nlive_passages) {
            attron(A_DIM);
            mvprintw(y1 + 4, 3, U_INFO" aucun depart pour cette ligne a cet arret");
            mvprintw(y1 + 6, 3, "r recharge le board, / revient a la liste des arrets.");
            attroff(A_DIM);
        }
    }

    draw_toast_msg();
    {
        char r[32];
        snprintf(r, sizeof(r), "%d dep.", g_nlive_passages);
        draw_status(" q:back"U_MDOT"r:refresh"U_MDOT"/:arret"U_MDOT"t:theme", r);
    }
}

static void draw_idfm_vehicles(void)
{
    ToulouseLine *line = selected_idfm_line();
    ToulouseStop *stop = (g_live_sel_stop >= 0 && g_live_sel_stop < g_nlive_stops) ? &g_live_stops[g_live_sel_stop] : NULL;
    int unique_destinations = 0;
    int alerts = count_idfm_line_alerts(line);
    int cached_passages = stop ? g_nlive_passages : 0;
    int top = 3;

    if (!line) {
        draw_header(live_network_is_sncf() ? "Journeys // SNCF" : "Vehicles // Paris IDFM", "no line selected");
        draw_tabs();
        panel_box(3, 1, LINES - 3, COLS - 2, "Active Journeys", "idle");
        attron(A_DIM);
        mvprintw(6, 4, U_INFO" aucune ligne selectionnee");
        attroff(A_DIM);
        draw_toast_msg();
        draw_status(" n:menu"U_MDOT"1:lignes"U_MDOT"t:theme", "n/a");
        return;
    }

    for (int i = 0; i < g_nlive_vehicles; i++) {
        int dup = 0;
        for (int j = 0; j < i; j++) {
            if (strcmp(g_live_vehicles[i].terminus, g_live_vehicles[j].terminus) == 0) {
                dup = 1;
                break;
            }
        }
        if (!dup && g_live_vehicles[i].terminus[0]) unique_destinations++;
    }

    draw_header(live_network_is_sncf() ? "Journeys // SNCF" : "Vehicles // Paris IDFM", line->libelle);
    draw_tabs();

    if (COLS >= 92 && LINES >= 24) {
        int gap = 1;
        int cw = (COLS - 5 - gap * 3) / 4;
        char buf[32];

        snprintf(buf, sizeof(buf), "%d", g_nlive_vehicles);
        stat_card(top, 2, cw, "JOURNEYS", buf, "courses actives", CP_ACCENT);
        snprintf(buf, sizeof(buf), "%d", unique_destinations);
        stat_card(top, 2 + cw + gap, cw, "HEADSIGNS", buf, "destinations vues", CP_CYAN_T);
        snprintf(buf, sizeof(buf), "%s", line->code);
        stat_card(top, 2 + (cw + gap) * 2, cw, "LINE", buf, line->libelle, CP_GREEN);
        snprintf(buf, sizeof(buf), "%d", alerts);
        stat_card(top, 2 + (cw + gap) * 3, cw, "ALERTS", buf, alerts ? "perturbations en cours" : "aucune alerte", alerts ? CP_ALERT_MED : CP_GREEN);
        top += 5;
    }

    {
        int y1 = top;
        int y2 = LINES - 3;
        int journeys_y2 = y2;

        if (cached_passages > 0 && y2 - y1 >= 12) journeys_y2 = y2 - 7;

        {
            int head_y = y1 + 1;
            int row_y = head_y + 2;

            panel_box(y1, 1, journeys_y2, COLS - 2, "Active Journeys", "positions non exposees par l'endpoint");
            attron(A_DIM);
            mvprintw(head_y, 3, "TRIP");
            mvprintw(head_y, 16, "HEADSIGN");
            attroff(A_DIM);

            for (int i = 0; i < g_nlive_vehicles && row_y + i < journeys_y2; i++) {
                if (i % 2) attron(A_DIM);
                mvhline(row_y + i, 2, ' ', COLS - 4);
                if (i % 2) attroff(A_DIM);
                attron(COLOR_PAIR(CP_ACCENT) | A_BOLD);
                mvprintw(row_y + i, 3, "%-10.10s", g_live_vehicles[i].current_stop[0] ? g_live_vehicles[i].current_stop : "--");
                attroff(COLOR_PAIR(CP_ACCENT) | A_BOLD);
                print_fit(row_y + i, 16, COLS - 20, g_live_vehicles[i].terminus[0] ? g_live_vehicles[i].terminus : "destination indisponible");
            }

            if (!g_nlive_vehicles) {
                attron(A_DIM);
                mvprintw(y1 + 4, 3, U_INFO" aucune course active retournee");
                mvprintw(y1 + 6, 3, "L'API Navitia expose ici des courses actives sans coordonnees cartographiques.");
                attroff(A_DIM);
            }
        }

        if (cached_passages > 0 && journeys_y2 < y2) {
            int py1 = journeys_y2 + 1;
            int py2 = y2;
            int head_y = py1 + 1;
            int row_y = head_y + 2;
            char meta[96];

            snprintf(meta, sizeof(meta), "%s", stop ? stop->libelle : "stop area");
            panel_box(py1, 1, py2, COLS - 2, "Live Departures", meta);
            attron(A_DIM);
            mvprintw(head_y, 3, "TIME");
            mvprintw(head_y, 12, "ETA");
            mvprintw(head_y, 22, "DIR");
            attroff(A_DIM);

            for (int i = 0; i < cached_passages && row_y + i < py2; i++) {
                char hr[8];
                char eta[16];
                int cp = g_live_passages[i].realtime ? CP_GREEN : CP_YELLOW;

                toulouse_passage_clock(&g_live_passages[i], hr, sizeof(hr));
                toulouse_waiting_eta(g_live_passages[i].waiting_time, eta, sizeof(eta));
                attron(COLOR_PAIR(cp) | A_BOLD);
                mvprintw(row_y + i, 3, "%5s", hr);
                attroff(COLOR_PAIR(cp) | A_BOLD);
                mvprintw(row_y + i, 12, "%6s", eta);
                print_fit(row_y + i, 22, COLS - 26, g_live_passages[i].destination);
            }
        } else if (!stop && y2 - y1 >= 12) {
            attron(A_DIM);
            mvprintw(y2 - 2, 3, "p puis Enter charge les arrets et les prochains passages de la ligne.");
            attroff(A_DIM);
        }
    }

    draw_toast_msg();
    {
        char ri[32];
        snprintf(ri, sizeof(ri), "%d act.", g_nlive_vehicles);
        draw_status(" q:back"U_MDOT"r:refresh"U_MDOT"a:alertes"U_MDOT"p:arrets"U_MDOT"t:theme", ri);
    }
}

/* ── search input ────────────────────────────────────────────────── */

static void do_search(void)
{
    int len=(int)strlen(g_search); curs_set(1); timeout(-1);
    for(;;){
        erase(); draw_lines();
        attron(COLOR_PAIR(CP_SEARCH)|A_BOLD); mvhline(LINES-2,0,' ',COLS);
        mvprintw(LINES-2,1," / %s",g_search); attroff(A_BOLD);
        printw("   (%d)",g_nfiltered); attroff(COLOR_PAIR(CP_SEARCH));
        move(LINES-2,3+len); refresh();
        int ch=getch(); if(ch=='\n'||ch==27) break;
        if((ch==KEY_BACKSPACE||ch==127||ch==8)&&len>0) g_search[--len]='\0';
        else if(len<(int)sizeof(g_search)-1&&ch>=32&&ch<127){g_search[len++]=(char)ch;g_search[len]='\0';}
        rebuild_filter(); g_cursor=0; g_scroll=0;
    }
    curs_set(0); timeout(1000);
}

static void do_stop_search(void)
{
    int len=(int)strlen(g_stop_search); curs_set(1); timeout(-1);
    for(;;){
        erase(); draw_stops();
        attron(COLOR_PAIR(CP_SEARCH)|A_BOLD); mvhline(LINES-2,0,' ',COLS);
        mvprintw(LINES-2,1," / %s",g_stop_search); attroff(A_BOLD);
        printw("   (%d)",g_nstop_filtered); attroff(COLOR_PAIR(CP_SEARCH));
        move(LINES-2,3+len); refresh();
        int ch=getch(); if(ch=='\n'||ch==27) break;
        if((ch==KEY_BACKSPACE||ch==127||ch==8)&&len>0) g_stop_search[--len]='\0';
        else if(len<(int)sizeof(g_stop_search)-1&&ch>=32&&ch<127){g_stop_search[len++]=(char)ch;g_stop_search[len]='\0';}
        rebuild_stop_filter(); g_cursor=0; g_scroll=0;
    }
    curs_set(0); timeout(1000);
}

static void do_toulouse_search(void)
{
    int len = (int)strlen(g_tls_search);
    curs_set(1);
    timeout(-1);
    for (;;) {
        erase();
        draw_toulouse();
        attron(COLOR_PAIR(CP_SEARCH) | A_BOLD);
        mvhline(LINES - 2, 0, ' ', COLS);
        mvprintw(LINES - 2, 1, " / %s", g_tls_search);
        attroff(A_BOLD);
        printw("   (%d)", g_ntls_filtered);
        attroff(COLOR_PAIR(CP_SEARCH));
        move(LINES - 2, 3 + len);
        refresh();
        int ch = getch();
        if (ch == '\n' || ch == 27) break;
        if ((ch == KEY_BACKSPACE || ch == 127 || ch == 8) && len > 0) g_tls_search[--len] = '\0';
        else if (len < (int)sizeof(g_tls_search) - 1 && ch >= 32 && ch < 127) {
            g_tls_search[len++] = (char)ch;
            g_tls_search[len] = '\0';
        }
        rebuild_toulouse_filter();
        g_tls_cursor = 0;
        g_tls_scroll = 0;
    }
    curs_set(0);
    timeout(1000);
}

static void do_toulouse_stop_search(void)
{
    int len = (int)strlen(g_tls_stop_search);
    curs_set(1);
    timeout(-1);
    for (;;) {
        erase();
        draw_toulouse_stops();
        attron(COLOR_PAIR(CP_SEARCH) | A_BOLD);
        mvhline(LINES - 2, 0, ' ', COLS);
        mvprintw(LINES - 2, 1, " / %s", g_tls_stop_search);
        attroff(A_BOLD);
        printw("   (%d)", g_ntls_stop_filtered);
        attroff(COLOR_PAIR(CP_SEARCH));
        move(LINES - 2, 3 + len);
        refresh();
        {
            int ch = getch();
            if (ch == '\n' || ch == 27) break;
            if ((ch == KEY_BACKSPACE || ch == 127 || ch == 8) && len > 0) g_tls_stop_search[--len] = '\0';
            else if (len < (int)sizeof(g_tls_stop_search) - 1 && ch >= 32 && ch < 127) {
                g_tls_stop_search[len++] = (char)ch;
                g_tls_stop_search[len] = '\0';
            }
        }
        rebuild_toulouse_stop_filter();
        g_tls_stop_cursor = 0;
        g_tls_stop_scroll = 0;
    }
    curs_set(0);
    timeout(1000);
}

static void do_idfm_search(void)
{
    int len = (int)strlen(g_live_search);
    curs_set(1);
    timeout(-1);
    for (;;) {
        erase();
        draw_idfm();
        attron(COLOR_PAIR(CP_SEARCH) | A_BOLD);
        mvhline(LINES - 2, 0, ' ', COLS);
        mvprintw(LINES - 2, 1, " / %s", g_live_search);
        attroff(A_BOLD);
        printw("   (%d)", g_nlive_filtered);
        attroff(COLOR_PAIR(CP_SEARCH));
        move(LINES - 2, 3 + len);
        refresh();
        {
            int ch = getch();
            if (ch == '\n' || ch == 27) break;
            if ((ch == KEY_BACKSPACE || ch == 127 || ch == 8) && len > 0) g_live_search[--len] = '\0';
            else if (len < (int)sizeof(g_live_search) - 1 && ch >= 32 && ch < 127) {
                g_live_search[len++] = (char)ch;
                g_live_search[len] = '\0';
            }
        }
        rebuild_idfm_filter();
        g_live_cursor = 0;
        g_live_scroll = 0;
    }
    curs_set(0);
    timeout(1000);
}

static void do_idfm_stop_search(void)
{
    int len = (int)strlen(g_live_stop_search);
    int submitted = 0;

    curs_set(1);
    timeout(-1);
    for (;;) {
        erase();
        draw_idfm_stops();
        attron(COLOR_PAIR(CP_SEARCH) | A_BOLD);
        mvhline(LINES - 2, 0, ' ', COLS);
        mvprintw(LINES - 2, 1, " / %s", g_live_stop_search);
        attroff(A_BOLD);
        printw("   (%d)", g_nlive_stop_filtered);
        attroff(COLOR_PAIR(CP_SEARCH));
        move(LINES - 2, 3 + len);
        refresh();
        {
            int ch = getch();
            if (ch == '\n') {
                submitted = 1;
                break;
            }
            if (ch == 27) break;
            if ((ch == KEY_BACKSPACE || ch == 127 || ch == 8) && len > 0) {
                g_live_stop_search[--len] = '\0';
                reset_idfm_stop_view(0);
            }
            else if (len < (int)sizeof(g_live_stop_search) - 1 && ch >= 32 && ch < 127) {
                g_live_stop_search[len++] = (char)ch;
                g_live_stop_search[len] = '\0';
                reset_idfm_stop_view(0);
            }
        }
    }
    curs_set(0);
    timeout(1000);
    if (!submitted) return;
    if (!g_live_stop_search[0]) {
        reset_idfm_stop_view(0);
        toast("Recherche arret effacee");
        return;
    }
    if (g_live_sel_line < 0) {
        toast("Selectionnez d'abord une ligne");
        return;
    }
    if (g_nlive_stops <= 0 && load_current_network_stops(NULL) < 0) return;
    g_nlive_stop_filtered = nvt_rebuild_toulouse_stop_filter(
        g_live_stops,
        g_nlive_stops,
        g_live_stop_search,
        g_live_stop_filtered,
        MAX_STOPS
    );
    g_live_stop_cursor = 0;
    g_live_stop_scroll = 0;
    if (g_nlive_stop_filtered > 0) toast("%d arrets trouves", g_nlive_stop_filtered);
    else toast("Aucun arret pour %s", g_live_stop_search);
}

static void do_atlas_search(void)
{
    int len=(int)strlen(g_atlas_search); curs_set(1); timeout(-1);
    for(;;){
        erase(); draw_atlas();
        attron(COLOR_PAIR(CP_SEARCH)|A_BOLD); mvhline(LINES-2,0,' ',COLS);
        mvprintw(LINES-2,1," / %s",g_atlas_search); attroff(A_BOLD);
        printw("   (%d)",g_natlas_filtered); attroff(COLOR_PAIR(CP_SEARCH));
        move(LINES-2,3+len); refresh();
        int ch=getch(); if(ch=='\n'||ch==27) break;
        if((ch==KEY_BACKSPACE||ch==127||ch==8)&&len>0) g_atlas_search[--len]='\0';
        else if(len<(int)sizeof(g_atlas_search)-1&&ch>=32&&ch<127){g_atlas_search[len++]=(char)ch;g_atlas_search[len]='\0';}
        g_atlas_focus_gid=0;
        rebuild_atlas_filter();
        g_atlas_cursor=0;
        g_atlas_scroll=0;
    }
    curs_set(0); timeout(1000);
}

static void do_itinerary_search(void)
{
    int len = (int)strlen(g_itinerary.search);

    curs_set(1);
    timeout(-1);
    for (;;) {
        erase();
        draw_itinerary();
        attron(COLOR_PAIR(CP_SEARCH) | A_BOLD);
        mvhline(LINES - 2, 0, ' ', COLS);
        mvprintw(LINES - 2, 1, " / %s", g_itinerary.search);
        attroff(A_BOLD);
        printw("   (%d)", g_itinerary.nfiltered);
        attroff(COLOR_PAIR(CP_SEARCH));
        move(LINES - 2, 3 + len);
        refresh();

        {
            int ch = getch();

            if (ch == '\n' || ch == 27) break;
            if ((ch == KEY_BACKSPACE || ch == 127 || ch == 8) && len > 0) g_itinerary.search[--len] = '\0';
            else if (len < (int)sizeof(g_itinerary.search) - 1 && ch >= 32 && ch < 127) {
                g_itinerary.search[len++] = (char)ch;
                g_itinerary.search[len] = '\0';
            }
        }

        nvt_itinerary_rebuild_filter(&g_itinerary);
        g_itinerary.cursor = 0;
        g_itinerary.scroll = 0;
    }
    curs_set(0);
    timeout(1000);
}

/* ── loading screen ──────────────────────────────────────────────── */

static void draw_load(int step,int tot,const char *name,int fr)
{
    static const char *logo[]={
        "  _   ___   _______ ",
        " | \\ | \\ \\ / /_   _|",
        " | .` |\\ V /  | |  ",
        " |_|\\_| \\_/   |_|  ",
    };
    const char *sp="|/-\\";
    int lh=4,lw=21,cy=LINES/2-5,cx=COLS/2-lw/2;
    if(cy<1)cy=1; if(cx<1)cx=1;
    int bw=lw+20,bh=lh+10,bx=COLS/2-bw/2,by=cy-2;
    if(by<0)by=0; if(bx<0)bx=0;
    erase(); rbox(by,bx,by+bh,bx+bw);

    attron(COLOR_PAIR(CP_ACCENT)|A_BOLD);
    for(int i=0;i<lh;i++) mvprintw(cy+i,cx,"%s",logo[i]);
    attroff(COLOR_PAIR(CP_ACCENT)|A_BOLD);

    attron(A_DIM); mvprintw(cy+lh+1,COLS/2-11,"Navigateur Transports"); attroff(A_DIM);
    attron(COLOR_PAIR(CP_SECTION)); mvprintw(cy+lh+2,COLS/2-3,"v%s",NVT_VERSION); attroff(COLOR_PAIR(CP_SECTION));

    int ly=cy+lh+4;
    attron(COLOR_PAIR(CP_ACCENT));
    mvprintw(ly,COLS/2-12," %c  %s"U_ELLIP"    ",sp[fr%4],name);
    attroff(COLOR_PAIR(CP_ACCENT));

    int pw=bw-8,px=bx+4,pct=step*100/tot,apct=pct+(fr%3)*(100/tot/3);
    if(apct>100)apct=100;
    progress_bar(ly+1,px,pw,apct);
    mvprintw(ly+1,px+pw+2,"%3d%%",pct);

    /* step dots */
    int dy=ly+3,dx=COLS/2-tot;
    for(int i=0;i<tot;i++){
        if(i<step){attron(COLOR_PAIR(CP_GREEN)|A_BOLD);mvaddstr(dy,dx+i*3,U_BULLET);attroff(COLOR_PAIR(CP_GREEN)|A_BOLD);}
        else if(i==step){attron(COLOR_PAIR(CP_ACCENT)|A_BOLD);mvaddstr(dy,dx+i*3,U_BULLET);attroff(COLOR_PAIR(CP_ACCENT)|A_BOLD);}
        else{attron(A_DIM);mvaddstr(dy,dx+i*3,U_BULLETO);attroff(A_DIM);}
    }
    refresh();
}

static const char *network_summary(NvtNetwork network)
{
    switch (network) {
    case NET_TLS:
        return "Metro, tram, bus et alertes Tisseo";
    case NET_IDFM:
        return "Metro, RER, tram, train et bus IDFM";
    case NET_SNCF:
        return "Train, TER, cars SNCF et departs Navitia";
    case NET_BDX:
    default:
        return "Tram, bus, passages et vehicules Bordeaux";
    }
}

static const char *network_menu_state(NvtNetwork network)
{
    if (network == g_network && g_network_loaded[network]) return "actif";
    if (g_network_loaded[network]) return "pret";
    return "a charger";
}

static void animate_load_step(int step, int total, const char *name, int base_frame)
{
    for (int f = 0; f < 4; f++) {
        draw_load(step, total, name, base_frame + f);
        napms(60);
    }
}

static int ensure_network_loaded(NvtNetwork network)
{
    char err[128];

    if (network < 0 || network >= NET_COUNT) return -1;
    if (g_network_loaded[network]) return 0;

    switch (network) {
    case NET_BDX:
        animate_load_step(0, 3, "Arrets Bordeaux", 0);
        if (nvt_data_init_bordeaux(&g_app, 2, err, sizeof(err)) < 0) {
            toast("%s", err);
            g_nstop_groups = 0;
            g_nstop_filtered = 0;
            return -1;
        }
        animate_load_step(1, 3, "Lignes Bordeaux", 4);
        if (nvt_data_refresh_bordeaux_overview(&g_app, 2, err, sizeof(err)) < 0) {
            toast("%s", err);
            g_nlines = 0;
            g_nfiltered = 0;
            g_natlas_filtered = 0;
            return -1;
        }
        animate_load_step(2, 3, "Alertes Bordeaux", 8);
        if (nvt_data_refresh_bordeaux_alerts(&g_app, 2, err, sizeof(err)) < 0) {
            toast("%s", err);
            g_nalerts = 0;
        }
        break;
    case NET_TLS:
        animate_load_step(0, 2, "Reseau Toulouse", 0);
        if (nvt_data_refresh_toulouse_overview(&g_app, 2, err, sizeof(err)) < 0) {
            toast("%s", err);
            memset(&g_tls_snapshot, 0, sizeof(g_tls_snapshot));
            g_ntls_lines = 0;
            g_ntls_stops = 0;
            g_ntls_filtered = 0;
            g_ntls_stop_filtered = 0;
            return -1;
        }
        animate_load_step(1, 2, "Alertes Toulouse", 4);
        if (nvt_data_refresh_toulouse_alerts(&g_app, 2, err, sizeof(err)) < 0) {
            toast("%s", err);
            g_ntls_alerts = 0;
        }
        break;
    case NET_IDFM:
        animate_load_step(0, 2, "Reseau IDFM", 0);
        if (nvt_data_refresh_idfm_overview(&g_app, 2, err, sizeof(err)) < 0) {
            toast("%s", err);
            memset(&g_idf_snapshot, 0, sizeof(g_idf_snapshot));
            g_nidf_lines = 0;
            g_nidf_filtered = 0;
            return -1;
        }
        animate_load_step(1, 2, "Alertes IDFM", 4);
        if (nvt_data_refresh_idfm_alerts(&g_app, 2, err, sizeof(err)) < 0) {
            toast("%s", err);
            g_nidf_alerts = 0;
        }
        break;
    case NET_SNCF:
        animate_load_step(0, 2, "Reseau SNCF", 0);
        if (nvt_data_refresh_sncf_overview(&g_app, 2, err, sizeof(err)) < 0) {
            toast("%s", err);
            memset(&g_app.sncf.snapshot, 0, sizeof(g_app.sncf.snapshot));
            g_app.sncf.nlines = 0;
            g_app.sncf.nfiltered = 0;
            return -1;
        }
        animate_load_step(1, 2, "Alertes SNCF", 4);
        if (nvt_data_refresh_sncf_alerts(&g_app, 2, err, sizeof(err)) < 0) {
            toast("%s", err);
            g_app.sncf.nalerts = 0;
        }
        break;
    case NET_COUNT:
        return -1;
    }

    g_network_loaded[network] = 1;
    draw_load(1, 1, "Pret !", 0);
    napms(180);
    return 0;
}

static void draw_network_menu(const char *title, const char *subtitle, int cursor, int allow_cancel)
{
    int h = 19;
    int w = 64;
    int y0 = (LINES - h) / 2;
    int x0 = (COLS - w) / 2;

    if (y0 < 0) y0 = 0;
    if (x0 < 0) x0 = 0;

    erase();
    for (int y = y0; y <= y0 + h; y++) mvhline(y, x0, ' ', w + 1);
    rbox(y0, x0, y0 + h, x0 + w);

    attron(COLOR_PAIR(CP_BORDER) | A_BOLD);
    mvprintw(y0, x0 + (w - (int)strlen(title) - 2) / 2, " %s ", title);
    attroff(COLOR_PAIR(CP_BORDER) | A_BOLD);

    if (subtitle && subtitle[0]) {
        attron(A_DIM);
        mvprintw(y0 + 2, x0 + 4, "%s", subtitle);
        attroff(A_DIM);
    }

    for (int i = 0; i < NET_COUNT; i++) {
        int row = y0 + 4 + i * 3;
        int selected = (i == cursor);
        int cp = selected ? CP_SEL : CP_HELP_KEY;

        if (selected) {
            fill_span(row, x0 + 2, x0 + w - 2, CP_SEL, 0);
            fill_span(row + 1, x0 + 2, x0 + w - 2, CP_SEL, 0);
        }

        attron(COLOR_PAIR(cp) | A_BOLD);
        mvprintw(row, x0 + 4, "%d. %-12s", i + 1, nvt_network_name((NvtNetwork)i));
        attroff(COLOR_PAIR(cp) | A_BOLD);

        if (selected) attron(COLOR_PAIR(CP_SEL));
        else attron(A_DIM);
        mvprintw(row, x0 + w - 18, "%-14s", network_menu_state((NvtNetwork)i));
        mvprintw(row + 1, x0 + 6, "%s", network_summary((NvtNetwork)i));
        if (selected) attroff(COLOR_PAIR(CP_SEL));
        else attroff(A_DIM);
    }

    attron(A_DIM);
    if (allow_cancel) mvprintw(y0 + h - 1, x0 + 4, "j/k ou fleches, Entree pour choisir, Esc pour annuler");
    else mvprintw(y0 + h - 1, x0 + 4, "j/k ou fleches, Entree pour choisir");
    attroff(A_DIM);
}

static int prompt_network_menu(const char *title, const char *subtitle, NvtNetwork initial, int allow_cancel)
{
    int cursor = (initial >= 0 && initial < NET_COUNT) ? initial : 0;

    timeout(-1);
    for (;;) {
        draw_network_menu(title, subtitle, cursor, allow_cancel);
        refresh();

        switch (getch()) {
        case 'j':
        case KEY_DOWN:
            cursor = (cursor + 1) % NET_COUNT;
            break;
        case 'k':
        case KEY_UP:
            cursor = (cursor + NET_COUNT - 1) % NET_COUNT;
            break;
        case '1':
            timeout(1000);
            return NET_BDX;
        case '2':
            timeout(1000);
            return NET_TLS;
        case '3':
            timeout(1000);
            return NET_IDFM;
        case '4':
            timeout(1000);
            return NET_SNCF;
        case '\n':
        case KEY_ENTER:
            timeout(1000);
            return cursor;
        case 27:
        case 'q':
            if (allow_cancel) {
                timeout(1000);
                return -1;
            }
            break;
        }
    }
}

static void open_network_menu(int allow_cancel)
{
    int selected = prompt_network_menu(
        allow_cancel ? "Changer de reseau" : "Choisir un reseau",
        allow_cancel ? "Les reseaux sont charges uniquement a la demande." : "Chargement initial limite au reseau choisi.",
        g_network,
        allow_cancel
    );

    if (selected < 0) return;
    if (ensure_network_loaded((NvtNetwork)selected) == 0) {
        switch_network((NvtNetwork)selected);
        toast("Reseau %s", network_name());
    } else if ((NvtNetwork)selected != g_network) {
        switch_network((NvtNetwork)selected);
    }
}

/* ── main ────────────────────────────────────────────────────────── */

int main(void)
{
    setlocale(LC_ALL,""); setlocale(LC_NUMERIC,"C");
    nvt_app_init(&g_app);
    nvt_itinerary_reset(&g_itinerary);
    api_init();
    initscr(); cbreak(); noecho(); keypad(stdscr,TRUE); curs_set(0); timeout(1000);
    init_colors();

    open_network_menu(0);

    time_t lvr=0, lar=time(NULL);

    for(;;){
        erase(); time_t now=time(NULL);
        switch(g_screen){
        case SCR_LINES:
            if(g_network==NET_TLS) draw_toulouse();
            else if(g_network==NET_IDFM || g_network==NET_SNCF) draw_idfm();
            else draw_lines();
            break;
        case SCR_VEHICLES:
            if(g_network==NET_TLS) draw_toulouse_vehicles();
            else if(g_network==NET_IDFM || g_network==NET_SNCF) draw_idfm_vehicles();
            else{
                if(now-lvr>=VEHICLE_REFRESH_SEC){
                    char fetch_err[128];
                    if(nvt_data_refresh_bordeaux_vehicles(&g_app, 2, fetch_err, sizeof(fetch_err)) < 0){
                        nvt_app_toast_error(&g_app, now, "%s", fetch_err);
                    } else if(g_nvehicles>0) toast("%d vehicules",g_nvehicles);
                    lvr=now;
                }
                if(now-lar>=ALERT_REFRESH_SEC){
                    char fetch_err[128];
                    if(nvt_data_refresh_bordeaux_alerts(&g_app, 2, fetch_err, sizeof(fetch_err)) < 0){
                        nvt_app_toast_error(&g_app, now, "%s", fetch_err);
                    }
                    lar=now;
                }
                draw_vehicles(lvr);
            }
            break;
        case SCR_ALERTS:
            if(g_network==NET_TLS) draw_toulouse_alerts();
            else if(g_network==NET_IDFM || g_network==NET_SNCF) draw_idfm_alerts();
            else draw_alerts();
            break;
        case SCR_STOP_SEARCH:
            if(g_network==NET_TLS) draw_toulouse_stops();
            else if(g_network==NET_IDFM || g_network==NET_SNCF) draw_idfm_stops();
            else draw_stops();
            break;
        case SCR_PASSAGES:
            if(g_network==NET_TLS) draw_toulouse_passages();
            else if(g_network==NET_IDFM || g_network==NET_SNCF) draw_idfm_passages();
            else draw_passages();
            break;
        case SCR_ITINERARY:
            draw_itinerary();
            break;
        case SCR_ATLAS: draw_atlas(); break;
        }
        if(g_show_help) draw_help();
        refresh();
        int ch=getch(); if(ch==ERR) continue;
        if(g_show_help){g_show_help=0;continue;}
        if(ch=='?'||ch==KEY_F(1)){g_show_help=1;continue;}
        if(ch=='t'&&g_256){g_theme=(g_theme+1)%N_THEMES;apply_theme();toast("Theme: %s",themes[g_theme].name);continue;}
        if(ch=='n'){open_network_menu(1);continue;}
        if(ch=='6'||ch=='i'){open_current_network_itinerary(g_screen==SCR_ITINERARY);continue;}

        int mr=LINES-8,hp=mr/2; if(mr<1)mr=1; if(hp<1)hp=1;

        switch(g_screen){
        case SCR_LINES:
            if(g_network==NET_TLS){
                switch(ch){
                case 'q': goto quit;
                case 'j': case KEY_DOWN: if(g_tls_cursor<g_ntls_filtered-1) g_tls_cursor++; break;
                case 'k': case KEY_UP: if(g_tls_cursor>0) g_tls_cursor--; break;
                case KEY_NPAGE: g_tls_cursor+=mr; if(g_tls_cursor>=g_ntls_filtered) g_tls_cursor=g_ntls_filtered>0?g_ntls_filtered-1:0; break;
                case KEY_PPAGE: g_tls_cursor-=mr; if(g_tls_cursor<0) g_tls_cursor=0; break;
                case 4: g_tls_cursor+=hp; if(g_tls_cursor>=g_ntls_filtered) g_tls_cursor=g_ntls_filtered>0?g_ntls_filtered-1:0; break;
                case 21: g_tls_cursor-=hp; if(g_tls_cursor<0) g_tls_cursor=0; break;
                case 'g': case KEY_HOME: g_tls_cursor=0;g_tls_scroll=0;break;
                case 'G': case KEY_END: g_tls_cursor=g_ntls_filtered>0?g_ntls_filtered-1:0;break;
                case '\n': case KEY_ENTER:
                    if(g_ntls_filtered>0){
                        g_tls_sel_line=g_tls_filtered[g_tls_cursor];
                        g_vehicle_zoom=0;
                        reset_toulouse_line_route();
                        g_screen=SCR_VEHICLES;
                        load_current_network_vehicles("%d vehicules");
                    }
                    break;
                case 'a': case '3': g_screen=SCR_ALERTS; g_alert_scroll=0; break;
                case '/': memset(g_tls_search,0,sizeof(g_tls_search)); do_toulouse_search(); break;
                case 27: if(g_tls_search[0]){memset(g_tls_search,0,sizeof(g_tls_search));rebuild_toulouse_filter();g_tls_cursor=0;g_tls_scroll=0;toast("Filtre efface");} break;
                case 'p': case '4': g_screen=SCR_STOP_SEARCH; g_tls_stop_cursor=0; g_tls_stop_scroll=0; break;
                case 'r': case KEY_F(5): refresh_current_network_overview("Flux Toulouse recharge"); break;
                }
            } else if(g_network==NET_IDFM || g_network==NET_SNCF){
                switch(ch){
                case 'q': goto quit;
                case 'j': case KEY_DOWN: if(g_live_cursor<g_nlive_filtered-1) g_live_cursor++; break;
                case 'k': case KEY_UP: if(g_live_cursor>0) g_live_cursor--; break;
                case KEY_NPAGE: g_live_cursor+=mr; if(g_live_cursor>=g_nlive_filtered) g_live_cursor=g_nlive_filtered>0?g_nlive_filtered-1:0; break;
                case KEY_PPAGE: g_live_cursor-=mr; if(g_live_cursor<0) g_live_cursor=0; break;
                case 4: g_live_cursor+=hp; if(g_live_cursor>=g_nlive_filtered) g_live_cursor=g_nlive_filtered>0?g_nlive_filtered-1:0; break;
                case 21: g_live_cursor-=hp; if(g_live_cursor<0) g_live_cursor=0; break;
                case 'g': case KEY_HOME: g_live_cursor=0; g_live_scroll=0; break;
                case 'G': case KEY_END: g_live_cursor=g_nlive_filtered>0?g_nlive_filtered-1:0; break;
                case '\n': case KEY_ENTER:
                    if(g_nlive_filtered>0){
                        g_live_sel_line=g_live_filtered[g_live_cursor];
                        g_screen=SCR_VEHICLES;
                        load_current_network_vehicles(live_network_is_sncf() ? "%d trajets" : "%d courses");
                    }
                    break;
                case 'a': case '3': g_screen=SCR_ALERTS; g_alert_scroll=0; break;
                case '/': memset(g_live_search,0,sizeof(g_live_search)); do_idfm_search(); break;
                case 27:
                    if(g_live_search[0]){
                        memset(g_live_search,0,sizeof(g_live_search));
                        rebuild_idfm_filter();
                        g_live_cursor=0;
                        g_live_scroll=0;
                        toast("Filtre efface");
                    }
                    break;
                case 'p': case '4':
                    if(g_nlive_filtered>0){
                        open_idfm_stop_search(g_live_filtered[g_live_cursor], 1);
                    }
                    break;
                case 'r': case KEY_F(5): refresh_current_network_overview(live_network_is_sncf() ? "Flux SNCF recharge" : "Flux IDFM recharge"); break;
                }
            } else {
                switch(ch){
                case 'q': goto quit;
                case 'j': case KEY_DOWN: if(g_cursor<g_nfiltered-1)g_cursor++; break;
                case 'k': case KEY_UP: if(g_cursor>0)g_cursor--; break;
                case KEY_NPAGE: g_cursor+=mr;if(g_cursor>=g_nfiltered)g_cursor=g_nfiltered-1;if(g_cursor<0)g_cursor=0;break;
                case KEY_PPAGE: g_cursor-=mr;if(g_cursor<0)g_cursor=0;break;
                case 4: g_cursor+=hp;if(g_cursor>=g_nfiltered)g_cursor=g_nfiltered-1;if(g_cursor<0)g_cursor=0;break;
                case 21: g_cursor-=hp;if(g_cursor<0)g_cursor=0;break;
                case 'g': case KEY_HOME: g_cursor=0;g_scroll=0;break;
                case 'G': case KEY_END: g_cursor=g_nfiltered>0?g_nfiltered-1:0;break;
                case '\n': case KEY_ENTER: if(g_nfiltered>0){g_sel_line=g_filtered[g_cursor];g_vehicle_zoom=0;reset_vehicle_detail_map();g_screen=SCR_VEHICLES;lvr=0;}break;
                case 'a': case '3': g_screen=SCR_ALERTS;g_alert_scroll=0;break;
                case '/': memset(g_search,0,sizeof(g_search));do_search();break;
                case 27: if(g_search[0]){memset(g_search,0,sizeof(g_search));rebuild_filter();g_cursor=0;g_scroll=0;toast("Filtre efface");}break;
                case 'p': case '4': g_screen=SCR_STOP_SEARCH;g_cursor=0;g_scroll=0;memset(g_stop_search,0,sizeof(g_stop_search));rebuild_stop_filter();break;
                case 'r': case KEY_F(5): refresh_current_network_overview("Donnees mises a jour"); break;
                }
            }
            break;
        case SCR_VEHICLES:
            if(g_network==NET_TLS){
                switch(ch){
                case 'q': case 27: case '1': g_screen=SCR_LINES;break;
                case 'a': case '3': g_screen=SCR_ALERTS;g_alert_scroll=0;break;
                case 'p': case '4': g_screen=SCR_STOP_SEARCH;g_tls_stop_cursor=0;g_tls_stop_scroll=0;break;
                case 'r': case KEY_F(5):
                    reset_toulouse_line_route();
                    load_current_network_vehicles("%d vehicules");
                    refresh_current_network_alerts(NULL);
                    break;
                case '+': case '=': if(g_vehicle_zoom<MAX_VEHICLE_ZOOM) g_vehicle_zoom++; break;
                case '-': case '_': if(g_vehicle_zoom>0) g_vehicle_zoom--; break;
                case '0': g_vehicle_zoom=0; break;
                }
            } else if(g_network==NET_IDFM || g_network==NET_SNCF){
                switch(ch){
                case 'q': case 27: case '1': g_screen=SCR_LINES; break;
                case 'a': case '3': g_screen=SCR_ALERTS; g_alert_scroll=0; break;
                case 'p': case '4':
                    open_idfm_stop_search(g_live_sel_line, 1);
                    break;
                case 'r': case KEY_F(5):
                    load_current_network_vehicles(live_network_is_sncf() ? "%d trajets" : "%d courses");
                    refresh_current_network_alerts(NULL);
                    break;
                }
            } else {
                switch(ch){
                case 'q': case 27: case '1': g_screen=SCR_LINES;break;
                case 'r': case KEY_F(5):
                    reset_line_route();
                    reset_vehicle_detail_map();
                    if (load_current_network_vehicles("%d vehicules") >= 0) lvr=now;
                    refresh_current_network_alerts(NULL);
                    lar=now;
                    break;
                case '+': case '=': if(g_vehicle_zoom<MAX_VEHICLE_ZOOM) g_vehicle_zoom++; break;
                case '-': case '_': if(g_vehicle_zoom>0) g_vehicle_zoom--; break;
                case '0': g_vehicle_zoom=0; break;
                case 'a': case '3': g_screen=SCR_ALERTS;g_alert_scroll=0;break;
                case 'p': case '4': g_screen=SCR_STOP_SEARCH;g_cursor=0;g_scroll=0;memset(g_stop_search,0,sizeof(g_stop_search));rebuild_stop_filter();break;
                }
            }
            break;
        case SCR_ALERTS: switch(ch){
            case 'q': case 27: case '1': g_screen=SCR_LINES;break;
            case 'j': case KEY_DOWN: if(g_alert_scroll<g_alert_total_h-1)g_alert_scroll++;break;
            case 'k': case KEY_UP: if(g_alert_scroll>0)g_alert_scroll--;break;
            case KEY_NPAGE: g_alert_scroll+=mr;if(g_alert_scroll>=g_alert_total_h)g_alert_scroll=g_alert_total_h>0?g_alert_total_h-1:0;break;
            case KEY_PPAGE: g_alert_scroll-=mr;if(g_alert_scroll<0)g_alert_scroll=0;break;
            case 4: g_alert_scroll+=hp;if(g_alert_scroll>=g_alert_total_h)g_alert_scroll=g_alert_total_h>0?g_alert_total_h-1:0;break;
            case 21: g_alert_scroll-=hp;if(g_alert_scroll<0)g_alert_scroll=0;break;
            case 'g': case KEY_HOME: g_alert_scroll=0;break;
            case 'G': case KEY_END: g_alert_scroll=g_alert_total_h>0?g_alert_total_h-1:0;break;
            case 'r': case KEY_F(5):
                refresh_current_network_alerts(
                    g_network==NET_TLS ? "%d alertes Toulouse"
                    : (g_network==NET_IDFM ? "%d alertes IDFM"
                       : (g_network==NET_SNCF ? "%d alertes SNCF" : "%d alertes"))
                );
                lar=time(NULL);
                break;
            case 'p': case '4':
                if(g_network==NET_TLS){g_screen=SCR_STOP_SEARCH;g_tls_stop_cursor=0;g_tls_stop_scroll=0;}
                else if(g_network==NET_IDFM || g_network==NET_SNCF){
                    if(g_live_sel_line < 0 && g_nlive_filtered > 0) g_live_sel_line = g_live_filtered[g_live_cursor];
                    open_idfm_stop_search(g_live_sel_line, 1);
                }
                else {g_screen=SCR_STOP_SEARCH;g_cursor=0;g_scroll=0;memset(g_stop_search,0,sizeof(g_stop_search));rebuild_stop_filter();}
                break;
            } break;
        case SCR_STOP_SEARCH:
            if(g_network==NET_TLS){
                switch(ch){
                case 'q': case 27: g_screen=SCR_LINES;g_tls_stop_cursor=0;g_tls_stop_scroll=0;break;
                case 'j': case KEY_DOWN: if(g_tls_stop_cursor<g_ntls_stop_filtered-1)g_tls_stop_cursor++;break;
                case 'k': case KEY_UP: if(g_tls_stop_cursor>0)g_tls_stop_cursor--;break;
                case KEY_NPAGE: g_tls_stop_cursor+=mr;if(g_tls_stop_cursor>=g_ntls_stop_filtered)g_tls_stop_cursor=g_ntls_stop_filtered>0?g_ntls_stop_filtered-1:0;break;
                case KEY_PPAGE: g_tls_stop_cursor-=mr;if(g_tls_stop_cursor<0)g_tls_stop_cursor=0;break;
                case 4: g_tls_stop_cursor+=hp;if(g_tls_stop_cursor>=g_ntls_stop_filtered)g_tls_stop_cursor=g_ntls_stop_filtered>0?g_ntls_stop_filtered-1:0;break;
                case 21: g_tls_stop_cursor-=hp;if(g_tls_stop_cursor<0)g_tls_stop_cursor=0;break;
                case 'g': case KEY_HOME: g_tls_stop_cursor=0;g_tls_stop_scroll=0;break;
                case 'G': case KEY_END: g_tls_stop_cursor=g_ntls_stop_filtered>0?g_ntls_stop_filtered-1:0;break;
                case '\n': case KEY_ENTER: if(g_ntls_stop_filtered>0){g_tls_sel_stop=g_tls_stop_filtered[g_tls_stop_cursor];g_screen=SCR_PASSAGES;load_current_network_passages("%d passages");}break;
                case '/': memset(g_tls_stop_search,0,sizeof(g_tls_stop_search));do_toulouse_stop_search();break;
                case '1': g_screen=SCR_LINES;g_tls_stop_cursor=0;g_tls_stop_scroll=0;break;
                case '3': case 'a': g_screen=SCR_ALERTS;g_alert_scroll=0;break;
                }
            } else if(g_network==NET_IDFM || g_network==NET_SNCF){
                switch(ch){
                case 'q': case 27: g_screen=SCR_LINES; g_live_stop_cursor=0; g_live_stop_scroll=0; break;
                case 'j': case KEY_DOWN: if(g_live_stop_cursor<g_nlive_stop_filtered-1) g_live_stop_cursor++; break;
                case 'k': case KEY_UP: if(g_live_stop_cursor>0) g_live_stop_cursor--; break;
                case KEY_NPAGE: g_live_stop_cursor+=mr; if(g_live_stop_cursor>=g_nlive_stop_filtered) g_live_stop_cursor=g_nlive_stop_filtered>0?g_nlive_stop_filtered-1:0; break;
                case KEY_PPAGE: g_live_stop_cursor-=mr; if(g_live_stop_cursor<0) g_live_stop_cursor=0; break;
                case 4: g_live_stop_cursor+=hp; if(g_live_stop_cursor>=g_nlive_stop_filtered) g_live_stop_cursor=g_nlive_stop_filtered>0?g_nlive_stop_filtered-1:0; break;
                case 21: g_live_stop_cursor-=hp; if(g_live_stop_cursor<0) g_live_stop_cursor=0; break;
                case 'g': case KEY_HOME: g_live_stop_cursor=0; g_live_stop_scroll=0; break;
                case 'G': case KEY_END: g_live_stop_cursor=g_nlive_stop_filtered>0?g_nlive_stop_filtered-1:0; break;
                case '\n': case KEY_ENTER:
                    if(g_nlive_stop_filtered>0){
                        g_live_sel_stop=g_live_stop_filtered[g_live_stop_cursor];
                        g_screen=SCR_PASSAGES;
                        load_current_network_passages("%d departs");
                    }
                    break;
                case '/': do_idfm_stop_search(); break;
                case '1': g_screen=SCR_LINES; g_live_stop_cursor=0; g_live_stop_scroll=0; break;
                case '3': case 'a': g_screen=SCR_ALERTS; g_alert_scroll=0; break;
                }
            } else {
                switch(ch){
                case 'q': case 27: g_screen=SCR_LINES;g_cursor=0;g_scroll=0;break;
                case 'j': case KEY_DOWN: if(g_cursor<g_nstop_filtered-1)g_cursor++;break;
                case 'k': case KEY_UP: if(g_cursor>0)g_cursor--;break;
                case KEY_NPAGE: g_cursor+=mr;if(g_cursor>=g_nstop_filtered)g_cursor=g_nstop_filtered-1;if(g_cursor<0)g_cursor=0;break;
                case KEY_PPAGE: g_cursor-=mr;if(g_cursor<0)g_cursor=0;break;
                case 4: g_cursor+=hp;if(g_cursor>=g_nstop_filtered)g_cursor=g_nstop_filtered-1;if(g_cursor<0)g_cursor=0;break;
                case 21: g_cursor-=hp;if(g_cursor<0)g_cursor=0;break;
                case 'g': case KEY_HOME: g_cursor=0;g_scroll=0;break;
                case 'G': case KEY_END: g_cursor=g_nstop_filtered>0?g_nstop_filtered-1:0;break;
                case '\n': case KEY_ENTER: if(g_nstop_filtered>0){g_sel_stop_group=g_stop_filtered[g_cursor];g_screen=SCR_PASSAGES;load_current_network_passages("%d passages");}break;
                case '/': memset(g_stop_search,0,sizeof(g_stop_search));do_stop_search();break;
                case '1': g_screen=SCR_LINES;g_cursor=0;g_scroll=0;break;
                case '3': case 'a': g_screen=SCR_ALERTS;g_alert_scroll=0;break;
                }
            }
            break;
        case SCR_PASSAGES:
            if(g_network==NET_TLS){
                switch(ch){
                case 'q': case 27: g_screen=SCR_STOP_SEARCH;g_tls_stop_cursor=0;g_tls_stop_scroll=0;break;
                case 'r': case KEY_F(5): load_current_network_passages("%d passages");break;
                case '/': g_screen=SCR_STOP_SEARCH;g_tls_stop_cursor=0;g_tls_stop_scroll=0;break;
                case '1': g_screen=SCR_LINES;g_tls_stop_cursor=0;g_tls_stop_scroll=0;break;
                case '3': case 'a': g_screen=SCR_ALERTS;g_alert_scroll=0;break;
                case '4': case 'p': g_screen=SCR_STOP_SEARCH;g_tls_stop_cursor=0;g_tls_stop_scroll=0;break;
                }
            } else if(g_network==NET_IDFM || g_network==NET_SNCF){
                switch(ch){
                case 'q': case 27: g_screen=SCR_STOP_SEARCH; g_live_stop_cursor=0; g_live_stop_scroll=0; break;
                case 'r': case KEY_F(5): load_current_network_passages("%d departs"); break;
                case '/': g_screen=SCR_STOP_SEARCH; g_live_stop_cursor=0; g_live_stop_scroll=0; break;
                case '1': g_screen=SCR_LINES; g_live_stop_cursor=0; g_live_stop_scroll=0; break;
                case '3': case 'a': g_screen=SCR_ALERTS; g_alert_scroll=0; break;
                case '4': case 'p': g_screen=SCR_STOP_SEARCH; g_live_stop_cursor=0; g_live_stop_scroll=0; break;
                }
            } else {
                switch(ch){
                case 'q': case 27: g_screen=SCR_STOP_SEARCH;g_cursor=0;g_scroll=0;break;
                case 'r': case KEY_F(5): load_current_network_passages("%d passages");break;
                case '/': g_screen=SCR_STOP_SEARCH;g_cursor=0;g_scroll=0;memset(g_stop_search,0,sizeof(g_stop_search));rebuild_stop_filter();break;
                case '1': g_screen=SCR_LINES;g_cursor=0;g_scroll=0;break;
                case '3': case 'a': g_screen=SCR_ALERTS;g_alert_scroll=0;break;
                case '4': case 'p': g_screen=SCR_STOP_SEARCH;g_cursor=0;g_scroll=0;memset(g_stop_search,0,sizeof(g_stop_search));rebuild_stop_filter();break;
                }
            }
            break;
        case SCR_ITINERARY:
            switch(ch){
            case 'q': case 27: case '1': g_screen=SCR_LINES; break;
            case 'j': case KEY_DOWN: if(g_itinerary.cursor<g_itinerary.nfiltered-1) g_itinerary.cursor++; break;
            case 'k': case KEY_UP: if(g_itinerary.cursor>0) g_itinerary.cursor--; break;
            case KEY_NPAGE: g_itinerary.cursor+=mr; if(g_itinerary.cursor>=g_itinerary.nfiltered) g_itinerary.cursor=g_itinerary.nfiltered>0?g_itinerary.nfiltered-1:0; break;
            case KEY_PPAGE: g_itinerary.cursor-=mr; if(g_itinerary.cursor<0) g_itinerary.cursor=0; break;
            case 4: g_itinerary.cursor+=hp; if(g_itinerary.cursor>=g_itinerary.nfiltered) g_itinerary.cursor=g_itinerary.nfiltered>0?g_itinerary.nfiltered-1:0; break;
            case 21: g_itinerary.cursor-=hp; if(g_itinerary.cursor<0) g_itinerary.cursor=0; break;
            case 'g': case KEY_HOME: g_itinerary.cursor=0; g_itinerary.scroll=0; break;
            case 'G': case KEY_END: g_itinerary.cursor=g_itinerary.nfiltered>0?g_itinerary.nfiltered-1:0; break;
            case 'o':
                if(g_itinerary.nfiltered>0) g_itinerary.origin=g_itinerary.filtered[g_itinerary.cursor];
                break;
            case 'd':
                if(g_itinerary.nfiltered>0) g_itinerary.destination=g_itinerary.filtered[g_itinerary.cursor];
                break;
            case 'x':
                if(g_itinerary.origin>=0&&g_itinerary.destination>=0) swap_itinerary_endpoints();
                break;
            case '/':
                memset(g_itinerary.search,0,sizeof(g_itinerary.search));
                do_itinerary_search();
                break;
            case 'r': case KEY_F(5):
                open_current_network_itinerary(1);
                break;
            }
            break;
        case SCR_ATLAS: switch(ch){
            case 'q': case 27: case '1': g_screen=SCR_LINES;break;
            case 'j': case KEY_DOWN: if(g_atlas_cursor<g_natlas_filtered-1)g_atlas_cursor++;break;
            case 'k': case KEY_UP: if(g_atlas_cursor>0)g_atlas_cursor--;break;
            case KEY_NPAGE: g_atlas_cursor+=mr;if(g_atlas_cursor>=g_natlas_filtered)g_atlas_cursor=g_natlas_filtered>0?g_natlas_filtered-1:0;break;
            case KEY_PPAGE: g_atlas_cursor-=mr;if(g_atlas_cursor<0)g_atlas_cursor=0;break;
            case 4: g_atlas_cursor+=hp;if(g_atlas_cursor>=g_natlas_filtered)g_atlas_cursor=g_natlas_filtered>0?g_natlas_filtered-1:0;break;
            case 21: g_atlas_cursor-=hp;if(g_atlas_cursor<0)g_atlas_cursor=0;break;
            case 'g': case KEY_HOME: g_atlas_cursor=0;g_atlas_scroll=0;break;
            case 'G': case KEY_END: g_atlas_cursor=g_natlas_filtered>0?g_natlas_filtered-1:0;break;
            case '\n': case KEY_ENTER:
                if(g_natlas_filtered>0){
                    int idx=g_atlas_filtered[g_atlas_cursor];
                    g_atlas_focus_gid=g_atlas_focus_gid==g_lines[idx].gid?0:g_lines[idx].gid;
                }
                break;
            case '/': memset(g_atlas_search,0,sizeof(g_atlas_search));do_atlas_search();break;
            case '3': case 'a': g_screen=SCR_ALERTS;g_alert_scroll=0;break;
            case '4': case 'p': g_screen=SCR_STOP_SEARCH;g_cursor=0;g_scroll=0;break;
            case 'r': case KEY_F(5):
                reset_metro_map_cache();
                reset_atlas_map();
                reset_atlas_routes();
                toast("Atlas recharge");
                break;
            case 'm': break;
            } break;
        }
    }
quit:
    endwin(); course_cache_free(&g_course_cache); stopmap_free(&g_stops); api_cleanup();
    return 0;
}
