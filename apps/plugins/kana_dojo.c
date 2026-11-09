/***************************************************************************
 * Kana Dojo - a Japanese kana reference chart and quiz for Rockbox.
 *
 * The reference view shows a 5 x 10 gojuuon grid (a, ka, sa, ta, na, ha,
 * ma, ya, ra, wa rows) plus the standalone "n", which sits in the last
 * column of the last row.  The view cycles between hiragana, katakana and
 * both scripts drawn on top of each other.  The quiz view shows one kana
 * big and asks for its romaji reading, ten questions per round.
 *
 * The kana are drawn from the real 16-GNU-Unifont glyph bitmaps, loaded
 * from /.rockbox/fonts/ at start-up and scaled to the cell size at run
 * time, so no hand-drawn line segments are involved.
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
#include "lib/cjk_font.h"

#define KANA_COUNT 46
#define GRID_ROWS 10
#define GRID_COLS 5
#define QUIZ_LEN 10
#define KANA_MAX_PX 96
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

/* Kana are real GNU Unifont characters, not hand-drawn line segments. */
static const char *const kana_hira[KANA_COUNT] = {
    "あ", "い", "う", "え", "お",
    "か", "き", "く", "け", "こ",
    "さ", "し", "す", "せ", "そ",
    "た", "ち", "つ", "て", "と",
    "な", "に", "ぬ", "ね", "の",
    "は", "ひ", "ふ", "へ", "ほ",
    "ま", "み", "む", "め", "も",
    "や", "ゆ", "よ", "ら", "り",
    "る", "れ", "ろ", "わ", "を",
    "ん",
};

static const char *const kana_kata[KANA_COUNT] = {
    "ア", "イ", "ウ", "エ", "オ",
    "カ", "キ", "ク", "ケ", "コ",
    "サ", "シ", "ス", "セ", "ソ",
    "タ", "チ", "ツ", "テ", "ト",
    "ナ", "ニ", "ヌ", "ネ", "ノ",
    "ハ", "ヒ", "フ", "ヘ", "ホ",
    "マ", "ミ", "ム", "メ", "モ",
    "ヤ", "ユ", "ヨ", "ラ", "リ",
    "ル", "レ", "ロ", "ワ", "ヲ",
    "ン",
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

static int kana_font = FONT_UI;

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

/* Draw one kana from the loaded GNU Unifont, scaled to fit a square box.
 * The font bitmap is column major, one bit per pixel, LSB = top row. */
static void draw_kana(int x, int y, int size, int script, int idx)
{
    static unsigned char scaled[KANA_MAX_PX * ((KANA_MAX_PX + 7) / 8)];
    const char *txt = (script == SCRIPT_KATA) ? kana_kata[idx] : kana_hira[idx];
    struct font *pf = rb->font_get(kana_font);
    const unsigned char *bits;
    ucschar_t cp = 0;
    int sw, sh, tx, ty, dm;

    if (size > KANA_MAX_PX)
        size = KANA_MAX_PX;
    if (size < 4 || pf == NULL)
        return;
    rb->utf8decode((const unsigned char *)txt, &cp);
    bits = rb->font_get_bits(pf, cp);
    if (bits == NULL)
        return;
    sw = rb->font_get_width(pf, cp);
    sh = (int)pf->height;
    if (sw <= 0 || sh <= 0)
        return;
    rb->memset(scaled, 0, (size_t)(size * ((size + 7) / 8)));
    for (tx = 0; tx < size; tx++) {
        int sx = tx * sw / size;
        for (ty = 0; ty < size; ty++) {
            int sy = ty * sh / size;
            if (bits[(sy / 8) * sw + sx] & (1 << (sy & 7)))
                scaled[(ty / 8) * size + tx] |= (unsigned char)(1 << (ty & 7));
        }
    }
    dm = rb->lcd_get_drawmode();
    rb->lcd_set_drawmode(DRMODE_FG);
    rb->lcd_mono_bitmap_part(scaled, 0, 0, size, x, y, size, size);
    rb->lcd_set_drawmode(dm);
}

static const char *view_name(int view)
{
    if (view == VIEW_KATA)
        return "片假名";
    if (view == VIEW_BOTH)
        return "对照";
    return "平假名";
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

    rb->lcd_putsxy(2, 1, (const unsigned char *)(quiz ? "假名测验"
                                                      : "五十音道场"));
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

    u = (const unsigned char *)"选择:切换 长按:测验";
    rb->lcd_getstringsize(u, &w, &h);
    if (w > LCD_WIDTH - 40) {
        u = (const unsigned char *)"选择:切换 长按:测验";
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
        rb->snprintf(buf, sizeof(buf), "测验完成");
        puts_centered(ty, buf);
        ty += L.ch + 6;
        rb->snprintf(buf, sizeof(buf), "得分 %d / %d", q_score, QUIZ_LEN);
        puts_centered(ty, buf);
        ty += L.ch + 6;
        rb->snprintf(buf, sizeof(buf), "正确率 %d%%",
                     q_score * 100 / QUIZ_LEN);
        puts_centered(ty, buf);
        ty += L.ch + 6;
        rb->snprintf(buf, sizeof(buf), "最佳连击 %d", q_best_streak);
        puts_centered(ty, buf);
        ty += L.ch + 6;
        rb->snprintf(buf, sizeof(buf), "历史最佳 %d", best_ever);
        puts_centered(ty, buf);
        ty += L.ch + 10;
        puts_centered(ty, "选择:再来 上:图表");
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
        u = (const unsigned char *)(q_last_right ? "正确!" : "错误");
        rb->lcd_putsxy(4, area_y, u);
        rb->snprintf(buf, sizeof(buf), "答案: %s", romaji[q_kana]);
        rb->lcd_putsxy(4, area_y + L.ch + 3, (const unsigned char *)buf);
        rb->lcd_putsxy(4, area_y + 2 * (L.ch + 3),
                       (const unsigned char *)"选择:下一题");
    }

    rb->lcd_hline(0, LCD_WIDTH - 1, L.status_y - 1);
    rb->snprintf(buf, sizeof(buf), "得分 %d  连击 %d", q_score, q_streak);
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
    int font_id;

    (void)parameter;

    font_id = rb_cjk_load();
    kana_font = (font_id >= 0) ? font_id : FONT_UI;
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
        if (act == PLA_EXIT || act == PLA_CANCEL) {
            rb_cjk_unload(font_id);
            return PLUGIN_OK;
        }

        if (quiz_active)
            quiz_handle(act);
        else
            ref_handle(act);
    }
}
