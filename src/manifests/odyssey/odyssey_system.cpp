#include "odyssey_system.hpp"

#include <algorithm>
#include <bit>
#include <cmath>

namespace mnemos::manifests::odyssey {

    namespace {
        constexpr std::uint8_t black_luma{0U};
        constexpr std::uint8_t white_luma{255U};
        constexpr std::uint32_t k_state_version = 1U;

        [[nodiscard]] float clamp_unit(float value) noexcept {
            return std::clamp(value, -1.0F, 1.0F);
        }

        [[nodiscard]] std::int16_t rounded(float value) noexcept {
            return static_cast<std::int16_t>(std::lround(value));
        }

        [[nodiscard]] std::uint16_t encode_i16(std::int16_t value) noexcept {
            return static_cast<std::uint16_t>(value);
        }

        [[nodiscard]] std::int16_t decode_i16(std::uint16_t value) noexcept {
            const auto widened = static_cast<std::int32_t>(value);
            return static_cast<std::int16_t>(widened <= 0x7FFF ? widened : widened - 0x10000);
        }

        void write_sprite(chips::state_writer& writer, const odyssey_sprite& sprite) {
            writer.u16(encode_i16(sprite.x));
            writer.u16(encode_i16(sprite.y));
            writer.u8(sprite.w);
            writer.u8(sprite.h);
            writer.boolean(sprite.visible);
        }

        [[nodiscard]] odyssey_sprite read_sprite(chips::state_reader& reader) noexcept {
            odyssey_sprite sprite{};
            sprite.x = decode_i16(reader.u16());
            sprite.y = decode_i16(reader.u16());
            sprite.w = reader.u8();
            sprite.h = reader.u8();
            sprite.visible = reader.boolean();
            return sprite;
        }

        void write_controller(chips::state_writer& writer, const odyssey_controller& controller) {
            writer.u32(std::bit_cast<std::uint32_t>(controller.horizontal));
            writer.u32(std::bit_cast<std::uint32_t>(controller.vertical));
            writer.u32(std::bit_cast<std::uint32_t>(controller.english));
            writer.boolean(controller.reset);
        }

        [[nodiscard]] odyssey_controller read_controller(chips::state_reader& reader) noexcept {
            odyssey_controller controller{};
            controller.horizontal = std::bit_cast<float>(reader.u32());
            controller.vertical = std::bit_cast<float>(reader.u32());
            controller.english = std::bit_cast<float>(reader.u32());
            controller.reset = reader.boolean();
            return controller;
        }
    } // namespace

    odyssey_system::odyssey_system(const odyssey_config& config) noexcept : config_(config) {
        config_.width = std::min<std::uint16_t>(config_.width, 320U);
        config_.height = std::min<std::uint16_t>(config_.height, 240U);
        frame_.width = config_.width;
        frame_.height = config_.height;
        set_card(is_valid_card(config_.card) ? config_.card : odyssey_card::table_tennis);
        reset();
    }

    bool odyssey_system::is_valid_card(odyssey_card card) noexcept {
        const auto value = static_cast<std::uint8_t>(card);
        return value >= static_cast<std::uint8_t>(odyssey_card::table_tennis) &&
               value <= static_cast<std::uint8_t>(odyssey_card::states);
    }

    odyssey_system::card_wiring odyssey_system::wiring_for(odyssey_card card) noexcept {
        switch (card) {
        case odyssey_card::table_tennis:
            return {.line_visible = true, .ball_visible = true, .line_h = 190U};
        case odyssey_card::ski:
            return {.line_visible = false, .ball_visible = false, .collision_reverses_x = false};
        case odyssey_card::simon_says:
            return {.line_visible = false, .ball_visible = false, .collision_reverses_x = false};
        case odyssey_card::tennis:
            return {.line_visible = true, .ball_visible = true, .ball_dx = 3, .ball_dy = 1, .line_h = 160U};
        case odyssey_card::analogic:
            return {.line_visible = false, .ball_visible = true, .collision_reverses_x = false, .ball_dx = 1, .ball_dy = 1};
        case odyssey_card::hockey:
            return {.line_visible = true, .ball_visible = true, .ball_dx = 3, .ball_dy = 2, .line_h = 120U};
        case odyssey_card::football:
            return {.line_visible = true, .ball_visible = true, .ball_dx = 2, .ball_dy = 2, .line_h = 96U};
        case odyssey_card::cat_and_mouse:
            return {.line_visible = false, .ball_visible = false, .collision_reverses_x = false};
        case odyssey_card::haunted_house:
            return {.line_visible = false, .ball_visible = false, .collision_reverses_x = false};
        case odyssey_card::submarine:
            return {.line_visible = false, .ball_visible = true, .player1_visible = false, .collision_reverses_x = false, .ball_dx = 1, .ball_dy = 0};
        case odyssey_card::roulette:
            return {.line_visible = false, .ball_visible = false, .player0_visible = false, .player1_visible = false, .collision_reverses_x = false};
        case odyssey_card::states:
            return {.line_visible = false, .ball_visible = false, .collision_reverses_x = false};
        }
        return {};
    }

