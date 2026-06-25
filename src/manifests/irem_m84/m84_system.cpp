#include "m84_system.hpp"

namespace mnemos::manifests::irem_m84 {

    m84_board_params board_params_for(std::string_view set_name) noexcept {
        if (set_name == "hharryb") {
            return {.dip_default = 0xFDBFU, .rom_layout = "bootleg_program_pair"};
        }
        if (set_name == "hharryu") {
            return {.dip_default = 0xFDBFU, .rom_layout = "us_program_pair"};
        }
        return {};
    }

} // namespace mnemos::manifests::irem_m84
