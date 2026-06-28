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
        if (id == "irem_m15" || id == "m15") {
            return system_family::irem_m15;
        }
        if (id == "irem_m52" || id == "m52") {
            return system_family::irem_m52;
        }
        if (id == "irem_m58" || id == "m58") {
            return system_family::irem_m58;
        }
        if (id == "irem_m72") {
            return system_family::irem_m72;
        }
        if (id == "irem_m75" || id == "m75") {
            return system_family::irem_m75;
        }
        if (id == "irem_m81" || id == "m81") {
            return system_family::irem_m81;
        }
        if (id == "irem_m82" || id == "m82") {
            return system_family::irem_m82;
        }
        if (id == "irem_m84" || id == "m84") {
            return system_family::irem_m84;
        }
        if (id == "irem_m90" || id == "m90") {
            return system_family::irem_m90;
        }
        if (id == "irem_m92" || id == "m92") {
            return system_family::irem_m92;
        }
        if (id == "irem_m107" || id == "m107") {
            return system_family::irem_m107;
        }
        if (id == "taito_f2") {
            return system_family::taito_f2;
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
        if (id == "msx") {
            return system_family::msx;
        }
        if (id == "msx2") {
            return system_family::msx2;
        }
        if (id == "amiga500" || id == "a500" || id == "amiga") {
            return system_family::amiga500;
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
        case system_family::irem_m15:
            return "irem_m15";
        case system_family::irem_m52:
            return "irem_m52";
        case system_family::irem_m58:
            return "irem_m58";
        case system_family::irem_m72:
            return "irem_m72";
        case system_family::irem_m75:
            return "irem_m75";
        case system_family::irem_m81:
            return "irem_m81";
        case system_family::irem_m82:
            return "irem_m82";
        case system_family::irem_m84:
            return "irem_m84";
        case system_family::irem_m90:
            return "irem_m90";
        case system_family::irem_m92:
            return "irem_m92";
        case system_family::irem_m107:
            return "irem_m107";
        case system_family::taito_f2:
            return "taito_f2";
        case system_family::capcom_cps1:
            return "cps1";
        case system_family::capcom_cps2:
            return "cps2";
        case system_family::spectrum:
            return "spectrum";
        case system_family::nes:
            return "nes";
        case system_family::msx:
            return "msx";
        case system_family::msx2:
            return "msx2";
        case system_family::amiga500:
            return "amiga500";
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
        case system_family::irem_m15:
            return "Irem M15";
        case system_family::irem_m52:
            return "Irem M52";
        case system_family::irem_m58:
            return "Irem M58";
        case system_family::irem_m72:
            return "Irem M72";
        case system_family::irem_m75:
            return "Irem M75";
        case system_family::irem_m81:
            return "Irem M81";
        case system_family::irem_m82:
            return "Irem M82";
        case system_family::irem_m84:
            return "Irem M84";
        case system_family::irem_m90:
            return "Irem M90";
        case system_family::irem_m92:
            return "Irem M92";
        case system_family::irem_m107:
            return "Irem M107";
        case system_family::taito_f2:
            return "Taito F2";
        case system_family::capcom_cps1:
            return "CPS1";
        case system_family::capcom_cps2:
            return "CPS2";
        case system_family::spectrum:
            return "ZX Spectrum";
        case system_family::nes:
            return "NES";
        case system_family::msx:
            return "MSX";
        case system_family::msx2:
            return "MSX2";
        case system_family::amiga500:
            return "Amiga 500";
        case system_family::genesis:
            break;
        }
        return "Genesis";
    }

    const char* family_names() noexcept {
        return "genesis, sms, gg, c64, segacd, sega32x, irem_m15, irem_m52, irem_m58, irem_m72, "
               "irem_m75, irem_m81, irem_m82, irem_m84, irem_m90, irem_m92, irem_m107, "
               "taito_f2, cps1, cps2, spectrum, nes, msx, msx2, amiga500";
    }

} // namespace mnemos::apps::player::adapters
