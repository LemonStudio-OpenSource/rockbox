/*
 * 6502 CPU emulator - with memory read/write callbacks
 */

#ifndef M6502_H
extern int g_log_counter;
#ifndef MAX_LOG_COUNT
#define MAX_LOG_COUNT 5000
#endif
#define M6502_H

#include <stdint.h>
#include <stdio.h>

/* Memory callbacks - must be set by user */
extern uint8_t (*cpu_read)(uint16_t addr);
extern void (*cpu_write)(uint16_t addr, uint8_t val);

/* CPU state */
static uint8_t regA = 0x00, regX = 0x00, regY = 0x00;
static uint8_t Status = 0b00100000;
static uint8_t StackPointer = 0xFD;
static uint16_t programcounter;
static uint8_t tempvalue;

/* Function prototypes for all instructions */
static void BRK(void);
static void NOP(void);
static void JAM(void);
static void LDA_a(void);
static void LDA_aX(void);
static void LDA_aY(void);
static void LDA_I(void);
static void LDA_zpg(void);
static void LDA_zpgX(void);
static void LDA_ind_Y(void);
static void LDA_X_ind(void);
static void LDX_a(void);
static void LDX_aY(void);
static void LDX_I(void);
static void LDX_zpg(void);
static void LDX_zpgY(void);
static void LDY_a(void);
static void LDY_aX(void);
static void LDY_I(void);
static void LDY_zpg(void);
static void LDY_zpgX(void);
static void STA_a(void);
static void STA_aX(void);
static void STA_aY(void);
static void STA_zpg(void);
static void STA_zpgX(void);
static void STA_ind_Y(void);
static void STA_X_ind(void);
static void STX_a(void);
static void STX_zpgY(void);
static void STX_zpg(void);
static void STY_a(void);
static void STY_zpgX(void);
static void STY_zpg(void);
static void ADC_a(void);
static void ADC_aX(void);
static void ADC_aY(void);
static void ADC_I(void);
static void ADC_zpg(void);
static void ADC_zpgX(void);
static void ADC_ind_Y(void);
static void ADC_X_ind(void);
static void SBC_a(void);
static void SBC_aX(void);
static void SBC_aY(void);
static void SBC_I(void);
static void SBC_zpg(void);
static void SBC_zpgX(void);
static void SBC_ind_Y(void);
static void SBC_X_ind(void);
static void INC_a(void);
static void INC_aX(void);
static void INC_zpg(void);
static void INC_zpgX(void);
static void INX(void);
static void INY(void);
static void DEC_a(void);
static void DEC_aX(void);
static void DEC_zpg(void);
static void DEC_zpgX(void);
static void DEX(void);
static void DEY(void);
static void ASL_zpg(void);
static void ASL_A(void);
static void ASL_a(void);
static void ASL_zpgX(void);
static void ASL_aX(void);
static void LSR_zpg(void);
static void LSR_A(void);
static void LSR_a(void);
static void LSR_zpgX(void);
static void LSR_aX(void);
static void ROL_zpg(void);
static void ROL_A(void);
static void ROL_a(void);
static void ROL_zpgX(void);
static void ROL_aX(void);
static void ROR_zpg(void);
static void ROR_A(void);
static void ROR_a(void);
static void ROR_zpgX(void);
static void ROR_aX(void);
static void AND_a(void);
static void AND_aX(void);
static void AND_aY(void);
static void AND_I(void);
static void AND_zpg(void);
static void AND_zpgX(void);
static void AND_ind_Y(void);
static void AND_X_ind(void);
static void ORA_a(void);
static void ORA_aX(void);
static void ORA_aY(void);
static void ORA_I(void);
static void ORA_zpg(void);
static void ORA_zpgX(void);
static void ORA_ind_Y(void);
static void ORA_X_ind(void);
static void EOR_a(void);
static void EOR_aX(void);
static void EOR_aY(void);
static void EOR_I(void);
static void EOR_zpg(void);
static void EOR_zpgX(void);
static void EOR_ind_Y(void);
static void EOR_X_ind(void);
static void CMP_a(void);
static void CMP_aX(void);
static void CMP_aY(void);
static void CMP_I(void);
static void CMP_zpg(void);
static void CMP_zpgX(void);
static void CMP_ind_Y(void);
static void CMP_X_ind(void);
static void CPX_a(void);
static void CPX_I(void);
static void CPX_zpg(void);
static void CPY_a(void);
static void CPY_I(void);
static void CPY_zpg(void);
static void BIT_a(void);
static void BIT_I(void);
static void BIT_zpg(void);
static void BCC(void);
static void BCS(void);
static void BNE(void);
static void BEQ(void);
static void BPL(void);
static void BMI(void);
static void BVC(void);
static void BVS(void);
static void TAX(void);
static void TXA(void);
static void TAY(void);
static void TYA(void);
static void TSX(void);
static void TXS(void);
static void PHA(void);
static void PLA(void);
static void PHP(void);
static void PLP(void);
static void JMP_a_ind(void);
static void JMP_a(void);
static void JSR(void);
static void RTS(void);
static void RTI(void);
static void CLC(void);
static void SEC(void);
static void CLD(void);
static void SED(void);
static void CLI(void);
static void SEI(void);
static void CLV(void);

