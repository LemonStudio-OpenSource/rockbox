/***************************************************************************
 *  Metronome Pro - Rockbox Plugin
 *
 *  基于原始 metronome.c 音频引擎 + Kimi 暗色 UI 设计 + 增强动画
 *
 *  功能特性:
 *  - 暗色主题 + 重音颜色编码 (强拍=红, 次强=橙, 弱拍=蓝)
 *  - 节拍缩放缓动动画 (Beat Scale Easing)
 *  - BPM 数字呼吸效果 (BPM Breathing on Strong Beats)
 *  - 节拍进度条 (Progress Arc)
 *  - 子拍进度指示器 (Sub-beat Progress)
 *  - 双击 SELECT 打点调速 (Tap Tempo)
 *  - 摇摆节奏支持 (Swing Rhythm)
 *  - 渐进加速模式 (Gradual Tempo Acceleration)
 *  - 静默小节训练 (Silent Bar Training)
 *  - 6种拍号: 2/4, 3/4, 4/4, 6/8, 5/4, 7/8
 *
 *  音频: 使用 rb->beep_play() (与 Kimi 代码一致，适合 iPod 性能)
 *  UI: 暗色主题 + 微交互动画
 *  目标: iPod Color (160x160) 及其他 Rockbox 设备
 *
 *  注意: 不要自行定义 PLUGIN_HEADER 和 rb
 *        这些由固件通过 plugin.h 提供，重复定义会导致编译错误
 ***************************************************************************/

#include "plugin.h"


/* ========== 屏幕尺寸 (iPod Color: 160x160) ========== */
#define LCD_W  160
#define LCD_H  160

/* ========== 颜色 (RGB565 via LCD_RGBPACK) ========== */
#define C_BG     LCD_RGBPACK(0,   0,   0)     /* 纯黑背景 */
#define C_FG     LCD_RGBPACK(255, 255, 255)   /* 白色文字 */
#define C_RED    LCD_RGBPACK(255, 50,  50)    /* 红色 - 强拍 */
#define C_ORG    LCD_RGBPACK(255, 180, 40)    /* 橙色 - 次强拍 */
#define C_BLU    LCD_RGBPACK(50,  150, 255)   /* 蓝色 - 弱拍 */
#define C_DIM    LCD_RGBPACK(60,  60,  60)    /* 暗灰 - 未激活 */
#define C_LGRY   LCD_RGBPACK(120, 120, 120)   /* 亮灰 - 已过拍 */
#define C_HL     LCD_RGBPACK(50,  255, 100)   /* 亮绿 - 高亮/激活 */

/* ========== BPM 范围 ========== */
#define BPM_MIN  20
#define BPM_MAX  280
#define BPM_DEF  120

/* ========== 音量范围 ========== */
#define VOL_MIN  20
#define VOL_MAX  100
#define VOL_DEF  80

/* ========== 摇摆范围 ========== */
#define SWING_MIN  0
#define SWING_MAX  100
#define SWING_DEF  50

/* ========== 渐进加速 ========== */
#define GRAD_MIN    0
#define GRAD_MAX    10
#define GRAD_DEF    0
#define GRAD_STEP   5

/* ========== 静默小节 ========== */
#define SILENT_MIN  0
#define SILENT_MAX  4
#define SILENT_DEF  0

/* ========== 计时 ========== */
#define BPM_TO_TICKS(bpm)  ((60 * HZ) / (bpm))

/* ========== 动画常量 ========== */
#define ANIM_BEAT_FRAMES     6
#define ANIM_PROG_SPEED      5
#define ANIM_BREATHE_FRAMES  8
#define FLASH_FRAMES         6

/* ========== 最大拍数 ========== */
#define MAX_BEATS  8

/* ========== Tap Tempo ========== */
#define TAP_HISTORY  8


/* ========== 拍号定义 ========== */
enum time_signature {
    TS_2_4 = 0,
    TS_3_4,
    TS_4_4,
    TS_6_8,
    TS_5_4,
    TS_7_8,
    TS_NUM
};

