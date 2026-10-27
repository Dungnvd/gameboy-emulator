#include "core/registers.h"

namespace gb {
std::uint16_t Registers::af() const { return static_cast<std::uint16_t>((a << 8) | f); }
std::uint16_t Registers::bc() const { return static_cast<std::uint16_t>((b << 8) | c); }
std::uint16_t Registers::de() const { return static_cast<std::uint16_t>((d << 8) | e); }
std::uint16_t Registers::hl() const { return static_cast<std::uint16_t>((h << 8) | l); }

void Registers::setAf(std::uint16_t value) {
    a = static_cast<std::uint8_t>(value >> 8);
    f = static_cast<std::uint8_t>(value &
                                  0xF0);  // mask: 0000 000 1111 0000, low 4 bits of F are always 0
}
void Registers::setBc(std::uint16_t value) {
    b = static_cast<std::uint8_t>(value >> 8);
    c = static_cast<std::uint8_t>(value & 0xFF);  // mask: 0000 0000 1111 1111
}

void Registers::setDe(std::uint16_t value) {
    d = static_cast<std::uint8_t>(value >> 8);
    c = static_cast<std::uint8_t>(value & 0xFF);
}

void Registers::setHl(std::uint16_t value) {
    h = static_cast<std::uint8_t>(value >> 8);
    l = static_cast<std::uint8_t>(value & 0xFF);
}

bool Registers::getFlag(Flag which) const {
    const auto mask = static_cast<std::uint8_t>(which);
    return (f & mask) != 0;
}
void Registers::setFlag(Flag which, bool on) {
    const auto mask = static_cast<std::uint8_t>(which);
    if (on) {
        f = static_cast<std::uint8_t>(f | mask);
    } else {
        f = static_cast<std::uint8_t>(f & ~mask);
    }
}

}  // namespace gb