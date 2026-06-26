#include "m72_system.hpp"

#include "rom_set_toml.hpp"
#include "scheduler.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifndef MNEMOS_IREM_M72_GAMES_DIR
#define MNEMOS_IREM_M72_GAMES_DIR ""
#endif

namespace {

    using mnemos::manifests::common::rom_set_image;
    using mnemos::manifests::common::rom_set_decl;
    using mnemos::manifests::common::rom_set_hle_decl;
    using mnemos::manifests::common::rom_set_region;
    using mnemos::manifests::irem_m72::assemble_m72;
    using mnemos::manifests::irem_m72::m72_rom_skeleton;

    // A maincpu region with a far jump at the V30 reset vector (FFFF:0000 ->
    // physical 0xFFFF0) into `program` placed at 0000:0200.
    [[nodiscard]] rom_set_image make_image(const std::vector<std::uint8_t>& program) {
        rom_set_image image;
        auto& main = image.regions["maincpu"];
        main.assign(mnemos::manifests::irem_m72::main_rom_size, 0xFFU);
        main[0xFFFF0U] = 0xEAU; // JMP 0000:0200
        main[0xFFFF1U] = 0x00U;
        main[0xFFFF2U] = 0x02U;
        main[0xFFFF3U] = 0x00U;
        main[0xFFFF4U] = 0x00U;
        for (std::size_t i = 0; i < program.size(); ++i) {
            main[0x200U + i] = program[i];
        }
        return image;
    }

    // Run the main CPU until HLT (bounded).
    void run_until_halt(mnemos::chips::cpu::v30& cpu, int max_instructions) {
        for (int i = 0; i < max_instructions && !cpu.halted(); ++i) {
            cpu.step_instruction();
        }
    }

    [[nodiscard]] std::string read_text_file(const std::filesystem::path& path) {
        std::ifstream in(path, std::ios::binary);
        REQUIRE(in.good());
        std::ostringstream text;
        text << in.rdbuf();
        return text.str();
    }

    [[nodiscard]] const rom_set_region* find_region(const rom_set_decl& decl,
                                                    std::string_view name) noexcept {
        const auto it = std::find_if(decl.regions.begin(), decl.regions.end(),
                                     [name](const rom_set_region& region) {
                                         return region.name == name;
                                     });
        return it == decl.regions.end() ? nullptr : &*it;
    }

    [[nodiscard]] bool has_file_alias(const rom_set_decl& decl,
                                      std::string_view file_name,
                                      std::string_view alias) noexcept {
        for (const auto& region : decl.regions) {
            for (const auto& file : region.files) {
                if (file.name != file_name) {
                    continue;
                }
                if (std::find(file.aliases.begin(), file.aliases.end(), alias) !=
                    file.aliases.end()) {
                    return true;
                }
            }
        }
        return false;
    }

    [[nodiscard]] const rom_set_hle_decl* find_hle(const rom_set_decl& decl,
                                                   std::string_view chip) noexcept {
        const auto it = std::find_if(decl.hle.begin(), decl.hle.end(),
                                     [chip](const rom_set_hle_decl& hle) {
                                         return hle.chip == chip;
                                     });
        return it == decl.hle.end() ? nullptr : &*it;
    }

    [[nodiscard]] bool has_dip(const rom_set_decl& decl,
                               std::string_view name,
                               std::uint16_t mask,
                               std::uint16_t default_value) noexcept {
        return std::any_of(decl.dips.begin(), decl.dips.end(), [&](const auto& dip) {
            return dip.name == name && dip.mask == mask && dip.default_value == default_value;
        });
    }

    [[nodiscard]] bool has_conditioned_dip(const rom_set_decl& decl,
                                           std::string_view name,
                                           std::uint16_t mask,
                                           std::uint16_t condition_mask,
                                           std::uint16_t condition_value) noexcept {
        return std::any_of(decl.dips.begin(), decl.dips.end(), [&](const auto& dip) {
            return dip.name == name && dip.mask == mask && dip.condition.has_value() &&
                   dip.condition->mask == condition_mask &&
                   dip.condition->value == condition_value;
        });
    }

    [[nodiscard]] std::size_t count_dips_named(const rom_set_decl& decl,
                                               std::string_view name) noexcept {
        return static_cast<std::size_t>(
            std::count_if(decl.dips.begin(), decl.dips.end(), [&](const auto& dip) {
                return dip.name == name;
            }));
    }

    void require_region_contract(const rom_set_region& region) {
        CHECK(region.size > 0U);
        REQUIRE_FALSE(region.files.empty());
        for (const auto& file : region.files) {
            INFO("region=" << region.name << " file=" << file.name);
            CHECK_FALSE(file.name.empty());
            CHECK(file.offset < region.size);
            CHECK(file.stride >= 1U);
            CHECK(file.unit >= 1U);
            CHECK(file.size > 0U);
            CHECK(file.crc32.has_value());
            const std::size_t source_bytes = file.length == 0U ? file.size : file.length;
            REQUIRE(source_bytes > 0U);
            const std::size_t chunks = (source_bytes + file.unit - 1U) / file.unit;
            const std::size_t last_start = file.offset + (chunks - 1U) * file.stride;
            CHECK(last_start < region.size);
        }
    }

} // namespace

