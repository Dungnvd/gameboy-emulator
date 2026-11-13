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

TEST_F(CpuTest, LdAFromBcIndirect) {
    load({0x0A});  // LD A, (BC)
    cpu.regs().setBc(0xC123);
    mmu.write(0xC123, 0x77);
    EXPECT_EQ(cpu.step(), 2);
    EXPECT_EQ(cpu.regs().a, 0x77);
}

TEST_F(CpuTest, LdDeIndirectFromA) {
    load({0x12});  // LD (DE), A
    cpu.regs().setDe(0xC456);
    cpu.regs().a = 0x88;
    EXPECT_EQ(cpu.step(), 2);
    EXPECT_EQ(mmu.read(0xC456), 0x88);
}

TEST_F(CpuTest, LdHlIncrementFromA) {
    load({0x22});  // LD (HL+), A
    cpu.regs().setHl(0xC000);
    cpu.regs().a = 0x11;
    EXPECT_EQ(cpu.step(), 2);
    EXPECT_EQ(mmu.read(0xC000), 0x11);
    EXPECT_EQ(cpu.regs().hl(), 0xC001);  // HL incremented after write
}

TEST_F(CpuTest, LdHlIncrementWrapsAround) {
    load({0x22});  // LD (HL+), A
    cpu.regs().setHl(0xFFFF);
    cpu.regs().a = 0x01;
    cpu.step();
    EXPECT_EQ(mmu.read(0xFFFF), 0x01);   // 0xFFFF = Interrupt Enable register
    EXPECT_EQ(cpu.regs().hl(), 0x0000);  // 16-bit wrap: 0xFFFF + 1 = 0x0000
}

TEST_F(CpuTest, LdAFromHlDecrement) {
    load({0x3A});  // LD A, (HL-)
    cpu.regs().setHl(0xC010);
    mmu.write(0xC010, 0x22);
    EXPECT_EQ(cpu.step(), 2);
    EXPECT_EQ(cpu.regs().a, 0x22);
    EXPECT_EQ(cpu.regs().hl(), 0xC00F);  // HL decremented after read
}

TEST_F(CpuTest, LdAFromAbsolute) {
    load({0xFA, 0x34, 0xC2});  // LD A, (0xC234)
    mmu.write(0xC234, 0x5C);
    EXPECT_EQ(cpu.step(), 4);
    EXPECT_EQ(cpu.regs().a, 0x5C);
    EXPECT_EQ(cpu.regs().pc, 0x103);
}

TEST_F(CpuTest, LdAbsoluteFromA) {
    load({0xEA, 0x00, 0xD0});  // LD (0xD000), A
    cpu.regs().a = 0x6D;
    EXPECT_EQ(cpu.step(), 4);
    EXPECT_EQ(mmu.read(0xD000), 0x6D);
    EXPECT_EQ(cpu.regs().pc, 0x103);
}

TEST_F(CpuTest, LdhImmediateFromA) {
    load({0xE0, 0x80});  // LDH (0x80), A -> writes at 0xFF00 + 0x80 = 0xFF80 (HRAM)
    cpu.regs().a = 0x3E;
    EXPECT_EQ(cpu.step(), 3);
    EXPECT_EQ(mmu.read(0xFF80), 0x3E);
    EXPECT_EQ(cpu.regs().pc, 0x0102);
}

TEST_F(CpuTest, LdhAFromImmediate) {
    load({0xF0, 0x90});  // LDH A, (0x90) -> reads 0xFF00 + 0x90 = 0xFF90 (HRAM)
    mmu.write(0xFF90, 0xC7);
    EXPECT_EQ(cpu.step(), 3);
    EXPECT_EQ(cpu.regs().a, 0xC7);
    EXPECT_EQ(cpu.regs().pc, 0x0102);
}

TEST_F(CpuTest, LdhCFromA) {
    load({0xE2});  // LDH (C), A -> writes 0xFF00 + C
    cpu.regs().c = 0xA0;
    cpu.regs().a = 0x2B;
    EXPECT_EQ(cpu.step(), 2);
    EXPECT_EQ(mmu.read(0xFFA0), 0x2B);
    EXPECT_EQ(cpu.regs().pc, 0x0101);
}

TEST_F(CpuTest, LdhAFromC) {
    load({0xF2});  // LDH A, (C) -> reads 0xFF00 + C
    cpu.regs().c = 0x85;
    mmu.write(0xFF85, 0x4F);
    EXPECT_EQ(cpu.step(), 2);
    EXPECT_EQ(cpu.regs().a, 0x4F);
    EXPECT_EQ(cpu.regs().pc, 0x0101);
}

TEST_F(CpuTest, AddAFromB) {
    load({0x80});         // ADD A, B
    cpu.regs().f = 0xF0;  // all flags set
    cpu.regs().a = 0x3C;
    cpu.regs().b = 0x12;
    EXPECT_EQ(cpu.step(), 1);
    EXPECT_EQ(cpu.regs().a, 0x4E);
    EXPECT_EQ(cpu.regs().f, 0x00);  // Z = 0, N = 0, H = 0, C = 0 -> 0000 0000 = 0x00
    EXPECT_EQ(cpu.regs().pc, 0x0101);
}

TEST_F(CpuTest, AddSetsHalfCarry) {
    load({0x80});  // ADD A, B
    cpu.regs().a = 0x0F;
    cpu.regs().b = 0x01;
    cpu.step();
    EXPECT_EQ(cpu.regs().a, 0x10);
    EXPECT_EQ(cpu.regs().f,
              0x20);  // Z = 0, N = 0, H = 1, C = 0 overflows bit 3 -> 0010 0000 = 0x20
}

