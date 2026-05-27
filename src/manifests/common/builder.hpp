#pragma once

#include "manifest.hpp"

#include "bus.hpp"
#include "chip.hpp"
#include "mmio_factory.hpp"
#include "predicates.hpp"

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

    // Host-supplied named-predicate registry. The builder consults this when
    // it wraps a chip per a manifest `[[gate]]` entry. Empty table is valid
    // as long as the manifest has no gates; a manifest gate referencing a
    // missing predicate produces a build-time diagnostic.
    using predicate_table = chips::predicate_table;

    // Host-supplied named-factory registry for system-specific MMIO blocks
    // declared via `[[mmio_block]]`. The builder calls each factory with the
    // block's base + size and binds the returned read/write handler pair
    // onto the named bus.
    using mmio_factory_table = chips::mmio_factory_table;

    // The instantiated machine: chips created from the manifest, the buses they
    // attach to, and the owned RAM/ROM storage the bus regions point into.
    //
    // `chips` is the SCHEDULER'S VIEW: for gated chips it holds the gated_chip
    // wrapper; for ungated chips it holds the chip directly. `chip_by_id`
    // resolves a manifest chip ID to the ORIGINAL chip (the inner of any
    // wrapper) so introspection / register queries / state access hit the
    // concrete chip type rather than the wrapper.
    //
    // `gated_originals` keeps the original chip unique_ptrs alive for chips
    // that got wrapped; the wrapper holds a raw ichip* into one of these.
    struct system_graph final {
        std::vector<std::unique_ptr<chips::ichip>> chips;
        std::vector<std::unique_ptr<chips::ichip>> gated_originals;
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
    //
    // `predicates` (optional) is the host-supplied named-predicate registry.
    // For each manifest `[[gate]]` entry the builder wraps the named chip in
    // a `gated_chip` whose tick is gated by the named predicate. The wrapper
    // replaces the chip in `system_graph::chips`; `system_graph::chip_by_id`
    // continues to resolve to the ORIGINAL chip so introspection and register
    // queries hit the right type.
    [[nodiscard]] build_result build_system(const manifest& m, const rom_provider& roms,
                                            const callback_table& callbacks = {},
                                            const predicate_table& predicates = {},
                                            const mmio_factory_table& mmio_factories = {});

} // namespace mnemos::manifests
