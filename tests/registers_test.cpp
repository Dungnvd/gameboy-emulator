#include "core/registers.h"

#include <gtest/gtest.h>

using gb::Flag;
using gb::Registers;

TEST(Registers, BcCombinesBAndC) {
    Registers r;
    r.b = 0x12;
    r.c = 0x34;
    EXPECT_EQ(r.bc(), 0x1234);
}

TEST(Registers, SetHlSplitsIntoHAndL) {
    Registers r;
    r.setHl(0xBEEF);
    EXPECT_EQ(r.h, 0xBE);
    EXPECT_EQ(r.l, 0xEF);
}

TEST(Registers, SetAfClearsLowNibbleOfF) {
    Registers r;
    r.setAf(0x12FF);
    EXPECT_EQ(r.a, 0x12);
    EXPECT_EQ(r.f, 0xF0);  // Low 4 bits of F are always 0
}

TEST(Registers, SetFlagOnlyTouchesThatBit) {
    Registers r;
    r.setFlag(Flag::Z, true);  // F = 1000 0000
    r.setFlag(Flag::C, true);  // F = 1001 0000
    EXPECT_EQ(r.f, 0x90);      // -> 0x90

    r.setFlag(Flag::Z, false);  // F = 0001 0000
    EXPECT_EQ(r.f, 0x10);       // -> 0x10
    EXPECT_TRUE(r.getFlag(Flag::C));
    EXPECT_FALSE(r.getFlag(Flag::Z));
}

TEST(Registers, SetHlOnlyChangesHAndL) {
    Registers r;
    r.a = 0x11;
    r.f = 0x10;  // F only keeps high 4 bits, so avoid values like 0x12

    r.b = 0x22;
    r.c = 0x33;

    r.d = 0x44;
    r.e = 0x55;

    r.h = 0x12;
    r.l = 0x34;

    r.sp = 0x1234;
    r.pc = 0x5678;

    const Registers before = r;

    r.setHl(0xBEEF);

    EXPECT_EQ(r.a, before.a);
    EXPECT_EQ(r.f, before.f);
    EXPECT_EQ(r.b, before.b);
    EXPECT_EQ(r.c, before.c);
    EXPECT_EQ(r.d, before.d);
    EXPECT_EQ(r.e, before.e);
    EXPECT_EQ(r.h, 0xBE);
    EXPECT_EQ(r.l, 0xEF);
    EXPECT_EQ(r.sp, before.sp);
    EXPECT_EQ(r.pc, before.pc);
}