TEST_F(CpuTest, AddSetsZeroHalfCarryAndCarry) {
    load({0x80});         // ADD A, B
    cpu.regs().f = 0x00;  // clear all flags because post-boot F is 0xB0
    cpu.regs().a = 0x3A;
    cpu.regs().b = 0xC6;
    cpu.step();
    EXPECT_EQ(cpu.regs().a, 0x00);  // 0x3A + 0xC6 = 0x100
    EXPECT_EQ(cpu.regs().f, 0xB0);  // Z = 1, N = 0, H = 1, C = 1 -> 1011 0000 -> 0xB0
}

TEST_F(CpuTest, AddAToItselfSetsCarryWithoutHalfCarry) {
    load({0x87});  // ADD A, A
    cpu.regs().a = 0x80;
    cpu.step();
    EXPECT_EQ(cpu.regs().a, 0x00);
    EXPECT_EQ(cpu.regs().f, 0x90);  // Z = 1, N = 0, H = 0, C = 1 -> 1001 0000 -> 0x90
}

TEST_F(CpuTest, AddAFromHlIndirect) {
    load({0x86});  // ADD A, (HL)
    cpu.regs().setHl(0xC000);
    mmu.write(0xC000, 0x02);
    cpu.regs().a = 0x01;
    EXPECT_EQ(cpu.step(), 2);
    EXPECT_EQ(cpu.regs().a, 0x03);
}

TEST_F(CpuTest, AddAImmediate) {
    load({0xC6, 0x12});  // ADD A, 0x12
    cpu.regs().a = 0x3C;
    EXPECT_EQ(cpu.step(), 2);
    EXPECT_EQ(cpu.regs().a, 0x4E);
    EXPECT_EQ(cpu.regs().pc, 0x0102);
}

TEST_F(CpuTest, SubAFromB) {
    load({0x90});  // SUB A, B
    cpu.regs().a = 0x3E;
    cpu.regs().b = 0x12;
    EXPECT_EQ(cpu.step(), 1);
    EXPECT_EQ(cpu.regs().a, 0x2C);
    EXPECT_EQ(cpu.regs().f, 0x40);  // Z = 0, N = 1, H = 0, C = 0 -> 0100 0000 = 0x40
    EXPECT_EQ(cpu.regs().pc, 0x0101);
}

TEST_F(CpuTest, SubSetsHalfCarry) {
    load({0x90});  // SUB A, B
    cpu.regs().a = 0x10;
    cpu.regs().b = 0x01;
    cpu.step();
    EXPECT_EQ(cpu.regs().a, 0x0F);
    EXPECT_EQ(cpu.regs().f, 0x60);  // Z = 0, N = 1, H = 1, C = 0 -> 0110 0000 = 0x60
}

TEST_F(CpuTest, SubSetsCarryAndWraps) {
    load({0x90});  // SUB A, B
    cpu.regs().a = 0x10;
    cpu.regs().b = 0x20;
    cpu.step();
    EXPECT_EQ(cpu.regs().a, 0xF0);
    EXPECT_EQ(cpu.regs().f, 0x50);  // Z = 0, N = 1, H = 0, C = 1 -> 0101 0000 = 0x50
}

TEST_F(CpuTest, SubAFromItselfSetsZero) {
    load({0x97});  // SUB A, A
    cpu.regs().a = 0x3E;
    cpu.step();
    EXPECT_EQ(cpu.regs().a, 0x00);
    EXPECT_EQ(cpu.regs().f,
              0xC0);  // Z = 1, N = 1, H = 0, C = 0 (equal: no borrow) -> 1100 0000 = 0xC0
}

TEST_F(CpuTest, SubAImmediate) {
    load({0xD6, 0x01});  // SUB A, 0x01
    cpu.regs().a = 0x05;
    EXPECT_EQ(cpu.step(), 2);
    EXPECT_EQ(cpu.regs().a, 0x04);
    EXPECT_EQ(cpu.regs().pc, 0x0102);
}

TEST_F(CpuTest, CpEqualSetsZeroAndKeepsA) {
    load({0xB8});  // CP A, B
    cpu.regs().a = 0x42;
    cpu.regs().b = 0x42;
    EXPECT_EQ(cpu.step(), 1);
    EXPECT_EQ(cpu.regs().a, 0x42);  // A unchanged: CP discards result
    EXPECT_EQ(cpu.regs().f, 0xC0);  // Z = 1, N = 1, H = 0, C = 0 -> 1100 0000 = 0xC0
}

TEST_F(CpuTest, CpImmediateLessSetsCarry) {
    load({0xFE, 0x20});  // CP A, 0x20
    cpu.regs().a = 0x10;
    EXPECT_EQ(cpu.step(), 2);
    EXPECT_EQ(cpu.regs().a, 0x10);  // A unchanged
    EXPECT_EQ(cpu.regs().f, 0x50);  // Z = 0, N = 1, H = 0, C = 1 -> 0101 0000 = 0x50
    EXPECT_EQ(cpu.regs().pc, 0x102);
}

TEST_F(CpuTest, AndAFromB) {
    load({0xA0});         // AND A, B
    cpu.regs().f = 0xD0;  // 1101 0000 : Z N C set, H clear, AND must clear Z N C and set H
    cpu.regs().a = 0x3C;
    cpu.regs().b = 0x0F;
    EXPECT_EQ(cpu.step(), 1);
    EXPECT_EQ(cpu.regs().a, 0x0C);
    EXPECT_EQ(cpu.regs().f,
              0x20);  // Z = 0, N = 0, H = 1, C = 0 -> 0010 0000 = 0x20, AND always sets H = 1
    EXPECT_EQ(cpu.regs().pc, 0x0101);
}

TEST_F(CpuTest, AndImmediateSetsZero) {
    load({0xE6, 0x0F});  // AND A, 0x0F
    cpu.regs().a = 0xF0;
    EXPECT_EQ(cpu.step(), 2);
    EXPECT_EQ(cpu.regs().a, 0x00);
    EXPECT_EQ(cpu.regs().f, 0xA0);  // Z = 1, N = 0, H = 1, C = 0 -> 1010 0000 = 0xA0
    EXPECT_EQ(cpu.regs().pc, 0x0102);
}

