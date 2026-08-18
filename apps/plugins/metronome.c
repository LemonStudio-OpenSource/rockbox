/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 *
 * Simple metronome for iPod Color (and other targets with mixer support)
 * - BPM = 60 fixed
 * - 1000 Hz square wave at -20 dB
 * - Plays via mixer channel, does NOT stop background music
 * - Exit with MENU or PLAY button
 ***************************************************************************/

#include "plugin.h"

#define SAMPLE_RATE 44100
#define DURATION_MS 50          /* length of each beep */
#define BPM_INTERVAL_MS 1000    /* 60 BPM = 1 beat per second */
#define VOLUME_DB   -20.0f

/* Amplitude factor for -20 dB: 10^(-20/20) = 0.1 */
#define AMPLITUDE   0.1f

/* PCM buffer for one beep (mono, 16-bit) */
static int16_t pcm_buffer[SAMPLE_RATE * DURATION_MS / 1000];

/* Generate a 1000 Hz square wave with the desired amplitude */
static void generate_beep(void)
{
    size_t n = sizeof(pcm_buffer) / sizeof(int16_t);
    float period = 1.0f / 1000.0f;          /* 1 ms */
    float half_period = period / 2.0f;
    int max_val = (int)(32767.0f * AMPLITUDE);

    for (size_t i = 0; i < n; i++) {
        float t = (float)i / SAMPLE_RATE;
        /* square wave: high for first half, low for second */
        pcm_buffer[i] = (t < half_period) ? max_val : -max_val;
    }
}

enum plugin_status plugin_start(const void *parameter)
{
    (void)parameter;  /* unused */

    generate_beep();

    /* Use current tick for timing (HZ = 100 ticks/second on most targets) */
    long next_tick = rb->current_tick + HZ;   /* first beat after 1 second */

    while (1) {
        /* Check if it's time for the next beat */
        if (rb->current_tick >= next_tick) {
            /* Play the beep via mixer – does not block audio playback */
            rb->mixer_channel_play_data(PCM_MIXER_CHAN_PLAYBACK,
                                        NULL,
                                        pcm_buffer,
                                        sizeof(pcm_buffer));
            next_tick += HZ;   /* schedule next beat 1 second later */
        }

        /* Non‑blocking key check – exit on MENU or PLAY */
        int btn = rb->button_get_w_tmo(0);
        if (btn == BUTTON_MENU || btn == BUTTON_PLAY) {
            rb->mixer_channel_stop(PCM_MIXER_CHAN_PLAYBACK);
            return PLUGIN_OK;
        }

        /* Yield to other tasks, keep CPU cool */
        rb->yield();
    }
}
