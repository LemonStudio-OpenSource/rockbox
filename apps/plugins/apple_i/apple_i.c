#include <stdint.h>
#include <stdarg.h>
#include "plugin.h"
#include "6502.h"

/*
 * Apple I Emulator - Enhanced debugging version with logging
 * Logs to /apple_i.log
 */

/* ============================================================
   Logging configuration
   ============================================================ */
#define LOG_ENABLED 1   /* set to 0 to disable logging */

#if LOG_ENABLED
#define LOG_FILE "/apple_i.log"

/* Write a log message (timestamp + message) */
static void log_message(const char *fmt, ...) {
    char buf[256];
    va_list args;
    va_start(args, fmt);
    rb->vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    /* Add timestamp (seconds since plugin start) */
    static long start_ticks = 0;
    if (start_ticks == 0) {
        start_ticks = *rb->current_tick;   /* dereference pointer */
    }
    long seconds = (*rb->current_tick - start_ticks) / HZ;
    char full[300];
    rb->snprintf(full, sizeof(full), "[%lds] %s\n", seconds, buf);

    /* Write to file */
    int fd = rb->open(LOG_FILE, O_WRONLY | O_CREAT | O_APPEND, 0666);
    if (fd >= 0) {
        rb->write(fd, full, rb->strlen(full));
        rb->close(fd);
    }
}

#define LOG(fmt, ...) log_message(fmt, ##__VA_ARGS__)
#else
#define LOG(fmt, ...) ((void)0)
#endif

/* ============================================================
   Screen & colors
   ============================================================ */
#define SCREEN_W 220
#define SCREEN_H 176

#define COLOR_BG      0x0000
#define COLOR_TEXT    0xFFFF    /* WHITE */
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

/* UI state */
static struct font *fixed_font = NULL;
static int char_w = 0, char_h = 0;
static const char *keyboard_chars_row1 = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
static const char *keyboard_chars_row2 = "0123456789 .:-+* /=();,";
static int kb_index = 0, kb_total_len = 0;

static uint8_t key_ready = 0;
static uint8_t key_value = 0;

/* Terminal geometry */
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
        LOG("ERROR: Failed to load 10-Fixed.fnt");
        return false;
    }
    LOG("Font loaded: 10-Fixed.fnt");
    rb->lcd_setfont(fixed_font);
    int w, h;
    rb->lcd_getstringsize("M", &w, &h);
    char_w = w;
    char_h = h;
    if (char_w <= 0 || char_h <= 0) {
        char_w = 6; char_h = 10;
    }
    cols = (SCREEN_W - LEFT_OFFSET) / char_w;
    if (cols > MAX_COLS) cols = MAX_COLS;
    if (cols < 10) cols = 10;
    rows = (SCREEN_H - TOP_OFFSET - BOTTOM_MARGIN) / char_h;
    if (rows > MAX_ROWS) rows = MAX_ROWS;
    if (rows < 4) rows = 4;
    kb_total_len = rb->strlen(keyboard_chars_row1) + rb->strlen(keyboard_chars_row2);
    LOG("Terminal: cols=%d rows=%d char_w=%d char_h=%d", cols, rows, char_w, char_h);
    return true;
}

static char get_kb_char(int idx) {
    int len1 = rb->strlen(keyboard_chars_row1);
    if (idx < len1) return keyboard_chars_row1[idx];
    else return keyboard_chars_row2[idx - len1];
}

/* ============================================================
   Drawing functions
   ============================================================ */
static void draw_char_at(int px, int py, char ch, uint16_t color) {
    if (ch == ' ') return;
    if (color != COLOR_BG) {
        color = COLOR_TEXT;   // enforce white
    }
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
            int x = px + col * char_w;
            int y = py + row * char_h;
            draw_char_at(x, y, ch, COLOR_TEXT);
        }
    }
    if (cursor_x < cols && cursor_y < rows) {
        int cx = px + cursor_x * char_w;
        int cy = py + cursor_y * char_h + char_h - 2;
        rb->lcd_set_foreground(COLOR_CURSOR);
        rb->lcd_fillrect(cx, cy, char_w, 1);
    }
}

