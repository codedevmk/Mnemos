#pragma once

#include "bus.hpp"             // topology bus
#include "genesis_banking.hpp" // cart_banking_runtime + wire_cart_banking
#include "genesis_cart.hpp"    // cart_sram_runtime + wire_cart_sram
#include "genesis_eeprom.hpp"  // cart_eeprom_runtime + wire_cart_eeprom
#include "genesis_lockon.hpp"  // cart_lockon_runtime + wire_cart_lockon
#include "genesis_vdp.hpp"     // video
#include "m68000.hpp"          // main cpu
#include "peripheral.hpp"      // peripheral::device (controller ports)
#include "region.hpp"          // mnemos::video_region (shared)
#include "sn76489.hpp"         // audio (PSG)
#include "ym2612.hpp"          // audio (FM)
#include "z80.hpp"             // sound cpu

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace mnemos::manifests::genesis {

    struct genesis_config final {
        mnemos::video_region video_region{mnemos::video_region::ntsc};
        // Optional "lock-on" inserted cartridge. When non-empty, the primary
        // `rom` passed to assemble/build is the lock-on BASE cartridge (boot
        // master, flat-mapped at $000000) and this image is the inserted game
        // mapped into the $300000-$3FFFFF window. Empty = ordinary single cart.
        std::vector<std::uint8_t> inserted_rom{};
    };

    // Scheduler view of a chip, ticked only while `gate` is true. Used for
    // the Z80 sound CPU, which the 68000 halts via BUSREQ / RESET.
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

    // gated_chip variant whose gate is a function pointer evaluated per tick.
    // Used to stall the 68000 while the VDP is holding the bus for DMA.
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

    // Heap-allocated, never-moved Sega Genesis / Mega Drive: the buses hold
    // spans into the member arrays and the MMIO / IRQ callbacks capture `this`.
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
        std::vector<std::uint8_t> inserted_rom;       // lock-on inserted cart (borrowed by `bus`)
        cart_sram_runtime sram;                       // battery SRAM (borrowed by `bus`)
        cart_eeprom_runtime eeprom;                   // serial EEPROM (borrowed by `bus`)
        cart_banking_runtime banking;                 // >4 MiB ROM bank-switch (borrowed by `bus`)
        cart_lockon_runtime lockon;                   // lock-on window select (borrowed by `bus`)

        std::uint8_t version_register{}; // $A10001 region/version readback

        // I/O sub-controller registers ($A10000-$A1001F), indexed by the low
        // byte offset (a & 0x1F). $A10001=version, $A10003/05/07 = data A/B/C,
        // $A10009/0B/0D = ctrl A/B/C, rest = serial TxData/RxData/SCtrl. All
        // bytes reset to 0; the non-zero power-on state lives in the data-port
        // read path (an unconnected port reads its pins high), not here.
        std::array<std::uint8_t, 0x20> io_regs{};

        // 16-bit coalescing latches for the VDP ports (68K word access splits
        // into a high-byte-even + low-byte-odd byte pair).
        std::uint8_t vdp_write_high{};
        std::uint8_t vdp_read_low{};

        // Z80 sound-CPU arbitration. 68K holds Z80 via RESET ($A11200) and
        // BUSREQ ($A11100); z80_bank is the 9-bit window from Z80 $8000-$FFFF
        // into 68K space at ((bank << 15) | offset).
        bool z80_bus_requested{};
        bool z80_reset_released{};
        bool z80_running{};
        std::uint16_t z80_bank{};

        gated_chip z80_gate{z80, z80_running};

        static bool cpu_runnable(void* user) noexcept {
            const auto* sys = static_cast<const genesis_system*>(user);
            return !sys->vdp.dma_stall_active();
        }
        predicate_gated_chip cpu_gate{cpu, &cpu_runnable, this};

        // V-blank-entry counter, used by the optional WRAM-write watcher
        // (MNEMOS_WRAM_WATCH). Not part of the architectural state.
        std::uint64_t frame_index{};

        // Controller ports 1/2. Each holds whichever peripheral the host
        // attached (3-button pad, 6-button pad, lightgun, mouse, multitap,
        // ...). The MMIO at $A10003/$A10005 routes byte reads + writes
        // through these; V-blank callbacks the per-device on_vblank() hook.
        std::array<std::unique_ptr<peripheral::device>, 2> ports{};

        void attach(int port, std::unique_ptr<peripheral::device> dev) noexcept {
            if (port >= 0 && port < 2) {
                ports[static_cast<std::size_t>(port)] = std::move(dev);
            }
        }

        [[nodiscard]] peripheral::device* port_device(int port) noexcept {
            return (port >= 0 && port < 2) ? ports[static_cast<std::size_t>(port)].get() : nullptr;
        }

        [[nodiscard]] std::uint8_t read_pad_port(int port) const noexcept {
            if (port < 0 || port >= 2) {
                return 0xFFU;
            }
            const auto& dev = ports[static_cast<std::size_t>(port)];
            return dev ? dev->read_data() : 0x7FU;
        }

        // The V-blank edge handler the VDP's vblank callback is wired to (Z80 INT
        // line, frame counter, per-port device timeouts). Public so an expansion
        // layer (the 32X machine) can re-wire the callback to its own wrapper and
        // still invoke the stock behaviour.
        void on_vblank(bool in_vblank);

        // System-level (non-chip) save/load (G7): the architectural state that
        // lives on the board rather than in a chip -- the I/O sub-controller
        // registers, the VDP word-access latches, the Z80 bus-arbitration lines +
        // bank window, and the cartridge mapper control latches (SRAM enable/WP,
        // SSF2 banks, EEPROM pins). The 5 chips + the work/Z80 RAM + SRAM bytes are
        // serialized separately by the machine-save path. Loading into a system
        // assembled from the SAME cartridge needs no bus re-wiring: every MMIO
        // closure reads these fields live, so restoring them is sufficient.
        void save_state(chips::state_writer& writer) const;
        void load_state(chips::state_reader& reader);
    };

    // 68000 boots from the ROM's reset vectors ($0 SSP, $4 PC). The caller is
    // responsible for ROM verification.
    [[nodiscard]] std::unique_ptr<genesis_system>
    assemble_genesis(std::vector<std::uint8_t> rom, const genesis_config& config = {});

} // namespace mnemos::manifests::genesis
