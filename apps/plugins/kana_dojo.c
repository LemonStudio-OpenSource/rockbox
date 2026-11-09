/***************************************************************************
 * Kana Dojo - a Japanese kana reference chart and quiz for Rockbox.
 *
 * The reference view shows a 5 x 10 gojuuon grid (a, ka, sa, ta, na, ha,
 * ma, ya, ra, wa rows) plus the standalone "n", which sits in the last
 * column of the last row.  The view cycles between hiragana, katakana and
 * both scripts drawn on top of each other.  The quiz view shows one kana
 * big and asks for its romaji reading, ten questions per round.
 *
 * No CJK font is required: every kana is stored as a small set of line
 * segments inside a 16x16 box and drawn with lcd_drawline(), scaled to the
 * cell size at run time.
 *
 * Controls
 *   reference view: arrows / wheel move the selection, a tap on SELECT
 *                   cycles the script view, holding SELECT starts the quiz,
 *                   EXIT or CANCEL leaves the plugin.
 *   quiz view:      left/right or up/down pick a choice, SELECT confirms,
 *                   SELECT advances after the feedback, up on the summary
 *                   goes back to the reference chart, EXIT/CANCEL leaves.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 **************************************************************************/

#include "plugin.h"
#include "lib/pluginlib_actions.h"

#define KANA_COUNT 46
#define GRID_ROWS 10
#define GRID_COLS 5
#define MAX_STROKES 6
#define KANA_STRIDE (1 + MAX_STROKES * 4)
#define QUIZ_LEN 10
#define BEST_FILE ROCKBOX_DIR "/kana_dojo.txt"

#define SCRIPT_HIRA 0
#define SCRIPT_KATA 1

#define VIEW_HIRA 0
#define VIEW_KATA 1
#define VIEW_BOTH 2

#define QS_QUESTION 0
#define QS_FEEDBACK 1
#define QS_DONE 2

/* Gojuuon layout.  -1 marks the unused cells of the ya and wa rows; the
 * standalone "n" (kana 45) closes the last row after "wo". */
static const signed char grid_map[GRID_ROWS][GRID_COLS] = {
    {  0,  1,  2,  3,  4 },
    {  5,  6,  7,  8,  9 },
    { 10, 11, 12, 13, 14 },
    { 15, 16, 17, 18, 19 },
    { 20, 21, 22, 23, 24 },
    { 25, 26, 27, 28, 29 },
    { 30, 31, 32, 33, 34 },
    { 35, -1, 36, -1, 37 },
    { 38, 39, 40, 41, 42 },
    { 43, -1, -1, 44, 45 }
};

static const char *const romaji[KANA_COUNT] = {
    "a", "i", "u", "e", "o",
    "ka", "ki", "ku", "ke", "ko",
    "sa", "shi", "su", "se", "so",
    "ta", "chi", "tsu", "te", "to",
    "na", "ni", "nu", "ne", "no",
    "ha", "hi", "fu", "he", "ho",
    "ma", "mi", "mu", "me", "mo",
    "ya", "yu", "yo",
    "ra", "ri", "ru", "re", "ro",
    "wa", "wo", "n"
};

/* Hiragana strokes.  Entry format: { stroke_count, x1,y1,x2,y2, ... } with
 * all coordinates in a 16x16 box (0..15).  Unused slots stay zero. */