TEST_F(CpuTest, XorAWithItselfClearsA) {
    load({0xAF});         // XOR A, A
    cpu.regs().f = 0x70;  // 0111 0000: N H C set, XOR must clear them
    cpu.regs().a = 0x5A;
    EXPECT_EQ(cpu.step(), 1);
    EXPECT_EQ(cpu.regs().a, 0x00);  // x ^ x = 0
    EXPECT_EQ(cpu.regs().f, 0x80);  // Z = 1, N = 0, H = 0, C = 0 -> 1000 0000 = 0x80
}

TEST_F(CpuTest, XorImmediateFlipsBits) {
    load({0xEE, 0xFF});   // XOR A, 0xFF
    cpu.regs().f = 0xF0;  // 1111 0000
    cpu.regs().a = 0x0F;  // 0000 1111
    EXPECT_EQ(cpu.step(), 2);
    EXPECT_EQ(cpu.regs().a, 0xF0);  // 1111 0000, XOR with 1s flips every bit
    EXPECT_EQ(cpu.regs().f, 0x00);  // Z = 0, N = 0, H = 0, C = 0
}

TEST_F(CpuTest, OrAFromB) {
    load({0xB0});         // OR A, B
    cpu.regs().f = 0xF0;  // all flags set
    cpu.regs().a = 0x5A;
    cpu.regs().b = 0x0F;
    EXPECT_EQ(cpu.step(), 1);
    EXPECT_EQ(cpu.regs().a, 0x5F);  // 0101 1010 | 0000 1111 = 0101 1111
    EXPECT_EQ(cpu.regs().f, 0x00);  // Z = 0, N = 0, H = 0, C = 0
}

TEST_F(CpuTest, OrImmediateSetsZeroOnlyIfBothZero) {
    load({0xF6, 0x00});   // OR A, 0x00
    cpu.regs().f = 0x70;  // 0111 0000: N H C set
    cpu.regs().a = 0x00;
    EXPECT_EQ(cpu.step(), 2);
    EXPECT_EQ(cpu.regs().a, 0x00);
    EXPECT_EQ(cpu.regs().f, 0x80);  // Z = 1, N = 0, H = 0, C = 0 -> 1000 0000 = 0x80
    EXPECT_EQ(cpu.regs().pc, 0x0102);
}

TEST_F(CpuTest, AdcAddsCarryIn) {
    load({0x88});         // ADC A, B
    cpu.regs().f = 0x10;  // 0001 0000, C = 1: carry in
    cpu.regs().a = 0x3C;
    cpu.regs().b = 0x12;
    EXPECT_EQ(cpu.step(), 1);
    EXPECT_EQ(cpu.regs().a, 0x4F);  // 0x3C + 0x12 + 1 (ADD would give 0x4E)
    EXPECT_EQ(cpu.regs().f, 0x00);  // Z = 0, N = 0, H = 0, C = 0 (0x4F < 0xFF)
    EXPECT_EQ(cpu.regs().pc, 0x0101);
}

TEST_F(CpuTest, AdcWithoutCarryInActsLikeAdd) {
    load({0x88});         // ADC A, B
    cpu.regs().f = 0x00;  // C = 0: nothing extra to add
    cpu.regs().a = 0x3C;
    cpu.regs().b = 0x12;
    cpu.step();
    EXPECT_EQ(cpu.regs().a, 0x4E);  // same as ADD
}

TEST_F(CpuTest, AdcCarryInOverflowsFromFF) {
    load({0xCE, 0xFF});   // ADC A, 0xFF
    cpu.regs().f = 0x10;  // 0001 0000, C = 1
    cpu.regs().a = 0x00;
    EXPECT_EQ(cpu.step(), 2);
    EXPECT_EQ(cpu.regs().a, 0x00);  // 0x00 + 0xFF + 1 = 0x100
    EXPECT_EQ(cpu.regs().f, 0xB0);  // Z = 1, N = 0, H = 1, C = 1 -> 1011 0000 = 0xB0
    EXPECT_EQ(cpu.regs().pc, 0x0102);
}

TEST_F(CpuTest, SbcSubtractsCarryIn) {
    load({0x98});         // SBC A, B
    cpu.regs().f = 0x10;  // 0001 0000, C = 1 : borrow in
    cpu.regs().a = 0x3E;
    cpu.regs().b = 0x12;
    EXPECT_EQ(cpu.step(), 1);
    EXPECT_EQ(cpu.regs().a, 0x2B);  // 0x3E - 0x12 - 1 (SUB would give 0x2C)
    EXPECT_EQ(cpu.regs().f, 0x40);  // Z = 0, N = 1, H = 0 ( E > 2 + 1), C = 0 -> 0100 0000 = 0x40
}

TEST_F(CpuTest, SbcWithoutCarryInActsLikeSub) {
    load({0x98});         // SBC A, B
    cpu.regs().f = 0x00;  // C = 0: nothing extra to subtract
    cpu.regs().a = 0x3E;
    cpu.regs().b = 0x12;
    cpu.step();
    EXPECT_EQ(cpu.regs().a, 0x2C);  // same as SUB
}

TEST_F(CpuTest, SbcCarryInBorrowsFromZero) {
    load({0xDE, 0xFF});   // SBC A, 0xFF
    cpu.regs().f = 0x10;  // 0001 0000, C = 1
    cpu.regs().a = 0x00;
    EXPECT_EQ(cpu.step(), 2);
    EXPECT_EQ(cpu.regs().a,
              0x00);  // 0x00 - 0xFF - 1 = -0x100, wraps to 0x00 (-0x100 + 0x100 (256))
    EXPECT_EQ(cpu.regs().f, 0xF0);  // Z = 1, N = 1, H =1 (0 < F + 1), C = 1 -> 1111 0000 = 0xF0
    EXPECT_EQ(cpu.regs().pc, 0x0102);
}

