#include "amiga500_adapter.hpp"
#include "c64_adapter.hpp"
#include "capability_discovery.hpp"
#include "capcom_cps1_adapter.hpp"
#include "capcom_cps2_adapter.hpp"
#include "genesis_adapter.hpp"
#include "irem_m107_adapter.hpp"
#include "irem_m15_adapter.hpp"
#include "irem_m27_adapter.hpp"
#include "irem_m47_adapter.hpp"
#include "irem_m52_adapter.hpp"
#include "irem_m58_adapter.hpp"
#include "irem_travrusa_adapter.hpp"
#include "irem_m72_adapter.hpp"
#include "irem_m75_adapter.hpp"
#include "irem_m81_adapter.hpp"
#include "irem_m82_adapter.hpp"
#include "irem_m84_adapter.hpp"
#include "irem_m90_adapter.hpp"
#include "irem_m92_adapter.hpp"
#include "msx_adapter.hpp"
#include "sega32x_adapter.hpp"
#include "segacd_adapter.hpp"
#include "sms_adapter.hpp"
#include "taito_f2_adapter.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

    namespace amiga500 = mnemos::apps::player::adapters::amiga500;
    namespace c64 = mnemos::apps::player::adapters::c64;
    namespace cps1 = mnemos::apps::player::adapters::capcom_cps1;
    namespace cps2 = mnemos::apps::player::adapters::capcom_cps2;
    namespace genesis = mnemos::apps::player::adapters::genesis;
    namespace irem_m15 = mnemos::apps::player::adapters::irem_m15;
    namespace irem_m27 = mnemos::apps::player::adapters::irem_m27;
    namespace irem_m47 = mnemos::apps::player::adapters::irem_m47;
    namespace irem_m52 = mnemos::apps::player::adapters::irem_m52;
    namespace irem_m58 = mnemos::apps::player::adapters::irem_m58;
    namespace irem_travrusa = mnemos::apps::player::adapters::irem_travrusa;
    namespace irem_m72 = mnemos::apps::player::adapters::irem_m72;
    namespace irem_m75 = mnemos::apps::player::adapters::irem_m75;
    namespace irem_m81 = mnemos::apps::player::adapters::irem_m81;
    namespace irem_m82 = mnemos::apps::player::adapters::irem_m82;
    namespace irem_m84 = mnemos::apps::player::adapters::irem_m84;
    namespace irem_m90 = mnemos::apps::player::adapters::irem_m90;
    namespace irem_m92 = mnemos::apps::player::adapters::irem_m92;
    namespace irem_m107 = mnemos::apps::player::adapters::irem_m107;
    namespace msx = mnemos::apps::player::adapters::msx;
    namespace sega32x = mnemos::apps::player::adapters::sega32x;
    namespace segacd = mnemos::apps::player::adapters::segacd;
    namespace sms = mnemos::apps::player::adapters::sms;
    namespace taito_f2 = mnemos::apps::player::adapters::taito_f2;

    [[nodiscard]] std::vector<std::uint8_t> genesis_cart() {
        std::vector<std::uint8_t> rom(0x100U, 0x00U);
        const auto w16 = [&](std::size_t off, std::uint16_t v) {
            rom[off] = static_cast<std::uint8_t>(v >> 8U);
            rom[off + 1U] = static_cast<std::uint8_t>(v);
        };
        const auto w32 = [&](std::size_t off, std::uint32_t v) {
            rom[off + 0U] = static_cast<std::uint8_t>(v >> 24U);
            rom[off + 1U] = static_cast<std::uint8_t>(v >> 16U);
            rom[off + 2U] = static_cast<std::uint8_t>(v >> 8U);
            rom[off + 3U] = static_cast<std::uint8_t>(v);
        };
        w32(0x00U, 0x00FFF000U);
        w32(0x04U, 0x00000008U);
        w16(0x08U, 0x46FCU); // MOVE.W #$2000,SR
        w16(0x0AU, 0x2000U);
        w16(0x0CU, 0x60FEU); // BRA.S *
        return rom;
    }

    [[nodiscard]] std::vector<std::uint8_t> sms_cart() {
        std::vector<std::uint8_t> rom(0x8000U, 0x00U);
        rom[0x0000U] = 0xF3U; // DI
        rom[0x0001U] = 0x76U; // HALT
        return rom;
    }

    [[nodiscard]] std::vector<std::uint8_t> c64_basic_rom() {
        return std::vector<std::uint8_t>(0x2000U, 0x00U);
    }

    [[nodiscard]] std::vector<std::uint8_t> c64_kernal_rom() {
        return std::vector<std::uint8_t>(0x2000U, 0x00U);
    }

    [[nodiscard]] std::vector<std::uint8_t> c64_chargen_rom() {
        return std::vector<std::uint8_t>(0x1000U, 0x00U);
    }

    [[nodiscard]] std::vector<std::uint8_t> c64_prg() {
        std::vector<std::uint8_t> prg = {0x01U, 0x08U};
        for (int i = 0; i < 64; ++i) {
            prg.push_back(static_cast<std::uint8_t>(i));
        }
        return prg;
    }

    [[nodiscard]] std::vector<std::uint8_t> segacd_bios() {
        std::vector<std::uint8_t> bios(0x20000U, 0x00U);
        bios[0] = 0x00U;
        bios[1] = 0xFFU;
        bios[2] = 0x00U;
        bios[3] = 0x00U; // SSP = $00FF0000
        bios[4] = 0x00U;
        bios[5] = 0x00U;
        bios[6] = 0x02U;
        bios[7] = 0x00U; // PC = $000200
        bios[0x200U] = 0x60U;
        bios[0x201U] = 0xFEU; // BRA *
        return bios;
    }

    [[nodiscard]] std::vector<std::uint8_t> sega32x_cart() {
        std::vector<std::uint8_t> cart(0x10000U, 0x00U);
        cart[0] = 0x00U;
        cart[1] = 0xFFU;
        cart[6] = 0x02U;
        cart[0x200U] = 0x60U;
        cart[0x201U] = 0xFEU;
        return cart;
    }

    void poke16_be(std::vector<std::uint8_t>& bytes, std::size_t at, std::uint16_t value) {
        bytes[at + 0U] = static_cast<std::uint8_t>(value >> 8U);
        bytes[at + 1U] = static_cast<std::uint8_t>(value);
    }

    void poke32_be(std::vector<std::uint8_t>& bytes, std::size_t at, std::uint32_t value) {
        bytes[at + 0U] = static_cast<std::uint8_t>(value >> 24U);
        bytes[at + 1U] = static_cast<std::uint8_t>(value >> 16U);
        bytes[at + 2U] = static_cast<std::uint8_t>(value >> 8U);
        bytes[at + 3U] = static_cast<std::uint8_t>(value);
    }

    [[nodiscard]] std::vector<std::uint8_t> amiga500_kickstart() {
        std::vector<std::uint8_t> rom(
            mnemos::manifests::amiga500::amiga500_system::kickstart_window_size, 0x00U);
        poke32_be(rom, 0x00U, 0x0007F000U);
        poke32_be(rom, 0x04U, mnemos::manifests::amiga500::amiga500_system::kickstart_base + 8U);
        poke16_be(rom, 0x08U, 0x46FCU);
        poke16_be(rom, 0x0AU, 0x2700U);
        poke16_be(rom, 0x0CU, 0x60FEU);
        return rom;
    }

    [[nodiscard]] std::vector<std::uint8_t> amiga500_adf() {
        return std::vector<std::uint8_t>(
            mnemos::manifests::amiga500::amiga500_system::floppy_dd_size, 0x00U);
    }

    [[nodiscard]] std::vector<std::uint8_t> cps1_program() {
        std::vector<std::uint8_t> rom(mnemos::manifests::capcom_cps1::main_rom_size, 0xFFU);
        poke32_be(rom, 0x0U, 0x00FF0000U);
        poke32_be(rom, 0x4U, 0x00000400U);
        poke16_be(rom, 0x400U, 0x33FCU);
        poke16_be(rom, 0x402U, 0x4242U);
        poke32_be(rom, 0x404U, 0x00FF0000U);
        poke16_be(rom, 0x408U, 0x60FEU);
        return rom;
    }

    [[nodiscard]] std::vector<std::uint8_t> cps2_program() {
        return std::vector<std::uint8_t>(0x40U, 0x00U);
    }

    [[nodiscard]] std::vector<std::uint8_t> irem_m72_program() {
        std::vector<std::uint8_t> rom(mnemos::manifests::irem_m72::main_rom_size, 0xFFU);
        rom[0xFFFF0U] = 0xEAU; // JMP 0000:0200
        rom[0xFFFF1U] = 0x00U;
        rom[0xFFFF2U] = 0x02U;
        rom[0xFFFF3U] = 0x00U;
        rom[0xFFFF4U] = 0x00U;
        const std::vector<std::uint8_t> program{0xB8U, 0x00U, 0xA0U, 0x8EU, 0xD8U, 0xB0U,
                                                0x42U, 0xA2U, 0x00U, 0x00U, 0xF4U};
        for (std::size_t i = 0; i < program.size(); ++i) {
            rom[0x200U + i] = program[i];
        }
        return rom;
    }

    [[nodiscard]] std::vector<std::uint8_t> irem_m52_program() {
        std::vector<std::uint8_t> rom(mnemos::manifests::irem_m52::main_rom_size, 0x00U);
        const std::vector<std::uint8_t> program{0x3EU, 0x77U, 0x32U, 0x00U, 0x80U,
                                                0xD3U, 0x10U, 0xC3U, 0x00U, 0x00U};
        for (std::size_t i = 0; i < program.size(); ++i) {
            rom[i] = program[i];
        }
        return rom;
    }

    [[nodiscard]] std::vector<std::uint8_t> irem_m47_program() {
        std::vector<std::uint8_t> rom(mnemos::manifests::irem_m47::main_rom_size, 0x00U);
        const std::vector<std::uint8_t> program{0x3EU, 0x77U, 0x32U, 0x00U, 0x80U,
                                                0xD3U, 0x10U, 0xC3U, 0x00U, 0x00U};
        for (std::size_t i = 0; i < program.size(); ++i) {
            rom[i] = program[i];
        }
        return rom;
    }

    [[nodiscard]] std::vector<std::uint8_t> irem_m58_program() {
        std::vector<std::uint8_t> rom(mnemos::manifests::irem_m58::main_rom_size, 0x00U);
        const std::vector<std::uint8_t> program{0x3EU, 0x77U, 0x32U, 0x00U, 0x80U,
                                                0xD3U, 0x10U, 0xC3U, 0x00U, 0x00U};
        for (std::size_t i = 0; i < program.size(); ++i) {
            rom[i] = program[i];
        }
        return rom;
    }

    [[nodiscard]] std::vector<std::uint8_t> irem_travrusa_program() {
        std::vector<std::uint8_t> rom(mnemos::manifests::irem_travrusa::main_rom_size, 0x00U);
        const std::vector<std::uint8_t> program{
            0x3EU, 0x77U, 0x32U, 0x00U, 0x80U, // LD A,$77 ; LD ($8000),A
            0x3EU, 0x2AU, 0x32U, 0x00U, 0x90U, // LD A,$2A ; LD ($9000),A
            0x3EU, 0x01U, 0x32U, 0x00U, 0xA0U, // LD A,$01 ; LD ($A000),A
            0x3EU, 0x01U, 0x32U, 0x00U, 0xD0U, // LD A,$01 ; LD ($D000),A
            0xC3U, 0x00U, 0x00U};
        for (std::size_t i = 0; i < program.size(); ++i) {
            rom[i] = program[i];
        }
        return rom;
    }

    [[nodiscard]] std::vector<std::uint8_t> irem_m82_program() {
        std::vector<std::uint8_t> rom(mnemos::manifests::irem_m82::main_rom_size, 0xFFU);
        rom[0xFFFF0U] = 0xEAU; // JMP 0000:0200
        rom[0xFFFF1U] = 0x00U;
        rom[0xFFFF2U] = 0x02U;
        rom[0xFFFF3U] = 0x00U;
        rom[0xFFFF4U] = 0x00U;
        const std::vector<std::uint8_t> program{0xB8U, 0x00U, 0xA0U, 0x8EU, 0xD8U, 0xB0U,
                                                0x42U, 0xA2U, 0x00U, 0x00U, 0xF4U};
        for (std::size_t i = 0; i < program.size(); ++i) {
            rom[0x200U + i] = program[i];
        }
        return rom;
    }

    [[nodiscard]] std::vector<std::uint8_t> irem_m75_program() {
        std::vector<std::uint8_t> rom(mnemos::manifests::irem_m75::main_rom_size, 0xFFU);
        const std::vector<std::uint8_t> program{0x3EU, 0x42U, 0x32U, 0x00U, 0xE0U, 0x76U};
        for (std::size_t i = 0; i < program.size(); ++i) {
            rom[i] = program[i];
        }
        return rom;
    }

    [[nodiscard]] std::vector<std::uint8_t> irem_m15_program() {
        std::vector<std::uint8_t> rom(mnemos::manifests::irem_m15::main_rom_size, 0xFFU);
        const std::vector<std::uint8_t> program{0xA9U, 0x42U, 0x8DU, 0x00U, 0x00U, 0xA9U, 0x81U,
                                                0x8DU, 0x00U, 0x40U, 0x4CU, 0x0AU, 0x10U};
        for (std::size_t i = 0; i < program.size(); ++i) {
            rom[mnemos::manifests::irem_m15::program_rom_base + i] = program[i];
        }
        rom[0xFFFCU] = static_cast<std::uint8_t>(mnemos::manifests::irem_m15::program_rom_base);
        rom[0xFFFDU] =
            static_cast<std::uint8_t>(mnemos::manifests::irem_m15::program_rom_base >> 8U);
        rom[0xFFFEU] = static_cast<std::uint8_t>(mnemos::manifests::irem_m15::program_rom_base);
        rom[0xFFFFU] =
            static_cast<std::uint8_t>(mnemos::manifests::irem_m15::program_rom_base >> 8U);
        return rom;
    }

    [[nodiscard]] std::vector<std::uint8_t> irem_m27_program() {
        std::vector<std::uint8_t> rom(mnemos::manifests::irem_m27::main_rom_size, 0xFFU);
        const std::vector<std::uint8_t> program{0xA9U, 0x42U, 0x8DU, 0x00U, 0x00U, 0xA9U, 0x81U,
                                                0x8DU, 0x00U, 0x20U, 0x4CU, 0x0AU, 0x80U};
        for (std::size_t i = 0; i < program.size(); ++i) {
            rom[mnemos::manifests::irem_m27::program_rom_base + i] = program[i];
        }
        rom[0xFFFCU] = static_cast<std::uint8_t>(mnemos::manifests::irem_m27::program_rom_base);
        rom[0xFFFDU] =
            static_cast<std::uint8_t>(mnemos::manifests::irem_m27::program_rom_base >> 8U);
        rom[0xFFFEU] = static_cast<std::uint8_t>(mnemos::manifests::irem_m27::program_rom_base);
        rom[0xFFFFU] =
            static_cast<std::uint8_t>(mnemos::manifests::irem_m27::program_rom_base >> 8U);
        return rom;
    }

    [[nodiscard]] std::vector<std::uint8_t> irem_m81_program() {
        std::vector<std::uint8_t> rom(mnemos::manifests::irem_m81::main_rom_size, 0xFFU);
        rom[0xFFFF0U] = 0xEAU; // JMP 0000:0200
        rom[0xFFFF1U] = 0x00U;
        rom[0xFFFF2U] = 0x02U;
        rom[0xFFFF3U] = 0x00U;
        rom[0xFFFF4U] = 0x00U;
        const std::vector<std::uint8_t> program{0xB8U, 0x00U, 0xA0U, 0x8EU, 0xD8U, 0xB0U,
                                                0x42U, 0xA2U, 0x00U, 0x00U, 0xF4U};
        for (std::size_t i = 0; i < program.size(); ++i) {
            rom[0x200U + i] = program[i];
        }
        return rom;
    }

    [[nodiscard]] std::vector<std::uint8_t> irem_m84_program() {
        std::vector<std::uint8_t> rom(mnemos::manifests::irem_m84::main_rom_size, 0xFFU);
        rom[0xFFFF0U] = 0xEAU; // JMP 0000:0200
        rom[0xFFFF1U] = 0x00U;
        rom[0xFFFF2U] = 0x02U;
        rom[0xFFFF3U] = 0x00U;
        rom[0xFFFF4U] = 0x00U;
        const std::vector<std::uint8_t> program{0xB8U, 0x00U, 0xA0U, 0x8EU, 0xD8U, 0xB0U,
                                                0x42U, 0xA2U, 0x00U, 0x00U, 0xF4U};
        for (std::size_t i = 0; i < program.size(); ++i) {
            rom[0x200U + i] = program[i];
        }
        return rom;
    }

    [[nodiscard]] std::vector<std::uint8_t> irem_m90_program() {
        std::vector<std::uint8_t> rom(mnemos::manifests::irem_m90::main_rom_size, 0xFFU);
        rom[0xFFFF0U] = 0xEAU; // JMP 0000:0200
        rom[0xFFFF1U] = 0x00U;
        rom[0xFFFF2U] = 0x02U;
        rom[0xFFFF3U] = 0x00U;
        rom[0xFFFF4U] = 0x00U;
        const std::vector<std::uint8_t> program{0xB8U, 0x00U, 0xA0U, 0x8EU, 0xD8U, 0xB0U,
                                                0x42U, 0xA2U, 0x00U, 0x00U, 0xF4U};
        for (std::size_t i = 0; i < program.size(); ++i) {
            rom[0x200U + i] = program[i];
        }
        return rom;
    }

    [[nodiscard]] std::vector<std::uint8_t> irem_m107_program() {
        std::vector<std::uint8_t> rom(mnemos::manifests::irem_m107::main_rom_size, 0xFFU);
        rom[0xFFFF0U] = 0xEAU; // JMP 0000:0200
        rom[0xFFFF1U] = 0x00U;
        rom[0xFFFF2U] = 0x02U;
        rom[0xFFFF3U] = 0x00U;
        rom[0xFFFF4U] = 0x00U;
        const std::vector<std::uint8_t> program{0xB8U, 0x00U, 0xA0U, 0x8EU, 0xD8U, 0xB0U,
                                                0x42U, 0xA2U, 0x00U, 0x00U, 0xF4U};
        for (std::size_t i = 0; i < program.size(); ++i) {
            rom[0x200U + i] = program[i];
        }
        return rom;
    }

    [[nodiscard]] std::vector<std::uint8_t> irem_m92_program() {
        std::vector<std::uint8_t> rom(mnemos::manifests::irem_m92::main_rom_size, 0xFFU);
        rom[0xFFFF0U] = 0xEAU; // JMP 0000:0200
        rom[0xFFFF1U] = 0x00U;
        rom[0xFFFF2U] = 0x02U;
        rom[0xFFFF3U] = 0x00U;
        rom[0xFFFF4U] = 0x00U;
        const std::vector<std::uint8_t> program{0xB8U, 0x00U, 0xE0U, 0x8EU, 0xD8U, 0xB0U,
                                                0x42U, 0xA2U, 0x00U, 0x00U, 0xF4U};
        for (std::size_t i = 0; i < program.size(); ++i) {
            rom[0x200U + i] = program[i];
        }
        return rom;
    }

    [[nodiscard]] std::vector<std::uint8_t> taito_f2_program() {
        std::vector<std::uint8_t> rom(mnemos::manifests::taito_f2::main_rom_size, 0xFFU);
        poke32_be(rom, 0x0U,
                  mnemos::manifests::taito_f2::work_ram_base +
                      mnemos::manifests::taito_f2::work_ram_size);
        poke32_be(rom, 0x4U, 0x00000400U);
        poke16_be(rom, 0x400U, 0x33FCU);
        poke16_be(rom, 0x402U, 0x4242U);
        poke32_be(rom, 0x404U, mnemos::manifests::taito_f2::work_ram_base);
        poke16_be(rom, 0x408U, 0x60FEU);
        return rom;
    }

    [[nodiscard]] std::string summary_for(const mnemos::frontend_sdk::player_system& system) {
        return mnemos::debug::format_capability_summary(
            mnemos::debug::discover_dump_capabilities(system));
    }

    void require_line(const std::string& summary, std::string_view needle) {
        INFO("missing capability line: " << needle);
        INFO(summary);
        CHECK(summary.find(needle) != std::string::npos);
    }

    void require_common_session_controls(const std::string& summary,
                                         bool rollback_available = false) {
        require_line(summary, "schema=1 producer=mnemos.debug.capability_discovery@1");
        require_line(summary,
                     "capability session session.input.port.0 state=available control=enabled "
                     "scope=session provider=mnemos.debug.session");
        require_line(summary,
                     "capability session session.mode.local state=available control=enabled "
                     "scope=session provider=mnemos.debug.session");
        require_line(summary,
                     "capability session session.mode.lockstep state=available control=enabled "
                     "scope=session provider=mnemos.debug.session");
        require_line(summary,
                     "capability session session.mode.input_delay state=available control=enabled "
                     "scope=session provider=mnemos.debug.session");
        if (rollback_available) {
            require_line(summary,
                         "capability session session.mode.rollback state=available control=enabled "
                         "scope=session provider=mnemos.debug.session");
        } else {
            require_line(
                summary,
                "capability session session.mode.rollback state=unavailable control=hidden "
                "scope=session provider=mnemos.debug.session");
        }
    }

    void require_degraded_media(const std::string& summary, std::string_view media_id) {
        const std::string line = "capability media " + std::string{media_id} +
                                 " state=degraded control=disabled scope=media "
                                 "provider=mnemos.debug.media";
        require_line(summary, line);
    }

    void require_available_media(const std::string& summary, std::string_view media_id) {
        const std::string line = "capability media " + std::string{media_id} +
                                 " state=available control=enabled scope=media "
                                 "provider=mnemos.debug.media";
        require_line(summary, line);
    }

} // namespace

