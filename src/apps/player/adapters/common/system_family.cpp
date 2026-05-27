#include "system_family.hpp"

#include <cctype>

namespace mnemos::apps::player::adapters {

    system_family detect_family(const std::string& path) noexcept {
        const auto dot = path.find_last_of('.');
        if (dot == std::string::npos) {
            return system_family::genesis;
        }
        std::string ext = path.substr(dot + 1);
        for (char& c : ext) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        if (ext == "sms" || ext == "sg") {
            return system_family::sms;
        }
        return system_family::genesis;
    }

    const char* family_label(system_family family) noexcept {
        return family == system_family::sms ? "SMS" : "Genesis";
    }

} // namespace mnemos::apps::player::adapters
