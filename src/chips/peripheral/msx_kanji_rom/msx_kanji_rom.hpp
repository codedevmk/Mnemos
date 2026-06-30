#pragma once

#include "chip.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace mnemos::chips::peripheral {

    // MSX Kanji ROM interface. The host writes a 12-bit JIS character index through
    // ports $D8/$D9 (level 1) or $DA/$DB (level 2), then reads 32 font bytes from
    // the matching data port. The byte cursor wraps inside the selected character.
    class msx_kanji_rom final : public iperipheral {
      public:
        static constexpr std::size_t level_count = 2U;
        static constexpr std::size_t level_size = 0x20000U;
        static constexpr std::size_t bytes_per_character = 32U;

        [[nodiscard]] static constexpr std::size_t
        complete_level_count_for_size(std::size_t bytes) noexcept {
            const std::size_t levels = bytes / level_size;
            return levels > level_count ? level_count : levels;
        }

        [[nodiscard]] static constexpr bool has_partial_level_for_size(std::size_t bytes) noexcept {
            return complete_level_count_for_size(bytes) < level_count && (bytes % level_size) != 0U;
        }

        msx_kanji_rom() = default;

        [[nodiscard]] chip_metadata metadata() const noexcept override;
        void tick(std::uint64_t cycles) override;
        void reset(reset_kind kind) override;
        void save_state(state_writer& writer) const override;
        void load_state(state_reader& reader) override;
        [[nodiscard]] instrumentation::ichip_introspection& introspection() noexcept override {
            return introspection_;
        }

        void attach_rom(std::span<const std::uint8_t> rom);
        void eject() noexcept;
        [[nodiscard]] bool loaded() const noexcept { return !rom_.empty(); }
        [[nodiscard]] bool empty() const noexcept { return rom_.empty(); }
        [[nodiscard]] std::size_t rom_size() const noexcept { return rom_.size(); }
        [[nodiscard]] std::size_t complete_level_count() const noexcept {
            return complete_level_count_for_size(rom_.size());
        }
        [[nodiscard]] bool has_partial_level() const noexcept {
            return has_partial_level_for_size(rom_.size());
        }
        [[nodiscard]] bool level_loaded(std::size_t level) const noexcept {
            return valid_level(level) && complete_level_count() > level;
        }

        [[nodiscard]] std::uint16_t character_address(std::size_t level) const noexcept;
        [[nodiscard]] std::uint8_t byte_counter(std::size_t level) const noexcept;
        [[nodiscard]] std::uint8_t read_data(std::size_t level) noexcept;
        void write_address(std::size_t level, bool upper, std::uint8_t value) noexcept;

      private:
        class introspection_surface final : public instrumentation::ichip_introspection {};

        [[nodiscard]] bool valid_level(std::size_t level) const noexcept {
            return level < level_count;
        }

        std::vector<std::uint8_t> rom_{};
        std::array<std::uint16_t, level_count> character_address_{};
        std::array<std::uint8_t, level_count> byte_counter_{};
        introspection_surface introspection_{};
    };

} // namespace mnemos::chips::peripheral