TEST_CASE("m72 checked-in game manifests parse and cover the phase-E roster", "[m72][romset]") {
    using mnemos::manifests::common::screen_orientation;
    namespace fs = std::filesystem;

    const fs::path games_dir{MNEMOS_IREM_M72_GAMES_DIR};
    REQUIRE_FALSE(games_dir.empty());
    REQUIRE(fs::exists(games_dir));

    std::map<std::string, rom_set_decl, std::less<>> declarations;
    for (const fs::directory_entry& entry : fs::directory_iterator(games_dir)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".toml") {
            continue;
        }
        const std::string text = read_text_file(entry.path());
        auto parsed = mnemos::manifests::common::parse_rom_set_decl(
            text, entry.path().filename().string());
        for (const auto& error : parsed.errors) {
            INFO(error.source << ":" << error.line << ":" << error.column << ": "
                              << error.message);
        }
        REQUIRE(parsed.ok());

        const rom_set_decl& decl = *parsed.value;
        INFO("set=" << decl.name);
        std::string set_name = decl.name;
        declarations.emplace(std::move(set_name), std::move(*parsed.value));
    }

    const std::map<std::string, std::size_t, std::less<>> expected_dip_counts{
        {"airdueljm72", 8U}, {"airduelm72", 8U}, {"bchopper", 13U}, {"dbreedjm72", 11U},
        {"dbreedm72", 11U},  {"dkgensanm72", 11U}, {"gallopm72", 10U}, {"imgfight", 10U},
        {"imgfightj", 10U},  {"imgfightjb", 10U},  {"loht", 11U},      {"lohtb2", 11U},
        {"lohtb3", 11U},     {"lohtj", 11U},       {"mrheli", 13U},    {"nspirit", 12U},
        {"nspiritj", 12U},   {"rtype", 13U},       {"rtypeb", 13U},    {"rtypej", 13U},
        {"rtypejp", 13U},    {"rtypeu", 13U},      {"xmultiplm72", 12U},
    };

    std::set<std::string> names;
    for (const auto& [set_name, raw_decl] : declarations) {
        INFO("set=" << set_name);
        rom_set_decl decl = raw_decl;
        if (decl.parent.has_value()) {
            const auto parent_it = declarations.find(*decl.parent);
            REQUIRE(parent_it != declarations.end());
            decl = mnemos::manifests::common::inherit_parent_regions(parent_it->second,
                                                                     std::move(decl));
        }
        names.insert(decl.name);
        CHECK(decl.board == "irem_m72");
        const auto board_params = mnemos::manifests::irem_m72::board_params_for(decl.name);
        CHECK(board_params.dip_default != 0xFFFFU);
        REQUIRE(find_region(decl, "maincpu") != nullptr);
        CHECK(find_region(decl, "maincpu")->size == mnemos::manifests::irem_m72::main_rom_size);
        require_region_contract(*find_region(decl, "maincpu"));

        REQUIRE(find_region(decl, "sprites") != nullptr);
        REQUIRE(find_region(decl, "tiles_a") != nullptr);
        REQUIRE(find_region(decl, "tiles_b") != nullptr);
        require_region_contract(*find_region(decl, "sprites"));
        require_region_contract(*find_region(decl, "tiles_a"));
        require_region_contract(*find_region(decl, "tiles_b"));

        const bool protected_set = !decl.name.starts_with("rtype");
        if (protected_set) {
            const rom_set_region* mcu_region = find_region(decl, "mcu");
            const rom_set_hle_decl* mcu_hle = find_hle(decl, "mcu");
            REQUIRE((mcu_region != nullptr || mcu_hle != nullptr));
            REQUIRE(find_region(decl, "samples") != nullptr);
            if (mcu_region != nullptr) {
                require_region_contract(*mcu_region);
            }
            if (mcu_hle != nullptr) {
                CHECK_FALSE(mcu_hle->profile.empty());
                CHECK(mnemos::manifests::irem_m72::supported_protection_hle_profile(
                    mcu_hle->profile));
                CHECK_FALSE(mcu_hle->rationale.empty());
            }
            require_region_contract(*find_region(decl, "samples"));
        } else {
            CHECK(find_region(decl, "mcu") == nullptr);
            CHECK(find_hle(decl, "mcu") == nullptr);
            CHECK(find_region(decl, "samples") == nullptr);
        }

        if (decl.name == "mrheli") {
            REQUIRE(decl.parent.has_value());
            CHECK(*decl.parent == "bchopper");
        }
        if (decl.name == "nspiritj") {
            REQUIRE(decl.parent.has_value());
            CHECK(*decl.parent == "nspirit");
        }
        if (decl.name == "lohtj" || decl.name == "lohtb2" || decl.name == "lohtb3") {
            REQUIRE(decl.parent.has_value());
            CHECK(*decl.parent == "loht");
        }
        if (decl.name == "rtypej" || decl.name == "rtypejp" || decl.name == "rtypeu" ||
            decl.name == "rtypeb") {
            REQUIRE(decl.parent.has_value());
            CHECK(*decl.parent == "rtype");
        }
        if (decl.name == "imgfightj" || decl.name == "imgfightjb") {
            REQUIRE(decl.parent.has_value());
            CHECK(*decl.parent == "imgfight");
        }
        if (decl.name == "airdueljm72") {
            REQUIRE(decl.parent.has_value());
            CHECK(*decl.parent == "airduelm72");
        }
        if (decl.name == "dbreedjm72") {
            REQUIRE(decl.parent.has_value());
            CHECK(*decl.parent == "dbreedm72");
            CHECK(find_hle(decl, "mcu") == nullptr);
            REQUIRE(find_region(decl, "mcu") != nullptr);
        }
        if (decl.name == "imgfight" || decl.name == "imgfightj" || decl.name == "imgfightjb" ||
            decl.name == "airduelm72" || decl.name == "airdueljm72") {
            CHECK(decl.orientation == screen_orientation::vertical);
        } else {
            CHECK(decl.orientation == screen_orientation::horizontal);
        }
        const auto expected_dip_count = expected_dip_counts.find(decl.name);
        REQUIRE(expected_dip_count != expected_dip_counts.end());
        CHECK(decl.dips.size() == expected_dip_count->second);
        if (decl.name == "dbreedm72") {
            const rom_set_hle_decl* mcu_hle = find_hle(decl, "mcu");
            REQUIRE(mcu_hle != nullptr);
            CHECK(mcu_hle->profile == "irem_m72.dbreedm72_no_dump_mcu");
            REQUIRE(mcu_hle->sample_triggers.size() == 9U);
            CHECK(mcu_hle->sample_triggers[6].trigger == 6U);
            CHECK(mcu_hle->sample_triggers[6].start == 0x13000U);
        }
        if (decl.name == "rtype") {
            REQUIRE_FALSE(decl.dips.empty());
            CHECK(decl.dips.front().name == "Lives");
            CHECK(decl.dips.front().default_value == 0x0003U);
            CHECK(std::any_of(decl.dips.begin(), decl.dips.end(), [](const auto& dip) {
                return dip.name == "Coinage" && dip.condition.has_value() &&
                       dip.condition->mask == 0x0400U && dip.condition->value == 0x0400U;
            }));
            CHECK(std::any_of(decl.dips.begin(), decl.dips.end(), [](const auto& dip) {
                return dip.name == "Coin B" && dip.condition.has_value() &&
                       dip.condition->mask == 0x0400U && dip.condition->value == 0x0000U;
            }));
        }
        if (decl.name == "bchopper" || decl.name == "mrheli") {
            CHECK(has_dip(decl, "Lives", 0x0003U, 0x0003U));
            CHECK(has_conditioned_dip(decl, "Coinage", 0x00f0U, 0x0400U, 0x0400U));
        }
        if (decl.name == "imgfight" || decl.name == "imgfightj" ||
            decl.name == "imgfightjb") {
            CHECK(has_dip(decl, "Demo Sounds", 0x0800U, 0x0000U));
        }
        if (decl.name == "xmultiplm72") {
            CHECK(count_dips_named(decl, "Cabinet") == 2U);
            CHECK(has_conditioned_dip(decl, "Cabinet", 0x0200U, 0x1000U, 0x1000U));
            CHECK(has_conditioned_dip(decl, "Cabinet", 0x0200U, 0x1000U, 0x0000U));
        }
        if (decl.name == "airduelm72" || decl.name == "airdueljm72") {
            CHECK(has_conditioned_dip(decl, "Coinage", 0xf000U, 0x0800U, 0x0800U));
        }
        if (decl.name == "dkgensanm72") {
            const rom_set_hle_decl* mcu_hle = find_hle(decl, "mcu");
            REQUIRE(mcu_hle != nullptr);
            CHECK(mcu_hle->profile == "irem_m72.dkgensanm72_no_dump_mcu");
            REQUIRE(mcu_hle->sample_triggers.size() == 28U);
            CHECK(mcu_hle->sample_triggers[20].trigger == 20U);
            CHECK(mcu_hle->sample_triggers[20].start == 0x12B20U);
            CHECK(has_dip(decl, "Continue Limit", 0x0010U, 0x0010U));
        }
        if (decl.name == "gallopm72") {
            CHECK(has_file_alias(decl, "cc_c-h0-.ic40", "cc-c-h0.bin"));
            CHECK(has_file_alias(decl, "cc_c-00.ic53", "cc-c-00.bin"));
            CHECK(has_file_alias(decl, "cc_b-a0.ic21", "cc-b-a0.bin"));
            CHECK(has_file_alias(decl, "cc_b-b0.ic26", "cc-b-b0.bin"));
            CHECK(has_file_alias(decl, "cc_c-v0.ic44", "cc-c-v0.bin"));
        }
        if (decl.name == "nspirit") {
            CHECK(has_file_alias(decl, "nin_c-h0-b.ic40", "nin_c-h0.6h"));
            CHECK(has_file_alias(decl, "nin-r00.ic53", "nin-r00.7m"));
            CHECK(has_file_alias(decl, "nin_b-a0.ic21", "nin_b-a0.4c"));
            CHECK(has_file_alias(decl, "b0.ic26", "b0.4j"));
            CHECK(has_file_alias(decl, "nin-v0.ic44", "nin-v0.7a"));
        }
    }

    CHECK(names.contains("rtype"));
    CHECK(names.contains("rtypej"));
    CHECK(names.contains("rtypejp"));
    CHECK(names.contains("rtypeu"));
    CHECK(names.contains("rtypeb"));
    CHECK(names.contains("bchopper"));
    CHECK(names.contains("mrheli"));
    CHECK(names.contains("nspirit"));
    CHECK(names.contains("nspiritj"));
    CHECK(names.contains("loht"));
    CHECK(names.contains("lohtj"));
    CHECK(names.contains("lohtb2"));
    CHECK(names.contains("lohtb3"));
    CHECK(names.contains("imgfight"));
    CHECK(names.contains("imgfightj"));
    CHECK(names.contains("imgfightjb"));
    CHECK(names.contains("airduelm72"));
    CHECK(names.contains("airdueljm72"));
    CHECK(names.contains("xmultiplm72"));
    CHECK(names.contains("dbreedm72"));
    CHECK(names.contains("dbreedjm72"));
    CHECK(names.contains("dkgensanm72"));
    CHECK(names.contains("gallopm72"));
}

TEST_CASE("m72 boots a synthetic program from the reset vector", "[m72]") {
    // MOV AX,A000; MOV DS,AX; MOV AL,77; MOV [0010],AL; HLT
    auto system = assemble_m72(
        make_image({0xB8U, 0x00U, 0xA0U, 0x8EU, 0xD8U, 0xB0U, 0x77U, 0xA2U, 0x10U, 0x00U, 0xF4U}));

    run_until_halt(system->main_cpu, 16);
    CHECK(system->main_cpu.halted());
    // DS=A000 offset 0x10 = physical 0xA0010, the base map's work-RAM overlay.
    CHECK(system->main_bus.read8(0xA0010U) == 0x77U);
    CHECK(system->work_ram[0x10U] == 0x77U);
}

TEST_CASE("m72 board params place the work RAM per declared set", "[m72]") {
    using mnemos::manifests::irem_m72::board_params_for;
    CHECK(board_params_for("rtype").work_ram_base == 0x40000U);
    CHECK(board_params_for("rtypej").work_ram_base == 0x40000U);
    CHECK(board_params_for("bchopper").work_ram_base == 0xA0000U);
    CHECK(board_params_for("imgfightj").work_ram_base == 0xA0000U);
    CHECK(board_params_for("airduelm72").work_ram_base == 0xA0000U);
    CHECK(board_params_for("xmultiplm72").work_ram_base == 0x80000U);
    CHECK(board_params_for("dbreedjm72").work_ram_base == 0x90000U);
    CHECK(board_params_for("rtype").dip_default == 0xFDFBU);
    CHECK(board_params_for("rtypejp").dip_default == 0xFDFFU);
    CHECK(board_params_for("bchopper").dip_default == 0xFDFFU);
    CHECK(board_params_for("mrheli").dip_default == 0xFDFFU);
    CHECK(board_params_for("nspirit").dip_default == 0xF5FFU);
    CHECK(board_params_for("imgfightj").dip_default == 0xF5FFU);
    CHECK(board_params_for("imgfightjb").dip_default == 0xF5FFU);
    CHECK(board_params_for("lohtb2").dip_default == 0xFDFBU);
    CHECK(board_params_for("lohtb3").dip_default == 0xFDFBU);
    CHECK(board_params_for("xmultiplm72").dip_default == 0xFDFFU);
    CHECK(board_params_for("dbreedm72").dip_default == 0xF5FFU);
    CHECK(board_params_for("dkgensanm72").dip_default == 0xFDBFU);
    CHECK(board_params_for("airduelm72").dip_default == 0xFFBFU);
    CHECK(board_params_for("gallopm72").dip_default == 0xF9BFU);
    CHECK(board_params_for("unknown").work_ram_base == 0xA0000U);

    // MOV AX,4000; MOV DS,AX; MOV AL,55; MOV [0010],AL; HLT
    auto system = assemble_m72(
        make_image({0xB8U, 0x00U, 0x40U, 0x8EU, 0xD8U, 0xB0U, 0x55U, 0xA2U, 0x10U, 0x00U, 0xF4U}),
        board_params_for("rtype"));
    run_until_halt(system->main_cpu, 16);
    CHECK(system->work_ram[0x10U] == 0x55U);
    CHECK(system->dip_switches == 0xFDFBU);
}