/* Helper functions */
static inline uint16_t dbyte(uint16_t addr) {
    return (uint16_t)(cpu_read(addr + 1) << 8 | cpu_read(addr));
}

static inline void pushstack(uint8_t val) {
    cpu_write(0x0100 + StackPointer, val);
    StackPointer--;
}

static inline uint8_t pullstack(void) {
    StackPointer++;
    return cpu_read(0x0100 + StackPointer);
}

static inline int8_t calcrelative(uint8_t operand) {
    if (operand & 0x80)
        return -(~(operand) + 1);
    return operand;
}

static inline void set_flags(uint8_t val) {
    Status &= ~0b10000010;
    if (val == 0) Status |= 0b00000010;
    if (val & 0x80) Status |= 0b10000000;
}

/* Instruction implementations */
static void BRK() {
    uint16_t ret = programcounter + 1;
    pushstack(ret >> 8);
    pushstack(ret & 0xFF);
    pushstack(Status | 0b00110000);
    Status |= 0b00000100; /* SEI */
    programcounter = dbyte(0xFFFE);
}
static void NOP() { /* 1-byte: no PC change */ }
static void JAM() { Status |= 0b00010000; programcounter++; }

/* LDA */
static void LDA_a() {
    regA = cpu_read(dbyte(programcounter));
    set_flags(regA);
    programcounter += 2;
}
static void LDA_aX() {
    regA = cpu_read(dbyte(programcounter) + regX);
    set_flags(regA);
    programcounter += 2;
}
static void LDA_aY() {
    regA = cpu_read(dbyte(programcounter) + regY);
    set_flags(regA);
    programcounter += 2;
}
static void LDA_I() {
    regA = cpu_read(programcounter);
    set_flags(regA);
    programcounter += 1;
}
static void LDA_zpg() {
    regA = cpu_read(cpu_read(programcounter));
    set_flags(regA);
    programcounter += 1;
}
static void LDA_zpgX() {
    regA = cpu_read(cpu_read(programcounter) + regX);
    set_flags(regA);
    programcounter += 1;
}
static void LDA_ind_Y() {
    regA = cpu_read(dbyte(cpu_read(programcounter)) + regY);
    set_flags(regA);
    programcounter += 1;
}
static void LDA_X_ind() {
    regA = cpu_read(dbyte(cpu_read(programcounter) + regX));
    set_flags(regA);
    programcounter += 1;
}

/* LDX */
static void LDX_a() {
    regX = cpu_read(dbyte(programcounter));
    set_flags(regX);
    programcounter += 2;
}
static void LDX_aY() {
    regX = cpu_read(dbyte(programcounter) + regY);
    set_flags(regX);
    programcounter += 2;
}
static void LDX_I() {
    regX = cpu_read(programcounter);
    set_flags(regX);
    programcounter += 1;
}
static void LDX_zpg() {
    regX = cpu_read(cpu_read(programcounter));
    set_flags(regX);
    programcounter += 1;
}
static void LDX_zpgY() {
    regX = cpu_read(cpu_read(programcounter) + regY);
    set_flags(regX);
    programcounter += 1;
}

/* LDY */
static void LDY_a() {
    regY = cpu_read(dbyte(programcounter));
    set_flags(regY);
    programcounter += 2;
}
static void LDY_aX() {
    regY = cpu_read(dbyte(programcounter) + regX);
    set_flags(regY);
    programcounter += 2;
}
static void LDY_I() {
    regY = cpu_read(programcounter);
    set_flags(regY);
    programcounter += 1;
}
static void LDY_zpg() {
    regY = cpu_read(cpu_read(programcounter));
    set_flags(regY);
    programcounter += 1;
}
static void LDY_zpgX() {
    regY = cpu_read(cpu_read(programcounter) + regX);
    set_flags(regY);
    programcounter += 1;
}

/* STA */
static void STA_a() {
    cpu_write(dbyte(programcounter), regA);
    programcounter += 2;
}
static void STA_aX() {
    cpu_write(dbyte(programcounter) + regX, regA);
    programcounter += 2;
}
static void STA_aY() {
    cpu_write(dbyte(programcounter) + regY, regA);
    programcounter += 2;
}
static void STA_zpg() {
    cpu_write(cpu_read(programcounter), regA);
    programcounter += 1;
}
static void STA_zpgX() {
    cpu_write(cpu_read(programcounter) + regX, regA);
    programcounter += 1;
}
static void STA_ind_Y() {
    cpu_write(dbyte(cpu_read(programcounter)) + regY, regA);
    programcounter += 1;
}
static void STA_X_ind() {
    cpu_write(dbyte(cpu_read(programcounter) + regX), regA);
    programcounter += 1;
}