static const unsigned char hira[KANA_COUNT][KANA_STRIDE] = {
    /* a   */ { 4,  3,4,13,4,  9,2,8,14,  8,9,4,12,  4,12,12,12 },
    /* i   */ { 3,  5,3,4,12,  11,2,10,8,  10,8,9,14 },
    /* u   */ { 5,  7,2,11,3,  4,5,12,6,  12,6,12,10,  12,10,7,14,  7,14,4,11 },
    /* e   */ { 4,  7,2,11,3,  4,5,12,6,  12,6,5,11,  5,11,12,13 },
    /* o   */ { 5,  3,4,13,4,  9,2,9,13,  9,9,5,12,  5,12,12,12,  13,7,14,8 },
    /* ka  */ { 5,  5,3,11,3,  11,3,11,8,  11,8,5,13,  7,2,6,14,  13,5,13,6 },
    /* ki  */ { 5,  3,4,12,4,  4,7,12,7,  8,2,8,11,  11,9,4,13,  4,13,9,14 },
    /* ku  */ { 2,  11,2,4,8,  4,8,11,14 },
    /* ke  */ { 4,  4,3,4,13,  8,5,13,5,  11,2,11,13,  11,10,13,11 },
    /* ko  */ { 3,  4,4,12,4,  12,6,4,10,  4,10,12,13 },
    /* sa  */ { 3,  3,4,12,4,  10,3,6,9,  6,9,12,12 },
    /* shi */ { 3,  8,2,8,10,  8,10,12,11,  12,11,13,9 },
    /* su  */ { 5,  2,4,13,4,  8,2,8,12,  8,8,4,11,  4,11,12,10,  12,10,10,7 },
    /* se  */ { 4,  2,5,13,5,  6,2,6,13,  6,9,12,9,  12,9,12,13 },
    /* so  */ { 4,  5,2,12,3,  12,3,5,8,  5,8,11,9,  11,9,5,13 },
    /* ta  */ { 4,  4,3,12,3,  9,3,5,13,  10,8,13,12,  8,11,13,11 },
    /* chi */ { 4,  4,4,12,4,  10,3,5,9,  5,9,12,10,  12,10,6,14 },
    /* tsu */ { 4,  3,5,11,5,  11,5,12,10,  12,10,7,14,  7,14,4,12 },
    /* te  */ { 3,  3,4,12,4,  12,4,5,8,  5,8,13,12 },
    /* to  */ { 3,  6,3,9,5,  9,5,4,9,  4,9,13,13 },
    /* na  */ { 5,  3,4,11,4,  9,3,5,11,  11,7,8,10,  8,10,13,11,  13,11,10,13 },
    /* ni  */ { 4,  4,3,4,13,  9,4,13,4,  13,6,8,10,  8,10,13,13 },
    /* nu  */ { 5,  4,2,8,9,  8,9,3,11,  3,11,5,14,  5,14,12,12,  12,12,12,6 },
    /* ne  */ { 5,  8,2,7,14,  7,5,12,3,  12,3,13,9,  13,9,8,10,  8,10,12,13 },
    /* no  */ { 6,  5,4,11,3,  11,3,13,8,  13,8,9,14,  9,14,4,12,  4,12,6,7,  6,7,10,6 },
    /* ha  */ { 5,  4,3,4,13,  8,4,13,4,  10,2,10,14,  10,10,13,12,  13,12,10,13 },
    /* hi  */ { 5,  3,5,6,3,  6,3,9,8,  9,8,5,13,  5,13,12,13,  12,13,13,7 },
    /* fu  */ { 5,  8,2,9,4,  5,5,3,9,  11,5,13,9,  4,9,11,10,  11,10,8,14 },
    /* he  */ { 2,  3,10,8,5,  8,5,13,10 },
    /* ho  */ { 5,  4,3,4,13,  8,4,13,4,  8,7,13,7,  10,2,10,14,  10,10,13,12 },
    /* ma  */ { 5,  4,4,12,4,  4,7,12,7,  8,2,8,13,  8,9,5,12,  5,12,12,12 },
    /* mi  */ { 5,  5,3,11,4,  11,4,4,8,  4,8,12,9,  12,9,6,13,  6,13,9,14 },
    /* mu  */ { 5,  3,4,12,4,  8,2,8,12,  8,7,12,9,  12,9,8,12,  8,12,5,11 },
    /* me  */ { 5,  5,2,9,8,  9,8,3,10,  3,10,5,14,  5,14,12,12,  12,12,9,5 },
    /* mo  */ { 4,  9,2,9,13,  3,5,11,5,  3,9,9,9,  9,13,6,14 },
    /* ya  */ { 4,  3,5,10,4,  10,4,5,10,  5,10,13,9,  7,2,8,14 },
    /* yu  */ { 5,  8,2,8,14,  8,5,4,7,  4,7,4,12,  4,12,10,13,  10,13,12,8 },
    /* yo  */ { 4,  4,4,12,4,  9,2,9,14,  9,8,5,11,  5,11,12,12 },
    /* ra  */ { 4,  7,2,9,4,  4,6,11,6,  11,6,5,11,  5,11,12,13 },
    /* ri  */ { 2,  5,3,5,9,  10,3,11,13 },
    /* ru  */ { 5,  5,3,12,3,  12,3,4,8,  4,8,11,9,  11,9,5,13,  5,13,10,14 },
    /* re  */ { 4,  8,2,7,14,  7,5,12,3,  12,3,13,13,  13,10,9,14 },
    /* ro  */ { 4,  5,3,12,3,  12,3,4,8,  4,8,11,9,  11,9,5,13 },
    /* wa  */ { 4,  8,2,7,14,  7,5,12,3,  12,3,13,8,  13,8,9,13 },
    /* wo  */ { 5,  4,4,12,4,  12,4,5,8,  5,8,12,9,  12,9,5,12,  5,12,11,14 },
    /* n   */ { 4,  4,3,8,5,  8,5,6,9,  6,9,12,11,  12,11,10,14 }
};

