#include <stdint.h>
#include <stdarg.h>
#include "plugin.h"
#include "a2core.h"
#include "a2video.h"

/* ============================================================
   Apple II emulator for Rockbox (UI layer)
   Keyboard model copied from apple_i.c:
     input_buf  : self-maintained line buffer (SELECT appends,
                  LEFT deletes, PLAY commits)
     playback_buf : on PLAY, short commands are checked first;
                  if not a command, the whole line is copied
                  here and fed to the Apple II one char at a time
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

static void log_flush(void) {
    if (!log_buf || log_buf_used == 0) return;
    rb->mkdir("/apple2");
    rb->mkdir("/apple2/log");
    int fd = rb->open("/apple2/log/apple2.log",
                      O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd >= 0) {
        rb->write(fd, log_buf, log_buf_used);
        rb->close(fd);
    }
}

/* ============================================================
   Load ROM
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
   Screen layout
     0   - 14   one-line status bar
     14  - 180  Apple II video (320x166, bottom 26px cropped)
     180 - 240  virtual keyboard (3 rows x 20px)
   ============================================================ */
#define VIDEO_W   320
#define VIDEO_TOP 14
#define VIDEO_H   166
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
   Virtual keyboard: 4 pages, 3 rows x 20 cols
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

static char kb_get(int r, int c) { return kb_layouts[kb_page][r][c]; }
static int kb_cell_page(int r, int c) { return (r == 2 && c == 19); }
static int kb_cell_space(int r, int c){ return ((kb_page == 0 || kb_page == 1) && r == 1 && c == 6); }

/* ============================================================
   Line buffer + playback buffer (exactly like apple_i.c)
   ============================================================ */
static char input_buf[1024];
static int  input_len = 0;

static char playback_buf[4096];
static int  playback_len = 0;
static int  playback_pos = 0;

/* ============================================================
   Save / Restore
   ============================================================ */
static bool save_state(void) {
    rb->mkdir("/apple2");
    rb->mkdir("/apple2/state");
    int fd = rb->open("/apple2/state/apple2_ram.bin",
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
    int fd = rb->open("/apple2/state/apple2_ram.bin", O_RDONLY);
    if (fd < 0) return false;
    size_t size = rb->ffilesize(fd);
    if (size < 65536 + 8) { rb->close(fd); return false; }
    rb->read(fd, a2_mem, 65536);
    uint8_t st[8];
    rb->read(fd, st, sizeof(st));
    rb->close(fd);
    a2_set_regs(st[0] | (st[1] << 8), st[2], st[3], st[4], st[5]);
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
        else if (c == '\r') {
            if (i + 1 >= size || temp[i + 1] != '\n')
                playback_buf[playback_len++] = '\r';
        } else playback_buf[playback_len++] = c;
    }
    playback_pos = 0;
    return true;
}

static bool load_program(void) {
    return load_program_from_path("/apple2/programs/program.txt");
}

/* ============================================================
   CC character lookup
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
            rb->snprintf(out, sizeof(out), "%s - %s", table[i].band, table[i].name);
            rb->splash(HZ * 3, out);
            return;
        }
    }
    rb->splash(HZ * 2, "UNKNOWN ABBR");
}

/* ============================================================
   PLAY handler: short commands first, else replay the line
   ============================================================ */