static void render_keyboard(void) {
    int y_base = TOP_OFFSET + rows * char_h + 2;
    int y = y_base;
    int char_spacing = char_w + 2;
    int len1 = rb->strlen(keyboard_chars_row1);
    int len2 = rb->strlen(keyboard_chars_row2);

    int x_start = (SCREEN_W - len1 * char_spacing) / 2;
    for (int i = 0; i < len1; i++) {
        int px = x_start + i * char_spacing;
        char ch = keyboard_chars_row1[i];
        if (i == kb_index) {
            rb->lcd_set_foreground(COLOR_HIGHLIGHT_BG);
            rb->lcd_fillrect(px - 1, y - 1, char_w + 2, char_h + 2);
            draw_char_at(px, y, ch, COLOR_BG);
        } else {
            draw_char_at(px, y, ch, COLOR_TEXT);
        }
    }
    y += char_h + 2;
    x_start = (SCREEN_W - len2 * char_spacing) / 2;
    for (int i = 0; i < len2; i++) {
        int idx = len1 + i;
        int px = x_start + i * char_spacing;
        char ch = keyboard_chars_row2[i];
        if (idx == kb_index) {
            rb->lcd_set_foreground(COLOR_HIGHLIGHT_BG);
            rb->lcd_fillrect(px - 1, y - 1, char_w + 2, char_h + 2);
            draw_char_at(px, y, ch, COLOR_BG);
        } else {
            draw_char_at(px, y, ch, COLOR_TEXT);
        }
    }
    rb->lcd_set_foreground(COLOR_LABEL);
    rb->lcd_setfont(fixed_font);
    rb->lcd_puts(0, 7, "KB: ");
}

