/*
 * Apple I 模拟器 V0.1 - 终端 UI 预览版
 * 特性：
 * 1. 纯像素绘制 40列 x 24行 5x7 点阵字符，完美适配 220x176 屏幕。
 * 2. 屏幕上方显示 CPU 状态（模拟值）。
 * 3. 中间 40x24 终端显示区域。
 * 4. 底部滚轮 ASCII 字符选择器（A-Z, 0-9, 符号, 回车, 退格）。
 * 5. 交互：滚轮移动光标，SELECT 输入字符，PLAY 运行。
 */

#include "plugin.h"

/* ============================================================
   屏幕尺寸
   ============================================================ */
#define SCREEN_W 220
#define SCREEN_H 176

/* ============================================================
   终端参数（5x7 点阵）
   ============================================================ */
#define COLS 40
#define ROWS 24
#define CHAR_W 5
#define CHAR_H 7
#define TERM_X_OFFSET ((SCREEN_W - (COLS * CHAR_W)) / 2)  /* 左右居中 */
#define TERM_Y_OFFSET 16                                  /* 顶部留空间给状态栏 */

/* ============================================================
   颜色定义
   ============================================================ */
#define COLOR_BG      0x0000    /* 黑色背景 */
#define COLOR_TEXT    0x00FF    /* 绿色字符（Apple I 经典绿色） */
#define COLOR_CURSOR  0xFFFF    /* 白色光标 */
#define COLOR_LABEL   0x8888    /* 灰色标签 */

/* ============================================================
   5x7 点阵字模表（ASCII 32~95，即空格到下划线）
   每个字节表示一行，低5位有效。
   ============================================================ */
