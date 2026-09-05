#include "plugin.h"
#include <ctype.h>
#include <fcntl.h>


/*************************** 按键映射 ***************************/
#if CONFIG_KEYPAD == IPOD_4G_PAD
#define MYLRC_QUIT        BUTTON_MENU
#define MYLRC_SELECT      BUTTON_SELECT
#define MYLRC_SCROLL_FWD  BUTTON_SCROLL_FWD
#define MYLRC_SCROLL_BACK BUTTON_SCROLL_BACK
#else
#error "Unsupported keypad. Please define keys for your device."
#endif

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

/*************************** 常量与数据结构 ***************************/
#define MAX_LRC_LINES     2000              /* 足够长歌词 */
#define MAX_LINE_LEN      256               /* 仅用于临时行缓冲，实际无限制 */
#define BUFFER_SIZE       (400 * 1024)      /* 400KB 数据缓冲区 */
#define MAX_META_LEN      128

struct lrc_line {
    long time;              /* 毫秒，-1 表示无时间（原始文本） */
    const char *text;       /* 指向 file_buffer 中该行文本的起始（以 '\0' 结尾） */
};

struct lrc_meta {
    char title[MAX_META_LEN];
    char artist[MAX_META_LEN];
    char album[MAX_META_LEN];
    char by[MAX_META_LEN];
    bool has_meta;
};

/* 所有静态数据均位于插件专用内存段 */
static char file_buffer[BUFFER_SIZE];
static struct lrc_line lrc_lines[MAX_LRC_LINES];
static struct lrc_meta lrc_meta;
static int line_count = 0;
static bool is_raw = false;
static long time_offset = 0;
static int font_height = 10;

/*************************** 函数声明 ***************************/
static long parse_time_tag(const char *str, const char **endptr);
static void format_time_str(long ms, char *buf, int buf_size);
static bool parse_lrc_text(char *text);   /* 注意：会修改 text 缓冲区 */
static bool load_lyrics_from_id3(void);
static bool load_lyrics_from_file(const char *audio_path);
static void display_lyrics(int current_line, long elapsed_ms, bool manual_mode);
static void display_raw_text(void);

/*************************** 工具函数 ***************************/
static long parse_number(const char **p) {
    while (**p && isspace(**p)) (*p)++;
    if (!**p) return -1;
    long val = 0;
    while (**p >= '0' && **p <= '9') {
        val = val * 10 + (**p - '0');
        (*p)++;
    }
    return val;
}

static long parse_time_tag(const char *str, const char **endptr) {
    if (!str || *str != '[') return -1;
    const char *p = str + 1;
    long min = parse_number(&p);
    if (min < 0 || *p != ':') return -1;
    p++;
    long sec = parse_number(&p);
    if (sec < 0 || (*p != ']' && *p != '.')) return -1;
    long cs = 0;
    if (*p == '.') {
        p++;
        cs = parse_number(&p);
        if (cs < 0) return -1;
    }
    if (*p != ']') return -1;
    if (endptr) *endptr = p + 1;
    return min * 60000 + sec * 1000 + cs * 10;
}

static void format_time_str(long ms, char *buf, int buf_size) {
    if (buf_size < 6) {
        if (buf_size > 0) buf[0] = '\0';
        return;
    }
    long min = ms / 60000;
    long sec = (ms % 60000) / 1000;
    if (min > 99) min = 99;
    buf[0] = '0' + (min / 10);
    buf[1] = '0' + (min % 10);
    buf[2] = ':';
    buf[3] = '0' + (sec / 10);
    buf[4] = '0' + (sec % 10);
    buf[5] = '\0';
}

