#pragma once

// Named-callback registry the manifest builder hands to each chip during
// configuration. Manifests reference callbacks by string ID via the
// [chip.config] table (e.g. `irq_ack = "genesis.vdp_irq_ack"`); the host
// (player binary, runtime CLI, tests) supplies a callback_table whose
// entries are typed std::function variants matching the signatures chips
// currently need.
//
// Adding a new callback signature: extend the variant. A chip that uses
// a new signature looks it up via find_callback<Sig>(...).
//
// Tier 2 placement so chips (also tier 2) can consume the same types the
// manifest layer (tier 4) and the host (tier 7) populate. Mirrors the
// config_table tier rationale.

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>

namespace mnemos::chips {

    // Common chip callback signatures. Order matters only for variant index
    // stability across the codebase; add NEW alternatives at the end.
    using callback_value = std::variant<
        // void(int level): IRQ ack / IRQ assertion / delayed-IRQ raise.
        std::function<void(int)>,
        // void(std::uint32_t addr): TAS-style suppression hook with address.
        std::function<void(std::uint32_t)>,
        // std::uint16_t(std::uint32_t addr): word read from a host-supplied
        // memory space (e.g. genesis_vdp's 68K->VRAM DMA reads).
        std::function<std::uint16_t(std::uint32_t)>,
        // void(bool): boolean edge signals such as VBLANK transitions.
        std::function<void(bool)>>;

    using callback_table = std::unordered_map<std::string, callback_value>;

    // Typed lookup. Returns a pointer to the held std::function if `name` is
    // present AND its variant alternative matches the requested signature;
    // returns nullptr otherwise (missing name OR signature mismatch).
    //
    // A chip that needs an installed callback at configure() time does:
    //   if (auto* fn = chips::find_callback<void(int)>(callbacks, "vdp_irq_ack")) {
    //       set_irq_ack_callback(*fn);
    //   }
    // Mismatches are silent (returns nullptr); the chip falls back to its
    // built-in default.
    template <class Sig>
    [[nodiscard]] const std::function<Sig>*
    find_callback(const callback_table& cbs, std::string_view name) noexcept {
        const auto it = cbs.find(std::string{name});
        if (it == cbs.end()) {
            return nullptr;
        }
        return std::get_if<std::function<Sig>>(&it->second);
    }

} // namespace mnemos::chips
