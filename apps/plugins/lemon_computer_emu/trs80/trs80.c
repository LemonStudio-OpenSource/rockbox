#include <stdint.h>
#include <stdarg.h>
#include "plugin.h"
#include "../core/logger.h"
#include "../core/zilogz80.h"

#define TRS80_LOG_PATH  "/lemon_computer_emu/logs/tandy/trs80/trs80.log"
#define TRS80_LOG_CAP   (64 * 1024)
#define ROM_A_PATH      "/lemon_computer_emu/roms/tandy/trs80/rom_a_L2_v13.bin"
#define ROM_B_PATH      "/lemon_computer_emu/roms/tandy/trs80/rom_b_L2_v13.bin"
#define ROM_A_SIZE      0x2000
#define ROM_B_SIZE      0x1000
#define SAVE_PATH       "/lemon_computer_emu/states/tandy/trs80/state.bin"
#define PROG_DIR        "/lemon_computer_emu/programs/tandy/trs80"
#define PROG_DEFAULT    "/lemon_computer_emu/programs/tandy/trs80/TRS80_PROGRAM.txt"

#define VIDEO_TOP 12
#define VIDEO_ROWS 16
#define VIDEO_COLS 64
#define VIDEO_ROW_H 11
#define INPUT_Y 177
#define KEY_TOP 188
#define KEY_ROWS 4
#define KEY_COLS 12
#define KEY_CELL_H 13

#define COLOR_BG 0x0000
#define COLOR_TEXT 0xFFFF
#define COLOR_STATUS 0xFFFF
#define COLOR_KEY_SEL_BG 0xFFFF
#define COLOR_KEY_SEL_FG 0x0000

/* ---------------- logging ---------------- */
static char trs80_log_fallback[4096];
static logger_t trs80_log;

static void log_init(void) {
    size_t avail = 0;
    char* buf = rb->plugin_get_buffer(&avail);
    size_t cap;
    if (!buf || avail < 4096) {
        buf = trs80_log_fallback;
        cap = sizeof(trs80_log_fallback);
    }
    else {
        cap = (avail < TRS80_LOG_CAP) ? avail : TRS80_LOG_CAP;
    }
    logger_init(&trs80_log, buf, cap, TRS80_LOG_PATH);
}

static void trs80_log_msg(const char* fmt, ...) {
    char tmp[256];
    va_list args;
    if (!trs80_log.enabled) return;
    va_start(args, fmt);
    rb->vsnprintf(tmp, sizeof(tmp), fmt, args);
    va_end(args);
    logger_log(&trs80_log, "%s\n", tmp);
}

#define LOG(fmt, ...) trs80_log_msg(fmt, ##__VA_ARGS__)

/* ============================================================
   Memory + Z80
   ============================================================ */
static uint8_t mem[65536];
static Z80 cpu;
static uint8_t kb_row[8];

static uint8_t trs80_read(void* ctx, uint16_t addr) {
    (void)ctx;
    /* 键盘映射：读地址本身决定返回哪一行 */
    if (addr >= 0x3801 && addr <= 0x3880) {
        if (addr & 0x01) return kb_row[0];
        if (addr & 0x02) return kb_row[1];
        if (addr & 0x04) return kb_row[2];
        if (addr & 0x08) return kb_row[3];
        if (addr & 0x10) return kb_row[4];
        if (addr & 0x20) return kb_row[5];
        if (addr & 0x40) return kb_row[6];
        if (addr & 0x80) return kb_row[7];
    }
    return mem[addr];
}

static void trs80_write(void* ctx, uint16_t addr, uint8_t val) {
    (void)ctx;

    /* 键盘区：写入忽略 */
    if (addr >= 0x3800 && addr <= 0x3880) {
        if (addr & 0x01) return kb_row[0];
        if (addr & 0x02) return kb_row[1];
        if (addr & 0x04) return kb_row[2];
        if (addr & 0x08) return kb_row[3];
        if (addr & 0x10) return kb_row[4];
        if (addr & 0x20) return kb_row[5];
        if (addr & 0x40) return kb_row[6];
        if (addr & 0x80) return kb_row[7];
        return kb_row[0];  /* 0x3800 本身，返回第0行 */
    }

    /* 视频RAM 0x3C00–0x3FFF：必须允许写入 */
    if (addr >= 0x3C00 && addr <= 0x3FFF) {
        mem[addr] = val;
        return;
    }

    /* 其他 >= 0x4000 的RAM区域 */
    if (addr >= 0x4000)
        mem[addr] = val;
}