/* Katakana strokes, same format as hira[]. */
static const unsigned char kata[KANA_COUNT][KANA_STRIDE] = {
    /* a   */ { 3,  3,4,13,4,  13,4,5,13,  8,8,12,8 },
    /* i   */ { 3,  7,2,3,10,  9,3,9,14,  3,10,9,10 },
    /* u   */ { 5,  8,2,9,4,  4,6,12,6,  4,6,4,12,  4,12,13,12,  13,12,12,6 },
    /* e   */ { 3,  4,4,12,4,  8,4,8,12,  3,12,13,12 },
    /* o   */ { 3,  3,5,13,5,  9,2,9,14,  9,7,4,13 },
    /* ka  */ { 4,  4,4,11,4,  11,4,11,9,  11,9,4,13,  8,2,6,14 },
    /* ki  */ { 3,  3,4,13,4,  4,8,12,8,  8,2,8,14 },
    /* ku  */ { 3,  4,3,12,4,  12,4,5,13,  10,9,12,9 },
    /* ke  */ { 3,  4,4,13,4,  13,4,5,13,  8,8,12,8 },
    /* ko  */ { 3,  4,4,12,4,  12,4,12,11,  4,11,12,11 },
    /* sa  */ { 3,  3,5,13,5,  6,2,6,14,  10,2,10,13 },
    /* shi */ { 3,  5,3,7,4,  5,8,7,9,  4,12,12,5 },
    /* su  */ { 3,  4,4,12,4,  12,4,6,12,  6,12,12,13 },
    /* se  */ { 3,  3,5,13,5,  6,2,6,13,  6,8,12,13 },
    /* so  */ { 2,  6,3,8,4,  5,10,12,4 },
    /* ta  */ { 3,  4,3,12,4,  12,4,5,13,  7,8,9,7 },
    /* chi */ { 4,  3,4,12,4,  10,2,6,10,  4,10,13,10,  9,10,9,14 },
    /* tsu */ { 3,  5,3,7,4,  10,3,12,4,  4,11,11,5 },
    /* te  */ { 3,  3,4,13,4,  5,7,12,7,  9,7,8,14 },
    /* to  */ { 2,  6,2,6,14,  6,7,11,10 },
    /* na  */ { 2,  3,5,13,5,  10,2,9,14 },
    /* ni  */ { 2,  4,5,12,5,  3,11,13,11 },
    /* nu  */ { 3,  4,3,12,4,  12,4,5,13,  9,7,5,13 },
    /* ne  */ { 5,  8,2,9,3,  4,5,12,5,  9,5,8,14,  8,9,4,12,  8,9,12,12 },
    /* no  */ { 1,  4,3,13,13 },
    /* ha  */ { 2,  4,4,3,13,  10,4,13,13 },
    /* hi  */ { 3,  4,5,12,5,  6,5,6,11,  6,11,12,11 },
    /* fu  */ { 2,  4,4,12,4,  12,4,4,13 },
    /* he  */ { 2,  3,10,8,4,  8,4,13,10 },
    /* ho  */ { 4,  4,5,12,5,  8,2,8,14,  8,9,4,13,  8,9,12,13 },
    /* ma  */ { 3,  4,4,12,4,  12,4,4,10,  4,10,9,10 },
    /* mi  */ { 3,  4,4,9,3,  5,8,10,7,  5,12,11,13 },
    /* mu  */ { 3,  4,5,11,5,  11,5,4,13,  4,13,12,13 },
    /* me  */ { 2,  4,3,12,13,  10,4,4,12 },
    /* mo  */ { 4,  4,5,12,5,  4,9,11,9,  8,2,8,13,  8,13,12,13 },
    /* ya  */ { 4,  3,5,11,5,  11,5,4,10,  4,10,12,10,  7,2,8,14 },
    /* yu  */ { 3,  5,4,11,4,  11,4,11,10,  4,10,12,10 },
    /* yo  */ { 4,  4,4,12,4,  4,8,11,8,  4,12,12,12,  12,4,12,12 },
    /* ra  */ { 3,  4,3,12,3,  3,6,12,6,  12,6,5,13 },
    /* ri  */ { 2,  5,3,5,10,  10,3,10,14 },
    /* ru  */ { 3,  5,3,5,12,  5,12,9,14,  10,3,9,13 },
    /* re  */ { 2,  5,3,6,12,  6,12,12,5 },
    /* ro  */ { 4,  4,3,12,3,  12,3,12,13,  12,13,4,13,  4,13,4,3 },
    /* wa  */ { 3,  4,4,12,4,  12,4,12,10,  12,10,4,13 },
    /* wo  */ { 4,  4,4,12,4,  4,8,11,8,  12,4,11,8,  11,8,4,13 },
    /* n   */ { 3,  5,4,7,5,  9,9,11,11,  4,13,13,8 }
};

