# Game Boy Emulator

![CI](https://github.com/Dungnvd/gameboy-emulator/actions/workflows/ci.yml/badge.svg)

A Game Boy emulator written in C++17

- CPU (SM83): All 501 instructions, interrupts
- 64 KiB memory map, cartridge header parsing
- GoogleTest unit tests, platform-independent core library

## Build and test

```bash
sudo apt install build-essential cmake libsdl2-dev # Mint
cmake -B build
cmake --build build
ctest --test-dir build
```

No ROMs are included in this repo.

### References

- [Pan Docs](https://gbdev.io/pandocs/)
- [Game Boy: Complete Technical Reference](https://gekkio.fi/files/gb-docs/gbctr.pdf)