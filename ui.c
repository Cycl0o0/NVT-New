#include "ui.h"

#include <ncurses.h>
#include <string.h>

#include "filter.h"

#define U_BLOCK     "\xe2\x96\x88"
#define U_BLIGHT    "\xe2\x96\x91"
#define U_MDOT      "\xc2\xb7"
#define RB_TL       "\xe2\x95\xad"
#define RB_TR       "\xe2\x95\xae"
#define RB_BL       "\xe2\x95\xb0"
#define RB_BR       "\xe2\x95\xaf"
#define RB_H        "\xe2\x94\x80"
#define RB_V        "\xe2\x94\x82"

static int nvt_ui_grad_color(int pct, int use_256, int green_cp, int yellow_cp, int red_cp)
{
    if (!use_256) return pct < 50 ? yellow_cp : green_cp;
    if (pct < 20) return red_cp;
    if (pct < 70) return yellow_cp;
    return green_cp;
}

void nvt_ui_rbox(int y1, int x1, int y2, int x2, int border_cp)
{
    if (y2 <= y1 || x2 <= x1) return;

    attron(COLOR_PAIR(border_cp));
    mvaddstr(y1, x1, RB_TL);
    mvaddstr(y1, x2, RB_TR);
    mvaddstr(y2, x1, RB_BL);
    mvaddstr(y2, x2, RB_BR);
    for (int x = x1 + 1; x < x2; x++) {
        mvaddstr(y1, x, RB_H);
        mvaddstr(y2, x, RB_H);
    }
    for (int y = y1 + 1; y < y2; y++) {
        mvaddstr(y, x1, RB_V);
        mvaddstr(y, x2, RB_V);
    }
    attroff(COLOR_PAIR(border_cp));
}

void nvt_ui_hbar(int y, int x1, int x2, int border_cp)
{
    attron(COLOR_PAIR(border_cp) | A_DIM);
    for (int x = x1; x < x2; x++) mvaddstr(y, x, RB_H);
    attroff(COLOR_PAIR(border_cp) | A_DIM);
}

void nvt_ui_scrollbar(int ys, int height, int pos, int total, int accent_cp, int border_cp)
{
    int thumb_height;
    int thumb_pos;
    int x = COLS - 1;

    if (total <= height || height < 3) return;

    thumb_height = height * height / total;
    if (thumb_height < 1) thumb_height = 1;
    thumb_pos = total > height ? (pos * (height - thumb_height)) / (total - height) : 0;
    if (thumb_pos < 0) thumb_pos = 0;
    if (thumb_pos + thumb_height > height) thumb_pos = height - thumb_height;

    for (int i = 0; i < height; i++) {
        if (i >= thumb_pos && i < thumb_pos + thumb_height) {
            attron(COLOR_PAIR(accent_cp) | A_BOLD);
            mvaddstr(ys + i, x, U_BLOCK);
            attroff(COLOR_PAIR(accent_cp) | A_BOLD);
        } else {
            attron(COLOR_PAIR(border_cp) | A_DIM);
            mvaddstr(ys + i, x, RB_V);
            attroff(COLOR_PAIR(border_cp) | A_DIM);
        }
    }
}

void nvt_ui_progress_bar(int y, int x, int width, int pct, int use_256,
                         int green_cp, int yellow_cp, int red_cp)
{
    int filled;

    if (width < 2) return;

    filled = pct * width / 100;
    if (filled > width) filled = width;

    move(y, x);
    for (int i = 0; i < width; i++) {
        if (i < filled) {
            int cp = nvt_ui_grad_color(i * 100 / width, use_256, green_cp, yellow_cp, red_cp);
            attron(COLOR_PAIR(cp) | A_BOLD);
            addstr(U_BLOCK);
            attroff(COLOR_PAIR(cp) | A_BOLD);
        } else if (i == filled && pct > 0) {
            attron(A_DIM);
            addstr(U_BLIGHT);
            attroff(A_DIM);
        } else {
            attron(A_DIM);
            addstr(U_MDOT);
            attroff(A_DIM);
        }
    }
}

