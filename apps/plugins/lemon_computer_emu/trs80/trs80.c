#include <stdint.h>
#include <stdarg.h>
#include "plugin.h"
#include "../core/logger.h"
#include "t80core.h"
#include "t80video.h"

/* ============================================================
   TRS-80 Model I / III (Level II) emulator for Rockbox (UI layer)

   Layout (320x240), identical skeleton to apple2:
     0   - 12   status bar (1 line, 10-Fixed)
     12  - 204  TRS-80 video (320x192, 64x16 chars, 5x12 each)
     204 - 240  virtual keyboard (3 rows x 20 cols, 10-Fixed)

   Keyboard: virtual keys (including shifted symbols such as ! " ( )
   ) are turned straight into matrix presses during replay.  The
   TRS-80 keyboard is memory-mapped at $3800-$3BFF and is scanned by
   the ROM driver; while a key is held its row bit reads 1 ("pressed
   = set", matching the real hardware).  PLAY submits the typed text,
   LEFT backspaces, RIGHT resets, MENU exits.
   ============================================================ */


/* ---------------- logging ----------------
   The plugin buffer is reserved for the video framebuffer (which needs
   ~122KB), so the log uses only its small static fallback buffer. */
#define TRS80_LOG_PATH "/lemon_computer_emu/logs/tandy/trs80/trs80.log"
#define TRS80_LOG_CAP  (64 * 1024)

static char trs80_log_fallback[4096];
static logger_t trs80_log;

static void log_init(void) {
    logger_init(&trs80_log, trs80_log_fallback, sizeof(trs80_log_fallback),
                TRS80_LOG_PATH);
}

static void trs80_log_msg(const char* fmt, ...) {
    char tmp[256];
    va_list args;

    if (!trs80_log.enabled)
        return;

    va_start(args, fmt);
    rb->vsnprintf(tmp, sizeof(tmp), fmt, args);
    va_end(args);

    logger_log(&trs80_log, "%s\n", tmp);
}

#define LOG(fmt, ...) trs80_log_msg(fmt, ##__VA_ARGS__)

/* ============================================================
   Load ROMs (paths unchanged)
   ============================================================ */
#define ROM_A_PATH  "/lemon_computer_emu/roms/tandy/trs80/rom_a_L2_v13.bin"
#define ROM_B_PATH  "/lemon_computer_emu/roms/tandy/trs80/rom_b_L2_v13.bin"
#define SAVE_PATH   "/lemon_computer_emu/states/tandy/trs80/state.bin"
#define PROG_DIR    "/lemon_computer_emu/programs/tandy/trs80"
#define PROG_DEFAULT "/lemon_computer_emu/programs/tandy/trs80/TRS80_PROGRAM.txt"

/* claim the video framebuffer from the single contiguous plugin buffer.
   Must be called once, before load_roms. */
static bool t80_fb_alloc(void) {
    size_t avail = 0;
    uint16_t* fb = (uint16_t*)rb->plugin_get_buffer(&avail);
    if (!fb || avail < (size_t)(T80_FB_W * T80_FB_H * sizeof(uint16_t)))
        return false;
    t80_video_init(fb);
    return true;
}

static bool load_roms(void) {
    static uint8_t roma[0x2000];
    static uint8_t romb[0x1000];

    int fd = rb->open(ROM_A_PATH, O_RDONLY);
    if (fd < 0) { LOG("ROM A open failed"); return false; }
    off_t s = rb->ffilesize(fd);
    if (s < (off_t)sizeof(roma)) {
        LOG("ROM A too small: %d", (int)s);
        rb->close(fd);
        return false;
    }
    if (rb->read(fd, roma, sizeof(roma)) != (ssize_t)sizeof(roma)) {
        rb->close(fd);
        return false;
    }
    rb->close(fd);

    fd = rb->open(ROM_B_PATH, O_RDONLY);
    if (fd < 0) { LOG("ROM B open failed"); return false; }
    s = rb->ffilesize(fd);
    if (s < (off_t)sizeof(romb)) {
        LOG("ROM B too small: %d", (int)s);
        rb->close(fd);
        return false;
    }
    if (rb->read(fd, romb, sizeof(romb)) != (ssize_t)sizeof(romb)) {
        rb->close(fd);
        return false;
    }
    rb->close(fd);

    trs80_load_rom_a(roma, sizeof(roma));
    trs80_load_rom_b(romb, sizeof(romb));
    LOG("TRS-80 Level II v1.3 ROMs loaded");
    return true;
}