TEST_F(CpuTest, IncBClearsZnhAndKeepsCarry) {
    load({0x04});         // INC B
    cpu.regs().f = 0xF0;  // 1111 0000: all flags set
    cpu.regs().b = 0x41;
    EXPECT_EQ(cpu.step(), 1);
    EXPECT_EQ(cpu.regs().b, 0x42);
    EXPECT_EQ(cpu.regs().f, 0x10);  // Z = 0, N = 0, H = 0, C = 1 (unchanged) -> 0001 0000 = 0x10
    EXPECT_EQ(cpu.regs().pc, 0x0101);
}

TEST_F(CpuTest, IncCSetsHalfCarry) {
    load({0x0C});  // INC C
    cpu.regs().f = 0x00;
    cpu.regs().c = 0x0F;
    cpu.step();
    EXPECT_EQ(cpu.regs().c, 0x10);
    EXPECT_EQ(cpu.regs().f, 0x20);  // Z = 0, N = 0, H = 1, C = 0 -> 0010 0000 = 0x20;
}

TEST_F(CpuTest, IncAWrapsToZeroWithoutCarry) {
    load({0x3C});         // INC A
    cpu.regs().f = 0x00;  // C = 0 must stay 0 even through 0xFF + 1 overflows
    cpu.regs().a = 0xFF;
    cpu.step();
    EXPECT_EQ(cpu.regs().a, 0x00);
    EXPECT_EQ(cpu.regs().f,
              0xA0);  // Z = 1, N = 0, H = 1, C = 0 (INC never sets C) -> 1010 0000 = 0xA0;
}

TEST_F(CpuTest, IncHlIndirect) {
    load({0x34});  // INC (HL)
    cpu.regs().setHl(0xC000);
    mmu.write(0xC000, 0x7F);
    EXPECT_EQ(cpu.step(), 3);  // fetch opcode + read (HL) + write (HL)
    EXPECT_EQ(mmu.read(0xC000), 0x80);
}

TEST_F(CpuTest, DecSetsSubtractAndKeepsCarry) {
    load({0x05});         // DEC B
    cpu.regs().f = 0xB0;  // 1011 0000: Z H C set, N clear
    cpu.regs().b = 0x42;
    EXPECT_EQ(cpu.step(), 1);
    EXPECT_EQ(cpu.regs().b, 0x41);
    EXPECT_EQ(cpu.regs().f, 0x50);  // Z = 0, N = 1, H = 0, C = 1 (unchanged) -> 0101 0000 = 0x50
    EXPECT_EQ(cpu.regs().pc, 0x0101);
}

TEST_F(CpuTest, DecEToZeroSetsZero) {
    load({0x1D});  // DEC E
    cpu.regs().f = 0x00;
    cpu.regs().e = 0x01;
    cpu.step();
    EXPECT_EQ(cpu.regs().e, 0x00);
    EXPECT_EQ(cpu.regs().f, 0xC0);  // Z = 1, N = 1, H = 0. C = 0 -> 1100 0000 = 0xC0
}

TEST_F(CpuTest, DecAWrapsSetsHalfCarryWithoutCarry) {
    load({0x3D});  // DEC A
    cpu.regs().f = 0x00;
    cpu.regs().a = 0x00;
    cpu.step();
    EXPECT_EQ(cpu.regs().a, 0xFF);  // 0x00 - 1 wraps to 0xFF
    EXPECT_EQ(cpu.regs().f,
              0x60);  // Z = 0, N = 1, H = 1, C = 0 (DEC never sets C) -> 0110 0000 = 0x60
}

TEST_F(CpuTest, DecHlIndirect) {
    load({0x35});  // DEC (HL)
    cpu.regs().setHl(0xC000);
    mmu.write(0xC000, 0x10);
    EXPECT_EQ(cpu.step(), 3);  // fetch opcode + read (HL) + write (HL)
    EXPECT_EQ(mmu.read(0xC000), 0x0F);
}

TEST_F(CpuTest, IncBcCarriesIntoHighByteWithoutFlags) {
    load({0x03});         // INC BC
    cpu.regs().f = 0xF0;  // INC rr must not touch any flag
    cpu.regs().setBc(0x12FF);
    EXPECT_EQ(cpu.step(), 2);
    EXPECT_EQ(cpu.regs().bc(), 0x1300);  // low byte FF + 1 carries into B
    EXPECT_EQ(cpu.regs().f, 0xF0);       // unchanged
    EXPECT_EQ(cpu.regs().pc, 0x0101);
}

TEST_F(CpuTest, IncSpWrapsAround) {
    load({0x33});  // INC SP
    cpu.regs().sp = 0xFFFF;
    cpu.step();
    EXPECT_EQ(cpu.regs().sp, 0x0000);  // 16-bit wrap: 0xFFFF + 1 = 0x0000
}

TEST_F(CpuTest, DecDeBorrowsFromHighByteWithoutFlags) {
    load({0x1B});         // DEC DE
    cpu.regs().f = 0x00;  // DEC rr must not set any flags
    cpu.regs().setDe(0x1300);
    EXPECT_EQ(cpu.step(), 2);
    EXPECT_EQ(cpu.regs().de(), 0x12FF);  // low byte 00 - 1 borrows from D
    EXPECT_EQ(cpu.regs().f, 0x00);       // unchanged
    EXPECT_EQ(cpu.regs().pc, 0x0101);
}

TEST_F(CpuTest, DecHlWrapsAround) {
    load({0x2B});  // DEC HL
    cpu.regs().setHl(0x0000);
    cpu.step();
    EXPECT_EQ(cpu.regs().hl(), 0xFFFF);  // 16-bit wrap
}

