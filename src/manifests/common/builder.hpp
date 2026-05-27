#pragma once

#include "manifest.hpp"

#include "bus.hpp"
#include "chip.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace mnemos::manifests {

    // Supplies ROM bytes for a manifest `file` path (host decides where from).
    // Returns nullopt when the file is unavailable.
    using rom_provider =
        std::function<std::optional<std::vector<std::uint8_t>>(std::string_view file)>;

    // Host-supplied named-callback registry. Each chip's `configure()` reads
    // callback IDs from its [chip.config] table and looks them up here.
    // See `src/chips/shared/callbacks.hpp` for the supported signatures.
    // Empty table = chips fall back to their built-in defaults.
    using callback_table = chips::callback_table;

    // The instantiated machine: chips created from the manifest, the buses they
    // attach to, and the owned RAM/ROM storage the bus regions point into.
    struct system_graph final {
        std::vector<std::unique_ptr<chips::ichip>> chips;
        std::vector<std::unique_ptr<topology::bus>> buses;
        std::vector<std::unique_ptr<std::vector<std::uint8_t>>> memory;
        std::unordered_map<std::string, chips::ichip*> chip_by_id;
        std::unordered_map<std::string, topology::bus*> bus_by_id;

        [[nodiscard]] chips::ichip* chip(std::string_view id) const;
        [[nodiscard]] topology::bus* bus(std::string_view id) const;
    };

    struct build_result final {
        std::optional<system_graph> value;
        std::vector<diagnostic> errors;

        [[nodiscard]] bool ok() const noexcept { return value.has_value() && errors.empty(); }
    };

    // Instantiate a system from a parsed manifest: create each chip by factory id,
    // build each bus, allocate RAM, load + SHA-256-verify ROMs, bind MMIO chip
    // windows, and attach CPUs to their buses.
    //
    // Region mapping uses generic defaults: RAM at base priority, ROM overlays as
    // read-only overlays, MMIO above both. Machine-specific banking (e.g. the C64
    // PLA gating which overlay wins) is applied by a separate machine wirer.
    //
    // `callbacks` (optional) is the host-supplied named-callback registry. The
    // builder hands each chip its [chip.config] table AND the callback table
    // simultaneously via `ichip::configure(cfg, callbacks)`; chips that don't
    // need callbacks ignore the parameter.
    [[nodiscard]] build_result build_system(const manifest& m, const rom_provider& roms,
                                            const callback_table& callbacks = {});

} // namespace mnemos::manifests
