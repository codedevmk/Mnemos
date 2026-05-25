#pragma once

#include "chip.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace mnemos::chips::mapper {

    // Commodore 64 expansion-port cartridge.
    //
    // Parses the CCS64/VICE `.crt` container and exposes the ROML ($8000-$9FFF) and
    // ROMH ($A000-$BFFF, or $E000-$FFFF in ultimax) views plus the I/O-1/I/O-2
    // window ($DE00-$DFFF) for bank switching. It also drives the /GAME and /EXROM
    // lines the PLA decodes. Supported hardware types: generic 8K/16K/ultimax,
    // Ocean (5), Magic Desk (19), and EasyFlash (32). Ported per ADR 0006.
    class c64_cartridge final : public imapper, public immio {
      public:
        enum class hardware : std::uint16_t {
            generic = 0U,
            ocean = 5U,
            magic_desk = 19U,
            easyflash = 32U,
        };

        static constexpr std::uint16_t bank_size = 0x2000U; // 8 KiB per ROML/ROMH bank
        static constexpr std::uint16_t io_window = 0x200U;  // $DE00-$DFFF

        c64_cartridge() { reset(reset_kind::power_on); }

        [[nodiscard]] chip_metadata metadata() const noexcept override;
        void tick(std::uint64_t cycles) override;
        void reset(reset_kind kind) override;

        void save_state(state_writer& writer) const override;
        void load_state(state_reader& reader) override;

        [[nodiscard]] instrumentation::ichip_introspection& introspection() noexcept override;

        // I/O-1/I/O-2 ($DE00-$DFFF): bank switching + EasyFlash control/RAM.
        [[nodiscard]] std::uint8_t mmio_read(std::uint16_t offset) override;
        void mmio_write(std::uint16_t offset, std::uint8_t value) override;

        // Load a `.crt` image. Returns false if the container is malformed.
        [[nodiscard]] bool load_crt(std::span<const std::uint8_t> crt);
        void eject() noexcept;
        [[nodiscard]] bool inserted() const noexcept { return inserted_; }
        [[nodiscard]] hardware type() const noexcept { return type_; }
        [[nodiscard]] std::uint16_t bank() const noexcept { return bank_; }
        [[nodiscard]] std::uint16_t bank_count() const noexcept { return bank_count_; }

        // ROM reads (offset within the 8 KiB window). 0xFF when nothing is mapped.
        [[nodiscard]] std::uint8_t read_roml(std::uint16_t offset) const noexcept;
        [[nodiscard]] std::uint8_t read_romh(std::uint16_t offset) const noexcept;

        // PLA control lines: true = released (high), false = asserted (low), matching
        // c64_pla::set_cart_lines(game, exrom).
        [[nodiscard]] bool game() const noexcept { return game_; }
        [[nodiscard]] bool exrom() const noexcept { return exrom_; }

      private:
        class introspection_surface final : public instrumentation::ichip_introspection {};

        hardware type_{hardware::generic};
        bool inserted_{};
        bool enabled_{true}; // Magic Desk software-disable
        bool game_{true};
        bool exrom_{true};
        bool game_default_{true};
        bool exrom_default_{true};

        std::uint16_t bank_{};
        std::uint16_t bank_count_{};
        std::vector<std::uint8_t> roml_;           // bank_count_ * 8K, flat (may be empty)
        std::vector<std::uint8_t> romh_;           // bank_count_ * 8K, flat (may be empty)
        std::array<std::uint8_t, 0x100> ef_ram_{}; // EasyFlash $DF00 RAM

        introspection_surface introspection_{};
    };

} // namespace mnemos::chips::mapper
