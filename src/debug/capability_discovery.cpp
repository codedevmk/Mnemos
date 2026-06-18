#include "capability_discovery.hpp"

#include "path_id.hpp"
#include "player_system.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace mnemos::debug {
    namespace {

        [[nodiscard]] std::string_view
        descriptor_kind_name(const capability_descriptor& descriptor) noexcept {
            if (!descriptor.kind_name.empty()) {
                return descriptor.kind_name;
            }
            return to_string(descriptor.kind);
        }

        [[nodiscard]] auto capability_order_key(const capability_descriptor& descriptor) {
            return std::tuple{
                descriptor_kind_name(descriptor),      std::string_view{descriptor.id},
                std::string_view{descriptor.provider}, descriptor.version,
                to_string(descriptor.state),           to_string(descriptor.scope)};
        }

        [[nodiscard]] auto duplicate_order_key(const capability_descriptor& descriptor) {
            return std::tuple{
                std::string_view{descriptor.provider}, std::string_view{descriptor.id},
                descriptor_kind_name(descriptor),      descriptor.version,
                to_string(descriptor.state),           to_string(descriptor.scope)};
        }

        [[nodiscard]] auto diagnostic_order_key(const diagnostic& diag) {
            return std::tuple{to_string(diag.severity), std::string_view{diag.id},
                              std::string_view{diag.capability_id}, std::string_view{diag.code},
                              std::string_view{diag.detail}};
        }

        [[nodiscard]] auto limit_order_key(const capability_limit& limit) {
            return std::tuple{std::string_view{limit.id}, std::string_view{limit.value}};
        }

        [[nodiscard]] auto provider_order_key(const capability_provider_registration& provider) {
            return std::tuple{std::string_view{provider.id},
                              std::string_view{provider.provenance.pack_id},
                              std::string_view{provider.provenance.plugin_id},
                              std::string_view{provider.provenance.pack_version},
                              std::string_view{provider.provenance.plugin_version},
                              std::string_view{provider.provenance.origin},
                              provider.api_version};
        }

        [[nodiscard]] auto system_order_key(const capability_system_identity& system) {
            return std::tuple{std::string_view{system.family}, std::string_view{system.manifest_id},
                              std::string_view{system.manifest_revision},
                              std::string_view{system.region},
                              std::string_view{system.media_identity}};
        }

        [[nodiscard]] bool is_empty(const capability_system_identity& system) noexcept {
            return system.family.empty() && system.manifest_id.empty() &&
                   system.manifest_revision.empty() && system.region.empty() &&
                   system.media_identity.empty();
        }

        void sort_diagnostics(std::vector<diagnostic>& diagnostics) {
            std::sort(diagnostics.begin(), diagnostics.end(),
                      [](const diagnostic& lhs, const diagnostic& rhs) {
                          return diagnostic_order_key(lhs) < diagnostic_order_key(rhs);
                      });
        }

        void sort_limits(std::vector<capability_limit>& limits) {
            std::sort(limits.begin(), limits.end(),
                      [](const capability_limit& lhs, const capability_limit& rhs) {
                          return limit_order_key(lhs) < limit_order_key(rhs);
                      });
        }

        [[nodiscard]] diagnostic make_duplicate_diagnostic(const capability_descriptor& descriptor,
                                                           const std::size_t count) {
            return make_diagnostic("capability.duplicate", diagnostic_severity::error,
                                   descriptor.id, "capability.duplicate",
                                   "provider '" + descriptor.provider +
                                       "' advertised capability '" + descriptor.id + "' " +
                                       std::to_string(count) + " times");
        }

        [[nodiscard]] diagnostic make_system_conflict_diagnostic() {
            return make_diagnostic(
                "system.identity.conflict", diagnostic_severity::warning, {},
                "system.identity.conflict",
                "provider manifests reported different loaded-system identities");
        }

        [[nodiscard]] std::string stable_id_segment(std::string_view raw, std::string fallback) {
            std::string segment = sanitize_id(raw);
            if (segment.empty()) {
                segment = std::move(fallback);
            }
            return segment;
        }

        [[nodiscard]] std::string chip_base_id(const chips::ichip& chip, const std::size_t index) {
            const chips::chip_metadata metadata = chip.metadata();
            if (!metadata.part_number.empty()) {
                return stable_id_segment(metadata.part_number, "chip");
            }
            if (!metadata.family.empty()) {
                return stable_id_segment(metadata.family, "chip");
            }
            return "chip_" + std::to_string(index);
        }

        [[nodiscard]] std::size_t count_base_ids(const std::vector<std::string>& base_ids,
                                                 std::string_view base) {
            return static_cast<std::size_t>(
                std::count(base_ids.begin(), base_ids.end(), std::string{base}));
        }

        [[nodiscard]] std::size_t occurrence_before(const std::vector<std::string>& base_ids,
                                                    const std::size_t index) {
            std::size_t count = 0U;
            for (std::size_t i = 0U; i < index; ++i) {
                if (base_ids[i] == base_ids[index]) {
                    ++count;
                }
            }
            return count;
        }

        [[nodiscard]] std::vector<std::string>
        stable_chip_ids(std::span<chips::ichip* const> chips) {
            std::vector<std::string> base_ids;
            base_ids.reserve(chips.size());
            for (std::size_t i = 0U; i < chips.size(); ++i) {
                chips::ichip* chip = chips[i];
                base_ids.push_back(chip != nullptr ? chip_base_id(*chip, i)
                                                   : "null_chip_" + std::to_string(i));
            }

            std::vector<std::string> ids;
            ids.reserve(base_ids.size());
            for (std::size_t i = 0U; i < base_ids.size(); ++i) {
                if (count_base_ids(base_ids, base_ids[i]) == 1U) {
                    ids.push_back(base_ids[i]);
                } else {
                    ids.push_back(base_ids[i] + "_" +
                                  std::to_string(occurrence_before(base_ids, i)));
                }
            }
            return ids;
        }

        void add_payload(capability_descriptor& descriptor, std::string key, std::string value) {
            descriptor.payload.push_back(
                capability_payload_field{.key = std::move(key), .value = std::move(value)});
        }

        void annotate_provider_payload(capability_descriptor& descriptor,
                                       const capability_provider_registration& provider) {
            add_payload(descriptor, "provider_registration_id", provider.id);
            add_payload(descriptor, "provider_api_version", std::to_string(provider.api_version));
            add_payload(descriptor, "provider_pack_id", provider.provenance.pack_id);
            add_payload(descriptor, "provider_pack_version", provider.provenance.pack_version);
            add_payload(descriptor, "provider_plugin_id", provider.provenance.plugin_id);
            add_payload(descriptor, "provider_plugin_version", provider.provenance.plugin_version);
            add_payload(descriptor, "provider_origin", provider.provenance.origin);
        }

        void attach_diagnostic(capability_manifest& manifest, capability_descriptor& descriptor,
                               diagnostic diag) {
            descriptor.diagnostics.push_back(diag);
            manifest.diagnostics.push_back(std::move(diag));
        }

        [[nodiscard]] std::string
        provider_diagnostic_target(const capability_provider_registration& provider) {
            if (!provider.id.empty()) {
                return provider.id;
            }
            if (!provider.provenance.plugin_id.empty()) {
                return provider.provenance.plugin_id;
            }
            if (!provider.provenance.pack_id.empty()) {
                return provider.provenance.pack_id;
            }
            return "capability.provider.unnamed";
        }

        [[nodiscard]] diagnostic
        make_provider_diagnostic(const capability_provider_registration& provider,
                                 const diagnostic_severity severity, std::string code,
                                 std::string detail) {
            const std::string id = code;
            return make_diagnostic(id, severity, provider_diagnostic_target(provider),
                                   std::move(code), std::move(detail));
        }

        [[nodiscard]] bool requirement_is_satisfied(std::span<const std::string> satisfied,
                                                    std::string_view requirement) noexcept {
            return std::any_of(satisfied.begin(), satisfied.end(),
                               [requirement](const std::string& id) { return id == requirement; });
        }

        [[nodiscard]] std::string_view
        first_unsatisfied_requirement(const capability_provider_registration& provider,
                                      std::span<const std::string> satisfied) noexcept {
            for (const auto& requirement : provider.requirements) {
                if (!requirement.id.empty() &&
                    !requirement_is_satisfied(satisfied, requirement.id)) {
                    return requirement.id;
                }
            }
            return {};
        }

        [[nodiscard]] bool control_visible(const capability_state state) noexcept {
            return state != capability_state::unavailable;
        }

        [[nodiscard]] bool control_enabled(const capability_state state) noexcept {
            return state == capability_state::available || state == capability_state::experimental;
        }

        [[nodiscard]] std::string diagnostic_explanation(const diagnostic& diag) {
            if (diag.detail.empty()) {
                return diag.code;
            }
            return diag.code + ": " + diag.detail;
        }

        [[nodiscard]] std::string
        first_capability_explanation(const capability_manifest& manifest,
                                     const capability_descriptor& descriptor) {
            if (!descriptor.diagnostics.empty()) {
                return diagnostic_explanation(descriptor.diagnostics.front());
            }
            for (const auto& diag : manifest.diagnostics) {
                if (diag.capability_id == descriptor.id) {
                    return diagnostic_explanation(diag);
                }
            }
            switch (descriptor.state) {
            case capability_state::available:
                return {};
            case capability_state::experimental:
                return "experimental capability";
            case capability_state::degraded:
                return "capability is degraded";
            case capability_state::unavailable:
                return "capability is unavailable";
            }
            return "capability is unavailable";
        }

        [[nodiscard]] std::string control_state_token(const capability_control_summary& control) {
            if (!control.visible) {
                return "hidden";
            }
            if (!control.enabled) {
                return "disabled";
            }
            return "enabled";
        }

        void append_summary_line(std::string& out, std::string line) {
            out += std::move(line);
            out.push_back('\n');
        }

        [[nodiscard]] bool
        framebuffer_is_dumpable(const chips::frame_buffer_view& framebuffer) noexcept {
            return framebuffer.pixels != nullptr && framebuffer.width != 0U &&
                   framebuffer.height != 0U && framebuffer.effective_stride() >= framebuffer.width;
        }

        [[nodiscard]] capability_descriptor
        make_dump_descriptor(capability_kind kind, std::string id,
                             const capability_state state = capability_state::available,
                             const capability_scope scope = capability_scope::system) {
            return make_capability_descriptor(kind, state, "mnemos.debug.dump", std::move(id),
                                              scope);
        }

        [[nodiscard]] capability_descriptor
        make_session_descriptor(std::string id,
                                const capability_state state = capability_state::available) {
            return make_capability_descriptor(capability_kind::session, state,
                                              "mnemos.debug.session", std::move(id),
                                              capability_scope::session);
        }

        [[nodiscard]] capability_descriptor
        make_media_descriptor(std::string id,
                              const capability_state state = capability_state::available) {
            return make_capability_descriptor(capability_kind::media, state, "mnemos.debug.media",
                                              std::move(id), capability_scope::media);
        }

        [[nodiscard]] std::string bool_payload(const bool value) {
            return value ? "true" : "false";
        }

        [[nodiscard]] std::string_view
        media_residency_name(const frontend_sdk::media_residency residency) noexcept {
            switch (residency) {
            case frontend_sdk::media_residency::resident:
                return "resident";
            case frontend_sdk::media_residency::paged:
                return "paged";
            case frontend_sdk::media_residency::streamed:
                return "streamed";
            }
            return "resident";
        }

        [[nodiscard]] std::string_view
        media_hash_algorithm_name(const frontend_sdk::media_hash_algorithm algorithm) noexcept {
            switch (algorithm) {
            case frontend_sdk::media_hash_algorithm::none:
                return "none";
            case frontend_sdk::media_hash_algorithm::crc32:
                return "crc32";
            case frontend_sdk::media_hash_algorithm::sha1:
                return "sha1";
            case frontend_sdk::media_hash_algorithm::sha256:
                return "sha256";
            }
            return "none";
        }

        [[nodiscard]] std::string_view
        input_format_name(const frontend_sdk::input_device_format format) noexcept {
            switch (format) {
            case frontend_sdk::input_device_format::unknown:
                return "unknown";
            case frontend_sdk::input_device_format::digital_pad:
                return "digital_pad";
            case frontend_sdk::input_device_format::arcade_panel:
                return "arcade_panel";
            case frontend_sdk::input_device_format::keyboard:
                return "keyboard";
            case frontend_sdk::input_device_format::mouse:
                return "mouse";
            case frontend_sdk::input_device_format::lightgun:
                return "lightgun";
            case frontend_sdk::input_device_format::analog:
                return "analog";
            }
            return "unknown";
        }

        [[nodiscard]] bool
        input_format_is_known(const frontend_sdk::input_device_format format) noexcept {
            return format != frontend_sdk::input_device_format::unknown;
        }

        [[nodiscard]] capability_state
        media_descriptor_state(const frontend_sdk::media_image_info& media) noexcept {
            if (!media.provider_available) {
                return capability_state::unavailable;
            }
            if (media.full_hash.empty() || !media.revision_supported ||
                (media.residency == frontend_sdk::media_residency::paged &&
                 media.page_size == 0U)) {
                return capability_state::degraded;
            }
            return capability_state::available;
        }

        void add_page_hash_payloads(capability_descriptor& descriptor,
                                    const frontend_sdk::media_image_info& media) {
            add_payload(descriptor, "page_hash_count", std::to_string(media.page_hashes.size()));
            for (std::size_t i = 0U; i < media.page_hashes.size(); ++i) {
                const frontend_sdk::media_page_hash& page = media.page_hashes[i];
                const std::string prefix = "page_hash." + std::to_string(i);
                add_payload(descriptor, prefix + ".offset", std::to_string(page.offset));
                add_payload(descriptor, prefix + ".bytes", std::to_string(page.byte_count));
                add_payload(descriptor, prefix + ".hash", page.hash);
            }
        }

        void add_media_diagnostics(capability_manifest& manifest, capability_descriptor& descriptor,
                                   const frontend_sdk::media_image_info& media) {
            if (media.full_hash.empty()) {
                attach_diagnostic(manifest, descriptor,
                                  make_diagnostic("media.hash.missing",
                                                  diagnostic_severity::warning, descriptor.id,
                                                  "media.hash.missing",
                                                  "media descriptor did not publish a full-media "
                                                  "hash"));
            }
            if (!media.provider_available) {
                attach_diagnostic(
                    manifest, descriptor,
                    make_diagnostic("media.provider.unavailable", diagnostic_severity::warning,
                                    descriptor.id, "media.provider.unavailable",
                                    "media provider '" + media.provider_id + "' is unavailable"));
            }
            if (!media.revision_supported) {
                attach_diagnostic(manifest, descriptor,
                                  make_diagnostic("media.revision.unsupported",
                                                  diagnostic_severity::warning, descriptor.id,
                                                  "media.revision.unsupported",
                                                  "media revision '" + media.revision +
                                                      "' is not supported by this provider"));
            }
            if (media.residency == frontend_sdk::media_residency::paged && media.page_size == 0U) {
                attach_diagnostic(manifest, descriptor,
                                  make_diagnostic("media.page_size.missing",
                                                  diagnostic_severity::warning, descriptor.id,
                                                  "media.page_size.missing",
                                                  "paged media descriptors must publish a "
                                                  "non-zero page size"));
            }
        }

        void add_media_descriptors(capability_manifest& manifest,
                                   const frontend_sdk::player_system& sys) {
            const frontend_sdk::media_capability_info& media_info = sys.media_capabilities();
            for (std::size_t i = 0U; i < media_info.media.size(); ++i) {
                const frontend_sdk::media_image_info& media = media_info.media[i];
                const std::string stable_media_id =
                    stable_id_segment(media.id, "index_" + std::to_string(i));
                auto descriptor = make_media_descriptor("media." + stable_media_id,
                                                        media_descriptor_state(media));
                add_payload(descriptor, "surface", "player_system.media_capabilities");
                add_payload(descriptor, "media_index", std::to_string(i));
                add_payload(descriptor, "media_id", media.id);
                add_payload(descriptor, "label", media.label);
                add_payload(descriptor, "residency",
                            std::string{media_residency_name(media.residency)});
                add_payload(descriptor, "bytes", std::to_string(media.byte_count));
                add_payload(descriptor, "page_size", std::to_string(media.page_size));
                add_payload(descriptor, "hash_algorithm",
                            std::string{media_hash_algorithm_name(media.hash_algorithm)});
                add_payload(descriptor, "full_hash", media.full_hash);
                add_payload(descriptor, "provider_id", media.provider_id);
                add_payload(descriptor, "provider_available",
                            bool_payload(media.provider_available));
                add_payload(descriptor, "revision", media.revision);
                add_payload(descriptor, "revision_supported",
                            bool_payload(media.revision_supported));
                add_payload(descriptor, "cache_hint", media.cache_hint);
                add_page_hash_payloads(descriptor, media);
                add_media_diagnostics(manifest, descriptor, media);
                manifest.capabilities.push_back(std::move(descriptor));
            }
        }

        void add_session_mode_descriptor(capability_manifest& manifest,
                                         capability_descriptor descriptor,
                                         std::string unavailable_code,
                                         std::string unavailable_detail) {
            add_payload(descriptor, "surface", "player_system.session_capabilities");
            if (descriptor.state == capability_state::unavailable) {
                attach_diagnostic(manifest, descriptor,
                                  make_diagnostic(unavailable_code, diagnostic_severity::warning,
                                                  descriptor.id, unavailable_code,
                                                  unavailable_detail));
            }
            manifest.capabilities.push_back(std::move(descriptor));
        }

        void add_session_descriptors(capability_manifest& manifest,
                                     const frontend_sdk::player_system& sys) {
            const frontend_sdk::session_capability_info& session = sys.session_capabilities();
            const bool has_ports = !session.input_ports.empty();
            const bool deterministic = session.deterministic_frame_input;

            auto local = make_session_descriptor("session.mode.local",
                                                 has_ports ? capability_state::available
                                                           : capability_state::degraded);
            add_payload(local, "surface", "player_system.session_capabilities");
            add_payload(local, "port_count", std::to_string(session.input_ports.size()));
            add_payload(local, "deterministic_frame_input", bool_payload(deterministic));
            if (!has_ports) {
                attach_diagnostic(
                    manifest, local,
                    make_diagnostic("session.input.ports_missing", diagnostic_severity::warning,
                                    local.id, "session.input.ports_missing",
                                    "player_system did not publish controller port metadata"));
            }
            manifest.capabilities.push_back(std::move(local));

            for (const frontend_sdk::session_input_port& port : session.input_ports) {
                auto descriptor = make_session_descriptor(
                    "session.input.port." + std::to_string(port.port_index),
                    input_format_is_known(port.format) ? capability_state::available
                                                       : capability_state::degraded);
                add_payload(descriptor, "surface", "player_system.session_capabilities");
                add_payload(descriptor, "port_index", std::to_string(port.port_index));
                add_payload(descriptor, "player_slot", std::to_string(port.player_slot));
                add_payload(descriptor, "format", std::string{input_format_name(port.format)});
                add_payload(descriptor, "device_id", port.device_id);
                add_payload(descriptor, "label", port.label);
                add_payload(descriptor, "connected", bool_payload(port.connected));
                if (!input_format_is_known(port.format)) {
                    attach_diagnostic(
                        manifest, descriptor,
                        make_diagnostic("session.input.device_unknown",
                                        diagnostic_severity::warning, descriptor.id,
                                        "session.input.device_unknown",
                                        "controller port " + std::to_string(port.port_index) +
                                            " did not publish a known input device format"));
                }
                manifest.capabilities.push_back(std::move(descriptor));
            }

            auto lockstep =
                make_session_descriptor("session.mode.lockstep",
                                        has_ports && deterministic ? capability_state::available
                                                                   : capability_state::unavailable);
            add_payload(lockstep, "port_count", std::to_string(session.input_ports.size()));
            add_payload(lockstep, "deterministic_frame_input", bool_payload(deterministic));
            add_session_mode_descriptor(
                manifest, std::move(lockstep), "session.lockstep.unavailable",
                "lockstep input requires published controller ports and deterministic "
                "frame-tagged input");

            auto input_delay = make_session_descriptor("session.mode.input_delay",
                                                       has_ports && deterministic &&
                                                               session.max_input_delay_frames > 0U
                                                           ? capability_state::available
                                                           : capability_state::unavailable);
            add_payload(input_delay, "port_count", std::to_string(session.input_ports.size()));
            add_payload(input_delay, "deterministic_frame_input", bool_payload(deterministic));
            add_payload(input_delay, "max_input_delay_frames",
                        std::to_string(session.max_input_delay_frames));
            add_session_mode_descriptor(
                manifest, std::move(input_delay), "session.input_delay.unavailable",
                "input-delay multiplayer requires deterministic input and a non-zero "
                "adapter-supported delay window");

            auto rollback = make_session_descriptor("session.mode.rollback",
                                                    has_ports && deterministic &&
                                                            session.save_state_supported &&
                                                            session.frame_exact_save_state
                                                        ? capability_state::available
                                                        : capability_state::unavailable);
            add_payload(rollback, "port_count", std::to_string(session.input_ports.size()));
            add_payload(rollback, "deterministic_frame_input", bool_payload(deterministic));
            add_payload(rollback, "save_state_supported",
                        bool_payload(session.save_state_supported));
            add_payload(rollback, "frame_exact_save_state",
                        bool_payload(session.frame_exact_save_state));
            add_session_mode_descriptor(
                manifest, std::move(rollback), "session.rollback.unavailable",
                "rollback requires deterministic input plus frame-exact save-state support");
        }

        void add_system_memory_descriptors(capability_manifest& manifest,
                                           const frontend_sdk::player_system& sys) {
            std::size_t index = 0U;
            for (instrumentation::memory_view* view : sys.memory_views()) {
                if (view == nullptr) {
                    ++index;
                    continue;
                }
                const std::string view_id =
                    stable_id_segment(view->name(), "memory_" + std::to_string(index));
                auto descriptor =
                    make_dump_descriptor(capability_kind::memory, "memory.system." + view_id);
                add_payload(descriptor, "surface", "player_system.memory_views");
                add_payload(descriptor, "source", "system");
                add_payload(descriptor, "view", std::string{view->name()});
                add_payload(descriptor, "artifact", "bin");
                add_payload(descriptor, "bytes", std::to_string(view->bytes().size()));
                manifest.capabilities.push_back(std::move(descriptor));
                ++index;
            }
        }

        void add_chip_memory_descriptors(capability_manifest& manifest,
                                         instrumentation::ichip_introspection& intro,
                                         const std::string& chip_id) {
            std::size_t index = 0U;
            for (instrumentation::memory_view* view : intro.memory_views()) {
                if (view == nullptr) {
                    ++index;
                    continue;
                }
                const std::string view_id =
                    stable_id_segment(view->name(), "memory_" + std::to_string(index));
                auto descriptor = make_dump_descriptor(capability_kind::memory,
                                                       "memory." + chip_id + "." + view_id);
                add_payload(descriptor, "surface", "ichip_introspection.memory_views");
                add_payload(descriptor, "source", "chip");
                add_payload(descriptor, "chip", chip_id);
                add_payload(descriptor, "view", std::string{view->name()});
                add_payload(descriptor, "artifact", "bin");
                add_payload(descriptor, "bytes", std::to_string(view->bytes().size()));
                manifest.capabilities.push_back(std::move(descriptor));
                ++index;
            }
        }

        void add_register_descriptor(capability_manifest& manifest,
                                     instrumentation::ichip_introspection& intro,
                                     const chips::chip_metadata& metadata,
                                     const std::string& chip_id) {
            if (intro.registers() == nullptr) {
                return;
            }
            auto descriptor =
                make_dump_descriptor(capability_kind::memory, "memory." + chip_id + ".registers");
            add_payload(descriptor, "surface", "ichip_introspection.registers");
            add_payload(descriptor, "source", "chip");
            add_payload(descriptor, "chip", chip_id);
            add_payload(descriptor, "chip_class",
                        std::string{chips::chip_class_name(metadata.klass)});
            add_payload(descriptor, "artifact",
                        metadata.klass == chips::chip_class::audio_synth ? "audio_json_registers"
                                                                         : "register_snapshot");
            manifest.capabilities.push_back(std::move(descriptor));
        }

        void add_graphics_descriptors(capability_manifest& manifest,
                                      instrumentation::ichip_introspection& intro,
                                      const std::string& chip_id) {
            if (intro.assets() != nullptr) {
                auto descriptor =
                    make_dump_descriptor(capability_kind::asset, "asset." + chip_id + ".graphics");
                add_payload(descriptor, "surface", "ichip_introspection.assets");
                add_payload(descriptor, "chip", chip_id);
                add_payload(descriptor, "artifact", "png,json");
                add_payload(descriptor, "content", "palettes,tilesets,sprites,fonts,bitmaps");
                manifest.capabilities.push_back(std::move(descriptor));
            }

            std::size_t index = 0U;
            for (instrumentation::debug_layer* layer : intro.debug_layers()) {
                if (layer == nullptr) {
                    ++index;
                    continue;
                }
                const std::string layer_id =
                    stable_id_segment(layer->name(), "layer_" + std::to_string(index));
                auto descriptor = make_dump_descriptor(capability_kind::asset,
                                                       "asset." + chip_id + ".layer." + layer_id);
                add_payload(descriptor, "surface", "ichip_introspection.debug_layers");
                add_payload(descriptor, "chip", chip_id);
                add_payload(descriptor, "layer", std::string{layer->name()});
                add_payload(descriptor, "artifact", "png,ppm");
                add_payload(descriptor, "content", "composed_debug_layer");
                manifest.capabilities.push_back(std::move(descriptor));
                ++index;
            }
        }

        void add_audio_descriptors(capability_manifest& manifest,
                                   instrumentation::ichip_introspection& intro,
                                   const chips::chip_metadata& metadata,
                                   const std::string& chip_id) {
            if (intro.audio() != nullptr) {
                auto descriptor =
                    make_dump_descriptor(capability_kind::audio, "audio." + chip_id + ".samples");
                add_payload(descriptor, "surface", "ichip_introspection.audio");
                add_payload(descriptor, "chip", chip_id);
                add_payload(descriptor, "artifact", "wav,json");
                add_payload(descriptor, "content", "pcm_samples");
                manifest.capabilities.push_back(std::move(descriptor));
            }

            if (intro.reg_writes() != nullptr && metadata.part_number == "SN76489") {
                auto descriptor = make_dump_descriptor(capability_kind::audio,
                                                       "audio." + chip_id + ".reg_write_vgm");
                add_payload(descriptor, "surface", "ichip_introspection.reg_writes");
                add_payload(descriptor, "chip", chip_id);
                add_payload(descriptor, "artifact", "vgm");
                add_payload(descriptor, "content", "sn76489_register_write_stream");
                manifest.capabilities.push_back(std::move(descriptor));
            }
        }

        void add_trace_descriptor(capability_manifest& manifest,
                                  instrumentation::ichip_introspection& intro,
                                  const std::string& chip_id) {
            if (intro.trace() == nullptr) {
                return;
            }
            auto descriptor =
                make_dump_descriptor(capability_kind::debug, "debug." + chip_id + ".cpu_trace");
            add_payload(descriptor, "surface", "ichip_introspection.trace");
            add_payload(descriptor, "chip", chip_id);
            add_payload(descriptor, "artifact", "csv");
            add_payload(descriptor, "content", "frame,inst,pc,cycles");
            manifest.capabilities.push_back(std::move(descriptor));
        }

        void add_screenshot_descriptor(capability_manifest& manifest,
                                       const frontend_sdk::player_system& sys) {
            const chips::frame_buffer_view framebuffer = sys.current_frame();
            auto descriptor = make_dump_descriptor(capability_kind::debug, "debug.screenshot",
                                                   framebuffer_is_dumpable(framebuffer)
                                                       ? capability_state::available
                                                       : capability_state::unavailable);
            add_payload(descriptor, "surface", "player_system.current_frame");
            add_payload(descriptor, "artifact", "ppm");
            add_payload(descriptor, "width", std::to_string(framebuffer.width));
            add_payload(descriptor, "height", std::to_string(framebuffer.height));
            add_payload(descriptor, "stride", std::to_string(framebuffer.effective_stride()));
            if (!framebuffer_is_dumpable(framebuffer)) {
                attach_diagnostic(
                    manifest, descriptor,
                    make_diagnostic("debug.framebuffer.unavailable", diagnostic_severity::warning,
                                    descriptor.id, "debug.framebuffer.unavailable",
                                    "player_system::current_frame() did not expose a dumpable "
                                    "framebuffer"));
            }
            manifest.capabilities.push_back(std::move(descriptor));
        }

    } // namespace

    void
    capability_provider_registry::register_provider(capability_provider_registration provider) {
        providers_.push_back(std::move(provider));
    }

    std::span<const capability_provider_registration>
    capability_provider_registry::providers() const noexcept {
        return std::span<const capability_provider_registration>{providers_.data(),
                                                                 providers_.size()};
    }

    std::string_view to_string(const capability_kind kind) noexcept {
        switch (kind) {
        case capability_kind::media:
            return "media";
        case capability_kind::watch:
            return "watch";
        case capability_kind::session:
            return "session";
        case capability_kind::achievement:
            return "achievement";
        case capability_kind::asset:
            return "asset";
        case capability_kind::audio:
            return "audio";
        case capability_kind::memory:
            return "memory";
        case capability_kind::debug:
            return "debug";
        case capability_kind::unknown:
            return "unknown";
        }
        return "unknown";
    }

    std::string_view to_string(const capability_state state) noexcept {
        switch (state) {
        case capability_state::available:
            return "available";
        case capability_state::unavailable:
            return "unavailable";
        case capability_state::degraded:
            return "degraded";
        case capability_state::experimental:
            return "experimental";
        }
        return "unavailable";
    }

    std::string_view to_string(const capability_scope scope) noexcept {
        switch (scope) {
        case capability_scope::system:
            return "system";
        case capability_scope::game:
            return "game";
        case capability_scope::media:
            return "media";
        case capability_scope::session:
            return "session";
        case capability_scope::host:
            return "host";
        }
        return "system";
    }

    std::string_view to_string(const diagnostic_severity severity) noexcept {
        switch (severity) {
        case diagnostic_severity::info:
            return "info";
        case diagnostic_severity::warning:
            return "warning";
        case diagnostic_severity::error:
            return "error";
        }
        return "error";
    }

    diagnostic make_diagnostic(std::string id, const diagnostic_severity severity,
                               std::string capability_id, std::string code, std::string detail) {
        return diagnostic{.id = std::move(id),
                          .severity = severity,
                          .capability_id = std::move(capability_id),
                          .code = std::move(code),
                          .detail = std::move(detail)};
    }

    capability_descriptor make_capability_descriptor(const capability_kind kind,
                                                     std::string provider, std::string id,
                                                     const capability_scope scope,
                                                     const std::uint32_t version) {
        return make_capability_descriptor(kind, capability_state::available, std::move(provider),
                                          std::move(id), scope, version);
    }

    capability_descriptor make_capability_descriptor(const capability_kind kind,
                                                     const capability_state state,
                                                     std::string provider, std::string id,
                                                     const capability_scope scope,
                                                     const std::uint32_t version) {
        return capability_descriptor{.id = std::move(id),
                                     .kind = kind,
                                     .kind_name = std::string{to_string(kind)},
                                     .provider = std::move(provider),
                                     .version = version,
                                     .state = state,
                                     .scope = scope};
    }

    capability_descriptor make_opaque_capability_descriptor(std::string kind_name,
                                                            std::string provider, std::string id,
                                                            const capability_scope scope,
                                                            const capability_state state,
                                                            const std::uint32_t version) {
        return capability_descriptor{.id = std::move(id),
                                     .kind = capability_kind::unknown,
                                     .kind_name = std::move(kind_name),
                                     .provider = std::move(provider),
                                     .version = version,
                                     .state = state,
                                     .scope = scope};
    }

    capability_manifest
    merge_capability_manifests(std::span<const capability_manifest> provider_manifests) {
        capability_manifest merged{};
        merged.schema = capability_manifest_schema_version;
        merged.producer = {.id = "mnemos.debug.capability_discovery", .version = 1U};

        std::vector<capability_system_identity> identities;
        std::vector<capability_descriptor> descriptors;

        for (const auto& manifest : provider_manifests) {
            if (!is_empty(manifest.system)) {
                identities.push_back(manifest.system);
            }
            merged.limits.insert(merged.limits.end(), manifest.limits.begin(),
                                 manifest.limits.end());
            merged.diagnostics.insert(merged.diagnostics.end(), manifest.diagnostics.begin(),
                                      manifest.diagnostics.end());
            descriptors.insert(descriptors.end(), manifest.capabilities.begin(),
                               manifest.capabilities.end());
        }

        if (!identities.empty()) {
            std::sort(
                identities.begin(), identities.end(),
                [](const capability_system_identity& lhs, const capability_system_identity& rhs) {
                    return system_order_key(lhs) < system_order_key(rhs);
                });
            merged.system = identities.front();
            identities.erase(std::unique(identities.begin(), identities.end(),
                                         [](const capability_system_identity& lhs,
                                            const capability_system_identity& rhs) {
                                             return system_order_key(lhs) == system_order_key(rhs);
                                         }),
                             identities.end());
            if (identities.size() > 1U) {
                merged.diagnostics.push_back(make_system_conflict_diagnostic());
            }
        }

        std::sort(descriptors.begin(), descriptors.end(),
                  [](const capability_descriptor& lhs, const capability_descriptor& rhs) {
                      return duplicate_order_key(lhs) < duplicate_order_key(rhs);
                  });

        for (std::size_t index = 0U; index < descriptors.size();) {
            const auto group_begin = index;
            const auto& first = descriptors[group_begin];
            while (index < descriptors.size() && descriptors[index].provider == first.provider &&
                   descriptors[index].id == first.id) {
                ++index;
            }

            const auto count = index - group_begin;
            if (count == 1U) {
                merged.capabilities.push_back(first);
            } else {
                merged.diagnostics.push_back(make_duplicate_diagnostic(first, count));
            }
        }

        std::sort(merged.capabilities.begin(), merged.capabilities.end(),
                  [](const capability_descriptor& lhs, const capability_descriptor& rhs) {
                      return capability_order_key(lhs) < capability_order_key(rhs);
                  });
        sort_limits(merged.limits);
        sort_diagnostics(merged.diagnostics);

        return merged;
    }

    capability_manifest
    collect_registered_capabilities(const capability_provider_registry& registry,
                                    std::span<const std::string> satisfied_requirements) {
        capability_manifest diagnostics_manifest{};
        diagnostics_manifest.schema = capability_manifest_schema_version;
        diagnostics_manifest.producer = {.id = "mnemos.debug.capability_provider_registry",
                                         .version = capability_provider_api_version};

        const auto registered = registry.providers();
        std::vector<capability_provider_registration> providers{registered.begin(),
                                                                registered.end()};
        std::sort(providers.begin(), providers.end(),
                  [](const capability_provider_registration& lhs,
                     const capability_provider_registration& rhs) {
                      return provider_order_key(lhs) < provider_order_key(rhs);
                  });

        std::vector<capability_manifest> manifests;
        manifests.reserve(providers.size() + 1U);
        manifests.push_back(std::move(diagnostics_manifest));

        for (const auto& provider : providers) {
            capability_manifest& collector_manifest = manifests.front();
            if (provider.id.empty()) {
                collector_manifest.diagnostics.push_back(make_provider_diagnostic(
                    provider, diagnostic_severity::error, "capability.provider.id_missing",
                    "provider registration did not declare a stable id"));
                continue;
            }

            if (!provider.enabled) {
                collector_manifest.diagnostics.push_back(make_provider_diagnostic(
                    provider, diagnostic_severity::info, "capability.provider.disabled",
                    "provider '" + provider.id + "' is disabled"));
                continue;
            }

            if (provider.api_version != capability_provider_api_version) {
                collector_manifest.diagnostics.push_back(make_provider_diagnostic(
                    provider, diagnostic_severity::warning, "capability.provider.stale_version",
                    "provider '" + provider.id + "' targets capability provider API " +
                        std::to_string(provider.api_version) + ", expected " +
                        std::to_string(capability_provider_api_version)));
                continue;
            }

            const std::string_view missing_requirement =
                first_unsatisfied_requirement(provider, satisfied_requirements);
            if (!missing_requirement.empty()) {
                collector_manifest.diagnostics.push_back(
                    make_provider_diagnostic(provider, diagnostic_severity::warning,
                                             "capability.provider.requirement_unsatisfied",
                                             "provider '" + provider.id + "' requires '" +
                                                 std::string{missing_requirement} + "'"));
                continue;
            }

            if (provider.collect == nullptr) {
                collector_manifest.diagnostics.push_back(make_provider_diagnostic(
                    provider, diagnostic_severity::error, "capability.provider.collect_missing",
                    "provider '" + provider.id + "' did not register a collect callback"));
                continue;
            }

            capability_manifest provider_manifest{};
            const bool collected = provider.collect(provider.context, provider_manifest,
                                                    collector_manifest.diagnostics);
            if (!collected) {
                collector_manifest.diagnostics.push_back(make_provider_diagnostic(
                    provider, diagnostic_severity::warning, "capability.provider.collect_failed",
                    "provider '" + provider.id + "' reported collection failure"));
                continue;
            }

            if (provider_manifest.producer.id.empty()) {
                provider_manifest.producer.id = provider.id;
            }
            if (provider_manifest.schema == 0U) {
                provider_manifest.schema = capability_manifest_schema_version;
            }

            for (auto& descriptor : provider_manifest.capabilities) {
                annotate_provider_payload(descriptor, provider);
            }
            manifests.push_back(std::move(provider_manifest));
        }

        return merge_capability_manifests(manifests);
    }

    std::vector<capability_control_summary>
    build_capability_control_summary(const capability_manifest& manifest) {
        std::vector<capability_control_summary> controls;
        controls.reserve(manifest.capabilities.size());
        for (const auto& descriptor : manifest.capabilities) {
            controls.push_back(capability_control_summary{
                .id = descriptor.id,
                .kind = std::string{descriptor_kind_name(descriptor)},
                .provider = descriptor.provider,
                .state = descriptor.state,
                .scope = descriptor.scope,
                .visible = control_visible(descriptor.state),
                .enabled = control_enabled(descriptor.state),
                .explanation = first_capability_explanation(manifest, descriptor)});
        }
        return controls;
    }

    std::string format_capability_summary(const capability_manifest& manifest) {
        std::string out;
        const auto controls = build_capability_control_summary(manifest);

        append_summary_line(out, "schema=" + std::to_string(manifest.schema) +
                                     " producer=" + manifest.producer.id + "@" +
                                     std::to_string(manifest.producer.version) +
                                     " capabilities=" + std::to_string(controls.size()) +
                                     " diagnostics=" + std::to_string(manifest.diagnostics.size()));
        for (const auto& control : controls) {
            std::string line = "capability " + control.kind + " " + control.id +
                               " state=" + std::string{to_string(control.state)} +
                               " control=" + control_state_token(control) +
                               " scope=" + std::string{to_string(control.scope)} +
                               " provider=" + control.provider;
            if (!control.explanation.empty()) {
                line += " explanation=" + control.explanation;
            }
            append_summary_line(out, std::move(line));
        }
        for (const auto& diag : manifest.diagnostics) {
            std::string line =
                "diagnostic " + std::string{to_string(diag.severity)} + " " + diag.code;
            if (!diag.capability_id.empty()) {
                line += " capability=" + diag.capability_id;
            }
            if (!diag.detail.empty()) {
                line += " detail=" + diag.detail;
            }
            append_summary_line(out, std::move(line));
        }
        return out;
    }

    capability_manifest discover_dump_capabilities(const frontend_sdk::player_system& sys) {
        capability_manifest manifest{};
        manifest.schema = capability_manifest_schema_version;
        manifest.producer = {.id = "mnemos.debug.dump", .version = 1U};

        add_screenshot_descriptor(manifest, sys);
        add_media_descriptors(manifest, sys);
        add_session_descriptors(manifest, sys);
        add_system_memory_descriptors(manifest, sys);

        const std::span<chips::ichip* const> chips = sys.chips();
        const std::vector<std::string> chip_ids = stable_chip_ids(chips);

        std::size_t valid_chips = 0U;
        std::size_t surface_count = 0U;
        for (std::size_t i = 0U; i < chips.size(); ++i) {
            chips::ichip* chip = chips[i];
            if (chip == nullptr) {
                continue;
            }
            ++valid_chips;
            const chips::chip_metadata metadata = chip->metadata();
            instrumentation::ichip_introspection& intro = chip->introspection();
            const std::size_t before = manifest.capabilities.size();

            add_chip_memory_descriptors(manifest, intro, chip_ids[i]);
            add_register_descriptor(manifest, intro, metadata, chip_ids[i]);
            add_graphics_descriptors(manifest, intro, chip_ids[i]);
            add_audio_descriptors(manifest, intro, metadata, chip_ids[i]);
            add_trace_descriptor(manifest, intro, chip_ids[i]);

            if (manifest.capabilities.size() != before) {
                ++surface_count;
            }
        }

        if (valid_chips == 0U) {
            manifest.diagnostics.push_back(make_diagnostic(
                "debug.chips.empty", diagnostic_severity::warning, {}, "debug.chips.empty",
                "player_system::chips() did not expose any non-null chips for introspection"));
        } else if (surface_count == 0U && sys.memory_views().empty()) {
            manifest.diagnostics.push_back(make_diagnostic(
                "debug.introspection.empty", diagnostic_severity::info, {},
                "debug.introspection.empty",
                "chips were present but none exposed dumpable introspection surfaces"));
        }

        const std::array manifests{std::move(manifest)};
        return merge_capability_manifests(manifests);
    }

} // namespace mnemos::debug