/*************************** 解析 LRC 文本（原地修改） ***************************/
static bool parse_lrc_text(char *text) {
    line_count = 0;
    rb->memset(&lrc_meta, 0, sizeof(lrc_meta));
    time_offset = 0;
    is_raw = false;

    char *p = text;
    char line_buf[MAX_LINE_LEN];   /* 仅用于临时提取标签，不存储文本副本 */
    bool any_valid = false;

    while (*p && line_count < MAX_LRC_LINES) {
        /* 找到行尾 */
        char *eol = rb->strchr(p, '\n');
        if (!eol) {
            /* 最后一行以 '\0' 结尾，我们手动处理 */
            eol = p + rb->strlen(p);
        }

        /* 保存行起始 */
        char *line_start = p;
        size_t line_len = eol - p;

        /* 将行尾 '\n' 和可能的 '\r' 替换为 '\0' */
        char *end = eol;
        if (end > p && *(end-1) == '\r') end--;
        *end = '\0';

        /* 跳过空行 */
        if (line_len == 0) {
            p = eol + 1;
            continue;
        }

        /* 复制行内容到临时缓冲区（因为我们要修改原缓冲区，但解析标签时需要完整行） */
        rb->strlcpy(line_buf, line_start, MIN(line_len+1, MAX_LINE_LEN));

        /* 解析元数据标签 */
        bool is_meta = false;
        if (line_buf[0] == '[' && line_buf[line_len-1] == ']') {
            int content_len = line_len - 1;
            const char *content = line_buf + 1;
            const char *colon = rb->strchr(content, ':');
            if (colon) {
                int tag_len = colon - content;
                const char *val = colon + 1;
                int val_len = content_len - (val - line_buf);
                if (val_len < 0) val_len = 0;
                if (val_len >= MAX_META_LEN) val_len = MAX_META_LEN - 1;

                if (tag_len == 2 && rb->strncmp(content, "ti", 2) == 0) {
                    rb->strlcpy(lrc_meta.title, val, val_len + 1);
                    is_meta = true;
                } else if (tag_len == 2 && rb->strncmp(content, "ar", 2) == 0) {
                    rb->strlcpy(lrc_meta.artist, val, val_len + 1);
                    is_meta = true;
                } else if (tag_len == 2 && rb->strncmp(content, "al", 2) == 0) {
                    rb->strlcpy(lrc_meta.album, val, val_len + 1);
                    is_meta = true;
                } else if (tag_len == 2 && rb->strncmp(content, "by", 2) == 0) {
                    rb->strlcpy(lrc_meta.by, val, val_len + 1);
                    is_meta = true;
                } else if (tag_len == 6 && rb->strncmp(content, "offset", 6) == 0) {
                    const char *num = val;
                    time_offset = parse_number(&num);
                    if (time_offset < 0) time_offset = 0;
                    is_meta = true;
                }
            }
            if (is_meta) {
                lrc_meta.has_meta = true;
                p = eol + 1;
                continue;
            }
        }

        /* 解析时间标签（在原缓冲区中处理，不破坏标签内容，但我们需要提取时间） */
        long times[16];
        int time_count = 0;
        const char *tp = line_start;   /* 原始行，未修改（但我们已把行尾改为\0，不影响前面的标签） */

        while (*tp == '[' && time_count < 16) {
            const char *next = NULL;
            long t = parse_time_tag(tp, &next);
            if (t < 0 || !next) break;
            times[time_count++] = t + time_offset;
            tp = next;
        }

        /* 如果存在时间标签且有歌词文本 */
        if (time_count > 0 && *tp) {
            /* 此时 tp 指向歌词文本起始，但该文本仍位于 line_start 区域，且行尾已被 '\0' 终止 */
            /* 我们需要将歌词文本与时间标签分离，但为了显示方便，我们可以将歌词文本的开头保留，
               但要去除前面的时间标签。由于我们已经修改了行尾，但时间标签部分还保留着。
               我们可以将歌词文本向前移动，覆盖掉时间标签？更简单：我们记录歌词文本的起始指针，
               即 tp，该指针指向 file_buffer 中的位置。这样文本仍然以 '\0' 结尾（因为行尾被替换了）。
               但是，该行开头还有时间标签，如果我们直接使用 tp，那么时间标签不会被显示，因为 tp 是
               从第一个非标签字符开始的。所以我们只需存储 tp 即可。注意，tp 指向的字符串以 '\0' 结尾。 */
            for (int i = 0; i < time_count && line_count < MAX_LRC_LINES; i++) {
                lrc_lines[line_count].time = times[i];
                lrc_lines[line_count].text = tp;   /* 指向歌词文本首字符 */
                line_count++;
            }
            any_valid = true;
        }

        p = eol + 1;
    }

    /* 如果没有任何有效时间标签，则作为原始文本 */
    if (!any_valid) {
        /* 将整个文件按行分割，每行一个条目，time = -1 */
        char *rp = text;
        while (*rp && line_count < MAX_LRC_LINES) {
            char *reol = rb->strchr(rp, '\n');
            if (!reol) reol = rp + rb->strlen(rp);
            char *line_start = rp;
            *reol = '\0';  /* 终止行 */
            if (reol > rp && *(reol-1) == '\r') *(reol-1) = '\0';
            if (*line_start) {
                lrc_lines[line_count].time = -1;
                lrc_lines[line_count].text = line_start;
                line_count++;
            }
            rp = reol + 1;
        }
        if (line_count > 0) {
            is_raw = true;
            return true;
        }
        return false;
    }

    /* 有时间标签：按时间排序 */
    for (int i = 0; i < line_count - 1; i++) {
        for (int j = 0; j < line_count - 1 - i; j++) {
            if (lrc_lines[j].time > lrc_lines[j+1].time) {
                struct lrc_line tmp = lrc_lines[j];
                lrc_lines[j] = lrc_lines[j+1];
                lrc_lines[j+1] = tmp;
            }
        }
    }
    return true;
}

