#pragma once

// Host-side glue for the SMS manifests (sms.ntsc.toml / sms.pal.toml).
// Builds the named callback and MMIO-factory tables the manifests
// reference, capturing closures over a caller-owned `sms_callbacks_state`
// that survives the system's lifetime.
//
// Why this shape: build_system constructs the chips internally; the host
// can't capture pointers to them in closures BEFORE the build runs. We
// resolve the chicken-and-egg by capturing `&state` (a stable address)
// in every closure, then populating state.cpu / state.vdp / state.psg /
// state.mapper from the constructed system_graph after build_system
// returns. The closures are installed during configure() but not invoked
// until the chip starts ticking, so the gap is benign.
//
// The corresponding manifest names (matching the [chip.config]/[[mmio_block]]
// strings in sms.ntsc.toml / sms.pal.toml):
//   sms.z80_port_in              uint8_t(uint16_t)
//   sms.z80_port_out             void(uint16_t, uint8_t)
//   sms.vdp_irq                  void(bool)
//   sms.mapper_register_overlay  mmio_factory at $FFFC-$FFFF

#include "callbacks.hpp"
#include "mmio_factory.hpp"
#include "peripheral.hpp"

#include "sms_mapper.hpp"
#include "sms_vdp.hpp"
#include "sn76489.hpp"
#include "z80.hpp"

#include <array>
#include <cstdint>
#include <memory>

namespace mnemos::manifests::sms {

    // Host state the SMS manifest's callbacks + mmio_factories close over.
    // Outlives every closure that captures it; chip pointers are filled in
    // by the caller after build_system constructs the chips.
    struct sms_callbacks_state final {
        // Constructed by build_system; the caller populates these by
        // downcasting system_graph::chip("cpu"|"video"|"audio"|"mapper").
        chips::cpu::z80* cpu{};
        chips::video::sms_vdp* vdp{};
        chips::audio::sn76489* psg{};
        chips::mapper::sms_mapper* mapper{};

        // SMS-specific state that doesn't live on a chip: the two
        // controller-port peripherals (default-attached to MK-3020 pads
        // by the caller), the I/O-control latch at $3F, and the reset
        // button bit visible in $DD bit 4.
        std::array<std::unique_ptr<peripheral::device>, 2> ports{};
        std::uint8_t io_ctrl{0xFFU};
        bool reset_pressed{};
    };

    struct sms_host_tables final {
        chips::callback_table callbacks;
        chips::mmio_factory_table mmio_factories;
    };

    // Build the host tables the SMS manifest references. `state` is captured
    // by pointer in every closure and MUST outlive the returned tables and
    // any system built with them.
    [[nodiscard]] sms_host_tables make_sms_host_tables(sms_callbacks_state& state);

} // namespace mnemos::manifests::sms