static const uint8_t font5x7[][5] = {
    /* 32 空格 */ {0x00,0x00,0x00,0x00,0x00},
    /* 33 !     */ {0x00,0x17,0x00,0x00,0x00}, /* 简化 */
    /* 34 "     */ {0x00,0x00,0x00,0x00,0x00},
    /* 35 #     */ {0x00,0x00,0x00,0x00,0x00},
    /* 36 $     */ {0x00,0x00,0x00,0x00,0x00},
    /* 37 %     */ {0x00,0x00,0x00,0x00,0x00},
    /* 38 &     */ {0x00,0x00,0x00,0x00,0x00},
    /* 39 '     */ {0x00,0x00,0x00,0x00,0x00},
    /* 40 (     */ {0x00,0x00,0x00,0x00,0x00},
    /* 41 )     */ {0x00,0x00,0x00,0x00,0x00},
    /* 42 *     */ {0x00,0x00,0x00,0x00,0x00},
    /* 43 +     */ {0x00,0x00,0x00,0x00,0x00},
    /* 44 ,     */ {0x00,0x00,0x00,0x00,0x00},
    /* 45 -     */ {0x00,0x00,0x00,0x00,0x00},
    /* 46 .     */ {0x00,0x00,0x00,0x00,0x00},
    /* 47 /     */ {0x00,0x00,0x00,0x00,0x00},
    /* 48 0     */ {0x1C,0x22,0x22,0x22,0x1C},
    /* 49 1     */ {0x08,0x18,0x08,0x08,0x1C},
    /* 50 2     */ {0x1C,0x22,0x04,0x08,0x3E},
    /* 51 3     */ {0x1C,0x22,0x0C,0x22,0x1C},
    /* 52 4     */ {0x10,0x28,0x28,0x3E,0x08},
    /* 53 5     */ {0x3E,0x20,0x3C,0x02,0x3C},
    /* 54 6     */ {0x1C,0x20,0x3C,0x22,0x1C},
    /* 55 7     */ {0x3E,0x02,0x04,0x08,0x10},
    /* 56 8     */ {0x1C,0x22,0x1C,0x22,0x1C},
    /* 57 9     */ {0x1C,0x22,0x1E,0x02,0x1C},
    /* 58 :     */ {0x00,0x00,0x00,0x00,0x00},
    /* 59 ;     */ {0x00,0x00,0x00,0x00,0x00},
    /* 60 <     */ {0x00,0x00,0x00,0x00,0x00},
    /* 61 =     */ {0x00,0x00,0x00,0x00,0x00},
    /* 62 >     */ {0x00,0x00,0x00,0x00,0x00},
    /* 63 ?     */ {0x1C,0x22,0x04,0x00,0x04},
    /* 64 @     */ {0x1C,0x22,0x2A,0x2E,0x10},
    /* 65 A     */ {0x1C,0x22,0x22,0x3E,0x22},
    /* 66 B     */ {0x3C,0x22,0x3C,0x22,0x3C},
    /* 67 C     */ {0x1C,0x22,0x20,0x22,0x1C},
    /* 68 D     */ {0x3C,0x22,0x22,0x22,0x3C},
    /* 69 E     */ {0x3E,0x20,0x3C,0x20,0x3E},
    /* 70 F     */ {0x3E,0x20,0x3C,0x20,0x20},
    /* 71 G     */ {0x1C,0x22,0x2E,0x22,0x1C},
    /* 72 H     */ {0x22,0x22,0x3E,0x22,0x22},
    /* 73 I     */ {0x1C,0x08,0x08,0x08,0x1C},
    /* 74 J     */ {0x3E,0x08,0x08,0x28,0x18},
    /* 75 K     */ {0x22,0x24,0x38,0x24,0x22},
    /* 76 L     */ {0x20,0x20,0x20,0x20,0x3E},
    /* 77 M     */ {0x41,0x63,0x55,0x49,0x41},
    /* 78 N     */ {0x22,0x32,0x2A,0x26,0x22},
    /* 79 O     */ {0x1C,0x22,0x22,0x22,0x1C},
    /* 80 P     */ {0x3C,0x22,0x3C,0x20,0x20},
    /* 81 Q     */ {0x1C,0x22,0x22,0x2A,0x1C},
    /* 82 R     */ {0x3C,0x22,0x3C,0x24,0x22},
    /* 83 S     */ {0x1C,0x22,0x08,0x22,0x1C},
    /* 84 T     */ {0x3E,0x08,0x08,0x08,0x08},
    /* 85 U     */ {0x22,0x22,0x22,0x22,0x1C},
    /* 86 V     */ {0x22,0x22,0x22,0x14,0x08},
    /* 87 W     */ {0x41,0x49,0x49,0x49,0x36},
    /* 88 X     */ {0x22,0x14,0x08,0x14,0x22},
    /* 89 Y     */ {0x22,0x22,0x14,0x08,0x08},
    /* 90 Z     */ {0x3E,0x04,0x08,0x10,0x3E},
    /* 91 [     */ {0x00,0x00,0x00,0x00,0x00},
    /* 92 \     */ {0x00,0x00,0x00,0x00,0x00},
    /* 93 ]     */ {0x00,0x00,0x00,0x00,0x00},
    /* 94 ^     */ {0x00,0x00,0x00,0x00,0x00},
    /* 95 _     */ {0x3E,0x02,0x04,0x08,0x10}, /* 简化，实际是下划线 */
};

/* ============================================================
   虚拟终端显存 (40x24)
   ============================================================ */
static char video[ROWS][COLS + 1]; /* +1 留 \0 结尾，方便调试 */
static int cursor_x = 0;
static int cursor_y = 0;

/* 模拟的 CPU 状态（仅做展示） */
static uint16_t mock_pc = 0x0100;
static uint8_t  mock_a = 0x00;
static uint8_t  mock_x = 0x00;
static uint8_t  mock_y = 0x00;

/* ============================================================
   键盘字符选择器（可输入的 ASCII 字符集）
   ============================================================ */
