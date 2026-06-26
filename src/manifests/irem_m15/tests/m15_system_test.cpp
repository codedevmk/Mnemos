#include "file.hpp"
#include "m15_game_manifests.hpp"
#include "m15_system.hpp"
#include "rom_set_toml.hpp"
#include "zip_archive.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#ifndef MNEMOS_IREM_M15_GAMES_DIR
#define MNEMOS_IREM_M15_GAMES_DIR ""
#endif

namespace {

    using mnemos::manifests::common::rom_set_decl;
    using mnemos::manifests::common::rom_set_file;
    using mnemos::manifests::common::rom_set_image;
    using mnemos::manifests::common::rom_set_region;

    [[nodiscard]] std::string read_text_file(const std::filesystem::path& path) {
        std::ifstream in(path, std::ios::binary);
        REQUIRE(in.good());
        std::ostringstream text;
        text << in.rdbuf();
        return text.str();
    }

    [[nodiscard]] const rom_set_region* find_region(const rom_set_decl& decl,
                                                    std::string_view name) noexcept {
        const auto it =
            std::find_if(decl.regions.begin(), decl.regions.end(),
                         [name](const rom_set_region& region) { return region.name == name; });
        return it == decl.regions.end() ? nullptr : &*it;
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

    [[nodiscard]] bool ends_with_zip(std::string_view name) {
        constexpr std::string_view suffix = ".zip";
        if (name.size() < suffix.size()) {
            return false;
        }
        return std::equal(suffix.rbegin(), suffix.rend(), name.rbegin(), [](char lhs, char rhs) {
            const auto l = static_cast<unsigned char>(lhs);
            const auto r = static_cast<unsigned char>(rhs);
            return std::tolower(l) == std::tolower(r);
        });
    }

    [[nodiscard]] bool zip_entry_is_file(const mnemos::compression::zip_entry& entry) noexcept {
        return !entry.name.empty() && entry.name.back() != '/' && entry.name.back() != '\\';
    }

    [[nodiscard]] std::set<std::string, std::less<>> embedded_set_names() {
        std::set<std::string, std::less<>> names;
        for (const auto& [set_name, _] : mnemos::manifests::irem_m15::embedded::game_manifests) {
            names.emplace(std::string{set_name});
        }
        return names;
    }

    [[nodiscard]] std::optional<std::string>
    single_nested_zip_set_id(std::span<const std::uint8_t> bytes) {
        auto archive = mnemos::compression::zip_archive::open(bytes);
        if (!archive.has_value()) {
            return std::nullopt;
        }

        const mnemos::compression::zip_entry* nested = nullptr;
        std::size_t file_count = 0U;
        for (const auto& entry : archive->entries()) {
            if (!zip_entry_is_file(entry)) {
                continue;
            }
            ++file_count;
            if (ends_with_zip(entry.name)) {
                nested = &entry;
            }
        }
        if (file_count != 1U || nested == nullptr) {
            return std::nullopt;
        }
        return std::filesystem::path{nested->name}.stem().string();
    }

    [[nodiscard]] std::optional<std::vector<std::uint8_t>>
    extract_single_nested_zip(std::span<const std::uint8_t> bytes) {
        auto archive = mnemos::compression::zip_archive::open(bytes);
        if (!archive.has_value()) {
            return std::nullopt;
        }

        const mnemos::compression::zip_entry* nested = nullptr;
        std::size_t file_count = 0U;
        for (const auto& entry : archive->entries()) {
            if (!zip_entry_is_file(entry)) {
                continue;
            }
            ++file_count;
            if (ends_with_zip(entry.name)) {
                nested = &entry;
            }
        }
        if (file_count != 1U || nested == nullptr) {
            return std::nullopt;
        }
        return archive->extract(*nested);
    }

    [[nodiscard]] std::vector<std::filesystem::path> source_roots(const char* env_value) {
        std::vector<std::filesystem::path> roots;
        if (env_value == nullptr || *env_value == '\0') {
            return roots;
        }
#if defined(_WIN32)
        constexpr char separator = ';';
#else
        constexpr char separator = ':';
#endif
        std::string_view text{env_value};
        std::size_t start = 0U;
        while (start <= text.size()) {
            const std::size_t end = text.find(separator, start);
            const std::string_view part = text.substr(
                start, end == std::string_view::npos ? std::string_view::npos : end - start);
            if (!part.empty()) {
                roots.emplace_back(std::string{part});
            }
            if (end == std::string_view::npos) {
                break;
            }
            start = end + 1U;
        }
        return roots;
    }

    [[nodiscard]] std::optional<std::string> environment_value(const char* name) {
#if defined(_WIN32)
        char* raw = nullptr;
        std::size_t size = 0U;
        if (_dupenv_s(&raw, &size, name) != 0 || raw == nullptr) {
            return std::nullopt;
        }
        std::string value{raw, size > 0U ? size - 1U : 0U};
        std::free(raw);
        return value;
#else
        const char* raw = std::getenv(name);
        return raw != nullptr ? std::optional<std::string>{raw} : std::nullopt;
#endif
    }

    [[nodiscard]] std::optional<std::string>
    identify_source(const std::filesystem::path& path,
                    const std::set<std::string, std::less<>>& known) {
        std::error_code ec;
        if (std::filesystem::is_directory(path, ec)) {
            const std::string set_id = path.filename().string();
            return known.contains(set_id) ? std::optional<std::string>{set_id} : std::nullopt;
        }
        if (!std::filesystem::is_regular_file(path, ec) ||
            !ends_with_zip(path.filename().string())) {
            return std::nullopt;
        }

        const std::string stem = path.stem().string();
        if (known.contains(stem)) {
            return stem;
        }

        auto bytes = mnemos::io::read_file(path.string());
        if (!bytes.has_value()) {
            return std::nullopt;
        }
        auto nested = single_nested_zip_set_id(*bytes);
        if (!nested.has_value() || !known.contains(*nested)) {
            return std::nullopt;
        }
        return nested;
    }

    [[nodiscard]] std::map<std::string, std::filesystem::path, std::less<>>
    index_source_roots(const std::vector<std::filesystem::path>& roots) {
        const auto known = embedded_set_names();
        std::map<std::string, std::filesystem::path, std::less<>> sources;

        auto maybe_add = [&](const std::filesystem::path& path) {
            auto set_id = identify_source(path, known);
            if (set_id.has_value() && sources.find(*set_id) == sources.end()) {
                sources.emplace(std::move(*set_id), path);
            }
        };

        for (const auto& root : roots) {
            std::error_code ec;
            if (std::filesystem::is_regular_file(root, ec)) {
                maybe_add(root);
                continue;
            }
            if (!std::filesystem::is_directory(root, ec)) {
                continue;
            }

            maybe_add(root);
            std::vector<std::filesystem::path> candidates;
            for (std::filesystem::recursive_directory_iterator it{root, ec}, end; !ec && it != end;
                 it.increment(ec)) {
                candidates.push_back(it->path());
            }
            std::sort(candidates.begin(), candidates.end());
            for (const auto& path : candidates) {
                maybe_add(path);
            }
        }

        return sources;
    }

    [[nodiscard]] rom_set_decl require_embedded_decl(std::string_view set_name) {
        const std::string_view text = mnemos::manifests::irem_m15::game_manifest_toml(set_name);
        REQUIRE_FALSE(text.empty());
        auto parsed = mnemos::manifests::common::parse_rom_set_decl(text, std::string{set_name});
        for (const auto& error : parsed.errors) {
            INFO(error.source << ":" << error.line << ":" << error.column << ": " << error.message);
        }
        REQUIRE(parsed.ok());
        return std::move(*parsed.value);
    }

    [[nodiscard]] mnemos::manifests::common::rom_file_provider
    require_provider(const std::filesystem::path& path) {
        std::error_code ec;
        if (std::filesystem::is_directory(path, ec)) {
            return mnemos::manifests::common::make_directory_rom_provider(path.string());
        }

        auto bytes = mnemos::io::read_file(path.string());
        REQUIRE(bytes.has_value());
        if (auto nested = extract_single_nested_zip(*bytes)) {
            bytes = std::move(*nested);
        }

        auto provider = mnemos::manifests::common::make_zip_rom_provider(std::move(*bytes));
        REQUIRE(provider.has_value());
        return std::move(*provider);
    }

    [[nodiscard]] bool has_non_fill_byte(const std::vector<std::uint8_t>& bytes,
                                         std::uint8_t fill = 0xFFU) {
        return std::any_of(bytes.begin(), bytes.end(),
                           [fill](std::uint8_t byte) { return byte != fill; });
    }

    void require_loaded_region(const rom_set_image& image, std::string_view name,
                               std::size_t expected_size) {
        const auto* region = image.region(name);
        REQUIRE(region != nullptr);
        CHECK(region->size() == expected_size);
        CHECK(has_non_fill_byte(*region));
    }

    [[nodiscard]] std::vector<std::uint8_t>
    make_m15_program(const std::vector<std::uint8_t>& program) {
        std::vector<std::uint8_t> rom(mnemos::manifests::irem_m15::main_rom_size, 0xFFU);
        REQUIRE(program.size() <= rom.size());
        std::copy(program.begin(), program.end(), rom.begin());
        return rom;
    }

    [[nodiscard]] std::vector<std::uint8_t> synthetic_m15_program() {
        return make_m15_program({0x3EU, 0x42U,       // MVI A,$42
                                 0x32U, 0x00U, 0x20U, // STA $2000
                                 0x3EU, 0x81U,       // MVI A,$81
                                 0x32U, 0x00U, 0x24U, // STA $2400
                                 0xD3U, 0x00U,       // OUT $00
                                 0x76U});            // HLT
    }

    [[nodiscard]] rom_set_image synthetic_m15_image() {
        rom_set_image image;
        image.regions.emplace("maincpu", synthetic_m15_program());
        return image;
    }

    struct i8080_harness final {
        std::array<std::uint8_t, 0x10000U> memory{};
        std::vector<std::pair<std::uint16_t, std::uint8_t>> port_writes;
        mnemos::manifests::irem_m15::m15_i8080_cpu cpu;

        i8080_harness() {
            cpu.set_memory(
                [this](std::uint16_t address) { return memory[address]; },
                [this](std::uint16_t address, std::uint8_t value) { memory[address] = value; });
            cpu.set_ports(
                [](std::uint16_t port) {
                    return static_cast<std::uint8_t>(0xA0U | (port & 0x0FU));
                },
                [this](std::uint16_t port, std::uint8_t value) {
                    port_writes.emplace_back(port, value);
                });
        }

        void run(const std::vector<std::uint8_t>& program, std::uint64_t cycles = 20000U) {
            memory.fill(0U);
            REQUIRE(program.size() <= memory.size());
            std::copy(program.begin(), program.end(), memory.begin());
            port_writes.clear();
            cpu.reset(mnemos::chips::reset_kind::power_on);
            cpu.tick(cycles);
        }

        [[nodiscard]] std::uint8_t ram(std::uint16_t address) const noexcept {
            return memory[address];
        }

        [[nodiscard]] std::uint64_t reg(std::string_view name) {
            for (const auto& desc : cpu.register_snapshot()) {
                if (desc.name == name) {
                    return desc.value;
                }
            }
            FAIL("missing i8080 register snapshot entry");
            return 0U;
        }
    };

    class program_builder final {
      public:
        void label(std::string name) { labels_.emplace(std::move(name), bytes_.size()); }

        void emit(std::uint8_t value) { bytes_.push_back(value); }

        void emit(std::initializer_list<std::uint8_t> values) {
            bytes_.insert(bytes_.end(), values.begin(), values.end());
        }

        void ref16(std::string name) {
            patches_.push_back({bytes_.size(), std::move(name)});
            bytes_.push_back(0U);
            bytes_.push_back(0U);
        }

        void pad_to(std::size_t offset) {
            REQUIRE(bytes_.size() <= offset);
            bytes_.resize(offset, 0x00U);
        }

        [[nodiscard]] std::vector<std::uint8_t> finish() {
            for (const auto& patch : patches_) {
                const auto label_it = labels_.find(patch.label);
                REQUIRE(label_it != labels_.end());
                const auto address = static_cast<std::uint16_t>(label_it->second);
                bytes_[patch.offset] = static_cast<std::uint8_t>(address);
                bytes_[patch.offset + 1U] = static_cast<std::uint8_t>(address >> 8U);
            }
            return bytes_;
        }

      private:
        struct patch_ref final {
            std::size_t offset{};
            std::string label;
        };

        std::vector<std::uint8_t> bytes_;
        std::map<std::string, std::size_t, std::less<>> labels_;
        std::vector<patch_ref> patches_;
    };

    [[nodiscard]] bool
    framebuffer_has_nonblack(const mnemos::chips::frame_buffer_view& frame) {
        REQUIRE(frame.pixels != nullptr);
        REQUIRE(frame.width > 0U);
        REQUIRE(frame.height > 0U);
        const std::uint32_t stride = frame.effective_stride();
        for (std::uint32_t y = 0; y < frame.height; ++y) {
            for (std::uint32_t x = 0; x < frame.width; ++x) {
                if (frame.pixels[static_cast<std::size_t>(y) * stride + x] != 0U) {
                    return true;
                }
            }
        }
        return false;
    }

} // namespace

TEST_CASE("m15 checked-in game manifests parse and cover local candidate corpus",
          "[m15][romset]") {
    namespace fs = std::filesystem;

    const fs::path games_dir{MNEMOS_IREM_M15_GAMES_DIR};
    REQUIRE_FALSE(games_dir.empty());
    REQUIRE(fs::exists(games_dir));

    std::map<std::string, rom_set_decl, std::less<>> declarations;
    for (const fs::directory_entry& entry : fs::directory_iterator(games_dir)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".toml") {
            continue;
        }
        const std::string text = read_text_file(entry.path());
        auto parsed =
            mnemos::manifests::common::parse_rom_set_decl(text, entry.path().filename().string());
        for (const auto& error : parsed.errors) {
            INFO(error.source << ":" << error.line << ":" << error.column << ": " << error.message);
        }
        REQUIRE(parsed.ok());

        const rom_set_decl& decl = *parsed.value;
        INFO("set=" << decl.name);
        declarations.emplace(decl.name, std::move(*parsed.value));
    }

