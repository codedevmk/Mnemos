#include "c64_callbacks.hpp"

#include <cstdint>

namespace mnemos::manifests::c64 {

    chips::overlay_predicate_table make_c64_overlay_predicates(c64_callbacks_state& state) {
        using region = chips::mapper::c64_pla::region;
        auto* s = &state;

        // Lifted intact from assemble_c64: read the live 6510 $01 port + the
        // cartridge lines, feed the PLA, and decode the CPU address.
        auto decode = [s](std::uint32_t address) -> region {
            const std::uint8_t port = s->cpu->read(0x0001U);
            s->pla->set_cpu_port((port & 0x01U) != 0U, (port & 0x02U) != 0U, (port & 0x04U) != 0U);
            s->pla->set_cart_lines(s->cart->game(), s->cart->exrom());
            return s->pla->decode_cpu_address(static_cast<std::uint16_t>(address));
        };

        chips::overlay_predicate_table out;
        out.emplace("c64.bank.basic", [decode](std::uint32_t a, bool is_write) {
            return !is_write && decode(a) == region::basic;
        });
        out.emplace("c64.bank.kernal", [decode](std::uint32_t a, bool is_write) {
            return !is_write && decode(a) == region::kernal;
        });
        out.emplace("c64.bank.chargen", [decode](std::uint32_t a, bool is_write) {
            return !is_write && decode(a) == region::chargen;
        });
        // I/O is active for reads AND writes (the VIC/SID/CIA/colour-RAM windows).
        out.emplace("c64.bank.io",
                    [decode](std::uint32_t a, bool) { return decode(a) == region::io; });
        return out;
    }

} // namespace mnemos::manifests::c64
