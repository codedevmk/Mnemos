#include "m68000.hpp"

#include "chip_registry.hpp"

#include <memory>

namespace mnemos::chips::cpu {

    void force_link_m68000_registration() noexcept {}

    namespace {
        [[maybe_unused]] const auto m68000_registration =
            register_factory("motorola.68000", chip_class::cpu,
                             []() -> std::unique_ptr<ichip> { return std::make_unique<m68000>(); });
    } // namespace
} // namespace mnemos::chips::cpu
