#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace mnemos::manifests::common {

    enum class msx_cartridge_mapper_kind : std::uint8_t {
        automatic,
        plain,
        ascii8,
        ascii8_sram8,
        ascii16,
        ascii16_sram2,
        generic8,
        konami,
        konami_scc,
        korean_msx,
        korean_msx_nemesis,
    };

    [[nodiscard]] msx_cartridge_mapper_kind
    detect_msx_cartridge_mapper(std::span<const std::uint8_t> rom) noexcept;

    [[nodiscard]] msx_cartridge_mapper_kind
    resolve_msx_cartridge_mapper(msx_cartridge_mapper_kind requested,
                                 std::span<const std::uint8_t> rom) noexcept;

    [[nodiscard]] msx_cartridge_mapper_kind
    parse_msx_cartridge_mapper_name(std::string_view name) noexcept;

    [[nodiscard]] std::string_view
    msx_cartridge_mapper_label(msx_cartridge_mapper_kind mapper) noexcept;

    [[nodiscard]] bool msx_mapper_has_scc_audio(msx_cartridge_mapper_kind mapper) noexcept;
    [[nodiscard]] bool msx_mapper_is_korean(msx_cartridge_mapper_kind mapper) noexcept;
    [[nodiscard]] std::size_t msx_mapper_sram_size(msx_cartridge_mapper_kind mapper) noexcept;
    [[nodiscard]] std::size_t msx_plain_rom_payload_offset(
        std::span<const std::uint8_t> rom) noexcept;
    [[nodiscard]] std::size_t msx_plain_rom_physical_offset(
        std::span<const std::uint8_t> rom, std::uint16_t address,
        bool mirror_16k_lower_page = false) noexcept;
    [[nodiscard]] bool msx_plain_32k_upper_rom_window(msx_cartridge_mapper_kind mapper,
                                                      std::size_t rom_size,
                                                      std::uint16_t address) noexcept;
    [[nodiscard]] bool msx_plain_32k_lower_rom_window(msx_cartridge_mapper_kind mapper,
                                                      std::size_t rom_size,
                                                      std::uint16_t address) noexcept;
    [[nodiscard]] bool msx_plain_32k_lower_handoff_required(
        std::span<const std::uint8_t> rom) noexcept;
    [[nodiscard]] std::uint8_t msx_ascii16_bank_register(std::uint16_t address) noexcept;
    [[nodiscard]] std::uint16_t mirror_msx_konami_address(std::uint16_t address) noexcept;

} // namespace mnemos::manifests::common
