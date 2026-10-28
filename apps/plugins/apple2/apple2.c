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
     0   - 20   status bar (2 text lines)
     20  - 180  Apple II video (320x160, bottom 32px cropped)
     180 - 240  virtual keyboard (3 rows x 20px)
   Keyboard always wins; the video area is squeezed on purpose
   so the bottom keyboard row can never be clipped.
   ============================================================ */
#define VIDEO_W   320
#define VIDEO_H   160
#define VIDEO_TOP 20
#define KEY_TOP   (VIDEO_TOP + VIDEO_H)   /* 180 */
#define KEY_H     20
#define KEY_ROWS  3
#define KEY_COLS  20
#define KEY_TOTAL (KEY_ROWS * KEY_COLS)
#define KB_PAGES  4

#define COLOR_BG      0x0000
#define COLOR_TEXT    0xFFFF
#define COLOR_STATUS  0xFFFF
#define COLOR_KEY_SEL 0xFFFF

/* ============================================================
   Virtual keyboard: 4 pages, 3 rows x 20 cols.
   Page 0 = UPPER, 1 = lower, 2 = digits/math, 3 = symbols.
   Row 2 col 19 is always the page switcher ("PG").
   Page 0/1 row 1 col 6 is the space cell ("SP").
   ============================================================ */
static int kb_page = 0;
static int kb_index = 0;

static const char kb_page_upper[KEY_ROWS][KEY_COLS + 1] = {
    "ABCDEFGHIJKLMNOPQRST",
    "UVWXYZ 123456789.,;:",
    "!?\"'()[]{}+-*/<>_@#P"
};
static const char kb_page_lower[KEY_ROWS][KEY_COLS + 1] = {
    "abcdefghijklmnopqrst",
    "uvwxyz 123456789.,;:",
    "!?\"'()[]{}+-*/<>_@#P"
};
static const char kb_page_digit[KEY_ROWS][KEY_COLS + 1] = {
    "0123456789+-*/=<>  ",
    ".,;:!?\"'()[]{}%#@&",
    "^_|~`\\            P"
};
static const char kb_page_sym[KEY_ROWS][KEY_COLS + 1] = {
    "!\"#$%&'()*+,-./:;<",
    "=>?@[\\]^_`{|}~    ",
    "                   P"
};

static const char (*kb_layouts[KB_PAGES])[KEY_COLS + 1] = {
    kb_page_upper, kb_page_lower, kb_page_digit, kb_page_sym
};

static char kb_get(int r, int c) {
    return kb_layouts[kb_page][r][c];
}

static int kb_cell_page(int r, int c) {
    return (r == 2 && c == 19);
}

static int kb_cell_space(int r, int c) {
    return ((kb_page == 0 || kb_page == 1) && r == 1 && c == 6);
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
    rb->lcd_puts(0, 1, "SEL:key PG:page PLAY:CR MENU:exit");
}

static void draw_keyboard(void) {
    int r, c;
    for (r = 0; r < KEY_ROWS; r++) {
        int ly = KEY_TOP + r * KEY_H;
        for (c = 0; c < KEY_COLS; c++) {
            char buf2[4];
            int lx = c * (SCREEN_W_PLACEHOLDER / KEY_COLS);
            int sel = (r * KEY_COLS + c == kb_index);

            if (kb_cell_page(r, c)) {
                buf2[0] = 'P'; buf2[1] = 'G'; buf2[2] = '\0';
            } else if (kb_cell_space(r, c)) {
                buf2[0] = 'S'; buf2[1] = 'P'; buf2[2] = '\0';
            } else {
                char ch = kb_get(r, c);
                buf2[0] = ch ? ch : ' ';
                buf2[1] = '\0';
            }

            if (sel) {
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
        for (int i = 0; i < 3000; i++) {
            if (!a2_step()) break;
        }

#ifdef HAVE_WHEEL_POSITION
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
                if (kb_cell_page(r, c)) {
                    kb_page = (kb_page + 1) % KB_PAGES;
                } else if (kb_cell_space(r, c)) {
                    a2_press_key(' ');
                } else {
                    char ch = kb_get(r, c);
                    if (ch) a2_press_key(ch);
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
