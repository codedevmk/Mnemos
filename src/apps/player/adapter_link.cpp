#include "adapter_link.hpp"

#include "c64_adapter.hpp"
#include "capcom_cps1_adapter.hpp"
#include "capcom_cps2_adapter.hpp"
#include "genesis_adapter.hpp"
#include "irem_m72_adapter.hpp"
#include "msx_adapter.hpp"
#include "nes_adapter.hpp"
#include "sega32x_adapter.hpp"
#include "segacd_adapter.hpp"
#include "sms_adapter.hpp"
#include "spectrum_adapter.hpp"

namespace mnemos::apps::player {

    void force_link_all_adapters() noexcept {
        adapters::genesis::force_link();
        adapters::sms::force_link();
        adapters::c64::force_link();
        adapters::segacd::force_link();
        adapters::sega32x::force_link();
        adapters::irem_m72::force_link();
        adapters::capcom_cps1::force_link();
        adapters::capcom_cps2::force_link();
        adapters::spectrum::force_link();
        adapters::nes::force_link();
        adapters::msx::force_link();
    }

} // namespace mnemos::apps::player
