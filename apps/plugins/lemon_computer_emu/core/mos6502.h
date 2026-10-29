/*
 * 6502 CPU emulator - with memory read/write callbacks
 *
 * Header-only. 每个包含本文件的编译单元会展开一份 static 实现。
 * 所有可变状态都放在 mos6502_t 实例里，不再有全局 cpu_read/cpu_write。
 *
 * 日志统一走 core/logger.h 的 logger_t。
 */

#ifndef M6502_H
#define M6502_H

#ifndef MAX_LOG_COUNT
#define MAX_LOG_COUNT 5000
#endif

#include <stdint.h>
#include <stddef.h>

#include "logger.h"

typedef struct mos6502 mos6502_t;

/* Memory callbacks - must be set by user */
typedef uint8_t(*mos6502_read_fn)(mos6502_t* cpu, uint16_t addr);
typedef void    (*mos6502_write_fn)(mos6502_t* cpu, uint16_t addr, uint8_t val);

struct mos6502 {
    /* CPU state */
    uint8_t  a, x, y, sp, p;
    uint16_t pc;
    uint8_t  tmp;

    /* logging */
    int log_counter;
    logger_t* log;

    /* memory callbacks */
    mos6502_read_fn  read;
    mos6502_write_fn write;

    /* user data, e.g. pointer back to emulator memory context */
    void* userdata;
};

/* Function prototypes for all instructions */
static void BRK(mos6502_t* cpu);
static void NOP(mos6502_t* cpu);
static void JAM(mos6502_t* cpu);
static void LDA_a(mos6502_t* cpu);
static void LDA_aX(mos6502_t* cpu);
static void LDA_aY(mos6502_t* cpu);
static void LDA_I(mos6502_t* cpu);
static void LDA_zpg(mos6502_t* cpu);
static void LDA_zpgX(mos6502_t* cpu);
static void LDA_ind_Y(mos6502_t* cpu);
static void LDA_X_ind(mos6502_t* cpu);
static void LDX_a(mos6502_t* cpu);
static void LDX_aY(mos6502_t* cpu);
static void LDX_I(mos6502_t* cpu);
static void LDX_zpg(mos6502_t* cpu);
static void LDX_zpgY(mos6502_t* cpu);
static void LDY_a(mos6502_t* cpu);
static void LDY_aX(mos6502_t* cpu);
static void LDY_I(mos6502_t* cpu);
static void LDY_zpg(mos6502_t* cpu);
static void LDY_zpgX(mos6502_t* cpu);
static void STA_a(mos6502_t* cpu);
static void STA_aX(mos6502_t* cpu);
static void STA_aY(mos6502_t* cpu);
static void STA_zpg(mos6502_t* cpu);
static void STA_zpgX(mos6502_t* cpu);
static void STA_ind_Y(mos6502_t* cpu);
static void STA_X_ind(mos6502_t* cpu);
static void STX_a(mos6502_t* cpu);
static void STX_zpgY(mos6502_t* cpu);
static void STX_zpg(mos6502_t* cpu);
static void STY_a(mos6502_t* cpu);
static void STY_zpgX(mos6502_t* cpu);
static void STY_zpg(mos6502_t* cpu);
static void ADC_a(mos6502_t* cpu);
static void ADC_aX(mos6502_t* cpu);
static void ADC_aY(mos6502_t* cpu);
static void ADC_I(mos6502_t* cpu);
static void ADC_zpg(mos6502_t* cpu);
static void ADC_zpgX(mos6502_t* cpu);
static void ADC_ind_Y(mos6502_t* cpu);
static void ADC_X_ind(mos6502_t* cpu);
static void SBC_a(mos6502_t* cpu);
static void SBC_aX(mos6502_t* cpu);
static void SBC_aY(mos6502_t* cpu);
static void SBC_I(mos6502_t* cpu);
static void SBC_zpg(mos6502_t* cpu);
static void SBC_zpgX(mos6502_t* cpu);
static void SBC_ind_Y(mos6502_t* cpu);
static void SBC_X_ind(mos6502_t* cpu);
static void INC_a(mos6502_t* cpu);
static void INC_aX(mos6502_t* cpu);
static void INC_zpg(mos6502_t* cpu);
static void INC_zpgX(mos6502_t* cpu);
static void INX(mos6502_t* cpu);
static void INY(mos6502_t* cpu);
static void DEC_a(mos6502_t* cpu);
static void DEC_aX(mos6502_t* cpu);
static void DEC_zpg(mos6502_t* cpu);
static void DEC_zpgX(mos6502_t* cpu);
static void DEX(mos6502_t* cpu);
static void DEY(mos6502_t* cpu);
static void ASL_zpg(mos6502_t* cpu);
static void ASL_A(mos6502_t* cpu);
static void ASL_a(mos6502_t* cpu);
static void ASL_zpgX(mos6502_t* cpu);
static void ASL_aX(mos6502_t* cpu);
static void LSR_zpg(mos6502_t* cpu);
static void LSR_A(mos6502_t* cpu);
static void LSR_a(mos6502_t* cpu);
static void LSR_zpgX(mos6502_t* cpu);
static void LSR_aX(mos6502_t* cpu);
static void ROL_zpg(mos6502_t* cpu);
static void ROL_A(mos6502_t* cpu);
static void ROL_a(mos6502_t* cpu);
static void ROL_zpgX(mos6502_t* cpu);
static void ROL_aX(mos6502_t* cpu);
static void ROR_zpg(mos6502_t* cpu);
static void ROR_A(mos6502_t* cpu);
static void ROR_a(mos6502_t* cpu);
static void ROR_zpgX(mos6502_t* cpu);
static void ROR_aX(mos6502_t* cpu);
static void AND_a(mos6502_t* cpu);
static void AND_aX(mos6502_t* cpu);
static void AND_aY(mos6502_t* cpu);
static void AND_I(mos6502_t* cpu);
static void AND_zpg(mos6502_t* cpu);
static void AND_zpgX(mos6502_t* cpu);
static void AND_ind_Y(mos6502_t* cpu);
static void AND_X_ind(mos6502_t* cpu);
static void ORA_a(mos6502_t* cpu);
static void ORA_aX(mos6502_t* cpu);
static void ORA_aY(mos6502_t* cpu);
static void ORA_I(mos6502_t* cpu);
static void ORA_zpg(mos6502_t* cpu);
static void ORA_zpgX(mos6502_t* cpu);
static void ORA_ind_Y(mos6502_t* cpu);
static void ORA_X_ind(mos6502_t* cpu);
static void EOR_a(mos6502_t* cpu);
static void EOR_aX(mos6502_t* cpu);
static void EOR_aY(mos6502_t* cpu);
static void EOR_I(mos6502_t* cpu);
static void EOR_zpg(mos6502_t* cpu);
static void EOR_zpgX(mos6502_t* cpu);
static void EOR_ind_Y(mos6502_t* cpu);
static void EOR_X_ind(mos6502_t* cpu);
static void CMP_a(mos6502_t* cpu);
static void CMP_aX(mos6502_t* cpu);
static void CMP_aY(mos6502_t* cpu);
static void CMP_I(mos6502_t* cpu);
static void CMP_zpg(mos6502_t* cpu);
static void CMP_zpgX(mos6502_t* cpu);
static void CMP_ind_Y(mos6502_t* cpu);
static void CMP_X_ind(mos6502_t* cpu);
static void CPX_a(mos6502_t* cpu);
static void CPX_I(mos6502_t* cpu);
static void CPX_zpg(mos6502_t* cpu);
static void CPY_a(mos6502_t* cpu);
static void CPY_I(mos6502_t* cpu);
static void CPY_zpg(mos6502_t* cpu);
static void BIT_a(mos6502_t* cpu);
static void BIT_I(mos6502_t* cpu);
static void BIT_zpg(mos6502_t* cpu);
static void BCC(mos6502_t* cpu);
static void BCS(mos6502_t* cpu);
static void BNE(mos6502_t* cpu);
static void BEQ(mos6502_t* cpu);
static void BPL(mos6502_t* cpu);
static void BMI(mos6502_t* cpu);
static void BVC(mos6502_t* cpu);
static void BVS(mos6502_t* cpu);
static void TAX(mos6502_t* cpu);
static void TXA(mos6502_t* cpu);
static void TAY(mos6502_t* cpu);
static void TYA(mos6502_t* cpu);
static void TSX(mos6502_t* cpu);
static void TXS(mos6502_t* cpu);
static void PHA(mos6502_t* cpu);
static void PLA(mos6502_t* cpu);
static void PHP(mos6502_t* cpu);
static void PLP(mos6502_t* cpu);
static void JMP_a_ind(mos6502_t* cpu);
static void JMP_a(mos6502_t* cpu);
static void JSR(mos6502_t* cpu);
static void RTS(mos6502_t* cpu);
static void RTI(mos6502_t* cpu);
static void CLC(mos6502_t* cpu);
static void SEC(mos6502_t* cpu);
static void CLD(mos6502_t* cpu);
static void SED(mos6502_t* cpu);
static void CLI(mos6502_t* cpu);
static void SEI(mos6502_t* cpu);
static void CLV(mos6502_t* cpu);

