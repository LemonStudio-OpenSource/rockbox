/***************************************************************************
 * cube_scramble.c - speedcubing companion for Rockbox
 *
 * WCA style scramble generator (3x3x3, 2x2x2, 4x4x4, Pyraminx) plus a
 * stackmat style solve timer with session statistics.
 *
 * Licensed under the GNU General Public License, version 2 or later.
 ****************************************************************************/
#include "plugin.h"
#include "lib/pluginlib_actions.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#define NUM_PUZZLES     4
#define MAX_MOVES       40
#define MAX_SOLVES      12
#define MAX_WRAP_LINES  16
#define MENU_ITEMS      4
#define MARGIN          4
#define STATS_PATH      ROCKBOX_DIR "/cube_scramble.txt"
#define ARM_TICKS       (HZ * 4 / 10)   /* 400 ms arming delay */
enum puzzle_id { PUZZLE_3X3 = 0, PUZZLE_2X2, PUZZLE_4X4, PUZZLE_PYRA };
enum view_id { VIEW_MENU = 0, VIEW_SCRAMBLE, VIEW_TIMER };
enum timer_state { TIMER_IDLE = 0, TIMER_ARMED, TIMER_RUNNING, TIMER_STOPPED };
static const struct button_mapping *plugin_contexts[] = { pla_main_ctx };
/* display names and the short keys used in the statistics file */
static const char * const puzzle_name[NUM_PUZZLES] =
    { "3x3x3", "2x2x2", "4x4x4", "Pyraminx" };
static const char * const puzzle_key[NUM_PUZZLES] =
    { "3x3", "2x2", "4x4", "pyraminx" };
static const int puzzle_moves[NUM_PUZZLES] = { 20, 11, 40, 11 };
static const char * const menu_items[MENU_ITEMS] =
    { "Scramble", "Timer", "", "Quit" };   /* row 2 is the puzzle name */
/* face coding: 0=U 1=D 2=L 3=R 4=F 5=B */
static const char face_chars[6] = { 'U', 'D', 'L', 'R', 'F', 'B' };
static const int face_axis[6] = { 0, 0, 1, 1, 2, 2 };
static const int faces_all[6] = { 0, 1, 2, 3, 4, 5 };
static const int faces_ruf[3] = { 3, 0, 4 };      /* WCA 2x2: R, U, F */
static const int faces_pyra[4] = { 0, 2, 3, 5 };  /* Pyraminx: U, L, R, B */
static int g_puzzle = PUZZLE_3X3, g_view = VIEW_MENU, g_menu_sel = 0;
static int g_page = 0, g_pages = 1, g_lines = 0;
static int g_fh = 8, g_lh = 9;      /* font height and line height */
static char g_moves[MAX_MOVES][4];
static int g_move_count = 0, g_move_w[MAX_MOVES];
static int g_line_start[MAX_WRAP_LINES], g_line_num[MAX_WRAP_LINES];
static int g_timer_state = TIMER_IDLE;
static long g_timer_start = 0, g_last_cs = 0;
/* per puzzle session history and statistics */
static int g_hist[NUM_PUZZLES][MAX_SOLVES], g_hist_count[NUM_PUZZLES];
static long g_total_cs[NUM_PUZZLES];
static int g_total_count[NUM_PUZZLES];
static int g_best[NUM_PUZZLES], g_best_avg5[NUM_PUZZLES];
static char g_iobuf[512];           /* statistics file buffer */
static uint32_t g_rng = 0x12345678u;
static void draw_menu(void), draw_scramble(void), draw_timer(void);

/* --- pseudo random numbers (no rand()/srand() dependency) ------------- */
static uint32_t prng_next(void)
{
    uint32_t x = g_rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    g_rng = x;
    return x;
}
static int prng_bounded(int n)
{
    if (n <= 1)
        return 0;
    return (int)(prng_next() % (uint32_t)n);
}
static void prng_seed(void)
{
    uint32_t s = (uint32_t)*rb->current_tick;
    s = s * 2654435761u + 0x9E3779B9u;
    s ^= s >> 16;
    g_rng = (s != 0) ? s : 0x12345678u;
}

