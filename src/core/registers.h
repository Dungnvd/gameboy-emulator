#pragma once

#include <cstdint>

namespace gb {

// Bit opsitions of the flags in register F.
enum class Flag : std::uint8_t {
    Z = 1 << 7,  // Zero flag: 1000 0000
    N = 1 << 6,  // Subtraction flag: 0100 0000
    H = 1 << 5,  // Half Carry flag: 0010 0000
    C = 1 << 4,  // Carry flag: 0001 0000
};

// The SM83 register file.
// Ref: https://gbdev.io/pandocs/CPU_Registers_and_Flags.html
struct Registers {
    std::uint8_t a = 0, f = 0;  // Accumulator & flags, f  = [ Z | N | H | C | 0 | 0 | 0 | 0 ]
    std::uint8_t b = 0, c = 0;
    std::uint8_t d = 0, e = 0;
    std::uint8_t h = 0, l = 0;
    std::uint16_t sp = 0;  // Stack Pointer
    std::uint16_t pc = 0;  // Program Counter/Pointer

    std::uint16_t af() const;
    std::uint16_t bc() const;
    std::uint16_t de() const;
    std::uint16_t hl() const;

    void setAf(std::uint16_t value);
    void setBc(std::uint16_t value);
    void setDe(std::uint16_t value);
    void setHl(std::uint16_t value);

    bool getFlag(Flag which) const;
    void setFlag(Flag which, bool on);
};
}  // namespace gb