/* Helper functions */
static inline uint16_t dbyte(mos6502_t* cpu, uint16_t addr) {
    return (uint16_t)(cpu->read(cpu, addr + 1) << 8 | cpu->read(cpu, addr));
}

/* 零页间接寻址专用的取字：零页指针的高字节必须在本页内回绕。
   绝对寻址用的 dbyte() 不能回绕，两者不能混用。 */
static inline uint16_t zpdbyte(mos6502_t* cpu, uint8_t addr) {
    return (uint16_t)(cpu->read(cpu, (uint8_t)(addr + 1)) << 8 | cpu->read(cpu, addr));
}

static inline void pushstack(mos6502_t* cpu, uint8_t val) {
    cpu->write(cpu, 0x0100 + cpu->sp, val);
    cpu->sp--;
}

static inline uint8_t pullstack(mos6502_t* cpu) {
    cpu->sp++;
    return cpu->read(cpu, 0x0100 + cpu->sp);
}

static inline int8_t calcrelative(uint8_t operand) {
    if (operand & 0x80)
        return -(~(operand)+1);
    return operand;
}

static inline void set_flags(mos6502_t* cpu, uint8_t val) {
    cpu->p &= ~0b10000010;
    if (val == 0) cpu->p |= 0b00000010;
    if (val & 0x80) cpu->p |= 0b10000000;
}

/* Instruction implementations */
static void BRK(mos6502_t* cpu) {
    uint16_t ret = cpu->pc + 1;
    pushstack(cpu, ret >> 8);
    pushstack(cpu, ret & 0xFF);
    pushstack(cpu, cpu->p | 0b00110000);
    cpu->p |= 0b00000100; /* SEI */
    cpu->pc = dbyte(cpu, 0xFFFE);
}
static void NOP(mos6502_t* cpu) { (void)cpu; /* 1-byte: no PC change */ }
static void JAM(mos6502_t* cpu) { cpu->p |= 0b00010000; cpu->pc++; }

/* LDA */
static void LDA_a(mos6502_t* cpu) {
    cpu->a = cpu->read(cpu, dbyte(cpu, cpu->pc));
    set_flags(cpu, cpu->a);
    cpu->pc += 2;
}
static void LDA_aX(mos6502_t* cpu) {
    cpu->a = cpu->read(cpu, dbyte(cpu, cpu->pc) + cpu->x);
    set_flags(cpu, cpu->a);
    cpu->pc += 2;
}
static void LDA_aY(mos6502_t* cpu) {
    cpu->a = cpu->read(cpu, dbyte(cpu, cpu->pc) + cpu->y);
    set_flags(cpu, cpu->a);
    cpu->pc += 2;
}
static void LDA_I(mos6502_t* cpu) {
    cpu->a = cpu->read(cpu, cpu->pc);
    set_flags(cpu, cpu->a);
    cpu->pc += 1;
}
static void LDA_zpg(mos6502_t* cpu) {
    cpu->a = cpu->read(cpu, cpu->read(cpu, cpu->pc));
    set_flags(cpu, cpu->a);
    cpu->pc += 1;
}
static void LDA_zpgX(mos6502_t* cpu) {
    cpu->a = cpu->read(cpu, (cpu->read(cpu, cpu->pc) + cpu->x) & 0xFF);
    set_flags(cpu, cpu->a);
    cpu->pc += 1;
}
static void LDA_ind_Y(mos6502_t* cpu) {
    cpu->a = cpu->read(cpu, zpdbyte(cpu, cpu->read(cpu, cpu->pc)) + cpu->y);
    set_flags(cpu, cpu->a);
    cpu->pc += 1;
}
static void LDA_X_ind(mos6502_t* cpu) {
    cpu->a = cpu->read(cpu, zpdbyte(cpu, cpu->read(cpu, cpu->pc) + cpu->x));
    set_flags(cpu, cpu->a);
    cpu->pc += 1;
}

/* LDX */
static void LDX_a(mos6502_t* cpu) {
    cpu->x = cpu->read(cpu, dbyte(cpu, cpu->pc));
    set_flags(cpu, cpu->x);
    cpu->pc += 2;
}
static void LDX_aY(mos6502_t* cpu) {
    cpu->x = cpu->read(cpu, dbyte(cpu, cpu->pc) + cpu->y);
    set_flags(cpu, cpu->x);
    cpu->pc += 2;
}
static void LDX_I(mos6502_t* cpu) {
    cpu->x = cpu->read(cpu, cpu->pc);
    set_flags(cpu, cpu->x);
    cpu->pc += 1;
}
static void LDX_zpg(mos6502_t* cpu) {
    cpu->x = cpu->read(cpu, cpu->read(cpu, cpu->pc));
    set_flags(cpu, cpu->x);
    cpu->pc += 1;
}
static void LDX_zpgY(mos6502_t* cpu) {
    cpu->x = cpu->read(cpu, (cpu->read(cpu, cpu->pc) + cpu->y) & 0xFF);
    set_flags(cpu, cpu->x);
    cpu->pc += 1;
}

