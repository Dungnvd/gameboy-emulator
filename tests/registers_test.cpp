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

TEST(Registers, SetHlDoesNotTouchOtherRegisters) {
    Registers r;
    r.b = 0x11;
    r.c = 0x22;
    r.setHl(0xBEEF);
    EXPECT_EQ(r.b, 0x11);
    EXPECT_EQ(r.c, 0x22);
}
