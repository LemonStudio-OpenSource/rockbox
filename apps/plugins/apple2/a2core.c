#include <stdint.h>
#include "a2core.h"

/* ============================================================
   Apple II core emulation - no rockbox dependency.
   Only this file uses the 6502 core (so CPU registers exist in
   exactly one compilation unit).
   ============================================================ */

/* lightweight LOG (the 6502 core logs illegal opcodes) */
int g_log_counter = 0;
#ifndef MAX_LOG_COUNT
#define MAX_LOG_COUNT 5000
#endif
#define LOG(fmt, ...) ((void)0)

#include "6502.h"

/* ---------------- state ---------------- */
uint8_t a2_mem[65536];
int a2_sw_text = 1;
int a2_sw_mixed = 0;
int a2_sw_page2 = 0;
int a2_sw_hires = 0;
uint8_t a2_kbd_key = 0;
uint32_t a2_spk_toggles = 0;

/* 6502 core callbacks */
static void soft_switch(uint16_t addr, uint8_t val) {
    (void)val;
    switch (addr) {
    case 0xC000: a2_kbd_key = val; break;
    case 0xC010: a2_kbd_key = 0; break;
    case 0xC030: case 0xC031: case 0xC032: case 0xC033:
    case 0xC034: case 0xC035: case 0xC036: case 0xC037:
        a2_spk_toggles++; break;
    case 0xC050: a2_sw_text = 0; a2_sw_mixed = 0; break;
    case 0xC051: a2_sw_text = 1; break;
    case 0xC052: a2_sw_mixed = 0; break;
    case 0xC053: a2_sw_mixed = 1; break;
    case 0xC054: a2_sw_page2 = 0; break;
    case 0xC055: a2_sw_page2 = 1; break;
    case 0xC056: a2_sw_hires = 0; break;
    case 0xC057: a2_sw_hires = 1; break;
    default: break;
    }
}

uint8_t a2_read(uint16_t addr) {
    if (addr < 0xC000) return a2_mem[addr];
    if (addr >= 0xD000) return a2_mem[addr];
    switch (addr) {
    case 0xC000: return a2_kbd_key;
    case 0xC010:
        a2_kbd_key = 0;
        return 0x00;
    case 0xC030: case 0xC031: case 0xC032: case 0xC033:
    case 0xC034: case 0xC035: case 0xC036: case 0xC037:
        a2_spk_toggles++;
        return 0x00;
    case 0xC050: case 0xC051: case 0xC052: case 0xC053:
    case 0xC054: case 0xC055: case 0xC056: case 0xC057:
        soft_switch(addr, 0);
        return 0x00;
    default:
        return 0x00;
    }
}

void a2_write(uint16_t addr, uint8_t val) {
    if (addr < 0xC000) { a2_mem[addr] = val; return; }
    if (addr >= 0xD000) return;       /* ROM write-protect */
    soft_switch(addr, val);
}

/* ============================================================
   FIX: define the 6502 core callback pointers that 6502.h
   declares as extern. Without these, every dbyte/pushstack/
   STA_* and ADC_* reference in a2core.o is an undefined symbol.
   ============================================================ */
uint8_t (*cpu_read)(uint16_t addr) = a2_read;
void (*cpu_write)(uint16_t addr, uint8_t val) = a2_write;

void a2_load_rom(const uint8_t *rom, uint32_t size) {
    if (size > 0x3000) {
        rom = rom + (size - 0x3000);
        size = 0x3000;
    }
    if (size > 0) {
        int i;
        for (i = 0; i < (int)size; i++) a2_mem[0x10000 - size + i] = rom[i];
    }
}

void a2_press_key(char c) {
    a2_kbd_key = (uint8_t)(c & 0x7F) | 0x80;
}

void a2_reset(void) {
    m6502_reset();
    a2_kbd_key = 0;
    a2_spk_toggles = 0;
}

int a2_step(void) {
    return m6502_step();
}

/* register readouts */
uint16_t a2_get_pc(void) { return programcounter; }
uint8_t  a2_get_a(void)  { return regA; }
uint8_t  a2_get_x(void)  { return regX; }
uint8_t  a2_get_y(void)  { return regY; }
uint8_t  a2_get_p(void)  { return Status; }
