#include "system_family.hpp"

#include "string.hpp"

namespace mnemos::apps::player::adapters {

    std::optional<system_family> family_from_name(const std::string& name) noexcept {
        const std::string id = mnemos::common::to_lower(name);
        if (id == "genesis") {
            return system_family::genesis;
        }
        if (id == "sms") {
            return system_family::sms;
        }
        if (id == "gg") {
            return system_family::gg;
        }
        if (id == "c64") {
            return system_family::c64;
        }
        if (id == "segacd") {
            return system_family::segacd;
        }
        if (id == "sega32x") {
            return system_family::sega32x;
        }
        if (id == "irem_m72") {
            return system_family::irem_m72;
        }
        if (id == "cps1") {
            return system_family::capcom_cps1;
        }
        if (id == "cps2") {
            return system_family::capcom_cps2;
        }
        if (id == "spectrum") {
            return system_family::spectrum;
        }
        if (id == "nes") {
            return system_family::nes;
        }
        return std::nullopt;
    }

    const char* family_id(system_family family) noexcept {
        switch (family) {
        case system_family::sms:
            return "sms";
        case system_family::gg:
            return "gg";
        case system_family::c64:
            return "c64";
        case system_family::segacd:
            return "segacd";
        case system_family::sega32x:
            return "sega32x";
        case system_family::irem_m72:
            return "irem_m72";
        case system_family::capcom_cps1:
            return "cps1";
        case system_family::capcom_cps2:
            return "cps2";
        case system_family::spectrum:
            return "spectrum";
        case system_family::nes:
            return "nes";
        case system_family::genesis:
            break;
        }
        return "genesis";
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
        case system_family::sega32x:
            return "32X";
        case system_family::irem_m72:
            return "Irem M72";
        case system_family::capcom_cps1:
            return "CPS1";
        case system_family::capcom_cps2:
            return "CPS2";
        case system_family::spectrum:
            return "ZX Spectrum";
        case system_family::nes:
            return "NES";
        case system_family::genesis:
            break;
        }
        return "Genesis";
    }

    const char* family_names() noexcept {
        return "genesis, sms, gg, c64, segacd, sega32x, irem_m72, cps1, cps2, spectrum, nes";
    }

} // namespace mnemos::apps::player::adapters