static const char *keyboard_chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 .:-+*/=();,";
static int kb_index = 0;        /* 当前高亮字符在字符串中的位置 */

/* ============================================================
   绘制单个 5x7 点阵字符
   ============================================================ */
static void draw_char_at(int px, int py, char ch, uint16_t color) {
    int idx = (unsigned char)ch - 32;
    if (idx < 0 || idx >= 64) return; /* 仅支持 ASCII 32~95 */

    rb->lcd_set_foreground(color);
    for (int row = 0; row < 7; row++) {
        uint8_t line = font5x7[idx][row];
        for (int col = 0; col < 5; col++) {
            if (line & (1 << (4 - col))) { /* 高位在前，对应屏幕左到右 */
                rb->lcd_fillrect(px + col, py + row, 1, 1);
            }
        }
    }
}

/* ============================================================
   绘制整个终端屏幕
   ============================================================ */
static void render_terminal(void) {
    int px = TERM_X_OFFSET;
    int py = TERM_Y_OFFSET;

    /* 填充终端背景（黑色） */
    rb->lcd_set_foreground(COLOR_BG);
    rb->lcd_fillrect(px, py, COLS * CHAR_W, ROWS * CHAR_H);

    /* 绘制所有字符 */
    for (int row = 0; row < ROWS; row++) {
        for (int col = 0; col < COLS; col++) {
            char ch = video[row][col];
            if (ch == 0) ch = ' '; /* 未初始化为空格 */
            int x = px + col * CHAR_W;
            int y = py + row * CHAR_H;
            draw_char_at(x, y, ch, COLOR_TEXT);
        }
    }

    /* 绘制光标（白色下划线） */
    if (cursor_x < COLS && cursor_y < ROWS) {
        int cx = px + cursor_x * CHAR_W;
        int cy = py + cursor_y * CHAR_H + CHAR_H - 1;
        rb->lcd_set_foreground(COLOR_CURSOR);
        rb->lcd_fillrect(cx, cy, CHAR_W, 1);
    }
}

/* ============================================================
   绘制键盘选择器（屏幕底部）
   ============================================================ */
static void render_keyboard(void) {
    int y = TERM_Y_OFFSET + ROWS * CHAR_H + 4; /* 终端下方留 4px 间距 */
    int x = 0;

    rb->lcd_set_foreground(COLOR_LABEL);
    rb->lcd_setfont(FONT_UI); /* 用系统字体显示提示，更清晰 */
    rb->lcd_puts(0, 7, "KB: "); /* 系统字体行高约8px，放在最底行 */

    /* 手动绘制键盘字符条，用方块高亮选中的字符 */
    int len = rb->strlen(keyboard_chars);
    int start_x = 24; /* 从 "KB: " 后面开始 */
    int char_spacing = 7; /* 每个字符占 5px 宽 + 2px 间隔 */
    int total_width = len * char_spacing;
    int offset_x = (SCREEN_W - start_x - total_width) / 2 + start_x;

    for (int i = 0; i < len; i++) {
        int px = offset_x + i * char_spacing;
        char ch = keyboard_chars[i];
        if (i == kb_index) {
            /* 高亮显示：画一个白色背景方块，再画黑色字符 */
            rb->lcd_set_foreground(COLOR_CURSOR);
            rb->lcd_fillrect(px - 1, y - 1, CHAR_W + 2, CHAR_H + 2);
            draw_char_at(px, y, ch, COLOR_BG);
        } else {
            draw_char_at(px, y, ch, COLOR_TEXT);
        }
    }
}

/* ============================================================
   绘制状态栏
   ============================================================ */
static void render_status(void) {
    char buf[64];
    rb->lcd_set_foreground(COLOR_TEXT);
    rb->lcd_setfont(FONT_UI);
    rb->snprintf(buf, sizeof(buf), "PC:%04X A:%02X X:%02X Y:%02X",
                 mock_pc, mock_a, mock_x, mock_y);
    rb->lcd_puts(0, 0, buf);
    rb->lcd_puts(0, 1, "Apple I Simulator V0.1  (PLAY=Run, MENU=Exit)");
}