static const struct button_mapping *plugin_contexts[] = { pla_main_ctx };

/* Screen layout, recomputed for every draw so it adapts to any LCD size. */
struct layout {
    int ch;         /* font line height */
    int header_h;
    int status_y;
    int grid_y;
    int cell_w;
    int cell_h;
    int glyph;      /* glyph box side in pixels (8..16 in the grid) */
    int labels;     /* non-zero when the romaji fits under each glyph */
};

static int view_mode = VIEW_HIRA;
static int sel_row;
static int sel_col;
static int best_ever;

static int quiz_active;
static int quiz_script;
static int q_num;       /* 0-based question counter */
static int q_kana;      /* kana index shown in the quiz */
static int q_slot;      /* highlighted choice, 0..3 */
static int q_correct_slot;
static int q_score;
static int q_streak;
static int q_best_streak;
static int q_state;
static int q_last_right;
static int q_choices[4];

static void draw_kana(int x, int y, int size, int script, int idx);

static void setup_colors(void)
{
#if LCD_DEPTH > 1
    rb->lcd_set_foreground(0xFFFF);
    rb->lcd_set_background(0x0000);
#endif
}

static void save_best(void)
{
    int fd;
    int len;
    char buf[16];

    fd = rb->creat(BEST_FILE, 0666);
    if (fd < 0)
        return;
    len = rb->snprintf(buf, sizeof(buf), "%d\n", best_ever);
    if (len > 0)
        rb->write(fd, buf, (size_t)len);
    rb->close(fd);
}

static void load_best(void)
{
    int fd;
    int n;
    char buf[16];

    best_ever = 0;
    fd = rb->open(BEST_FILE, O_RDONLY);
    if (fd < 0)
        return;
    n = (int)rb->read(fd, buf, sizeof(buf) - 1);
    rb->close(fd);
    if (n > 0) {
        buf[n] = '\0';
        best_ever = rb->atoi(buf);
        if (best_ever < 0)
            best_ever = 0;
        if (best_ever > 9999)
            best_ever = 9999;
    }
}

