#include "dac8.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <memory>

namespace mnemos::chips::audio {

    chip_metadata dac8::metadata() const noexcept {
        return {
            .manufacturer = "Generic",
            .part_number = "dac8",
            .family = "dac",
            .klass = chip_class::audio_synth,
            .revision = 1U,
        };
    }

    void dac8::reset(reset_kind /*kind*/) {
        level_ = 0x80U;
        elapsed_ = 0U;
    }

    void dac8::save_state(state_writer& writer) const {
        writer.u8(level_);
        writer.u64(elapsed_);
    }

    void dac8::load_state(state_reader& reader) {
        level_ = reader.u8();
        elapsed_ = reader.u64();
    }

    namespace {
        [[maybe_unused]] const auto dac8_registration =
            register_factory("generic.dac8", chip_class::audio_synth,
                             []() -> std::unique_ptr<ichip> { return std::make_unique<dac8>(); });
    } // namespace

} // namespace mnemos::chips::audio
