#include "machine_save.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace mnemos::runtime {

    save_target build_save_target(const manifests::system_graph& graph, std::string manifest_id,
                                  std::uint32_t manifest_rev, std::uint64_t master_cycle,
                                  std::span<const std::string_view> ram_regions) {
        save_target target;
        target.manifest_id = std::move(manifest_id);
        target.manifest_rev = manifest_rev;
        target.master_cycle = master_cycle;

        // Chips in id order: chip_by_id is an unordered_map, so collect and sort the
        // ids first to keep the serialised chunk order -- and therefore the byte
        // stream -- deterministic. chip_by_id resolves to the original chip (the
        // inner of any gate wrapper), which is the concrete type whose state we save.
        std::vector<std::string> ids;
        ids.reserve(graph.chip_by_id.size());
        for (const auto& [id, chip] : graph.chip_by_id) {
            static_cast<void>(chip);
            ids.push_back(id);
        }
        std::sort(ids.begin(), ids.end());
        for (const std::string& id : ids) {
            chips::ichip* const chip = graph.chip(id);
            if (chip != nullptr) {
                target.chips.push_back(save_chip{.id = id, .chip = chip});
            }
        }

        // Named writable regions, in the caller's order. A name the graph does not
        // expose resolves to an empty span and is skipped, so a caller may list the
        // union of regions across machine variants.
        for (const std::string_view name : ram_regions) {
            const std::span<std::uint8_t> bytes = graph.region_span(name);
            if (!bytes.empty()) {
                target.memory.push_back(save_memory{.id = std::string(name), .bytes = bytes});
            }
        }

        return target;
    }

} // namespace mnemos::runtime