static void layout_init(struct layout *L, int view)
{
    int w = 0;
    int h = 0;
    int avail;

    rb->lcd_getstringsize((const unsigned char *)"Ag", &w, &h);
    if (h < 8)
        h = 8;
    L->ch = h;
    L->header_h = h + 2;
    L->grid_y = L->header_h;
    L->status_y = LCD_HEIGHT - (h + 2);
    L->cell_w = LCD_WIDTH / GRID_COLS;
    if (L->cell_w < 8)
        L->cell_w = 8;
    L->cell_h = (L->status_y - L->grid_y) / GRID_ROWS;
    if (L->cell_h < 8)
        L->cell_h = 8;
    avail = L->cell_h - 1;
    if (avail < 8)
        avail = 8;
    L->labels = 0;
    if (view != VIEW_BOTH && avail - h >= 10 && L->cell_w >= 24)
        L->labels = 1;
    L->glyph = avail - (L->labels ? h : 0);
    if (view == VIEW_BOTH)
        L->glyph = avail / 2;
    if (L->glyph < 8)
        L->glyph = 8;
    if (L->glyph > 16)
        L->glyph = 16;
}

static void draw_kana(int x, int y, int size, int script, int idx)
{
    const unsigned char *g;
    const unsigned char *p;
    int n;
    int s;
    int x0, y0, x1, y1;

    g = (script == SCRIPT_KATA) ? kata[idx] : hira[idx];
    n = (int)g[0];
    for (s = 0; s < n; s++) {
        p = g + 1 + s * 4;
        x0 = x + ((int)p[0] * (size - 1)) / 15;
        y0 = y + ((int)p[1] * (size - 1)) / 15;
        x1 = x + ((int)p[2] * (size - 1)) / 15;
        y1 = y + ((int)p[3] * (size - 1)) / 15;
        rb->lcd_drawline(x0, y0, x1, y1);
        if (size >= 32) {
            /* thicken the big quiz glyph a little */
            rb->lcd_drawline(x0 + 1, y0, x1 + 1, y1);
            rb->lcd_drawline(x0, y0 + 1, x1, y1 + 1);
        }
    }
}

static const char *view_name(int view)
{
    if (view == VIEW_KATA)
        return "KATA";
    if (view == VIEW_BOTH)
        return "BOTH";
    return "HIRA";
}

static void puts_centered(int y, const char *s)
{
    int w = 0;
    int h = 0;
    const unsigned char *u = (const unsigned char *)s;

    rb->lcd_getstringsize(u, &w, &h);
    rb->lcd_putsxy((LCD_WIDTH - w) / 2, y, u);
}

static void draw_header(int quiz)
{
    char right[24];
    int w = 0;
    int h = 0;
    int qn = QUIZ_LEN;
    const unsigned char *u;

    rb->lcd_putsxy(2, 1, (const unsigned char *)(quiz ? "Kana Quiz"
                                                      : "Kana Dojo"));
    if (quiz) {
        if (q_state != QS_DONE)
            qn = q_num + 1;
        rb->snprintf(right, sizeof(right), "Q%d/%d", qn, QUIZ_LEN);
    } else
        rb->snprintf(right, sizeof(right), "%s B%d", view_name(view_mode),
                     best_ever);
    u = (const unsigned char *)right;
    rb->lcd_getstringsize(u, &w, &h);
    if (w < LCD_WIDTH - 30)
        rb->lcd_putsxy(LCD_WIDTH - w - 2, 1, u);
}

static void draw_cell(const struct layout *L, int x, int y, int idx, int view)
{
    int content;
    int top;
    int gx;
    int w = 0;
    int h = 0;

    content = (view == VIEW_BOTH) ? (L->glyph * 2) : L->glyph;
    if (L->labels)
        content += L->ch;
    top = y + (L->cell_h - content) / 2;
    if (top < y)
        top = y;
    gx = x + (L->cell_w - L->glyph) / 2;

    if (view == VIEW_BOTH) {
        draw_kana(gx, top, L->glyph, SCRIPT_HIRA, idx);
        draw_kana(gx, top + L->glyph, L->glyph, SCRIPT_KATA, idx);
    } else {
        draw_kana(gx, top, L->glyph,
                  (view == VIEW_KATA) ? SCRIPT_KATA : SCRIPT_HIRA, idx);
        if (L->labels) {
            const unsigned char *r = (const unsigned char *)romaji[idx];
            rb->lcd_getstringsize(r, &w, &h);
            if (w <= L->cell_w - 2)
                rb->lcd_putsxy(x + (L->cell_w - w) / 2, top + L->glyph, r);
        }
    }
}

