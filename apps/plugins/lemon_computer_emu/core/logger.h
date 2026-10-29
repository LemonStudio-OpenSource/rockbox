/*
 * core/logger.h - 统一日志工具（header-only，依赖 Rockbox）
 *
 * 用法：
 *   static char logbuf[64 * 1024];
 *   static logger_t log;
 *
 *   logger_init(&log, logbuf, sizeof(logbuf),
 *               "/lemon_computer_emu/log/apple2.log");
 *   logger_log(&log, "ROM loaded: %d bytes", rom_size);
 *   logger_save("/lemon_computer_emu/log/dump.bin", data, len);
 *   logger_flush(&log);
 *   logger_reset(&log);
 *
 * 所有函数都是 static，每个包含本文件的编译单元展开一份实现。
 */

#ifndef LEMON_LOGGER_H
#define LEMON_LOGGER_H

#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <fcntl.h>

#include "plugin.h"

#if defined(__GNUC__)
#define LOGGER_UNUSED __attribute__((unused))
#else
#define LOGGER_UNUSED
#endif

 /* 统一路径前缀，调用方可直接用 */
#define LEMON_DATA_DIR "/lemon_computer_emu"
#define LEMON_LOG_DIR  LEMON_DATA_DIR "/log"

typedef struct logger {
    char* buf;      /* 调用方提供的缓冲区 */
    size_t      len;      /* 已写入字节数，不含结尾 '\0' */
    size_t      cap;      /* 缓冲区容量 */
    const char* path;     /* flush 目标路径 */
    int         enabled;  /* buf 非空且 cap > 0 时为 1 */
} logger_t;

/*
 * 逐级创建 path 的父目录。
 * 例如 "/lemon_computer_emu/log/apple1.log" 会依次 mkdir
 * "/lemon_computer_emu" 和 "/lemon_computer_emu/log"。
 * 已存在的目录忽略错误。
 */
static LOGGER_UNUSED void logger_mkdir_parents(const char* path)
{
    char tmp[256];
    size_t n;
    char* p;

    if (!path)
        return;

    n = rb->strlen(path);
    if (n == 0 || n >= sizeof(tmp))
        return;

    rb->memcpy(tmp, path, n + 1);

    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            rb->mkdir(tmp);
            *p = '/';
        }
    }
}

/*
 * 覆盖写文件。data 可以为 NULL 且 len 为 0，表示创建空文件。
 * 返回 0 成功，-1 失败。
 */
static LOGGER_UNUSED int logger_write_file(const char* path,
    const void* data, size_t len)
{
    int fd;
    const char* p = (const char*)data;
    size_t written = 0;

    if (!path)
        return -1;

    logger_mkdir_parents(path);

    fd = rb->open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0)
        return -1;

    while (written < len) {
        int n = rb->write(fd, p + written, len - written);
        if (n <= 0) {
            rb->close(fd);
            return -1;
        }
        written += (size_t)n;
    }

    rb->close(fd);
    return 0;
}

/*
 * 独立保存接口，对应 logger->log_save(PATH, DATA) 的用法。
 * 不依赖 logger_t，直接覆盖写到 path。
 * 返回 0 成功，-1 失败。
 */
static LOGGER_UNUSED int logger_save(const char* path,
    const void* data, size_t len)
{
    return logger_write_file(path, data, len);
}

/*
 * 初始化 logger 实例。buf 由调用方提供，logger 不负责释放。
 * cap 为 0 或 buf 为 NULL 时实例处于禁用状态，后续 log 调用为无操作。
 */
static LOGGER_UNUSED void logger_init(logger_t* l, char* buf, size_t cap,
    const char* path)
{
    if (!l)
        return;

    l->buf = buf;
    l->len = 0;
    l->cap = cap;
    l->path = path;
    l->enabled = (buf != NULL && cap > 0);

    if (l->enabled)
        l->buf[0] = '\0';
}

/*
 * 重置已写长度，保留缓冲区和路径配置，便于切换模拟器时复用。
 */
static LOGGER_UNUSED void logger_reset(logger_t* l)
{
    if (!l)
        return;
    l->len = 0;
    if (l->enabled)
        l->buf[0] = '\0';
}

/*
 * 追加一条格式化日志到缓冲区。
 * 缓冲区不足时截断，始终保留结尾 '\0'。
 */
static LOGGER_UNUSED void logger_log(logger_t* l, const char* fmt, ...)
{
    va_list ap;
    int n;
    size_t remain;

    if (!l || !l->enabled || !l->buf || l->len >= l->cap)
        return;

    remain = l->cap - l->len;
    va_start(ap, fmt);
    n = rb->vsnprintf(l->buf + l->len, remain, fmt, ap);
    va_end(ap);

    if (n <= 0)
        return;

    if ((size_t)n < remain)
        l->len += (size_t)n;
    else
        l->len = l->cap - 1;  /* 截断，保留 '\0' 位置 */
}

/*
 * 把缓冲区内容写到 l->path。
 * 成功后清空缓冲区，便于继续记录。
 * 返回 0 成功，-1 失败，缓冲区为空时直接返回 0。
 */
static LOGGER_UNUSED int logger_flush(logger_t* l)
{
    int rc;

    if (!l || !l->enabled || !l->path || l->len == 0)
        return 0;

    rc = logger_write_file(l->path, l->buf, l->len);
    if (rc == 0)
        logger_reset(l);

    return rc;
}

/*
 * 把当前缓冲区内容写到指定 path，而不是 l->path。
 * 成功后清空缓冲区。
 */
static LOGGER_UNUSED int logger_flush_to(logger_t* l, const char* path)
{
    int rc;

    if (!l || !l->enabled || !path || l->len == 0)
        return 0;

    rc = logger_write_file(path, l->buf, l->len);
    if (rc == 0)
        logger_reset(l);

    return rc;
}

#endif /* LEMON_LOGGER_H */