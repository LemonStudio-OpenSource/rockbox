/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 *
 * Simple Metronome – Using Official Mixer API
 * - Scroll wheel adjusts BPM, Select toggles play/pause
 * - Generates 1000 Hz square wave
 * - Logs to /metronome/metronome.log
 ***************************************************************************/

#include "plugin.h"

/* -------- Audio parameters -------- */
#define SAMPLE_RATE     44100
#define DURATION_MS     50
#define AMPLITUDE       0.5f
#define SAMPLES_PER_CHAN (SAMPLE_RATE * DURATION_MS / 1000)
#define PCM_BUF_SIZE    (SAMPLES_PER_CHAN * 2 * sizeof(int16_t))

/* -------- BPM limits -------- */
#define MIN_BPM         20
#define MAX_BPM         300
#define DEFAULT_BPM     60

/* -------- Logging -------- */
#define LOG_PATH        "/metronome/metronome.log"
#define LOG_BUF_SIZE    4096

/* -------- Global state -------- */
static int16_t pcm_buffer[SAMPLES_PER_CHAN * 2];
static bool    playing = false;
static unsigned int bpm = DEFAULT_BPM;
static int     orig_volume;

/* Logging */
static int log_fd = -1;
static long start_tick = 0;
static unsigned int beat_counter = 0;
static char log_buf[LOG_BUF_SIZE];
static size_t log_buf_used = 0;

/* -------- Logging helpers (renamed to avoid conflict) -------- */
static void log_format_time(char *buf, size_t sz)
{
    long diff = *rb->current_tick - start_tick;
    long total_ms = (diff * 1000) / HZ;
    int sec = total_ms / 1000;
    int ms = total_ms % 1000;
    rb->snprintf(buf, sz, "[%02d-%03d]", sec, ms);
}

static void log_msg(const char *fmt, ...)
{
    if (log_fd < 0) return;

    char tbuf[16];
    log_format_time(tbuf, sizeof(tbuf));

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
        log_msg("PLUGIN START (Mixer API mode)");
    } else {
        rb->splash(HZ, "Log open failed");
    }
}

/* -------- Generate 1000 Hz square wave (stereo) -------- */
static void generate_beep(void)
{
    int max_val = (int)(32767.0f * AMPLITUDE);
    int period_samples = SAMPLE_RATE / 1000;
    int half = period_samples / 2;

    for (int i = 0; i < SAMPLES_PER_CHAN; i++) {
        int16_t sample = ((i % period_samples) < half) ? max_val : -max_val;
        pcm_buffer[i * 2]     = sample;
        pcm_buffer[i * 2 + 1] = sample;
    }
}

/* Trigger one beat using the Mixer API */
static void play_beat(void)
{
    if (!playing) return;
    /* 使用官方 mixer API */
    rb->mixer_channel_play_data(PCM_MIXER_CHAN_PLAYBACK, NULL,
                                pcm_buffer, PCM_BUF_SIZE);
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

/* -------- Update display -------- */
static void update_display(void)
{
    rb->lcd_clear_display();
    rb->lcd_putsf(0, 0, "Metronome");
    rb->lcd_putsf(0, 1, "BPM: %-3d  Vol: %d", bpm, rb->global_status->volume);
    rb->lcd_putsf(0, 2, "%s", playing ? "Running...  [Stop]" : "Stopped    [Start]");
    rb->lcd_putsf(0, 3, "Scroll: BPM  MENU: Exit");
    rb->lcd_update();
}

/* -------- Main plugin -------- */
enum plugin_status plugin_start(const void *param)
{
    (void)param;

    /* Init */
    log_init();

    orig_volume = rb->global_status->volume;
    set_volume(orig_volume);

    generate_beep();
    log_msg("Beep generated (square, -6 dB)");

    /* Stop background audio (exclusive mode) */
    rb->audio_stop();
    log_msg("Audio stopped");

    /* Display */
    update_display();
    log_msg("Display updated");

    /* Main loop */
    long next_tick = *rb->current_tick + (HZ * 60) / bpm;
    beat_counter = 0;
    playing = false;

    while (1) {
        /* Check for next beat */
        if (playing && *rb->current_tick >= next_tick) {
            play_beat();
            beat_counter++;
            if (beat_counter % 30 == 0)
                log_msg("Beat #%u", beat_counter);
            next_tick += (HZ * 60) / bpm;
        }

        /* Button handling */
        int btn = rb->button_get_w_tmo(0);

        switch (btn) {
            /* Scroll wheel: adjust BPM */
            case BUTTON_SCROLL_FWD:
            case BUTTON_SCROLL_FWD | BUTTON_REPEAT:
                if (bpm < MAX_BPM) {
                    bpm++;
                    update_display();
                    log_msg("BPM increased to %u", bpm);
                }
                break;

            case BUTTON_SCROLL_BACK:
            case BUTTON_SCROLL_BACK | BUTTON_REPEAT:
                if (bpm > MIN_BPM) {
                    bpm--;
                    update_display();
                    log_msg("BPM decreased to %u", bpm);
                }
                break;

            /* Toggle play/pause */
            case BUTTON_SELECT:
            case BUTTON_PLAY:
                playing = !playing;
                if (playing) {
                    next_tick = *rb->current_tick + (HZ * 60) / bpm;
                    beat_counter = 0;
                    log_msg("Started");
                } else {
                    /* 停止混音器通道播放 */
                    rb->mixer_channel_stop(PCM_MIXER_CHAN_PLAYBACK);
                    log_msg("Stopped");
                }
                update_display();
                break;

            /* Exit */
            case BUTTON_MENU:
                log_msg("Exit requested");
                goto exit_loop;

            default:
                break;
        }

        rb->yield();
    }

exit_loop:
    /* Cleanup */
    /* 停止混音器通道播放并恢复音量 */
    rb->mixer_channel_stop(PCM_MIXER_CHAN_PLAYBACK);
    set_volume(orig_volume);

    log_msg("PLUGIN EXIT (total beats: %u)", beat_counter);
    log_flush();
    if (log_fd >= 0)
        rb->close(log_fd);

    return PLUGIN_OK;
}