/* LDY */
static void LDY_a(mos6502_t* cpu) {
    cpu->y = cpu->read(cpu, dbyte(cpu, cpu->pc));
    set_flags(cpu, cpu->y);
    cpu->pc += 2;
}
static void LDY_aX(mos6502_t* cpu) {
    cpu->y = cpu->read(cpu, dbyte(cpu, cpu->pc) + cpu->x);
    set_flags(cpu, cpu->y);
    cpu->pc += 2;
}
static void LDY_I(mos6502_t* cpu) {
    cpu->y = cpu->read(cpu, cpu->pc);
    set_flags(cpu, cpu->y);
    cpu->pc += 1;
}
static void LDY_zpg(mos6502_t* cpu) {
    cpu->y = cpu->read(cpu, cpu->read(cpu, cpu->pc));
    set_flags(cpu, cpu->y);
    cpu->pc += 1;
}
static void LDY_zpgX(mos6502_t* cpu) {
    cpu->y = cpu->read(cpu, (cpu->read(cpu, cpu->pc) + cpu->x) & 0xFF);
    set_flags(cpu, cpu->y);
    cpu->pc += 1;
}

/* STA */
static void STA_a(mos6502_t* cpu) {
    cpu->write(cpu, dbyte(cpu, cpu->pc), cpu->a);
    cpu->pc += 2;
}
static void STA_aX(mos6502_t* cpu) {
    cpu->write(cpu, dbyte(cpu, cpu->pc) + cpu->x, cpu->a);
    cpu->pc += 2;
}
static void STA_aY(mos6502_t* cpu) {
    cpu->write(cpu, dbyte(cpu, cpu->pc) + cpu->y, cpu->a);
    cpu->pc += 2;
}
static void STA_zpg(mos6502_t* cpu) {
    cpu->write(cpu, cpu->read(cpu, cpu->pc), cpu->a);
    cpu->pc += 1;
}
static void STA_zpgX(mos6502_t* cpu) {
    cpu->write(cpu, (cpu->read(cpu, cpu->pc) + cpu->x) & 0xFF, cpu->a);
    cpu->pc += 1;
}
static void STA_ind_Y(mos6502_t* cpu) {
    cpu->write(cpu, zpdbyte(cpu, cpu->read(cpu, cpu->pc)) + cpu->y, cpu->a);
    cpu->pc += 1;
}
static void STA_X_ind(mos6502_t* cpu) {
    cpu->write(cpu, zpdbyte(cpu, cpu->read(cpu, cpu->pc) + cpu->x), cpu->a);
    cpu->pc += 1;
}

/* STX */
static void STX_a(mos6502_t* cpu) {
    cpu->write(cpu, dbyte(cpu, cpu->pc), cpu->x);
    cpu->pc += 2;
}
static void STX_zpgY(mos6502_t* cpu) {
    cpu->write(cpu, (cpu->read(cpu, cpu->pc) + cpu->y) & 0xFF, cpu->x);
    cpu->pc += 1;
}
static void STX_zpg(mos6502_t* cpu) {
    cpu->write(cpu, cpu->read(cpu, cpu->pc), cpu->x);
    cpu->pc += 1;
}

/* STY */
static void STY_a(mos6502_t* cpu) {
    cpu->write(cpu, dbyte(cpu, cpu->pc), cpu->y);
    cpu->pc += 2;
}
static void STY_zpgX(mos6502_t* cpu) {
    cpu->write(cpu, (cpu->read(cpu, cpu->pc) + cpu->x) & 0xFF, cpu->y);
    cpu->pc += 1;
}
static void STY_zpg(mos6502_t* cpu) {
    cpu->write(cpu, cpu->read(cpu, cpu->pc), cpu->y);
    cpu->pc += 1;
}

/* ADC - with 6502 decimal (BCD) mode support */
static void adc_common(mos6502_t* cpu, uint8_t operand) {
    uint16_t temp;
    uint16_t result;
    if (cpu->p & 0b00001000) {
        /* Decimal mode: adjust each nibble separately */
        temp = (uint16_t)(cpu->a & 0x0F) + (operand & 0x0F) + (cpu->p & 1);
        if (temp > 9)
            temp += 6;
        temp += (uint16_t)(cpu->a & 0xF0) + (operand & 0xF0);
        if (temp > 0x9F)
            temp += 0x60;
        cpu->p &= ~0b11000011;
        if (temp > 0xFF)
            cpu->p |= 0b00000001;
        cpu->a = temp & 0xFF;
        if (cpu->a == 0)
            cpu->p |= 0b00000010;
        if (cpu->a & 0x80)
            cpu->p |= 0b10000000;
    }
    else {
        temp = (uint16_t)(cpu->p & 1) + operand;
        result = (uint16_t)cpu->a + temp;
        cpu->p &= ~0b11000011;
        cpu->p |= ((result & 0x80) | ((result & 0x100) >> 8) | (((result & 0xFF) == 0) << 1));
        cpu->p |= ((((temp & 0x80) == (cpu->a & 0x80)) && ((cpu->a & 0x80) != (result & 0x80))) ? 0x40 : 0);
        cpu->a = result & 0xFF;
    }
}
static void ADC_a(mos6502_t* cpu) {
    adc_common(cpu, cpu->read(cpu, dbyte(cpu, cpu->pc)));
    cpu->pc += 2;
}
static void ADC_aX(mos6502_t* cpu) {
    adc_common(cpu, cpu->read(cpu, dbyte(cpu, cpu->pc) + cpu->x));
    cpu->pc += 2;
}
static void ADC_aY(mos6502_t* cpu) {
    adc_common(cpu, cpu->read(cpu, dbyte(cpu, cpu->pc) + cpu->y));
    cpu->pc += 2;
}
static void ADC_I(mos6502_t* cpu) {
    adc_common(cpu, cpu->read(cpu, cpu->pc));
    cpu->pc += 1;
}
static void ADC_zpg(mos6502_t* cpu) {
    adc_common(cpu, cpu->read(cpu, cpu->read(cpu, cpu->pc)));
    cpu->pc += 1;
}
static void ADC_zpgX(mos6502_t* cpu) {
    adc_common(cpu, cpu->read(cpu, (cpu->read(cpu, cpu->pc) + cpu->x) & 0xFF));
    cpu->pc += 1;
}
static void ADC_ind_Y(mos6502_t* cpu) {
    adc_common(cpu, cpu->read(cpu, zpdbyte(cpu, cpu->read(cpu, cpu->pc)) + cpu->y));
    cpu->pc += 1;
}
static void ADC_X_ind(mos6502_t* cpu) {
    adc_common(cpu, cpu->read(cpu, zpdbyte(cpu, cpu->read(cpu, cpu->pc) + cpu->x)));
    cpu->pc += 1;
}