TEST_CASE("player capability summaries expose real console adapter controls",
          "[player][capabilities]") {
    SECTION("Genesis") {
        genesis::genesis_adapter adapter(genesis_cart(), {}, "Tiny Genesis");
        const auto summary = summary_for(adapter);
        require_common_session_controls(summary);
        require_degraded_media(summary, "media.cart");
    }

    SECTION("Master System") {
        sms::sms_adapter adapter(sms_cart(), {}, "Tiny SMS");
        const auto summary = summary_for(adapter);
        require_common_session_controls(summary);
        require_degraded_media(summary, "media.cart");
    }

    SECTION("Game Gear") {
        sms::sms_adapter adapter(sms_cart(), {.game_gear = true}, "Tiny GG");
        const auto summary = summary_for(adapter);
        require_common_session_controls(summary);
        require_degraded_media(summary, "media.game_card");
    }

    SECTION("Sega CD") {
        segacd::segacd_adapter adapter(segacd_bios(), {}, "Tiny BIOS");
        const auto summary = summary_for(adapter);
        require_common_session_controls(summary);
        require_degraded_media(summary, "media.bios");
    }

    SECTION("Sega 32X") {
        sega32x::sega32x_adapter adapter(sega32x_cart(), {}, {}, "Tiny 32X");
        const auto summary = summary_for(adapter);
        require_common_session_controls(summary);
        require_degraded_media(summary, "media.cart");
    }
}

