#include "sms_callbacks.hpp"

#include <utility>

namespace mnemos::manifests::sms {

    namespace {

        // ----- I/O control + pad multiplex helpers ------------------------
        // The $3F register layout matches sms_system.cpp's hand-written
        // version; lifted intact so behavioural parity is by construction
        // rather than re-derivation.

        bool tr_is_input(const sms_callbacks_state& s, int port) noexcept {
            return (s.io_ctrl & (1U << static_cast<unsigned>(port * 2))) != 0U;
        }
        bool th_is_input(const sms_callbacks_state& s, int port) noexcept {
            return (s.io_ctrl & (1U << static_cast<unsigned>(port * 2 + 1))) != 0U;
        }
        std::uint8_t tr_output(const sms_callbacks_state& s, int port) noexcept {
            return static_cast<std::uint8_t>((s.io_ctrl >> static_cast<unsigned>(4 + port * 2)) &
                                             1U);
        }
        std::uint8_t th_output(const sms_callbacks_state& s, int port) noexcept {
            return static_cast<std::uint8_t>((s.io_ctrl >> static_cast<unsigned>(5 + port * 2)) &
                                             1U);
        }

        std::uint8_t port_input(const sms_callbacks_state& s, int port) noexcept {
            const auto& dev = s.ports[static_cast<std::size_t>(port)];
            return dev ? dev->read_data() : 0xFFU;
        }

        std::uint8_t read_pad_dc(const sms_callbacks_state& s) noexcept {
            const std::uint8_t p1 = port_input(s, 0);
            const std::uint8_t p2 = port_input(s, 1);
            std::uint8_t v = static_cast<std::uint8_t>(p1 & 0x1FU); // U/D/L/R/B1
            const std::uint8_t b2 =
                tr_is_input(s, 0) ? static_cast<std::uint8_t>((p1 >> 5U) & 1U) : tr_output(s, 0);
            v |= static_cast<std::uint8_t>(b2 << 5U);
            v |= static_cast<std::uint8_t>(((p2 >> 0U) & 1U) << 6U); // P2 Up
            v |= static_cast<std::uint8_t>(((p2 >> 1U) & 1U) << 7U); // P2 Down
            return v;
        }

        std::uint8_t read_pad_dd(const sms_callbacks_state& s) noexcept {
            const std::uint8_t p2 = port_input(s, 1);
            std::uint8_t v = 0U;
            v |= static_cast<std::uint8_t>(((p2 >> 2U) & 1U) << 0U); // Left
            v |= static_cast<std::uint8_t>(((p2 >> 3U) & 1U) << 1U); // Right
            v |= static_cast<std::uint8_t>(((p2 >> 4U) & 1U) << 2U); // Button 1
            const std::uint8_t b2 =
                tr_is_input(s, 1) ? static_cast<std::uint8_t>((p2 >> 5U) & 1U) : tr_output(s, 1);
            v |= static_cast<std::uint8_t>(b2 << 3U);
            v |= static_cast<std::uint8_t>((s.reset_pressed ? 0U : 1U) << 4U);
            v |= static_cast<std::uint8_t>(1U << 5U);
            v |= static_cast<std::uint8_t>((th_is_input(s, 0) ? 1U : th_output(s, 0)) << 6U);
            v |= static_cast<std::uint8_t>((th_is_input(s, 1) ? 1U : th_output(s, 1)) << 7U);
            return v;
        }

    } // namespace