/* STX */
static void STX_a() {
    cpu_write(dbyte(programcounter), regX);
    programcounter += 2;
}
static void STX_zpgY() {
    cpu_write(cpu_read(programcounter) + regY, regX);
    programcounter += 1;
}
static void STX_zpg() {
    cpu_write(cpu_read(programcounter), regX);
    programcounter += 1;
}

/* STY */
static void STY_a() {
    cpu_write(dbyte(programcounter), regY);
    programcounter += 2;
}
static void STY_zpgX() {
    cpu_write(cpu_read(programcounter) + regX, regY);
    programcounter += 1;
}
static void STY_zpg() {
    cpu_write(cpu_read(programcounter), regY);
    programcounter += 1;
}

/* ADC */
static void ADC_a() {
    uint16_t result;
    tempvalue = (Status & 1) + cpu_read(dbyte(programcounter));
    result = regA + tempvalue;
    Status &= ~0b11000011;
    Status |= ((result & 0x80) | ((result & 0x100) >> 8) | ((result == 0) << 1));
    Status |= ((((tempvalue & 0x80) == (regA & 0x80)) && ((regA & 0x80) != (result & 0x80))) ? 0x40 : 0);
    regA = result & 0xFF;
    programcounter += 2;
}
static void ADC_aX() {
    uint16_t result;
    tempvalue = (Status & 1) + cpu_read(dbyte(programcounter) + regX);
    result = regA + tempvalue;
    Status &= ~0b11000011;
    Status |= ((result & 0x80) | ((result & 0x100) >> 8) | ((result == 0) << 1));
    Status |= ((((tempvalue & 0x80) == (regA & 0x80)) && ((regA & 0x80) != (result & 0x80))) ? 0x40 : 0);
    regA = result & 0xFF;
    programcounter += 2;
}
static void ADC_aY() {
    uint16_t result;
    tempvalue = (Status & 1) + cpu_read(dbyte(programcounter) + regY);
    result = regA + tempvalue;
    Status &= ~0b11000011;
    Status |= ((result & 0x80) | ((result & 0x100) >> 8) | ((result == 0) << 1));
    Status |= ((((tempvalue & 0x80) == (regA & 0x80)) && ((regA & 0x80) != (result & 0x80))) ? 0x40 : 0);
    regA = result & 0xFF;
    programcounter += 2;
}
static void ADC_I() {
    uint16_t result;
    tempvalue = (Status & 1) + cpu_read(programcounter);
    result = regA + tempvalue;
    Status &= ~0b11000011;
    Status |= ((result & 0x80) | ((result & 0x100) >> 8) | ((result == 0) << 1));
    Status |= ((((tempvalue & 0x80) == (regA & 0x80)) && ((regA & 0x80) != (result & 0x80))) ? 0x40 : 0);
    regA = result & 0xFF;
    programcounter += 1;
}
static void ADC_zpg() {
    uint16_t result;
    tempvalue = (Status & 1) + cpu_read(cpu_read(programcounter));
    result = regA + tempvalue;
    Status &= ~0b11000011;
    Status |= ((result & 0x80) | ((result & 0x100) >> 8) | ((result == 0) << 1));
    Status |= ((((tempvalue & 0x80) == (regA & 0x80)) && ((regA & 0x80) != (result & 0x80))) ? 0x40 : 0);
    regA = result & 0xFF;
    programcounter += 1;
}
static void ADC_zpgX() {
    uint16_t result;
    tempvalue = (Status & 1) + cpu_read(cpu_read(programcounter) + regX);
    result = regA + tempvalue;
    Status &= ~0b11000011;
    Status |= ((result & 0x80) | ((result & 0x100) >> 8) | ((result == 0) << 1));
    Status |= ((((tempvalue & 0x80) == (regA & 0x80)) && ((regA & 0x80) != (result & 0x80))) ? 0x40 : 0);
    regA = result & 0xFF;
    programcounter += 1;
}
static void ADC_ind_Y() {
    uint16_t result;
    tempvalue = (Status & 1) + cpu_read(dbyte(cpu_read(programcounter)) + regY);
    result = regA + tempvalue;
    Status &= ~0b11000011;
    Status |= ((result & 0x80) | ((result & 0x100) >> 8) | ((result == 0) << 1));
    Status |= ((((tempvalue & 0x80) == (regA & 0x80)) && ((regA & 0x80) != (result & 0x80))) ? 0x40 : 0);
    regA = result & 0xFF;
    programcounter += 1;
}
static void ADC_X_ind() {
    uint16_t result;
    tempvalue = (Status & 1) + cpu_read(dbyte(cpu_read(programcounter) + regX));
    result = regA + tempvalue;
    Status &= ~0b11000011;
    Status |= ((result & 0x80) | ((result & 0x100) >> 8) | ((result == 0) << 1));
    Status |= ((((tempvalue & 0x80) == (regA & 0x80)) && ((regA & 0x80) != (result & 0x80))) ? 0x40 : 0);
    regA = result & 0xFF;
    programcounter += 1;
}