    const std::set<std::string, std::less<>> expected_names{"headoni"};
    std::set<std::string, std::less<>> names;
    for (const auto& [set_name, decl] : declarations) {
        INFO("set=" << set_name);
        names.insert(decl.name);
        CHECK(decl.board == "irem_m15");
        CHECK(decl.orientation == mnemos::manifests::common::screen_orientation::vertical);

        const rom_set_region* maincpu = find_region(decl, "maincpu");
        REQUIRE(maincpu != nullptr);
        CHECK(maincpu->size == mnemos::manifests::irem_m15::main_rom_size);
        REQUIRE(maincpu->files.size() == 6U);
        require_region_contract(*maincpu);
        for (std::size_t i = 0U; i < maincpu->files.size(); ++i) {
            CHECK(maincpu->files[i].offset == i * mnemos::manifests::irem_m15::program_rom_size);
        }
    }

    CHECK(names == expected_names);
    CHECK(mnemos::manifests::irem_m15::board_params_for("headoni").rom_layout ==
          "head_on_i8080");
}

TEST_CASE("m15 embedded game manifests mirror the checked-in roster", "[m15][romset]") {
    using mnemos::manifests::irem_m15::embedded::game_manifests;

    CHECK(game_manifests.size() == 1U);
    CHECK_FALSE(mnemos::manifests::irem_m15::game_manifest_toml("headoni").empty());
    CHECK(mnemos::manifests::irem_m15::game_manifest_toml("rtype").empty());
}

