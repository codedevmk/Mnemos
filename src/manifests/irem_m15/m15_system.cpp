#include "m15_system.hpp"

namespace mnemos::manifests::irem_m15 {

    m15_board_params board_params_for(std::string_view set_name) noexcept {
        if (set_name == "headoni") {
            return {.cpu_clock_hz = 1'996'800U, .rom_layout = "head_on_i8080"};
        }
        return {};
    }

} // namespace mnemos::manifests::irem_m15
