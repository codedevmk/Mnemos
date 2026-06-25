#include "msx_cartridge_mapper.hpp"

#include "crc32.hpp"

#include <algorithm>
#include <array>
#include <string_view>

namespace mnemos::manifests::common {

    namespace {
        constexpr std::size_t k_ascii8_sram8_size = 0x2000U;
        constexpr std::size_t k_ascii16_sram2_size = 0x0800U;
        constexpr std::size_t k_loader_signature_scan_bytes = 0x2000U;
        constexpr std::size_t k_converter_marker_scan_bytes = 0x80U;
        constexpr std::string_view k_cas2rom64ks_marker = "[cas2rom64ks]";
        constexpr std::string_view k_jam_converter_marker = "adapted by jam";
        constexpr std::string_view k_kabish_marker = "kabish";
        constexpr std::uint32_t k_a_b_arabic_msx_crc = 0x088F0DF9U;

        struct known_crc_mapper final {
            std::uint32_t crc{};
            msx_cartridge_mapper_kind mapper{};
        };

        struct mapper_write_signature final {
            std::array<bool, 4> ascii8{};
            std::array<bool, 2> ascii16{};
            std::array<bool, 4> generic8{};
            std::array<bool, 3> konami{};
            std::array<bool, 4> konami_scc{};
        };

        constexpr std::array<known_crc_mapper, 5> k_known_crc_mapper_db{{
            {0x7E59C936U, msx_cartridge_mapper_kind::korean_msx}, // 1942 (Zemina) [a2]
            {0xDDF0E6DFU, msx_cartridge_mapper_kind::ascii8},      // ADVKIDA
            {0x1944E3ECU, msx_cartridge_mapper_kind::ascii8}, // Aliens (Mr. Micro) (1987)
            {0xA5C35354U, msx_cartridge_mapper_kind::generic8}, // AshGuine Story II
            {0xDE7D193BU, msx_cartridge_mapper_kind::ascii8},   // Black Beard (Kabish)
        }};

        template <std::size_t N>
        [[nodiscard]] int count_set(const std::array<bool, N>& bits) noexcept {
            int count = 0;
            for (const bool bit : bits) {
                if (bit) {
                    ++count;
                }
            }
            return count;
        }

        void note_mapper_write(mapper_write_signature& sig, std::uint16_t address) noexcept {
            if (address >= 0x6000U && address < 0x6800U) {
                sig.ascii8[0] = true;
                sig.ascii16[0] = true;
            } else if (address >= 0x6800U && address < 0x7000U) {
                sig.ascii8[1] = true;
                sig.ascii16[0] = true;
            } else if (address >= 0x7000U && address < 0x7800U) {
                sig.ascii8[2] = true;
                sig.ascii16[1] = true;
            } else if (address >= 0x7800U && address < 0x8000U) {
                sig.ascii8[3] = true;
                sig.ascii16[1] = true;
            }

            if (address >= 0x4000U && address < 0x6000U) {
                sig.generic8[0] = true;
            } else if (address >= 0x6000U && address < 0x8000U) {
                sig.generic8[1] = true;
            } else if (address >= 0x8000U && address < 0xA000U) {
                sig.generic8[2] = true;
            } else if (address >= 0xA000U && address < 0xC000U) {
                sig.generic8[3] = true;
            }

            if (address >= 0x6000U && address < 0x8000U) {
                sig.konami[0] = true;
            } else if (address >= 0x8000U && address < 0xA000U) {
                sig.konami[1] = true;
            } else if (address >= 0xA000U && address < 0xC000U) {
                sig.konami[2] = true;
            }

            if (address >= 0x5000U && address < 0x5800U) {
                sig.konami_scc[0] = true;
            } else if (address >= 0x7000U && address < 0x7800U) {
                sig.konami_scc[1] = true;
            } else if (address >= 0x9000U && address < 0x9800U) {
                sig.konami_scc[2] = true;
            } else if (address >= 0xB000U && address < 0xB800U) {
                sig.konami_scc[3] = true;
            }
        }

        [[nodiscard]] mapper_write_signature
        mapper_write_signature_for_rom(std::span<const std::uint8_t> rom) noexcept {
            mapper_write_signature sig{};
            for (std::size_t i = 0; i + 2U < rom.size(); ++i) {
                if (rom[i] == 0x32U || rom[i] == 0x22U) {
                    const auto address = static_cast<std::uint16_t>(
                        rom[i + 1U] | (static_cast<std::uint16_t>(rom[i + 2U]) << 8U));
                    note_mapper_write(sig, address);
                    continue;
                }
                if (i + 3U < rom.size() && (rom[i] == 0xDDU || rom[i] == 0xFDU) &&
                    rom[i + 1U] == 0x22U) {
                    const auto address = static_cast<std::uint16_t>(
                        rom[i + 2U] | (static_cast<std::uint16_t>(rom[i + 3U]) << 8U));
                    note_mapper_write(sig, address);
                }
            }
            return sig;
        }