TEST_CASE("m15 i8080 executes expanded data stack and ALU opcode groups", "[m15][i8080]") {
    i8080_harness h;
    h.run({
        0x31U, 0xF0U, 0x23U,             // LXI SP,$23F0
        0x01U, 0x00U, 0x20U,             // LXI B,$2000
        0x3EU, 0x12U,                    // MVI A,$12
        0x02U,                           // STAX B
        0x0AU,                           // LDAX B
        0x32U, 0x01U, 0x20U,             // STA $2001
        0x21U, 0x78U, 0x56U,             // LXI H,$5678
        0x22U, 0x02U, 0x20U,             // SHLD $2002
        0x21U, 0x00U, 0x00U,             // LXI H,$0000
        0x2AU, 0x02U, 0x20U,             // LHLD $2002
        0x7CU,                           // MOV A,H
        0x32U, 0x04U, 0x20U,             // STA $2004
        0x7DU,                           // MOV A,L
        0x32U, 0x05U, 0x20U,             // STA $2005
        0xEBU,                           // XCHG
        0x7AU,                           // MOV A,D
        0x32U, 0x06U, 0x20U,             // STA $2006
        0x7BU,                           // MOV A,E
        0x32U, 0x07U, 0x20U,             // STA $2007
        0x21U, 0xBCU, 0x9AU,             // LXI H,$9ABC
        0xE5U,                           // PUSH H
        0x21U, 0x00U, 0x00U,             // LXI H,$0000
        0xE1U,                           // POP H
        0x7CU,                           // MOV A,H
        0x32U, 0x08U, 0x20U,             // STA $2008
        0x7DU,                           // MOV A,L
        0x32U, 0x09U, 0x20U,             // STA $2009
        0x3EU, 0x81U,                    // MVI A,$81
        0x07U, 0x17U, 0x0FU, 0x1FU,       // RLC; RAL; RRC; RAR
        0x32U, 0x0AU, 0x20U,             // STA $200A
        0x3EU, 0x09U,                    // MVI A,$09
        0xC6U, 0x09U,                    // ADI $09
        0x27U,                           // DAA -> $18
        0x32U, 0x0BU, 0x20U,             // STA $200B
        0x3EU, 0xFEU,                    // MVI A,$FE
        0xC6U, 0x02U,                    // ADI $02 -> carry
        0xCEU, 0x00U,                    // ACI $00 -> $01
        0x32U, 0x0CU, 0x20U,             // STA $200C
        0x3EU, 0x10U,                    // MVI A,$10
        0xD6U, 0x01U,                    // SUI $01
        0xDEU, 0x0FU,                    // SBI $0F -> $00
        0x32U, 0x0DU, 0x20U,             // STA $200D
        0x3EU, 0xF0U,                    // MVI A,$F0
        0xE6U, 0x0FU,                    // ANI $0F
        0xF6U, 0x80U,                    // ORI $80
        0xEEU, 0xFFU,                    // XRI $FF
        0xFEU, 0x7FU,                    // CPI $7F
        0xF5U,                           // PUSH PSW
        0x3EU, 0x00U,                    // MVI A,$00
        0xC6U, 0x01U,                    // ADI $01
        0xF1U,                           // POP PSW
        0x32U, 0x0EU, 0x20U,             // STA $200E
        0x21U, 0x10U, 0x20U,             // LXI H,$2010
        0x36U, 0x0FU,                    // MVI M,$0F
        0x34U,                           // INR M
        0x35U,                           // DCR M
        0x7EU,                           // MOV A,M
        0x32U, 0x0FU, 0x20U,             // STA $200F
        0xDBU, 0x03U,                    // IN $03
        0x32U, 0x10U, 0x20U,             // STA $2010
        0x3EU, 0x5AU,                    // MVI A,$5A
        0xD3U, 0x00U,                    // OUT $00
        0x76U,                           // HLT
    });

    CHECK(h.ram(0x2000U) == 0x12U);
    CHECK(h.ram(0x2001U) == 0x12U);
    CHECK(h.ram(0x2002U) == 0x78U);
    CHECK(h.ram(0x2003U) == 0x56U);
    CHECK(h.ram(0x2004U) == 0x56U);
    CHECK(h.ram(0x2005U) == 0x78U);
    CHECK(h.ram(0x2006U) == 0x56U);
    CHECK(h.ram(0x2007U) == 0x78U);
    CHECK(h.ram(0x2008U) == 0x9AU);
    CHECK(h.ram(0x2009U) == 0xBCU);
    CHECK(h.ram(0x200AU) == 0xC1U);
    CHECK(h.ram(0x200BU) == 0x18U);
    CHECK(h.ram(0x200CU) == 0x01U);
    CHECK(h.ram(0x200DU) == 0x00U);
    CHECK(h.ram(0x200EU) == 0x7FU);
    CHECK(h.ram(0x200FU) == 0x0FU);
    CHECK(h.ram(0x2010U) == 0xA3U);
    REQUIRE(h.port_writes.size() == 1U);
    CHECK(h.port_writes[0] == std::make_pair<std::uint16_t, std::uint8_t>(0U, 0x5AU));
    CHECK(h.reg("F") == 0x14U);
    CHECK(h.cpu.unsupported_opcode_count() == 0U);
}

