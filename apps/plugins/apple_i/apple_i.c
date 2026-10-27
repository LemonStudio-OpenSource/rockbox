#include <stdint.h>
#include <stdarg.h>
#include "plugin.h"
int g_log_counter = 0;
#define MAX_LOG_COUNT 5000
/* ============================================================
   Logging configuration
   ============================================================ */
#define LOG_ENABLED 1
#if LOG_ENABLED
#define LOG_FILE "/apple_i/log/apple_i.log"
#define LOG_MAX_SIZE (128 * 1024)

static char *log_buf = NULL;
static size_t log_buf_size = 0;
static size_t log_buf_used = 0;
static long log_start_ticks = 0;

static bool log_init(void) {
    size_t avail;
    log_buf = rb->plugin_get_buffer(&avail);
    if (!log_buf || avail < 4096) {
        static char fallback[4096];
        log_buf = fallback;
        log_buf_size = sizeof(fallback);
    } else {
        log_buf_size = (avail < LOG_MAX_SIZE) ? avail : LOG_MAX_SIZE;
    }
    log_buf_used = 0;
    log_start_ticks = 0;
    return true;
}

static void log_message(const char *fmt, ...) {
#if LOG_ENABLED
    if (!log_buf || log_buf_used >= log_buf_size - 256) return;
    char tmp[256];
    va_list args;
    va_start(args, fmt);
    rb->vsnprintf(tmp, sizeof(tmp), fmt, args);
    va_end(args);
    if (log_start_ticks == 0) log_start_ticks = *rb->current_tick;
    long seconds = (*rb->current_tick - log_start_ticks) / HZ;
    int n = rb->snprintf(log_buf + log_buf_used, log_buf_size - log_buf_used,
                         "[%lds] %s\n", seconds, tmp);
    if (n > 0) log_buf_used += n;
#endif
}

#define LOG(fmt, ...) log_message(fmt, ##__VA_ARGS__)
#else
#define LOG(fmt, ...) ((void)0)
#endif

static void log_flush(void) {
#if LOG_ENABLED
    if (!log_buf || log_buf_used == 0) return;
    rb->mkdir("/apple_i");
    rb->mkdir("/apple_i/log");
    int fd = rb->open(LOG_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd >= 0) {
        rb->write(fd, log_buf, log_buf_used);
        rb->close(fd);
    }
#endif
}

#include "6502.h"

static void init_input(void) {
#ifdef HAVE_WHEEL_POSITION
    rb->wheel_send_events(false);
#endif
}

/* ============================================================
   Screen & colors
   ============================================================ */
#define SCREEN_W 320
#define SCREEN_H 240

#define COLOR_BG      0x0000
#define COLOR_TEXT    0xFFFF
#define COLOR_CURSOR  0xFFFF
#define COLOR_LABEL   0x8410
#define COLOR_HIGHLIGHT_BG 0xFFFF
#define COLOR_STATUS  0xFFFF

/* ============================================================
   Memory and video
   ============================================================ */
#define MAX_COLS 40
#define MAX_ROWS 24

static uint8_t mem[65536];
static char video[MAX_ROWS][MAX_COLS + 1];
static int cols, rows;
static int cursor_x = 0, cursor_y = 0;
static bool cpu_halted = false;
static bool rom_loaded = false;

static struct font *fixed_font = NULL;
static int char_w = 0, char_h = 0;
static const char key_chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "0123456789"
    " !\"#$%&'()*+,-./"
    ":;<=>?@[\\]^_`{|}~";
static int kb_index = 0, kb_total_len = 0;

/* ===== 键盘 FIFO 队列 ===== */
#define KEY_QUEUE_SIZE 256
static uint8_t key_queue[KEY_QUEUE_SIZE];
static volatile int key_q_head = 0;
static volatile int key_q_tail = 0;

static inline int key_q_empty(void) { return key_q_head == key_q_tail; }
static inline int key_q_full(void) { return ((key_q_tail + 1) % KEY_QUEUE_SIZE) == key_q_head; }
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

/* 键盘状态锁：读完 $D010 后 $D011 在若干周期内返回 0 */
static int kbd_lock_cycles = 0;

/* MS-BASIC 输入缓冲地址（cz6502.bin 实测：$13 起，最长 $47 字符，终止符 $5A） */
#define MS_LINBUF_BASE_OSI     0x13
#define MS_LINBUF_MAX          0x5A

/* 是否运行 MS-BASIC */
static bool ms_basic_mode = false;

/* 当前 ROM 显示名，用于状态栏 */
static const char *rom_display_name = "APPLE";

static char input_buf[1024];
static int input_len = 0;