static const int ts_beats[TS_NUM] = {2, 3, 4, 6, 5, 7};
static const char *ts_names[TS_NUM] = {"2/4", "3/4", "4/4", "6/8", "5/4", "7/8"};

/* ========== 重音类型 ========== */
enum accent {
    ACCENT_NONE = 0,
    ACCENT_WEAK,
    ACCENT_MEDIUM,
    ACCENT_STRONG
};

/* 各拍号的重音模式表 (每行最多 MAX_BEATS 个元素) */
static const int ts_accents[TS_NUM][MAX_BEATS] = {
    /* 2/4 */  {ACCENT_STRONG, ACCENT_WEAK,  0, 0, 0, 0, 0, 0},
    /* 3/4 */  {ACCENT_STRONG, ACCENT_WEAK,  ACCENT_WEAK, 0, 0, 0, 0, 0},
    /* 4/4 */  {ACCENT_STRONG, ACCENT_WEAK,  ACCENT_MEDIUM, ACCENT_WEAK, 0, 0, 0, 0},
    /* 6/8 */  {ACCENT_STRONG, ACCENT_WEAK,  ACCENT_WEAK, ACCENT_MEDIUM, ACCENT_WEAK, ACCENT_WEAK, 0, 0},
    /* 5/4 */  {ACCENT_STRONG, ACCENT_WEAK,  ACCENT_MEDIUM, ACCENT_WEAK, ACCENT_STRONG, 0, 0, 0},
    /* 7/8 */  {ACCENT_STRONG, ACCENT_WEAK,  ACCENT_MEDIUM, ACCENT_WEAK, ACCENT_MEDIUM, ACCENT_WEAK, ACCENT_STRONG, 0},
};

/* ========== 全局状态 ========== */
static struct {
    /* 设置 */
    int bpm;
    int ts;
    int volume;
    int swing;
    int gradual_accel;
    int gradual_target;
    int silent_bars;

    /* 运行时状态 */
    int state;
    int current_beat;
    int bar_count;
    long next_beat_tick;
    int tick_interval;

    /* Tap Tempo */
    int tap_times[TAP_HISTORY];
    int tap_count;
    int tap_bpm;

    /* 动画状态 */
    int beat_ease_frames;
    int beat_target_scale;
    int bpm_breathe;
    int prog_arc;
    int sub_prog;
    int last_beat;
    int key_feedback;
    int key_focus;

    /* 输入 */
    int last_btn;
} g;

/* ========== 辅助函数 ========== */

static inline int get_accent(int beat)
{
    return ts_accents[g.ts][beat % ts_beats[g.ts]];
}

static inline int get_accent_color(int accent)
{
    switch (accent) {
        case ACCENT_STRONG:  return C_RED;
        case ACCENT_MEDIUM:  return C_ORG;
        default:             return C_BLU;
    }
}


/* ========== 音频引擎 (基于原始 metronome.c 的 beep_play 方案) ========== */

static void play_beep(int accent)
{
    int freq, dur, amp;
    switch (accent) {
        case ACCENT_STRONG:
            freq = 1200; dur = 60; amp = g.volume; break;
        case ACCENT_MEDIUM:
            freq = 1000; dur = 50; amp = g.volume * 8 / 10; break;
        case ACCENT_WEAK:
            freq = 800; dur = 40; amp = g.volume * 6 / 10; break;
        default:
            return;
    }
    rb->beep_play(freq, dur, amp);
}

/* ========== 节拍核心 ========== */

static void metro_trigger(void)
{
    int accent = get_accent(g.current_beat);

    /* 静默小节检查 */
    int is_silent = (g.silent_bars > 0 &&
                     g.bar_count % (g.silent_bars + 1) == 0);

    if (!is_silent)
        play_beep(accent);

    /* 更新动画状态 */
    g.last_beat = g.current_beat;
    g.beat_ease_frames = ANIM_BEAT_FRAMES;
    g.beat_target_scale = 180;
    g.prog_arc = 0;
    g.sub_prog = 0;

    if (accent == ACCENT_STRONG && !is_silent)
        g.bpm_breathe = ANIM_BREATHE_FRAMES;
}

