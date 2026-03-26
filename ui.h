#ifndef UI_H
#define UI_H

/* Core UI primitives */
void nvt_ui_rbox(int y1, int x1, int y2, int x2, int border_cp);
void nvt_ui_hbar(int y, int x1, int x2, int border_cp);
void nvt_ui_scrollbar(int ys, int height, int pos, int total, int accent_cp, int border_cp);
void nvt_ui_progress_bar(int y, int x, int width, int pct, int use_256,
                         int green_cp, int yellow_cp, int red_cp);
void nvt_ui_print_highlight(int y, int x, const char *text, const char *search, int max_width);
void nvt_ui_fill_span(int y, int x1, int x2, int cp, int attr);

/* Panel and card components */
void nvt_ui_panel_box(int y1, int x1, int y2, int x2, const char *title, const char *meta,
                      int border_cp, int section_cp);
void nvt_ui_print_fit(int y, int x, int width, const char *text);
void nvt_ui_stat_card(int y, int x, int width, const char *label, const char *value,
                      const char *detail, int cp, int border_cp, int section_cp);
void nvt_ui_kv_line(int y, int x, int label_width, const char *label, const char *value, int cp);
void nvt_ui_shadow_box(int y1, int x1, int y2, int x2, const char *title, const char *meta,
                       int border_cp, int section_cp);
int nvt_ui_draw_wrapped_block(int y, int x, int width, int max_lines, const char *text, int cp, int attr);

/* Enhanced visual components */
void nvt_ui_heavy_box(int y1, int x1, int y2, int x2, int border_cp);
void nvt_ui_fancy_separator(int y, int x1, int x2, int cp);
void nvt_ui_live_indicator(int y, int x, int cp, int is_live);
void nvt_ui_glow_badge(int y, int x, const char *text, int text_cp, int glow_cp);
void nvt_ui_sparkline(int y, int x, int width, const int *values, int count, int max_val, int cp);

#endif
