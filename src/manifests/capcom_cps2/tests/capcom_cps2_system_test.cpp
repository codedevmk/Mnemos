#include "capcom_cps2_system.hpp"

#include "cps2_crypto.hpp"
#include "rom_set_toml.hpp"
#include "state.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {
    namespace cps2 = mnemos::manifests::capcom_cps2;

    using mnemos::manifests::capcom_cps2::cps2_board_params;
    using mnemos::manifests::capcom_cps2::cps2_rom_skeleton;
    using mnemos::manifests::capcom_cps2::cps2_system;
    using mnemos::manifests::capcom_cps2::crypto_key_size;
    using mnemos::manifests::capcom_cps2::encrypt_opcodes;
    using mnemos::manifests::common::parse_rom_set_decl;
    using mnemos::manifests::common::rom_set_decl;
    using mnemos::manifests::common::rom_set_image;
    using mnemos::manifests::common::rom_set_region;

    std::array<std::uint8_t, crypto_key_size> sample_key() {
        std::array<std::uint8_t, crypto_key_size> k{};
        for (std::size_t i = 0; i < k.size(); ++i) {
            k[i] = static_cast<std::uint8_t>(i * 7U + 3U);
        }
        return k;
    }

    // A tiny 68000 program (big-endian): reset vector (SSP=$00FF0000, PC=$000008)
    // then one caller-selected instruction. Padded to an even length.
    std::vector<std::uint8_t> plain_program(std::uint16_t opcode = 0x707FU,
                                            std::uint32_t reset_ssp = 0x00FF0000U,
                                            std::uint32_t reset_pc = 0x00000008U) {
        std::vector<std::uint8_t> p(0x40U, 0x00U);
        const auto w16 = [&](std::size_t a, std::uint16_t v) {
            p[a] = static_cast<std::uint8_t>(v >> 8U);
            p[a + 1U] = static_cast<std::uint8_t>(v);
        };
        const auto w32 = [&](std::size_t a, std::uint32_t v) {
            w16(a, static_cast<std::uint16_t>(v >> 16U));
            w16(a + 2U, static_cast<std::uint16_t>(v));
        };
        w32(0x0U, reset_ssp);
        w32(0x4U, reset_pc);
        w16(0x8U, opcode);
        return p;
    }

    // The encrypted program a real CPS-2 board ships (encrypt the plaintext with
    // the board key); the machine must decrypt it back to boot.
    std::vector<std::uint8_t> encrypted_program(const std::array<std::uint8_t, crypto_key_size>& k,
                                                std::uint16_t opcode = 0x707FU,
                                                std::uint32_t reset_ssp = 0x00FF0000U,
                                                std::uint32_t reset_pc = 0x00000008U) {
        const std::vector<std::uint8_t> plain = plain_program(opcode, reset_ssp, reset_pc);
        mnemos::manifests::capcom_cps2::cps2_crypto_key key{};
        REQUIRE(decode_key(k, key));
        std::vector<std::uint8_t> enc(plain.size());
        REQUIRE(encrypt_opcodes(plain, enc, key));
        return enc;
    }

    rom_set_image save_state_image(const std::array<std::uint8_t, crypto_key_size>& k) {
        rom_set_image image;
        image.regions["maincpu"] = encrypted_program(k, 0x60FEU); // BRA * for frame stepping
        image.regions["gfx"].assign(0x10000U, 0xFFU);
        image.regions["audiocpu"].assign(0x12000U, 0x00U);
        image.regions["qsound"].assign(0x20000U, 0x80U);
        return image;
    }

    std::vector<std::uint8_t> snapshot(cps2_system& sys) {
        std::vector<std::uint8_t> blob;
        mnemos::chips::state_writer writer(blob);
        sys.save_state(writer);
        return blob;
    }

    void seed_mutable_board_state(cps2_system& sys) {
        auto& bus = sys.bus();
        bus.write16_be(cps2::main_ram_base, 0xCAFEU);
        bus.write16_be(cps2::video_ram_base + 0x20U, 0x1357U);
        bus.write16_be(cps2::object_ram_base + 0x40U, 0x2468U);
        bus.write16_be(cps2::extra_ram_base + 0x10U, 0x5A5AU);
        bus.write16_be(cps2::control_reg_base + 0x04U, 0x0FEDU);
        bus.write16_be(cps2::cps_a_base + cps2::cps_a_palette_base * 2U,
                       static_cast<std::uint16_t>(cps2::video_ram_base >> 8U));
        bus.write16_be(cps2::cps_b_base + 0x02U, 0x2222U);
        bus.write8(0x8040E0U, 0x01U);
        bus.write8(cps2::sound_reset_port, 0x08U);
        bus.write8(cps2::sound_reset_port, 0x09U); // run Z80 + pulse coin counter 1
        bus.write8(cps2::sound_reset_port, 0x08U);
        bus.write8(cps2::sound_reset_port, 0x19U); // release coin lockout 1

        sys.input0 = 0xBEEFU;
        sys.input1 = 0x7F7FU;
        sys.input_sys = 0xFFFCU;
        sys.set_development_dips({0x12U, 0x34U, 0x56U});
        auto eeprom = sys.eeprom().bytes();
        eeprom[0] = 0x34U;
        eeprom[1] = 0x12U;

        sys.sound_bus().write8(cps2::z80_bank_reg, 0x03U);
        sys.sound_bus().write8(cps2::z80_shared_base + 3U, 0xA6U);
        sys.sound_bus().write8(cps2::z80_ram_base + 4U, 0x5CU);
        sys.sound_bus().write8(cps2::z80_work_base + 5U, 0xC3U);

        // Program one QSound voice through the same three-port interface the Z80
        // uses, so the DSP register latch participates in the board snapshot.
        auto& q = sys.qsound_dsp();
        q.write_port(0U, 0x00U);
        q.write_port(1U, 0x40U);
        q.write_port(2U, 0x06U); // voice 0 volume
        q.write_port(0U, 0x00U);
        q.write_port(1U, 0x10U);
        q.write_port(2U, 0x02U); // voice 0 rate
    }

    void cps2_eeprom_set(cps2_system& sys, std::uint8_t pins) {
        sys.bus().write8(0x804040U, pins);
    }

    void cps2_eeprom_select(cps2_system& sys) {
        cps2_eeprom_set(sys, 0x00U);
        cps2_eeprom_set(sys, cps2::eeprom_cs_bit);
    }

    void cps2_eeprom_deselect(cps2_system& sys) { cps2_eeprom_set(sys, 0x00U); }

    void cps2_eeprom_send_bit(cps2_system& sys, bool bit) {
        const auto pins = static_cast<std::uint8_t>(cps2::eeprom_cs_bit |
                                                    (bit ? cps2::eeprom_di_bit : 0U));
        cps2_eeprom_set(sys, pins);
        cps2_eeprom_set(sys, static_cast<std::uint8_t>(pins | cps2::eeprom_clk_bit));
        cps2_eeprom_set(sys, pins);
    }

    void cps2_eeprom_send_bits(cps2_system& sys, std::uint32_t value, std::uint8_t bits) {
        for (std::uint8_t bit = bits; bit > 0U; --bit) {
            cps2_eeprom_send_bit(sys, ((value >> (bit - 1U)) & 1U) != 0U);
        }
    }

    bool cps2_eeprom_read_clock(cps2_system& sys) {
        cps2_eeprom_set(sys, cps2::eeprom_cs_bit);
        cps2_eeprom_set(sys, static_cast<std::uint8_t>(cps2::eeprom_cs_bit |
                                                       cps2::eeprom_clk_bit));
        const bool bit = (sys.bus().read16_be(0x804020U) & 0x0001U) != 0U;
        cps2_eeprom_set(sys, cps2::eeprom_cs_bit);
        return bit;
    }

    void cps2_eeprom_write_enable(cps2_system& sys) {
        cps2_eeprom_select(sys);
        cps2_eeprom_send_bits(sys, 0x100U | 0x30U, 9U);
        cps2_eeprom_deselect(sys);
    }

    void cps2_eeprom_write_word(cps2_system& sys, std::uint8_t address, std::uint16_t value) {
        cps2_eeprom_select(sys);
        cps2_eeprom_send_bits(sys, 0x100U | (1U << 6U) | (address & 0x3FU), 9U);
        cps2_eeprom_send_bits(sys, value, 16U);
        cps2_eeprom_deselect(sys);
    }

    std::uint16_t cps2_eeprom_read_word(cps2_system& sys, std::uint8_t address) {
        std::uint16_t value = 0U;
        cps2_eeprom_select(sys);
        cps2_eeprom_send_bits(sys, 0x100U | (2U << 6U) | (address & 0x3FU), 9U);
        for (std::uint8_t i = 0U; i < 16U; ++i) {
            value = static_cast<std::uint16_t>((value << 1U) |
                                               (cps2_eeprom_read_clock(sys) ? 1U : 0U));
        }
        cps2_eeprom_deselect(sys);
        return value;
    }

    std::uint16_t cps2_eeprom_read_word_19xx(cps2_system& sys, std::uint8_t address) {
        std::uint16_t value = 0U;
        cps2_eeprom_select(sys);
        cps2_eeprom_send_bits(sys, 0x180U | (address & 0x3FU), 10U);
        for (std::uint8_t i = 0U; i < 16U; ++i) {
            value = static_cast<std::uint16_t>((value << 1U) |
                                               (cps2_eeprom_read_clock(sys) ? 1U : 0U));
        }
        cps2_eeprom_deselect(sys);
        return value;
    }

    void qsound_port_write(cps2_system& sys, std::uint8_t reg, std::uint16_t data) {
        auto& sound = sys.sound_bus();
        sound.write8(cps2::z80_port_base + 0U, static_cast<std::uint8_t>(data >> 8U));
        sound.write8(cps2::z80_port_base + 1U, static_cast<std::uint8_t>(data));
        sound.write8(cps2::z80_port_base + 2U, reg);
    }

    [[nodiscard]] bool any_nonzero(std::span<const std::int16_t> samples) noexcept {
        for (const std::int16_t sample : samples) {
            if (sample != 0) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] rom_set_decl parse_checked_in_cps2_game(std::string_view stem) {
        namespace fs = std::filesystem;
        const fs::path path = fs::path{MNEMOS_CPS2_GAMES_DIR} / (std::string{stem} + ".toml");

        std::ifstream file(path, std::ios::binary);
        REQUIRE(file.good());
        const std::string text{std::istreambuf_iterator<char>{file},
                               std::istreambuf_iterator<char>{}};
        auto result = parse_rom_set_decl(text, path.string());
        REQUIRE(result.ok());
        return *result.value;
    }

    [[nodiscard]] const rom_set_region* find_region(const rom_set_decl& decl,
                                                    std::string_view name) noexcept {
        const auto it = std::find_if(decl.regions.begin(), decl.regions.end(),
                                     [name](const auto& region) { return region.name == name; });
        return it != decl.regions.end() ? &*it : nullptr;
    }

    [[nodiscard]] bool region_files_cover_destination_offset(const rom_set_region& region,
                                                            std::size_t offset) noexcept {
        for (const auto& file : region.files) {
            const std::size_t stride = file.stride != 0U ? file.stride : 1U;
            const std::size_t unit = file.unit != 0U ? file.unit : 1U;
            const std::size_t source_bytes = file.length != 0U ? file.length : file.size;
            const std::size_t chunks = source_bytes / unit;
            for (std::size_t c = 0; c < chunks; ++c) {
                const std::size_t base = file.offset + c * stride;
                if (offset >= base && offset < base + unit) {
                    return true;
                }
            }
        }
        return false;
    }

} // namespace

