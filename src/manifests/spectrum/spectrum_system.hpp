#pragma once

#include "bus.hpp"    // topology::bus
#include "region.hpp" // mnemos::video_region
#include "state.hpp"  // chips::state_writer / state_reader
#include "ula.hpp"    // chips::video::ula
#include "z80.hpp"    // chips::cpu::z80

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace mnemos::manifests::spectrum {

    struct spectrum_config final {
        // The Spectrum is a 50 Hz machine; PAL selects the 50 Hz frame cadence in
        // the host timing tables.
        mnemos::video_region video_region{mnemos::video_region::pal};
    };

    // Sinclair ZX Spectrum 48K: a Z80 + ULA, a 16 KiB system ROM at $0000 and 48
    // KiB RAM at $4000 (the display lives at $4000-$5AFF). Heap-allocated and never
    // moved -- the bus holds spans into the member arrays, the ULA borrows the
    // screen RAM, and the port handlers capture `this`.
    struct spectrum_system final {
        chips::cpu::z80 cpu;
        chips::video::ula ula;
        topology::bus bus{16U, topology::endianness::little};

        std::array<std::uint8_t, 0x4000> rom{}; // 16 KiB system ROM ($0000-$3FFF)
        std::array<std::uint8_t, 0xC000> ram{}; // 48 KiB RAM ($4000-$FFFF)

        // Keyboard half-rows read through port $FE: active-low, low 5 bits used.
        // 0xFF = all keys released (the power-on state).
        std::array<std::uint8_t, 8> keyboard_rows{};
        bool ear_input{}; // tape EAR line, sampled in bit 6 of a port-$FE read

        // Press/release a key at (half-row 0..7, bit 0..4). Active-low: a held key
        // clears its bit.
        void set_key(int row, int bit, bool pressed) noexcept;

        // Non-chip latch state (keyboard + EAR). The chips, ROM and RAM are
        // serialized separately by a machine-save path.
        void save_state(chips::state_writer& writer) const;
        void load_state(chips::state_reader& reader);
    };

    // Build a runnable 48K Spectrum. `rom` is the 16 KiB system ROM (a smaller
    // image is zero-padded, a larger one truncated to 16 KiB).
    [[nodiscard]] std::unique_ptr<spectrum_system>
    assemble_spectrum(std::span<const std::uint8_t> rom, const spectrum_config& config = {});

} // namespace mnemos::manifests::spectrum