    void odyssey_system::set_card(odyssey_card card) noexcept {
        if (!is_valid_card(card)) {
            return;
        }
        card_ = card;
        wiring_ = wiring_for(card_);
        wiring_.line_visible = wiring_.line_visible && config_.visible_center_line;
        wiring_.ball_visible = wiring_.ball_visible && config_.visible_ball;
        player0_.visible = wiring_.player0_visible;
        player1_.visible = wiring_.player1_visible;
        ball_.visible = wiring_.ball_visible;
        ball_dx_ = wiring_.ball_dx;
        ball_dy_ = wiring_.ball_dy;
        line_h_ = wiring_.line_h;
        line_x_ = static_cast<std::int16_t>(config_.width / 2U);
    }

    void odyssey_system::set_controller(std::size_t port, const odyssey_controller& controller) noexcept {
        if (port < controllers_.size()) {
            controllers_[port] = {
                .horizontal = clamp_unit(controller.horizontal),
                .vertical = clamp_unit(controller.vertical),
                .english = clamp_unit(controller.english),
                .reset = controller.reset,
            };
        }
    }

    void odyssey_system::set_master_line(float position, float height) noexcept {
        line_x_ = map_x(position);
        const auto h = static_cast<float>(config_.height) * ((clamp_unit(height) + 1.0F) * 0.5F);
        line_h_ = static_cast<std::uint16_t>(std::clamp(rounded(h), std::int16_t{8}, static_cast<std::int16_t>(config_.height)));
    }

    void odyssey_system::set_ball_speed(float speed) noexcept {
        const auto magnitude = std::clamp(rounded((clamp_unit(speed) + 1.0F) * 2.0F), std::int16_t{1}, std::int16_t{4});
        ball_dx_ = static_cast<std::int16_t>((ball_dx_ < 0) ? -magnitude : magnitude);
    }

    void odyssey_system::reset() noexcept {
        player0_ = {.x = map_x(-0.82F), .y = map_y(0.0F), .w = 6U, .h = 6U, .visible = wiring_.player0_visible};
        player1_ = {.x = map_x(0.82F), .y = map_y(0.0F), .w = 6U, .h = 6U, .visible = wiring_.player1_visible};
        ball_ = {.x = map_x(0.0F), .y = map_y(0.0F), .w = 4U, .h = 4U, .visible = wiring_.ball_visible};
        ball_dx_ = wiring_.ball_dx;
        ball_dy_ = wiring_.ball_dy;
        frame_counter_ = 0U;
    }

    std::int16_t odyssey_system::map_x(float value) const noexcept {
        const auto max_x = static_cast<float>(config_.width > 8U ? config_.width - 8U : 0U);
        return rounded((clamp_unit(value) + 1.0F) * 0.5F * max_x);
    }

    std::int16_t odyssey_system::map_y(float value) const noexcept {
        const auto max_y = static_cast<float>(config_.height > 8U ? config_.height - 8U : 0U);
        return rounded((1.0F - ((clamp_unit(value) + 1.0F) * 0.5F)) * max_y);
    }

    void odyssey_system::apply_controller_positions() noexcept {
        player0_.x = map_x(controllers_[0].horizontal);
        player0_.y = map_y(controllers_[0].vertical);
        player1_.x = map_x(controllers_[1].horizontal);
        player1_.y = map_y(controllers_[1].vertical);
        if (controllers_[0].reset || controllers_[1].reset) {
            ball_.visible = wiring_.ball_visible;
            if (wiring_.reset_centers_ball) {
                ball_.x = map_x(0.0F);
                ball_.y = map_y(0.0F);
            }
        }
    }

    void odyssey_system::integrate_ball() noexcept {
        if (!ball_.visible) {
            return;
        }

        const auto english = rounded((controllers_[0].english + controllers_[1].english) * 0.5F);
        ball_.x = static_cast<std::int16_t>(ball_.x + ball_dx_);
        ball_.y = static_cast<std::int16_t>(ball_.y + ball_dy_ + english);

        if (ball_.y <= 0 || ball_.y + static_cast<std::int16_t>(ball_.h) >= static_cast<std::int16_t>(config_.height)) {
            ball_dy_ = static_cast<std::int16_t>(-ball_dy_);
        }

        const auto hit_player0 = ball_.x <= player0_.x + static_cast<std::int16_t>(player0_.w) &&
                                 ball_.y + static_cast<std::int16_t>(ball_.h) >= player0_.y &&
                                 ball_.y <= player0_.y + static_cast<std::int16_t>(player0_.h) && player0_.visible;
        const auto hit_player1 = ball_.x + static_cast<std::int16_t>(ball_.w) >= player1_.x &&
                                 ball_.y + static_cast<std::int16_t>(ball_.h) >= player1_.y &&
                                 ball_.y <= player1_.y + static_cast<std::int16_t>(player1_.h) && player1_.visible;
        if (wiring_.collision_reverses_x && (hit_player0 || hit_player1)) {
            ball_dx_ = static_cast<std::int16_t>(-ball_dx_);
        }

        if (ball_.x < 0 || ball_.x >= static_cast<std::int16_t>(config_.width)) {
            ball_.visible = false;
        }
    }