/* --- text helpers ----------------------------------------------------- */
static void lcd_text(int x, int y, const char *s)
{
    rb->lcd_putsxy(x, y, (const unsigned char *)s);
}
static int text_width(const char *s)
{
    int w = 0, h = 0;
    rb->lcd_getstringsize((const unsigned char *)s, &w, &h);
    return w;
}
static void draw_header(const char *right)
{
    int y = 2, w, x;
    lcd_text(MARGIN, y, "CUBE SCRAMBLE");
    if (right != NULL && right[0] != '\0')
    {
        w = text_width(right);
        x = LCD_WIDTH - MARGIN - w;
        if (x > MARGIN)
            lcd_text(x, y, right);
    }
    rb->lcd_hline(0, LCD_WIDTH - 1, y + g_fh + 2);
}
/* centiseconds as SS.CC, or M:SS.CC when needed */
static void fmt_time(long cs, char *buf, int buflen)
{
    long m, s, c;
    if (cs < 0)
        cs = 0;
    m = cs / 6000;
    s = (cs / 100) % 60;
    c = cs % 100;
    if (m > 0)
        rb->snprintf(buf, (size_t)buflen, "%ld:%02ld.%02ld", m, s, c);
    else
        rb->snprintf(buf, (size_t)buflen, "%ld.%02ld", s, c);
}
static void text_center(int y, const char *s)
{
    int x = (LCD_WIDTH - text_width(s)) / 2;
    lcd_text(x < 0 ? 0 : x, y, s);
}
static void fmt_or_dash(int cs, char *buf, int buflen)
{
    if (cs > 0)
        fmt_time(cs, buf, buflen);
    else
        rb->strlcpy(buf, "--", (size_t)buflen);
}

/* --- scramble generation ---------------------------------------------- */
static const int *allowed_faces(int puzzle, int *nfaces)
{
    if (puzzle == PUZZLE_PYRA) { *nfaces = 4; return faces_pyra; }
    if (puzzle == PUZZLE_2X2)  { *nfaces = 3; return faces_ruf; }
    *nfaces = 6;
    return faces_all;
}
static int axis_of(int puzzle, int face)
{
    if (puzzle == PUZZLE_PYRA)      /* U/B opposite, L/R opposite */
        return (face == 0 || face == 5) ? 0 : 1;
    return face_axis[face];
}
static void gen_scramble(void)
{
    const int *faces;
    int nfaces, count = puzzle_moves[g_puzzle];
    int last_face = -1, last_axis = -1, axis_run = 0, i;
    faces = allowed_faces(g_puzzle, &nfaces);
    for (i = 0; i < count; i++)
    {
        char *m = g_moves[i];
        int start = prng_bounded(nfaces), face;
        int len = 0, j, ax;
        face = faces[start];
        /* scan from a random start for the first legal face */
        for (j = 0; j < nfaces; j++)
        {
            int cand = faces[(start + j) % nfaces];
            if (cand == last_face)
                continue;
            if (axis_of(g_puzzle, cand) == last_axis && axis_run >= 2)
                continue;
            face = cand;
            break;
        }
        m[len++] = face_chars[face];
        if (g_puzzle == PUZZLE_4X4 && prng_bounded(100) < 40)
            m[len++] = 'w';         /* 4x4 wide move */
        switch (prng_bounded(3))
        {
            case 1: m[len++] = '2';  break;
            case 2: m[len++] = '\''; break;
        }
        m[len] = '\0';
        ax = axis_of(g_puzzle, face);
        if (ax == last_axis) axis_run++;
        else { last_axis = ax; axis_run = 1; }
        last_face = face;
    }
    g_move_count = count;
    g_page = 0;
}
/* lay the current scramble out on lines that fit the display width */
static void wrap_scramble(void)
{
    int maxw = LCD_WIDTH - 2 * MARGIN;
    int space_w, n = 0, cur = -1, cur_w = 0, i;
    if (maxw < 1)
        maxw = 1;
    space_w = text_width(" ");
    if (space_w < 0)
        space_w = 0;
    for (i = 0; i < g_move_count; i++)
    {
        g_move_w[i] = text_width(g_moves[i]);
        if (cur < 0)
        { cur = 0; n = 1; g_line_start[0] = i; g_line_num[0] = 1; cur_w = g_move_w[i]; }
        else if (n < MAX_WRAP_LINES && cur_w + space_w + g_move_w[i] <= maxw)
        { g_line_num[cur]++; cur_w += space_w + g_move_w[i]; }
        else if (n < MAX_WRAP_LINES)
        { g_line_start[n] = i; g_line_num[n] = 1; cur = n++; cur_w = g_move_w[i]; }
        else
            g_line_num[cur]++;      /* only reachable on very small screens */
    }
    g_lines = n;
}