TEST_CASE("player capability summaries expose computer and arcade adapter controls",
          "[player][capabilities]") {
    SECTION("C64 disk") {
        c64::c64_adapter adapter(c64_basic_rom(), c64_kernal_rom(), c64_chargen_rom(), c64_prg(),
                                 {}, false, {}, "Tiny Disk");
        const auto summary = summary_for(adapter);
        require_common_session_controls(summary);
        require_degraded_media(summary, "media.disk_0");
    }

    SECTION("Amiga 500") {
        amiga500::amiga500_adapter adapter(amiga500_kickstart(), {}, "Tiny Kickstart");
        const auto summary = summary_for(adapter);
        require_common_session_controls(summary);
        require_degraded_media(summary, "media.kickstart");
    }

    SECTION("Amiga 500 disk") {
        std::vector<std::vector<std::uint8_t>> disks;
        disks.push_back(amiga500_adf());
        amiga500::amiga500_adapter adapter(amiga500_kickstart(), {}, "Tiny ADF", std::move(disks));
        const auto summary = summary_for(adapter);
        require_common_session_controls(summary);
        require_degraded_media(summary, "media.disk_0");
    }

    SECTION("Irem M72") {
        irem_m72::irem_m72_adapter adapter(irem_m72_program(), "Tiny M72");
        const auto summary = summary_for(adapter);
        require_common_session_controls(summary, true);
        require_available_media(summary, "media.rom_set");
    }

    SECTION("Irem M75") {
        irem_m75::irem_m75_adapter adapter(irem_m75_program(), "Tiny M75");
        const auto summary = summary_for(adapter);
        require_common_session_controls(summary, true);
        require_available_media(summary, "media.rom_set");
    }

    SECTION("Irem M15") {
        irem_m15::irem_m15_adapter adapter(irem_m15_program(), "Tiny M15");
        const auto summary = summary_for(adapter);
        require_common_session_controls(summary, true);
        require_available_media(summary, "media.rom_set");
    }

    SECTION("Irem M27") {
        irem_m27::irem_m27_adapter adapter(irem_m27_program(), "Tiny M27");
        const auto summary = summary_for(adapter);
        require_common_session_controls(summary, true);
        require_available_media(summary, "media.rom_set");
    }

    SECTION("Irem M52") {
        irem_m52::irem_m52_adapter adapter(irem_m52_program(), "Tiny M52");
        const auto summary = summary_for(adapter);
        require_common_session_controls(summary, true);
        require_available_media(summary, "media.rom_set");
        require_line(summary, "capability memory memory.z80_0.registers state=available");
        require_line(summary, "capability memory memory.z80_1.registers state=available");
        require_line(summary, "capability memory memory.ym2149_0.registers state=available");
        require_line(summary, "capability memory memory.ym2149_1.registers state=available");
        require_line(summary, "capability memory memory.msm5205.registers state=available");
    }

    SECTION("Irem M47") {
        irem_m47::irem_m47_adapter adapter(irem_m47_program(), "Tiny M47");
        const auto summary = summary_for(adapter);
        require_common_session_controls(summary, true);
        require_available_media(summary, "media.rom_set");
        require_line(summary, "capability memory memory.z80_0.registers state=available");
        require_line(summary, "capability memory memory.z80_1.registers state=available");
        require_line(summary, "capability memory memory.ym2149_0.registers state=available");
        require_line(summary, "capability memory memory.ym2149_1.registers state=available");
    }

    SECTION("Irem M58") {
        irem_m58::irem_m58_adapter adapter(irem_m58_program(), "Tiny M58");
        const auto summary = summary_for(adapter);
        require_common_session_controls(summary, true);
        require_available_media(summary, "media.rom_set");
        require_line(summary, "capability memory memory.z80_0.registers state=available");
        require_line(summary, "capability memory memory.z80_1.registers state=available");
        require_line(summary, "capability memory memory.ym2149_0.registers state=available");
        require_line(summary, "capability memory memory.ym2149_1.registers state=available");
    }

    SECTION("Irem Traverse USA") {
        irem_travrusa::irem_travrusa_adapter adapter(irem_travrusa_program(), "Tiny travrusa");
        const auto summary = summary_for(adapter);
        require_common_session_controls(summary, true);
        require_available_media(summary, "media.rom_set");
        require_line(summary, "capability memory memory.z80_0.registers state=available");
        require_line(summary, "capability memory memory.z80_1.registers state=available");
        require_line(summary, "capability memory memory.ym2149_0.registers state=available");
        require_line(summary, "capability memory memory.ym2149_1.registers state=available");
        require_line(summary, "capability memory memory.msm5205.registers state=available");
    }

    SECTION("Irem M81") {
        irem_m81::irem_m81_adapter adapter(irem_m81_program(), "Tiny M81");
        const auto summary = summary_for(adapter);
        require_common_session_controls(summary, true);
        require_available_media(summary, "media.rom_set");
    }

    SECTION("Irem M82") {
        irem_m82::irem_m82_adapter adapter(irem_m82_program(), "Tiny M82");
        const auto summary = summary_for(adapter);
        require_common_session_controls(summary, true);
        require_available_media(summary, "media.rom_set");
    }

    SECTION("Irem M84") {
        irem_m84::irem_m84_adapter adapter(irem_m84_program(), "Tiny M84");
        const auto summary = summary_for(adapter);
        require_common_session_controls(summary, true);
        require_available_media(summary, "media.rom_set");
    }

    SECTION("Irem M90") {
        irem_m90::irem_m90_adapter adapter(irem_m90_program(), "Tiny M90");
        const auto summary = summary_for(adapter);
        require_common_session_controls(summary, true);
        require_available_media(summary, "media.rom_set");
    }

    SECTION("Irem M107") {
        irem_m107::irem_m107_adapter adapter(irem_m107_program(), "Tiny M107");
        const auto summary = summary_for(adapter);
        require_common_session_controls(summary, true);
        require_available_media(summary, "media.rom_set");
    }

    SECTION("Irem M92") {
        irem_m92::irem_m92_adapter adapter(irem_m92_program(), "Tiny M92");
        const auto summary = summary_for(adapter);
        require_common_session_controls(summary, true);
        require_available_media(summary, "media.rom_set");
    }

    SECTION("Taito F2") {
        taito_f2::taito_f2_adapter adapter(taito_f2_program(), "Tiny Taito F2");
        const auto summary = summary_for(adapter);
        require_common_session_controls(summary);
        require_degraded_media(summary, "media.rom_set");
    }

    SECTION("Capcom CPS1") {
        cps1::capcom_cps1_adapter adapter(cps1_program(), "Tiny CPS1");
        const auto summary = summary_for(adapter);
        require_common_session_controls(summary);
        require_degraded_media(summary, "media.rom_set");
    }

    SECTION("MSX") {
        msx::msx_adapter adapter(std::vector<std::uint8_t>(0x8000U, 0x00U),
                                 std::vector<std::uint8_t>(0x4000U, 0xFFU), {}, "Tiny MSX");
        const auto summary = summary_for(adapter);
        require_common_session_controls(summary);
        require_degraded_media(summary, "media.bios");
        require_degraded_media(summary, "media.cart");
    }

    SECTION("Capcom CPS2") {
        cps2::capcom_cps2_adapter adapter(cps2_program(), "Tiny CPS2");
        const auto summary = summary_for(adapter);
        require_common_session_controls(summary, true);
        require_degraded_media(summary, "media.rom_set");
    }
}
