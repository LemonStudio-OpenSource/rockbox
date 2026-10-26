/***************************************************************************
 *             ebook.c - 电子书阅读器 (iPod Color / Video / Classic)
 *  版本：9.0 修复章节缓存加载只读第一项的问题
 *  功能：文件浏览、UTF-8 渲染、夜间模式、5个书签、精确翻页、偏移量缓存、
 *        章节扫描（首次扫描写入缓存，MD5 校验后复用）、目录列表/数字输入跳转
 *  内存：缓存 41 页偏移量 + 静态章节数组（200 章 × 64B）+ 章节缓存文件
 *  按键：左/右键翻页，MENU 主菜单，PLAY 快速书签
 *  目录：SELECT 切换列表/数字模式，PLAY 跳转，MENU 返回
 *  滚轮：基于 wheel_status 绝对位置轮询，快速滚动一次多步
 *  日志：/ebook/logs/yyyy-mm-dd-hh-mm-ss.txt
 *  开屏：根据机型显示 "Lemon E-Book / For XXX / Ver.0.9Beta"，像素居中
 ***************************************************************************/

#include "plugin.h"
#include "lib/md5.h"

#ifndef MD5_STRING_LENGTH
#define MD5_STRING_LENGTH 32
#endif

/* 配置 */
#ifndef LCD_WIDTH
#define LCD_WIDTH       220
#endif
#ifndef LCD_HEIGHT
#define LCD_HEIGHT      176
#endif
#define MAX_BOOKMARKS   5
#define SETTINGS_FILE   "/ebook/config/settings.cfg"
#define READ_BUFFER_SIZE (4 * 1024)
#define CACHE_SIZE      41

#define MAX_CHAPTERS    200
#define CHAP_TITLE_LEN  64
#define MD5_HEX_LEN     32

/* 全局变量 */
static char *render_buffer = NULL;
static size_t render_buf_size = 0;
static int current_file_offset = 0;
static char current_file[MAX_PATH];
static int file_size = 0;
static int font_height, font_width;
static int lines_per_screen = 0;

/* 书签 */
static int bookmarks[MAX_BOOKMARKS];

/* 章节 */
static int  chapter_offsets[MAX_CHAPTERS];
static char chapter_titles[MAX_CHAPTERS][CHAP_TITLE_LEN];
static int  chapter_numbers[MAX_CHAPTERS];
static int  chapter_count = 0;

/* 标题栏轮换 */
static long last_toggle_tick = 0;
static bool show_mem = false;

/* 夜间模式 */
static bool night_mode = false;

/* ---------- 日志系统 ---------- */
static int log_fd = -1;
static char log_file_path[MAX_PATH];

