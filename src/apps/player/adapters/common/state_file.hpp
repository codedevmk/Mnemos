#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace mnemos::apps::player::adapters {

    // Derive the quick-save path that sits beside a ROM: the ROM path with its
    // extension replaced by ".mns" (Mnemos save-state), or appended when absent.
    [[nodiscard]] std::string state_path_for(std::string_view rom_path);

    // Load a raw Mnemos save-state container. Missing or unreadable files return
    // nullopt; an empty file is returned as an empty vector so the caller can
    // reject it as a malformed state.
    [[nodiscard]] std::optional<std::vector<std::uint8_t>>
    load_save_state_file(const std::string& path);

    // Persist a raw save-state container atomically through a sibling temp file.
    // Empty states are rejected so a failed save cannot replace a valid one.
    [[nodiscard]] bool save_save_state_file(const std::string& path,
                                            std::span<const std::uint8_t> bytes);

} // namespace mnemos::apps::player::adapters
