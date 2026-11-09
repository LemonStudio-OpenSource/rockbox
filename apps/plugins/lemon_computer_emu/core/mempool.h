/***************************************************************************
 * core/mempool.h - shared memory pool for every emulated machine
 *
 * The plugin only ever runs one machine at a time: the launcher menu
 * picks APPLE1, APPLE2 or TRS-80, that emulator runs to completion and
 * only then may another one start.  Therefore all three cores share a
 * single RAM image, a single ROM staging area and a single 320x192
 * framebuffer instead of each one owning a private copy.
 *
 * Before this change every core had its own 64 KiB RAM plus its own
 * ROM scratch area plus (for Apple II) a private framebuffer.  On the
 * iPod target PLUGIN_BUFFER_SIZE is 512 KiB and the linked image grew
 * past it, which made the link fail; the plugin then also had no
 * runtime buffer left for the TRS-80 framebuffer, which reported
 * "Not enough buffer" at start-up.  Sharing the arrays fixes both
 * problems at once.
 */

#ifndef LEMON_MEMPOOL_H
#define LEMON_MEMPOOL_H

#include <stdint.h>
#include <stddef.h>

/* 64 KiB CPU address space - enough for all three machines. */
#define LE_RAM_SIZE   0x10000u
/* Largest ROM image staged at boot (Apple II needs 20 KiB). */
#define LE_ROM_SIZE   0x5000u
/* 320x192 RGB565 display, shared by Apple II and TRS-80. */
#define LE_FB_W       320
#define LE_FB_H       192

extern uint8_t  le_ram[LE_RAM_SIZE];
extern uint8_t  le_rom[LE_ROM_SIZE];
extern uint16_t le_fb[LE_FB_W * LE_FB_H];

/* Zero the shared RAM before a machine boots so that leftovers from the
 * previously emulated computer can never show up as garbage. */
void le_ram_clear(void);

/* Claim the leftover plugin buffer as a log scratch area.  All three
 * emulators reuse the same area because they never run concurrently.
 * Returns the internal fallback buffer when the plugin buffer is
 * exhausted or absent, so this never returns NULL.
 * `want` is the preferred number of bytes, 0 means "as much as fits". */
char *le_log_buffer(size_t want, size_t *out_size);

#endif /* LEMON_MEMPOOL_H */