/* ============================================================
   Screen layout (320x240) - mirrors apple2
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
   Font (Rockbox system font for status + virtual keyboard,
   exactly as apple2; the video uses t80_fb + lcd_bitmap_part)
   ============================================================ */
static struct font* fixed_font = NULL;

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
   Virtual keyboard (3 pages, mirrors apple2)
   ============================================================ */
static int kb_page = 0;
static int kb_index = 0;

static const char kb_page_main[KEY_ROWS][KEY_COLS + 1] = {
    "ABCDEFGHIJKLMNOPQRST",
    "UVWXYZ 0123456789.,:",
    "@()*+<=>?_!\"#$%&'/;P"
};
static const char kb_page_digit[KEY_ROWS][KEY_COLS + 1] = {
    "0123456789+-*/=<>  ",
    ".,;:!?@_\"#$%&'()  ",
    "                   P"
};
static const char kb_page_sym[KEY_ROWS][KEY_COLS + 1] = {
    "!\"#$%&'()*+<=>?@ , ",
    "0123456789-./:;_=?  ",
    "                   P"
};

static const char (*kb_layouts[KB_PAGES])[KEY_COLS + 1] = {
    kb_page_main, kb_page_digit, kb_page_sym
};

static char kb_get(int r, int c) { return kb_layouts[kb_page][r][c]; }
static int  kb_cell_page(int r, int c) { return (r == KEY_ROWS - 1 && c == KEY_COLS - 1); }

/* ============================================================
   Char -> matrix (row/bit/shift), using the real TRS-80 Model I
   keyboard shift layout.  See t80core.h for the matrix rows.
   ============================================================ */
static bool char_to_matrix_ex(char ch, int* row, int* bit, bool* shift) {
    *shift = false;
    if (ch == '\r' || ch == '\n') { *row = 6; *bit = 0; return true; }  /* ENTER */
    if (ch == ' ')                { *row = 6; *bit = 7; return true; }  /* SPACE */

    /* shifted digits: 0..9 -> _ ! " # $ % & ' ( )  (underscore key for 0) */
    static const char shifted_digits[] = "_!\"#$%&'";
    for (int i = 0; i < 8; i++) {
        if (ch == shifted_digits[i]) { *row = 4; *bit = i; *shift = true; return true; }
    }
    if (ch == '(') { *row = 5; *bit = 0; *shift = true; return true; }  /* 8 key */
    if (ch == ')') { *row = 5; *bit = 1; *shift = true; return true; }  /* 9 key */
    if (ch == '*') { *row = 5; *bit = 2; *shift = true; return true; }  /* ':' key */
    if (ch == '+') { *row = 5; *bit = 3; *shift = true; return true; }  /* ';' key */
    if (ch == '<') { *row = 5; *bit = 4; *shift = true; return true; }  /* ',' key */
    if (ch == '=') { *row = 5; *bit = 5; *shift = true; return true; }  /* '-' key */
    if (ch == '>') { *row = 5; *bit = 6; *shift = true; return true; }  /* '.' key */
    if (ch == '?') { *row = 5; *bit = 7; *shift = true; return true; }  /* '/' key */

    /* ordinary keys: @ A-Z 0-9 : ; , - . / */
    static const char row0[] = "@ABCDEFG";
    static const char row1[] = "HIJKLMNO";
    static const char row2[] = "PQRSTUVW";
    static const char row3[] = "XYZ";
    static const char row4[] = "01234567";
    static const char row5[] = "89:;,-./";
    for (int i = 1; i <= 7; i++) if (ch == row0[i]) { *row = 0; *bit = i; return true; }
    if (ch == '@') { *row = 0; *bit = 0; return true; }
    for (int i = 0; i < 8; i++) if (ch == row1[i]) { *row = 1; *bit = i; return true; }
    for (int i = 0; i < 8; i++) if (ch == row2[i]) { *row = 2; *bit = i; return true; }
    for (int i = 0; i < 3; i++) if (ch == row3[i]) { *row = 3; *bit = i; return true; }
    for (int i = 0; i < 8; i++) if (ch == row4[i]) { *row = 4; *bit = i; return true; }
    for (int i = 0; i < 8; i++) if (ch == row5[i]) { *row = 5; *bit = i; return true; }

    return false;
}

