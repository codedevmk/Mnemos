#include "genesis_vdp.hpp"

#include "chip_registry.hpp"

#include <memory>

namespace mnemos::chips::video {

    void force_link_genesis_vdp_registration() noexcept {}

    namespace {
        [[maybe_unused]] const auto genesis_vdp_registration =
            register_factory("sega.315_5313", chip_class::video, []() -> std::unique_ptr<ichip> {
                return std::make_unique<genesis_vdp>();
            });
    } // namespace
} // namespace mnemos::chips::video
