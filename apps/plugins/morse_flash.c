/***************************************************************************
 * Morse Flash - encode, transmit and practise Morse code on Rockbox.
 * ENCODE  : arrows move over the picker, SELECT inserts, CANCEL deletes,
 *           EXIT returns to the menu.
 * TRANSMIT: LEFT/RIGHT changes the unit length (60/90/120/150 ms),
 *           SELECT sends, EXIT aborts.
 * DRILL   : LEFT/RIGHT picks an answer, UP replays, SELECT confirms;
 *           ten questions per round with score, streak and a summary.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 ***************************************************************************/
#include "plugin.h"
#include "lib/pluginlib_actions.h"
#include "lib/cjk_font.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#define MF_MSG_MAX     16
#define MF_PICK_COUNT  40
#define MF_QUIZ_COUNT  36
#define MF_TAPE_MAX    200
#define MF_QUESTIONS   10
#define MF_SPEEDS      4
#define MF_DIT         1
#define MF_DAH         3
#define MF_LETTER      4
#define MF_WORD        5
struct mf_morse { char ch; const char *code; };
static const struct mf_morse mf_morse_table[] =
{
    {'A', ".-"},     {'B', "-..."},   {'C', "-.-."},   {'D', "-.."},
    {'E', "."},      {'F', "..-."},   {'G', "--."},    {'H', "...."},
    {'I', ".."},     {'J', ".---"},   {'K', "-.-"},    {'L', ".-.."},
    {'M', "--"},     {'N', "-."},     {'O', "---"},    {'P', ".--."},
    {'Q', "--.-"},   {'R', ".-."},    {'S', "..."},    {'T', "-"},
    {'U', "..-"},    {'V', "...-"},   {'W', ".--"},    {'X', "-..-"},
    {'Y', "-.--"},   {'Z', "--.."},
    {'0', "-----"},  {'1', ".----"},  {'2', "..---"},  {'3', "...--"},
    {'4', "....-"},  {'5', "....."},  {'6', "-...."},  {'7', "--..."},
    {'8', "---.."},  {'9', "----."},
    {'.', ".-.-.-"}, {',', "--..--"}, {'?', "..--.."}, {'/', "-..-."},
    {'-', "-....-"}, {'(', "-.--."},  {')', "-.--.-"}, {' ', " "}
};
static const struct button_mapping *plugin_contexts[] = { pla_main_ctx };
static const char mf_picker[MF_PICK_COUNT] =
{
    'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J',
    'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T',
    'U', 'V', 'W', 'X', 'Y', 'Z',
    '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
    ' ', '.', '-', '\b'
};
static const char mf_quiz[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
static const int mf_speed[MF_SPEEDS] = { 60, 90, 120, 150 };
static char mf_msg[MF_MSG_MAX + 1] = "SOS";
static int mf_msg_len = 3;
static char mf_morse[MF_MSG_MAX * 8 + 8];
struct mf_tape { unsigned char e[MF_TAPE_MAX]; int head; int count; };
enum mf_view { MF_MENU, MF_ENCODE, MF_TRANSMIT, MF_DRILL, MF_QUIT };
static void mf_put(int x, int y, const char *s)
{ rb->lcd_putsxy(x, y, (const unsigned char *)s); }
static int mf_width(const char *s)
{
    int w = 0, h = 0;
    rb->lcd_getstringsize((const unsigned char *)s, &w, &h);
    return w;
}
static int mf_lh(void)
{
    int w = 0, h = 0;
    rb->lcd_getstringsize((const unsigned char *)"Ag", &w, &h);
    if (h < 6) h = 6;
    return h + 1;
}
static void mf_center(int y, const char *s)
{
    int x = (LCD_WIDTH - mf_width(s)) / 2;
    mf_put(x < 0 ? 0 : x, y, s);
}
static void mf_append(char *dst, int dstsz, const char *src)
{
    int len = (int)rb->strlen(dst);
    if (len < dstsz - 1) rb->strlcpy(dst + len, src, (size_t)(dstsz - len));
}
static const char *mf_code(char c)
{
    int i;
    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    for (i = 0; i < (int)ARRAYLEN(mf_morse_table); i++)
        if (mf_morse_table[i].ch == c) return mf_morse_table[i].code;
    return NULL;
}
static void mf_build_morse(const char *msg, char *out, int outsz)
{
    int i;
    bool first = true;
    out[0] = '\0';
    for (i = 0; msg[i] != '\0'; i++) {
        const char *code = mf_code(msg[i]);
        if (code == NULL) continue;
        if (!first) mf_append(out, outsz, " ");
        mf_append(out, outsz, (msg[i] == ' ') ? "/" : code);
        first = false;
    }
}
static int mf_wrap(int x, int y, int maxw, int maxlines, const char *s, int lh)
{
    char line[96];
    int lines = 0;
    while (s[0] != '\0' && lines < maxlines) {
        int n = 0, last = -1, w = 0, h = 0, len;
        while (s[n] != '\0' && n < (int)sizeof line - 1) {
            line[n] = s[n];
            line[n + 1] = '\0';
            rb->lcd_getstringsize((const unsigned char *)line, &w, &h);
            if (w > maxw) break;
            if (s[n] == ' ') last = n;
            n++;
        }
        if (s[n] == '\0') len = n;
        else if (last > 0) len = last;
        else len = (n > 0) ? n : 1;
        if (len > (int)sizeof line - 1) len = (int)sizeof line - 1;
        /* never split a UTF-8 sequence at the wrap point */
        while (len > 0 && ((unsigned char)s[len] & 0xC0) == 0x80)
            len--;
        rb->memcpy(line, s, (size_t)len);
        line[len] = '\0';
        rb->lcd_putsxy(x, y, (const unsigned char *)line);
        y += lh;
        lines++;
        s += len;
        while (s[0] == ' ') s++;
    }
    return y;
}
static long mf_ticks(int ms)
{
    long t = ((long)ms * HZ) / 1000;
    return (t < 1) ? 1 : t;
}
static void mf_backlight(void)
{
#ifdef HAVE_BACKLIGHT
    rb->backlight_on();
#endif
}
static void mf_flash(bool on)
{
#ifdef HAVE_BACKLIGHT
    if (on) rb->backlight_on();
    else rb->backlight_off();
#endif
#if LCD_DEPTH > 1
    if (on) { rb->lcd_set_foreground(LCD_WHITE); rb->lcd_set_background(LCD_BLACK); }
    else { rb->lcd_set_foreground(LCD_DEFAULT_FG); rb->lcd_set_background(LCD_DEFAULT_BG); }
#endif
    if (on) rb->lcd_fillrect(0, 0, LCD_WIDTH, LCD_HEIGHT);
    else rb->lcd_clear_display();
    rb->lcd_update();
}
/* Poll in one-tick slices so the wait yields and EXIT can interrupt. */
static int mf_wait(long deadline)
{
    while ((long)(*rb->current_tick - deadline) < 0) {
        int a = pluginlib_getaction(1, plugin_contexts,
                                    (int)ARRAYLEN(plugin_contexts));
        if (a == PLA_EXIT) return -1;
    }
    return 0;
}
static void mf_tape_push(struct mf_tape *t, unsigned char v)
{
    t->e[t->head] = v;
    t->head = (t->head + 1) % MF_TAPE_MAX;
    if (t->count < MF_TAPE_MAX) t->count++;
}
static void mf_draw_tape(const struct mf_tape *t, int y0, int ymax)
{
    int u = LCD_WIDTH / 44, bh = 6, gap = 3;
    int x = 0, y = y0, start, i;
    if (u < 3) u = 3;
    if (u > 8) u = 8;
    if (y + bh > ymax) return;
    start = (t->count >= MF_TAPE_MAX) ? t->head : 0;
    for (i = 0; i < t->count; i++) {
        unsigned char e = t->e[(start + i) % MF_TAPE_MAX];
        int w;
        if (e == MF_LETTER) {
            x += 2 * u;
            if (x >= LCD_WIDTH) { x = 0; y += bh + gap; }
            continue;
        }
        if (e == MF_WORD) {
            x += 6 * u;
            if (x >= LCD_WIDTH) { x = 0; y += bh + gap; }
            continue;
        }
        w = (e == MF_DAH) ? 3 * u : u;
        if (x + w > LCD_WIDTH) { x = 0; y += bh + gap; }
        if (y + bh > ymax) return;
        rb->lcd_fillrect(x, y, w, bh);
        x += w + u;
    }
}
static void mf_draw_tx(int playing, int speed, const struct mf_tape *tape, const char *footer)
{
    char buf[MF_MSG_MAX + 16];
    int lh = mf_lh();
    int footer_y = LCD_HEIGHT - lh - 2;
    int y;
    rb->lcd_clear_display();
    mf_center(2, "摩尔斯电码");
    y = 2 + lh + 2;
    if (playing) mf_put(2, y, "发报中");
    else {
        rb->snprintf(buf, sizeof buf, "速度 %d 毫秒  < >", mf_speed[speed]);
        mf_put(2, y, buf);
    }
    y += lh + 2;
    if (mf_msg_len > 0) rb->snprintf(buf, sizeof buf, "电文: %s", mf_msg);
    else rb->strlcpy(buf, "电文: (空)", sizeof buf);
    y = mf_wrap(2, y, LCD_WIDTH - 4, 2, buf, lh);
    mf_draw_tape(tape, y + 3, footer_y - 2);
    mf_put(2, footer_y, footer);
    rb->lcd_update();
}
static int mf_send(const char *msg, int unit_ms, struct mf_tape *tape)
{
    long tu = mf_ticks(unit_ms);
    int i;
    for (i = 0; msg[i] != '\0'; i++) {
        const char *code = mf_code(msg[i]);
        int k;
        if (code == NULL) continue;
        if (msg[i] == ' ') {
            mf_tape_push(tape, MF_WORD);
            mf_draw_tx(1, 0, tape, "退出停止");
            if (mf_wait(*rb->current_tick + 6 * tu) < 0) return -1;
            continue;
        }
        for (k = 0; code[k] != '\0'; k++) {
            int units = (code[k] == '-') ? 3 : 1;
            long t0;
            mf_tape_push(tape, (units == 3) ? MF_DAH : MF_DIT);
            t0 = *rb->current_tick;
            mf_flash(true);
            if (mf_wait(t0 + units * tu) < 0) { mf_flash(false); return -1; }
            mf_flash(false);
            mf_draw_tx(1, 0, tape, "退出停止");
            if (mf_wait(t0 + (units + 1) * tu) < 0) return -1;
        }
        /* The element gap is spent; two more units complete the letter gap. */
        if (msg[i + 1] != '\0' && msg[i + 1] != ' ') {
            mf_tape_push(tape, MF_LETTER);
            mf_draw_tx(1, 0, tape, "退出停止");
            if (mf_wait(*rb->current_tick + 2 * tu) < 0) return -1;
        }
    }
    return 0;
}
static int transmit_view(void)
{
    struct mf_tape tape;
    int speed = 1, result = 0;
    tape.head = tape.count = 0;
    for (;;) {
        const char *footer;
        int a;
        switch (result) {
            case 1:  footer = "已发送! 再按选择"; break;
            case 2:  footer = "已中止";          break;
            case 3:  footer = "电文为空";     break;
            default: footer = "选择发报 退出菜单"; break;
        }
        mf_draw_tx(0, speed, &tape, footer);
        a = pluginlib_getaction(TIMEOUT_BLOCK, plugin_contexts,
                                (int)ARRAYLEN(plugin_contexts));
        if (a == PLA_EXIT || a == PLA_CANCEL) return MF_MENU;
        if (a == PLA_LEFT || a == PLA_LEFT_REPEAT) {
            if (speed > 0) speed--;
            result = 0;
        } else if (a == PLA_RIGHT || a == PLA_RIGHT_REPEAT) {
            if (speed < MF_SPEEDS - 1) speed++;
            result = 0;
        } else if (a == PLA_SELECT) {
            if (mf_msg_len < 1) { result = 3; continue; }
            tape.head = tape.count = 0;
            result = (mf_send(mf_msg, mf_speed[speed], &tape) < 0) ? 2 : 1;
            mf_flash(false);
            mf_backlight();
            rb->button_clear_queue();
        }
    }
}
static int mf_cols(void)
{ return (LCD_WIDTH >= 280) ? 8 : 5; }
static void mf_label(int item, char *out, int outsz)
{
    if (item == MF_PICK_COUNT - 1) rb->strlcpy(out, "删", outsz);
    else if (mf_picker[item] == ' ') rb->strlcpy(out, "空", outsz);
    else { out[0] = mf_picker[item]; out[1] = '\0'; }
}
static void mf_backspace(void)
{
    if (mf_msg_len > 0) mf_msg[--mf_msg_len] = '\0';
}
static void mf_insert(int item)
{
    if (item == MF_PICK_COUNT - 1) { mf_backspace(); return; }
    if (mf_msg_len < MF_MSG_MAX) {
        mf_msg[mf_msg_len++] = mf_picker[item];
        mf_msg[mf_msg_len] = '\0';
    }
}
static void mf_draw_encode(int sel)
{
    char buf[MF_MSG_MAX + 16];
    char mbuf[MF_MSG_MAX * 8 + 16];
    char label[8];
    int lh = mf_lh();
    int cols = mf_cols();
    int rows = (MF_PICK_COUNT + cols - 1) / cols;
    int footer_y = LCD_HEIGHT - lh - 2;
    int cell_w = LCD_WIDTH / cols;
    int cell_h = lh + 2;
    int max_rows, sel_row, row_off, y, r, c;
    rb->lcd_clear_display();
    mf_center(2, "摩尔斯电码");
    y = 2 + lh + 2;
    if (mf_msg_len > 0) rb->snprintf(buf, sizeof buf, "电文: %s", mf_msg);
    else rb->strlcpy(buf, "电文: (空)", sizeof buf);
    y = mf_wrap(2, y, LCD_WIDTH - 4, 2, buf, lh) + 2;
    mf_build_morse(mf_msg, mf_morse, (int)sizeof mf_morse);
    rb->snprintf(mbuf, sizeof mbuf, "摩尔斯: %s", mf_morse);
    y = mf_wrap(2, y, LCD_WIDTH - 4, 4, mbuf, lh) + 4;
    max_rows = (footer_y - y - 2) / cell_h;
    if (max_rows < 1) max_rows = 1;
    if (max_rows > rows) max_rows = rows;
    sel_row = sel / cols;
    row_off = (sel_row >= max_rows) ? sel_row - max_rows + 1 : 0;
    if (row_off > rows - max_rows) row_off = rows - max_rows;
    if (row_off < 0) row_off = 0;
    for (r = 0; r < max_rows; r++) {
        for (c = 0; c < cols; c++) {
            int item = (row_off + r) * cols + c;
            int x = c * cell_w;
            int cy = y + r * cell_h;
            int tw;
            if (item >= MF_PICK_COUNT) break;
            mf_label(item, label, (int)sizeof label);
            tw = mf_width(label);
            mf_put(x + (cell_w - tw) / 2, cy + 1, label);
            if (item == sel && cell_w > 4 && cell_h > 5)
                rb->lcd_drawrect(x + 1, cy, cell_w - 2, cell_h - 1);
        }
    }
    mf_put(2, footer_y, "选择:添加 返回:删除");
    rb->lcd_update();
}
static int encode_view(void)
{
    int sel = 0;
    for (;;) {
        int a, cols;
        mf_draw_encode(sel);
        a = pluginlib_getaction(TIMEOUT_BLOCK, plugin_contexts,
                                (int)ARRAYLEN(plugin_contexts));
        cols = mf_cols();
        if (a == PLA_EXIT) return MF_MENU;
        else if (a == PLA_UP || a == PLA_UP_REPEAT)
            sel = (sel + MF_PICK_COUNT - cols) % MF_PICK_COUNT;
        else if (a == PLA_DOWN || a == PLA_DOWN_REPEAT)
            sel = (sel + cols) % MF_PICK_COUNT;
        else if (a == PLA_LEFT || a == PLA_LEFT_REPEAT)
            sel = (sel + MF_PICK_COUNT - 1) % MF_PICK_COUNT;
        else if (a == PLA_RIGHT || a == PLA_RIGHT_REPEAT)
            sel = (sel + 1) % MF_PICK_COUNT;
        else if (a == PLA_SELECT) mf_insert(sel);
        else if (a == PLA_CANCEL) mf_backspace();
    }
}
static int mf_play(const char *code, int unit_ms)
{
    long tu = mf_ticks(unit_ms);
    int k;
    for (k = 0; code[k] != '\0'; k++) {
        int units = (code[k] == '-') ? 3 : 1;
        long t0 = *rb->current_tick;
        mf_flash(true);
        if (mf_wait(t0 + units * tu) < 0) { mf_flash(false); return -1; }
        mf_flash(false);
        if (mf_wait(t0 + (units + 1) * tu) < 0) return -1;
    }
    return 0;
}
static int mf_play_target(char target)
{
    const char *code = mf_code(target);
    int rc = (code != NULL) ? mf_play(code, 90) : 0;
    mf_flash(false);
    mf_backlight();
    return rc;
}
static char mf_question(char cand[4])
{
    int t = rb->rand() % MF_QUIZ_COUNT;
    int step = 5 + 2 * (rb->rand() % 3);   /* 5, 7 or 9: 4 distinct picks */
    char target = mf_quiz[t];
    int i;
    cand[0] = target;
    for (i = 1; i < 4; i++)
        cand[i] = mf_quiz[(t + i * step) % MF_QUIZ_COUNT];
    for (i = 3; i > 0; i--) {
        int r = rb->rand() % (i + 1);
        char tmp = cand[i];
        cand[i] = cand[r];
        cand[r] = tmp;
    }
    return target;
}
static void mf_draw_drill(int q, int score, int streak, const char cand[4],
                          int sel, int answered, int correct, char target)
{
    char buf[48];
    int lh = mf_lh();
    int footer_y = LCD_HEIGHT - lh - 2;
    int box_w = LCD_WIDTH / 4;
    int box_h = lh + 8;
    int y, by, i;
    rb->lcd_clear_display();
    mf_center(2, "摩尔斯听辨");
    y = 2 + lh + 2;
    rb->snprintf(buf, sizeof buf, "第 %d/%d 题  得分 %d", q + 1, MF_QUESTIONS, score);
    mf_put(2, y, buf);
    y += lh;
    rb->snprintf(buf, sizeof buf, "连击 %d", streak);
    mf_put(2, y, buf);
    y += lh + 4;
    if (answered) mf_put(2, y, correct ? "正确!" : "错误!");
    else mf_put(2, y, "你听到了什么?");
    by = y + lh + 10;
    if (by + box_h > footer_y - lh - 4) by = footer_y - lh - 4 - box_h;
    if (by < y + lh) by = y + lh;
    for (i = 0; i < 4; i++) {
        int x = i * box_w;
        char label[2];
        label[0] = cand[i];
        label[1] = '\0';
        mf_put(x + (box_w - mf_width(label)) / 2, by + 4, label);
        if (i == sel) rb->lcd_drawrect(x + 2, by, box_w - 4, box_h);
    }
    if (answered && !correct) {
        rb->snprintf(buf, sizeof buf, "答案是 %c", target);
        mf_put(2, by + box_h + 4, buf);
    } else if (!answered) mf_put(2, by + box_h + 4, "左右选择 选择确认");
    mf_put(2, footer_y, answered ? "下一题..." : "上:重播 退出");
    rb->lcd_update();
}
static void mf_summary(int score, int best)
{
    char buf[40];
    int lh = mf_lh();
    int y;
    rb->lcd_clear_display();
    mf_center(2, "本轮结束");
    y = 2 + lh * 3;
    rb->snprintf(buf, sizeof buf, "得分 %d / %d", score, MF_QUESTIONS);
    mf_center(y, buf);
    y += lh + 2;
    rb->snprintf(buf, sizeof buf, "最佳连击 %d", best);
    mf_center(y, buf);
    y += lh + 2;
    mf_center(y, (score == MF_QUESTIONS) ? "完美!" : "继续练习");
    mf_center(LCD_HEIGHT - lh - 2, "选择:菜单");
    rb->lcd_update();
}
static int drill_view(void)
{
    int q, score = 0, streak = 0, best = 0;
    char cand[4];
    for (q = 0; q < MF_QUESTIONS; q++) {
        char target = mf_question(cand);
        int sel = 0;
        if (mf_play_target(target) < 0) return MF_MENU;
        for (;;) {
            int a, correct;
            mf_draw_drill(q, score, streak, cand, sel, 0, 0, target);
            a = pluginlib_getaction(TIMEOUT_BLOCK, plugin_contexts,
                                    (int)ARRAYLEN(plugin_contexts));
            if (a == PLA_EXIT || a == PLA_CANCEL) return MF_MENU;
            if (a == PLA_LEFT || a == PLA_LEFT_REPEAT
#ifdef HAVE_SCROLLWHEEL
                || a == PLA_SCROLL_BACK || a == PLA_SCROLL_BACK_REPEAT
#endif
                ) sel = (sel + 3) % 4;
            else if (a == PLA_RIGHT || a == PLA_RIGHT_REPEAT
#ifdef HAVE_SCROLLWHEEL
                || a == PLA_SCROLL_FWD || a == PLA_SCROLL_FWD_REPEAT
#endif
                ) sel = (sel + 1) % 4;
            else if (a == PLA_UP) {
                if (mf_play_target(target) < 0) return MF_MENU;
            } else if (a == PLA_SELECT) {
                correct = (cand[sel] == target) ? 1 : 0;
                if (correct) {
                    score++;
                    streak++;
                    if (streak > best) best = streak;
                } else streak = 0;
                mf_draw_drill(q, score, streak, cand, sel, 1, correct, target);
                if (mf_wait(*rb->current_tick + mf_ticks(900)) < 0)
                    return MF_MENU;
                break;
            }
        }
    }
    mf_summary(score, best);
    for (;;) {
        int a = pluginlib_getaction(TIMEOUT_BLOCK, plugin_contexts,
                                    (int)ARRAYLEN(plugin_contexts));
        if (a == PLA_EXIT || a == PLA_CANCEL || a == PLA_SELECT)
            return MF_MENU;
    }
}
static void mf_draw_menu(int sel)
{
    static const char *items[] = { "编码", "发报", "听辨", "退出" };
    int lh = mf_lh();
    int item_h = lh + 6;
    int total = (int)ARRAYLEN(items) * item_h;
    int y0 = (LCD_HEIGHT - total) / 2;
    int i;
    rb->lcd_clear_display();
    mf_center(2, "摩尔斯电码");
    for (i = 0; i < (int)ARRAYLEN(items); i++) {
        int ty = y0 + i * item_h;
        int tw = mf_width(items[i]);
        int tx = (LCD_WIDTH - tw) / 2;
        if (tx < 0) tx = 0;
        mf_put(tx, ty, items[i]);
        if (i == sel && tx > 6 && tw + 12 < LCD_WIDTH)
            rb->lcd_drawrect(tx - 6, ty - 2, tw + 12, lh + 4);
    }
    mf_put(2, LCD_HEIGHT - lh - 2, "上下选择 退出");
    rb->lcd_update();
}
static int menu_view(void)
{
    int sel = 0;
    for (;;) {
        int a;
        mf_draw_menu(sel);
        a = pluginlib_getaction(TIMEOUT_BLOCK, plugin_contexts,
                                (int)ARRAYLEN(plugin_contexts));
        if (a == PLA_EXIT || a == PLA_CANCEL) return MF_QUIT;
        if (a == PLA_UP || a == PLA_UP_REPEAT) sel = (sel + 3) % 4;
        else if (a == PLA_DOWN || a == PLA_DOWN_REPEAT) sel = (sel + 1) % 4;
#ifdef HAVE_SCROLLWHEEL
        else if (a == PLA_SCROLL_BACK || a == PLA_SCROLL_BACK_REPEAT)
            sel = (sel + 3) % 4;
        else if (a == PLA_SCROLL_FWD || a == PLA_SCROLL_FWD_REPEAT)
            sel = (sel + 1) % 4;
#endif
        else if (a == PLA_SELECT) {
            if (sel == 0) return MF_ENCODE;
            if (sel == 1) return MF_TRANSMIT;
            if (sel == 2) return MF_DRILL;
            return MF_QUIT;
        }
    }
}
enum plugin_status plugin_start(const void *parameter)
{
    enum mf_view view = MF_MENU;
    int font_id;
    (void)parameter;
#if LCD_DEPTH > 1
    rb->lcd_set_foreground(LCD_DEFAULT_FG);
    rb->lcd_set_background(LCD_DEFAULT_BG);
#endif
    rb->lcd_set_drawmode(DRMODE_SOLID);
    font_id = rb_cjk_load();
    rb->srand((unsigned int)*rb->current_tick);
    while (view != MF_QUIT) {
        switch (view) {
            case MF_ENCODE:   view = encode_view();   break;
            case MF_TRANSMIT: view = transmit_view(); break;
            case MF_DRILL:    view = drill_view();    break;
            default:          view = menu_view();     break;
        }
    }
    rb->lcd_clear_display();
    rb->lcd_update();
    mf_backlight();
    rb_cjk_unload(font_id);
    return PLUGIN_OK;
}