TEST_CASE("cps2 ROM skeleton declares the fixed executable regions", "[capcom_cps2][system]") {
    const auto decl = cps2_rom_skeleton("cps2_synth");

    CHECK(decl.name == "cps2_synth");
    CHECK(decl.board == "capcom_cps2");
    REQUIRE(decl.regions.size() == 2U);
    CHECK(decl.regions[0].name == "maincpu");
    CHECK(decl.regions[0].size == cps2::main_rom_size);
    CHECK(decl.regions[0].fill == 0xFFU);
    CHECK(decl.regions[1].name == "key");
    CHECK(decl.regions[1].size == crypto_key_size);
    CHECK(decl.regions[1].fill == 0x00U);
}

TEST_CASE("cps2 checked-in game TOMLs parse and declare QSound HLE",
          "[capcom_cps2][manifest]") {
    namespace fs = std::filesystem;
    const fs::path games_dir{MNEMOS_CPS2_GAMES_DIR};
    REQUIRE(fs::is_directory(games_dir));

    std::vector<fs::path> tomls;
    for (const fs::directory_entry& entry : fs::directory_iterator(games_dir)) {
        if (entry.path().extension() == ".toml") {
            tomls.push_back(entry.path());
        }
    }
    std::sort(tomls.begin(), tomls.end());
    REQUIRE(tomls.size() >= 36U);

    for (const fs::path& path : tomls) {
        INFO(path.string());
        std::ifstream file(path, std::ios::binary);
        REQUIRE(file.good());
        const std::string text{std::istreambuf_iterator<char>{file},
                               std::istreambuf_iterator<char>{}};
        const auto result = parse_rom_set_decl(text, path.string());
        REQUIRE(result.ok());
        CHECK(result.value->board == "capcom_cps2");

        const auto has_qsound_hle =
            std::any_of(result.value->hle.begin(), result.value->hle.end(), [](const auto& hle) {
                return hle.chip == "capcom.qsound" && !hle.rationale.empty();
            });
        CHECK(has_qsound_hle);

        const auto audio_region =
            std::find_if(result.value->regions.begin(), result.value->regions.end(),
                         [](const auto& region) { return region.name == "audiocpu"; });
        if (audio_region != result.value->regions.end()) {
            CHECK(audio_region->fill == 0x00U);
        }
    }
}

TEST_CASE("hsf2 preserves the expanded QSound Z80 ROM holes as zero",
          "[capcom_cps2][manifest][sound]") {
    const auto decl = parse_checked_in_cps2_game("hsf2");
    const auto* audio = find_region(decl, "audiocpu");
    REQUIRE(audio != nullptr);
    REQUIRE(audio->files.size() == 3U);

    CHECK(audio->size == cps2::z80_qsound_cpu_rom_region_size);
    CHECK(audio->fill == 0x00U);

    const auto& fixed = audio->files[0];
    CHECK(fixed.name == "hs2.01");
    CHECK(fixed.offset == 0x00000U);
    CHECK(fixed.source_offset == 0x00000U);
    CHECK(fixed.length == 0x08000U);
    CHECK(fixed.size == 0x20000U);

    const auto& expanded = audio->files[1];
    CHECK(expanded.name == "hs2.01");
    CHECK(expanded.offset == 0x10000U);
    CHECK(expanded.source_offset == 0x08000U);
    CHECK(expanded.length == 0x18000U);
    CHECK(expanded.size == 0x20000U);

    const auto& packed_next = audio->files[2];
    CHECK(packed_next.name == "hs2.02");
    CHECK(packed_next.offset == 0x20000U);
    CHECK(packed_next.source_offset == 0x00000U);
    CHECK(packed_next.length == 0x00000U);
    CHECK(packed_next.size == 0x20000U);

    // HSF2's driver reads through high expanded banks during attract mode. Old
    // CPS2 loaders calloc this region, so uncovered addresses must read zero.
    CHECK_FALSE(region_files_cover_destination_offset(*audio, 0x401C9U));
}

