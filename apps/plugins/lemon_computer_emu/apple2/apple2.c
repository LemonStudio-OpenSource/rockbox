#include <stdint.h>
#include <stdarg.h>
#include "plugin.h"
#include "../core/logger.h"
#include "a2core.h"
#include "a2video.h"

/* ============================================================
   Apple II emulator for Rockbox (UI layer)

   Layout (320x240):
     0   - 12   status bar (1 line, 10-Fixed)
     12  - 204  Apple II video (320x192, full frame)
     204 - 240  virtual keyboard (3 rows x 12px, 10-Fixed)

   Keyboard: keys go into a FIFO and are fed to the Apple II
   keyboard latch when it is free. BACKSPACE sends 0x08 0x20 0x08
   so GETLN deletes the char, the space erases the glyph on the
   text page, and the second backspace removes the space from
   the GETLN buffer again.

   Apple II+ has no lowercase, so there is no lowercase page.
   ============================================================ */


   /* ---------------- logging ---------------- */
#define APPLE2_LOG_PATH "/lemon_computer_emu/logs/apple/apple2/apple2.log"
#define APPLE2_LOG_CAP  (64 * 1024)

static char apple2_log_fallback[4096];
static logger_t apple2_log;

static void log_init(void) {
    size_t avail = 0;
    char *buf = rb->plugin_get_buffer(&avail);
    size_t cap;

    if (!buf || avail < 4096) {
        buf = apple2_log_fallback;
        cap = sizeof(apple2_log_fallback);
    } else {
        cap = (avail < APPLE2_LOG_CAP) ? avail : APPLE2_LOG_CAP;
    }
    logger_init(&apple2_log, buf, cap, APPLE2_LOG_PATH);
}

static void apple2_log_msg(const char *fmt, ...) {
    char tmp[256];
    va_list args;

    if (!apple2_log.enabled)
        return;

    va_start(args, fmt);
    rb->vsnprintf(tmp, sizeof(tmp), fmt, args);
    va_end(args);

    logger_log(&apple2_log, "%s\n", tmp);
}

#define LOG(fmt, ...) apple2_log_msg(fmt, ##__VA_ARGS__)

/* ============================================================
   Load ROM
   ============================================================ */
static bool load_rom(void) {
    const char* path = "/lemon_computer_emu/roms/apple/apple2/apple2.rom";
    int fd = rb->open(path, O_RDONLY);
    if (fd < 0) return false;
    off_t size = rb->ffilesize(fd);
    LOG("ROM size %ld", (long)size);
    static uint8_t rombuf[0x5000];
    uint32_t rd = size;
    if (rd > sizeof(rombuf)) {
        rb->lseek(fd, size - (off_t)sizeof(rombuf), SEEK_SET);
        rd = sizeof(rombuf);
    }
    rb->read(fd, rombuf, rd);
    rb->close(fd);
    a2_load_rom(rombuf, rd);
    return true;
}

/* ============================================================
   Screen layout (320x240)
   ============================================================ */
#define VIDEO_W   320
#define VIDEO_TOP 12
#define VIDEO_H   192
#define KEY_TOP   (VIDEO_TOP + VIDEO_H)   /* 204 */
#define KEY_H     12
#define KEY_ROWS  3
#define KEY_COLS  20
#define KEY_TOTAL (KEY_ROWS * KEY_COLS)
#define KB_PAGES  3

#define COLOR_BG      0x0000
#define COLOR_TEXT    0xFFFF
#define COLOR_STATUS  0xFFFF
#define COLOR_KEY_SEL 0xFFFF

/* ============================================================
   Font
   ============================================================ */
static struct font *fixed_font = NULL;

static bool init_font(void) {
    fixed_font = rb->font_load("/.rockbox/fonts/10-Fixed.fnt");
    if (!fixed_font) {
        rb->splash(HZ * 2, "Font not found");
        return false;
    }
    rb->lcd_setfont(fixed_font);
    return true;
}

/* ============================================================
   Virtual keyboard
   ============================================================ */
static int kb_page = 0;
static int kb_index = 0;

