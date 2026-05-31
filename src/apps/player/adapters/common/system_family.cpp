#include "system_family.hpp"

#include "string.hpp"

namespace mnemos::apps::player::adapters {

    system_family detect_family(const std::string& path) noexcept {
        const auto dot = path.find_last_of('.');
        if (dot == std::string::npos) {
            return system_family::genesis;
        }
        const std::string ext = mnemos::common::to_lower(path.substr(dot + 1));
        if (ext == "sms" || ext == "sg") {
            return system_family::sms;
        }
        return system_family::genesis;
    }

    const char* family_label(system_family family) noexcept {
        return family == system_family::sms ? "SMS" : "Genesis";
    }

} // namespace mnemos::apps::player::adapters