TEST_CASE("cps2 system boots the 68000 from the decrypted opcode image", "[capcom_cps2][system]") {
    const auto k = sample_key();
    rom_set_image image;
    image.regions["maincpu"] = encrypted_program(k);

    cps2_system sys(std::move(image), cps2_board_params{.key = k});
    REQUIRE(sys.executable());

    auto r = sys.cpu().cpu_registers();
    CHECK(r.pc == 0x00000008U);   // reset PC from the decrypted vector
    CHECK(r.a[7] == 0x00FF0000U); // SSP from the decrypted vector

    // Data reads see the encrypted ROM; opcode fetches see the decrypted image.
    CHECK(sys.bus().read16_be(0x0008U) != 0x707FU);
    CHECK(sys.bus().fetch16_be_opcode(0x0008U) == 0x707FU);

    sys.run_cycles(4); // executes the decrypted MOVEQ #$7F,D0
    r = sys.cpu().cpu_registers();
    CHECK((r.d[0] & 0xFFU) == 0x7FU);
}

TEST_CASE("cps2 system without a key is a non-executable blocker", "[capcom_cps2][system]") {
    const auto k = sample_key();
    rom_set_image image;
    image.regions["maincpu"] = encrypted_program(k);

    // No key supplied: the program stays encrypted and the board is not runnable.
    cps2_system sys(std::move(image), cps2_board_params{});
    CHECK_FALSE(sys.executable());
    // The opcode overlay is the raw encrypted bytes, so a fetch != the plaintext.
    CHECK(sys.bus().fetch16_be_opcode(0x0008U) != 0x707FU);
}

TEST_CASE("cps2 system rejects decrypted programs with unsafe reset vectors",
          "[capcom_cps2][system]") {
    const auto k = sample_key();

    SECTION("odd SSP") {
        rom_set_image image;
        image.regions["maincpu"] = encrypted_program(k, 0x707FU, 0x00FF0001U, 0x00000008U);
        cps2_system sys(std::move(image), cps2_board_params{.key = k});

        CHECK_FALSE(sys.executable());
    }

    SECTION("odd PC") {
        rom_set_image image;
        image.regions["maincpu"] = encrypted_program(k, 0x707FU, 0x00FF0000U, 0x00000009U);
        cps2_system sys(std::move(image), cps2_board_params{.key = k});
        const auto before = sys.cpu().cpu_registers();

        sys.run_cycles(64);

        CHECK_FALSE(sys.executable());
        CHECK(sys.cpu().cpu_registers().pc == before.pc);
    }

    SECTION("PC outside loaded program image") {
        rom_set_image image;
        image.regions["maincpu"] = encrypted_program(k, 0x707FU, 0x00FF0000U, 0x00010000U);
        cps2_system sys(std::move(image), cps2_board_params{.key = k});

        CHECK_FALSE(sys.executable());
    }

    SECTION("high even SSP is accepted on the 24-bit bus") {
        rom_set_image image;
        image.regions["maincpu"] = encrypted_program(k, 0x707FU, 0x01000000U, 0x00000008U);
        cps2_system sys(std::move(image), cps2_board_params{.key = k});

        REQUIRE(sys.executable());
        CHECK(sys.cpu().cpu_registers().a[7] == 0x01000000U);
    }
}

TEST_CASE("cps2 direct cycle stepping is blocked until a key makes the board executable",
          "[capcom_cps2][system]") {
    rom_set_image image;
    image.regions["maincpu"] = plain_program();

    cps2_system sys(std::move(image), cps2_board_params{});
    REQUIRE_FALSE(sys.executable());
    const auto before = sys.cpu().cpu_registers();

    sys.run_cycles(64);

    const auto after = sys.cpu().cpu_registers();
    CHECK(after.pc == before.pc);
    CHECK(after.d[0] == before.d[0]);
}

TEST_CASE("cps2 system reads the board key from a 'key' set region", "[capcom_cps2][system]") {
    const auto k = sample_key();
    rom_set_image image;
    image.regions["maincpu"] = encrypted_program(k);
    image.regions["key"].assign(k.begin(), k.end());

    cps2_system sys(std::move(image), cps2_board_params{}); // key resolved from the region
    REQUIRE(sys.executable());
    CHECK(sys.cpu().cpu_registers().pc == 0x00000008U);
}

TEST_CASE("cps2 system maps RAM, CPS registers, inputs, and the EEPROM port",
          "[capcom_cps2][system]") {
    namespace cps2 = mnemos::manifests::capcom_cps2;
    const auto k = sample_key();
    rom_set_image image;
    image.regions["maincpu"] = encrypted_program(k);
    cps2_system sys(std::move(image), cps2_board_params{.key = k});
    auto& bus = sys.bus();

    SECTION("the RAM regions round-trip") {
        // (The QSound comm RAM at $618000 and the banked object RAM are tested
        // separately.)
        const std::array<std::uint32_t, 4> ram_bases{cps2::main_ram_base, cps2::video_ram_base,
                                                     cps2::extra_ram_base, cps2::control_reg_base};
        std::uint16_t v = 0x1111U;
        for (const std::uint32_t base : ram_bases) {
            bus.write16_be(base, v);
            CHECK(bus.read16_be(base) == v);
            v = static_cast<std::uint16_t>(v + 0x1111U);
        }
    }

    SECTION("the CPS-A/B register file latches and the legacy mirror aliases it") {
        bus.write16_be(cps2::cps_a_base, 0xABCDU);
        CHECK(bus.read16_be(cps2::cps_a_base) == 0xABCDU);
        CHECK(bus.read16_be(cps2::cps_a_mirror_base) == 0xABCDU); // mirror sees the same latch
        bus.write16_be(cps2::cps_b_mirror_base, 0x1234U);
        CHECK(bus.read16_be(cps2::cps_b_base) == 0x1234U); // primary sees the mirror's write
        CHECK(sys.video().cps_b_reg(0U) == 0x1234U);
    }

    SECTION("the input ports read the active-low controls + QSound volume status") {
        sys.input0 = 0x4321U;
        sys.input1 = 0x8765U;
        sys.input_sys = 0xFFF9U; // test/service active low; bit0 remains EEPROM-owned
        CHECK(bus.read16_be(0x804000U) == 0x4321U);
        CHECK(bus.read16_be(0x804010U) == 0x8765U);
        CHECK((bus.read16_be(0x804020U) & 0x0006U) == 0x0000U);
        CHECK(bus.read16_be(0x804030U) == cps2::qsound_volume_status);
    }

    SECTION("the EEPROM port is wired: data-out on input 2 bit 0, pins on $804040") {
        // CS low resets the serial state; an idle 93C46 drives DO high.
        bus.write8(0x804040U, 0x00U);
        sys.input_sys = 0xFFFEU;
        CHECK((bus.read16_be(0x804020U) & 0x0001U) == 0x0001U);
        CHECK(((bus.read16_be(0x804020U) & 0x0001U) != 0U) == sys.eeprom().data_out());

        cps2_eeprom_write_enable(sys);
        cps2_eeprom_write_word(sys, 3U, 0xA55AU);
        CHECK(cps2_eeprom_read_word(sys, 3U) == 0xA55AU);
        CHECK(cps2_eeprom_read_word_19xx(sys, 3U) == 0xA55AU);
    }

    SECTION("the low EEPROM/output byte latches coin counters and lockouts") {
        CHECK(sys.coin_counter(0U) == 0U);
        CHECK(sys.coin_counter(1U) == 0U);
        CHECK_FALSE(sys.coin_lockout(0U));

        bus.write8(cps2::sound_reset_port, 0x03U);
        CHECK(sys.coin_counter(0U) == 1U);
        CHECK(sys.coin_counter(1U) == 1U);
        bus.write8(cps2::sound_reset_port, 0x03U);
        CHECK(sys.coin_counter(0U) == 1U);
        CHECK(sys.coin_counter(1U) == 1U);
        bus.write8(cps2::sound_reset_port, 0x02U);
        bus.write8(cps2::sound_reset_port, 0x03U);
        CHECK(sys.coin_counter(0U) == 2U);
        CHECK(sys.coin_counter(1U) == 1U);

        bus.write8(cps2::sound_reset_port, 0x00U);
        CHECK(sys.coin_lockout(0U));
        CHECK(sys.coin_lockout(1U));
        CHECK(sys.coin_lockout(2U));
        CHECK(sys.coin_lockout(3U));
        bus.write8(cps2::sound_reset_port, 0xF0U);
        CHECK_FALSE(sys.coin_lockout(0U));
        CHECK_FALSE(sys.coin_lockout(3U));
    }

    SECTION("the development DIP banks read through $8040B0-$8040B2") {
        CHECK(bus.read8(cps2::development_dip_base + 0U) == 0xFFU);
        CHECK(bus.read8(cps2::development_dip_base + 1U) == 0xFFU);
        CHECK(bus.read8(cps2::development_dip_base + 2U) == 0xFFU);
        CHECK(bus.read16_be(cps2::development_dip_base) == 0xFFFFU);

        sys.set_development_dips({0x12U, 0x34U, 0x56U});

        CHECK(bus.read8(cps2::development_dip_base + 0U) == 0x12U);
        CHECK(bus.read8(cps2::development_dip_base + 1U) == 0x34U);
        CHECK(bus.read8(cps2::development_dip_base + 2U) == 0x56U);
        CHECK(bus.read8(cps2::development_dip_base + 3U) == 0xFFU);
        CHECK(bus.read16_be(cps2::development_dip_base) == 0x1234U);
        CHECK(bus.read16_be(cps2::development_dip_base + 2U) == 0x56FFU);
    }
}