/* SBC - similar to ADC but with operand complemented */
#define SBC_common(operand)     uint16_t result;     tempvalue = (Status & 1) + operand;     tempvalue = (tempvalue ^ 0xFF) + 1;     result = regA + tempvalue;     Status &= ~0b11000011;     Status |= ((result & 0x80) | ((result & 0x100) >> 8) | ((result == 0) << 1));     Status |= ((((tempvalue & 0x80) == (regA & 0x80)) && ((regA & 0x80) != (result & 0x80))) ? 0x40 : 0);     regA = result & 0xFF;

static void SBC_a() {
    SBC_common(cpu_read(dbyte(programcounter)));
    programcounter += 2;
}
static void SBC_aX() {
    SBC_common(cpu_read(dbyte(programcounter) + regX));
    programcounter += 2;
}
static void SBC_aY() {
    SBC_common(cpu_read(dbyte(programcounter) + regY));
    programcounter += 2;
}
static void SBC_I() {
    SBC_common(cpu_read(programcounter));
    programcounter += 1;
}
static void SBC_zpg() {
    SBC_common(cpu_read(cpu_read(programcounter)));
    programcounter += 1;
}
static void SBC_zpgX() {
    SBC_common(cpu_read(cpu_read(programcounter) + regX));
    programcounter += 1;
}
static void SBC_ind_Y() {
    SBC_common(cpu_read(dbyte(cpu_read(programcounter)) + regY));
    programcounter += 1;
}
static void SBC_X_ind() {
    SBC_common(cpu_read(dbyte(cpu_read(programcounter) + regX)));
    programcounter += 1;
}

/* INC */
static void INC_a() {
    uint8_t val = cpu_read(dbyte(programcounter)) + 1;
    cpu_write(dbyte(programcounter), val);
    set_flags(val);
    programcounter += 2;
}
static void INC_aX() {
    uint8_t val = cpu_read(dbyte(programcounter) + regX) + 1;
    cpu_write(dbyte(programcounter) + regX, val);
    set_flags(val);
    programcounter += 2;
}
static void INC_zpg() {
    uint8_t val = cpu_read(cpu_read(programcounter)) + 1;
    cpu_write(cpu_read(programcounter), val);
    set_flags(val);
    programcounter += 1;
}
static void INC_zpgX() {
    uint8_t val = cpu_read(cpu_read(programcounter) + regX) + 1;
    cpu_write(cpu_read(programcounter) + regX, val);
    set_flags(val);
    programcounter += 1;
}
static void INX() {
    regX++;
    set_flags(regX);
    /* 1-byte: no PC change */
}
static void INY() {
    regY++;
    set_flags(regY);
    /* 1-byte: no PC change */
}

/* DEC */
static void DEC_a() {
    uint8_t val = cpu_read(dbyte(programcounter)) - 1;
    cpu_write(dbyte(programcounter), val);
    set_flags(val);
    programcounter += 2;
}
static void DEC_aX() {
    uint8_t val = cpu_read(dbyte(programcounter) + regX) - 1;
    cpu_write(dbyte(programcounter) + regX, val);
    set_flags(val);
    programcounter += 2;
}
static void DEC_zpg() {
    uint8_t val = cpu_read(cpu_read(programcounter)) - 1;
    cpu_write(cpu_read(programcounter), val);
    set_flags(val);
    programcounter += 1;
}
static void DEC_zpgX() {
    uint8_t val = cpu_read(cpu_read(programcounter) + regX) - 1;
    cpu_write(cpu_read(programcounter) + regX, val);
    set_flags(val);
    programcounter += 1;
}
static void DEX() {
    regX--;
    set_flags(regX);
    /* 1-byte: no PC change */
}
static void DEY() {
    regY--;
    set_flags(regY);
    /* 1-byte: no PC change */
}

