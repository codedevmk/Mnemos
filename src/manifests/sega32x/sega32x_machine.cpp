#include "sega32x_machine.hpp"

namespace mnemos::manifests::sega32x {

    void sega32x_machine::begin_slice() noexcept {
        slice_base_main_ = genesis->cpu.elapsed_cycles();
        slice_base_sh2_ = thirtytwox->master_cpu.elapsed_cycles();
    }

    void sega32x_machine::catch_up_sh2() {
        // Run both SH-2s up to the 68000's position within the current slice. The
        // SH-2s tick at 3x the 68000, so the target is the slice's 68000 delta
        // scaled by 3. run_cycles is a no-op while the SH-2s are held in reset.
        const std::uint64_t main_now = genesis->cpu.elapsed_cycles();
        if (main_now <= slice_base_main_) {
            return;
        }
        const std::uint64_t main_delta = main_now - slice_base_main_;
        const std::uint64_t target = slice_base_sh2_ + main_delta * sh2_clock_multiplier;
        const std::uint64_t cur = thirtytwox->master_cpu.elapsed_cycles();
        if (target > cur) {
            thirtytwox->run_cycles(target - cur);
        }
    }

    std::unique_ptr<sega32x_machine>
    assemble_sega32x_machine(std::vector<std::uint8_t> cart,
                             const genesis::genesis_config& config) {
        auto machine = std::make_unique<sega32x_machine>();
        machine->thirtytwox = assemble_sega32x();
        // The Genesis main side boots the 32X cartridge as its cartridge ROM.
        machine->genesis = genesis::assemble_genesis(std::move(cart), config);

        sega32x_system* tx = machine->thirtytwox.get();
        topology::bus& bus = machine->genesis->bus;

        // $A15100-$A15101: adapter control (68000 view). Big-endian byte lanes --
        // the even address is the high byte (FM/RV), the odd address the low byte
        // carrying ADEN (bit 1) and RES (bit 0). Writing the low byte drives the
        // SH-2 /RES line: ADEN+RES-release starts the SH-2s, clearing RES parks
        // them. Priority 1 keeps it above the cartridge ROM. This window exists
        // only on a 32X machine; a plain Genesis never maps it.
        bus.map_mmio(
            0xA15100U, 0x2U,
            [tx](std::uint32_t a) -> std::uint8_t {
                std::uint16_t v = tx->adapter_ctrl;
                if (tx->sh2_reset_asserted) {
                    v &= static_cast<std::uint16_t>(~0x0001U); // RES reads 0 while held
                }
                return (a & 1U) ? static_cast<std::uint8_t>(v & 0xFFU)
                                : static_cast<std::uint8_t>(v >> 8U);
            },
            [tx](std::uint32_t a, std::uint8_t value) {
                if ((a & 1U) != 0U) { // low byte: ADEN + RES
                    const bool aden = (value & 0x02U) != 0U;
                    const bool res_release = (value & 0x01U) != 0U;
                    // Latch the written bits (RES is driven, not stored) so game
                    // code reads back its own non-reset writes.
                    tx->adapter_ctrl =
                        static_cast<std::uint16_t>((tx->adapter_ctrl & 0xFF00U) | (value & 0xFEU));
                    if (aden && res_release) {
                        tx->set_sh2_reset(false); // release the SH-2s
                    } else if (!res_release) {
                        tx->set_sh2_reset(true); // park the SH-2s
                    }
                } else { // high byte: FM / RV and friends
                    tx->adapter_ctrl = static_cast<std::uint16_t>(
                        (tx->adapter_ctrl & 0x00FFU) | (static_cast<std::uint16_t>(value) << 8U));
                }
            },
            1);

        return machine;
    }

} // namespace mnemos::manifests::sega32x
