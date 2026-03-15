#ifndef MAP_MATH_H
#define MAP_MATH_H

void nvt_map_project(double lon, double lat, double minlon, double maxlon,
                     double minlat, double maxlat, int width, int height,
                     int *px, int *py);
int nvt_map_clip_code(double lon, double lat, double minlon, double maxlon,
                      double minlat, double maxlat);
int nvt_map_clip_segment(double *lon0, double *lat0, double *lon1, double *lat1,
                         double minlon, double maxlon, double minlat, double maxlat);

#endif