static void metro_check(void)
{
    if (g.state != 1) return;

    if (*rb->current_tick >= g.next_beat_tick) {
        metro_trigger();

        g.current_beat++;
        if (g.current_beat >= ts_beats[g.ts]) {
            g.current_beat = 0;
            g.bar_count++;

            /* 渐进加速: 每 N 小节增加 GRAD_STEP BPM */
            if (g.gradual_accel > 0 &&
                g.bar_count % g.gradual_accel == 0 &&
                g.bpm < g.gradual_target) {
                g.bpm += GRAD_STEP;
                if (g.bpm > g.gradual_target)
                    g.bpm = g.gradual_target;
                g.tick_interval = BPM_TO_TICKS(g.bpm);
            }
        }

        g.next_beat_tick += g.tick_interval;
        long now = *rb->current_tick;
        if (g.next_beat_tick < now)
            g.next_beat_tick = now + g.tick_interval;
    }
}

static void metro_reset(void)
{
    g.tick_interval = BPM_TO_TICKS(g.bpm);
    g.next_beat_tick = *rb->current_tick + g.tick_interval;
    g.current_beat = 0;
    g.bar_count = 0;
}

/* ========== Tap Tempo ========== */

static void tap_calculate(void)
{
    if (g.tap_count < 2) return;

    int total = 0;
    for (int i = 1; i < g.tap_count; i++)
        total += g.tap_times[i] - g.tap_times[i - 1];

    int avg = total / (g.tap_count - 1);
    if (avg > 0) {
        g.tap_bpm = 60 * HZ / avg;
        if (g.tap_bpm < BPM_MIN) g.tap_bpm = BPM_MIN;
        if (g.tap_bpm > BPM_MAX) g.tap_bpm = BPM_MAX;
    }
}

static void tap_record(void)
{
    long now = *rb->current_tick;

    if (g.tap_count > 0 && now - g.tap_times[g.tap_count - 1] > HZ * 2)
        g.tap_count = 0;

    if (g.tap_count < TAP_HISTORY) {
        g.tap_times[g.tap_count++] = now;
        tap_calculate();
    }
}


/* ========== UI 绘制 ========== */