static char playback_buf[4096];
static int playback_len = 0;
static int playback_pos = 0;

static const int TOP_OFFSET = 24;
static const int BOTTOM_MARGIN = 2;
static const int LEFT_OFFSET = 0;

/* ============================================================
   Font & UI init
   ============================================================ */
static bool init_font(void) {
    fixed_font = rb->font_load("/.rockbox/fonts/10-Fixed.fnt");
    if (!fixed_font) {
        rb->splash(HZ*2, "Failed to load 10-Fixed.fnt");
        return false;
    }
    rb->lcd_setfont(fixed_font);
    int w, h;
    rb->lcd_getstringsize("M", &w, &h);
    char_w = w;
    char_h = h;
    if (char_w <= 0 || char_h <= 0) { char_w = 6; char_h = 10; }
    cols = (SCREEN_W - LEFT_OFFSET) / char_w;
    if (cols > MAX_COLS) cols = MAX_COLS;
    if (cols < 10) cols = 10;
    rows = (SCREEN_H - TOP_OFFSET - BOTTOM_MARGIN) / char_h;
    if (rows > MAX_ROWS) rows = MAX_ROWS;
    if (rows < 4) rows = 4;
    if (rows > 16) rows = 16;
    kb_total_len = sizeof(key_chars) - 1;
    return true;
}

static char get_kb_char(int idx) {
    if (idx >= 0 && idx < kb_total_len) return key_chars[idx];
    return ' ';
}

/* ============================================================
   Drawing functions
   ============================================================ */
static void draw_char_at(int px, int py, char ch, uint16_t color) {
    if (ch == ' ') return;
    if (color != COLOR_BG) color = COLOR_TEXT;
    char str[2] = {ch, 0};
    rb->lcd_set_foreground(color);
    rb->lcd_setfont(fixed_font);
    rb->lcd_putsxy(px, py, str);
}

static void render_terminal(void) {
    int px = LEFT_OFFSET;
    int py = TOP_OFFSET;
    rb->lcd_set_foreground(COLOR_BG);
    rb->lcd_fillrect(px, py, cols * char_w, rows * char_h);
    for (int row = 0; row < rows; row++) {
        for (int col = 0; col < cols; col++) {
            char ch = video[row][col];
            if (ch == 0) ch = ' ';
            draw_char_at(px + col * char_w, py + row * char_h, ch, COLOR_TEXT);
        }
    }
    if (cursor_x < cols && cursor_y < rows) {
        int cx = px + cursor_x * char_w;
        int cy = py + cursor_y * char_h + char_h - 2;
        rb->lcd_set_foreground(COLOR_CURSOR);
        rb->lcd_fillrect(cx, cy, char_w, 1);
    }
    if (input_len > 0) {
        int echo_x = px + cursor_x * char_w;
        int echo_y = py + cursor_y * char_h;
        rb->lcd_set_foreground(COLOR_TEXT);
        for (int i = 0; i < input_len && (cursor_x + i) < cols; i++) {
            char str[2] = {input_buf[i], 0};
            rb->lcd_putsxy(echo_x + i * char_w, echo_y, str);
        }
        int cx = echo_x + input_len * char_w;
        int cy = echo_y + char_h - 2;
        rb->lcd_set_foreground(COLOR_CURSOR);
        rb->lcd_fillrect(cx, cy, char_w, 1);
    }
}

static void render_keyboard(void)
{
    int i;
    int y = SCREEN_H - 3 * char_h - 4;
    int start_x = 0;
    for (i = 0; i < 36; i++) {
        char buf[2] = {key_chars[i], '\0'};
        if (i == kb_index) { rb->lcd_set_foreground(COLOR_BG); rb->lcd_set_background(COLOR_TEXT); }
        else { rb->lcd_set_foreground(COLOR_TEXT); rb->lcd_set_background(COLOR_BG); }
        rb->lcd_putsxy(start_x + i * char_w, y, buf);
    }
    y += char_h + 2;
    start_x = 0;
    for (i = 36; i < 53; i++) {
        char buf[5];
        if (i == kb_index) { rb->lcd_set_foreground(COLOR_BG); rb->lcd_set_background(COLOR_TEXT); }
        else { rb->lcd_set_foreground(COLOR_TEXT); rb->lcd_set_background(COLOR_BG); }
        if (key_chars[i] == ' ') rb->snprintf(buf, sizeof(buf), "\xE2\x90\xA3");
        else { buf[0] = key_chars[i]; buf[1] = '\0'; }
        rb->lcd_putsxy(start_x, y, buf);
        start_x += char_w;
    }
    y += char_h + 2;
    start_x = 0;
    for (i = 53; i < 69; i++) {
        char buf[2] = {key_chars[i], '\0'};
        if (i == kb_index) { rb->lcd_set_foreground(COLOR_BG); rb->lcd_set_background(COLOR_TEXT); }
        else { rb->lcd_set_foreground(COLOR_TEXT); rb->lcd_set_background(COLOR_BG); }
        rb->lcd_putsxy(start_x, y, buf);
        start_x += char_w;
    }
    rb->lcd_set_foreground(COLOR_TEXT);
    rb->lcd_set_background(COLOR_BG);
    rb->lcd_update();
}

