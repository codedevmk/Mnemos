#pragma once

#include "beeper.hpp"            // chips::audio::beeper (1-bit speaker)
#include "bus.hpp"               // topology::bus
#include "region.hpp"            // mnemos::video_region
#include "spectrum_snapshot.hpp" // spectrum_snapshot
#include "ssg.hpp"               // chips::audio::ssg (AY-3-8910, 128K)
#include "state.hpp"             // chips::state_writer / state_reader
#include "ula.hpp"               // chips::video::ula
#include "z80.hpp"               // chips::cpu::z80

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace mnemos::manifests::spectrum {

    enum class spectrum_model : std::uint8_t { k48, k128 };

    struct spectrum_config final {
        // The Spectrum is a 50 Hz machine; PAL selects the 50 Hz frame cadence in
        // the host timing tables.
        mnemos::video_region video_region{mnemos::video_region::pal};
        // 48K vs 128K. If left at k48 but a 32 KiB ROM is supplied, assemble picks
        // 128K automatically (the 128.rom carries both 16 KiB ROM halves).
        spectrum_model model{spectrum_model::k48};
    };

    // Sinclair ZX Spectrum (48K or 128K): a Z80 + ULA (+ a 1-bit beeper). Memory is
    // modelled as the 128K layout -- eight 16 KiB RAM banks + up to two 16 KiB ROM
    // halves -- of which the 48K maps a fixed subset (ROM half 0, RAM banks 5/2/0).
    // The 128K pages $0000 (ROM half) and $C000 (RAM bank) through the $7FFD latch
    // and can show the shadow screen (bank 7). Heap-allocated and never moved -- the
    // bus holds spans into the member arrays and the port handlers capture `this`.
    struct spectrum_system final {
        chips::cpu::z80 cpu;
        chips::video::ula ula;
        chips::audio::beeper beeper;
        chips::audio::ssg ay; // AY-3-8910, 128K only (ports $FFFD/$BFFD)
        topology::bus bus{16U, topology::endianness::little};

        // Two 16 KiB ROM halves: [0] = 48K BASIC / 128K editor, [1] = 128K 48-BASIC.
        std::array<std::uint8_t, 0x8000> rom{};
        // Eight 16 KiB RAM banks (128 KiB). The 48K uses banks 5 ($4000), 2 ($8000)
        // and 0 ($C000); the display is bank 5 (or bank 7 = the 128K shadow screen).
        std::array<std::array<std::uint8_t, 0x4000>, 8> ram_bank{};

        spectrum_model model{spectrum_model::k48};

        // Keyboard half-rows read through port $FE: active-low, low 5 bits used.
        // 0xFF = all keys released (the power-on state).
        std::array<std::uint8_t, 8> keyboard_rows{};
        bool ear_input{}; // tape EAR line, sampled in bit 6 of a port-$FE read

        // Kempston joystick, read at port $1F: active-HIGH bits b0=right, b1=left,
        // b2=down, b3=up, b4=fire (the de-facto joystick standard many games read).
        std::uint8_t kempston{};

        // 128K paging latch ($7FFD): bits 0-2 = RAM bank at $C000, bit 3 = screen
        // (0=bank5, 1=bank7 shadow), bit 4 = ROM half, bit 5 = lock paging.
        std::uint8_t port_7ffd{};
        bool paging_locked{};

        // Press/release a key at (half-row 0..7, bit 0..4). Active-low: a held key
        // clears its bit.
        void set_key(int row, int bit, bool pressed) noexcept;

        // Apply a $7FFD write: re-page $0000 (ROM half), $C000 (RAM bank) and the
        // display bank. A no-op once paging is locked. 128K only.
        void set_paging(std::uint8_t value) noexcept;

        // Load a snapshot's registers, RAM and border into the running machine.
        void apply_snapshot(const spectrum_snapshot& snap) noexcept;

        // Non-chip latch state (keyboard + EAR + paging). The chips, ROM and RAM
        // are serialized separately by a machine-save path.
        void save_state(chips::state_writer& writer) const;
        void load_state(chips::state_reader& reader);
    };

    // Build a runnable Spectrum. `rom` is the system ROM: 16 KiB for the 48K, or 32
    // KiB (both halves) for the 128K. The model follows config.model, upgraded to
    // 128K automatically when a >=32 KiB ROM is supplied.
    [[nodiscard]] std::unique_ptr<spectrum_system>
    assemble_spectrum(std::span<const std::uint8_t> rom, const spectrum_config& config = {});

} // namespace mnemos::manifests::spectrum
