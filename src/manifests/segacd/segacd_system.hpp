#pragma once

#include "bus.hpp"        // topology bus
#include "disc_image.hpp" // CD media
#include "m68000.hpp"     // sub-CPU
#include "rf5c68.hpp"     // PCM

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace mnemos::manifests::segacd {

    inline constexpr std::size_t prg_ram_size = 512U * 1024U;  // 0x80000 sub-CPU program RAM
    inline constexpr std::size_t word_ram_size = 256U * 1024U; // 0x40000 dual-port word RAM
    inline constexpr std::size_t backup_ram_size = 8U * 1024U; // 0x2000 battery backup RAM
    inline constexpr std::size_t bios_max_size = 256U * 1024U; // sub-CPU boot ROM ceiling
    inline constexpr std::size_t gate_array_size = 0x100;      // 256-byte gate-array block

    // Heap-allocated, never-moved Sega CD sub side: the sub-bus holds spans into
    // the member arrays and the MMIO handlers capture `this`. Phase B1 wires the
    // sub-CPU, its bus (PRG/word RAM + PCM + BIOS overlay), and the run/reset
    // control. The gate array, word-RAM 2M/1M banking, backup RAM, and the
    // sub-CPU IRQ controller arrive in B2/B3; the CDC/CDD and stamp ASIC in
    // phase C; Genesis main-side integration in phase D.
    struct segacd_system final {
        chips::cpu::m68000 sub_cpu;
        chips::audio::rf5c68 pcm;
        topology::bus sub_bus{24U, topology::endianness::big};

        std::array<std::uint8_t, prg_ram_size> prg_ram{};
        std::array<std::uint8_t, word_ram_size> word_ram{};
        std::array<std::uint8_t, backup_ram_size> backup_ram{};
        std::array<std::uint8_t, gate_array_size> gate_array{};

        std::vector<std::uint8_t> bios; // borrowed by the sub-bus (read overlay)
        bool sub_reset_asserted{true};  // held in reset until the main CPU releases it
        bool sub_busreq{false};         // main CPU holds the sub-CPU bus ($01 bit 1)

        // Sub-CPU IRQ source bits (pending/mask); priority level = bit index + 1.
        static constexpr std::uint8_t irq_graphics = 0x01U; // level 1 (stamp ASIC done)
        static constexpr std::uint8_t irq_ifl2 = 0x02U;     // level 2 (main->sub pulse)
        static constexpr std::uint8_t irq_timer = 0x04U;    // level 3
        static constexpr std::uint8_t irq_cdd = 0x08U;      // level 4 (CDD frame)
        static constexpr std::uint8_t irq_cdc = 0x10U;      // level 5 (CDC sector)
        static constexpr std::uint8_t irq_subcode = 0x20U;  // level 6
        std::uint8_t sub_irq_mask{};                        // gate-array $33
        std::uint8_t sub_irq_pending{};

        // CDD drive status codes (status-frame byte RS0).
        enum cdd_status_code : std::uint8_t {
            cdd_stop = 0x00,
            cdd_play = 0x01,
            cdd_seek = 0x02,
            cdd_scan = 0x03,
            cdd_pause = 0x04,
            cdd_open = 0x05,
            cdd_toc = 0x09,
            cdd_nodisc = 0x0B,
            cdd_end = 0x0C,
        };

        const mnemos::disc::disc_image* disc{}; // attached disc image (borrowed)
        std::array<std::uint8_t, 10> cdd_command{};
        std::array<std::uint8_t, 10> cdd_status{};
        std::uint8_t cdd_drive_status{cdd_nodisc};
        std::uint8_t cdd_pending_status{};
        int cdd_latency{};
        std::int32_t cdd_lba{};
        int cdd_track{};
        bool cdd_loaded{};
        // C1 seams (the real CDC + CD-DA arrive in C2/C3): a count of sectors
        // handed to the decoder, the last block header, and the CD-DA request.
        std::uint64_t cdc_sectors_decoded{};
        std::uint32_t last_sector_header{};
        bool cdda_active{};
        std::uint32_t cdda_start_lba{};
        std::uint32_t cdda_end_lba{};

        // Advance the sub-CPU by `cycles` of its clock. No-op while held in reset.
        void run_cycles(std::uint64_t cycles);
        // Release the sub-CPU from reset and boot it from the $0/$4 vectors (which
        // come from the BIOS overlay, or from PRG-RAM in tests with no BIOS).
        void release_sub_reset();
        void assert_sub_reset() noexcept { sub_reset_asserted = true; }
        void reset();

        // Gate-array register access. The sub side ($FF8000 / $0FF800 on the
        // sub-bus) and the main side ($A12000, wired in phase D) differ on the
        // memory-mode register ($03): the main CPU owns the PRG-RAM bank + DMNA,
        // the sub-CPU owns RET. $01 controls the sub-CPU reset / bus request.
        [[nodiscard]] std::uint8_t gate_read(std::uint8_t offset) noexcept;
        void gate_write_main(std::uint8_t offset, std::uint8_t value);
        void gate_write_sub(std::uint8_t offset, std::uint8_t value);

        // Sub-CPU interrupt controller. A source latches its bit into
        // sub_irq_pending; the highest-priority enabled (masked by $33) pending
        // source drives the sub-CPU IPL. $36 bit 0 acknowledges all pending.
        // Driven by phase-C devices (CDD/CDC/timer) and the main CPU (IFL2).
        void raise_sub_irq(std::uint8_t source_bit);
        void update_sub_irq();
        [[nodiscard]] int pending_irq_level() const noexcept;

        // CDD (drive). attach_disc plugs a borrowed disc image; cdd_process_command
        // runs when the CPU commits a command ($4B); cdd_update is the 75 Hz
        // CD-frame tick that advances the read head and resolves pending seeks.
        void attach_disc(const mnemos::disc::disc_image* image);
        void cdd_process_command();
        void cdd_update();

      private:
        void cdd_set_status();
        void cdd_report_toc();
        void cdd_commit_status();
        [[nodiscard]] std::int32_t cdd_seek_target_lba() const;
        [[nodiscard]] std::uint32_t disc_total_lbas() const;
        [[nodiscard]] bool disc_lba_is_data(std::int32_t lba) const;
        [[nodiscard]] int disc_track_of_lba(std::int32_t lba) const;
        void feed_cdc_sector(std::uint32_t header);             // C1 seam (C2: real CDC)
        void cdda_play(std::uint32_t start, std::uint32_t end); // C1 seam (C3: real CD-DA)
        void cdda_stop();
    };

    // Build a Sega CD sub side and wire the sub-bus. `bios` may be empty (the
    // sub-CPU then boots from whatever is loaded into PRG-RAM, e.g. unit tests).
    // The sub-CPU starts held in reset; call release_sub_reset() to run it.
    [[nodiscard]] std::unique_ptr<segacd_system>
    assemble_segacd(std::vector<std::uint8_t> bios = {});

} // namespace mnemos::manifests::segacd