static void commit_line(void) {
    input_buf[input_len] = '\0';

    if (input_len == 1 && input_buf[0] == 'S') {
        if (save_state()) rb->splash(HZ, "SAVED");
        else              rb->splash(HZ, "SAVE FAIL");
        input_len = 0;
        return;
    }
    if (input_len == 1 && input_buf[0] == 'R') {
        if (restore_state()) rb->splash(HZ, "RESTORED");
        else                 rb->splash(HZ, "NO STATE");
        input_len = 0;
        return;
    }
    if (input_len == 1 && input_buf[0] == 'P') {
        if (load_program()) rb->splash(HZ, "LOADING");
        else                rb->splash(HZ, "NO FILE");
        input_len = 0;
        return;
    }
    if (input_len > 2 && input_buf[0] == 'P' && input_buf[1] == ' ') {
        int pos = 1;
        while (pos < input_len && input_buf[pos] == ' ') pos++;
        if (pos < input_len) {
            char path[128];
            rb->snprintf(path, sizeof(path), "/apple2/programs/%s", input_buf + pos);
            if (load_program_from_path(path)) rb->splash(HZ, "LOADING");
            else                              rb->splash(HZ, "NO FILE");
        } else {
            load_program();
        }
        input_len = 0;
        return;
    }
    if (input_len == 2 && input_buf[0] == 'L' && input_buf[1] == 'P') {
        input_len = 0;
        DIR *dir = rb->opendir("/apple2/programs");
        if (dir) {
            struct dirent *entry;
            rb->splash(HZ * 2, "LISTING TO LOG");
            while ((entry = rb->readdir(dir)) != NULL) {
                if (entry->d_name[0] == '.') continue;
                LOG("PROG: %s", entry->d_name);
            }
            rb->closedir(dir);
        } else {
            rb->splash(HZ * 2, "NO PROG DIR");
        }
        return;
    }
    if (input_len == 1 && input_buf[0] == 'A') {
        rb->splash(HZ * 3, "Apple II for Rockbox");
        input_len = 0;
        return;
    }
    if (input_len == 1 && input_buf[0] == 'H') {
        rb->splash(HZ * 3, "S/R/P/LP/A/H/RST/EX/B/CC");
        input_len = 0;
        return;
    }
    if (input_len == 3 && input_buf[0] == 'R' && input_buf[1] == 'S' && input_buf[2] == 'T') {
        a2_reset();
        input_len = 0;
        playback_pos = playback_len = 0;
        rb->splash(HZ, "RESET");
        return;
    }
    if (input_len == 1 && input_buf[0] == 'B') {
        rb->splash(HZ * 2, "Yes! BanG Dream!");
        input_len = 0;
        return;
    }
    if (input_len >= 2 && input_buf[0] == 'C' && input_buf[1] == 'C') {
        int i = 2;
        while (i < input_len && input_buf[i] == ' ') i++;
        cc_lookup(input_buf + i);
        input_len = 0;
        return;
    }

    /* not a command: replay whole line to the Apple II */
    if (input_len > 0) {
        rb->memcpy(playback_buf, input_buf, input_len);
        playback_buf[input_len] = '\r';
        playback_len = input_len + 1;
        playback_pos = 0;
        input_len = 0;
    } else {
        playback_buf[0] = '\r';
        playback_len = 1;
        playback_pos = 0;
    }
}

/* ============================================================
   UI
   ============================================================ */
static void draw_status(void) {
    char buf[96];
    rb->lcd_set_foreground(COLOR_STATUS);
    rb->snprintf(buf, sizeof(buf), "PC:%04X >%.60s", a2_get_pc(), input_buf);
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

    LOG("=== Apple II START ===");

    if (!load_rom()) {
        rb->splash(HZ * 2, "ROM not found");
        return PLUGIN_ERROR;
    }

    a2_reset();
    input_len = 0;
    playback_len = playback_pos = 0;

    int btn;
    while (1) {
        /* Feed one char into the Apple II keyboard latch.
           $C000 returns a2_kbd_key, $C010 clears it, so only
           push a new char when the latch is free. */
        if (playback_pos < playback_len && a2_kbd_key == 0) {
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
                LOG("=== EXIT ===");
                log_flush();
#ifdef HAVE_WHEEL_POSITION
                rb->wheel_send_events(true);
#endif
                return PLUGIN_OK;
            }

            if (btn == BUTTON_PLAY) {
                if (playback_pos < playback_len) {
                    /* replay in progress, ignore */
                } else {
                    input_buf[input_len] = '\0';
                    if (input_len == 2 && input_buf[0] == 'E' && input_buf[1] == 'X') {
                        input_len = 0;
                        log_flush();
#ifdef HAVE_WHEEL_POSITION
                        rb->wheel_send_events(true);
#endif
                        return PLUGIN_OK;
                    }
                    commit_line();
                }
            } else if (btn == BUTTON_SELECT) {
                if (playback_pos < playback_len) {
                    /* ignore during replay */
                } else {
                    int r = kb_index / KEY_COLS;
                    int c = kb_index % KEY_COLS;
                    if (kb_cell_page(r, c)) {
                        kb_page = (kb_page + 1) % KB_PAGES;
                    } else if (kb_cell_space(r, c)) {
                        if (input_len < (int)sizeof(input_buf) - 1)
                            input_buf[input_len++] = ' ';
                    } else {
                        char ch = kb_get(r, c);
                        if (ch && input_len < (int)sizeof(input_buf) - 1)
                            input_buf[input_len++] = ch;
                    }
                }
            } else if (btn == BUTTON_LEFT) {
                if (input_len > 0) input_len--;
            } else if (btn == BUTTON_RIGHT) {
                if (playback_pos < playback_len) {
                    playback_pos = playback_len = 0;
                } else {
                    input_len = 0;
                    a2_reset();
                }
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
