#ifndef A2CORE_H
#define A2CORE_H
#include <stdint.h>

/* Apple II core: flat memory, softswitches, CPU integration.
   UI (plugin) provides the ROM bytes via a2core_load_rom and calls
   a2core_reset / a2core_step.  This module has NO rockbox dependency
   so it can be compiled/run on a host for verification. */

/* ---------------- state ---------------- */
extern uint8_t a2_mem[65536];
extern int  a2_sw_text;   /* true=text file (nonzero) */
extern int  a2_sw_mixed;
extern int  a2_sw_page2;
extern int  a2_sw_hires;
extern uint8_t a2_kbd_key;      /* pending key (char|0x80) or 0 */
extern uint32_t a2_spk_toggles; /* speaker toggles since reset */

/* memory mapping entry points (used by the 6502 core) */
uint8_t a2_read(uint16_t addr);
void    a2_write(uint16_t addr, uint8_t val);

/* load ROM: the LAST min(size, 0x3000) bytes go to $D000-$FFFF */
void a2_load_rom(const uint8_t *rom, uint32_t size);

/* reset CPU to the Apple II+ autostart vector */
void a2_reset(void);

/* step one instruction; returns 0 on fatal (illegal opcode) */
int  a2_step(void);

/* press a key (ASCII; stored internally as char|0x80) */
void a2_press_key(char c);

/* register/status readout for the UI status bar */
uint16_t a2_get_pc(void);
uint8_t  a2_get_a(void);
uint8_t  a2_get_x(void);
uint8_t  a2_get_y(void);
uint8_t  a2_get_p(void);

#endif
