#include <stdint.h>
#include <stdarg.h>
#include "plugin.h"
#include "a2core.h"
#include "a2video.h"

/* ============================================================
   Apple II emulator for Rockbox (UI layer)
   Targets: iPod 4G / 5G / 6G (classic) - 320x240 colour LCD
   Core emulation lives in a2core.c, video rendering in a2video.c
   ============================================================ */

/* ---------------- logging ---------------- */
static char *log_buf = NULL;
static size_t log_buf_size = 0;
static size_t log_buf_used = 0;
static void log_init(void) {
    size_t avail;
    log_buf = rb->plugin_get_buffer(&avail);
    if (!log_buf || avail < 4096) {
        static char fallback[4096];
        log_buf = fallback;
        log_buf_size = sizeof(fallback);
    } else {
        log_buf_size = (avail < 64 * 1024) ? avail : 64 * 1024;
    }
    log_buf_used = 0;
}
static void log_message(const char *fmt, ...) {
    if (!log_buf || log_buf_used >= log_buf_size - 256) return;
    char tmp[256];
    va_list args;
    va_start(args, fmt);
    rb->vsnprintf(tmp, sizeof(tmp), fmt, args);
    va_end(args);
    int n = rb->snprintf(log_buf + log_buf_used, log_buf_size - log_buf_used,
                         "%s\n", tmp);
    if (n > 0) log_buf_used += n;
}
#define LOG(fmt, ...) log_message(fmt, ##__VA_ARGS__)

/* ============================================================
   Load ROM: /apple2/roms/apple2.rom
   ============================================================ */
static bool load_rom(void) {
    const char *path = "/apple2/roms/apple2.rom";
    int fd = rb->open(path, O_RDONLY);
    if (fd < 0) return false;
    off_t size = rb->ffilesize(fd);
    LOG("ROM size %ld", (long)size);
    static uint8_t rombuf[0x5000];
    uint8_t *dst = rombuf;
    uint32_t rd = size;
    if (rd > sizeof(rombuf)) {
        rb->lseek(fd, size - (off_t)sizeof(rombuf), SEEK_SET);
        rd = sizeof(rombuf);
    }
    rb->read(fd, dst, rd);
    rb->close(fd);
    a2_load_rom(rombuf, rd);
    return true;
}

/* ============================================================
   Screen layout (iPod classic 320x240)
   ============================================================ */
#define VIDEO_W   320
#define VIDEO_H   192
#define VIDEO_TOP 20             /* below 2 status lines (~20px) */
#define KEY_TOP   (VIDEO_TOP + VIDEO_H)   /* 212 */
#define KEY_H     13             /* keyboard row step in px */

#define COLOR_BG      0x0000
#define COLOR_TEXT    0xFFFF
#define COLOR_STATUS  0xFFFF
#define COLOR_KEY_SEL 0xFFFF

/* ============================================================
   Virtual keyboard (2 rows x 20 cols)
   Flat index: row * KEY_COLS + col
   Row 2: col 6 = space (drawn "SP"), col 19 = ENTER (drawn "CR")
   ============================================================ */
#define KEY_ROWS 2
#define KEY_COLS 20
#define KEY_TOTAL (KEY_ROWS * KEY_COLS)
static const char kb_layout[KEY_ROWS][KEY_COLS] = {
    "ABCDEFGHIJKLMNOPQRST",
    "UVWXYZ 0123456789_#"   /* 19 chars + NUL; col 19 rendered as "CR" */
};
static int kb_index = 0;

static int kb_cell_space(int r, int c) {
    return (r == 1 && c == 6);   /* real space character in row 2 */
}
static int kb_cell_enter(int r, int c) {
    return (r == 1 && c == 19);  /* trailing NUL slot, rendered as "CR" */
}

/* ============================================================
   Status bar + UI
   ============================================================ */
static void draw_status(void) {
    char buf[64];
    rb->lcd_set_foreground(COLOR_STATUS);
    rb->snprintf(buf, sizeof(buf), "Apple II  PC:%04X A:%02X X:%02X Y:%02X",
                 a2_get_pc(), a2_get_a(), a2_get_x(), a2_get_y());
    rb->lcd_puts(0, 0, buf);
    rb->lcd_puts(0, 1, "Sel:key Play:CR Rgt:ESC Lft:BS Menu:exit");
}