TEST_CASE("m72 sound latch crosses from the V30 to the Z80 and acks via port 6", "[m72]") {
    // Main: MOV AL,5A; OUT 00,AL; HLT
    auto system = assemble_m72(make_image({0xB0U, 0x5AU, 0xE6U, 0x00U, 0xF4U}));

    // Z80 program: IN A,(02); LD (F800),A; OUT (06),A; HALT
    const std::vector<std::uint8_t> z80_program{0xDBU, 0x02U, 0x32U, 0x00U,
                                                0xF8U, 0xD3U, 0x06U, 0x76U};
    for (std::size_t i = 0; i < z80_program.size(); ++i) {
        system->sound_ram[i] = z80_program[i];
    }
    system->sound_cpu.set_reset_line(false); // upload done, run

    run_until_halt(system->main_cpu, 16);
    CHECK(system->sound_latch == 0x5AU);
    CHECK(system->sound_latch_irq); // pending until the explicit acknowledge

    system->sound_cpu.step_instruction(); // IN A,(02) -- read does NOT ack
    CHECK(system->sound_latch_irq);
    system->sound_cpu.step_instruction(); // LD (F800),A
    system->sound_cpu.step_instruction(); // OUT (06),A -- the acknowledge
    CHECK_FALSE(system->sound_latch_irq);
    system->sound_cpu.step_instruction(); // HALT
    CHECK(system->sound_bus.read8(0xF800U) == 0x5AU);
    CHECK(system->sound_ram[0xF800U] == 0x5AU);
}

TEST_CASE("m72 V30 uploads the sound program through the shared-RAM window and releases the Z80",
          "[m72]") {
    // The hardware boot path: at power-on the Z80 is parked in reset; the V30
    // writes its program through 0xE0000 and raises control bit 4.
    // Main: MOV AX,E000; MOV DS,AX; MOV AL,3E (LD A,n); MOV [0000],AL;
    //       MOV AL,77; MOV [0001],AL; MOV AL,76 (HALT); MOV [0002],AL;
    //       MOV AL,10; OUT 02,AL; HLT
    auto system = assemble_m72(make_image({
        0xB8U, 0x00U, 0xE0U, // MOV AX,E000
        0x8EU, 0xD8U,        // MOV DS,AX
        0xB0U, 0x3EU,        // MOV AL,3E
        0xA2U, 0x00U, 0x00U, // MOV [0000],AL
        0xB0U, 0x77U,        // MOV AL,77
        0xA2U, 0x01U, 0x00U, // MOV [0001],AL
        0xB0U, 0x76U,        // MOV AL,76
        0xA2U, 0x02U, 0x00U, // MOV [0002],AL
        0xB0U, 0x10U,        // MOV AL,10 (bit 4: release the sound CPU)
        0xE6U, 0x02U,        // OUT 02,AL
        0xF4U                // HLT
    }));

    CHECK(system->sound_cpu.reset_line_held()); // parked at power-on

    // Held: stepping the Z80 does nothing.
    system->sound_cpu.tick(16U);
    CHECK(system->sound_cpu.cpu_registers().pc == 0x0000U);

    run_until_halt(system->main_cpu, 32);
    CHECK(system->sound_ram[0x0000U] == 0x3EU); // upload landed in Z80 space
    CHECK_FALSE(system->sound_cpu.reset_line_held());

    system->sound_cpu.step_instruction(); // LD A,77
    CHECK((system->sound_cpu.cpu_registers().af >> 8U) == 0x77U);
}

TEST_CASE("m72 control register bit 2 drives video flip-screen", "[m72]") {
    auto flipped = assemble_m72(make_image({
        0xB0U, 0x04U, // MOV AL,04
        0xE6U, 0x02U, // OUT 02,AL
        0xF4U         // HLT
    }));
    CHECK_FALSE(flipped->video.flip_screen());

    run_until_halt(flipped->main_cpu, 8);
    CHECK(flipped->control_register == 0x04U);
    CHECK(flipped->video.flip_screen());

    auto cleared = assemble_m72(make_image({
        0xB0U, 0x00U, // MOV AL,00
        0xE6U, 0x02U, // OUT 02,AL
        0xF4U         // HLT
    }));
    cleared->video.set_flip_screen(true);

    run_until_halt(cleared->main_cpu, 8);
    CHECK(cleared->control_register == 0x00U);
    CHECK_FALSE(cleared->video.flip_screen());
}

TEST_CASE("m72 control register bits 0 and 1 pulse the coin counters", "[m72]") {
    auto system = assemble_m72(make_image({
        0xB0U, 0x01U, 0xE6U, 0x02U, // counter 0 rising edge
        0xB0U, 0x01U, 0xE6U, 0x02U, // held high: no extra count
        0xB0U, 0x03U, 0xE6U, 0x02U, // counter 1 rising edge
        0xB0U, 0x02U, 0xE6U, 0x02U, // counter 0 falls
        0xB0U, 0x00U, 0xE6U, 0x02U, // both clear
        0xB0U, 0x03U, 0xE6U, 0x02U, // both rising edges
        0xF4U                        // HLT
    }));

    run_until_halt(system->main_cpu, 32);
    CHECK(system->control_register == 0x03U);
    CHECK(system->coin_counters[0] == 2U);
    CHECK(system->coin_counters[1] == 2U);
}

TEST_CASE("m72 system input port keeps the sprite DMA complete bit asserted", "[m72]") {
    // Main: MOV AX,A000; MOV DS,AX; IN AL,02; MOV [0010],AL; HLT
    auto system = assemble_m72(make_image({
        0xB8U, 0x00U, 0xA0U, // MOV AX,A000
        0x8EU, 0xD8U,        // MOV DS,AX
        0xE4U, 0x02U,        // IN AL,02
        0xA2U, 0x10U, 0x00U, // MOV [0010],AL
        0xF4U,               // HLT
    }));
    system->input_system = 0x00U; // all active-low cabinet bits asserted

    run_until_halt(system->main_cpu, 16);
    CHECK(system->work_ram[0x10U] == 0x80U);
}

TEST_CASE("m72 soundcpu region selects the ROM-backed Z80 map", "[m72]") {
    namespace m72 = mnemos::manifests::irem_m72;

    rom_set_image image;
    auto& sound_rom = image.regions["soundcpu"];
    sound_rom.assign(m72::sound_rom_size, 0x00U);
    sound_rom[0xF010U] = 0x99U; // the public 64 KiB region tail is shadowed by RAM
    // LD A,(F010); LD (F011),A; LD A,7B; LD (F010),A; LD A,(0000); LD (F012),A; HALT
    const std::vector<std::uint8_t> program{
        0x3AU, 0x10U, 0xF0U, 0x32U, 0x11U, 0xF0U, 0x3EU, 0x7BU,
        0x32U, 0x10U, 0xF0U, 0x3AU, 0x00U, 0x00U, 0x32U, 0x12U,
        0xF0U, 0x76U,
    };
    for (std::size_t i = 0; i < program.size(); ++i) {
        sound_rom[i] = program[i];
    }

    auto system = assemble_m72(std::move(image));
    REQUIRE(system->sound_rom_present);
    CHECK_FALSE(system->sound_cpu.reset_line_held());

    CHECK(system->sound_bus.read8(0x0000U) == 0x3AU);
    system->sound_bus.write8(0x0000U, 0x00U);
    CHECK(system->sound_bus.read8(0x0000U) == 0x3AU); // ROM writes are dropped

    for (int i = 0; i < 7; ++i) {
        system->sound_cpu.step_instruction();
    }
    CHECK(system->sound_cpu.cpu_registers().halted);
    CHECK(system->sound_ram[0xF010U] == 0x7BU);
    CHECK(system->sound_ram[0xF011U] == 0x00U); // read back from RAM, not ROM tail
    CHECK(system->sound_ram[0xF012U] == 0x3AU); // read back from ROM byte 0

    system->sound_bus.write8(0xF012U, 0x44U);
    CHECK(system->sound_ram[0xF012U] == 0x44U);

    // ROM-backed sound boards do not expose the R-Type upload window on the
    // V30 bus; the underlying main ROM remains open-bus padded.
    CHECK(system->main_bus.read8(m72::sound_ram_window) == 0xFFU);
    system->main_bus.write8(m72::sound_ram_window + 0x10U, 0xABU);
    CHECK(system->sound_ram[0x0010U] == 0x00U);
}

