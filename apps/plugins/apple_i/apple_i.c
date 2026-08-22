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
#define LOG_MAX_SIZE (128 * 1024)  /* 最大 128KB 日志缓冲 */

static char *log_buf = NULL;
static size_t log_buf_size = 0;
static size_t log_buf_used = 0;
static long log_start_ticks = 0;

/* 初始化日志缓冲区（使用 plugin_get_buffer 获取内存） */
static bool log_init(void) {
    size_t avail;
    log_buf = rb->plugin_get_buffer(&avail);
    if (!log_buf || avail < 4096) {
        /* fallback：用静态小缓冲区 */
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

/* Write a log message (timestamp + message) */
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

/* 退出时一次性刷到硬盘 */
static void log_flush(void) {
#if LOG_ENABLED
    if (!log_buf || log_buf_used == 0) return;
    int fd = rb->open(LOG_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd >= 0) {
        rb->write(fd, log_buf, log_buf_used);
        rb->close(fd);
    }
#endif
}

#include "6502.h"

/*
 * Apple I Emulator - Enhanced debugging version with logging
 * Logs to /apple_i/log/apple_i.log
 */

static void init_input(void) {
#ifdef HAVE_WHEEL_POSITION
    rb->wheel_send_events(false);  // 关闭默认事件，我们自己轮询
#endif
}


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
static const char key_chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"   /* 0-25  */
    "0123456789"                    /* 26-35 */
    " !\"#$%&'()*+,-./"             /* 36-52: 空格+符号前半 */
    ":;<=>?@[\\]^_`{|}~";           /* 53-68: 符号后半 */
static int kb_index = 0, kb_total_len = 0;

static uint8_t key_ready = 0;
static uint8_t key_value = 0;

/* Command input buffer */
static char input_buf[1024];
static int input_len = 0;

/* Program playback buffer */
static char playback_buf[4096];
static int playback_len = 0;
static int playback_pos = 0;

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
    /* 为键盘预留 3 行 + 间距 */
    if (rows > 10) rows = 10;
    kb_total_len = sizeof(key_chars) - 1;  /* 去掉末尾 '\0' */
    LOG("Terminal: cols=%d rows=%d char_w=%d char_h=%d", cols, rows, char_w, char_h);
    return true;
}

static char get_kb_char(int idx) {
    if (idx >= 0 && idx < kb_total_len)
        return key_chars[idx];
    return ' ';
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
        /* ===== 本地回显：显示已缓冲但未发送给 ROM 的输入 ===== */
    if (input_len > 0) {
        int echo_x = px + cursor_x * char_w;
        int echo_y = py + cursor_y * char_h;
        rb->lcd_set_foreground(COLOR_TEXT);
        for (int i = 0; i < input_len && (cursor_x + i) < cols; i++) {
            char str[2] = {input_buf[i], 0};
            rb->lcd_putsxy(echo_x + i * char_w, echo_y, str);
        }
        /* 把光标画在输入末尾 */
        int cx = echo_x + input_len * char_w;
        int cy = echo_y + char_h - 2;
        rb->lcd_set_foreground(COLOR_CURSOR);
        rb->lcd_fillrect(cx, cy, char_w, 1);
    }
}

static void render_keyboard(void)
{
    int i;
    int y = SCREEN_H - 3 * char_h - 4;  /* 3行 + 2个2px间距 */
    int start_x = 0;

    /* ========== 第1行：A-Z + 0-9 (36个) ========== */
    for (i = 0; i < 36; i++) {
        char buf[2] = {key_chars[i], '\0'};
        if (i == kb_index) {
            rb->lcd_set_foreground(COLOR_BG);
            rb->lcd_set_background(COLOR_TEXT);
        } else {
            rb->lcd_set_foreground(COLOR_TEXT);
            rb->lcd_set_background(COLOR_BG);
        }
        rb->lcd_putsxy(start_x + i * char_w, y, buf);
    }

    /* ========== 第2行：空格 + !"#$%&'()*+,-./ (17个) ========== */
    y += char_h + 2;
    start_x = 0;
    for (i = 36; i < 53; i++) {
        char buf[5];
        if (i == kb_index) {
            rb->lcd_set_foreground(COLOR_BG);
            rb->lcd_set_background(COLOR_TEXT);
        } else {
            rb->lcd_set_foreground(COLOR_TEXT);
            rb->lcd_set_background(COLOR_BG);
        }
        if (key_chars[i] == ' ') {
            rb->snprintf(buf, sizeof(buf), "\xE2\x90\xA3"); /* ␣ U+2423 */
        } else {
            buf[0] = key_chars[i];
            buf[1] = '\0';
        }
        rb->lcd_putsxy(start_x, y, buf);
        start_x += char_w;
    }

    /* ========== 第3行：:;<=>?@[\]^_`{|}~ (16个) ========== */
    y += char_h + 2;
    start_x = 0;
    for (i = 53; i < 69; i++) {
        char buf[2] = {key_chars[i], '\0'};
        if (i == kb_index) {
            rb->lcd_set_foreground(COLOR_BG);
            rb->lcd_set_background(COLOR_TEXT);
        } else {
            rb->lcd_set_foreground(COLOR_TEXT);
            rb->lcd_set_background(COLOR_BG);
        }
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

    const char *mode_str = "";
    if (playback_pos < playback_len)      mode_str = "PLY";
    else if (cpu_halted)                mode_str = "HLT";
    else if (rom_loaded)                mode_str = "ROM";
    else                                mode_str = "NO ROM";

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
   Video output (called by CPU write)
   ============================================================ */
static void video_type_char(char ch) {
    LOG("VIDEO_TYPE_CHAR: ch=0x%02X cursor=(%d,%d)", (unsigned char)ch, cursor_x, cursor_y);
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

static uint8_t mem_read(uint16_t addr) {        /* NOTICE：This Function is deprecated! Need to handle keyboard input,please use the "apple_i_cpu_read" function */
    if (addr == 0xD010) {
        uint8_t ret = key_ready ? (key_value | 0x80) : 0;
        if (key_ready) {
            key_ready = 0;
            LOG("KEY READ D010 -> 0x%02X (key_value=0x%02X)", ret, key_value);
        } else if (g_log_counter < MAX_LOG_COUNT) {
            LOG("READ D010 -> 0x00 (no key)");
            g_log_counter++;
        }
        return ret;
    }
    if (addr == 0xD011) {
        uint8_t ret = key_ready ? 0x80 : 0x00;
        if (g_log_counter < MAX_LOG_COUNT) {
            LOG("READ D011 -> 0x%02X (key_ready=%d)", ret, key_ready);
            g_log_counter++;
        }
        return ret;
    }
    uint8_t val = mem[addr];
    if (g_log_counter < MAX_LOG_COUNT) {
        LOG("READ %04X -> %02X", addr, val);
        g_log_counter++;
    }
    return val;
}

static void mem_write(uint16_t addr, uint8_t val) {
    if (addr == 0xD012 || addr == 0xD0F2) {
        LOG("VIDEO WRITE %04X <- %02X ('%c') cursor=(%d,%d)",
            addr, val, (val >= 0x20 && val < 0x7F) ? (val & 0x7F) : '.', cursor_x, cursor_y);
        video_type_char((char)(val & 0x7F));
    }
    mem[addr] = val;
}

/* ============================================================
   Load ROM
   ============================================================ */
static bool load_rom(void) {
    int fd = rb->open("/apple_i/roms/apple1basic.bin", O_RDONLY);
    if (fd < 0) {
        LOG("ROM open failed: /apple_i/roms/apple1basic.bin not found");
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
    int fd = rb->open("/apple_i/roms/apple1monitor.bin", O_RDONLY);
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
   Save / Restore / Program Load
   ============================================================ */

static bool save_state(void) {
    int fd = rb->open("/apple_i/rams/apple_i_ram_data.bin", O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) {
        LOG("SAVE failed: cannot open /apple_i/rams/apple_i_ram_data.bin");
        return false;
    }

    /* 1) 64KB 完整内存 */
    rb->write(fd, mem, 65536);

    /* 2) CPU 状态: PC(2) A(1) X(1) Y(1) SP(1) Status(1) Version(1) */
    uint8_t state[8];
    state[0] = programcounter & 0xFF;
    state[1] = (programcounter >> 8) & 0xFF;
    state[2] = regA;
    state[3] = regX;
    state[4] = regY;
    state[5] = StackPointer;
    state[6] = Status;
    state[7] = 0x01;  /* file format version */

    rb->write(fd, state, sizeof(state));
    rb->close(fd);

    LOG("SAVE OK: PC=%04X A=%02X X=%02X Y=%02X SP=%02X S=%02X",
        programcounter, regA, regX, regY, StackPointer, Status);
    return true;
}

static bool restore_state(void) {
    int fd = rb->open("/apple_i/rams/apple_i_ram_data.bin", O_RDONLY);
    if (fd < 0) {
        LOG("RESTORE failed: /apple_i/rams/apple_i_ram_data.bin not found");
        return false;
    }

    size_t size = rb->filesize(fd);
    if (size < 65536 + 8) {
        LOG("RESTORE failed: file too small (%d bytes)", (int)size);
        rb->close(fd);
        return false;
    }

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
    /* state[7] = version, reserved for future use */

    cpu_halted = false;

    LOG("RESTORE OK: PC=%04X A=%02X X=%02X Y=%02X SP=%02X S=%02X",
        programcounter, regA, regX, regY, StackPointer, Status);
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
    if (fd < 0) {
        LOG("PROGRAM LOAD failed: %s not found", path);
        return false;
    }

    size_t size = rb->filesize(fd);
    if (size == 0) {
        rb->close(fd);
        return false;
    }

    char temp[sizeof(playback_buf)];
    if (size > sizeof(temp) - 1) {
        size = sizeof(temp) - 1;
        LOG("PROGRAM LOAD warning: file truncated to %d bytes", (int)size);
    }
    rb->read(fd, temp, size);
    rb->close(fd);

    /* Normalize line endings: \r\n / \n / \r -> single \r */
    playback_len = 0;
    for (size_t i = 0; i < size && playback_len < (int)sizeof(playback_buf) - 1; i++) {
        char c = temp[i];
        if (c == '\n') {
            playback_buf[playback_len++] = '\r';
        } else if (c == '\r') {
            if (i + 1 >= size || temp[i + 1] != '\n') {
                playback_buf[playback_len++] = '\r';
            }
        } else {
            playback_buf[playback_len++] = c;
        }
    }

    playback_pos = 0;

    LOG("PROGRAM LOAD OK: %d bytes queued from %s", playback_len, path);
    return true;
}

static bool load_program(void) {
    return load_program_from_path("/apple_i/programs/apple_i_program.txt");
}

/* ============================================================
   Terminal direct print (bypass ROM)
   ============================================================ */
static void terminal_print(const char *str) {
    while (*str) {
        video_type_char(*str++);
    }
}


/* ============================================================
   Force display some test text
   ============================================================ */
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
    LOG("Initial video text set");
}

static uint8_t apple_i_cpu_read(uint16_t addr)
{
    /* 键盘数据端口 */
    if (addr == 0xD010) {
        uint8_t ret = key_ready ? (key_value | 0x80) : 0;
        if (key_ready) {
            key_ready = 0;
            LOG("KEY READ D010 -> 0x%02X (key_value=0x%02X)", ret, key_value);
        }
        return ret;
    }

    /* 键盘状态端口 */
    if (addr == 0xD011) {
        uint8_t ret = key_ready ? 0x80 : 0x00;
        return ret;
    }

    /* 视频端口：强制 bit7 = 0，避免 BIT/BMI 误判 */
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
    mem[0xFFFF] = 0xFF;
    LOG("IRQ/BRK vector set to $E000");

    m6502_reset();
    LOG("CPU reset, PC=0x%04X", programcounter);
    /* 强制 PC 为 BASIC 入口，以防复位向量未生效 */
    programcounter = 0xE000;
    LOG("PC forced to 0xE000 (BASIC entry)");
    cpu_halted = false;

    static int last_wheel = -1;

    while (1) {
        /* Inject queued playback characters before CPU runs */
        if (playback_pos < playback_len && !key_ready) {
            key_ready = 1;
            key_value = playback_buf[playback_pos++];
            LOG("PLAYBACK inject 0x%02X '%c'", key_value,
                (key_value >= 0x20 && key_value < 0x7F) ? key_value : '.');
        }
        
        if (!cpu_halted) {
            for (int i = 0; i < 500; i++) {
                if (!m6502_step()) {
                    cpu_halted = true;
                    LOG("CPU HALT PC=%04X op=%02X", programcounter, mem[programcounter]);
                    break;
                }
            }
        }


            /* ===== 滚轮连续输入（核心修改）===== */
    #ifdef HAVE_WHEEL_POSITION
        int wheel = rb->wheel_status();  // 0~95 绝对位置
        if (wheel >= 0 && last_wheel >= 0) {
            int delta = wheel - last_wheel;
            /* 处理 0<->95 回绕 */
            if (delta < -48) delta += 96;
            else if (delta > 48) delta -= 96;
        
            /* 转得越快跳得越多，最小灵敏度 2 格 */
            if (delta >= 2) {
                kb_index += delta / 2;
                if (kb_index >= kb_total_len) kb_index -= kb_total_len;
            } else if (delta <= -2) {
                kb_index += delta / 2;  // delta 是负数
                if (kb_index < 0) kb_index += kb_total_len;
            }
        }
        last_wheel = wheel;
    #endif
       
        /* 批量处理所有待处理的按钮事件 */
        while ((btn = rb->button_get(false)) != 0) {
            if (btn == BUTTON_MENU) {
                LOG("MENU pressed, exiting");
                LOG("=== Apple I Emulator EXIT ===");
                log_flush();        /* Write logs to the disk */
            #ifdef HAVE_WHEEL_POSITION
                rb->wheel_send_events(true);  // 恢复滚轮事件
            #endif
                return PLUGIN_OK;        /* <--The only way out is here */
            }
            if (btn == BUTTON_PLAY) {
                if (playback_pos < playback_len) {
                    LOG("PLAY ignored: playback in progress");
                } else if (input_len == 1 && input_buf[0] == 'S') {
                    save_state();
                    input_len = 0;
                    key_ready = 1;
                    key_value = '\r';
                    LOG("SAVE command executed");
                } else if (input_len == 1 && input_buf[0] == 'R') {
                    restore_state();
                    input_len = 0;
                    key_ready = 1;
                    key_value = '\r';
                    LOG("RESTORE command executed");
                                } else if (input_len == 1 && input_buf[0] == 'P') {
                    /* 无参数 P：使用默认路径 */
                    load_program();
                    input_len = 0;
                    LOG("PROGRAM LOAD command executed (default)");
                } else if (input_len > 2 && input_buf[0] == 'P' && input_buf[1] == ' ') {
                    /* 有参数 P <FILENAME> */
                    int space_pos = 1;
                    while (space_pos < input_len && input_buf[space_pos] == ' ') space_pos++;
                    if (space_pos < input_len) {
                        char path[128];
                        rb->snprintf(path, sizeof(path), "/%s", input_buf + space_pos);
                        load_program_from_path(path);
                        LOG("PROGRAM LOAD command executed: %s", path);
                    } else {
                        load_program();
                        LOG("PROGRAM LOAD command executed (default)");
                    }
                    input_len = 0;
                } else if (input_len == 2 && input_buf[0] == 'L' && input_buf[1] == 'P') {
                    /* LP：列出根目录下全大写的 .TXT 文件 */
                    input_len = 0;
                    DIR *dir = rb->opendir("/apple_i/programs");
                    if (dir) {
                        struct dirent *entry;
                        terminal_print("\r");
                        while ((entry = rb->readdir(dir)) != NULL) {
                            if (entry->d_name[0] == '.') continue;
                            
                            int name_len = rb->strlen(entry->d_name);
                            if (name_len < 5) continue;
                            
                            /* 检查后缀 .txt（不区分大小写）且全文件名大写 */
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
                    terminal_print("\rApple I Emulator For Rockbox\rby LemonStudio\rThis is open-source software\rCommercial use prohibited\rLicense: GPLv2\r> ");
                    LOG("ABOUT command executed");
                } else if (input_len == 1 && input_buf[0] == 'H') {
                    input_len = 0;
                    terminal_print("\rA - About this emulator\rH - Help (commands)\rS - Save state to file\rR - Restore state from file\rP <File name> - Load program from txt\rPREV - Backspace key\rNEXT - Soft reset/Stop\rSELECT - Input char\rPLAY - Enter command\rSCROLL - Move cursor\rRST - Reset and clear RAM\rLP - List all programs in root\rCC <ABBR> - Check Character (BanG Dream!)\r> ");
                    LOG("HELP command executed");
                } else if (input_len == 3 && input_buf[0] == 'R' && input_buf[1] == 'S' && input_buf[2] == 'T') {
                    input_len = 0;
                    /* 强制重置：清零 RAM，保留 ROM，进入 Woz Monitor */
                    rb->memset(mem, 0, 0xE000);  /* 只清 $0000-$DFFF，保留 $E000-$FFFF 的 ROM */
                    for (int r = 0; r < MAX_ROWS; r++) {
                        rb->memset(video[r], ' ', MAX_COLS);
                    }
                    cursor_x = 0;
                    cursor_y = 0;
                    m6502_reset();
                    programcounter = 0xFF00;  /* 直接跳 Monitor 入口 */
                    cpu_halted = false;
                    key_ready = 0;
                    playback_pos = playback_len = 0;
                    terminal_print("\\ ");    /* Monitor 提示符 */
                    LOG("RST command executed, hard reset to Monitor");
                } else if (input_len == 2 && input_buf[0] == 'E' && input_buf[1] == 'X') {
                    terminal_print("Exiting...");
                    LOG("EXIT BY USER COMMAND");
                    log_flush();
                #ifdef HAVE_WHEEL_POSITION
                    rb->wheel_send_events(true);  // 恢复滚轮事件
                #endif
                    return PLUGIN_OK;
                } else if (input_len == 1 && input_buf[0] == 'B') {
                    input_len=0;
                    terminal_print("Yes! BanG Dream!");        /* An Easter Egg: Yes! BanG Dream! */
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
                    /* Normal input: playback buffered chars + CR */
                    if (input_len > 0) {
                        rb->memcpy(playback_buf, input_buf, input_len);
                        playback_buf[input_len] = '\r';
                        playback_len = input_len + 1;
                        playback_pos = 0;
                        input_len = 0;
                    } else {
                        key_ready = 1;
                        key_value = '\r';
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
                    LOG("PREV pressed, backspace buffered input, len=%d", input_len);
                } else {
                    /* 缓冲为空时，发送 Apple I 退格符 '_' (0x5F) 给 ROM */
                    key_ready = 1;
                    key_value = '_';
                    LOG("PREV pressed, send '_' backspace to ROM");
                }
            } else if (btn == BUTTON_RIGHT) {
                if (playback_pos < playback_len) {
                    /* 停止正在进行的程序回放 */
                    playback_pos = playback_len = 0;
                    LOG("NEXT pressed, playback aborted");
                } else {
                    /* 软 RESET：跳转到 Monitor 入口，不清内存 */
                    programcounter = 0xFF00;
                    cpu_halted = false;
                    key_ready = 0;
                    input_len = 0;
                    LOG("NEXT pressed, soft RESET to Monitor $FF00");
                }            
            } else if (btn == BUTTON_SELECT) {
                if (playback_pos < playback_len) {
                    LOG("SELECT ignored: playback in progress");
                } else {
                    char ch = get_kb_char(kb_index);
                    if (input_len < (int)sizeof(input_buf) - 1) {
                        input_buf[input_len++] = ch;
                    }
                    LOG("SELECT pressed, buffered '%c'", ch);
                }
            }
        }

        draw_ui();
        rb->yield();
    }



   /* LOG("=== Apple I Emulator EXIT ==="); */
   /* return PLUGIN_OK; */
   /* 
    * This plugin only has one way to exit—pressing MENU.
    * But the PLUGIN_OK used to end the run has already been used in the check for whether MENU is pressed,
    * so the program can't exit through any way outside the while(1) loop.
    * Therefore, this exit point has been abandoned. 
    */
}
