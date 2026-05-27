#include "region_args.hpp"

#include <string>

namespace mnemos::apps::player::adapters {

    namespace {
        [[nodiscard]] bool ieq(std::string_view a, std::string_view b) noexcept {
            if (a.size() != b.size()) {
                return false;
            }
            for (std::size_t i = 0; i < a.size(); ++i) {
                const char ca = a[i];
                const char cb = b[i];
                const char la = (ca >= 'A' && ca <= 'Z') ? static_cast<char>(ca + 32) : ca;
                const char lb = (cb >= 'A' && cb <= 'Z') ? static_cast<char>(cb + 32) : cb;
                if (la != lb) {
                    return false;
                }
            }
            return true;
        }
    } // namespace

    region_override parse_region_arg(int argc, char* argv[]) {
        for (int i = 1; i < argc - 1; ++i) {
            const std::string_view a{argv[i]};
            if (a != "--region") {
                continue;
            }
            const std::string_view v{argv[i + 1]};
            if (ieq(v, "pal")) {
                return region_override::pal;
            }
            if (ieq(v, "ntsc")) {
                return region_override::ntsc;
            }
        }
        return region_override::auto_detect;
    }

    mnemos::video_region resolve_video_region(region_override ov,
                                              mnemos::video_region cart_default) noexcept {
        switch (ov) {
        case region_override::pal:
            return mnemos::video_region::pal;
        case region_override::ntsc:
            return mnemos::video_region::ntsc;
        case region_override::auto_detect:
        default:
            return cart_default;
        }
    }

    const char* region_source_label(region_override ov) noexcept {
        return ov == region_override::auto_detect ? "auto-detected" : "explicit --region";
    }

} // namespace mnemos::apps::player::adapters
