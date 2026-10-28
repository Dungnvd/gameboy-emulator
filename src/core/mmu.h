#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace gb {

// Address decoder for 64KB address space
// Ref: https://gbdev.io/pandocs/Memory_Map.html
class Mmu {
public:
    void loadRom(const std::vector<std::uint8_t>& rom);

    std::uint8_t read(std::uint16_t addr) const;
    void write(std::uint16_t addr, std::uint8_t value);

    // 16-bit access, little-endian (low byte at addr)
    std::uint16_t read16(std::uint16_t addr) const;
    void write16(std::uint16_t addr, std::uint16_t value);

private:
    std::vector<std::uint8_t> rom_;              // 0000-7FFF (32 KiB, more with MBC)
    std::array<std::uint8_t, 0x2000> vram_{};    // 8000-9FFF (8 KiB)
    std::array<std::uint8_t, 0x2000> extRam_{};  // A000-BFFF (8 KiB)
    std::array<std::uint8_t, 0x2000> wram_{};    // C000-DFFF (8 KiB), echoed at E000-FDFF
    std::array<std::uint8_t, 0xA0> oam_{};       // FE00-FE9F (160 B)
    std::array<std::uint8_t, 0x80> io_{};        // FF00-FF7F (128 B)
    std::array<std::uint8_t, 0x7F> hram_{};      // FF80-FFFE (127 B)
    std::uint8_t ie_ = 0;                        // FFFF
};
}  // namespace gb
