/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 *
 * Simple Metronome – Exclusive PCM mode, direction keys for BPM/Volume
 * - Generates 1000 Hz square wave at ~ -6 dB
 * - Left/Right adjust BPM (1–400)
 * - Up/Down adjust system volume
 * - Select toggles play/pause
 * - MENU exits, restores original volume
 * - Logs to /metronome/metronome.log (RAM buffer, flushed on exit)
 ***************************************************************************/

#include "plugin.h"

/* -------- Audio parameters -------- */
#define SAMPLE_RATE     44100
#define DURATION_MS     50          /* length of each beep */
#define AMPLITUDE       0.5f        /* -6 dB relative to full scale */
#define SAMPLES_PER_CHAN (SAMPLE_RATE * DURATION_MS / 1000)
#define PCM_BUF_SIZE    (SAMPLES_PER_CHAN * 2 * sizeof(int16_t))

/* -------- BPM limits -------- */
#define MIN_BPM         1
#define MAX_BPM         400
#define DEFAULT_BPM     60

/* -------- Logging -------- */
#define LOG_PATH        "/metronome/metronome.log"
#define LOG_BUF_SIZE    4096

/* -------- Global state -------- */
static int16_t pcm_buffer[SAMPLES_PER_CHAN * 2];
static bool    playing = false;          /* true when metronome is ticking */
static unsigned int bpm = DEFAULT_BPM;
static int     orig_volume;              /* saved system volume */

/* PCM playback control */
static int pcm_calls = 0;                /* used in callback */

/* Logging */
static int log_fd = -1;
static long start_tick = 0;              /* tick at plugin start */
static unsigned int beat_counter = 0;
static char log_buf[LOG_BUF_SIZE];
static size_t log_buf_used = 0;

/* -------- Logging helpers (memory buffered) -------- */
static void format_time(char *buf, size_t sz)
{
    long diff = *rb->current_tick - start_tick;
    long total_ms = (diff * 1000) / HZ;   /* convert ticks to ms */
    int sec = total_ms / 1000;
    int ms = total_ms % 1000;
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
    if (log_buf_used + need >= LOG_BUF_SIZE) {
        if (log_fd >= 0)
            rb->write(log_fd, log_buf, log_buf_used);
        log_buf_used = 0;
    }
    log_buf_used += rb->snprintf(log_buf + log_buf_used,
                                 LOG_BUF_SIZE - log_buf_used,
                                 "%s %s\n", tbuf, msg);
}

static void log_flush(void)
{
    if (log_fd >= 0 && log_buf_used > 0) {
        rb->write(log_fd, log_buf, log_buf_used);
        log_buf_used = 0;
    }
}

static void log_init(void)
{
    rb->mkdir("/metronome");
    log_fd = rb->open(LOG_PATH, O_RDWR | O_CREAT | O_APPEND, 0666);
    if (log_fd >= 0) {
        start_tick = *rb->current_tick;
        log_msg("PLUGIN START (exclusive PCM mode)");
    } else {
        rb->splash(HZ, "Log open failed");
    }
}

/* -------- Generate 1000 Hz square wave (stereo) -------- */
static void generate_beep(void)
{
    int max_val = (int)(32767.0f * AMPLITUDE);
    int period_samples = SAMPLE_RATE / 1000;   /* samples per 1 ms */
    int half = period_samples / 2;

    for (int i = 0; i < SAMPLES_PER_CHAN; i++) {
        int16_t sample = ( (i % period_samples) < half ) ? max_val : -max_val;
        pcm_buffer[i * 2]     = sample;   /* left */
        pcm_buffer[i * 2 + 1] = sample;   /* right */
    }
}

/* -------- PCM callback (called once per beat) -------- */
static void pcm_callback(const void **start, size_t *size)
{
    if (pcm_calls == 0) {
        *start = pcm_buffer;
        *size  = PCM_BUF_SIZE;
        pcm_calls = 1;
    } else {
        *start = NULL;
        *size  = 0;
        pcm_calls = 0;
    }
}

/* Trigger one beat (if playing) */
static void play_beat(void)
{
    if (!playing) return;
    pcm_calls = 0;
    rb->pcm_play_data(pcm_callback, NULL);
}

