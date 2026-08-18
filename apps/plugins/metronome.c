/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 *
 * Simple Metronome with logging (fixed for compilation)
 * - 60 BPM, 1000 Hz square wave, -20 dB
 * - Uses mixer channel, does NOT stop music
 * - Logs to /metronome/metronome.log (appended)
 ***************************************************************************/

#include "plugin.h"

#define SAMPLE_RATE 44100
#define DURATION_MS 50
#define VOLUME_DB   -20.0f
#define AMPLITUDE   0.1f

#define SAMPLES_PER_CHANNEL  (SAMPLE_RATE * DURATION_MS / 1000)

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
static void format_rel_time(char *buf, size_t bufsize)
{
    long diff = *rb->current_tick - start_tick;   /* dereference pointer */
    int sec = diff / HZ;
    int msec = (diff % HZ) * 1000 / HZ;
    rb->snprintf(buf, bufsize, "[%02d-%03d]", sec, msec);
}

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

static void log_init(void)
{
    /* create directory if missing */
    rb->mkdir("/metronome");

    /* fixed log file (appended each run) */
    const char *logpath = "/metronome/metronome.log";
    log_fd = rb->open(logpath, O_RDWR | O_CREAT | O_APPEND, 0666);
    if (log_fd >= 0) {
        start_tick = *rb->current_tick;   /* record start tick */
        log_message("PLUGIN START SUCCESSFULLY");

        /* try to get absolute time (if available), but don't crash if not */
        struct tm tm;
        if (rb->rtc_read_datetime) {   /* check if function exists */
            rb->rtc_read_datetime(&tm);
            char datebuf[32];
            rb->snprintf(datebuf, sizeof(datebuf),
                         "[%04d-%02d-%02d %02d:%02d:%02d]",
                         tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                         tm.tm_hour, tm.tm_min, tm.tm_sec);
            log_message("Absolute start time: %s", datebuf);
        } else {
            log_message("RTC not available, absolute time unknown");
        }
    } else {
        rb->splash(HZ, "Log open failed");
    }
}

/* ---------- main plugin entry ---------- */
enum plugin_status plugin_start(const void *parameter)
{
    (void)parameter;

    log_init();

    generate_beep();
    log_message("Beep buffer generated, samples per channel=%d", SAMPLES_PER_CHANNEL);

    rb->lcd_clear_display();
    rb->lcd_puts(0, 0, "Metronome");
    rb->lcd_puts(0, 1, "60 BPM");
    rb->lcd_puts(0, 2, "Press MENU to quit");
    rb->lcd_update();
    log_message("Display updated");

    long next_tick = *rb->current_tick + HZ;
    beat_counter = 0;

    while (1) {
        if (*rb->current_tick >= next_tick) {
            rb->mixer_channel_play_data(PCM_MIXER_CHAN_PLAYBACK,
                                        NULL,
                                        pcm_buffer,
                                        sizeof(pcm_buffer));
            beat_counter++;
            log_message("Played beat #%u", beat_counter);
            next_tick += HZ;
        }

        int btn = rb->button_get_w_tmo(0);
        if (btn == BUTTON_MENU || btn == BUTTON_PLAY) {
            log_message("User pressed exit button (0x%x), stopping", btn);
            rb->mixer_channel_stop(PCM_MIXER_CHAN_PLAYBACK);
            log_message("Mixer channel stopped");
            break;
        }

        rb->yield();
    }

    if (log_fd >= 0) {
        log_message("PLUGIN EXIT");
        rb->close(log_fd);
        log_fd = -1;
    }

    return PLUGIN_OK;
}