TEST_CASE("m15 i8080 executes conditional jumps calls and returns", "[m15][i8080]") {
    program_builder p;
    p.emit({0x31U, 0xF0U, 0x23U, 0x3EU, 0x00U, 0xFEU, 0x00U}); // Z set
    p.emit(0xC4U);                                             // CNZ fail
    p.ref16("fail");
    p.emit(0xCCU); // CZ mark_z
    p.ref16("mark_z");
    p.emit(0xC2U); // JNZ fail
    p.ref16("fail");
    p.emit(0xCAU); // JZ after_z
    p.ref16("after_z");

    p.label("fail");
    p.emit({0x3EU, 0xEEU, 0x32U, 0x12U, 0x20U, 0x76U});

    p.label("after_z");
    p.emit({0x3EU, 0xFEU, 0xC6U, 0x02U}); // A=0, carry set
    p.emit(0xD2U);                        // JNC fail
    p.ref16("fail");
    p.emit(0xDAU); // JC after_c
    p.ref16("after_c");

    p.label("after_c");
    p.emit(0xCDU); // CALL cond_ret
    p.ref16("cond_ret");
    p.emit({0x3EU, 0x33U, 0x32U, 0x13U, 0x20U, 0x76U});

    p.label("mark_z");
    p.emit({0x3EU, 0x11U, 0x32U, 0x11U, 0x20U, 0xC9U});

    p.label("cond_ret");
    p.emit({0x3EU, 0x44U, 0x37U, 0xD8U, 0x3EU, 0xEEU, 0x32U, 0x12U, 0x20U,
            0xC9U});

    i8080_harness h;
    h.run(p.finish());

    CHECK(h.ram(0x2011U) == 0x11U);
    CHECK(h.ram(0x2012U) == 0x00U);
    CHECK(h.ram(0x2013U) == 0x33U);
    CHECK(h.reg("A") == 0x33U);
    CHECK(h.cpu.unsupported_opcode_count() == 0U);
}

