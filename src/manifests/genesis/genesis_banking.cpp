#include "genesis_banking.hpp"

#include "bus.hpp"

namespace mnemos::manifests::genesis {

    namespace {
        constexpr std::size_t cart_window = 0x400000U; // 4 MiB cartridge window
        constexpr std::size_t bank_size = 0x80000U;    // 512 KiB slot/bank
        constexpr std::uint32_t reg_base = 0xA130F1U;  // $A130F1; F3/F5/.../FF = slots 1-7
    } // namespace

    void wire_cart_banking(topology::bus& bus, cart_banking_runtime& out,
                           std::span<const std::uint8_t> rom) {
        if (rom.size() <= cart_window) {
            return; // fits the flat window; no banking hardware
        }
        out.active = true;
        auto* s = &out;

        // Banked view over the whole $000000-$3FFFFF window (priority 1, above the
        // flat ROM). slot = addr / 512 KiB selects the bank; slot 0's bank is never
        // written, so it stays fixed at bank 0 (the running code).
        bus.map_mmio(
            0x000000U, static_cast<std::uint32_t>(cart_window),
            [s, rom](std::uint32_t addr) -> std::uint8_t {
                const std::uint32_t slot = (addr >> 19U) & 7U;
                const std::size_t off =
                    static_cast<std::size_t>(s->bank[slot]) * bank_size + (addr & 0x7FFFFU);
                return off < rom.size() ? rom[off] : 0xFFU;
            },
            [](std::uint32_t, std::uint8_t) {}, // ROM ignores writes
            /*priority=*/1);

        // Bank registers $A130F3/F5/F7/F9/FB/FD/FF: the odd byte at $A130F1 + 2*slot
        // sets slot 1-7's 512 KiB ROM bank.
        bus.map_mmio(
            0xA130F3U, 0x0DU,
            [s](std::uint32_t addr) -> std::uint8_t {
                const std::uint32_t slot = (addr - reg_base) / 2U;
                return ((addr & 1U) != 0U && slot >= 1U && slot <= 7U) ? s->bank[slot] : 0xFFU;
            },
            [s](std::uint32_t addr, std::uint8_t v) {
                const std::uint32_t slot = (addr - reg_base) / 2U;
                if ((addr & 1U) != 0U && slot >= 1U && slot <= 7U) {
                    s->bank[slot] = v;
                }
            },
            /*priority=*/1);
    }

} // namespace mnemos::manifests::genesis
