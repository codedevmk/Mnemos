#include "m107_system.hpp"

namespace mnemos::manifests::irem_m107 {

    m107_board_params board_params_for(std::string_view set_name) noexcept {
        if (set_name == "airass") {
            return {.dip_default = 0xFFFFU, .rom_layout = "air_assault"};
        }
        if (set_name == "firebarr") {
            return {.dip_default = 0xFFFFU, .rom_layout = "fire_barrel"};
        }
        return {};
    }

} // namespace mnemos::manifests::irem_m107