TEST_CASE("m15 i8080 executes RST vector calls", "[m15][i8080]") {
    program_builder p;
    p.emit(0xC3U); // JMP start
    p.ref16("start");
    p.pad_to(0x10U);

    p.label("start");
    p.emit({0x31U, 0xF0U, 0x23U, // LXI SP,$23F0
            0xFFU,               // RST 7 -> $0038
            0x3EU, 0x66U,        // MVI A,$66
            0x32U, 0x14U, 0x20U, // STA $2014
            0x76U});             // HLT

    p.pad_to(0x38U);
    p.emit({0x3EU, 0x55U,        // MVI A,$55
            0x32U, 0x15U, 0x20U, // STA $2015
            0xC9U});             // RET

    i8080_harness h;
    h.run(p.finish());

    CHECK(h.ram(0x2014U) == 0x66U);
    CHECK(h.ram(0x2015U) == 0x55U);
    CHECK(h.cpu.unsupported_opcode_count() == 0U);
}

TEST_CASE("m15 local artifacts load CRC-clean through embedded manifests",
          "[m15][romset][data]") {
    namespace m15 = mnemos::manifests::irem_m15;

    const auto dir_env = environment_value("MNEMOS_M15_SET_DIR");
    if (!dir_env.has_value() || dir_env->empty()) {
        SKIP("set MNEMOS_M15_SET_DIR to directories containing the M15 zip/folder corpus");
    }

    const auto roots = source_roots(dir_env->c_str());
    REQUIRE_FALSE(roots.empty());
    for (const auto& root : roots) {
        INFO("root=" << root.string());
        REQUIRE(std::filesystem::exists(root));
    }

    const auto indexed_sources = index_source_roots(roots);
    const auto expected_sets = embedded_set_names();
    std::vector<std::string> missing_sets;
    for (const auto& set_name : expected_sets) {
        if (indexed_sources.find(set_name) == indexed_sources.end()) {
            missing_sets.push_back(set_name);
        }
    }
    if (!missing_sets.empty()) {
        std::ostringstream missing;
        for (std::size_t i = 0; i < missing_sets.size(); ++i) {
            if (i != 0U) {
                missing << ", ";
            }
            missing << missing_sets[i];
        }
        FAIL("missing M15 artifacts: " << missing.str());
    }

    for (const auto& set_name : expected_sets) {
        INFO("set=" << set_name);
        const auto decl = require_embedded_decl(set_name);
        const auto source_it = indexed_sources.find(set_name);
        REQUIRE(source_it != indexed_sources.end());
        INFO("source=" << source_it->second.string());

        const auto image =
            mnemos::manifests::common::load_rom_set(decl, require_provider(source_it->second));
        for (const auto& issue : image.issues) {
            INFO(issue.file << ": " << issue.message);
        }
        CHECK(image.issues.empty());

        require_loaded_region(image, "maincpu", m15::main_rom_size);
    }
}

