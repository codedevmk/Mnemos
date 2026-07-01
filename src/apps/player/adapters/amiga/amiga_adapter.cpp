#include "amiga_adapter.hpp"

#include "adapter_registry.hpp"
#include "audio_resampler.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace mnemos::apps::player::adapters::amiga {

    namespace {
        [[nodiscard]] std::uint8_t
        board_irq_level(const manifests::amiga::amiga_system& sys) noexcept {
            using system = manifests::amiga::amiga_system;
            if ((sys.intena & system::int_master) == 0U) {
                return 0U;
            }

            const std::uint16_t pending =
                static_cast<std::uint16_t>(sys.intena & sys.visible_intreq() & 0x3FFFU);
            if ((pending & system::int_exter) != 0U) {
                return 6U;
            }
            if ((pending & (system::int_rbf | system::int_dsksyn)) != 0U) {
                return 5U;
            }
            if ((pending & (system::int_aud0 | system::int_aud1 | system::int_aud2 |
                            system::int_aud3)) != 0U) {
                return 4U;
            }
            if ((pending & (system::int_coper | system::int_vertb | system::int_blit)) != 0U) {
                return 3U;
            }
            if ((pending & system::int_ports) != 0U) {
                return 2U;
            }
            if ((pending & (system::int_tbe | system::int_dskblk | system::int_soft)) != 0U) {
                return 1U;
            }
            return 0U;
        }

        [[nodiscard]] std::uint64_t bool_value(bool value) noexcept { return value ? 1U : 0U; }

        class board_registers final : public instrumentation::register_view {
          public:
            explicit board_registers(manifests::amiga::amiga_system& sys) noexcept : sys_(&sys) {}

            [[nodiscard]] std::span<const chips::register_descriptor> registers() override {
                using chips::register_value_format;
                const auto& sys = *sys_;
                const auto& drive = sys.floppy_active_drive < sys.floppy_drives.size()
                                        ? sys.floppy_drives[sys.floppy_active_drive]
                                        : sys.floppy_drives[0U];

                registers_ = {
                    chips::register_descriptor{
                        .name = "INTENA",
                        .value = sys.intena,
                        .bit_width = 16U,
                        .format = register_value_format::flags,
                    },
                    chips::register_descriptor{
                        .name = "INTREQ",
                        .value = sys.visible_intreq(),
                        .bit_width = 16U,
                        .format = register_value_format::flags,
                    },
                    chips::register_descriptor{
                        .name = "RAWINTREQ",
                        .value = sys.intreq,
                        .bit_width = 16U,
                        .format = register_value_format::flags,
                    },
                    chips::register_descriptor{
                        .name = "IRQ",
                        .value = board_irq_level(sys),
                        .bit_width = 3U,
                        .format = register_value_format::unsigned_integer,
                    },
                    chips::register_descriptor{
                        .name = "OVERLAY",
                        .value = bool_value(sys.overlay_active),
                        .bit_width = 1U,
                        .format = register_value_format::flags,
                    },
                    chips::register_descriptor{
                        .name = "FRAME",
                        .value = sys.frame_index,
                        .bit_width = 64U,
                        .format = register_value_format::unsigned_integer,
                    },
                    chips::register_descriptor{
                        .name = "DMACON",
                        .value = sys.agnus.dmacon(),
                        .bit_width = 16U,
                        .format = register_value_format::flags,
                    },
                    chips::register_descriptor{
                        .name = "DMACONR",
                        .value = sys.agnus.read_dmaconr(),
                        .bit_width = 16U,
                        .format = register_value_format::flags,
                    },
                    chips::register_descriptor{
                        .name = "BBUSY",
                        .value = bool_value(
                            (sys.agnus.read_dmaconr() & chips::video::agnus::dmacon_bbusy) != 0U),
                        .bit_width = 1U,
                        .format = register_value_format::flags,
                    },
                    chips::register_descriptor{
                        .name = "BLTCYC",
                        .value = sys.blitter_cycles_remaining,
                        .bit_width = 64U,
                        .format = register_value_format::unsigned_integer,
                    },
                    chips::register_descriptor{
                        .name = "COP1LC",
                        .value = sys.agnus.cop1lc(),
                        .bit_width = 24U,
                        .format = register_value_format::unsigned_integer,
                    },
                    chips::register_descriptor{
                        .name = "COP2LC",
                        .value = sys.agnus.cop2lc(),
                        .bit_width = 24U,
                        .format = register_value_format::unsigned_integer,
                    },
                    chips::register_descriptor{
                        .name = "COPPC",
                        .value = sys.agnus.copper_pc(),
                        .bit_width = 24U,
                        .format = register_value_format::unsigned_integer,
                    },
                    chips::register_descriptor{
                        .name = "COPRUN",
                        .value = bool_value(sys.agnus.copper_running()),
                        .bit_width = 1U,
                        .format = register_value_format::flags,
                    },
                    chips::register_descriptor{
                        .name = "COPDLY",
                        .value = sys.agnus.copper_delay(),
                        .bit_width = 8U,
                        .format = register_value_format::unsigned_integer,
                    },
                    chips::register_descriptor{
                        .name = "DSKPTR",
                        .value = sys.disk_pointer,
                        .bit_width = 24U,
                        .format = register_value_format::unsigned_integer,
                    },
                    chips::register_descriptor{
                        .name = "DSKLEN",
                        .value = sys.disk_length,
                        .bit_width = 16U,
                        .format = register_value_format::flags,
                    },
                    chips::register_descriptor{
                        .name = "DSKDMA",
                        .value = sys.disk_dma_bytes_remaining,
                        .bit_width = 32U,
                        .format = register_value_format::unsigned_integer,
                    },
                    chips::register_descriptor{
                        .name = "DFSEL",
                        .value = sys.floppy_selected_mask,
                        .bit_width = 4U,
                        .format = register_value_format::flags,
                    },
                    chips::register_descriptor{
                        .name = "DFACTIVE",
                        .value = sys.floppy_active_drive,
                        .bit_width = 8U,
                        .format = register_value_format::unsigned_integer,
                    },
                    chips::register_descriptor{
                        .name = "DFMOTOR",
                        .value = bool_value(sys.floppy_motor_on),
                        .bit_width = 1U,
                        .format = register_value_format::flags,
                    },
                    chips::register_descriptor{
                        .name = "DFCyl",
                        .value = drive.cylinder_pos,
                        .bit_width = 8U,
                        .format = register_value_format::unsigned_integer,
                    },
                    chips::register_descriptor{
                        .name = "DFSide",
                        .value = sys.floppy_side_pos,
                        .bit_width = 1U,
                        .format = register_value_format::unsigned_integer,
                    },
                };
                return registers_;
            }

          private:
            manifests::amiga::amiga_system* sys_;
            std::array<chips::register_descriptor, 23> registers_{};
        };

        class board_introspection final : public instrumentation::ichip_introspection {
          public:
            explicit board_introspection(manifests::amiga::amiga_system& sys) noexcept
                : registers_(sys) {}

            [[nodiscard]] instrumentation::register_view* registers() override {
                return &registers_;
            }

          private:
            board_registers registers_;
        };

        std::vector<runtime::scheduled_chip> build_schedule(manifests::amiga::amiga_system& sys) {
            return {
                {&sys.agnus, 2U}, // OCS display/color clock from a 68K-cycle master.
                {&sys.cpu, 1U},   {&sys.paula, 2U}, {&sys.cia_a, 10U}, {&sys.cia_b, 10U},
            };
        }

        frontend_sdk::session_capability_info make_session_capabilities() {
            frontend_sdk::session_capability_info session{};
            session.input_ports = {
                {.port_index = 0U,
                 .player_slot = 1U,
                 .format = frontend_sdk::input_device_format::digital_pad,
                 .device_id = "amiga.joystick.port.2",
                 .label = "Joystick Port 2"},
                {.port_index = 1U,
                 .player_slot = 2U,
                 .format = frontend_sdk::input_device_format::digital_pad,
                 .device_id = "amiga.joystick.port.1",
                 .label = "Joystick Port 1"},
                {.port_index = 2U,
                 .player_slot = 1U,
                 .format = frontend_sdk::input_device_format::keyboard,
                 .device_id = "amiga.keyboard",
                 .label = "Keyboard"},
                {.port_index = 3U,
                 .player_slot = 1U,
                 .format = frontend_sdk::input_device_format::mouse,
                 .device_id = "amiga.mouse.port.1",
                 .label = "Mouse Port 1"},
                {.port_index = 4U,
                 .player_slot = 1U,
                 .format = frontend_sdk::input_device_format::analog,
                 .device_id = "amiga.pot.port.1",
                 .label = "Analog POT Port 1"},
                {.port_index = 5U,
                 .player_slot = 2U,
                 .format = frontend_sdk::input_device_format::analog,
                 .device_id = "amiga.pot.port.2",
                 .label = "Analog POT Port 2"},
            };
            session.deterministic_frame_input = true;
            session.save_state_supported = true;
            session.frame_exact_save_state = true;
            session.max_input_delay_frames = 4U;
            return session;
        }

        frontend_sdk::media_image_info make_media(std::string id, std::string label,
                                                  std::uint64_t byte_count, std::string provider_id,
                                                  std::string cache_hint) {
            return frontend_sdk::media_image_info{
                .id = std::move(id),
                .label = std::move(label),
                .residency = frontend_sdk::media_residency::resident,
                .byte_count = byte_count,
                .hash_algorithm = frontend_sdk::media_hash_algorithm::none,
                .provider_id = std::move(provider_id),
                .revision = "loaded",
                .cache_hint = std::move(cache_hint)};
        }

        [[nodiscard]] std::string unsupported_adf_size_detail(std::size_t byte_count) {
            return "expected " + std::to_string(manifests::amiga::amiga_system::floppy_dd_size) +
                   "-byte standard DD ADF; got " + std::to_string(byte_count) + " bytes";
        }

        [[nodiscard]] const char* model_family_id(manifests::amiga::amiga_model model) noexcept {
            using model_t = manifests::amiga::amiga_model;
            switch (model) {
            case model_t::amiga500_plus:
                return "amiga500plus";
            case model_t::amiga600:
                return "amiga600";
            case model_t::amiga2000:
            case model_t::amiga2000_ecs_1m:
                return "amiga2000";
            case model_t::amiga500:
                break;
            }
            return "amiga500";
        }

        [[nodiscard]] const char* model_display_name(manifests::amiga::amiga_model model) noexcept {
            using model_t = manifests::amiga::amiga_model;
            switch (model) {
            case model_t::amiga500_plus:
                return "Amiga 500+";
            case model_t::amiga600:
                return "Amiga 600";
            case model_t::amiga2000:
            case model_t::amiga2000_ecs_1m:
                return "Amiga 2000";
            case model_t::amiga500:
                break;
            }
            return "Amiga 500";
        }

        [[nodiscard]] const char*
        model_configuration_label(manifests::amiga::amiga_model model) noexcept {
            using model_t = manifests::amiga::amiga_model;
            switch (model) {
            case model_t::amiga2000:
                return "OCS base";
            case model_t::amiga2000_ecs_1m:
                return "ECS / 1 MiB upgrade";
            default:
                return "";
            }
        }

        [[nodiscard]] std::string normalize_amiga_model_token(std::string_view token) {
            std::string out;
            out.reserve(token.size());
            for (char c : token) {
                if (c == '_' || std::isspace(static_cast<unsigned char>(c)) != 0) {
                    out.push_back('-');
                } else {
                    out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
                }
            }
            return out;
        }

        [[nodiscard]] std::string_view strip_token_padding(std::string_view token) noexcept {
            while (!token.empty() && token.front() == '-') {
                token.remove_prefix(1U);
            }
            while (!token.empty() && token.back() == '-') {
                token.remove_suffix(1U);
            }
            return token;
        }

        [[nodiscard]] std::optional<std::size_t> parse_memory_size_token(std::string_view token) {
            token = strip_token_padding(token);
            if (token.empty()) {
                return std::nullopt;
            }
            if (token == "0" || token == "off" || token == "none") {
                return 0U;
            }

            std::string value{token};
            if (value.ends_with("ib")) {
                value.pop_back();
                value.pop_back();
            } else if (value.ends_with("b")) {
                value.pop_back();
            }
            if (value.empty()) {
                return std::nullopt;
            }

            std::size_t multiplier = 1U;
            const char suffix = value.back();
            if (suffix == 'k') {
                multiplier = 1024U;
                value.pop_back();
            } else if (suffix == 'm') {
                multiplier = 1024U * 1024U;
                value.pop_back();
            }
            if (value.empty()) {
                return std::nullopt;
            }

            char* end = nullptr;
            const unsigned long long parsed = std::strtoull(value.c_str(), &end, 10);
            if (end == value.c_str() || *end != '\0') {
                return std::nullopt;
            }
            if (multiplier != 0U && parsed > (std::numeric_limits<unsigned long long>::max() /
                                              static_cast<unsigned long long>(multiplier))) {
                return std::nullopt;
            }
            const unsigned long long bytes = parsed * static_cast<unsigned long long>(multiplier);
            if (bytes > manifests::amiga::amiga_system::fast_ram_max_size) {
                return std::nullopt;
            }
            if (bytes != 0U && (bytes % manifests::amiga::amiga_system::fast_ram_size_512k) != 0U) {
                return std::nullopt;
            }
            return static_cast<std::size_t>(bytes);
        }

        [[nodiscard]] std::optional<std::string_view>
        token_suffix(std::string_view token, std::string_view prefix) noexcept {
            if (!token.starts_with(prefix)) {
                return std::nullopt;
            }
            return token.substr(prefix.size());
        }

        bool apply_fast_ram_token(manifests::amiga::amiga_config& config, std::string_view token) {
            if (token == "no-fast-ram" || token == "no-fast" || token == "fast-ram-none") {
                config.fast_ram_size = 0U;
                return true;
            }

            std::optional<std::string_view> size_token = token_suffix(token, "fast-ram=");
            if (!size_token) {
                size_token = token_suffix(token, "fast=");
            }
            if (!size_token) {
                size_token = token_suffix(token, "ram=");
            }
            if (!size_token) {
                size_token = token_suffix(token, "fast-ram-");
            }
            if (!size_token) {
                size_token = token_suffix(token, "fast-");
            }
            if (!size_token) {
                return false;
            }

            if (const auto size = parse_memory_size_token(*size_token)) {
                config.fast_ram_size = *size;
            }
            return true;
        }

        void apply_amiga2000_config_token(manifests::amiga::amiga_config& config,
                                          std::string_view token) {
            using model_t = manifests::amiga::amiga_model;
            token = strip_token_padding(token);
            if (token.empty()) {
                return;
            }
            if (token == "base" || token == "ocs" || token == "ocs-512k" || token == "512k") {
                config.model = model_t::amiga2000;
                return;
            }
            if (token == "ecs" || token == "ecs-1m" || token == "ecs1m" || token == "1m" ||
                token == "ks2" || token == "kickstart2" || token == "kickstart-2" ||
                token == "kickstart-2.0" || token == "2.0") {
                config.model = model_t::amiga2000_ecs_1m;
                return;
            }
            static_cast<void>(apply_fast_ram_token(config, token));
        }

        [[nodiscard]] manifests::amiga::amiga_config
        resolve_amiga_config_override(manifests::amiga::amiga_config base_config,
                                      std::string_view override) {
            using model_t = manifests::amiga::amiga_model;
            if (base_config.model != model_t::amiga2000) {
                return base_config;
            }
            const std::string normalized = normalize_amiga_model_token(override);
            std::size_t token_start = 0U;
            while (token_start <= normalized.size()) {
                const std::size_t token_end = normalized.find_first_of("+,;", token_start);
                const std::size_t count =
                    token_end == std::string::npos ? std::string::npos : token_end - token_start;
                apply_amiga2000_config_token(
                    base_config, std::string_view{normalized}.substr(token_start, count));
                if (token_end == std::string::npos) {
                    break;
                }
                token_start = token_end + 1U;
            }
            return base_config;
        }

        [[nodiscard]] std::string memory_size_label(std::size_t bytes) {
            constexpr std::size_t kib = 1024U;
            constexpr std::size_t mib = 1024U * 1024U;
            if (bytes >= mib && (bytes % mib) == 0U) {
                return std::to_string(bytes / mib) + " MiB";
            }
            if ((bytes % kib) == 0U) {
                return std::to_string(bytes / kib) + " KiB";
            }
            return std::to_string(bytes) + " bytes";
        }

        [[nodiscard]] std::uint8_t
        pack_joystick(const frontend_sdk::controller_state& state) noexcept {
            std::uint8_t mask = 0U;
            if (state.up) {
                mask = static_cast<std::uint8_t>(mask | manifests::amiga::amiga_system::joy_up);
            }
            if (state.down) {
                mask = static_cast<std::uint8_t>(mask | manifests::amiga::amiga_system::joy_down);
            }
            if (state.left) {
                mask = static_cast<std::uint8_t>(mask | manifests::amiga::amiga_system::joy_left);
            }
            if (state.right) {
                mask = static_cast<std::uint8_t>(mask | manifests::amiga::amiga_system::joy_right);
            }
            if (state.a || state.trigger) {
                mask = static_cast<std::uint8_t>(mask | manifests::amiga::amiga_system::joy_fire);
            }
            if (state.b || state.c) {
                mask = static_cast<std::uint8_t>(
                    mask | manifests::amiga::amiga_system::joy_secondary_fire);
            }
            return mask;
        }

        [[nodiscard]] std::uint8_t pot_axis(std::int16_t value) noexcept {
            if (value < 0) {
                return 0xFFU;
            }
            if (value > 0xFF) {
                return 0xFFU;
            }
            return static_cast<std::uint8_t>(value);
        }

        struct physical_key_map final {
            std::uint16_t usage;
            std::uint8_t raw_keycode;
        };

        using keyboard_layout = manifests::amiga::amiga_keyboard_layout;
        using raw_key_bitmap =
            std::array<bool, manifests::amiga::amiga_system::keyboard_raw_key_count>;

        constexpr std::uint32_t amiga_adapter_state_version = 1U;
        constexpr double audio_fraction_scale = 4294967295.0;
        constexpr std::uint16_t hid_caps_lock = 0x39U;

        [[nodiscard]] bool trace_cia_bus() noexcept {
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
            const char* trace_env = std::getenv("MNEMOS_AMIGA500_CIA_TRACE");
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
            return trace_env != nullptr && trace_env[0] != '\0' && trace_env[0] != '0';
        }

        [[nodiscard]] bool trace_ram_reads() noexcept {
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
            const char* trace_env = std::getenv("MNEMOS_AMIGA500_RAM_TRACE_READS");
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
            return trace_env != nullptr && trace_env[0] != '\0' && trace_env[0] != '0';
        }

        struct trace_range final {
            std::uint32_t first{};
            std::uint32_t last{};
        };

        [[nodiscard]] std::uint32_t parse_trace_hex(std::string_view text,
                                                    std::uint32_t fallback) noexcept {
            std::uint32_t value = 0U;
            bool any = false;
            for (char c : text) {
                if (c == 'x' || c == 'X') {
                    value = 0U;
                    any = false;
                    continue;
                }
                std::uint32_t digit = 0U;
                if (c >= '0' && c <= '9') {
                    digit = static_cast<std::uint32_t>(c - '0');
                } else if (c >= 'a' && c <= 'f') {
                    digit = static_cast<std::uint32_t>(10 + c - 'a');
                } else if (c >= 'A' && c <= 'F') {
                    digit = static_cast<std::uint32_t>(10 + c - 'A');
                } else {
                    return fallback;
                }
                value = static_cast<std::uint32_t>((value << 4U) | digit);
                any = true;
            }
            return any ? value : fallback;
        }

        [[nodiscard]] std::vector<trace_range> trace_ram_ranges() {
            std::vector<trace_range> ranges;
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
            const char* env = std::getenv("MNEMOS_AMIGA500_RAM_TRACE");
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
            if (env == nullptr || env[0] == '\0' || (env[0] == '0' && env[1] == '\0')) {
                return ranges;
            }

            std::string_view spec(env);
            while (!spec.empty()) {
                const std::size_t comma = spec.find(',');
                const std::string_view token =
                    comma == std::string_view::npos ? spec : spec.substr(0U, comma);
                const std::size_t colon = token.find(':');
                const std::size_t dash = token.find('-');
                if (colon != std::string_view::npos) {
                    const std::uint32_t start = parse_trace_hex(token.substr(0U, colon), 0U);
                    const std::uint32_t length = parse_trace_hex(token.substr(colon + 1U), 0U);
                    if (length != 0U) {
                        ranges.push_back(trace_range{
                            .first = start & 0x00FFFFFFU,
                            .last = (start + length - 1U) & 0x00FFFFFFU,
                        });
                    }
                } else if (dash != std::string_view::npos) {
                    const std::uint32_t start = parse_trace_hex(token.substr(0U, dash), 0U);
                    const std::uint32_t end = parse_trace_hex(token.substr(dash + 1U), start);
                    ranges.push_back(trace_range{
                        .first = start & 0x00FFFFFFU,
                        .last = end & 0x00FFFFFFU,
                    });
                }
                if (comma == std::string_view::npos) {
                    break;
                }
                spec.remove_prefix(comma + 1U);
            }
            return ranges;
        }

        [[nodiscard]] std::vector<trace_range> trace_cpu_ranges() {
            std::vector<trace_range> ranges;
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
            const char* env = std::getenv("MNEMOS_AMIGA500_CPU_TRACE");
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
            if (env == nullptr || env[0] == '\0' || (env[0] == '0' && env[1] == '\0')) {
                return ranges;
            }
            if (env[0] == '1' && env[1] == '\0') {
                ranges.push_back(trace_range{.first = 0U, .last = 0x00FFFFFFU});
                return ranges;
            }

            std::string_view spec(env);
            while (!spec.empty()) {
                const std::size_t comma = spec.find(',');
                const std::string_view token =
                    comma == std::string_view::npos ? spec : spec.substr(0U, comma);
                const std::size_t colon = token.find(':');
                const std::size_t dash = token.find('-');
                if (colon != std::string_view::npos) {
                    const std::uint32_t start = parse_trace_hex(token.substr(0U, colon), 0U);
                    const std::uint32_t length = parse_trace_hex(token.substr(colon + 1U), 0U);
                    if (length != 0U) {
                        ranges.push_back(trace_range{
                            .first = start & 0x00FFFFFFU,
                            .last = (start + length - 1U) & 0x00FFFFFFU,
                        });
                    }
                } else if (dash != std::string_view::npos) {
                    const std::uint32_t start = parse_trace_hex(token.substr(0U, dash), 0U);
                    const std::uint32_t end = parse_trace_hex(token.substr(dash + 1U), start);
                    ranges.push_back(trace_range{
                        .first = start & 0x00FFFFFFU,
                        .last = end & 0x00FFFFFFU,
                    });
                }
                if (comma == std::string_view::npos) {
                    break;
                }
                spec.remove_prefix(comma + 1U);
            }
            return ranges;
        }

        [[nodiscard]] bool traced_ram_access(std::span<const trace_range> ranges,
                                             std::uint32_t address) noexcept {
            const std::uint32_t a = address & 0x00FFFFFFU;
            for (const trace_range& range : ranges) {
                if (range.first <= range.last) {
                    if (a >= range.first && a <= range.last) {
                        return true;
                    }
                } else if (a >= range.first || a <= range.last) {
                    return true;
                }
            }
            return false;
        }

        [[nodiscard]] std::uint16_t trace_fetch_word(manifests::amiga::amiga_system& sys,
                                                     std::uint32_t address) noexcept {
            const std::uint32_t a = address & 0x00FFFFFEU;
            const auto read_be = [](std::span<const std::uint8_t> bytes,
                                    std::size_t offset) noexcept -> std::uint16_t {
                return static_cast<std::uint16_t>(
                    (static_cast<std::uint16_t>(bytes[offset]) << 8U) | bytes[offset + 1U]);
            };
            if (a < sys.chip_ram.size()) {
                if (sys.overlay_active && a < sys.kickstart_rom.size()) {
                    return read_be(sys.kickstart_rom,
                                   static_cast<std::size_t>(a) % sys.kickstart_rom.size());
                }
                return read_be(sys.chip_ram, static_cast<std::size_t>(a) % sys.chip_ram.size());
            }
            if (a >= manifests::amiga::amiga_system::kickstart_base &&
                a + 1U < manifests::amiga::amiga_system::kickstart_base +
                             manifests::amiga::amiga_system::kickstart_window_size) {
                return read_be(
                    sys.kickstart_rom,
                    static_cast<std::size_t>(a - manifests::amiga::amiga_system::kickstart_base));
            }
            return sys.bus.fetch16_be_opcode(a);
        }

        [[nodiscard]] std::uint8_t traced_cia_register(std::uint32_t address) noexcept {
            return static_cast<std::uint8_t>((address >> 8U) & 0x0FU);
        }

        [[nodiscard]] bool traced_disk_register(std::uint32_t reg, bool write) noexcept {
            switch (reg & 0x00FFFFFEU) {
            case 0x00DFF010U: // ADKCONR.
                return !write;
            case 0x00DFF01AU: // DSKBYTR.
                return !write;
            case 0x00DFF020U: // DSKPTH.
            case 0x00DFF022U: // DSKPTL.
            case 0x00DFF024U: // DSKLEN.
            case 0x00DFF026U: // DSKDAT.
            case 0x00DFF07EU: // DSKSYNC.
            case 0x00DFF09EU: // ADKCON.
                return write;
            default:
                return false;
            }
        }

        [[nodiscard]] bool traced_blitter_register(std::uint32_t reg, bool write) noexcept {
            if (!write) {
                return false;
            }
            switch (reg & 0x00FFFFFEU) {
            case 0x00DFF040U: // BLTCON0.
            case 0x00DFF042U: // BLTCON1.
            case 0x00DFF044U: // BLTAFWM.
            case 0x00DFF046U: // BLTALWM.
            case 0x00DFF048U: // BLTCPTH.
            case 0x00DFF04AU: // BLTCPTL.
            case 0x00DFF04CU: // BLTBPTH.
            case 0x00DFF04EU: // BLTBPTL.
            case 0x00DFF050U: // BLTAPTH.
            case 0x00DFF052U: // BLTAPTL.
            case 0x00DFF054U: // BLTDPTH.
            case 0x00DFF056U: // BLTDPTL.
            case 0x00DFF058U: // BLTSIZE.
            case 0x00DFF060U: // BLTCMOD.
            case 0x00DFF062U: // BLTBMOD.
            case 0x00DFF064U: // BLTAMOD.
            case 0x00DFF066U: // BLTDMOD.
            case 0x00DFF070U: // BLTCDAT.
            case 0x00DFF072U: // BLTBDAT.
            case 0x00DFF074U: // BLTADAT.
                return true;
            default:
                return false;
            }
        }

        [[nodiscard]] bool normalized_custom_register(std::uint32_t address,
                                                      std::uint32_t& reg) noexcept {
            const std::uint32_t a = address & 0x00FFFFFFU;
            if (a >= manifests::amiga::amiga_system::custom_base &&
                a < manifests::amiga::amiga_system::custom_base + 0x2000U) {
                reg = manifests::amiga::amiga_system::custom_base |
                      ((a - manifests::amiga::amiga_system::custom_base) & 0x01FEU);
                return true;
            }
            if (a >= 0x00C00000U && a < 0x00DC0000U) {
                reg = manifests::amiga::amiga_system::custom_base | (a & 0x01FEU);
                return true;
            }
            return false;
        }

        [[nodiscard]] bool traced_amiga_register(std::uint32_t address, bool write) noexcept {
            const std::uint32_t a = address & 0x00FFFFFFU;
            if ((a & 0x00FFF000U) == manifests::amiga::amiga_system::cia_a_base ||
                (a & 0x00FFF000U) == manifests::amiga::amiga_system::cia_b_base) {
                const std::uint8_t cia_reg = traced_cia_register(a);
                const bool cia_timer_or_tod_read = !write && cia_reg >= 0x04U && cia_reg <= 0x0FU;
                const bool cia_a_port_a_read =
                    !write && (a & 0x00FFF000U) == manifests::amiga::amiga_system::cia_a_base &&
                    cia_reg == 0x00U;
                return write || cia_timer_or_tod_read || cia_a_port_a_read;
            }
            std::uint32_t reg = 0U;
            if (!normalized_custom_register(a, reg)) {
                return false;
            }
            if (traced_disk_register(reg, write)) {
                return true;
            }
            if (traced_blitter_register(reg, write)) {
                return true;
            }
            if (reg == 0x00DFF080U || reg == 0x00DFF082U || reg == 0x00DFF084U ||
                reg == 0x00DFF086U || reg == 0x00DFF088U || reg == 0x00DFF08AU ||
                reg == 0x00DFF096U || reg == 0x00DFF09AU || reg == 0x00DFF09CU ||
                reg == 0x00DFF01CU || reg == 0x00DFF01EU || reg == 0x00DFF100U) {
                return true;
            }
            return false;
        }

        [[nodiscard]] bool traced_copper_write_register(std::uint16_t reg) noexcept {
            switch (reg & 0x01FEU) {
            case 0x080U:
            case 0x082U:
            case 0x084U:
            case 0x086U:
            case 0x088U:
            case 0x08AU:
            case 0x096U:
            case 0x09AU:
            case 0x09CU:
            case 0x100U:
                return true;
            default:
                return false;
            }
        }

        constexpr physical_key_map physical_keyboard_map[] = {
            {0x35U, 0x00U}, // `
            {0x1EU, 0x01U}, {0x1FU, 0x02U}, {0x20U, 0x03U}, {0x21U, 0x04U}, {0x22U, 0x05U},
            {0x23U, 0x06U}, {0x24U, 0x07U}, {0x25U, 0x08U}, {0x26U, 0x09U}, {0x27U, 0x0AU},
            {0x2DU, 0x0BU}, {0x2EU, 0x0CU}, {0x31U, 0x0DU}, {0x89U, 0x0EU}, {0x62U, 0x0FU},
            {0x14U, 0x10U}, {0x1AU, 0x11U}, {0x08U, 0x12U}, {0x15U, 0x13U}, {0x17U, 0x14U},
            {0x1CU, 0x15U}, {0x18U, 0x16U}, {0x0CU, 0x17U}, {0x12U, 0x18U}, {0x13U, 0x19U},
            {0x2FU, 0x1AU}, {0x30U, 0x1BU}, {0x59U, 0x1DU}, {0x5AU, 0x1EU}, {0x5BU, 0x1FU},
            {0x04U, 0x20U}, {0x16U, 0x21U}, {0x07U, 0x22U}, {0x09U, 0x23U}, {0x0AU, 0x24U},
            {0x0BU, 0x25U}, {0x0DU, 0x26U}, {0x0EU, 0x27U}, {0x0FU, 0x28U}, {0x33U, 0x29U},
            {0x34U, 0x2AU}, {0x32U, 0x2BU}, {0x5CU, 0x2DU}, {0x5DU, 0x2EU}, {0x5EU, 0x2FU},
            {0x64U, 0x30U}, {0x1DU, 0x31U}, {0x1BU, 0x32U}, {0x06U, 0x33U}, {0x19U, 0x34U},
            {0x05U, 0x35U}, {0x11U, 0x36U}, {0x10U, 0x37U}, {0x36U, 0x38U}, {0x37U, 0x39U},
            {0x38U, 0x3AU}, {0x87U, 0x3BU}, {0x63U, 0x3CU}, {0x5FU, 0x3DU}, {0x60U, 0x3EU},
            {0x61U, 0x3FU}, {0x2CU, 0x40U}, {0x2AU, 0x41U}, {0x2BU, 0x42U}, {0x58U, 0x43U},
            {0x28U, 0x44U}, {0x29U, 0x45U}, {0x4CU, 0x46U}, {0x56U, 0x4AU}, {0x52U, 0x4CU},
            {0x51U, 0x4DU}, {0x4FU, 0x4EU}, {0x50U, 0x4FU}, {0x3AU, 0x50U}, {0x3BU, 0x51U},
            {0x3CU, 0x52U}, {0x3DU, 0x53U}, {0x3EU, 0x54U}, {0x3FU, 0x55U}, {0x40U, 0x56U},
            {0x41U, 0x57U}, {0x42U, 0x58U}, {0x43U, 0x59U}, {0x54U, 0x5CU}, {0x55U, 0x5DU},
            {0x57U, 0x5EU}, {0x75U, 0x5FU}, {0xE1U, 0x60U}, {0xE5U, 0x61U}, {0xE0U, 0x63U},
            {0xE2U, 0x64U}, {0xE6U, 0x65U}, {0xE3U, 0x66U}, {0xE7U, 0x67U},
        };

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
            writer.u16(state.paddle);
            writer.u16(std::bit_cast<std::uint16_t>(state.aim_x));
            writer.u16(std::bit_cast<std::uint16_t>(state.aim_y));
            writer.boolean(state.trigger);
            for (const bool pressed : state.keyboard_usage) {
                writer.boolean(pressed);
            }
        }

        [[nodiscard]] frontend_sdk::controller_state
        load_controller_state(chips::state_reader& reader) noexcept {
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
            state.paddle = reader.u16();
            state.aim_x = std::bit_cast<std::int16_t>(reader.u16());
            state.aim_y = std::bit_cast<std::int16_t>(reader.u16());
            state.trigger = reader.boolean();
            for (bool& pressed : state.keyboard_usage) {
                pressed = reader.boolean();
            }
            return state;
        }

        void save_raw_key_bitmap(chips::state_writer& writer, const raw_key_bitmap& keys) {
            for (const bool pressed : keys) {
                writer.boolean(pressed);
            }
        }

        void load_raw_key_bitmap(chips::state_reader& reader, raw_key_bitmap& keys) noexcept {
            for (bool& pressed : keys) {
                pressed = reader.boolean();
            }
        }

        [[nodiscard]] std::uint64_t encode_audio_fraction(double fraction) noexcept {
            if (!(fraction > 0.0)) {
                return 0U;
            }
            if (fraction >= 1.0) {
                return 0xFFFF'FFFFULL;
            }
            return static_cast<std::uint64_t>(fraction * audio_fraction_scale + 0.5);
        }

        [[nodiscard]] double decode_audio_fraction(std::uint64_t encoded) noexcept {
            const std::uint64_t bounded = std::min<std::uint64_t>(encoded, 0xFFFF'FFFFULL);
            return static_cast<double>(bounded) / audio_fraction_scale;
        }

        [[nodiscard]] char ascii_lower(char ch) noexcept {
            return ch >= 'A' && ch <= 'Z' ? static_cast<char>(ch - 'A' + 'a') : ch;
        }

        [[nodiscard]] char layout_token_char(char ch) noexcept {
            if (ch == '_' || ch == ' ') {
                return '-';
            }
            return ascii_lower(ch);
        }

        [[nodiscard]] bool token_equal(std::string_view lhs, std::string_view rhs) noexcept {
            if (lhs.size() != rhs.size()) {
                return false;
            }
            for (std::size_t i = 0U; i < lhs.size(); ++i) {
                if (layout_token_char(lhs[i]) != layout_token_char(rhs[i])) {
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] keyboard_layout keyboard_layout_from_token(std::string_view token) noexcept {
            if (token_equal(token, "de") || token_equal(token, "de-de") ||
                token_equal(token, "de-at") || token_equal(token, "de-ch") ||
                token_equal(token, "german") || token_equal(token, "qwertz")) {
                return keyboard_layout::german;
            }
            if (token_equal(token, "fr") || token_equal(token, "fr-fr") ||
                token_equal(token, "be") || token_equal(token, "be-fr") ||
                token_equal(token, "fr-be") || token_equal(token, "french") ||
                token_equal(token, "belgian") || token_equal(token, "azerty")) {
                return keyboard_layout::azerty;
            }
            if (token_equal(token, "en-gb") || token_equal(token, "gb") ||
                token_equal(token, "uk") || token_equal(token, "us-intl") ||
                token_equal(token, "international") || token_equal(token, "intl") ||
                token_equal(token, "qwerty-intl") || token_equal(token, "es") ||
                token_equal(token, "es-es") || token_equal(token, "it") ||
                token_equal(token, "it-it") || token_equal(token, "pt") ||
                token_equal(token, "pt-pt") || token_equal(token, "pt-br") ||
                token_equal(token, "br") || token_equal(token, "brazil") ||
                token_equal(token, "brazilian") || token_equal(token, "sv") ||
                token_equal(token, "sv-se") || token_equal(token, "se") ||
                token_equal(token, "fi") || token_equal(token, "fi-fi") ||
                token_equal(token, "dk") || token_equal(token, "da-dk") ||
                token_equal(token, "no") || token_equal(token, "nb-no")) {
                return keyboard_layout::qwerty_international;
            }
            return keyboard_layout::us;
        }

        [[nodiscard]] std::string_view keyboard_layout_label(keyboard_layout layout) noexcept {
            switch (layout) {
            case keyboard_layout::azerty:
                return "AZERTY";
            case keyboard_layout::german:
                return "German";
            case keyboard_layout::qwerty_international:
                return "International QWERTY";
            case keyboard_layout::us:
            default:
                return "US";
            }
        }

        [[nodiscard]] std::uint8_t layout_raw_keycode(keyboard_layout layout, std::uint16_t usage,
                                                      std::uint8_t us_raw_keycode) noexcept {
            if (layout == keyboard_layout::german) {
                if (usage == 0x1CU) {
                    return 0x31U; // HID Y position -> Amiga Z on German QWERTZ.
                }
                if (usage == 0x1DU) {
                    return 0x15U; // HID Z position -> Amiga Y on German QWERTZ.
                }
            }
            if (layout == keyboard_layout::azerty) {
                if (usage == 0x14U) {
                    return 0x20U; // HID Q position -> Amiga A on AZERTY.
                }
                if (usage == 0x04U) {
                    return 0x10U; // HID A position -> Amiga Q on AZERTY.
                }
                if (usage == 0x1AU) {
                    return 0x31U; // HID W position -> Amiga Z on AZERTY.
                }
                if (usage == 0x1DU) {
                    return 0x11U; // HID Z position -> Amiga W on AZERTY.
                }
                if (usage == 0x33U) {
                    return 0x37U; // HID semicolon position -> Amiga M on AZERTY.
                }
                if (usage == 0x10U) {
                    return 0x38U; // HID M position -> Amiga comma on AZERTY.
                }
            }
            return us_raw_keycode;
        }

        [[nodiscard]] bool raw_key_is_modifier(std::uint8_t raw_keycode) noexcept {
            const std::uint8_t key = static_cast<std::uint8_t>(raw_keycode & 0x7FU);
            return key >= 0x60U && key <= 0x67U;
        }

        [[nodiscard]] bool raw_key_has_physical_matrix_position(std::uint8_t raw_keycode,
                                                                keyboard_layout layout) noexcept {
            const std::uint8_t key = static_cast<std::uint8_t>(raw_keycode & 0x7FU);
            if (raw_key_is_modifier(key)) {
                return false;
            }
            for (const auto& mapped : physical_keyboard_map) {
                if (layout_raw_keycode(layout, mapped.usage, mapped.raw_keycode) == key) {
                    return true;
                }
            }
            return false;
        }

        [[nodiscard]] std::uint8_t matrix_cross_key(std::uint8_t row_key,
                                                    std::uint8_t column_key) noexcept {
            return static_cast<std::uint8_t>((row_key & 0xF0U) | (column_key & 0x0FU));
        }

        [[nodiscard]] bool matrix_key_press_would_jam(const raw_key_bitmap& reported,
                                                      std::uint8_t raw_keycode,
                                                      keyboard_layout layout) noexcept {
            const std::uint8_t key = static_cast<std::uint8_t>(raw_keycode & 0x7FU);
            if (!raw_key_has_physical_matrix_position(key, layout)) {
                return false;
            }

            for (std::uint8_t anchor = 0U; anchor < reported.size(); ++anchor) {
                if (!reported[anchor] || !raw_key_has_physical_matrix_position(anchor, layout)) {
                    continue;
                }

                const bool shares_matrix_row = (anchor & 0xF0U) == (key & 0xF0U);
                const bool shares_matrix_column = (anchor & 0x0FU) == (key & 0x0FU);
                if (shares_matrix_row == shares_matrix_column) {
                    continue;
                }

                for (std::uint8_t peer = 0U; peer < reported.size(); ++peer) {
                    if (peer == anchor || !reported[peer] ||
                        !raw_key_has_physical_matrix_position(peer, layout)) {
                        continue;
                    }

                    const std::uint8_t ghost = shares_matrix_column ? matrix_cross_key(key, peer)
                                                                    : matrix_cross_key(peer, key);
                    if (reported[ghost] || !raw_key_has_physical_matrix_position(ghost, layout)) {
                        continue;
                    }

                    const bool peer_completes_row = shares_matrix_column &&
                                                    ((peer & 0xF0U) == (anchor & 0xF0U)) &&
                                                    ((peer & 0x0FU) != (key & 0x0FU));
                    const bool peer_completes_column = shares_matrix_row &&
                                                       ((peer & 0x0FU) == (anchor & 0x0FU)) &&
                                                       ((peer & 0xF0U) != (key & 0xF0U));
                    if (peer_completes_row || peer_completes_column) {
                        return true;
                    }
                }
            }
            return false;
        }

        template <typename Visitor>
        void visit_adapter_raw_keys(keyboard_layout layout, Visitor&& visit) noexcept {
            raw_key_bitmap emitted{};
            const auto visit_once = [&](std::uint8_t raw_keycode) noexcept {
                const std::uint8_t key = static_cast<std::uint8_t>(raw_keycode & 0x7FU);
                if (emitted[key]) {
                    return;
                }
                emitted[key] = true;
                visit(key);
            };

            visit_once(0x40U); // Space
            visit_once(0x44U); // Return
            visit_once(0x4CU); // Cursor up
            visit_once(0x4DU); // Cursor down
            visit_once(0x4EU); // Cursor right
            visit_once(0x4FU); // Cursor left
            visit_once(0x60U); // Left Shift
            visit_once(0x45U); // Escape
            visit_once(0x64U); // Left Alt
            visit_once(0x65U); // Right Alt
            visit_once(0x63U); // Control
            visit_once(0x5FU); // Help
            for (const auto& key : physical_keyboard_map) {
                visit_once(layout_raw_keycode(layout, key.usage, key.raw_keycode));
            }
        }

        void mark_raw_key(raw_key_bitmap& keys, std::uint8_t raw_keycode) noexcept {
            keys[raw_keycode & 0x7FU] = true;
        }

        void mark_button_key(raw_key_bitmap& keys, bool pressed,
                             std::uint8_t raw_keycode) noexcept {
            if (pressed) {
                mark_raw_key(keys, raw_keycode);
            }
        }

        void mark_keyboard_usage(raw_key_bitmap& keys, const frontend_sdk::controller_state& state,
                                 std::uint16_t usage, std::uint8_t raw_keycode) noexcept {
            if (state.key_down(usage)) {
                mark_raw_key(keys, raw_keycode);
            }
        }

        void collect_keyboard_keys(raw_key_bitmap& keys,
                                   const frontend_sdk::controller_state& state, bool keyboard_port,
                                   keyboard_layout layout) noexcept {
            mark_button_key(keys, state.select, 0x40U); // Space
            mark_button_key(keys, state.start, 0x44U);  // Return
            mark_button_key(keys, state.up, 0x4CU);     // Cursor up
            mark_button_key(keys, state.down, 0x4DU);   // Cursor down
            mark_button_key(keys, state.right, 0x4EU);  // Cursor right
            mark_button_key(keys, state.left, 0x4FU);   // Cursor left
            mark_button_key(keys, state.mode, 0x60U);   // Left Shift
            if (!keyboard_port) {
                return;
            }
            mark_button_key(keys, state.x, 0x45U); // Escape
            mark_button_key(keys, state.a, 0x64U); // Left Alt
            mark_button_key(keys, state.b, 0x65U); // Right Alt
            mark_button_key(keys, state.c, 0x63U); // Control
            mark_button_key(keys, state.z, 0x5FU); // Help
            for (const auto& key : physical_keyboard_map) {
                mark_keyboard_usage(keys, state, key.usage,
                                    layout_raw_keycode(layout, key.usage, key.raw_keycode));
            }
        }

        void
        collect_adapter_keyboard_keys(raw_key_bitmap& keys,
                                      const std::array<frontend_sdk::controller_state, 6>& ports,
                                      keyboard_layout layout) noexcept {
            collect_keyboard_keys(keys, ports[0], false, layout);
            collect_keyboard_keys(keys, ports[2], true, layout);
        }

        void route_keyboard_edges(manifests::amiga::amiga_system& sys,
                                  const std::array<frontend_sdk::controller_state, 6>& old_ports,
                                  const std::array<frontend_sdk::controller_state, 6>& new_ports,
                                  keyboard_layout layout, raw_key_bitmap& reported_keys) noexcept {
            raw_key_bitmap physical_keys{};
            collect_adapter_keyboard_keys(physical_keys, new_ports, layout);

            raw_key_bitmap next_reported = reported_keys;
            visit_adapter_raw_keys(layout, [&](std::uint8_t key) noexcept {
                if (reported_keys[key] && !physical_keys[key]) {
                    next_reported[key] = false;
                }
            });
            visit_adapter_raw_keys(layout, [&](std::uint8_t key) noexcept {
                if (!reported_keys[key] && physical_keys[key] &&
                    !matrix_key_press_would_jam(next_reported, key, layout)) {
                    next_reported[key] = true;
                }
            });
            visit_adapter_raw_keys(layout, [&](std::uint8_t key) noexcept {
                if (reported_keys[key] && !next_reported[key] &&
                    sys.enqueue_keyboard_key(key, false)) {
                    reported_keys[key] = false;
                }
            });
            visit_adapter_raw_keys(layout, [&](std::uint8_t key) noexcept {
                if (!reported_keys[key] && next_reported[key] &&
                    sys.enqueue_keyboard_key(key, true)) {
                    reported_keys[key] = true;
                }
            });

            const bool old_caps = old_ports[2].y || old_ports[2].key_down(hid_caps_lock);
            const bool new_caps = new_ports[2].y || new_ports[2].key_down(hid_caps_lock);
            if (!old_caps && new_caps) {
                (void)sys.press_caps_lock();
            }
        }

        frontend_sdk::media_capability_info
        make_media_capabilities(std::string_view display_name, std::uint64_t kickstart_byte_count,
                                const std::vector<std::vector<std::uint8_t>>& disks,
                                manifests::amiga::amiga_model model) {
            frontend_sdk::media_capability_info media{};
            const std::string provider_prefix = model_family_id(model);
            media.media.push_back(make_media("kickstart", "Kickstart", kickstart_byte_count,
                                             provider_prefix + ".kickstart", "resident"));
            for (std::size_t i = 0U; i < disks.size(); ++i) {
                const std::string label =
                    disks.size() == 1U
                        ? (display_name.empty() ? std::string{"Disk"} : std::string{display_name})
                        : ((display_name.empty() ? std::string{"Disk"}
                                                 : std::string{display_name}) +
                           " disk " + std::to_string(i + 1U));
                const std::string provider_id =
                    i < manifests::amiga::amiga_system::floppy_drive_count
                        ? provider_prefix + ".df" + std::to_string(i)
                        : provider_prefix + ".df0";
                auto disk =
                    make_media("disk." + std::to_string(i), label, disks[i].size(), provider_id,
                               disks.size() == 1U ? "resident" : "resident_removable");
                if (disks[i].size() != manifests::amiga::amiga_system::floppy_dd_size) {
                    disk.revision_supported = false;
                    disk.validation_issues.push_back(frontend_sdk::media_validation_issue{
                        .code = "media.amiga.adf.unsupported_size",
                        .detail = unsupported_adf_size_detail(disks[i].size())});
                }
                media.media.push_back(std::move(disk));
            }
            return media;
        }

        [[nodiscard]] std::uint32_t read_be32(std::span<const std::uint8_t> bytes,
                                              std::size_t offset) noexcept {
            if (offset + 4U > bytes.size()) {
                return 0U;
            }
            return (static_cast<std::uint32_t>(bytes[offset + 0U]) << 24U) |
                   (static_cast<std::uint32_t>(bytes[offset + 1U]) << 16U) |
                   (static_cast<std::uint32_t>(bytes[offset + 2U]) << 8U) |
                   static_cast<std::uint32_t>(bytes[offset + 3U]);
        }

        [[nodiscard]] bool contains_ascii(std::span<const std::uint8_t> bytes,
                                          std::string_view needle) noexcept {
            const auto* const begin = reinterpret_cast<const char*>(bytes.data());
            const auto* const end = begin + bytes.size();
            return std::search(begin, end, needle.begin(), needle.end()) != end;
        }

        [[nodiscard]] bool
        looks_like_resident_kickstart(std::span<const std::uint8_t> kickstart_rom) noexcept {
            constexpr std::uint32_t amiga_bus_segment_mask = 0x00FF0000U;
            constexpr std::uint32_t kickstart_512k_segment = 0x00F80000U;
            constexpr std::uint32_t kickstart_256k_segment = 0x00FC0000U;
            const std::uint32_t reset_pc = read_be32(kickstart_rom, 4U);
            const bool reset_vector_in_kickstart =
                (reset_pc & amiga_bus_segment_mask) == kickstart_512k_segment ||
                (reset_pc & amiga_bus_segment_mask) == kickstart_256k_segment;
            const std::size_t header_size = std::min<std::size_t>(kickstart_rom.size(), 4096U);
            const auto header = kickstart_rom.subspan(0U, header_size);
            return reset_vector_in_kickstart && contains_ascii(header, "exec.library") &&
                   contains_ascii(header, "AMIGA ROM");
        }

        void seed_keyboard_powerup_stream(manifests::amiga::amiga_system& sys,
                                          std::span<const std::uint8_t> kickstart_rom) noexcept {
            if (!looks_like_resident_kickstart(kickstart_rom)) {
                return;
            }
            (void)sys.enqueue_keyboard_control_code(
                manifests::amiga::amiga_system::keyboard_powerup_stream_start_code);
            (void)sys.enqueue_keyboard_control_code(
                manifests::amiga::amiga_system::keyboard_powerup_stream_end_code);
        }
    } // namespace

    class amiga_board_debug_chip final : public chips::iperipheral {
      public:
        explicit amiga_board_debug_chip(manifests::amiga::amiga_system& sys) noexcept
            : sys_(&sys), introspection_(sys) {}

        [[nodiscard]] chips::chip_metadata metadata() const noexcept override {
            return {
                .manufacturer = "Mnemos",
                .part_number = "amiga_board",
                .family = "amiga",
                .klass = chips::chip_class::peripheral,
                .revision = 1U,
            };
        }

        void tick(std::uint64_t) override {}
        void reset(chips::reset_kind) override {}
        void save_state(chips::state_writer&) const override {}
        void load_state(chips::state_reader&) override {}
        [[nodiscard]] instrumentation::ichip_introspection& introspection() noexcept override {
            return introspection_;
        }

      private:
        manifests::amiga::amiga_system* sys_;
        board_introspection introspection_;
    };

    amiga_adapter::amiga_adapter(std::vector<std::uint8_t> kickstart_rom,
                                 const manifests::amiga::amiga_config& config,
                                 std::string display_name,
                                 frontend_sdk::scheduler_factory* scheduler_factory)
        : amiga_adapter(std::move(kickstart_rom), config, std::move(display_name), {},
                        scheduler_factory) {}

    amiga_adapter::amiga_adapter(std::vector<std::uint8_t> kickstart_rom,
                                 const manifests::amiga::amiga_config& config,
                                 std::string display_name,
                                 std::vector<std::vector<std::uint8_t>> disks,
                                 frontend_sdk::scheduler_factory* scheduler_factory)
        : session_(make_session_capabilities()),
          media_(make_media_capabilities(display_name, kickstart_rom.size(), disks, config.model)),
          sys_(manifests::amiga::assemble_amiga(std::move(kickstart_rom), config)),
          chip_ram_view_("chip_ram", sys_->chip_ram), fast_ram_view_("fast_ram", sys_->fast_ram),
          scheduler_(
              frontend_sdk::make_scheduler(scheduler_factory, build_schedule(*sys_), &sys_->agnus)),
          region_(config.video_region), keyboard_layout_(config.keyboard_layout),
          model_(config.model),
          target_fps_(mnemos::target_fps[static_cast<std::size_t>(config.video_region)]),
          disks_(std::move(disks)) {
        board_debug_chip_ = std::make_unique<amiga_board_debug_chip>(*sys_);
        const bool trace_enabled = trace_cia_bus();
        const bool ram_trace_reads = trace_ram_reads();
        const std::vector<trace_range> ram_trace_ranges = trace_ram_ranges();
        const std::vector<trace_range> cpu_trace_ranges = trace_cpu_ranges();
        if (!cpu_trace_ranges.empty()) {
            sys_->cpu.diagnostics().set_trace_callback([this, cpu_trace_ranges](std::uint32_t pc) {
                if (!traced_ram_access(cpu_trace_ranges, pc)) {
                    return;
                }
                const auto regs = sys_->cpu.cpu_registers();
                std::fprintf(stderr,
                             "[amiga-cpu] pc=%06X op=%04X beam=%03u:%03u frame=%llu "
                             "sr=%04X d0=%08X d1=%08X d2=%08X d3=%08X "
                             "a0=%08X a1=%08X a2=%08X a3=%08X "
                             "a4=%08X a5=%08X a6=%08X a7=%08X\n",
                             pc & 0x00FFFFFFU, trace_fetch_word(*sys_, pc), sys_->agnus.beam_line(),
                             sys_->agnus.beam_clock(),
                             static_cast<unsigned long long>(sys_->frame_index), regs.sr, regs.d[0],
                             regs.d[1], regs.d[2], regs.d[3], regs.a[0], regs.a[1], regs.a[2],
                             regs.a[3], regs.a[4], regs.a[5], regs.a[6], regs.a[7]);
            });
        }
        if (trace_enabled) {
            sys_->cpu.set_reset_callback([this] {
                std::fprintf(stderr,
                             "[amiga-reset] before pc=%06X beam=%03u:%03u frame=%llu "
                             "overlay=%u "
                             "dmacon=%04X dmaconr=%04X bltcyc=%llu "
                             "cop1=%06X cop2=%06X coppc=%06X "
                             "intena=%04X intreq=%04X irq=%u\n",
                             sys_->cpu.current_instruction_addr(), sys_->agnus.beam_line(),
                             sys_->agnus.beam_clock(),
                             static_cast<unsigned long long>(sys_->frame_index),
                             sys_->overlay_active ? 1U : 0U, sys_->agnus.dmacon(),
                             sys_->agnus.read_dmaconr(),
                             static_cast<unsigned long long>(sys_->blitter_cycles_remaining),
                             sys_->agnus.cop1lc(), sys_->agnus.cop2lc(), sys_->agnus.copper_pc(),
                             sys_->intena, sys_->visible_intreq(), board_irq_level(*sys_));
                sys_->reset_board_from_cpu();
                std::fprintf(stderr,
                             "[amiga-reset] after  pc=%06X beam=%03u:%03u frame=%llu "
                             "overlay=%u "
                             "dmacon=%04X dmaconr=%04X bltcyc=%llu "
                             "cop1=%06X cop2=%06X coppc=%06X "
                             "intena=%04X intreq=%04X irq=%u\n",
                             sys_->cpu.current_instruction_addr(), sys_->agnus.beam_line(),
                             sys_->agnus.beam_clock(),
                             static_cast<unsigned long long>(sys_->frame_index),
                             sys_->overlay_active ? 1U : 0U, sys_->agnus.dmacon(),
                             sys_->agnus.read_dmaconr(),
                             static_cast<unsigned long long>(sys_->blitter_cycles_remaining),
                             sys_->agnus.cop1lc(), sys_->agnus.cop2lc(), sys_->agnus.copper_pc(),
                             sys_->intena, sys_->visible_intreq(), board_irq_level(*sys_));
            });
        }
        if (trace_enabled || !ram_trace_ranges.empty()) {
            sys_->bus.set_access_observer([this, trace_enabled, ram_trace_ranges, ram_trace_reads](
                                              const mnemos::topology::access_event& ev) {
                if ((ev.write || ram_trace_reads) &&
                    traced_ram_access(ram_trace_ranges, ev.address)) {
                    const auto regs = sys_->cpu.cpu_registers();
                    std::fprintf(stderr,
                                 "[amiga-ram] pc=%06X beam=%03u:%03u %c %06X %02X "
                                 "d0=%08X d1=%08X a0=%08X a1=%08X "
                                 "a3=%08X a5=%08X a6=%08X\n",
                                 sys_->cpu.current_instruction_addr(), sys_->agnus.beam_line(),
                                 sys_->agnus.beam_clock(), ev.write ? 'W' : 'R',
                                 ev.address & 0x00FFFFFFU, ev.value, regs.d[0], regs.d[1],
                                 regs.a[0], regs.a[1], regs.a[3], regs.a[5], regs.a[6]);
                }
                if (!trace_enabled || !traced_amiga_register(ev.address, ev.write)) {
                    return;
                }
                std::uint32_t reg = 0U;
                const bool custom_reg = normalized_custom_register(ev.address, reg);
                const std::uint32_t page = ev.address & 0x00FFF000U;
                const std::uint8_t cia_reg = traced_cia_register(ev.address);
                if (!ev.write && page == manifests::amiga::amiga_system::cia_a_base &&
                    cia_reg == 0x00U) {
                    const bool drive_connected =
                        sys_->floppy_active_drive < sys_->floppy_drives.size() &&
                        sys_->floppy_drives[sys_->floppy_active_drive].connected;
                    const std::uint8_t pra_pins = sys_->cia_a_port_a_inputs();
                    std::fprintf(
                        stderr,
                        "[amiga-ciaa] pc=%06X beam=%03u:%03u R %06X %02X "
                        "pra_pins=%02X drive=%02X conn=%u sel=%u motor=%u cyl=%u side=%u "
                        "pin_chng=%u pin_rdy=%u pin_tk0=%u pin_wpro=%u\n",
                        sys_->cpu.current_instruction_addr(), sys_->agnus.beam_line(),
                        sys_->agnus.beam_clock(), ev.address, ev.value, pra_pins,
                        sys_->floppy_active_drive, drive_connected ? 1U : 0U,
                        sys_->floppy_selected ? 1U : 0U, sys_->floppy_motor_on ? 1U : 0U,
                        sys_->floppy_cylinder(), sys_->floppy_side(),
                        (pra_pins & 0x04U) == 0U ? 1U : 0U, (pra_pins & 0x20U) == 0U ? 1U : 0U,
                        (pra_pins & 0x10U) == 0U ? 1U : 0U, (pra_pins & 0x08U) == 0U ? 1U : 0U);
                    return;
                }
                if (ev.write && page == manifests::amiga::amiga_system::cia_b_base &&
                    (cia_reg == 0x01U || cia_reg == 0x03U)) {
                    std::fprintf(stderr,
                                 "[amiga-ciab] pc=%06X beam=%03u:%03u W %06X %02X "
                                 "prb_pins=%02X drive=%02X mask=%X sel=%u motor=%u step=%u "
                                 "dir_in=%u side=%u\n",
                                 sys_->cpu.current_instruction_addr(), sys_->agnus.beam_line(),
                                 sys_->agnus.beam_clock(), ev.address, ev.value,
                                 sys_->cia_b.port_b_pins(), sys_->floppy_active_drive,
                                 sys_->floppy_selected_mask, sys_->floppy_selected ? 1U : 0U,
                                 sys_->floppy_motor_on ? 1U : 0U, sys_->floppy_step_line ? 1U : 0U,
                                 sys_->floppy_direction_inward ? 1U : 0U, sys_->floppy_side());
                    return;
                }
                if (custom_reg && traced_disk_register(reg, ev.write)) {
                    const auto regs = sys_->cpu.cpu_registers();
                    const bool drive_connected =
                        sys_->floppy_active_drive < sys_->floppy_drives.size() &&
                        sys_->floppy_drives[sys_->floppy_active_drive].connected;
                    std::fprintf(stderr,
                                 "[amiga-disk] pc=%06X beam=%03u:%03u %c %06X %02X "
                                 "dskptr=%06X dsklen=%04X dskdma=%u adkcon=%04X "
                                 "dskdat=%04X shift=%04X sync=%04X byte=%u syncmatch=%u "
                                 "wordsync=%u drive=%02X conn=%u sel=%u motor=%u dmacon=%04X "
                                 "intena=%04X intreq=%04X irq=%u d0=%08X a0=%08X a1=%08X\n",
                                 sys_->cpu.current_instruction_addr(), sys_->agnus.beam_line(),
                                 sys_->agnus.beam_clock(), ev.write ? 'W' : 'R', ev.address,
                                 ev.value, sys_->disk_pointer, sys_->disk_length,
                                 sys_->disk_dma_bytes_remaining, sys_->disk_adkcon, sys_->disk_data,
                                 sys_->disk_shift, sys_->disk_sync, sys_->disk_byte_valid ? 1U : 0U,
                                 sys_->disk_sync_match ? 1U : 0U,
                                 sys_->disk_wordsync_waiting ? 1U : 0U, sys_->floppy_active_drive,
                                 drive_connected ? 1U : 0U, sys_->floppy_selected ? 1U : 0U,
                                 sys_->floppy_motor_on ? 1U : 0U, sys_->agnus.dmacon(),
                                 sys_->intena, sys_->visible_intreq(), board_irq_level(*sys_),
                                 regs.d[0], regs.a[0], regs.a[1]);
                    return;
                }
                if (custom_reg && traced_blitter_register(reg, ev.write)) {
                    const auto regs = sys_->cpu.cpu_registers();
                    std::fprintf(stderr,
                                 "[amiga-blit] pc=%06X beam=%03u:%03u W %06X %02X "
                                 "bltcon0=%04X bltcon1=%04X afwm=%04X alwm=%04X "
                                 "a=%06X b=%06X c=%06X d=%06X "
                                 "am=%04X bm=%04X cm=%04X dm=%04X "
                                 "adat=%04X bdat=%04X cdat=%04X ddat=%04X "
                                 "bltcyc=%llu dmacon=%04X dmaconr=%04X "
                                 "intreq=%04X d0=%08X d1=%08X a0=%08X a1=%08X a6=%08X\n",
                                 sys_->cpu.current_instruction_addr(), sys_->agnus.beam_line(),
                                 sys_->agnus.beam_clock(), ev.address, ev.value, sys_->bltcon0,
                                 sys_->bltcon1, sys_->bltafwm, sys_->bltalwm,
                                 sys_->blitter_pointer[0], sys_->blitter_pointer[1],
                                 sys_->blitter_pointer[2], sys_->blitter_pointer[3],
                                 static_cast<std::uint16_t>(sys_->blitter_modulo[0]),
                                 static_cast<std::uint16_t>(sys_->blitter_modulo[1]),
                                 static_cast<std::uint16_t>(sys_->blitter_modulo[2]),
                                 static_cast<std::uint16_t>(sys_->blitter_modulo[3]),
                                 sys_->blitter_data[0], sys_->blitter_data[1],
                                 sys_->blitter_data[2], sys_->blitter_data[3],
                                 static_cast<unsigned long long>(sys_->blitter_cycles_remaining),
                                 sys_->agnus.dmacon(), sys_->agnus.read_dmaconr(),
                                 sys_->visible_intreq(), regs.d[0], regs.d[1], regs.a[0],
                                 regs.a[1], regs.a[6]);
                    return;
                }
                if (custom_reg && reg == 0x00DFF096U) {
                    const auto regs = sys_->cpu.cpu_registers();
                    const bool audio_dma = sys_->agnus.dma_audio(0) || sys_->agnus.dma_audio(1) ||
                                           sys_->agnus.dma_audio(2) || sys_->agnus.dma_audio(3);
                    std::fprintf(stderr,
                                 "[amiga-dma] pc=%06X beam=%03u:%03u %c %06X %02X "
                                 "dmacon=%04X dmaconr=%04X disk=%u copper=%u blitter=%u "
                                 "bpl=%u sprite=%u audio=%u intena=%04X intreq=%04X "
                                 "d0=%08X a0=%08X a1=%08X a6=%08X\n",
                                 sys_->cpu.current_instruction_addr(), sys_->agnus.beam_line(),
                                 sys_->agnus.beam_clock(), ev.write ? 'W' : 'R', ev.address,
                                 ev.value, sys_->agnus.dmacon(), sys_->agnus.read_dmaconr(),
                                 sys_->agnus.dma_disk() ? 1U : 0U,
                                 sys_->agnus.dma_copper() ? 1U : 0U,
                                 sys_->agnus.dma_blitter() ? 1U : 0U,
                                 sys_->agnus.dma_bitplane() ? 1U : 0U,
                                 sys_->agnus.dma_sprite() ? 1U : 0U, audio_dma ? 1U : 0U,
                                 sys_->intena, sys_->visible_intreq(), regs.d[0], regs.a[0],
                                 regs.a[1], regs.a[6]);
                    return;
                }
                if (custom_reg && (reg == 0x00DFF09AU || reg == 0x00DFF09CU ||
                                   reg == 0x00DFF01CU || reg == 0x00DFF01EU)) {
                    std::fprintf(stderr,
                                 "[amiga-cia] pc=%06X beam=%03u:%03u %c %06X %02X "
                                 "intena=%04X "
                                 "intreq=%04X irq=%u\n",
                                 sys_->cpu.current_instruction_addr(), sys_->agnus.beam_line(),
                                 sys_->agnus.beam_clock(), ev.write ? 'W' : 'R', ev.address,
                                 ev.value, sys_->intena, sys_->visible_intreq(),
                                 board_irq_level(*sys_));
                    return;
                }
                if (custom_reg && reg >= 0x00DFF080U && reg <= 0x00DFF08AU) {
                    const auto regs = sys_->cpu.cpu_registers();
                    std::fprintf(stderr,
                                 "[amiga-cia] pc=%06X beam=%03u:%03u %c %06X %02X "
                                 "cop1=%06X "
                                 "cop2=%06X coppc=%06X dmacon=%04X dmaconr=%04X "
                                 "bltcyc=%llu "
                                 "d0=%08X a0=%08X a1=%08X a6=%08X\n",
                                 sys_->cpu.current_instruction_addr(), sys_->agnus.beam_line(),
                                 sys_->agnus.beam_clock(), ev.write ? 'W' : 'R', ev.address,
                                 ev.value, sys_->agnus.cop1lc(), sys_->agnus.cop2lc(),
                                 sys_->agnus.copper_pc(), sys_->agnus.dmacon(),
                                 sys_->agnus.read_dmaconr(),
                                 static_cast<unsigned long long>(sys_->blitter_cycles_remaining),
                                 regs.d[0], regs.a[0], regs.a[1], regs.a[6]);
                    return;
                }
                std::fprintf(stderr, "[amiga-cia] pc=%06X beam=%03u:%03u %c %06X %02X\n",
                             sys_->cpu.current_instruction_addr(), sys_->agnus.beam_line(),
                             sys_->agnus.beam_clock(), ev.write ? 'W' : 'R', ev.address, ev.value);
            });
        }
        sys_->agnus.set_custom_write_callback(
            [this, trace_enabled](std::uint16_t reg, std::uint16_t value) {
                const std::uint16_t normalized = static_cast<std::uint16_t>(reg & 0x01FEU);
                const std::uint32_t before_cop1 = sys_->agnus.cop1lc();
                const std::uint32_t before_cop2 = sys_->agnus.cop2lc();
                const std::uint32_t before_coppc = sys_->agnus.copper_pc();
                const std::uint16_t before_dmacon = sys_->agnus.dmacon();
                const std::uint16_t before_dmaconr = sys_->agnus.read_dmaconr();
                const std::uint64_t before_bltcyc = sys_->blitter_cycles_remaining;
                sys_->write_custom_word(reg, value);
                if (!trace_enabled || !traced_copper_write_register(normalized)) {
                    return;
                }
                std::fprintf(stderr,
                             "[amiga-copper] beam=%03u:%03u reg=%03X value=%04X "
                             "pc=%06X->%06X cop1=%06X->%06X cop2=%06X->%06X "
                             "dmacon=%04X->%04X dmaconr=%04X->%04X bltcyc=%llu->%llu "
                             "intena=%04X intreq=%04X irq=%u\n",
                             sys_->agnus.beam_line(), sys_->agnus.beam_clock(), normalized, value,
                             before_coppc, sys_->agnus.copper_pc(), before_cop1,
                             sys_->agnus.cop1lc(), before_cop2, sys_->agnus.cop2lc(), before_dmacon,
                             sys_->agnus.dmacon(), before_dmaconr, sys_->agnus.read_dmaconr(),
                             static_cast<unsigned long long>(before_bltcyc),
                             static_cast<unsigned long long>(sys_->blitter_cycles_remaining),
                             sys_->intena, sys_->visible_intreq(), board_irq_level(*sys_));
            });
        seed_keyboard_powerup_stream(*sys_, sys_->kickstart_rom);
        sys_->paula.enable_audio_capture(true);
        const std::size_t mounted_drives =
            std::min(disks_.size(), manifests::amiga::amiga_system::floppy_drive_count);
        for (std::size_t drive = 0U; drive < mounted_drives; ++drive) {
            if (sys_->mount_floppy(drive, disks_[drive])) {
                sys_->set_floppy_change_latch(drive, false);
            }
        }
        chip_view_[0] = &sys_->agnus;
        chip_view_[1] = &sys_->cpu;
        chip_view_[2] = &sys_->paula;
        chip_view_[3] = &sys_->denise;
        chip_view_[4] = &sys_->cia_a;
        chip_view_[5] = &sys_->cia_b;
        chip_view_[6] = board_debug_chip_.get();
        system_mem_view_[0] = &chip_ram_view_;
        system_mem_view_count_ = 1U;
        if (!sys_->fast_ram.empty()) {
            system_mem_view_[system_mem_view_count_] = &fast_ram_view_;
            ++system_mem_view_count_;
        }

        spec_.push_back({.label = "System", .value = model_display_name(model_)});
        spec_.push_back({.label = "Chip RAM", .value = memory_size_label(sys_->chip_ram.size())});
        if (!sys_->fast_ram.empty()) {
            spec_.push_back(
                {.label = "Fast RAM", .value = memory_size_label(sys_->fast_ram.size())});
        }
        if (const std::string_view config_label = model_configuration_label(model_);
            !config_label.empty()) {
            spec_.push_back({.label = "Configuration", .value = std::string{config_label}});
        }
        spec_.push_back(
            {.label = "Region",
             .value = config.video_region == mnemos::video_region::pal ? "PAL" : "NTSC"});
        spec_.push_back(
            {.label = "Keyboard", .value = std::string{keyboard_layout_label(keyboard_layout_)}});
        if (!display_name.empty()) {
            spec_.push_back({.label = disks_.empty() ? "BIOS" : "Disk", .value = display_name});
        }
        if (disks_.size() > 1U) {
            spec_.push_back({.label = "Disks", .value = std::to_string(disks_.size())});
        }
    }

    frontend_sdk::video_region amiga_adapter::region() const noexcept {
        return {mnemos::fps_x1000[static_cast<std::size_t>(region_)]};
    }

    void amiga_adapter::step_one_frame() {
        scheduler_.run_frame();
        sys_->service_keyboard_queue();
    }

    void amiga_adapter::apply_input(int port,
                                    const frontend_sdk::controller_state& state) noexcept {
        if (port < 0 || port >= static_cast<int>(ports_.size())) {
            return;
        }
        const auto old_ports = ports_;
        ports_[static_cast<std::size_t>(port)] = state;

        if (port == 3) {
            std::int16_t delta_x = 0;
            std::int16_t delta_y = 0;
            if (state.aim_x < 0 || state.aim_y < 0) {
                mouse_position_valid_ = false;
            } else {
                if (mouse_position_valid_) {
                    delta_x = static_cast<std::int16_t>(static_cast<int>(state.aim_x) -
                                                        static_cast<int>(mouse_x_));
                    delta_y = static_cast<std::int16_t>(static_cast<int>(state.aim_y) -
                                                        static_cast<int>(mouse_y_));
                }
                mouse_position_valid_ = true;
                mouse_x_ = state.aim_x;
                mouse_y_ = state.aim_y;
            }
            sys_->set_mouse(0U, delta_x, delta_y, state.trigger, state.a, state.b);
            return;
        }

        if (port == 4 || port == 5) {
            const std::size_t hardware_port = static_cast<std::size_t>(port - 4);
            sys_->set_pot_position(hardware_port, pot_axis(state.aim_x), pot_axis(state.aim_y));
            return;
        }

        if (port == 0 || port == 1) {
            // Player 1 drives the right Amiga controller pair (JOY1DAT/CIAA bit 7),
            // the socket most games expect for a joystick. Player 2 maps to JOY0DAT.
            const std::size_t hardware_port = port == 0 ? 1U : 0U;
            sys_->set_joystick(hardware_port, pack_joystick(state));
        }
        if (port == 0 || port == 2) {
            route_keyboard_edges(*sys_, old_ports, ports_, keyboard_layout_,
                                 reported_keyboard_keys_);
        }
    }

    bool amiga_adapter::insert_media(std::size_t index) noexcept {
        if (index >= disks_.size()) {
            return false;
        }
        if (!sys_->mount_floppy(0U, disks_[index])) {
            return false;
        }
        disk_index_ = index;
        return true;
    }

    std::vector<std::uint8_t> amiga_adapter::save_state() {
        if (trace_cia_bus()) {
            std::fprintf(stderr, "[amiga-save] before intena=%04X intreq=%04X irq=%u frame=%llu\n",
                         sys_->intena, sys_->visible_intreq(), board_irq_level(*sys_),
                         static_cast<unsigned long long>(sys_->frame_index));
        }
        std::vector<std::uint8_t> state = runtime::write_save_state(build_save_target(*this));
        if (trace_cia_bus()) {
            std::fprintf(stderr, "[amiga-save] after intena=%04X intreq=%04X irq=%u frame=%llu\n",
                         sys_->intena, sys_->visible_intreq(), board_irq_level(*sys_),
                         static_cast<unsigned long long>(sys_->frame_index));
        }
        return state;
    }

    runtime::load_result amiga_adapter::load_state(std::span<const std::uint8_t> data) {
        runtime::save_target target = build_save_target(*this);
        const runtime::load_result result = runtime::read_save_state(data, target);
        if (result.ok()) {
            paula_buf_.clear();
            mix_buf_.clear();
        }
        return result;
    }

    void amiga_adapter::save_adapter_state(chips::state_writer& writer) const {
        writer.u32(amiga_adapter_state_version);
        writer.u8(static_cast<std::uint8_t>(ports_.size()));
        writer.u16(static_cast<std::uint16_t>(reported_keyboard_keys_.size()));
        writer.u64(static_cast<std::uint64_t>(disk_index_));
        writer.boolean(mouse_position_valid_);
        writer.u16(std::bit_cast<std::uint16_t>(mouse_x_));
        writer.u16(std::bit_cast<std::uint16_t>(mouse_y_));
        writer.u64(encode_audio_fraction(audio_frac_));
        for (const auto& port : ports_) {
            save_controller_state(writer, port);
        }
        save_raw_key_bitmap(writer, reported_keyboard_keys_);
    }

    void amiga_adapter::load_adapter_state(chips::state_reader& reader) {
        if (reader.u32() != amiga_adapter_state_version) {
            reader.fail();
            return;
        }
        const std::uint8_t saved_port_count = reader.u8();
        const std::uint16_t saved_raw_key_count = reader.u16();
        const std::uint64_t saved_disk_index = reader.u64();
        if (!reader.ok()) {
            return;
        }
        const bool disk_index_valid =
            saved_disk_index <=
                static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()) &&
            ((disks_.empty() && saved_disk_index == 0U) ||
             (!disks_.empty() && saved_disk_index < static_cast<std::uint64_t>(disks_.size())));
        if (static_cast<std::size_t>(saved_port_count) != ports_.size() ||
            static_cast<std::size_t>(saved_raw_key_count) != reported_keyboard_keys_.size() ||
            !disk_index_valid) {
            reader.fail();
            return;
        }

        disk_index_ = static_cast<std::size_t>(saved_disk_index);
        if (!disks_.empty() &&
            disks_[disk_index_].size() == manifests::amiga::amiga_system::floppy_dd_size) {
            auto& df0 = sys_->floppy_drives[0U];
            if (df0.image.size() != manifests::amiga::amiga_system::floppy_dd_size) {
                df0.image.assign(disks_[disk_index_].begin(), disks_[disk_index_].end());
            }
        }
        mouse_position_valid_ = reader.boolean();
        mouse_x_ = std::bit_cast<std::int16_t>(reader.u16());
        mouse_y_ = std::bit_cast<std::int16_t>(reader.u16());
        audio_frac_ = decode_audio_fraction(reader.u64());
        for (auto& port : ports_) {
            port = load_controller_state(reader);
        }
        load_raw_key_bitmap(reader, reported_keyboard_keys_);
    }

    frontend_sdk::audio_chunk amiga_adapter::drain_audio() noexcept {
        const std::size_t pairs = sys_->paula.pending_samples();
        if (pairs == 0U) {
            return {.samples = nullptr, .frame_count = 0U, .sample_rate = mnemos::dsp::kOutputRate};
        }
        paula_buf_.resize(pairs * 2U);
        sys_->paula.drain_samples(paula_buf_.data(), pairs);

        const double exact =
            (static_cast<double>(mnemos::dsp::kOutputRate) / target_fps_) + audio_frac_;
        int dst_pairs = static_cast<int>(exact);
        if (dst_pairs <= 0) {
            dst_pairs = 1;
        }
        audio_frac_ = exact - static_cast<double>(dst_pairs);
        mix_buf_.resize(static_cast<std::size_t>(dst_pairs) * 2U);

        const double scale = static_cast<double>(pairs) / static_cast<double>(dst_pairs);
        for (int i = 0; i < dst_pairs; ++i) {
            int left = 0;
            int right = 0;
            if (scale > 1.0) {
                left = mnemos::dsp::sample_channel_box(
                    paula_buf_.data(), 2, 0, static_cast<int>(pairs), scale * i, scale * (i + 1));
                right = mnemos::dsp::sample_channel_box(
                    paula_buf_.data(), 2, 1, static_cast<int>(pairs), scale * i, scale * (i + 1));
            } else {
                left = mnemos::dsp::sample_channel_linear(paula_buf_.data(), 2, 0,
                                                          static_cast<int>(pairs), scale * i);
                right = mnemos::dsp::sample_channel_linear(paula_buf_.data(), 2, 1,
                                                           static_cast<int>(pairs), scale * i);
            }
            mix_buf_[static_cast<std::size_t>(i) * 2U] = mnemos::dsp::clip_i16(left);
            mix_buf_[static_cast<std::size_t>(i) * 2U + 1U] = mnemos::dsp::clip_i16(right);
        }
        return {.samples = mix_buf_.data(),
                .frame_count = static_cast<std::uint32_t>(dst_pairs),
                .sample_rate = mnemos::dsp::kOutputRate};
    }

    void force_link() noexcept {}

    runtime::save_target build_save_target(amiga_adapter& adapter) {
        auto& sys = adapter.system();
        auto& sched = adapter.scheduler();

        runtime::save_target target;
        target.manifest_id = model_family_id(adapter.model_);
        target.manifest_rev = 4U;
        target.master_cycle = sched.master_cycle();
        target.chips.push_back({"cpu", &sys.cpu});
        target.chips.push_back({"agnus", &sys.agnus});
        target.chips.push_back({"denise", &sys.denise});
        target.chips.push_back({"paula", &sys.paula});
        target.chips.push_back({"cia_a", &sys.cia_a});
        target.chips.push_back({"cia_b", &sys.cia_b});
        target.components.push_back(runtime::save_component{
            .id = "system",
            .save = [&sys](chips::state_writer& writer) { sys.save_state(writer); },
            .load = [&sys](chips::state_reader& reader) { sys.load_state(reader); }});
        target.components.push_back(runtime::save_component{
            .id = "scheduler",
            .save = [&sched](chips::state_writer& writer) { sched.save_state(writer); },
            .load = [&sched](chips::state_reader& reader) { sched.load_state(reader); }});
        target.components.push_back(runtime::save_component{
            .id = "adapter",
            .save = [&adapter](chips::state_writer& writer) { adapter.save_adapter_state(writer); },
            .load =
                [&adapter](chips::state_reader& reader) { adapter.load_adapter_state(reader); }});
        return target;
    }

    namespace {
        void register_amiga_family(const char* family_id, manifests::amiga::amiga_model model) {
            mnemos::frontend_sdk::adapter_registry::instance().register_family(
                family_id,
                [model](mnemos::frontend_sdk::adapter_options opts)
                    -> std::unique_ptr<mnemos::frontend_sdk::player_system> {
                    const auto config = manifests::amiga::amiga_config{
                        .video_region = opts.video_region,
                        .keyboard_layout =
                            keyboard_layout_from_token(opts.keyboard_layout_override),
                        .model = model};
                    const auto selected_config =
                        resolve_amiga_config_override(config, opts.amiga_model_override);
                    return std::make_unique<amiga_adapter>(
                        std::move(opts.rom), selected_config, std::move(opts.display_name),
                        std::move(opts.additional_media), opts.scheduler_factory_override);
                });
        }

        const auto register_amiga = [] {
            register_amiga_family("amiga500", manifests::amiga::amiga_model::amiga500);
            register_amiga_family("amiga500plus", manifests::amiga::amiga_model::amiga500_plus);
            register_amiga_family("amiga600", manifests::amiga::amiga_model::amiga600);
            register_amiga_family("amiga2000", manifests::amiga::amiga_model::amiga2000);
            return 0;
        }();
    } // namespace

} // namespace mnemos::apps::player::adapters::amiga