static void render_status(void) {
    char buf[64];
    rb->lcd_set_foreground(COLOR_STATUS);
    rb->lcd_setfont(fixed_font);
    const char *mode_str;
    if (playback_pos < playback_len)      mode_str = "PLY";
    else if (cpu_halted)                  mode_str = "HLT";
    else if (rom_loaded)                  mode_str = rom_display_name;
    else                                  mode_str = "NO ROM";
    rb->snprintf(buf, sizeof(buf), "PC:%04X A:%02X X:%02X Y:%02X %s",
                 programcounter, regA, regX, regY, mode_str);
    rb->lcd_puts(0, 0, buf);
    rb->lcd_puts(0, 1, "PLAY=enter  MENU=exit");
}

static void draw_ui(void) {
    rb->lcd_clear_display();
    rb->lcd_set_background(COLOR_BG);
    render_status();
    render_terminal();
    render_keyboard();
    rb->lcd_update();
}

/* ============================================================
   Video output
   ============================================================ */
static void video_type_char(char ch) {
    if (ch == '\r') {
        cursor_x = 0;
        if (cursor_y < rows - 1) cursor_y++;
        else {
            for (int r = 1; r < rows; r++)
                rb->memcpy(video[r-1], video[r], cols);
            rb->memset(video[rows-1], ' ', cols);
        }
        return;
    }
    if (ch >= 32 && ch <= 126) {
        video[cursor_y][cursor_x] = ch;
        cursor_x++;
        if (cursor_x >= MAX_COLS) {
            cursor_x = 0;
            if (cursor_y < rows - 1) cursor_y++;
            else {
                for (int r = 1; r < rows; r++)
                    rb->memcpy(video[r-1], video[r], MAX_COLS);
                rb->memset(video[rows-1], ' ', MAX_COLS);
            }
        }
    }
}

/* ============================================================
   CPU memory callbacks
   ============================================================ */
uint8_t (*cpu_read)(uint16_t addr) = NULL;
void (*cpu_write)(uint16_t addr, uint8_t val) = NULL;

static uint8_t mem_read(uint16_t addr) {
    if (addr == 0xD010) {
        if (!key_q_empty()) {
            uint8_t c = key_q_pop();
            kbd_lock_cycles = 200;
            return c | 0x80;
        }
        return 0x00;
    }
    if (addr == 0xD011) {
        if (kbd_lock_cycles > 0) return 0x00;
        return key_q_empty() ? 0x00 : 0x80;
    }
    return mem[addr];
}

static void mem_write(uint16_t addr, uint8_t val) {
    if (addr == 0xD012 || addr == 0xD0F2) {
        video_type_char((char)(val & 0x7F));
        return;
    }
    if (addr >= 0xE000) return;   /* ROM 写保护 */
    mem[addr] = val;
}

/* ============================================================
   Load ROM
   ============================================================ */
static bool load_rom(void) {
    int fd = rb->open("/apple_i/roms/AM27C256@DIP28.BIN", O_RDONLY);
    if (fd < 0) { LOG("AM27C256@DIP28.BIN not found"); return false; }
    off_t pos = rb->lseek(fd, 0x0000, SEEK_SET);
    if (pos < 0) { rb->close(fd); return false; }
    rb->read(fd, mem + 0xE000, 0x2000);
    rb->close(fd);
    LOG("Apple BASIC (Block 0) loaded");
    return true;
}

static bool load_monitor(void) {
    int fd = rb->open("/apple_i/roms/apple1monitor.bin", O_RDONLY);
    if (fd < 0) return false;
    size_t size = rb->ffilesize(fd);
    if (size > 0x100) size = 0x100;
    rb->read(fd, mem + 0xFF00, size);
    rb->close(fd);
    return true;
}