/* ASL */
static void ASL_zpg() {
    uint8_t val = cpu_read(cpu_read(programcounter));
    Status = (Status & ~0b10000011) | ((val & 0x80) >> 7);
    val <<= 1;
    cpu_write(cpu_read(programcounter), val);
    set_flags(val);
    programcounter += 1;
}
static void ASL_A() {
    Status = (Status & ~0b10000011) | ((regA & 0x80) >> 7);
    regA <<= 1;
    set_flags(regA);
    programcounter++;
}
static void ASL_a() {
    uint8_t val = cpu_read(dbyte(programcounter));
    Status = (Status & ~0b10000011) | ((val & 0x80) >> 7);
    val <<= 1;
    cpu_write(dbyte(programcounter), val);
    set_flags(val);
    programcounter += 2;
}
static void ASL_zpgX() {
    uint8_t val = cpu_read(cpu_read(programcounter) + regX);
    Status = (Status & ~0b10000011) | ((val & 0x80) >> 7);
    val <<= 1;
    cpu_write(cpu_read(programcounter) + regX, val);
    set_flags(val);
    programcounter += 1;
}
static void ASL_aX() {
    uint8_t val = cpu_read(dbyte(programcounter) + regX);
    Status = (Status & ~0b10000011) | ((val & 0x80) >> 7);
    val <<= 1;
    cpu_write(dbyte(programcounter) + regX, val);
    set_flags(val);
    programcounter += 2;
}

/* LSR */
static void LSR_zpg() {
    uint8_t val = cpu_read(cpu_read(programcounter));
    Status = (Status & ~0b10000011) | (val & 1);
    val >>= 1;
    cpu_write(cpu_read(programcounter), val);
    Status = (Status & ~0b00000010) | ((val == 0) << 1);
    programcounter += 1;
}
static void LSR_A() {
    Status = (Status & ~0b10000011) | (regA & 1);
    regA >>= 1;
    Status = (Status & ~0b00000010) | ((regA == 0) << 1);
    programcounter++;
}
static void LSR_a() {
    uint8_t val = cpu_read(dbyte(programcounter));
    Status = (Status & ~0b10000011) | (val & 1);
    val >>= 1;
    cpu_write(dbyte(programcounter), val);
    Status = (Status & ~0b00000010) | ((val == 0) << 1);
    programcounter += 2;
}
static void LSR_zpgX() {
    uint8_t val = cpu_read(cpu_read(programcounter) + regX);
    Status = (Status & ~0b10000011) | (val & 1);
    val >>= 1;
    cpu_write(cpu_read(programcounter) + regX, val);
    Status = (Status & ~0b00000010) | ((val == 0) << 1);
    programcounter += 1;
}
static void LSR_aX() {
    uint8_t val = cpu_read(dbyte(programcounter) + regX);
    Status = (Status & ~0b10000011) | (val & 1);
    val >>= 1;
    cpu_write(dbyte(programcounter) + regX, val);
    Status = (Status & ~0b00000010) | ((val == 0) << 1);
    programcounter += 2;
}

/* ROL */
static void ROL_zpg() {
    uint8_t val = cpu_read(cpu_read(programcounter));
    uint8_t carry = (Status & 1);
    Status = (Status & ~0b10000011) | ((val & 0x80) >> 7);
    val = (val << 1) | carry;
    cpu_write(cpu_read(programcounter), val);
    set_flags(val);
    programcounter += 1;
}
static void ROL_A() {
    uint8_t carry = (Status & 1);
    Status = (Status & ~0b10000011) | ((regA & 0x80) >> 7);
    regA = (regA << 1) | carry;
    set_flags(regA);
    programcounter++;
}
static void ROL_a() {
    uint8_t val = cpu_read(dbyte(programcounter));
    uint8_t carry = (Status & 1);
    Status = (Status & ~0b10000011) | ((val & 0x80) >> 7);
    val = (val << 1) | carry;
    cpu_write(dbyte(programcounter), val);
    set_flags(val);
    programcounter += 2;
}
static void ROL_zpgX() {
    uint8_t val = cpu_read(cpu_read(programcounter) + regX);
    uint8_t carry = (Status & 1);
    Status = (Status & ~0b10000011) | ((val & 0x80) >> 7);
    val = (val << 1) | carry;
    cpu_write(cpu_read(programcounter) + regX, val);
    set_flags(val);
    programcounter += 1;
}
static void ROL_aX() {
    uint8_t val = cpu_read(dbyte(programcounter) + regX);
    uint8_t carry = (Status & 1);
    Status = (Status & ~0b10000011) | ((val & 0x80) >> 7);
    val = (val << 1) | carry;
    cpu_write(dbyte(programcounter) + regX, val);
    set_flags(val);
    programcounter += 2;
}

