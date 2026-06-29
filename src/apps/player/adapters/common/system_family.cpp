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
        if (id == "irem_m10" || id == "m10") {
            return system_family::irem_m10;
        }
        if (id == "irem_m14" || id == "m14") {
            return system_family::irem_m14;
        }
        if (id == "irem_m15" || id == "m15") {
            return system_family::irem_m15;
        }
        if (id == "irem_m27" || id == "m27") {
            return system_family::irem_m27;
        }
        if (id == "irem_m47" || id == "m47") {
            return system_family::irem_m47;
        }
        if (id == "irem_m52" || id == "m52") {
            return system_family::irem_m52;
        }
        if (id == "irem_m57" || id == "m57") {
            return system_family::irem_m57;
        }
        if (id == "irem_m58" || id == "m58") {
            return system_family::irem_m58;
        }
        if (id == "irem_m62" || id == "m62") {
            return system_family::irem_m62;
        }
        if (id == "irem_m63" || id == "m63") {
            return system_family::irem_m63;
        }
        if (id == "irem_travrusa" || id == "travrusa") {
            return system_family::irem_travrusa;
        }
        if (id == "irem_redalert" || id == "redalert") {
            return system_family::irem_redalert;
        }
        if (id == "irem_m72") {
            return system_family::irem_m72;
        }
        if (id == "irem_m75" || id == "m75") {
            return system_family::irem_m75;
        }
        if (id == "irem_m78" || id == "m78") {
            return system_family::irem_m78;
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
        if (id == "irem_m85" || id == "m85") {
            return system_family::irem_m85;
        }
        if (id == "irem_m90" || id == "m90") {
            return system_family::irem_m90;
        }
        if (id == "irem_m92" || id == "m92") {
            return system_family::irem_m92;
        }
        if (id == "irem_m102" || id == "m102") {
            return system_family::irem_m102;
        }
        if (id == "irem_m107" || id == "m107") {
            return system_family::irem_m107;
        }
        if (id == "irem_m119" || id == "m119") {
            return system_family::irem_m119;
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
        case system_family::irem_m10:
            return "irem_m10";
        case system_family::irem_m14:
            return "irem_m14";
        case system_family::irem_m15:
            return "irem_m15";
        case system_family::irem_m27:
            return "irem_m27";
        case system_family::irem_m47:
            return "irem_m47";
        case system_family::irem_m52:
            return "irem_m52";
        case system_family::irem_m57:
            return "irem_m57";
        case system_family::irem_m58:
            return "irem_m58";
        case system_family::irem_m62:
            return "irem_m62";
        case system_family::irem_m63:
            return "irem_m63";
        case system_family::irem_travrusa:
            return "irem_travrusa";
        case system_family::irem_redalert:
            return "irem_redalert";
        case system_family::irem_m72:
            return "irem_m72";
        case system_family::irem_m75:
            return "irem_m75";
        case system_family::irem_m78:
            return "irem_m78";
        case system_family::irem_m81:
            return "irem_m81";
        case system_family::irem_m82:
            return "irem_m82";
        case system_family::irem_m84:
            return "irem_m84";
        case system_family::irem_m85:
            return "irem_m85";
        case system_family::irem_m90:
            return "irem_m90";
        case system_family::irem_m92:
            return "irem_m92";
        case system_family::irem_m102:
            return "irem_m102";
        case system_family::irem_m107:
            return "irem_m107";
        case system_family::irem_m119:
            return "irem_m119";
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
        case system_family::irem_m10:
            return "Irem M10";
        case system_family::irem_m14:
            return "Irem M14";
        case system_family::irem_m15:
            return "Irem M15";
        case system_family::irem_m27:
            return "Irem M27";
        case system_family::irem_m47:
            return "Irem M47";
        case system_family::irem_m52:
            return "Irem M52";
        case system_family::irem_m57:
            return "Irem M57";
        case system_family::irem_m58:
            return "Irem M58";
        case system_family::irem_m62:
            return "Irem M62";
        case system_family::irem_m63:
            return "Irem M63";
        case system_family::irem_travrusa:
            return "Irem Traverse USA";
        case system_family::irem_redalert:
            return "Irem Red Alert";
        case system_family::irem_m72:
            return "Irem M72";
        case system_family::irem_m75:
            return "Irem M75";
        case system_family::irem_m78:
            return "Irem M78";
        case system_family::irem_m81:
            return "Irem M81";
        case system_family::irem_m82:
            return "Irem M82";
        case system_family::irem_m84:
            return "Irem M84";
        case system_family::irem_m85:
            return "Irem M85";
        case system_family::irem_m90:
            return "Irem M90";
        case system_family::irem_m92:
            return "Irem M92";
        case system_family::irem_m102:
            return "Irem M102";
        case system_family::irem_m107:
            return "Irem M107";
        case system_family::irem_m119:
            return "Irem M119";
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
        return "genesis, sms, gg, c64, segacd, sega32x, irem_m10, irem_m14, irem_m15, irem_m27, "
               "irem_m47, irem_m52, irem_m57, irem_m58, irem_m62, irem_m63, irem_travrusa, "
               "irem_redalert, irem_m72, irem_m75, irem_m78, "
               "irem_m81, irem_m82, irem_m84, irem_m85, irem_m90, irem_m92, irem_m102, "
               "irem_m107, irem_m119, taito_f2, cps1, cps2, spectrum, nes, msx, msx2, "
               "amiga500";
    }

} // namespace mnemos::apps::player::adapters