/* ============================================================
   UI 主绘制函数
   ============================================================ */
static void draw_ui(void) {
    rb->lcd_clear_display();
    rb->lcd_set_background(COLOR_BG);

    render_status();
    render_terminal();
    render_keyboard();

    rb->lcd_update();
}

/* ============================================================
   向终端输入一个字符（模拟键盘输入）
   ============================================================ */
static void type_char(char ch) {
    if (ch == '\b') { /* 退格 */
        if (cursor_x > 0) {
            cursor_x--;
            video[cursor_y][cursor_x] = ' ';
        } else if (cursor_y > 0) {
            cursor_y--;
            cursor_x = COLS - 1;
            video[cursor_y][cursor_x] = ' ';
        }
        return;
    }
    if (ch == '\r') { /* 回车 */
        cursor_x = 0;
        if (cursor_y < ROWS - 1) {
            cursor_y++;
        } else {
            /* 屏幕滚动：将上面所有行上移一行（纯 UI 模拟） */
            for (int r = 1; r < ROWS; r++) {
                rb->memcpy(video[r-1], video[r], COLS);
            }
            rb->memset(video[ROWS-1], ' ', COLS);
        }
        return;
    }
    if (ch >= 32 && ch <= 95) {
        video[cursor_y][cursor_x] = ch;
        cursor_x++;
        if (cursor_x >= COLS) {
            cursor_x = 0;
            if (cursor_y < ROWS - 1) cursor_y++;
            else {
                /* 屏幕滚动 */
                for (int r = 1; r < ROWS; r++) {
                    rb->memcpy(video[r-1], video[r], COLS);
                }
                rb->memset(video[ROWS-1], ' ', COLS);
            }
        }
    }
}

/* ============================================================
   插件入口
   ============================================================ */
enum plugin_status plugin_start(const void *parameter) {
    (void)parameter;
    int btn;

    /* 清空显存（全部填空格） */
    for (int r = 0; r < ROWS; r++) {
        rb->memset(video[r], ' ', COLS);
        video[r][COLS] = '\0';
    }
    /* 在屏幕上写一些欢迎字符，模拟 Apple I 启动 */
    rb->strcpy(video[0], "  WELCOME TO APPLE I EMULATOR       ");
    rb->strcpy(video[1], "  TYPE 'HELLO' AND PRESS RETURN    ");
    rb->strcpy(video[2], "  TO SEE THE MAGIC!                ");
    cursor_x = 0;
    cursor_y = 2;

    while (1) {
        draw_ui();

        btn = rb->button_get(true);

        if (btn == BUTTON_MENU) {
            break; /* 退出 */
        }
        else if (btn == BUTTON_PLAY) {
            /* 模拟运行：在终端输出一段文字，展示交互 */
            type_char('\r');
            type_char('H');
            type_char('E');
            type_char('L');
            type_char('L');
            type_char('O');
            type_char('\r');
            type_char(' ');
            type_char(' ');
            type_char(' ');
            type_char('W');
            type_char('O');
            type_char('R');
            type_char('L');
            type_char('D');
            type_char('!');
            type_char('\r');
            continue;
        }
        else if (btn == BUTTON_SCROLL_FWD) {
            kb_index++;
            if (kb_index >= (int)rb->strlen(keyboard_chars)) kb_index = 0;
        }
        else if (btn == BUTTON_SCROLL_BACK) {
            kb_index--;
            if (kb_index < 0) kb_index = rb->strlen(keyboard_chars) - 1;
        }
        else if (btn == BUTTON_SELECT) {
            /* 将当前选中的字符输入到终端 */
            char ch = keyboard_chars[kb_index];
            type_char(ch);
        }
    }

    return PLUGIN_OK;
}