static void draw_keyboard(void) {
    int r, c;
    for (r = 0; r < KEY_ROWS; r++) {
        for (c = 0; c < KEY_COLS; c++) {
            char buf2[4];
            if (kb_cell_space(r, c)) {
                buf2[0] = 'S'; buf2[1] = 'P'; buf2[2] = '\0';
            } else if (kb_cell_enter(r, c)) {
                buf2[0] = 'C'; buf2[1] = 'R'; buf2[2] = '\0';
            } else {
                buf2[0] = kb_layout[r][c];
                buf2[1] = '\0';
            }
            /* 320px / 20 cols = 16px per slot */
            int lx = 2 + c * (320 / KEY_COLS);
            int ly = KEY_TOP + r * KEY_H;
            if (r * KEY_COLS + c == kb_index) {
                rb->lcd_set_foreground(COLOR_BG);
                rb->lcd_set_background(COLOR_KEY_SEL);
            } else {
                rb->lcd_set_foreground(COLOR_TEXT);
                rb->lcd_set_background(COLOR_BG);
            }
            rb->lcd_putsxy(lx, ly, buf2);
        }
    }
    rb->lcd_set_foreground(COLOR_TEXT);
    rb->lcd_set_background(COLOR_BG);
}

static void draw_ui(void) {
    rb->lcd_clear_display();
    rb->lcd_set_background(COLOR_BG);
    draw_status();

    /* video */
    a2_render_video();
    rb->lcd_bitmap_part(a2_fb, 0, 0, VIDEO_W, 0, VIDEO_TOP, VIDEO_W, VIDEO_H);
    rb->lcd_update_rect(0, VIDEO_TOP, VIDEO_W, VIDEO_H);

    draw_keyboard();
    rb->lcd_update();
}

/* ============================================================
   Plugin entry
   ============================================================ */
#ifdef HAVE_WHEEL_POSITION
static int last_wheel = -1;
#endif

enum plugin_status plugin_start(const void *parameter) {
    (void)parameter;
    log_init();

#ifdef HAVE_WHEEL_POSITION
    rb->wheel_send_events(false);
#endif

    LOG("=== Apple II Emulator START ===");

    if (!load_rom()) {
        rb->splash(HZ * 2, "ROM not found (/apple2/roms/apple2.rom)");
        return PLUGIN_ERROR;
    }

    a2_reset();
    LOG("reset PC=%04X", a2_get_pc());

    int btn;
    while (1) {
        /* run CPU for a chunk */
        for (int i = 0; i < 3000; i++) {
            if (!a2_step()) break;
        }

#ifdef HAVE_WHEEL_POSITION
        /* wheel scrolls the flat keyboard index */
        int wheel = rb->wheel_status();
        if (wheel >= 0 && last_wheel >= 0) {
            int delta = wheel - last_wheel;
            if (delta < -48) delta += 96;
            else if (delta > 48) delta -= 96;
            if (delta >= 2) {
                kb_index += delta / 2;
                while (kb_index >= KEY_TOTAL) kb_index -= KEY_TOTAL;
            } else if (delta <= -2) {
                kb_index += delta / 2;
                while (kb_index < 0) kb_index += KEY_TOTAL;
            }
        }
        last_wheel = wheel;
#endif

        while ((btn = rb->button_get(false)) != 0) {
            if (btn & BUTTON_REL) continue;
            if (btn & BUTTON_REPEAT) continue;

            if (btn == BUTTON_MENU) {
                LOG("=== Apple II Emulator EXIT ===");
#ifdef HAVE_WHEEL_POSITION
                rb->wheel_send_events(true);
#endif
                return PLUGIN_OK;
            } else if (btn == BUTTON_SELECT) {
                int r = kb_index / KEY_COLS;
                int c = kb_index % KEY_COLS;
                if (kb_cell_space(r, c)) {
                    a2_press_key(' ');
                } else if (kb_cell_enter(r, c)) {
                    a2_press_key('\r');
                } else {
                    a2_press_key(kb_layout[r][c]);
                }
            } else if (btn == BUTTON_PLAY) {
                a2_press_key('\r');
            } else if (btn == BUTTON_RIGHT) {
                a2_press_key(0x1B);   /* ESC */
            } else if (btn == BUTTON_LEFT) {
                a2_press_key(0x08);   /* BS */
            } else if (btn == BUTTON_SCROLL_FWD) {
                kb_index++;
                if (kb_index >= KEY_TOTAL) kb_index = 0;
            } else if (btn == BUTTON_SCROLL_BACK) {
                kb_index--;
                if (kb_index < 0) kb_index = KEY_TOTAL - 1;
            }
        }

        draw_ui();
        rb->yield();
    }
}