    sms_host_tables make_sms_host_tables(sms_callbacks_state& state) {
        sms_host_tables out;

        // Z80 IN handler. Decodes the SMS I/O map exactly as the hand-written
        // assemble_sms version did.
        out.callbacks.emplace(
            "sms.z80_port_in",
            chips::callback_value{std::function<std::uint8_t(std::uint16_t)>{
                [s = &state](std::uint16_t port) -> std::uint8_t {
                    const auto p = static_cast<std::uint8_t>(port & 0xFFU);
                    if (s->gg.enabled() && p <= 0x06U) {
                        return s->gg.read(p); // Game Gear handset ($00 mode + EXT link)
                    }
                    if (p <= 0x3FU) {
                        return 0xFFU; // open bus
                    }
                    if (p <= 0x7FU) {
                        return (p & 1U) != 0U ? s->vdp->hcounter() : s->vdp->vcounter();
                    }
                    if (p <= 0xBFU) {
                        return (p & 1U) != 0U ? s->vdp->ctrl_read() : s->vdp->data_read();
                    }
                    return (p & 1U) != 0U ? read_pad_dd(*s) : read_pad_dc(*s);
                }}});

        // Z80 OUT handler.
        out.callbacks.emplace(
            "sms.z80_port_out",
            chips::callback_value{std::function<void(std::uint16_t, std::uint8_t)>{
                [s = &state](std::uint16_t port, std::uint8_t value) {
                    const auto p = static_cast<std::uint8_t>(port & 0xFFU);
                    if (s->gg.enabled() && p <= 0x06U) {
                        s->gg.write(p, value, *s->psg); // GG EXT link + $06 PSG stereo
                        return;
                    }
                    if (p <= 0x3FU) {
                        if ((p & 1U) != 0U) {
                            s->io_ctrl = value; // $3F I/O control
                        }
                        return;
                    }
                    if (p <= 0x7FU) {
                        s->psg->write(value);
                        return;
                    }
                    if (p <= 0xBFU) {
                        if ((p & 1U) != 0U) {
                            s->vdp->ctrl_write(value);
                        } else {
                            s->vdp->data_write(value);
                        }
                        return;
                    }
                    // $C0-$FF: no writable registers on a base SMS.
                }}});

        // VDP /INT -> Z80 IRQ bridge. Same shape as the hand-written
        // s->vdp.set_irq_callback([s](bool){ s->cpu.set_irq_line(...); }).
        out.callbacks.emplace("sms.vdp_irq", chips::callback_value{std::function<void(bool)>{
                                                 [s = &state](bool /*asserted*/) {
                                                     s->cpu->set_irq_line(s->vdp->irq_asserted());
                                                 }}});

        // Sega-mapper register overlay at $FFFC-$FFFF. Bus writes here route
        // to mapper.write_register(); reads return open-bus.
        //
        // NOTE: real hardware ALSO leaves the byte in work RAM beneath
        // (games read $FFFC-$FFFF back as ordinary RAM). The current
        // build_system bus model gives priority to the higher-priority
        // mmio_block over the RAM region; there's no "fall-through after
        // handler" semantic. For B.1 smoke we accept this behavioral gap
        // and verify whether the SMS BIOS smoke test trips on it; if so,
        // a follow-up adds the RAM passthrough either via a new bus mode
        // or a host-pointer-into-RAM dual write.
        out.mmio_factories.emplace(
            "sms.mapper_register_overlay",
            [s = &state](std::uint32_t /*base*/, std::uint32_t /*size*/) -> chips::mmio_handlers {
                return {
                    .on_read = [](std::uint32_t /*address*/) -> std::uint8_t { return 0xFFU; },
                    .on_write =
                        [s](std::uint32_t address, std::uint8_t value) {
                            s->mapper->write_register(static_cast<std::uint16_t>(address), value);
                        },
                };
            });

        // HiCom 188-in-1 register overlay at $FFFF. Same shape as the Sega
        // overlay: writes drive the page register (cpu_write only latches $FFFF),
        // reads return open bus.
        out.mmio_factories.emplace(
            "sms.hicom_register_overlay",
            [s = &state](std::uint32_t /*base*/, std::uint32_t /*size*/) -> chips::mmio_handlers {
                return {
                    .on_read = [](std::uint32_t /*address*/) -> std::uint8_t { return 0xFFU; },
                    .on_write =
                        [s](std::uint32_t address, std::uint8_t value) {
                            s->hicom->cpu_write(static_cast<std::uint16_t>(address), value);
                        },
                };
            });

        // Janggun register overlay at $FFFE-$FFFF. Same shape: writes drive the
        // 16 KiB pair selects (cpu_write decodes $FFFE/$FFFF), reads return open bus.
        out.mmio_factories.emplace(
            "sms.janggun_register_overlay",
            [s = &state](std::uint32_t /*base*/, std::uint32_t /*size*/) -> chips::mmio_handlers {
                return {
                    .on_read = [](std::uint32_t /*address*/) -> std::uint8_t { return 0xFFU; },
                    .on_write =
                        [s](std::uint32_t address, std::uint8_t value) {
                            s->janggun->cpu_write(static_cast<std::uint16_t>(address), value);
                        },
                };
            });

        return out;
    }

} // namespace mnemos::manifests::sms
