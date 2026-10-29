#include "plugin.h"
#include "lemon_computer_emu.h"

typedef enum plugin_status(*emu_run_fn)(const void* parameter);

struct emu_entry {
    const char* name;
    emu_run_fn  run;
};

static const struct emu_entry emu_table[] = {
    { "APPLE1", apple1_run },
    { "APPLE2", apple2_run },
    { "TRS-80", trs80_run },
};

#define EMU_COUNT ((int)(sizeof(emu_table) / sizeof(emu_table[0])))

#define COLOR_FG 0xFFFF
#define COLOR_BG 0x0000

static void draw_menu(int sel)
{
    int i;

    rb->lcd_clear_display();
    rb->lcd_set_foreground(COLOR_FG);
    rb->lcd_set_background(COLOR_BG);

    rb->lcd_puts(0, 0, "WELCOME TO LEMON_COMPUTER_EMULATOR!");
    rb->lcd_puts(0, 1, "PLEASE CHOOSE TARGET COMPUTER");

    for (i = 0; i < EMU_COUNT; i++) {
        char line[32];
        rb->snprintf(line, sizeof(line), "%s%s",
            (i == sel) ? ">" : " ", emu_table[i].name);
        rb->lcd_puts(0, 3 + i, line);
    }

    rb->lcd_puts(0, 3 + EMU_COUNT + 1, "PRESS SELECT TO ENTER");

    rb->lcd_update();
}

/* 返回选中的模拟器索引；-1 表示用户按 MENU 退出 */
static int menu_loop(void)
{
    int sel = 0;
    int btn;
#ifdef HAVE_WHEEL_POSITION
    int last_wheel = -1;
#endif

    draw_menu(sel);

    while (1) {
#ifdef HAVE_WHEEL_POSITION
        /* 和 apple1.c 相同：用绝对位置差驱动滚动，一次拨多格也能跟上 */
        int wheel = rb->wheel_status();
        if (wheel >= 0 && last_wheel >= 0) {
            int delta = wheel - last_wheel;
            if (delta < -48) delta += 96;
            else if (delta > 48) delta -= 96;
            if (delta >= 2) {
                sel += delta / 2;
                while (sel >= EMU_COUNT) sel -= EMU_COUNT;
                draw_menu(sel);
            }
            else if (delta <= -2) {
                sel += delta / 2;
                while (sel < 0) sel += EMU_COUNT;
                draw_menu(sel);
            }
        }
        last_wheel = wheel;
#endif

        btn = rb->button_get(false);
        if (btn == 0) {
            rb->yield();
            continue;
        }
        if (btn & BUTTON_REL)
            continue;
        if (btn & BUTTON_REPEAT)
            continue;

        switch (btn) {
        case BUTTON_SELECT:
            return sel;

        case BUTTON_MENU:
            return -1;

            /* 拨轮事件由 wheel_status 处理，这里忽略按键事件 */
        case BUTTON_SCROLL_FWD:
        case BUTTON_SCROLL_BACK:
            break;

        default:
            break;
        }
    }
}

enum plugin_status plugin_start(const void* parameter)
{
    (void)parameter;

    while (1) {
        int sel;

        /* 菜单期间关闭拨轮的按键事件，只靠 wheel_status 读位置 */
#ifdef HAVE_WHEEL_POSITION
        rb->wheel_send_events(false);
#endif
        sel = menu_loop();
#ifdef HAVE_WHEEL_POSITION
        rb->wheel_send_events(true);
#endif

        if (sel < 0)
            return PLUGIN_OK;

        /* 进入模拟器；模拟器内部自己管理 wheel 事件，
           返回后菜单重新关闭 wheel 事件 */
        emu_table[sel].run(NULL);
    }
}