TEST_CASE("cps2 system multiplexes CPS2 analog paddle profiles on the IN0 port",
          "[capcom_cps2][system][input]") {
    namespace cps2 = mnemos::manifests::capcom_cps2;
    const auto k = sample_key();

    SECTION("Puzz Loop 2 switches between 8-bit paddles and digital controls") {
        rom_set_image image;
        image.regions["maincpu"] = encrypted_program(k);
        cps2_system sys(std::move(image),
                        cps2_board_params{.key = k,
                                          .analog_input =
                                              cps2::cps2_analog_input_mode::puzz_loop_2});
        auto& bus = sys.bus();
        sys.input0 = 0xABCDU;
        sys.set_analog_dial(0U, 0x019AU);
        sys.set_analog_dial(1U, 0x02BCU);

        CHECK(sys.analog_dial(0U) == 0x009AU);
        CHECK(sys.analog_dial(1U) == 0x00BCU);
        CHECK(bus.read16_be(cps2::cps_io_base) == 0xBC9AU);

        bus.write8(cps2::sound_reset_port, 0x02U);
        CHECK(bus.read16_be(cps2::cps_io_base) == 0xABCDU);
        CHECK(sys.coin_counter(1U) == 0U); // bit 1 is the paddle selector, not coin 2.
        bus.write8(cps2::sound_reset_port, 0x00U);
        CHECK(bus.read16_be(cps2::cps_io_base) == 0xBC9AU);
    }

    SECTION("Eco Fighters returns spinner bytes and tracks digital direction bits") {
        rom_set_image image;
        image.regions["maincpu"] = encrypted_program(k);
        cps2_system sys(std::move(image),
                        cps2_board_params{.key = k,
                                          .analog_input =
                                              cps2::cps2_analog_input_mode::eco_fighters});
        auto& bus = sys.bus();
        sys.input0 = 0xFFFFU;
        sys.input1 = 0xFFEFU; // "Use Spinners" configuration bit active-low.
        sys.set_analog_dial(0U, 0x0123U);
        sys.set_analog_dial(1U, 0x0456U);

        CHECK(bus.read16_be(cps2::cps_io_base) == 0xDFDFU);
        bus.write8(0x804040U, 0x01U);
        CHECK(bus.read16_be(cps2::cps_io_base) == 0x5623U);
        bus.write8(0x804040U, 0x00U);
        CHECK(bus.read16_be(cps2::cps_io_base) == 0xFFFFU);

        sys.input0 = 0x1234U;
        sys.input1 = 0xFFFFU; // spinner mode disabled: raw digital IN0.
        CHECK(bus.read16_be(cps2::cps_io_base) == 0x1234U);
    }
}

TEST_CASE("cps2 system supports the Mars Matrix coin-lockout polarity",
          "[capcom_cps2][system][input]") {
    namespace cps2 = mnemos::manifests::capcom_cps2;
    const auto k = sample_key();
    rom_set_image image;
    image.regions["maincpu"] = encrypted_program(k);
    cps2_system sys(std::move(image),
                    cps2_board_params{.key = k, .coin_lockout_active_high = true});
    auto& bus = sys.bus();

    bus.write8(cps2::sound_reset_port, 0x00U);
    CHECK_FALSE(sys.coin_lockout(0U));
    CHECK_FALSE(sys.coin_lockout(3U));
    bus.write8(cps2::sound_reset_port, 0x90U);
    CHECK(sys.coin_lockout(0U));
    CHECK_FALSE(sys.coin_lockout(1U));
    CHECK_FALSE(sys.coin_lockout(2U));
    CHECK(sys.coin_lockout(3U));
}

TEST_CASE("cps2 system maps object RAM through the selected bank and alternate window",
          "[capcom_cps2][system][video]") {
    const auto k = sample_key();
    rom_set_image image;
    image.regions["maincpu"] = encrypted_program(k);
    cps2_system sys(std::move(image), cps2_board_params{.key = k});
    auto& bus = sys.bus();

    bus.write16_be(cps2::object_ram_base, 0x1111U);     // bank 0, offset 0
    bus.write16_be(cps2::object_ram_alt_base, 0x2222U); // bank 1, offset 0

    CHECK(bus.read16_be(cps2::object_ram_base) == 0x1111U);
    CHECK(bus.read16_be(cps2::object_ram_alt_base) == 0x2222U);
    CHECK(bus.read16_be(cps2::object_ram_base + cps2::object_bank_bytes) == 0xFFFFU);

    bus.write16_be(cps2::object_ram_alt_base + cps2::object_bank_bytes, 0x3333U);
    CHECK(bus.read16_be(cps2::object_ram_alt_base + cps2::object_bank_bytes) == 0x3333U);
    CHECK(bus.read16_be(cps2::object_ram_alt_base) == 0x3333U);
    bus.write32_be(cps2::object_ram_alt_base + 0x2018U, 0x89ABCDEFU);
    CHECK(bus.read32_be(cps2::object_ram_alt_base + 0x18U) == 0x89ABCDEFU);
    CHECK(bus.read32_be(cps2::object_ram_alt_base + 0x6018U) == 0x89ABCDEFU);

    bus.write8(0x8040E0U, 0x01U); // selected bank flips to bank 1
    CHECK(bus.read16_be(cps2::object_ram_base) == 0x3333U);
    CHECK(bus.read32_be(cps2::object_ram_base + 0x18U) == 0x89ABCDEFU);
    CHECK(bus.read16_be(cps2::object_ram_alt_base) == 0x1111U);

    bus.write16_be(cps2::object_ram_base + 0x10U, 0x4444U);
    CHECK(sys.object_ram()[cps2::object_bank_bytes + 0x10U] == 0x44U);
    CHECK(sys.object_ram()[cps2::object_bank_bytes + 0x11U] == 0x44U);
}