static void log_init(void)
{
    struct tm *tm = rb->get_time();
    if (!tm) {
        rb->snprintf(log_file_path, sizeof(log_file_path), "/ebook/logs/unknown.log");
    } else {
        rb->snprintf(log_file_path, sizeof(log_file_path),
                     "/ebook/logs/%04d-%02d-%02d-%02d-%02d-%02d.log",
                     tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
                     tm->tm_hour, tm->tm_min, tm->tm_sec);
    }
    rb->mkdir("/ebook/logs");
    log_fd = rb->open(log_file_path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (log_fd >= 0) {
        char header[64];
        rb->snprintf(header, sizeof(header), "=== E-Book Plugin Started ===\n");
        rb->write(log_fd, header, rb->strlen(header));
    }
}

static void log_write(const char *level, const char *fmt, ...)
{
    if (log_fd < 0) return;
    char buf[256];
    va_list args;
    va_start(args, fmt);
    int len = rb->vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    if (len > 0) {
        char line[300];
        rb->snprintf(line, sizeof(line), "[%s] %s\n", level, buf);
        rb->write(log_fd, line, rb->strlen(line));
    }
}

#define LOG_INFO(fmt, ...)   log_write("INFO", fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...)   log_write("WARNING", fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...)  log_write("ERROR", fmt, ##__VA_ARGS__)

/* ---------- 偏移量缓存 ---------- */
static int page_offsets[CACHE_SIZE];
static int cache_count = 0;
static int current_cache_pos = 0;
static bool current_cache_valid = false;

static void cache_clear(void)
{
    cache_count = 0;
    current_cache_pos = 0;
    current_cache_valid = false;
    LOG_INFO("Cache cleared");
}

static int cache_add_offset(int offset)
{
    if (offset < 0 || offset >= file_size) return -1;

    for (int i = 0; i < cache_count; i++) {
        if (page_offsets[i] == offset) {
            LOG_INFO("Cache duplicate offset %d ignored", offset);
            return i;
        }
    }

    if (cache_count < CACHE_SIZE) {
        page_offsets[cache_count] = offset;
        int idx = cache_count;
        cache_count++;
        LOG_INFO("Cache add offset %d at idx %d", offset, idx);
        return idx;
    }

    for (int i = 0; i < CACHE_SIZE - 1; i++) {
        page_offsets[i] = page_offsets[i + 1];
    }
    page_offsets[CACHE_SIZE - 1] = offset;
    cache_count = CACHE_SIZE;
    if (current_cache_valid && current_cache_pos > 0) {
        current_cache_pos--;
    }
    LOG_INFO("Cache evict oldest, add offset %d at idx %d", offset, CACHE_SIZE - 1);
    return CACHE_SIZE - 1;
}

static int cache_find_prev(int offset)
{
    int best_idx = -1;
    int best_val = -1;
    for (int i = 0; i < cache_count; i++) {
        if (page_offsets[i] < offset && page_offsets[i] > best_val) {
            best_val = page_offsets[i];
            best_idx = i;
        }
    }
    if (best_idx >= 0) {
        LOG_INFO("Cache find prev: offset %d -> idx %d (val %d)", offset, best_idx, best_val);
    } else {
        LOG_INFO("Cache find prev: no candidate for offset %d", offset);
    }
    return best_idx;
}

static int cache_find_next(int offset)
{
    int best_idx = -1;
    int best_val = file_size;
    for (int i = 0; i < cache_count; i++) {
        if (page_offsets[i] > offset && page_offsets[i] < best_val) {
            best_val = page_offsets[i];
            best_idx = i;
        }
    }
    if (best_idx >= 0) {
        LOG_INFO("Cache find next: offset %d -> idx %d (val %d)", offset, best_idx, best_val);
    } else {
        LOG_INFO("Cache find next: no candidate for offset %d", offset);
    }
    return best_idx;
}

/* ---------- UTF-8 辅助 ---------- */
static int utf8_char_len(unsigned char c)
{
    if (c < 0x80) return 1;
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    if ((c & 0xF8) == 0xF0) return 4;
    return 1;
}

/* ---------- 颜色方案 ---------- */
static void apply_color_scheme(void)
{
#if LCD_DEPTH > 1 /* 单色目标没有前景/背景色 API（见 apps/plugin.h:253） */
    if (night_mode) {
        rb->lcd_set_foreground(LCD_WHITE);
        rb->lcd_set_background(LCD_BLACK);
    } else {
        rb->lcd_set_foreground(LCD_BLACK);
        rb->lcd_set_background(LCD_WHITE);
    }
#endif
}

/* ---------- 设置读写 ---------- */
static void load_global_settings(void)
{
    int fd = rb->open(SETTINGS_FILE, O_RDONLY);
    if (fd < 0) {
        night_mode = false;
        LOG_INFO("No settings file, night_mode off");
        return;
    }
    char buf[64];
    ssize_t bytes = rb->read(fd, buf, sizeof(buf) - 1);
    rb->close(fd);
    if (bytes <= 0) {
        night_mode = false;
        LOG_INFO("Empty settings, night_mode off");
        return;
    }
    buf[bytes] = '\0';
    char *p = rb->strstr(buf, "NightMode=");
    if (p) {
        p += 10;
        night_mode = (*p == '1');
        LOG_INFO("Loaded night_mode = %d", night_mode ? 1 : 0);
    } else {
        night_mode = false;
        LOG_INFO("No NightMode entry, default off");
    }
}

static void save_global_settings(void)
{
    rb->mkdir("/ebook/config");
    int fd = rb->open(SETTINGS_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) {
        LOG_ERROR("Cannot save settings file");
        return;
    }
    char content[32];
    rb->snprintf(content, sizeof(content), "NightMode=%d\n", night_mode ? 1 : 0);
    rb->write(fd, content, rb->strlen(content));
    rb->close(fd);
    LOG_INFO("Saved night_mode = %d", night_mode ? 1 : 0);
}

/* ---------- 辅助函数 ---------- */
static const char* get_battery_icon(void)
{
    static char batt_str[10];
    int percent = rb->battery_level();
    rb->snprintf(batt_str, sizeof(batt_str), "[%d%%]=", percent);
    return batt_str;
}

static void format_filename_short(const char *full, char *out, size_t out_size)
{
    char buf[MAX_PATH];
    rb->strlcpy(buf, full, sizeof(buf));
    char *p = rb->strrchr(buf, '/');
    if (p) rb->strlcpy(buf, p + 1, sizeof(buf));
    char *dot = rb->strrchr(buf, '.');
    if (dot && rb->strcmp(dot, ".txt") == 0) *dot = '\0';
    if (rb->strlen(buf) > 6) {
        rb->strlcpy(out, buf, 7);
        rb->strlcat(out, "...", out_size);
    } else {
        rb->strlcpy(out, buf, out_size);
    }
}

static void get_full_display_name(const char *full, char *out, size_t out_size)
{
    char buf[MAX_PATH];
    rb->strlcpy(buf, full, sizeof(buf));
    char *p = rb->strrchr(buf, '/');
    if (p) rb->strlcpy(buf, p + 1, sizeof(buf));
    char *dot = rb->strrchr(buf, '.');
    if (dot && rb->strcmp(dot, ".txt") == 0) *dot = '\0';
    rb->strlcpy(out, buf, out_size);
}

static int get_file_size(int fd)
{
    off_t cur = rb->lseek(fd, 0, SEEK_CUR);
    off_t size = rb->lseek(fd, 0, SEEK_END);
    rb->lseek(fd, cur, SEEK_SET);
    return (int)size;
}

/* ============================================================
   核心渲染与邻页计算
   ============================================================ */

static int compute_prev_offset(int start_offset)
{
    if (start_offset <= 0) return -1;

    int fd = rb->open(current_file, O_RDONLY);
    if (fd < 0) {
        LOG_ERROR("compute_prev_offset: cannot open %s", current_file);
        return -1;
    }

    int search_offset = start_offset - 1;
    int newline_count = 0;
    int target_newlines = lines_per_screen;
    if (target_newlines < 5) target_newlines = 5;
    char ch;
    int found_offset = -1;

    while (search_offset > 0) {
        if (rb->lseek(fd, search_offset, SEEK_SET) < 0) break;
        ssize_t n = rb->read(fd, &ch, 1);
        if (n != 1) break;
        if (ch == '\n' || ch == '\r') {
            newline_count++;
            if (newline_count >= target_newlines) {
                int candidate = search_offset + 1;
                if (candidate < start_offset) {
                    while (candidate < start_offset) {
                        unsigned char c;
                        if (rb->lseek(fd, candidate, SEEK_SET) < 0) break;
                        if (rb->read(fd, &c, 1) != 1) break;
                        if ((c & 0xC0) != 0x80) break;
                        candidate++;
                    }
                    if (candidate < start_offset) {
                        found_offset = candidate;
                        break;
                    }
                }
                search_offset = search_offset - 1;
                newline_count = 0;
                continue;
            }
        }
        search_offset--;
    }
    rb->close(fd);

    if (found_offset == -1) found_offset = 0;
    LOG_INFO("compute_prev_offset: start=%d -> prev=%d (newlines=%d)", start_offset, found_offset, newline_count);
    return found_offset;
}

static int compute_next_offset(int start_offset)
{
    if (start_offset >= file_size) return -1;

    int fd = rb->open(current_file, O_RDONLY);
    if (fd < 0) {
        LOG_ERROR("compute_next_offset: cannot open %s", current_file);
        return -1;
    }

    rb->lseek(fd, start_offset, SEEK_SET);
    ssize_t bytes_read = rb->read(fd, render_buffer, render_buf_size - 1);
    if (bytes_read <= 0) {
        rb->close(fd);
        LOG_WARN("compute_next_offset: no data at offset %d", start_offset);
        return -1;
    }
    render_buffer[bytes_read] = '\0';

    int safe_start = 0;
    while (safe_start < bytes_read && ((unsigned char)render_buffer[safe_start] & 0xC0) == 0x80) {
        safe_start++;
    }
    if (safe_start >= bytes_read) {
        rb->close(fd);
        LOG_WARN("compute_next_offset: safe start beyond buffer");
        return -1;
    }

    char *p = render_buffer + safe_start;
    char *end_ptr = render_buffer + bytes_read;
    int line_y = 1;
    char line_buf[256];
    int line_buf_len = 0;
    int next_offset_candidate = start_offset + safe_start;

    while (line_y < lines_per_screen - 1 && p < end_ptr) {
        unsigned char c = (unsigned char)*p;
        if (c == '\n' || c == '\r') {
            line_y++;
            if (c == '\r' && *(p+1) == '\n') { p++; next_offset_candidate++; }
            else if (c == '\n' && *(p+1) == '\r') { p++; next_offset_candidate++; }
            p++;
            next_offset_candidate++;
            line_buf_len = 0;
            continue;
        }

        int char_bytes = utf8_char_len(c);
        if (p + char_bytes > end_ptr) break;

        char temp_buf[256];
        rb->memcpy(temp_buf, line_buf, line_buf_len);
        rb->memcpy(temp_buf + line_buf_len, p, char_bytes);
        temp_buf[line_buf_len + char_bytes] = '\0';
        int w_line, h_line;
        const char *measure = temp_buf;
        /* 居中标记不参与绘制，也就不能参与宽度计算 */
        if (line_buf_len >= 5 && rb->strncmp(line_buf, "<<C>>", 5) == 0)
            measure = temp_buf + 5;
        rb->font_getstringsize((const unsigned char *)measure, &w_line, &h_line, FONT_UI);

        if (w_line >= LCD_WIDTH) {
            if (line_buf_len > 0) {
                line_y++;
                if (line_y >= lines_per_screen - 1) break;
                line_buf_len = 0;
                continue;
            } else {
                if (line_buf_len + char_bytes < 250) {
                    rb->memcpy(line_buf + line_buf_len, p, char_bytes);
                    line_buf_len += char_bytes;
                }
                p += char_bytes;
                next_offset_candidate += char_bytes;
                continue;
            }
        }

        if (line_buf_len + char_bytes < 250) {
            rb->memcpy(line_buf + line_buf_len, p, char_bytes);
            line_buf_len += char_bytes;
        }
        p += char_bytes;
        next_offset_candidate += char_bytes;
    }

    rb->close(fd);

    if (next_offset_candidate > start_offset && next_offset_candidate < file_size) {
        LOG_INFO("compute_next_offset: start=%d -> next=%d", start_offset, next_offset_candidate);
        return next_offset_candidate;
    } else {
        LOG_INFO("compute_next_offset: start=%d -> next=%d (end or invalid)", start_offset, next_offset_candidate);
        return (next_offset_candidate < file_size) ? next_offset_candidate : -1;
    }
}

/* 当前页第一张图片在 EPUB 内的路径（空串表示本页没有图片） */
static char page_image_path[MAX_PATH] = "";
static char line_buf_placeholder[96];

/* 输出一行文本；遇到 EPUB 图片标记则记录路径并画占位符 */
static void emit_text_line(const char *text, int len, int y)
{
    /* 居中块（标题等）：转换阶段已加上 "<<C>>" 标记 */
    if (len > 5 && rb->strncmp(text, "<<C>>", 5) == 0) {
        const char *body = text + 5;
        int w = 0, h = 0;

        rb->font_getstringsize((const unsigned char *)body, &w, &h, FONT_UI);
        if (w > 0 && w <= LCD_WIDTH) {
            int x = (LCD_WIDTH - w) / 2;
            if (x < 0)
                x = 0;
            rb->lcd_putsxy(x, y * font_height, (const unsigned char *)body);
        } else {
            rb->lcd_puts(0, y, (const unsigned char *)body);   /* 太宽则不居中 */
        }
        return;
    }
    if (len > 7 && rb->strncmp(text, "<<IMG:", 6) == 0) {
        const char *body = text + 6;
        const char *q = body;
        const char *end = text + len;
        int plen;

        /* 找到结尾的 ">>" */
        while (q + 1 < end && !(q[0] == '>' && q[1] == '>'))
            q++;
        plen = (int)(q - body);
        if (plen > 0 && plen < (int)sizeof(page_image_path)) {
            if (page_image_path[0] == '\0') {   /* 只记本页第一张 */
                rb->memcpy(page_image_path, body, plen);
                page_image_path[plen] = '\0';
            }
            {
                /* 占位符带上文件名，方便知道是哪张图 */
                const char *slash = body + plen;
                while (slash > body && slash[-1] != '/')
                    slash--;
                rb->snprintf(line_buf_placeholder, sizeof(line_buf_placeholder),
                             "[图片: %.60s]", slash);
                rb->lcd_puts(0, y, (const unsigned char *)line_buf_placeholder);
            }
            return;
        }
    }
    rb->lcd_puts(0, y, (const unsigned char *)text);
}

static void render_page(int offset)
{
    LOG_INFO("render_page at offset %d", offset);

    int fd = rb->open(current_file, O_RDONLY);
    if (fd < 0) {
        LOG_ERROR("render_page: cannot open %s", current_file);
        return;
    }

    rb->lseek(fd, offset, SEEK_SET);
    ssize_t bytes_read = rb->read(fd, render_buffer, render_buf_size - 1);
    if (bytes_read <= 0) {
        rb->close(fd);
        LOG_WARN("render_page: no data at offset %d", offset);
        return;
    }
    render_buffer[bytes_read] = '\0';

    int safe_start = 0;
    while (safe_start < bytes_read && ((unsigned char)render_buffer[safe_start] & 0xC0) == 0x80) {
        safe_start++;
    }
    if (safe_start >= bytes_read) {
        rb->close(fd);
        LOG_WARN("render_page: safe start beyond buffer");
        return;
    }

    char *p = render_buffer + safe_start;
    char *end_ptr = render_buffer + bytes_read;

    rb->lcd_scroll_stop();
    apply_color_scheme();
    rb->lcd_clear_display();

    page_image_path[0] = '\0';

    long tick = *rb->current_tick;
    if (tick - last_toggle_tick >= HZ * 5) {
        last_toggle_tick = tick;
        show_mem = !show_mem;
    }
    char left_text[32];
    if (show_mem) {
        rb->snprintf(left_text, sizeof(left_text), "内存:%dKB", (int)(render_buf_size/1024));
    } else {
        format_filename_short(current_file, left_text, sizeof(left_text));
    }
    rb->lcd_puts(0, 0, (const unsigned char *)left_text);

    const char *batt = get_battery_icon();
    int w_batt, h;
    rb->font_getstringsize((const unsigned char *)batt, &w_batt, &h, FONT_UI);
    int max_x = LCD_WIDTH - w_batt;
    if (max_x >= 0) rb->lcd_puts(max_x / font_width, 0, (const unsigned char *)batt);

    int line_y = 1;
    char line_buf[256];
    int line_buf_len = 0;

    while (line_y < lines_per_screen - 1 && p < end_ptr) {
        unsigned char c = (unsigned char)*p;
        if (c == '\n' || c == '\r') {
            if (line_buf_len > 0) {
                line_buf[line_buf_len] = '\0';
                emit_text_line(line_buf, line_buf_len, line_y);
                line_buf_len = 0;
                line_y++;
            } else {
                line_y++;
            }
            if (c == '\r' && *(p+1) == '\n') { p++; }
            else if (c == '\n' && *(p+1) == '\r') { p++; }
            p++;
            continue;
        }

        int char_bytes = utf8_char_len(c);
        if (p + char_bytes > end_ptr) break;

        char temp_buf[256];
        rb->memcpy(temp_buf, line_buf, line_buf_len);
        rb->memcpy(temp_buf + line_buf_len, p, char_bytes);
        temp_buf[line_buf_len + char_bytes] = '\0';
        int w_line, h_line;
        const char *measure = temp_buf;
        /* 居中标记不参与绘制，也就不能参与宽度计算 */
        if (line_buf_len >= 5 && rb->strncmp(line_buf, "<<C>>", 5) == 0)
            measure = temp_buf + 5;
        rb->font_getstringsize((const unsigned char *)measure, &w_line, &h_line, FONT_UI);

        if (w_line >= LCD_WIDTH) {
            if (line_buf_len > 0) {
                line_buf[line_buf_len] = '\0';
                emit_text_line(line_buf, line_buf_len, line_y);
                line_buf_len = 0;
                line_y++;
                if (line_y >= lines_per_screen - 1) break;
                continue;
            } else {
                if (line_buf_len + char_bytes < 250) {
                    rb->memcpy(line_buf + line_buf_len, p, char_bytes);
                    line_buf_len += char_bytes;
                }
                p += char_bytes;
                continue;
            }
        }

        if (line_buf_len + char_bytes < 250) {
            rb->memcpy(line_buf + line_buf_len, p, char_bytes);
            line_buf_len += char_bytes;
        }
        p += char_bytes;
    }

    if (line_buf_len > 0) {
        line_buf[line_buf_len] = '\0';
        emit_text_line(line_buf, line_buf_len, line_y);
    }

    int percent = (file_size > 0) ? (offset * 100) / file_size : 0;
    char status_left[16], status_right[16];
    rb->snprintf(status_left, sizeof(status_left), "偏移:%d", offset);
    rb->snprintf(status_right, sizeof(status_right), "%d%%", percent);
    rb->lcd_puts(0, lines_per_screen - 1, (const unsigned char *)status_left);
    int sw, sh;
    rb->font_getstringsize((const unsigned char *)status_right, &sw, &sh, FONT_UI);
    int right_x = LCD_WIDTH - sw;
    if (right_x >= 0) rb->lcd_puts(right_x / font_width, lines_per_screen - 1, (const unsigned char *)status_right);

    rb->lcd_update();
    rb->close(fd);

    int idx = cache_add_offset(offset);
    if (idx >= 0) {
        current_cache_pos = idx;
        current_cache_valid = true;
    }
}

static void refresh_current_page(void)
{
    render_page(current_file_offset);
}

/* ---------- 翻页函数 ---------- */
static void go_next_page(void)
{
    LOG_INFO("go_next_page from offset %d", current_file_offset);

    int idx = cache_find_next(current_file_offset);
    if (idx >= 0) {
        int next_off = page_offsets[idx];
        if (next_off > current_file_offset && next_off < file_size) {
            current_file_offset = next_off;
            current_cache_pos = idx;
            current_cache_valid = true;
            render_page(current_file_offset);
            LOG_INFO("go_next_page: used cached next offset %d", current_file_offset);
            return;
        }
    }

    int next_off = compute_next_offset(current_file_offset);
    if (next_off < 0 || next_off >= file_size) {
        LOG_WARN("End of file reached at offset %d", current_file_offset);
        rb->splash(HZ, "已到文件末尾");
        return;
    }

    current_file_offset = next_off;
    cache_add_offset(current_file_offset);
    render_page(current_file_offset);
    LOG_INFO("go_next_page: computed next offset %d", current_file_offset);
}

static void go_prev_page(void)
{
    LOG_INFO("go_prev_page from offset %d", current_file_offset);

    int idx = cache_find_prev(current_file_offset);
    if (idx >= 0) {
        int prev_off = page_offsets[idx];
        if (prev_off < current_file_offset && prev_off >= 0) {
            current_file_offset = prev_off;
            current_cache_pos = idx;
            current_cache_valid = true;
            render_page(current_file_offset);
            LOG_INFO("go_prev_page: used cached prev offset %d", current_file_offset);
            return;
        }
    }

    int prev_off = compute_prev_offset(current_file_offset);
    if (prev_off < 0 || prev_off >= current_file_offset) {
        LOG_WARN("Start of file reached at offset %d", current_file_offset);
        rb->splash(HZ, "已到文件开头");
        return;
    }

    current_file_offset = prev_off;
    cache_add_offset(current_file_offset);
    render_page(current_file_offset);
    LOG_INFO("go_prev_page: computed prev offset %d", current_file_offset);
}

/* ---------- 章节扫描 ---------- */

/* 解析中文数字，p 指向数字起始，end 是行尾。
   成功返回 >=1 的数值，*bytes_out 为消耗字节数；无数字返回 0。 */
static int parse_chinese_number(const char *p, const char *end, int *bytes_out)
{
    int section = 0;
    int num = 0;
    int consumed = 0;

    while (p + 3 <= end) {
        const unsigned char *u = (const unsigned char *)p;
        int d = -1, unit = 0;

        if      (u[0] == 0xE4 && u[1] == 0xB8 && u[2] == 0x80) d = 1;
        else if (u[0] == 0xE4 && u[1] == 0xBA && u[2] == 0x8C) d = 2;
        else if (u[0] == 0xE4 && u[1] == 0xB8 && u[2] == 0x89) d = 3;
        else if (u[0] == 0xE5 && u[1] == 0x9B && u[2] == 0x9B) d = 4;
        else if (u[0] == 0xE4 && u[1] == 0xBA && u[2] == 0x94) d = 5;
        else if (u[0] == 0xE5 && u[1] == 0x85 && u[2] == 0xAD) d = 6;
        else if (u[0] == 0xE4 && u[1] == 0xB8 && u[2] == 0x83) d = 7;
        else if (u[0] == 0xE5 && u[1] == 0x85 && u[2] == 0xAB) d = 8;
        else if (u[0] == 0xE4 && u[1] == 0xB9 && u[2] == 0x9D) d = 9;
        else if (u[0] == 0xE5 && u[1] == 0x8D && u[2] == 0x81) unit = 10;
        else if (u[0] == 0xE7 && u[1] == 0x99 && u[2] == 0xBE) unit = 100;
        else if (u[0] == 0xE5 && u[1] == 0x8D && u[2] == 0x83) unit = 1000;
        else if (u[0] == 0xE9 && u[1] == 0x9B && u[2] == 0xB6) { p += 3; consumed += 3; continue; }
        else break;

        p += 3;
        consumed += 3;

        if (d > 0) {
            num = d;
        } else {
            if (num == 0) num = 1;
            section += num * unit;
            num = 0;
        }
    }

    *bytes_out = consumed;
    int total = section + num;
    return (total > 0) ? total : 0;
}

static int match_chapter_line(const char *line, int len,
                              char *title, size_t title_size, int *num_out)
{
    if (len <= 0 || len > 200) return 0;

    unsigned char last = (unsigned char)line[len - 1];
    if (last == '.' || last == '!' || last == '?' || last == ';' || last == ',') return 0;
    if (len >= 3) {
        const unsigned char *t = (const unsigned char *)line + len - 3;
        if (t[0] == 0xE3 && (t[1] == 0x80 || t[1] == 0x82)) return 0;
        if (t[0] == 0xEF && t[1] == 0xBC &&
            (t[2] == 0x81 || t[2] == 0x9F || t[2] == 0x9B || t[2] == 0x8C)) return 0;
    }

    int evidence_pos = -1, evidence_len = 0, num = -1;

    {
        const char *p = line;
        const char *end = line + len;
        while (p + 5 < end) {
            if ((unsigned char)p[0] == 0xE7 && (unsigned char)p[1] == 0xAC && (unsigned char)p[2] == 0xAC) {
                const char *q = p + 3;
                int value = 0, ok = 0;
                while (q < end && *q >= '0' && *q <= '9') {
                    value = value * 10 + (*q - '0');
                    q++; ok = 1;
                }
                if (!ok && q + 3 <= end) {
                    int cn_bytes = 0;
                    int cn_val = parse_chinese_number(q, end, &cn_bytes);
                    if (cn_val > 0) {
                        value = cn_val;
                        q += cn_bytes;
                        ok = 1;
                    }
                }
                if (ok && q + 3 <= end) {
                    if ((unsigned char)q[0] == 0xE7 && (unsigned char)q[1] == 0xAB && (unsigned char)q[2] == 0xA0) {
                        evidence_pos = (int)(p - line);
                        evidence_len = (int)(q + 3 - p);
                        num = value;
                        break;
                    }
                    if ((unsigned char)q[0] == 0xE5 && (unsigned char)q[1] == 0x9B && (unsigned char)q[2] == 0x9E) {
                        evidence_pos = (int)(p - line);
                        evidence_len = (int)(q + 3 - p);
                        num = value;
                        break;
                    }
                    if ((unsigned char)q[0] == 0xE8 && (unsigned char)q[1] == 0x8A && (unsigned char)q[2] == 0x82) {
                        evidence_pos = (int)(p - line);
                        evidence_len = (int)(q + 3 - p);
                        num = value;
                        break;
                    }
                    if ((unsigned char)q[0] == 0xE5 && (unsigned char)q[1] == 0x8D && (unsigned char)q[2] == 0xB7) {
                        evidence_pos = (int)(p - line);
                        evidence_len = (int)(q + 3 - p);
                        num = value;
                        break;
                    }
                    if ((unsigned char)q[0] == 0xE8 && (unsigned char)q[1] == 0xAF && (unsigned char)q[2] == 0x9D) {
                        evidence_pos = (int)(p - line);
                        evidence_len = (int)(q + 3 - p);
                        num = value;
                        break;
                    }
                    if ((unsigned char)q[0] == 0xE7 && (unsigned char)q[1] == 0xAF && (unsigned char)q[2] == 0x87) {
                        evidence_pos = (int)(p - line);
                        evidence_len = (int)(q + 3 - p);
                        num = value;
                        break;
                    }
                }
            }
            p++;
        }
    }

    if (evidence_pos < 0) {
        const char *p = line;
        const char *end = line + len;
        while (p + 7 <= end) {
            if ((*p == 'C' || *p == 'c') &&
                (p[1] == 'H' || p[1] == 'h') &&
                (p[2] == 'A' || p[2] == 'a') &&
                (p[3] == 'P' || p[3] == 'p') &&
                (p[4] == 'T' || p[4] == 't') &&
                (p[5] == 'E' || p[5] == 'e') &&
                (p[6] == 'R' || p[6] == 'r')) {
                const char *q = p + 7;
                while (q < end && (*q == ' ' || *q == '\t')) q++;
                int value = 0, ok = 0;
                while (q < end && *q >= '0' && *q <= '9') {
                    value = value * 10 + (*q - '0');
                    q++; ok = 1;
                }
                if (ok) {
                    evidence_pos = (int)(p - line);
                    evidence_len = (int)(q - p);
                    num = value;
                    break;
                }
            }
            p++;
        }
    }

    if (evidence_pos < 0) return 0;

    const char *after = line + evidence_pos + evidence_len;
    const char *line_end = line + len;
    while (after < line_end && (*after == ' ' || *after == '\t')) after++;
    if (after < line_end && (unsigned char)*after == ',') {
        if ((int)(line_end - after - 1) > 10) return 0;
    }

    int copy_len = len < (int)title_size - 1 ? len : (int)title_size - 1;
    while (copy_len > 0 && ((unsigned char)line[copy_len] & 0xC0) == 0x80) copy_len--;
    rb->memcpy(title, line, copy_len);
    title[copy_len] = '\0';
    if (num_out) *num_out = num;
    return 1;
}

static bool scan_chapters(void)
{
    int fd = rb->open(current_file, O_RDONLY);
    if (fd < 0) {
        LOG_ERROR("scan_chapters: cannot open %s", current_file);
        return false;
    }

    char *buf = render_buffer;
    size_t buf_size = render_buf_size;
    if (buf_size < 1024) { rb->close(fd); return false; }

    int carry = 0;
    int abs_base = 0;
    chapter_count = 0;

    while (1) {
        ssize_t n = rb->read(fd, buf + carry, buf_size - carry - 1);
        if (n <= 0) break;
        int total = carry + n;
        buf[total] = '\0';

        int line_begin = 0;
        for (int i = 0; i < total; i++) {
            if (buf[i] != '\n') continue;

            int line_len = i - line_begin;
            if (line_len > 0 && buf[line_begin + line_len - 1] == '\r') line_len--;

            int line_abs = abs_base + line_begin;

            char title[CHAP_TITLE_LEN];
            int num = -1;
            if (line_len > 0 &&
                match_chapter_line(buf + line_begin, line_len, title, sizeof(title), &num)) {
                if (chapter_count < MAX_CHAPTERS) {
                    chapter_offsets[chapter_count] = line_abs;
                    rb->strlcpy(chapter_titles[chapter_count], title, CHAP_TITLE_LEN);
                    chapter_numbers[chapter_count] = num;
                    LOG_INFO("Chapter %d: off=%d num=%d '%s'",
                             chapter_count, line_abs, num, title);
                    chapter_count++;
                }
            }

            line_begin = i + 1;
        }

        carry = total - line_begin;
        if (carry > 0) rb->memmove(buf, buf + line_begin, carry);
        abs_base += line_begin;
    }

    if (carry > 0) {
        int line_len = carry;
        if (line_len > 0 && buf[line_len - 1] == '\r') line_len--;
        char title[CHAP_TITLE_LEN];
        int num = -1;
        if (line_len > 0 &&
            match_chapter_line(buf, line_len, title, sizeof(title), &num)) {
            if (chapter_count < MAX_CHAPTERS) {
                chapter_offsets[chapter_count] = abs_base;
                rb->strlcpy(chapter_titles[chapter_count], title, CHAP_TITLE_LEN);
                chapter_numbers[chapter_count] = num;
                chapter_count++;
            }
        }
    }

    rb->close(fd);
    LOG_INFO("scan_chapters: %d chapters found", chapter_count);
    return chapter_count > 0;
}

static void jump_to_chapter(int idx)
{
    if (idx < 0 || idx >= chapter_count) return;
    LOG_INFO("jump_to_chapter %d -> offset %d", idx, chapter_offsets[idx]);
    current_file_offset = chapter_offsets[idx];
    cache_clear();
    render_page(current_file_offset);
}

/* ---------- MD5 与章节缓存 ---------- */

static int compute_file_md5(char *hex_out, size_t hex_size)
{
    if (hex_size < MD5_STRING_LENGTH + 1) return -1;

    int fd = rb->open(current_file, O_RDONLY);
    if (fd < 0) return -1;

    struct md5_s md5;
    InitMD5(&md5);

    ssize_t n;
    while ((n = rb->read(fd, render_buffer, render_buf_size)) > 0) {
        AddMD5(&md5, render_buffer, (size_t)n);
    }
    rb->close(fd);

    if (n < 0) return -1;

    EndMD5(&md5);
    psz_md5_hash(hex_out, &md5);
    return 0;
}

static bool load_chapter_cache(const char *cache_path, const char *expected_md5)
{
    int fd = rb->open(cache_path, O_RDONLY);
    if (fd < 0) return false;

    char *buf = render_buffer;
    size_t buf_size = render_buf_size;
    if (buf_size < 256) { rb->close(fd); return false; }

    ssize_t bytes = rb->read(fd, buf, buf_size - 1);
    rb->close(fd);
    if (bytes <= 0) return false;
    buf[bytes] = '\0';

    char *md5_line = rb->strstr(buf, "MD5=");
    if (!md5_line) return false;
    md5_line += 4;
    if (rb->strncmp(md5_line, expected_md5, MD5_HEX_LEN) != 0) {
        LOG_INFO("Chapter cache MD5 mismatch, rescan needed");
        return false;
    }

    chapter_count = 0;
    char *p = buf;
    while (p && *p && chapter_count < MAX_CHAPTERS) {
        char *line = rb->strstr(p, "\n");
        if (!line) break;
        line++;
        p = line;

        if (*line < '0' || *line > '9') continue;

        char *sep1 = rb->strchr(line, '|');
        if (!sep1) continue;
        char *sep2 = rb->strchr(sep1 + 1, '|');
        if (!sep2) continue;

        /* 手动解析，不修改原缓冲区，避免破坏下一轮的 strstr */
        int idx = 0;
        for (char *c = line; c < sep1 && *c >= '0' && *c <= '9'; c++) {
            idx = idx * 10 + (*c - '0');
        }

        int offset = 0;
        for (char *c = sep1 + 1; c < sep2 && *c >= '0' && *c <= '9'; c++) {
            offset = offset * 10 + (*c - '0');
        }

        char *title = sep2 + 1;
        int tlen = 0;
        while (title[tlen] && title[tlen] != '\n' && title[tlen] != '\r') tlen++;

        if (tlen > CHAP_TITLE_LEN - 1) {
            tlen = CHAP_TITLE_LEN - 1;
            while (tlen > 0 && ((unsigned char)title[tlen] & 0xC0) == 0x80) tlen--;
        }

        if (idx >= 1 && offset >= 0 && offset < file_size && tlen > 0) {
            chapter_offsets[chapter_count] = offset;
            rb->memcpy(chapter_titles[chapter_count], title, tlen);
            chapter_titles[chapter_count][tlen] = '\0';
            chapter_numbers[chapter_count] = idx;
            chapter_count++;
        }
    }

    LOG_INFO("Loaded %d chapters from cache", chapter_count);
    return chapter_count > 0;
}

static void save_chapter_cache(const char *cache_path, const char *md5_hex)
{
    rb->mkdir("/ebook/config");
    int fd = rb->open(cache_path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) {
        LOG_ERROR("Cannot write chapter cache %s", cache_path);
        return;
    }

    char header[128];
    int len = rb->snprintf(header, sizeof(header),
                           "[Chapters]\nMD5=%s\nCount=%d\n",
                           md5_hex, chapter_count);
    rb->write(fd, header, len);

    for (int i = 0; i < chapter_count; i++) {
        char line[CHAP_TITLE_LEN + 32];
        char safe_title[CHAP_TITLE_LEN];
        rb->strlcpy(safe_title, chapter_titles[i], sizeof(safe_title));
        for (char *c = safe_title; *c; c++) {
            if (*c == '|') *c = ' ';
        }
        int n = rb->snprintf(line, sizeof(line), "%d|%d|%s\n",
                             chapter_numbers[i] > 0 ? chapter_numbers[i] : (i + 1),
                             chapter_offsets[i],
                             safe_title);
        rb->write(fd, line, n);
    }
    rb->close(fd);
    LOG_INFO("Chapter cache saved: %d chapters, md5=%s", chapter_count, md5_hex);
}

static void load_or_scan_chapters(void)
{
    char base[MAX_PATH];
    rb->strlcpy(base, current_file, sizeof(base));
    char *p = rb->strrchr(base, '/');
    if (p) rb->strlcpy(base, p + 1, sizeof(base));
    char *dot = rb->strrchr(base, '.');
    if (dot && rb->strcmp(dot, ".txt") == 0) {
        rb->strlcpy(dot, "_chapters.cfg", sizeof(base) - (dot - base));
    } else {
        rb->strlcat(base, "_chapters.cfg", sizeof(base));
    }

    char cache_path[MAX_PATH];
    rb->snprintf(cache_path, sizeof(cache_path), "/ebook/config/%s", base);

    char md5_hex[MD5_HEX_LEN + 1];
    if (compute_file_md5(md5_hex, sizeof(md5_hex)) != 0) {
        LOG_WARN("MD5 compute failed, fallback to scan");
        scan_chapters();
        return;
    }
    LOG_INFO("Current txt MD5: %s", md5_hex);

    if (load_chapter_cache(cache_path, md5_hex)) {
        LOG_INFO("Chapter cache hit, skip scanning");
        return;
    }

    LOG_INFO("Chapter cache miss or stale, scanning");
    scan_chapters();
    if (chapter_count > 0) {
        save_chapter_cache(cache_path, md5_hex);
    }
}

/* ---------- 书签管理 ---------- */
static void load_bookmarks_from_file(void)
{
    char conf_path[MAX_PATH];
    char base[MAX_PATH];
    rb->strlcpy(base, current_file, sizeof(base));
    char *p = rb->strrchr(base, '/');
    if (p) rb->strlcpy(base, p + 1, sizeof(base));
    char *dot = rb->strrchr(base, '.');
    if (dot && rb->strcmp(dot, ".txt") == 0) rb->strlcpy(dot, ".conf", sizeof(base) - (dot - base));
    rb->snprintf(conf_path, sizeof(conf_path), "/ebook/config/%s", base);

    int fd = rb->open(conf_path, O_RDONLY);
    if (fd < 0) {
        for (int i = 0; i < MAX_BOOKMARKS; i++) bookmarks[i] = 0;
        LOG_INFO("No bookmark file for %s", current_file);
        return;
    }
    char buf[256];
    ssize_t bytes = rb->read(fd, buf, sizeof(buf) - 1);
    rb->close(fd);
    if (bytes <= 0) {
        for (int i = 0; i < MAX_BOOKMARKS; i++) bookmarks[i] = 0;
        LOG_INFO("Empty bookmark file for %s", current_file);
        return;
    }
    buf[bytes] = '\0';
    char *bm_ptr = rb->strstr(buf, "[Bookmarks:");
    if (bm_ptr) {
        bm_ptr += 11;
        for (int i = 0; i < MAX_BOOKMARKS; i++) {
            while (*bm_ptr == ' ' || *bm_ptr == ',' || *bm_ptr == ']' || *bm_ptr == '\n') bm_ptr++;
            if (*bm_ptr >= '0' && *bm_ptr <= '9') {
                bookmarks[i] = 0;
                while (*bm_ptr >= '0' && *bm_ptr <= '9') {
                    bookmarks[i] = bookmarks[i] * 10 + (*bm_ptr - '0');
                    bm_ptr++;
                }
            } else {
                bookmarks[i] = 0;
            }
        }
        LOG_INFO("Loaded bookmarks: %d,%d,%d,%d,%d",
                 bookmarks[0], bookmarks[1], bookmarks[2], bookmarks[3], bookmarks[4]);
    } else {
        for (int i = 0; i < MAX_BOOKMARKS; i++) bookmarks[i] = 0;
        LOG_WARN("No [Bookmarks:] entry in file");
    }
}

static void save_bookmarks_to_file(void)
{
    char conf_path[MAX_PATH];
    char base[MAX_PATH];
    rb->strlcpy(base, current_file, sizeof(base));
    char *p = rb->strrchr(base, '/');
    if (p) rb->strlcpy(base, p + 1, sizeof(base));
    char *dot = rb->strrchr(base, '.');
    if (dot && rb->strcmp(dot, ".txt") == 0) rb->strlcpy(dot, ".conf", sizeof(base) - (dot - base));
    rb->snprintf(conf_path, sizeof(conf_path), "/ebook/config/%s", base);

    rb->mkdir("/ebook/config");
    int fd = rb->open(conf_path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) {
        LOG_ERROR("Cannot save bookmarks to %s", conf_path);
        rb->splash(HZ, "无法保存书签");
        return;
    }
    char content[256];
    int len = rb->snprintf(content, sizeof(content),
        "{\n    [BOOKMARK CONFIG START];\n    [Bookmarks: %d", bookmarks[0]);
    for (int i = 1; i < MAX_BOOKMARKS; i++) {
        if (len >= (int)sizeof(content) - 10) break;
        len += rb->snprintf(content + len, sizeof(content) - len, ", %d", bookmarks[i]);
    }
    if (len < (int)sizeof(content) - 30) {
        rb->snprintf(content + len, sizeof(content) - len, "];\n    [BOOKMARK CONFIG END];\n");
    }
    rb->write(fd, content, rb->strlen(content));
    rb->close(fd);
    LOG_INFO("Saved bookmarks: %d,%d,%d,%d,%d",
             bookmarks[0], bookmarks[1], bookmarks[2], bookmarks[3], bookmarks[4]);
}

/* ---------- 菜单系统 ---------- */
typedef struct {
    const char *text;
    int (*func)(void *);
    void *arg;
} menu_item_t;

static int show_menu(const menu_item_t *items, int count, const char *title, int menu_level)
{
    int selected = 0;
    int button;

    while (1) {
        rb->lcd_scroll_stop();
        apply_color_scheme();
        rb->lcd_clear_display();
        if (title) rb->lcd_puts(0, 0, (const unsigned char *)title);
        int start_line = title ? 2 : 0;
        int max_display = lines_per_screen - start_line - 1;
        int scroll_offset = 0;
        if (count > max_display && selected >= max_display) {
            scroll_offset = selected - max_display + 1;
        }
        for (int i = 0; i < max_display && (i + scroll_offset) < count; i++) {
            int idx = i + scroll_offset;
            if (idx == selected) {
                rb->lcd_puts(0, start_line + i, (const unsigned char *)">");
                rb->lcd_puts(1, start_line + i, (const unsigned char *)items[idx].text);
            } else {
                rb->lcd_puts(0, start_line + i, (const unsigned char *)" ");
                rb->lcd_puts(1, start_line + i, (const unsigned char *)items[idx].text);
            }
        }
        rb->lcd_update();

        button = rb->button_get_w_tmo(HZ/2);
        if (button == BUTTON_LEFT) {
            if (selected > 0) selected--;
            else selected = count - 1;
        } else if (button == BUTTON_RIGHT) {
            if (selected < count - 1) selected++;
            else selected = 0;
        } else if (button == BUTTON_SELECT) {
            int ret = items[selected].func(items[selected].arg);
            if (ret == -1) return -1;
            if (ret == 1)  return 1;
            if (ret == -2) return -2;
            if (ret == -3) return -3;
        } else if (button == BUTTON_MENU) {
            if (menu_level == 0) {
                continue;
            } else {
                return -1;
            }
        }
    }
}

/* ---- 目录 ---- */
static int toc_func(void *arg)
{
    (void)arg;

    if (chapter_count == 0) {
        rb->splash(HZ, "未识别到章节");
        return -1;
    }

    int selected = 0;
    for (int i = 0; i < chapter_count; i++) {
        if (chapter_offsets[i] <= current_file_offset) {
            selected = i;
        } else {
            break;
        }
    }

    int mode = 0;
    int button;
    int last_wheel = -1;

    char input[8] = "";
    int cursor_pos = 0;
    const char *keys = "1234567890X";
    int key_len = rb->strlen(keys);

    while (1) {
        rb->lcd_scroll_stop();
        apply_color_scheme();
        rb->lcd_clear_display();

        if (mode == 0) {
            char hdr[32];
            rb->snprintf(hdr, sizeof(hdr), "目录 %d/%d", selected + 1, chapter_count);
            rb->lcd_puts(0, 0, (const unsigned char *)hdr);

            int max_display = lines_per_screen - 3;
            if (max_display < 1) max_display = 1;
            int start_idx = 0;
            if (selected >= max_display) start_idx = selected - max_display + 1;

            for (int i = 0; i < max_display && (start_idx + i) < chapter_count; i++) {
                int idx = start_idx + i;
                char line[CHAP_TITLE_LEN + 12];
                int num = chapter_numbers[idx];
                if (num < 0) num = idx + 1;
                rb->snprintf(line, sizeof(line), "%d.%s", num, chapter_titles[idx]);

                int y = i + 1;
                if (idx == selected) {
                    rb->lcd_puts(0, y, (const unsigned char *)">");
                    rb->lcd_puts_scroll(1, y, (const unsigned char *)line);
                } else {
                    rb->lcd_puts(0, y, (const unsigned char *)" ");
                    rb->lcd_puts(1, y, (const unsigned char *)line);
                }
            }

            rb->lcd_puts(0, lines_per_screen - 2, (const unsigned char *)"[滚轮/<>]选择 [SELECT]输入数字");
            rb->lcd_puts(0, lines_per_screen - 1, (const unsigned char *)"[PLAY]跳转 [MENU]返回");
            rb->lcd_update();

            button = rb->button_get_w_tmo(HZ/10);

#ifdef HAVE_WHEEL_POSITION
            {
                int wheel = rb->wheel_status();
                if (wheel >= 0 && last_wheel >= 0) {
                    int delta = wheel - last_wheel;
                    if (delta < -48) delta += 96;
                    else if (delta > 48) delta -= 96;
                    if (delta >= 2 || delta <= -2) {
                        int step = delta / 2;
                        selected += step;
                        if (selected < 0) selected = chapter_count - 1;
                        if (selected >= chapter_count) selected = 0;
                        LOG_INFO("TOC wheel delta=%d step=%d selected=%d", delta, step, selected);
                    }
                }
                last_wheel = wheel;
            }
#endif

#ifdef HAVE_WHEEL_POSITION
            if (button == BUTTON_SCROLL_BACK || button == BUTTON_SCROLL_FWD) {
                /* 已由 wheel_status 处理，忽略 */
            } else
#endif
            if (button == BUTTON_LEFT) {
                if (selected > 0) selected--;
                else selected = chapter_count - 1;
            } else if (button == BUTTON_RIGHT) {
                if (selected < chapter_count - 1) selected++;
                else selected = 0;
            } else if (button == BUTTON_SELECT) {
                mode = 1;
                input[0] = '\0';
                cursor_pos = 0;
                last_wheel = -1;
                LOG_INFO("TOC switch to input mode");
            } else if (button == BUTTON_PLAY) {
                LOG_INFO("TOC jump to chapter %d", selected);
                jump_to_chapter(selected);
                return -2;
            } else if (button == BUTTON_MENU) {
                LOG_INFO("TOC exit");
                return -1;
            }
        } else {
            rb->lcd_puts(0, 0, (const unsigned char *)"输入章节号:");
            char disp[24];
            rb->snprintf(disp, sizeof(disp), "输入: %s", input);
            rb->lcd_puts(0, 1, (const unsigned char *)disp);
            rb->snprintf(disp, sizeof(disp), "范围: 1-%d", chapter_count);
            rb->lcd_puts(0, 2, (const unsigned char *)disp);

            int line_y = 4;
            int col = 0;
            for (int i = 0; i < key_len; i++) {
                char ch[2] = {keys[i], '\0'};
                if (i == cursor_pos) {
                    char buf[4];
                    rb->snprintf(buf, sizeof(buf), "[%c]", keys[i]);
                    rb->lcd_puts(col, line_y, (const unsigned char *)buf);
                    col += rb->strlen(buf) + 1;
                } else {
                    rb->lcd_puts(col, line_y, (const unsigned char *)ch);
                    col += 2;
                }
                if (col >= LCD_WIDTH / font_width - 2) {
                    col = 0;
                    line_y++;
                }
            }

            rb->lcd_puts(0, lines_per_screen - 3, (const unsigned char *)"[<-->]移动 [SELECT]输入");
            rb->lcd_puts(0, lines_per_screen - 2, (const unsigned char *)"[PLAY]跳转 [MENU]返回列表");
            rb->lcd_update();

            button = rb->button_get_w_tmo(HZ/2);
            if (button == BUTTON_LEFT) {
                if (cursor_pos > 0) cursor_pos--;
                else cursor_pos = key_len - 1;
            } else if (button == BUTTON_RIGHT) {
                if (cursor_pos < key_len - 1) cursor_pos++;
                else cursor_pos = 0;
            } else if (button == BUTTON_SELECT) {
                char c = keys[cursor_pos];
                if (c == 'X') {
                    int len = rb->strlen(input);
                    if (len > 0) input[len - 1] = '\0';
                    LOG_INFO("TOC input backspace, now '%s'", input);
                } else {
                    if (rb->strlen(input) < 4) {
                        char digit[2] = {c, '\0'};
                        rb->strlcat(input, digit, sizeof(input));
                        LOG_INFO("TOC input append %c, now '%s'", c, input);
                    }
                }
            } else if (button == BUTTON_PLAY) {
                if (rb->strlen(input) == 0) {
                    rb->splash(HZ, "请输入章节号");
                    continue;
                }
                int target = rb->atoi(input);
                if (target < 1 || target > chapter_count) {
                    rb->splash(HZ, "章节号超出范围");
                    LOG_WARN("TOC target %d out of range [1,%d]", target, chapter_count);
                    continue;
                }
                LOG_INFO("TOC jump to chapter %d (input)", target);
                jump_to_chapter(target - 1);
                return -2;
            } else if (button == BUTTON_MENU) {
                mode = 0;
                last_wheel = -1;
                LOG_INFO("TOC back to list mode");
            }
        }
    }
}

/* ---- 菜单功能 ---- */
static int file_info_func(void *arg)
{
    (void)arg;
    char info[8][32];
    get_full_display_name(current_file, info[0], sizeof(info[0]));
    rb->snprintf(info[1], sizeof(info[1]), "大小: %d 字节 (%dKB)", file_size, file_size/1024);
    rb->snprintf(info[2], sizeof(info[2]), "偏移: %d", current_file_offset);
    int percent = (file_size > 0) ? (current_file_offset * 100) / file_size : 0;
    rb->snprintf(info[3], sizeof(info[3]), "进度: %d%%", percent);
    rb->snprintf(info[4], sizeof(info[4]), "缓存页: %d", cache_count);
    rb->snprintf(info[5], sizeof(info[5]), "章节: %d", chapter_count);
    rb->snprintf(info[6], sizeof(info[6]), "夜间模式: %s", night_mode ? "开" : "关");
    rb->snprintf(info[7], sizeof(info[7]), "版本: 9.0");

    int button;
    while (1) {
        rb->lcd_scroll_stop();
        apply_color_scheme();
        rb->lcd_clear_display();
        rb->lcd_puts(0, 0, (const unsigned char *)"文件信息");
        int y = 2;
        for (int i = 0; i < 8; i++) {
            rb->lcd_puts(0, y++, (const unsigned char *)info[i]);
        }
        rb->lcd_puts(0, y+1, (const unsigned char *)"[MENU] 返回");
        rb->lcd_update();
        button = rb->button_get_w_tmo(HZ);
        if (button == BUTTON_MENU) {
            LOG_INFO("File info: MENU pressed");
            return -1;
        }
    }
}

static int bookmark_list_func(void *arg)
{
    (void)arg;
    int selected = 0, button;
    while (1) {
        rb->lcd_scroll_stop();
        apply_color_scheme();
        rb->lcd_clear_display();
        rb->lcd_puts(0, 0, (const unsigned char *)"书签");
        int max_display = lines_per_screen - 3;
        int scroll_offset = 0;
        if (selected >= max_display) scroll_offset = selected - max_display + 1;
        for (int i = 0; i < max_display && (i + scroll_offset) < MAX_BOOKMARKS; i++) {
            int idx = i + scroll_offset;
            char text[20];
            if (bookmarks[idx] > 0) {
                rb->snprintf(text, sizeof(text), "%d: 偏移 %d", idx+1, bookmarks[idx]);
            } else {
                rb->snprintf(text, sizeof(text), "%d: [空]", idx+1);
            }
            if (idx == selected) {
                rb->lcd_puts(0, i+2, (const unsigned char *)">");
                rb->lcd_puts(1, i+2, (const unsigned char *)text);
            } else {
                rb->lcd_puts(0, i+2, (const unsigned char *)" ");
                rb->lcd_puts(1, i+2, (const unsigned char *)text);
            }
        }
        rb->lcd_update();

        button = rb->button_get_w_tmo(HZ/2);
        if (button == BUTTON_LEFT) {
            if (selected > 0) selected--;
            else selected = MAX_BOOKMARKS - 1;
        } else if (button == BUTTON_RIGHT) {
            if (selected < MAX_BOOKMARKS - 1) selected++;
            else selected = 0;
        } else if (button == BUTTON_SELECT) {
            int idx = selected;
            const char *ops[] = {"保存到此", "删除", "跳转", "返回"};
            int op_selected = 0, op_btn;
            while (1) {
                rb->lcd_scroll_stop();
                apply_color_scheme();
                rb->lcd_clear_display();
                rb->lcd_puts(0, 0, (const unsigned char *)"书签操作");
                char pos[20];
                if (bookmarks[idx] > 0) {
                    rb->snprintf(pos, sizeof(pos), "位置: %d", bookmarks[idx]);
                } else {
                    rb->snprintf(pos, sizeof(pos), "位置: [空]");
                }
                rb->lcd_puts(0, 2, (const unsigned char *)pos);
                for (int i = 0; i < 4; i++) {
                    if (i == op_selected) {
                        rb->lcd_puts(0, i+4, (const unsigned char *)">");
                        rb->lcd_puts(1, i+4, (const unsigned char *)ops[i]);
                    } else {
                        rb->lcd_puts(0, i+4, (const unsigned char *)" ");
                        rb->lcd_puts(1, i+4, (const unsigned char *)ops[i]);
                    }
                }
                rb->lcd_update();

                op_btn = rb->button_get_w_tmo(HZ/2);
                if (op_btn == BUTTON_LEFT) {
                    if (op_selected > 0) op_selected--;
                    else op_selected = 3;
                } else if (op_btn == BUTTON_RIGHT) {
                    if (op_selected < 3) op_selected++;
                    else op_selected = 0;
                } else if (op_btn == BUTTON_SELECT) {
                    if (op_selected == 0) {
                        bookmarks[idx] = current_file_offset;
                        save_bookmarks_to_file();
                        rb->splash(HZ, "已保存");
                        LOG_INFO("Bookmark %d saved at offset %d", idx, current_file_offset);
                        break;
                    } else if (op_selected == 1) {
                        bookmarks[idx] = 0;
                        save_bookmarks_to_file();
                        rb->splash(HZ, "已删除");
                        LOG_INFO("Bookmark %d deleted", idx);
                        break;
                    } else if (op_selected == 2) {
                        if (bookmarks[idx] > 0 && bookmarks[idx] < file_size) {
                            current_file_offset = bookmarks[idx];
                            cache_clear();
                            refresh_current_page();
                            LOG_INFO("Jumped to bookmark %d offset %d", idx, current_file_offset);
                            return -1;
                        } else {
                            rb->splash(HZ, "空书签");
                            LOG_WARN("Empty bookmark %d", idx);
                        }
                    } else {
                        break;
                    }
                } else if (op_btn == BUTTON_MENU) {
                    LOG_INFO("Bookmark op MENU");
                    return -1;
                }
            }
        } else if (button == BUTTON_MENU) {
            LOG_INFO("Bookmark list MENU");
            return -1;
        }
    }
}

static int jump_offset_func(void *arg)
{
    (void)arg;
    char input[16] = "";
    int cursor_pos = 0;
    const char *keys = "1234567890X";
    int key_len = rb->strlen(keys);
    int button;

    while (1) {
        rb->lcd_scroll_stop();
        apply_color_scheme();
        rb->lcd_clear_display();

        rb->lcd_puts(0, 0, (const unsigned char *)"跳转到偏移量:");
        char disp[20];
        rb->snprintf(disp, sizeof(disp), "输入: %s", input);
        rb->lcd_puts(0, 1, (const unsigned char *)disp);

        int line_y = 3;
        int col = 0;
        for (int i = 0; i < key_len; i++) {
            char ch[2] = {keys[i], '\0'};
            if (i == cursor_pos) {
                char buf[4];
                rb->snprintf(buf, sizeof(buf), "[%c]", keys[i]);
                rb->lcd_puts(col, line_y, (const unsigned char *)buf);
                col += rb->strlen(buf) + 1;
            } else {
                rb->lcd_puts(col, line_y, (const unsigned char *)ch);
                col += 2;
            }
            if (col >= LCD_WIDTH / font_width - 2) {
                col = 0;
                line_y++;
            }
        }

        rb->lcd_puts(0, lines_per_screen - 3, (const unsigned char *)"[<-->]移动 [SELECT]输入");
        rb->lcd_puts(0, lines_per_screen - 2, (const unsigned char *)"[PLAY]跳转 [MENU]取消");
        rb->lcd_update();

        button = rb->button_get_w_tmo(HZ/2);
        if (button == BUTTON_LEFT) {
            if (cursor_pos > 0) cursor_pos--;
            else cursor_pos = key_len - 1;
        } else if (button == BUTTON_RIGHT) {
            if (cursor_pos < key_len - 1) cursor_pos++;
            else cursor_pos = 0;
        } else if (button == BUTTON_SELECT) {
            char c = keys[cursor_pos];
            if (c == 'X') {
                int len = rb->strlen(input);
                if (len > 0) input[len-1] = '\0';
                LOG_INFO("Jump input backspace, now '%s'", input);
            } else {
                if (rb->strlen(input) < 7) {
                    char digit[2] = {c, '\0'};
                    rb->strlcat(input, digit, sizeof(input));
                    LOG_INFO("Jump input append %c, now '%s'", c, input);
                }
            }
        } else if (button == BUTTON_PLAY) {
            if (rb->strlen(input) == 0) {
                rb->splash(HZ, "请输入偏移量");
                LOG_WARN("Jump attempt with empty input");
                continue;
            }
            int target = rb->atoi(input);
            if (target < 0 || target >= file_size) {
                rb->splash(HZ, "偏移量超出范围");
                LOG_WARN("Jump target %d out of range [0,%d)", target, file_size);
                continue;
            }
            current_file_offset = target;
            cache_clear();
            refresh_current_page();
            LOG_INFO("Jumped to offset %d", target);
            return -2;
        } else if (button == BUTTON_MENU) {
            LOG_INFO("Jump cancel");
            return -1;
        }
    }
}

static int night_mode_func(void *arg)
{
    (void)arg;

    night_mode = !night_mode;
    save_global_settings();
    load_global_settings();
    apply_color_scheme();

    LOG_INFO("Night mode toggled to %d, request menu redraw", night_mode ? 1 : 0);
    return -3;
}

static int exit_func(void *arg)
{
    (void)arg;
    LOG_INFO("User selected Exit");
    return 1;
}

static int return_to_reading_func(void *arg)
{
    (void)arg;
    LOG_INFO("Return to reading selected");
    refresh_current_page();
    return -2;
}

static int show_main_menu_with_exit(void)
{
    while (1) {
        char night_text[20];
        rb->snprintf(night_text, sizeof(night_text),
                     "夜间模式 [%s]", night_mode ? "开" : "关");

        const menu_item_t items[] = {
            {"返回阅读",       return_to_reading_func, NULL},
            {"目录",           toc_func,               NULL},
            {"文件信息",       file_info_func,         NULL},
            {"书签",           bookmark_list_func,     NULL},
            {"跳转到偏移量",   jump_offset_func,       NULL},
            {night_text,       night_mode_func,        NULL},
            {"退出",           exit_func,              NULL}
        };

        int ret = show_menu(items, 7, "主菜单", 0);

        if (ret == 1) {
            return 1;
        } else if (ret == -2) {
            return -2;
        } else if (ret == -3) {
            continue;
        }
    }
}

/* ---------- 开屏画面 ---------- */
static void show_splash_screen(void)
{
#if LCD_DEPTH > 1 /* 单色目标没有前景/背景色 API（见 apps/plugin.h:253） */
    rb->lcd_set_foreground(LCD_BLACK);
    rb->lcd_set_background(LCD_WHITE);
#endif
    rb->lcd_clear_display();

#if defined(APPLE_IPODVIDEO) || defined(IPOD_VIDEO)
    const char *model_line = "For iPod Video";
#elif defined(IPOD_6G) || defined(IPOD_CLASSIC)
    const char *model_line = "For iPod Classic";
#elif defined(IPOD_COLOR)
    const char *model_line = "For iPod Color";
#elif defined(IPOD_4G)
    const char *model_line = "For iPod 4G";
#elif defined(IPOD_NANO)
    const char *model_line = "For iPod Nano";
#else
    const char *model_line = "For Rockbox";
#endif

    const char *lines[] = {
        "Lemon E-Book",
        model_line,
        "Ver.0.9Beta"
    };
    int num_lines = 3;

    int line_widths[3];
    int line_heights[3];
    int total_height = 0;

    for (int i = 0; i < num_lines; i++) {
        int w = 0, h = 0;
        rb->font_getstringsize((const unsigned char *)lines[i], &w, &h, FONT_UI);
        line_widths[i] = w;
        line_heights[i] = h;
        total_height += h;
    }

    int y = (LCD_HEIGHT - total_height) / 2;
    if (y < 0) y = 0;

    for (int i = 0; i < num_lines; i++) {
        int x = (LCD_WIDTH - line_widths[i]) / 2;
        if (x < 0) x = 0;
        rb->lcd_putsxy(x, y, (const unsigned char *)lines[i]);
        y += line_heights[i];
    }

    rb->lcd_update();

    long start_tick = *rb->current_tick;
    while (*rb->current_tick - start_tick < HZ * 2) {
        int btn = rb->button_get_w_tmo(HZ/10);
        if (btn != BUTTON_NONE) break;
    }
}

/* ---------- 打开书籍 ---------- */
/* ==================== EPUB 阅读支持 ====================
 *
 * 内存策略：EPUB 的解压工作区来自 plugin_get_buffer() 中
 * “渲染缓冲之后、文件列表之前”的那一段（见 plugin_start()），
 * 绝不触碰音乐缓冲区，因此播放中的音频缓冲区不会被抢占。
 *
 * 初次打开 .epub 时把它整本转成同名的 .txt 缓存，之后所有阅读、
 * 翻页、书签、目录功能都复用现有纯文本通路，风险最小。
 *
 * DEFLATE 复用 imageviewer 的 tiny inflate（zlib 许可，无 malloc）。
 */
#include "imageviewer/png/tinflate.c"

#define EPUB_LOCAL_SIG      0x04034b50UL
#define EPUB_CD_SIG         0x02014b50UL
#define EPUB_EOCD_SIG       0x06054b50UL
#define EPUB_MAX_ITEMS      128
#define EPUB_ID_LEN         40
#define EPUB_HREF_LEN       152
#define EPUB_MANIFEST_SIZE  (EPUB_MAX_ITEMS * (EPUB_ID_LEN + EPUB_HREF_LEN) + 256)

static unsigned char *epub_work = NULL;     /* 解压工作区（非音乐缓冲区） */
static size_t epub_work_size = 0;

typedef struct
{
    int fd;
    unsigned int cd_offset;
    unsigned int cd_count;
} epub_zip_t;

/* ---------- 小工具 ---------- */
static unsigned int epub_le16(const unsigned char *p)
{
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8);
}

