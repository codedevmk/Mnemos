#pragma once

#include "bus.hpp"         // topology bus
#include "genesis_vdp.hpp" // video
#include "m68000.hpp"      // main cpu
#include "region.hpp"      // mnemos::video_region (shared)
#include "sn76489.hpp"     // audio (PSG)
#include "ym2612.hpp"      // audio (FM)
#include "z80.hpp"         // sound cpu

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace mnemos::manifests::genesis {

    struct genesis_config final {
        mnemos::video_region video_region{mnemos::video_region::ntsc};
    };

    // Wraps a chip so the scheduler only advances it while `gate` is true. The Genesis
    // uses this for the Z80 sound CPU, which the 68000 halts via BUSREQ / RESET.
    class gated_chip final : public chips::ichip {
      public:
        gated_chip(chips::ichip& inner, const bool& gate) noexcept : inner_(&inner), gate_(&gate) {}

        [[nodiscard]] chips::chip_metadata metadata() const noexcept override {
            return inner_->metadata();
        }
        void tick(std::uint64_t cycles) override {
            if (*gate_) {
                inner_->tick(cycles);
            }
        }
        void reset(chips::reset_kind kind) override { inner_->reset(kind); }
        void save_state(chips::state_writer& writer) const override { inner_->save_state(writer); }
        void load_state(chips::state_reader& reader) override { inner_->load_state(reader); }
        [[nodiscard]] instrumentation::ichip_introspection& introspection() noexcept override {
            return inner_->introspection();
        }

      private:
        chips::ichip* inner_;
        const bool* gate_;
    };

    // Like gated_chip but the gate is a callable evaluated per tick. The
    // Genesis uses this to stall the 68000 while the VDP's DMA stall debt
    // is being drained -- the 68K only ticks when the VDP says the bus is
    // free. Captures a thin function pointer rather than a std::function so
    // every scheduler tick stays tiny.
    class predicate_gated_chip final : public chips::ichip {
      public:
        using predicate_fn = bool (*)(void* user) noexcept;
        predicate_gated_chip(chips::ichip& inner, predicate_fn fn, void* user) noexcept
            : inner_(&inner), fn_(fn), user_(user) {}
        [[nodiscard]] chips::chip_metadata metadata() const noexcept override {
            return inner_->metadata();
        }
        void tick(std::uint64_t cycles) override {
            if (fn_(user_)) {
                inner_->tick(cycles);
            }
        }
        void reset(chips::reset_kind kind) override { inner_->reset(kind); }
        void save_state(chips::state_writer& writer) const override { inner_->save_state(writer); }
        void load_state(chips::state_reader& reader) override { inner_->load_state(reader); }
        [[nodiscard]] instrumentation::ichip_introspection& introspection() noexcept override {
            return inner_->introspection();
        }

      private:
        chips::ichip* inner_;
        predicate_fn fn_;
        void* user_;
    };

    // A wired Sega Genesis / Mega Drive: the 68000 main CPU, the VDP, the YM2612 FM
    // and SN76489 PSG, the Z80 sound CPU, 64 KiB of 68K work RAM, 8 KiB of Z80 RAM,
    // and the 68000 24-bit big-endian bus. Heap-allocated and never moved after
    // assembly, because the bus regions hold spans into the members and the MMIO/IRQ
    // callbacks capture `this`.
    //
    // The 68000 side: the full memory map (ROM, work RAM, the A-bank with Z80 RAM +
    // YM2612, the I/O + Z80-control region, and the VDP), the VDP V/H-blank IRQ into
    // the 68000 IPL, VDP DMA reading from 68K space, and the PSG.
    //
    // The Z80 sound side: its own bus (Z80 RAM, the YM2612 at $4000, the PSG at $7F11,
    // the $6000 bank register, and the $8000-$FFFF banked window into 68K space), plus
    // BUSREQ/RESET arbitration so the 68000 can halt the Z80 and share the A-bank. The
    // Z80 is scheduled through z80_gate, which advances it only while it owns its bus.
    // Combining the YM2612 + PSG into mixed audio output is the next phase.
    struct genesis_system final {
        chips::cpu::m68000 cpu;
        chips::cpu::z80 z80;
        chips::video::genesis_vdp vdp;
        chips::audio::ym2612 fm;
        chips::audio::sn76489 psg;
        topology::bus bus{24U, topology::endianness::big};        // 68000 main bus
        topology::bus z80_bus{16U, topology::endianness::little}; // Z80 sound bus

        std::array<std::uint8_t, 0x10000> work_ram{}; // 64 KiB, mirrored $E00000-$FFFFFF
        std::array<std::uint8_t, 0x2000> z80_ram{};   // 8 KiB, Z80 $0000 / 68K $A00000
        std::vector<std::uint8_t> rom;                // cartridge image (borrowed by the buses)

        std::uint8_t version_register{}; // $A10001 region/version readback

        // I/O sub-controller registers ($A10000-$A1001F, byte-addressable).
        // Layout: 0x01=version (read-only), 0x03/0x05/0x07 = data A/B/C (read
        // via read_pad_port), 0x09/0x0B/0x0D = control A/B/C, 0x0F-0x1F =
        // serial regs. All bytes default to 0 after a hardware reset, which the
        // ROM's TST on these registers at boot relies on.
        std::array<std::uint8_t, 0x20> io_regs{};

        // 16-bit coalescing latches for the VDP ports: a 68000 word access arrives as
        // an even byte (high) then an odd byte (low); the VDP sees one whole word.
        std::uint8_t vdp_write_high{};
        std::uint8_t vdp_read_low{};

        // Z80 sound-CPU arbitration. The 68000 holds the Z80 via RESET ($A11200) and
        // BUSREQ ($A11100); the Z80 runs only while reset is released and the bus is
        // not requested. z80_bank is the 9-bit register that maps the Z80 $8000-$FFFF
        // window onto 68K address space ((bank << 15) | offset).
        bool z80_bus_requested{};
        bool z80_reset_released{};
        bool z80_running{};
        std::uint16_t z80_bank{};

        // Scheduler view of the Z80, advanced only while z80_running (see gated_chip).
        gated_chip z80_gate{z80, z80_running};

        // Scheduler view of the 68000, advanced only while the VDP isn't
        // holding the bus for DMA. Without this gate the 68K runs through
        // DMA in zero emulated time and the game's per-frame work budget
        // grows, pulling DMA-driven screen updates earlier than real hardware.
        static bool cpu_runnable(void* user) noexcept {
            const auto* sys = static_cast<const genesis_system*>(user);
            return !sys->vdp.dma_stall_active();
        }
        predicate_gated_chip cpu_gate{cpu, &cpu_runnable, this};

        // Free-running V-blank counter (one tick per real V-blank entry); the
        // optional WRAM-write watcher (MNEMOS_WRAM_WATCH env) consults this to
        // frame-gate its logs. Not part of the architectural state, not saved.
        std::uint64_t frame_index{};

        // Controller state, active-high (a set bit = pressed). Bit layout:
        //   0=Up  1=Down  2=Left  3=Right  4=A  5=B  6=C  7=Start
        // The $A1000{3,5} read handler converts to the active-low Genesis pad
        // wire protocol; bit 6 of the CPU-written byte at the same address is
        // the TH (select) line that toggles between the (U/D/L/R/B/C) and
        // (U/D/A/Start) button banks. `pad_th[]` latches the most-recent TH
        // write per port so the read returns the right bank.
        std::array<std::uint8_t, 2> pad{};
        std::array<bool, 2> pad_th{{true, true}}; // TH defaults high on reset

        void set_pad(int port, std::uint8_t buttons) noexcept {
            if (port >= 0 && port < 2) {
                pad[static_cast<std::size_t>(port)] = buttons;
            }
        }

        // Encode the current pad state as the byte the 68000 reads at
        // $A10003/$A10005 for the given port. Matches the standard Sega 3-button
        // pad protocol: when TH is high the bank is U/D/L/R/B/C; when TH is low
        // it's U/D/-/-/A/Start (left/right read as if pressed so games can
        // detect a 3-button vs 6-button pad).
        [[nodiscard]] std::uint8_t read_pad_port(int port) const noexcept {
            if (port < 0 || port >= 2) {
                return 0xFFU;
            }
            const auto bits = pad[static_cast<std::size_t>(port)];
            const bool th = pad_th[static_cast<std::size_t>(port)];
            const auto inv = [&](std::uint8_t mask) -> std::uint8_t {
                return (bits & mask) ? 0U : 1U; // active-high pad -> active-low wire
            };
            std::uint8_t out = th ? 0x40U : 0x00U;
            if (th) {
                out |= inv(0x01U) << 0; // Up
                out |= inv(0x02U) << 1; // Down
                out |= inv(0x04U) << 2; // Left
                out |= inv(0x08U) << 3; // Right
                out |= inv(0x20U) << 4; // B
                out |= inv(0x40U) << 5; // C
            } else {
                out |= inv(0x01U) << 0; // Up
                out |= inv(0x02U) << 1; // Down
                // bits 2,3 always 0 (so a 3-button pad is identifiable: L+R both "pressed")
                out |= inv(0x10U) << 4; // A
                out |= inv(0x80U) << 5; // Start
            }
            return out;
        }
    };

    // Assemble a bootable Genesis from a cartridge image (moved in). The 68000 boots
    // from the ROM's reset vectors ($000000 SSP, $000004 PC). ROM verification is the
    // caller's job; pass any image to exercise the wiring.
    [[nodiscard]] std::unique_ptr<genesis_system>
    assemble_genesis(std::vector<std::uint8_t> rom, const genesis_config& config = {});

} // namespace mnemos::manifests::genesis