/* -------- Volume helpers -------- */
static void set_volume(int vol)
{
    int min_vol = rb->sound_min(SOUND_VOLUME);
    int max_vol = rb->sound_max(SOUND_VOLUME);
    if (vol < min_vol) vol = min_vol;
    if (vol > max_vol) vol = max_vol;
    rb->sound_set(SOUND_VOLUME, vol);
}

/* -------- Main plugin -------- */
enum plugin_status plugin_start(const void *param)
{
    (void)param;

    /* ---- Init ---- */
    log_init();

    /* Save original volume and set a reasonable default */
    orig_volume = rb->global_status->volume;
    set_volume(orig_volume);   /* keep as is, user can adjust */

    generate_beep();
    log_msg("Beep generated (square, -6 dB)");

    /* Stop any background audio (exclusive mode) */
    rb->audio_stop();
    log_msg("Audio stopped");

    /* Display UI */
    rb->lcd_clear_display();
    rb->lcd_puts(0, 0, "Metronome");
    rb->lcd_puts(0, 1, "BPM: 60  Vol: XX");
    rb->lcd_puts(0, 2, "Select: Start/Stop");
    rb->lcd_puts(0, 3, "MENU: Exit");
    rb->lcd_update();
    log_msg("Display updated");

    /* ---- Main loop ---- */
    long next_tick = *rb->current_tick + (HZ * 60) / bpm;   /* first beat after delay */
    beat_counter = 0;
    playing = false;

    while (1) {
        /* Check if it's time for the next beat */
        if (playing && *rb->current_tick >= next_tick) {
            play_beat();
            beat_counter++;
            if (beat_counter % 30 == 0)
                log_msg("Beat #%u", beat_counter);
            next_tick += (HZ * 60) / bpm;   /* schedule next beat */
        }

        /* ---- Button handling ---- */
        int btn = rb->button_get_w_tmo(0);

        switch (btn) {
            /* Adjust BPM (Left/Right) */
            case BUTTON_LEFT:
            case BUTTON_LEFT | BUTTON_REPEAT:
                if (bpm > MIN_BPM) {
                    bpm--;
                    /* update display */
                    rb->lcd_putsf(0, 1, "BPM: %-3d  Vol: %d", bpm, rb->global_status->volume);
                    rb->lcd_update();
                }
                break;

            case BUTTON_RIGHT:
            case BUTTON_RIGHT | BUTTON_REPEAT:
                if (bpm < MAX_BPM) {
                    bpm++;
                    rb->lcd_putsf(0, 1, "BPM: %-3d  Vol: %d", bpm, rb->global_status->volume);
                    rb->lcd_update();
                }
                break;

            /* Adjust Volume (Up/Down) */
            case BUTTON_UP:
            case BUTTON_UP | BUTTON_REPEAT: {
                int v = rb->global_status->volume + 1;
                set_volume(v);
                rb->lcd_putsf(0, 1, "BPM: %-3d  Vol: %d", bpm, rb->global_status->volume);
                rb->lcd_update();
                break;
            }

            case BUTTON_DOWN:
            case BUTTON_DOWN | BUTTON_REPEAT: {
                int v = rb->global_status->volume - 1;
                set_volume(v);
                rb->lcd_putsf(0, 1, "BPM: %-3d  Vol: %d", bpm, rb->global_status->volume);
                rb->lcd_update();
                break;
            }

            /* Toggle play/pause (Select) */
            case BUTTON_SELECT:
            case BUTTON_PLAY:   /* also allow PLAY button */
                playing = !playing;
                if (playing) {
                    /* Reset timing and start */
                    next_tick = *rb->current_tick + (HZ * 60) / bpm;
                    beat_counter = 0;
                    rb->lcd_puts(0, 2, "Select: Stop       ");
                    log_msg("Started");
                } else {
                    rb->pcm_play_stop();
                    rb->lcd_puts(0, 2, "Select: Start      ");
                    log_msg("Stopped");
                }
                rb->lcd_update();
                break;

            /* Exit (MENU) */
            case BUTTON_MENU:
                log_msg("Exit requested");
                goto exit_loop;

            default:
                break;
        }

        rb->yield();
    }

exit_loop:
    /* ---- Cleanup ---- */
    if (playing)
        rb->pcm_play_stop();

    /* Restore original volume */
    set_volume(orig_volume);

    log_msg("PLUGIN EXIT (total beats: %u)", beat_counter);
    log_flush();
    if (log_fd >= 0)
        rb->close(log_fd);

    return PLUGIN_OK;
}
