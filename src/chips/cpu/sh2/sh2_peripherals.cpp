#include "sh2_peripherals.hpp"

#include "state.hpp"

namespace mnemos::chips::cpu {

    std::uint8_t sh2_peripherals::read8(std::uint32_t addr) const noexcept {
        return regs_[addr & (window_size - 1U)];
    }

    void sh2_peripherals::write8(std::uint32_t addr, std::uint8_t value) noexcept {
        regs_[addr & (window_size - 1U)] = value;
    }

    void sh2_peripherals::reset() noexcept { regs_.fill(0U); }

    void sh2_peripherals::save_state(state_writer& writer) const { writer.bytes(regs_); }

    void sh2_peripherals::load_state(state_reader& reader) { reader.bytes(regs_); }

} // namespace mnemos::chips::cpu
