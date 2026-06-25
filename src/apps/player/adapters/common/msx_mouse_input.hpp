#pragma once

#include "peripheral.hpp" // mnemos::peripheral::controller_state
#include "state.hpp"      // mnemos::chips::state_reader/state_writer

#include <cstdint>

namespace mnemos::apps::player::adapters {

    struct msx_mouse_delta final {
        std::int16_t x{};
        std::int16_t y{};
        bool left_button{};
        bool right_button{};
    };

    class msx_mouse_tracker final {
      public:
        [[nodiscard]] msx_mouse_delta
        apply(const peripheral::controller_state& state) noexcept;

        void reset() noexcept;
        void save_state(chips::state_writer& writer) const;
        void load_state(chips::state_reader& reader) noexcept;

      private:
        bool position_valid_{};
        std::int16_t x_{-1};
        std::int16_t y_{-1};
    };

} // namespace mnemos::apps::player::adapters