/* ROR */
static void ROR_zpg() {
    uint8_t val = cpu_read(cpu_read(programcounter));
    uint8_t carry = (Status & 1);
    Status = (Status & ~1) | (val & 1);
    val = (val >> 1) | (carry << 7);
    cpu_write(cpu_read(programcounter), val);
    set_flags(val);
    programcounter += 1;
}
static void ROR_A() {
    uint8_t carry = (Status & 1);
    Status = (Status & ~1) | (regA & 1);
    regA = (regA >> 1) | (carry << 7);
    set_flags(regA);
    programcounter++;
}
static void ROR_a() {
    uint8_t val = cpu_read(dbyte(programcounter));
    uint8_t carry = (Status & 1);
    Status = (Status & ~1) | (val & 1);
    val = (val >> 1) | (carry << 7);
    cpu_write(dbyte(programcounter), val);
    set_flags(val);
    programcounter += 2;
}
static void ROR_zpgX() {
    uint8_t val = cpu_read(cpu_read(programcounter) + regX);
    uint8_t carry = (Status & 1);
    Status = (Status & ~1) | (val & 1);
    val = (val >> 1) | (carry << 7);
    cpu_write(cpu_read(programcounter) + regX, val);
    set_flags(val);
    programcounter += 1;
}
static void ROR_aX() {
    uint8_t val = cpu_read(dbyte(programcounter) + regX);
    uint8_t carry = (Status & 1);
    Status = (Status & ~1) | (val & 1);
    val = (val >> 1) | (carry << 7);
    cpu_write(dbyte(programcounter) + regX, val);
    set_flags(val);
    programcounter += 2;
}

/* AND */
static void AND_a() {
    regA &= cpu_read(dbyte(programcounter));
    set_flags(regA);
    programcounter += 2;
}
static void AND_aX() {
    regA &= cpu_read(dbyte(programcounter) + regX);
    set_flags(regA);
    programcounter += 2;
}
static void AND_aY() {
    regA &= cpu_read(dbyte(programcounter) + regY);
    set_flags(regA);
    programcounter += 2;
}
static void AND_I() {
    regA &= cpu_read(programcounter);
    set_flags(regA);
    programcounter += 1;
}
static void AND_zpg() {
    regA &= cpu_read(cpu_read(programcounter));
    set_flags(regA);
    programcounter += 1;
}
static void AND_zpgX() {
    regA &= cpu_read(cpu_read(programcounter) + regX);
    set_flags(regA);
    programcounter += 1;
}
static void AND_ind_Y() {
    regA &= cpu_read(dbyte(cpu_read(programcounter)) + regY);
    set_flags(regA);
    programcounter += 1;
}
static void AND_X_ind() {
    regA &= cpu_read(dbyte(cpu_read(programcounter) + regX));
    set_flags(regA);
    programcounter += 1;
}

/* ORA */
static void ORA_a() {
    regA |= cpu_read(dbyte(programcounter));
    set_flags(regA);
    programcounter += 2;
}
static void ORA_aX() {
    regA |= cpu_read(dbyte(programcounter) + regX);
    set_flags(regA);
    programcounter += 2;
}
static void ORA_aY() {
    regA |= cpu_read(dbyte(programcounter) + regY);
    set_flags(regA);
    programcounter += 2;
}
static void ORA_I() {
    regA |= cpu_read(programcounter);
    set_flags(regA);
    programcounter += 1;
}
static void ORA_zpg() {
    regA |= cpu_read(cpu_read(programcounter));
    set_flags(regA);
    programcounter += 1;
}
static void ORA_zpgX() {
    regA |= cpu_read(cpu_read(programcounter) + regX);
    set_flags(regA);
    programcounter += 1;
}
static void ORA_ind_Y() {
    regA |= cpu_read(dbyte(cpu_read(programcounter)) + regY);
    set_flags(regA);
    programcounter += 1;
}
static void ORA_X_ind() {
    regA |= cpu_read(dbyte(cpu_read(programcounter) + regX));
    set_flags(regA);
    programcounter += 1;
}

/* EOR */
static void EOR_a() {
    regA ^= cpu_read(dbyte(programcounter));
    set_flags(regA);
    programcounter += 2;
}
static void EOR_aX() {
    regA ^= cpu_read(dbyte(programcounter) + regX);
    set_flags(regA);
    programcounter += 2;
}
static void EOR_aY() {
    regA ^= cpu_read(dbyte(programcounter) + regY);
    set_flags(regA);
    programcounter += 2;
}
static void EOR_I() {
    regA ^= cpu_read(programcounter);
    set_flags(regA);
    programcounter += 1;
}
static void EOR_zpg() {
    regA ^= cpu_read(cpu_read(programcounter));
    set_flags(regA);
    programcounter += 1;
}
static void EOR_zpgX() {
    regA ^= cpu_read(cpu_read(programcounter) + regX);
    set_flags(regA);
    programcounter += 1;
}
static void EOR_ind_Y() {
    regA ^= cpu_read(dbyte(cpu_read(programcounter)) + regY);
    set_flags(regA);
    programcounter += 1;
}
static void EOR_X_ind() {
    regA ^= cpu_read(dbyte(cpu_read(programcounter) + regX));
    set_flags(regA);
    programcounter += 1;
}

