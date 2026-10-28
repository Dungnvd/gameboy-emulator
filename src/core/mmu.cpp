#include "core/mmu.h"

namespace gb {
void Mmu::loadRom(const std::vector<std::uint8_t>& rom) { rom_ = rom; }

std::uint8_t Mmu::read(std::uint16_t addr) const {
    if (addr < 0x8000) {
        // ROM: out-of-range reads return 0xFF (open bus)
        return addr < rom_.size() ? rom_[addr] : 0xFF;
    }

    if (addr < 0xA000) {
        return vram_[addr - 0x8000];
    }

    if (addr < 0xC000) {
        return extRam_[addr - 0xA000];
    }

    if (addr < 0xE000) {
        return wram_[addr - 0xC000];
    }

    if (addr < 0xFE00) {
        // Echo RAM: mirrors C000-DDFF (E000 -> wram_[0])
        return wram_[addr - 0xE000];
    }

    if (addr < 0xFEA0) {
        return oam_[addr - 0xFE00];
    }

    if (addr < 0xFF00) {
        // Not usable: DMG reads 0x00
        return 0x00;
    }

    if (addr < 0xFF80) {
        return io_[addr - 0xFF00];
    }

    if (addr < 0xFFFF) {
        return hram_[addr - 0xFF80];
    }

    return ie_;  // 0xFFFF: Interrupt Enable
}

void Mmu::write(std::uint16_t addr, std::uint8_t value) {
    if (addr < 0x8000) {
        // ROM: read only, ignored write
        return;
    }

    if (addr < 0xA000) {
        vram_[addr - 0x8000] = value;
        return;
    }

    if (addr < 0xC000) {
        extRam_[addr - 0xA000] = value;
        return;
    }

    if (addr < 0xE000) {
        wram_[addr - 0xC000] = value;
        return;
    }

    if (addr < 0xFE00) {
        wram_[addr - 0xE000] = value;
        return;
    }

    if (addr < 0xFEA0) {
        oam_[addr - 0xFE00] = value;
        return;
    }

    if (addr < 0xFF00) {
        // Not usable: writes are ignored
        return;
    }

    if (addr < 0xFF80) {
        io_[addr - 0xFF00] = value;
        return;
    }

    if (addr < 0xFFFF) {
        hram_[addr - 0xFF80] = value;
        return;
    }

    ie_ = value;  // 0xFFFF: Interrupt Enable
}

std::uint16_t Mmu::read16(std::uint16_t addr) const {
    const auto lowByte = read(addr);
    const auto highByte = read(static_cast<std::uint16_t>(addr + 1));
    return static_cast<std::uint16_t>((highByte << 8) | lowByte);
}

void Mmu::write16(std::uint16_t addr, std::uint16_t value) {
    const auto lowByte = static_cast<std::uint8_t>(value & 0xFF);
    const auto highByte = static_cast<std::uint8_t>(value >> 8);
    write(addr, lowByte);
    write(static_cast<std::uint16_t>(addr + 1), highByte);
}
}  // namespace gb
