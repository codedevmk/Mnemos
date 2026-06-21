#include "capcom_cps2_adapter.hpp"

#include "adapter_registry.hpp"
#include "cps2_crypto.hpp"
#include "file.hpp"
#include "input_pack.hpp"
#include "rom_set.hpp"
#include "rom_set_toml.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace mnemos::apps::player::adapters::capcom_cps2 {

    namespace {

        using mnemos::manifests::common::rom_set_image;
        namespace cps2 = mnemos::manifests::capcom_cps2;

        struct loaded_set final {
            rom_set_image image;
            frontend_sdk::display_orientation orientation{
                frontend_sdk::display_orientation::horizontal};
        };

        [[nodiscard]] frontend_sdk::display_orientation
        to_display_orientation(mnemos::manifests::common::screen_orientation orientation) noexcept {
            return orientation == mnemos::manifests::common::screen_orientation::vertical
                       ? frontend_sdk::display_orientation::vertical
                       : frontend_sdk::display_orientation::horizontal;
        }

        // Resolve a clone set's parent zip beside the clone on disk and compose a
        // fallback provider (clone first, then parent). Identical in shape to the
        // CPS1 adapter's helper.
        [[nodiscard]] mnemos::manifests::common::rom_file_provider
        with_parent_fallback(const mnemos::manifests::common::rom_file_provider& clone,
                             const std::string& parent, const std::string& rom_path) {
            if (rom_path.empty()) {
                std::fprintf(stderr,
                             "[capcom_cps2] set declares parent '%s' but no path is known to "
                             "locate it; shared ROMs will be missing\n",
                             parent.c_str());
                return clone;
            }
            if (parent.find('/') != std::string::npos || parent.find('\\') != std::string::npos ||
                parent.find("..") != std::string::npos) {
                std::fprintf(stderr,
                             "[capcom_cps2] refusing to resolve parent '%s': not a plain set id\n",
                             parent.c_str());
                return clone;
            }
            const auto slash = rom_path.find_last_of("/\\");
            const std::string dir =
                slash == std::string::npos ? std::string{} : rom_path.substr(0, slash + 1);
            const std::string parent_path = dir + parent + ".zip";
            bool unreadable_zip = false;
            auto parent_provider = mnemos::manifests::common::make_zip_rom_provider_from_path(
                parent_path, &unreadable_zip);
            if (!parent_provider.has_value()) {
                std::fprintf(stderr, "[capcom_cps2] parent set %s: %s\n",
                             unreadable_zip ? "is not a readable zip" : "not found",
                             parent_path.c_str());
                return clone;
            }
            return mnemos::manifests::common::make_fallback_rom_provider(
                clone, std::move(*parent_provider));
        }

        // A 20-byte board key validates against the encrypted program when the
        // decrypted reset vector is sane (even SSP/PC, PC inside the program). This
        // is how a region/revision variant is picked when several keys share the
        // set's name prefix (e.g. 1944.key vs 1944u.key).
        [[nodiscard]] bool key_decrypts_program(std::span<const std::uint8_t> key_bytes,
                                                const std::vector<std::uint8_t>& program) {
            if (key_bytes.size() != cps2::crypto_key_size) {
                return false;
            }
            std::array<std::uint8_t, cps2::crypto_key_size> raw{};
            std::copy(key_bytes.begin(), key_bytes.end(), raw.begin());
            cps2::cps2_crypto_key key{};
            if (!cps2::decode_key(raw, key)) {
                return false;
            }
            std::vector<std::uint8_t> opcode(program.size(), 0U);
            if (!cps2::decrypt_opcodes(program, opcode, key) || opcode.size() < 8U) {
                return false;
            }
            const auto be32 = [&opcode](std::size_t o) -> std::uint32_t {
                return (static_cast<std::uint32_t>(opcode[o]) << 24U) |
                       (static_cast<std::uint32_t>(opcode[o + 1U]) << 16U) |
                       (static_cast<std::uint32_t>(opcode[o + 2U]) << 8U) | opcode[o + 3U];
            };
            const std::uint32_t reset_ssp = be32(0U);
            const std::uint32_t reset_pc = be32(4U);
            return (reset_ssp & 1U) == 0U && (reset_pc & 1U) == 0U && reset_pc < opcode.size();
        }

        // CPS2 boards are encrypted; the board key is a 20-byte external asset. If
        // the declaration did not place a "key" region, scan `<dir>/keys` and the
        // zip's own dir for `.key` files whose name shares the set's prefix, and
        // adopt the first that decrypts the program to a sane reset vector. (The
        // zip name does not reliably encode the region, so the right variant is
        // chosen by validation, mirroring the reference loader.)
        void resolve_key_region(rom_set_image& image, const std::string& set_name,
                                const std::string& rom_path) {
            if (const auto* k = image.region("key");
                k != nullptr && k->size() == cps2::crypto_key_size) {
                return; // already supplied by the declaration
            }
            const auto* program = image.region("maincpu");
            if (program == nullptr || program->empty() || (program->size() & 1U) != 0U) {
                return;
            }
            if (set_name.empty() || rom_path.empty() || set_name.find('/') != std::string::npos ||
                set_name.find('\\') != std::string::npos ||
                set_name.find("..") != std::string::npos) {
                return;
            }

            namespace fs = std::filesystem;
            std::error_code ec;
            const fs::path dir = fs::path(rom_path).parent_path();
            for (const fs::path& key_dir : {dir / "keys", dir}) {
                if (!fs::is_directory(key_dir, ec)) {
                    continue;
                }
                std::vector<fs::path> candidates;
                for (const auto& entry : fs::directory_iterator(key_dir, ec)) {
                    if (!entry.is_regular_file(ec) || entry.path().extension() != ".key") {
                        continue;
                    }
                    const std::string stem = entry.path().stem().string();
                    if (stem.rfind(set_name, 0U) == 0U || set_name.rfind(stem, 0U) == 0U) {
                        candidates.push_back(entry.path());
                    }
                }
                // Prefer an exact name match, then the shorter (base) names.
                std::sort(candidates.begin(), candidates.end(),
                          [&set_name](const fs::path& a, const fs::path& b) {
                              const bool ea = a.stem().string() == set_name;
                              const bool eb = b.stem().string() == set_name;
                              if (ea != eb) {
                                  return ea;
                              }
                              return a.stem().string().size() < b.stem().string().size();
                          });
                for (const auto& candidate : candidates) {
                    auto bytes = mnemos::io::read_file(candidate.string());
                    if (bytes && bytes->size() == cps2::crypto_key_size &&
                        key_decrypts_program(*bytes, *program)) {
                        std::fprintf(stderr, "[capcom_cps2] board key: %s\n",
                                     candidate.string().c_str());
                        image.regions["key"] = std::move(*bytes);
                        return;
                    }
                }
            }
            std::fprintf(stderr,
                         "[capcom_cps2] no valid board key for '%s' beside %s -- the board stays "
                         "a non-executable blocker\n",
                         set_name.c_str(), rom_path.c_str());
        }

        // Set loader. A .zip carrying a "game.toml" (schema mnemos-romset/1) loads
        // declaratively; a clone names a `parent` whose zip supplies the shared
        // dumps. Without a manifest the development format applies (region-named
        // <region>.bin entries). A bare binary is the encrypted 68000 program.
        [[nodiscard]] loaded_set load_set(std::vector<std::uint8_t> rom,
                                          const std::string& rom_path) {
            loaded_set result;
            const bool is_zip = rom.size() >= 4U && rom[0] == 'P' && rom[1] == 'K';
            if (!is_zip) {
                result.image.regions.emplace("maincpu", std::move(rom));
                return result;
            }
            auto provider = mnemos::manifests::common::make_zip_rom_provider(std::move(rom));
            if (!provider.has_value()) {
                return result;
            }
            if (auto manifest_bytes = (*provider)("game.toml")) {
                const std::string text(manifest_bytes->begin(), manifest_bytes->end());
                const auto parsed =
                    mnemos::manifests::common::parse_rom_set_decl(text, "game.toml");
                if (!parsed.ok()) {
                    for (const auto& error : parsed.errors) {
                        std::fprintf(stderr, "[capcom_cps2] %s:%u:%u: %s\n", error.source.c_str(),
                                     error.line, error.column, error.message.c_str());
                    }
                    return result; // declared but invalid: boot an empty board
                }
                if (parsed.value->board != "capcom_cps2") {
                    std::fprintf(stderr,
                                 "[capcom_cps2] game.toml declares board '%s', expected "
                                 "'capcom_cps2'\n",
                                 parsed.value->board.c_str());
                    return result;
                }
                result.orientation = to_display_orientation(parsed.value->orientation);
                const auto effective =
                    parsed.value->parent.has_value()
                        ? with_parent_fallback(*provider, *parsed.value->parent, rom_path)
                        : *provider;
                result.image = mnemos::manifests::common::load_rom_set(*parsed.value, effective);
                for (const auto& issue : result.image.issues) {
                    std::fprintf(stderr, "[capcom_cps2] %s: %s\n", issue.file.c_str(),
                                 issue.message.c_str());
                }
                resolve_key_region(result.image, parsed.value->name, rom_path);
                return result;
            }
            for (const char* region : {"maincpu", "gfx", "audiocpu", "qsound", "key"}) {
                if (auto bytes = (*provider)(std::string{region} + ".bin")) {
                    result.image.regions.emplace(region, std::move(*bytes));
                }
            }
            return result;
        }

        [[nodiscard]] std::unique_ptr<cps2::cps2_system> assemble_from(rom_set_image image) {
            return std::make_unique<cps2::cps2_system>(std::move(image), cps2::cps2_board_params{});
        }

    } // namespace

    capcom_cps2_adapter::capcom_cps2_adapter(std::vector<std::uint8_t> rom,
                                             std::string display_name,
                                             frontend_sdk::scheduler_factory* /*scheduler_factory*/,
                                             std::optional<std::uint16_t> /*dip_override*/,
                                             std::string rom_path)
    // The CPS2 board integrates the 68000 + the QSound Z80/DSP + the beam in
    // its own run_frame(), so there is no per-chip master-clock schedule to
    // build; the scheduler_factory override does not apply.
    {
        loaded_set set = load_set(std::move(rom), rom_path);
        orientation_ = set.orientation;
        sys_ = assemble_from(std::move(set.image));
        chip_view_ = {&sys_->video(), &sys_->cpu(), &sys_->sound_cpu(), &sys_->qsound_dsp()};
        publish_memory_views();
        spec_ = {{"System", "Arcade"},
                 {"Board", "Capcom CPS2"},
                 {"Game", display_name.empty() ? std::string{"unknown"} : std::move(display_name)}};
    }

    void capcom_cps2_adapter::publish_memory_views() {
        auto publish = [this](std::size_t index, std::string_view name,
                              std::span<const std::uint8_t> bytes) {
            memory_view_storage_[index] =
                std::make_unique<instrumentation::span_memory_view>(name, bytes);
            system_mem_view_[index] = memory_view_storage_[index].get();
        };

        publish(0U, "main_work_ram", sys_->main_work_ram());
        publish(1U, "video_ram", sys_->video_ram());
        publish(2U, "object_ram", sys_->object_ram());
        publish(3U, "extra_ram", sys_->extra_ram());
        publish(4U, "control_registers", sys_->control_registers());
        publish(5U, "extra_control", sys_->extra_control());
        publish(6U, "cps_registers", sys_->cps_registers());
        publish(7U, "qsound_shared_ram", sys_->qsound_shared_ram());
        publish(8U, "z80_ram", sys_->z80_ram());
        publish(9U, "qsound_work_ram", sys_->qsound_work_ram());
    }

    void capcom_cps2_adapter::step_one_frame() {
        // run_frame() integrates the 68000, the QSound subsystem, and the beam
        // (incl. the vblank IRQ tail) for exactly one frame.
        sys_->run_frame();
        ++frames_stepped_;
    }

    frontend_sdk::audio_chunk capcom_cps2_adapter::drain_audio() noexcept {
        // QSound emits a fixed ~24 kHz stereo stream independent of the CPU clock;
        // pace the drain off the frame clock (native rate / fps) and step the HLE
        // mixer once per output frame via generate().
        constexpr std::uint32_t rate = chips::audio::qsound::native_sample_rate;
        const std::uint64_t due = frames_stepped_ * rate / manifests::capcom_cps2::frame_rate_hz;
        const std::uint64_t pending = due - samples_drained_;
        samples_drained_ = due;
        if (pending == 0U) {
            return {};
        }
        audio_buf_.assign(static_cast<std::size_t>(pending) * 2U, 0);
        sys_->qsound_dsp().generate(audio_buf_);
        return {.samples = audio_buf_.data(),
                .frame_count = static_cast<std::uint32_t>(pending),
                .sample_rate = rate};
    }

    void capcom_cps2_adapter::apply_input(int port,
                                          const frontend_sdk::controller_state& state) noexcept {
        if (port < 0 || port > 1) {
            return; // two-player hardware
        }
        ports_[static_cast<std::size_t>(port)] = state;
        refresh_inputs();
    }

    void capcom_cps2_adapter::refresh_inputs() noexcept {
        // Player byte (active low): right/left/down/up in bits 0-3 (the standard
        // arcade nibble), buttons 1/2/3 in bits 4-6.
        const auto pack = [](const frontend_sdk::controller_state& c) -> std::uint8_t {
            return pack_active_low_pad(c, dpad_layout{},
                                       {{c.a, 0x10U}, {c.b, 0x20U}, {c.c, 0x40U}});
        };
        // Extra-button byte (active low): buttons 4/5/6 in bits 0-2, no directions.
        // CPS2 fighting cabinets wire these through the second player input word
        // rather than the joystick word above.
        const auto pack_extra = [](const frontend_sdk::controller_state& c) -> std::uint8_t {
            return pack_active_low_buttons({{c.x, 0x01U}, {c.y, 0x02U}, {c.z, 0x04U}});
        };
        // P2 high byte, P1 low byte.
        sys_->input0 = static_cast<std::uint16_t>(
            (static_cast<std::uint16_t>(pack(ports_[1])) << 8U) | pack(ports_[0]));
        sys_->input1 = static_cast<std::uint16_t>(
            (static_cast<std::uint16_t>(pack_extra(ports_[1])) << 8U) | pack_extra(ports_[0]));

        // System word (active low): the CPS-2 IN2 layout puts START1-4 in bits
        // 8-11 and COIN1-4 in bits 12-15 (bit 0 is the EEPROM data-out the board
        // overlays at read time). The pads' `select` is the coin slot.
        std::uint16_t system = 0xFFFFU;
        const auto clear = [&system](std::uint16_t bit) {
            system &= static_cast<std::uint16_t>(~bit);
        };
        if (ports_[0].start) {
            clear(0x0100U); // START1
        }
        if (ports_[1].start) {
            clear(0x0200U); // START2
        }
        if (ports_[0].select) {
            clear(0x1000U); // COIN1
        }
        if (ports_[1].select) {
            clear(0x2000U); // COIN2
        }
        sys_->input_sys = system;
    }

    void force_link() noexcept {}

    namespace {
        const auto register_capcom_cps2 = [] {
            mnemos::frontend_sdk::adapter_registry::instance().register_family(
                "cps2",
                [](mnemos::frontend_sdk::adapter_options opts)
                    -> std::unique_ptr<mnemos::frontend_sdk::player_system> {
                    return std::make_unique<capcom_cps2_adapter>(
                        std::move(opts.rom), std::move(opts.display_name),
                        opts.scheduler_factory_override, opts.dip_override,
                        std::move(opts.rom_path));
                });
            return 0;
        }();
    } // namespace

} // namespace mnemos::apps::player::adapters::capcom_cps2
