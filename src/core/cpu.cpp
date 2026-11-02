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
}

int Cpu::step() {
    const std::uint8_t op = fetch8();

    if (op == 0x00) {
        // NOP
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
        // HALT: must be checked before LD r, r' (both match 01 xxx xxx)
        throw std::runtime_error("HALT not implemented yet");
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