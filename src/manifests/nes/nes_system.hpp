#pragma once

#include "bus.hpp"            // topology::bus
#include "m6510.hpp"          // chips::cpu::m6510 (the 2A03 is a 6502 core)
#include "nes_mapper.hpp"     // manifests::nes::nes_mapper
#include "ppu2c02.hpp"        // chips::video::ppu2c02
#include "region.hpp"         // mnemos::video_region
#include "ricoh_2a03_apu.hpp" // chips::audio::ricoh_2a03_apu

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace mnemos::manifests::nes {

    struct nes_config final {
        // The NES is principally a 60 Hz (NTSC) machine; PAL timing is a later
        // increment, so the host frame cadence defaults to NTSC.
        mnemos::video_region video_region{mnemos::video_region::ntsc};
        // The 8 KiB Famicom Disk System BIOS (DISKSYS.ROM). When non-empty AND the
        // loaded image is a .fds disk, assemble_nes builds the FDS RAM adapter
        // instead of a cartridge. Empty for ordinary iNES carts.
        std::vector<std::uint8_t> fds_bios{};
        // Plug a Zapper light gun into controller port 2 (read through $4017). The
        // games that use it (Duck Hunt, Hogan's Alley, Wild Gunman, ...) replace the
        // second pad with the gun. Off by default.
        bool zapper{};
    };

    // A parsed iNES (.nes) image: the PRG/CHR banks plus the cartridge wiring the
    // 16-byte header declares.
    struct ines_image final {
        std::vector<std::uint8_t> prg; // PRG-ROM (16 or 32 KiB for NROM)
        std::vector<std::uint8_t> chr; // CHR-ROM (8 KiB); empty => CHR-RAM
        chips::video::ppu2c02::mirroring mirroring{chips::video::ppu2c02::mirroring::horizontal};
        int mapper{};      // iNES mapper number (0 = NROM)
        bool chr_is_ram{}; // header CHR count 0 => 8 KiB CHR-RAM
        bool battery{};    // flags6 bit1: $6000-$7FFF RAM is battery-backed (persist it)
        bool valid{};      // false if the header magic / sizes don't parse
    };

    // Parse an iNES image. `valid` is false (and the banks empty) when the magic
    // or the declared sizes don't fit the data.
    [[nodiscard]] ines_image parse_ines(std::span<const std::uint8_t> data);

    // A hand-wired NES (the NROM increment): the 2A03 (a 6502 with the on-chip
    // I/O port disabled) + the 2C02 PPU + the APU on a 16-bit little-endian bus.
    // The PPU is the frame source; it raises the CPU /NMI at vblank through an
    // injected callback. Heap-allocated and never moved -- the bus holds spans
    // into the member arrays and the MMIO / NMI handlers capture `this`.
    struct nes_system final {
        // Standard-pad buttons, in the $4016 serial-read order (A is shifted out
        // first). set_pad() takes this bitmask; the shift register emits one bit
        // per read.
        static constexpr std::uint8_t btn_a = 0x01U;
        static constexpr std::uint8_t btn_b = 0x02U;
        static constexpr std::uint8_t btn_select = 0x04U;
        static constexpr std::uint8_t btn_start = 0x08U;
        static constexpr std::uint8_t btn_up = 0x10U;
        static constexpr std::uint8_t btn_down = 0x20U;
        static constexpr std::uint8_t btn_left = 0x40U;
        static constexpr std::uint8_t btn_right = 0x80U;

        chips::cpu::m6510 cpu;
        chips::video::ppu2c02 ppu;
        chips::audio::ricoh_2a03_apu apu;
        topology::bus bus{16U, topology::endianness::little};

        std::array<std::uint8_t, 0x800> wram{};     // 2 KiB work RAM ($0000-$07FF, x4 mirror)
        std::array<std::uint8_t, 0x2000> prg_ram{}; // 8 KiB cartridge work/save RAM ($6000-$7FFF)
        std::vector<std::uint8_t> prg;              // PRG-ROM, mapped at $8000 by the mapper
        std::vector<std::uint8_t> chr;              // CHR (ROM or 8 KiB RAM), the PPU's $0000-$1FFF
        std::unique_ptr<nes_mapper> mapper;         // owns the $8000-$FFFF + CHR banking
        bool battery{};                             // the $6000 RAM is battery-backed (persist it)
        bool chr_is_ram{};                          // CHR is writable RAM (save it in a save-state)

        // Famicom Disk System (RP2C33 RAM adapter). For a .fds image these hold the
        // 8 KiB BIOS ($E000-$FFFF), the 32 KiB PRG-RAM ($6000-$DFFF) the BIOS loads
        // files into, and the raw disk sides the drive streams. `mapper` is the FDS
        // disk cartridge; the cart `prg`/`prg_ram` above stay empty.
        bool is_fds{};
        std::vector<std::uint8_t> fds_bios;
        std::vector<std::uint8_t> fds_ram;
        std::vector<std::uint8_t> fds_disk;

        // The cartridge (MMC3) and the APU (frame + DMC) share the CPU /IRQ line as
        // a wired-OR; each callback updates its source and republishes the union.
        bool mapper_irq{};
        bool apu_irq{};

        // Two standard pads. pad_buttons is the live state; pad_shift is the
        // per-port serial register the $4016/$4017 reads clock out.
        std::array<std::uint8_t, 2> pad_buttons{};
        std::array<std::uint8_t, 2> pad_shift{};
        bool pad_strobe{};

        // Zapper light gun on port 2 ($4017). When enabled, that port's read returns
        // the gun's light-sense (bit 3: 0 = light) + trigger (bit 4: 1 = pulled)
        // instead of a serial pad. aim_x/aim_y are the framebuffer pixel the gun
        // points at (negative = off-screen); the light sense samples the PPU output
        // there.
        bool zapper_enabled{};
        std::int16_t zapper_x{-1};
        std::int16_t zapper_y{-1};
        bool zapper_trigger{};

        // Set the live button bitmask for controller `port` (0 or 1).
        void set_pad(int port, std::uint8_t buttons) noexcept;
        // Update the Zapper aim (framebuffer pixel) + trigger.
        void set_zapper(int x, int y, bool trigger) noexcept;
        // $4016 write: bit 0 is the strobe; a high level (re)loads both shift
        // registers from the live button state.
        void write_controller_strobe(std::uint8_t value) noexcept;
        // $4016 / $4017 read: the next serial button bit (in bit 0) OR'd with the
        // open-bus high bits the data lines float to -- or, on port 2 with the Zapper
        // enabled, the gun's light-sense + trigger byte.
        [[nodiscard]] std::uint8_t read_controller(int port) noexcept;
        // True when the PPU output at the Zapper aim point is bright enough to trip
        // the photodiode.
        [[nodiscard]] bool zapper_light_detected() const noexcept;

        // (OAM-DMA cycle stall, APU frame IRQ, mid-frame NMI edges, CHR-RAM and
        // save-state arrive in later increments.)
    };

    // Build a runnable NES from a .nes image. An image that doesn't parse (or a
    // mapper this increment doesn't implement) still yields a machine -- it boots
    // a blank PRG -- so the player never crashes on an unsupported cart.
    [[nodiscard]] std::unique_ptr<nes_system> assemble_nes(std::span<const std::uint8_t> rom,
                                                           const nes_config& config = {});

} // namespace mnemos::manifests::nes
