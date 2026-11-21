#include "core/cartridge.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

using gb::Cartridge;

namespace {

// 32 KiB ROM filled with 0x00. Header fields are set per test
std::vector<std::uint8_t> makeRom(std::size_t size = 0x8000) {
    return std::vector<std::uint8_t>(size, 0x00);
}

void writeTitle(std::vector<std::uint8_t>& rom, const std::string& title) {
    for (std::size_t i = 0; i < title.size(); ++i) {
        rom[0x0134 + i] = static_cast<std::uint8_t>(title[i]);
    }
}
}  // namespace

TEST(CartridgeTest, ParsesTitleUpToFirstZeroByte) {
    auto rom = makeRom();
    writeTitle(rom, "CPU_INSTRS");  // 10 chars, rest of 0x0134-0x0143 stays 0x00
    const Cartridge cart(rom);
    EXPECT_EQ(cart.header().title, "CPU_INSTRS");
}

TEST(CartridgeTest, TitleUsesAllSixteenBytesWhenNotPadding) {
    auto rom = makeRom();
    writeTitle(rom, "ABCDEFGHIJKLMNOP");  // 0x0134-0x0143 full, 0x0144 is not title
    rom[0x0144] = 'X';
    const Cartridge cart(rom);
    EXPECT_EQ(cart.header().title, "ABCDEFGHIJKLMNOP");
}

TEST(CartridgeTest, ParsesCartridgeType) {
    auto rom = makeRom();
    rom[0x0147] = 0x01;
    const Cartridge cart(rom);
    EXPECT_EQ(cart.header().type, 0x01);
}

TEST(CartridgeTest, RomSizeIs32KiBShiftedByCode) {
    auto rom = makeRom();
    rom[0x0148] = 0x00;
    EXPECT_EQ(Cartridge(rom).header().romSize, 0x8000u);  // 32 KiB << 0

    rom[0x0148] = 0x01;
    EXPECT_EQ(Cartridge(rom).header().romSize, 0x10000u);  // 32 KiB << 1

    rom[0x0148] = 0x08;
    EXPECT_EQ(Cartridge(rom).header().romSize, 0x800000u);  // 32 KiB << 8
}

TEST(CartridgeTest, UnknownRomSizeCodeThrows) {
    auto rom = makeRom();
    rom[0x0148] = 0x09;  // no such size on DMG (Dot Matrix Game - GameBoy)
    EXPECT_THROW(Cartridge{rom}, std::runtime_error);
}

TEST(CartridgeTest, RamSizeFromCode) {
    auto rom = makeRom();
    rom[0x0149] = 0x00;
    EXPECT_EQ(Cartridge(rom).header().ramSize, 0x00u);  // No RAM

    rom[0x0149] = 0x02;
    EXPECT_EQ(Cartridge(rom).header().ramSize, 0x2000u);  // 8 KiB

    rom[0x0149] = 0x03;
    EXPECT_EQ(Cartridge(rom).header().ramSize, 0x8000u);  // 32 KiB

    rom[0x0149] = 0x04;
    EXPECT_EQ(Cartridge(rom).header().ramSize, 0x20000u);  // 128 KiB

    rom[0x0149] = 0x05;
    EXPECT_EQ(Cartridge(rom).header().ramSize, 0x10000u);  // 64 KiB
}

TEST(CartridgeTest, RamSizeCode0x01TreatedAsNoRam) {
    auto rom = makeRom();
    rom[0x0149] = 0x01;
    EXPECT_EQ(Cartridge(rom).header().ramSize, 0u);
}

TEST(CartridgeTest, UnknownRamSizeCodeThrows) {
    auto rom = makeRom();
    rom[0x0149] = 0x06;
    EXPECT_THROW(Cartridge{rom}, std::runtime_error);
}

TEST(CartridgeTest, ChecksumValidFromAllZeroHeader) {
    auto rom = makeRom();
    rom[0x014D] = 0xE7;  // 25 bytes of 0x00: 0 - 25 * 1 = -25 = 0xE7
    EXPECT_TRUE(Cartridge(rom).header().checksumValid);
}

TEST(CartridgeTest, ChecksumValidWithTitleByte) {
    auto rom = makeRom();
    rom[0x0134] = 'A';   // 0x41
    rom[0x014D] = 0xA6;  // -25 - 0x41 = -90 = 0xA6
    EXPECT_TRUE(Cartridge(rom).header().checksumValid);
}

TEST(CartridgeTest, ChecksumInvalidWhenMismatch) {
    auto rom = makeRom();
    rom[0x014D] = 0xE6;
    EXPECT_FALSE(Cartridge(rom).header().checksumValid);
}

TEST(CartridgeTest, ChecksumCoversLastByte0x014C) {
    auto rom = makeRom();
    rom[0x014C] = 0x01;
    rom[0x014D] = 0xE7;  // valid for all-zero header
    EXPECT_FALSE(Cartridge(rom).header().checksumValid);
}

TEST(CartridgeTest, ChecksumIgnoresBytesOutsideRange) {
    auto rom = makeRom();
    rom[0x0133] = 0x01;
    rom[0x014D] = 0xE7;  // valid for all-zero header
    rom[0x014E] = 0x01;
    EXPECT_TRUE(Cartridge(rom).header().checksumValid);
}

TEST(CartridgeTest, RomSmallerThanHeaderThrows) {
    EXPECT_THROW(Cartridge{makeRom(0x014F)}, std::runtime_error);  // header ends at 0x014F
    EXPECT_NO_THROW(Cartridge{makeRom(0x0150)});                   // smallest valid size
}

TEST(CartridgeTest, KeepsRomBytes) {
    auto rom = makeRom();
    rom[0x0150] = 0xC3;  // first byte of game code
    const Cartridge cart(rom);
    ASSERT_EQ(cart.rom().size(), 0x8000u);
    EXPECT_EQ(cart.rom()[0x0150], 0xC3);
}

TEST(CartridgeTest, FromFileLoadsRom) {
    auto rom = makeRom();
    writeTitle(rom, "FILETEST");
    const auto path = std::filesystem::temp_directory_path() / "gbemu_cartridge_test.gb";
    {
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(rom.data()),
                  static_cast<std::streamsize>(rom.size()));
    }

    const Cartridge cart = Cartridge::fromFile(path.string());
    std::filesystem::remove(path);

    EXPECT_EQ(cart.header().title, "FILETEST");
    EXPECT_EQ(cart.rom().size(), rom.size());
}

TEST(CartridgeTest, FromFileMissingThrowsWithPath) {
    // Message must name file: an empty read would also throw "ROM too small"
    // so checking only expection type can't tell two errors part
    try {
        Cartridge::fromFile("/no/such/dir/missing.gb");
        FAIL() << "expected std::runtime_error";
    } catch (const std::runtime_error& e) {
        EXPECT_NE(std::string(e.what()).find("missing.gb"), std::string::npos);
    }
}
