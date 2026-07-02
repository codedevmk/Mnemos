#include "presentation_math.hpp"

#include <algorithm>

namespace mnemos::apps::player {

    presentation_rect integer_letterbox(int target_w, int target_h, int src_w,
                                        int src_h) noexcept {
        if (src_w <= 0 || src_h <= 0 || target_w <= 0 || target_h <= 0) {
            return {0, 0, target_w, target_h};
        }
        const int scale_w = target_w / src_w;
        const int scale_h = target_h / src_h;
        int scale = std::min(scale_w, scale_h);
        if (scale < 1) {
            scale = 1;
        }
        const int dst_w = src_w * scale;
        const int dst_h = src_h * scale;
        return {
            .x = (target_w - dst_w) / 2,
            .y = (target_h - dst_h) / 2,
            .w = dst_w,
            .h = dst_h,
        };
    }

    frame_point_projection project_window_point_to_frame(float window_x, float window_y,
                                                         int window_w, int window_h, int target_w,
                                                         int target_h, int frame_w,
                                                         int frame_h) noexcept {
        if (target_w <= 0) {
            target_w = window_w;
        }
        if (target_h <= 0) {
            target_h = window_h;
        }

        frame_point_projection out{
            .rect = integer_letterbox(target_w, target_h, frame_w, frame_h),
        };
        if (window_w <= 0 || window_h <= 0 || target_w <= 0 || target_h <= 0 || frame_w <= 0 ||
            frame_h <= 0 || out.rect.w <= 0 || out.rect.h <= 0) {
            return out;
        }

        const double pixel_x =
            static_cast<double>(window_x) * static_cast<double>(target_w) /
            static_cast<double>(window_w);
        const double pixel_y =
            static_cast<double>(window_y) * static_cast<double>(target_h) /
            static_cast<double>(window_h);
        if (pixel_x < static_cast<double>(out.rect.x) ||
            pixel_y < static_cast<double>(out.rect.y) ||
            pixel_x >= static_cast<double>(out.rect.x + out.rect.w) ||
            pixel_y >= static_cast<double>(out.rect.y + out.rect.h)) {
            return out;
        }

        out.inside = true;
        out.frame_x = static_cast<int>((pixel_x - static_cast<double>(out.rect.x)) *
                                       static_cast<double>(frame_w) /
                                       static_cast<double>(out.rect.w));
        out.frame_y = static_cast<int>((pixel_y - static_cast<double>(out.rect.y)) *
                                       static_cast<double>(frame_h) /
                                       static_cast<double>(out.rect.h));
        out.frame_x = std::clamp(out.frame_x, 0, frame_w - 1);
        out.frame_y = std::clamp(out.frame_y, 0, frame_h - 1);
        return out;
    }

    double window_delta_to_frame_delta(double window_delta, int window_extent, int target_extent,
                                       int frame_extent, int rect_extent) noexcept {
        if (target_extent <= 0) {
            target_extent = window_extent;
        }
        if (window_extent <= 0 || target_extent <= 0 || frame_extent <= 0 || rect_extent <= 0) {
            return 0.0;
        }
        return window_delta * static_cast<double>(target_extent) /
               static_cast<double>(window_extent) * static_cast<double>(frame_extent) /
               static_cast<double>(rect_extent);
    }

    double frame_delta_to_window_delta(int frame_delta, int window_extent, int target_extent,
                                       int frame_extent, int rect_extent) noexcept {
        if (target_extent <= 0) {
            target_extent = window_extent;
        }
        if (window_extent <= 0 || target_extent <= 0 || frame_extent <= 0 || rect_extent <= 0) {
            return 0.0;
        }
        return static_cast<double>(frame_delta) * static_cast<double>(window_extent) *
               static_cast<double>(rect_extent) /
               (static_cast<double>(target_extent) * static_cast<double>(frame_extent));
    }

} // namespace mnemos::apps::player