TEST_CASE("cps2 system latches sprites from the selected object bank start",
          "[capcom_cps2][system][video]") {
    const auto k = sample_key();
    rom_set_image image;
    image.regions["maincpu"] = encrypted_program(k, 0x60FEU); // BRA * for frame stepping

    // Sprite tile 1, texel (0,0) = pen 0xA.
    image.regions["gfx"].assign(0x1000U, 0x00U);
    image.regions["gfx"][128U + 1U] = 0x80U;
    image.regions["gfx"][128U + 3U] = 0x80U;

    cps2_system sys(std::move(image), cps2_board_params{.key = k});
    auto& bus = sys.bus();

    constexpr std::uint32_t palette_source = 0x20000U;
    bus.write16_be(cps2::video_ram_base + palette_source + 0x34U, 0xF0F0U);
    bus.write16_be(cps2::video_ram_base + palette_source + 0x54U, 0xFF00U);
    bus.write16_be(cps2::cps_a_base + cps2::cps_a_palette_base * 2U,
                   static_cast<std::uint16_t>((cps2::video_ram_base + palette_source) >> 8U));

    bus.write16_be(cps2::object_ram_base + 0x0U, 0x0000U); // raw_x
    bus.write16_be(cps2::object_ram_base + 0x2U, 0x0000U); // raw_y
    bus.write16_be(cps2::object_ram_base + 0x4U, 0x0001U); // tile
    bus.write16_be(cps2::object_ram_base + 0x6U, 0x0002U); // palette 2 = red
    bus.write16_be(cps2::object_ram_base + 0xAU, 0xFFFFU); // terminator

    constexpr std::uint32_t table_offset = 0x0800U;
    bus.write16_be(cps2::object_ram_base + table_offset + 0x0U, 0x0000U); // raw_x
    bus.write16_be(cps2::object_ram_base + table_offset + 0x2U, 0x0000U); // raw_y
    bus.write16_be(cps2::object_ram_base + table_offset + 0x4U, 0x0001U); // tile
    bus.write16_be(cps2::object_ram_base + table_offset + 0x6U, 0x0001U); // palette 1 = green
    bus.write16_be(cps2::object_ram_base + table_offset + 0xAU, 0xFFFFU); // terminator
    // Real CPS-2 games write nonzero values here, but hardware does not use this
    // CPS-A register as the sprite table base.
    bus.write16_be(cps2::cps_a_base + cps2::cps_a_obj_base * 2U,
                   static_cast<std::uint16_t>((cps2::object_ram_base + table_offset) >> 8U));

    sys.run_frame();

    const auto fb = sys.video().framebuffer();
    REQUIRE(fb.pixels != nullptr);
    CHECK(fb.pixels[0] == 0x00FF0000U);
}

TEST_CASE("cps2 system forwards CPS-A flip-screen control to the video chip",
          "[capcom_cps2][system][video]") {
    const auto k = sample_key();
    rom_set_image image;
    image.regions["maincpu"] = encrypted_program(k, 0x60FEU); // BRA * for frame stepping

    image.regions["gfx"].assign(2U * 128U, 0x00U);
    image.regions["gfx"][128U + 1U] = 0x80U; // sprite tile 1 texel (0,0) = pen 0xA
    image.regions["gfx"][128U + 3U] = 0x80U;

    cps2_system sys(std::move(image), cps2_board_params{.key = k});
    auto& bus = sys.bus();

    constexpr std::uint32_t palette_source = 0x20000U;
    bus.write16_be(cps2::video_ram_base + palette_source + 0x14U, 0xFF00U);
    bus.write16_be(cps2::cps_a_base + cps2::cps_a_palette_base * 2U,
                   static_cast<std::uint16_t>((cps2::video_ram_base + palette_source) >> 8U));
    bus.write16_be(cps2::object_ram_base + 0x0U, 0x0000U); // raw_x
    bus.write16_be(cps2::object_ram_base + 0x2U, 0x0000U); // raw_y
    bus.write16_be(cps2::object_ram_base + 0x4U, 0x0001U); // tile 1
    bus.write16_be(cps2::object_ram_base + 0x6U, 0x0000U); // palette 0
    bus.write16_be(cps2::object_ram_base + 0xAU, 0x8000U); // terminator
    bus.write16_be(cps2::cps_a_base + cps2::cps_a_video_control * 2U, 0x8000U);

    sys.run_frame();

    const auto fb = sys.video().framebuffer();
    REQUIRE(fb.pixels != nullptr);
    CHECK(fb.pixels[0] == 0x00000000U);
    CHECK(fb.pixels[(fb.height - 1U) * fb.width + (fb.width - 1U)] == 0x00FF0000U);
}

TEST_CASE("cps2 system forwards sprite control-register bases to offset-mode objects",
          "[capcom_cps2][system][video]") {
    const auto k = sample_key();
    rom_set_image image;
    image.regions["maincpu"] = encrypted_program(k, 0x60FEU); // BRA * for frame stepping

    image.regions["gfx"].assign(2U * 128U, 0x00U);
    image.regions["gfx"][128U + 0U] = 0x80U; // sprite tile 1 texel (0,0) = pen 1

    cps2_system sys(std::move(image), cps2_board_params{.key = k});
    auto& bus = sys.bus();

    constexpr std::uint32_t palette_source = 0x20000U;
    bus.write16_be(cps2::video_ram_base + palette_source + 0x02U, 0xFF00U);
    bus.write16_be(cps2::cps_a_base + cps2::cps_a_palette_base * 2U,
                   static_cast<std::uint16_t>((cps2::video_ram_base + palette_source) >> 8U));

    bus.write16_be(cps2::control_reg_base + 0x08U, 0x0020U); // sprite x base
    bus.write16_be(cps2::control_reg_base + 0x0AU, 0x0010U); // sprite y base
    bus.write16_be(cps2::object_ram_base + 0x0U, 0x0000U);   // base-relative raw_x
    bus.write16_be(cps2::object_ram_base + 0x2U, 0x0000U);   // base-relative raw_y
    bus.write16_be(cps2::object_ram_base + 0x4U, 0x0001U);   // tile 1
    bus.write16_be(cps2::object_ram_base + 0x6U, 0x0080U);   // attr bit 7: offset mode
    bus.write16_be(cps2::object_ram_base + 0xAU, 0x8000U);   // terminator

    sys.run_frame();

    const auto fb = sys.video().framebuffer();
    REQUIRE(fb.pixels != nullptr);
    CHECK(fb.pixels[0] == 0x00FF0000U);
}

