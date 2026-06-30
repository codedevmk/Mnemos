#include "msx_mouse_input.hpp"

#include <algorithm>
#include <limits>

namespace mnemos::apps::player::adapters {

    namespace {
        [[nodiscard]] std::int16_t clamp_i16_delta(int value) noexcept {
            return static_cast<std::int16_t>(
                std::clamp(value, static_cast<int>(std::numeric_limits<std::int16_t>::min()),
                           static_cast<int>(std::numeric_limits<std::int16_t>::max())));
        }
    } // namespace

    msx_mouse_delta msx_mouse_tracker::apply(
        const peripheral::controller_state& state) noexcept {
        msx_mouse_delta delta{
            .left_button = state.trigger,
            .right_button = state.a || state.b,
        };

        if (state.aim_x < 0 || state.aim_y < 0) {
            position_valid_ = false;
            return delta;
        }

        if (position_valid_) {
            delta.x = clamp_i16_delta(static_cast<int>(state.aim_x) - static_cast<int>(x_));
            delta.y = clamp_i16_delta(static_cast<int>(state.aim_y) - static_cast<int>(y_));
        }
        position_valid_ = true;
        x_ = state.aim_x;
        y_ = state.aim_y;
        return delta;
    }

    void msx_mouse_tracker::reset() noexcept {
        position_valid_ = false;
        x_ = -1;
        y_ = -1;
    }

    void msx_mouse_tracker::save_state(chips::state_writer& writer) const {
        writer.boolean(position_valid_);
        writer.u16(static_cast<std::uint16_t>(x_));
        writer.u16(static_cast<std::uint16_t>(y_));
    }

    void msx_mouse_tracker::load_state(chips::state_reader& reader) noexcept {
        position_valid_ = reader.boolean();
        x_ = static_cast<std::int16_t>(reader.u16());
        y_ = static_cast<std::int16_t>(reader.u16());
        if (!reader.ok()) {
            reset();
        }
    }

} // namespace mnemos::apps::player::adapters