static bool load_apple_basic_v2(void) {
    int fd = rb->open("/apple_i/roms/AM27C256@DIP28.BIN", O_RDONLY);
    if (fd < 0) { LOG("AM27C256@DIP28.BIN not found"); return false; }
    off_t pos = rb->lseek(fd, 0x2000, SEEK_SET);
    if (pos < 0) { rb->close(fd); return false; }
    rb->read(fd, mem + 0xE000, 0x2000);
    rb->close(fd);
    LOG("Apple BASIC (Block 1, Krusader 1.3) loaded");
    return true;
}

static bool name_has_6502(const char *name) {
    int nlen = rb->strlen(name);
    if (nlen < 4) return false;
    for (int k = 0; k <= nlen - 4; k++) {
        char c0 = name[k],   c1 = name[k+1], c2 = name[k+2], c3 = name[k+3];
        if (c0 >= 'a' && c0 <= 'z') c0 -= 32;
        if (c1 >= 'a' && c1 <= 'z') c1 -= 32;
        if (c2 >= 'a' && c2 <= 'z') c2 -= 32;
        if (c3 >= 'a' && c3 <= 'z') c3 -= 32;
        if (c0 == '6' && c1 == '5' && c2 == '0' && c3 == '2') return true;
    }
    return false;
}

static bool load_ms_basic(void) {
    static const char *paths[] = {
        "/apple_i/roms/cz6502.bin",
        "/apple_i/roms/CZ6502.BIN",
        "/apple_i/roms/CZ6502.bin",
        "/apple_i/roms/cz6502.BIN",
        "/apple_i/roms/Cz6502.bin",
    };
    int fd = -1;
    for (int i = 0; i < (int)(sizeof(paths) / sizeof(paths[0])); i++) {
        fd = rb->open(paths[i], O_RDONLY);
        if (fd >= 0) {
            LOG("MS-BASIC opened: %s", paths[i]);
            break;
        }
    }
    if (fd < 0) {
        LOG("cz6502.bin not found, scanning directory");
        DIR *dir = rb->opendir("/apple_i/roms");
        if (dir) {
            struct dirent *entry;
            while ((entry = rb->readdir(dir)) != NULL) {
                if (entry->d_name[0] == '.') continue;
                if (!name_has_6502(entry->d_name)) continue;
                char fullpath[256];
                rb->snprintf(fullpath, sizeof(fullpath), "/apple_i/roms/%s", entry->d_name);
                fd = rb->open(fullpath, O_RDONLY);
                if (fd >= 0) {
                    LOG("MS-BASIC found via scan: %s", fullpath);
                    break;
                }
            }
            rb->closedir(dir);
        }
    }
    if (fd < 0) {
        LOG("MS-BASIC file not found anywhere");
        return false;
    }
    rb->read(fd, mem + 0xE000, 0x2000);
    rb->close(fd);
    LOG("MS-BASIC loaded");
    return true;
}

/* ============================================================
   Save / Restore / Program Load
   ============================================================ */
static bool save_state(void) {
    int fd = rb->open("/apple_i/rams/apple_i_ram_data.bin", O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) return false;
    rb->write(fd, mem, 65536);
    uint8_t state[8];
    state[0] = programcounter & 0xFF;
    state[1] = (programcounter >> 8) & 0xFF;
    state[2] = regA;
    state[3] = regX;
    state[4] = regY;
    state[5] = StackPointer;
    state[6] = Status;
    state[7] = 0x01;
    rb->write(fd, state, sizeof(state));
    rb->close(fd);
    return true;
}

static bool restore_state(void) {
    int fd = rb->open("/apple_i/rams/apple_i_ram_data.bin", O_RDONLY);
    if (fd < 0) return false;
    size_t size = rb->ffilesize(fd);
    if (size < 65536 + 8) { rb->close(fd); return false; }
    rb->read(fd, mem, 65536);
    uint8_t state[8];
    rb->read(fd, state, sizeof(state));
    rb->close(fd);
    programcounter = state[0] | (state[1] << 8);
    regA = state[2];
    regX = state[3];
    regY = state[4];
    StackPointer = state[5];
    Status = state[6];
    cpu_halted = false;
    return true;
}

static bool is_all_uppercase(const char *str) {
    for (int i = 0; str[i]; i++) {
        if (str[i] >= 'a' && str[i] <= 'z') return false;
    }
    return true;
}

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

static bool load_program(void) {
    return load_program_from_path("/apple_i/programs/apple_i_program.txt");
}

static void terminal_print(const char *str) {
    while (*str) video_type_char(*str++);
}