TEST_F(CpuTest, AddHlBcKeepsZeroClearsNhc) {
    load({0x09});         // ADD HL, BC
    cpu.regs().f = 0xF0;  // 1111 0000 -> Z must stay 1 (defined by hardware). N H C must be cleared
    cpu.regs().setHl(0x1000);
    cpu.regs().setBc(0x0234);
    EXPECT_EQ(cpu.step(), 2);
    EXPECT_EQ(cpu.regs().hl(), 0x1234);
    EXPECT_EQ(cpu.regs().f, 0x80);  // Z = 1 (unchanged), N = 0, H = 0, C = 0 -> 1000 0000 = 0x80
    EXPECT_EQ(cpu.regs().pc, 0x0101);
}

TEST_F(CpuTest, AddHlSetsHalfCarryFromBit11) {
    load({0x19});  // ADD HL, DE
    cpu.regs().f = 0x00;
    cpu.regs().setHl(0x0F00);
    cpu.regs().setDe(0x0100);
    cpu.step();
    EXPECT_EQ(cpu.regs().hl(), 0x1000);
    EXPECT_EQ(cpu.regs().f, 0x20);  // Z = 0, N = 0, H = 1, C = 0 -> 0010 0000 = 0x20
}

TEST_F(CpuTest, AddHlToItselfSetsCarryButNotZero) {
    load({0x29});         // ADD HL, HL
    cpu.regs().f = 0x00;  // Z = 0, must stay 0 even though result = 0
    cpu.regs().setHl(0x8000);
    cpu.step();
    EXPECT_EQ(cpu.regs().hl(), 0x0000);  // 0x8000 + 0x8000 = 0x10000
    EXPECT_EQ(cpu.regs().f, 0x10);  // Z = 0 (unchanged), N = 0, H = 0, C = 1 -> 0001 0000 = 0x10
}

TEST_F(CpuTest, AddHlSpSetsHalfCarryAndCarry) {
    load({0x39});  // ADD HL, SP
    cpu.regs().f = 0x00;
    cpu.regs().setHl(0x0001);
    cpu.regs().sp = 0xFFFF;
    cpu.step();
    EXPECT_EQ(cpu.regs().hl(), 0x0000);  // 0x0001 + 0xFFFF = 0x10000
    EXPECT_EQ(cpu.regs().f, 0x30);  // Z = 0 (unchanged), N = 0, H = 1, C = 1 -> 0011 0000 = 0x30
}

TEST_F(CpuTest, AddSpPositiveOffset) {
    load({0xE8, 0x05});   // ADD SP, 5
    cpu.regs().f = 0xF0;  // 1111 0000: Z and N must be clear
    cpu.regs().sp = 0xD000;
    EXPECT_EQ(cpu.step(), 4);  // fetch opcode + fetch byte e8 + low byte + high byte
    EXPECT_EQ(cpu.regs().sp, 0xD005);
    EXPECT_EQ(cpu.regs().f, 0x00);  // Z = 0, N = 0, H = 0, C = 0
    EXPECT_EQ(cpu.regs().pc, 0x0102);
}

TEST_F(CpuTest, AddSpNegativeOffset) {
    load({0xE8, 0xFE});  // ADD SP, -2 (0xFE = -2 in two's complement)
    cpu.regs().f = 0x00;
    cpu.regs().sp = 0xD000;
    cpu.step();
    EXPECT_EQ(cpu.regs().sp, 0xCFFE);
    EXPECT_EQ(cpu.regs().f, 0x00);
}

TEST_F(CpuTest, AddSpNegativeOffsetStillSetsCarry) {
    load({0xE8, 0xFF});  // ADD SP, -1
    cpu.regs().f = 0x00;
    cpu.regs().sp = 0x00FF;
    cpu.step();
    EXPECT_EQ(cpu.regs().sp, 0x00FE);
    EXPECT_EQ(cpu.regs().f, 0x30);  // Z = 0, N = 0, H = 1 (F + F), C = 1 (FF + FF)
}

TEST_F(CpuTest, AddSpResultZeroButZStaysClear) {
    load({0xE8, 0x08});  // ADD SP, 8
    cpu.regs().f = 0x00;
    cpu.regs().sp = 0xFFF8;
    cpu.step();
    EXPECT_EQ(cpu.regs().sp, 0x0000);
    EXPECT_EQ(cpu.regs().f, 0x30);  // Z = 0 (always), N = 0, H = 1, C = 1
}

TEST_F(CpuTest, LdHlSpPlusOffset) {
    load({0xF8, 0x02});   // LD HL, SP + 2
    cpu.regs().f = 0xF0;  // set all flags
    cpu.regs().sp = 0xD000;
    cpu.regs().setHl(0x1234);  // old HL must be overwritten, not added to
    EXPECT_EQ(cpu.step(), 3);  // opcode + fetch e8 + write in HL
    EXPECT_EQ(cpu.regs().hl(), 0xD002);
    EXPECT_EQ(cpu.regs().sp, 0xD000);  // unchanged
    EXPECT_EQ(cpu.regs().f, 0x00);
    EXPECT_EQ(cpu.regs().pc, 0x0102);
}

TEST_F(CpuTest, LdHlSpMinusOffset) {
    load({0xF8, 0x80});  // LD HL, SP - 128
    cpu.regs().f = 0x00;
    cpu.regs().sp = 0xD080;
    cpu.step();
    EXPECT_EQ(cpu.regs().hl(), 0xD000);
    EXPECT_EQ(cpu.regs().f, 0x10);  // Z = 0, N = 0, H = 0, C = 1 (80 + 80 = 0x100)
}