/*************************** 从 ID3v2 中提取 USLT 帧 ***************************/
static const char* find_uslt_in_id3v2(const unsigned char *buf, int len) {
    if (!buf || len < 10) return NULL;
    if (len < 10 || rb->strncmp((const char*)buf, "ID3", 3) != 0) return NULL;
    int tag_size = ((buf[6] & 0x7f) << 21) | ((buf[7] & 0x7f) << 14) |
                   ((buf[8] & 0x7f) << 7) | (buf[9] & 0x7f);
    if (tag_size > len - 10) tag_size = len - 10;
    const unsigned char *p = buf + 10;
    const unsigned char *end = p + tag_size;
    while (p + 10 <= end) {
        char frame_id[5] = {0};
        rb->strncpy(frame_id, (const char*)p, 4);
        int frame_size = ((p[4] & 0xff) << 24) | ((p[5] & 0xff) << 16) |
                         ((p[6] & 0xff) << 8) | (p[7] & 0xff);
        if (rb->strcmp(frame_id, "USLT") == 0) {
            const unsigned char *data = p + 10;
            int data_len = frame_size;
            if (data_len > 0 && data < end) {
                const unsigned char *lyrics = data + 3;
                while (lyrics < end && *lyrics != 0) lyrics++;
                if (lyrics < end) lyrics++;
                if (lyrics < end && *lyrics != 0) {
                    return (const char*)lyrics;
                }
            }
        }
        p += 10 + frame_size;
    }
    return NULL;
}

static bool load_lyrics_from_id3(void) {
    struct mp3entry *id3 = rb->audio_current_track();
    if (!id3) return false;
    if (id3->id3v2buf && id3->id3v2len > 0) {
        const char *uslt_text = find_uslt_in_id3v2((const unsigned char*)id3->id3v2buf, id3->id3v2len);
        if (uslt_text && uslt_text[0]) {
            /* 拷贝到 file_buffer 以便原地修改 */
            size_t len = rb->strlen(uslt_text);
            if (len < BUFFER_SIZE - 1) {
                rb->strlcpy(file_buffer, uslt_text, BUFFER_SIZE);
                return parse_lrc_text(file_buffer);
            }
        }
    }
    return false;
}

static bool load_lyrics_from_file(const char *audio_path) {
    if (!audio_path || !audio_path[0]) return false;
    char lrc_path[MAX_PATH];
    rb->strlcpy(lrc_path, audio_path, sizeof(lrc_path));
    char *dot = rb->strrchr(lrc_path, '.');
    if (!dot) return false;
    if ((size_t)(dot - lrc_path) + 5 > sizeof(lrc_path)) return false;
    rb->strlcpy(dot, ".lrc", sizeof(lrc_path) - (dot - lrc_path));

    int fd = rb->open(lrc_path, O_RDONLY);
    if (fd < 0) return false;
    off_t file_size = rb->filesize(fd);
    if (file_size <= 0 || file_size >= BUFFER_SIZE - 1) {
        rb->close(fd);
        return false;
    }
    ssize_t total_read = 0;
    while (total_read < file_size) {
        ssize_t n = rb->read(fd, file_buffer + total_read, file_size - total_read);
        if (n <= 0) break;
        total_read += n;
    }
    rb->close(fd);
    if (total_read != file_size) return false;
    file_buffer[total_read] = '\0';
    return parse_lrc_text(file_buffer);
}

/*************************** 显示函数 ***************************/
static void build_meta_str(char *buf, int buf_size) {
    buf[0] = '\0';
    if (lrc_meta.title[0]) rb->strlcpy(buf, lrc_meta.title, buf_size);
    if (lrc_meta.artist[0]) {
        int len = rb->strlen(buf);
        if (len > 0 && len + 3 < buf_size) {
            rb->strlcpy(buf + len, " - ", buf_size - len);
            len += 3;
        }
        if (len < buf_size) rb->strlcpy(buf + len, lrc_meta.artist, buf_size - len);
    }
}