static void init_video_text(void) {
    for (int r = 0; r < MAX_ROWS; r++) {
        rb->memset(video[r], ' ', MAX_COLS);
        video[r][MAX_COLS] = '\0';
    }
    const char *msg = "APPLE I EMULATOR BY LEMONSTUDIO";
    int len = rb->strlen(msg);
    if (len > cols) len = cols;
    rb->memcpy(video[0], msg, len);
    rb->memcpy(video[1], "PRESS SELECT TO INPUT", 21);
    rb->memcpy(video[2], "PLAY = ENTER", 12);
    cursor_x = 0;
    cursor_y = 3;
}

static uint8_t apple_i_cpu_read(uint16_t addr)
{
    if (addr == 0xD010) {
        if (!key_q_empty()) {
            uint8_t c = key_q_pop();
            kbd_lock_cycles = 200;
            uint8_t ret = c | 0x80;
            LOG("KEY READ D010 -> 0x%02X", ret);
            return ret;
        }
        return 0x00;
    }
    if (addr == 0xD011) {
        if (kbd_lock_cycles > 0) return 0x00;
        return key_q_empty() ? 0x00 : 0x80;
    }
    if (addr == 0xD0F2 || addr == 0xD012) {
        return mem[addr] & 0x7F;
    }
    return mem[addr];
}

/* ============================================================
   Plugin entry
   ============================================================ */
enum plugin_status plugin_start(const void *parameter) {
    (void)parameter;
    log_init();
    init_input();
    int btn;

    LOG("=== Apple I Emulator START ===");

    if (!init_font()) return PLUGIN_ERROR;

    cpu_read = apple_i_cpu_read;
    cpu_write = mem_write;

    rom_loaded = load_rom();
    if (!rom_loaded) rb->splash(HZ, "ROM not found");

    init_video_text();

    if (!load_monitor()) {
        mem[0xFFEF] = 0x4C; mem[0xFFF0] = 0x11; mem[0xFFF1] = 0xD0;
        mem[0xFFD0] = 0x4C; mem[0xFFD1] = 0x10; mem[0xFFD2] = 0xD0;
    }

    mem[0xFFFC] = 0x00;
    mem[0xFFFD] = 0xE0;
    mem[0xFFFE] = 0x00;
    mem[0xFFFF] = 0xFF;

    m6502_reset();
    programcounter = 0xE000;
    cpu_halted = false;
    ms_basic_mode = false;
    rom_display_name = "APPLE";

    key_q_clear();
    kbd_lock_cycles = 0;

    static int last_wheel = -1;

