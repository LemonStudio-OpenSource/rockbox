#include <stdint.h>
#include "../core/mos6502.h"
#include "a2core.h"

/* ============================================================
   Apple II core emulation.
   Only this file uses the 6502 core (CPU registers exist in
   exactly one compilation unit).  The optional logger is passed
   in via a2_init and forwarded to the 6502 core.
   ============================================================ */

   /* ---------------- state ---------------- */
uint8_t a2_mem[65536];
int a2_sw_text = 1;
int a2_sw_mixed = 0;
int a2_sw_page2 = 0;
int a2_sw_hires = 0;
uint8_t a2_kbd_key = 0;
uint32_t a2_spk_toggles = 0;

/* 6502 core instance and optional logger */
static mos6502_t a2_cpu;
static logger_t* a2_log = NULL;

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

/* wrappers matching mos6502_read_fn / mos6502_write_fn */
static uint8_t a2_cpu_read(mos6502_t* cpu, uint16_t addr) {
    (void)cpu;
    return a2_read(addr);
}

static void a2_cpu_write(mos6502_t* cpu, uint16_t addr, uint8_t val) {
    (void)cpu;
    a2_write(addr, val);
}

void a2_init(logger_t* log) {
    a2_log = log;
}

void a2_load_rom(const uint8_t* rom, uint32_t size) {
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
    mos6502_init(&a2_cpu, a2_cpu_read, a2_cpu_write, NULL, a2_log);
    mos6502_reset(&a2_cpu);
    a2_kbd_key = 0;
    a2_spk_toggles = 0;
}

int a2_step(void) {
    return mos6502_step(&a2_cpu);
}

/* register readouts */
uint16_t a2_get_pc(void) { return a2_cpu.pc; }
uint8_t  a2_get_a(void) { return a2_cpu.a; }
uint8_t  a2_get_x(void) { return a2_cpu.x; }
uint8_t  a2_get_y(void) { return a2_cpu.y; }
uint8_t  a2_get_p(void) { return a2_cpu.p; }

void a2_set_regs(uint16_t pc, uint8_t a, uint8_t x, uint8_t y, uint8_t p) {
    a2_cpu.pc = pc;
    a2_cpu.a = a;
    a2_cpu.x = x;
    a2_cpu.y = y;
    a2_cpu.p = p;
}