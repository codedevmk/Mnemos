#pragma once

#include "chip.hpp"

#include <array>
#include <cstdint>
#include <span>

namespace mnemos::chips::mapper {

    // Commodore 64 PLA (MOS 906114-01 / 82S100) memory-banking decoder.
    //
    // Pure combinational: five
    // logic inputs — the CPU-port bits LORAM/HIRAM/CHAREN plus the cartridge
    // /GAME and /EXROM lines — select one chip-select output per region of CPU
    // (or VIC-II) address space. No clocked state, so tick() is a no-op.
    class c64_pla final : public imapper {
      public:
        enum class region : std::uint8_t {
            ram,
            basic,
            kernal,
            chargen,
            io,
            roml,
            romh,
            open, // ultimax $1000-$7FFF: reads float, writes drop
        };

        c64_pla() { reset(reset_kind::power_on); }

        [[nodiscard]] chip_metadata metadata() const noexcept override;
        void tick(std::uint64_t cycles) override;
        void reset(reset_kind kind) override;

        void save_state(state_writer& writer) const override;
        void load_state(state_reader& reader) override;

        [[nodiscard]] instrumentation::ichip_introspection& introspection() noexcept override;

        // Inputs. CPU-port bits mirror the low 3 bits of the 6510 $0001 port;
        // /GAME and /EXROM float high (true) on a bare machine with no cartridge.
        void set_cpu_port(bool loram, bool hiram, bool charen) noexcept;
        void set_cart_lines(bool game, bool exrom) noexcept;

        // Decode a CPU-bus address to the region the PLA selects for the current
        // input configuration.
        [[nodiscard]] region decode_cpu_address(std::uint16_t address) const noexcept;

        // Decode a VIC-II private-bus fetch. The VIC path ignores CHAREN and the
        // CPU port; only RAM, CHARGEN, and (ultimax) ROMH are reachable.
        [[nodiscard]] region decode_vic_address(std::uint16_t address) const noexcept;

        [[nodiscard]] bool loram() const noexcept { return loram_; }
        [[nodiscard]] bool hiram() const noexcept { return hiram_; }
        [[nodiscard]] bool charen() const noexcept { return charen_; }
        [[nodiscard]] bool game() const noexcept { return game_; }
        [[nodiscard]] bool exrom() const noexcept { return exrom_; }
        [[nodiscard]] bool ultimax() const noexcept;

        [[nodiscard]] std::span<const register_descriptor> register_snapshot() noexcept;

      private:
        class introspection_surface final : public instrumentation::ichip_introspection {};

        [[nodiscard]] region decode_a000_bfff() const noexcept;
        [[nodiscard]] region decode_d000_dfff() const noexcept;
        [[nodiscard]] region decode_e000_ffff() const noexcept;

        bool loram_{true};
        bool hiram_{true};
        bool charen_{true};
        bool game_{true};
        bool exrom_{true};

        std::array<register_descriptor, 1> register_view_{};
        introspection_surface introspection_{};
    };

} // namespace mnemos::chips::mapper
