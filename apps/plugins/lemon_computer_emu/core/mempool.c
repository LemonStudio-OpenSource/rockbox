/***************************************************************************
 * core/mempool.c - shared memory pool for every emulated machine
 *
 * See core/mempool.h for the rationale.  Everything defined here is
 * plain storage: only mempool.c allocates it, the cores just use it.
 */

#include <stdint.h>
#include <stddef.h>

#include "plugin.h"
#include "mempool.h"

/* One RAM image shared by Apple 1, Apple II and TRS-80. */
uint8_t le_ram[LE_RAM_SIZE];

/* One ROM staging area shared by the loaders. */
uint8_t le_rom[LE_ROM_SIZE];

/* One framebuffer shared by the two bitmap machines. */
uint16_t le_fb[LE_FB_W * LE_FB_H];

/* Last-resort log area when the plugin buffer is already used up. */
static char le_log_fallback[2048];

void le_ram_clear(void)
{
    rb->memset(le_ram, 0, sizeof(le_ram));
}

char *le_log_buffer(size_t want, size_t *out_size)
{
    size_t avail = 0;
    char *buf = rb->plugin_get_buffer(&avail);

    if (!buf || avail < 2048)
    {
        if (out_size)
            *out_size = sizeof(le_log_fallback);
        return le_log_fallback;
    }

    /* keep a little headroom so a later claim still has something */
    if (want == 0 || want > avail - 1024)
        want = avail - 1024;

    if (out_size)
        *out_size = want;
    return buf;
}