/* --- statistics storage ----------------------------------------------- */
static int parse_kv(const char *line, const char *key)
{
    const char *v = rb->strstr(line, key);
    if (v == NULL)
        return 0;
    return rb->atoi(v + rb->strlen(key));
}
static void load_stats(void)
{
    int fd, n, i;
    char *p, *eol;
    fd = rb->open(STATS_PATH, O_RDONLY);
    if (fd < 0)
        return;
    n = (int)rb->read(fd, g_iobuf, sizeof(g_iobuf) - 1);
    rb->close(fd);
    if (n <= 0)
        return;
    g_iobuf[n] = '\0';
    for (p = g_iobuf; *p != '\0'; p = eol)
    {
        eol = p;
        while (*eol != '\0' && *eol != '\n' && *eol != '\r')
            eol++;
        if (*eol != '\0')
            *eol++ = '\0';
        for (i = 0; i < NUM_PUZZLES; i++)
        {
            int klen = (int)rb->strlen(puzzle_key[i]);
            int b, a;
            if ((int)rb->strlen(p) <= klen ||
                rb->strncmp(p, puzzle_key[i], (size_t)klen) != 0 ||
                p[klen] != ' ')
                continue;
            b = parse_kv(p, "best=");
            a = parse_kv(p, "avg5=");
            if (b > 0) g_best[i] = b;
            if (a > 0) g_best_avg5[i] = a;
        }
    }
}
static void save_stats(void)
{
    int fd;
    rb->snprintf(g_iobuf, sizeof(g_iobuf),
                 "%s best=%d avg5=%d\n"
                 "%s best=%d avg5=%d\n"
                 "%s best=%d avg5=%d\n"
                 "%s best=%d avg5=%d\n",
                 puzzle_key[0], g_best[0], g_best_avg5[0],
                 puzzle_key[1], g_best[1], g_best_avg5[1],
                 puzzle_key[2], g_best[2], g_best_avg5[2],
                 puzzle_key[3], g_best[3], g_best_avg5[3]);
    fd = rb->open(STATS_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd >= 0)
    {
        (void)rb->write(fd, g_iobuf, rb->strlen(g_iobuf));
        rb->close(fd);
    }
}
static int calc_avg5(int puzzle)
{
    int n = g_hist_count[puzzle];
    int start = n - 5, sum = 0, mn, mx, i;
    if (n < 5)
        return 0;
    mn = g_hist[puzzle][start];
    mx = mn;
    for (i = start; i < n; i++)
    {
        int v = g_hist[puzzle][i];
        sum += v;
        if (v < mn) mn = v;
        if (v > mx) mx = v;
    }
    return (sum - mn - mx) / 3;
}
static void record_solve(int puzzle, int cs)
{
    int need_save = 0, i, n;
    if (cs < 0)
        cs = 0;
    n = g_hist_count[puzzle];
    if (n < MAX_SOLVES)
    { g_hist[puzzle][n] = cs; g_hist_count[puzzle] = n + 1; }
    else
    {
        for (i = 0; i < MAX_SOLVES - 1; i++)
            g_hist[puzzle][i] = g_hist[puzzle][i + 1];
        g_hist[puzzle][MAX_SOLVES - 1] = cs;
    }
    g_total_cs[puzzle] += cs;
    g_total_count[puzzle]++;
    if (g_best[puzzle] == 0 || cs < g_best[puzzle])
    { g_best[puzzle] = cs; need_save = 1; }
    if (g_hist_count[puzzle] >= 5)
    {
        int avg = calc_avg5(puzzle);
        if (g_best_avg5[puzzle] == 0 || avg < g_best_avg5[puzzle])
        { g_best_avg5[puzzle] = avg; need_save = 1; }
    }
    if (need_save)
        save_stats();
}

