#pragma once

// A Sega 32X machine: a Genesis (booting the 32X cartridge) plus the
// sega32x_system (two SH-2s + the 32X memory and interrupt sources), joined by
// the 68000-side $A15xxx bridge. Built ADDITIVELY on the unchanged
// assemble_genesis -- a plain Genesis cart never traverses any 32X path, so the
// Genesis byte-parity floor is structurally untouched.

#include "genesis_system.hpp" // genesis_system + genesis_config + assemble_genesis
#include "sega32x_system.hpp"

#include <cstdint>
#include <memory>
#include <vector>

namespace mnemos::manifests::sega32x {

    struct sega32x_machine final {
        std::unique_ptr<genesis::genesis_system> genesis; // main side (the 68000)
        std::unique_ptr<sega32x_system> thirtytwox;       // the 32X board (two SH-2s)

        // SH-2 : 68000 clock ratio. Both derive from the same NTSC master clock --
        // the 68000 runs at master/7, each SH-2 at master*3/7 -- so the SH-2s tick
        // at exactly 3x the 68000. The integer ratio means no fractional carry is
        // needed (unlike the Sega CD sub-CPU's 87.5/53.693175).
        static constexpr std::uint64_t sh2_clock_multiplier = 3U;

        // begin_slice() baselines the SH-2 position against the 68000 at the start
        // of an interleave slice; catch_up_sh2() advances both SH-2s up to the
        // 68000's current cycle * 3. A no-op while the SH-2s are held in reset.
        void begin_slice() noexcept;
        void catch_up_sh2();

      private:
        std::uint64_t slice_base_main_ = 0; // 68000 cycles at the slice baseline
        std::uint64_t slice_base_sh2_ = 0;  // master SH-2 cycles at the slice baseline
    };

    // The three 32X boot ROM images. m_bios/s_bios seed the SH-2s' reset
    // vectors; g_bios overlays the 68000's $000000-$0000FF vectors so the boot
    // sequence runs the adapter's security/handshake code before the cart.
    // Any image may be empty: the matching window then stays on the cart (the
    // unit tests run BIOS-less).
    struct sega32x_bios final {
        std::vector<std::uint8_t> m_bios;
        std::vector<std::uint8_t> s_bios;
        std::vector<std::uint8_t> g_bios;
    };

    // Assemble a 32X machine from its cartridge image. The Genesis boots it as the
    // cartridge; the two SH-2s stay held in reset until the 68000 sets ADEN and
    // releases RES through the adapter-control register at $A15100.
    [[nodiscard]] std::unique_ptr<sega32x_machine>
    assemble_sega32x_machine(std::vector<std::uint8_t> cart, const sega32x_bios& bios = {},
                             const genesis::genesis_config& config = {});

} // namespace mnemos::manifests::sega32x