/* SBC - with 6502 decimal (BCD) mode support */
static void sbc_common(mos6502_t* cpu, uint8_t operand) {
    uint16_t temp;
    uint16_t result;
    if (cpu->p & 0b00001000) {
        /* Decimal mode */
        uint8_t carry_in = cpu->p & 1;
        int lo = (cpu->a & 0x0F) - (operand & 0x0F) - (carry_in ? 0 : 1);
        int hi = (cpu->a >> 4) - (operand >> 4);
        int borrow = 0;
        if (lo < 0) {
            lo += 10;
            hi--;
        }
        if (hi < 0) {
            hi += 10;
            borrow = 1;
        }
        cpu->p &= ~0b11000011;
        if (!borrow)
            cpu->p |= 0b00000001;
        cpu->a = (uint8_t)(((hi & 0x0F) << 4) | (lo & 0x0F));
        if (cpu->a == 0)
            cpu->p |= 0b00000010;
        if (cpu->a & 0x80)
            cpu->p |= 0b10000000;
    }
    else {
        temp = (uint16_t)(operand ^ 0xFF) + (cpu->p & 1);
        result = (uint16_t)cpu->a + temp;
        cpu->p &= ~0b11000011;
        cpu->p |= ((result & 0x80) | ((result & 0x100) >> 8) | (((result & 0xFF) == 0) << 1));
        cpu->p |= ((((temp & 0x80) == (cpu->a & 0x80)) && ((cpu->a & 0x80) != (result & 0x80))) ? 0x40 : 0);
        cpu->a = result & 0xFF;
    }
}
static void SBC_a(mos6502_t* cpu) {
    sbc_common(cpu, cpu->read(cpu, dbyte(cpu, cpu->pc)));
    cpu->pc += 2;
}
static void SBC_aX(mos6502_t* cpu) {
    sbc_common(cpu, cpu->read(cpu, dbyte(cpu, cpu->pc) + cpu->x));
    cpu->pc += 2;
}
static void SBC_aY(mos6502_t* cpu) {
    sbc_common(cpu, cpu->read(cpu, dbyte(cpu, cpu->pc) + cpu->y));
    cpu->pc += 2;
}
static void SBC_I(mos6502_t* cpu) {
    sbc_common(cpu, cpu->read(cpu, cpu->pc));
    cpu->pc += 1;
}
static void SBC_zpg(mos6502_t* cpu) {
    sbc_common(cpu, cpu->read(cpu, cpu->read(cpu, cpu->pc)));
    cpu->pc += 1;
}
static void SBC_zpgX(mos6502_t* cpu) {
    sbc_common(cpu, cpu->read(cpu, (cpu->read(cpu, cpu->pc) + cpu->x) & 0xFF));
    cpu->pc += 1;
}
static void SBC_ind_Y(mos6502_t* cpu) {
    sbc_common(cpu, cpu->read(cpu, zpdbyte(cpu, cpu->read(cpu, cpu->pc)) + cpu->y));
    cpu->pc += 1;
}
static void SBC_X_ind(mos6502_t* cpu) {
    sbc_common(cpu, cpu->read(cpu, zpdbyte(cpu, cpu->read(cpu, cpu->pc) + cpu->x)));
    cpu->pc += 1;
}

/* INC */
static void INC_a(mos6502_t* cpu) {
    uint8_t val = cpu->read(cpu, dbyte(cpu, cpu->pc)) + 1;
    cpu->write(cpu, dbyte(cpu, cpu->pc), val);
    set_flags(cpu, val);
    cpu->pc += 2;
}
static void INC_aX(mos6502_t* cpu) {
    uint8_t val = cpu->read(cpu, dbyte(cpu, cpu->pc) + cpu->x) + 1;
    cpu->write(cpu, dbyte(cpu, cpu->pc) + cpu->x, val);
    set_flags(cpu, val);
    cpu->pc += 2;
}
static void INC_zpg(mos6502_t* cpu) {
    uint8_t val = cpu->read(cpu, cpu->read(cpu, cpu->pc)) + 1;
    cpu->write(cpu, cpu->read(cpu, cpu->pc), val);
    set_flags(cpu, val);
    cpu->pc += 1;
}
static void INC_zpgX(mos6502_t* cpu) {
    uint8_t val = cpu->read(cpu, (cpu->read(cpu, cpu->pc) + cpu->x) & 0xFF) + 1;
    cpu->write(cpu, (cpu->read(cpu, cpu->pc) + cpu->x) & 0xFF, val);
    set_flags(cpu, val);
    cpu->pc += 1;
}
static void INX(mos6502_t* cpu) {
    cpu->x++;
    set_flags(cpu, cpu->x);
}
static void INY(mos6502_t* cpu) {
    cpu->y++;
    set_flags(cpu, cpu->y);
}

/* DEC */
static void DEC_a(mos6502_t* cpu) {
    uint8_t val = cpu->read(cpu, dbyte(cpu, cpu->pc)) - 1;
    cpu->write(cpu, dbyte(cpu, cpu->pc), val);
    set_flags(cpu, val);
    cpu->pc += 2;
}
static void DEC_aX(mos6502_t* cpu) {
    uint8_t val = cpu->read(cpu, dbyte(cpu, cpu->pc) + cpu->x) - 1;
    cpu->write(cpu, dbyte(cpu, cpu->pc) + cpu->x, val);
    set_flags(cpu, val);
    cpu->pc += 2;
}
static void DEC_zpg(mos6502_t* cpu) {
    uint8_t val = cpu->read(cpu, cpu->read(cpu, cpu->pc)) - 1;
    cpu->write(cpu, cpu->read(cpu, cpu->pc), val);
    set_flags(cpu, val);
    cpu->pc += 1;
}
static void DEC_zpgX(mos6502_t* cpu) {
    uint8_t val = cpu->read(cpu, (cpu->read(cpu, cpu->pc) + cpu->x) & 0xFF) - 1;
    cpu->write(cpu, (cpu->read(cpu, cpu->pc) + cpu->x) & 0xFF, val);
    set_flags(cpu, val);
    cpu->pc += 1;
}
static void DEX(mos6502_t* cpu) {
    cpu->x--;
    set_flags(cpu, cpu->x);
}
static void DEY(mos6502_t* cpu) {
    cpu->y--;
    set_flags(cpu, cpu->y);
}

/* ASL */
static void ASL_zpg(mos6502_t* cpu) {
    uint8_t val = cpu->read(cpu, cpu->read(cpu, cpu->pc));
    cpu->p = (cpu->p & ~0b10000011) | ((val & 0x80) >> 7);
    val <<= 1;
    cpu->write(cpu, cpu->read(cpu, cpu->pc), val);
    set_flags(cpu, val);
    cpu->pc += 1;
}
static void ASL_A(mos6502_t* cpu) {
    cpu->p = (cpu->p & ~0b10000011) | ((cpu->a & 0x80) >> 7);
    cpu->a <<= 1;
    set_flags(cpu, cpu->a);
}
static void ASL_a(mos6502_t* cpu) {
    uint8_t val = cpu->read(cpu, dbyte(cpu, cpu->pc));
    cpu->p = (cpu->p & ~0b10000011) | ((val & 0x80) >> 7);
    val <<= 1;
    cpu->write(cpu, dbyte(cpu, cpu->pc), val);
    set_flags(cpu, val);
    cpu->pc += 2;
}
static void ASL_zpgX(mos6502_t* cpu) {
    uint8_t val = cpu->read(cpu, (cpu->read(cpu, cpu->pc) + cpu->x) & 0xFF);
    cpu->p = (cpu->p & ~0b10000011) | ((val & 0x80) >> 7);
    val <<= 1;
    cpu->write(cpu, (cpu->read(cpu, cpu->pc) + cpu->x) & 0xFF, val);
    set_flags(cpu, val);
    cpu->pc += 1;
}
static void ASL_aX(mos6502_t* cpu) {
    uint8_t val = cpu->read(cpu, dbyte(cpu, cpu->pc) + cpu->x);
    cpu->p = (cpu->p & ~0b10000011) | ((val & 0x80) >> 7);
    val <<= 1;
    cpu->write(cpu, dbyte(cpu, cpu->pc) + cpu->x, val);
    set_flags(cpu, val);
    cpu->pc += 2;
}