static uint8_t trs80_in(void* ctx, uint16_t port) { (void)ctx; (void)port; return 0xFF; }
static void trs80_out(void* ctx, uint16_t port, uint8_t val) { (void)ctx; (void)port; (void)val; }

/* ============================================================
   ROM loading
   ============================================================ */
static bool load_roms(void) {
    int fd = rb->open(ROM_A_PATH, O_RDONLY);
    if (fd < 0) { LOG("ROM A open failed"); return false; }
    off_t s = rb->ffilesize(fd);
    if (s < ROM_A_SIZE) {
        LOG("ROM A too small: %d", (int)s);
        rb->close(fd);
        return false;
    }
    if (rb->read(fd, mem, ROM_A_SIZE) != ROM_A_SIZE) { rb->close(fd); return false; }
    rb->close(fd);

    fd = rb->open(ROM_B_PATH, O_RDONLY);
    if (fd < 0) { LOG("ROM B open failed"); return false; }
    s = rb->ffilesize(fd);
    if (s < ROM_B_SIZE) {
        LOG("ROM B too small: %d", (int)s);
        rb->close(fd);
        return false;
    }
    if (rb->read(fd, mem + 0x2000, ROM_B_SIZE) != ROM_B_SIZE) { rb->close(fd); return false; }
    rb->close(fd);

    LOG("TRS-80 Level II v1.3 ROMs loaded");
    return true;
}

/* ============================================================
   Font + video
   ============================================================ */
static int char_w = 6, char_h = 10;
static int visible_cols = 53;

static bool init_font(void) {
    /* FONT_SYSFIXED 是系统内置固定宽度字体，不会被其他插件改掉 */
    rb->lcd_setfont(FONT_SYSFIXED);
    int w = 0, h = 0;
    rb->lcd_getstringsize("M", &w, &h);
    if (w < 1) w = 6;
    if (h < 1) h = 10;
    char_w = w;
    char_h = h;
    visible_cols = 320 / char_w;
    if (visible_cols > VIDEO_COLS) visible_cols = VIDEO_COLS;
    LOG("font %dx%d, visible_cols=%d", char_w, char_h, visible_cols);
    return true;
}

static void render_text(void) {
    rb->lcd_set_foreground(COLOR_TEXT);
    rb->lcd_set_background(COLOR_BG);
    for (int row = 0; row < VIDEO_ROWS; row++) {
        int y = VIDEO_TOP + row * VIDEO_ROW_H;
        for (int col = 0; col < visible_cols; col++) {
            uint8_t ch = mem[0x3C00 + row * 64 + col];
            if (ch < 32 || ch > 126) ch = ' ';
            char str[2] = { (char)ch, 0 };
            rb->lcd_putsxy(col * char_w, y, str);
        }
    }
}

/* ============================================================
   Virtual keyboard
   ============================================================ */
struct key_def { uint8_t row, bit; const char* label; };

static const struct key_def key_map[45] = {
    { 0,1,"A" },{ 0,2,"B" },{ 0,3,"C" },{ 0,4,"D" },{ 0,5,"E" },{ 0,6,"F" },
    { 0,7,"G" },{ 1,0,"H" },{ 1,1,"I" },{ 1,2,"J" },{ 1,3,"K" },{ 1,4,"L" },
    { 1,5,"M" },{ 1,6,"N" },{ 1,7,"O" },{ 2,0,"P" },{ 2,1,"Q" },{ 2,2,"R" },
    { 2,3,"S" },{ 2,4,"T" },{ 2,5,"U" },{ 2,6,"V" },{ 2,7,"W" },{ 3,0,"X" },
    { 3,1,"Y" },{ 3,2,"Z" },{ 4,0,"0" },{ 4,1,"1" },{ 4,2,"2" },{ 4,3,"3" },
    { 4,4,"4" },{ 4,5,"5" },{ 4,6,"6" },{ 4,7,"7" },{ 5,0,"8" },{ 5,1,"9" },
    { 0,0,"@" },{ 5,2,":" },{ 5,3,";" },{ 5,4,"," },{ 5,5,"-" },{ 5,6,"." },
    { 5,7,"/" },{ 6,7,"SP" },{ 7,0,"SH" },
};

#define KEY_COUNT ((int)(sizeof(key_map)/sizeof(key_map[0])))

static int kb_index = 0;
static bool shift_pending = false;

static bool is_shift_key(int idx) {
    return key_map[idx].row == 7 && key_map[idx].bit == 0;
}

