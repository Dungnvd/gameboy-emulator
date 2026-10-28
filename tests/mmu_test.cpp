#include "core/mmu.h"

#include <gtest/gtest.h>

#include <vector>

using gb::Mmu;

TEST(Mmu, WramReadsBackWhatWasWritten) {
    Mmu mmu;
    mmu.write(0xC000, 0x42);
    EXPECT_EQ(mmu.read(0xC000), 0x42);
}

TEST(Mmu, EchoRamMirrorsWram) {
    // Echo RAM E000-FDFF mirrors WRAM C000-DDFF (offset 0x2000)
    Mmu mmu;
    mmu.write(0xC123, 0xAB);
    EXPECT_EQ(mmu.read(0xE123), 0xAB);  // read through echo: 0xC123 + 0x2000

    mmu.write(0xE456, 0xCD);
    EXPECT_EQ(mmu.read(0xC456), 0xCD);  // write through echo
}

TEST(Mmu, RomIsReadOnly) {
    std::vector<std::uint8_t> rom(0x8000, 0x00);  // 32KB
    rom[0x0100] = 0x3C;

    Mmu mmu;
    mmu.loadRom(rom);
    mmu.write(0x0100, 0xFF);            // try to overwrite
    EXPECT_EQ(mmu.read(0x0100), 0x3C);  // must still be 0x3C
}

TEST(Mmu, VramAndExtRamAreSeparate) {
    Mmu mmu;
    mmu.write(0x9FFF, 0x11);
    mmu.write(0xA000, 0x22);
    EXPECT_EQ(mmu.read(0x9FFF), 0x11);
    EXPECT_EQ(mmu.read(0xA000), 0x22);
}

TEST(Mmu, HramAndIeAreSeparate) {
    Mmu mmu;
    mmu.write(0xFF80, 0x11);
    mmu.write(0xFFFE, 0x22);
    mmu.write(0xFFFF, 0x33);
    EXPECT_EQ(mmu.read(0xFF80), 0x11);
    EXPECT_EQ(mmu.read(0xFFFE), 0x22);
    EXPECT_EQ(mmu.read(0xFFFF), 0x33);
}

TEST(Mmu, Read16IsLittleEndian) {
    // Little-endian: low byte at addr, high byte at addr + 1
    // e.g. 0x1234 in memory:
    //   0xC000: 34  <- low byte first
    //   0xC001: 12  <- high byte second
    Mmu mmu;
    mmu.write(0xC000, 0x34);
    mmu.write(0xC001, 0x12);
    EXPECT_EQ(mmu.read16(0xC000), 0x1234);
}

TEST(Mmu, Write16IsLittleEndian) {
    Mmu mmu;
    mmu.write16(0xC000, 0xBEEF);
    EXPECT_EQ(mmu.read(0xC000), 0xEF);
    EXPECT_EQ(mmu.read(0xC001), 0xBE);
}