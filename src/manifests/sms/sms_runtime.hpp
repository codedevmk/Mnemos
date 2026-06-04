#pragma once

// Manifest-path counterpart of `sms_system`/`assemble_sms`.
//
// `assemble_sms` hand-wires an SMS into a value-member struct and stays the
// independent parity oracle. `build_sms_runtime` produces the SAME machine
// through the data-driven path: it picks the right embedded manifest (Sega vs
// Codemasters, NTSC vs PAL), runs build_system with the SMS host callbacks,
// attaches the cartridge, and applies the post-BIOS CPU fixup + default pads.
// The player adapter holds an `sms_runtime` and queries its accessors exactly
// as it used to query `sms_system`.

#include "builder.hpp"       // mnemos::manifests::system_graph
#include "sms_callbacks.hpp" // sms_callbacks_state + z80/sms_vdp/sn76489 + peripheral
#include "sms_system.hpp"    // sms_config

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace mnemos::manifests::sms {

    // A fully wired SMS built through the manifest path. Owns the constructed
    // chips (via system_graph), the host-side glue the chip callbacks close
    // over, and the cartridge image the mapper borrows.
    //
    // Member ORDER is load-bearing. Members destruct in reverse declaration
    // order, so `graph` (declared last) destructs FIRST: the chips stop ticking
    // -- and stop dereferencing `state` / borrowing `rom` -- before the state
    // and ROM they point at are torn down.
    struct sms_runtime final {
        std::vector<std::uint8_t> rom; // cartridge image; mapper borrows a span into it
        sms_callbacks_state state;     // chip pointers + ports + io_ctrl + reset latch
        bool codemasters_active{};
        bool korean_active{};
        bool korean_msx_active{};
        bool korean_hicom_active{};
        bool korean_janggun_active{};
        bool korean_multi_4x8k_active{};
        bool korean_multi_16k_active{};
        chips::storage::eeprom_93c46 eeprom; // 93C46 serial save; inert unless active
        bool eeprom_93c46_active{};          // cart carries the 93C46 EEPROM
        bool eeprom_enabled{};               // $FFFC bit3: $8000 routed to the EEPROM
        system_graph graph;                  // owns chips/buses/memory; MUST destruct first

        [[nodiscard]] chips::cpu::z80* cpu() const noexcept { return state.cpu; }
        [[nodiscard]] chips::video::sms_vdp* vdp() const noexcept { return state.vdp; }
        [[nodiscard]] chips::audio::sn76489* psg() const noexcept { return state.psg; }

        [[nodiscard]] peripheral::device* port_device(int port) noexcept {
            return (port >= 0 && port < 2) ? state.ports[static_cast<std::size_t>(port)].get()
                                           : nullptr;
        }

        void set_reset_button(bool pressed) noexcept { state.reset_pressed = pressed; }

        // Game Gear START button (port $00 bit 7). No-op on a base Master System.
        void set_gg_start(bool pressed) noexcept { state.gg.set_start(pressed); }

        // Cartridge battery store for .srm persistence: the 93C46's 128 bytes when
        // the cart has one, else empty.
        [[nodiscard]] std::span<std::uint8_t> battery_ram() noexcept {
            return eeprom_93c46_active ? eeprom.bytes() : std::span<std::uint8_t>{};
        }
    };

    // Build a runnable SMS from a cartridge image (moved in) via the manifest
    // path. Mapper selection mirrors assemble_sms: forced by config, otherwise
    // auto-detected from the Codemasters checksum header. Equivalent to
    // assemble_sms by construction -- verified byte-for-byte in
    // sms_runtime_parity_test.
    [[nodiscard]] std::unique_ptr<sms_runtime> build_sms_runtime(std::vector<std::uint8_t> rom,
                                                                 const sms_config& config = {});

} // namespace mnemos::manifests::sms