static void draw_ui(void)
{
    char buf[32];
    int w, h;

    /* 清屏 */
    rb->lcd_set_background(C_BG);
    rb->lcd_clear_display();

    /* ===== 顶部栏 (y=2) ===== */
    rb->lcd_setfont(FONT_UI);
    rb->lcd_set_foreground(C_FG);
    rb->lcd_putsxy(4, 2, (const unsigned char *)"Metro");

    rb->lcd_set_foreground(C_HL);
    rb->lcd_putsxy(100, 2, (const unsigned char *)ts_names[g.ts]);

    if (g.state == 1) {
        rb->lcd_set_foreground(C_HL);
        rb->lcd_putsxy(140, 2, (const unsigned char *)">>");
    } else {
        rb->lcd_set_foreground(C_DIM);
        rb->lcd_putsxy(140, 2, (const unsigned char *)"||");
    }

    /* 状态指示器 (y=12) */
    int ind_x = 4;
    if (g.gradual_accel > 0) {
        rb->snprintf(buf, sizeof(buf), "ACC %d->%d", g.bpm, g.gradual_target);
        rb->lcd_set_foreground(C_ORG);
        rb->lcd_putsxy(ind_x, 12, (const unsigned char *)buf);
        rb->lcd_getstringsize((const unsigned char *)buf, &w, &h);
        ind_x += w;
    }
    if (g.silent_bars > 0) {
        rb->snprintf(buf, sizeof(buf), "S:%d", g.silent_bars);
        rb->lcd_set_foreground(C_BLU);
        rb->lcd_putsxy(ind_x, 12, (const unsigned char *)buf);
        rb->lcd_getstringsize((const unsigned char *)buf, &w, &h);
        ind_x += w;
    }
    rb->lcd_set_foreground(C_DIM);
    rb->snprintf(buf, sizeof(buf), "BAR:%d", g.bar_count);
    rb->lcd_putsxy(120, 12, (const unsigned char *)buf);

    /* ===== 节拍方块 (y=24) ===== */
    int beats = ts_beats[g.ts];
    int dot_r = 7;
    int dot_gap = 24;
    int total_w = (beats - 1) * dot_gap;
    int x0 = (LCD_W - total_w) / 2;
    int y_dot = 28;

    for (int i = 0; i < beats; i++) {
        int cx = x0 + i * dot_gap;
        int cy = y_dot + dot_r;

        int accent = get_accent(i);
        int col = get_accent_color(accent);

        /* 动画缩放: 从 180 缓动回 256 */
        int scale = 256;
        if (g.beat_ease_frames > 0 && i == g.last_beat) {
            int progress = ANIM_BEAT_FRAMES - g.beat_ease_frames;
            scale = 256 - (256 - g.beat_target_scale) *
                    progress / ANIM_BEAT_FRAMES;
            if (scale > 256) scale = 256;
        }

        int r = dot_r * scale / 256;
        int half = r / 2;

        if (g.state == 1 && i == g.current_beat) {
            /* 当前拍: 实心 + 外描边 */
            rb->lcd_set_foreground(col);
            rb->lcd_fillrect(cx - half, cy - half, r, r);
            rb->lcd_set_foreground(C_BG);
            rb->lcd_drawrect(cx - half - 1, cy - half - 1, r + 2, r + 2);
            rb->lcd_set_foreground(col);
        } else if (g.state == 1 && i < g.current_beat) {
            /* 已过拍: 灰色实心 */
            rb->lcd_set_foreground(C_LGRY);
            rb->lcd_fillrect(cx - half, cy - half, r, r);
        } else {
            /* 未到达拍: 空心轮廓 */
            rb->lcd_set_foreground(C_DIM);
            rb->lcd_drawrect(cx - half, cy - half, r, r);
        }
    }

    /* ===== BPM 数字 (y=48) ===== */
    int breathe = 0;
    if (g.bpm_breathe > 0) {
        breathe = (ANIM_BREATHE_FRAMES - g.bpm_breathe) * 2;
        g.bpm_breathe--;
    }

    int bpm_y = 48 + breathe;
    rb->lcd_setfont(FONT_SYSFIXED);
    rb->lcd_set_foreground(C_FG);
    rb->snprintf(buf, sizeof(buf), "%d", g.bpm);
    rb->lcd_getstringsize((const unsigned char *)buf, &w, &h);
    rb->lcd_putsxy((LCD_W - w) / 2, bpm_y, (const unsigned char *)buf);

    rb->lcd_setfont(FONT_UI);
    rb->lcd_set_foreground(C_DIM);
    rb->lcd_putsxy((LCD_W - 24) / 2, bpm_y + 14,
                   (const unsigned char *)"BPM");

    /* ===== 进度条 (y=70) ===== */
    int bar_x = 10;
    int bar_w = LCD_W - 20;

    /* 主进度条 */
    rb->lcd_set_foreground(C_DIM);
    rb->lcd_fillrect(bar_x, 70, bar_w, 4);
    if (g.prog_arc > 0) {
        int prog_w = bar_w * g.prog_arc / 256;
        rb->lcd_set_foreground(C_HL);
        rb->lcd_fillrect(bar_x, 70, prog_w, 4);
    }

    /* 子拍进度条 */
    rb->lcd_set_foreground(C_DIM);
    rb->lcd_fillrect(bar_x, 77, bar_w, 2);
    if (g.sub_prog > 0) {
        int prog_w = bar_w * g.sub_prog / 256;
        rb->lcd_set_foreground(C_HL);
        rb->lcd_fillrect(bar_x, 77, prog_w, 2);
    }

    /* ===== 底部参数栏 (y=92) ===== */
    int y_label = 92;
    int y_value = 106;
    int col_w = LCD_W / 4;
    const char *labels[4] = {"BPM", "TS", "VOL", "SW"};

    for (int i = 0; i < 4; i++) {
        int cx = i * col_w + col_w / 2;
        int c = (g.key_focus == i && g.key_feedback > 0) ? C_HL : C_DIM;

        rb->lcd_setfont(FONT_UI);
        rb->lcd_getstringsize((const unsigned char *)labels[i], &w, &h);
        rb->lcd_set_foreground(c);
        rb->lcd_putsxy(cx - w / 2, y_label, (const unsigned char *)labels[i]);
    }

    /* 参数值 */
    static char vbuf[4][8];
    const char *values[4];

    rb->snprintf(vbuf[0], 8, "%d", g.bpm);
    values[0] = vbuf[0];
    values[1] = ts_names[g.ts];
    rb->snprintf(vbuf[2], 8, "%d", g.volume);
    values[2] = vbuf[2];
    rb->snprintf(vbuf[3], 8, "%d", g.swing);
    values[3] = vbuf[3];

    for (int i = 0; i < 4; i++) {
        int cx = i * col_w + col_w / 2;
        int c = (g.key_focus == i && g.key_feedback > 0) ? C_HL : C_DIM;

        rb->lcd_setfont(FONT_UI);
        rb->lcd_getstringsize((const unsigned char *)values[i], &w, &h);
        rb->lcd_set_foreground(c);
        rb->lcd_putsxy(cx - w / 2, y_value, (const unsigned char *)values[i]);
    }

    /* ===== Tap Tempo 指示器 (y=122) ===== */
    if (g.tap_count >= 2) {
        rb->lcd_set_foreground(C_HL);
        rb->snprintf(buf, sizeof(buf), "Tap: %d BPM", g.tap_bpm);
        rb->lcd_putsxy(50, 122, (const unsigned char *)buf);
    } else {
        rb->lcd_set_foreground(C_DIM);
        rb->lcd_putsxy(30, 122,
                       (const unsigned char *)"Double-tap SELECT for Tap Tempo");
    }

    /* ===== 底部操作提示 (y=148) ===== */
    rb->lcd_set_foreground(C_DIM);
    rb->lcd_putsxy(4, 148,
                   (const unsigned char *)"PLAY:Start SEL:Focus L/R:Adj U/D:Vol WHEEL:Swing");

    /* 更新显示 */
    rb->lcd_update();
}


