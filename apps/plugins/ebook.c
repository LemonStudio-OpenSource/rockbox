/***************************************************************************
 *             ebook.c - 电子书阅读器 (iPod Color / Rockbox)
 *  版本：8.3 修复编译错误，滚动逻辑优化
 *  功能：文件浏览、UTF-8 渲染、夜间模式、5个书签、精确翻页、偏移量缓存
 *  内存：缓存 41 页偏移量，内存占用极小
 *  按键：左/右键翻页，MENU 主菜单，PLAY 快速书签
 *  菜单：主菜单按 MENU 无效，新增"返回阅读"项（生效）
 *  滚轮：仅在文件列表可用，其他界面仅左右键
 *  日志：/ebook/logs/yyyy-mm-dd-hh-mm-ss.txt，记录所有操作
 *  开屏：显示 "Lemon E-Book / For iPod Color / Ver.0.9Beta" 持续2秒
 *  改进：文件信息增强，文件名列表滚动显示
 ***************************************************************************/

#include "plugin.h"

/* 配置 */
#define LCD_WIDTH       220
#define LCD_HEIGHT      176
#define MAX_BOOKMARKS   5
#define SETTINGS_FILE   "/ebook/config/settings.cfg"
#define READ_BUFFER_SIZE (4 * 1024)   /* 渲染缓冲区 */
#define CACHE_SIZE      41            /* 窗口大小：存储最近访问的页偏移量 */

/* 全局变量 */
static char *render_buffer = NULL;
static size_t render_buf_size = 0;
static int current_file_offset = 0;    /* 当前页起始偏移 */
static char current_file[MAX_PATH];
static int file_size = 0;
static int font_height, font_width;
static int lines_per_screen = 0;

/* 书签 */
static int bookmarks[MAX_BOOKMARKS];

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

/* ---------- 偏移量缓存（无序集合，支持最邻近查找） ---------- */
static int page_offsets[CACHE_SIZE];
static int cache_count = 0;                /* 当前缓存中有效偏移量个数 */
static int current_cache_pos = 0;          /* 当前页在缓存中的索引（若有） */
static bool current_cache_valid = false;   /* 当前偏移量是否在缓存中 */

/* 清空缓存 */
static void cache_clear(void)
{
    cache_count = 0;
    current_cache_pos = 0;
    current_cache_valid = false;
    LOG_INFO("Cache cleared");
}

/* 添加一个偏移量到缓存（去重，若已存在则忽略，返回其在缓存中的索引） */
static int cache_add_offset(int offset)
{
    if (offset < 0 || offset >= file_size) return -1;

    /* 检查是否已存在 */
    for (int i = 0; i < cache_count; i++) {
        if (page_offsets[i] == offset) {
            LOG_INFO("Cache duplicate offset %d ignored", offset);
            return i;
        }
    }

    /* 缓存未满，直接添加 */
    if (cache_count < CACHE_SIZE) {
        page_offsets[cache_count] = offset;
        int idx = cache_count;
        cache_count++;
        LOG_INFO("Cache add offset %d at idx %d", offset, idx);
        return idx;
    }

    /* 缓存已满：丢弃最旧的（索引0），左移所有元素，新元素放在末尾 */
    for (int i = 0; i < CACHE_SIZE - 1; i++) {
        page_offsets[i] = page_offsets[i + 1];
    }
    page_offsets[CACHE_SIZE - 1] = offset;
    cache_count = CACHE_SIZE;
    /* 如果当前页索引受影响，调整 current_cache_pos */
    if (current_cache_valid && current_cache_pos > 0) {
        current_cache_pos--;
    }
    LOG_INFO("Cache evict oldest, add offset %d at idx %d", offset, CACHE_SIZE - 1);
    return CACHE_SIZE - 1;
}

/* 在缓存中查找小于 offset 的最大偏移量（即上一页），返回索引，未找到返回 -1 */
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

