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

    throw std::runtime_error("Unimplemented opcode");
}

std::uint8_t Cpu::fetch8() {
    const std::uint8_t value = mmu_.read(regs_.pc);
    ++regs_.pc;
    return value;
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
}  // namespace gb