/* LSR */
static void LSR_zpg(mos6502_t* cpu) {
    uint8_t val = cpu->read(cpu, cpu->read(cpu, cpu->pc));
    cpu->p = (cpu->p & ~0b10000011) | (val & 1);
    val >>= 1;
    cpu->write(cpu, cpu->read(cpu, cpu->pc), val);
    cpu->p = (cpu->p & ~0b00000010) | ((val == 0) << 1);
    cpu->pc += 1;
}
static void LSR_A(mos6502_t* cpu) {
    cpu->p = (cpu->p & ~0b10000011) | (cpu->a & 1);
    cpu->a >>= 1;
    cpu->p = (cpu->p & ~0b00000010) | ((cpu->a == 0) << 1);
}
static void LSR_a(mos6502_t* cpu) {
    uint8_t val = cpu->read(cpu, dbyte(cpu, cpu->pc));
    cpu->p = (cpu->p & ~0b10000011) | (val & 1);
    val >>= 1;
    cpu->write(cpu, dbyte(cpu, cpu->pc), val);
    cpu->p = (cpu->p & ~0b00000010) | ((val == 0) << 1);
    cpu->pc += 2;
}
static void LSR_zpgX(mos6502_t* cpu) {
    uint8_t val = cpu->read(cpu, (cpu->read(cpu, cpu->pc) + cpu->x) & 0xFF);
    cpu->p = (cpu->p & ~0b10000011) | (val & 1);
    val >>= 1;
    cpu->write(cpu, (cpu->read(cpu, cpu->pc) + cpu->x) & 0xFF, val);
    cpu->p = (cpu->p & ~0b00000010) | ((val == 0) << 1);
    cpu->pc += 1;
}
static void LSR_aX(mos6502_t* cpu) {
    uint8_t val = cpu->read(cpu, dbyte(cpu, cpu->pc) + cpu->x);
    cpu->p = (cpu->p & ~0b10000011) | (val & 1);
    val >>= 1;
    cpu->write(cpu, dbyte(cpu, cpu->pc) + cpu->x, val);
    cpu->p = (cpu->p & ~0b00000010) | ((val == 0) << 1);
    cpu->pc += 2;
}

/* ROL */
static void ROL_zpg(mos6502_t* cpu) {
    uint8_t val = cpu->read(cpu, cpu->read(cpu, cpu->pc));
    uint8_t carry = (cpu->p & 1);
    cpu->p = (cpu->p & ~0b10000011) | ((val & 0x80) >> 7);
    val = (val << 1) | carry;
    cpu->write(cpu, cpu->read(cpu, cpu->pc), val);
    set_flags(cpu, val);
    cpu->pc += 1;
}
static void ROL_A(mos6502_t* cpu) {
    uint8_t carry = (cpu->p & 1);
    cpu->p = (cpu->p & ~0b10000011) | ((cpu->a & 0x80) >> 7);
    cpu->a = (cpu->a << 1) | carry;
    set_flags(cpu, cpu->a);
}
static void ROL_a(mos6502_t* cpu) {
    uint8_t val = cpu->read(cpu, dbyte(cpu, cpu->pc));
    uint8_t carry = (cpu->p & 1);
    cpu->p = (cpu->p & ~0b10000011) | ((val & 0x80) >> 7);
    val = (val << 1) | carry;
    cpu->write(cpu, dbyte(cpu, cpu->pc), val);
    set_flags(cpu, val);
    cpu->pc += 2;
}
static void ROL_zpgX(mos6502_t* cpu) {
    uint8_t val = cpu->read(cpu, (cpu->read(cpu, cpu->pc) + cpu->x) & 0xFF);
    uint8_t carry = (cpu->p & 1);
    cpu->p = (cpu->p & ~0b10000011) | ((val & 0x80) >> 7);
    val = (val << 1) | carry;
    cpu->write(cpu, (cpu->read(cpu, cpu->pc) + cpu->x) & 0xFF, val);
    set_flags(cpu, val);
    cpu->pc += 1;
}
static void ROL_aX(mos6502_t* cpu) {
    uint8_t val = cpu->read(cpu, dbyte(cpu, cpu->pc) + cpu->x);
    uint8_t carry = (cpu->p & 1);
    cpu->p = (cpu->p & ~0b10000011) | ((val & 0x80) >> 7);
    val = (val << 1) | carry;
    cpu->write(cpu, dbyte(cpu, cpu->pc) + cpu->x, val);
    set_flags(cpu, val);
    cpu->pc += 2;
}

/* ROR */
static void ROR_zpg(mos6502_t* cpu) {
    uint8_t val = cpu->read(cpu, cpu->read(cpu, cpu->pc));
    uint8_t carry = (cpu->p & 1);
    cpu->p = (cpu->p & ~1) | (val & 1);
    val = (val >> 1) | (carry << 7);
    cpu->write(cpu, cpu->read(cpu, cpu->pc), val);
    set_flags(cpu, val);
    cpu->pc += 1;
}
static void ROR_A(mos6502_t* cpu) {
    uint8_t carry = (cpu->p & 1);
    cpu->p = (cpu->p & ~1) | (cpu->a & 1);
    cpu->a = (cpu->a >> 1) | (carry << 7);
    set_flags(cpu, cpu->a);
}
static void ROR_a(mos6502_t* cpu) {
    uint8_t val = cpu->read(cpu, dbyte(cpu, cpu->pc));
    uint8_t carry = (cpu->p & 1);
    cpu->p = (cpu->p & ~1) | (val & 1);
    val = (val >> 1) | (carry << 7);
    cpu->write(cpu, dbyte(cpu, cpu->pc), val);
    set_flags(cpu, val);
    cpu->pc += 2;
}
static void ROR_zpgX(mos6502_t* cpu) {
    uint8_t val = cpu->read(cpu, (cpu->read(cpu, cpu->pc) + cpu->x) & 0xFF);
    uint8_t carry = (cpu->p & 1);
    cpu->p = (cpu->p & ~1) | (val & 1);
    val = (val >> 1) | (carry << 7);
    cpu->write(cpu, (cpu->read(cpu, cpu->pc) + cpu->x) & 0xFF, val);
    set_flags(cpu, val);
    cpu->pc += 1;
}
static void ROR_aX(mos6502_t* cpu) {
    uint8_t val = cpu->read(cpu, dbyte(cpu, cpu->pc) + cpu->x);
    uint8_t carry = (cpu->p & 1);
    cpu->p = (cpu->p & ~1) | (val & 1);
    val = (val >> 1) | (carry << 7);
    cpu->write(cpu, dbyte(cpu, cpu->pc) + cpu->x, val);
    set_flags(cpu, val);
    cpu->pc += 2;
}