TEST_F(CpuTest, RlcaMovesBit7IntoCarryAndBit0) {
    load({0x07});         // RLCA
    cpu.regs().f = 0xE0;  // 1110 0000: Z N H set, C clear
    cpu.regs().a = 0x85;  // 1000 0101
    EXPECT_EQ(cpu.step(), 1);
    EXPECT_EQ(cpu.regs().a, 0x0B);  // 0000 1011: bit 7 wrapped around to bit 0
    EXPECT_EQ(cpu.regs().f, 0x10);  // Z = 0, N = 0, H = 0, C = 1 (old bit 7)
    EXPECT_EQ(cpu.regs().pc, 0x0101);
}

TEST_F(CpuTest, RlcaZeroResultKeepsZClear) {
    load({0x07});         // RLCA
    cpu.regs().f = 0x90;  // 1001 0000: Z and C set
    cpu.regs().a = 0x00;
    cpu.step();
    EXPECT_EQ(cpu.regs().a, 0x00);
    EXPECT_EQ(cpu.regs().f, 0x00);  // Z = 0 (always), C = 0 (old bit 7)
}

TEST_F(CpuTest, RrcaMovesBit0IntoCarryAndBit7) {
    load({0x0F});  // RRCA
    cpu.regs().f = 0x00;
    cpu.regs().a = 0x01;
    cpu.step();
    EXPECT_EQ(cpu.regs().a, 0x80);  // 1000 0000: bit 0 wrapped around to bit 7
    EXPECT_EQ(cpu.regs().f, 0x10);  // C = 1 (old bit 0)
}

TEST_F(CpuTest, RrcaClearsCarryWhenBit0Clear) {
    load({0x0F});         // RRCA
    cpu.regs().f = 0x10;  // C set
    cpu.regs().a = 0x02;  // 0000 0010
    cpu.step();
    EXPECT_EQ(cpu.regs().a, 0x01);  // 0000 0001
    EXPECT_EQ(cpu.regs().f, 0x00);  // C = 0 (old bit 0)
}

TEST_F(CpuTest, RlaShiftsOldCarryIntoBit0) {
    load({0x17});         // RLA
    cpu.regs().f = 0x10;  // C set
    cpu.regs().a = 0x40;  // 0100 0000
    cpu.step();
    EXPECT_EQ(cpu.regs().a, 0x81);  // 1000 0001: old C went into bit 0 (RLCA would give 0x80)
    EXPECT_EQ(cpu.regs().f, 0x00);  // C = 0 (old bit 7)
}

TEST_F(CpuTest, RlaMovesBit7IntoCarry) {
    load({0x17});         // RLA
    cpu.regs().f = 0x00;  // C = 0
    cpu.regs().a = 0x80;  // 1000 0000
    cpu.step();
    EXPECT_EQ(cpu.regs().a, 0x00);  // bit 7 left A (RLCA would give 0x01)
    EXPECT_EQ(cpu.regs().f, 0x10);  // Z = 0 (always), C = 1 (old bit 7)
}

TEST_F(CpuTest, RraShiftsOldCarryIntoBit7) {
    load({0x1F});         // RRA
    cpu.regs().f = 0x10;  // C = 1
    cpu.regs().a = 0x02;  // 0000 0010
    cpu.step();
    EXPECT_EQ(cpu.regs().a, 0x81);  // 1000 0001: old C went into bit 7 (RRCA would give 0x01)
    EXPECT_EQ(cpu.regs().f, 0x00);  // C = 0 (old bit 0)
}

TEST_F(CpuTest, RraMovesBit0IntoCarry) {
    load({0x1F});         // RRA
    cpu.regs().f = 0x00;  // C = 0
    cpu.regs().a = 0x03;  // 0000 0011
    cpu.step();
    EXPECT_EQ(cpu.regs().a, 0x01);  // 0000 0001 (RRCA would give 0x81)
    EXPECT_EQ(cpu.regs().f, 0x10);  // C = 1 (old bit 0)
}

TEST_F(CpuTest, CplFlipsAKeepsZAndCSet) {
    load({0x2F});         // CPL
    cpu.regs().f = 0x90;  // 1001 0000: Z C set, N H clear
    cpu.regs().a = 0x35;  // 0011 0101
    cpu.step();
    EXPECT_EQ(cpu.regs().a, 0xCA);  // 1100 1010: flipped
    EXPECT_EQ(cpu.regs().f,
              0xF0);  // 1111 0000: Z and C unchanged while N H always 1 (hardware defined)
}

TEST_F(CpuTest, CplKeepsZAndCClear) {
    load({0x2F});  // CPL
    cpu.regs().f = 0x00;
    cpu.regs().a = 0xFF;
    cpu.step();
    EXPECT_EQ(cpu.regs().a, 0x00);
    EXPECT_EQ(cpu.regs().f, 0x60);  // 0110 0000
}

TEST_F(CpuTest, ScfSetsCarryClearsNh) {
    load({0x37});         // SCF
    cpu.regs().f = 0x60;  // 0110 0000: N H set, Z C clear
    cpu.step();
    EXPECT_EQ(cpu.regs().f, 0x10);  // Z = 0 (unchanged), N = 0, H = 0, C = 1
}

TEST_F(CpuTest, ScfKeepsZSet) {
    load({0x37});         // SCF
    cpu.regs().f = 0x80;  // 1000 0000: Z set
    cpu.step();
    EXPECT_EQ(cpu.regs().f, 0x90);  // Z = 1 (unchanged), N = 0, H = 0, C = 1
}

TEST_F(CpuTest, CcfFlipsCarryToZero) {
    load({0x3F});         // CCF
    cpu.regs().f = 0x70;  // 0111 0000: Z clear, N H C set
    cpu.step();
    EXPECT_EQ(cpu.regs().f, 0x00);  // Z = 0 (unchanged), N = 0, H = 0, C = 0 (flipped)
}