static bool char_to_matrix_ex(char ch, int* row, int* bit, bool* shift) {
    *shift = false;
    if (ch == '\r' || ch == '\n') { *row = 6; *bit = 0; return true; }
    if (ch == ' ') { *row = 6; *bit = 7; return true; }

    static const char shifted_digits[] = "_!\"#$%&'";
    for (int i = 0; i < 8; i++) {
        if (ch == shifted_digits[i]) {
            *row = 4; *bit = i; *shift = true;
            return true;
        }
    }
    if (ch == '(') { *row = 5; *bit = 0; *shift = true; return true; }
    if (ch == ')') { *row = 5; *bit = 1; *shift = true; return true; }
    if (ch == '*') { *row = 5; *bit = 2; *shift = true; return true; }
    if (ch == '+') { *row = 5; *bit = 3; *shift = true; return true; }
    if (ch == '<') { *row = 5; *bit = 4; *shift = true; return true; }
    if (ch == '=') { *row = 5; *bit = 5; *shift = true; return true; }
    if (ch == '>') { *row = 5; *bit = 6; *shift = true; return true; }
    if (ch == '?') { *row = 5; *bit = 7; *shift = true; return true; }

    for (int i = 0; i < KEY_COUNT; i++) {
        if (key_map[i].label[0] == ch && key_map[i].label[1] == 0) {
            *row = key_map[i].row;
            *bit = key_map[i].bit;
            return true;
        }
    }
    return false;
}

/* ============================================================
   input_buf + replay
   ============================================================ */
static char input_buf[128];
static int input_len = 0;

static char replay_buf[4096];
static int replay_len = 0;
static int replay_pos = -1;
static int replay_frames = 0;

static char lp_lines[2][64];
static int lp_timer = 0;

