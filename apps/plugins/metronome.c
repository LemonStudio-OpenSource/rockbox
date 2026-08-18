/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 *
 * Copyright (C) 2026
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This software is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY
 * KIND, either express or implied.
 *
 ****************************************************************************/

#include "plugin.h"
#include <stdarg.h>

PLUGIN_HEADER

/* ========== 路径常量 ========== */
#define METRONOME_DIR       "/metronome"
#define METRONOME_LOG_DIR   "/metronome/log"
#define METRONOME_CFG_DIR   "/metronome/config"
#define METRONOME_CFG_FILE  "/metronome/config/config.ini"

/* ========== 屏幕与硬件 ========== */
#define LCD_W  220
#define LCD_H  176

/* ========== 拍号与重音 ========== */
enum time_signature {
    TS_2_4 = 0,
    TS_3_4,
    TS_4_4,
    TS_6_8,
    TS_NUM
};

enum accent {
    ACCENT_NONE = 0,
    ACCENT_WEAK,
    ACCENT_MEDIUM,
    ACCENT_STRONG
};

/* 每小节拍数 —— 静态加载在内存 */
static const int ts_beats[TS_NUM] = {2, 3, 4, 6};

/* 拍号名称 */
static const char *ts_names[TS_NUM] = {"2/4", "3/4", "4/4", "6/8"};

/* 重音模式表 —— 全部静态加载在内存，运行时零磁盘访问 */
static const enum accent ts_accents[TS_NUM][6] = {
    {ACCENT_STRONG, ACCENT_WEAK,  ACCENT_NONE,   ACCENT_NONE,   ACCENT_NONE,   ACCENT_NONE},
    {ACCENT_STRONG, ACCENT_WEAK,  ACCENT_WEAK,   ACCENT_NONE,   ACCENT_NONE,   ACCENT_NONE},
    {ACCENT_STRONG, ACCENT_WEAK,  ACCENT_MEDIUM, ACCENT_WEAK,   ACCENT_NONE,   ACCENT_NONE},
    {ACCENT_STRONG, ACCENT_WEAK,  ACCENT_WEAK,   ACCENT_MEDIUM, ACCENT_WEAK,   ACCENT_WEAK}
};

/* ========== 颜色 ========== */
#define C_BG    LCD_RGBPACK(0,   0,   0)
#define C_FG    LCD_RGBPACK(255, 255, 255)
#define C_RED   LCD_RGBPACK(255, 50,  50)
#define C_ORG   LCD_RGBPACK(255, 180, 40)
#define C_BLU   LCD_RGBPACK(50,  150, 255)
#define C_DIM   LCD_RGBPACK(60,  60,  60)
#define C_LGRY  LCD_RGBPACK(120, 120, 120)
#define C_HL    LCD_RGBPACK(50,  255, 100)

/* ========== 配置结构 ========== */
struct metro_cfg {
    int bpm;                    /* 20 ~ 280 */
    enum time_signature ts;
    bool blink;
    int volume;                 /* 20 ~ 100 */
};

/* ========== 运行时状态 —— 全部驻留内存 ========== */
struct metro_state {
    bool running;
    int current_beat;           /* 0-based，当前小节内第几拍 */
    long next_beat_tick;        /* 下一拍应触发的系统 tick */
    int tick_interval;          /* 每拍间隔 tick 数 */
    struct metro_cfg cfg;

    /* 滚轮调速状态 */
    long last_scroll_tick;
    int  scroll_accum;

    /* 编辑焦点：0=BPM 1=TS 2=Vol 3=Blink */
    int edit_item;

    /* 日志 */
    int log_fd;
    long start_tick;

    /* 按键边沿检测 */
    int last_btn;
};

static struct metro_state g;

#define BPM_TO_TICKS(bpm) ((60 * HZ) / (bpm))

/* ==================== 日志系统 ==================== */

static void log_write(const char *fmt, ...)
{
    if (g.log_fd < 0) return;
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    rb->vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    rb->fdprintf(g.log_fd, "%s\n", buf);
}

