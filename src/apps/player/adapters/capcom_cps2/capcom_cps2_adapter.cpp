#include "capcom_cps2_adapter.hpp"

#include "adapter_registry.hpp"
#include "crc32.hpp"
#include "cps2_game_manifests.hpp"
#include "cps2_crypto.hpp"
#include "file.hpp"
#include "input_pack.hpp"
#include "introspection_adapters.hpp"
#include "rom_set.hpp"
#include "rom_set_toml.hpp"
#include "zip_archive.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace mnemos::apps::player::adapters::capcom_cps2 {

    namespace {

        using mnemos::manifests::common::rom_set_image;
        namespace cps2 = mnemos::manifests::capcom_cps2;

        constexpr std::uint32_t cps2_adapter_state_version = 5U;
        constexpr std::uint32_t cps2_audio_output_rate = 44'100U;
        constexpr std::uint64_t cps2_audio_slice_cycles =
            cps2::m68k_clock_hz / cps2_audio_output_rate;
        constexpr std::uint64_t cps2_qsound_rate_num = 60'000'000ULL / 2ULL;
        constexpr std::uint64_t cps2_qsound_rate_den =
            1'248ULL * static_cast<std::uint64_t>(cps2_audio_output_rate);

        struct loaded_set final {
            rom_set_image image;
            std::string set_name;
            frontend_sdk::display_orientation orientation{
                frontend_sdk::display_orientation::horizontal};
            std::uint8_t players{2U};
            cps2_input_profile input_profile{cps2_input_profile::six_button};
            cps2::cps2_analog_input_mode analog_input_mode{cps2::cps2_analog_input_mode::none};
            bool coin_lockout_active_high{};
        };

        struct key_file_candidate final {
            std::string label;
            std::string stem;
            std::vector<std::uint8_t> bytes;
        };

        struct resolved_rom_provider final {
            mnemos::manifests::common::rom_file_provider provider;
            std::vector<key_file_candidate> key_candidates;
        };

        [[nodiscard]] std::uint64_t audio_due_after_frames(std::uint64_t frames) noexcept {
            return frames * static_cast<std::uint64_t>(cps2_audio_output_rate) *
                   cps2::refresh_hz_den / cps2::refresh_hz_num;
        }

        [[nodiscard]] std::vector<key_file_candidate>
        collect_zip_key_candidates(std::span<const std::uint8_t> archive_bytes);

        class cps2_board_debug_chip final : public chips::iperipheral {
          public:
            explicit cps2_board_debug_chip(cps2::cps2_system& system) : system_(&system) {
                introspection_.with_registers([this] { return register_snapshot(); });
            }

            [[nodiscard]] chips::chip_metadata metadata() const noexcept override {
                return {.manufacturer = "Capcom",
                        .part_number = "CPS2_BUS",
                        .family = "CPS2 board diagnostics",
                        .klass = chips::chip_class::peripheral,
                        .revision = 1U};
            }

            void tick(std::uint64_t /*cycles*/) override {}
            void reset(chips::reset_kind /*kind*/) override {}
            void save_state(chips::state_writer& /*writer*/) const override {}
            void load_state(chips::state_reader& /*reader*/) override {}

            [[nodiscard]] instrumentation::ichip_introspection& introspection() noexcept override {
                return introspection_;
            }

          private:
            [[nodiscard]] std::span<const chips::register_descriptor>
            register_snapshot() noexcept {
                using fmt = chips::register_value_format;
                const cps2::cps2_qsound_bus_diagnostics& q =
                    system_->qsound_bus_diagnostics();
                std::size_t out = 0U;
                const auto add = [this, &out](const char* name, std::uint64_t value,
                                              std::uint8_t bits, fmt format) {
                    register_view_[out++] = {name, value, bits, format};
                };
                const auto control_word = [this](std::size_t offset) -> std::uint16_t {
                    const auto regs = system_->control_registers();
                    if (offset + 1U >= regs.size()) {
                        return 0U;
                    }
                    return static_cast<std::uint16_t>(
                        (static_cast<std::uint16_t>(regs[offset]) << 8U) | regs[offset + 1U]);
                };

                add("CPSA_OBJBASE", system_->cps_a_register(cps2::cps_a_obj_base), 16U,
                    fmt::unsigned_integer);
                add("CPSA_SCR1BASE", system_->cps_a_register(cps2::cps_a_scroll1_base), 16U,
                    fmt::unsigned_integer);
                add("CPSA_SCR2BASE", system_->cps_a_register(cps2::cps_a_scroll2_base), 16U,
                    fmt::unsigned_integer);
                add("CPSA_SCR3BASE", system_->cps_a_register(cps2::cps_a_scroll3_base), 16U,
                    fmt::unsigned_integer);
                add("CPSA_ROWBASE", system_->cps_a_register(cps2::cps_a_rowscroll_base), 16U,
                    fmt::unsigned_integer);
                add("CPSA_PALBASE", system_->cps_a_register(cps2::cps_a_palette_base), 16U,
                    fmt::unsigned_integer);
                add("CPSA_SCR1X", system_->cps_a_register(cps2::cps_a_scroll1_x), 16U,
                    fmt::signed_integer);
                add("CPSA_SCR1Y", system_->cps_a_register(cps2::cps_a_scroll1_y), 16U,
                    fmt::signed_integer);
                add("CPSA_SCR2X", system_->cps_a_register(cps2::cps_a_scroll2_x), 16U,
                    fmt::signed_integer);
                add("CPSA_SCR2Y", system_->cps_a_register(cps2::cps_a_scroll2_y), 16U,
                    fmt::signed_integer);
                add("CPSA_SCR3X", system_->cps_a_register(cps2::cps_a_scroll3_x), 16U,
                    fmt::signed_integer);
                add("CPSA_SCR3Y", system_->cps_a_register(cps2::cps_a_scroll3_y), 16U,
                    fmt::signed_integer);
                add("CPSA_ROWOFFS", system_->cps_a_register(cps2::cps_a_rowscroll_offset), 16U,
                    fmt::unsigned_integer);
                add("CPSA_VCTRL", system_->cps_a_register(cps2::cps_a_video_control), 16U,
                    fmt::flags);
                add("PAL_SRC", system_->active_palette_source(), 32U, fmt::unsigned_integer);
                add("CPSB_LAYER", system_->cps_b_register(0x13U), 16U, fmt::flags);
                add("CPSB_PALCTRL", system_->active_palette_control(), 16U, fmt::flags);
                add("CTRL_OBJPRI", control_word(0x04U), 16U, fmt::flags);
                add("CTRL_SPRX", control_word(0x08U), 16U, fmt::signed_integer);
                add("CTRL_SPRY", control_word(0x0AU), 16U, fmt::signed_integer);

                add("S68K_W", q.shared_68k_write_count, 32U, fmt::unsigned_integer);
                add("S68K_NFFW", q.shared_68k_non_ff_write_count, 32U,
                    fmt::unsigned_integer);
                add("S68K_EW", q.shared_68k_even_write_count, 32U, fmt::unsigned_integer);
                add("S68K_ENFFW", q.shared_68k_even_non_ff_write_count, 32U,
                    fmt::unsigned_integer);
                add("S68K_R", q.shared_68k_read_count, 32U, fmt::unsigned_integer);
                add("S68K_OR", q.shared_68k_odd_read_count, 32U, fmt::unsigned_integer);
                add("S68K_ER", q.shared_68k_even_read_count, 32U, fmt::unsigned_integer);
                add("S68K_STATUSR", q.shared_68k_status_read_count, 32U,
                    fmt::unsigned_integer);
                add("S68K_MAGICR", q.shared_68k_magic_read_count, 32U,
                    fmt::unsigned_integer);
                add("CMD68K_W", q.shared_68k_command_signal_write_count, 32U,
                    fmt::unsigned_integer);
                add("CMDZ80_R", q.shared_z80_command_signal_read_count, 32U,
                    fmt::unsigned_integer);
                add("SZ80_W", q.shared_z80_write_count, 32U, fmt::unsigned_integer);
                add("WZ80_W", q.work_z80_write_count, 32U, fmt::unsigned_integer);
                add("LAST68K_IDX", q.shared_last_68k_index, 16U, fmt::unsigned_integer);
                add("LAST68K_VAL", q.shared_last_68k_value, 8U, fmt::unsigned_integer);
                add("LAST68K_WPC", q.shared_last_68k_write_pc, 24U, fmt::unsigned_integer);
                add("LAST68K_NFFWPC", q.shared_last_68k_non_ff_write_pc, 24U,
                    fmt::unsigned_integer);
                add("LAST68K_RIDX", q.shared_last_68k_read_index, 16U,
                    fmt::unsigned_integer);
                add("LAST68K_RVAL", q.shared_last_68k_read_value, 8U,
                    fmt::unsigned_integer);
                add("LAST68K_RPC", q.shared_last_68k_read_pc, 24U, fmt::unsigned_integer);
                add("LAST68K_EIDX", q.shared_last_even_68k_index, 16U,
                    fmt::unsigned_integer);
                add("LAST68K_EVAL", q.shared_last_even_68k_value, 8U,
                    fmt::unsigned_integer);
                add("LASTZ80_ADDR", q.shared_last_z80_addr, 16U, fmt::unsigned_integer);
                add("LASTZ80_VAL", q.shared_last_z80_value, 8U, fmt::unsigned_integer);
                add("STATUS_FIRST_SEEN", q.shared_status_first_read_seen ? 1U : 0U, 1U,
                    fmt::flags);
                add("STATUS_FIRST_VAL", q.shared_status_first_read_value, 8U,
                    fmt::unsigned_integer);
                add("STATUS_FIRST_PC", q.shared_status_first_read_pc, 24U,
                    fmt::unsigned_integer);
                add("STATUS_LAST_VAL", q.shared_status_last_read_value, 8U,
                    fmt::unsigned_integer);
                add("STATUS_LAST_PC", q.shared_status_last_read_pc, 24U,
                    fmt::unsigned_integer);
                add("CMD68K_VAL", q.shared_command_signal_last_68k_value, 8U,
                    fmt::unsigned_integer);
                add("CMD68K_PC", q.shared_command_signal_last_68k_pc, 24U,
                    fmt::unsigned_integer);
                add("CMDZ80_VAL", q.shared_command_signal_last_z80_value, 8U,
                    fmt::unsigned_integer);
                add("WORKZ80_ADDR", q.work_last_z80_addr, 16U, fmt::unsigned_integer);
                add("WORKZ80_VAL", q.work_last_z80_value, 8U, fmt::unsigned_integer);
                add("MAINCYC", system_->cpu().elapsed_cycles(), 64U, fmt::unsigned_integer);
                add("SNDCYC", system_->sound_cpu().elapsed_cycles(), 64U, fmt::unsigned_integer);
                add("SNDDEBT", static_cast<std::uint64_t>(system_->sound_cycle_debt()), 64U,
                    fmt::signed_integer);
                add("SNDACCUM", system_->sound_cycle_accum(), 64U, fmt::unsigned_integer);
                add("SNDIRQACC", system_->qsound_irq_accum(), 32U, fmt::unsigned_integer);
                add("SNDIRQLINE", system_->qsound_irq_line() ? 1U : 0U, 1U, fmt::flags);
                add("SNDRESET", system_->sound_cpu().reset_line_held() ? 1U : 0U, 1U,
                    fmt::flags);
                add("SNAP00", q.shared_command_snapshot[0], 8U, fmt::unsigned_integer);
                add("SNAP01", q.shared_command_snapshot[1], 8U, fmt::unsigned_integer);
                add("SNAP02", q.shared_command_snapshot[2], 8U, fmt::unsigned_integer);
                add("SNAP03", q.shared_command_snapshot[3], 8U, fmt::unsigned_integer);
                add("SNAP04", q.shared_command_snapshot[4], 8U, fmt::unsigned_integer);
                add("SNAP05", q.shared_command_snapshot[5], 8U, fmt::unsigned_integer);
                add("SNAP06", q.shared_command_snapshot[6], 8U, fmt::unsigned_integer);
                add("SNAP07", q.shared_command_snapshot[7], 8U, fmt::unsigned_integer);
                add("SNAP08", q.shared_command_snapshot[8], 8U, fmt::unsigned_integer);
                add("SNAP09", q.shared_command_snapshot[9], 8U, fmt::unsigned_integer);
                add("SNAP10", q.shared_command_snapshot[10], 8U, fmt::unsigned_integer);
                add("SNAP11", q.shared_command_snapshot[11], 8U, fmt::unsigned_integer);
                add("SNAP12", q.shared_command_snapshot[12], 8U, fmt::unsigned_integer);
                add("SNAP13", q.shared_command_snapshot[13], 8U, fmt::unsigned_integer);
                add("SNAP14", q.shared_command_snapshot[14], 8U, fmt::unsigned_integer);
                add("SNAP15", q.shared_command_snapshot[15], 8U, fmt::unsigned_integer);

                return std::span<const chips::register_descriptor>{register_view_.data(), out};
            }

            cps2::cps2_system* system_{};
            std::array<chips::register_descriptor, 77> register_view_{};
            instrumentation::introspection_builder introspection_;
        };

        [[nodiscard]] std::unique_ptr<chips::ichip>
        make_board_debug_chip(cps2::cps2_system& system) {
            return std::make_unique<cps2_board_debug_chip>(system);
        }

        [[nodiscard]] std::span<const std::uint8_t> as_bytes(std::string_view text) noexcept {
            return std::span<const std::uint8_t>(
                reinterpret_cast<const std::uint8_t*>(text.data()), text.size());
        }

        [[nodiscard]] std::uint32_t crc32_u64(std::uint32_t crc,
                                               std::uint64_t value) noexcept {
            std::array<std::uint8_t, 8> bytes{};
            for (std::size_t i = 0; i < bytes.size(); ++i) {
                bytes[i] = static_cast<std::uint8_t>((value >> (i * 8U)) & 0xFFU);
            }
            return mnemos::security::cryptography::crc32(
                std::span<const std::uint8_t>(bytes.data(), bytes.size()), crc);
        }

        [[nodiscard]] std::uint32_t crc32_string(std::uint32_t crc,
                                                 std::string_view text) noexcept {
            crc = crc32_u64(crc, text.size());
            return mnemos::security::cryptography::crc32(text, crc);
        }

        [[nodiscard]] std::string hex32(std::uint32_t value) {
            static constexpr char digits[] = "0123456789abcdef";
            std::string out(8U, '0');
            for (std::size_t i = 0; i < out.size(); ++i) {
                const auto shift = static_cast<unsigned>((out.size() - 1U - i) * 4U);
                out[i] = digits[(value >> shift) & 0x0FU];
            }
            return out;
        }

        [[nodiscard]] std::uint64_t resident_image_byte_count(const rom_set_image& image) noexcept {
            std::uint64_t bytes = 0U;
            for (const auto& [_, region] : image.regions) {
                bytes += region.size();
            }
            return bytes;
        }

        [[nodiscard]] bool resident_image_is_complete(const rom_set_image& image) noexcept {
            if (!image.ok()) {
                return false;
            }
            for (std::string_view region_name : {"maincpu", "gfx", "audiocpu", "qsound", "key"}) {
                const std::vector<std::uint8_t>* region = image.region(region_name);
                if (region == nullptr || region->empty()) {
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] std::string resident_media_crc32(const loaded_set& set) {
            const rom_set_image& image = set.image;
            if (image.regions.empty()) {
                return {};
            }
            std::uint32_t crc =
                mnemos::security::cryptography::crc32("capcom_cps2.resident_media.v1");
            crc = crc32_string(crc, set.set_name);
            crc = crc32_u64(crc, static_cast<std::uint64_t>(set.orientation));
            crc = crc32_u64(crc, set.players);
            crc = crc32_u64(crc, static_cast<std::uint64_t>(set.input_profile));
            crc = crc32_u64(crc, static_cast<std::uint64_t>(set.analog_input_mode));
            crc = crc32_u64(crc, set.coin_lockout_active_high ? 1U : 0U);
            crc = crc32_u64(crc, image.regions.size());
            for (const auto& [name, bytes] : image.regions) {
                crc = crc32_string(crc, name);
                crc = crc32_u64(crc, bytes.size());
                crc = mnemos::security::cryptography::crc32(
                    std::span<const std::uint8_t>(bytes.data(), bytes.size()), crc);
            }
            return hex32(crc);
        }

        [[nodiscard]] frontend_sdk::display_orientation
        to_display_orientation(mnemos::manifests::common::screen_orientation orientation) noexcept {
            switch (orientation) {
            case mnemos::manifests::common::screen_orientation::vertical_counterclockwise:
                return frontend_sdk::display_orientation::vertical_counterclockwise;
            case mnemos::manifests::common::screen_orientation::vertical:
                return frontend_sdk::display_orientation::vertical_clockwise;
            case mnemos::manifests::common::screen_orientation::horizontal:
            default:
                return frontend_sdk::display_orientation::horizontal;
            }
        }

        frontend_sdk::session_capability_info make_session_capabilities(std::uint8_t players) {
            frontend_sdk::session_capability_info session{};
            session.input_ports.reserve(players);
            for (std::uint8_t i = 0U; i < players; ++i) {
                const std::uint8_t slot = static_cast<std::uint8_t>(i + 1U);
                session.input_ports.push_back(frontend_sdk::session_input_port{
                    .port_index = i,
                    .player_slot = slot,
                    .format = frontend_sdk::input_device_format::arcade_panel,
                    .device_id = "cps2.panel.p" + std::to_string(slot),
                    .label = "Player " + std::to_string(slot) + " Panel"});
            }
            session.deterministic_frame_input = true;
            session.save_state_supported = true;
            session.frame_exact_save_state = true;
            session.max_input_delay_frames = 8U;
            return session;
        }

        frontend_sdk::media_capability_info make_media_capabilities(std::string_view display_name,
                                                                    const loaded_set& set,
                                                                    std::uint64_t source_bytes,
                                                                    std::string full_hash) {
            const std::uint64_t resident_bytes = resident_image_byte_count(set.image);
            frontend_sdk::media_capability_info media{};
            media.media.push_back(frontend_sdk::media_image_info{
                .id = "rom_set",
                .label = display_name.empty() ? std::string{"ROM set"} : std::string{display_name},
                .residency = frontend_sdk::media_residency::resident,
                .byte_count = resident_bytes == 0U ? source_bytes : resident_bytes,
                .hash_algorithm = full_hash.empty() ? frontend_sdk::media_hash_algorithm::none
                                                    : frontend_sdk::media_hash_algorithm::crc32,
                .full_hash = std::move(full_hash),
                .provider_id = "cps2.adapter",
                .revision = "loaded",
                .cache_hint = "resident"});
            return media;
        }

        [[nodiscard]] cps2_input_profile
        input_profile_for(const std::optional<std::string>& input, std::uint8_t players) {
            if (input.has_value()) {
                if (*input == "six_button") {
                    return cps2_input_profile::six_button;
                }
                if (*input == "six_button_ticket_dispenser" ||
                    *input == "two_player_six_button_ticket" || *input == "cps2_2p6bt") {
                    return cps2_input_profile::six_button_ticket_dispenser;
                }
                if (*input == "four_player") {
                    return cps2_input_profile::four_player;
                }
                if (*input == "two_player") {
                    return cps2_input_profile::two_player;
                }
                if (*input == "two_player_one_button") {
                    return cps2_input_profile::two_player_one_button;
                }
                if (*input == "two_player_two_button") {
                    return cps2_input_profile::two_player_two_button;
                }
                if (*input == "two_player_three_button") {
                    return cps2_input_profile::two_player;
                }
                if (*input == "two_player_four_button") {
                    return cps2_input_profile::two_player_four_button;
                }
                if (*input == "three_player_three_button") {
                    return cps2_input_profile::three_player_three_button;
                }
                if (*input == "four_player_two_button") {
                    return cps2_input_profile::four_player_two_button;
                }
                if (*input == "four_player_three_button") {
                    return cps2_input_profile::four_player;
                }
                if (*input == "four_player_four_button") {
                    return cps2_input_profile::four_player_four_button;
                }
                if (*input == "one_player_three_button") {
                    return cps2_input_profile::one_player_three_button;
                }
                if (*input == "cybots_four_button") {
                    return cps2_input_profile::cybots_four_button;
                }
                if (*input == "ecofighters_spinner") {
                    return cps2_input_profile::ecofighters_spinner;
                }
                if (*input == "puzz_loop_2_paddle") {
                    return cps2_input_profile::puzz_loop_2_paddle;
                }
                std::fprintf(stderr,
                             "[capcom_cps2] unsupported input profile '%s'; using automatic "
                             "layout\n",
                             input->c_str());
            }
            return players > 2U ? cps2_input_profile::four_player
                                : cps2_input_profile::six_button;
        }

        [[nodiscard]] cps2::cps2_analog_input_mode
        analog_input_mode_for(cps2_input_profile profile) noexcept {
            switch (profile) {
            case cps2_input_profile::ecofighters_spinner:
                return cps2::cps2_analog_input_mode::eco_fighters;
            case cps2_input_profile::puzz_loop_2_paddle:
                return cps2::cps2_analog_input_mode::puzz_loop_2;
            default:
                return cps2::cps2_analog_input_mode::none;
            }
        }

        [[nodiscard]] bool coin_lockout_active_high_for(std::string_view set_name) noexcept {
            constexpr std::string_view prefix = "mmatrix";
            return set_name.size() >= prefix.size() &&
                   set_name.substr(0U, prefix.size()) == prefix;
        }

        // Resolve a clone set's parent zip beside the clone on disk and compose a
        // fallback provider (clone first, then parent) while carrying any parent
        // zip-contained key candidates for validated CPS2 key recovery.
        [[nodiscard]] resolved_rom_provider
        with_parent_fallback(const mnemos::manifests::common::rom_file_provider& clone,
                             std::vector<key_file_candidate> key_candidates,
                             const std::string& parent, const std::string& rom_path) {
            resolved_rom_provider result{clone, std::move(key_candidates)};
            if (rom_path.empty()) {
                std::fprintf(stderr,
                             "[capcom_cps2] set declares parent '%s' but no path is known to "
                             "locate it; shared ROMs will be missing\n",
                             parent.c_str());
                return result;
            }
            if (parent.find('/') != std::string::npos || parent.find('\\') != std::string::npos ||
                parent.find("..") != std::string::npos) {
                std::fprintf(stderr,
                             "[capcom_cps2] refusing to resolve parent '%s': not a plain set id\n",
                             parent.c_str());
                return result;
            }
            const auto slash = rom_path.find_last_of("/\\");
            const std::string dir =
                slash == std::string::npos ? std::string{} : rom_path.substr(0, slash + 1);
            const std::string parent_path = dir + parent + ".zip";
            auto parent_bytes = mnemos::io::read_file(parent_path);
            if (!parent_bytes.has_value()) {
                std::fprintf(stderr, "[capcom_cps2] parent set not found: %s\n",
                             parent_path.c_str());
                return result;
            }
            auto parent_key_candidates = collect_zip_key_candidates(*parent_bytes);
            auto parent_provider =
                mnemos::manifests::common::make_zip_rom_provider(std::move(*parent_bytes));
            if (!parent_provider.has_value()) {
                std::fprintf(stderr, "[capcom_cps2] parent set is not a readable zip: %s\n",
                             parent_path.c_str());
                return result;
            }
            for (key_file_candidate& candidate : parent_key_candidates) {
                candidate.label = parent_path + ":" + candidate.label;
                result.key_candidates.push_back(std::move(candidate));
            }
            result.provider = mnemos::manifests::common::make_fallback_rom_provider(
                clone, std::move(*parent_provider));
            return result;
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

        [[nodiscard]] bool is_plain_set_id(std::string_view name) noexcept {
            return !name.empty() && name.find('/') == std::string_view::npos &&
                   name.find('\\') == std::string_view::npos &&
                   name.find("..") == std::string_view::npos;
        }

        [[nodiscard]] std::string set_id_from_rom_path(const std::string& rom_path) {
            if (rom_path.empty()) {
                return {};
            }
            namespace fs = std::filesystem;
            std::string set_id = fs::path(rom_path).stem().string();
            return is_plain_set_id(set_id) ? set_id : std::string{};
        }

        [[nodiscard]] std::string filename_from_entry(std::string_view name) {
            const std::size_t slash = name.find_last_of("/\\");
            return std::string(slash == std::string_view::npos ? name : name.substr(slash + 1U));
        }

        [[nodiscard]] std::string stem_from_filename(std::string_view name) {
            const std::size_t dot = name.find_last_of('.');
            return std::string(dot == std::string_view::npos ? name : name.substr(0U, dot));
        }

        [[nodiscard]] bool has_key_extension(std::string_view name) noexcept {
            const std::size_t dot = name.find_last_of('.');
            return dot != std::string_view::npos && name.substr(dot) == ".key";
        }

        void push_key_id(std::vector<std::string>& ids, std::string id) {
            if (!is_plain_set_id(id)) {
                return;
            }
            if (std::find(ids.begin(), ids.end(), id) == ids.end()) {
                ids.push_back(std::move(id));
            }
        }

        [[nodiscard]] std::vector<std::string>
        key_id_candidates(const std::string& set_name, const std::optional<std::string>& parent,
                          const std::string& rom_path) {
            std::vector<std::string> ids;
            push_key_id(ids, set_name);
            if (parent.has_value()) {
                push_key_id(ids, *parent);
            }
            if (!rom_path.empty()) {
                namespace fs = std::filesystem;
                std::error_code ec;
                const fs::path stem = fs::path(rom_path).stem();
                push_key_id(ids, stem.string());
            }
            return ids;
        }

        [[nodiscard]] int key_match_rank(std::string_view stem,
                                         const std::vector<std::string>& ids) noexcept {
            if (stem.empty()) {
                return 2;
            }
            for (const std::string& id : ids) {
                if (stem == id) {
                    return 0;
                }
            }
            for (const std::string& id : ids) {
                if (stem.rfind(id, 0U) == 0U || std::string_view{id}.rfind(stem, 0U) == 0U) {
                    return 1;
                }
            }
            return 2;
        }

        [[nodiscard]] std::vector<key_file_candidate>
        collect_zip_key_candidates(std::span<const std::uint8_t> archive_bytes) {
            std::vector<key_file_candidate> candidates;
            const auto archive = mnemos::compression::zip_archive::open(archive_bytes);
            if (!archive.has_value()) {
                return candidates;
            }
            for (const mnemos::compression::zip_entry& entry : archive->entries()) {
                const std::string filename = filename_from_entry(entry.name);
                if (filename.empty() || !has_key_extension(filename)) {
                    continue;
                }
                auto bytes = archive->extract(entry);
                if (!bytes.has_value()) {
                    continue;
                }
                std::string stem = stem_from_filename(filename);
                if (stem.empty()) {
                    continue;
                }
                candidates.push_back({entry.name, std::move(stem), std::move(*bytes)});
            }
            return candidates;
        }

        // CPS2 boards are encrypted; the board key is a 20-byte external asset. If
        // the declaration did not place a "key" region, scan provider-backed zip
        // entries plus `<dir>/keys` and the zip's own dir for `.key` files whose
        // name shares the set's prefix, and adopt the first that decrypts the
        // program to a sane reset vector. (The zip name does not reliably encode
        // the region, so the right variant is chosen by validation, mirroring the
        // reference loader.)
        void resolve_key_region(rom_set_image& image, const std::string& set_name,
                                const std::optional<std::string>& parent,
                                const std::string& rom_path,
                                const mnemos::manifests::common::rom_file_provider& provider,
                                std::vector<key_file_candidate> zip_keys) {
            if (const auto* k = image.region("key");
                k != nullptr && k->size() == cps2::crypto_key_size) {
                return; // already supplied by the declaration
            }
            const auto* program = image.region("maincpu");
            if (program == nullptr || program->empty() || (program->size() & 1U) != 0U) {
                return;
            }
            const std::vector<std::string> ids = key_id_candidates(set_name, parent, rom_path);
            if (ids.empty()) {
                return;
            }

            const auto adopt = [&image, program](const std::string& label,
                                                 std::vector<std::uint8_t> bytes) {
                if (bytes.size() == cps2::crypto_key_size && key_decrypts_program(bytes, *program)) {
                    std::fprintf(stderr, "[capcom_cps2] board key: %s\n", label.c_str());
                    image.regions["key"] = std::move(bytes);
                    return true;
                }
                return false;
            };

            if (provider) {
                for (const std::string& id : ids) {
                    for (const std::string& name : {id + ".key", "keys/" + id + ".key"}) {
                        if (auto bytes = provider(name)) {
                            if (adopt(name, std::move(*bytes))) {
                                return;
                            }
                        }
                    }
                }
            }

            std::stable_sort(zip_keys.begin(), zip_keys.end(),
                             [&ids](const key_file_candidate& a, const key_file_candidate& b) {
                                 const int ar = key_match_rank(a.stem, ids);
                                 const int br = key_match_rank(b.stem, ids);
                                 if (ar != br) {
                                     return ar < br;
                                 }
                                 return a.stem.size() < b.stem.size();
                             });
            const bool allow_single_unmatched_key = zip_keys.size() == 1U;
            for (key_file_candidate& candidate : zip_keys) {
                if (key_match_rank(candidate.stem, ids) > 1 && !allow_single_unmatched_key) {
                    continue;
                }
                if (adopt("zip:" + candidate.label, std::move(candidate.bytes))) {
                    return;
                }
            }

            if (rom_path.empty()) {
                std::fprintf(stderr,
                             "[capcom_cps2] no valid board key for '%s' in the zip -- the board "
                             "stays a non-executable blocker\n",
                             set_name.c_str());
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
                    if (key_match_rank(stem, ids) < 2) {
                        candidates.push_back(entry.path());
                    }
                }
                // Prefer an exact name match, then the shorter (base) names.
                std::sort(candidates.begin(), candidates.end(),
                          [&ids](const fs::path& a, const fs::path& b) {
                              const std::string as = a.stem().string();
                              const std::string bs = b.stem().string();
                              const int ar = key_match_rank(as, ids);
                              const int br = key_match_rank(bs, ids);
                              if (ar != br) {
                                  return ar < br;
                              }
                              return as.size() < bs.size();
                          });
                for (const auto& candidate : candidates) {
                    auto bytes = mnemos::io::read_file(candidate.string());
                    if (bytes && adopt(candidate.string(), std::move(*bytes))) {
                        return;
                    }
                }
            }
            std::fprintf(stderr,
                         "[capcom_cps2] no valid board key for '%s' beside %s -- the board stays "
                         "a non-executable blocker\n",
                         set_name.c_str(), rom_path.c_str());
        }

        [[nodiscard]] loaded_set
        load_declared_set(std::string_view text, std::string source_name,
                          const mnemos::manifests::common::rom_file_provider& provider,
                          std::vector<key_file_candidate> zip_key_candidates,
                          const std::string& rom_path) {
            loaded_set result;
            const auto parsed = mnemos::manifests::common::parse_rom_set_decl(text, source_name);
            if (!parsed.ok()) {
                for (const auto& error : parsed.errors) {
                    std::fprintf(stderr, "[capcom_cps2] %s:%u:%u: %s\n", error.source.c_str(),
                                 error.line, error.column, error.message.c_str());
                }
                return result; // declared but invalid: boot an empty board
            }
            if (parsed.value->board != "capcom_cps2") {
                std::fprintf(stderr,
                             "[capcom_cps2] %s declares board '%s', expected 'capcom_cps2'\n",
                             source_name.c_str(), parsed.value->board.c_str());
                return result;
            }
            result.set_name = parsed.value->name;
            result.orientation = to_display_orientation(parsed.value->orientation);
            result.players = parsed.value->players;
            result.input_profile = input_profile_for(parsed.value->input, result.players);
            result.analog_input_mode = analog_input_mode_for(result.input_profile);
            result.coin_lockout_active_high =
                coin_lockout_active_high_for(parsed.value->name) ||
                (parsed.value->parent.has_value() &&
                 coin_lockout_active_high_for(*parsed.value->parent));
            auto effective = parsed.value->parent.has_value()
                                 ? with_parent_fallback(provider, std::move(zip_key_candidates),
                                                        *parsed.value->parent, rom_path)
                                 : resolved_rom_provider{provider, std::move(zip_key_candidates)};
            result.image = mnemos::manifests::common::load_rom_set(*parsed.value,
                                                                   effective.provider);
            for (const auto& issue : result.image.issues) {
                std::fprintf(stderr, "[capcom_cps2] %s: %s\n", issue.file.c_str(),
                             issue.message.c_str());
            }
            resolve_key_region(result.image, parsed.value->name, parsed.value->parent, rom_path,
                               effective.provider, std::move(effective.key_candidates));
            return result;
        }

        // Set loader. A .zip carrying a "game.toml" (schema mnemos-romset/1) loads
        // declaratively. A normal set zip without that extra file resolves the
        // same checked-in declaration by zip stem (games/<set>.toml), so authentic
        // MAME-style archives remain read-only. A clone names a `parent` whose zip
        // supplies shared dumps. Without any declaration, the development format
        // applies (region-named <region>.bin entries). A bare binary is the
        // encrypted 68000 program.
        [[nodiscard]] loaded_set load_set(std::vector<std::uint8_t> rom,
                                          const std::string& rom_path) {
            loaded_set result;
            const bool is_zip = rom.size() >= 4U && rom[0] == 'P' && rom[1] == 'K';
            if (!is_zip) {
                result.set_name = set_id_from_rom_path(rom_path);
                result.image.regions.emplace("maincpu", std::move(rom));
                return result;
            }
            auto zip_key_candidates = collect_zip_key_candidates(rom);
            auto provider = mnemos::manifests::common::make_zip_rom_provider(std::move(rom));
            if (!provider.has_value()) {
                return result;
            }
            if (auto manifest_bytes = (*provider)("game.toml")) {
                const std::string text(manifest_bytes->begin(), manifest_bytes->end());
                return load_declared_set(text, "game.toml", *provider,
                                         std::move(zip_key_candidates), rom_path);
            }
            if (const std::string set_id = set_id_from_rom_path(rom_path); !set_id.empty()) {
                const std::string_view manifest = cps2::cps2_game_manifest_toml(set_id);
                if (!manifest.empty()) {
                    return load_declared_set(
                        manifest, std::string{"capcom_cps2/games/"} + set_id + ".toml",
                        *provider, std::move(zip_key_candidates), rom_path);
                }
            }
            for (const char* region : {"maincpu", "gfx", "audiocpu", "qsound", "key"}) {
                if (auto bytes = (*provider)(std::string{region} + ".bin")) {
                    result.image.regions.emplace(region, std::move(*bytes));
                }
            }
            return result;
        }

        [[nodiscard]] std::unique_ptr<cps2::cps2_system>
        assemble_from(rom_set_image image, cps2::cps2_analog_input_mode analog_input_mode,
                      bool coin_lockout_active_high) {
            cps2::cps2_board_params params;
            params.analog_input = analog_input_mode;
            params.coin_lockout_active_high = coin_lockout_active_high;
            return std::make_unique<cps2::cps2_system>(std::move(image), params);
        }

        void save_controller_state(chips::state_writer& writer,
                                   const frontend_sdk::controller_state& state) {
            writer.boolean(state.up);
            writer.boolean(state.down);
            writer.boolean(state.left);
            writer.boolean(state.right);
            writer.boolean(state.start);
            writer.boolean(state.select);
            writer.boolean(state.a);
            writer.boolean(state.b);
            writer.boolean(state.c);
            writer.boolean(state.x);
            writer.boolean(state.y);
            writer.boolean(state.z);
            writer.boolean(state.mode);
            writer.boolean(state.service);
            writer.boolean(state.test);
            writer.u16(std::bit_cast<std::uint16_t>(state.aim_x));
            writer.u16(std::bit_cast<std::uint16_t>(state.aim_y));
            writer.boolean(state.trigger);
            writer.u16(state.paddle);
        }

        [[nodiscard]] frontend_sdk::controller_state
        load_controller_state(chips::state_reader& reader, std::uint32_t version) {
            frontend_sdk::controller_state state{};
            state.up = reader.boolean();
            state.down = reader.boolean();
            state.left = reader.boolean();
            state.right = reader.boolean();
            state.start = reader.boolean();
            state.select = reader.boolean();
            state.a = reader.boolean();
            state.b = reader.boolean();
            state.c = reader.boolean();
            state.x = reader.boolean();
            state.y = reader.boolean();
            state.z = reader.boolean();
            state.mode = reader.boolean();
            state.service = reader.boolean();
            state.test = reader.boolean();
            state.aim_x = std::bit_cast<std::int16_t>(reader.u16());
            state.aim_y = std::bit_cast<std::int16_t>(reader.u16());
            state.trigger = reader.boolean();
            if (version >= 3U) {
                state.paddle = reader.u16();
            }
            return state;
        }

        [[nodiscard]] bool valid_input_profile(std::uint8_t value) noexcept {
            switch (static_cast<cps2_input_profile>(value)) {
            case cps2_input_profile::six_button:
            case cps2_input_profile::four_player:
            case cps2_input_profile::two_player:
            case cps2_input_profile::two_player_one_button:
            case cps2_input_profile::two_player_two_button:
            case cps2_input_profile::two_player_four_button:
            case cps2_input_profile::three_player_three_button:
            case cps2_input_profile::four_player_two_button:
            case cps2_input_profile::four_player_four_button:
            case cps2_input_profile::one_player_three_button:
            case cps2_input_profile::cybots_four_button:
            case cps2_input_profile::ecofighters_spinner:
            case cps2_input_profile::puzz_loop_2_paddle:
            case cps2_input_profile::six_button_ticket_dispenser:
                return true;
            }
            return false;
        }

        [[nodiscard]] bool valid_orientation(std::uint8_t value) noexcept {
            switch (static_cast<frontend_sdk::display_orientation>(value)) {
            case frontend_sdk::display_orientation::horizontal:
            case frontend_sdk::display_orientation::vertical:
            case frontend_sdk::display_orientation::vertical_counterclockwise:
                return true;
            }
            return false;
        }

    } // namespace

    capcom_cps2_adapter::capcom_cps2_adapter(std::vector<std::uint8_t> rom,
                                             std::string display_name,
                                             frontend_sdk::scheduler_factory* /*scheduler_factory*/,
                                             std::optional<std::uint16_t> dip_override,
                                             std::string rom_path)
    // The CPS2 board integrates the 68000 + the QSound Z80/DSP + the beam in
    // its own run_frame(), so there is no per-chip master-clock schedule to
    // build; the scheduler_factory override does not apply.
    {
        const std::uint64_t source_bytes = rom.size();
        loaded_set set = load_set(std::move(rom), rom_path);
        resident_media_hash_ = resident_media_crc32(set);
        media_ = make_media_capabilities(display_name, set, source_bytes,
                                         resident_image_is_complete(set.image)
                                             ? resident_media_hash_
                                             : std::string{});
        orientation_ = set.orientation;
        player_count_ = set.players;
        input_profile_ = set.input_profile;
        session_ = make_session_capabilities(player_count_);
        sys_ = assemble_from(std::move(set.image), set.analog_input_mode,
                             set.coin_lockout_active_high);
        if (dip_override.has_value()) {
            sys_->set_development_dips(
                {static_cast<std::uint8_t>(*dip_override & 0xFFU),
                 static_cast<std::uint8_t>((*dip_override >> 8U) & 0xFFU), 0xFFU});
        }
        board_debug_chip_ = make_board_debug_chip(*sys_);
        chip_view_ = {&sys_->video(), &sys_->cpu(), &sys_->sound_cpu(), &sys_->qsound_dsp(),
                      board_debug_chip_.get()};
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
        publish(10U, "development_dips", sys_->development_dips());
    }

    void capcom_cps2_adapter::capture_audio_slice_callback(
        void* context,
        std::uint64_t frame_budget,
        std::uint64_t frame_cycles_done) noexcept {
        auto* adapter = static_cast<capcom_cps2_adapter*>(context);
        if (adapter == nullptr) {
            return;
        }
        adapter->frame_audio_cycle_budget_ = frame_budget == 0U
                                                  ? cps2::cpu_cycles_per_frame
                                                  : frame_budget;
        adapter->capture_audio_until(adapter->frame_audio_cycle_budget_, frame_cycles_done);
    }

    void capcom_cps2_adapter::capture_audio_until(
        std::uint64_t frame_budget,
        std::uint64_t frame_cycles_done) noexcept {
        if (frame_audio_target_ == 0U || frame_budget == 0U) {
            return;
        }
        const std::uint64_t clamped_cycles = std::min(frame_cycles_done, frame_budget);
        const std::uint64_t target =
            frame_audio_target_ * clamped_cycles / frame_budget;
        while (frame_audio_generated_ < target) {
            append_qsound_output_sample();
        }
    }

    void capcom_cps2_adapter::append_qsound_output_sample() noexcept {
        auto& qsound = sys_->qsound_dsp();
        qsound_output_accum_ += cps2_qsound_rate_num;
        while (qsound_output_accum_ >= cps2_qsound_rate_den) {
            qsound_output_accum_ -= cps2_qsound_rate_den;
            qsound_prev_left_ = qsound_curr_left_;
            qsound_prev_right_ = qsound_curr_right_;
            qsound.step();
            qsound_curr_left_ = qsound.last_left();
            qsound_curr_right_ = qsound.last_right();
        }
        const auto blend = [this](std::int16_t previous, std::int16_t current) noexcept {
            const std::int64_t delta =
                static_cast<std::int64_t>(current) - static_cast<std::int64_t>(previous);
            const std::int64_t scaled =
                static_cast<std::int64_t>(previous) +
                (delta * static_cast<std::int64_t>(qsound_output_accum_)) /
                    static_cast<std::int64_t>(cps2_qsound_rate_den);
            return static_cast<std::int16_t>(std::clamp<std::int64_t>(
                scaled,
                static_cast<std::int64_t>(std::numeric_limits<std::int16_t>::min()),
                static_cast<std::int64_t>(std::numeric_limits<std::int16_t>::max())));
        };
        pending_audio_.push_back(blend(qsound_prev_left_, qsound_curr_left_));
        pending_audio_.push_back(blend(qsound_prev_right_, qsound_curr_right_));
        ++frame_audio_generated_;
    }

    void capcom_cps2_adapter::reset_audio_pipeline(bool reset_timing) noexcept {
        audio_buf_.clear();
        pending_audio_.clear();
        if (reset_timing) {
            qsound_output_accum_ = 0U;
        }
        qsound_prev_left_ = 0;
        qsound_prev_right_ = 0;
        qsound_curr_left_ = 0;
        qsound_curr_right_ = 0;
        frame_audio_target_ = 0U;
        frame_audio_generated_ = 0U;
        frame_audio_cycle_budget_ = cps2::cpu_cycles_per_frame;
    }

    void capcom_cps2_adapter::step_one_frame() {
        const std::uint64_t due_before = audio_due_after_frames(frames_stepped_);
        const std::uint64_t due_after = audio_due_after_frames(frames_stepped_ + 1U);
        frame_audio_target_ = due_after - due_before;
        frame_audio_generated_ = 0U;
        frame_audio_cycle_budget_ = cps2::cpu_cycles_per_frame;
        pending_audio_.reserve(
            pending_audio_.size() + static_cast<std::size_t>(frame_audio_target_ * 2U));

        // Slice the frame at roughly one output sample of 68K time so short-lived
        // QSound register changes are rendered before later driver writes replace
        // them.
        sys_->run_frame_sliced(cps2_audio_slice_cycles,
                               &capcom_cps2_adapter::capture_audio_slice_callback,
                               this);
        capture_audio_until(frame_audio_cycle_budget_, frame_audio_cycle_budget_);
        samples_drained_ = due_after;
        frame_audio_target_ = 0U;
        frame_audio_generated_ = 0U;
        ++frames_stepped_;
    }

    frontend_sdk::audio_chunk capcom_cps2_adapter::drain_audio() noexcept {
        if (pending_audio_.empty()) {
            return {.samples = nullptr, .frame_count = 0U, .sample_rate = cps2_audio_output_rate};
        }
        audio_buf_.swap(pending_audio_);
        pending_audio_.clear();
        return {.samples = audio_buf_.data(),
                .frame_count = static_cast<std::uint32_t>(audio_buf_.size() / 2U),
                .sample_rate = cps2_audio_output_rate};
    }

    void capcom_cps2_adapter::apply_input(int port,
                                          const frontend_sdk::controller_state& state) noexcept {
        if (port < 0 || static_cast<std::size_t>(port) >= player_count_) {
            return;
        }
        ports_[static_cast<std::size_t>(port)] = state;
        refresh_inputs();
    }

    std::vector<std::uint8_t> capcom_cps2_adapter::save_state() {
        return runtime::write_save_state(build_save_target(*this));
    }

    runtime::load_result capcom_cps2_adapter::load_state(std::span<const std::uint8_t> data) {
        runtime::save_target target = build_save_target(*this);
        const runtime::load_result result = runtime::read_save_state(data, target);
        if (result.ok()) {
            reset_audio_pipeline(false);
        }
        return result;
    }

    void capcom_cps2_adapter::refresh_inputs() noexcept {
        const auto clear8 = [](std::uint8_t& word, std::uint8_t bit) {
            word = static_cast<std::uint8_t>(word & static_cast<std::uint8_t>(~bit));
        };
        const auto clear16 = [](std::uint16_t& word, std::uint16_t bit) {
            word = static_cast<std::uint16_t>(word & static_cast<std::uint16_t>(~bit));
        };
        const auto main_button_count = [this]() -> std::uint8_t {
            switch (input_profile_) {
            case cps2_input_profile::two_player_one_button:
            case cps2_input_profile::puzz_loop_2_paddle:
                return 1U;
            case cps2_input_profile::two_player_two_button:
            case cps2_input_profile::four_player_two_button:
                return 2U;
            case cps2_input_profile::two_player_four_button:
            case cps2_input_profile::four_player_four_button:
                return 4U;
            case cps2_input_profile::six_button:
            case cps2_input_profile::six_button_ticket_dispenser:
            case cps2_input_profile::four_player:
            case cps2_input_profile::two_player:
            case cps2_input_profile::three_player_three_button:
            case cps2_input_profile::one_player_three_button:
            case cps2_input_profile::cybots_four_button:
            case cps2_input_profile::ecofighters_spinner:
            default:
                return 3U;
            }
        };
        // Main player byte (active low): right/left/down/up in bits 0-3, then
        // buttons 1-4 in bits 4-7 for profiles whose cabinet exposes them there.
        const auto pack_main = [&clear8](const frontend_sdk::controller_state& c,
                                         std::uint8_t buttons) -> std::uint8_t {
            std::uint8_t result = pack_active_low_pad(c, dpad_layout{}, {});
            if (buttons >= 1U && c.a) {
                clear8(result, 0x10U);
            }
            if (buttons >= 2U && c.b) {
                clear8(result, 0x20U);
            }
            if (buttons >= 3U && c.c) {
                clear8(result, 0x40U);
            }
            if (buttons >= 4U && c.x) {
                clear8(result, 0x80U);
            }
            return result;
        };
        const auto main_byte = [this, &pack_main, &main_button_count](
                                   std::size_t port) -> std::uint8_t {
            return port < player_count_ ? pack_main(ports_[port], main_button_count()) : 0xFFU;
        };
        const auto multi_player_word = [this, &pack_main, &main_button_count]() -> std::uint16_t {
            return static_cast<std::uint16_t>(
                (static_cast<std::uint16_t>(player_count_ > 3U
                                                ? pack_main(ports_[3U], main_button_count())
                                                : 0xFFU)
                 << 8U) |
                (player_count_ > 2U ? pack_main(ports_[2U], main_button_count()) : 0xFFU));
        };

        // P2 high byte, P1 low byte.
        sys_->input0 = static_cast<std::uint16_t>(
            (static_cast<std::uint16_t>(main_byte(1U)) << 8U) | main_byte(0U));
        switch (input_profile_) {
        case cps2_input_profile::six_button:
        case cps2_input_profile::six_button_ticket_dispenser: {
            // Two-row fighters wire P1 buttons 4-6 to IN1 bits 0-2, P2 buttons
            // 4-5 to IN1 bits 4-5, and P2 button 6 to IN2 bit 14. SFA3's
            // Hispanic/Brazil dispenser profile adds an active-high ticket-empty
            // line on IN1 bit 13; keep it low (tickets present) until a real
            // dispenser device exists.
            std::uint16_t extra = 0xFFFFU;
            if (input_profile_ == cps2_input_profile::six_button_ticket_dispenser) {
                clear16(extra, 0x2000U);
            }
            if (player_count_ > 0U) {
                if (ports_[0U].x) {
                    clear16(extra, 0x0001U);
                }
                if (ports_[0U].y) {
                    clear16(extra, 0x0002U);
                }
                if (ports_[0U].z) {
                    clear16(extra, 0x0004U);
                }
            }
            if (player_count_ > 1U) {
                if (ports_[1U].x) {
                    clear16(extra, 0x0010U);
                }
                if (ports_[1U].y) {
                    clear16(extra, 0x0020U);
                }
            }
            sys_->input1 = extra;
            break;
        }
        case cps2_input_profile::cybots_four_button: {
            std::uint16_t extra = 0xFFFFU;
            if (player_count_ > 0U && ports_[0U].x) {
                clear16(extra, 0x0001U);
            }
            if (player_count_ > 1U && ports_[1U].x) {
                clear16(extra, 0x0010U);
            }
            sys_->input1 = extra;
            break;
        }
        case cps2_input_profile::ecofighters_spinner:
            // MAME's Eco Fighters profile defaults "Use Spinners" to yes by
            // clearing IN1 bit 4; the CPU then selects digital vs dial reads.
            sys_->input1 = 0xFFEFU;
            break;
        case cps2_input_profile::four_player:
        case cps2_input_profile::three_player_three_button:
        case cps2_input_profile::four_player_two_button:
        case cps2_input_profile::four_player_four_button:
            // Multiplayer CPS2 beat-em-up cabinets repurpose IN1 as P3/P4 main
            // controls with the same per-player button count as IN0.
            sys_->input1 = multi_player_word();
            break;
        case cps2_input_profile::two_player_one_button:
        case cps2_input_profile::two_player_two_button:
        case cps2_input_profile::two_player_four_button:
        case cps2_input_profile::one_player_three_button:
        case cps2_input_profile::puzz_loop_2_paddle:
        case cps2_input_profile::two_player:
            sys_->input1 = 0xFFFFU;
            break;
        default:
            sys_->input1 = 0xFFFFU;
            break;
        }

        // System word (active low): IN2 bit 1 is the operator test switch, bit 2
        // is service credit, START1-4 are bits 8-11, and COIN1-4 are bits 12-15.
        // Bit 0 is the EEPROM data-out the board overlays at read time. On
        // six-button cabinets, bit 14 is P2 button 6 instead of COIN3.
        std::uint16_t system = 0xFFFFU;
        for (std::uint8_t i = 0U; i < player_count_; ++i) {
            if (ports_[i].test) {
                clear16(system, 0x0002U);
            }
            if (ports_[i].service) {
                clear16(system, 0x0004U);
            }
            if (ports_[i].start) {
                clear16(system, static_cast<std::uint16_t>(0x0100U << i));
            }
            if (ports_[i].select) {
                clear16(system, static_cast<std::uint16_t>(0x1000U << i));
            }
        }
        if ((input_profile_ == cps2_input_profile::six_button ||
             input_profile_ == cps2_input_profile::six_button_ticket_dispenser) &&
            player_count_ > 1U && ports_[1U].z) {
            clear16(system, 0x4000U);
        }
        for (std::uint8_t i = 0U; i < 2U; ++i) {
            sys_->set_analog_dial(i, ports_[i].paddle);
        }
        sys_->input_sys = system;
    }

    void force_link() noexcept {}

    runtime::save_target build_save_target(capcom_cps2_adapter& adapter) {
        runtime::save_target target;
        target.manifest_id = adapter.resident_media_hash_.empty()
                                 ? std::string{"capcom_cps2"}
                                 : "capcom_cps2:" + adapter.resident_media_hash_;
        target.manifest_rev = 1U;
        target.master_cycle = adapter.sys_->cpu().elapsed_cycles();
        target.components.push_back(
            {"board",
             [&adapter](chips::state_writer& writer) { adapter.sys_->save_state(writer); },
             [&adapter](chips::state_reader& reader) { adapter.sys_->load_state(reader); }});
        target.components.push_back(
            {"adapter",
             [&adapter](chips::state_writer& writer) {
                 writer.u32(cps2_adapter_state_version);
                 writer.u8(adapter.player_count_);
                 writer.u8(static_cast<std::uint8_t>(adapter.input_profile_));
                 writer.u8(static_cast<std::uint8_t>(adapter.orientation_));
                 writer.blob(as_bytes(adapter.resident_media_hash_));
                 writer.u64(adapter.frames_stepped_);
                 writer.u64(adapter.samples_drained_);
                 writer.u64(adapter.qsound_output_accum_);
                 for (const frontend_sdk::controller_state& port : adapter.ports_) {
                     save_controller_state(writer, port);
                 }
             },
             [&adapter](chips::state_reader& reader) {
                 const std::uint32_t version = reader.u32();
                 const std::uint8_t players = reader.u8();
                 const std::uint8_t profile = reader.u8();
                 const std::uint8_t orientation = reader.u8();
                 const std::vector<std::uint8_t> media_hash_bytes =
                     version >= 4U ? reader.blob() : std::vector<std::uint8_t>{};
                 const std::string media_hash(media_hash_bytes.begin(), media_hash_bytes.end());
                 if (version < 2U || version > cps2_adapter_state_version ||
                     players != adapter.player_count_ ||
                     !valid_input_profile(profile) ||
                     static_cast<cps2_input_profile>(profile) != adapter.input_profile_ ||
                     !valid_orientation(orientation) ||
                     static_cast<frontend_sdk::display_orientation>(orientation) !=
                         adapter.orientation_ ||
                     (version >= 4U && media_hash != adapter.resident_media_hash_)) {
                     reader.fail();
                     return;
                 }
                 adapter.frames_stepped_ = reader.u64();
                 adapter.samples_drained_ = reader.u64();
                 adapter.qsound_output_accum_ = version >= 5U ? reader.u64() : 0U;
                 for (frontend_sdk::controller_state& port : adapter.ports_) {
                     port = load_controller_state(reader, version);
                 }
                 if (reader.ok()) {
                     adapter.refresh_inputs();
                     adapter.reset_audio_pipeline(false);
                 }
             }});
        return target;
    }

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
