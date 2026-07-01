#pragma once

// In-process debugger session facade over frontend_sdk::player_system.
//
// This is the first sidecar-facing read-only contract slice: consumers enumerate
// stable ids, then capture copied snapshots by id. The facade deliberately uses
// only player_system + chip introspection surfaces so a UI can stay system
// agnostic.

#include "chip.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mnemos::frontend_sdk {
    class player_system;
}

namespace mnemos::debug {

    enum class debug_surface_kind : std::uint8_t {
        primary_frame,
        memory_space,
        register_bank,
        debug_layer
    };

    struct debug_surface_descriptor final {
        std::string id{};
        debug_surface_kind kind{debug_surface_kind::memory_space};
        std::string owner_id{};
        std::string display_name{};
        std::uint64_t byte_count{};
        std::uint32_t width{};
        std::uint32_t height{};
    };

    struct debug_memory_snapshot final {
        std::string id{};
        std::vector<std::uint8_t> bytes{};
    };

    struct debug_register_value final {
        std::string name{};
        std::uint64_t value{};
        std::uint8_t bit_width{};
        chips::register_value_format format{chips::register_value_format::unsigned_integer};
    };

    struct debug_register_snapshot final {
        std::string id{};
        std::vector<debug_register_value> registers{};
    };

    struct debug_frame_snapshot final {
        std::string id{};
        std::uint32_t width{};
        std::uint32_t height{};
        std::vector<std::uint32_t> pixels{};
    };

    class debug_session final {
      public:
        explicit debug_session(frontend_sdk::player_system& system) noexcept;

        [[nodiscard]] std::vector<debug_surface_descriptor> enumerate() const;

        [[nodiscard]] std::optional<debug_memory_snapshot>
        capture_memory(std::string_view id) const;

        [[nodiscard]] std::optional<debug_register_snapshot>
        capture_registers(std::string_view id) const;

        [[nodiscard]] std::optional<debug_frame_snapshot>
        capture_frame(std::string_view id) const;

      private:
        frontend_sdk::player_system* system_{};
    };

} // namespace mnemos::debug