/* AND */
static void AND_a(mos6502_t* cpu) {
    cpu->a &= cpu->read(cpu, dbyte(cpu, cpu->pc));
    set_flags(cpu, cpu->a);
    cpu->pc += 2;
}
static void AND_aX(mos6502_t* cpu) {
    cpu->a &= cpu->read(cpu, dbyte(cpu, cpu->pc) + cpu->x);
    set_flags(cpu, cpu->a);
    cpu->pc += 2;
}
static void AND_aY(mos6502_t* cpu) {
    cpu->a &= cpu->read(cpu, dbyte(cpu, cpu->pc) + cpu->y);
    set_flags(cpu, cpu->a);
    cpu->pc += 2;
}
static void AND_I(mos6502_t* cpu) {
    cpu->a &= cpu->read(cpu, cpu->pc);
    set_flags(cpu, cpu->a);
    cpu->pc += 1;
}
static void AND_zpg(mos6502_t* cpu) {
    cpu->a &= cpu->read(cpu, cpu->read(cpu, cpu->pc));
    set_flags(cpu, cpu->a);
    cpu->pc += 1;
}
static void AND_zpgX(mos6502_t* cpu) {
    cpu->a &= cpu->read(cpu, (cpu->read(cpu, cpu->pc) + cpu->x) & 0xFF);
    set_flags(cpu, cpu->a);
    cpu->pc += 1;
}
static void AND_ind_Y(mos6502_t* cpu) {
    cpu->a &= cpu->read(cpu, zpdbyte(cpu, cpu->read(cpu, cpu->pc)) + cpu->y);
    set_flags(cpu, cpu->a);
    cpu->pc += 1;
}
static void AND_X_ind(mos6502_t* cpu) {
    cpu->a &= cpu->read(cpu, zpdbyte(cpu, cpu->read(cpu, cpu->pc) + cpu->x));
    set_flags(cpu, cpu->a);
    cpu->pc += 1;
}

/* ORA */
static void ORA_a(mos6502_t* cpu) {
    cpu->a |= cpu->read(cpu, dbyte(cpu, cpu->pc));
    set_flags(cpu, cpu->a);
    cpu->pc += 2;
}
static void ORA_aX(mos6502_t* cpu) {
    cpu->a |= cpu->read(cpu, dbyte(cpu, cpu->pc) + cpu->x);
    set_flags(cpu, cpu->a);
    cpu->pc += 2;
}
static void ORA_aY(mos6502_t* cpu) {
    cpu->a |= cpu->read(cpu, dbyte(cpu, cpu->pc) + cpu->y);
    set_flags(cpu, cpu->a);
    cpu->pc += 2;
}
static void ORA_I(mos6502_t* cpu) {
    cpu->a |= cpu->read(cpu, cpu->pc);
    set_flags(cpu, cpu->a);
    cpu->pc += 1;
}
static void ORA_zpg(mos6502_t* cpu) {
    cpu->a |= cpu->read(cpu, cpu->read(cpu, cpu->pc));
    set_flags(cpu, cpu->a);
    cpu->pc += 1;
}
static void ORA_zpgX(mos6502_t* cpu) {
    cpu->a |= cpu->read(cpu, (cpu->read(cpu, cpu->pc) + cpu->x) & 0xFF);
    set_flags(cpu, cpu->a);
    cpu->pc += 1;
}
static void ORA_ind_Y(mos6502_t* cpu) {
    cpu->a |= cpu->read(cpu, zpdbyte(cpu, cpu->read(cpu, cpu->pc)) + cpu->y);
    set_flags(cpu, cpu->a);
    cpu->pc += 1;
}
static void ORA_X_ind(mos6502_t* cpu) {
    cpu->a |= cpu->read(cpu, zpdbyte(cpu, cpu->read(cpu, cpu->pc) + cpu->x));
    set_flags(cpu, cpu->a);
    cpu->pc += 1;
}

/* EOR */
static void EOR_a(mos6502_t* cpu) {
    cpu->a ^= cpu->read(cpu, dbyte(cpu, cpu->pc));
    set_flags(cpu, cpu->a);
    cpu->pc += 2;
}
static void EOR_aX(mos6502_t* cpu) {
    cpu->a ^= cpu->read(cpu, dbyte(cpu, cpu->pc) + cpu->x);
    set_flags(cpu, cpu->a);
    cpu->pc += 2;
}
static void EOR_aY(mos6502_t* cpu) {
    cpu->a ^= cpu->read(cpu, dbyte(cpu, cpu->pc) + cpu->y);
    set_flags(cpu, cpu->a);
    cpu->pc += 2;
}
static void EOR_I(mos6502_t* cpu) {
    cpu->a ^= cpu->read(cpu, cpu->pc);
    set_flags(cpu, cpu->a);
    cpu->pc += 1;
}
static void EOR_zpg(mos6502_t* cpu) {
    cpu->a ^= cpu->read(cpu, cpu->read(cpu, cpu->pc));
    set_flags(cpu, cpu->a);
    cpu->pc += 1;
}
static void EOR_zpgX(mos6502_t* cpu) {
    cpu->a ^= cpu->read(cpu, (cpu->read(cpu, cpu->pc) + cpu->x) & 0xFF);
    set_flags(cpu, cpu->a);
    cpu->pc += 1;
}
static void EOR_ind_Y(mos6502_t* cpu) {
    cpu->a ^= cpu->read(cpu, zpdbyte(cpu, cpu->read(cpu, cpu->pc)) + cpu->y);
    set_flags(cpu, cpu->a);
    cpu->pc += 1;
}
static void EOR_X_ind(mos6502_t* cpu) {
    cpu->a ^= cpu->read(cpu, zpdbyte(cpu, cpu->read(cpu, cpu->pc) + cpu->x));
    set_flags(cpu, cpu->a);
    cpu->pc += 1;
}

/* CMP */
static void CMP_a(mos6502_t* cpu) {
    uint8_t val = cpu->read(cpu, dbyte(cpu, cpu->pc));
    cpu->p = (cpu->p & ~0b10000011) | ((cpu->a >= val) ? 1 : 0) | ((cpu->a == val) << 1) | ((uint8_t)(cpu->a - val) & 0x80);
    cpu->pc += 2;
}
static void CMP_aX(mos6502_t* cpu) {
    uint8_t val = cpu->read(cpu, dbyte(cpu, cpu->pc) + cpu->x);
    cpu->p = (cpu->p & ~0b10000011) | ((cpu->a >= val) ? 1 : 0) | ((cpu->a == val) << 1) | ((uint8_t)(cpu->a - val) & 0x80);
    cpu->pc += 2;
}
static void CMP_aY(mos6502_t* cpu) {
    uint8_t val = cpu->read(cpu, dbyte(cpu, cpu->pc) + cpu->y);
    cpu->p = (cpu->p & ~0b10000011) | ((cpu->a >= val) ? 1 : 0) | ((cpu->a == val) << 1) | ((uint8_t)(cpu->a - val) & 0x80);
    cpu->pc += 2;
}
static void CMP_I(mos6502_t* cpu) {
    uint8_t val = cpu->read(cpu, cpu->pc);
    cpu->p = (cpu->p & ~0b10000011) | ((cpu->a >= val) ? 1 : 0) | ((cpu->a == val) << 1) | ((uint8_t)(cpu->a - val) & 0x80);
    cpu->pc += 1;
}
static void CMP_zpg(mos6502_t* cpu) {
    uint8_t val = cpu->read(cpu, cpu->read(cpu, cpu->pc));
    cpu->p = (cpu->p & ~0b10000011) | ((cpu->a >= val) ? 1 : 0) | ((cpu->a == val) << 1) | ((uint8_t)(cpu->a - val) & 0x80);
    cpu->pc += 1;
}
static void CMP_zpgX(mos6502_t* cpu) {
    uint8_t val = cpu->read(cpu, (cpu->read(cpu, cpu->pc) + cpu->x) & 0xFF);
    cpu->p = (cpu->p & ~0b10000011) | ((cpu->a >= val) ? 1 : 0) | ((cpu->a == val) << 1) | ((uint8_t)(cpu->a - val) & 0x80);
    cpu->pc += 1;
}
static void CMP_ind_Y(mos6502_t* cpu) {
    uint8_t val = cpu->read(cpu, zpdbyte(cpu, cpu->read(cpu, cpu->pc)) + cpu->y);
    cpu->p = (cpu->p & ~0b10000011) | ((cpu->a >= val) ? 1 : 0) | ((cpu->a == val) << 1) | ((uint8_t)(cpu->a - val) & 0x80);
    cpu->pc += 1;
}
static void CMP_X_ind(mos6502_t* cpu) {
    uint8_t val = cpu->read(cpu, zpdbyte(cpu, cpu->read(cpu, cpu->pc) + cpu->x));
    cpu->p = (cpu->p & ~0b10000011) | ((cpu->a >= val) ? 1 : 0) | ((cpu->a == val) << 1) | ((uint8_t)(cpu->a - val) & 0x80);
    cpu->pc += 1;
}