TEST_CASE("m72 inputs and DIP switches read through the V30 I/O ports", "[m72]") {
    // IN AL,00; MOV BL,AL; IN AL,04; MOV BH,AL; HLT
    auto system =
        assemble_m72(make_image({0xE4U, 0x00U, 0x88U, 0xC3U, 0xE4U, 0x04U, 0x88U, 0xC7U, 0xF4U}));
    system->input_p1 = 0xFEU; // button held (active low)
    system->dip_switches = 0xABCDU;

    run_until_halt(system->main_cpu, 16);
    const auto regs = system->main_cpu.cpu_registers();
    CHECK((regs.bx & 0xFFU) == 0xFEU); // BL = P1 inputs
    CHECK((regs.bx >> 8U) == 0xCDU);   // BH = DSW low byte
}

TEST_CASE("m72 ROM skeleton declares the program region only", "[m72]") {
    // No sound region: the Z80 runs from the RAM the V30 fills at boot.
    const auto decl = m72_rom_skeleton("rtype");
    CHECK(decl.name == "rtype");
    REQUIRE(decl.regions.size() == 1U);
    CHECK(decl.regions[0].name == "maincpu");
    CHECK(decl.regions[0].size == mnemos::manifests::irem_m72::main_rom_size);
}

TEST_CASE("m72 pads a missing program region to open bus; sound RAM reads zero", "[m72]") {
    auto system = assemble_m72(rom_set_image{});
    CHECK(system->main_bus.read8(0x00000U) == 0xFFU);
    CHECK(system->main_bus.read8(0xFFFF0U) == 0xFFU);
    CHECK(system->sound_bus.read8(0x0000U) == 0x00U); // RAM, not ROM
    // The V30's window and the Z80's address space are the same memory.
    system->main_bus.write8(mnemos::manifests::irem_m72::sound_ram_window + 0x1234U, 0xABU);
    CHECK(system->sound_bus.read8(0x1234U) == 0xABU);
}

TEST_CASE("m72 vblank interrupts the V30 through the interrupt controller under the scheduler",
          "[m72]") {
    // Boot: program the uPD71059 (edge, single, vector base 0x20, ICW4,
    // unmasked), set up a stack, enable interrupts, halt. The one-line
    // vblank pulse on IR0 then vectors through IVT[0x20] to the handler,
    // which writes a marker into work RAM and halts again.
    auto image = make_image({
        0xB0U, 0x13U,        // MOV AL,13    (ICW1: edge, SNGL, IC4)
        0xE6U, 0x40U,        // OUT 40,AL
        0xB0U, 0x20U,        // MOV AL,20    (ICW2: vector base)
        0xE6U, 0x42U,        // OUT 42,AL
        0xB0U, 0x01U,        // MOV AL,01    (ICW4: 8086 mode)
        0xE6U, 0x42U,        // OUT 42,AL
        0xB0U, 0x00U,        // MOV AL,00    (OCW1: unmask all)
        0xE6U, 0x42U,        // OUT 42,AL
        0xB8U, 0x00U, 0xA0U, // MOV AX,A000
        0x8EU, 0xD0U,        // MOV SS,AX
        0xBCU, 0x00U, 0x10U, // MOV SP,1000
        0xFBU,               // STI
        0xF4U                // HLT
    });
    auto& main = image.regions["maincpu"];
    // IVT[0x20] -> 0040:0008 (physical 0x408).
    main[0x80U] = 0x08U;
    main[0x81U] = 0x00U;
    main[0x82U] = 0x40U;
    main[0x83U] = 0x00U;
    // Handler: MOV AX,A000; MOV DS,AX; MOV AL,99; MOV [0020],AL; HLT
    const std::vector<std::uint8_t> handler{0xB8U, 0x00U, 0xA0U, 0x8EU, 0xD8U, 0xB0U,
                                            0x99U, 0xA2U, 0x20U, 0x00U, 0xF4U};
    for (std::size_t i = 0; i < handler.size(); ++i) {
        main[0x408U + i] = handler[i];
    }

    auto system = assemble_m72(std::move(image));
    // 32 MHz master: pixel clock /4, V30 /4 (8 MHz). Video first so the
    // CPUs observe the freshly advanced beam.
    mnemos::runtime::scheduler scheduler(
        {{.chip = &system->video, .divider = 4U}, {.chip = &system->main_cpu, .divider = 4U}},
        &system->video);

    scheduler.run_frame(); // stops the master cycle the frame completes
    CHECK(system->video.frame_index() == 1U);
    scheduler.run_master_cycles(8192U); // let the handler run
    CHECK(system->work_ram[0x20U] == 0x99U);
    CHECK(system->pic.vector_base() == 0x20U);
    CHECK(system->pic.isr() == 0x01U); // IR0 in service, never EOI'd
}

TEST_CASE("m72 raster port arms the compare line and withdraws a pending request", "[m72]") {
    // OUT 06/07 byte lanes: line = (0x150 & 0x1ff) - 128 = 208.
    auto system = assemble_m72(make_image({
        0xB0U, 0x50U, // MOV AL,50
        0xE6U, 0x06U, // OUT 06,AL
        0xB0U, 0x01U, // MOV AL,01
        0xE6U, 0x07U, // OUT 07,AL
        0xF4U         // HLT
    }));
    run_until_halt(system->main_cpu, 16);
    CHECK(system->video.raster_compare_matches(208U));
    CHECK_FALSE(system->video.raster_compare_matches(207U));
}

TEST_CASE("m72 palette bus mirrors disconnected A9 and exposes 5-bit low bytes", "[m72]") {
    namespace m72 = mnemos::manifests::irem_m72;

    auto system = assemble_m72(rom_set_image{});

    system->main_bus.write8(m72::palette_a_base + 0x000U, 0xE7U);
    CHECK(system->palette_a[0x000U] == 0x07U);
    CHECK(system->main_bus.read8(m72::palette_a_base + 0x000U) == 0xE7U);
    CHECK(system->main_bus.read8(m72::palette_a_base + 0x001U) == 0xFFU);

    system->main_bus.write8(m72::palette_a_base + 0x200U, 0x3FU);
    CHECK(system->palette_a[0x000U] == 0x1FU);
    CHECK(system->palette_a[0x200U] == 0x00U);
    CHECK(system->main_bus.read8(m72::palette_a_base + 0x200U) == 0xFFU);

    system->main_bus.write8(m72::palette_a_base + 0x201U, 0x00U);
    CHECK(system->palette_a[0x001U] == 0x00U);
    CHECK(system->main_bus.read8(m72::palette_a_base + 0x201U) == 0xFFU);

    system->main_bus.write8(m72::palette_b_base + 0x600U, 0x12U);
    CHECK(system->palette_b[0x400U] == 0x12U);
    CHECK(system->palette_b[0x600U] == 0x00U);
    CHECK(system->main_bus.read8(m72::palette_b_base + 0x400U) == 0xF2U);

    system->main_bus.write16_le(m72::palette_b_base + 0xA00U, 0x001AU);
    CHECK(system->palette_b[0x800U] == 0x1AU);
    CHECK(system->palette_b[0xA00U] == 0x00U);
    CHECK(system->main_bus.read16_le(m72::palette_b_base + 0x800U) == 0xFFFAU);
}

TEST_CASE("m72 sprite DMA latches a stable copy for the renderer", "[m72]") {
    // A 16x16 sprite of solid pen 15 at the visible origin; sprite palette
    // color 0 pen 15 = full red. OUT 04 copies live sprite RAM into the
    // video unit's holding buffer -- so clearing sprite RAM AFTER the DMA
    // must not blank the rendered sprite.
    auto image = make_image({0xE6U, 0x04U, 0xF4U});   // OUT 04,AL ; HLT
    image.regions["sprites"].assign(4U * 32U, 0xFFU); // one solid cell, 4 planes
    auto system = assemble_m72(std::move(image));

    system->palette_a[15U * 2U] = 0x1FU; // pen 15: red gun full
    // Entry: y = 0x170 (384-368-16 = row 0), code 0, color 0, x = 0x140
    // (320 - 256 - 64 = column 0).
    system->sprite_ram[0] = 0x70U;
    system->sprite_ram[1] = 0x01U;
    system->sprite_ram[6] = 0x40U;
    system->sprite_ram[7] = 0x01U;

    run_until_halt(system->main_cpu, 8); // OUT 04 performs the DMA
    for (auto& byte : system->sprite_ram) {
        byte = 0U; // post-DMA clobber must not show
    }

    // One full beam pass reaches the vblank render point.
    system->video.tick(static_cast<std::uint64_t>(512U) * 284U);
    const auto view = system->video.framebuffer();
    CHECK(view.pixels[0] != 0U); // the latched sprite still renders
}

TEST_CASE("m72 IM0 jam vector ANDs the pending RST sources", "[m72]") {
    auto system = assemble_m72(rom_set_image{});

    // Idle: floating bus.
    system->sound_latch_irq = false;
    CHECK_FALSE(system->fm.irq_asserted());

    system->sound_latch_irq = true;
    system->update_sound_irq();
    // Latch only -> RST 18h.
    // (Query through the Z80's own IACK: IM 0 + EI + a pending line.)
    auto regs = system->sound_cpu.cpu_registers();
    regs.im = 0U;
    regs.iff1 = regs.iff2 = true;
    regs.sp = 0xF000U;
    regs.pc = 0x0100U;
    system->sound_cpu.set_registers(regs);
    system->sound_cpu.set_reset_line(false);
    system->sound_cpu.step_instruction(); // services the IRQ
    CHECK(system->sound_cpu.cpu_registers().pc == 0x0018U);
}