void nvt_ui_print_highlight(int y, int x, const char *text, const char *search, int max_width)
{
    int text_len = (int)strlen(text);
    int search_len = (int)strlen(search);
    int match = search[0] ? nvt_match_offset(text, search) : -1;

    if (text_len > max_width) text_len = max_width;

    move(y, x);
    if (match < 0 || match >= text_len) {
        printw("%-*.*s", max_width, max_width, text);
        return;
    }

    printw("%.*s", match, text);
    attron(A_UNDERLINE | A_BOLD);
    if (match + search_len > text_len) search_len = text_len - match;
    printw("%.*s", search_len, text + match);
    attroff(A_UNDERLINE | A_BOLD);
    if (text_len - match - search_len > 0) {
        printw("%.*s", text_len - match - search_len, text + match + search_len);
    }
    for (int printed = text_len; printed < max_width; printed++) addch(' ');
}

void nvt_ui_fill_span(int y, int x1, int x2, int cp, int attr)
{
    if (y < 0 || y >= LINES || x2 < x1) return;
    if (x1 < 0) x1 = 0;
    if (x2 >= COLS) x2 = COLS - 1;

    attron(COLOR_PAIR(cp) | attr);
    mvhline(y, x1, ' ', x2 - x1 + 1);
    attroff(COLOR_PAIR(cp) | attr);
}

void nvt_ui_panel_box(int y1, int x1, int y2, int x2, const char *title, const char *meta,
                      int border_cp, int section_cp)
{
    if (y2 <= y1 || x2 <= x1) return;

    for (int y = y1 + 1; y < y2; y++) mvhline(y, x1 + 1, ' ', x2 - x1 - 1);
    nvt_ui_rbox(y1, x1, y2, x2, border_cp);
    if (title && title[0]) {
        attron(COLOR_PAIR(section_cp) | A_BOLD);
        mvprintw(y1, x1 + 2, " %s ", title);
        attroff(COLOR_PAIR(section_cp) | A_BOLD);
    }
    if (meta && meta[0]) {
        int meta_len = (int)strlen(meta) + 2;
        int meta_x = x2 - meta_len;

        if (meta_x > x1 + 12) {
            attron(A_DIM);
            mvprintw(y1, meta_x, " %s ", meta);
            attroff(A_DIM);
        }
    }
}

void nvt_ui_print_fit(int y, int x, int width, const char *text)
{
    if (width <= 0) return;
    mvprintw(y, x, "%-*.*s", width, width, text ? text : "");
}

void nvt_ui_stat_card(int y, int x, int width, const char *label, const char *value,
                      const char *detail, int cp, int border_cp, int section_cp)
{
    int x2 = x + width - 1;

    if (width < 16) return;

    nvt_ui_panel_box(y, x, y + 4, x2, label, NULL, border_cp, section_cp);
    attron(COLOR_PAIR(cp) | A_BOLD);
    mvprintw(y + 1, x + 2, "%s", value);
    attroff(COLOR_PAIR(cp) | A_BOLD);
    if (detail && detail[0]) {
        attron(A_DIM);
        nvt_ui_print_fit(y + 2, x + 2, width - 4, detail);
        attroff(A_DIM);
    }
    nvt_ui_fill_span(y + 3, x + 2, x2 - 2, cp, A_DIM);
}

void nvt_ui_kv_line(int y, int x, int label_width, const char *label, const char *value, int cp)
{
    attron(A_DIM);
    mvprintw(y, x, "%-*s", label_width, label);
    attroff(A_DIM);
    attron(COLOR_PAIR(cp) | A_BOLD);
    printw("%s", value && value[0] ? value : "-");
    attroff(COLOR_PAIR(cp) | A_BOLD);
}

int nvt_ui_draw_wrapped_block(int y, int x, int width, int max_lines, const char *text, int cp, int attr)
{
    int lines = 0;
    const char *cursor = text;

    if (width < 4 || max_lines < 1 || !text) return 0;

    while (*cursor && lines < max_lines) {
        int len = 0;
        int cut = 0;
        int last_space = -1;

        while (*cursor == ' ') cursor++;
        if (!*cursor) break;
        if (*cursor == '\n') {
            lines++;
            cursor++;
            continue;
        }

        while (cursor[len] && cursor[len] != '\n' && len < width) {
            if (cursor[len] == ' ') last_space = len;
            len++;
        }
        cut = len;
        if (cursor[len] && cursor[len] != '\n' && len == width && last_space > 0) cut = last_space;
        if (cut <= 0) cut = len;

        attron(COLOR_PAIR(cp) | attr);
        mvprintw(y + lines, x, "%.*s", cut, cursor);
        attroff(COLOR_PAIR(cp) | attr);
        lines++;
        cursor += cut;
        while (*cursor == ' ') cursor++;
        if (*cursor == '\n') cursor++;
    }

    return lines;
}
