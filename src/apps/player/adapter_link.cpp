#include "adapter_link.hpp"

#include "amiga500_adapter.hpp"
#include "c64_adapter.hpp"
#include "capcom_cps1_adapter.hpp"
#include "capcom_cps2_adapter.hpp"
#include "genesis_adapter.hpp"
#include "irem_m10_adapter.hpp"
#include "irem_m107_adapter.hpp"
#include "irem_m14_adapter.hpp"
#include "irem_m15_adapter.hpp"
#include "irem_m27_adapter.hpp"
#include "irem_m47_adapter.hpp"
#include "irem_m52_adapter.hpp"
#include "irem_m57_adapter.hpp"
#include "irem_m58_adapter.hpp"
#include "irem_m62_adapter.hpp"
#include "irem_m63_adapter.hpp"
#include "irem_m72_adapter.hpp"
#include "irem_m75_adapter.hpp"
#include "irem_m78_adapter.hpp"
#include "irem_m81_adapter.hpp"
#include "irem_m82_adapter.hpp"
#include "irem_m84_adapter.hpp"
#include "irem_m85_adapter.hpp"
#include "irem_m90_adapter.hpp"
#include "irem_m92_adapter.hpp"
#include "irem_redalert_adapter.hpp"
#include "irem_travrusa_adapter.hpp"
#include "msx2_adapter.hpp"
#include "msx_adapter.hpp"
#include "nes_adapter.hpp"
#include "sega32x_adapter.hpp"
#include "segacd_adapter.hpp"
#include "sms_adapter.hpp"
#include "spectrum_adapter.hpp"
#include "taito_f2_adapter.hpp"

namespace mnemos::apps::player {

    void force_link_all_adapters() noexcept {
        adapters::genesis::force_link();
        adapters::sms::force_link();
        adapters::c64::force_link();
        adapters::segacd::force_link();
        adapters::sega32x::force_link();
        adapters::irem_m10::force_link();
        adapters::irem_m14::force_link();
        adapters::irem_m15::force_link();
        adapters::irem_m27::force_link();
        adapters::irem_m47::force_link();
        adapters::irem_m52::force_link();
        adapters::irem_m57::force_link();
        adapters::irem_m58::force_link();
        adapters::irem_m62::force_link();
        adapters::irem_m63::force_link();
        adapters::irem_travrusa::force_link();
        adapters::irem_redalert::force_link();
        adapters::irem_m72::force_link();
        adapters::irem_m75::force_link();
        adapters::irem_m78::force_link();
        adapters::irem_m81::force_link();
        adapters::irem_m82::force_link();
        adapters::irem_m84::force_link();
        adapters::irem_m85::force_link();
        adapters::irem_m90::force_link();
        adapters::irem_m92::force_link();
        adapters::irem_m107::force_link();
        adapters::taito_f2::force_link();
        adapters::capcom_cps1::force_link();
        adapters::capcom_cps2::force_link();
        adapters::spectrum::force_link();
        adapters::nes::force_link();
        adapters::msx::force_link();
        adapters::msx2::force_link();
        adapters::amiga500::force_link();
    }

} // namespace mnemos::apps::player
