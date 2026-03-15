#ifndef FILTER_H
#define FILTER_H

#include "api.h"

int nvt_cmp_lines(const void *a, const void *b);
int nvt_strcasestr_s(const char *haystack, const char *needle);
int nvt_match_offset(const char *haystack, const char *needle);
int nvt_line_matches_search(const Line *line, const char *search);
int nvt_line_matches_atlas(const Line *line, const char *search);
int nvt_find_line_index_by_gid(const Line *lines, int nlines, int gid);
int nvt_rebuild_line_filter(const Line *lines, int nlines, const char *search, int *filtered, int max_filtered);
int nvt_rebuild_atlas_filter(const Line *lines, int nlines, const char *search, int *filtered, int max_filtered);
int nvt_rebuild_stop_filter(const StopGroup *groups, int ngroups, const char *search, int *filtered, int max_filtered);
int nvt_toulouse_mode_is_rail(const char *mode);
int nvt_cmp_idfm_lines(const void *a, const void *b);
const char *nvt_idfm_line_type_label(const ToulouseLine *line);
int nvt_toulouse_list_has_token(const char *list, const char *token);
int nvt_toulouse_line_matches_search(const ToulouseLine *line, const char *search);
int nvt_rebuild_toulouse_line_filter(const ToulouseLine *lines, int nlines, const char *search, int *filtered, int max_filtered);
int nvt_rebuild_toulouse_stop_filter(const ToulouseStop *stops, int nstops, const char *search, int *filtered, int max_filtered);
int nvt_toulouse_waiting_minutes(const char *waiting_time);
int nvt_hhmm_to_minutes(const char *hhmm);

#endif