TEST_CASE("cps2 system decodes CPS-A latches into video while QSound advances",
          "[capcom_cps2][system][video]") {
    const auto k = sample_key();
    rom_set_image image;
    image.regions["maincpu"] = encrypted_program(k, 0x60FEU); // BRA * for frame stepping
    image.regions["gfx"].assign(0x10000U, 0xFFU);

    // A tiny Z80 sound program: write $24 to shared comm RAM, then spin. The
    // frame loop must run it when the 68K releases sound reset.
    auto& audio = image.regions["audiocpu"];
    audio.assign(0x8000U, 0x00U);
    const std::array<std::uint8_t, 7> z80_prog{0x3EU, 0x24U,        // LD A,$24
                                               0x32U, 0x00U, 0xC0U, // LD ($C000),A
                                               0x18U, 0xFEU};       // JR $ (spin)
    std::copy(z80_prog.begin(), z80_prog.end(), audio.begin());
    image.regions["qsound"].assign(0x1000U, 0x00U);

    cps2_system sys(std::move(image), cps2_board_params{.key = k});
    auto& bus = sys.bus();

    // The CPS-2 backdrop is the last palette entry (pal_num 0xBF, pen 0xF). Stage
    // the palette source in video RAM, write a full-red backdrop there (the 16-bit
    // brightness:R:G:B word 0xFF00 = full red), and point the CPS-A reg5 palette
    // base at it. cps2_video DMAs + decodes it at vblank.
    constexpr std::uint32_t palette_source = 0x20000U;
    constexpr std::uint32_t backdrop = 0xBFFU * 2U;
    bus.write16_be(cps2::video_ram_base + palette_source + backdrop, 0xFF00U);
    bus.write16_be(cps2::cps_a_base + cps2::cps_a_palette_base * 2U,
                   static_cast<std::uint16_t>((cps2::video_ram_base + palette_source) >> 8U));
    bus.write8(cps2::sound_reset_port, 0x08U); // release sound CPU

    REQUIRE(sys.video().frame_index() == 0U);
    REQUIRE(sys.vblank_irq_raised() == 0U);

    sys.run_frame();

    CHECK(sys.video().frame_index() == 1U);
    CHECK(sys.vblank_irq_raised() == 1U);
    CHECK(sys.vblank_irq_acked() == 0U); // reset SR keeps IPM at 7 in this tiny program
    CHECK(sys.sound_bus().read8(0xC000U) == 0x24U);
    // The backdrop decodes through the CPS-A reg5 palette latch and reaches the
    // framebuffer end-to-end (the scroll layers + sprites render in later increments).
    const auto fb = sys.video().framebuffer();
    CHECK(fb.pixels[0] == 0xFF0000U);
    CHECK(fb.pixels[120U * fb.width + 200U] == 0xFF0000U);
}

TEST_CASE("cps2 system accounts only level-2 acknowledges as vblank IRQs",
          "[capcom_cps2][system]") {
    const auto k = sample_key();
    rom_set_image image;
    image.regions["maincpu"] = encrypted_program(k, 0x60FEU); // BRA * for stable PC setup
    cps2_system sys(std::move(image), cps2_board_params{.key = k});

    const auto unmask_main_irq = [&sys] {
        auto regs = sys.cpu().cpu_registers();
        regs.sr = mnemos::chips::cpu::m68000::sr_s; // supervisor, IPM 0
        regs.a[7] = cps2::main_ram_base + 0x1000U;
        regs.ssp = regs.a[7];
        regs.pc = 0x00000008U;
        sys.cpu().set_registers(regs);
    };

    unmask_main_irq();
    sys.cpu().set_irq_level(4);
    sys.cpu().step_instruction();

    CHECK(sys.vblank_irq_acked() == 0U);
    CHECK(((sys.cpu().cpu_registers().sr >> 8U) & 7U) == 4U);

    unmask_main_irq();
    sys.cpu().set_irq_level(2);
    sys.cpu().step_instruction();

    CHECK(sys.vblank_irq_acked() == 1U);
    CHECK(((sys.cpu().cpu_registers().sr >> 8U) & 7U) == 2U);
}

TEST_CASE("cps2 system forwards CPS-B palette-control masks into video DMA",
          "[capcom_cps2][system][video]") {
    const auto k = sample_key();
    rom_set_image image;
    image.regions["maincpu"] = encrypted_program(k, 0x60FEU); // BRA * for frame stepping
    cps2_system sys(std::move(image), cps2_board_params{.key = k});
    auto& bus = sys.bus();

    constexpr std::uint32_t palette_source = 0x20000U;
    bus.write16_be(cps2::video_ram_base + palette_source + 0x0000U, 0x1111U);
    bus.write16_be(cps2::video_ram_base + palette_source + 0x0400U, 0x2222U);
    bus.write16_be(cps2::cps_a_base + cps2::cps_a_palette_base * 2U,
                   static_cast<std::uint16_t>((cps2::video_ram_base + palette_source) >> 8U));

    bus.write16_be(cps2::cps_b_base + cps2::cps_b_palette_control_word * 2U, 0x0002U);
    sys.run_frame();

    CHECK(sys.video().palette_color(0U) == 0x0000U);
    CHECK(sys.video().palette_color(0x200U) == 0x1111U);
}

TEST_CASE("cps2 system QSound: shared comm RAM, Z80 boot, reset gating", "[capcom_cps2][system]") {
    const auto k = sample_key();
    rom_set_image image;
    image.regions["maincpu"] = encrypted_program(k);
    // A tiny Z80 sound program at $0000: write $42 to the comm RAM ($C000), loop.
    auto& audio = image.regions["audiocpu"];
    audio.assign(0x8000U, 0x00U);
    const std::array<std::uint8_t, 7> z80_prog{0x3EU, 0x42U,        // LD A,$42
                                               0x32U, 0x00U, 0xC0U, // LD ($C000),A
                                               0x18U, 0xFEU};       // JR $ (spin)
    std::copy(z80_prog.begin(), z80_prog.end(), audio.begin());
    image.regions["qsound"].assign(0x1000U, 0x00U); // DL-1425 sample ROM

    cps2_system sys(std::move(image), cps2_board_params{.key = k});
    REQUIRE(sys.has_sound());
    auto& m68k = sys.bus();
    auto& z80 = sys.sound_bus();

    // The comm RAM is shared: the 68K sees the buffer on the ODD byte of $618000.
    z80.write8(0xC000U, 0x5AU);
    CHECK(m68k.read8(0x618001U) == 0x5AU); // 68K odd byte, index 0
    CHECK(m68k.read8(0x618000U) == 0xFFU); // even byte is open bus
    m68k.write8(0x618003U, 0xA5U);         // 68K writes index 1 (odd of $618002)
    CHECK(z80.read8(0xC001U) == 0xA5U);    // Z80 sees it flat

    // Held in reset, the Z80 does not run; once $804041 bit3 is set, it boots and
    // executes its program, writing $42 into the comm RAM.
    sys.run_cycles(100);
    CHECK(z80.read8(0xC000U) == 0x5AU); // unchanged: Z80 still in reset
    m68k.write16_be(0x804040U, 0x0008U); // release the sound-CPU reset via low byte
    CHECK(sys.sound_cpu().cpu_registers().pc == 0x0000U);
    sys.run_cycles(200);
    CHECK(z80.read8(0xC000U) == 0x42U);    // the Z80 ran its program
    CHECK(m68k.read8(0x618001U) == 0x42U); // and the 68K sees it

    auto regs = sys.sound_cpu().cpu_registers();
    regs.pc = 0x1234U;
    sys.sound_cpu().set_registers(regs);
    m68k.write16_be(0x804040U, 0x0008U);
    CHECK(sys.sound_cpu().cpu_registers().pc == 0x1234U);

    z80.write8(0xC000U, 0x00U);
    m68k.write16_be(0x804040U, 0x0000U);
    CHECK(sys.sound_cpu().cpu_registers().pc == 0x0000U);
    sys.run_cycles(200);
    CHECK(z80.read8(0xC000U) == 0x00U);

    // The DL-1425 ready flag is readable at $D007 (deterministic; not the scratch
    // RAM that backs the rest of the $D000 window).
    CHECK(z80.read8(0xD007U) == z80.read8(0xD007U));
}