static void display_lyrics(int current_line, long elapsed_ms, bool manual_mode) {
    rb->lcd_clear_display();
    int y = 0;
    if (lrc_meta.has_meta) {
        char meta_str[MAX_META_LEN * 2 + 4];
        build_meta_str(meta_str, sizeof(meta_str));
        if (meta_str[0]) {
            rb->lcd_puts(0, y, meta_str);
            y += font_height;
        }
    }
    int bottom_reserve = manual_mode ? font_height : 0;
    int max_y = LCD_HEIGHT - bottom_reserve;
    int usable_lines = (max_y - y) / font_height;
    if (usable_lines < 1) usable_lines = 1;
    int start = current_line - usable_lines / 2;
    if (start < 0) start = 0;
    if (start > line_count - usable_lines && line_count >= usable_lines)
        start = line_count - usable_lines;

    for (int i = start; i < line_count && y + font_height <= max_y; i++) {
        if (i == current_line) {
            rb->lcd_set_drawmode(DRMODE_SOLID | DRMODE_INVERSEVID);
        } else {
            rb->lcd_set_drawmode(DRMODE_SOLID);
        }
        rb->lcd_puts(0, y, lrc_lines[i].text);  /* 直接使用指针 */
        y += font_height;
    }
    rb->lcd_set_drawmode(DRMODE_SOLID);
    if (manual_mode) {
        char status[16];
        format_time_str(elapsed_ms, status, sizeof(status));
        rb->lcd_puts(0, LCD_HEIGHT - font_height, status);
    }
    rb->lcd_update();
}

static void display_raw_text(void) {
    rb->lcd_clear_display();
    int y = 0;
    if (lrc_meta.has_meta) {
        char meta_str[MAX_META_LEN * 2 + 4];
        build_meta_str(meta_str, sizeof(meta_str));
        if (meta_str[0]) {
            rb->lcd_puts(0, y, meta_str);
            y += font_height;
        }
    }
    for (int i = 0; i < line_count && y + font_height <= LCD_HEIGHT; i++) {
        rb->lcd_puts(0, y, lrc_lines[i].text);
        y += font_height;
    }
    rb->lcd_update();
}

/*************************** 主入口 ***************************/
enum plugin_status plugin_start(const void* parameter) {
    (void)parameter;
    int uifont = rb->screens[0]->getuifont();
    font_height = rb->font_get(uifont)->height;

    bool loaded = false;
    if (load_lyrics_from_id3()) {
        loaded = true;
    } else {
        struct mp3entry *id3 = rb->audio_current_track();
        if (id3 && id3->path[0] && load_lyrics_from_file(id3->path)) {
            loaded = true;
        }
    }

    if (!loaded || line_count == 0) {
        rb->splash(HZ * 2, "No lyrics found.");
        return PLUGIN_OK;
    }

    bool quit = false;
    bool manual_mode = false;
    int manual_line_idx = 0;
    int current_line = 0;
    struct mp3entry *id3 = rb->audio_current_track();
    long elapsed = id3 ? id3->elapsed : 0;

    if (is_raw) {
        display_raw_text();
    } else {
        while (current_line < line_count - 1 &&
               lrc_lines[current_line + 1].time <= elapsed) {
            current_line++;
        }
        manual_line_idx = current_line;
        display_lyrics(current_line, elapsed, manual_mode);
    }

    while (!quit) {
        id3 = rb->audio_current_track();
        elapsed = id3 ? id3->elapsed : 0;

        if (!is_raw && !manual_mode) {
            int new_line = 0;
            while (new_line < line_count - 1 &&
                   lrc_lines[new_line + 1].time <= elapsed) {
                new_line++;
            }
            if (new_line != current_line) {
                current_line = new_line;
                manual_line_idx = current_line;
                display_lyrics(current_line, elapsed, manual_mode);
            }
        }

        int button = rb->button_get_w_tmo(HZ / 10);
        if (button == BUTTON_NONE) {
            if (manual_mode && !is_raw) {
                static long last_sec = -1;
                long sec = elapsed / 1000;
                if (sec != last_sec) {
                    last_sec = sec;
                    display_lyrics(current_line, elapsed, manual_mode);
                }
            }
            continue;
        }

        switch (button) {
            case MYLRC_QUIT:
                quit = true;
                break;
            case MYLRC_SELECT:
                if (!is_raw) {
                    manual_mode = !manual_mode;
                    rb->splash(HZ / 2, manual_mode ? "Manual" : "Auto");
                    if (manual_mode) manual_line_idx = current_line;
                    display_lyrics(current_line, elapsed, manual_mode);
                }
                break;
            case MYLRC_SCROLL_FWD:
                if (manual_mode && !is_raw && manual_line_idx < line_count - 1) {
                    manual_line_idx++;
                    current_line = manual_line_idx;
                    display_lyrics(current_line, elapsed, manual_mode);
                }
                break;
            case MYLRC_SCROLL_BACK:
                if (manual_mode && !is_raw && manual_line_idx > 0) {
                    manual_line_idx--;
                    current_line = manual_line_idx;
                    display_lyrics(current_line, elapsed, manual_mode);
                }
                break;
            default:
                if (rb->default_event_handler(button) == SYS_USB_CONNECTED)
                    return PLUGIN_USB_CONNECTED;
                break;
        }
    }
    return PLUGIN_OK;
}