static void draw_ref_status(const struct layout *L)
{
    const unsigned char *u;
    int w = 0;
    int h = 0;
    int idx;

    idx = (int)grid_map[sel_row][sel_col];
    rb->lcd_putsxy(3, L->status_y + 2, (const unsigned char *)romaji[idx]);

    u = (const unsigned char *)"SEL:view HOLD:quiz";
    rb->lcd_getstringsize(u, &w, &h);
    if (w > LCD_WIDTH - 40) {
        u = (const unsigned char *)"SEL:vw HOLD:qz";
        rb->lcd_getstringsize(u, &w, &h);
    }
    if (w < LCD_WIDTH - 24)
        rb->lcd_putsxy(LCD_WIDTH - w - 2, L->status_y + 2, u);
}

static void draw_reference(void)
{
    struct layout L;
    int r, c, idx, x, y;

    rb->lcd_set_drawmode(DRMODE_SOLID);
    setup_colors();
    rb->lcd_clear_display();
    layout_init(&L, view_mode);
    draw_header(0);

    rb->lcd_hline(0, LCD_WIDTH - 1, L.header_h - 1);
    for (r = 0; r < GRID_ROWS; r++) {
        for (c = 0; c < GRID_COLS; c++) {
            idx = (int)grid_map[r][c];
            if (idx < 0)
                continue;
            x = c * L.cell_w;
            y = L.grid_y + r * L.cell_h;
            if (r == sel_row && c == sel_col) {
                rb->lcd_set_drawmode(DRMODE_COMPLEMENT);
                rb->lcd_fillrect(x + 1, y, L.cell_w - 2, L.cell_h);
                rb->lcd_set_drawmode(DRMODE_SOLID);
            }
            draw_cell(&L, x, y, idx, view_mode);
        }
    }
    rb->lcd_drawrect(0, L.grid_y, LCD_WIDTH, GRID_ROWS * L.cell_h);
    rb->lcd_hline(0, LCD_WIDTH - 1, L.status_y - 1);
    draw_ref_status(&L);
    rb->lcd_update();
}