/* CMP */
static void CMP_a() {
    uint8_t val = cpu_read(dbyte(programcounter));
    Status = (Status & ~0b10000011) | ((regA >= val) ? 1 : 0) | ((regA == val) << 1) | ((regA < val) << 7);
    programcounter += 2;
}
static void CMP_aX() {
    uint8_t val = cpu_read(dbyte(programcounter) + regX);
    Status = (Status & ~0b10000011) | ((regA >= val) ? 1 : 0) | ((regA == val) << 1) | ((regA < val) << 7);
    programcounter += 2;
}
static void CMP_aY() {
    uint8_t val = cpu_read(dbyte(programcounter) + regY);
    Status = (Status & ~0b10000011) | ((regA >= val) ? 1 : 0) | ((regA == val) << 1) | ((regA < val) << 7);
    programcounter += 2;
}
static void CMP_I() {
    uint8_t val = cpu_read(programcounter);
    Status = (Status & ~0b10000011) | ((regA >= val) ? 1 : 0) | ((regA == val) << 1) | ((regA < val) << 7);
    programcounter += 1;
}
static void CMP_zpg() {
    uint8_t val = cpu_read(cpu_read(programcounter));
    Status = (Status & ~0b10000011) | ((regA >= val) ? 1 : 0) | ((regA == val) << 1) | ((regA < val) << 7);
    programcounter += 1;
}
static void CMP_zpgX() {
    uint8_t val = cpu_read(cpu_read(programcounter) + regX);
    Status = (Status & ~0b10000011) | ((regA >= val) ? 1 : 0) | ((regA == val) << 1) | ((regA < val) << 7);
    programcounter += 1;
}
static void CMP_ind_Y() {
    uint8_t val = cpu_read(dbyte(cpu_read(programcounter)) + regY);
    Status = (Status & ~0b10000011) | ((regA >= val) ? 1 : 0) | ((regA == val) << 1) | ((regA < val) << 7);
    programcounter += 1;
}
static void CMP_X_ind() {
    uint8_t val = cpu_read(dbyte(cpu_read(programcounter) + regX));
    Status = (Status & ~0b10000011) | ((regA >= val) ? 1 : 0) | ((regA == val) << 1) | ((regA < val) << 7);
    programcounter += 1;
}

/* CPX */
static void CPX_a() {
    uint8_t val = cpu_read(dbyte(programcounter));
    Status = (Status & ~0b10000011) | ((regX >= val) ? 1 : 0) | ((regX == val) << 1) | ((regX < val) << 7);
    programcounter += 2;
}
static void CPX_I() {
    uint8_t val = cpu_read(programcounter);
    Status = (Status & ~0b10000011) | ((regX >= val) ? 1 : 0) | ((regX == val) << 1) | ((regX < val) << 7);
    programcounter += 1;
}
static void CPX_zpg() {
    uint8_t val = cpu_read(cpu_read(programcounter));
    Status = (Status & ~0b10000011) | ((regX >= val) ? 1 : 0) | ((regX == val) << 1) | ((regX < val) << 7);
    programcounter += 1;
}

/* CPY */
static void CPY_a() {
    uint8_t val = cpu_read(dbyte(programcounter));
    Status = (Status & ~0b10000011) | ((regY >= val) ? 1 : 0) | ((regY == val) << 1) | ((regY < val) << 7);
    programcounter += 2;
}
static void CPY_I() {
    uint8_t val = cpu_read(programcounter);
    Status = (Status & ~0b10000011) | ((regY >= val) ? 1 : 0) | ((regY == val) << 1) | ((regY < val) << 7);
    programcounter += 1;
}
static void CPY_zpg() {
    uint8_t val = cpu_read(cpu_read(programcounter));
    Status = (Status & ~0b10000011) | ((regY >= val) ? 1 : 0) | ((regY == val) << 1) | ((regY < val) << 7);
    programcounter += 1;
}

/* BIT */
static void BIT_a() {
    uint8_t val = cpu_read(dbyte(programcounter));
    Status = (Status & ~0b11000010) | (val & 0b11000000) | (((val & regA) == 0) << 1);
    programcounter += 2;
}
static void BIT_I() {
    uint8_t val = cpu_read(programcounter);
    Status = (Status & ~0b11000010) | (val & 0b11000000) | (((val & regA) == 0) << 1);
    programcounter += 1;
}
static void BIT_zpg() {
    uint8_t val = cpu_read(cpu_read(programcounter));
    Status = (Status & ~0b11000010) | (val & 0b11000000) | (((val & regA) == 0) << 1);
    programcounter += 1;
}

/* Branches */
static void BCC() {
    if (!(Status & 1)) programcounter += calcrelative(cpu_read(programcounter));
    programcounter += 1;
}
static void BCS() {
    if (Status & 1) programcounter += calcrelative(cpu_read(programcounter));
    programcounter += 1;
}
static void BNE() {
    if (!(Status & 2)) programcounter += calcrelative(cpu_read(programcounter));
    programcounter += 1;
}
static void BEQ() {
    if (Status & 2) programcounter += calcrelative(cpu_read(programcounter));
    programcounter += 1;
}
static void BPL() {
    if (!(Status & 0x80)) programcounter += calcrelative(cpu_read(programcounter));
    programcounter += 1;
}
static void BMI() {
    if (Status & 0x80) programcounter += calcrelative(cpu_read(programcounter));
    programcounter += 1;
}
static void BVC() {
    if (!(Status & 0x40)) programcounter += calcrelative(cpu_read(programcounter));
    programcounter += 1;
}
static void BVS() {
    if (Status & 0x40) programcounter += calcrelative(cpu_read(programcounter));
    programcounter += 1;
}