static void render_status(void) {
    char buf[64];
    rb->lcd_set_foreground(COLOR_STATUS);
    rb->lcd_setfont(fixed_font);
    rb->snprintf(buf, sizeof(buf), "PC:%04X A:%02X X:%02X Y:%02X %s",
                 programcounter, regA, regX, regY,
                 cpu_halted ? "HLT" : (rom_loaded ? "ROM" : "NO ROM"));
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
   Video output (called by CPU write)
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
        if (cursor_x >= cols) {
            cursor_x = 0;
            if (cursor_y < rows - 1) cursor_y++;
            else {
                for (int r = 1; r < rows; r++)
                    rb->memcpy(video[r-1], video[r], cols);
                rb->memset(video[rows-1], ' ', cols);
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
        if (key_ready) {
            key_ready = 0;
            LOG("KEY read: 0x%02X ('%c')", key_value, key_value >= 32 ? key_value : '.');
            return key_value | 0x80;
        }
        return 0;
    }
    if (addr == 0xD011) {
        /* 键盘控制寄存器：bit 7 = 1 表示有按键等待 */
        return key_ready ? 0x80 : 0x00;
    }
    return mem[addr];
}

static void mem_write(uint16_t addr, uint8_t val) {
    if (addr >= 0x0200 && addr <= 0x03FF) {
        int offset = addr - 0x0200;
        int row = offset / cols;
        int col = offset % cols;
        if (row < rows && col < cols) {
            char ch = val;
            if (ch < 32) ch = ' ';
            if (video[row][col] != ch) {
                LOG("VIDEO write addr=%04X val=0x%02X ('%c') at (%d,%d)", addr, val, val>=32?val:'.', row, col);
                video[row][col] = ch;
            }
        }
    }
    /* Apple I 显示端口：$D012 是标准地址，$D0F2 因不完全解码等效 */
    if (addr == 0xD011 || addr == 0xD012 || addr == 0xD0F2) {
        if (addr == 0xD011) LOG("D011 write val=0x%02X ('%c')", val, val>=32?val:'.');
        video_type_char((char)val);
    }
    mem[addr] = val;
}

/* ============================================================
   Load ROM
   ============================================================ */
static bool load_rom(void) {
    int fd = rb->open("/apple1basic.bin", O_RDONLY);
    if (fd < 0) {
        LOG("ROM open failed: /apple1basic.bin not found");
        return false;
    }
    size_t size = rb->filesize(fd);
    if (size > 0x2000) size = 0x2000;
    rb->read(fd, mem + 0xE000, size);
    rb->close(fd);
    #if LOG_ENABLED
        char hexbuf[64];
        int pos = 0;
        int show = size < 16 ? size : 16;
        for (int i = 0; i < show; i++) {
            pos += rb->snprintf(hexbuf+pos, sizeof(hexbuf)-pos, "%02X ", mem[0xE000+i]);
        }
        LOG("ROM loaded: %d bytes at 0xE000, first %d bytes: %s", size, show, hexbuf);
    #endif
    return true;
}

static bool load_monitor(void) {
    int fd = rb->open("/apple1monitor.bin", O_RDONLY);
    if (fd < 0) {
        LOG("Monitor ROM not found, using built-in stubs");
        return false;
    }
    size_t size = rb->filesize(fd);
    if (size > 0x100) size = 0x100;  /* Monitor is 256 bytes */
    rb->read(fd, mem + 0xFF00, size);
    rb->close(fd);
    LOG("Monitor ROM loaded: %d bytes at 0xFF00", (int)size);
    return true;
}

/* ============================================================
   Force display some test text
   ============================================================ */
static void init_video_text(void) {
    for (int r = 0; r < MAX_ROWS; r++) {
        rb->memset(video[r], ' ', MAX_COLS);
        video[r][MAX_COLS] = '\0';
    }
    const char *msg = "APPLE I EMULATOR READY";
    int len = rb->strlen(msg);
    if (len > cols) len = cols;
    rb->memcpy(video[0], msg, len);
    rb->memcpy(video[1], "PRESS SELECT TO INPUT", 21);
    rb->memcpy(video[2], "PLAY = ENTER", 12);
    cursor_x = 0;
    cursor_y = 3;
    LOG("Initial video text set");
}

/* ============================================================
   Plugin entry
   ============================================================ */
enum plugin_status plugin_start(const void *parameter) {
    (void)parameter;
    int btn;

    LOG("=== Apple I Emulator START ===");

    if (!init_font()) return PLUGIN_ERROR;

    cpu_read = mem_read;
    cpu_write = mem_write;

    rom_loaded = load_rom();
    if (!rom_loaded) {
        rb->splash(HZ, "ROM not found");
    }

    init_video_text();

    /* 加载 Woz Monitor ROM */
    if (!load_monitor()) {
        /* fallback：Monitor ROM 不存在时，用最简 stub */
        mem[0xFFEF] = 0x4C; mem[0xFFF0] = 0x11; mem[0xFFF1] = 0xD0; /* JMP $D011 */
        mem[0xFFD0] = 0x4C; mem[0xFFD1] = 0x10; mem[0xFFD2] = 0xD0; /* JMP $D010 */
        LOG("Using built-in Monitor stubs");
    }

    /* 确保复位向量指向 BASIC 入口 */
    mem[0xFFFC] = 0x00;
    mem[0xFFFD] = 0xE0;
    LOG("Reset vector set to $E000");
    /* BRK/IRQ 向量指向 Monitor 的 BRK 处理或 BASIC 冷启动 */
    mem[0xFFFE] = 0x00;
    mem[0xFFFF] = 0xE0;
    LOG("IRQ/BRK vector set to $E000");

    m6502_reset();
    LOG("CPU reset, PC=0x%04X", programcounter);
    /* 强制 PC 为 BASIC 入口，以防复位向量未生效 */
    programcounter = 0xE000;
    LOG("PC forced to 0xE000 (BASIC entry)");
    cpu_halted = false;

    while (1) {
        if (!cpu_halted) {
            for (int i = 0; i < 500; i++) {
                if (!m6502_step()) {
                    cpu_halted = true;
                    LOG("CPU HALTED (HLT instruction or invalid)");
                    break;
                }
            }
        }

        btn = rb->button_get(false);
        if (btn == BUTTON_MENU) {
            LOG("MENU pressed, exiting");
            break;
        }

        if (btn == BUTTON_PLAY) {
            LOG("PLAY pressed -> ENTER key");
            key_ready = 1;
            key_value = '\r';
        } else if (btn == BUTTON_SCROLL_FWD) {
            kb_index++;
            if (kb_index >= kb_total_len) kb_index = 0;
        } else if (btn == BUTTON_SCROLL_BACK) {
            kb_index--;
            if (kb_index < 0) kb_index = kb_total_len - 1;
        } else if (btn == BUTTON_SELECT) {
            char ch = get_kb_char(kb_index);
            LOG("SELECT pressed -> key '%c' (0x%02X)", ch, ch);
            key_ready = 1;
            key_value = (uint8_t)ch;
        }

        draw_ui();
        rb->yield();
    }

    LOG("=== Apple I Emulator EXIT ===");
    return PLUGIN_OK;
}