static void draw_quiz(void)
{
    struct layout L;
    int cw, chh, area_y, big, big_y;
    int ty;
    int i, x, y;
    int w = 0;
    int h = 0;
    char buf[48];
    const unsigned char *u;

    rb->lcd_set_drawmode(DRMODE_SOLID);
    setup_colors();
    rb->lcd_clear_display();
    layout_init(&L, VIEW_HIRA);
    draw_header(1);
    rb->lcd_hline(0, LCD_WIDTH - 1, L.header_h - 1);

    if (q_state == QS_DONE) {
        ty = L.header_h + 6;
        rb->snprintf(buf, sizeof(buf), "Quiz complete");
        puts_centered(ty, buf);
        ty += L.ch + 6;
        rb->snprintf(buf, sizeof(buf), "Score %d / %d", q_score, QUIZ_LEN);
        puts_centered(ty, buf);
        ty += L.ch + 6;
        rb->snprintf(buf, sizeof(buf), "Accuracy %d%%",
                     q_score * 100 / QUIZ_LEN);
        puts_centered(ty, buf);
        ty += L.ch + 6;
        rb->snprintf(buf, sizeof(buf), "Best streak %d", q_best_streak);
        puts_centered(ty, buf);
        ty += L.ch + 6;
        rb->snprintf(buf, sizeof(buf), "Best ever %d", best_ever);
        puts_centered(ty, buf);
        ty += L.ch + 10;
        puts_centered(ty, "SEL:again UP:chart");
        rb->lcd_update();
        return;
    }

    cw = (LCD_WIDTH - 8) / 2;
    chh = L.ch + 8;
    area_y = L.status_y - (2 * chh + 6);
    if (area_y < L.header_h + 40)
        area_y = L.header_h + 40;
    big = area_y - L.header_h - 6;
    if (big > 96)
        big = 96;
    if (big < 24)
        big = 24;
    big_y = L.header_h + (area_y - L.header_h - big) / 2;
    if (big_y < L.header_h + 2)
        big_y = L.header_h + 2;

    draw_kana((LCD_WIDTH - big) / 2, big_y, big, quiz_script, q_kana);

    if (q_state == QS_QUESTION) {
        for (i = 0; i < 4; i++) {
            x = 4 + (i % 2) * (LCD_WIDTH / 2);
            y = area_y + (i / 2) * (chh + 4);
            if (i == q_slot) {
                rb->lcd_set_drawmode(DRMODE_COMPLEMENT);
                rb->lcd_fillrect(x, y, cw, chh);
                rb->lcd_set_drawmode(DRMODE_SOLID);
            }
            rb->lcd_drawrect(x, y, cw, chh);
            rb->snprintf(buf, sizeof(buf), "%s", romaji[q_choices[i]]);
            rb->lcd_getstringsize((const unsigned char *)buf, &w, &h);
            rb->lcd_putsxy(x + (cw - w) / 2, y + (chh - h) / 2,
                           (const unsigned char *)buf);
        }
    } else {
        rb->snprintf(buf, sizeof(buf), "= %s", romaji[q_kana]);
        rb->lcd_getstringsize((const unsigned char *)buf, &w, &h);
        if (big_y + big + 2 + h < area_y)
            rb->lcd_putsxy((LCD_WIDTH - w) / 2, big_y + big + 2,
                           (const unsigned char *)buf);
        u = (const unsigned char *)(q_last_right ? "CORRECT!" : "WRONG");
        rb->lcd_putsxy(4, area_y, u);
        rb->snprintf(buf, sizeof(buf), "Answer: %s", romaji[q_kana]);
        rb->lcd_putsxy(4, area_y + L.ch + 3, (const unsigned char *)buf);
        rb->lcd_putsxy(4, area_y + 2 * (L.ch + 3),
                       (const unsigned char *)"SEL: next");
    }

    rb->lcd_hline(0, LCD_WIDTH - 1, L.status_y - 1);
    rb->snprintf(buf, sizeof(buf), "Score %d  Streak %d", q_score, q_streak);
    rb->lcd_putsxy(3, L.status_y + 2, (const unsigned char *)buf);
    rb->lcd_update();
}

static void ref_move(int dr, int dc)
{
    int r = sel_row;
    int c = sel_col;
    int i;

    for (i = 0; i < GRID_ROWS * GRID_COLS; i++) {
        if (dr > 0) {
            r++;
            if (r >= GRID_ROWS)
                r = 0;
        } else if (dr < 0) {
            r--;
            if (r < 0)
                r = GRID_ROWS - 1;
        }
        if (dc > 0) {
            c++;
            if (c >= GRID_COLS)
                c = 0;
        } else if (dc < 0) {
            c--;
            if (c < 0)
                c = GRID_COLS - 1;
        }
        if (grid_map[r][c] >= 0) {
            sel_row = r;
            sel_col = c;
            return;
        }
    }
}

static void ref_cycle_view(void)
{
    view_mode++;
    if (view_mode > VIEW_BOTH)
        view_mode = VIEW_HIRA;
}

static void quiz_new_question(void)
{
    int i, j, t, v, dup;

    q_kana = (int)(rb->rand() % KANA_COUNT);
    q_choices[0] = q_kana;
    for (i = 1; i < 4; i++) {
        do {
            v = (int)(rb->rand() % KANA_COUNT);
            dup = 0;
            for (j = 0; j < i; j++) {
                if (q_choices[j] == v)
                    dup = 1;
            }
        } while (dup);
        q_choices[i] = v;
    }
    for (i = 3; i > 0; i--) {
        j = (int)(rb->rand() % (i + 1));
        t = q_choices[i];
        q_choices[i] = q_choices[j];
        q_choices[j] = t;
    }
    q_correct_slot = 0;
    for (i = 0; i < 4; i++) {
        if (q_choices[i] == q_kana)
            q_correct_slot = i;
    }
    q_slot = 0;
    q_state = QS_QUESTION;
}

static void quiz_start(void)
{
    quiz_active = 1;
    quiz_script = (view_mode == VIEW_KATA) ? SCRIPT_KATA : SCRIPT_HIRA;
    q_num = 0;
    q_score = 0;
    q_streak = 0;
    q_best_streak = 0;
    quiz_new_question();
    draw_quiz();
}

