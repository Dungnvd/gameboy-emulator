#include "core/cpu.h"

#include <stdexcept>

namespace gb {

Cpu::Cpu(Mmu& mmu) : mmu_(mmu) {}

void Cpu::reset() {
    regs_.setAf(0x01B0);
    regs_.setBc(0x0013);
    regs_.setDe(0x00D8);
    regs_.setHl(0x014D);
    regs_.sp = 0xFFFE;
    regs_.pc = 0x0100;
    ime_ = false;
    imeScheduled_ = false;
    halted_ = false;
}

int Cpu::serviceInterrupt() {
    const std::uint8_t interruptFlag = mmu_.read(0xFF0F);          // IF
    const int pending = mmu_.read(0xFFFF) & interruptFlag & 0x1F;  // IE & IF, bit 0 - 4
    if (pending == 0) {
        return 0;
    }

    halted_ = false;  // any pending interrupt wakes HALT, even with IME = 0
    if (!ime_) {
        return 0;  // No dispatch, no M-cycle taken
    }

    // Lowest set bit = highest priority
    int bit = 0;
    while ((pending & (1 << bit)) == 0) {
        ++bit;
    }

    ime_ = false;  // no nested interrupts unless handler runs EI or RETI
    mmu_.write(0xFF0F, static_cast<std::uint8_t>(interruptFlag & ~(1 << bit)));
    call(static_cast<std::uint16_t>(0x40 + bit * 8));  // vector 0x40, 0x48, 0x50, 0x58, 0x60
    return 5;
}

int Cpu::step() {
    const int interruptCycles = serviceInterrupt();
    if (interruptCycles > 0) {
        return interruptCycles;
    }

    if (halted_) {
        return 1;  // sleeping: 1 M-cycle passes, nothing fetched
    }

    if (imeScheduled_) {
        // EI ran in previous step. Interrupts were already checked above
        // so first dispatch can only happen after this instruction
        ime_ = true;
        imeScheduled_ = false;
    }

    const std::uint8_t op = fetch8();

    if (op == 0x00) {
        // NOP
        return 1;
    }

    if (op == 0x10) {
        // STOP: 2 bytes (0x10 0x00). Low power mode until a button press
        // No joypad yet -> skip second byte, behave like NOP
        fetch8();
        return 1;
    }

    if ((op & 0xC7) == 0x06) {
        // LD r, n (00 ddd 110)
        // 0xC7 = 11 000 111
        // 0x07 = 0000 0111
        const int dst = (op >> 3) & 0x07;
        const std::uint8_t n = fetch8();
        writeR8(dst, n);
        return (dst == 6) ? 3 : 2;
    }

    if (op == 0x76) {
        // HALT: sleep until IE & IF != 0
        halted_ = true;
        return 1;
    }

    if ((op & 0xC0) == 0x40) {
        // LD r, r' (01 ddd sss)
        // 0xC0 = 11 000 000
        // 0x40 = 01 000 000
        // 0x07 = 0000 0111
        const int dst = (op >> 3) & 0x07;
        const int src = op & 0x07;
        writeR8(dst, readR8(src));
        return (dst == 6 || src == 6) ? 2 : 1;
    }

    if ((op & 0xCF) == 0x01) {
        // LD rr, nn (00 rr0 001)
        // 0xCF = 11 001 111 (mask)
        // 0x01 = 00 000 001 (expected)
        // 0x03 = 00 000 011 (keep 2 bits)
        const int rr = (op >> 4) & 0x03;
        const std::uint16_t value = fetch16();
        writeR16(rr, value);
        return 3;
    }

    if (op == 0x08) {
        // LD (nn), SP (00 001 000)
        const std::uint16_t addr = fetch16();
        mmu_.write16(addr, regs_.sp);
        return 5;
    }

    if (op == 0xF9) {
        // LD SP, HL (11 111 001)
        regs_.sp = regs_.hl();
        return 2;
    }

    if ((op & 0xCF) == 0xC5) {
        // PUSH rr (11 rr0 101), rr uses r16stk
        // Copy register pair rr onto top of stack
        // 0xCF = 11 001 111 (mask)
        // 0xC5 = 11 000 101 (expected)
        // 0x03 = 00 000 011 (keep 2 bits)
        const int rr = (op >> 4) & 0x03;
        push16(readR16Stk(rr));
        return 4;
    }

    if ((op & 0xCF) == 0xC1) {
        // POP rr (11 rr0 001), rr uses r16stk
        // Take value on top of the stack into register pair rr
        // 0xCF = 11 001 111 (mask)
        // 0xC1 = 11 000 001 (expected)
        const int rr = (op >> 4) & 0x03;
        writeR16Stk(rr, pop16());
        return 3;
    }

    if ((op & 0xCF) == 0x02) {
        // LD (r16mem), A (00 rr0 010)
        // 0xCF = 11 001 111 (mask)
        // 0x02 = 00 000 010 (expected)
        // 0x03 = 00 000 011 (keep 2 bits)
        const int rr = (op >> 4) & 0x03;
        mmu_.write(addrR16Mem(rr), regs_.a);
        return 2;
    }

    if ((op & 0xCF) == 0x0A) {
        // LD A, (r16mem) (00 rr1 010)
        // 0xCF = 11 001 111 (mask)
        // 0x0A = 00 001 010 (expected)
        const int rr = (op >> 4) & 0x03;
        regs_.a = mmu_.read(addrR16Mem(rr));
        return 2;
    }

    if (op == 0xEA) {
        // LD (nn), A
        // 0xEA = 11 101 010
        const std::uint16_t addr = fetch16();
        mmu_.write(addr, regs_.a);
        return 4;
    }

    if (op == 0xFA) {
        // LD A, (nn)
        // 0xFA = 11 111 010
        const std::uint16_t addr = fetch16();
        regs_.a = mmu_.read(addr);
        return 4;
    }

    if (op == 0xE0) {
        // LDH (n), A
        // 0xE0 = 11 100 000
        // H = High page (0xFF00-0xFFFF): I/O registers + HRAM
        const std::uint16_t addr = static_cast<std::uint16_t>(0xFF00 + fetch8());
        mmu_.write(addr, regs_.a);
        return 3;
    }

    if (op == 0xF0) {
        // LDH A, (n)
        // 0xF0 = 11 110 000
        const std::uint16_t addr = static_cast<std::uint16_t>(0xFF00 + fetch8());
        regs_.a = mmu_.read(addr);
        return 3;
    }

    if (op == 0xE2) {
        // LDH (C), A
        // 0xE2 = 11 100 010
        const std::uint16_t addr = static_cast<std::uint16_t>(0xFF00 + regs_.c);
        mmu_.write(addr, regs_.a);
        return 2;
    }

    if (op == 0xF2) {
        // LDH A, (C)
        // 0xF2 = 11 110 010
        const std::uint16_t addr = static_cast<std::uint16_t>(0xFF00 + regs_.c);
        regs_.a = mmu_.read(addr);
        return 2;
    }

    if ((op & 0xC0) == 0x80) {
        // ALU A, r8 (10 ooo rrr): ooo = operation
        // 0xC0 = 11 000 000 (mask)
        // 0x80 = 10 000 000 (expected)
        const int operation = (op >> 3) & 0x07;
        const int src = op & 0x07;
        alu8(operation, readR8(src));
        return (src == 6) ? 2 : 1;
    }

    if ((op & 0xC7) == 0xC6) {
        // ALU A, n (11 ooo 110): ooo = operation, n = immediate byte
        // 0xC7 = 11 000 111 (mask)
        // 0xC6 = 11 000 110 (expected)
        const int operation = (op >> 3) & 0x07;
        alu8(operation, fetch8());
        return 2;
    }

    if ((op & 0xC7) == 0x04) {
        // INC r8 (00 rrr 100): Increment value in register r8 by 1
        // 0xC7 = 11 000 111 (mask)
        // 0x04 = 00 000 100 (expected)
        const int reg = (op >> 3) & 0x07;
        writeR8(reg, inc8(readR8(reg)));
        return (reg == 6) ? 3 : 1;  // (HL): read + write memory
    }

    if ((op & 0xC7) == 0x05) {
        // DEC r8 ( 00 rrr 101): Decrement value in register r8 by 1
        // 0xC7 = 11 000 111 (mask)
        // 0x05 = 00 000 101 (expected)
        const int reg = (op >> 3) & 0x07;
        writeR8(reg, dec8(readR8(reg)));
        return (reg == 6) ? 3 : 1;
    }

    if ((op & 0xCF) == 0x03) {
        // INC rr (00 rr0 011): Increment value in register r16 by 1
        // Flags: None
        // Done by IDU (16-bit incrementer), not ALU -> no flags change
        // 0xCF = 11 001 111 (mask)
        // 0x03 = 00 000 011 (expected)
        const int rr = (op >> 4) & 0x03;
        writeR16(rr, static_cast<std::uint16_t>(readR16(rr) + 1));
        return 2;
    }

    if ((op & 0xCF) == 0x0B) {
        // DEC rr (00 rr1 011)
        // Flags: None
        // 0xCF = 11 001 111 (mask)
        // 0x0B = 00 001 011 (expected)
        const int rr = (op >> 4) & 0x03;
        writeR16(rr, static_cast<std::uint16_t>(readR16(rr) - 1));
        return 2;
    }

    if ((op & 0xCF) == 0x09) {
        // ADD HL, rr (00 rr1 001): Add value in r16 to HL
        // 0x09 = 00 001 001 (expected)
        const int rr = (op >> 4) & 0x03;
        addHl(readR16(rr));
        return 2;
    }

    if (op == 0xE8) {
        // ADD SP, e8: Add signed value e8 to SP
        // 0xE8 = 11 101 000
        regs_.sp = spPlusE8(fetch8());
        return 4;
    }

    if (op == 0xF8) {
        // LD HL, SP + e8: Add signed value e8 to SP and copy result in HL (SP unchanged)
        // 0xF8 = 11 111 000
        regs_.setHl(spPlusE8(fetch8()));
        return 3;
    }

    if ((op & 0xC7) == 0x07) {
        // (00 ooo 111): ooo = operation
        // RLCA / RRCA / RLA / RRA / DAA / CPL / SCF / CCF
        // 0x07 = 00 000 111 (expected)
        accFlagOp((op >> 3) & 0x07);
        return 1;
    }

    if (op == 0x18) {
        // JR e8 (00 011 000): Relative jump to address n16 = PC + e8
        jumpRelative(fetch8());
        return 3;
    }

    if ((op & 0xE7) == 0x20) {
        // JR cc, e8 (00 1cc 000): Relative jump if condition cc is met
        // 0xE7 = 11 100 111 (mask)
        // 0x20 = 00 100 000 (expected)
        const std::uint8_t e8 = fetch8();  // always read, even if not taken
        if (condition((op >> 3) & 0x03)) {
            jumpRelative(e8);
            return 3;
        }
        return 2;
    }

    if (op == 0xC3) {
        // JP n16 (11 000 011): Jump to address n16
        regs_.pc = fetch16();
        return 4;
    }

    if ((op & 0xE7) == 0xC2) {
        // JP cc, n16 (11 0cc 010): Jump to address n16 if condition cc is met
        // 0xC2 = 11 000 010 (expected)
        const std::uint16_t addr = fetch16();  // always read, even if not taken
        if (condition((op >> 3) & 0x03)) {
            regs_.pc = addr;
            return 4;
        }
        return 3;
    }

    if (op == 0xE9) {
        // JP HL (11 101 001): Jump to address HL
        regs_.pc = regs_.hl();
        return 1;
    }

    if (op == 0xCD) {
        // CALL n16 (11 001 101): Push address of next instruction, then jump to n16
        const std::uint16_t addr = fetch16();
        call(addr);
        return 6;
    }

    if ((op & 0xE7) == 0xC4) {
        // CALL cc, n16 (11 0cc 100): Call n16 if condition cc is met
        // 0xE7 = 11 100 111 (mask)
        // 0xC4 = 11 000 100 (expected)
        const std::uint16_t addr = fetch16();
        if (condition((op >> 3) & 0x03)) {
            call(addr);
            return 6;
        }
        return 3;
    }

    if (op == 0xC9) {
        // RET (11 001 001): Return from subroutine: pop PC from stack
        regs_.pc = pop16();
        return 4;
    }

    if ((op & 0xE7) == 0xC0) {
        // RET cc (11 0cc 000): Return from subroutine if condition cc is met
        // 0xC0 = 11 000 000 (expected)
        if (condition((op >> 3) & 0x03)) {
            regs_.pc = pop16();
            return 5;
        }

        return 2;  // not taken: nothing popped, SP unchanged
    }

    if ((op & 0xC7) == 0xC7) {
        // RST vec (11 nnn 111): Call address vec = nnn * 8 (0x00, 0x08,..0x38)
        // 0xC7 = 11 000 111 (mask)
        // 0x38 = 00 111 000 (keep nnn in place -> already nnn * 8)
        call(static_cast<std::uint16_t>(op & 0x38));
        return 4;
    }

    if (op == 0xF3) {
        // DI (Disable Interrupts): IME (Interrupt Master Enable) = 0 immediately
        ime_ = false;
        return 1;
    }

    if (op == 0xFB) {
        // EI (Enable Interrupts): IME = 1 after next instruction
        imeScheduled_ = true;
        return 1;
    }

    if (op == 0xD9) {
        // RETI (RETurn from Interrupt): RET + IME = 1 immediately (no delay)
        regs_.pc = pop16();
        ime_ = true;
        return 4;
    }

    if (op == 0xCB) {
        // CB prefix: next byte selects one of 256 extended instructions
        return executeCb(fetch8());
    }

    throw std::runtime_error("Unimplemented opcode");
}

std::uint8_t Cpu::fetch8() {
    const std::uint8_t value = mmu_.read(regs_.pc);
    ++regs_.pc;
    return value;
}

std::uint16_t Cpu::fetch16() {
    const std::uint8_t lowByte = fetch8();
    const std::uint8_t highByte = fetch8();
    return static_cast<std::uint16_t>((highByte << 8) | lowByte);
}

std::uint8_t Cpu::readR8(int index) const {
    switch (index) {
        case 0:
            return regs_.b;
        case 1:
            return regs_.c;
        case 2:
            return regs_.d;
        case 3:
            return regs_.e;
        case 4:
            return regs_.h;
        case 5:
            return regs_.l;
        case 6:
            return mmu_.read(regs_.hl());
        case 7:
            return regs_.a;
    }

    return 0xFF;  // unreachable: index is always 0-7
}

void Cpu::writeR8(int index, std::uint8_t value) {
    // 3-bit (r8) register index: 0 = B, 1 = C, 2 = D, 3 = E, 4 = H, 5 = L, 6 = (HL), 7 = A

    switch (index) {
        case 0:
            regs_.b = value;
            break;
        case 1:
            regs_.c = value;
            break;
        case 2:
            regs_.d = value;
            break;
        case 3:
            regs_.e = value;
            break;
        case 4:
            regs_.h = value;
            break;
        case 5:
            regs_.l = value;
            break;
        case 6:
            mmu_.write(regs_.hl(), value);
            break;
        case 7:
            regs_.a = value;
            break;
    }
}

std::uint16_t Cpu::readR16(int index) const {
    // r16: 0 = BC, 1 = DE, 2 = HL, 3 = SP

    switch (index) {
        case 0:
            return regs_.bc();
        case 1:
            return regs_.de();
        case 2:
            return regs_.hl();
        case 3:
            return regs_.sp;
    }

    return 0xFFFF;  // unreachable: index is always 0-3
}

void Cpu::writeR16(int index, std::uint16_t value) {
    // r16: 0 = BC, 1 = DE, 2 = HL, 3 = SP
    switch (index) {
        case 0:
            regs_.setBc(value);
            break;
        case 1:
            regs_.setDe(value);
            break;
        case 2:
            regs_.setHl(value);
            break;
        case 3:
            regs_.sp = value;
            break;
    }
}

std::uint16_t Cpu::readR16Stk(int index) const {
    // r16stk: 0 = BC, 1 = DE, 2 = HL, 3 = AF

    switch (index) {
        case 0:
            return regs_.bc();
        case 1:
            return regs_.de();
        case 2:
            return regs_.hl();
        case 3:
            return regs_.af();
    }

    return 0xFFFF;  // unreachable: index is always 0-3
}

void Cpu::writeR16Stk(int index, std::uint16_t value) {
    // r16stk: 0 = BC, 1 = DE, 2 = HL, 3 = AF
    switch (index) {
        case 0:
            regs_.setBc(value);
            break;
        case 1:
            regs_.setDe(value);
            break;
        case 2:
            regs_.setHl(value);
            break;
        case 3:
            regs_.setAf(value);
            break;
    }
}

std::uint16_t Cpu::addrR16Mem(int index) {
    // r16mem: 0 = BC, 1 = DE, 2 = HL+, 3 = HL-
    switch (index) {
        case 0:
            return regs_.bc();
        case 1:
            return regs_.de();
        case 2: {
            const std::uint16_t addr = regs_.hl();
            regs_.setHl(static_cast<std::uint16_t>(addr + 1));
            return addr;
        }
        case 3: {
            const std::uint16_t addr = regs_.hl();
            regs_.setHl(static_cast<std::uint16_t>(addr - 1));
            return addr;
        }
    }

    return 0xFFFF;  // unreachable
}

void Cpu::alu8(int operation, std::uint8_t value) {
    // operation - ooo: 0 = ADD, 1 = ADC, 2 = SUB, 3 = SBC, 4 = AND, 5 = XOR, 6 = OR, 7 = CP
    switch (operation) {
        case 0:
            add8(value, false);
            break;
        case 1:
            add8(value, regs_.getFlag(Flag::C));
            break;
        case 2:
            sub8(value, false);
            break;
        case 3:
            sub8(value, regs_.getFlag(Flag::C));
            break;
        case 4:
            and8(value);
            break;
        case 5:
            xor8(value);
            break;
        case 6:
            or8(value);
            break;
        case 7:
            cp8(value);
            break;

        default:
            throw std::logic_error("unreachable: operation must be 0 - 7");
    }
}

void Cpu::add8(std::uint8_t value, bool carry) {
    // carry = false: ADD A, value, A = A + value
    // carry = old C: ADC A, value, A = A + value + carry
    // Destination is always A (no 8-bit ADD into other registers)
    // Flags: Z 0 H C
    const std::uint8_t a = regs_.a;
    const int carryIn = carry ? 1 : 0;
    const int sum = a + value + carryIn;  // int keeps bit 8, needed for carry check
    const std::uint8_t result = static_cast<std::uint8_t>(sum);

    const bool z = (result == 0);
    const bool h =
        ((a & 0x0F) + (value & 0x0F) + carryIn) >
        0x0F;  // 0x0F = 0000 1111 -> keeps only low nibbles -> sum > 0x0F means carry from bit 3
    const bool c = sum > 0xFF;  // means carry from bit 7

    regs_.a = result;
    setFlags(z, false, h, c);
}

void Cpu::sub8(std::uint8_t value, bool carry) {
    // carry = false: SUB A, value, A = A - value
    // carry = old C: SBC A, value, A = A - value - carry
    // SUB A, value: A = A - value. Destination is always A
    // Flags: Z 1 H C
    const std::uint8_t a = regs_.a;
    const int carryIn = carry ? 1 : 0;
    const int diff = a - value - carryIn;                         // int can go negative
    const std::uint8_t result = static_cast<std::uint8_t>(diff);  // wraps: 0x10 - 0x20 = 0xF0

    const bool z = (result == 0);
    // H: & 0x0F keeps only low nibbles: A's low nibble < (value + carry) means borrow from bit 4
    const bool h = (a & 0x0F) < (value & 0x0F) + carryIn;
    const bool c = diff < 0;  // C: negative difference means borrow

    regs_.a = result;
    setFlags(z, true, h, c);
}

void Cpu::cp8(std::uint8_t value) {
    // CP A, value: SUB that discards result (A unchanged)
    // Flags: Z 1 H C
    const std::uint8_t a = regs_.a;
    sub8(value, false);
    regs_.a = a;
}

void Cpu::and8(std::uint8_t value) {
    // AND A, value: A = A & value
    // Flags: Z 0 1 0
    // H is always 1 for AND: defined by hardware
    regs_.a = static_cast<std::uint8_t>(regs_.a & value);
    setFlags(regs_.a == 0, false, true, false);
}

void Cpu::xor8(std::uint8_t value) {
    // XOR A, value: A = A ^ value (bitwise)
    // Flags: Z 0 0 0
    regs_.a = static_cast<std::uint8_t>(regs_.a ^ value);
    setFlags(regs_.a == 0, false, false, false);
}

void Cpu::or8(std::uint8_t value) {
    // OR A, value: A = A | value (bitwise)
    // Flags: Z 0 0 0
    regs_.a = static_cast<std::uint8_t>(regs_.a | value);
    setFlags(regs_.a == 0, false, false, false);
}

std::uint8_t Cpu::inc8(std::uint8_t value) {
    // INC: value + 1
    // Flags: Z 0 H. C unchanged
    const std::uint8_t result = static_cast<std::uint8_t>(value + 1);  // wraps: 0xFF + 1 = 0x00
    const bool h = (value & 0x0F) == 0x0F;  // H: low nibble F + 1 carries from bit 3
    setFlags(result == 0, false, h, regs_.getFlag(Flag::C));
    return result;
}

std::uint8_t Cpu::dec8(std::uint8_t value) {
    // DEC: value - 1
    // Flags: Z 1 H. C unchanged
    const std::uint8_t result = static_cast<std::uint8_t>(value - 1);  // wraps: 0x00 - 1 = 0xFF
    const bool h = (value & 0x0F) == 0x00;  // H: low nibble 0 - 1 borrows from bit 4
    setFlags(result == 0, true, h, regs_.getFlag(Flag::C));
    return result;
}

void Cpu::addHl(std::uint16_t value) {
    // ADD HL, value: HL = HL + value
    // Flags: - 0 H C (Z unchanged)
    const std::uint16_t hl = regs_.hl();
    const int sum = hl + value;  // int keeps bit 16, needed for carry check

    // H: & 0x0FFF keeps low 12 bits. Sum > 0x0FFF means carry from bit 11
    const bool h = ((hl & 0x0FFF) + (value & 0x0FFF)) > 0x0FFF;
    const bool c = sum > 0xFFFF;  // C: sum > 0xFFFF means carry from bit 15

    regs_.setHl(static_cast<std::uint16_t>(sum));
    setFlags(regs_.getFlag(Flag::Z), false, h, c);  // old Z
}

std::uint16_t Cpu::spPlusE8(std::uint8_t e8) {
    // ADD signed value e8 to SP
    // Flags: 0 0 H C
    const int offset = (e8 < 0x80) ? e8 : e8 - 0x100;
    const std::uint16_t sp = regs_.sp;
    const std::uint16_t result = static_cast<std::uint16_t>(sp + offset);

    const bool h = ((sp & 0x0F) + (e8 & 0x0F)) > 0x0F;  // carry from bit 3
    const bool c = ((sp & 0xFF) + e8) > 0xFF;           // carry from bit 7

    setFlags(false, false, h, c);
    return result;
}

void Cpu::accFlagOp(int operation) {
    // 0 = RLCA, 1 = RRCA, 2 = RLA, 3 = RRA, 4 = DAA, 5 = CPL, 6 = SCF, 7 = CCF
    const std::uint8_t a = regs_.a;
    const bool oldZ = regs_.getFlag(Flag::Z);
    const bool oldC = regs_.getFlag(Flag::C);

    switch (operation) {
        case 0:
        case 1:
        case 2:
        case 3:
            // RLCA / RRCA / RLA / RRA = CB RLC / RRC / RL / RR on A (same operation index)
            // Flags: 0 0 0 C (unlike CB version, Z is always 0)
            regs_.a = rotateShift(operation, a);
            setFlags(false, false, false, regs_.getFlag(Flag::C));
            break;
        case 4: {
            // DAA: Decimal Adjust Accumulator: fix A after a BCD ADD/ADC/SUB/SBC
            // Flags: Z - 0 C (N unchanged, H cleared and C only set, never cleared)
            const bool n = regs_.getFlag(Flag::N);
            const bool h = regs_.getFlag(Flag::H);
            bool carry = oldC;
            int adjust = 0;
            if (!n) {
                // After an addition: a digit above 9 or a (half) carry out of it, needs + 6
                if (h || (a & 0x0F) > 0x09) {
                    adjust |= 0x06;  // fix low digit
                }
                if (oldC || a > 0x99) {
                    adjust |= 0x60;  // fix high digit
                    carry = true;    // BCD result is >= 100
                }
                regs_.a = static_cast<std::uint8_t>(a + adjust);
            } else {
                // After a subtraction: only a borrow needs a fix (H -> -0x06, C -> -0x60).
                // Digit range is not checked
                if (h) {
                    adjust |= 0x06;
                }
                if (oldC) {
                    adjust |= 0x60;
                }
                regs_.a = static_cast<std::uint8_t>(a - adjust);
            }
            setFlags(regs_.a == 0, n, false, carry);
            break;
        }
        case 5:
            // CPL A = ~A (bitwise NOT)
            // Flags: - 1 1 -
            regs_.a = static_cast<std::uint8_t>(~a);
            setFlags(oldZ, true, true, oldC);
            break;
        case 6:
            // SCF: Set Carry Flag
            // Flags: - 0 0 1
            setFlags(oldZ, false, false, true);
            break;
        case 7:
            // CCF: Complement (invert) Carry Flag
            // Flags: - 0 0 !C
            setFlags(oldZ, false, false, !oldC);
            break;
        default:
            throw std::logic_error("unreachable: operation must be 0 - 7");
    }
}

bool Cpu::condition(int cc) const {
    // Condition code (cc): 0 = NZ, 1 = Z, 2 = NC, 3 = C
    switch (cc) {
        case 0:
            return !regs_.getFlag(Flag::Z);
        case 1:
            return regs_.getFlag(Flag::Z);
        case 2:
            return !regs_.getFlag(Flag::C);
        case 3:
            return regs_.getFlag(Flag::C);
        default:
            throw std::logic_error("unreachable: cc must be 0 - 3");
    }
}

void Cpu::jumpRelative(std::uint8_t e8) {
    // e8: 0x00-0x7F = 0..127, 0x80-0xFF = -128..-1
    // bit 7 is sign bit
    const int offset = (e8 < 0x80) ? e8 : e8 - 0x100;
    regs_.pc = static_cast<std::uint16_t>(regs_.pc + offset);
}

void Cpu::call(std::uint16_t addr) {
    // PC already points to next instruction
    push16(regs_.pc);
    regs_.pc = addr;
}

int Cpu::executeCb(std::uint8_t op) {
    // CB opcode (xx yyy zzz): xx = group, yyy = operation, zzz = r8 index
    const int group = op >> 6;
    const int operation = (op >> 3) & 0x07;
    const int reg = op & 0x07;

    if (group == 0) {
        // Rotate / shift / swap r8 (00 yyy zzz)
        writeR8(reg, rotateShift(operation, readR8(reg)));
        return (reg == 6) ? 4 : 2;  // (HL): prefix + opcode + read + write
    }

    const int bit = operation;  // group 01 / 10 / 11: yyy is bit index
    const std::uint8_t mask = static_cast<std::uint8_t>(1 << bit);
    const std::uint8_t value = readR8(reg);

    if (group == 1) {
        // BIT b, r8 (01 bbb zzz): Test bit b. Z = 1 if bit is 0
        // Flags: Z 0 1 - (C unchanged). r8 is only read, never written
        setFlags((value & mask) == 0, false, true, regs_.getFlag(Flag::C));
        return (reg == 6) ? 3 : 2;  // (HL): prefix + opcode + read (no write)
    }

    if (group == 2) {
        // RES b, r8 (10 bbb zzz): Reset bit b to 0.
        // Flags: - - - - (unchanged)
        writeR8(reg, static_cast<std::uint8_t>(value & ~mask));
        return (reg == 6) ? 4 : 2;  // (HL): prefix + opcode + read + write
    }

    if (group == 3) {
        // SET b, r8 (11 bbb zzz): Set bit b to 1.
        // Flags: - - - - (unchanged)
        writeR8(reg, static_cast<std::uint8_t>(value | mask));
        return (reg == 6) ? 4 : 2;
    }
    throw std::logic_error("unreachable: group must be 0 - 3");
}

std::uint8_t Cpu::rotateShift(int operation, std::uint8_t value) {
    // Flags: Z 0 0 C (SWAP: C = 0)
    // Unlike RLCA / RRCA / RLA / RRA, Z is computed
    const bool oldC = regs_.getFlag(Flag::C);
    const bool bit7 = (value & 0x80) != 0;
    const bool bit0 = (value & 0x01) != 0;
    int result = 0;
    bool carry = false;

    switch (operation) {
        case 0:
            // RLC: Rotate left circularly: bit 7 -> bit 0 and -> C
            result = (value << 1) | (bit7 ? 0x01 : 0);
            carry = bit7;
            break;
        case 1:
            // RRC: Rotate right circularly: bit 0 -> bit 7 and -> C
            result = (value >> 1) | (bit0 ? 0x80 : 0);
            carry = bit0;
            break;
        case 2:
            // RL: Rotate left through carry: old C -> bit 0, bit 7 -> C
            result = (value << 1) | (oldC ? 0x01 : 0);
            carry = bit7;
            break;
        case 3:
            // RR: Rotate right through carry: old C -> bit 7, bit 0 -> C
            result = (value >> 1) | (oldC ? 0x80 : 0);
            carry = bit0;
            break;
        case 4:
            // SLA: Shift Left Arithmetically: 0 -> bit 0, bit 7 -> C
            result = value << 1;
            carry = bit7;
            break;
        case 5:
            // SRA: Shift Right Arithmetically: bit 7 stays (sign kept), bit 0 -> C
            result = (value >> 1) | (value & 0x80);
            carry = bit0;
            break;
        case 6:
            // SWAP: Exchange high and low nibbles
            result = (value << 4) | (value >> 4);
            carry = false;
            break;
        case 7:
            // SRL: Shift Right Logically: 0 -> bit 7, bit 0 -> C
            result = value >> 1;
            carry = bit0;
            break;
        default:
            throw std::logic_error("unreachable: operation must be 0 - 7");
    }

    const std::uint8_t r = static_cast<std::uint8_t>(result);
    setFlags(r == 0, false, false, carry);
    return r;
}

void Cpu::setFlags(bool z, bool n, bool h, bool c) {
    // f  = [ Z | N | H | C | 0 | 0 | 0 | 0 ]
    // Z (0x80): set if result is 0
    // N (0x40): set if operation was a subtraction
    // H (0x20): set if carry from bit 3 / borrow from bit 4
    // C (0x10): set if carry from bit 7 / borrow (A < value)
    regs_.f = static_cast<std::uint8_t>((z ? 0x80 : 0) | (n ? 0x40 : 0) | (h ? 0x20 : 0) |
                                        (c ? 0x10 : 0));
}

void Cpu::push16(std::uint16_t value) {
    // Push 16-bit value onto stack
    // high byte at SP - 1
    // low byte at SP - 2

    const std::uint8_t highByte = static_cast<std::uint8_t>(value >> 8);
    const std::uint8_t lowByte = static_cast<std::uint8_t>(value & 0xFF);

    --regs_.sp;
    mmu_.write(regs_.sp, highByte);

    --regs_.sp;
    mmu_.write(regs_.sp, lowByte);
}

std::uint16_t Cpu::pop16() {
    // Pop 16-bit value from stack
    // low byte at SP
    // high byte at SP + 1
    const std::uint8_t lowByte = mmu_.read(regs_.sp);
    ++regs_.sp;

    const std::uint8_t highByte = mmu_.read(regs_.sp);
    ++regs_.sp;

    return static_cast<std::uint16_t>((highByte << 8) | lowByte);
}
}  // namespace gb
