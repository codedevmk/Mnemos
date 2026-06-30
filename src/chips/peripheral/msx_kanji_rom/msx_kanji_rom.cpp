#include "msx_kanji_rom.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <memory>

namespace mnemos::chips::peripheral {

    namespace {
        constexpr std::uint32_t k_state_version = 1U;
    } // namespace

    chip_metadata msx_kanji_rom::metadata() const noexcept {
        return {
            .manufacturer = "Microsoft",
            .part_number = "MSX-KANJI",
            .family = "kanji_rom",
            .klass = chip_class::peripheral,
            .revision = 1U,
        };
    }

    void msx_kanji_rom::tick(std::uint64_t /*cycles*/) {}

    void msx_kanji_rom::reset(reset_kind /*kind*/) {
        character_address_ = {};
        byte_counter_ = {};
    }

    void msx_kanji_rom::attach_rom(std::span<const std::uint8_t> rom) {
        rom_.assign(rom.begin(), rom.end());
        reset(reset_kind::power_on);
    }

    void msx_kanji_rom::eject() noexcept {
        rom_.clear();
        reset(reset_kind::power_on);
    }

    std::uint16_t msx_kanji_rom::character_address(std::size_t level) const noexcept {
        return valid_level(level) ? character_address_[level] : 0U;
    }

    std::uint8_t msx_kanji_rom::byte_counter(std::size_t level) const noexcept {
        return valid_level(level) ? static_cast<std::uint8_t>(byte_counter_[level] & 0x1FU) : 0U;
    }

    std::uint8_t msx_kanji_rom::read_data(std::size_t level) noexcept {
        if (!valid_level(level)) {
            return 0xFFU;
        }
        const std::size_t counter = byte_counter_[level] & 0x1FU;
        const std::size_t offset =
            (level * level_size) +
            (static_cast<std::size_t>(character_address_[level] & 0x0FFFU) * bytes_per_character) +
            counter;
        byte_counter_[level] = static_cast<std::uint8_t>((counter + 1U) & 0x1FU);
        return offset < rom_.size() ? rom_[offset] : 0xFFU;
    }

    void msx_kanji_rom::write_address(std::size_t level, bool upper, std::uint8_t value) noexcept {
        if (!valid_level(level)) {
            return;
        }
        const auto bits = static_cast<std::uint16_t>(value & 0x3FU);
        if (upper) {
            character_address_[level] = static_cast<std::uint16_t>(
                ((bits << 6U) | (character_address_[level] & 0x003FU)) & 0x0FFFU);
        } else {
            character_address_[level] =
                static_cast<std::uint16_t>((character_address_[level] & 0x0FC0U) | bits);
        }
        byte_counter_[level] = 0U;
    }

    void msx_kanji_rom::save_state(state_writer& writer) const {
        writer.u32(k_state_version);
        for (const std::uint16_t address : character_address_) {
            writer.u16(address);
        }
        writer.bytes(byte_counter_);
    }

    void msx_kanji_rom::load_state(state_reader& reader) {
        const std::uint32_t version = reader.u32();
        if (version == 0U || version > k_state_version) {
            reader.fail();
            return;
        }
        for (std::uint16_t& address : character_address_) {
            address = static_cast<std::uint16_t>(reader.u16() & 0x0FFFU);
        }
        reader.bytes(byte_counter_);
        for (std::uint8_t& counter : byte_counter_) {
            counter = static_cast<std::uint8_t>(counter & 0x1FU);
        }
    }

    namespace {
        [[maybe_unused]] const auto msx_kanji_rom_registration = register_factory(
            "msx.kanji_rom", chip_class::peripheral,
            []() -> std::unique_ptr<ichip> { return std::make_unique<msx_kanji_rom>(); });
    } // namespace

} // namespace mnemos::chips::peripheral
