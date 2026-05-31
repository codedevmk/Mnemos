#pragma once

// Per-chip configuration values supplied at construction time, originating
// in the system manifest (TOML [chip.config] tables) and consumed by each
// chip's `ichip::configure(...)` override. Tier 2 placement so chips
// (also tier 2) can consume the same types the manifest layer (tier 4)
// fills.
//
// Each chip ignores keys it doesn't recognize -- forward-compat for adding
// new config in newer manifest versions against older binaries.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>

namespace mnemos::chips {

    // The handful of scalar shapes a TOML scalar can take, plus string for
    // named callbacks / variant tags. Pick deliberately small so the variant
    // stays cheap to copy.
    using config_value = std::variant<bool, std::int64_t, double, std::string>;

    // Keyed bag of config values. Caller (the manifest builder) populates;
    // callee (each chip's `configure`) reads what it knows about. Empty
    // table is valid (chip uses its defaults).
    using config_table = std::unordered_map<std::string, config_value>;

    // ---- Small typed accessors so chips don't open-code std::get_if everywhere.

    [[nodiscard]] inline std::optional<bool> cfg_bool(const config_table& cfg,
                                                      std::string_view key) noexcept {
        const auto it = cfg.find(std::string{key});
        if (it == cfg.end()) {
            return std::nullopt;
        }
        if (const auto* v = std::get_if<bool>(&it->second)) {
            return *v;
        }
        return std::nullopt;
    }

    [[nodiscard]] inline std::optional<std::int64_t> cfg_int(const config_table& cfg,
                                                             std::string_view key) noexcept {
        const auto it = cfg.find(std::string{key});
        if (it == cfg.end()) {
            return std::nullopt;
        }
        if (const auto* v = std::get_if<std::int64_t>(&it->second)) {
            return *v;
        }
        return std::nullopt;
    }

    [[nodiscard]] inline std::optional<double> cfg_double(const config_table& cfg,
                                                          std::string_view key) noexcept {
        const auto it = cfg.find(std::string{key});
        if (it == cfg.end()) {
            return std::nullopt;
        }
        if (const auto* v = std::get_if<double>(&it->second)) {
            return *v;
        }
        return std::nullopt;
    }

    [[nodiscard]] inline std::optional<std::string_view> cfg_string(const config_table& cfg,
                                                                    std::string_view key) noexcept {
        const auto it = cfg.find(std::string{key});
        if (it == cfg.end()) {
            return std::nullopt;
        }
        if (const auto* v = std::get_if<std::string>(&it->second)) {
            return std::string_view{*v};
        }
        return std::nullopt;
    }

} // namespace mnemos::chips
