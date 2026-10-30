#ifndef T80CORE_H
#define T80CORE_H
#include <stdint.h>
#include "../core/zilogz80.h"

/* TRS-80 Model I / III (Level II) core: flat memory, keyboard matrix,
   Z80 integration.
   UI (plugin) provides the ROM bytes via trs80_load_rom_a/b and calls
   trs80_init / trs80_reset / trs80_step.  The optional logger is passed
   in via trs80_init.  Memory + keyboard map follow the real Model I
   (see t80core.c header comments). */

struct logger;

/* ---------------- state ---------------- */
extern uint8_t trs_mem[65536];
/* keyboard matrix: trs_kb_row[r] bit b = 1 means that key is pressed.
   Idle rows read 0x00; a pressed key sets its bit. (matches real HW) */
extern uint8_t trs_kb_row[8];
/* the live Z80 (public so the UI can dump/restore it for save states) */
extern Z80 t80_cpu;

/* memory mapping entry points */
uint8_t trs80_read(uint16_t addr);
void    trs80_write(uint16_t addr, uint8_t val);

/* attach an optional logger (may be NULL). call before trs80_reset. */
void trs80_init(struct logger* log);

/* load ROM A (=> 0x0000, up to 0x2000 bytes) */
void trs80_load_rom_a(const uint8_t* rom, uint32_t size);
/* load ROM B (=> 0x2000, up to 0x1000 bytes) */
void trs80_load_rom_b(const uint8_t* rom, uint32_t size);

/* reset the CPU to 0x0000 and clear the keyboard matrix */
void trs80_reset(void);

/* step one instruction; returns number of T-states, or 0 on halt */
int trs80_step(void);

/* register readouts for the UI status bar */
uint16_t trs80_get_pc(void);
uint8_t  trs80_get_a(void);
uint8_t  trs80_get_f(void);

/* save / restore the Z80 for snapshots.  trs80_save clears the host
   callbacks/context from *out so it can be written to disk verbatim;
   trs80_restore re-links them onto the live cpu.  Returns 0 on success. */
void trs80_save_cpu(Z80* out);
void trs80_restore_cpu(const Z80* in);

#endif