static const char kb_page_upper[KEY_ROWS][KEY_COLS + 1] = {
    "ABCDEFGHIJKLMNOPQRST",
    "UVWXYZ 0123456789.,:",
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
    kb_page_upper, kb_page_digit, kb_page_sym
};

static char kb_get(int r, int c) { return kb_layouts[kb_page][r][c]; }
static int kb_cell_page(int r, int c) { return (r == 2 && c == 19); }
static int kb_cell_space(int r, int c){ return (kb_page == 0
                                              && r == 1 && c == 6); }

/* ============================================================
   Key FIFO
   ============================================================ */
#define KEY_QUEUE_SIZE 256
static uint8_t key_queue[KEY_QUEUE_SIZE];
static volatile int key_q_head = 0;
static volatile int key_q_tail = 0;

static inline int key_q_empty(void) { return key_q_head == key_q_tail; }
static inline int key_q_full(void)  { return ((key_q_tail + 1) % KEY_QUEUE_SIZE)
                                          == key_q_head; }
static inline void key_q_push(uint8_t c) {
    if (!key_q_full()) {
        key_queue[key_q_tail] = c;
        key_q_tail = (key_q_tail + 1) % KEY_QUEUE_SIZE;
    }
}
static inline uint8_t key_q_pop(void) {
    if (key_q_empty()) return 0;
    uint8_t c = key_queue[key_q_head];
    key_q_head = (key_q_head + 1) % KEY_QUEUE_SIZE;
    return c;
}
static inline void key_q_clear(void) { key_q_head = key_q_tail = 0; }

/* ============================================================
   Shadow buffer for short-command detection
   ============================================================ */
static char shadow_buf[128];
static int  shadow_len = 0;

/* ============================================================
   Playback for P / LOAD
   ============================================================ */
static char playback_buf[4096];
static int  playback_len = 0;
static int  playback_pos = 0;

/* ============================================================
   Save / Restore
   ============================================================ */
static bool save_state(void) {
    rb->mkdir("/lemon_computer_emu");
    rb->mkdir("/lemon_computer_emu/states");
    rb->mkdir("/lemon_computer_emu/states/apple");
    rb->mkdir("/lemon_computer_emu/states/apple/apple2");
    int fd = rb->open("/lemon_computer_emu/states/apple/apple2/apple2_ram.bin",
        O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) return false;
    rb->write(fd, a2_mem, 65536);
    uint8_t st[8];
    st[0] = a2_get_pc() & 0xFF;
    st[1] = (a2_get_pc() >> 8) & 0xFF;
    st[2] = a2_get_a();
    st[3] = a2_get_x();
    st[4] = a2_get_y();
    st[5] = a2_get_p();
    st[6] = 0;
    st[7] = 0x01;
    rb->write(fd, st, sizeof(st));
    rb->close(fd);
    return true;
}
static bool restore_state(void) {
    int fd = rb->open("/lemon_computer_emu/states/apple/apple2/apple2_ram.bin", O_RDONLY);
    if (fd < 0) return false;
    size_t size = rb->ffilesize(fd);
    if (size < 65536 + 8) { rb->close(fd); return false; }
    rb->read(fd, a2_mem, 65536);
    uint8_t st[8];
    rb->read(fd, st, sizeof(st));
    rb->close(fd);
    uint16_t pc = st[0] | (st[1] << 8);
    a2_set_regs(pc, st[2], st[3], st[4], st[5]);
    return true;
}

/* ============================================================
   Program load
   ============================================================ */
static bool load_program_from_path(const char *path) {
    int fd = rb->open(path, O_RDONLY);
    if (fd < 0) return false;
    size_t size = rb->ffilesize(fd);
    if (size == 0) { rb->close(fd); return false; }
    char temp[sizeof(playback_buf)];
    if (size > sizeof(temp) - 1) size = sizeof(temp) - 1;
    rb->read(fd, temp, size);
    rb->close(fd);
    playback_len = 0;
    for (size_t i = 0; i < size && playback_len < (int)sizeof(playback_buf) - 1; i++) {
        char c = temp[i];
        if (c == '\n') playback_buf[playback_len++] = '\r';
        else if (c == '\r') { if (i + 1 >= size || temp[i + 1] != '\n') playback_buf[playback_len++] = '\r'; }
        else playback_buf[playback_len++] = c;
    }
    playback_pos = 0;
    return true;
}