TEST_CASE("m72 Z80 programs the YM2151 through its ports and takes the timer IRQ", "[m72]") {
    auto system = assemble_m72(rom_set_image{});
    auto& sound = system->sound_ram;
    system->sound_cpu.set_reset_line(false); // as if the upload completed

    // Program CLKA = 1023 (overflow every 64 chip clocks), run + IRQ enable,
    // then IM 1; EI; HALT. The IM 1 handler at 0x38 stores a marker.
    const std::vector<std::uint8_t> program{
        0x3EU, 0x10U, 0xD3U, 0x00U, // LD A,10; OUT (0),A   (address CLKA hi)
        0x3EU, 0xFFU, 0xD3U, 0x01U, // LD A,FF; OUT (1),A
        0x3EU, 0x11U, 0xD3U, 0x00U, // LD A,11; OUT (0),A   (address CLKA lo)
        0x3EU, 0x03U, 0xD3U, 0x01U, // LD A,03; OUT (1),A   (CLKA = 1023)
        0x3EU, 0x14U, 0xD3U, 0x00U, // LD A,14; OUT (0),A   (timer control)
        0x3EU, 0x05U, 0xD3U, 0x01U, // LD A,05; OUT (1),A   (run A + IRQ enable)
        0xEDU, 0x56U,               // IM 1
        0x31U, 0x00U, 0xF8U,        // LD SP,F800 (stack in sound RAM)
        0xFBU,                      // EI
        0x76U,                      // HALT
    };
    for (std::size_t i = 0; i < program.size(); ++i) {
        sound[i] = program[i];
    }
    // IM 1 vector: LD A,99; LD (F900),A; HALT
    const std::vector<std::uint8_t> handler{0x3EU, 0x99U, 0x32U, 0x00U, 0xF9U, 0x76U};
    for (std::size_t i = 0; i < handler.size(); ++i) {
        sound[0x38U + i] = handler[i];
    }

    for (int i = 0; i < 16; ++i) {
        system->sound_cpu.step_instruction(); // runs to the HALT
    }
    CHECK(system->fm.register_value(0x14U) == 0x05U);
    CHECK_FALSE(system->sound_cpu.cpu_registers().halted == false);

    system->fm.tick(64U); // timer A overflows -> IRQ -> Z80 INT line
    CHECK(system->fm.irq_asserted());
    for (int i = 0; i < 4; ++i) {
        system->sound_cpu.step_instruction(); // INT accept + handler
    }
    CHECK(system->sound_bus.read8(0xF900U) == 0x99U);
}

TEST_CASE("m72 sound latch and YM2151 IRQs OR onto the Z80 INT line", "[m72]") {
    auto system = assemble_m72(rom_set_image{});

    // Assert both sources, then clear them one at a time: the line drops only
    // after BOTH are acknowledged.
    system->sound_latch_irq = true;
    system->update_sound_irq();
    system->fm.write_address(0x10U);
    system->fm.write_data(0xFFU);
    system->fm.write_address(0x11U);
    system->fm.write_data(0x03U);
    system->fm.write_address(0x14U);
    system->fm.write_data(0x05U);
    system->fm.tick(64U);
    REQUIRE(system->fm.irq_asserted());

    system->sound_latch_irq = false; // latch acknowledged
    system->update_sound_irq();
    // The YM2151 still holds the line: clearing its flag drops it.
    system->fm.write_address(0x14U);
    system->fm.write_data(0x15U); // reset flag A
    CHECK_FALSE(system->fm.irq_asserted());
}

TEST_CASE("m72 Z80 streams sample bytes from the sample ROM into the DAC", "[m72]") {
    rom_set_image image;
    image.regions["samples"] = {0x10U, 0x20U, 0x30U, 0x40U, 0x90U, 0xA0U};
    auto system = assemble_m72(std::move(image));
    auto& sound = system->sound_ram;
    system->sound_cpu.set_reset_line(false); // as if the upload completed

    // Set the sample cursor to 4, then stream two bytes to the DAC.
    const std::vector<std::uint8_t> program{
        0x3EU, 0x04U, 0xD3U, 0x80U, // LD A,04; OUT (80),A  (address low)
        0x3EU, 0x00U, 0xD3U, 0x81U, // LD A,00; OUT (81),A  (address high)
        0xDBU, 0x84U, 0xD3U, 0x82U, // IN A,(84); OUT (82),A
        0xDBU, 0x84U, 0xD3U, 0x82U, // IN A,(84); OUT (82),A
        0x76U,                      // HALT
    };
    for (std::size_t i = 0; i < program.size(); ++i) {
        sound[i] = program[i];
    }

    for (int i = 0; i < 6; ++i) {
        system->sound_cpu.step_instruction(); // through the first IN/OUT pair
    }
    CHECK(system->dac.level() == 0x90U); // first streamed byte
    for (int i = 0; i < 2; ++i) {
        system->sound_cpu.step_instruction(); // second IN/OUT pair
    }
    CHECK(system->dac.level() == 0xA0U); // cursor auto-incremented
    CHECK(system->sample_address == 6U);
    CHECK(system->dac.output() == (0xA0 - 0x80) * 64);
}

TEST_CASE("m72 records DAC writes on the sound-clock timeline", "[m72]") {
    auto system = assemble_m72(rom_set_image{});
    REQUIRE(system->dac_write_events.empty());

    system->record_dac_write(0xC0U);
    REQUIRE(system->dac_write_events.size() == 1U);
    CHECK(system->dac_write_events[0].sound_clock == 0U);
    CHECK(system->dac_write_events[0].output == (0xC0 - 0x80) * 64);

    system->fm.tick(64U);
    system->record_dac_write(0x80U);
    REQUIRE(system->dac_write_events.size() == 2U);
    CHECK(system->dac_write_events[1].sound_clock == 64U);
    CHECK(system->dac_write_events[1].output == 0);

    system->discard_dac_write_events_before(64U);
    REQUIRE(system->dac_write_events.size() == 1U);
    CHECK(system->dac_write_events[0].sound_clock == 64U);
}

TEST_CASE("m72 unprotected boards leave the absent MCU latch as open bus", "[m72]") {
    auto system = assemble_m72(make_image({
        0xB8U, 0x00U, 0xA0U, // MOV AX,A000
        0x8EU, 0xD8U,        // MOV DS,AX
        0xE4U, 0xC0U,        // IN AL,C0
        0xA2U, 0x10U, 0x00U, // MOV [0010],AL
        0xB0U, 0x5AU,        // MOV AL,5A
        0xE6U, 0xC0U,        // OUT C0,AL
        0xE4U, 0xC0U,        // IN AL,C0
        0xA2U, 0x11U, 0x00U, // MOV [0011],AL
        0xF4U,               // HLT
    }));
    REQUIRE_FALSE(system->mcu_present);
    REQUIRE_FALSE(system->protection_hle_present);

    run_until_halt(system->main_cpu, 24);

    CHECK(system->main_cpu.halted());
    CHECK(system->work_ram[0x10U] == 0xFFU);
    CHECK(system->work_ram[0x11U] == 0xFFU);
    CHECK(system->main_to_mcu == 0x00U);
    CHECK(system->mcu_to_main == 0x00U);
}

TEST_CASE("m72 protection MCU answers the V30 through the latch pair", "[m72]") {
    // MCU program: read the main->MCU latch, reply with value+1, write the
    // shared-RAM window, then stream one sample byte through the MCU sample
    // address latch.
    rom_set_image image;
    image.regions["mcu"] = {
        0x90U, 0x00U, 0x02U, // MOV DPTR,#0002
        0xE0U,               // MOVX A,@DPTR
        0x24U, 0x01U,        // ADD A,#1
        0xF0U,               // MOVX @DPTR,A
        0x90U, 0xC0U, 0x10U, // MOV DPTR,#C010
        0x74U, 0x5AU,        // MOV A,#5A
        0xF0U,               // MOVX @DPTR,A
        0x90U, 0x00U, 0x00U, // MOV DPTR,#0000
        0x74U, 0x01U,        // MOV A,#01 (sample offset = value << 5)
        0xF0U,               // MOVX @DPTR,A
        0xE0U,               // MOVX A,@DPTR
        0x90U, 0xC0U, 0x11U, // MOV DPTR,#C011
        0xF0U,               // MOVX @DPTR,A
        0x80U, 0xFEU,        // SJMP $
    };
    image.regions["samples"].assign(0x40U, 0x00U);
    image.regions["samples"][0x20U] = 0x77U;
    auto& main = image.regions["maincpu"];
    main.assign(mnemos::manifests::irem_m72::main_rom_size, 0xFFU);
    main[0xFFFF0U] = 0xEAU; // JMP 0000:0200
    main[0xFFFF1U] = 0x00U;
    main[0xFFFF2U] = 0x02U;
    main[0xFFFF3U] = 0x00U;
    main[0xFFFF4U] = 0x00U;
    // MOV AL,41; OUT C0,AL (latch + INT1 knock); HLT
    const std::vector<std::uint8_t> program{0xB0U, 0x41U, 0xE6U, 0xC0U, 0xF4U};
    for (std::size_t i = 0; i < program.size(); ++i) {
        main[0x200U + i] = program[i];
    }

    auto system = assemble_m72(std::move(image));
    REQUIRE(system->mcu_present);
    CHECK_FALSE(system->protection_hle_present);

    run_until_halt(system->main_cpu, 8);
    CHECK(system->main_to_mcu == 0x41U);

    system->mcu.tick(96U);
    CHECK(system->mcu_to_main == 0x42U);
    CHECK(system->mcu_shared_ram[0x10U] == 0x5AU);
    CHECK(system->main_bus.read8(mnemos::manifests::irem_m72::mcu_shared_main_base + 0x10U) ==
          0x5AU);
    CHECK(system->main_bus.read8(mnemos::manifests::irem_m72::mcu_shared_main_base + 0x11U) ==
          0x77U);
    CHECK(system->mcu_sample_address == 0x21U);
}

