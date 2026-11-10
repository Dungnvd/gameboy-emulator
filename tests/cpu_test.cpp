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
    EXPECT_EQ(cpu.regs().f, 0x40);  // Z = 0, N = 1, H = 0 ( E > 2 + 1), C = 0 -> 0100 000 = 0x40
}

TEST_F(CpuTest, SubWithoutCarryInActsLikeSub) {
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
    EXPECT_EQ(cpu.regs().f, 0x10);  // Z = 0, N = 0, H = 0, C = 1 (unchanged) -> 0001 0000 = 0x01
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
    EXPECT_EQ(cpu.regs().f, 0xC0);  // Z = 1, N = 1, H = 0. C = 0 -> 1100 0000 = 0x50
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