static unsigned long epub_le32(const unsigned char *p)
{
    return (unsigned long)p[0] | ((unsigned long)p[1] << 8) |
           ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24);
}

static int epub_bytes_eq(const char *a, const char *b, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        if (a[i] != b[i])
            return 0;
    }
    return 1;
}

static int epub_ci_eq(const char *a, const char *b, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        char ca = a[i];
        char cb = b[i];
        if (ca >= 'A' && ca <= 'Z')
            ca += 32;
        if (cb >= 'A' && cb <= 'Z')
            cb += 32;
        if (ca != cb)
            return 0;
    }
    return 1;
}

static const char *epub_memchr(const char *s, int len, char c)
{
    int i;
    for (i = 0; i < len; i++) {
        if (s[i] == c)
            return s + i;
    }
    return NULL;
}

static const char *epub_memmem(const char *hay, int haylen, const char *needle)
{
    int nlen = rb->strlen(needle);
    int i;
    if (nlen <= 0 || haylen < nlen)
        return NULL;
    for (i = 0; i + nlen <= haylen; i++) {
        if (hay[i] == needle[0] && epub_bytes_eq(hay + i, needle, nlen))
            return hay + i;
    }
    return NULL;
}

static int epub_utf8_encode(unsigned long cp, unsigned char *out)
{
    if (cp < 0x80) {
        out[0] = (unsigned char)cp;
        return 1;
    }
    if (cp < 0x800) {
        out[0] = (unsigned char)(0xC0 | (cp >> 6));
        out[1] = (unsigned char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        out[0] = (unsigned char)(0xE0 | (cp >> 12));
        out[1] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (unsigned char)(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = (unsigned char)(0xF0 | (cp >> 18));
    out[1] = (unsigned char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (unsigned char)(0x80 | (cp & 0x3F));
    return 4;
}

/* ---------- 输出缓冲（避免逐字节写盘） ---------- */
#define EPUB_OUTBUF_SIZE 512
static char epub_outbuf[EPUB_OUTBUF_SIZE];
static int epub_outlen = 0;
static int epub_outfd = -1;

static void epub_flush(void)
{
    if (epub_outlen > 0 && epub_outfd >= 0) {
        rb->write(epub_outfd, epub_outbuf, epub_outlen);
        epub_outlen = 0;
    }
}

static void epub_put(const char *s, int n)
{
    while (n > 0) {
        int room = EPUB_OUTBUF_SIZE - epub_outlen;
        int take = (n < room) ? n : room;
        rb->memcpy(epub_outbuf + epub_outlen, s, take);
        epub_outlen += take;
        s += take;
        n -= take;
        if (epub_outlen >= EPUB_OUTBUF_SIZE)
            epub_flush();
    }
}

static void epub_puts(const char *s)
{
    epub_put(s, rb->strlen(s));
}

/* ---------- ZIP ---------- */
static bool epub_zip_open(epub_zip_t *z, int fd)
{
    unsigned char buf[1024];
    long fsize;
    int n, i;

    rb->lseek(fd, 0, SEEK_END);
    fsize = rb->lseek(fd, 0, SEEK_CUR);
    if (fsize < 22)
        return false;
    n = (fsize > (long)sizeof(buf)) ? (int)sizeof(buf) : (int)fsize;
    rb->lseek(fd, fsize - n, SEEK_SET);
    if (rb->read(fd, buf, n) != n)
        return false;
    for (i = n - 22; i >= 0; i--) {
        if (epub_le32(buf + i) == EPUB_EOCD_SIG) {
            z->fd = fd;
            z->cd_count = epub_le16(buf + i + 10);
            z->cd_offset = (unsigned int)epub_le32(buf + i + 16);
            if (z->cd_offset > 0 && z->cd_offset < (unsigned long)fsize && z->cd_count > 0)
                return true;
            return false;
        }
    }
    return false;
}

static bool epub_zip_find(const epub_zip_t *z, const char *name,
                          unsigned int *method, unsigned int *comp_size,
                          unsigned int *raw_size, unsigned int *data_off)
{
    unsigned char hdr[46];
    char nbuf[EPUB_HREF_LEN + 8];
    unsigned int i;
    unsigned long pos = z->cd_offset;
    int namelen = rb->strlen(name);

    for (i = 0; i < z->cd_count; i++) {
        unsigned int nl, el, cl;
        unsigned long lho;

        rb->lseek(z->fd, (off_t)pos, SEEK_SET);
        if (rb->read(z->fd, hdr, 46) != 46)
            return false;
        if (epub_le32(hdr) != EPUB_CD_SIG)
            return false;
        nl = epub_le16(hdr + 28);
        el = epub_le16(hdr + 30);
        cl = epub_le16(hdr + 32);
        lho = epub_le32(hdr + 42);

        if (nl < sizeof(nbuf)) {
            if (rb->read(z->fd, nbuf, (int)nl) != (int)nl)
                return false;
            nbuf[nl] = '\0';
            if ((int)nl == namelen && rb->strcmp(nbuf, name) == 0) {
                unsigned char lh[30];
                unsigned int lnl, lel;
                *method = epub_le16(hdr + 10);
                *comp_size = (unsigned int)epub_le32(hdr + 20);
                *raw_size = (unsigned int)epub_le32(hdr + 24);
                rb->lseek(z->fd, (off_t)lho, SEEK_SET);
                if (rb->read(z->fd, lh, 30) != 30)
                    return false;
                if (epub_le32(lh) != EPUB_LOCAL_SIG)
                    return false;
                lnl = epub_le16(lh + 26);
                lel = epub_le16(lh + 28);
                *data_off = (unsigned int)(lho + 30 + lnl + lel);
                return true;
            }
        }
        pos += 46 + nl + el + cl;
    }
    return false;
}

/* 解压一个条目。返回 0 成功，负值为错误码。 */
static int epub_zip_extract(const epub_zip_t *z, const char *name,
                            unsigned char *out, unsigned int out_cap,
                            unsigned char *scratch, unsigned int scratch_cap,
                            unsigned int *out_len)
{
    unsigned int method, comp, raw, off;

    *out_len = 0;
    if (!epub_zip_find(z, name, &method, &comp, &raw, &off))
        return -1;
    if (comp == 0 || raw == 0)
        return -2;
    if (comp > scratch_cap || raw > out_cap)
        return -2;
    rb->lseek(z->fd, (off_t)off, SEEK_SET);
    if (rb->read(z->fd, scratch, (int)comp) != (int)comp)
        return -3;
    if (method == 0) {
        rb->memcpy(out, scratch, comp);
        *out_len = comp;
        return 0;
    }
    if (method == 8) {
        unsigned int dst = raw;
        if (tinf_uncompress(out, &dst, scratch, comp) != TINF_OK)
            return -4;
        *out_len = dst;
        return 0;
    }
    return -5;
}

/* ---------- XML 属性 ---------- */
static bool epub_get_attr(const char *tagstart, const char *tagend,
                          const char *attr, char *out, int out_size)
{
    int alen = rb->strlen(attr);
    const char *p = tagstart;

    while (p + alen < tagend) {
        if (epub_bytes_eq(p, attr, alen) && p[alen] == '=') {
            const char *q = p + alen + 1;
            char quote = 0;
            int n = 0;
            if (q < tagend && (*q == '"' || *q == '\''))
                quote = *q++;
            while (q < tagend && n < out_size - 1) {
                if (quote) {
                    if (*q == quote)
                        break;
                } else if (*q == ' ' || *q == '>' || *q == '/') {
                    break;
                }
                out[n++] = *q++;
            }
            out[n] = '\0';
            return n > 0;
        }
        p++;
    }
    return false;
}

/* ---------- HTML 转纯文本 ---------- */
typedef struct
{
    const char *name;
    const char *utf8;
} epub_entity_t;

static const epub_entity_t epub_entities[] =
{
    { "amp", "&" }, { "lt", "<" }, { "gt", ">" }, { "quot", "\"" }, { "apos", "'" },
    { "nbsp", " " }, { "ensp", " " }, { "emsp", " " }, { "shy", "" },
    { "mdash", "\xe2\x80\x94" }, { "ndash", "\xe2\x80\x93" }, { "hellip", "\xe2\x80\xa6" },
    { "ldquo", "\xe2\x80\x9c" }, { "rdquo", "\xe2\x80\x9d" },
    { "lsquo", "\xe2\x80\x98" }, { "rsquo", "\xe2\x80\x99" },
    { "bull", "\xe2\x80\xa2" }, { "middot", "\xc2\xb7" },
    { "copy", "\xc2\xa9" }, { "reg", "\xc2\xae" }, { "trade", "\xe2\x84\xa2" },
    { "times", "\xc3\x97" }, { "divide", "\xc3\xb7" },
    { "laquo", "\xc2\xab" }, { "raquo", "\xc2\xbb" },
    { "deg", "\xc2\xb0" }, { "plusmn", "\xc2\xb1" },
    { NULL, NULL }
};

static const char *epub_entity_lookup(const char *name)
{
    int i;
    for (i = 0; epub_entities[i].name != NULL; i++) {
        if (rb->strcmp(epub_entities[i].name, name) == 0)
            return epub_entities[i].utf8;
    }
    return NULL;
}

static bool epub_tag_is_block(const char *name, int nlen)
{
    static const char *blocks[] =
    {
        "p", "div", "br", "h1", "h2", "h3", "h4", "h5", "h6", "li", "tr",
        "table", "section", "article", "blockquote", "title", "hr", "ul", "ol",
        "body", "html", "pre", "figure", "figcaption", "td", "th", "dd", "dt", NULL
    };
    int i;
    for (i = 0; blocks[i] != NULL; i++) {
        int l = rb->strlen(blocks[i]);
        if (l == nlen && epub_ci_eq(name, blocks[i], nlen))
            return true;
    }
    return false;
}

/* 取路径的目录部分（含结尾 '/'） */
static void epub_dirname(const char *path, char *out, int out_size)
{
    int i, last = -1;
    int len = rb->strlen(path);

    for (i = 0; i < len; i++) {
        if (path[i] == '/')
            last = i;
    }
    if (last < 0 || last + 2 > out_size) {
        out[0] = '\0';
        return;
    }
    rb->memcpy(out, path, last + 1);
    out[last + 1] = '\0';
}

/* 把相对 src 解析成 zip 内绝对路径，处理 "./" 与 "../" */
static void epub_resolve_rel(const char *base_dir, const char *rel, char *out, int out_size)
{
    char tmp[MAX_PATH * 2];
    int blen = rb->strlen(base_dir);
    int rlen = rb->strlen(rel);
    int i, j = 0;

    if (blen + rlen + 1 > (int)sizeof(tmp)) {
        out[0] = '\0';
        return;
    }
    rb->memcpy(tmp, base_dir, blen);
    rb->memcpy(tmp + blen, rel, rlen);
    rlen += blen;
    tmp[rlen] = '\0';

    for (i = 0; i < rlen; ) {
        if (tmp[i] == '.' && i + 1 < rlen && tmp[i + 1] == '/') {
            i += 2;
            continue;
        }
        if (tmp[i] == '.' && i + 2 < rlen && tmp[i + 1] == '.' && tmp[i + 2] == '/') {
            /* 先去掉结尾的 '/'，再去掉最后一段目录名，否则会残留上一级名字 */
            if (j > 0 && out[j - 1] == '/')
                j--;
            while (j > 0 && out[j - 1] != '/')
                j--;
            i += 3;
            continue;
        }
        if (j < out_size - 1)
            out[j++] = tmp[i];
        i++;
    }
    out[j] = '\0';
}

/* ---------- 极简 CSS：只处理块级元素的居中 ---------- */

#define EPUB_CSS_MAX 12
static char css_center_sel[EPUB_CSS_MAX][24];
static int  css_center_count = 0;

static bool epub_css_center_has(const char *sel, int sellen)
{
    int i;

    if (sellen <= 0)
        return false;
    for (i = 0; i < css_center_count; i++) {
        if ((int)rb->strlen(css_center_sel[i]) == sellen &&
            epub_bytes_eq(css_center_sel[i], sel, sellen))
            return true;
    }
    return false;
}

/* 扫 <style> 块，记下声明含 text-align:center 的选择器（取最后一个词） */
static void epub_css_scan(const char *buf, int len)
{
    const char *end = buf + len;
    const char *p = buf;

    css_center_count = 0;
    while (p < end && css_center_count < EPUB_CSS_MAX) {
        const char *lt = epub_memmem(p, (int)(end - p), "<style");
        const char *gt, *close, *brace, *rbrace;

        if (lt == NULL)
            break;
        gt = epub_memchr(lt, (int)(end - lt), '>');
        if (gt == NULL)
            break;
        gt++;
        close = epub_memmem(gt, (int)(end - gt), "</style");
        if (close == NULL)
            close = end;

        brace = gt;
        while (brace < close) {
            brace = epub_memchr(brace, (int)(close - brace), '{');
            if (brace == NULL)
                break;
            rbrace = epub_memchr(brace, (int)(close - brace), '}');
            if (rbrace == NULL)
                break;

            if (epub_memmem(brace, (int)(rbrace - brace), "text-align") != NULL &&
                epub_memmem(brace, (int)(rbrace - brace), "center") != NULL) {
                const char *w = brace;
                const char *wstart;
                int slen;

                while (w > gt && (w[-1] == ' ' || w[-1] == '\t' ||
                                  w[-1] == '\n' || w[-1] == '\r'))
                    w--;
                wstart = w;
                while (wstart > gt && wstart[-1] != ' ' && wstart[-1] != '\t' &&
                       wstart[-1] != '\n' && wstart[-1] != '\r' && wstart[-1] != ',' &&
                       wstart[-1] != '}' && wstart[-1] != ';' && wstart[-1] != '{')
                    wstart--;
                slen = (int)(w - wstart);

                /* 复合选择器 p.title 只保留类名；#id 无法匹配，忽略整条 */
                {
                    int k, cut = 0;
                    for (k = slen - 1; k > 0; k--) {
                        if (wstart[k - 1] == '.') {
                            cut = k;
                            break;
                        }
                    }
                    if (cut > 0) {
                        wstart += cut;
                        slen -= cut;
                    }
                }
                if (slen > 0 && wstart[0] != '#' &&
                    slen < (int)sizeof(css_center_sel[0])) {
                    rb->memcpy(css_center_sel[css_center_count], wstart, slen);
                    css_center_sel[css_center_count][slen] = '\0';
                    css_center_count++;
                }
            }
            brace = rbrace + 1;
        }
        p = (close < end) ? close : end;
        if (p < end)
            p++;
    }
}

static bool epub_tag_is_heading(const char *name, int nlen)
{
    return (nlen == 2 && name[0] == 'h' && name[1] >= '1' && name[1] <= '6');
}

/* 该标签是否居中：内联 style 优先，其次按标签名或 class 查样式表 */
static bool epub_center_for(const char *name, int nlen,
                            const char *tagstart, const char *tagend)
{
    char val[80];
    int slen;

    if (epub_get_attr(tagstart, tagend, "style", val, sizeof(val))) {
        slen = rb->strlen(val);
        if (epub_memmem(val, slen, "text-align") != NULL &&
            epub_memmem(val, slen, "center") != NULL)
            return true;
    }
    if (epub_css_center_has(name, nlen))
        return true;

    /* class="foo bar" 中任一个命中样式表即居中 */
    if (epub_get_attr(tagstart, tagend, "class", val, sizeof(val))) {
        const char *p = val;
        while (*p != '\0') {
            const char *q = p;
            while (*q != '\0' && *q != ' ' && *q != '\t')
                q++;
            if (epub_css_center_has(p, (int)(q - p)))
                return true;
            while (*q == ' ' || *q == '\t')
                q++;
            p = q;
        }
    }
    return false;
}

/* 开始新行：必要时先补上居中标记 */
static void epub_break_line(int *pending_nl, int center)
{
    if (*pending_nl) {
        epub_puts("\n");
        if (center)
            epub_puts("<<C>>");
        *pending_nl = 0;
    }
}

static void epub_html_to_text(const char *buf, int len, const char *chap_dir)
{
    int i = 0;
    bool skip = false;
    int pending_nl = 0;
    int cur_center = 0;         /* 当前块级元素是否居中 */

    epub_css_scan(buf, len);

    while (i < len) {
        if (buf[i] == '<') {
            int j = i + 1;
            bool closing = false;
            const char *name;
            const char *gt;
            int nlen;

            if (j < len && buf[j] == '/') {
                closing = true;
                j++;
            }
            name = buf + j;
            while (j < len && buf[j] != '>' && buf[j] != ' ' && buf[j] != '\t' &&
                   buf[j] != '\n' && buf[j] != '\r' && buf[j] != '/')
                j++;
            nlen = (int)(buf + j - name);

            /* 先定位标签结束，属性解析要用到它 */
            while (i < len && buf[i] != '>')
                i++;
            gt = (i < len) ? buf + i : buf + len;

            if (nlen == 6 && epub_ci_eq(name, "script", 6))
                skip = !closing;
            else if (nlen == 5 && epub_ci_eq(name, "style", 5))
                skip = !closing;
            else if (!skip && !closing && nlen == 3 && epub_ci_eq(name, "img", 3)) {
                char src[EPUB_HREF_LEN];
                if (epub_get_attr(name + nlen, gt, "src", src, sizeof(src))) {
                    char full[MAX_PATH * 2];
                    epub_resolve_rel(chap_dir, src, full, sizeof(full));
                    if (full[0] != '\0') {
                        epub_puts("\n<<IMG:");
                        epub_puts(full);
                        epub_puts(">>\n");
                    }
                }
            }
            else if (!skip && !closing && nlen == 5 && epub_ci_eq(name, "image", 5)) {
                /* SVG 内嵌位图 */
                char src[EPUB_HREF_LEN];
                if (epub_get_attr(name + nlen, gt, "xlink:href", src, sizeof(src)) ||
                    epub_get_attr(name + nlen, gt, "href", src, sizeof(src))) {
                    char full[MAX_PATH * 2];
                    epub_resolve_rel(chap_dir, src, full, sizeof(full));
                    if (full[0] != '\0') {
                        epub_puts("\n<<IMG:");
                        epub_puts(full);
                        epub_puts(">>\n");
                    }
                }
            }
            else if (!skip && !closing && epub_tag_is_block(name, nlen)) {
                pending_nl = 1;
                if (epub_tag_is_heading(name, nlen) &&
                    epub_center_for(name, nlen, name + nlen, gt))
                    cur_center = 1;
            }
            else if (!skip && closing && epub_tag_is_heading(name, nlen))
                cur_center = 0;

            if (i < len)
                i++;
            continue;
        }

        if (buf[i] == '&') {
            int j = i + 1;
            char ent[16];
            int n = 0;
            while (j < len && n < 15 && buf[j] != ';' && buf[j] != ' ' &&
                   buf[j] != '<' && buf[j] != '&' && buf[j] != '\n')
                ent[n++] = buf[j++];
            ent[n] = '\0';
            if (j < len && buf[j] == ';' && n > 0) {
                const char *rep = epub_entity_lookup(ent);
                if (rep != NULL) {
                    epub_break_line(&pending_nl, cur_center);
                    if (!skip)
                        epub_puts(rep);
                    i = j + 1;
                    continue;
                }
                if (ent[0] == '#' && n > 1) {
                    unsigned long v = 0;
                    int k;
                    bool valid = true;
                    if (ent[1] == 'x' || ent[1] == 'X') {
                        for (k = 2; k < n; k++) {
                            char ch = ent[k];
                            int d;
                            if (ch >= '0' && ch <= '9')
                                d = ch - '0';
                            else if (ch >= 'a' && ch <= 'f')
                                d = ch - 'a' + 10;
                            else if (ch >= 'A' && ch <= 'F')
                                d = ch - 'A' + 10;
                            else {
                                valid = false;
                                break;
                            }
                            v = v * 16 + (unsigned long)d;
                        }
                    } else {
                        for (k = 1; k < n; k++) {
                            if (ent[k] < '0' || ent[k] > '9') {
                                valid = false;
                                break;
                            }
                            v = v * 10 + (unsigned long)(ent[k] - '0');
                        }
                    }
                    if (valid && v > 0 && v < 0x110000UL) {
                        unsigned char ub[4];
                        int ul = epub_utf8_encode(v, ub);
                        epub_break_line(&pending_nl, cur_center);
                        if (!skip)
                            epub_put((const char *)ub, ul);
                        i = j + 1;
                        continue;
                    }
                }
            }
            epub_break_line(&pending_nl, cur_center);
            if (!skip)
                epub_put("&", 1);
            i++;
            continue;
        }

        if (!skip) {
            /* 折叠连续空白 */
            if (buf[i] == '\n' || buf[i] == '\r' || buf[i] == '\t') {
                epub_break_line(&pending_nl, cur_center);
                epub_put(" ", 1);
            } else {
                epub_break_line(&pending_nl, cur_center);
                epub_put(buf + i, 1);
            }
        }
        i++;
    }
    epub_puts("\n");
}

/* 从 <title> 或第一个 <h1>/<h2> 里取一个简短章节名 */
static void epub_extract_title(const char *buf, int len, char *out, int out_size)
{
    static const char *tags[] = { "title", "h1", "h2", "h3", NULL };
    int t;

    out[0] = '\0';
    for (t = 0; tags[t] != NULL && out[0] == '\0'; t++) {
        int tlen = rb->strlen(tags[t]);
        int pos = 0;

        while (pos < len) {
            const char *lt = epub_memchr(buf + pos, len - pos, '<');
            const char *gt;
            int after;
            if (lt == NULL)
                break;
            gt = epub_memchr(lt, (int)(buf + len - lt), '>');
            if (gt == NULL)
                break;
            if (gt - lt > tlen + 1 && epub_ci_eq(lt + 1, tags[t], tlen)) {
                const char *cp = gt + 1;
                int n = 0;
                while (cp < buf + len && n < out_size - 1) {
                    if (*cp == '<')
                        break;
                    if (*cp != '\n' && *cp != '\r' && *cp != '\t')
                        out[n++] = *cp;
                    cp++;
                }
                out[n] = '\0';
                break;
            }
            after = (int)(gt + 1 - buf);
            if (after <= pos)
                break;
            pos = after;
        }
    }
    if (out[0] == '\0')
        rb->strlcpy(out, "Chapter", out_size);
}

/* ---------- OPF / 导航 ---------- */
static void epub_join_path(const char *base, const char *rel, char *out, int out_size)
{
    int blen = 0;
    int i;
    int bl = rb->strlen(base);

    for (i = bl - 1; i >= 0; i--) {
        if (base[i] == '/') {
            blen = i + 1;
            break;
        }
    }
    if (blen + rb->strlen(rel) + 1 > out_size) {
        out[0] = '\0';
        return;
    }
    rb->memcpy(out, base, blen);
    rb->strlcpy(out + blen, rel, out_size - blen);
}

static int epub_parse_manifest(const char *opf, int opflen, char *ids, char *hrefs)
{
    int count = 0;
    const char *p = opf;
    const char *end = opf + opflen;

    while (p < end && count < EPUB_MAX_ITEMS) {
        const char *lt = epub_memchr(p, (int)(end - p), '<');
        const char *gt;
        if (lt == NULL)
            break;
        gt = epub_memchr(lt, (int)(end - lt), '>');
        if (gt == NULL)
            break;
        if (gt - lt > 5 && epub_ci_eq(lt + 1, "item", 4) &&
            (lt[5] == ' ' || lt[5] == '\t' || lt[5] == '\n' || lt[5] == '\r' || lt[5] == '/')) {
            char id[EPUB_ID_LEN];
            char href[EPUB_HREF_LEN];
            if (epub_get_attr(lt, gt, "id", id, sizeof(id)) &&
                epub_get_attr(lt, gt, "href", href, sizeof(href))) {
                rb->strlcpy(ids + count * EPUB_ID_LEN, id, EPUB_ID_LEN);
                rb->strlcpy(hrefs + count * EPUB_HREF_LEN, href, EPUB_HREF_LEN);
                count++;
            }
        }
        p = gt + 1;
    }
    return count;
}

static int epub_parse_spine(const char *opf, int opflen, const char *ids, int idcount,
                            unsigned char *order, int max)
{
    int count = 0;
    const char *p = opf;
    const char *end = opf + opflen;

    while (p < end && count < max) {
        const char *lt = epub_memchr(p, (int)(end - p), '<');
        const char *gt;
        if (lt == NULL)
            break;
        gt = epub_memchr(lt, (int)(end - lt), '>');
        if (gt == NULL)
            break;
        if (gt - lt > 8 && epub_ci_eq(lt + 1, "itemref", 7)) {
            char idref[EPUB_ID_LEN];
            if (epub_get_attr(lt, gt, "idref", idref, sizeof(idref))) {
                int k;
                for (k = 0; k < idcount; k++) {
                    if (rb->strcmp(ids + k * EPUB_ID_LEN, idref) == 0) {
                        order[count++] = (unsigned char)k;
                        break;
                    }
                }
            }
        }
        p = gt + 1;
    }
    return count;
}

/* ---------- 缓存路径 ---------- */
static bool epub_is_epub(const char *path)
{
    int len = rb->strlen(path);
    return len > 5 && epub_ci_eq(path + len - 5, ".epub", 5);
}

static void epub_cache_path(const char *epub, char *out, int out_size)
{
    int len;
    rb->strlcpy(out, epub, out_size);
    len = rb->strlen(out);
    if (len > 5 && epub_ci_eq(out + len - 5, ".epub", 5))
        rb->strlcpy(out + len - 5, ".txt", out_size - (len - 5));
}

/* ---------- 整本转换 ---------- */
static bool epub_convert(const char *epub_path, const char *out_path)
{
    epub_zip_t z;
    int fd = -1;
    int out_fd = -1;
    char *ids;
    char *hrefs;
    unsigned char *order;
    unsigned char *zbuf;
    unsigned char *obuf;
    unsigned int zcap, ocap;
    unsigned int olen = 0;
    int idcount, spcount, i;
    char opf_path[EPUB_HREF_LEN];
    char chap_path[MAX_PATH * 2];
    bool ok = false;

    if (epub_work == NULL || epub_work_size < EPUB_MANIFEST_SIZE + 32768) {
        LOG_ERROR("EPUB: workspace too small (%d bytes)", (int)epub_work_size);
        rb->splash(HZ, "内存不足，无法解析 EPUB");
        return false;
    }

    ids = (char *)epub_work;
    hrefs = ids + EPUB_MAX_ITEMS * EPUB_ID_LEN;
    order = (unsigned char *)(hrefs + EPUB_MAX_ITEMS * EPUB_HREF_LEN);
    zbuf = epub_work + EPUB_MANIFEST_SIZE;
    zcap = (unsigned int)((epub_work_size - EPUB_MANIFEST_SIZE) / 2);
    obuf = zbuf + zcap;
    ocap = (unsigned int)(epub_work_size - EPUB_MANIFEST_SIZE - zcap);

    fd = rb->open(epub_path, O_RDONLY);
    if (fd < 0) {
        LOG_ERROR("EPUB: cannot open %s", epub_path);
        return false;
    }
    if (!epub_zip_open(&z, fd)) {
        LOG_ERROR("EPUB: not a valid zip");
        goto done;
    }

    /* 1) META-INF/container.xml -> OPF 路径 */
    if (epub_zip_extract(&z, "META-INF/container.xml", obuf, ocap, zbuf, zcap, &olen) != 0) {
        LOG_ERROR("EPUB: container.xml missing");
        goto done;
    }
    opf_path[0] = '\0';
    {
        const char *p = epub_memmem((const char *)obuf, (int)olen, "full-path");
        const char *end = (const char *)obuf + olen;
        const char *lt;
        const char *gt;
        /* 不能搜 "rootfile"：它会先命中容器标签 <rootfiles> */
        if (p != NULL) {
            lt = p;
            while (lt > (const char *)obuf && *lt != '<')
                lt--;
            gt = epub_memchr(p, (int)(end - p), '>');
            if (gt != NULL)
                epub_get_attr(lt, gt, "full-path", opf_path, sizeof(opf_path));
        }
    }
    if (opf_path[0] == '\0') {
        LOG_ERROR("EPUB: cannot locate OPF");
        goto done;
    }
    LOG_INFO("EPUB OPF: %s", opf_path);

    /* 2) OPF -> manifest + spine */
    if (epub_zip_extract(&z, opf_path, obuf, ocap, zbuf, zcap, &olen) != 0) {
        LOG_ERROR("EPUB: cannot read OPF");
        goto done;
    }
    idcount = epub_parse_manifest((const char *)obuf, (int)olen, ids, hrefs);
    spcount = epub_parse_spine((const char *)obuf, (int)olen, ids, idcount, order, EPUB_MAX_ITEMS);
    LOG_INFO("EPUB: %d manifest items, %d spine refs", idcount, spcount);
    if (spcount <= 0) {
        rb->splash(HZ, "EPUB 目录为空");
        goto done;
    }

    /* 3) 逐章解压成纯文本 */
    out_fd = rb->open(out_path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (out_fd < 0) {
        LOG_ERROR("EPUB: cannot create %s", out_path);
        goto done;
    }
    epub_outfd = out_fd;
    epub_outlen = 0;

    for (i = 0; i < spcount; i++) {
        int idx = order[i];
        char title[64];
        char chap_dir[EPUB_HREF_LEN];

        epub_join_path(opf_path, hrefs + idx * EPUB_HREF_LEN, chap_path, sizeof(chap_path));
        if (chap_path[0] == '\0')
            continue;
        /* 图片 src 相对于章节文件所在目录，而不是 OPF */
        epub_dirname(chap_path, chap_dir, sizeof(chap_dir));
        if (epub_zip_extract(&z, chap_path, obuf, ocap, zbuf, zcap, &olen) != 0) {
            LOG_WARN("EPUB: skip %s", chap_path);
            continue;
        }
        epub_extract_title((const char *)obuf, (int)olen, title, sizeof(title));
        epub_puts("\n\n");
        epub_puts(title);
        epub_puts("\n\n");
        epub_html_to_text((const char *)obuf, (int)olen, chap_dir);

        if ((i & 3) == 0) {
            rb->lcd_clear_display();
            rb->lcd_putsxy(4, 4, "Converting EPUB...");
            rb->lcd_update();
        }
    }
    epub_flush();
    ok = true;
    LOG_INFO("EPUB: converted %d chapters", spcount);

  done:
    if (out_fd >= 0)
        rb->close(out_fd);
    epub_outfd = -1;
    epub_outlen = 0;
    if (fd >= 0)
        rb->close(fd);
    return ok;
}

/* 由 .txt 缓存路径推出同名 .epub */
static bool epub_sibling_path(char *out, int out_size)
{
    int len = rb->strlen(current_file);

    if (len < 5 || len + 1 > out_size)
        return false;
    if (rb->strcmp(current_file + len - 4, ".txt") != 0)
        return false;
    rb->strcpy(out, current_file);
    rb->strcpy(out + len - 4, ".epub");
    return true;
}

/* ---------- EPUB 内嵌图片 ---------- */

/* decode_jpeg_mem() 来自 apps/plugins/lib/pluginlib_jpeg_mem.c，随 libplugin.a
 * 链进每个 .rock。它在 IDCT 阶段就缩放到目标尺寸，所以 1600x1200 的照片
 * 解码到屏幕大小只要几十 KB——比先整幅解码再缩放省得多，也让“图片比插件
 * 缓冲区大”不再是问题。用法参照 apps/plugins/test_mem_jpeg.c。 */
#include "lib/jpeg_mem.h"
#if LCD_DEPTH < 8
#include "lib/grey.h"
GREY_INFO_STRUCT
#define EPUB_CFORMAT (&format_grey)
#else
#define EPUB_CFORMAT (&format_native)
#endif

/* 解码并全屏显示一张图片；work 是可用工作区（可能借自音频缓冲区） */
static void show_image_blob(const unsigned char *blob, int len,
                            unsigned char *work, size_t work_size)
{
    struct bitmap bm;
    int ret, x, y;

    /* 目前只认 JPEG（EPUB 里占绝大多数） */
    if (len < 4 || blob[0] != 0xFF || blob[1] != 0xD8) {
        rb->splash(HZ, "暂不支持该图片格式（目前仅 JPEG）");
        return;
    }

#if LCD_DEPTH < 8
    {   /* 灰度屏（如 iPod 4G 的 2bpp）走 greylib，抖动后观感好得多 */
        long greysize = 0;
        if (!grey_init(work, (long)work_size, GREY_ON_COP,
                       LCD_WIDTH, LCD_HEIGHT, &greysize)) {
            rb->splash(HZ, "灰度缓冲不足");
            return;
        }
        work += greysize;
        work_size -= (size_t)greysize;
    }
#endif

    if (work_size < 512) {
        rb->splash(HZ, "内存不足，无法解码此图片");
        goto done;
    }

    bm.width = LCD_WIDTH;
    bm.height = LCD_HEIGHT;
    bm.data = work;

    rb->splash(HZ / 4, "解码中...");
#ifdef HAVE_ADJUSTABLE_CPU_FREQ
    rb->cpu_boost(true);
#endif
    ret = decode_jpeg_mem((unsigned char *)blob, (unsigned long)len, &bm,
                          (int)work_size,
                          FORMAT_NATIVE | FORMAT_RESIZE | FORMAT_KEEP_ASPECT,
                          EPUB_CFORMAT);
#ifdef HAVE_ADJUSTABLE_CPU_FREQ
    rb->cpu_boost(false);
#endif
    if (ret < 1) {
        rb->splash(HZ, "解码失败 (%d)", ret);
        goto done;
    }

    x = (LCD_WIDTH - bm.width) / 2;
    y = (LCD_HEIGHT - bm.height) / 2;
    if (x < 0)
        x = 0;
    if (y < 0)
        y = 0;

    rb->lcd_clear_display();
#if LCD_DEPTH < 8
    grey_show(true);
    grey_ub_gray_bitmap((const unsigned char *)bm.data, x, y, bm.width, bm.height);
#else
    rb->lcd_bitmap((fb_data *)bm.data, x, y, bm.width, bm.height);
#endif
    rb->lcd_update();

    /* 按任意键返回阅读；灰度模式下由 ISR 持续刷新缓冲区 */
    rb->button_get(true);

done:
    rb->lcd_scroll_stop();
#if LCD_DEPTH < 8
    grey_show(false);
    grey_release();
#endif
    rb->button_clear_queue();
}

/* 查看当前页的图片：解压到缓冲区后交给 show_image_blob() */
static void view_page_image(void)
{
    char epub_path[MAX_PATH];
    char img_name[MAX_PATH];
    epub_zip_t z;
    int fd;
    unsigned int method = 0, comp = 0, raw = 0, off = 0;
    unsigned char *out = NULL;
    unsigned char *scratch = NULL;
    unsigned int out_cap = 0, scratch_cap = 0, out_len = 0;
    unsigned char *abuf = NULL;
    size_t abuf_size = 0;
    bool borrowed = false;
    int rc;

    if (page_image_path[0] == '\0')
        return;

    rb->strncpy(img_name, page_image_path, sizeof(img_name) - 1);
    img_name[sizeof(img_name) - 1] = '\0';

    if (!epub_sibling_path(epub_path, sizeof(epub_path))) {
        rb->splash(HZ, "该文件没有对应 EPUB");
        return;
    }

    fd = rb->open(epub_path, O_RDONLY);
    if (fd < 0) {
        rb->splash(HZ, "无法打开 EPUB");
        return;
    }
    if (!epub_zip_open(&z, fd)) {
        rb->close(fd);
        rb->splash(HZ, "EPUB 索引损坏");
        return;
    }
    if (!epub_zip_find(&z, img_name, &method, &comp, &raw, &off) || comp == 0 || raw == 0) {
        rb->close(fd);
        rb->splash(HZ, "图片不存在");
        return;
    }

    /* 优先用 EPUB 工作区，不够再借音频缓冲区（用完立刻归还） */
    if (epub_work != NULL && epub_work_size >= (size_t)raw + (size_t)comp) {
        out = epub_work;
        out_cap = raw;
        scratch = epub_work + raw;
        scratch_cap = (unsigned int)(epub_work_size - raw);
    } else {
        rb->splash(HZ, "解压图片...");
        abuf = (unsigned char *)rb->plugin_get_audio_buffer(&abuf_size);
        if (abuf == NULL || abuf_size < (size_t)raw + (size_t)comp) {
            if (abuf != NULL)
                rb->plugin_release_audio_buffer();
            rb->close(fd);
            rb->splash(HZ, "内存不足，无法显示此图片");
            return;
        }
        borrowed = true;
        out = abuf;
        out_cap = raw;
        scratch = abuf + raw;
        scratch_cap = (unsigned int)(abuf_size - raw);
    }

    rc = epub_zip_extract(&z, img_name, out, out_cap, scratch, scratch_cap, &out_len);
    rb->close(fd);

    if (rc != 0) {
        if (borrowed)
            rb->plugin_release_audio_buffer();
        rb->splash(HZ, "解压失败 (%d)", rc);
        return;
    }

    LOG_INFO("image %s: %d bytes (method %u)", img_name, (int)out_len, method);

    /* 数据和缓冲区在查看期间保持有效；scratch 在解压后已空闲，
     * 正好当作解码输出缓冲（避免二次索取音频缓冲区） */
    show_image_blob(out, (int)out_len, scratch, scratch_cap);

    if (borrowed)
        rb->plugin_release_audio_buffer();
}

static bool epub_prepare(const char *epub_path, char *out_path, int out_size)
{
    int fd;

    epub_cache_path(epub_path, out_path, out_size);
    fd = rb->open(out_path, O_RDONLY);
    if (fd >= 0) {
        long sz;
        rb->lseek(fd, 0, SEEK_END);
        sz = rb->lseek(fd, 0, SEEK_CUR);
        rb->close(fd);
        if (sz > 0) {
            LOG_INFO("EPUB: using cache %s (%ld bytes)", out_path, sz);
            return true;
        }
    }
    rb->splash(0, "正在转换 EPUB...");
    if (!epub_convert(epub_path, out_path)) {
        rb->splash(HZ, "EPUB 转换失败");
        return false;
    }
    return true;
}


static bool open_book(const char *filename)
{
    char book_path[MAX_PATH];

    if (epub_is_epub(filename)) {
        if (!epub_prepare(filename, book_path, sizeof(book_path)))
            return false;
        LOG_INFO("EPUB %s converted to %s", filename, book_path);
        filename = book_path;
    }
    LOG_INFO("Opening book %s", filename);
    rb->strlcpy(current_file, filename, sizeof(current_file));
    int fd = rb->open(current_file, O_RDONLY);
    if (fd < 0) {
        LOG_ERROR("Cannot open file %s", filename);
        rb->splash(HZ, "无法打开文件");
        return false;
    }
    file_size = get_file_size(fd);
    rb->close(fd);
    if (file_size <= 0) {
        LOG_ERROR("Empty file %s", filename);
        rb->splash(HZ, "空文件");
        return false;
    }
    LOG_INFO("File size: %d bytes", file_size);

    load_bookmarks_from_file();
    int start_offset = 0;
    for (int i = 0; i < MAX_BOOKMARKS; i++) {
        if (bookmarks[i] > 0 && bookmarks[i] < file_size) {
            start_offset = bookmarks[i];
            LOG_INFO("Using bookmark %d offset %d", i, start_offset);
            break;
        }
    }
    current_file_offset = start_offset;
    cache_clear();

    load_or_scan_chapters();
    refresh_current_page();

    return true;
}

/* ---------- 文件浏览器 ---------- */
/* 文件名列表不能放栈上（主线程栈仅 8 KiB，128×MAX_PATH 会溢出），
 * 也不宜放进 BSS（BSS 计入插件占用、会挤占 plugin_get_buffer() 的可用量），
 * 因此由 plugin_start() 从插件缓冲区尾部划出一块专门存放。 */
#define MAX_FILES       128
static char *file_list = NULL;      /* 区域起点，每条 MAX_PATH 字节 */
static int   max_files = 0;         /* 实际可容纳的条目数 */
#define FILE_ENTRY(i)   (file_list + (size_t)(i) * MAX_PATH)

static int file_browser(void)
{
    struct dirent *entry;
    DIR *dir;
    int num_files = 0, selected = 0;
    int prev_selected = -1;
    int last_wheel = -1;

    if (max_files <= 0) {
        LOG_ERROR("No memory for file list");
        rb->splash(HZ, "内存不足");
        return PLUGIN_ERROR;
    }

    LOG_INFO("File browser started, scanning /ebook");
    dir = rb->opendir("/ebook");
    if (!dir) {
        LOG_ERROR("/ebook directory not found");
        rb->splash(HZ, "未找到 /ebook 目录");
        return PLUGIN_ERROR;
    }
    while ((entry = rb->readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;
        int len = rb->strlen(entry->d_name);
        if ((len > 4 && epub_ci_eq(entry->d_name + len - 4, ".txt", 4)) ||
            (len > 5 && epub_ci_eq(entry->d_name + len - 5, ".epub", 5))) {
            if (num_files < max_files) {
                rb->snprintf(FILE_ENTRY(num_files), MAX_PATH, "/ebook/%s", entry->d_name);
                LOG_INFO("Found file: %s", entry->d_name);
                num_files++;
            }
        }
    }
    rb->closedir(dir);
    if (num_files == 0) {
        LOG_WARN("No .txt files found");
        rb->splash(HZ, "没有 .txt 文件");
        return PLUGIN_ERROR;
    }
    LOG_INFO("Total %d files found", num_files);

    bool exit_plugin = false;
    int button;
    while (!exit_plugin) {
        if (selected != prev_selected) {
            rb->lcd_scroll_stop();
            prev_selected = selected;
        }

        rb->lcd_clear_display();
        int max_display = lines_per_screen - 1;
        int start_idx = 0;
        if (selected >= max_display) start_idx = selected - max_display + 1;
        for (int i = 0; i < max_display && (start_idx + i) < num_files; i++) {
            char dname[128];
            get_full_display_name(FILE_ENTRY(start_idx + i), dname, sizeof(dname));
            if (start_idx + i == selected) {
                rb->lcd_puts(0, i, (const unsigned char *)">");
                rb->lcd_puts_scroll(1, i, (const unsigned char *)dname);
            } else {
                rb->lcd_puts(0, i, (const unsigned char *)" ");
                rb->lcd_puts(1, i, (const unsigned char *)dname);
            }
        }
        rb->lcd_update();

        button = rb->button_get_w_tmo(HZ/10);

#ifdef HAVE_WHEEL_POSITION
        {
            int wheel = rb->wheel_status();
            if (wheel >= 0 && last_wheel >= 0) {
                int delta = wheel - last_wheel;
                if (delta < -48) delta += 96;
                else if (delta > 48) delta -= 96;
                if (delta >= 2 || delta <= -2) {
                    int step = delta / 2;
                    selected += step;
                    if (selected < 0) selected = num_files - 1;
                    if (selected >= num_files) selected = 0;
                    LOG_INFO("File list wheel delta=%d step=%d selected=%d", delta, step, selected);
                }
            }
            last_wheel = wheel;
        }
#endif

#ifdef HAVE_WHEEL_POSITION
        if (button == BUTTON_SCROLL_BACK || button == BUTTON_SCROLL_FWD) {
            /* 已由 wheel_status 处理，忽略 */
        } else
#endif
        if (button == BUTTON_LEFT || button == BUTTON_RIGHT) {
            if (button == BUTTON_LEFT) {
                if (selected > 0) selected--;
                else selected = num_files - 1;
            } else {
                if (selected < num_files - 1) selected++;
                else selected = 0;
            }
            LOG_INFO("File list left/right, selected=%d", selected);
        } else if (button == BUTTON_SELECT) {
            LOG_INFO("File selected: %s", FILE_ENTRY(selected));
            if (open_book(FILE_ENTRY(selected))) {
                bool reading = true;
                while (reading) {
                    int btn = rb->button_get_w_tmo(HZ/10);
                    if (btn == BUTTON_LEFT) {
                        go_prev_page();
                    } else if (btn == BUTTON_RIGHT) {
                        go_next_page();
                    } else if (btn == BUTTON_MENU) {
                        int ret = show_main_menu_with_exit();
                        if (ret == 1) {
                            LOG_INFO("Exit reading loop");
                            reading = false;
                        } else if (ret == -2) {
                            LOG_INFO("Return to reading from menu");
                        }
                    } else if (btn == BUTTON_SELECT) {
                        if (page_image_path[0] != '\0')
                            view_page_image();
                    } else if (btn == BUTTON_PLAY) {
                        bookmarks[0] = current_file_offset;
                        save_bookmarks_to_file();
                        rb->splash(HZ, "已保存到书签1");
                        LOG_INFO("Quick save bookmark 1 at offset %d", current_file_offset);
                    }
                }
                rb->lcd_scroll_stop();
                last_wheel = -1;
            }
        } else if (button == BUTTON_MENU) {
            LOG_INFO("File browser MENU pressed, exit plugin");
            exit_plugin = true;
        }
    }

    rb->lcd_scroll_stop();
    return PLUGIN_OK;
}

/* ---------- 插件入口 ---------- */
enum plugin_status plugin_start(const void* parameter)
{
    (void)parameter;
    log_init();
    LOG_INFO("Plugin started");

    load_global_settings();

    int w, h;
    rb->font_getstringsize((const unsigned char *)"0", &w, &h, FONT_UI);
    font_width = w > 0 ? w : 8;
    font_height = h > 0 ? h : 16;
    if (font_height <= 0) font_height = 16;
    if (font_width <= 0) font_width = 8;
    lines_per_screen = LCD_HEIGHT / font_height;
    if (lines_per_screen < 3) lines_per_screen = 3;
    LOG_INFO("LCD: %dx%d, Font: width=%d, height=%d, lines=%d",
             LCD_WIDTH, LCD_HEIGHT, font_width, font_height, lines_per_screen);

    render_buffer = rb->plugin_get_buffer(&render_buf_size);
    if (render_buf_size < 1024) {
        LOG_ERROR("Insufficient memory, got %d bytes", (int)render_buf_size);
        rb->splash(HZ, "内存不足");
        if (log_fd >= 0) {
            rb->write(log_fd, "PLUGIN EXIT WITH CODE 1 (error)\n", 31);
            rb->close(log_fd);
        }
        return PLUGIN_ERROR;
    }
    /* 文件列表区域从同一块插件缓冲区尾部划出，渲染缓冲至少保留 READ_BUFFER_SIZE */
    if (render_buf_size > READ_BUFFER_SIZE) {
        max_files = (int)((render_buf_size - READ_BUFFER_SIZE) / MAX_PATH);
    } else {
        max_files = 0;
    }
    if (max_files > MAX_FILES) max_files = MAX_FILES;
    {
        size_t total_buf = render_buf_size;
        render_buf_size -= (size_t)max_files * MAX_PATH;
        if (render_buf_size > 32768) render_buf_size = 32768;
        /* 文件列表贴在插件缓冲区末尾，渲染缓冲在前部，
         * 中间剩余的一段作为 EPUB 解压工作区（不动音乐缓冲区）。 */
        file_list = render_buffer + total_buf - (size_t)max_files * MAX_PATH;
        epub_work = (unsigned char *)render_buffer + render_buf_size;
        epub_work_size = (size_t)((char *)file_list - (char *)epub_work);
    }
    LOG_INFO("Render buffer size: %d bytes, file list slots: %d, EPUB workspace: %d",
             (int)render_buf_size, max_files, (int)epub_work_size);

    show_splash_screen();

    int ret = file_browser();

    if (log_fd >= 0) {
        rb->write(log_fd, "PLUGIN EXIT WITH CODE 0\n", 24);
        rb->close(log_fd);
    }
    return (ret == PLUGIN_OK) ? PLUGIN_OK : PLUGIN_ERROR;
}
