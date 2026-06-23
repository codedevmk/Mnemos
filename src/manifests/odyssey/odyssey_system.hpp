#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace mnemos::manifests::odyssey {

    enum class odyssey_card : std::uint8_t {
        table_tennis = 1U,
        ski = 2U,
        simon_says = 3U,
        tennis = 4U,
        analogic = 5U,
        hockey = 6U,
        football = 7U,
        cat_and_mouse = 8U,
        haunted_house = 9U,
        submarine = 10U,
        roulette = 11U,
        states = 12U,
    };

    struct odyssey_config final {
        odyssey_card card{odyssey_card::table_tennis};
        std::uint16_t width{320U};
        std::uint16_t height{240U};
        bool visible_center_line{true};
        bool visible_ball{true};
    };

    struct odyssey_controller final {
        // Odyssey controllers are analog knob boxes: horizontal player dot,
        // vertical player dot, and English/curve influence for the ball path.
        // Values are normalized to [-1, 1] by the frontend/peripheral adapter.
        float horizontal{};
        float vertical{};
        float english{};
        bool reset{};
    };

    struct odyssey_sprite final {
        std::int16_t x{};
        std::int16_t y{};
        std::uint8_t w{4U};
        std::uint8_t h{4U};
        bool visible{true};
    };

    struct odyssey_frame final {
        std::uint16_t width{};
        std::uint16_t height{};
        // 1 byte per pixel, monochrome: 0 = black, 255 = white.
        std::array<std::uint8_t, 320U * 240U> luma{};
    };

    struct odyssey_observable_state final {
        odyssey_card card{};
        std::uint64_t frame{};
        odyssey_sprite player0{};
        odyssey_sprite player1{};
        odyssey_sprite ball{};
        std::int16_t line_x{};
        std::uint16_t line_h{};
        bool line_visible{};
        bool sound_present{};
        bool cpu_present{};
        bool rom_present{};
        bool ram_present{};
    };

    class odyssey_system final {
    public:
        explicit odyssey_system(const odyssey_config& config = {}) noexcept;

        void set_card(odyssey_card card) noexcept;
        void set_controller(std::size_t port, const odyssey_controller& controller) noexcept;
        void set_master_line(float position, float height) noexcept;
        void set_ball_speed(float speed) noexcept;

        void reset() noexcept;
        void tick_frame() noexcept;
        [[nodiscard]] const odyssey_frame& render_frame() noexcept;

        [[nodiscard]] odyssey_observable_state observable_state() const noexcept;
        [[nodiscard]] odyssey_card card() const noexcept { return card_; }
        [[nodiscard]] std::uint64_t frame_counter() const noexcept { return frame_counter_; }

    private:
        struct card_wiring final {
            bool line_visible{};
            bool ball_visible{};
            bool player0_visible{true};
            bool player1_visible{true};
            bool collision_reverses_x{true};
            bool reset_centers_ball{true};
            std::int16_t ball_dx{2};
            std::int16_t ball_dy{1};
            std::uint16_t line_h{180U};
        };

        [[nodiscard]] static card_wiring wiring_for(odyssey_card card) noexcept;
        [[nodiscard]] std::int16_t map_x(float value) const noexcept;
        [[nodiscard]] std::int16_t map_y(float value) const noexcept;
        void apply_controller_positions() noexcept;
        void integrate_ball() noexcept;
        void draw_rect(odyssey_sprite sprite) noexcept;
        void draw_center_line() noexcept;

        odyssey_config config_{};
        odyssey_card card_{};
        card_wiring wiring_{};
        std::array<odyssey_controller, 2> controllers_{};
        odyssey_sprite player0_{};
        odyssey_sprite player1_{};
        odyssey_sprite ball_{};
        std::int16_t ball_dx_{};
        std::int16_t ball_dy_{};
        std::int16_t line_x_{};
        std::uint16_t line_h_{};
        odyssey_frame frame_{};
        std::uint64_t frame_counter_{};
    };

    [[nodiscard]] odyssey_system assemble_odyssey(const odyssey_config& config = {}) noexcept;

} // namespace mnemos::manifests::odyssey
