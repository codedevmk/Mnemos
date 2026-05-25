#include <mnemos/tools/cli.hpp>

#include <mnemos/foundation/sha256.hpp>
#include <mnemos/manifests/c64/c64_system.hpp>
#include <mnemos/manifests/manifest.hpp>
#include <mnemos/runtime/save_state.hpp>
#include <mnemos/runtime/scheduler.hpp>

#include <charconv>
#include <fstream>
#include <iterator>
#include <optional>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace mnemos::tools {
    namespace {

        // Read a whole file as bytes. nullopt if it cannot be opened.
        std::optional<std::vector<std::uint8_t>> read_file(const std::filesystem::path& path) {
            std::ifstream in(path, std::ios::binary);
            if (!in) {
                return std::nullopt;
            }
            return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(in)),
                                             std::istreambuf_iterator<char>());
        }

        bool write_file(const std::filesystem::path& path, std::span<const std::uint8_t> bytes) {
            std::ofstream out(path, std::ios::binary);
            if (!out) {
                return false;
            }
            out.write(reinterpret_cast<const char*>(bytes.data()),
                      static_cast<std::streamsize>(bytes.size()));
            return static_cast<bool>(out);
        }

        // An all-zero sha256 is the "unverified placeholder" sentinel (see ROMS.md).
        bool is_placeholder_hash(std::string_view hex) noexcept {
            return hex.size() == 64U && hex.find_first_not_of('0') == std::string_view::npos;
        }

        bool take_value(int argc, const char* const* argv, int& i, std::string_view flag,
                        std::string& value, std::ostream& err) {
            if (i + 1 >= argc) {
                err << "error: " << flag << " requires a value\n";
                return false;
            }
            value = argv[++i];
            return true;
        }

    } // namespace

    void print_usage(std::ostream& out) {
        out << "mnemos_runtime_cli - headless deterministic runner\n\n"
               "usage: mnemos_runtime_cli --manifest <file> [options]\n\n"
               "  --manifest <file>   system manifest (TOML) to boot (required)\n"
               "  --rom-dir <dir>     directory holding the manifest's ROM files\n"
               "  --frames <n>        number of frames to run (default 1)\n"
               "  --dump-hash         print the SHA-256 of the final framebuffer\n"
               "  --save <file>       write a save state after the run\n"
               "  --load <file>       load a save state before the run\n"
               "  --input-log <file>  (deferred) replay a frame-tagged input log\n"
               "  -h, --help          show this help\n";
    }

    bool parse_args(int argc, const char* const* argv, cli_options& out, std::ostream& err) {
        for (int i = 1; i < argc; ++i) {
            const std::string_view arg = argv[i];
            std::string value;
            if (arg == "-h" || arg == "--help") {
                out.help = true;
            } else if (arg == "--manifest") {
                if (!take_value(argc, argv, i, arg, value, err)) {
                    return false;
                }
                out.manifest = value;
            } else if (arg == "--rom-dir") {
                if (!take_value(argc, argv, i, arg, value, err)) {
                    return false;
                }
                out.rom_dir = value;
            } else if (arg == "--frames") {
                if (!take_value(argc, argv, i, arg, value, err)) {
                    return false;
                }
                std::uint64_t frames = 0U;
                const char* begin = value.data();
                const char* end = begin + value.size();
                const auto [ptr, ec] = std::from_chars(begin, end, frames);
                if (ec != std::errc{} || ptr != end) {
                    err << "error: --frames expects a non-negative integer, got '" << value
                        << "'\n";
                    return false;
                }
                out.frames = frames;
            } else if (arg == "--dump-hash") {
                out.dump_hash = true;
            } else if (arg == "--save") {
                if (!take_value(argc, argv, i, arg, value, err)) {
                    return false;
                }
                out.save = value;
            } else if (arg == "--load") {
                if (!take_value(argc, argv, i, arg, value, err)) {
                    return false;
                }
                out.load = value;
            } else if (arg == "--input-log") {
                if (!take_value(argc, argv, i, arg, value, err)) {
                    return false;
                }
                out.input_log = value;
            } else {
                err << "error: unknown argument '" << arg << "'\n";
                return false;
            }
        }
        return true;
    }

    std::string hash_framebuffer(const chips::frame_buffer_view& fb) {
        const std::size_t count = static_cast<std::size_t>(fb.width) * fb.height;
        std::vector<std::uint8_t> bytes;
        bytes.reserve(count * 4U);
        for (std::size_t i = 0; i < count; ++i) {
            const std::uint32_t p = fb.pixels != nullptr ? fb.pixels[i] : 0U;
            bytes.push_back(static_cast<std::uint8_t>((p >> 16U) & 0xFFU)); // R
            bytes.push_back(static_cast<std::uint8_t>((p >> 8U) & 0xFFU));  // G
            bytes.push_back(static_cast<std::uint8_t>(p & 0xFFU));          // B
            bytes.push_back(0xFFU);                                         // A (opaque)
        }
        return foundation::sha256(bytes).hex();
    }

    int run(const cli_options& options, std::ostream& out, std::ostream& err) {
        if (options.help) {
            print_usage(out);
            return 0;
        }
        if (options.manifest.empty()) {
            err << "error: --manifest is required\n";
            return 2;
        }
        if (!options.input_log.empty()) {
            err << "error: --input-log is not implemented yet\n";
            return 7;
        }

        const auto loaded = manifests::load_manifest_file(options.manifest);
        if (!loaded.ok()) {
            for (const auto& d : loaded.errors) {
                err << d.source << ":" << d.line << ":" << d.column << ": " << d.message << "\n";
            }
            return 2;
        }
        const manifests::manifest& m = *loaded.value;
        if (m.family != "commodore") {
            err << "error: unsupported system family '" << m.family
                << "' (only the Commodore 64 is wired up)\n";
            return 3;
        }

        std::vector<std::uint8_t> basic;
        std::vector<std::uint8_t> kernal;
        std::vector<std::uint8_t> chargen;
        for (const auto& bus : m.buses) {
            for (const auto& region : bus.regions) {
                if (region.backing != manifests::region_backing::rom || !region.file) {
                    continue;
                }
                const std::filesystem::path file(*region.file);
                const std::filesystem::path path = options.rom_dir / file.filename();
                auto bytes = read_file(path);
                if (!bytes) {
                    err << "error: cannot read ROM '" << region.name << "' from " << path.string()
                        << "\n";
                    return 4;
                }
                if (region.sha256 && !is_placeholder_hash(*region.sha256)) {
                    const std::string actual = foundation::sha256(*bytes).hex();
                    if (actual != *region.sha256) {
                        err << "error: ROM '" << region.name << "' sha256 mismatch\n  expected "
                            << *region.sha256 << "\n  actual   " << actual << "\n";
                        return 5;
                    }
                } else {
                    err << "warning: ROM '" << region.name
                        << "' is unverified (placeholder sha256; see ROMS.md)\n";
                }

                if (region.name.find("basic") != std::string::npos) {
                    basic = std::move(*bytes);
                } else if (region.name.find("kernal") != std::string::npos) {
                    kernal = std::move(*bytes);
                } else if (region.name.find("char") != std::string::npos) {
                    chargen = std::move(*bytes);
                }
            }
        }
        if (basic.empty() || kernal.empty() || chargen.empty()) {
            err << "error: manifest is missing one of the basic/kernal/char ROM regions\n";
            return 4;
        }

        auto sys =
            manifests::c64::assemble_c64(std::move(basic), std::move(kernal), std::move(chargen));
        sys->cpu.reset(chips::reset_kind::power_on);
        sys->cia1.reset(chips::reset_kind::power_on);
        sys->cia2.reset(chips::reset_kind::power_on);
        sys->sid.reset(chips::reset_kind::power_on);
        sys->vic.reset(chips::reset_kind::power_on);

        // The VIC drives frame boundaries; list it before the CPU so the CPU reads
        // the freshly advanced beam. All C64 chips run at phi2 (divider 1).
        std::vector<runtime::scheduled_chip> chips = {
            {&sys->vic, 1U}, {&sys->cpu, 1U}, {&sys->cia1, 1U}, {&sys->cia2, 1U}, {&sys->sid, 1U}};
        runtime::scheduler sched(std::move(chips), &sys->vic);

        // A save-state view over the assembled machine (chunk ids match the manifest).
        const auto build_target = [&](std::uint64_t master_cycle) {
            runtime::save_target t;
            t.manifest_id = m.id;
            t.manifest_rev = m.revision;
            t.master_cycle = master_cycle;
            t.chips = {{"cpu", &sys->cpu},   {"video", &sys->vic}, {"audio", &sys->sid},
                       {"cia1", &sys->cia1}, {"cia2", &sys->cia2}, {"pla", &sys->pla}};
            t.memory = {{"ram", std::span<std::uint8_t>(sys->ram)},
                        {"color_ram", std::span<std::uint8_t>(sys->color_ram)}};
            return t;
        };

        if (!options.load.empty()) {
            const auto bytes = read_file(options.load);
            if (!bytes) {
                err << "error: cannot read save state " << options.load.string() << "\n";
                return 8;
            }
            const runtime::load_result lr = runtime::read_save_state(*bytes, build_target(0U));
            if (!lr.ok()) {
                err << "error: failed to load save state (status " << static_cast<int>(lr.status)
                    << ")\n";
                return 8;
            }
            out << "loaded save state at master cycle " << lr.master_cycle << "\n";
        }

        sched.run_frames(options.frames);

        if (!options.save.empty()) {
            const std::vector<std::uint8_t> bytes =
                runtime::write_save_state(build_target(sched.master_cycle()));
            if (!write_file(options.save, bytes)) {
                err << "error: cannot write save state " << options.save.string() << "\n";
                return 8;
            }
            out << "wrote save state (" << bytes.size() << " bytes) to " << options.save.string()
                << "\n";
        }

        if (options.dump_hash) {
            out << "frame " << sched.frame_index() << " "
                << hash_framebuffer(sys->vic.framebuffer()) << "\n";
        } else {
            out << "ran " << options.frames << " frame(s); frame_index=" << sched.frame_index()
                << "\n";
        }
        return 0;
    }

    int main_cli(int argc, const char* const* argv, std::ostream& out, std::ostream& err) {
        cli_options options;
        if (!parse_args(argc, argv, options, err)) {
            print_usage(err);
            return 1;
        }
        return run(options, out, err);
    }

} // namespace mnemos::tools
