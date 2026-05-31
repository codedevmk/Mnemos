#pragma once

// Process-wide registry of adapter factories keyed by system family name
// ("genesis", "sms", "c64", "32x", ...). Each adapter source file populates
// the registry at static-init time so the player binary can boot a system
// from a string ID without compile-time knowledge of which adapter types
// exist. Tier 7 (frontend_sdk) -- shared by the player executable and by
// any tool that wants to instantiate systems generically.
//
// LINKER NOTE: an adapter library that nothing else references can be
// discarded entirely by the linker, which silently disables its static-init
// self-registration. To prevent this, each adapter exposes a `force_link()`
// no-op function declared in its header; the main binary calls it once at
// startup. Adding a new adapter is: drop the adapter directory + add one
// force_link() call in main.cpp. main.cpp never names a concrete adapter
// type.

#include "player_system.hpp"
#include "region.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace mnemos::frontend_sdk {

    class scheduler_factory; // scheduler_factory.hpp

    // Inputs every adapter factory takes. Future adapters may need richer
    // config (CD-ROM image, BIOS path, manifest selection); extend this
    // struct rather than the factory signature so existing factories keep
    // working.
    struct adapter_options final {
        std::vector<std::uint8_t> rom;
        mnemos::video_region video_region{mnemos::video_region::ntsc};
        std::string display_name;
        // Optional scheduler-construction override. null = adapter falls back
        // to its built-in scheduler. Non-null lets tooling (deterministic
        // replay, profilers, slice-based multi-clock for 32X/Saturn/CD)
        // intercept scheduler construction without modifying adapter code.
        // Non-owning; caller keeps the factory alive for the adapter's
        // construction call.
        scheduler_factory* scheduler_factory{};
    };

    class adapter_registry final {
      public:
        using factory = std::function<std::unique_ptr<player_system>(adapter_options options)>;

        adapter_registry(const adapter_registry&) = delete;
        adapter_registry& operator=(const adapter_registry&) = delete;
        adapter_registry(adapter_registry&&) = delete;
        adapter_registry& operator=(adapter_registry&&) = delete;

        // The single, process-wide registry. Constructed lazily on first
        // access (no global-ordering issues for static-init self-registers).
        [[nodiscard]] static adapter_registry& instance();

        // Map a family ID ("genesis", "sms", ...) to a factory. Re-registering
        // the same family replaces the previous factory; not an error -- the
        // expected use is each adapter source registers its own family once
        // at static-init, and re-registration only happens in tests.
        void register_family(std::string family, factory fn);

        // Invoke the factory for `family`. Returns nullptr (without
        // constructing a system) if no adapter is registered under that name.
        [[nodiscard]] std::unique_ptr<player_system> create(std::string_view family,
                                                            adapter_options options) const;

        // Snapshot of currently-registered family IDs. Useful for diagnostic
        // listings ("supported systems: genesis, sms, ...").
        [[nodiscard]] std::vector<std::string> families() const;

      private:
        adapter_registry() = default;

        mutable std::mutex mu_{};
        std::unordered_map<std::string, factory> factories_{};
    };

} // namespace mnemos::frontend_sdk
