#include "core/cpu.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <initializer_list>
#include <vector>

using gb::Cpu;
using gb::Mmu;

// Test fixture: every TEST_F gets a fresh Mmu and Cpu
class CpuTest : public ::testing::Test {
protected:
    // Put program at 0x0100 (post-boot PC) and reset CPU
    void load(std::initializer_list<std::uint8_t> program) {
        std::vector<std::uint8_t> rom(0x8000, 0x00);
        std::copy(program.begin(), program.end(), rom.begin() + 0x0100);
        mmu.loadRom(rom);
        cpu.reset();
    }

    Mmu mmu;
    Cpu cpu{mmu};
};

TEST_F(CpuTest, ResetSetsPostBootState) {
    load({});
    const auto& r = cpu.regs();
    EXPECT_EQ(r.af(), 0x01B0);  // DMG -> F:[Z|N|H|C|0|0|0|0], checksum != 0 -> 1011 0000 = 0xB0
    EXPECT_EQ(r.bc(), 0x0013);
    EXPECT_EQ(r.de(), 0x00D8);
    EXPECT_EQ(r.hl(), 0x014D);
    EXPECT_EQ(r.sp, 0xFFFE);
    EXPECT_EQ(r.pc, 0x0100);
}

TEST_F(CpuTest, NopTakesOneCycle) {
    load({0x00});  // NOP
    EXPECT_EQ(cpu.step(), 1);
    EXPECT_EQ(cpu.regs().pc, 0x0101);
}

TEST_F(CpuTest, LdBImmediate) {
    load({0x06, 0x42});  // LD B, 0x42
    EXPECT_EQ(cpu.step(), 2);
    EXPECT_EQ(cpu.regs().b, 0x42);
    EXPECT_EQ(cpu.regs().pc, 0x0102);
}

TEST_F(CpuTest, LdCFromB) {
    load({0x06, 0x42, 0x48});  // LD B, 0x42; LD C, B
    cpu.step();                // LD B, 0x42
    EXPECT_EQ(cpu.step(), 1);
    EXPECT_EQ(cpu.regs().c, 0x42);
}

TEST_F(CpuTest, LdAFromHlIndirect) {
    load({0x7E});  // LD A, (HL)
    cpu.regs().setHl(0xC000);
    mmu.write(0xC000, 0x99);  // WRAM region
    EXPECT_EQ(cpu.step(), 2);
    EXPECT_EQ(cpu.regs().a, 0x99);
}

TEST_F(CpuTest, LdHlIndirectFromA) {
    load({0x77});              // LD (HL), A
    cpu.regs().setHl(0xC000);  // set addr
    cpu.regs().a = 0x5A;
    EXPECT_EQ(cpu.step(), 2);
    EXPECT_EQ(mmu.read(0xC000), 0x5A);
}

TEST_F(CpuTest, LdHlIndirectImmediate) {
    load({0x36, 0x3C});  // LD (HL), 0x3C
    cpu.regs().setHl(0xC000);
    EXPECT_EQ(cpu.step(), 3);
    EXPECT_EQ(mmu.read(0xC000), 0x3C);
    EXPECT_EQ(cpu.regs().pc, 0x0102);
}

TEST_F(CpuTest, LdDoesNotChangeFlags) {
    load({0x06, 0x00});   // LD B, 0x00: a zero must NOT set Z
    cpu.regs().f = 0x00;  // clear all flags so any wrongly set flag shows up
    cpu.step();
    EXPECT_EQ(cpu.regs().f, 0x00);
}

TEST_F(CpuTest, LdBcImmediate16) {
    load({0x01, 0x34, 0x12});  // LD BC, 0x1234 (little-endian immediate)
    EXPECT_EQ(cpu.step(), 3);
    EXPECT_EQ(cpu.regs().bc(), 0x1234);
    EXPECT_EQ(cpu.regs().pc, 0x0103);
}

TEST_F(CpuTest, LdSpImmediate16) {
    load({0x31, 0xFE, 0xCF});  // LD SP, 0xCFFE
    EXPECT_EQ(cpu.step(), 3);
    EXPECT_EQ(cpu.regs().sp, 0xCFFE);
}

TEST_F(CpuTest, LdSpFromHl) {
    load({0xF9});  // LD SP, HL
    cpu.regs().setHl(0xC100);
    EXPECT_EQ(cpu.step(), 2);
    EXPECT_EQ(cpu.regs().sp, 0xC100);
}

TEST_F(CpuTest, LdAbsoluteFromSp) {
    load({0x08, 0x00, 0xC0});  // LD (0xC000), SP
    cpu.regs().sp = 0xBEEF;
    EXPECT_EQ(cpu.step(), 5);
    EXPECT_EQ(mmu.read(0xC000), 0xEF);  // low byte first
    EXPECT_EQ(mmu.read(0xC001), 0xBE);
    EXPECT_EQ(cpu.regs().pc, 0x0103);
}

TEST_F(CpuTest, PushBcWritesHighThenLowBelowSp) {
    load({0xC5});  // PUSH BC
    cpu.regs().sp = 0xD000;
    cpu.regs().setBc(0x1234);
    EXPECT_EQ(cpu.step(), 4);
    EXPECT_EQ(cpu.regs().sp, 0xCFFE);
    EXPECT_EQ(mmu.read(0xCFFF), 0x12);  // high byte
    EXPECT_EQ(mmu.read(0xCFFE), 0x34);  // low byte at new top of stack
}

TEST_F(CpuTest, PopDeReadsLittleEndian) {
    load({0xD1});  // POP DE
    cpu.regs().sp = 0xCFFE;
    mmu.write(0xCFFE, 0x34);
    mmu.write(0xCFFF, 0x12);
    EXPECT_EQ(cpu.step(), 3);
    EXPECT_EQ(cpu.regs().de(), 0x1234);
    EXPECT_EQ(cpu.regs().sp, 0xD000);
}

TEST_F(CpuTest, PushThenPopRestoresValue) {
    load({0xC5, 0xD1});  // PUSH BC; POP DE
    cpu.regs().sp = 0xD000;
    cpu.regs().setBc(0xABCD);
    cpu.step();  // PUSH BC
    cpu.step();  // POP DE
    EXPECT_EQ(cpu.regs().de(), 0xABCD);
    EXPECT_EQ(cpu.regs().sp, 0xD000);
}

TEST_F(CpuTest, PopAfClearsLowNibbleOfF) {
    load({0xF1});  // POP AF
    cpu.regs().sp = 0xCFFE;
    mmu.write(0xCFFE, 0xFF);  // would-be F: low nibble must be dropped
    mmu.write(0xCFFF, 0x12);  // A
    cpu.step();
    EXPECT_EQ(cpu.regs().af(), 0x12F0);
}