/* ============================================================
   CC lookup
   ============================================================ */
static void cc_lookup(const char *abbr) {
    struct cc_entry { const char *abbr; const char *band; const char *name; };
    static const struct cc_entry table[] = {
        {"KSM","Poppin'Party","Toyama Kasumi"},
        {"ARS","Poppin'Party","Ichigaya Arisa"},
        {"SAYA","Poppin'Party","Yamabuki Saaya"},
        {"OTAE","Poppin'Party","Hanazono Tae"},
        {"RIMI","Poppin'Party","Ushigome Rimi"},
        {"KKR","Hello, Happy World!","Tsurumaki Kokoro"},
        {"MSK","Hello, Happy World!","Okusawa Misaki"},
        {"HGM","Hello, Happy World!","Kitazawa Hagumi"},
        {"KN","Hello, Happy World!","Matsubara Kanon"},
        {"KOR","Hello, Happy World!","Seta Kaoru"},
        {"MOCA","Afterglow","Aoba Moca"},
        {"HMR","Afterglow","Uehara Himari"},
        {"TSUGU","Afterglow","Hazawa Tsugumi"},
        {"RAN","Afterglow","Mitake Ran"},
        {"SOYA","Afterglow","Udagawa Tomoe"},
        {"CST","Pastel*Palettes","Shirasagi Chisato"},
        {"HINA","Pastel*Palettes","Hikawa Hina"},
        {"EVE","Pastel*Palettes","Wakamiya Eve"},
        {"MAYA","Pastel*Palettes","Yamato Maya"},
        {"AYA","Pastel*Palettes","Maruyama Aya"},
        {"YKN","Roselia","Minato Yukina"},
        {"LISA","Roselia","Imai Lisa"},
        {"RINRIN","Roselia","Shirokane Rinko"},
        {"AKO","Roselia","Udagawa Ako"},
        {"SAYO","Roselia","Hikawa Sayo"},
        {"MSR","Morfonica","Kurata Mashiro"},
        {"TOKO","Morfonica","Kirigaya Touko"},
        {"NNM","Morfonica","Hiromachi Nanami"},
        {"TKS","Morfonica","Futaba Tsukushi"},
        {"RUI","Morfonica","Yashio Rui"},
        {"CHUCHU","RAISE A SUILEN","Tamade Chiyu"},
        {"PAREO","RAISE A SUILEN","Nyubara Reona"},
        {"MASKING","RAISE A SUILEN","Satou Masuki"},
        {"LAYER","RAISE A SUILEN","Wakana Rei"},
        {"LOCK","RAISE A SUILEN","Asahi Rokka"},
        {"ANON","MyGO!!!!!","Chihaya Anon"},
        {"TOMO","MyGO!!!!!","Takamatsu Tomori"},
        {"TAKI","MyGO!!!!!","Shiina Taki"},
        {"SOYO","MyGO!!!!!","Nagasaki Soyo"},
        {"RANA","MyGO!!!!!","Kaname Raana"},
    };
    for (unsigned i = 0; i < sizeof(table)/sizeof(table[0]); i++) {
        if (rb->strcasecmp(abbr, table[i].abbr) == 0) {
            char out[128];
            rb->snprintf(out, sizeof(out), "%s - %s", table[i].band,
                         table[i].name);
            rb->splash(HZ * 3, out);
            return;
        }
    }
    rb->splash(HZ * 2, "UNKNOWN ABBR");
}

/* ============================================================
   Erase the typed command from the Apple II screen
   ============================================================ */
static void erase_typed(void) {
    for (int i = 0; i < shadow_len; i++) {
        key_q_push(0x08);
        key_q_push(0x20);
        key_q_push(0x08);
    }
    shadow_len = 0;
}

