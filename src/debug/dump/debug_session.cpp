#include "debug_session.hpp"

#include "introspection_views.hpp"
#include "path_id.hpp"
#include "player_system.hpp"

#include <algorithm>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace mnemos::debug {
    namespace {

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

        [[nodiscard]] debug_frame_snapshot copy_frame(std::string id,
                                                      const chips::frame_buffer_view view) {
            debug_frame_snapshot snapshot{
                .id = std::move(id),
                .width = view.width,
                .height = view.height,
            };
            if (view.pixels == nullptr || view.width == 0U || view.height == 0U) {
                return snapshot;
            }
            snapshot.pixels.resize(static_cast<std::size_t>(view.width) * view.height);
            const std::uint32_t stride = view.effective_stride();
            for (std::uint32_t y = 0U; y < view.height; ++y) {
                const std::uint32_t* src = view.pixels + static_cast<std::size_t>(y) * stride;
                std::uint32_t* dst = snapshot.pixels.data() +
                                     static_cast<std::size_t>(y) * view.width;
                std::copy_n(src, view.width, dst);
            }
            return snapshot;
        }

        [[nodiscard]] auto surface_order_key(const debug_surface_descriptor& descriptor) {
            return std::tuple{descriptor.kind, std::string_view{descriptor.id}};
        }

        void sort_surfaces(std::vector<debug_surface_descriptor>& surfaces) {
            std::sort(surfaces.begin(), surfaces.end(),
                      [](const debug_surface_descriptor& lhs,
                         const debug_surface_descriptor& rhs) {
                          return surface_order_key(lhs) < surface_order_key(rhs);
                      });
        }

    } // namespace

    debug_session::debug_session(frontend_sdk::player_system& system) noexcept
        : system_(&system) {}

    std::vector<debug_surface_descriptor> debug_session::enumerate() const {
        std::vector<debug_surface_descriptor> surfaces;
        if (system_ == nullptr) {
            return surfaces;
        }

        const chips::frame_buffer_view primary = system_->current_frame();
        surfaces.push_back({.id = "video.primary",
                            .kind = debug_surface_kind::primary_frame,
                            .owner_id = "system",
                            .display_name = "Primary Frame",
                            .width = primary.width,
                            .height = primary.height});

        std::size_t system_memory_index = 0U;
        for (instrumentation::memory_view* view : system_->memory_views()) {
            if (view == nullptr) {
                continue;
            }
            const std::string view_id =
                stable_id_segment(view->name(), "memory_" + std::to_string(system_memory_index));
            const auto bytes = view->bytes();
            surfaces.push_back({.id = "memory.system." + view_id,
                                .kind = debug_surface_kind::memory_space,
                                .owner_id = "system",
                                .display_name = std::string{view->name()},
                                .byte_count = bytes.size()});
            ++system_memory_index;
        }

        const auto chips = system_->chips();
        const std::vector<std::string> chip_ids = stable_chip_ids(chips);
        for (std::size_t chip_index = 0U; chip_index < chips.size(); ++chip_index) {
            chips::ichip* chip = chips[chip_index];
            if (chip == nullptr) {
                continue;
            }
            const std::string& chip_id = chip_ids[chip_index];
            instrumentation::ichip_introspection& intro = chip->introspection();

            std::size_t memory_index = 0U;
            for (instrumentation::memory_view* view : intro.memory_views()) {
                if (view == nullptr) {
                    continue;
                }
                const std::string view_id =
                    stable_id_segment(view->name(), "memory_" + std::to_string(memory_index));
                const auto bytes = view->bytes();
                surfaces.push_back({.id = "memory." + chip_id + "." + view_id,
                                    .kind = debug_surface_kind::memory_space,
                                    .owner_id = chip_id,
                                    .display_name = std::string{view->name()},
                                    .byte_count = bytes.size()});
                ++memory_index;
            }

            if (intro.registers() != nullptr) {
                surfaces.push_back({.id = "register_bank." + chip_id,
                                    .kind = debug_surface_kind::register_bank,
                                    .owner_id = chip_id,
                                    .display_name = chip_id + " registers"});
            }

            std::size_t layer_index = 0U;
            for (instrumentation::debug_layer* layer : intro.debug_layers()) {
                if (layer == nullptr) {
                    continue;
                }
                const std::string layer_id =
                    stable_id_segment(layer->name(), "layer_" + std::to_string(layer_index));
                const chips::frame_buffer_view view = layer->view();
                surfaces.push_back({.id = "debug_layer." + chip_id + "." + layer_id,
                                    .kind = debug_surface_kind::debug_layer,
                                    .owner_id = chip_id,
                                    .display_name = std::string{layer->name()},
                                    .width = view.width,
                                    .height = view.height});
                ++layer_index;
            }
        }

        sort_surfaces(surfaces);
        return surfaces;
    }

    std::optional<debug_memory_snapshot> debug_session::capture_memory(std::string_view id) const {
        if (system_ == nullptr) {
            return std::nullopt;
        }

        std::size_t system_memory_index = 0U;
        for (instrumentation::memory_view* view : system_->memory_views()) {
            if (view == nullptr) {
                continue;
            }
            const std::string view_id =
                stable_id_segment(view->name(), "memory_" + std::to_string(system_memory_index));
            const std::string surface_id = "memory.system." + view_id;
            if (surface_id == id) {
                const auto bytes = view->bytes();
                return debug_memory_snapshot{
                    .id = surface_id,
                    .bytes = std::vector<std::uint8_t>(bytes.begin(), bytes.end())};
            }
            ++system_memory_index;
        }

        const auto chips = system_->chips();
        const std::vector<std::string> chip_ids = stable_chip_ids(chips);
        for (std::size_t chip_index = 0U; chip_index < chips.size(); ++chip_index) {
            chips::ichip* chip = chips[chip_index];
            if (chip == nullptr) {
                continue;
            }
            instrumentation::ichip_introspection& intro = chip->introspection();
            std::size_t memory_index = 0U;
            for (instrumentation::memory_view* view : intro.memory_views()) {
                if (view == nullptr) {
                    continue;
                }
                const std::string view_id =
                    stable_id_segment(view->name(), "memory_" + std::to_string(memory_index));
                const std::string surface_id = "memory." + chip_ids[chip_index] + "." + view_id;
                if (surface_id == id) {
                    const auto bytes = view->bytes();
                    return debug_memory_snapshot{
                        .id = surface_id,
                        .bytes = std::vector<std::uint8_t>(bytes.begin(), bytes.end())};
                }
                ++memory_index;
            }
        }
        return std::nullopt;
    }

    std::optional<debug_register_snapshot>
    debug_session::capture_registers(std::string_view id) const {
        if (system_ == nullptr) {
            return std::nullopt;
        }

        const auto chips = system_->chips();
        const std::vector<std::string> chip_ids = stable_chip_ids(chips);
        for (std::size_t chip_index = 0U; chip_index < chips.size(); ++chip_index) {
            chips::ichip* chip = chips[chip_index];
            if (chip == nullptr) {
                continue;
            }
            const std::string surface_id = "register_bank." + chip_ids[chip_index];
            if (surface_id != id) {
                continue;
            }
            instrumentation::register_view* registers = chip->introspection().registers();
            if (registers == nullptr) {
                return std::nullopt;
            }
            debug_register_snapshot snapshot{.id = surface_id};
            const auto live = registers->registers();
            snapshot.registers.reserve(live.size());
            for (const chips::register_descriptor& reg : live) {
                snapshot.registers.push_back({.name = std::string{reg.name},
                                              .value = reg.value,
                                              .bit_width = reg.bit_width,
                                              .format = reg.format});
            }
            return snapshot;
        }
        return std::nullopt;
    }

    std::optional<debug_frame_snapshot> debug_session::capture_frame(std::string_view id) const {
        if (system_ == nullptr) {
            return std::nullopt;
        }

        if (id == "video.primary") {
            return copy_frame("video.primary", system_->current_frame());
        }

        const auto chips = system_->chips();
        const std::vector<std::string> chip_ids = stable_chip_ids(chips);
        for (std::size_t chip_index = 0U; chip_index < chips.size(); ++chip_index) {
            chips::ichip* chip = chips[chip_index];
            if (chip == nullptr) {
                continue;
            }
            instrumentation::ichip_introspection& intro = chip->introspection();
            std::size_t layer_index = 0U;
            for (instrumentation::debug_layer* layer : intro.debug_layers()) {
                if (layer == nullptr) {
                    continue;
                }
                const std::string layer_id =
                    stable_id_segment(layer->name(), "layer_" + std::to_string(layer_index));
                const std::string surface_id =
                    "debug_layer." + chip_ids[chip_index] + "." + layer_id;
                if (surface_id == id) {
                    return copy_frame(surface_id, layer->view());
                }
                ++layer_index;
            }
        }
        return std::nullopt;
    }

} // namespace mnemos::debug