TEST_CASE("m15 executable board runs first-pass i8080 memory and video path", "[m15][board]") {
    namespace m15 = mnemos::manifests::irem_m15;

    auto system = m15::assemble_m15(synthetic_m15_image(), m15::board_params_for("headoni"));
    REQUIRE(system != nullptr);

    system->run_frame();

    CHECK(system->work_ram[0] == 0x42U);
    CHECK(system->video_ram[0] == 0x81U);
    CHECK(system->speaker_latch == 0x81U);
    CHECK(system->main_cpu.unsupported_opcode_count() == 0U);
    CHECK(system->video.framebuffer().width == m15::visible_width);
    CHECK(system->video.framebuffer().height == m15::visible_height);
    CHECK(framebuffer_has_nonblack(system->video.framebuffer()));
}

TEST_CASE("m15 save state preserves board identity and runtime state", "[m15][board]") {
    namespace m15 = mnemos::manifests::irem_m15;

    auto source = m15::assemble_m15(synthetic_m15_image(), m15::board_params_for("headoni"));
    REQUIRE(source != nullptr);
    source->set_inputs(0xEEU, 0xDDU, 0xCCU);
    source->run_frame();
    source->work_ram[3] = 0x5AU;

    std::vector<std::uint8_t> state;
    mnemos::chips::state_writer writer(state);
    source->save_state(writer);
    REQUIRE_FALSE(state.empty());

    auto restored = m15::assemble_m15(synthetic_m15_image(), m15::board_params_for("headoni"));
    REQUIRE(restored != nullptr);
    mnemos::chips::state_reader reader(state);
    restored->load_state(reader);
    CHECK(reader.ok());
    CHECK(restored->work_ram[0] == 0x42U);
    CHECK(restored->work_ram[3] == 0x5AU);
    CHECK(restored->video_ram[0] == 0x81U);
    CHECK(restored->input_p1 == 0xEEU);
    CHECK(restored->input_p2 == 0xDDU);
    CHECK(restored->input_system == 0xCCU);

    auto incompatible =
        m15::assemble_m15(synthetic_m15_image(), m15::m15_board_params{.dip_default = 0x7FU});
    REQUIRE(incompatible != nullptr);
    mnemos::chips::state_reader rejected(state);
    incompatible->load_state(rejected);
    CHECK_FALSE(rejected.ok());
}
