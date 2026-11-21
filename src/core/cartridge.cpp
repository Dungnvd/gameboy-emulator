#include "core/cartridge.h"

#include <fstream>
#include <iterator>
#include <stdexcept>
#include <utility>

namespace gb {

namespace {

std::size_t ramSizeFromCode(std::uint8_t code) {
    switch (code) {
        case 0x00:  // No RAM
        case 0x01:  // Unused
            return 0;
        case 0x02:
            return 0x2000;  // 8 KiB
        case 0x03:
            return 0x8000;  // 32 KiB (4 banks of 8 KiB)
        case 0x04:
            return 0x20000;  // 128 KiB (16 banks of 8 KiB)
        case 0x05:
            return 0x10000;  // 64 KiB (8 banks of 8 KiB)
        default:
            throw std::runtime_error("Unknown RAM size code");
    }
}
}  // namespace
Cartridge::Cartridge(std::vector<std::uint8_t> rom) : rom_(std::move(rom)) {
    if (rom_.size() < 0x0150) {
        throw std::runtime_error("ROM too small: header ends at 0x014F");
    }

    // Title: up to 16 bytes, stops at first 0x00 (padding)
    for (std::size_t addr = 0x0134; addr <= 0x0143 && rom_[addr] != 0x00; ++addr) {
        header_.title += static_cast<char>(rom_[addr]);
    }

    header_.type = rom_[0x0147];

    const std::uint8_t romCode = rom_[0x0148];
    if (romCode > 0x08) {
        throw std::runtime_error("Unknown ROM size code");
    }

    header_.romSize = static_cast<std::size_t>(0x8000) << romCode;  // 32 KiB << code

    header_.ramSize = ramSizeFromCode(rom_[0x0149]);

    // Header checksum: x = x - byte - 1 over 0x0134-0x014C, keep low 8 bits
    std::uint8_t checksum = 0;
    for (std::size_t addr = 0x0134; addr <= 0x014C; ++addr) {
        checksum = static_cast<std::uint8_t>(checksum - rom_[addr] - 1);
    }
    header_.checksumValid = (checksum == rom_[0x014D]);
}
Cartridge Cartridge::fromFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Can't open ROM file: " + path);
    }
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)),
                                    std::istreambuf_iterator<char>());

    return Cartridge(std::move(bytes));
}

}  // namespace gb