/* ============================================================
   input_buf + replay (matrix pokes)
   ============================================================ */
static char input_buf[128];
static int  input_len = 0;

static char replay_buf[4096];
static int  replay_len = 0;
static int  replay_pos = -1;
static int  replay_frames = 0;

static char lp_lines[2][64];
static int  lp_timer = 0;

/* Rebuild the keyboard matrix from the current hold state.
   NOTE: rows idle at 0x00; a held key sets its bit (matches real HW). */
static void rebuild_keyboard(void) {
    for (int i = 0; i < 8; i++) trs_kb_row[i] = 0x00;

    if (replay_pos >= 0 && replay_pos < replay_len && replay_frames > 0) {
        int r, b;
        bool sh;
        if (char_to_matrix_ex(replay_buf[replay_pos], &r, &b, &sh)) {
            trs_kb_row[r] |= (uint8_t)(1 << b);
            if (sh) trs_kb_row[7] |= (uint8_t)(1 << 0);
        }
    }
}

static void start_replay(void) {
    if (replay_len == 0) return;
    replay_pos = 0;
    replay_frames = 2;
    rebuild_keyboard();
}

static void step_replay(void) {
    if (replay_pos < 0) return;
    if (replay_frames > 0) {
        replay_frames--;
        if (replay_frames == 0) {
            /* release current char, advance */
            for (int i = 0; i < 8; i++) trs_kb_row[i] = 0x00;
            replay_pos++;
            if (replay_pos >= replay_len) {
                replay_len = 0;
                replay_pos = -1;
            } else {
                replay_frames = 2;
                rebuild_keyboard();
            }
        }
    }
}

static bool load_program_from_path(const char* path) {
    int fd = rb->open(path, O_RDONLY);
    if (fd < 0) return false;
    size_t size = rb->ffilesize(fd);
    if (size == 0) { rb->close(fd); return false; }
    if (size > sizeof(replay_buf) - 1) size = sizeof(replay_buf) - 1;
    size_t rd = rb->read(fd, replay_buf, size);
    rb->close(fd);
    replay_len = (int)rd;
    start_replay();
    return true;
}

/* ============================================================
   Save / Restore / LP (format: 64K RAM + raw Z80 block,
   reusing the existing on-disk format for compatibility)
   ============================================================ */
static bool save_state(void) {
    rb->mkdir("/lemon_computer_emu");
    rb->mkdir("/lemon_computer_emu/states");
    rb->mkdir("/lemon_computer_emu/states/tandy");
    rb->mkdir("/lemon_computer_emu/states/tandy/trs80");
    int fd = rb->open(SAVE_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) return false;
    rb->write(fd, trs_mem, 65536);
    Z80 tmp;
    trs80_save_cpu(&tmp);
    rb->write(fd, &tmp, sizeof(Z80));
    rb->close(fd);
    return true;
}

static bool restore_state(void) {
    int fd = rb->open(SAVE_PATH, O_RDONLY);
    if (fd < 0) return false;
    size_t sz = rb->ffilesize(fd);
    if (sz < 65536 + sizeof(Z80)) { rb->close(fd); return false; }
    rb->read(fd, trs_mem, 65536);
    Z80 tmp;
    rb->read(fd, &tmp, sizeof(Z80));
    rb->close(fd);
    trs80_restore_cpu(&tmp);
    return true;
}

static void list_programs(void) {
    lp_lines[0][0] = 0;
    lp_lines[1][0] = 0;
    lp_timer = 180;

    DIR* dir = rb->opendir(PROG_DIR);
    if (!dir) {
        rb->snprintf(lp_lines[0], sizeof(lp_lines[0]), "(no program dir)");
        return;
    }
    int idx = 0;
    struct dirent* e;
    while ((e = rb->readdir(dir)) != NULL && idx < 8) {
        if (e->d_name[0] == '.') continue;
        int nlen = rb->strlen(e->d_name);
        if (nlen < 5) continue;
        if (rb->strcasecmp(e->d_name + nlen - 4, ".txt") != 0) continue;
        int line = idx / 4;
        int cur = rb->strlen(lp_lines[line]);
        int room = (int)sizeof(lp_lines[line]) - cur;
        if (room > 1) {
            if (cur > 0)
                rb->snprintf(lp_lines[line] + cur, (size_t)room, " %s", e->d_name);
            else
                rb->snprintf(lp_lines[line], sizeof(lp_lines[line]), "%s", e->d_name);
        }
        idx++;
    }
    rb->closedir(dir);
    if (idx == 0)
        rb->snprintf(lp_lines[0], sizeof(lp_lines[0]), "(no .txt programs)");
}