/* --- timer helpers ---------------------------------------------------- */
static long ticks_to_cs(long t)
{
    if (t < 0)
        return 0;
    return (t / HZ) * 100 + ((t % HZ) * 100) / HZ;
}
static long timer_elapsed_cs(void)
{
    if (g_timer_state != TIMER_RUNNING)
        return g_last_cs;
    return ticks_to_cs(*rb->current_tick - g_timer_start);
}
static void cycle_puzzle(int delta)
{
    g_puzzle = (g_puzzle + NUM_PUZZLES + delta) % NUM_PUZZLES;
    gen_scramble();
}
static void timer_arm_and_start(void)
{
    long arm_start;
    g_timer_state = TIMER_ARMED;
    g_last_cs = 0;
    draw_timer();                       /* READY */
    arm_start = *rb->current_tick;
    while (*rb->current_tick - arm_start < ARM_TICKS)
        rb->sleep(HZ / 100 + 1);
    rb->button_clear_queue();
    g_timer_start = *rb->current_tick;
    g_timer_state = TIMER_RUNNING;
}

/* --- drawing ---------------------------------------------------------- */
static void draw_scramble(void)
{
    char line[48];
    int info_y, top, footer1, footer2, vis, y, l, k;
    rb->lcd_clear_display();
    draw_header(puzzle_name[g_puzzle]);
    info_y = 2 + g_fh + 4;
    rb->snprintf(line, sizeof(line), "%s  %d moves",
                 puzzle_name[g_puzzle], g_move_count);
    lcd_text(MARGIN, info_y, line);
    wrap_scramble();
    top = info_y + g_lh + 2;
    footer2 = LCD_HEIGHT - g_fh - 2;
    footer1 = footer2 - g_lh;
    vis = (footer1 - 2 - top) / g_lh;
    if (vis < 1)
        vis = 1;
    g_pages = (g_lines + vis - 1) / vis;
    if (g_pages < 1) g_pages = 1;
    if (g_page >= g_pages) g_page = g_pages - 1;
    if (g_page < 0) g_page = 0;
    y = top;
    for (l = g_page * vis; l < g_lines && l < (g_page + 1) * vis; l++)
    {
        int x = MARGIN;
        for (k = 0; k < g_line_num[l]; k++)
        {
            int idx = g_line_start[l] + k;
            if (k > 0)
                x += text_width(" ");
            lcd_text(x, y, g_moves[idx]);
            x += g_move_w[idx];
        }
        y += g_lh;
    }
    lcd_text(MARGIN, footer1, "SEL:new  L/R:puzzle");
    if (g_pages > 1)
        rb->snprintf(line, sizeof(line), "UP/DN p%d/%d  CANCEL",
                     g_page + 1, g_pages);
    else
        rb->strlcpy(line, "CANCEL:menu", sizeof(line));
    lcd_text(MARGIN, footer2, line);
    rb->lcd_update();
}
static void draw_timer(void)
{
    char timebuf[24], bestbuf[24], avgbuf[24], best5buf[24], meanbuf[24];
    char line[48];
    const char *status;
    int p = g_puzzle;
    int footer1, footer2, stats_y, area_top, area_h, base_y;
    int n, max_cs, bar_w, y, i;
    rb->lcd_clear_display();
    draw_header(puzzle_name[p]);
    if (g_timer_state == TIMER_ARMED)       status = "READY";
    else if (g_timer_state == TIMER_RUNNING) status = "GO";
    else if (g_timer_state == TIMER_STOPPED) status = "STOPPED";
    else                                     status = "SELECT TO START";
    y = 2 + g_fh + 4;
    lcd_text(MARGIN, y, status);
    fmt_time(timer_elapsed_cs(), timebuf, sizeof(timebuf));
    text_center(y + g_lh, timebuf);
    rb->lcd_hline(MARGIN, LCD_WIDTH - 1 - MARGIN, y + 2 * g_lh + 1);
    stats_y = y + 2 * g_lh + 4;
    fmt_or_dash(g_best[p], bestbuf, sizeof(bestbuf));
    fmt_or_dash(calc_avg5(p), avgbuf, sizeof(avgbuf));
    fmt_or_dash(g_best_avg5[p], best5buf, sizeof(best5buf));
    fmt_or_dash(g_total_count[p] ? (int)(g_total_cs[p] / g_total_count[p]) : 0,
                meanbuf, sizeof(meanbuf));
    rb->snprintf(line, sizeof(line), "best  %s", bestbuf);
    lcd_text(MARGIN, stats_y, line);
    rb->snprintf(line, sizeof(line), "avg5  %s", avgbuf);
    lcd_text(MARGIN, stats_y + g_lh, line);
    rb->snprintf(line, sizeof(line), "best5 %s", best5buf);
    lcd_text(MARGIN, stats_y + 2 * g_lh, line);
    rb->snprintf(line, sizeof(line), "mean  %s  n=%d",
                 meanbuf, g_total_count[p]);
    lcd_text(MARGIN, stats_y + 3 * g_lh, line);
    footer2 = LCD_HEIGHT - g_fh - 2;
    footer1 = footer2 - g_lh;
    /* history sparkline: the fastest solve gets the shortest bar */
    n = g_hist_count[p];
    area_top = stats_y + 4 * g_lh + 3;
    base_y = footer1 - 4;
    area_h = base_y - area_top;
    if (n > 0 && area_h >= 4)
    {
        max_cs = 1;
        for (i = 0; i < n; i++)
            if (g_hist[p][i] > max_cs)
                max_cs = g_hist[p][i];
        bar_w = (LCD_WIDTH - 2 * MARGIN) / MAX_SOLVES;
        if (bar_w < 2)
            bar_w = 2;
        for (i = 0; i < n; i++)
        {
            int bh = 1 + (int)(((long)g_hist[p][i] * (area_h - 1)) / max_cs);
            int x = MARGIN + i * bar_w;
            if (bh > area_h) bh = area_h;
            if (x + bar_w > LCD_WIDTH)
                break;
            rb->lcd_fillrect(x, base_y - bh + 1,
                             (bar_w > 2) ? bar_w - 1 : 1, bh);
        }
        rb->lcd_hline(MARGIN, LCD_WIDTH - 1 - MARGIN, base_y + 1);
    }
    if (g_timer_state == TIMER_RUNNING)
        rb->strlcpy(line, "ANY KEY:stop", sizeof(line));
    else if (g_timer_state == TIMER_STOPPED)
        rb->strlcpy(line, "SEL:again CANCEL:menu", sizeof(line));
    else
        rb->strlcpy(line, "SEL:start CANCEL:menu", sizeof(line));
    lcd_text(MARGIN, footer1, line);
    rb->snprintf(line, sizeof(line), "puzzle: %s", puzzle_name[p]);
    lcd_text(MARGIN, footer2, line);
    rb->lcd_update();
}
static void draw_menu(void)
{
    char line[48], puzzle_row[32];
    int top, i;
    rb->lcd_clear_display();
    draw_header(NULL);
    rb->snprintf(line, sizeof(line), "puzzle: %s", puzzle_name[g_puzzle]);
    lcd_text(MARGIN, 2 + g_fh + 4, line);
    rb->snprintf(puzzle_row, sizeof(puzzle_row), "Puzzle: %s",
                 puzzle_name[g_puzzle]);
    top = 2 + g_fh + 6 + 2 * g_lh;
    for (i = 0; i < MENU_ITEMS; i++)
    {
        int y = top + i * (g_lh + 3);
        const char *label = menu_items[i][0] ? menu_items[i] : puzzle_row;
        if (i == g_menu_sel)
            rb->lcd_drawrect(MARGIN - 2, y - 2,
                             LCD_WIDTH - 2 * MARGIN + 4, g_lh + 3);
        lcd_text(MARGIN, y, label);
    }
    lcd_text(MARGIN, LCD_HEIGHT - 2 * g_lh - 2, "UP/DN:move SEL:open");
    lcd_text(MARGIN, LCD_HEIGHT - g_fh - 2, "CANCEL:quit");
    rb->lcd_update();
}