TEST_CASE("m72 protection MCU mailbox interrupt is asserted by the shared-RAM tail",
          "[m72]") {
    namespace m72 = mnemos::manifests::irem_m72;

    // Reset jumps around the INT0 vector. The level-sensed ISR acknowledges
    // the dual-port RAM mailbox by reading the final word, then counts once.
    std::vector<std::uint8_t> mcu_program(0x28U, 0x00U);
    mcu_program[0x00U] = 0x02U; // LJMP 0020
    mcu_program[0x01U] = 0x00U;
    mcu_program[0x02U] = 0x20U;
    mcu_program[0x03U] = 0x90U; // MOV DPTR,#CFFE
    mcu_program[0x04U] = 0xCFU;
    mcu_program[0x05U] = 0xFEU;
    mcu_program[0x06U] = 0xE0U; // MOVX A,@DPTR
    mcu_program[0x07U] = 0x05U; // INC 30
    mcu_program[0x08U] = 0x30U;
    mcu_program[0x09U] = 0x32U; // RETI
    mcu_program[0x20U] = 0x75U; // MOV IE,#EA|EX0
    mcu_program[0x21U] = 0xA8U;
    mcu_program[0x22U] = 0x81U;
    mcu_program[0x23U] = 0x80U; // SJMP $
    mcu_program[0x24U] = 0xFEU;

    rom_set_image image;
    image.regions["mcu"] = std::move(mcu_program);
    image.regions["maincpu"].assign(m72::main_rom_size, 0xFFU);
    auto system = assemble_m72(std::move(image));
    REQUIRE(system->mcu_present);

    system->mcu.step_instruction(); // LJMP main
    system->mcu.step_instruction(); // MOV IE,#EA|EX0
    CHECK(system->mcu.cpu_registers().pc == 0x0023U);

    system->main_bus.write8(m72::mcu_shared_main_base + 0x010U, 0x5AU);
    system->mcu.step_instruction(); // ordinary shared-RAM writes do not knock INT0
    CHECK(system->mcu.peek_direct(0x30U) == 0x00U);

    system->main_bus.write8(
        m72::mcu_shared_main_base + static_cast<std::uint32_t>(m72::mcu_shared_ram_size - 2U),
        0xA5U);
    system->mcu.step_instruction(); // INT0 service entry
    CHECK(system->mcu.cpu_registers().pc == 0x0003U);
    system->mcu.step_instruction(); // MOV DPTR,#CFFE
    system->mcu.step_instruction(); // MOVX A,@DPTR clears the mailbox interrupt line
    system->mcu.step_instruction(); // INC 30
    system->mcu.step_instruction(); // RETI
    CHECK(system->mcu.peek_direct(0x30U) == 0x01U);

    for (int i = 0; i < 4; ++i) {
        system->mcu.step_instruction();
    }
    CHECK(system->mcu.peek_direct(0x30U) == 0x01U);
}

TEST_CASE("m72 protection MCU reaches board latches through P2-latched MOVX @Ri", "[m72]") {
    // Some i8751 programs use MOVX @R0/@R1 instead of DPTR; P2 supplies the
    // MOVX high address byte, so the board must expose the same latch/RAM map.
    rom_set_image image;
    image.regions["mcu"] = {
        0x75U, 0xA0U, 0x00U, // MOV P2,#00
        0x78U, 0x02U,        // MOV R0,#02
        0xE2U,               // MOVX A,@R0 (main->MCU latch)
        0x24U, 0x01U,        // ADD A,#1
        0xF2U,               // MOVX @R0,A (MCU->main latch)
        0x75U, 0xA0U, 0xC0U, // MOV P2,#C0
        0x79U, 0x20U,        // MOV R1,#20
        0x74U, 0x6BU,        // MOV A,#6B
        0xF3U,               // MOVX @R1,A (shared RAM)
        0x75U, 0xA0U, 0x00U, // MOV P2,#00
        0x78U, 0x00U,        // MOV R0,#00
        0x74U, 0x02U,        // MOV A,#02 (sample offset = value << 5)
        0xF2U,               // MOVX @R0,A
        0xE2U,               // MOVX A,@R0 (sample data)
        0x75U, 0xA0U, 0xC0U, // MOV P2,#C0
        0x79U, 0x21U,        // MOV R1,#21
        0xF3U,               // MOVX @R1,A (shared RAM)
        0x80U, 0xFEU,        // SJMP $
    };
    image.regions["samples"].assign(0x60U, 0x00U);
    image.regions["samples"][0x40U] = 0x88U;
    auto& main = image.regions["maincpu"];
    main.assign(mnemos::manifests::irem_m72::main_rom_size, 0xFFU);
    main[0xFFFF0U] = 0xEAU; // JMP 0000:0200
    main[0xFFFF1U] = 0x00U;
    main[0xFFFF2U] = 0x02U;
    main[0xFFFF3U] = 0x00U;
    main[0xFFFF4U] = 0x00U;
    const std::vector<std::uint8_t> program{0xB0U, 0x51U, 0xE6U, 0xC0U, 0xF4U};
    for (std::size_t i = 0; i < program.size(); ++i) {
        main[0x200U + i] = program[i];
    }

    auto system = assemble_m72(std::move(image));
    REQUIRE(system->mcu_present);

    run_until_halt(system->main_cpu, 8);
    system->mcu.tick(256U);

    CHECK(system->mcu_to_main == 0x52U);
    CHECK(system->mcu_shared_ram[0x20U] == 0x6BU);
    CHECK(system->mcu_shared_ram[0x21U] == 0x88U);
    CHECK(system->main_bus.read8(mnemos::manifests::irem_m72::mcu_shared_main_base + 0x20U) ==
          0x6BU);
    CHECK(system->main_bus.read8(mnemos::manifests::irem_m72::mcu_shared_main_base + 0x21U) ==
          0x88U);
    CHECK(system->mcu_sample_address == 0x41U);
}

TEST_CASE("m72 manifest-declared MCU HLE inverts only the startup fill pattern", "[m72]") {
    namespace m72 = mnemos::manifests::irem_m72;

    rom_set_image image;
    image.regions["maincpu"].assign(m72::main_rom_size, 0xFFU);
    image.regions["samples"].assign(0x13001U, 0x00U);
    auto params = m72::board_params_for("dbreedm72");
    params.protection_hle_profile = "irem_m72.dbreedm72_no_dump_mcu";
    params.protection_hle_sample_triggers = {{0x06U, 0x13000U}};

    auto system = assemble_m72(std::move(image), params);
    REQUIRE_FALSE(system->mcu_present);
    REQUIRE(system->protection_hle_present);

    for (std::size_t offset = 0; offset < m72::mcu_shared_ram_size; ++offset) {
        const auto pattern =
            static_cast<std::uint8_t>(((offset >> 8U) & 0x0FU) + (offset & 0xFFU));
        system->main_bus.write8(m72::mcu_shared_main_base + static_cast<std::uint32_t>(offset),
                                pattern);
        CHECK(system->mcu_shared_ram[offset] == static_cast<std::uint8_t>(~pattern));
    }

    for (std::size_t offset = 0; offset < m72::mcu_shared_ram_size - 4U; ++offset) {
        const auto incremented = static_cast<std::uint8_t>(system->mcu_shared_ram[offset] + 1U);
        system->main_bus.write8(m72::mcu_shared_main_base + static_cast<std::uint32_t>(offset),
                                incremented);
        CHECK(system->mcu_shared_ram[offset] == incremented);
    }
    CHECK(system->protection_hle_entry_stub_active);
    CHECK(system->main_bus.read8(m72::mcu_shared_main_base + 0U) == 0xEAU);
    CHECK(system->main_bus.read8(m72::mcu_shared_main_base + 1U) == 0x6CU);
    CHECK(system->main_bus.read8(m72::mcu_shared_main_base + 2U) == 0x00U);
    CHECK(system->main_bus.read8(m72::mcu_shared_main_base + 3U) == 0x00U);
    CHECK(system->main_bus.read8(m72::mcu_shared_main_base + 4U) == 0x00U);

    system->main_bus.write8(m72::mcu_shared_main_base + 0x12U, 0xA5U);
    CHECK(system->mcu_shared_ram[0x12U] == 0xA5U);
    CHECK(system->main_bus.read8(m72::mcu_shared_main_base + 0x12U) == 0xA5U);
}