/* CPX */
static void CPX_a(mos6502_t* cpu) {
    uint8_t val = cpu->read(cpu, dbyte(cpu, cpu->pc));
    cpu->p = (cpu->p & ~0b10000011) | ((cpu->x >= val) ? 1 : 0) | ((cpu->x == val) << 1) | ((uint8_t)(cpu->x - val) & 0x80);
    cpu->pc += 2;
}
static void CPX_I(mos6502_t* cpu) {
    uint8_t val = cpu->read(cpu, cpu->pc);
    cpu->p = (cpu->p & ~0b10000011) | ((cpu->x >= val) ? 1 : 0) | ((cpu->x == val) << 1) | ((uint8_t)(cpu->x - val) & 0x80);
    cpu->pc += 1;
}
static void CPX_zpg(mos6502_t* cpu) {
    uint8_t val = cpu->read(cpu, cpu->read(cpu, cpu->pc));
    cpu->p = (cpu->p & ~0b10000011) | ((cpu->x >= val) ? 1 : 0) | ((cpu->x == val) << 1) | ((uint8_t)(cpu->x - val) & 0x80);
    cpu->pc += 1;
}

/* CPY */
static void CPY_a(mos6502_t* cpu) {
    uint8_t val = cpu->read(cpu, dbyte(cpu, cpu->pc));
    cpu->p = (cpu->p & ~0b10000011) | ((cpu->y >= val) ? 1 : 0) | ((cpu->y == val) << 1) | ((uint8_t)(cpu->y - val) & 0x80);
    cpu->pc += 2;
}
static void CPY_I(mos6502_t* cpu) {
    uint8_t val = cpu->read(cpu, cpu->pc);
    cpu->p = (cpu->p & ~0b10000011) | ((cpu->y >= val) ? 1 : 0) | ((cpu->y == val) << 1) | ((uint8_t)(cpu->y - val) & 0x80);
    cpu->pc += 1;
}
static void CPY_zpg(mos6502_t* cpu) {
    uint8_t val = cpu->read(cpu, cpu->read(cpu, cpu->pc));
    cpu->p = (cpu->p & ~0b10000011) | ((cpu->y >= val) ? 1 : 0) | ((cpu->y == val) << 1) | ((uint8_t)(cpu->y - val) & 0x80);
    cpu->pc += 1;
}

/* BIT */
static void BIT_a(mos6502_t* cpu) {
    uint8_t val = cpu->read(cpu, dbyte(cpu, cpu->pc));
    cpu->p = (cpu->p & ~0b11000010) | (val & 0b11000000) | (((val & cpu->a) == 0) << 1);
    cpu->pc += 2;
}
static void BIT_I(mos6502_t* cpu) {
    uint8_t val = cpu->read(cpu, cpu->pc);
    cpu->p = (cpu->p & ~0b11000010) | (val & 0b11000000) | (((val & cpu->a) == 0) << 1);
    cpu->pc += 1;
}
static void BIT_zpg(mos6502_t* cpu) {
    uint8_t val = cpu->read(cpu, cpu->read(cpu, cpu->pc));
    cpu->p = (cpu->p & ~0b11000010) | (val & 0b11000000) | (((val & cpu->a) == 0) << 1);
    cpu->pc += 1;
}

/* Branches */
static void BCC(mos6502_t* cpu) {
    if (!(cpu->p & 1)) cpu->pc += calcrelative(cpu->read(cpu, cpu->pc));
    cpu->pc += 1;
}
static void BCS(mos6502_t* cpu) {
    if (cpu->p & 1) cpu->pc += calcrelative(cpu->read(cpu, cpu->pc));
    cpu->pc += 1;
}
static void BNE(mos6502_t* cpu) {
    if (!(cpu->p & 2)) cpu->pc += calcrelative(cpu->read(cpu, cpu->pc));
    cpu->pc += 1;
}
static void BEQ(mos6502_t* cpu) {
    if (cpu->p & 2) cpu->pc += calcrelative(cpu->read(cpu, cpu->pc));
    cpu->pc += 1;
}
static void BPL(mos6502_t* cpu) {
    if (!(cpu->p & 0x80)) cpu->pc += calcrelative(cpu->read(cpu, cpu->pc));
    cpu->pc += 1;
}
static void BMI(mos6502_t* cpu) {
    if (cpu->p & 0x80) cpu->pc += calcrelative(cpu->read(cpu, cpu->pc));
    cpu->pc += 1;
}
static void BVC(mos6502_t* cpu) {
    if (!(cpu->p & 0x40)) cpu->pc += calcrelative(cpu->read(cpu, cpu->pc));
    cpu->pc += 1;
}
static void BVS(mos6502_t* cpu) {
    if (cpu->p & 0x40) cpu->pc += calcrelative(cpu->read(cpu, cpu->pc));
    cpu->pc += 1;
}

/* Transfers */
static void TAX(mos6502_t* cpu) {
    cpu->x = cpu->a;
    set_flags(cpu, cpu->x);
}
static void TXA(mos6502_t* cpu) {
    cpu->a = cpu->x;
    set_flags(cpu, cpu->a);
}
static void TAY(mos6502_t* cpu) {
    cpu->y = cpu->a;
    set_flags(cpu, cpu->y);
}
static void TYA(mos6502_t* cpu) {
    cpu->a = cpu->y;
    set_flags(cpu, cpu->a);
}
static void TSX(mos6502_t* cpu) {
    cpu->x = cpu->sp;
    set_flags(cpu, cpu->x);
}
static void TXS(mos6502_t* cpu) {
    cpu->sp = cpu->x;
}

/* Stack */
static void PHA(mos6502_t* cpu) {
    pushstack(cpu, cpu->a);
}
static void PLA(mos6502_t* cpu) {
    cpu->a = pullstack(cpu);
    set_flags(cpu, cpu->a);
}
static void PHP(mos6502_t* cpu) {
    pushstack(cpu, cpu->p | 0b00110000);
}
static void PLP(mos6502_t* cpu) {
    cpu->p = pullstack(cpu) & 0b11101111;
}

/* JMP */
static void JMP_a_ind(mos6502_t* cpu) {
    uint16_t addr = dbyte(cpu, cpu->pc);
    if ((addr & 0xFF) == 0xFF) {
        addr = cpu->read(cpu, addr) | (cpu->read(cpu, addr & 0xFF00) << 8);
    }
    else {
        addr = cpu->read(cpu, addr) | (cpu->read(cpu, addr + 1) << 8);
    }
    cpu->pc = addr;
}
static void JMP_a(mos6502_t* cpu) {
    cpu->pc = dbyte(cpu, cpu->pc);
}