/* 当前键盘状态（由 rebuild_keyboard 重建，不在主循环里乱清） */
static void rebuild_keyboard(void) {
    for (int i = 0; i < 8; i++) kb_row[i] = 0xFF;

    if (shift_pending) kb_row[7] &= ~(1 << 0);

    /* 回放：按当前字符 */
    if (replay_pos >= 0 && replay_pos < replay_len && replay_frames > 0) {
        int r, b;
        bool sh;
        if (char_to_matrix_ex(replay_buf[replay_pos], &r, &b, &sh)) {
            kb_row[r] &= ~(1 << b);
            if (sh) kb_row[7] &= ~(1 << 0);
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
            /* 释放当前字符，前进到下一个 */
            for (int i = 0; i < 8; i++) kb_row[i] = 0xFF;
            replay_pos++;
            if (replay_pos >= replay_len) {
                replay_len = 0;
                replay_pos = -1;
            }
            else {
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
   Save / Restore / LP
   ============================================================ */
static bool save_state(void) {
    rb->mkdir("/lemon_computer_emu");
    rb->mkdir("/lemon_computer_emu/states");
    rb->mkdir("/lemon_computer_emu/states/tandy");
    rb->mkdir("/lemon_computer_emu/states/tandy/trs80");
    int fd = rb->open(SAVE_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) return false;
    rb->write(fd, mem, 65536);
    Z80 tmp = cpu;
    tmp.mem_read = NULL; tmp.mem_write = NULL;
    tmp.io_read = NULL; tmp.io_write = NULL;
    tmp.ctx = NULL;
    rb->write(fd, &tmp, sizeof(Z80));
    rb->close(fd);
    return true;
}

static bool restore_state(void) {
    int fd = rb->open(SAVE_PATH, O_RDONLY);
    if (fd < 0) return false;
    size_t sz = rb->ffilesize(fd);
    if (sz < 65536 + sizeof(Z80)) { rb->close(fd); return false; }
    rb->read(fd, mem, 65536);
    Z80 tmp;
    rb->read(fd, &tmp, sizeof(Z80));
    rb->close(fd);
    tmp.mem_read = trs80_read; tmp.mem_write = trs80_write;
    tmp.io_read = trs80_in; tmp.io_write = trs80_out;
    tmp.ctx = NULL;
    cpu = tmp;
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
    struct dirent* entry;
    while ((entry = rb->readdir(dir)) != NULL && idx < 8) {
        if (entry->d_name[0] == '.') continue;
        int nlen = rb->strlen(entry->d_name);
        if (nlen < 5) continue;
        if (rb->strcasecmp(entry->d_name + nlen - 4, ".txt") != 0) continue;
        int line = idx / 4;
        int cur = rb->strlen(lp_lines[line]);
        int room = (int)sizeof(lp_lines[line]) - cur;
        if (room > 1) {
            if (cur > 0)
                rb->snprintf(lp_lines[line] + cur, (size_t)room, " %s", entry->d_name);
            else
                rb->snprintf(lp_lines[line], sizeof(lp_lines[line]), "%s", entry->d_name);
        }
        idx++;
    }
    rb->closedir(dir);
    if (idx == 0)
        rb->snprintf(lp_lines[0], sizeof(lp_lines[0]), "(no .txt programs)");
}

/* ============================================================
   UI
   ============================================================ */
static void draw_input_line(void) {
    rb->lcd_set_foreground(COLOR_TEXT);
    rb->lcd_set_background(COLOR_BG);
    rb->lcd_fillrect(0, INPUT_Y, 320, 11);

    if (lp_timer > 0) {
        rb->lcd_putsxy(0, INPUT_Y, lp_lines[0]);
        rb->lcd_putsxy(0, INPUT_Y + 1, lp_lines[1]);
        return;
    }

    rb->lcd_putsxy(0, INPUT_Y, "> ");
    rb->lcd_putsxy(char_w * 2, INPUT_Y, input_buf);
    int cx = char_w * (2 + input_len);
    if (cx < 318)
        rb->lcd_fillrect(cx, INPUT_Y + char_h - 1, char_w, 1);
}

static void draw_keyboard(void) {
    int cell_w = 320 / KEY_COLS;
    for (int i = 0; i < KEY_COUNT; i++) {
        int r = i / KEY_COLS;
        int c = i % KEY_COLS;
        int x = c * cell_w;
        int y = KEY_TOP + r * KEY_CELL_H;
        int sel = (i == kb_index);

        if (sel) {
            /* 选中：整格反色，确保一眼能看见 */
            rb->lcd_set_foreground(COLOR_KEY_SEL_FG);
            rb->lcd_set_background(COLOR_KEY_SEL_BG);
            rb->lcd_fillrect(x, y, cell_w, KEY_CELL_H);
        }
        else {
            rb->lcd_set_foreground(COLOR_TEXT);
            rb->lcd_set_background(COLOR_BG);
        }

        int tw = 0, th = 0;
        rb->lcd_getstringsize(key_map[i].label, &tw, &th);
        rb->lcd_putsxy(x + (cell_w - tw) / 2, y + (KEY_CELL_H - th) / 2, key_map[i].label);
    }
    rb->lcd_set_foreground(COLOR_TEXT);
    rb->lcd_set_background(COLOR_BG);
}

static void draw_status(void) {
    char buf[64];
    rb->lcd_set_foreground(COLOR_STATUS);
    rb->snprintf(buf, sizeof(buf), "TRS-80 PC:%04X A:%02X F:%02X%s",
        cpu.pc, cpu.a, cpu.f, shift_pending ? " SH" : "");
    rb->lcd_puts(0, 0, buf);
}

static void draw_ui(void) {
    rb->lcd_clear_display();
    rb->lcd_set_background(COLOR_BG);
    draw_status();
    render_text();
    draw_input_line();
    draw_keyboard();
    rb->lcd_update();
}

/* ============================================================
   Commands
   ============================================================ */
static bool handle_short_command(void) {
    if (input_len == 1 && input_buf[0] == 'S') {
        if (save_state()) rb->splash(HZ, "SAVED");
        else              rb->splash(HZ, "SAVE FAIL");
        input_len = 0; input_buf[0] = 0;
        return true;
    }
    if (input_len == 1 && input_buf[0] == 'R') {
        if (restore_state()) rb->splash(HZ, "RESTORED");
        else                 rb->splash(HZ, "NO STATE");
        input_len = 0; input_buf[0] = 0;
        return true;
    }
    if (input_len == 1 && input_buf[0] == 'P') {
        if (load_program_from_path(PROG_DEFAULT)) rb->splash(HZ, "LOADING");
        else                                      rb->splash(HZ, "NO FILE");
        input_len = 0; input_buf[0] = 0;
        return true;
    }
    if (input_len > 2 && input_buf[0] == 'P' && input_buf[1] == ' ') {
        char path[128];
        rb->snprintf(path, sizeof(path), "%s/%s", PROG_DIR, input_buf + 2);
        bool ok = load_program_from_path(path);
        if (ok) rb->splash(HZ, "LOADING");
        else    rb->splash(HZ, "NO FILE");
        input_len = 0; input_buf[0] = 0;
        return true;
    }
    if (input_len == 2 && input_buf[0] == 'L' && input_buf[1] == 'P') {
        list_programs();
        input_len = 0; input_buf[0] = 0;
        return true;
    }
    return false;
}

static void submit_input(void) {
    if (input_len == 0) {
        replay_buf[0] = '\r';
        replay_len = 1;
        start_replay();
        return;
    }
    if (handle_short_command()) return;

    int n = 0;
    for (int i = 0; i < input_len && n < (int)sizeof(replay_buf) - 1; i++)
        replay_buf[n++] = input_buf[i];
    replay_buf[n++] = '\r';
    replay_len = n;
    input_len = 0;
    input_buf[0] = 0;
    start_replay();
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

    if (!init_font()) { logger_flush(&trs80_log); return PLUGIN_ERROR; }
    if (!load_roms()) {
        rb->splash(HZ * 2, "TRS-80 ROMs not found");
        logger_flush(&trs80_log);
        return PLUGIN_ERROR;
    }

    for (int i = 0x3000; i < 0x10000; i++) mem[i] = 0;

    z80_init(&cpu);
    cpu.mem_read = trs80_read; cpu.mem_write = trs80_write;
    cpu.io_read = trs80_in;    cpu.io_write = trs80_out;
    cpu.ctx = NULL; cpu.pc = 0x0000;

    kb_index = 0; shift_pending = false;
    input_len = 0; input_buf[0] = 0;
    replay_len = 0; replay_pos = -1; replay_frames = 0;
    lp_timer = 0;
    for (int i = 0; i < 8; i++) kb_row[i] = 0xFF;

    uint32_t held = 0;
    static bool sel_was = false;
    int btn;

    while (1) {
        for (int i = 0; i < 5000; i++) z80_step(&cpu);

        step_replay();

        if (lp_timer > 0) lp_timer--;

        /* SELECT 边沿触发 */
        bool sel_now = (held & BUTTON_SELECT) != 0;
        if (sel_now && !sel_was && replay_pos < 0) {
            if (is_shift_key(kb_index)) {
                shift_pending = !shift_pending;
            }
            else if (input_len < (int)sizeof(input_buf) - 1) {
                char ch = key_map[kb_index].label[0];
                if (shift_pending && ch >= '0' && ch <= '9') {
                    static const char shifted[] = "_!\"#$%&'()";
                    ch = shifted[ch - '0'];
                }
                input_buf[input_len++] = ch;
                input_buf[input_len] = 0;
                shift_pending = false;
            }
        }
        sel_was = sel_now;

#ifdef HAVE_WHEEL_POSITION
        int wheel = rb->wheel_status();
        if (wheel >= 0 && last_wheel >= 0) {
            int delta = wheel - last_wheel;
            if (delta < -48) delta += 96;
            else if (delta > 48) delta -= 96;
            if (delta >= 2) {
                kb_index += delta / 2;
                while (kb_index >= KEY_COUNT) kb_index -= KEY_COUNT;
            }
            else if (delta <= -2) {
                kb_index += delta / 2;
                while (kb_index < 0) kb_index += KEY_COUNT;
            }
        }
        last_wheel = wheel;
#endif

        while ((btn = rb->button_get(false)) != 0) {
            if (btn & BUTTON_REL) {
                held &= ~(btn & ~BUTTON_REL);
                continue;
            }
            if (btn & BUTTON_REPEAT) continue;

            if (btn == BUTTON_MENU) {
                LOG("=== TRS-80 EXIT ===");
                logger_flush(&trs80_log);
#ifdef HAVE_WHEEL_POSITION
                rb->wheel_send_events(true);
#endif
                return PLUGIN_OK;
            }
            if (btn == BUTTON_RIGHT) {
                for (int i = 0x3000; i < 0x10000; i++) mem[i] = 0;
                z80_init(&cpu);
                cpu.mem_read = trs80_read; cpu.mem_write = trs80_write;
                cpu.io_read = trs80_in;    cpu.io_write = trs80_out;
                cpu.pc = 0x0000;
                input_len = 0; input_buf[0] = 0;
                shift_pending = false;
                replay_len = 0; replay_pos = -1; replay_frames = 0;
                for (int i = 0; i < 8; i++) kb_row[i] = 0xFF;
                continue;
            }
            if (btn == BUTTON_LEFT) {
                if (replay_pos < 0 && input_len > 0)
                    input_buf[--input_len] = 0;
                continue;
            }
            if (btn == BUTTON_PLAY) {
                if (replay_pos < 0) submit_input();
                continue;
            }
            if (btn == BUTTON_SCROLL_FWD) {
                kb_index++;
                if (kb_index >= KEY_COUNT) kb_index = 0;
                continue;
            }
            if (btn == BUTTON_SCROLL_BACK) {
                kb_index--;
                if (kb_index < 0) kb_index = KEY_COUNT - 1;
                continue;
            }

            held |= btn;
        }

        draw_ui();
        rb->yield();
    }
}