static void log_init(void)
{
    rb->mkdir(METRONOME_DIR);
    rb->mkdir(METRONOME_LOG_DIR);
    rb->mkdir(METRONOME_CFG_DIR);

    struct tm *tm = rb->get_time();
    char path[64];
    rb->snprintf(path, sizeof(path),
                 "%s/%04d-%02d-%02d-%02d-%02d-%02d.log",
                 METRONOME_LOG_DIR,
                 tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
                 tm->tm_hour, tm->tm_min, tm->tm_sec);

    g.log_fd = rb->open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (g.log_fd >= 0) {
        rb->fdprintf(g.log_fd, "=== Metronome Log ===\n");
        rb->fdprintf(g.log_fd, "Start: %04d-%02d-%02d %02d:%02d:%02d\n",
                     tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
                     tm->tm_hour, tm->tm_min, tm->tm_sec);
    }
}

static void log_close(void)
{
    if (g.log_fd >= 0) {
        log_write("Total run ticks: %ld", *rb->current_tick - g.start_tick);
        log_write("=== End ===");
        rb->close(g.log_fd);
        g.log_fd = -1;
    }
}

/* ==================== 配置系统 ==================== */

static void cfg_default(struct metro_cfg *c)
{
    c->bpm    = 120;
    c->ts     = TS_4_4;
    c->blink  = true;
    c->volume = 80;
}

static void cfg_load(struct metro_cfg *c)
{
    cfg_default(c);
    int fd = rb->open(METRONOME_CFG_FILE, O_RDONLY);
    if (fd < 0) {
        log_write("No config file, using defaults");
        return;
    }
    log_write("Loading config");

    char line[128];
    while (rb->read_line(fd, line, sizeof(line)) > 0) {
        char *name, *value;
        if (!rb->settings_parseline(line, &name, &value))
            continue;

        if (rb->strcmp(name, "bpm") == 0) {
            c->bpm = rb->atoi(value);
            if (c->bpm < 20) c->bpm = 20;
            if (c->bpm > 280) c->bpm = 280;
        } else if (rb->strcmp(name, "ts") == 0) {
            int v = rb->atoi(value);
            if (v >= 0 && v < TS_NUM) c->ts = v;
        } else if (rb->strcmp(name, "blink") == 0) {
            c->blink = (rb->atoi(value) != 0);
        } else if (rb->strcmp(name, "volume") == 0) {
            c->volume = rb->atoi(value);
            if (c->volume < 20) c->volume = 20;
            if (c->volume > 100) c->volume = 100;
        }
    }
    rb->close(fd);
    log_write("Cfg: bpm=%d ts=%s blink=%d vol=%d",
              c->bpm, ts_names[c->ts], c->blink, c->volume);
}

static void cfg_save(const struct metro_cfg *c)
{
    int fd = rb->open(METRONOME_CFG_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) {
        log_write("Failed to save config");
        return;
    }
    rb->fdprintf(fd, "bpm=%d\n", c->bpm);
    rb->fdprintf(fd, "ts=%d\n", c->ts);
    rb->fdprintf(fd, "blink=%d\n", c->blink ? 1 : 0);
    rb->fdprintf(fd, "volume=%d\n", c->volume);
    rb->close(fd);
    log_write("Config saved");
}

/* ==================== 节拍核心（零磁盘访问） ==================== */

static void metro_reset_timing(void)
{
    g.tick_interval = BPM_TO_TICKS(g.cfg.bpm);
    g.next_beat_tick = *rb->current_tick + g.tick_interval;
    g.current_beat = -1; /* 下一拍进位为 0 */
    log_write("Timing reset: BPM=%d interval=%d ticks", g.cfg.bpm, g.tick_interval);
}

static void metro_sound(enum accent ac)
{
    int freq, dur, amp;
    switch (ac) {
        case ACCENT_STRONG:  freq = 1200; dur = 60; amp = g.cfg.volume; break;
        case ACCENT_MEDIUM:  freq = 1000; dur = 50; amp = g.cfg.volume * 8 / 10; break;
        case ACCENT_WEAK:    freq = 800;  dur = 40; amp = g.cfg.volume * 6 / 10; break;
        default: return;
    }
    rb->beep_play(freq, dur, amp);
}

