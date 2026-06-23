#pragma once

namespace mnemos::apps::player {

    // Retains every statically linked adapter so its registration side effect is
    // available before player_system construction.
    void force_link_all_adapters() noexcept;

} // namespace mnemos::apps::player
