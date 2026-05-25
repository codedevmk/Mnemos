#include "sms_system.hpp"

#include <span>
#include <utility>

namespace mnemos::manifests::sms {

    namespace {
        // Active-low pin level: a pressed button drives its pin to 0.
        constexpr std::uint8_t pin(bool pressed) noexcept { return pressed ? 0U : 1U; }

        // The I/O control latch ($3F) selects, per controller port, whether the TR/TH
        // pins are inputs (read the controller) or outputs (read back the driven
        // level). Bits: 0/1 = port-0 TR/TH direction, 2/3 = port-1, 4/5 = port-0
        // TR/TH output level, 6/7 = port-1.
        bool tr_is_input(const sms_system* s, int port) noexcept {
            return (s->io_ctrl & (1U << static_cast<unsigned>(port * 2))) != 0U;
        }
        bool th_is_input(const sms_system* s, int port) noexcept {
            return (s->io_ctrl & (1U << static_cast<unsigned>(port * 2 + 1))) != 0U;
        }
        std::uint8_t tr_output(const sms_system* s, int port) noexcept {
            return static_cast<std::uint8_t>((s->io_ctrl >> static_cast<unsigned>(4 + port * 2)) &
                                             1U);
        }
        std::uint8_t th_output(const sms_system* s, int port) noexcept {
            return static_cast<std::uint8_t>((s->io_ctrl >> static_cast<unsigned>(5 + port * 2)) &
                                             1U);
        }

        unsigned bit(std::uint8_t value, unsigned shift) noexcept {
            return static_cast<unsigned>(value) << shift;
        }

        // Port $DC: P1 directions + buttons, P1 TR (button 2 or driven level), P2 up/down.
        std::uint8_t read_pad_dc(const sms_system* s) noexcept {
            const std::uint8_t p1 = s->pad[0];
            const std::uint8_t p2 = s->pad[1];
            unsigned v =
                bit(pin((p1 & pad_button::up) != 0U), 0) |
                bit(pin((p1 & pad_button::down) != 0U), 1) |
                bit(pin((p1 & pad_button::left) != 0U), 2) |
                bit(pin((p1 & pad_button::right) != 0U), 3) |
                bit(pin((p1 & pad_button::button_1) != 0U), 4) |
                bit(tr_is_input(s, 0) ? pin((p1 & pad_button::button_2) != 0U) : tr_output(s, 0),
                    5) |
                bit(pin((p2 & pad_button::up) != 0U), 6) |
                bit(pin((p2 & pad_button::down) != 0U), 7);
            return static_cast<std::uint8_t>(v);
        }

        // Port $DD: P2 directions + buttons, P2 TR, the reset button, and the TH pins.
        std::uint8_t read_pad_dd(const sms_system* s) noexcept {
            const std::uint8_t p2 = s->pad[1];
            unsigned v =
                bit(pin((p2 & pad_button::left) != 0U), 0) |
                bit(pin((p2 & pad_button::right) != 0U), 1) |
                bit(pin((p2 & pad_button::button_1) != 0U), 2) |
                bit(tr_is_input(s, 1) ? pin((p2 & pad_button::button_2) != 0U) : tr_output(s, 1),
                    3) |
                bit(pin(s->reset_pressed), 4) | bit(1U, 5) |
                bit(th_is_input(s, 0) ? 1U : th_output(s, 0), 6) |
                bit(th_is_input(s, 1) ? 1U : th_output(s, 1), 7);
            return static_cast<std::uint8_t>(v);
        }
    } // namespace

    std::unique_ptr<sms_system> assemble_sms(std::vector<std::uint8_t> rom,
                                             const sms_config& config) {
        auto sys = std::make_unique<sms_system>();
        sms_system* s = sys.get();
        s->rom = std::move(rom);

        // Region: NTSC = 262 scanlines, PAL = 313.
        s->vdp.set_pal(config.video_region == sms_config::region::pal);

        // The Sega mapper banks the borrowed cartridge image.
        s->mapper.attach_rom(std::span<const std::uint8_t>(s->rom));

        // --- Z80 memory map (16-bit address space) ---
        // $0000-$BFFF: three ROM slots + optional cart RAM, via the mapper.
        s->bus.map_mmio(
            0x0000U, 0xC000U,
            [s](std::uint32_t a) { return s->mapper.cpu_read(static_cast<std::uint16_t>(a)); },
            [s](std::uint32_t a, std::uint8_t v) {
                s->mapper.cpu_write(static_cast<std::uint16_t>(a), v);
            },
            0);
        // $C000-$DFFF: 8 KiB system RAM, mirrored at $E000-$FFFF (the same storage).
        s->bus.map_ram(0xC000U, std::span<std::uint8_t>(s->ram), 0);
        s->bus.map_ram(0xE000U, std::span<std::uint8_t>(s->ram), 0);
        // The mapper control registers overlap the top of the RAM mirror: a write
        // both lands in RAM (games read $FFFC-$FFFF back as ordinary work RAM) and
        // updates the mapper; reads return the RAM byte. Priority 1 wins over RAM.
        s->bus.map_mmio(
            chips::mapper::sms_mapper::register_base, 0x4U,
            [s](std::uint32_t a) { return s->ram[a & 0x1FFFU]; },
            [s](std::uint32_t a, std::uint8_t v) {
                s->ram[a & 0x1FFFU] = v;
                s->mapper.write_register(static_cast<std::uint16_t>(a), v);
            },
            1);

        // --- Z80 I/O ports (separate 64K IN/OUT space) ---
        s->cpu.set_port_in([s](std::uint16_t port) -> std::uint8_t {
            const auto p = static_cast<std::uint8_t>(port & 0xFFU);
            if (p <= 0x3FU) {
                return 0xFFU; // open bus
            }
            if (p <= 0x7FU) {
                return (p & 1U) != 0U ? s->vdp.hcounter() : s->vdp.vcounter();
            }
            if (p <= 0xBFU) {
                return (p & 1U) != 0U ? s->vdp.ctrl_read() : s->vdp.data_read();
            }
            return (p & 1U) != 0U ? read_pad_dd(s) : read_pad_dc(s);
        });
        s->cpu.set_port_out([s](std::uint16_t port, std::uint8_t value) {
            const auto p = static_cast<std::uint8_t>(port & 0xFFU);
            if (p <= 0x3FU) {
                if ((p & 1U) != 0U) {
                    s->io_ctrl = value; // I/O port control ($3F)
                }
                return;
            }
            if (p <= 0x7FU) {
                s->psg.write(value);
                return;
            }
            if (p <= 0xBFU) {
                if ((p & 1U) != 0U) {
                    s->vdp.ctrl_write(value);
                } else {
                    s->vdp.data_write(value);
                }
                return;
            }
            // $C0-$FF: no writable registers on a base SMS.
        });

        // --- Interrupts: the VDP /INT line is ORed into the Z80 IRQ. ---
        s->vdp.set_irq_callback([s](bool) { s->cpu.set_irq_line(s->vdp.irq_asserted()); });

        // --- CPU bus + post-BIOS stack pointer. ---
        s->cpu.attach_bus(s->bus);
        // The Z80 powers on with SP=$FFFF; the SMS BIOS sets SP=$DFF0 before handing
        // off to the cart. We do not boot the BIOS, so emulate its post-init SP here
        // so a cart that issues a CALL before setting SP does not push onto the
        // $FFFC-$FFFF mapper page registers and corrupt its own banking.
        auto regs = s->cpu.cpu_registers();
        regs.sp = 0xDFF0U;
        s->cpu.set_registers(regs);

        return sys;
    }

} // namespace mnemos::manifests::sms