/* JSR / RTS / RTI */
static void JSR(mos6502_t* cpu) {
    uint16_t ret = cpu->pc + 1;
    pushstack(cpu, ret >> 8);
    pushstack(cpu, ret & 0xFF);
    cpu->pc = dbyte(cpu, cpu->pc);
}
static void RTS(mos6502_t* cpu) {
    cpu->pc = pullstack(cpu);
    cpu->pc |= (pullstack(cpu) << 8);
    cpu->pc++;
}
static void RTI(mos6502_t* cpu) {
    cpu->p = pullstack(cpu) & 0b11101111;
    cpu->pc = pullstack(cpu);
    cpu->pc |= (pullstack(cpu) << 8);
}

/* Flag ops */
static void CLC(mos6502_t* cpu) { cpu->p &= ~1; }
static void SEC(mos6502_t* cpu) { cpu->p |= 1; }
static void CLD(mos6502_t* cpu) { cpu->p &= ~0b00001000; }
static void SED(mos6502_t* cpu) { cpu->p |= 0b00001000; }
static void CLI(mos6502_t* cpu) { cpu->p &= ~0b00000100; }
static void SEI(mos6502_t* cpu) { cpu->p |= 0b00000100; }
static void CLV(mos6502_t* cpu) { cpu->p &= ~0b01000000; }

/* Instruction decode table */
static void (*instructionArr[])(mos6502_t*) = {
    /* 0x00-0x0F */
    BRK, ORA_X_ind, JAM, NULL, NOP, ORA_zpg, ASL_zpg, NULL,
    PHP, ORA_I, ASL_A, NULL, NOP, ORA_a, ASL_a, NULL,
    /* 0x10-0x1F */
    BPL, ORA_ind_Y, JAM, NULL, NOP, ORA_zpgX, ASL_zpgX, NULL,
    CLC, ORA_aY, NOP, NULL, NOP, ORA_aX, ASL_aX, NULL,
    /* 0x20-0x2F */
    JSR, AND_X_ind, JAM, NULL, BIT_zpg, AND_zpg, ROL_zpg, NULL,
    PLP, AND_I, ROL_A, NULL, BIT_a, AND_a, ROL_a, NULL,
    /* 0x30-0x3F */
    BMI, AND_ind_Y, JAM, NULL, NOP, AND_zpgX, ROL_zpgX, NULL,
    SEC, AND_aY, NOP, NULL, NOP, AND_aX, ROL_aX, NULL,
    /* 0x40-0x4F */
    RTI, EOR_X_ind, JAM, NULL, NOP, EOR_zpg, LSR_zpg, NULL,
    PHA, EOR_I, LSR_A, NULL, JMP_a, EOR_a, LSR_a, NULL,
    /* 0x50-0x5F */
    BVC, EOR_ind_Y, JAM, NULL, NOP, EOR_zpgX, LSR_zpgX, NULL,
    CLI, EOR_aY, NOP, NULL, NOP, EOR_aX, LSR_aX, NULL,
    /* 0x60-0x6F */
    RTS, ADC_X_ind, JAM, NULL, NOP, ADC_zpg, ROR_zpg, NULL,
    PLA, ADC_I, ROR_A, NULL, JMP_a_ind, ADC_a, ROR_a, NULL,
    /* 0x70-0x7F */
    BVS, ADC_ind_Y, JAM, NULL, NOP, ADC_zpgX, ROR_zpgX, NULL,
    SEI, ADC_aY, NOP, NULL, NOP, ADC_aX, ROR_aX, NULL,
    /* 0x80-0x8F */
    NOP, STA_X_ind, NOP, NULL, STY_zpg, STA_zpg, STX_zpg, NULL,
    DEY, NOP, TXA, NULL, STY_a, STA_a, STX_a, NULL,
    /* 0x90-0x9F */
    BCC, STA_ind_Y, JAM, NULL, STY_zpgX, STA_zpgX, STX_zpgY, NULL,
    TYA, STA_aY, TXS, NULL, NULL, STA_aX, NULL, NULL,
    /* 0xA0-0xAF */
    LDY_I, LDA_X_ind, LDX_I, NULL, LDY_zpg, LDA_zpg, LDX_zpg, NULL,
    TAY, LDA_I, TAX, NULL, LDY_a, LDA_a, LDX_a, NULL,
    /* 0xB0-0xBF */
    BCS, LDA_ind_Y, JAM, NULL, LDY_zpgX, LDA_zpgX, LDX_zpgY, NULL,
    CLV, LDA_aY, TSX, NULL, LDY_aX, LDA_aX, LDX_aY, NULL,
    /* 0xC0-0xCF */
    CPY_I, CMP_X_ind, NOP, NULL, CPY_zpg, CMP_zpg, DEC_zpg, NULL,
    INY, CMP_I, DEX, NULL, CPY_a, CMP_a, DEC_a, NULL,
    /* 0xD0-0xDF */
    BNE, CMP_ind_Y, JAM, NULL, NOP, CMP_zpgX, DEC_zpgX, NULL,
    CLD, CMP_aY, NOP, NULL, NULL, CMP_aX, DEC_aX, NULL,
    /* 0xE0-0xEF */
    CPX_I, SBC_X_ind, NOP, NULL, CPX_zpg, SBC_zpg, INC_zpg, NULL,
    INX, SBC_I, NOP, NULL, CPX_a, SBC_a, INC_a, NULL,
    /* 0xF0-0xFF */
    BEQ, SBC_ind_Y, JAM, NULL, NOP, SBC_zpgX, INC_zpgX, NULL,
    SED, SBC_aY, NOP, NULL, NOP, SBC_aX, INC_aX, NULL
};

static void mos6502_init(mos6502_t* cpu,
    mos6502_read_fn read,
    mos6502_write_fn write,
    void* userdata,
    logger_t* log) {
    cpu->a = 0;
    cpu->x = 0;
    cpu->y = 0;
    cpu->sp = 0xFD;
    cpu->p = 0b00100000;
    cpu->pc = 0;
    cpu->tmp = 0;
    cpu->log_counter = 0;
    cpu->log = log;
    cpu->read = read;
    cpu->write = write;
    cpu->userdata = userdata;
}

/* Reset CPU */
static void mos6502_reset(mos6502_t* cpu) {
    cpu->a = cpu->x = cpu->y = 0;
    cpu->p = 0b00100000;
    cpu->sp = 0xFD;
    cpu->pc = dbyte(cpu, 0xFFFC);
}

static int mos6502_step(mos6502_t* cpu) {
    uint8_t opcode = cpu->read(cpu, cpu->pc);
    cpu->pc++;
    void (*func)(mos6502_t*) = instructionArr[opcode];
    if (func) {
        func(cpu);
        return 1;
    }
    if (cpu->log_counter < MAX_LOG_COUNT) {
        if (cpu->log)
            logger_log(cpu->log, "!!! ILLEGAL OPCODE %02X at PC=%04X !!!",
                opcode, cpu->pc - 1);
        cpu->log_counter++;
    }
    return 0;
}
#endif /* M6502_H */