/* Transfers */
static void TAX() {
    regX = regA;
    set_flags(regX);
    /* 1-byte: no PC change */
}
static void TXA() {
    regA = regX;
    set_flags(regA);
    /* 1-byte: no PC change */
}
static void TAY() {
    regY = regA;
    set_flags(regY);
    /* 1-byte: no PC change */
}
static void TYA() {
    regA = regY;
    set_flags(regA);
    /* 1-byte: no PC change */
}
static void TSX() {
    regX = StackPointer;
    set_flags(regX);
    /* 1-byte: no PC change */
}
static void TXS() {
    StackPointer = regX;
    /* 1-byte: no PC change */
}

/* Stack */
static void PHA() {
    pushstack(regA);
    /* 1-byte: no PC change */
}
static void PLA() {
    regA = pullstack();
    set_flags(regA);
    /* 1-byte: no PC change */
}
static void PHP() {
    pushstack(Status | 0b00110000);
    /* 1-byte: no PC change */
}
static void PLP() {
    Status = pullstack() & 0b11101111;
    /* 1-byte: no PC change */
}

/* JMP */
static void JMP_a_ind() {
    uint16_t addr = dbyte(programcounter);
    if ((addr & 0xFF) == 0xFF) {
        addr = (addr & 0xFF00) | cpu_read(addr & 0xFF00);
    } else {
        addr = cpu_read(addr) | (cpu_read(addr + 1) << 8);
    }
    programcounter = addr;
}
static void JMP_a() {
    programcounter = dbyte(programcounter);
}

/* JSR / RTS / RTI */
static void JSR() {
    uint16_t ret = programcounter + 2;
    pushstack(ret >> 8);
    pushstack(ret & 0xFF);
    programcounter = dbyte(programcounter);
}
static void RTS() {
    programcounter = pullstack();
    programcounter |= (pullstack() << 8);
    programcounter++;
}
static void RTI() {
    Status = pullstack() & 0b11101111;
    programcounter = pullstack();
    programcounter |= (pullstack() << 8);
}

/* Flag ops */
static void CLC() { Status &= ~1; /* 1-byte: no PC change */ }
static void SEC() { Status |= 1; /* 1-byte: no PC change */ }
static void CLD() { Status &= ~0b00001000; /* 1-byte: no PC change */ }
static void SED() { Status |= 0b00001000; /* 1-byte: no PC change */ }
static void CLI() { Status &= ~0b00000100; /* 1-byte: no PC change */ }
static void SEI() { Status |= 0b00000100; /* 1-byte: no PC change */ }
static void CLV() { Status &= ~0b01000000; /* 1-byte: no PC change */ }

/* Instruction decode table */
static void (*instructionArr[])(void) = {
    /* 0x00-0x0F */
    BRK, ORA_X_ind, JAM, NULL, NOP, ORA_zpg, ASL_zpg, NULL,
    PHP, ORA_I, ASL_A, NULL, NOP, ORA_a, ASL_a, NULL,
    /* 0x10-0x1F */
    BPL, ORA_ind_Y, JAM, NULL, NOP, ORA_zpgX, ASL_zpgX, NULL,
    CLC, ORA_aX, NOP, NULL, NOP, ORA_aX, ASL_aX, NULL,
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
    SED, SBC_aX, NOP, NULL, NOP, SBC_aX, INC_aX, NULL
};

/* Reset CPU */
void m6502_reset(void) {
    regA = regX = regY = 0;
    Status = 0b00100000;
    StackPointer = 0xFD;
    /* Read reset vector at 0xFFFC-0xFFFD */
    programcounter = dbyte(0xFFFC);
}

int m6502_step(void) {
    uint8_t opcode = cpu_read(programcounter);
    if (g_log_counter < MAX_LOG_COUNT) {
        LOG("STEP PC=%04X OPC=%02X A=%02X X=%02X Y=%02X SP=%02X S=%02X",
            programcounter, opcode, regA, regX, regY, StackPointer, Status);
        g_log_counter++;
    }
    programcounter++;
    void (*func)(void) = instructionArr[opcode];
    if (func) {
        func();
        return 1;
    }
    if (g_log_counter < MAX_LOG_COUNT) {
        LOG("!!! ILLEGAL OPCODE %02X at PC=%04X !!!", opcode, programcounter - 1);
        g_log_counter++;
    }
    return 0;
}
#endif /* M6502_H */