TEST_CASE("cps2 system records QSound shared-bus diagnostics", "[capcom_cps2][system]") {
    const auto k = sample_key();
    rom_set_image image;
    image.regions["maincpu"] = encrypted_program(k);
    image.regions["audiocpu"].assign(0x8000U, 0x00U);
    image.regions["qsound"].assign(0x1000U, 0x00U);

    cps2_system sys(std::move(image), cps2_board_params{.key = k});
    auto& main = sys.bus();
    auto& sound = sys.sound_bus();

    main.write8(cps2::qsound_shared_base, 0x12U); // even byte: open bus, counted only
    main.write8(cps2::qsound_shared_base + 1U, 0x34U);
    main.write8(cps2::qsound_shared_base + (0x000FU << 1U) + 1U, 0x56U);

    const auto& after_main_writes = sys.qsound_bus_diagnostics();
    CHECK(after_main_writes.shared_68k_even_write_count == 1U);
    CHECK(after_main_writes.shared_68k_even_non_ff_write_count == 1U);
    CHECK(after_main_writes.shared_68k_write_count == 2U);
    CHECK(after_main_writes.shared_68k_non_ff_write_count == 2U);
    CHECK(after_main_writes.shared_68k_command_signal_write_count == 1U);
    CHECK(after_main_writes.shared_last_even_68k_index == 0U);
    CHECK(after_main_writes.shared_last_even_68k_value == 0x12U);
    CHECK(after_main_writes.shared_last_68k_index == 0x000FU);
    CHECK(after_main_writes.shared_last_68k_value == 0x56U);
    CHECK(after_main_writes.shared_command_signal_last_68k_value == 0x56U);
    CHECK(after_main_writes.shared_command_snapshot[0] == 0x34U);
    CHECK(after_main_writes.shared_command_snapshot[0x0FU] == 0x56U);
    CHECK(sys.qsound_shared_ram()[0] == 0x34U);
    CHECK(sys.qsound_shared_ram()[0x0FU] == 0x56U);

    CHECK(main.read8(cps2::qsound_shared_base + (0x0FFFU << 1U) + 1U) == 0x00U);
    CHECK(main.read8(cps2::qsound_shared_base + (0x0FFDU << 1U) + 1U) == 0x00U);
    CHECK(main.read8(cps2::qsound_shared_base + (0x0002U << 1U)) == 0xFFU);

    const auto& after_reads = sys.qsound_bus_diagnostics();
    CHECK(after_reads.shared_68k_read_count == 3U);
    CHECK(after_reads.shared_68k_odd_read_count == 2U);
    CHECK(after_reads.shared_68k_even_read_count == 1U);
    CHECK(after_reads.shared_68k_status_read_count == 1U);
    CHECK(after_reads.shared_68k_magic_read_count == 1U);
    CHECK(after_reads.shared_status_first_read_seen);
    CHECK(after_reads.shared_status_first_read_value == 0x00U);
    CHECK(after_reads.shared_status_last_read_value == 0x00U);
    CHECK(after_reads.shared_last_68k_read_index == 0x0002U);
    CHECK(after_reads.shared_last_68k_read_value == 0xFFU);

    sound.write8(cps2::z80_shared_base + 0x000FU, 0xA5U);
    CHECK(sound.read8(cps2::z80_shared_base + 0x000FU) == 0xA5U);
    sound.write8(cps2::z80_work_base + 0x0023U, 0xC3U);

    const auto& after_z80 = sys.qsound_bus_diagnostics();
    CHECK(after_z80.shared_z80_write_count == 1U);
    CHECK(after_z80.shared_last_z80_addr == cps2::z80_shared_base + 0x000FU);
    CHECK(after_z80.shared_last_z80_value == 0xA5U);
    CHECK(after_z80.shared_z80_command_signal_read_count == 1U);
    CHECK(after_z80.shared_command_signal_last_z80_value == 0xA5U);
    CHECK(after_z80.work_z80_write_count == 1U);
    CHECK(after_z80.work_last_z80_addr == cps2::z80_work_base + 0x0023U);
    CHECK(after_z80.work_last_z80_value == 0xC3U);
}

TEST_CASE("cps2 system maps QSound DSP ports to PCM output",
          "[capcom_cps2][system][sound]") {
    const auto k = sample_key();
    rom_set_image image;
    image.regions["maincpu"] = encrypted_program(k);
    image.regions["audiocpu"].assign(0x8000U, 0x00U);
    image.regions["qsound"].assign(0x20000U, 0x00U);
    image.regions["qsound"][0x10U] = 0x40U;

    cps2_system sys(std::move(image), cps2_board_params{.key = k});
    auto regs = sys.sound_cpu().cpu_registers();
    regs.pc = 0x2468U;
    sys.sound_cpu().set_registers(regs);

    qsound_port_write(sys, 1U, 0x0010U);  // voice 0 address
    qsound_port_write(sys, 2U, 0x0100U);  // voice 0 rate
    qsound_port_write(sys, 5U, 0x1000U);  // voice 0 end address
    qsound_port_write(sys, 6U, 0x4000U);  // voice 0 volume
    CHECK(sys.qsound_dsp().last_register() == 6U);
    CHECK(sys.qsound_dsp().last_register_data() == 0x4000U);
    CHECK(sys.qsound_dsp().last_register_pc() == 0x2468U);
    qsound_port_write(sys, 0x80U, 0x20U); // voice 0 pan, centered

    std::array<std::int16_t, 16> samples{};
    sys.qsound_dsp().generate(samples);

    CHECK(sys.sound_bus().read8(cps2::z80_ready_reg) ==
          mnemos::chips::audio::qsound::ready_flag);
    CHECK(any_nonzero(samples));
}

TEST_CASE("cps2 system maps QSound DSP ports to ADPCM output",
          "[capcom_cps2][system][sound][adpcm]") {
    const auto k = sample_key();
    rom_set_image image;
    image.regions["maincpu"] = encrypted_program(k);
    image.regions["audiocpu"].assign(0x8000U, 0x00U);
    image.regions["qsound"].assign(0x20000U, 0x00U);
    image.regions["qsound"][0x20U] = 0x70U;

    cps2_system sys(std::move(image), cps2_board_params{.key = k});

    qsound_port_write(sys, 0xCAU, 0x0020U); // ADPCM voice 0 start
    qsound_port_write(sys, 0xCBU, 0x0022U); // ADPCM voice 0 end
    qsound_port_write(sys, 0xCCU, 0x8000U); // ADPCM voice 0 bank
    qsound_port_write(sys, 0xD6U, 0x0001U); // trigger voice 0
    qsound_port_write(sys, 0xCDU, 0x4000U); // late volume must still become audible

    std::array<std::int16_t, 32> samples{};
    sys.qsound_dsp().generate(samples);

    CHECK(any_nonzero(samples));
}

TEST_CASE("cps2 system maps compact QSound Z80 banks from the 32 KiB split",
          "[capcom_cps2][system][sound]") {
    const auto k = sample_key();
    rom_set_image image;
    image.regions["maincpu"] = encrypted_program(k);
    auto& audio = image.regions["audiocpu"];
    audio.assign(0x20000U, 0xFFU);
    audio[cps2::z80_bank_rom_base_small + 2U * cps2::z80_bank_window] = 0x5AU;
    audio[cps2::z80_bank_rom_base_large + 2U * cps2::z80_bank_window] = 0xA5U;
    image.regions["qsound"].assign(0x1000U, 0x00U);

    cps2_system sys(std::move(image), cps2_board_params{.key = k});
    sys.sound_bus().write8(cps2::z80_bank_reg, 0x02U);

    CHECK(sys.sound_bus().read8(cps2::z80_bank_base) == 0x5AU);
}

TEST_CASE("cps2 system zero-fills partial expanded QSound Z80 bank regions",
          "[capcom_cps2][system][sound]") {
    const auto k = sample_key();
    rom_set_image image;
    image.regions["maincpu"] = encrypted_program(k);
    auto& audio = image.regions["audiocpu"];
    audio.assign(0x28000U, 0xFFU);
    audio[cps2::z80_bank_rom_base_large + 2U * cps2::z80_bank_window] = 0x5AU;
    image.regions["qsound"].assign(0x1000U, 0x00U);

    cps2_system sys(std::move(image), cps2_board_params{.key = k});
    const auto* sound = sys.rom_set().region("audiocpu");
    REQUIRE(sound != nullptr);
    CHECK(sound->size() == cps2::z80_qsound_cpu_rom_region_size);

    sys.sound_bus().write8(cps2::z80_bank_reg, 0x02U);
    CHECK(sys.sound_bus().read8(cps2::z80_bank_base) == 0x5AU);

    sys.sound_bus().write8(cps2::z80_bank_reg, 0x07U);
    CHECK(sys.sound_bus().read8(cps2::z80_bank_base) == 0x00U);
}

