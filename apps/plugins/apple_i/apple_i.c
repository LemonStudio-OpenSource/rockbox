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
#define LOG_FILE "/apple_i.log"
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
 * Logs to /apple_i.log
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
                key_ready = 1;
                key_value = '\r';
                LOG("PLAY pressed,link to ENTER");
            } else if (btn == BUTTON_SCROLL_FWD) {
                kb_index++;
                if (kb_index >= kb_total_len) kb_index = 0;
            } else if (btn == BUTTON_SCROLL_BACK) {
                kb_index--;
                if (kb_index < 0) kb_index = kb_total_len - 1;
            } else if (btn == BUTTON_SELECT) {
                char ch = get_kb_char(kb_index);
                key_ready = 1;
                key_value = (uint8_t)ch;
                LOG("SELECT pressed,link to '%c'", ch);
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