TEST_CASE("m72 Daiku no Gensan no-dump MCU HLE exposes its entry continuation", "[m72]") {
    namespace m72 = mnemos::manifests::irem_m72;

    rom_set_image image;
    image.regions["maincpu"].assign(m72::main_rom_size, 0xFFU);
    image.regions["samples"].assign(0x12B40U, 0x00U);
    auto params = m72::board_params_for("dkgensanm72");
    params.protection_hle_profile = "irem_m72.dkgensanm72_no_dump_mcu";
    params.protection_hle_sample_triggers = {{0x14U, 0x12B20U}};

    auto system = assemble_m72(std::move(image), params);
    REQUIRE_FALSE(system->mcu_present);
    REQUIRE(system->protection_hle_present);

    for (std::size_t offset = 0; offset < m72::mcu_shared_ram_size; ++offset) {
        const auto pattern =
            static_cast<std::uint8_t>(((offset >> 8U) & 0x0FU) + (offset & 0xFFU));
        system->main_bus.write8(m72::mcu_shared_main_base + static_cast<std::uint32_t>(offset),
                                pattern);
    }
    for (std::size_t offset = 0; offset < m72::mcu_shared_ram_size - 4U; ++offset) {
        system->main_bus.write8(m72::mcu_shared_main_base + static_cast<std::uint32_t>(offset),
                                static_cast<std::uint8_t>(system->mcu_shared_ram[offset] + 1U));
    }

    REQUIRE(system->protection_hle_entry_stub_active);
    CHECK(system->main_bus.read8(m72::mcu_shared_main_base + 0U) == 0xEAU);
    CHECK(system->main_bus.read8(m72::mcu_shared_main_base + 1U) == 0x3DU);
    CHECK(system->main_bus.read8(m72::mcu_shared_main_base + 2U) == 0x00U);
    CHECK(system->main_bus.read8(m72::mcu_shared_main_base + 3U) == 0x00U);
    CHECK(system->main_bus.read8(m72::mcu_shared_main_base + 4U) == 0x10U);
}

TEST_CASE("m72 no-dump MCU HLE exposes profile-specific checksum responses", "[m72]") {
    namespace m72 = mnemos::manifests::irem_m72;

    auto make_hle_system = [](std::string_view set_name, std::string profile,
                              std::uint8_t sample_trigger, std::uint32_t sample_start) {
        rom_set_image image;
        image.regions["maincpu"].assign(m72::main_rom_size, 0xFFU);
        image.regions["samples"].assign(sample_start + 1U, 0x00U);
        auto params = m72::board_params_for(set_name);
        params.protection_hle_profile = std::move(profile);
        params.protection_hle_sample_triggers = {{sample_trigger, sample_start}};
        return assemble_m72(std::move(image), params);
    };

    SECTION("Dragon Breed") {
        auto system =
            make_hle_system("dbreedm72", "irem_m72.dbreedm72_no_dump_mcu", 0x06U, 0x13000U);
        REQUIRE(system->protection_hle_present);
        std::fill(system->mcu_shared_ram.begin() + 0x0FE0,
                  system->mcu_shared_ram.begin() + 0x0FF2, std::uint8_t{0xEEU});

        system->main_bus.write8(m72::mcu_shared_main_base + 0x0FFFU, 0x00U);

        const std::vector<std::uint8_t> expected{0xA4U, 0x96U, 0x5FU, 0xC0U, 0xABU, 0x49U,
                                                 0x9FU, 0x19U, 0x84U, 0xE6U, 0xD6U, 0xCAU,
                                                 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U};
        for (std::size_t i = 0; i < expected.size(); ++i) {
            CHECK(system->main_bus.read8(m72::mcu_shared_main_base + 0x0FE0U +
                                         static_cast<std::uint32_t>(i)) == expected[i]);
        }
    }

    SECTION("Daiku no Gensan") {
        auto system = make_hle_system("dkgensanm72", "irem_m72.dkgensanm72_no_dump_mcu", 0x14U,
                                      0x12B20U);
        REQUIRE(system->protection_hle_present);
        std::fill(system->mcu_shared_ram.begin() + 0x0FE0,
                  system->mcu_shared_ram.begin() + 0x0FF2, std::uint8_t{0xEEU});

        system->main_bus.write8(m72::mcu_shared_main_base + 0x0FFFU, 0x00U);

        const std::vector<std::uint8_t> expected{0xC8U, 0xB4U, 0xDCU, 0xF8U, 0xD3U, 0xBAU,
                                                 0x48U, 0xEDU, 0x79U, 0x08U, 0x1CU, 0xB3U,
                                                 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U};
        for (std::size_t i = 0; i < expected.size(); ++i) {
            CHECK(system->main_bus.read8(m72::mcu_shared_main_base + 0x0FE0U +
                                         static_cast<std::uint32_t>(i)) == expected[i]);
        }
    }
}

TEST_CASE("m72 rejects unsupported MCU HLE profiles at board construction", "[m72]") {
    namespace m72 = mnemos::manifests::irem_m72;

    rom_set_image image;
    image.regions["maincpu"].assign(m72::main_rom_size, 0xFFU);
    auto params = m72::board_params_for("dbreedm72");
    params.protection_hle_profile = "irem_m72.unknown_no_dump_mcu";

    auto system = assemble_m72(std::move(image), params);
    CHECK_FALSE(system->mcu_present);
    CHECK_FALSE(system->protection_hle_present);
    CHECK_FALSE(system->params.protection_hle_profile.has_value());
    REQUIRE(system->roms.issues.size() == 1U);
    CHECK(system->roms.issues[0].file == "mcu");
    CHECK(system->roms.issues[0].message.find("unsupported M72 MCU HLE profile") !=
          std::string::npos);
    CHECK(system->main_bus.read8(m72::mcu_shared_main_base) == 0xFFU);
}

TEST_CASE("m72 rejects no-dump MCU HLE profiles without sample trigger metadata", "[m72]") {
    namespace m72 = mnemos::manifests::irem_m72;

    rom_set_image image;
    image.regions["maincpu"].assign(m72::main_rom_size, 0xFFU);
    auto params = m72::board_params_for("dbreedm72");
    params.protection_hle_profile = "irem_m72.dbreedm72_no_dump_mcu";

    auto system = assemble_m72(std::move(image), params);
    CHECK_FALSE(system->mcu_present);
    CHECK_FALSE(system->protection_hle_present);
    CHECK_FALSE(system->params.protection_hle_profile.has_value());
    REQUIRE(system->roms.issues.size() == 1U);
    CHECK(system->roms.issues[0].file == "mcu");
    CHECK(system->roms.issues[0].message.find("missing sample-trigger metadata") !=
          std::string::npos);
    CHECK(system->main_bus.read8(m72::mcu_shared_main_base) == 0xFFU);
}

TEST_CASE("m72 rejects no-dump MCU HLE sample triggers outside the samples region",
          "[m72]") {
    namespace m72 = mnemos::manifests::irem_m72;

    rom_set_image image;
    image.regions["maincpu"].assign(m72::main_rom_size, 0xFFU);
    image.regions["samples"].assign(0x100U, 0x00U);
    auto params = m72::board_params_for("dbreedm72");
    params.protection_hle_profile = "irem_m72.dbreedm72_no_dump_mcu";
    params.protection_hle_sample_triggers = {{0x06U, 0x13000U}};

    auto system = assemble_m72(std::move(image), params);
    CHECK_FALSE(system->mcu_present);
    CHECK_FALSE(system->protection_hle_present);
    CHECK_FALSE(system->params.protection_hle_profile.has_value());
    REQUIRE(system->roms.issues.size() == 1U);
    CHECK(system->roms.issues[0].file == "mcu");
    CHECK(system->roms.issues[0].message.find("beyond samples region size") !=
          std::string::npos);
    CHECK(system->main_bus.read8(m72::mcu_shared_main_base) == 0xFFU);
}

TEST_CASE("m72 no-dump MCU HLE sample trigger selects sample segments above 64K", "[m72]") {
    namespace m72 = mnemos::manifests::irem_m72;

    auto image = make_image({
        0xB8U, 0x00U, 0xA0U, // MOV AX,A000
        0x8EU, 0xD8U,        // MOV DS,AX
        0xB0U, 0x14U,        // MOV AL,14
        0xE6U, 0xC0U,        // OUT C0,AL
        0xE4U, 0xC0U,        // IN AL,C0
        0xA2U, 0x10U, 0x00U, // MOV [0010],AL
        0xF4U,               // HLT
    });
    auto& samples = image.regions["samples"];
    samples.assign(0x12B40U, 0x00U);
    samples[0x20U] = 0x11U;    // trigger 1
    samples[0x21U] = 0x12U;
    samples[0x30U] = 0x00U;    // explicit separator
    samples[0x12B20U] = 0x99U; // trigger 20, beyond a 16-bit cursor
    samples[0x12B21U] = 0x98U;

    auto params = m72::board_params_for("dkgensanm72");
    params.protection_hle_profile = "irem_m72.dkgensanm72_no_dump_mcu";
    params.protection_hle_sample_triggers = {{0x14U, 0x12B20U}};

    auto system = assemble_m72(std::move(image), params);
    REQUIRE(system->protection_hle_present);
    run_until_halt(system->main_cpu, 16);
    REQUIRE(system->main_cpu.halted());
    CHECK(system->main_to_mcu == 0x14U);
    CHECK(system->mcu_to_main == 0x14U);
    CHECK(system->work_ram[0x10U] == 0x14U);
    REQUIRE(system->sample_address == 0x12B20U);

    // Z80: IN A,(84); OUT (82),A; HALT. The no-dump HLE trigger primes the
    // same sample cursor the sound CPU's sample-read port consumes.
    const std::vector<std::uint8_t> sound_program{0xDBU, 0x84U, 0xD3U, 0x82U, 0x76U};
    for (std::size_t i = 0; i < sound_program.size(); ++i) {
        system->sound_ram[i] = sound_program[i];
    }
    system->sound_cpu.set_reset_line(false);
    for (int i = 0; i < 3; ++i) {
        system->sound_cpu.step_instruction();
    }
    CHECK(system->dac.level() == 0x99U);
    CHECK(system->sample_address == 0x12B21U);
}

