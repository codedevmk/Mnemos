// Functional V30 conformance against the public per-instruction JSON corpus
// for the 8088/V20 CPU family (initial/final register + RAM images per test).
// Data-gated by MNEMOS_V30_TESTS_DIR (see THIRD-PARTY.md); files may be plain
// .json or gzipped .json.gz (decoded with the in-tree inflate). When the
// corpus ships a metadata.json, its per-opcode (and per-group-reg) status and
// flags-mask fields drive test selection and the flags compare; the optional
// MNEMOS_V30_FLAGS_MASK (hex) is ANDed on top. Cycle-count and bus-order
// comparison arrive with the timing increment (plan A3/A4).
//
// Known deviation (the corpus's remaining red): on a divide fault the V20
// pushes flags carrying the internal division algorithm's residue; Mnemos
// pushes the pre-instruction flags. Only the two pushed flag bytes differ
// (DIV/IDIV trap cases in F6.6/F6.7/F7.6/F7.7). Modelling the microcoded
// division flag residue is deferred with the cycle-accuracy work.

#include "v30.hpp"

#include "ibus.hpp"
#include "inflate.hpp"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace {

    using mnemos::chips::cpu::v30;

    // Flat 1 MiB physical memory with a write log so each test's footprint can
    // be reverted without re-clearing the whole array.
    class corpus_bus final : public mnemos::chips::ibus {
      public:
        std::vector<std::uint8_t> memory = std::vector<std::uint8_t>(0x100000U, 0U);
        std::vector<std::uint32_t> dirty;

        [[nodiscard]] std::uint8_t read8(std::uint32_t address) override {
            return memory[address & 0xFFFFFU];
        }

        void write8(std::uint32_t address, std::uint8_t value) override {
            const std::uint32_t addr = address & 0xFFFFFU;
            memory[addr] = value;
            dirty.push_back(addr);
        }

        void poke(std::uint32_t address, std::uint8_t value) {
            const std::uint32_t addr = address & 0xFFFFFU;
            memory[addr] = value;
            dirty.push_back(addr);
        }

        void revert() {
            for (const std::uint32_t addr : dirty) {
                memory[addr] = 0U;
            }
            dirty.clear();
        }
    };

    [[nodiscard]] std::optional<std::vector<std::uint8_t>>
    read_file_bytes(const std::filesystem::path& path) {
        std::ifstream stream(path, std::ios::binary);
        if (!stream) {
            return std::nullopt;
        }
        std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(stream)),
                                        std::istreambuf_iterator<char>{});
        return bytes;
    }

    // Minimal gzip unwrap over the in-tree raw-DEFLATE decoder: parse the
    // member header, inflate the payload into a buffer sized from the
    // trailer's ISIZE field.
    [[nodiscard]] std::optional<std::vector<std::uint8_t>>
    gunzip(std::span<const std::uint8_t> bytes) {
        if (bytes.size() < 18U || bytes[0] != 0x1FU || bytes[1] != 0x8BU || bytes[2] != 0x08U) {
            return std::nullopt;
        }
        const std::uint8_t flags = bytes[3];
        std::size_t offset = 10U;
        if ((flags & 0x04U) != 0U) { // FEXTRA
            if (offset + 2U > bytes.size()) {
                return std::nullopt;
            }
            const std::size_t extra = bytes[offset] | (bytes[offset + 1U] << 8U);
            offset += 2U + extra;
        }
        for (const std::uint8_t string_flag : {std::uint8_t{0x08U}, std::uint8_t{0x10U}}) {
            if ((flags & string_flag) != 0U) { // FNAME / FCOMMENT: NUL-terminated
                while (offset < bytes.size() && bytes[offset] != 0U) {
                    ++offset;
                }
                ++offset;
            }
        }
        if ((flags & 0x02U) != 0U) { // FHCRC
            offset += 2U;
        }
        if (offset + 8U > bytes.size()) {
            return std::nullopt;
        }
        const std::size_t trailer = bytes.size() - 8U;
        const std::uint32_t isize = static_cast<std::uint32_t>(bytes[trailer + 4U]) |
                                    (static_cast<std::uint32_t>(bytes[trailer + 5U]) << 8U) |
                                    (static_cast<std::uint32_t>(bytes[trailer + 6U]) << 16U) |
                                    (static_cast<std::uint32_t>(bytes[trailer + 7U]) << 24U);
        std::vector<std::uint8_t> out(isize);
        const auto written =
            mnemos::compression::inflate_raw(bytes.subspan(offset, trailer - offset), out);
        if (!written.has_value() || *written != out.size()) {
            return std::nullopt;
        }
        return out;
    }

    // Per-opcode corpus metadata: whether to run it and which flag bits the
    // hardware defines (undefined bits are masked out of the compare).
    struct opcode_meta final {
        bool skip{};
        std::uint16_t flags_mask{0xFFFFU};
        bool per_reg{};
        std::array<bool, 8> reg_skip{};
        std::array<std::uint16_t, 8> reg_mask{0xFFFFU, 0xFFFFU, 0xFFFFU, 0xFFFFU,
                                              0xFFFFU, 0xFFFFU, 0xFFFFU, 0xFFFFU};
    };

    [[nodiscard]] bool status_runs(const std::string& status) {
        // alias behaves like its primary; undocumented V20 behaviour and the
        // FPU escapes are later increments.
        return status == "normal" || status == "alias";
    }

    [[nodiscard]] std::map<std::string, opcode_meta>
    load_metadata(const std::filesystem::path& dir) {
        std::map<std::string, opcode_meta> result;
        std::ifstream stream(dir / "metadata.json");
        if (!stream) {
            return result;
        }
        const nlohmann::json doc = nlohmann::json::parse(stream, nullptr, false);
        if (doc.is_discarded() || !doc.contains("opcodes")) {
            return result;
        }
        for (const auto& [key, entry] : doc.at("opcodes").items()) {
            opcode_meta meta;
            if (entry.contains("reg")) {
                meta.per_reg = true;
                meta.reg_skip.fill(true);
                for (const auto& [reg_key, reg_entry] : entry.at("reg").items()) {
                    const std::size_t reg = std::strtoul(reg_key.c_str(), nullptr, 10) & 7U;
                    meta.reg_skip[reg] =
                        !status_runs(reg_entry.value("status", std::string{"normal"}));
                    meta.reg_mask[reg] =
                        static_cast<std::uint16_t>(reg_entry.value("flags-mask", 0xFFFFU));
                }
            } else {
                meta.skip = !status_runs(entry.value("status", std::string{"normal"}));
                meta.flags_mask = static_cast<std::uint16_t>(entry.value("flags-mask", 0xFFFFU));
            }
            result.emplace(key, std::move(meta));
        }
        return result;
    }

    // The modrm reg field of a test's instruction (for per-reg group masks):
    // skip prefixes, skip the opcode byte(s), read bits 5-3 of the modrm.
    [[nodiscard]] std::size_t reg_field(const nlohmann::json& bytes) {
        std::size_t i = 0;
        const auto is_prefix = [](std::uint8_t b) {
            return b == 0x26U || b == 0x2EU || b == 0x36U || b == 0x3EU || b == 0x64U ||
                   b == 0x65U || b == 0xF0U || b == 0xF2U || b == 0xF3U;
        };
        while (i < bytes.size() && is_prefix(bytes.at(i).get<std::uint8_t>())) {
            ++i;
        }
        if (i < bytes.size() && bytes.at(i).get<std::uint8_t>() == 0x0FU) {
            ++i; // extension byte follows
        }
        i += 1U; // the opcode itself
        if (i >= bytes.size()) {
            return 0U;
        }
        return (bytes.at(i).get<std::uint8_t>() >> 3U) & 7U;
    }

    // Overlay the registers present in `node` onto `base` (the corpus encodes
    // final state sparsely: only changed registers appear).
    [[nodiscard]] v30::registers read_regs(const nlohmann::json& node, const v30::registers& base) {
        v30::registers r = base;
        const auto get = [&](const char* name, std::uint16_t& slot) {
            if (node.contains(name)) {
                slot = node.at(name).get<std::uint16_t>();
            }
        };
        get("ax", r.ax);
        get("bx", r.bx);
        get("cx", r.cx);
        get("dx", r.dx);
        get("si", r.si);
        get("di", r.di);
        get("bp", r.bp);
        get("sp", r.sp);
        get("ip", r.ip);
        get("cs", r.cs);
        get("ds", r.ds);
        get("es", r.es);
        get("ss", r.ss);
        get("flags", r.flags);
        return r;
    }

    struct file_tally final {
        std::size_t passed{};
        std::size_t failed{};
        std::size_t skipped{};
    };

    file_tally run_file(const std::filesystem::path& path, const opcode_meta& meta,
                        std::uint16_t global_mask, std::vector<std::string>& failures) {
        file_tally tally;
        const auto raw = read_file_bytes(path);
        if (!raw.has_value()) {
            failures.push_back(path.filename().string() + ": unreadable");
            ++tally.failed;
            return tally;
        }
        nlohmann::json doc;
        if (path.extension() == ".gz") {
            const auto inflated = gunzip(*raw);
            if (!inflated.has_value()) {
                failures.push_back(path.filename().string() + ": bad gzip");
                ++tally.failed;
                return tally;
            }
            doc = nlohmann::json::parse(inflated->begin(), inflated->end(), nullptr, false);
        } else {
            doc = nlohmann::json::parse(raw->begin(), raw->end(), nullptr, false);
        }
        if (doc.is_discarded()) {
            failures.push_back(path.filename().string() + ": bad json");
            ++tally.failed;
            return tally;
        }

        corpus_bus bus;
        v30 cpu;
        cpu.attach_bus(bus);

        for (const auto& test : doc) {
            std::uint16_t mask = global_mask;
            if (meta.per_reg) {
                const std::size_t reg = reg_field(test.at("bytes"));
                if (meta.reg_skip[reg]) {
                    ++tally.skipped;
                    continue;
                }
                mask &= meta.reg_mask[reg];
            } else {
                mask &= meta.flags_mask;
            }

            const auto initial = read_regs(test.at("initial").at("regs"), v30::registers{});
            for (const auto& cell : test.at("initial").at("ram")) {
                bus.poke(cell.at(0).get<std::uint32_t>(), cell.at(1).get<std::uint8_t>());
            }
            cpu.set_registers(initial);

            cpu.step_instruction();

            const auto expected = read_regs(test.at("final").at("regs"), initial);
            const auto got = cpu.cpu_registers();
            bool ok = got.ax == expected.ax && got.bx == expected.bx && got.cx == expected.cx &&
                      got.dx == expected.dx && got.si == expected.si && got.di == expected.di &&
                      got.bp == expected.bp && got.sp == expected.sp && got.ip == expected.ip &&
                      got.cs == expected.cs && got.ds == expected.ds && got.es == expected.es &&
                      got.ss == expected.ss && (got.flags & mask) == (expected.flags & mask);

            for (const auto& cell : test.at("final").at("ram")) {
                if (bus.memory[cell.at(0).get<std::uint32_t>() & 0xFFFFFU] !=
                    cell.at(1).get<std::uint8_t>()) {
                    ok = false;
                }
            }

            if (ok) {
                ++tally.passed;
            } else {
                ++tally.failed;
                if (failures.size() < 25U) {
                    failures.push_back(path.filename().string() + ": " +
                                       test.at("name").get<std::string>());
                }
            }
            bus.revert();
        }
        return tally;
    }

} // namespace