/* --- plugin entry point ----------------------------------------------- */
enum plugin_status plugin_start(const void *parameter)
{
    int w = 0, h = 0;
    bool done = false, need_draw = true;
    (void)parameter;
    rb->lcd_getstringsize((const unsigned char *)"Ag", &w, &h);
    g_fh = (h > 0) ? h : 8;
    g_lh = g_fh + 1;
    prng_seed();
    load_stats();
    g_puzzle = PUZZLE_3X3;
    g_view = VIEW_MENU;
    g_menu_sel = 0;
    gen_scramble();
    while (!done)
    {
        int act, timeout;
        if (need_draw)
        {
            if (g_view == VIEW_SCRAMBLE)
                draw_scramble();
            else if (g_view == VIEW_TIMER)
                draw_timer();
            else
                draw_menu();
            need_draw = false;
        }
        timeout = TIMEOUT_BLOCK;
        if (g_view == VIEW_TIMER && g_timer_state == TIMER_RUNNING)
            timeout = HZ / 10;
        act = pluginlib_getaction(timeout, plugin_contexts,
                                  ARRAYLEN(plugin_contexts));
        if (act == ACTION_NONE)
        {
            if (g_view == VIEW_TIMER && g_timer_state == TIMER_RUNNING)
                need_draw = true;
            continue;
        }
        if (g_view == VIEW_MENU)
        {
            if (act == PLA_UP || act == PLA_UP_REPEAT)
                g_menu_sel = (g_menu_sel + MENU_ITEMS - 1) % MENU_ITEMS;
            else if (act == PLA_DOWN || act == PLA_DOWN_REPEAT)
                g_menu_sel = (g_menu_sel + 1) % MENU_ITEMS;
            else if (act == PLA_SELECT)
            {
                if (g_menu_sel == 0) { gen_scramble(); g_view = VIEW_SCRAMBLE; }
                else if (g_menu_sel == 1)
                { g_timer_state = TIMER_IDLE; g_last_cs = 0; g_view = VIEW_TIMER; }
                else if (g_menu_sel == 2) cycle_puzzle(1);
                else done = true;
            }
            else if (act == PLA_CANCEL || act == PLA_EXIT)
                done = true;
            need_draw = true;
        }
        else if (g_view == VIEW_SCRAMBLE)
        {
            if (act == PLA_SELECT)
                gen_scramble();
            else if (act == PLA_LEFT || act == PLA_LEFT_REPEAT)
                cycle_puzzle(-1);
            else if (act == PLA_RIGHT || act == PLA_RIGHT_REPEAT)
                cycle_puzzle(1);
            else if ((act == PLA_UP || act == PLA_UP_REPEAT) && g_pages > 1)
                g_page = (g_page + 1) % g_pages;
            else if ((act == PLA_DOWN || act == PLA_DOWN_REPEAT) && g_pages > 1)
                g_page = (g_page + g_pages - 1) % g_pages;
            else if (act == PLA_CANCEL || act == PLA_EXIT)
                g_view = VIEW_MENU;
            need_draw = true;
        }
        else if (g_timer_state == TIMER_RUNNING)
        {
            long cs;
            /* releasing the starting SELECT is not a stop */
            if (act == PLA_SELECT_REL || act == PLA_SELECT_REPEAT)
                continue;
            cs = timer_elapsed_cs();
            g_last_cs = cs;
            g_timer_state = TIMER_STOPPED;
            record_solve(g_puzzle, (cs > 100000000L) ? 100000000 : (int)cs);
            if (act == PLA_CANCEL || act == PLA_EXIT)
                g_view = VIEW_MENU;
            need_draw = true;
        }
        else
        {
            if (act == PLA_SELECT)
                timer_arm_and_start();
            else if (act == PLA_CANCEL || act == PLA_EXIT)
                g_view = VIEW_MENU;
            need_draw = true;
        }
    }
    rb->lcd_clear_display();
    rb->lcd_update();
    return PLUGIN_OK;
}
