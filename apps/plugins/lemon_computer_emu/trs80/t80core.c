#include <stdint.h>
#include <string.h>
#include "../core/zilogz80.h"
#include "../core/logger.h"
#include "t80core.h"

/* ============================================================
   TRS-80 Model I core.
   Only this file touches the Z80 instance.  The optional logger
   is forwarded to the Z80 core via trs80_init.

   Memory map (Model I, Level II):
      0x0000 - 0x2FFF  12K BASIC ROM (read-only)
      0x3000 - 0x37DF  system reserved I/O (unused / read-back 0)
      0x37E8 - 0x37EB  printer interface (Model I)
      0x3800 - 0x3BFF  keyboard scan (memory-mapped, not RAM)
      0x3C00 - 0x3FFF  video display RAM (64x16)
      0x4000 - 0xFFFF  user RAM (size depends on installed memory)

   Keyboard: the matrix is I/O-mapped at 0x3800-0x3BFF.  Only 8
   addresses are actually decoded; the low byte is a rotating row
   select (01,02,04,08,10,20,40,80):
      0x3801 -> row0  @ A B C D E F G
      0x3802 -> row1  H I J K L M N O
      0x3804 -> row2  P Q R S T U V W
      0x3808 -> row3  X Y Z
      0x3810 -> row4  0 1 2 3 4 5 6 7
      0x3820 -> row5  8 9 : ; , - . /
      0x3840 -> row6  ENTER CLEAR BREAK UP DOWN LEFT RIGHT SPACE
      0x3880 -> row7  SHIFT
   Reading one of those addresses returns the byte whose bits are
   the pressed keys of that row (1 = pressed, 0 = released).  The
   ROM keyboard driver at 0x03E3 reads these to build ASCII chars.
   ============================================================ */

/* ---------------- state ---------------- */
uint8_t trs_mem[65536];
uint8_t trs_kb_row[8];
Z80 t80_cpu;
static logger_t* t80_log = NULL;

uint8_t trs80_read(uint16_t addr) {
    /* keyboard scan region 0x3800-0x3BFF */
    if (addr >= 0x3800 && addr < 0x3C00) {
        uint8_t result = 0;
        /* the low byte encodes the row-select bits */
        if (addr & 0x01) result |= trs_kb_row[0]; /* 0x3801 */
        if (addr & 0x02) result |= trs_kb_row[1]; /* 0x3802 */
        if (addr & 0x04) result |= trs_kb_row[2]; /* 0x3804 */
        if (addr & 0x08) result |= trs_kb_row[3]; /* 0x3808 */
        if (addr & 0x10) result |= trs_kb_row[4]; /* 0x3810 */
        if (addr & 0x20) result |= trs_kb_row[5]; /* 0x3820 */
        if (addr & 0x40) result |= trs_kb_row[6]; /* 0x3840 */
        if (addr & 0x80) result |= trs_kb_row[7]; /* 0x3880 */
        return result;
    }
    return trs_mem[addr];
}

void trs80_write(uint16_t addr, uint8_t val) {
    /* keyboard scan area: writes ignored */
    if (addr >= 0x3800 && addr < 0x3C00)
        return;

    /* video display RAM 0x3C00-0x3FFF: writable */
    if (addr >= 0x3C00 && addr <= 0x3FFF) {
        trs_mem[addr] = val;
        return;
    }

    /* user RAM from 0x4000 upward */
    if (addr >= 0x4000) {
        trs_mem[addr] = val;
        return;
    }

    /* everything below 0x3800 is ROM / system I/O: write-protected */
}

/* host callbacks matching the Z80 interface */
static uint8_t t80_cpu_read(void* ctx, uint16_t addr) { (void)ctx; return trs80_read(addr); }
static void t80_cpu_write(void* ctx, uint16_t addr, uint8_t val) { (void)ctx; trs80_write(addr, val); }
static uint8_t t80_cpu_in(void* ctx, uint16_t port)   { (void)ctx; (void)port; return 0xFF; }
static void t80_cpu_out(void* ctx, uint16_t port, uint8_t val) { (void)ctx; (void)port; (void)val; }

void trs80_init(logger_t* log) {
    t80_log = log;
}

void trs80_load_rom_a(const uint8_t* rom, uint32_t size) {
    if (size > 0x2000) size = 0x2000;
    if (size > 0) {
        int i;
        for (i = 0; i < (int)size; i++) trs_mem[i] = rom[i];
    }
}

void trs80_load_rom_b(const uint8_t* rom, uint32_t size) {
    if (size > 0x1000) size = 0x1000;
    if (size > 0) {
        int i;
        for (i = 0; i < (int)size; i++) trs_mem[0x2000 + i] = rom[i];
    }
}

void trs80_reset(void) {
    z80_init(&t80_cpu);
    t80_cpu.mem_read = t80_cpu_read;
    t80_cpu.mem_write = t80_cpu_write;
    t80_cpu.io_read = t80_cpu_in;
    t80_cpu.io_write = t80_cpu_out;
    t80_cpu.ctx = NULL;
    t80_cpu.pc = 0x0000;
    memset(trs_kb_row, 0, sizeof(trs_kb_row));
}

int trs80_step(void) {
    return z80_step(&t80_cpu);
}

/* register readouts */
uint16_t trs80_get_pc(void) { return t80_cpu.pc; }
uint8_t  trs80_get_a(void) { return t80_cpu.a; }
uint8_t  trs80_get_f(void) { return t80_cpu.f; }

void trs80_save_cpu(Z80* out) {
    if (!out) return;
    *out = t80_cpu;
    out->mem_read = NULL;
    out->mem_write = NULL;
    out->io_read = NULL;
    out->io_write = NULL;
    out->ctx = NULL;
}

void trs80_restore_cpu(const Z80* in) {
    if (!in) return;
    t80_cpu = *in;
    t80_cpu.mem_read = t80_cpu_read;
    t80_cpu.mem_write = t80_cpu_write;
    t80_cpu.io_read = t80_cpu_in;
    t80_cpu.io_write = t80_cpu_out;
    t80_cpu.ctx = NULL;
}
