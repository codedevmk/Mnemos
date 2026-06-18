#pragma once

// In-process capability discovery model for debug/tooling consumers.
//
// This is intentionally serialization-neutral: it defines the logical snapshot
// and merge behavior without committing Mnemos to a wire format.

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace mnemos::frontend_sdk {
    class player_system;
}

namespace mnemos::debug {

    inline constexpr std::uint32_t capability_manifest_schema_version = 1U;
    inline constexpr std::uint32_t capability_provider_api_version = 1U;

    enum class capability_kind : std::uint8_t {
        media,
        watch,
        session,
        achievement,
        asset,
        audio,
        memory,
        debug,
        unknown
    };

    enum class capability_state : std::uint8_t { available, unavailable, degraded, experimental };

    enum class capability_scope : std::uint8_t { system, game, media, session, host };

    enum class diagnostic_severity : std::uint8_t { info, warning, error };

    struct diagnostic {
        std::string id{};
        diagnostic_severity severity{diagnostic_severity::info};
        std::string capability_id{};
        std::string code{};
        std::string detail{};
    };

    struct capability_requirement {
        std::string id{};
    };

    struct capability_payload_field {
        std::string key{};
        std::string value{};
    };

    struct capability_descriptor {
        std::string id{};
        capability_kind kind{capability_kind::unknown};
        std::string kind_name{};
        std::string provider{};
        std::uint32_t version{1U};
        capability_state state{capability_state::available};
        capability_scope scope{capability_scope::system};
        std::vector<capability_requirement> requirements{};
        std::vector<diagnostic> diagnostics{};
        std::vector<capability_payload_field> payload{};
    };

    struct capability_producer {
        std::string id{};
        std::uint32_t version{1U};
    };

    struct capability_system_identity {
        std::string family{};
        std::string manifest_id{};
        std::string manifest_revision{};
        std::string region{};
        std::string media_identity{};
    };

    struct capability_limit {
        std::string id{};
        std::string value{};
    };

    struct capability_manifest {
        std::uint32_t schema{capability_manifest_schema_version};
        capability_producer producer{};
        capability_system_identity system{};
        std::vector<capability_descriptor> capabilities{};
        std::vector<capability_limit> limits{};
        std::vector<diagnostic> diagnostics{};
    };

    struct capability_provider_provenance {
        std::string pack_id{};
        std::string pack_version{};
        std::string plugin_id{};
        std::string plugin_version{};
        std::string origin{};
    };

    using capability_provider_collect_fn = bool (*)(void* context, capability_manifest& out,
                                                    std::vector<diagnostic>& diagnostics) noexcept;

    struct capability_provider_registration {
        std::string id{};
        std::uint32_t api_version{capability_provider_api_version};
        bool enabled{true};
        std::vector<capability_requirement> requirements{};
        capability_provider_provenance provenance{};
        void* context{};
        capability_provider_collect_fn collect{};
    };

    class capability_provider_registry {
      public:
        void register_provider(capability_provider_registration provider);

        [[nodiscard]] std::span<const capability_provider_registration> providers() const noexcept;

      private:
        std::vector<capability_provider_registration> providers_{};
    };

    struct capability_control_summary {
        std::string id{};
        std::string kind{};
        std::string provider{};
        capability_state state{capability_state::available};
        capability_scope scope{capability_scope::system};
        bool visible{true};
        bool enabled{true};
        std::string explanation{};
    };

    [[nodiscard]] std::string_view to_string(capability_kind kind) noexcept;
    [[nodiscard]] std::string_view to_string(capability_state state) noexcept;
    [[nodiscard]] std::string_view to_string(capability_scope scope) noexcept;
    [[nodiscard]] std::string_view to_string(diagnostic_severity severity) noexcept;

    [[nodiscard]] diagnostic make_diagnostic(std::string id, diagnostic_severity severity,
                                             std::string capability_id, std::string code,
                                             std::string detail = {});

    [[nodiscard]] capability_descriptor
    make_capability_descriptor(capability_kind kind, std::string provider, std::string id,
                               capability_scope scope = capability_scope::system,
                               std::uint32_t version = 1U);

    [[nodiscard]] capability_descriptor
    make_capability_descriptor(capability_kind kind, capability_state state, std::string provider,
                               std::string id, capability_scope scope = capability_scope::system,
                               std::uint32_t version = 1U);

    [[nodiscard]] capability_descriptor
    make_opaque_capability_descriptor(std::string kind_name, std::string provider, std::string id,
                                      capability_scope scope = capability_scope::system,
                                      capability_state state = capability_state::available,
                                      std::uint32_t version = 1U);

    [[nodiscard]] capability_manifest
    merge_capability_manifests(std::span<const capability_manifest> provider_manifests);

    [[nodiscard]] capability_manifest
    collect_registered_capabilities(const capability_provider_registry& registry,
                                    std::span<const std::string> satisfied_requirements = {});

    [[nodiscard]] std::vector<capability_control_summary>
    build_capability_control_summary(const capability_manifest& manifest);

    [[nodiscard]] std::string format_capability_summary(const capability_manifest& manifest);

    [[nodiscard]] capability_manifest
    discover_dump_capabilities(const frontend_sdk::player_system& sys);

} // namespace mnemos::debug
