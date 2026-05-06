#include "interpolated_positions.h"

#include <string.h>
#include <stdio.h>
#include <ctype.h>

/* Parse "HH:MM:SS" / "HH:MM" / "MM" into total seconds. Negative on error. */
static int waiting_time_to_seconds(const char *s)
{
    int h = 0, m = 0, sec = 0;
    int n;

    if (!s || !s[0]) return -1;
    /* Try HH:MM:SS */
    n = sscanf(s, "%d:%d:%d", &h, &m, &sec);
    if (n == 3) return h * 3600 + m * 60 + sec;
    if (n == 2) return h * 60 + m;       /* MM:SS */
    if (n == 1) return h * 60;           /* MM only */
    return -1;
}

/* Pick the soonest non-negative passage at a stop. */
static const ToulousePassage *soonest_passage(const StopPassages *sp, int *out_seconds)
{
    const ToulousePassage *best = NULL;
    int best_secs = -1;

    if (!sp || sp->count <= 0 || !sp->passages) return NULL;
    for (int i = 0; i < sp->count; i++) {
        const ToulousePassage *p = &sp->passages[i];
        int secs = waiting_time_to_seconds(p->waiting_time);
        if (secs < 0) continue;
        if (best_secs < 0 || secs < best_secs) {
            best_secs = secs;
            best = p;
        }
    }
    if (best && out_seconds) *out_seconds = best_secs;
    return best;
}

static int passage_already_emitted(const ToulouseVehicle *out, int n,
                                   const char *destination, const char *datetime)
{
    if (!destination || !datetime) return 0;
    for (int i = 0; i < n; i++) {
        if (strcmp(out[i].terminus, destination) == 0 &&
            strcmp(out[i].datetime, datetime) == 0) return 1;
    }
    return 0;
}

int nvt_synthesize_vehicles_from_passages(
    const ToulouseLine     *line,
    const ToulouseStop     *stops,
    int                     nstops,
    const StopPassages     *passages_per_stop,
    int                     imminent_seconds,
    ToulouseVehicle        *out,
    int                     max)
{
    int n = 0;
    int threshold = imminent_seconds > 0 ? imminent_seconds : 120;

    if (!line || !stops || !passages_per_stop || !out || max <= 0 || nstops <= 0) return 0;

    for (int i = 0; i < nstops && n < max; i++) {
        const StopPassages *sp = &passages_per_stop[i];
        const ToulouseStop *stop;
        const ToulousePassage *p;
        int wait_secs = -1;
        int s_index = sp->stop_index;
        ToulouseVehicle *v;

        if (s_index < 0 || s_index >= nstops) continue;
        stop = &stops[s_index];
        if (stop->lat == 0 && stop->lon == 0) continue;

        p = soonest_passage(sp, &wait_secs);
        if (!p || wait_secs < 0) continue;
        if (wait_secs > threshold) continue;
        if (passage_already_emitted(out, n, p->destination, p->datetime)) continue;

        v = &out[n];
        memset(v, 0, sizeof(*v));
        snprintf(v->ref, sizeof(v->ref), "synth-%d-%s",
                 s_index, p->datetime[0] ? p->datetime : "");
        snprintf(v->line_code, sizeof(v->line_code), "%s",
                 p->line_code[0] ? p->line_code : line->code);
        snprintf(v->line_name, sizeof(v->line_name), "%s",
                 p->line_name[0] ? p->line_name : line->libelle);
        snprintf(v->current_stop, sizeof(v->current_stop), "%s",
                 stop->libelle);
        snprintf(v->next_stop, sizeof(v->next_stop), "%s",
                 stop->libelle);
        snprintf(v->terminus, sizeof(v->terminus), "%s", p->destination);
        snprintf(v->sens, sizeof(v->sens), "%s", "ALLER");
        snprintf(v->datetime, sizeof(v->datetime), "%s", p->datetime);
        snprintf(v->waiting_time, sizeof(v->waiting_time), "%s", p->waiting_time);

        v->lon = stop->lon;
        v->lat = stop->lat;
        v->bearing = -1;
        v->has_position = 1;
        v->realtime = p->realtime;
        v->delayed = p->delayed;
        v->vitesse = 0;
        v->arret = wait_secs <= 30;  /* "à quai" si <30s */

        n++;
    }

    return n;
}