TEST_F(CpuTest, CcfFlipsCarryToOneKeepsZ) {
    load({0x3F});         // CCF
    cpu.regs().f = 0x80;  // 1000 0000: Z set, N H C clear
    cpu.step();
    EXPECT_EQ(cpu.regs().f, 0x90);  // Z = 1 (unchanged), N = 0, H = 0, C = 1 (flipped)
}

TEST_F(CpuTest, DaaAfterAddFixesLowDigit) {
    load({0x27});         // DAA
    cpu.regs().f = 0x80;  // 1000 0000
    cpu.regs().a = 0x3C;  // 0x15 + 0x27, after ADD (N = 0, H = 0, C = 0), Z set (wrong, it should
                          // be 0) so DAA must recompute it
    EXPECT_EQ(cpu.step(), 1);
    EXPECT_EQ(cpu.regs().a, 0x42);  // low nibble > 9 -> 0x3C + 0x06 = 0x42 -> BCD 15 + 27 = 42
    EXPECT_EQ(cpu.regs().f, 0x00);  // Z = 0 (recomputed), N = 0 (unchanged), H = 0 (always) , C = 0
    EXPECT_EQ(cpu.regs().pc, 0x0101);
}

TEST_F(CpuTest, DaaAfterAddUsesHalfCarry) {
    load({0x27});         // DAA
    cpu.regs().f = 0x20;  // 0010 0000, after ADD with H = 1: 0x09 + 0x08 = 0x11
    cpu.regs().a = 0x11;
    cpu.step();
    EXPECT_EQ(cpu.regs().a, 0x17);  // H = 1 -> 0x11 + 0x06 = 0x17 -> BCD 9 + 8 = 17;
    EXPECT_EQ(cpu.regs().f, 0x00);  // H cleared (always)
}

TEST_F(CpuTest, DaaAfterAddFixesHighDigitAndSetsCarry) {
    load({0x27});         // DAA
    cpu.regs().f = 0x00;  // after ADD: 0x90 + 0x20 = 0xB0, no carry
    cpu.regs().a = 0xB0;  // A > 0x99: high digit B is not a decimal digit
    cpu.step();
    EXPECT_EQ(cpu.regs().a, 0x10);  // A > 0x99 -> 0xB0 + 0x60 = 0x110 -> 0x10
    EXPECT_EQ(cpu.regs().f, 0x10);  // 0001 0000: C = 1 -> BCD 90 + 20 = 110
}

TEST_F(CpuTest, DaaAfterAddUsesCarry) {
    load({0x27});         // DAA
    cpu.regs().f = 0x10;  // After ADD with C = 1: 0x90 + 0x90 = 0x120, overflowed
    cpu.regs().a = 0x20;
    cpu.step();
    EXPECT_EQ(cpu.regs().a, 0x80);  // C = 1 -> 0x20 + 0x60 = 0x80-> BCD 90 + 90 = 180
    EXPECT_EQ(cpu.regs().f, 0x10);  // C stays 1
}

TEST_F(CpuTest, DaaAfterAddWrapsToZero) {
    load({0x27});         // DAA
    cpu.regs().f = 0x00;  // After ADD: 0x45 + 0x55 = 0x9A
    cpu.regs().a = 0x9A;  // both fixes apply: A > 0x99 and digit A > 9
    cpu.step();
    EXPECT_EQ(cpu.regs().a, 0x00);  // 0x9A + 0x66 = 0x100 -> 0x00
    EXPECT_EQ(cpu.regs().f, 0x90);  // Z = 1, C = 1: BCD 45 + 55 = 100
}

TEST_F(CpuTest, DaaAfterSubUsesHalfCarry) {
    load({0x27});         // DAA
    cpu.regs().f = 0x60;  // 0110 0000: After SUB with N = 1, H = 1: 0x42 - 0x15 = 0x2D
    cpu.regs().a = 0x2D;
    cpu.step();
    EXPECT_EQ(cpu.regs().a, 0x27);  // H = 1 -> 0x2D - 0x06 -> BCD 42 - 15 = 27
    EXPECT_EQ(cpu.regs().f, 0x40);  // N = 1 (unchanged), H cleared
}

TEST_F(CpuTest, DaaAfterSubUsesCarry) {
    load({0x27});         // DAA
    cpu.regs().f = 0x50;  // After SUB with N = 1, C = 1: 0x10 - 0x20 = 0xF0
    cpu.regs().a = 0xF0;
    cpu.step();
    EXPECT_EQ(cpu.regs().a, 0x90);  // C = 1 -> 0xF0 - 0x60 = 0x90 -> BCD 10 - 20 = 90 with a borrow
    EXPECT_EQ(cpu.regs().f, 0x50);  // 0101 0000, N = 1 and C = 1 (DAA never clears C)
}

TEST_F(CpuTest, DaaAfterSubDoesNotCheckDigitRange) {
    load({0x27});         // DAA
    cpu.regs().f = 0x40;  // 0100 0000: N = 1, H = 0, C = 0
    cpu.regs().a = 0x0A;
    cpu.step();
    EXPECT_EQ(cpu.regs().a, 0x0A);  // DAA in SUB only looks at H and C, not A's digits
    EXPECT_EQ(cpu.regs().f, 0x40);
}

TEST_F(CpuTest, BcdAddition19Plus28) {
    load({0x3E, 0x19, 0xC6, 0x28, 0x27});  // LD A, 0x19; ADD A, 0x28; DAA
    cpu.step();                            // LD A, 0x19
    cpu.step();                            // ADD A, 0x28 -> 0x41, H = 1
    cpu.step();                            // DAA
    EXPECT_EQ(cpu.regs().a, 0x47);         // BCD 19 + 28 = 47
}

