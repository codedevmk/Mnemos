#include "genesis_cart.hpp"

#include "bus.hpp"

namespace mnemos::manifests::genesis {

    std::optional<cart_sram> parse_cart_sram(std::span<const std::uint8_t> rom) noexcept {
        // The external-RAM header block spans $1B0-$1BB; require it in full.
        if (rom.size() < 0x1BCU) {
            return std::nullopt;
        }
        if (rom[0x1B0U] != static_cast<std::uint8_t>('R') ||
            rom[0x1B1U] != static_cast<std::uint8_t>('A')) {
            return std::nullopt; // no external-RAM signature
        }
        const auto be32 = [rom](std::size_t off) -> std::uint32_t {
            return (static_cast<std::uint32_t>(rom[off]) << 24U) |
                   (static_cast<std::uint32_t>(rom[off + 1U]) << 16U) |
                   (static_cast<std::uint32_t>(rom[off + 2U]) << 8U) |
                   static_cast<std::uint32_t>(rom[off + 3U]);
        };
        cart_sram s;
        s.start = be32(0x1B4U) & 0xFFFFFFU; // 24-bit address bus
        s.end = be32(0x1B8U) & 0xFFFFFFU;

        // Header sanitisation (mirrors how real hardware/cores coerce the
        // well-known garbage values):
        if (s.start == 0xFF0000U) {
            // Work RAM mislabelled as external backup RAM -- no SRAM here.
            return std::nullopt;
        }
        if (s.start >= 0x800000U) {
            // Start off the cartridge window: force the canonical 64 KiB block.
            s.start = 0x200000U;
            s.end = 0x20FFFFU;
        } else if (s.start > s.end || (s.end - s.start) >= 0x10000U) {
            // Inverted or oversized range: clamp to the 64 KiB hardware maximum.
            s.end = s.start + 0xFFFFU;
        }
        return s;
    }

    void wire_cart_sram(topology::bus& bus, cart_sram_runtime& out,
                        std::span<const std::uint8_t> rom) {
        out.info = parse_cart_sram(rom);
        if (!out.info) {
            return;
        }
        const cart_sram info = *out.info;
        out.data.assign(info.byte_count(), 0xFFU);
        // SRAM powers on mapped only when its window sits above the ROM image; an
        // overlapping window (>2 MB cart) boots ROM-visible and is banked in on
        // demand via $A130F1, matching the hardware mapper.
        out.enabled = info.start >= rom.size();
        out.write_protect = false;
        auto* s = &out;
        const std::uint32_t base = info.start;

        // Priority-1 region above the priority-0 cartridge ROM: when enabled it
        // shadows the ROM window; when disabled the read falls through to the ROM.
        bus.map_mmio(
            info.start, info.end - info.start + 1U,
            [s, base](std::uint32_t addr) -> std::uint8_t {
                const std::size_t i = addr - base;
                return i < s->data.size() ? s->data[i] : 0xFFU;
            },
            [s, base](std::uint32_t addr, std::uint8_t v) {
                const std::size_t i = addr - base;
                if (!s->write_protect && i < s->data.size()) {
                    s->data[i] = v;
                }
            },
            /*priority=*/1, [s](std::uint32_t, bool) { return s->enabled; });

        // $A130F1: SRAM control latch. Bit 0 maps SRAM over the cartridge window
        // (vs the ROM beneath); bit 1 write-protects it while mapped.
        bus.map_mmio(
            0xA130F1U, 1U, [s](std::uint32_t) -> std::uint8_t { return s->enabled ? 1U : 0U; },
            [s](std::uint32_t, std::uint8_t v) {
                s->enabled = (v & 1U) != 0U;
                s->write_protect = (v & 2U) != 0U;
            },
            1);
    }

} // namespace mnemos::manifests::genesis