/* ========== 输入处理 ========== */

static void handle_input(int btn, int pressed)
{
    /* 防止未使用参数警告 */
    (void)btn;

    /* 1. 滚轮控制：始终调节 Swing 或 音量 (根据焦点) */
    if (pressed & BUTTON_SCROLL_FWD) {
        if (g.key_focus == 3) {
            g.swing = MIN(g.swing + 5, 75);
        } else {
            g.volume = MIN(g.volume + 1, 10);
        }
        g.key_feedback = FLASH_FRAMES;
        return;
    }
    if (pressed & BUTTON_SCROLL_BACK) {
        if (g.key_focus == 3) {
            g.swing = MAX(g.swing - 5, 0);
        } else {
            g.volume = MAX(g.volume - 1, 0);
        }
        g.key_feedback = FLASH_FRAMES;
        return;
    }

    /* 2. SELECT (中心键)：切换焦点 / Tap Tempo */
    if (pressed & BUTTON_SELECT) {
        /* 简单双击检测：如果距离上次 < 400ms，视为 Tap */
        long now = *rb->current_tick;
        tap_record();  // 无参数，内部用 tap_times[] 环形缓冲区
		if (g.tap_count >= 2) {
			g.bpm = g.tap_bpm;
			g.tick_interval = BPM_TO_TICKS(g.bpm);
			g.key_focus = 0;
			g.key_feedback = 12;
		} else {
			g.key_focus = (g.key_focus + 1) % 4;
			g.key_feedback = 6;
		}
        // 已删除重复的 g.key_feedback = FLASH_FRAMES;
        return;
    }

    /* 3. PLAY/PAUSE：开始/暂停 */
    if (pressed & BUTTON_PLAY) {
        metro_reset();
        if (g.state == 1) {
            g.state = 2;    // 暂停
        } else {
            g.state = 1;    // 播放
            metro_reset();
        }
        return;
    }

    /* 4. MENU：退出 */
    if (pressed & BUTTON_MENU) {
        return;
    }

    /* 5. 其他按键：根据当前焦点调整 */
    /* 注意：iPod Classic 没有 LEFT/RIGHT 物理按键，
       这里用 SCROLL 配合 SELECT 切换焦点来调整，
       或者你可以用 BUTTON_SCROLL_FWD/BACK 配合 BUTTON_SELECT 按住来快速调 */
    
    /* 如果非要保留 LEFT/RIGHT 逻辑（仅对有方向键的设备生效）： */
    #ifdef BUTTON_LEFT
    if (pressed & BUTTON_LEFT) {
        if (g.key_focus == 0) g.bpm = MAX(g.bpm - 1, 30);
        else { g.ts--; if(g.ts<0) g.ts=TS_NUM-1; }
        g.key_feedback = FLASH_FRAMES;
    }
    #endif
    #ifdef BUTTON_RIGHT
    if (pressed & BUTTON_RIGHT) {
        if (g.key_focus == 0) g.bpm = MIN(g.bpm + 1, 300);
        else { g.ts++; if(g.ts>=TS_NUM) g.ts=0; }
        g.key_feedback = FLASH_FRAMES;
    }
    #endif
}