/* ============================================================
   Handle PLAY / short commands
   ============================================================ */
static bool handle_play(void) {
    if (shadow_len == 1 && shadow_buf[0] == 'S') {
        erase_typed();
        if (save_state()) rb->splash(HZ, "SAVED");
        else              rb->splash(HZ, "SAVE FAIL");
        return false;
    }
    if (shadow_len == 1 && shadow_buf[0] == 'R') {
        erase_typed();
        if (restore_state()) rb->splash(HZ, "RESTORED");
        else                 rb->splash(HZ, "NO STATE");
        return false;
    }
    if (shadow_len == 1 && shadow_buf[0] == 'P') {
        erase_typed();
        if (load_program_from_path("/lemon_computer_emu/programs/apple/apple2/apple2_program.txt"))
            rb->splash(HZ, "LOADING");
        else
            rb->splash(HZ, "NO FILE");
        return false;
    }
    if (shadow_len > 2 && shadow_buf[0] == 'P' &&
        shadow_buf[1] == ' ') {
        char path[128];
        rb->snprintf(path, sizeof(path), "/lemon_computer_emu/programs/apple/apple2/%s", shadow_buf +
            2);
        erase_typed();
        if (load_program_from_path(path)) rb->splash(HZ, "LOADING");
        else                              rb->splash(HZ, "NO FILE");
        return false;
    }
    if (shadow_len == 2 && shadow_buf[0] == 'L' && shadow_buf[1]
        == 'P') {
        erase_typed();
        DIR* dir = rb->opendir("/lemon_computer_emu/programs/apple/apple2");
        if (dir) {
            for (int i = 0x400; i < 0x800; i++) a2_mem[i] = 0xA0;
            int row = 0;
            struct dirent *entry;
            while ((entry = rb->readdir(dir)) != NULL && row < 23)
            {
                if (entry->d_name[0] == '.') continue;
                int lb = ((row & 0x07) << 7) | (((row >> 3)
                         & 3) * 0x28);
                int len = rb->strlen(entry->d_name);
                if (len > 40) len = 40;
                for (int i = 0; i < len; i++)
                    a2_mem[0x400 + lb + i] = (uint8_t)entry->d_name[i] |
                                             0x80;
                row++;
            }
            rb->closedir(dir);
        } else {
            rb->splash(HZ * 2, "NO PROG DIR");
        }
        return false;
    }
    if (shadow_len == 1 && shadow_buf[0] == 'A') {
        erase_typed();
        rb->splash(HZ * 3, "Apple II for Rockbox");
        return false;
    }
    if (shadow_len == 1 && shadow_buf[0] == 'H') {
        erase_typed();
        rb->splash(HZ * 3, "S R P LP A H RST EX B CC");
        return false;
    }
    if (shadow_len == 3 && shadow_buf[0] == 'R' && shadow_buf[1]
        == 'S' && shadow_buf[2] == 'T') {
        erase_typed();
        a2_reset();
        key_q_clear();
        playback_pos = playback_len = 0;
        rb->splash(HZ, "RESET");
        return false;
    }
    if (shadow_len == 2 && shadow_buf[0] == 'E' && shadow_buf[1]
        == 'X') {
        erase_typed();
        return true;
    }
    if (shadow_len == 1 && shadow_buf[0] == 'B') {
        erase_typed();
        rb->splash(HZ * 2, "Yes! BanG Dream!");
        return false;
    }
    if (shadow_len >= 2 && shadow_buf[0] == 'C' &&
        shadow_buf[1] == 'C') {
        int i = 2;
        while (i < shadow_len && shadow_buf[i] == ' ') i++;
        erase_typed();
        cc_lookup(shadow_buf + i);
        return false;
    }
    key_q_push('\r');
    shadow_len = 0;
    return false;
}

/* ============================================================
   UI
   ============================================================ */
static void draw_status(void) {
    char buf[64];
    rb->lcd_set_foreground(COLOR_STATUS);
    rb->snprintf(buf, sizeof(buf), "Apple II PC:%04X", a2_get_pc());
    rb->lcd_puts(0, 0, buf);
}