/* ============================================================
   Commands (same set as apple2)
   ============================================================ */
static void warm_reset(void);

static void cc_lookup(const char* abbr) {
    struct cc_entry { const char* abbr; const char* band; const char* name; };
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

/* Handle PLAY / short commands. Returns true when the caller should
   exit the emulator (EX command). */
static bool handle_play(void) {
    if (input_len == 1 && input_buf[0] == 'S') {
        if (save_state()) rb->splash(HZ, "SAVED");
        else              rb->splash(HZ, "SAVE FAIL");
        input_len = 0; input_buf[0] = 0;
        return false;
    }
    if (input_len == 1 && input_buf[0] == 'R') {
        if (restore_state()) rb->splash(HZ, "RESTORED");
        else                 rb->splash(HZ, "NO STATE");
        input_len = 0; input_buf[0] = 0;
        return false;
    }
    if (input_len == 1 && input_buf[0] == 'P') {
        if (load_program_from_path(PROG_DEFAULT)) rb->splash(HZ, "LOADING");
        else                                      rb->splash(HZ, "NO FILE");
        input_len = 0; input_buf[0] = 0;
        return false;
    }
    if (input_len > 2 && input_buf[0] == 'P' && input_buf[1] == ' ') {
        char path[128];
        rb->snprintf(path, sizeof(path), "%s/%s", PROG_DIR, input_buf + 2);
        bool ok = load_program_from_path(path);
        if (ok) rb->splash(HZ, "LOADING");
        else    rb->splash(HZ, "NO FILE");
        input_len = 0; input_buf[0] = 0;
        return false;
    }
    if (input_len == 2 && input_buf[0] == 'L' && input_buf[1] == 'P') {
        list_programs();
        input_len = 0; input_buf[0] = 0;
        return false;
    }
    if (input_len == 1 && input_buf[0] == 'A') {
        rb->splash(HZ * 3, "TRS-80 for Rockbox");
        input_len = 0; input_buf[0] = 0;
        return false;
    }
    if (input_len == 1 && input_buf[0] == 'H') {
        rb->splash(HZ * 3, "S R P LP A H RST EX B CC");
        input_len = 0; input_buf[0] = 0;
        return false;
    }
    if (input_len == 3 && input_buf[0] == 'R' && input_buf[1] == 'S'
        && input_buf[2] == 'T') {
        warm_reset();
        replay_pos = replay_len = 0;
        rb->splash(HZ, "RESET");
        input_len = 0; input_buf[0] = 0;
        return false;
    }
    if (input_len == 2 && input_buf[0] == 'E' && input_buf[1] == 'X') {
        rb->splash(HZ * 3, "EXITING...");
        input_len = 0; input_buf[0] = 0;
        return true;
    }
    if (input_len == 1 && input_buf[0] == 'B') {
        rb->splash(HZ * 2, "Yes! BanG Dream!");
        input_len = 0; input_buf[0] = 0;
        return false;
    }
    if (input_len >= 2 && input_buf[0] == 'C' && input_buf[1] == 'C') {
        int i = 2;
        while (i < input_len && input_buf[i] == ' ') i++;
        char abbr[32];
        int j = 0;
        while (i < input_len && j < 31) abbr[j++] = input_buf[i++];
        abbr[j] = 0;
        cc_lookup(abbr);
        input_len = 0; input_buf[0] = 0;
        return false;
    }
    return false;
}

/* submit input to the TRS-80.  Returns true when the caller should
   exit the emulator (EX command handled). */
static bool submit_input(void) {
    if (handle_play())
        return true; /* caller will exit */
    if (replay_pos >= 0) return false;  /* replay active */

    int n = 0;
    if (input_len == 0) {
        replay_buf[n++] = '\r';
    } else {
        for (int i = 0; i < input_len && n < (int)sizeof(replay_buf) - 1; i++)
            replay_buf[n++] = input_buf[i];
        replay_buf[n++] = '\r';
    }
    replay_len = n;
    input_len = 0;
    input_buf[0] = 0;
    start_replay();
    return false;
}

/* ============================================================
   UI
   ============================================================ */
/* warm reset: clear usable RAM (0x3000-0xFFFF) and re-hook the CPU */
static void warm_reset(void) {
    for (int i = 0x3000; i < 0x10000; i++) trs_mem[i] = 0;
    trs80_reset();
}

static void draw_status(void) {
    char buf[64];
    rb->lcd_set_foreground(COLOR_STATUS);
    rb->snprintf(buf, sizeof(buf), "TRS-80 PC:%04X A:%02X F:%02X",
                 trs80_get_pc(), trs80_get_a(), trs80_get_f());
    rb->lcd_puts(0, 0, buf);
}

static void draw_keyboard(void) {
    int r, c;
    for (r = 0; r < KEY_ROWS; r++) {
        int ly = KEY_TOP + r * KEY_H;
        for (c = 0; c < KEY_COLS; c++) {
            char buf2[4];
            int lx = c * (VIDEO_W / KEY_COLS);
            int sel = (r * KEY_COLS + c == kb_index);
            if (kb_cell_page(r, c)) {
                buf2[0] = 'P'; buf2[1] = 'G'; buf2[2] = '\0';
            } else {
                char ch = kb_get(r, c);
                if (ch == ' ') {
                    buf2[0] = 'S'; buf2[1] = 'P'; buf2[2] = '\0';
                } else {
                    buf2[0] = ch ? ch : ' ';
                    buf2[1] = '\0';
                }
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
    t80_render_video();
    rb->lcd_bitmap_part((fb_data*)t80_fb, 0, 0, VIDEO_W, 0, VIDEO_TOP,
                        VIDEO_W, VIDEO_H);
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

enum plugin_status trs80_run(const void* parameter) {
    (void)parameter;
    log_init();

#ifdef HAVE_WHEEL_POSITION
    rb->wheel_send_events(false);
#endif

    LOG("=== TRS-80 START ===");

    if (!init_font()) {
        logger_flush(&trs80_log);
        return PLUGIN_ERROR;
    }

    /* allocate the video framebuffer from the plugin buffer.
       PLUGIN_BUFFER is a single contiguous area that must be claimed up
       front, so the log above intentionally uses its static fallback. */
    if (!t80_fb_alloc()) {
        rb->splash(HZ * 2, "Not enough buffer");
        logger_flush(&trs80_log);
        return PLUGIN_ERROR;
    }

    if (!load_roms()) {
        rb->splash(HZ * 2, "TRS-80 ROMs not found");
        logger_flush(&trs80_log);
        return PLUGIN_ERROR;
    }

    trs80_init(&trs80_log);
    warm_reset();
    kb_page = 0;
    kb_index = 0;
    input_len = 0; input_buf[0] = 0;
    replay_len = 0; replay_pos = -1; replay_frames = 0;
    lp_timer = 0;

    int btn;
    while (1) {
        for (int i = 0; i < 3000; i++) {
            if (!trs80_step()) break;
        }

        step_replay();
        if (lp_timer > 0) lp_timer--;

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
                LOG("=== TRS-80 EXIT ===");
                logger_flush(&trs80_log);
#ifdef HAVE_WHEEL_POSITION
                rb->wheel_send_events(true);
#endif
                return PLUGIN_OK;
            }
            if (btn == BUTTON_PLAY) {
                if (replay_pos < 0) {
                    if (submit_input()) {
                        logger_flush(&trs80_log);
#ifdef HAVE_WHEEL_POSITION
                        rb->wheel_send_events(true);
#endif
                        return PLUGIN_OK;
                    }
                }
            } else if (btn == BUTTON_SELECT) {
                int r = kb_index / KEY_COLS;
                int c = kb_index % KEY_COLS;
                if (kb_cell_page(r, c)) {
                    kb_page = (kb_page + 1) % KB_PAGES;
                } else {
                    char ch = kb_get(r, c);
                    if (ch && input_len < (int)sizeof(input_buf) - 1) {
                        input_buf[input_len++] = ch;
                        input_buf[input_len] = '\0';
                    }
                }
            } else if (btn == BUTTON_LEFT) {
                if (replay_pos < 0 && input_len > 0)
                    input_buf[--input_len] = '\0';
            } else if (btn == BUTTON_RIGHT) {
                warm_reset();
                input_len = 0; input_buf[0] = 0;
                replay_len = 0; replay_pos = -1; replay_frames = 0;
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
