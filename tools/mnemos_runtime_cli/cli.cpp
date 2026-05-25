#include "cli.hpp"

#include "c64_system.hpp"
#include "manifest.hpp"
#include "save_state.hpp"
#include "scheduler.hpp"
#include "sha256.hpp"

#include <charconv>
#include <fstream>
#include <istream>
#include <iterator>
#include <optional>
#include <ostream>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
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

        // ----- input-log parsing -----

        using c64_key = manifests::c64::c64_input::key;

        // Map an input-log key token to a C64 key. Accepts every key-enum name plus
        // friendly aliases (the digits 0-9, "return", "shift", "stop", the arrows).
        std::optional<c64_key> key_from_name(std::string_view name) {
            static const std::unordered_map<std::string_view, c64_key> table = {
                {"a", c64_key::a},
                {"b", c64_key::b},
                {"c", c64_key::c},
                {"d", c64_key::d},
                {"e", c64_key::e},
                {"f", c64_key::f},
                {"g", c64_key::g},
                {"h", c64_key::h},
                {"i", c64_key::i},
                {"j", c64_key::j},
                {"k", c64_key::k},
                {"l", c64_key::l},
                {"m", c64_key::m},
                {"n", c64_key::n},
                {"o", c64_key::o},
                {"p", c64_key::p},
                {"q", c64_key::q},
                {"r", c64_key::r},
                {"s", c64_key::s},
                {"t", c64_key::t},
                {"u", c64_key::u},
                {"v", c64_key::v},
                {"w", c64_key::w},
                {"x", c64_key::x},
                {"y", c64_key::y},
                {"z", c64_key::z},
                {"0", c64_key::k0},
                {"1", c64_key::k1},
                {"2", c64_key::k2},
                {"3", c64_key::k3},
                {"4", c64_key::k4},
                {"5", c64_key::k5},
                {"6", c64_key::k6},
                {"7", c64_key::k7},
                {"8", c64_key::k8},
                {"9", c64_key::k9},
                {"ret", c64_key::ret},
                {"return", c64_key::ret},
                {"enter", c64_key::ret},
                {"space", c64_key::space},
                {"ins_del", c64_key::ins_del},
                {"del", c64_key::ins_del},
                {"home", c64_key::home},
                {"crsr_lr", c64_key::crsr_lr},
                {"right", c64_key::crsr_lr},
                {"crsr_ud", c64_key::crsr_ud},
                {"down", c64_key::crsr_ud},
                {"f1", c64_key::f1},
                {"f3", c64_key::f3},
                {"f5", c64_key::f5},
                {"f7", c64_key::f7},
                {"lshift", c64_key::lshift},
                {"shift", c64_key::lshift},
                {"rshift", c64_key::rshift},
                {"ctrl", c64_key::ctrl},
                {"cbm", c64_key::cbm},
                {"run_stop", c64_key::run_stop},
                {"runstop", c64_key::run_stop},
                {"stop", c64_key::run_stop},
                {"plus", c64_key::plus},
                {"minus", c64_key::minus},
                {"period", c64_key::period},
                {"comma", c64_key::comma},
                {"colon", c64_key::colon},
                {"semicolon", c64_key::semicolon},
                {"at", c64_key::at},
                {"asterisk", c64_key::asterisk},
                {"slash", c64_key::slash},
                {"equals", c64_key::equals},
                {"pound", c64_key::pound},
                {"up_arrow", c64_key::up_arrow},
                {"uparrow", c64_key::up_arrow},
                {"left_arrow", c64_key::left_arrow},
                {"leftarrow", c64_key::left_arrow},
            };
            const auto it = table.find(name);
            return it == table.end() ? std::nullopt : std::optional<c64_key>(it->second);
        }

        // Parse a joystick direction list ("up,down,left,right,fire" in any order,
        // or "none"/"center"/"0") into an OR of the joy_* bit masks.
        std::optional<std::uint8_t> joystick_mask(std::string_view dirs) {
            using input = manifests::c64::c64_input;
            if (dirs == "none" || dirs == "center" || dirs == "0") {
                return std::uint8_t{0U};
            }
            std::uint8_t mask = 0U;
            std::size_t start = 0U;
            while (start <= dirs.size()) {
                const std::size_t comma = dirs.find(',', start);
                const std::string_view tok =
                    dirs.substr(start, comma == std::string_view::npos ? std::string_view::npos
                                                                       : comma - start);
                if (tok == "up") {
                    mask = static_cast<std::uint8_t>(mask | input::joy_up);
                } else if (tok == "down") {
                    mask = static_cast<std::uint8_t>(mask | input::joy_down);
                } else if (tok == "left") {
                    mask = static_cast<std::uint8_t>(mask | input::joy_left);
                } else if (tok == "right") {
                    mask = static_cast<std::uint8_t>(mask | input::joy_right);
                } else if (tok == "fire") {
                    mask = static_cast<std::uint8_t>(mask | input::joy_fire);
                } else {
                    return std::nullopt;
                }
                if (comma == std::string_view::npos) {
                    break;
                }
                start = comma + 1U;
            }
            return mask;
        }

        std::optional<int> parse_uint(const std::string& tok, int max) {
            int value = 0;
            const char* begin = tok.data();
            const char* end = begin + tok.size();
            const auto [ptr, ec] = std::from_chars(begin, end, value);
            if (ec != std::errc{} || ptr != end || value < 0 || value > max) {
                return std::nullopt;
            }
            return value;
        }

    } // namespace

    bool parse_input_log(std::istream& in, runtime::input_buffer& out, std::ostream& err) {
        std::string line;
        int lineno = 0;
        while (std::getline(in, line)) {
            ++lineno;
            if (const auto hash = line.find('#'); hash != std::string::npos) {
                line.erase(hash); // strip the trailing comment
            }
            std::istringstream ss(line);
            std::string frame_tok;
            if (!(ss >> frame_tok)) {
                continue; // blank / comment-only line
            }

            std::uint64_t frame = 0U;
            const char* fb = frame_tok.data();
            const char* fe = fb + frame_tok.size();
            if (const auto [ptr, ec] = std::from_chars(fb, fe, frame);
                ec != std::errc{} || ptr != fe) {
                err << "input log:" << lineno << ": invalid frame '" << frame_tok << "'\n";
                return false;
            }

            std::string cmd;
            if (!(ss >> cmd)) {
                err << "input log:" << lineno << ": missing command\n";
                return false;
            }

            if (cmd == "press" || cmd == "release") {
                std::string key_tok;
                if (!(ss >> key_tok)) {
                    err << "input log:" << lineno << ": " << cmd << " needs a key\n";
                    return false;
                }
                const auto key = key_from_name(key_tok);
                if (!key) {
                    err << "input log:" << lineno << ": unknown key '" << key_tok << "'\n";
                    return false;
                }
                out.post({.frame = frame,
                          .device = input_device::keyboard,
                          .code = static_cast<std::uint8_t>(*key),
                          .pressed = cmd == "press"});
            } else if (cmd == "releaseall") {
                out.post({.frame = frame,
                          .device = input_device::release_all,
                          .code = 0U,
                          .pressed = false});
            } else if (cmd == "joy1" || cmd == "joy2") {
                std::string dirs;
                if (!(ss >> dirs)) {
                    err << "input log:" << lineno << ": " << cmd
                        << " needs directions (or 'none')\n";
                    return false;
                }
                const auto mask = joystick_mask(dirs);
                if (!mask) {
                    err << "input log:" << lineno << ": invalid joystick directions '" << dirs
                        << "'\n";
                    return false;
                }
                out.post(
                    {.frame = frame,
                     .device = cmd == "joy1" ? input_device::joystick1 : input_device::joystick2,
                     .code = *mask,
                     .pressed = false});
            } else if (cmd == "paddle1" || cmd == "paddle2") {
                std::string xs;
                std::string ys;
                if (!(ss >> xs) || !(ss >> ys)) {
                    err << "input log:" << lineno << ": " << cmd << " needs X and Y (0-255)\n";
                    return false;
                }
                const auto x = parse_uint(xs, 255);
                const auto y = parse_uint(ys, 255);
                if (!x || !y) {
                    err << "input log:" << lineno << ": " << cmd << " X/Y must be 0-255\n";
                    return false;
                }
                const bool port1 = cmd == "paddle1";
                out.post({.frame = frame,
                          .device = port1 ? input_device::paddle1_x : input_device::paddle2_x,
                          .code = static_cast<std::uint8_t>(*x),
                          .pressed = false});
                out.post({.frame = frame,
                          .device = port1 ? input_device::paddle1_y : input_device::paddle2_y,
                          .code = static_cast<std::uint8_t>(*y),
                          .pressed = false});
            } else {
                err << "input log:" << lineno << ": unknown command '" << cmd << "'\n";
                return false;
            }
        }
        return true;
    }

    void print_usage(std::ostream& out) {
        out << "mnemos_runtime_cli - headless deterministic runner\n\n"
               "usage: mnemos_runtime_cli --manifest <file> [options]\n\n"
               "  --manifest <file>   system manifest (TOML) to boot (required)\n"
               "  --rom-dir <dir>     directory holding the manifest's ROM files\n"
               "  --disk <file>       .d64 disk image to mount on drive 8\n"
               "  --drive-rom <file>  16K 1541 DOS ROM -> use the cycle-accurate drive 8\n"
               "  --cart <file>       .crt cartridge image to insert\n"
               "  --tape <file>       .tap datasette image (PLAY auto-pressed)\n"
               "  --frames <n>        number of frames to run (default 1)\n"
               "  --sid <6581|8580>   select the SID revision (default 6581)\n"
               "  --dual-sid          add a second SID at $D420 (stereo)\n"
               "  --reu <128|256|512> add a RAM Expansion Unit at $DF00 (KiB)\n"
               "  --modem             attach an RS-232 userport modem (loopback)\n"
               "  --dial <host[:port]> attach the modem and dial host over TCP\n"
               "  --dump-hash         print the SHA-256 of the final framebuffer\n"
               "  --save <file>       write a save state after the run\n"
               "  --load <file>       load a save state before the run\n"
               "  --input-log <file>  replay a frame-tagged input script (keys/joystick)\n"
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
            } else if (arg == "--disk") {
                if (!take_value(argc, argv, i, arg, value, err)) {
                    return false;
                }
                out.disk = value;
            } else if (arg == "--drive-rom") {
                if (!take_value(argc, argv, i, arg, value, err)) {
                    return false;
                }
                out.drive_rom = value;
            } else if (arg == "--cart") {
                if (!take_value(argc, argv, i, arg, value, err)) {
                    return false;
                }
                out.cart = value;
            } else if (arg == "--tape") {
                if (!take_value(argc, argv, i, arg, value, err)) {
                    return false;
                }
                out.tape = value;
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
            } else if (arg == "--reu") {
                if (!take_value(argc, argv, i, arg, value, err)) {
                    return false;
                }
                if (value == "128") {
                    out.reu_kib = 128U;
                } else if (value == "256") {
                    out.reu_kib = 256U;
                } else if (value == "512") {
                    out.reu_kib = 512U;
                } else {
                    err << "error: --reu expects 128, 256 or 512, got '" << value << "'\n";
                    return false;
                }
            } else if (arg == "--modem") {
                out.modem = true;
            } else if (arg == "--dial") {
                if (!take_value(argc, argv, i, arg, value, err)) {
                    return false;
                }
                out.dial = value;
                out.modem = true;
            } else if (arg == "--dump-hash") {
                out.dump_hash = true;
            } else if (arg == "--dual-sid") {
                out.dual_sid = true;
            } else if (arg == "--sid") {
                if (!take_value(argc, argv, i, arg, value, err)) {
                    return false;
                }
                if (value == "8580") {
                    out.sid_8580 = true;
                } else if (value == "6581") {
                    out.sid_8580 = false;
                } else {
                    err << "error: --sid expects 6581 or 8580, got '" << value << "'\n";
                    return false;
                }
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

        // Parse the input script up front so a malformed log fails before booting.
        runtime::input_buffer replay;
        if (!options.input_log.empty()) {
            std::ifstream in(options.input_log);
            if (!in) {
                err << "error: cannot read input log " << options.input_log.string() << "\n";
                return 7;
            }
            if (!parse_input_log(in, replay, err)) {
                return 7;
            }
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

        // Region comes from the manifest (the NTSC manifest's id); SID revision and
        // stereo are CLI selections.
        manifests::c64::c64_config cfg;
        if (m.id.find("ntsc") != std::string::npos) {
            cfg.video_region = manifests::c64::c64_config::region::ntsc;
        }
        if (options.sid_8580) {
            cfg.sid_variant = chips::audio::sid_6581::variant::mos_8580;
        }
        cfg.dual_sid = options.dual_sid;
        if (options.reu_kib != 0U) {
            cfg.reu = true;
            cfg.reu_model = options.reu_kib == 128U   ? chips::peripheral::reu::model::ram_128k
                            : options.reu_kib == 256U ? chips::peripheral::reu::model::ram_256k
                                                      : chips::peripheral::reu::model::ram_512k;
        }
        cfg.modem = options.modem;

        auto sys = manifests::c64::assemble_c64(std::move(basic), std::move(kernal),
                                                std::move(chargen), cfg);

        // Insert the cartridge before reset so an ultimax/16K cart's vectors apply.
        if (!options.cart.empty()) {
            const auto crt = read_file(options.cart);
            if (!crt) {
                err << "error: cannot read cartridge " << options.cart.string() << "\n";
                return 6;
            }
            if (!sys->cart.load_crt(*crt)) {
                err << "error: " << options.cart.string() << " is not a valid .crt\n";
                return 6;
            }
            out << "inserted cartridge " << options.cart.string() << "\n";
        }

        sys->cpu.reset(chips::reset_kind::power_on);
        sys->cia1.reset(chips::reset_kind::power_on);
        sys->cia2.reset(chips::reset_kind::power_on);
        sys->sid.reset(chips::reset_kind::power_on);
        sys->sid2.reset(chips::reset_kind::power_on);
        sys->vic.reset(chips::reset_kind::power_on);
        sys->drive8.reset(chips::reset_kind::power_on);
        sys->tape.reset(chips::reset_kind::power_on);

        if (!options.tape.empty()) {
            const auto image = read_file(options.tape);
            if (!image) {
                err << "error: cannot read tape image " << options.tape.string() << "\n";
                return 6;
            }
            if (!sys->tape.load_tap(*image)) {
                err << "error: " << options.tape.string() << " is not a valid .tap\n";
                return 6;
            }
            sys->tape.set_play(true); // auto-press PLAY
            out << "loaded tape " << options.tape.string() << " (PLAY pressed)\n";
        }

        // Select drive 8: the cycle-accurate full drive when a DOS ROM is supplied,
        // otherwise the protocol-level synthetic drive. Only the chosen one is ticked.
        const bool use_full_drive = !options.drive_rom.empty();
        chips::i_chip* drive = &sys->drive8;
        if (use_full_drive) {
            const auto rom = read_file(options.drive_rom);
            if (!rom) {
                err << "error: cannot read drive ROM " << options.drive_rom.string() << "\n";
                return 6;
            }
            if (!sys->drive8_full.load_rom(*rom)) {
                err << "error: " << options.drive_rom.string() << " is not a 16 KiB 1541 DOS ROM\n";
                return 6;
            }
            sys->drive8_full.set_clock_ratio(1'000'000U, 985'248U);
            sys->drive8_full.reset(chips::reset_kind::power_on);
            drive = &sys->drive8_full;
            out << "using the cycle-accurate drive 8 (" << options.drive_rom.string() << ")\n";
        }

        if (!options.disk.empty()) {
            const auto image = read_file(options.disk);
            if (!image) {
                err << "error: cannot read disk image " << options.disk.string() << "\n";
                return 6;
            }
            const bool mounted =
                use_full_drive ? sys->drive8_full.mount(*image) : sys->drive8.mount(*image);
            if (!mounted) {
                err << "error: " << options.disk.string()
                    << " is not a valid .d64 (expected 174848 or 196608 bytes)\n";
                return 6;
            }
            out << "mounted " << options.disk.string() << " on drive 8\n";
        }

        // The VIC drives frame boundaries; list it before the CPU so the CPU reads
        // the freshly advanced beam. All C64 chips run at phi2 (divider 1).
        std::vector<runtime::scheduled_chip> chips = {
            {&sys->vic, 1U}, {&sys->cpu, 1U}, {&sys->cia1, 1U}, {&sys->cia2, 1U},
            {&sys->sid, 1U}, {drive, 1U},     {&sys->tape, 1U}};
        if (cfg.dual_sid) {
            chips.push_back({&sys->sid2, 1U});
        }
        // RS-232 userport modem: --dial uses a live TCP backend, otherwise a
        // loopback echoes whatever the C64 sends. The UART samples/shifts the
        // serial lines each cycle; the modem advances its guard timer + pumps the
        // link. The transport must outlive the scheduler run.
        chips::peripheral::loopback_transport modem_loop;
        chips::peripheral::tcp_transport modem_tcp;
        if (cfg.modem) {
            if (!options.dial.empty()) {
                sys->modem_unit.set_transport(&modem_tcp);
            } else {
                sys->modem_unit.set_transport(&modem_loop);
            }
            chips.push_back({&sys->rs232_unit, 1U});
            chips.push_back({&sys->modem_unit, 1U});
        }
        runtime::scheduler sched(std::move(chips), &sys->vic);

        // Auto-issue the dial as if the C64 typed it (the headless runner has no
        // KERNAL terminal). The modem connects via the TCP transport; peer bytes
        // surface to the C64 over the next frames.
        if (cfg.modem && !options.dial.empty()) {
            const std::string at = "ATDT " + options.dial + "\r";
            for (const char c : at) {
                sys->modem_unit.dte_write(static_cast<std::uint8_t>(c));
            }
            out << "dialing " << options.dial << " ...\n";
        }

        // A save-state view over the assembled machine (chunk ids match the manifest).
        const auto build_target = [&](std::uint64_t master_cycle) {
            runtime::save_target t;
            t.manifest_id = m.id;
            t.manifest_rev = m.revision;
            t.master_cycle = master_cycle;
            t.chips = {{"cpu", &sys->cpu},   {"video", &sys->vic}, {"audio", &sys->sid},
                       {"cia1", &sys->cia1}, {"cia2", &sys->cia2}, {"pla", &sys->pla},
                       {"cart", &sys->cart}, {"tape", &sys->tape}, {"drive8", drive}};
            if (cfg.dual_sid) {
                t.chips.push_back({"audio2", &sys->sid2});
            }
            if (cfg.reu) {
                t.chips.push_back({"reu", &sys->reu_unit});
            }
            if (cfg.modem) {
                t.chips.push_back({"rs232", &sys->rs232_unit});
                t.chips.push_back({"modem", &sys->modem_unit});
            }
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

        if (replay.empty()) {
            sched.run_frames(options.frames);
        } else {
            // Apply each frame's input transitions to the C64 keyboard/joystick/
            // paddle state, then advance exactly that frame. Frame N in the log is
            // sampled at the start of the N-th emulated frame of this run.
            using key = manifests::c64::c64_input::key;
            for (std::uint64_t f = 0; f < options.frames; ++f) {
                for (const runtime::input_event& ev : replay.events_for_frame(f)) {
                    switch (ev.device) {
                    case input_device::keyboard:
                        sys->input.set_key(static_cast<key>(ev.code), ev.pressed);
                        break;
                    case input_device::joystick1:
                        sys->input.set_joystick(1U, ev.code);
                        break;
                    case input_device::joystick2:
                        sys->input.set_joystick(2U, ev.code);
                        break;
                    case input_device::paddle1_x:
                        sys->input.set_paddle(1U, ev.code, sys->input.paddle_y(1U));
                        break;
                    case input_device::paddle1_y:
                        sys->input.set_paddle(1U, sys->input.paddle_x(1U), ev.code);
                        break;
                    case input_device::paddle2_x:
                        sys->input.set_paddle(2U, ev.code, sys->input.paddle_y(2U));
                        break;
                    case input_device::paddle2_y:
                        sys->input.set_paddle(2U, sys->input.paddle_x(2U), ev.code);
                        break;
                    case input_device::release_all:
                        sys->input.release_all_keys();
                        break;
                    default:
                        break;
                    }
                }
                sched.run_frame();
            }
        }

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
