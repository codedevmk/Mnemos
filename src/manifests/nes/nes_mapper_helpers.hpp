#pragma once

#include "ppu2c02.hpp"
#include "state.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace mnemos::manifests::nes::detail {

    inline constexpr std::size_t k_prg_bank = 0x4000U; // 16 KiB
    inline constexpr std::size_t k_prg_8k = 0x2000U;   // 8 KiB PRG bank (MMC3)
    inline constexpr std::size_t k_prg_32k = 0x8000U;  // 32 KiB PRG bank (AxROM)
    inline constexpr std::size_t k_chr_4k = 0x1000U;   // 4 KiB CHR bank
    inline constexpr std::size_t k_chr_2k = 0x0800U;   // 2 KiB CHR bank (Sunsoft-4)
    inline constexpr std::size_t k_chr_1k = 0x0400U;   // 1 KiB CHR bank (MMC3)
    inline constexpr std::size_t k_chr_8k = 0x2000U;   // 8 KiB CHR bank (CNROM)

    class chr_window {
      public:
        chr_window(std::span<std::uint8_t> chr, bool is_ram) noexcept
            : chr_(chr), is_ram_(is_ram), count_1k_(chr.size() / k_chr_1k) {}

        void attach(chips::video::ppu2c02& ppu) noexcept {
            if (chr_.empty()) {
                return;
            }
            if (is_ram_) {
                ppu.attach_chr_ram(chr_);
            } else {
                ppu.attach_chr(std::span<const std::uint8_t>(window_));
            }
        }

        [[nodiscard]] std::size_t count_1k() const noexcept { return count_1k_; }
        [[nodiscard]] bool bankable() const noexcept { return !is_ram_ && count_1k_ != 0U; }

        void set_1k(std::size_t slot, std::size_t bank) noexcept { put(slot, bank, 1U); }
        void set_2k(std::size_t slot, std::size_t bank) noexcept { put(slot, bank, 2U); }
        void set_4k(std::size_t slot, std::size_t bank) noexcept { put(slot, bank, 4U); }

      private:
        void put(std::size_t slot, std::size_t bank, std::size_t units) noexcept {
            if (!bankable()) {
                return;
            }
            const std::size_t bytes = units * k_chr_1k;
            const std::size_t banks = count_1k_ / units;
            const std::size_t src = (banks == 0U ? 0U : bank % banks) * bytes;
            std::copy_n(chr_.begin() + static_cast<std::ptrdiff_t>(src), bytes,
                        window_.begin() + static_cast<std::ptrdiff_t>(slot * bytes));
        }

        std::span<std::uint8_t> chr_;
        bool is_ram_;
        std::size_t count_1k_;
        std::array<std::uint8_t, 0x2000U> window_{};
    };

    inline void apply_mirror_mode(chips::video::ppu2c02& ppu, std::uint8_t mode) {
        using m = chips::video::ppu2c02::mirroring;
        switch (mode & 0x03U) {
        case 0U:
            ppu.set_mirroring(m::vertical);
            break;
        case 1U:
            ppu.set_mirroring(m::horizontal);
            break;
        case 2U:
            ppu.set_mirroring(m::single_a);
            break;
        default:
            ppu.set_mirroring(m::single_b);
            break;
        }
    }

    struct konami_vrc_irq {
        std::uint8_t latch{};
        std::uint8_t counter{};
        int prescaler{};
        bool enabled{};
        bool ack_enable{};
        bool cycle_mode{};

        void reset() noexcept {
            latch = counter = 0U;
            prescaler = 0;
            enabled = ack_enable = cycle_mode = false;
        }

        template <typename Raise> void write_control(std::uint8_t value, const Raise& raise) {
            ack_enable = (value & 0x01U) != 0U;
            enabled = (value & 0x02U) != 0U;
            cycle_mode = (value & 0x04U) != 0U;
            if (enabled) {
                counter = latch;
                prescaler = 0;
            }
            raise(false);
        }

        template <typename Raise> void acknowledge(const Raise& raise) {
            raise(false);
            enabled = ack_enable;
        }

        template <typename Raise> void tick_counter(const Raise& raise) {
            if (counter == 0xFFU) {
                counter = latch;
                raise(true);
            } else {
                ++counter;
            }
        }

        template <typename Raise> void clock(std::uint32_t cpu_cycles, const Raise& raise) {
            if (!enabled) {
                return;
            }
            for (std::uint32_t i = 0; i < cpu_cycles; ++i) {
                if (cycle_mode) {
                    tick_counter(raise);
                } else {
                    prescaler += 3;
                    if (prescaler >= 341) {
                        prescaler -= 341;
                        tick_counter(raise);
                    }
                }
            }
        }

        void save(chips::state_writer& writer) const {
            writer.u8(latch);
            writer.u8(counter);
            writer.u32(static_cast<std::uint32_t>(prescaler));
            writer.boolean(enabled);
            writer.boolean(ack_enable);
            writer.boolean(cycle_mode);
        }

        void load(chips::state_reader& reader) {
            latch = reader.u8();
            counter = reader.u8();
            prescaler = static_cast<int>(reader.u32());
            enabled = reader.boolean();
            ack_enable = reader.boolean();
            cycle_mode = reader.boolean();
        }
    };

} // namespace mnemos::manifests::nes::detail
