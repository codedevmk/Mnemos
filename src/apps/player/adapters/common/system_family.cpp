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
        if (ext == "gg") {
            return system_family::gg;
        }
        if (ext == "cue" || ext == "iso" || ext == "chd") {
            return system_family::segacd;
        }
        if (ext == "prg" || ext == "d64" || ext == "d71" || ext == "d81" || ext == "t64" ||
            ext == "tap" || ext == "crt" || ext == "g64" || ext == "p00") {
            return system_family::c64;
        }
        return system_family::genesis;
    }

    const char* family_label(system_family family) noexcept {
        switch (family) {
        case system_family::sms:
            return "SMS";
        case system_family::gg:
            return "Game Gear";
        case system_family::c64:
            return "C64";
        case system_family::segacd:
            return "Sega CD";
        case system_family::genesis:
            break;
        }
        return "Genesis";
    }

} // namespace mnemos::apps::player::adapters