static void draw_keyboard(void) {
    int r, c;
    for (r = 0; r < KEY_ROWS; r++) {
        int ly = KEY_TOP + r * KEY_H;
        for (c = 0; c < KEY_COLS; c++) {
            char buf2[4];
            int lx = c * (320 / KEY_COLS);
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
    rb->lcd_bitmap_part(a2_fb, 0, 0, VIDEO_W, 0, VIDEO_TOP, VIDEO_W,
                        VIDEO_H);
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

enum plugin_status apple2_run(const void *parameter) {
    (void)parameter;
    log_init();

#ifdef HAVE_WHEEL_POSITION
    rb->wheel_send_events(false);
#endif

    LOG("=== Apple II START ===");

    if (!init_font()) {
        logger_flush(&apple2_log);
        return PLUGIN_ERROR;
    }

    if (!load_rom()) {
        rb->splash(HZ * 2, "ROM not found");
        logger_flush(&apple2_log);
        return PLUGIN_ERROR;
    }

    a2_init(&apple2_log);
    a2_reset();
    shadow_len = 0;
    playback_len = playback_pos = 0;
    key_q_clear();

    int btn;
    while (1) {
        if (a2_kbd_key == 0 && !key_q_empty()) {
            a2_press_key(key_q_pop());
        }
        if (a2_kbd_key == 0 && playback_pos < playback_len &&
            key_q_empty()) {
            a2_press_key(playback_buf[playback_pos++]);
        }

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
                if (kb_index >= KEY_TOTAL) kb_index -= KEY_TOTAL;
            } else if (delta <= -2) {
                kb_index += delta / 2;
                if (kb_index < 0) kb_index += KEY_TOTAL;
            }
        }
        last_wheel = wheel;
#endif

        while ((btn = rb->button_get(false)) != 0) {
            if (btn & BUTTON_REL) continue;
            if (btn & BUTTON_REPEAT) continue;

            if (btn == BUTTON_MENU) {
                LOG("=== Apple II EXIT ===");
                logger_flush(&apple2_log);
#ifdef HAVE_WHEEL_POSITION
                rb->wheel_send_events(true);
#endif
                return PLUGIN_OK;
            }
            if (btn == BUTTON_PLAY) {
                if (handle_play()) {
                    logger_flush(&apple2_log);
#ifdef HAVE_WHEEL_POSITION
                    rb->wheel_send_events(true);
#endif
                    return PLUGIN_OK;
                }
            } else if (btn == BUTTON_SELECT) {
                int r = kb_index / KEY_COLS;
                int c = kb_index % KEY_COLS;
                if (kb_cell_page(r, c)) {
                    kb_page = (kb_page + 1) % KB_PAGES;
                } else if (kb_cell_space(r, c)) {
                    key_q_push(' ');
                    if (shadow_len < (int)sizeof(shadow_buf) - 1)
                        shadow_buf[shadow_len++] = ' ';
                } else {
                    char ch = kb_get(r, c);
                    if (ch) {
                        key_q_push(ch);
                        if (shadow_len < (int)sizeof(shadow_buf) - 1)
                            shadow_buf[shadow_len++] = ch;
                    }
                }
            } else if (btn == BUTTON_LEFT) {
                key_q_push(0x08);
                key_q_push(0x20);
                key_q_push(0x08);
                if (shadow_len > 0) shadow_len--;
            } else if (btn == BUTTON_RIGHT) {
                key_q_clear();
                playback_pos = playback_len = 0;
                shadow_len = 0;
                a2_reset();
            } else if (btn == BUTTON_SCROLL_FWD) {
                kb_index++;
                if (kb_index >= KEY_TOTAL) kb_index = 0;
            } else if (kb_index >= 0 && btn == BUTTON_SCROLL_BACK) {
                kb_index--;
                if (kb_index < 0) kb_index = KEY_TOTAL - 1;
            }
        }

        draw_ui();
        rb->yield();
    }
}
