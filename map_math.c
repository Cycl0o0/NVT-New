#include "map_math.h"

enum {
    NVT_MAP_LEFT = 1,
    NVT_MAP_RIGHT = 2,
    NVT_MAP_BOTTOM = 4,
    NVT_MAP_TOP = 8,
};

void nvt_map_project(double lon, double lat, double minlon, double maxlon,
                     double minlat, double maxlat, int width, int height,
                     int *px, int *py)
{
    double dx = maxlon - minlon;
    double dy = maxlat - minlat;

    if (dx <= 0) dx = 1;
    if (dy <= 0) dy = 1;

    *px = (int)((lon - minlon) * (width - 1) / dx + 0.5);
    *py = (int)((maxlat - lat) * (height - 1) / dy + 0.5);
}

int nvt_map_clip_code(double lon, double lat, double minlon, double maxlon,
                      double minlat, double maxlat)
{
    int code = 0;

    if (lon < minlon) code |= NVT_MAP_LEFT;
    if (lon > maxlon) code |= NVT_MAP_RIGHT;
    if (lat < minlat) code |= NVT_MAP_BOTTOM;
    if (lat > maxlat) code |= NVT_MAP_TOP;
    return code;
}

int nvt_map_clip_segment(double *lon0, double *lat0, double *lon1, double *lat1,
                         double minlon, double maxlon, double minlat, double maxlat)
{
    int code0 = nvt_map_clip_code(*lon0, *lat0, minlon, maxlon, minlat, maxlat);
    int code1 = nvt_map_clip_code(*lon1, *lat1, minlon, maxlon, minlat, maxlat);

    while (1) {
        double lon;
        double lat;
        int outside;

        if (!(code0 | code1)) return 1;
        if (code0 & code1) return 0;

        outside = code0 ? code0 : code1;
        if (outside & NVT_MAP_TOP) {
            lon = *lon0 + (*lon1 - *lon0) * (maxlat - *lat0) / (*lat1 - *lat0);
            lat = maxlat;
        } else if (outside & NVT_MAP_BOTTOM) {
            lon = *lon0 + (*lon1 - *lon0) * (minlat - *lat0) / (*lat1 - *lat0);
            lat = minlat;
        } else if (outside & NVT_MAP_RIGHT) {
            lat = *lat0 + (*lat1 - *lat0) * (maxlon - *lon0) / (*lon1 - *lon0);
            lon = maxlon;
        } else {
            lat = *lat0 + (*lat1 - *lat0) * (minlon - *lon0) / (*lon1 - *lon0);
            lon = minlon;
        }

        if (outside == code0) {
            *lon0 = lon;
            *lat0 = lat;
            code0 = nvt_map_clip_code(*lon0, *lat0, minlon, maxlon, minlat, maxlat);
        } else {
            *lon1 = lon;
            *lat1 = lat;
            code1 = nvt_map_clip_code(*lon1, *lat1, minlon, maxlon, minlat, maxlat);
        }
    }
}