TEST_CASE("m72 dbreed no-dump MCU HLE sample trigger uses declared metadata", "[m72]") {
    namespace m72 = mnemos::manifests::irem_m72;

    auto image = make_image({
        0xB0U, 0x06U, // MOV AL,06
        0xE6U, 0xC0U, // OUT C0,AL
        0xF4U,        // HLT
    });
    image.regions["samples"].assign(0x13020U, 0x00U);
    image.regions["samples"][0x13000U] = 0x7DU;

    auto params = m72::board_params_for("dbreedm72");
    params.protection_hle_profile = "irem_m72.dbreedm72_no_dump_mcu";
    params.protection_hle_sample_triggers = {{0x06U, 0x13000U}};

    auto system = assemble_m72(std::move(image), params);
    REQUIRE(system->protection_hle_present);
    run_until_halt(system->main_cpu, 8);
    REQUIRE(system->main_cpu.halted());
    CHECK(system->main_to_mcu == 0x06U);
    CHECK(system->mcu_to_main == 0x06U);
    CHECK(system->sample_address == 0x13000U);
}

TEST_CASE("m72 no-dump MCU HLE leaves the sample cursor unchanged for unknown triggers", "[m72]") {
    namespace m72 = mnemos::manifests::irem_m72;

    auto image = make_image({
        0xB0U, 0xFFU, // MOV AL,FF
        0xE6U, 0xC0U, // OUT C0,AL
        0xF4U,        // HLT
    });
    image.regions["samples"].assign(0x20000U, 0x55U);

    auto params = m72::board_params_for("dkgensanm72");
    params.protection_hle_profile = "irem_m72.dkgensanm72_no_dump_mcu";
    params.protection_hle_sample_triggers = {{0x14U, 0x12B20U}};

    auto system = assemble_m72(std::move(image), params);
    system->sample_address = 0x1234U;
    REQUIRE(system->protection_hle_present);
    run_until_halt(system->main_cpu, 8);
    REQUIRE(system->main_cpu.halted());
    CHECK(system->main_to_mcu == 0xFFU);
    CHECK(system->mcu_to_main == 0xFFU);
    CHECK(system->sample_address == 0x1234U);
}

TEST_CASE("m72 board save_state/load_state round-trips glue RAM and latches", "[m72]") {
    namespace m72 = mnemos::manifests::irem_m72;

    auto make_hle_image = [] {
        rom_set_image image;
        image.regions["maincpu"].assign(m72::main_rom_size, 0xFFU);
        image.regions["samples"].assign(0x13020U, 0x00U);
        return image;
    };
    auto params = m72::board_params_for("dbreedm72");
    params.protection_hle_profile = "irem_m72.dbreedm72_no_dump_mcu";
    params.protection_hle_sample_triggers = {{0x06U, 0x13000U}};
    auto source = assemble_m72(make_hle_image(), params);

    source->work_ram[0x20U] = 0x44U;
    source->sound_ram[0xF010U] = 0x66U;
    source->sprite_ram[0x02U] = 0x81U;
    source->palette_a[0x03U] = 0x22U;
    source->palette_b[0x04U] = 0x33U;
    source->vram_a[0x05U] = 0x55U;
    source->vram_b[0x06U] = 0x77U;
    source->mcu_shared_ram[0x12U] = 0xA5U;
    source->sound_latch = 0x9CU;
    source->input_p1 = 0xEFU;
    source->input_p2 = 0xDFU;
    source->input_system = 0xF7U;
    source->dip_switches = 0x1234U;
    source->control_register = 0x1CU;
    source->coin_counters = {7U, 9U};
    source->video.set_flip_screen(true);
    source->scroll_regs = {0x01U, 0x02U, 0x03U, 0x04U, 0x05U, 0x06U, 0x07U, 0x08U};
    source->raster_regs = {0xAAU, 0x01U};
    source->sample_address = 0x10021U;
    source->main_to_mcu = 0x41U;
    source->mcu_to_main = 0x42U;
    source->mcu_sample_address = 0x23456U;
    source->protection_hle_startup_invert_active = true;
    source->protection_hle_startup_next_offset = 0x0123U;
    source->protection_hle_startup_fill_completed = true;
    source->protection_hle_entry_write_next_offset = 0x0456U;
    source->protection_hle_entry_stub_active = true;
    source->fm.tick(128U);
    source->record_dac_write(0x9AU);
    source->sound_latch_irq = true;
    source->update_sound_irq();

    std::vector<std::uint8_t> snapshot;
    mnemos::chips::state_writer writer(snapshot);
    source->save_state(writer);

    auto restored = assemble_m72(make_hle_image(), params);
    mnemos::chips::state_reader reader(snapshot);
    restored->load_state(reader);
    REQUIRE(reader.ok());

    CHECK(restored->work_ram[0x20U] == 0x44U);
    CHECK(restored->sound_ram[0xF010U] == 0x66U);
    CHECK(restored->sprite_ram[0x02U] == 0x81U);
    CHECK(restored->palette_a[0x03U] == 0x22U);
    CHECK(restored->palette_b[0x04U] == 0x33U);
    CHECK(restored->vram_a[0x05U] == 0x55U);
    CHECK(restored->vram_b[0x06U] == 0x77U);
    CHECK(restored->mcu_shared_ram[0x12U] == 0xA5U);
    CHECK(restored->sound_latch == 0x9CU);
    CHECK(restored->input_p1 == 0xEFU);
    CHECK(restored->input_p2 == 0xDFU);
    CHECK(restored->input_system == 0xF7U);
    CHECK(restored->dip_switches == 0x1234U);
    CHECK(restored->control_register == 0x1CU);
    CHECK(restored->coin_counters[0] == 7U);
    CHECK(restored->coin_counters[1] == 9U);
    CHECK(restored->video.flip_screen());
    CHECK(restored->scroll_regs[7] == 0x08U);
    CHECK(restored->raster_regs[0] == 0xAAU);
    CHECK(restored->sample_address == 0x10021U);
    CHECK(restored->main_to_mcu == 0x41U);
    CHECK(restored->mcu_to_main == 0x42U);
    CHECK(restored->mcu_sample_address == 0x23456U);
    CHECK(restored->protection_hle_startup_invert_active);
    CHECK(restored->protection_hle_startup_next_offset == 0x0123U);
    CHECK(restored->protection_hle_startup_fill_completed);
    CHECK(restored->protection_hle_entry_write_next_offset == 0x0456U);
    CHECK(restored->protection_hle_entry_stub_active);
    CHECK(restored->dac.level() == 0x9AU);
    REQUIRE(restored->dac_write_events.size() == 1U);
    CHECK(restored->dac_write_events[0].sound_clock == 128U);
    CHECK(restored->dac_write_events[0].output == (0x9A - 0x80) * 64);
    CHECK(restored->sound_latch_irq);
}

TEST_CASE("m72 board load_state rejects a structurally different board or ROM image", "[m72]") {
    namespace m72 = mnemos::manifests::irem_m72;

    auto protected_image = [] {
        rom_set_image image;
        image.regions["maincpu"].assign(m72::main_rom_size, 0xFFU);
        image.regions["samples"].assign(0x15821U, 0x00U);
        return image;
    };
    auto params = m72::board_params_for("dbreedm72");
    params.protection_hle_profile = "irem_m72.dbreedm72_no_dump_mcu";
    params.protection_hle_sample_triggers = {{0x06U, 0x13000U}};
    auto source = assemble_m72(protected_image(), params);

    std::vector<std::uint8_t> snapshot;
    mnemos::chips::state_writer writer(snapshot);
    source->save_state(writer);

    auto incompatible = assemble_m72(rom_set_image{}, m72::board_params_for("rtype"));
    mnemos::chips::state_reader reader(snapshot);
    incompatible->load_state(reader);
    CHECK_FALSE(reader.ok());

    auto different_hle_params = params;
    different_hle_params.protection_hle_sample_triggers = {{0x07U, 0x15820U}};
    auto same_rom_different_hle = assemble_m72(protected_image(), different_hle_params);
    mnemos::chips::state_reader hle_reader(snapshot);
    same_rom_different_hle->load_state(hle_reader);
    CHECK_FALSE(hle_reader.ok());

    auto same_wiring_source = assemble_m72(make_image({0xF4U}), m72::board_params_for("rtype"));
    std::vector<std::uint8_t> same_wiring_snapshot;
    mnemos::chips::state_writer same_wiring_writer(same_wiring_snapshot);
    same_wiring_source->save_state(same_wiring_writer);

    auto same_wiring_different_rom =
        assemble_m72(make_image({0x90U, 0xF4U}), m72::board_params_for("rtype"));
    mnemos::chips::state_reader same_wiring_reader(same_wiring_snapshot);
    same_wiring_different_rom->load_state(same_wiring_reader);
    CHECK_FALSE(same_wiring_reader.ok());
}

TEST_CASE("m72 without an mcu region schedules no MCU", "[m72]") {
    auto system = assemble_m72(rom_set_image{});
    CHECK_FALSE(system->mcu_present);
    CHECK_FALSE(system->protection_hle_present);
}