static void metro_trigger(void)
{
    int beats = ts_beats[g.cfg.ts];
    g.current_beat = (g.current_beat + 1) % beats;
    enum accent ac = ts_accents[g.cfg.ts][g.current_beat];
    metro_sound(ac);

    g.next_beat_tick += g.tick_interval;
    long now = *rb->current_tick;
    if (g.next_beat_tick < now) {
        g.next_beat_tick = now + g.tick_interval; /* 跳拍保护 */
        log_write("Skip protection");
    }
    log_write("Beat %d/%d accent=%d", g.current_beat + 1, beats, ac);
}

static void metro_check(void)
{
    if (!g.running) return;
    if (*rb->current_tick >= g.next_beat_tick)
        metro_trigger();
}

/* ==================== 输入处理 ==================== */

static void handle_scroll(int dir)
{
    long now = *rb->current_tick;
    if (now - g.last_scroll_tick > HZ / 3)
        g.scroll_accum = 0;
    g.scroll_accum += dir;
    g.last_scroll_tick = now;

    int step = (g.scroll_accum >= 3 || g.scroll_accum <= -3) ? 5 : 1;

    switch (g.edit_item) {
        case 0: /* BPM */
            g.cfg.bpm += dir * step;
            if (g.cfg.bpm < 20) g.cfg.bpm = 20;
            if (g.cfg.bpm > 280) g.cfg.bpm = 280;
            g.tick_interval = BPM_TO_TICKS(g.cfg.bpm);
            break;
        case 1: /* TS */
            {
                int v = (int)g.cfg.ts + dir;
                if (v < 0) v = TS_NUM - 1;
                if (v >= TS_NUM) v = 0;
                g.cfg.ts = v;
            }
            break;
        case 2: /* Volume */
            g.cfg.volume += dir * step;
            if (g.cfg.volume < 20) g.cfg.volume = 20;
            if (g.cfg.volume > 100) g.cfg.volume = 100;
            break;
        case 3: /* Blink */
            if (dir != 0) g.cfg.blink = !g.cfg.blink;
            break;
    }
}

static void handle_input(int btn, int pressed)
{
    if (btn & BUTTON_SCROLL_FWD)   handle_scroll(1);
    if (btn & BUTTON_SCROLL_BACK)  handle_scroll(-1);

    if (pressed & BUTTON_SELECT) {
        g.edit_item = (g.edit_item + 1) % 4;
        log_write("Edit focus -> %d", g.edit_item);
    }
    if (pressed & BUTTON_PLAY) {
        g.running = !g.running;
        if (g.running) {
            metro_reset_timing();
            log_write("STARTED");
        } else {
            log_write("STOPPED");
        }
    }
}

/* ==================== 绘制 ==================== */

static void draw_text_c(int y, const char *s, int font, unsigned c)
{
    int w, h;
    rb->lcd_setfont(font);
    rb->lcd_getstringsize((const unsigned char *)s, &w, &h);
    rb->lcd_set_foreground(c);
    rb->lcd_putsxy((LCD_W - w) / 2, y, (const unsigned char *)s);
}

static void draw_beats(void)
{
    int beats = ts_beats[g.cfg.ts];
    int r = 8;
    int gap = 26;
    int total_w = beats * gap - (gap - r * 2);
    int x0 = (LCD_W - total_w) / 2 + r;
    int y = 42;

    for (int i = 0; i < beats; i++) {
        int cx = x0 + i * gap;
        enum accent ac = ts_accents[g.cfg.ts][i];
        unsigned col;

        if (i == g.current_beat && g.running) {
            switch (ac) {
                case ACCENT_STRONG:  col = C_RED; break;
                case ACCENT_MEDIUM:  col = C_ORG; break;
                default:             col = C_BLU; break;
            }
            rb->lcd_set_foreground(col);
            rb->lcd_fillrect(cx - r, y - r, r * 2, r * 2);
            rb->lcd_set_foreground(C_FG);
            rb->lcd_drawrect(cx - r - 1, y - r - 1, r * 2 + 2, r * 2 + 2);
        } else {
            col = (i < g.current_beat && g.running) ? C_LGRY : C_DIM;
            rb->lcd_set_foreground(col);
            rb->lcd_drawrect(cx - r, y - r, r * 2, r * 2);
        }
    }
}