        [[nodiscard]] msx_cartridge_mapper_kind
        classify_mapper_signature(const mapper_write_signature& sig) noexcept {
            if (count_set(sig.konami_scc) >= 3) {
                return msx_cartridge_mapper_kind::konami_scc;
            }
            const int generic8_count = count_set(sig.generic8);
            if (sig.generic8[0] && generic8_count >= 2) {
                return msx_cartridge_mapper_kind::generic8;
            }
            if (count_set(sig.konami) >= 2 && (sig.konami[1] || sig.konami[2])) {
                return msx_cartridge_mapper_kind::konami;
            }
            const int ascii8_count = count_set(sig.ascii8);
            if (ascii8_count >= 2 && (sig.ascii8[1] || sig.ascii8[3] || ascii8_count >= 3)) {
                return msx_cartridge_mapper_kind::ascii8;
            }
            if (sig.ascii16[0] && sig.ascii16[1]) {
                return msx_cartridge_mapper_kind::ascii16;
            }
            return msx_cartridge_mapper_kind::plain;
        }

        [[nodiscard]] char folded_char(char c) noexcept {
            if (c >= 'A' && c <= 'Z') {
                return static_cast<char>(c - 'A' + 'a');
            }
            return c == '_' ? '-' : c;
        }

        [[nodiscard]] bool name_equals(std::string_view lhs, std::string_view rhs) noexcept {
            if (lhs.size() != rhs.size()) {
                return false;
            }
            for (std::size_t i = 0; i < lhs.size(); ++i) {
                if (folded_char(lhs[i]) != folded_char(rhs[i])) {
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] bool bytes_contain_folded_ascii(std::span<const std::uint8_t> bytes,
                                                      std::string_view needle) noexcept {
            if (needle.empty() || bytes.size() < needle.size()) {
                return false;
            }
            for (std::size_t i = 0; i + needle.size() <= bytes.size(); ++i) {
                bool matched = true;
                for (std::size_t j = 0; j < needle.size(); ++j) {
                    const auto byte = static_cast<char>(bytes[i + j]);
                    if (folded_char(byte) != folded_char(needle[j])) {
                        matched = false;
                        break;
                    }
                }
                if (matched) {
                    return true;
                }
            }
            return false;
        }

        [[nodiscard]] bool has_cas2rom64ks_marker(std::span<const std::uint8_t> rom) noexcept {
            const auto marker_scan_size = std::min(rom.size(), k_converter_marker_scan_bytes);
            return bytes_contain_folded_ascii(rom.first(marker_scan_size), k_cas2rom64ks_marker);
        }

        [[nodiscard]] bool has_jam_converter_marker(std::span<const std::uint8_t> rom) noexcept {
            const auto marker_scan_size = std::min(rom.size(), k_converter_marker_scan_bytes);
            return bytes_contain_folded_ascii(rom.first(marker_scan_size), k_jam_converter_marker);
        }

        [[nodiscard]] bool has_kabish_converter_marker(std::span<const std::uint8_t> rom) noexcept {
            const auto marker_scan_size = std::min<std::size_t>(rom.size(), 0x800U);
            return bytes_contain_folded_ascii(rom.first(marker_scan_size), k_kabish_marker);
        }

        [[nodiscard]] bool has_msx_header_at(std::span<const std::uint8_t> rom,
                                             std::size_t offset) noexcept {
            return offset + 1U < rom.size() && rom[offset] == 'A' && rom[offset + 1U] == 'B';
        }

        [[nodiscard]] std::uint16_t read_le16(std::span<const std::uint8_t> rom,
                                              std::size_t offset) noexcept {
            if (offset + 1U >= rom.size()) {
                return 0U;
            }
            return static_cast<std::uint16_t>(
                rom[offset] | (static_cast<std::uint16_t>(rom[offset + 1U]) << 8U));
        }

        [[nodiscard]] bool plain_16k_upper_page_entry(std::span<const std::uint8_t> rom,
                                                      std::size_t payload_offset) noexcept {
            if (!has_msx_header_at(rom, payload_offset)) {
                return false;
            }
            for (const std::size_t field : {2U, 4U, 6U, 8U}) {
                const std::uint16_t entry = read_le16(rom, payload_offset + field);
                if (entry >= 0x8000U && entry < 0xC000U) {
                    return true;
                }
            }
            return false;
        }

        [[nodiscard]] msx_cartridge_mapper_kind
        detect_known_crc_mapper(std::span<const std::uint8_t> rom) noexcept {
            const std::uint32_t crc = security::cryptography::crc32(rom);
            for (const known_crc_mapper& entry : k_known_crc_mapper_db) {
                if (entry.crc == crc) {
                    return entry.mapper;
                }
            }
            return msx_cartridge_mapper_kind::automatic;
        }
    } // namespace

    msx_cartridge_mapper_kind
    detect_msx_cartridge_mapper(std::span<const std::uint8_t> rom) noexcept {
        if (rom.size() <= 0x8000U) {
            return msx_cartridge_mapper_kind::plain;
        }

        if (msx_plain_rom_payload_offset(rom) != 0U) {
            return msx_cartridge_mapper_kind::plain;
        }

        if (const msx_cartridge_mapper_kind crc_kind = detect_known_crc_mapper(rom);
            crc_kind != msx_cartridge_mapper_kind::automatic) {
            return crc_kind;
        }

        // Mapper loaders normally live near the reset/header area; scanning the whole payload first
        // lets compressed game data masquerade as writes to unrelated bank registers.
        const auto loader_scan_size = std::min(rom.size(), k_loader_signature_scan_bytes);
        const mapper_write_signature loader_signature =
            mapper_write_signature_for_rom(rom.first(loader_scan_size));
        const bool ascii16_converter =
            has_cas2rom64ks_marker(rom) || has_jam_converter_marker(rom);
        if (has_cas2rom64ks_marker(rom) && has_kabish_converter_marker(rom)) {
            return msx_cartridge_mapper_kind::ascii8;
        }
        if (ascii16_converter && loader_signature.ascii16[0] &&
            loader_signature.ascii16[1]) {
            return msx_cartridge_mapper_kind::ascii16;
        }
        const msx_cartridge_mapper_kind loader_kind = classify_mapper_signature(loader_signature);
        if (loader_kind != msx_cartridge_mapper_kind::plain) {
            return loader_kind;
        }
        return classify_mapper_signature(mapper_write_signature_for_rom(rom));
    }

    msx_cartridge_mapper_kind
    resolve_msx_cartridge_mapper(msx_cartridge_mapper_kind requested,
                                 std::span<const std::uint8_t> rom) noexcept {
        return requested == msx_cartridge_mapper_kind::automatic
                   ? detect_msx_cartridge_mapper(rom)
                   : requested;
    }

    msx_cartridge_mapper_kind parse_msx_cartridge_mapper_name(std::string_view name) noexcept {
        if (name.empty() || name_equals(name, "auto") || name_equals(name, "automatic")) {
            return msx_cartridge_mapper_kind::automatic;
        }
        if (name_equals(name, "plain")) {
            return msx_cartridge_mapper_kind::plain;
        }
        if (name_equals(name, "ascii8")) {
            return msx_cartridge_mapper_kind::ascii8;
        }
        if (name_equals(name, "ascii8-sram8")) {
            return msx_cartridge_mapper_kind::ascii8_sram8;
        }
        if (name_equals(name, "ascii16")) {
            return msx_cartridge_mapper_kind::ascii16;
        }
        if (name_equals(name, "ascii16-sram2")) {
            return msx_cartridge_mapper_kind::ascii16_sram2;
        }
        if (name_equals(name, "generic8") || name_equals(name, "generic-8") ||
            name_equals(name, "konami-generic")) {
            return msx_cartridge_mapper_kind::generic8;
        }
        if (name_equals(name, "konami") || name_equals(name, "konami4")) {
            return msx_cartridge_mapper_kind::konami;
        }
        if (name_equals(name, "konami-scc") || name_equals(name, "konamiscc") ||
            name_equals(name, "scc")) {
            return msx_cartridge_mapper_kind::konami_scc;
        }
        if (name_equals(name, "korean-msx")) {
            return msx_cartridge_mapper_kind::korean_msx;
        }
        if (name_equals(name, "korean-msx-nemesis") || name_equals(name, "nemesis")) {
            return msx_cartridge_mapper_kind::korean_msx_nemesis;
        }
        return msx_cartridge_mapper_kind::automatic;
    }

    std::string_view msx_cartridge_mapper_label(msx_cartridge_mapper_kind mapper) noexcept {
        switch (mapper) {
        case msx_cartridge_mapper_kind::ascii8:
            return "ASCII8";
        case msx_cartridge_mapper_kind::ascii8_sram8:
            return "ASCII8 SRAM";
        case msx_cartridge_mapper_kind::ascii16:
            return "ASCII16";
        case msx_cartridge_mapper_kind::ascii16_sram2:
            return "ASCII16 SRAM";
        case msx_cartridge_mapper_kind::generic8:
            return "Generic8";
        case msx_cartridge_mapper_kind::konami:
            return "Konami";
        case msx_cartridge_mapper_kind::konami_scc:
            return "Konami SCC";
        case msx_cartridge_mapper_kind::korean_msx:
            return "Korean MSX";
        case msx_cartridge_mapper_kind::korean_msx_nemesis:
            return "Korean MSX Nemesis";
        case msx_cartridge_mapper_kind::automatic:
            return "Automatic";
        case msx_cartridge_mapper_kind::plain:
        default:
            return "Plain";
        }
    }

    bool msx_mapper_has_scc_audio(msx_cartridge_mapper_kind mapper) noexcept {
        return mapper == msx_cartridge_mapper_kind::konami_scc;
    }

    bool msx_mapper_is_korean(msx_cartridge_mapper_kind mapper) noexcept {
        return mapper == msx_cartridge_mapper_kind::korean_msx ||
               mapper == msx_cartridge_mapper_kind::korean_msx_nemesis;
    }

    std::size_t msx_mapper_sram_size(msx_cartridge_mapper_kind mapper) noexcept {
        switch (mapper) {
        case msx_cartridge_mapper_kind::ascii8_sram8:
            return k_ascii8_sram8_size;
        case msx_cartridge_mapper_kind::ascii16_sram2:
            return k_ascii16_sram2_size;
        default:
            return 0U;
        }
    }

    std::size_t msx_plain_rom_payload_offset(std::span<const std::uint8_t> rom) noexcept {
        constexpr std::size_t padded_plain_payload = 0x4000U;
        constexpr std::size_t min_padded_plain_size = padded_plain_payload + 0x4000U;
        if (rom.size() >= min_padded_plain_size && !has_msx_header_at(rom, 0U) &&
            has_msx_header_at(rom, padded_plain_payload)) {
            return padded_plain_payload;
        }
        return 0U;
    }

    std::size_t msx_plain_rom_physical_offset(std::span<const std::uint8_t> rom,
                                              std::uint16_t address,
                                              bool mirror_16k_lower_page) noexcept {
        if (address < 0x4000U || address >= 0xC000U || rom.empty()) {
            return rom.size();
        }
        const std::size_t payload_offset = msx_plain_rom_payload_offset(rom);
        if (payload_offset >= rom.size()) {
            return rom.size();
        }
        std::size_t window_offset = static_cast<std::size_t>(address - 0x4000U);
        const std::size_t payload_size = rom.size() - payload_offset;
        if (payload_size == 0x4000U) {
            if (window_offset >= 0x4000U &&
                !plain_16k_upper_page_entry(rom, payload_offset) &&
                !mirror_16k_lower_page) {
                return rom.size();
            }
            window_offset &= 0x3FFFU;
        }
        return payload_offset + window_offset;
    }

    bool msx_plain_32k_upper_rom_window(msx_cartridge_mapper_kind mapper, std::size_t rom_size,
                                        std::uint16_t address) noexcept {
        return mapper == msx_cartridge_mapper_kind::plain && rom_size == 0x8000U &&
               address >= 0x8000U && address < 0xC000U;
    }

    bool msx_plain_32k_lower_rom_window(msx_cartridge_mapper_kind mapper, std::size_t rom_size,
                                        std::uint16_t address) noexcept {
        return mapper == msx_cartridge_mapper_kind::plain && rom_size == 0x8000U &&
               address >= 0x4000U && address < 0x8000U;
    }

    bool msx_plain_32k_lower_handoff_required(std::span<const std::uint8_t> rom) noexcept {
        return rom.size() == 0x8000U &&
               security::cryptography::crc32(rom) == k_a_b_arabic_msx_crc;
    }

    std::uint8_t msx_ascii16_bank_register(std::uint16_t address) noexcept {
        if (address >= 0x6000U && address < 0x6800U) {
            return 0U;
        }
        if (address >= 0x7000U && address < 0x7800U) {
            return 1U;
        }
        return 0xFFU;
    }

    std::uint16_t mirror_msx_konami_address(std::uint16_t address) noexcept {
        if (address < 0x2000U) {
            return static_cast<std::uint16_t>(0x8000U + address);
        }
        if (address < 0x4000U) {
            return static_cast<std::uint16_t>(0xA000U + (address - 0x2000U));
        }
        if (address >= 0xC000U && address < 0xE000U) {
            return static_cast<std::uint16_t>(0x4000U + (address - 0xC000U));
        }
        if (address >= 0xE000U) {
            return static_cast<std::uint16_t>(0x6000U + (address - 0xE000U));
        }
        return address;
    }

} // namespace mnemos::manifests::common
