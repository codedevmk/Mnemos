#include "genesis_callbacks.hpp"

#include "chip.hpp" // chips::reset_kind

#include <functional>

namespace mnemos::manifests::genesis {

    namespace {

        // Active-low controller byte from the plugged peripheral, or 0x7F when
        // the socket is empty -- matches genesis_system::read_pad_port.
        std::uint8_t read_pad_port(const genesis_callbacks_state& s, int port) noexcept {
            if (port < 0 || port >= 2) {
                return 0xFFU;
            }
            const auto& dev = s.ports[static_cast<std::size_t>(port)];
            return dev ? dev->read_data() : 0x7FU;
        }

        // mmio_factory whose handlers ignore (base,size) and decode the full bus
        // address directly, exactly as the assemble_genesis map_mmio lambdas did.
        using factory = chips::mmio_factory_fn;

    } // namespace

    genesis_host_tables make_genesis_host_tables(genesis_callbacks_state& state) {
        genesis_host_tables out;
        auto* s = &state;

        // ---- Callbacks ------------------------------------------------------

        // 68K IACK clears the VDP V-int latch (many V-blank handlers rely on
        // this rather than acking via the status read).
        out.callbacks.emplace("genesis.irq_ack",
                              chips::callback_value{std::function<void(int)>{
                                  [s](int level) { s->vdp->acknowledge_irq(level); }}});

        // Genesis bus controller drops the TAS write phase on a memory operand.
        out.callbacks.emplace("genesis.tas_drop",
                              chips::callback_value{std::function<void(std::uint32_t)>{
                                  [](std::uint32_t /*addr*/) {}}});

        // 68K -> VDP DMA word source: read a big-endian word off the main bus.
        out.callbacks.emplace("genesis.dma_read",
                              chips::callback_value{std::function<std::uint16_t(std::uint32_t)>{
                                  [s](std::uint32_t addr) -> std::uint16_t {
                                      const auto hi = s->main_bus->read8(addr & 0xFFFFFFU);
                                      const auto lo = s->main_bus->read8((addr + 1U) & 0xFFFFFFU);
                                      return static_cast<std::uint16_t>(
                                          (static_cast<std::uint16_t>(hi) << 8U) | lo);
                                  }}});

        // VDP /INT -> 68K IRQ level.
        out.callbacks.emplace("genesis.vdp_irq",
                              chips::callback_value{std::function<void(int)>{
                                  [s](int level) { s->cpu->set_irq_level(level); }}});

        // One-instruction-delayed IRQ for the V-int-enable-via-MOVE.W path.
        out.callbacks.emplace("genesis.vdp_delayed_irq",
                              chips::callback_value{std::function<void(int)>{
                                  [s](int level) { s->cpu->schedule_delayed_irq(level); }}});

        // V-blank edge: pulses the Z80 IRQ, advances the frame counter, and
        // fires per-device on_vblank() hooks (6-button pad phase counter, ...).
        out.callbacks.emplace("genesis.vblank",
                              chips::callback_value{std::function<void(bool)>{[s](bool in_vblank) {
                                  s->z80->set_irq_line(in_vblank);
                                  if (in_vblank) {
                                      ++s->frame_index;
                                      for (auto& p : s->ports) {
                                          if (p) {
                                              p->on_vblank();
                                          }
                                      }
                                  }
                              }}});

        // ---- Predicates (chip gates) ---------------------------------------

        // Z80 ticks only while the 68K has released RESET and not asserted BUSREQ.
        out.predicates.emplace("genesis.z80_running", [s]() -> bool { return s->z80_running; });

        // 68K stalls while the VDP holds the bus for a DMA burst.
        out.predicates.emplace("genesis.cpu_runnable",
                               [s]() -> bool { return !s->vdp->dma_stall_active(); });

        // ---- MMIO factories: main bus --------------------------------------

        // $A00000-$A03FFF: Z80 RAM (8 KiB, mirrored). Shared buffer with the
        // z80-bus $0000 window -- both factories close over the same s->z80_ram.
        out.mmio_factories.emplace(
            "genesis.z80_ram_main",
            factory{[s](std::uint32_t, std::uint32_t) -> chips::mmio_handlers {
                return {.on_read = [s](std::uint32_t a) { return s->z80_ram[a & 0x1FFFU]; },
                        .on_write = [s](std::uint32_t a,
                                        std::uint8_t v) { s->z80_ram[a & 0x1FFFU] = v; }};
            }});

        // $A04000-$A05FFF: YM2612 (addr/data x 2, mirrored).
        out.mmio_factories.emplace(
            "genesis.ym2612_main",
            factory{[s](std::uint32_t, std::uint32_t) -> chips::mmio_handlers {
                return {.on_read = [s](std::uint32_t) { return s->fm->read_status(); },
                        .on_write =
                            [s](std::uint32_t a, std::uint8_t v) {
                                s->fm->write(static_cast<int>((a >> 1U) & 1U), (a & 1U) != 0U, v);
                            }};
            }});

        // $A10000-$A1001F: I/O sub-controller (version RO, pad data/ctrl, serial).
        out.mmio_factories.emplace(
            "genesis.io_controller",
            factory{[s](std::uint32_t, std::uint32_t) -> chips::mmio_handlers {
                return {.on_read = [s](std::uint32_t a) -> std::uint8_t {
                            const std::uint32_t off = a & 0x1FU;
                            switch (off) {
                            case 0x01:
                                return s->version_register;
                            case 0x03:
                                return read_pad_port(*s, 0);
                            case 0x05:
                                return read_pad_port(*s, 1);
                            default:
                                return s->io_regs[off];
                            }
                        },
                        .on_write =
                            [s](std::uint32_t a, std::uint8_t v) {
                                const std::uint32_t off = a & 0x1FU;
                                if (off == 0x03 && s->ports[0]) {
                                    s->ports[0]->write_data(v);
                                } else if (off == 0x05 && s->ports[1]) {
                                    s->ports[1]->write_data(v);
                                }
                                if (off != 0x01) {
                                    s->io_regs[off] = v;
                                }
                            }};
            }});

        // $A11100-$A11101: Z80 BUSREQ. Bit 0 reads 1 while the Z80 holds its bus.
        out.mmio_factories.emplace(
            "genesis.z80_busreq",
            factory{[s](std::uint32_t, std::uint32_t) -> chips::mmio_handlers {
                return {.on_read = [s](std::uint32_t a) -> std::uint8_t {
                            return (a & 1U) == 0U
                                       ? static_cast<std::uint8_t>(s->z80_running ? 0x01U : 0x00U)
                                       : 0x00U;
                        },
                        .on_write =
                            [s](std::uint32_t a, std::uint8_t v) {
                                if ((a & 1U) == 0U) {
                                    s->z80_bus_requested = (v & 0x01U) != 0U;
                                    s->z80_running = s->z80_reset_released && !s->z80_bus_requested;
                                }
                            }};
            }});

        // $A11200-$A11201: Z80 RESET. Falling edge (1->0) resets the Z80.
        out.mmio_factories.emplace(
            "genesis.z80_reset", factory{[s](std::uint32_t, std::uint32_t) -> chips::mmio_handlers {
                return {.on_read = [](std::uint32_t) -> std::uint8_t { return 0x00U; },
                        .on_write =
                            [s](std::uint32_t a, std::uint8_t v) {
                                if ((a & 1U) != 0U) {
                                    return;
                                }
                                const bool released = (v & 0x01U) != 0U;
                                if (!released && s->z80_reset_released) {
                                    s->z80->reset(chips::reset_kind::power_on);
                                }
                                s->z80_reset_released = released;
                                s->z80_running = s->z80_reset_released && !s->z80_bus_requested;
                            }};
            }});

        // $C00000-$C0001F: VDP data/control/HV ports + PSG at $C00011. 68K word
        // accesses split into even-high + odd-low bytes; the data-port cache
        // keeps that split atomic, status/HV reads bypass it.
        out.mmio_factories.emplace(
            "genesis.vdp_ports", factory{[s](std::uint32_t, std::uint32_t) -> chips::mmio_handlers {
                return {
                    .on_read = [s](std::uint32_t a) -> std::uint8_t {
                        const std::uint32_t offset = a & 0x1FU;
                        const bool is_data_port = (offset & 0x1CU) == 0x00U;
                        if ((offset & 1U) == 0U) {
                            const std::uint16_t word = s->vdp->read16(offset);
                            if (is_data_port) {
                                s->vdp_read_low = static_cast<std::uint8_t>(word);
                            }
                            return static_cast<std::uint8_t>(word >> 8U);
                        }
                        if (is_data_port) {
                            return s->vdp_read_low;
                        }
                        return static_cast<std::uint8_t>(s->vdp->read16(offset & ~1U));
                    },
                    .on_write =
                        [s](std::uint32_t a, std::uint8_t v) {
                            const std::uint32_t offset = a & 0x1FU;
                            if (offset == 0x11U) {
                                s->psg->write(v);
                                return;
                            }
                            if ((offset & 1U) == 0U) {
                                s->vdp_write_high = v;
                            } else {
                                s->vdp->write16(
                                    offset & ~1U,
                                    static_cast<std::uint16_t>(
                                        (static_cast<std::uint16_t>(s->vdp_write_high) << 8U) | v));
                            }
                        }};
            }});

        // ---- MMIO factories: Z80 bus ---------------------------------------

        // $0000-$3FFF: Z80 RAM (same buffer as $A00000 on the main bus).
        out.mmio_factories.emplace(
            "genesis.z80_ram_z80",
            factory{[s](std::uint32_t, std::uint32_t) -> chips::mmio_handlers {
                return {.on_read = [s](std::uint32_t a) { return s->z80_ram[a & 0x1FFFU]; },
                        .on_write = [s](std::uint32_t a,
                                        std::uint8_t v) { s->z80_ram[a & 0x1FFFU] = v; }};
            }});

        // $4000-$5FFF: YM2612 (shared with the 68K's $A04000).
        out.mmio_factories.emplace(
            "genesis.ym2612_z80",
            factory{[s](std::uint32_t, std::uint32_t) -> chips::mmio_handlers {
                return {.on_read = [s](std::uint32_t) { return s->fm->read_status(); },
                        .on_write =
                            [s](std::uint32_t a, std::uint8_t v) {
                                s->fm->write(static_cast<int>((a >> 1U) & 1U), (a & 1U) != 0U, v);
                            }};
            }});

        // $6000-$60FF: bank register -- shifts bit 0 into bit 8 of the 9-bit
        // window base addressing Z80 $8000-$FFFF.
        out.mmio_factories.emplace(
            "genesis.z80_bank", factory{[s](std::uint32_t, std::uint32_t) -> chips::mmio_handlers {
                return {.on_read = [](std::uint32_t) -> std::uint8_t { return 0xFFU; },
                        .on_write =
                            [s](std::uint32_t, std::uint8_t v) {
                                s->z80_bank = static_cast<std::uint16_t>(
                                    ((s->z80_bank >> 1U) | ((v & 1U) << 8U)) & 0x1FFU);
                            }};
            }});

        // $7F00-$7FFF: SN76489 PSG at $7F11 (shared with the 68K's $C00011).
        out.mmio_factories.emplace(
            "genesis.psg_z80", factory{[s](std::uint32_t, std::uint32_t) -> chips::mmio_handlers {
                return {.on_read = [](std::uint32_t) -> std::uint8_t { return 0xFFU; },
                        .on_write =
                            [s](std::uint32_t a, std::uint8_t v) {
                                if ((a & 0xFFU) == 0x11U) {
                                    s->psg->write(v);
                                }
                            }};
            }});

        // $8000-$FFFF: banked 32 KiB window into 68K address space.
        out.mmio_factories.emplace(
            "genesis.z80_window",
            factory{[s](std::uint32_t, std::uint32_t) -> chips::mmio_handlers {
                return {.on_read = [s](std::uint32_t a) -> std::uint8_t {
                            const std::uint32_t addr =
                                ((static_cast<std::uint32_t>(s->z80_bank) << 15U) | (a & 0x7FFFU)) &
                                0xFFFFFFU;
                            return s->main_bus->read8(addr);
                        },
                        .on_write =
                            [s](std::uint32_t a, std::uint8_t v) {
                                const std::uint32_t addr =
                                    ((static_cast<std::uint32_t>(s->z80_bank) << 15U) |
                                     (a & 0x7FFFU)) &
                                    0xFFFFFFU;
                                s->main_bus->write8(addr, v);
                            }};
            }});

        return out;
    }

} // namespace mnemos::manifests::genesis