    while (1) {
        while (playback_pos < playback_len && !key_q_full()) {
            uint8_t c = playback_buf[playback_pos++];
            key_q_push(c);
        }

        if (!cpu_halted) {
            for (int i = 0; i < 500; i++) {
                if (kbd_lock_cycles > 0) kbd_lock_cycles--;
                if (!m6502_step()) {
                    cpu_halted = true;
                    break;
                }
            }
        }

    #ifdef HAVE_WHEEL_POSITION
        int wheel = rb->wheel_status();
        if (wheel >= 0 && last_wheel >= 0) {
            int delta = wheel - last_wheel;
            if (delta < -48) delta += 96;
            else if (delta > 48) delta -= 96;
            if (delta >= 2) {
                kb_index += delta / 2;
                if (kb_index >= kb_total_len) kb_index -= kb_total_len;
            } else if (delta <= -2) {
                kb_index += delta / 2;
                if (kb_index < 0) kb_index += kb_total_len;
            }
        }
        last_wheel = wheel;
    #endif

        while ((btn = rb->button_get(false)) != 0) {
            if (btn & BUTTON_REL) continue;
            if (btn & BUTTON_REPEAT) continue;

            if (btn == BUTTON_MENU) {
                LOG("=== Apple I Emulator EXIT ===");
                log_flush();
            #ifdef HAVE_WHEEL_POSITION
                rb->wheel_send_events(true);
            #endif
                return PLUGIN_OK;
            }
            if (btn == BUTTON_PLAY) {
                if (playback_pos < playback_len) {
                    /* playback in progress */
                } else if (input_len == 1 && input_buf[0] == 'S') {
                    save_state();
                    input_len = 0;
                    key_q_push('\r');
                } else if (input_len == 1 && input_buf[0] == 'R') {
                    restore_state();
                    input_len = 0;
                    key_q_push('\r');
                } else if (input_len == 1 && input_buf[0] == 'P') {
                    load_program();
                    input_len = 0;
                } else if (input_len > 2 && input_buf[0] == 'P' && input_buf[1] == ' ') {
                    int space_pos = 1;
                    while (space_pos < input_len && input_buf[space_pos] == ' ') space_pos++;
                    if (space_pos < input_len) {
                        char path[128];
                        rb->snprintf(path, sizeof(path), "/apple_i/programs/%s", input_buf + space_pos);
                        load_program_from_path(path);
                    } else {
                        load_program();
                    }
                    input_len = 0;
                } else if (input_len >= 4 &&
                           input_buf[0] == 'L' && input_buf[1] == 'O' &&
                           input_buf[2] == 'A' && input_buf[3] == 'D') {
                    int pos = 4;
                    while (pos < input_len && input_buf[pos] == ' ') pos++;
                    bool ok = false;
                    const char *arg = input_buf + pos;

                    if (pos == input_len || rb->strcmp(arg, "APPLE") == 0) {
                        ok = load_rom();
                        ms_basic_mode = false;
                        rom_display_name = "APPLE";
                    } else if (rb->strcmp(arg, "APPLE2") == 0) {
                        ok = load_apple_basic_v2();
                        ms_basic_mode = false;
                        rom_display_name = "KRUSADER";
                    } else if (rb->strcmp(arg, "MS") == 0) {
                        ok = load_ms_basic();
                        ms_basic_mode = true;
                        rom_display_name = "MS-BASIC";
                    }

                    input_len = 0;
                    if (ok) {
                        for (int r = 0; r < MAX_ROWS; r++) rb->memset(video[r], ' ', MAX_COLS);
                        cursor_x = 0;
                        cursor_y = 0;
                        m6502_reset();
                        programcounter = 0xE000;
                        cpu_halted = false;
                        key_q_clear();
                        kbd_lock_cycles = 0;
                        playback_pos = playback_len = 0;
                        LOG("LOAD command OK: %s", arg);
                        if (rb->strcmp(arg, "MS") == 0) {
                            terminal_print("\rMS-BASIC LOADED\r> ");
                        } else if (rb->strcmp(arg, "APPLE2") == 0) {
                            terminal_print("\rKRUSADER LOADED\r> ");
                        } else {
                            terminal_print("\rAPPLE BASIC LOADED\r> ");
                        }
                    } else {
                        LOG("LOAD command FAILED");
                        terminal_print("\rROM NOT FOUND\r> ");
                    }
                } else if (input_len == 2 && input_buf[0] == 'L' && input_buf[1] == 'P') {
                    input_len = 0;
                    DIR *dir = rb->opendir("/apple_i/programs");
                    if (dir) {
                        struct dirent *entry;
                        terminal_print("\r");
                        while ((entry = rb->readdir(dir)) != NULL) {
                            if (entry->d_name[0] == '.') continue;
                            int name_len = rb->strlen(entry->d_name);
                            if (name_len < 5) continue;
                            if (rb->strcasecmp(entry->d_name + name_len - 4, ".txt") != 0) continue;
                            if (!is_all_uppercase(entry->d_name)) continue;
                            terminal_print(entry->d_name);
                            terminal_print("\r");
                        }
                        rb->closedir(dir);
                        terminal_print("> ");
                    } else {
                        terminal_print("\rERROR: CANNOT OPEN DIR\r> ");
                    }
                    LOG("LP command executed");
                } else if (input_len == 1 && input_buf[0] == 'A') {
                    input_len = 0;
                    terminal_print("\rApple I Emulator For Rockbox\rby LemonStudio\rThis is open-source software\rCommercial use prohibited\rLicense: GPLv3\r> ");
                    LOG("ABOUT command executed");
                } else if (input_len == 1 && input_buf[0] == 'H') {
                    input_len = 0;
                    terminal_print("\rA - About this emulator\rH - Help (commands)\rS - Save state to file\rR - Restore state from file\rP <File name> - Load program from txt\rLP - List all programs\rLOAD [APPLE|APPLE2|MS] - Load ROM\rCC <ABBR> - Check Character (BanG Dream!)\rRST - Reset and clear RAM\rEX - Exit\r> ");
                    LOG("HELP command executed");
                } else if (input_len == 3 && input_buf[0] == 'R' && input_buf[1] == 'S' && input_buf[2] == 'T') {
                    input_len = 0;
                    rb->memset(mem, 0, 0xE000);
                    for (int r = 0; r < MAX_ROWS; r++) rb->memset(video[r], ' ', MAX_COLS);
                    cursor_x = 0; cursor_y = 0;
                    m6502_reset();
                    programcounter = 0xFF00;
                    cpu_halted = false;
                    key_q_clear();
                    kbd_lock_cycles = 0;
                    playback_pos = playback_len = 0;
                    terminal_print("\\ ");
                    LOG("RST command executed");
                } else if (input_len == 2 && input_buf[0] == 'E' && input_buf[1] == 'X') {
                    terminal_print("Exiting...");
                    LOG("EXIT BY USER COMMAND");
                    log_flush();
                #ifdef HAVE_WHEEL_POSITION
                    rb->wheel_send_events(true);
                #endif
                    return PLUGIN_OK;
                } else if (input_len == 1 && input_buf[0] == 'B') {
                    /* Easter egg: restored from the August 2026 revision */
                    input_len = 0;
                    terminal_print("\rYes! BanG Dream!\r> ");
                    LOG("Yes! BanG Dream!");
                } else if (input_len >= 2 && input_buf[0] == 'C' && input_buf[1] == 'C') {
                    /* CC: Check Character - BanG Dream! character lookup */
                    char abbr[32];
                    int abbr_pos = 0;
                    int i = 2;
                    while (i < input_len && input_buf[i] == ' ') i++;
                    while (i < input_len && abbr_pos < 31) {
                        abbr[abbr_pos++] = input_buf[i++];
                    }
                    abbr[abbr_pos] = '\0';
                    input_len = 0;

                    const char *band = NULL;
                    const char *name = NULL;

                    if (rb->strcmp(abbr, "KSM") == 0) { band = "Poppin'Party"; name = "Toyama Kasumi"; }
                    else if (rb->strcmp(abbr, "ARS") == 0) { band = "Poppin'Party"; name = "Ichigaya Arisa"; }
                    else if (rb->strcmp(abbr, "SAYA") == 0) { band = "Poppin'Party"; name = "Yamabuki Saaya"; }
                    else if (rb->strcmp(abbr, "OTAE") == 0) { band = "Poppin'Party"; name = "Hanazono Tae"; }
                    else if (rb->strcmp(abbr, "RIMI") == 0) { band = "Poppin'Party"; name = "Ushigome Rimi"; }
                    else if (rb->strcmp(abbr, "KKR") == 0) { band = "Hello, Happy World!"; name = "Tsurumaki Kokoro"; }
                    else if (rb->strcmp(abbr, "MSK") == 0) { band = "Hello, Happy World!"; name = "Okusawa Misaki"; }
                    else if (rb->strcmp(abbr, "HGM") == 0) { band = "Hello, Happy World!"; name = "Kitazawa Hagumi"; }
                    else if (rb->strcmp(abbr, "KN") == 0) { band = "Hello, Happy World!"; name = "Matsubara Kanon"; }
                    else if (rb->strcmp(abbr, "KOR") == 0) { band = "Hello, Happy World!"; name = "Seta Kaoru"; }
                    else if (rb->strcmp(abbr, "MOCA") == 0) { band = "Afterglow"; name = "Aoba Moca"; }
                    else if (rb->strcmp(abbr, "HMR") == 0) { band = "Afterglow"; name = "Uehara Himari"; }
                    else if (rb->strcmp(abbr, "TSUGU") == 0) { band = "Afterglow"; name = "Hazawa Tsugumi"; }
                    else if (rb->strcmp(abbr, "RAN") == 0) { band = "Afterglow"; name = "Mitake Ran"; }
                    else if (rb->strcmp(abbr, "SOYA") == 0) { band = "Afterglow"; name = "Udagawa Tomoe"; }
                    else if (rb->strcmp(abbr, "CST") == 0) { band = "Pastel*Palettes"; name = "Shirasagi Chisato"; }
                    else if (rb->strcmp(abbr, "HINA") == 0) { band = "Pastel*Palettes"; name = "Hikawa Hina"; }
                    else if (rb->strcmp(abbr, "EVE") == 0) { band = "Pastel*Palettes"; name = "Wakamiya Eve"; }
                    else if (rb->strcmp(abbr, "MAYA") == 0) { band = "Pastel*Palettes"; name = "Yamato Maya"; }
                    else if (rb->strcmp(abbr, "AYA") == 0) { band = "Pastel*Palettes"; name = "Maruyama Aya"; }
                    else if (rb->strcmp(abbr, "YKN") == 0) { band = "Roselia"; name = "Minato Yukina"; }
                    else if (rb->strcmp(abbr, "LISA") == 0) { band = "Roselia"; name = "Imai Lisa"; }
                    else if (rb->strcmp(abbr, "RINRIN") == 0) { band = "Roselia"; name = "Shirokane Rinko"; }
                    else if (rb->strcmp(abbr, "AKO") == 0) { band = "Roselia"; name = "Udagawa Ako"; }
                    else if (rb->strcmp(abbr, "SAYO") == 0) { band = "Roselia"; name = "Hikawa Sayo"; }
                    else if (rb->strcmp(abbr, "MSR") == 0) { band = "Morfonica"; name = "Kurata Mashiro"; }
                    else if (rb->strcmp(abbr, "TOKO") == 0) { band = "Morfonica"; name = "Kirigaya Touko"; }
                    else if (rb->strcmp(abbr, "NNM") == 0) { band = "Morfonica"; name = "Hiromachi Nanami"; }
                    else if (rb->strcmp(abbr, "TKS") == 0) { band = "Morfonica"; name = "Futaba Tsukushi"; }
                    else if (rb->strcmp(abbr, "RUI") == 0) { band = "Morfonica"; name = "Yashio Rui"; }
                    else if (rb->strcmp(abbr, "CHUCHU") == 0) { band = "RAISE A SUILEN"; name = "Tamade Chiyu"; }
                    else if (rb->strcmp(abbr, "PAREO") == 0) { band = "RAISE A SUILEN"; name = "Nyubara Reona"; }
                    else if (rb->strcmp(abbr, "MASKING") == 0) { band = "RAISE A SUILEN"; name = "Satou Masuki"; }
                    else if (rb->strcmp(abbr, "LAYER") == 0) { band = "RAISE A SUILEN"; name = "Wakana Rei"; }
                    else if (rb->strcmp(abbr, "LOCK") == 0) { band = "RAISE A SUILEN"; name = "Asahi Rokka"; }
                    else if (rb->strcmp(abbr, "ANON") == 0) { band = "MyGO!!!!!"; name = "Chihaya Anon"; }
                    else if (rb->strcmp(abbr, "TOMO") == 0) { band = "MyGO!!!!!"; name = "Takamatsu Tomori"; }
                    else if (rb->strcmp(abbr, "TAKI") == 0) { band = "MyGO!!!!!"; name = "Shiina Taki"; }
                    else if (rb->strcmp(abbr, "SOYO") == 0) { band = "MyGO!!!!!"; name = "Nagasaki Soyo"; }
                    else if (rb->strcmp(abbr, "RANA") == 0) { band = "MyGO!!!!!"; name = "Kaname Raana"; }
                    else {
                        terminal_print("\rUNKNOWN ABBR\r> ");
                        LOG("CC command: unknown abbreviation '%s'", abbr);
                    }

                    if (band && name) {
                        char outbuf[128];
                        rb->snprintf(outbuf, sizeof(outbuf), "\r%s-%s\r> ", band, name);
                        terminal_print(outbuf);
                        LOG("CC command: %s -> %s-%s", abbr, band, name);
                    }
                } else {
                    if (input_len > 0) {
                        /* MS-BASIC 的 GETLINE 用 STA $13,X / CPX #$47，
                           LINBUF 位于 $13，最长 71 字符，终止符在 $5A。
                           只写 $13，不再同时写 $19 以免污染零页。 */
                        if (ms_basic_mode) {
                            int base = MS_LINBUF_BASE_OSI;
                            for (int i = 0; i < input_len && (base + i) < MS_LINBUF_MAX; i++) {
                                mem[base + i] = (uint8_t)input_buf[i];
                            }
                            if (base + input_len <= MS_LINBUF_MAX) {
                                mem[base + input_len] = 0x00;
                            }
                            LOG("LINBUF written at $13: %d chars", input_len);
                        }
                        rb->memcpy(playback_buf, input_buf, input_len);
                        playback_buf[input_len] = '\r';
                        playback_len = input_len + 1;
                        playback_pos = 0;
                        input_len = 0;
                    } else {
                        key_q_push('\r');
                    }
                }
            } else if (btn == BUTTON_SCROLL_FWD) {
                kb_index++;
                if (kb_index >= kb_total_len) kb_index = 0;
            } else if (btn == BUTTON_SCROLL_BACK) {
                kb_index--;
                if (kb_index < 0) kb_index = kb_total_len - 1;
            } else if (btn == BUTTON_LEFT) {
                if (input_len > 0) {
                    input_len--;
                } else {
                    key_q_push('_');
                }
            } else if (btn == BUTTON_RIGHT) {
                if (playback_pos < playback_len) {
                    playback_pos = playback_len = 0;
                } else {
                    programcounter = 0xFF00;
                    cpu_halted = false;
                    key_q_clear();
                    kbd_lock_cycles = 0;
                    input_len = 0;
                }
            } else if (btn == BUTTON_SELECT) {
                if (playback_pos < playback_len) {
                    /* ignore */
                } else {
                    char ch = get_kb_char(kb_index);
                    if (input_len < (int)sizeof(input_buf) - 1) {
                        input_buf[input_len++] = ch;
                    }
                }
            }
        }

        draw_ui();
        rb->yield();
    }
}
