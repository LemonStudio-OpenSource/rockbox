#include "plugin.h"

#define BPM 60
#define INTERVAL_TICKS (HZ * 60 / BPM)   /* 100 ticks/sec, 60 BPM => 1 beat per second */

static int log_fd = -1;
static long start_tick = 0;
static unsigned int beat_counter = 0;
static char log_buf[4096];
static size_t log_buf_used = 0;

/* -------- logging with RAM buffer -------- */
static void format_time(char *buf, size_t sz)
{
    long diff = *rb->current_tick - start_tick;
    int sec = diff / HZ;
    int ms = (diff % HZ) * 1000 / HZ;
    rb->snprintf(buf, sz, "[%02d-%03d]", sec, ms);
}

static void log_msg(const char *fmt, ...)
{
    if (log_fd < 0) return;

    char tbuf[16];
    format_time(tbuf, sizeof(tbuf));

    char msg[256];
    va_list ap;
    va_start(ap, fmt);
    rb->vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    int need = rb->snprintf(NULL, 0, "%s %s\n", tbuf, msg) + 1;
    if (log_buf_used + need >= sizeof(log_buf)) {
        if (log_fd >= 0)
            rb->write(log_fd, log_buf, log_buf_used);
        log_buf_used = 0;
    }
    log_buf_used += rb->snprintf(log_buf + log_buf_used,
                                 sizeof(log_buf) - log_buf_used,
                                 "%s %s\n", tbuf, msg);
}

static void log_flush(void)
{
    if (log_fd >= 0 && log_buf_used > 0) {
        rb->write(log_fd, log_buf, log_buf_used);
        log_buf_used = 0;
    }
}

/* -------- main plugin -------- */
enum plugin_status plugin_start(const void *param)
{
    (void)param;

    /* init logging */
    rb->mkdir("/metronome");
    log_fd = rb->open("/metronome/metronome.log",
                      O_RDWR | O_CREAT | O_APPEND, 0666);
    if (log_fd >= 0) {
        start_tick = *rb->current_tick;
        log_msg("PLUGIN START (Keyclick mode)");
    } else {
        rb->splash(HZ, "Log open failed");
    }

    /* display */
    rb->lcd_clear_display();
    rb->lcd_puts(0, 0, "Metronome (Keyclick)");
    rb->lcd_puts(0, 1, "60 BPM");
    rb->lcd_puts(0, 2, "Press MENU/PLAY to exit");
    rb->lcd_update();
    log_msg("Display updated");

    long next_tick = *rb->current_tick + INTERVAL_TICKS;
    beat_counter = 0;

    while (1) {
        if (*rb->current_tick >= next_tick) {
            /* play keyclick: 1000 Hz, 2 ms duration */
            rb->sound_play(1000, 2);
            beat_counter++;
            if (beat_counter % 30 == 0)
                log_msg("Beat #%u", beat_counter);
            next_tick += INTERVAL_TICKS;
        }

        int btn = rb->button_get_w_tmo(0);
        if (btn == BUTTON_MENU || btn == BUTTON_PLAY) {
            log_msg("Exit button pressed (0x%x)", btn);
            break;
        }

        rb->yield();
    }

    log_msg("PLUGIN EXIT (total beats: %u)", beat_counter);
    log_flush();
    if (log_fd >= 0) rb->close(log_fd);

    return PLUGIN_OK;
}
