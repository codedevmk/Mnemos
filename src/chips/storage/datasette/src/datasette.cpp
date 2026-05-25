#include <mnemos/chips/storage/datasette.hpp>

#include <mnemos/chips/common/chip_registry.hpp>
#include <mnemos/chips/common/state.hpp>

#include <memory>
#include <string_view>
#include <utility>

namespace mnemos::chips::storage {
    namespace {
        constexpr std::string_view tap_magic = "C64-TAPE-RAW"; // 12 bytes
        constexpr std::size_t tap_header = 20U;                // magic + version + 3 + size(4)
    } // namespace

    chip_metadata datasette::metadata() const noexcept {
        return {
            .manufacturer = "Commodore",
            .part_number = "1530",
            .family = "datasette",
            .klass = chip_class::storage,
            .revision = 1U,
        };
    }

    void datasette::configure(config cfg) {
        cfg_ = std::move(cfg);
        update_sense();
    }

    void datasette::reset(reset_kind /*kind*/) {
        play_ = false;
        pos_ = 0U;
        countdown_ = 0U;
        update_sense();
    }

    bool datasette::load_tap(std::span<const std::uint8_t> tap) {
        if (tap.size() < tap_header || std::string_view(reinterpret_cast<const char*>(tap.data()),
                                                        tap_magic.size()) != tap_magic) {
            return false;
        }
        version_ = tap[12];
        std::size_t size =
            static_cast<std::size_t>(tap[16]) | (static_cast<std::size_t>(tap[17]) << 8U) |
            (static_cast<std::size_t>(tap[18]) << 16U) | (static_cast<std::size_t>(tap[19]) << 24U);
        if (tap_header + size > tap.size()) {
            size = tap.size() - tap_header; // clamp to the available data
        }
        data_.assign(tap.begin() + static_cast<std::ptrdiff_t>(tap_header),
                     tap.begin() + static_cast<std::ptrdiff_t>(tap_header + size));
        pos_ = 0U;
        countdown_ = 0U;
        return true;
    }

    void datasette::eject() noexcept {
        data_.clear();
        version_ = 0U;
        pos_ = 0U;
        countdown_ = 0U;
    }

    void datasette::set_play(bool pressed) noexcept {
        play_ = pressed;
        update_sense();
    }

    void datasette::rewind() noexcept {
        pos_ = 0U;
        countdown_ = 0U;
    }

    void datasette::update_sense() {
        if (cfg_.set_sense) {
            cfg_.set_sense(play_); // a held key pulls the sense line low
        }
    }

    std::uint32_t datasette::next_pulse_cycles() {
        if (pos_ >= data_.size()) {
            return 0U;
        }
        const std::uint8_t b = data_[pos_++];
        if (b != 0U) {
            return static_cast<std::uint32_t>(b) * 8U;
        }
        if (version_ == 0U) {
            return v0_overflow_cycles; // v0: exact long-pulse length is not recorded
        }
        if (pos_ + 3U > data_.size()) { // v1: three little-endian length bytes follow
            pos_ = data_.size();
            return 0U;
        }
        const std::uint32_t len = static_cast<std::uint32_t>(data_[pos_]) |
                                  (static_cast<std::uint32_t>(data_[pos_ + 1U]) << 8U) |
                                  (static_cast<std::uint32_t>(data_[pos_ + 2U]) << 16U);
        pos_ += 3U;
        return len;
    }

    void datasette::tick(std::uint64_t cycles) {
        for (std::uint64_t i = 0; i < cycles; ++i) {
            if (!play_ || (cfg_.motor_on && !cfg_.motor_on())) {
                continue; // paused: no key held, or motor off
            }
            if (countdown_ == 0U) {
                countdown_ = next_pulse_cycles(); // 0 once the tape is exhausted
                if (countdown_ == 0U) {
                    continue; // end of tape / malformed
                }
            }
            --countdown_;
            if (countdown_ == 0U && cfg_.flag_pulse) {
                cfg_.flag_pulse(); // pulse boundary -> CIA1 /FLAG negative edge
            }
        }
    }

    void datasette::save_state(state_writer& writer) const {
        writer.u8(version_);
        writer.u64(pos_);
        writer.u32(countdown_);
        writer.boolean(play_);
    }

    void datasette::load_state(state_reader& reader) {
        version_ = reader.u8();
        pos_ = reader.u64();
        countdown_ = reader.u32();
        play_ = reader.boolean();
        update_sense();
    }

    instrumentation::i_chip_introspection& datasette::introspection() noexcept {
        return introspection_;
    }

    namespace {
        [[maybe_unused]] const auto datasette_registration = register_factory(
            "commodore.1530", chip_class::storage,
            []() -> std::unique_ptr<i_chip> { return std::make_unique<datasette>(); });
    } // namespace

} // namespace mnemos::chips::storage