static void draw_ui(void)
{
    rb->lcd_set_background(C_BG);
    rb->lcd_clear_display();

    /* 顶部栏 */
    rb->lcd_setfont(FONT_UI);
    rb->lcd_set_foreground(C_FG);
    rb->lcd_putsxy(4, 2, (const unsigned char *)"Metronome");

    if (g.running) {
        rb->lcd_set_foreground(C_RED);
        rb->lcd_putsxy(LCD_W - 28, 2, (const unsigned char *)"[>]");
    } else {
        rb->lcd_set_foreground(C_DIM);
        rb->lcd_putsxy(LCD_W - 32, 2, (const unsigned char *)"[||]");
    }

    char buf[32];
    rb->snprintf(buf, sizeof(buf), "%s", ts_names[g.cfg.ts]);
    rb->lcd_set_foreground(C_FG);
    rb->lcd_putsxy(100, 2, (const unsigned char *)buf);

    /* 拍点 */
    draw_beats();

    /* 中央 BPM */
    rb->snprintf(buf, sizeof(buf), "%d", g.cfg.bpm);
    draw_text_c(72, buf, FONT_SYSFIXED, C_FG);
    draw_text_c(96, "BPM", FONT_UI, C_DIM);

    /* 底部参数栏 */
    const char *lbl[4] = {"BPM", "TS", "Vol", "Blink"};
    char vbuf[4][8];
    const char *val[4];

    rb->snprintf(vbuf[0], 8, "%d", g.cfg.bpm);
    val[0] = vbuf[0];

    val[1] = ts_names[g.cfg.ts];

    rb->snprintf(vbuf[2], 8, "%d", g.cfg.volume);
    val[2] = vbuf[2];

    rb->strcpy(vbuf[3], g.cfg.blink ? "ON" : "OFF");
    val[3] = vbuf[3];

    int y_base = 132;
    int col_w = LCD_W / 4;

    for (int i = 0; i < 4; i++) {
        int cx = i * col_w + col_w / 2;
        unsigned c = (i == g.edit_item) ? C_HL : C_DIM;

        int w, h;
        rb->lcd_setfont(FONT_UI);
        rb->lcd_getstringsize((const unsigned char *)lbl[i], &w, &h);
        rb->lcd_set_foreground(c);
        rb->lcd_putsxy(cx - w / 2, y_base, (const unsigned char *)lbl[i]);

        rb->lcd_getstringsize((const unsigned char *)val[i], &w, &h);
        rb->lcd_putsxy(cx - w / 2, y_base + 14, (const unsigned char *)val[i]);
    }

    /* 底部提示 */
    rb->lcd_set_foreground(C_DIM);
    rb->lcd_putsxy(4, LCD_H - 10, (const unsigned char *)"PLAY:Start SEL:Edit Wheel:Adj");

    rb->lcd_update();
}

/* ==================== 主入口 ==================== */

enum plugin_status plugin_start(const void *parameter)
{
    (void)parameter;

    rb->memset(&g, 0, sizeof(g));
    g.log_fd = -1;
    g.start_tick = *rb->current_tick;

    /* 1. 停止音频，释放解码线程，独占 CPU 调度 */
    rb->audio_stop();

    /* 2. 初始化日志 */
    log_init();
    log_write("Audio stopped, plugin started");

    /* 3. 加载配置 */
    cfg_load(&g.cfg);

    /* 4. 初始化节拍参数 */
    g.tick_interval = BPM_TO_TICKS(g.cfg.bpm);

    /* 5. 主循环 —— 100ms 轮询，兼顾响应与功耗 */
    while (1) {
        int btn = rb->button_get_w_tmo(HZ / 10);
        int pressed = btn & ~g.last_btn;
        g.last_btn = btn;

        if (pressed & BUTTON_MENU) {
            log_write("Exit by MENU");
            break;
        }

        handle_input(btn, pressed);
        metro_check();
        draw_ui();

        rb->yield();
    }

    /* 6. 退出清理 */
    cfg_save(&g.cfg);
    log_close();

    return PLUGIN_OK;
}