TEST_CASE("v30 passes the public per-instruction 8088/V20 conformance corpus",
          "[conformance][v30]") {
    const char* dir_env = std::getenv("MNEMOS_V30_TESTS_DIR");
    if (dir_env == nullptr || std::string{dir_env}.empty() ||
        !std::filesystem::is_directory(dir_env)) {
        SKIP("set MNEMOS_V30_TESTS_DIR to a directory of per-instruction 8088/V20 "
             "test JSON files (see THIRD-PARTY.md)");
    }
    const std::filesystem::path dir{dir_env};

    std::uint16_t global_mask = 0xFFFFU;
    if (const char* mask_env = std::getenv("MNEMOS_V30_FLAGS_MASK");
        mask_env != nullptr && *mask_env != '\0') {
        global_mask = static_cast<std::uint16_t>(std::strtoul(mask_env, nullptr, 16));
    }

    const auto metadata = load_metadata(dir);

    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        const auto name = entry.path().filename().string();
        if (name.ends_with(".json.gz") || (name.ends_with(".json") && name != "metadata.json")) {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());

    file_tally total;
    std::size_t files_skipped = 0;
    std::vector<std::string> failures;
    std::string per_file;

    for (const auto& path : files) {
        std::string stem = path.filename().string();
        stem = stem.substr(0, stem.find('.'));
        opcode_meta meta; // default: run with a full mask
        if (const auto it = metadata.find(stem); it != metadata.end()) {
            if (it->second.skip) {
                ++files_skipped;
                continue;
            }
            meta = it->second;
        }
        const file_tally tally = run_file(path, meta, global_mask, failures);
        total.passed += tally.passed;
        total.failed += tally.failed;
        total.skipped += tally.skipped;
        if (tally.failed > 0U) {
            per_file += " " + path.filename().string() + "=" + std::to_string(tally.failed);
        }
    }

    INFO("files=" << files.size() << " (skipped " << files_skipped << ")  passed=" << total.passed
                  << " failed=" << total.failed << " tests-skipped=" << total.skipped
                  << "\nper-file failures:" << per_file << "\nexamples:" << [&] {
                         std::string detail;
                         for (const std::string& failure : failures) {
                             detail += "\n  " + failure;
                         }
                         return detail;
                     }());
    CHECK(!files.empty());
    CHECK(total.failed == 0U);
}