/* ========== 主入口 ========== */

enum plugin_status plugin_start(const void *parameter)
{
    (void)parameter;

    /* 停止任何正在播放的音频 */
    rb->audio_stop();

    /* 初始化默认设置 */
    rb->memset(&g, 0, sizeof(g));
    g.bpm = BPM_DEF;
    g.ts = TS_4_4;
    g.volume = VOL_DEF;
    g.swing = SWING_DEF;
    g.gradual_accel = GRAD_DEF;
    g.gradual_target = BPM_DEF;
    g.silent_bars = SILENT_DEF;
    g.state = 0;
    g.current_beat = 0;
    g.bar_count = 0;
    g.key_focus = 0;
    g.tap_count = 0;
    g.tap_bpm = BPM_DEF;
    g.beat_ease_frames = 0;
    g.bpm_breathe = 0;
    g.prog_arc = 256;
    g.sub_prog = 256;
    g.last_beat = -1;
    g.last_btn = 0;

    /* 主循环: 100ms 轮询, 兼顾响应与功耗 */
    while (1) {
        int btn = rb->button_get_w_tmo(HZ / 10);
        int pressed = btn & ~g.last_btn;
        g.last_btn = btn;

        if (pressed & BUTTON_MENU)
            break;

        handle_input(btn, pressed);
        metro_check();

        /* 更新动画 (仅播放时) */
        if (g.state == 1) {
            /* 节拍缓动 */
            if (g.beat_ease_frames > 0) {
                g.beat_ease_frames--;
                if (g.beat_ease_frames == 0)
                    g.beat_target_scale = 256;
            }
            /* 进度条 */
            if (g.prog_arc < 256) {
                g.prog_arc += ANIM_PROG_SPEED;
                if (g.prog_arc > 256) g.prog_arc = 256;
            }
            /* 子拍进度 */
            if (g.sub_prog < 256) {
                g.sub_prog += ANIM_PROG_SPEED / 2;
                if (g.sub_prog > 256) g.sub_prog = 256;
            }
        }

        /* 按键反馈衰减 */
        if (g.key_feedback > 0)
            g.key_feedback--;

        draw_ui();
        rb->yield();
    }

    return PLUGIN_OK;
}