/* 在缓存中查找大于 offset 的最小偏移量（即下一页），返回索引，未找到返回 -1 */
static int cache_find_next(int offset)
{
    int best_idx = -1;
    int best_val = file_size; /* 大于所有合法偏移 */
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
    if (night_mode) {
        rb->lcd_set_foreground(LCD_WHITE);
        rb->lcd_set_background(LCD_BLACK);
    } else {
        rb->lcd_set_foreground(LCD_BLACK);
        rb->lcd_set_background(LCD_WHITE);
    }
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
   核心渲染与邻页计算（拆分为三个函数，避免重复渲染）
   ============================================================ */

/* 1. 计算上一页偏移量（仅扫描，不绘制） */
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
                    /* 对齐 UTF-8 边界 */
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
                /* 候选无效，继续向前搜索 */
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

/* 2. 计算下一页偏移量（模拟渲染，不绘制） */
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

    /* 对齐 UTF-8 边界 */
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
        rb->font_getstringsize((const unsigned char *)temp_buf, &w_line, &h_line, FONT_UI);

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

/* 3. 渲染指定偏移量的页面（不计算邻页，只绘制） */
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

    /* 标题栏 */
    long tick = *rb->current_tick;   /* 解引用 */
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

    /* 内容区 */
    int line_y = 1;
    char line_buf[256];
    int line_buf_len = 0;

    while (line_y < lines_per_screen - 1 && p < end_ptr) {
        unsigned char c = (unsigned char)*p;
        if (c == '\n' || c == '\r') {
            if (line_buf_len > 0) {
                line_buf[line_buf_len] = '\0';
                rb->lcd_puts(0, line_y, (const unsigned char *)line_buf);
                line_buf_len = 0;
                line_y++;
            } else {
                line_y++;
            }
            /* 跳过 \r\n 组合 */
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
        rb->font_getstringsize((const unsigned char *)temp_buf, &w_line, &h_line, FONT_UI);

        if (w_line >= LCD_WIDTH) {
            if (line_buf_len > 0) {
                line_buf[line_buf_len] = '\0';
                rb->lcd_puts(0, line_y, (const unsigned char *)line_buf);
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
        rb->lcd_puts(0, line_y, (const unsigned char *)line_buf);
    }

    /* 底部状态栏 */
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

    /* 更新当前偏移量在缓存中的位置 */
    int idx = cache_add_offset(offset);
    if (idx >= 0) {
        current_cache_pos = idx;
        current_cache_valid = true;
    }
}

/* 刷新当前页（供外部调用，直接渲染当前偏移量） */
static void refresh_current_page(void)
{
    render_page(current_file_offset);
}

/* ---------- 翻页函数（混合模式） ---------- */
static void go_next_page(void)
{
    LOG_INFO("go_next_page from offset %d", current_file_offset);

    /* 1. 尝试从缓存中查找下一页 */
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

    /* 2. 缓存未命中，计算下一页偏移量（不渲染） */
    int next_off = compute_next_offset(current_file_offset);
    if (next_off < 0 || next_off >= file_size) {
        LOG_WARN("End of file reached at offset %d", current_file_offset);
        rb->splash(HZ, "已到文件末尾");
        return;
    }

    /* 3. 跳转并渲染 */
    current_file_offset = next_off;
    cache_add_offset(current_file_offset);   /* 加入缓存 */
    render_page(current_file_offset);
    LOG_INFO("go_next_page: computed next offset %d", current_file_offset);
}

static void go_prev_page(void)
{
    LOG_INFO("go_prev_page from offset %d", current_file_offset);

    /* 1. 尝试从缓存中查找上一页 */
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

    /* 2. 缓存未命中，计算上一页偏移量（扫描） */
    int prev_off = compute_prev_offset(current_file_offset);
    if (prev_off < 0 || prev_off >= current_file_offset) {
        LOG_WARN("Start of file reached at offset %d", current_file_offset);
        rb->splash(HZ, "已到文件开头");
        return;
    }

    /* 3. 跳转并渲染 */
    current_file_offset = prev_off;
    cache_add_offset(current_file_offset);   /* 加入缓存 */
    render_page(current_file_offset);
    LOG_INFO("go_prev_page: computed prev offset %d", current_file_offset);
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

/* ---------- 菜单系统（无滚轮，仅左右键） ---------- */
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
            if (ret == 1) return 1;
            if (ret == -2) return -2;
        } else if (button == BUTTON_MENU) {
            if (menu_level == 0) {
                continue;
            } else {
                return -1;
            }
        }
    }
}

/* ---- 菜单功能 ---- */

/* 增强的文件信息 */
static int file_info_func(void *arg)
{
    (void)arg;
    char info[7][32];  /* 增加行数 */
    get_full_display_name(current_file, info[0], sizeof(info[0]));
    rb->snprintf(info[1], sizeof(info[1]), "大小: %d 字节 (%dKB)", file_size, file_size/1024);
    rb->snprintf(info[2], sizeof(info[2]), "偏移: %d", current_file_offset);
    int percent = (file_size > 0) ? (current_file_offset * 100) / file_size : 0;
    rb->snprintf(info[3], sizeof(info[3]), "进度: %d%%", percent);
    rb->snprintf(info[4], sizeof(info[4]), "缓存页: %d", cache_count);
    rb->snprintf(info[5], sizeof(info[5]), "夜间模式: %s", night_mode ? "开" : "关");
    /* 预留一行备用，或显示版本 */
    rb->snprintf(info[6], sizeof(info[6]), "版本: 8.3");

    int button;
    while (1) {
        rb->lcd_scroll_stop();
        apply_color_scheme();
        rb->lcd_clear_display();
        rb->lcd_puts(0, 0, (const unsigned char *)"文件信息");
        int y = 2;
        for (int i = 0; i < 7; i++) {
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

/* ---- 偏移量跳转 ---- */
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
    refresh_current_page();
    LOG_INFO("Night mode toggled to %d", night_mode ? 1 : 0);
    return 0;
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

/* ---- 主菜单（修复夜间模式显示） ---- */
static int show_main_menu_with_exit(void)
{
    char night_text[20];
    rb->snprintf(night_text, sizeof(night_text), "夜间模式 [%s]", night_mode ? "开" : "关");
    const menu_item_t items[] = {
        {"返回阅读", return_to_reading_func, NULL},
        {"文件信息", file_info_func, NULL},
        {"书签", bookmark_list_func, NULL},
        {"跳转到偏移量", jump_offset_func, NULL},
        {night_text, night_mode_func, NULL},
        {"退出", exit_func, NULL}
    };
    while (1) {
        int ret = show_menu(items, 6, "主菜单", 0);
        if (ret == 1) {
            return 1;
        } else if (ret == -2) {
            return -2;
        }
    }
}

/* ---------- 开屏画面 ---------- */
static void show_splash_screen(void)
{
    rb->lcd_set_foreground(LCD_BLACK);
    rb->lcd_set_background(LCD_WHITE);
    rb->lcd_clear_display();

    const char *lines[] = {
        "Lemon E-Book",
        "For iPod Color",
        "Ver.0.9Beta"
    };
    int num_lines = 3;
    int y_offset = (lines_per_screen - num_lines) / 2;

    for (int i = 0; i < num_lines; i++) {
        int w, h;
        rb->font_getstringsize((const unsigned char *)lines[i], &w, &h, FONT_UI);
        int x = (LCD_WIDTH - w) / 2 / font_width;
        if (x < 0) x = 0;
        rb->lcd_puts(x, y_offset + i, (const unsigned char *)lines[i]);
    }

    rb->lcd_update();

    long start_tick = *rb->current_tick;  /* 解引用 */
    while (*rb->current_tick - start_tick < HZ * 2) {
        int btn = rb->button_get_w_tmo(HZ/10);
        if (btn != BUTTON_NONE) break;
    }
}

/* ---------- 打开书籍 ---------- */
static bool open_book(const char *filename)
{
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
    refresh_current_page();
    return true;
}

/* ---------- 文件浏览器（带滚轮 + 文件名滚动修复） ---------- */
static int file_browser(void)
{
    struct dirent *entry;
    DIR *dir;
    char file_list[128][MAX_PATH];
    int num_files = 0, selected = 0;
    long last_scroll_tick = 0;
    int scroll_step = 1;
    int last_scroll_button = 0;
    int prev_selected = -1;  /* 用于检测选中项是否变化，控制滚动重启 */

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
        if (len > 4 && rb->strcmp(entry->d_name + len - 4, ".txt") == 0) {
            if (num_files < 128) {
                rb->snprintf(file_list[num_files], MAX_PATH, "/ebook/%s", entry->d_name);
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
        /* 如果选中的文件名变化，则停止旧滚动并重置 */
        if (selected != prev_selected) {
            rb->lcd_scroll_stop();
            prev_selected = selected;
        }

        /* 绘制文件列表 */
        rb->lcd_clear_display();
        int max_display = lines_per_screen - 1;
        int start_idx = 0;
        if (selected >= max_display) start_idx = selected - max_display + 1;
        for (int i = 0; i < max_display && (start_idx + i) < num_files; i++) {
            char dname[128];
            get_full_display_name(file_list[start_idx + i], dname, sizeof(dname));
            if (start_idx + i == selected) {
                rb->lcd_puts(0, i, (const unsigned char *)">");
                /* 使用滚动显示长文件名 */
                rb->lcd_puts_scroll(1, i, (const unsigned char *)dname);
            } else {
                rb->lcd_puts(0, i, (const unsigned char *)" ");
                rb->lcd_puts(1, i, (const unsigned char *)dname);
            }
        }
        rb->lcd_update();

        /* 等待按键，超时缩短使滚动更新及时（但不再手动 step） */
        button = rb->button_get_w_tmo(HZ/10);

        if (button == BUTTON_SCROLL_BACK) {
            long cur_tick = (long)*rb->current_tick;
            if (button != last_scroll_button) {
                scroll_step = 1;
                last_scroll_button = button;
                last_scroll_tick = cur_tick;
                selected -= 1;
            } else {
                long diff = cur_tick - last_scroll_tick;
                if (diff < 0) diff += 0x7FFFFFFF;
                if (diff < HZ / 10) {
                    if (scroll_step < 2) scroll_step++;
                } else {
                    scroll_step = 1;
                }
                last_scroll_tick = cur_tick;
                selected -= scroll_step;
            }
            if (selected < 0) selected = num_files - 1;
            LOG_INFO("File list scroll back, selected=%d", selected);
        } else if (button == BUTTON_SCROLL_FWD) {
            long cur_tick = (long)*rb->current_tick;
            if (button != last_scroll_button) {
                scroll_step = 1;
                last_scroll_button = button;
                last_scroll_tick = cur_tick;
                selected += 1;
            } else {
                long diff = cur_tick - last_scroll_tick;
                if (diff < 0) diff += 0x7FFFFFFF;
                if (diff < HZ / 10) {
                    if (scroll_step < 2) scroll_step++;
                } else {
                    scroll_step = 1;
                }
                last_scroll_tick = cur_tick;
                selected += scroll_step;
            }
            if (selected >= num_files) selected = 0;
            LOG_INFO("File list scroll forward, selected=%d", selected);
        } else if (button == BUTTON_LEFT || button == BUTTON_RIGHT) {
            if (button == BUTTON_LEFT) {
                if (selected > 0) selected--;
                else selected = num_files - 1;
            } else {
                if (selected < num_files - 1) selected++;
                else selected = 0;
            }
            LOG_INFO("File list left/right, selected=%d", selected);
        } else if (button == BUTTON_SELECT) {
            LOG_INFO("File selected: %s", file_list[selected]);
            if (open_book(file_list[selected])) {
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
                    } else if (btn == BUTTON_PLAY) {
                        bookmarks[0] = current_file_offset;
                        save_bookmarks_to_file();
                        rb->splash(HZ, "已保存到书签1");
                        LOG_INFO("Quick save bookmark 1 at offset %d", current_file_offset);
                    }
                }
                /* 退出阅读后，停止滚动并恢复列表状态 */
                rb->lcd_scroll_stop();
            }
        } else if (button == BUTTON_MENU) {
            LOG_INFO("File browser MENU pressed, exit plugin");
            exit_plugin = true;
        }
    }

    /* 退出前停止滚动 */
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
    LOG_INFO("Font: width=%d, height=%d, lines=%d", font_width, font_height, lines_per_screen);

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
    if (render_buf_size > 8192) render_buf_size = 8192;
    LOG_INFO("Render buffer size: %d bytes", (int)render_buf_size);

    /* 显示开屏画面（持续2秒或按键跳过） */
    show_splash_screen();

    int ret = file_browser();

    if (log_fd >= 0) {
        rb->write(log_fd, "PLUGIN EXIT WITH CODE 0\n", 24);
        rb->close(log_fd);
    }
    return (ret == PLUGIN_OK) ? PLUGIN_OK : PLUGIN_ERROR;
}