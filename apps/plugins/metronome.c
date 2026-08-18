/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 *
 * Metronome with logging - iPod Color version (no <time.h>)
 * - 60 BPM, 1000 Hz square wave, -20 dB
 * - Uses mixer channel, does NOT stop background music
 * - Logs to /metronome/YYYY-MM-DD-HH-MM-SS.log
 ***************************************************************************/

#include "plugin.h"

#define SAMPLE_RATE 44100
#define DURATION_MS 50
#define BPM_INTERVAL_MS 1000
#define VOLUME_DB   -20.0f
#define AMPLITUDE   0.1f

#define SAMPLES_PER_CHANNEL  (SAMPLE_RATE * DURATION_MS / 1000)

/* stereo interleaved buffer */
static int16_t pcm_buffer[SAMPLES_PER_CHANNEL * 2];

/* logging */
static int log_fd = -1;
static long start_tick = 0;
static unsigned int beat_counter = 0;

/* generate square wave in stereo */
static void generate_beep(void)
{
    int max_val = (int)(32767.0f * AMPLITUDE);
    float period = 1.0f / 1000.0f;
    float half_period = period / 2.0f;

    for (size_t i = 0; i < SAMPLES_PER_CHANNEL; i++) {
        float t = (float)i / SAMPLE_RATE;
        int16_t sample = (t < half_period) ? max_val : -max_val;
        pcm_buffer[i * 2]     = sample;
        pcm_buffer[i * 2 + 1] = sample;
    }
}

/* ---------- logging helpers ---------- */
/* Format relative time since plugin start as [ss-mmm] (seconds-milliseconds) */
static void format_rel_time(char *buf, size_t bufsize)
{
    long diff = rb->current_tick - start_tick;
    int sec = diff / HZ;
    int msec = (diff % HZ) * 1000 / HZ;
    rb->snprintf(buf, bufsize, "[%02d-%03d]", sec, msec);
}

/* Write a log message (with timestamp) to the log file */
static void log_message(const char *fmt, ...)
{
    if (log_fd < 0) return;

    char timebuf[16];
    format_rel_time(timebuf, sizeof(timebuf));

    char msg[256];
    va_list args;
    va_start(args, fmt);
    rb->vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);

    char line[512];
    rb->snprintf(line, sizeof(line), "%s %s\n", timebuf, msg);

    rb->write(log_fd, line, rb->strlen(line));
}

/* Initialize logging: create directory, open log file, write first line */
static void log_init(void)
{
    struct tm tm;
    rb->rtc_read_datetime(&tm);

    /* create directory /metronome (ignore errors if it exists) */
    rb->mkdir("/metronome");

    /* build log filename: /metronome/YYYY-MM-DD-HH-MM-SS.log */
    char logpath[64];
    rb->snprintf(logpath, sizeof(logpath),
                 "/metronome/%04d-%02d-%02d-%02d-%02d-%02d.log",
                 tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                 tm.tm_hour, tm.tm_min, tm.tm_sec);

    log_fd = rb->open(logpath, O_RDWR | O_CREAT | O_APPEND, 0666);
    if (log_fd >= 0) {
        start_tick = rb->current_tick;
        log_message("PLUGIN START SUCCESSFULLY");
    } else {
        /* if logging fails, we still run without logs */
        rb->splash(HZ, "Log open failed");
    }
}

/* ---------- main plugin entry ---------- */
enum plugin_status plugin_start(const void *parameter)
{
    (void)parameter;

    /* 1. Initialize logging as early as possible */
    log_init();

    /* 2. Generate beep buffer */
    generate_beep();
    log_message("Beep buffer generated, samples per channel=%d", SAMPLES_PER_CHANNEL);

    /* 3. Clear screen and show status */
    rb->lcd_clear_display();
    rb->lcd_puts(0, 0, "Metronome");
    rb->lcd_puts(0, 1, "60 BPM");
    rb->lcd_puts(0, 2, "Press MENU to quit");
    rb->lcd_update();
    log_message("Display updated");

    /* 4. Timing loop */
    long next_tick = rb->current_tick + HZ;   /* first beat after 1 second */
    beat_counter = 0;

    while (1) {
        /* check if it's time for the next beat */
        if (rb->current_tick >= next_tick) {
            /* play the beep */
            rb->mixer_channel_play_data(PCM_MIXER_CHAN_PLAYBACK,
                                        NULL,
                                        pcm_buffer,
                                        sizeof(pcm_buffer));
            beat_counter++;
            log_message("Played beat #%u", beat_counter);

            next_tick += HZ;   /* schedule next beat */
        }

        /* non‑blocking key check */
        int btn = rb->button_get_w_tmo(0);
        if (btn == BUTTON_MENU || btn == BUTTON_PLAY) {
            log_message("User pressed exit button (0x%x), stopping", btn);
            rb->mixer_channel_stop(PCM_MIXER_CHAN_PLAYBACK);
            log_message("Mixer channel stopped");
            break;
        }

        rb->yield();
    }

    /* 5. Cleanup and close log */
    if (log_fd >= 0) {
        log_message("PLUGIN EXIT");
        rb->close(log_fd);
        log_fd = -1;
    }

    return PLUGIN_OK;
}
