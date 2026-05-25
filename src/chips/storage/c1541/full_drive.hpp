#pragma once

#include "chip.hpp"
#include "d64_image.hpp"
#include "disk_bind.hpp"
#include "ibus.hpp"
#include "iec_bus.hpp"
#include "m6510.hpp"
#include "via_6522.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace mnemos::chips::storage::c1541 {

    // Full cycle-accurate Commodore 1541 disk drive: a complete second computer —
    // a 6502 (the 6510 core with its I/O port disabled), two 6522 VIAs (VIA1 = IEC
    // serial port, VIA2 = disk mechanism + GCR head), 2 KB RAM, and the 16 KB 1541
    // DOS ROM — spinning a GCR-encoded disk under its head.
    //
    // Ported from Emu's c1541_full (ADR 0006). The DOS ROM is copyrighted and
    // never committed, so end-to-end operation is data-gated (see ROMS.md), exactly
    // like the C64 golden boot; and, as in Emu, the GCR read path is the hard part
    // still being proven. The memory map, VIA port wiring, stepper, and head
    // byte/SYNC mechanics are unit-tested with a synthetic ROM.
    class full_drive final : public i_storage {
      public:
        static constexpr std::uint16_t ram_size = 0x800U;
        static constexpr std::uint32_t rom_size = 0x4000U;
        static constexpr std::uint8_t park_half_track = 34U; // track 18

        explicit full_drive(std::uint8_t device_id = 8U);

        [[nodiscard]] chip_metadata metadata() const noexcept override;
        void tick(std::uint64_t cycles) override;
        void reset(reset_kind kind) override;

        void save_state(state_writer& writer) const override;
        void load_state(state_reader& reader) override;

        [[nodiscard]] instrumentation::i_chip_introspection& introspection() noexcept override;

        void attach_bus(iec_bus& bus) noexcept;
        [[nodiscard]] bool load_rom(std::span<const std::uint8_t> rom); // exactly 16 KiB
        [[nodiscard]] bool mount(std::span<const std::uint8_t> d64);    // binds GCR tracks
        [[nodiscard]] bool rom_loaded() const noexcept { return rom_.size() == rom_size; }
        [[nodiscard]] bool disk_loaded() const noexcept { return !tracks_.empty(); }

        // Drive runs at `drive_hz` against a `host_hz` phi2 (fractional divider).
        void set_clock_ratio(std::uint32_t drive_hz, std::uint32_t host_hz) noexcept;

        // Inspection (for tests / instrumentation).
        [[nodiscard]] std::uint8_t head_half_track() const noexcept { return head_half_track_; }
        [[nodiscard]] std::uint8_t current_track() const noexcept {
            return static_cast<std::uint8_t>(head_half_track_ / 2U + 1U);
        }
        [[nodiscard]] bool motor_on() const noexcept { return motor_; }
        [[nodiscard]] std::uint8_t head_byte() const noexcept { return latched_byte_; }
        [[nodiscard]] cpu::m6510& cpu() noexcept { return cpu_; }

      private:
        class introspection_surface final : public instrumentation::i_chip_introspection {};

        // The drive's private 16-bit address space (RAM mirror / VIA1 / VIA2 / ROM).
        class drive_bus final : public i_bus {
          public:
            explicit drive_bus(full_drive& owner) noexcept : owner_(owner) {}
            [[nodiscard]] std::uint8_t read8(std::uint32_t address) override {
                return owner_.bus_read(static_cast<std::uint16_t>(address));
            }
            void write8(std::uint32_t address, std::uint8_t value) override {
                owner_.bus_write(static_cast<std::uint16_t>(address), value);
            }

          private:
            full_drive& owner_;
        };

        void configure_vias();
        [[nodiscard]] std::uint8_t bus_read(std::uint16_t address);
        void bus_write(std::uint16_t address, std::uint8_t value);
        void drive_cycle();
        void update_cpu_irq();
        void refresh_via1_bus();
        void poll_atn();
        void update_mechanism();
        [[nodiscard]] std::uint8_t via1_iec_input() const;
        [[nodiscard]] std::uint8_t via2_status_input() const;
        void advance_head();

        cpu::m6510 cpu_;
        bus_controller::via_6522 via1_; // IEC
        bus_controller::via_6522 via2_; // mechanism + head
        drive_bus bus_{*this};
        std::array<std::uint8_t, ram_size> ram_{};
        std::vector<std::uint8_t> rom_;
        d64_image disk_;
        std::vector<gcr_track> tracks_;

        iec_bus* iec_{};
        std::uint8_t device_{8U};

        // Head + platter.
        std::uint8_t head_half_track_{park_half_track};
        std::size_t byte_index_{};
        std::uint8_t latched_byte_{0x55U};
        std::uint8_t stepper_prev_{};
        std::uint8_t density_zone_{3U};
        bool motor_{};
        bool atn_prev_{true};

        // Byte-cell timing.
        std::uint32_t byte_cycles_{};
        std::uint32_t ratio_num_{1'000'000U};
        std::uint32_t ratio_den_{985'248U};
        std::uint64_t ratio_acc_{};

        introspection_surface introspection_{};
    };

} // namespace mnemos::chips::storage::c1541
