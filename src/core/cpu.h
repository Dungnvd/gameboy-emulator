#pragma once

#include <cstdint>

#include "core/mmu.h"
#include "core/registers.h"

namespace gb {

// Sharp SM83 CPU core
// Ref: https://gbdev.io/pandocs/CPU_Instruction_Set.html
//      https://gbdev.io/pandocs/Power_Up_Sequence.html
class Cpu {
public:
    explicit Cpu(Mmu& mmu);

    // Set registers to DMG post-boot state (no boot ROM)
    void reset();

    // Execute one instruction. Returns number of M-cycles taken
    int step();

    Registers& regs() { return regs_; }
    const Registers& regs() const { return regs_; }

private:
    std::uint8_t fetch8();
    std::uint16_t fetch16();

    // 3-bit (r8) register index: 0 = B, 1 = C, 2 = D, 3 = E, 4 = H, 5 = L, 6 = (HL), 7 = A
    std::uint8_t readR8(int index) const;
    void writeR8(int index, std::uint8_t value);

    // 2-bit register pair index
    // r16: 0 = BC, 1 = DE, 2 = HL, 3 = SP
    // r16stk: 0 = BC, 1 = DE, 2 = HL, 3 = AF
    std::uint16_t readR16(int index) const;
    void writeR16(int index, std::uint16_t value);
    std::uint16_t readR16Stk(int index) const;
    void writeR16Stk(int index, std::uint16_t value);

    // r16mem: 0 = BC, 1 = DE, 2 = HL+, 3 = HL-
    // Returns address and applies HL increment/decrement
    std::uint16_t addrR16Mem(int index);

    void push16(std::uint16_t value);
    std::uint16_t pop16();

    Mmu& mmu_;
    Registers regs_;
};
}  // namespace gb