static void quiz_answer(void)
{
    if (q_slot == q_correct_slot) {
        q_last_right = 1;
        q_score++;
        q_streak++;
        if (q_streak > q_best_streak)
            q_best_streak = q_streak;
        if (q_streak > best_ever) {
            best_ever = q_streak;
            save_best();
        }
    } else {
        q_last_right = 0;
        q_streak = 0;
    }
    q_state = QS_FEEDBACK;
    draw_quiz();
}

static void quiz_advance(void)
{
    q_num++;
    if (q_num >= QUIZ_LEN) {
        q_state = QS_DONE;
        draw_quiz();
    } else {
        quiz_new_question();
        draw_quiz();
    }
}

static void quiz_handle(int act)
{
    int next;

    if (q_state == QS_QUESTION) {
        next = q_slot;
        switch (act) {
        case PLA_LEFT:
        case PLA_LEFT_REPEAT:
        case PLA_UP:
        case PLA_UP_REPEAT:
            next = q_slot - 1;
            break;
        case PLA_RIGHT:
        case PLA_RIGHT_REPEAT:
        case PLA_DOWN:
        case PLA_DOWN_REPEAT:
            next = q_slot + 1;
            break;
#ifdef HAVE_SCROLLWHEEL
        case PLA_SCROLL_FWD:
        case PLA_SCROLL_FWD_REPEAT:
            next = q_slot + 1;
            break;
        case PLA_SCROLL_BACK:
        case PLA_SCROLL_BACK_REPEAT:
            next = q_slot - 1;
            break;
#endif
        case PLA_SELECT:
            quiz_answer();
            return;
        default:
            return;
        }
        if (next < 0)
            next = 3;
        if (next > 3)
            next = 0;
        if (next != q_slot) {
            q_slot = next;
            draw_quiz();
        }
    } else if (q_state == QS_FEEDBACK) {
        if (act == PLA_SELECT)
            quiz_advance();
    } else {
        if (act == PLA_SELECT) {
            quiz_start();
        } else if (act == PLA_UP || act == PLA_UP_REPEAT) {
            quiz_active = 0;
            draw_reference();
        }
    }
}

static void ref_handle(int act)
{
    switch (act) {
    case PLA_UP:
    case PLA_UP_REPEAT:
        ref_move(-1, 0);
        draw_reference();
        break;
    case PLA_DOWN:
    case PLA_DOWN_REPEAT:
        ref_move(1, 0);
        draw_reference();
        break;
    case PLA_LEFT:
    case PLA_LEFT_REPEAT:
        ref_move(0, -1);
        draw_reference();
        break;
    case PLA_RIGHT:
    case PLA_RIGHT_REPEAT:
        ref_move(0, 1);
        draw_reference();
        break;
#ifdef HAVE_SCROLLWHEEL
    case PLA_SCROLL_FWD:
    case PLA_SCROLL_FWD_REPEAT:
        ref_move(0, 1);
        draw_reference();
        break;
    case PLA_SCROLL_BACK:
    case PLA_SCROLL_BACK_REPEAT:
        ref_move(0, -1);
        draw_reference();
        break;
#endif
    case PLA_SELECT_REL:
        /* a short tap cycles hiragana -> katakana -> both */
        ref_cycle_view();
        draw_reference();
        break;
    case PLA_SELECT_REPEAT:
        /* a long press starts the quiz */
        quiz_start();
        break;
    default:
        break;
    }
}

enum plugin_status plugin_start(const void *parameter)
{
    int act;

    (void)parameter;

    load_best();
    rb->button_clear_queue();
    /* otherwise every session asks the same questions */
    rb->srand((unsigned int)*rb->current_tick);
    sel_row = 0;
    sel_col = 0;
    quiz_active = 0;
    draw_reference();

    while (1) {
        act = pluginlib_getaction(TIMEOUT_BLOCK, plugin_contexts,
                                  ARRAYLEN(plugin_contexts));
        if (act == PLA_EXIT || act == PLA_CANCEL)
            return PLUGIN_OK;

        if (quiz_active)
            quiz_handle(act);
        else
            ref_handle(act);
    }
}