TEST_CASE("cps2 system maps full QSound Z80 banks from the 64 KiB split",
          "[capcom_cps2][system][sound]") {
    const auto k = sample_key();
    rom_set_image image;
    image.regions["maincpu"] = encrypted_program(k);
    auto& audio = image.regions["audiocpu"];
    audio.assign(cps2::z80_qsound_cpu_rom_region_size, 0xFFU);
    audio[cps2::z80_bank_rom_base_small + 2U * cps2::z80_bank_window] = 0xA5U;
    audio[cps2::z80_bank_rom_base_large + 2U * cps2::z80_bank_window] = 0x5AU;
    image.regions["qsound"].assign(0x1000U, 0x00U);

    cps2_system sys(std::move(image), cps2_board_params{.key = k});
    sys.sound_bus().write8(cps2::z80_bank_reg, 0x02U);

    CHECK(sys.sound_bus().read8(cps2::z80_bank_base) == 0x5AU);
}

TEST_CASE("cps2 system clocks the QSound Z80 with reference catch-up cadence",
          "[capcom_cps2][system]") {
    const auto k = sample_key();
    rom_set_image image;
    image.regions["maincpu"] = encrypted_program(k, 0x60FEU); // BRA * for steady 68K cycles
    image.regions["audiocpu"].assign(0x8000U, 0x00U);         // Z80 NOP stream
    image.regions["qsound"].assign(0x1000U, 0x00U);

    cps2_system sys(std::move(image), cps2_board_params{.key = k});
    sys.bus().write8(cps2::sound_reset_port, 0x08U);

    sys.run_cycles(cps2::m68k_clock_hz / 1000U); // 1 ms of 68K time

    const std::uint64_t main_cycles = sys.cpu().elapsed_cycles();
    const std::uint64_t sound_cycles = sys.sound_cpu().elapsed_cycles();
    const std::uint64_t expected =
        main_cycles * cps2::qsound_z80_clock_hz / cps2::m68k_clock_hz;
    INFO("main=" << main_cycles << " sound=" << sound_cycles << " expected=" << expected);

    REQUIRE(main_cycles % 10U == 0U); // BRA * consumes ten 68K cycles.
    // Whole Z80 instructions can overshoot the target by one instruction; CPS2
    // QSound command cadence matches the reference path when each 68K slice
    // discards that local overshoot.
    CHECK(sound_cycles >= expected);
    std::uint64_t reference_sound = 0U;
    std::uint64_t reference_accum = 0U;
    for (std::uint64_t step = 0U; step < main_cycles / 10U; ++step) {
        reference_accum += 10U * cps2::qsound_z80_clock_hz;
        const std::uint64_t due = reference_accum / cps2::m68k_clock_hz;
        reference_accum -= due * cps2::m68k_clock_hz;
        // The NOP-stream Z80 test program consumes whole 4T instructions. CPS2
        // does not carry this per-slice whole-instruction overshoot into the
        // next 68K slice; this is the cadence HSF2's QSound driver expects.
        reference_sound += ((due + 3U) / 4U) * 4U;
    }
    CHECK(sound_cycles == reference_sound);
    CHECK(sys.sound_cycle_debt() == 0);
}

TEST_CASE("cps2 system frame budget follows the CPS2 raster cadence",
          "[capcom_cps2][system]") {
    const auto k = sample_key();
    rom_set_image image;
    image.regions["maincpu"] = encrypted_program(k, 0x60FEU); // BRA * for steady frame timing

    cps2_system sys(std::move(image), cps2_board_params{.key = k});
    sys.run_frame();

    const std::uint64_t expected =
        (static_cast<std::uint64_t>(cps2::m68k_clock_hz) * cps2::refresh_hz_den +
         cps2::refresh_hz_num / 2U) /
        cps2::refresh_hz_num;
    const std::uint64_t elapsed = sys.cpu().elapsed_cycles();
    INFO("elapsed=" << elapsed << " expected=" << expected);

    CHECK(cps2::m68k_clock_hz == 11'800'000U);
    CHECK(cps2::qsound_z80_clock_hz == 8'000'000U);
    CHECK(cps2::refresh_hz_num == 59'637'405U);
    CHECK(cps2::refresh_hz_den == 1'000'000U);
    CHECK(cps2::frame_rate_millihz == 59'637U);
    CHECK(cps2::cpu_cycles_per_frame == expected);
    CHECK(cps2::frame_scanlines == 262U);
    CHECK(cps2::vblank_start_line == 224U);
    CHECK(cps2::cpu_cycles_per_scanline == 755U);

    // Carry final whole-instruction overshoot into the next frame so a long
    // frame sequence stays on the native CPS2 frame budget.
    CHECK(elapsed >= expected);
    CHECK(elapsed <= expected + 64U);

    for (int i = 0; i < 31; ++i) {
        sys.run_frame();
    }
    const std::uint64_t expected_32 = 32ULL * expected;
    const std::uint64_t elapsed_32 = sys.cpu().elapsed_cycles();
    INFO("elapsed_32=" << elapsed_32 << " expected_32=" << expected_32);
    CHECK(elapsed_32 >= expected_32);
    CHECK(elapsed_32 <= expected_32 + 64U);
}

TEST_CASE("cps2 system save/load reproduces whole-board forward evolution",
          "[capcom_cps2][system][save]") {
    const auto k = sample_key();
    cps2_system live(save_state_image(k), cps2_board_params{.key = k});
    cps2_system restored(save_state_image(k), cps2_board_params{.key = k});

    seed_mutable_board_state(live);
    live.run_frame();
    const std::vector<std::uint8_t> saved = snapshot(live);

    live.run_frame();
    const std::vector<std::uint8_t> reference = snapshot(live);

    mnemos::chips::state_reader reader(saved);
    restored.load_state(reader);
    REQUIRE(reader.ok());
    restored.run_frame();

    CHECK(snapshot(restored) == reference);
}

TEST_CASE("cps2 gfx unshuffle de-interleaves 8-byte units recursively", "[capcom_cps2][gfx]") {
    // Eight 8-byte units, unit i tagged with the value i in every byte. The
    // recursive unshuffle is an inverse perfect-shuffle: the result orders the
    // units [0,2,4,6,1,3,5,7] (evens then odds, computed by hand from the
    // reference algorithm).
    std::vector<std::uint8_t> buf(8U * 8U, 0U);
    for (std::uint8_t unit = 0U; unit < 8U; ++unit) {
        std::fill_n(buf.begin() + unit * 8U, 8U, unit);
    }

    cps2_system::unshuffle_gfx_units(std::span<std::uint8_t>(buf));

    const std::array<std::uint8_t, 8> expected{0U, 2U, 4U, 6U, 1U, 3U, 5U, 7U};
    for (std::size_t pos = 0U; pos < expected.size(); ++pos) {
        // Every byte of the unit carries its tag, so checking the first suffices.
        CHECK(buf[pos * 8U] == expected[pos]);
    }

    // A run of two units (or any count not a multiple of four) is already ordered.
    std::vector<std::uint8_t> small{1U, 1U, 1U, 1U, 1U, 1U, 1U, 1U, 2U, 2U, 2U, 2U, 2U, 2U, 2U, 2U};
    const std::vector<std::uint8_t> before = small;
    cps2_system::unshuffle_gfx_units(std::span<std::uint8_t>(small));
    CHECK(small == before);
}
