/*
 * Altair 8800 模拟器 V0.1 - UI 预览版
 * 仅实现界面绘制和按键流程，不包含 CPU 模拟、文件读写。
 * 
 * 按键映射：
 *   PLAY  : 弹出 "Run now?" 对话框
 *   MENU  : 退出插件
 *   POWER : 退出插件
 *   (其他按键无功能)
 */

#include "plugin.h"

/* ============================================================
   屏幕尺寸（iPod Color）
   ============================================================ */
#define SCREEN_W 220
#define SCREEN_H 176

/* ============================================================
   LED 布局参数
   ============================================================ */
#define LED_SIZE     8          /* 方块边长（像素） */
#define LED_GAP      2          /* 方块间距 */
#define LED_GROUP_GAP 10        /* 三组之间的垂直间距 */

/* 颜色定义（RGB565） */
#define COLOR_LED_ON  0xF800    /* 红色 */
#define COLOR_LED_OFF 0x8410    /* 深灰色（像熄灭的灯） */
#define COLOR_BG      0x0000    /* 黑色背景 */
#define COLOR_TEXT    0xFFFF    /* 白色文字 */

/* ============================================================
   模拟状态数据（纯展示用）
   ============================================================ */
static uint16_t mock_addr = 0x03F2;   /* 地址 LED 将显示这个值的二进制 */
static uint8_t  mock_data = 0x7C;     /* 数据 LED 将显示这个值的二进制 */
static uint8_t  mock_status = 0x0F;   /* 状态 LED 只用低 12 位（这里设全亮） */
static const char *mock_filename = "KillBit.bin";

/* ============================================================
   绘图辅助函数
   ============================================================ */

/* 画一个 LED 方块 */
static void draw_led(int x, int y, bool on) {
    rb->lcd_set_foreground(on ? COLOR_LED_ON : COLOR_LED_OFF);
    rb->lcd_fillrect(x, y, LED_SIZE, LED_SIZE);
}

/* 在 LED 下方画标签（比如 "A0"） */
static void draw_label(int x, int y, const char *label) {
    rb->lcd_set_foreground(COLOR_TEXT);
    rb->lcd_putsxy(x, y, label, FONT_UI);   /* FONT_UI 是系统字体，够小 */
}

/* ============================================================
   主界面绘制
   ============================================================ */
static void draw_ui(void) {
    int i, x, y;
    char buf[32];

    /* ---------- 清屏 ---------- */
    rb->lcd_clear_display();
    rb->lcd_set_background(COLOR_BG);
    rb->lcd_set_foreground(COLOR_TEXT);

    /* ---------- 标题 ---------- */
    rb->lcd_puts(0, 0, "ALT AIR 8800");

    /* ---------- 地址区：显示 16 位地址值 ---------- */
    rb->snprintf(buf, sizeof(buf), "ADDR: %04X", mock_addr);
    rb->lcd_puts(0, 1, buf);

    /* ---------- 地址 LED (16 个) ---------- */
    y = 20;                               /* 起始 Y */
    x = (SCREEN_W - (16 * (LED_SIZE + LED_GAP) - LED_GAP)) / 2;
    for (i = 0; i < 16; i++) {
        bool on = (mock_addr >> (15 - i)) & 1;  /* 从最高位开始显示 */
        draw_led(x, y, on);
        /* 标签放在下方，略偏移 */
        char label[4];
        rb->snprintf(label, sizeof(label), "A%d", 15 - i);
        draw_label(x, y + LED_SIZE + 1, label);
        x += LED_SIZE + LED_GAP;
    }

    /* ---------- 数据 LED (8 个) ---------- */
    y = 20 + LED_SIZE + 8 + LED_GROUP_GAP;   /* 与上一组隔开 */
    x = (SCREEN_W - (8 * (LED_SIZE + LED_GAP) - LED_GAP)) / 2;
    /* 显示当前数据值 */
    rb->snprintf(buf, sizeof(buf), "DATA: %02X", mock_data);
    rb->lcd_puts(0, 3, buf);   /* 用第3行显示 */
    for (i = 0; i < 8; i++) {
        bool on = (mock_data >> (7 - i)) & 1;
        draw_led(x, y, on);
        char label[4];
        rb->snprintf(label, sizeof(label), "D%d", 7 - i);
        draw_label(x, y + LED_SIZE + 1, label);
        x += LED_SIZE + LED_GAP;
    }

    /* ---------- 状态 LED (12 个) ---------- */
    y = y + LED_SIZE + 8 + LED_GROUP_GAP;
    x = (SCREEN_W - (12 * (LED_SIZE + LED_GAP) - LED_GAP)) / 2;
    for (i = 0; i < 12; i++) {
        bool on = (mock_status >> (11 - i)) & 1;  /* 低12位有效 */
        draw_led(x, y, on);
        char label[5];
        /* 模拟 Altair 的状态标签，简写 */
        const char *names[12] = {"INTE","HLTA","STOP","DBIN","WR","SYNC","HLT","OUT","IN","M1","MEMR","POWER"};
        draw_label(x, y + LED_SIZE + 1, names[i % 12]);
        x += LED_SIZE + LED_GAP;
    }

    /* ---------- 底部信息 ---------- */
    y = y + LED_SIZE + 12;
    rb->lcd_set_foreground(COLOR_TEXT);
    rb->snprintf(buf, sizeof(buf), "File: %s", mock_filename);
    rb->lcd_puts(0, 6, buf);
    rb->lcd_puts(0, 7, "PLAY: Run    MENU: Exit");

    /* 刷新屏幕 */
    rb->lcd_update();
}

/* ============================================================
   对话框（覆盖在主界面上）
   ============================================================ */
static void show_dialog(const char *title, const char *msg) {
    /* 简单地在屏幕中央显示文本，不清屏（保留背景 LED） */
    rb->lcd_set_foreground(COLOR_TEXT);
    /* 可以用一个半透明矩形做背景，这里简化，直接写文字 */
    rb->lcd_puts(5, 3, title);
    rb->lcd_puts(5, 4, msg);
    rb->lcd_update();
}

/* ============================================================
   插件入口
   ============================================================ */
enum plugin_status plugin_start(const void *parameter) {
    (void)parameter;
    int btn;
    bool run_asked = false;

    while (1) {
        /* 每次循环重新绘制主界面（保证对话框消失后恢复） */
        draw_ui();

        /* 等待按键 */
        btn = rb->button_get(true);

        if (btn == BUTTON_PLAY) {
            /* 按 PLAY 弹出询问框 */
            draw_ui();   /* 清掉可能残留的对话框 */
            show_dialog("Run now?", "PLAY to confirm, MENU to cancel");

            /* 等待用户选择 */
            btn = rb->button_get(true);
            if (btn == BUTTON_PLAY) {
                /* 确认运行（仅展示，不实际执行） */
                draw_ui();
                show_dialog("Running...", "Press any key to stop");
                rb->button_get(true);   /* 等待任意按键返回主界面 */
                /* 回到主循环后会自动重绘 */
            } else if (btn == BUTTON_MENU || btn == BUTTON_POWER) {
                /* 取消，回到主界面 */
                continue;
            }
        } else if (btn == BUTTON_MENU || btn == BUTTON_POWER) {
            /* 退出插件 */
            break;
        }
    }

    return PLUGIN_OK;
}