TEST_F(CpuTest, JpAbsolute) {
    load({0xC3, 0x50, 0x01});  // JP 0x0150 (little-endian: low byte first)
    EXPECT_EQ(cpu.step(),
              4);  // fetch opcode + read low byte + read high byte + load address into PC
    EXPECT_EQ(cpu.regs().pc, 0x0150);
}

TEST_F(CpuTest, JpHl) {
    load({0xE9});  // JP HL
    cpu.regs().setHl(0xC000);
    EXPECT_EQ(cpu.step(), 1);  // no memory read: PC is copied from HL
    EXPECT_EQ(cpu.regs().pc, 0xC000);
}

TEST_F(CpuTest, JpNzNotTakenWhenZSet) {
    load({0xC2, 0x50, 0x01});  // JP NZ, 0x0150
    cpu.regs().f = 0x80;       // 1000 0000: Z = 1 -> NZ false -> Not Jump
    EXPECT_EQ(cpu.step(), 3);
    EXPECT_EQ(cpu.regs().pc, 0x0103);
}

TEST_F(CpuTest, JpZNotTakenWhenZClear) {
    load({0xCA, 0x50, 0x01});  // JP Z, 0x0150
    cpu.regs().f = 0x10;       // 0001 0000: Z = 0 -> Z false -> Not Jump
    EXPECT_EQ(cpu.step(), 3);
    EXPECT_EQ(cpu.regs().pc, 0x0103);
}

TEST_F(CpuTest, JpNcNotTakenWhenCSet) {
    load({0xD2, 0x50, 0x01});  // JP NC, 0x0150
    cpu.regs().f = 0x10;       // 0001 0000: C = 1 -> NC false -> Not Jump
    EXPECT_EQ(cpu.step(), 3);
    EXPECT_EQ(cpu.regs().pc, 0x0103);
}

TEST_F(CpuTest, JpCNotTakenWhenCClear) {
    load({0xDA, 0x50, 0x01});  // JP C, 0x0150
    cpu.regs().f = 0x80;       // 1000 0000: C = 0 -> C false -> Not jump
    EXPECT_EQ(cpu.step(), 3);
    EXPECT_EQ(cpu.regs().pc, 0x0103);
}

TEST_F(CpuTest, JpCTakenWhenCSet) {
    load({0xDA, 0x50, 0x01});  // JP C, 0x0150
    cpu.regs().f = 0x10;       // 0001 0000: C = 1 -> C true -> jump
    EXPECT_EQ(cpu.step(), 4);
    EXPECT_EQ(cpu.regs().pc, 0x0150);
}

TEST_F(CpuTest, JrForward) {
    load({0x18, 0x05});                // JR +5
    EXPECT_EQ(cpu.step(), 3);          // fetch opcode + read e8 + add e8 to PC
    EXPECT_EQ(cpu.regs().pc, 0x0107);  // counted from next instruction: 0x0102 + 5
}

TEST_F(CpuTest, JrMinus2JumpsToItself) {
    load({0x18, 0xFE});  // JR -2 (0xFE = -2): classic infinite loop
    cpu.step();
    EXPECT_EQ(cpu.regs().pc, 0x0100);  // 0x0102 - 2 = back to JR opcode itself
}

TEST_F(CpuTest, JrNzTakenWhenZClear) {
    load({0x20, 0x05});   // JR NZ, +5
    cpu.regs().f = 0x10;  // 0001 0000: Z = 0 -> NZ true -> Jump
    EXPECT_EQ(cpu.step(), 3);
    EXPECT_EQ(cpu.regs().pc, 0x0107);
}

TEST_F(CpuTest, JrZTakenWhenZSet) {
    load({0x28, 0x05});   // JR Z, +5
    cpu.regs().f = 0x80;  // 1000 0000: Z = 1 -> Z true -> jump
    EXPECT_EQ(cpu.step(), 3);
    EXPECT_EQ(cpu.regs().pc, 0x0107);
}

TEST_F(CpuTest, JrNcTakenWhenCClear) {
    load({0x30, 0x05});   // JR NC, +5
    cpu.regs().f = 0x80;  // 1000 0000: C = 0 -> NC true -> jump
    EXPECT_EQ(cpu.step(), 3);
    EXPECT_EQ(cpu.regs().pc, 0x0107);
}

TEST_F(CpuTest, JrCTakenWhenCSet) {
    load({0x38, 0x05});   // JR C, +5
    cpu.regs().f = 0x10;  // C = 1 -> jump
    EXPECT_EQ(cpu.step(), 3);
    EXPECT_EQ(cpu.regs().pc, 0x0107);
}

TEST_F(CpuTest, JrCNotTakenWhenCClear) {
    load({0x38, 0x05});   // JR C, +5
    cpu.regs().f = 0x00;  // C = 0 -> not jump
    EXPECT_EQ(cpu.step(), 2);
    EXPECT_EQ(cpu.regs().pc, 0x0102);  // not taken: still skips e8 byte
}

TEST_F(CpuTest, CountdownLoopWithDecAndJrNz) {
    load({0x06, 0x03, 0x05, 0x20, 0xFD});
    // 0x0100: {0x06, 0x03}:    LD B, 3         (2 M-cycles)
    // 0x0102: {0x05}:          DEC B           (1 M-cycle)
    // 0x0103: {0x20, 0xFD}:    JR NZ, -3      (3 taken / 2 not taken), 0x0105 - 3 = 0x0102
    // 0x0105:                  exit
    int cycles = 0;
    for (int i = 0; i < 7; i++) {  // LD, then (DEC, JR) x 3
        cycles += cpu.step();
    }

    EXPECT_EQ(cpu.regs().b, 0x00);
    EXPECT_EQ(cpu.regs().pc, 0x0105);  // last JR NZ not taken because DEC set Z = 1 (B = 0)
    EXPECT_EQ(cycles, 13);             // 2 + (1 + 3) + (1 + 3) + (1 + 2)
}