    void odyssey_system::tick_frame() noexcept {
        apply_controller_positions();
        integrate_ball();
        ++frame_counter_;
    }

    void odyssey_system::draw_rect(odyssey_sprite sprite) noexcept {
        if (!sprite.visible) {
            return;
        }
        for (std::uint8_t y = 0U; y < sprite.h; ++y) {
            const auto py = static_cast<std::int16_t>(sprite.y + static_cast<std::int16_t>(y));
            if (py < 0 || py >= static_cast<std::int16_t>(frame_.height)) {
                continue;
            }
            for (std::uint8_t x = 0U; x < sprite.w; ++x) {
                const auto px = static_cast<std::int16_t>(sprite.x + static_cast<std::int16_t>(x));
                if (px < 0 || px >= static_cast<std::int16_t>(frame_.width)) {
                    continue;
                }
                frame_.luma[static_cast<std::size_t>(py) * frame_.width + static_cast<std::size_t>(px)] = white_luma;
            }
        }
    }

    void odyssey_system::draw_center_line() noexcept {
        if (!wiring_.line_visible) {
            return;
        }
        const auto half_h = static_cast<std::int16_t>(line_h_ / 2U);
        const auto center_y = static_cast<std::int16_t>(frame_.height / 2U);
        for (std::int16_t y = static_cast<std::int16_t>(center_y - half_h); y < static_cast<std::int16_t>(center_y + half_h); ++y) {
            if (y < 0 || y >= static_cast<std::int16_t>(frame_.height)) {
                continue;
            }
            for (std::int16_t x = line_x_; x < static_cast<std::int16_t>(line_x_ + 2); ++x) {
                if (x < 0 || x >= static_cast<std::int16_t>(frame_.width)) {
                    continue;
                }
                frame_.luma[static_cast<std::size_t>(y) * frame_.width + static_cast<std::size_t>(x)] = white_luma;
            }
        }
    }

    const odyssey_frame& odyssey_system::render_frame() noexcept {
        std::ranges::fill(frame_.luma, black_luma);
        draw_center_line();
        draw_rect(player0_);
        draw_rect(player1_);
        draw_rect(ball_);
        return frame_;
    }

    void odyssey_system::save_state(chips::state_writer& writer) const {
        writer.u32(k_state_version);
        writer.u8(static_cast<std::uint8_t>(card_));
        writer.u64(frame_counter_);
        write_sprite(writer, player0_);
        write_sprite(writer, player1_);
        write_sprite(writer, ball_);
        writer.u16(encode_i16(ball_dx_));
        writer.u16(encode_i16(ball_dy_));
        writer.u16(encode_i16(line_x_));
        writer.u16(line_h_);
        write_controller(writer, controllers_[0]);
        write_controller(writer, controllers_[1]);
    }

    void odyssey_system::load_state(chips::state_reader& reader) noexcept {
        if (reader.u32() != k_state_version) {
            reader.fail();
            return;
        }

        const auto decoded_card = static_cast<odyssey_card>(reader.u8());
        const auto decoded_frame = reader.u64();
        const auto decoded_player0 = read_sprite(reader);
        const auto decoded_player1 = read_sprite(reader);
        const auto decoded_ball = read_sprite(reader);
        const auto decoded_ball_dx = decode_i16(reader.u16());
        const auto decoded_ball_dy = decode_i16(reader.u16());
        const auto decoded_line_x = decode_i16(reader.u16());
        const auto decoded_line_h = reader.u16();
        const auto decoded_controller0 = read_controller(reader);
        const auto decoded_controller1 = read_controller(reader);

        if (!reader.ok() || !is_valid_card(decoded_card)) {
            reader.fail();
            return;
        }

        set_card(decoded_card);
        frame_counter_ = decoded_frame;
        player0_ = decoded_player0;
        player1_ = decoded_player1;
        ball_ = decoded_ball;
        ball_dx_ = decoded_ball_dx;
        ball_dy_ = decoded_ball_dy;
        line_x_ = decoded_line_x;
        line_h_ = std::min<std::uint16_t>(decoded_line_h, config_.height);
        controllers_[0] = decoded_controller0;
        controllers_[1] = decoded_controller1;
    }

    odyssey_observable_state odyssey_system::observable_state() const noexcept {
        return {
            .card = card_,
            .frame = frame_counter_,
            .player0 = player0_,
            .player1 = player1_,
            .ball = ball_,
            .line_x = line_x_,
            .line_h = line_h_,
            .line_visible = wiring_.line_visible,
            .sound_present = false,
            .cpu_present = false,
            .rom_present = false,
            .ram_present = false,
        };
    }

    odyssey_system assemble_odyssey(const odyssey_config& config) noexcept {
        return odyssey_system{config};
    }

} // namespace mnemos::manifests::odyssey
