#pragma once

#include "sn76489.hpp"

#include <array>
#include <cstdint>

namespace mnemos::manifests::sms {

    // Game Gear system I/O registers (Z80 ports $00-$06). These exist only on
    // Game Gear hardware -- a base Master System decodes none of them and the
    // decode is gated by enable(). The handset adds:
    //   $00 read   START/region mode register (no write)
    //   $01-$05    EXT serial-link port (parallel data, direction, TX/RX, control)
    //   $06 write  PSG stereo-output control (routed to sn76489::write_stereo)
    //
    // $00 layout (read): bit7 = START (active-low: 1 idle, 0 pressed), bit6 =
    // region (1 = overseas/export, 0 = domestic/Japan), bits 5-0 = 0. The EXT
    // port is never connected here, so its registers behave as the reset-state
    // loopback the hardware presents to an unplugged link cable.
    class gg_io final {
      public:
        // Game Gear hardware present. Off (default) = base Master System, which
        // ignores $00-$06 entirely (open bus / coarse $3F decode).
        void enable(bool on) noexcept { enabled_ = on; }
        [[nodiscard]] bool enabled() const noexcept { return enabled_; }

        // $00 bit6 region. True = overseas/export console, false = domestic (Japan).
        void set_export(bool on) noexcept { export_region_ = on; }
        // $00 bit7 START button. True = pressed (drives the bus bit low).
        void set_start(bool pressed) noexcept { start_ = pressed; }

        [[nodiscard]] std::uint8_t read(std::uint8_t port) const noexcept {
            switch (port) {
            case 0x00U: {
                std::uint8_t v =
                    static_cast<std::uint8_t>(0x80U | (export_region_ ? 0x40U : 0x00U));
                if (start_) {
                    v = static_cast<std::uint8_t>(v & ~0x80U); // START pulls bit7 low
                }
                return v;
            }
            case 0x01U:
                // Parallel data: output pins (direction bits set) read back their
                // latched level; input pins float to the direction register's value.
                return static_cast<std::uint8_t>((reg_[1] & ~(reg_[2] & 0x7FU)) |
                                                 (reg_[2] & 0x7FU));
            case 0x02U:
                return reg_[2];
            case 0x03U:
                return reg_[3];
            case 0x04U:
                return reg_[4];
            case 0x05U:
                return reg_[5];
            default:
                return 0xFFU; // $06 is write-only; $00-$06 is the only decoded window
            }
        }

        void write(std::uint8_t port, std::uint8_t value, chips::audio::sn76489& psg) noexcept {
            switch (port) {
            case 0x01U:
                reg_[1] = value;
                return;
            case 0x02U:
                reg_[2] = value;
                return;
            case 0x03U:
                reg_[3] = value;
                return;
            case 0x05U:
                reg_[5] = static_cast<std::uint8_t>(value & 0xF8U); // bits 0-2 read-only
                return;
            case 0x06U:
                reg_[6] = value;
                psg.write_stereo(value); // PSG stereo-output control
                return;
            default:
                return; // $00 (mode) and $04 (RX buffer) are read-only
            }
        }

      private:
        // EXT-port register file ($01-$05 working state, $06 last write). Index 0
        // is unused ($00 is computed from start_/export_region_). Reset state of an
        // unconnected link: direction $02 = $FF, RX $04 = $FF, the rest 0.
        std::array<std::uint8_t, 7> reg_{0x00U, 0x00U, 0xFFU, 0x00U, 0xFFU, 0x00U, 0xFFU};
        bool enabled_{};
        bool export_region_{true};
        bool start_{};
    };

} // namespace mnemos::manifests::sms
