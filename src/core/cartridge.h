#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace gb {

// Cartridge header: 0x0100-0x014F
// Ref: https://gbdev.io/pandocs/The_Cartridge_Header.html
struct CartridgeHeader {
    std::string title;           // 0x0134-0x0143, ASCII, padded with 0x00
    std::uint8_t type = 0;       // 0x0147: 0x00 = ROM ONLY, 0x01-0x03 = MBC1
    std::size_t romSize = 0;     // 0x0148: 32 KiB << code (bytes)
    std::size_t ramSize = 0;     // 0x0149: External RAM size (bytes)
    bool checksumValid = false;  // 0x014D matches checksum of 0x0134-0x014C
};

class Cartridge {
public:
    // Parse header from ROM bytes. Throws std::runtime_error if ROM is
    // too small or size code is unknown
    explicit Cartridge(std::vector<std::uint8_t> rom);

    // Read whole file (binary) and parse it. Throws std::runtime_error if file can't be opened
    static Cartridge fromFile(const std::string& path);

    const CartridgeHeader& header() const { return header_; }
    const std::vector<std::uint8_t>& rom() const { return rom_; }

private:
    std::vector<std::uint8_t> rom_;
    CartridgeHeader header_;
};
}  // namespace gb
