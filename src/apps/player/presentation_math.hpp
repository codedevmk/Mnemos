#pragma once

namespace mnemos::apps::player {

    struct presentation_rect {
        int x{};
        int y{};
        int w{};
        int h{};
    };

    struct frame_point_projection {
        presentation_rect rect{};
        bool inside{};
        int frame_x{-1};
        int frame_y{-1};
    };

    [[nodiscard]] presentation_rect integer_letterbox(int target_w, int target_h, int src_w,
                                                      int src_h) noexcept;

    [[nodiscard]] frame_point_projection project_window_point_to_frame(
        float window_x, float window_y, int window_w, int window_h, int target_w, int target_h,
        int frame_w, int frame_h) noexcept;

    [[nodiscard]] double window_delta_to_frame_delta(double window_delta, int window_extent,
                                                     int target_extent, int frame_extent,
                                                     int rect_extent) noexcept;

    [[nodiscard]] double frame_delta_to_window_delta(int frame_delta, int window_extent,
                                                     int target_extent, int frame_extent,
                                                     int rect_extent) noexcept;

} // namespace mnemos::apps::player
