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
    // sub-CPU, its bus (PRG/word RAM + PCM), and the run/reset
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

        bool sub_reset_asserted{true}; // held in reset until the main CPU releases it
        bool sub_busreq{false};        // main CPU holds the sub-CPU bus ($01 bit 1)
        std::uint8_t sub_led{};        // sub-side $00 write target (LED control)

        // Sub-CPU IRQ source bits (pending/mask). The gate-array $33 mask uses
        // bit N = level N (bit 0 unused), so the pending bits use the same
        // convention -- pending & mask then aligns. (The Emu reference used bit
        // N-1, which never aligned with the BIOS mask; that off-by-one blocked
        // the main<->sub IFL2 handshake and no disc/game would boot.)
        static constexpr std::uint8_t irq_graphics = 0x02U; // level 1 (stamp ASIC done)
        static constexpr std::uint8_t irq_ifl2 = 0x04U;     // level 2 (main->sub pulse)
        static constexpr std::uint8_t irq_timer = 0x08U;    // level 3
        static constexpr std::uint8_t irq_cdd = 0x10U;      // level 4 (CDD frame)
        static constexpr std::uint8_t irq_cdc = 0x20U;      // level 5 (CDC sector)
        static constexpr std::uint8_t irq_subcode = 0x40U;  // level 6
        std::uint8_t sub_irq_mask{};                        // gate-array $33
        std::uint8_t sub_irq_pending{};

        // Sub-CPU timer (gate-array $31). Period = (timer_word + 1) * 385 sub
        // cycles (~30.72 us @ 12.5 MHz); raises the level-3 IRQ. 0 = disabled.
        std::uint8_t timer_word{};
        std::uint32_t timer_cycle_acc{};

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
        // One CD frame of sector-sync acquisition between the PLAY promotion and
        // the first decode: a real drive reports 'playing' first, then the first
        // complete sector passes under the pickup a frame later. The BIOS read
        // driver arms its per-sector (DECI) service in that gap; without it the
        // first sector's snapshot is consumed by the arm itself and the driver
        // misses its approach window.
        int cdd_play_warmup{};
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
        std::uint32_t cdda_current_lba{};
        std::uint16_t cdda_sample_in_sector{};
        bool cdda_loop{};
        // CD-DA sector cache: read each 2352-byte raw sector once (not on every
        // 44.1 kHz sample). Refilled when the playing LBA changes.
        std::array<std::uint8_t, mnemos::disc::disc_image::raw_sector_size> cdda_sector{};
        std::uint32_t cdda_sector_lba{};
        bool cdda_sector_valid{};

        // CDC (LC8951) state. cdc_ram is the 16 KB decode ring (+2352 headroom).
        std::array<std::uint8_t, 0x4000 + 2352> cdc_ram{};
        std::uint8_t cdc_ifstat{0xFFU};
        std::uint8_t cdc_ifctrl{};
        std::uint16_t cdc_dbc{};
        std::uint16_t cdc_dac{};
        std::uint16_t cdc_pt{};
        std::uint16_t cdc_wa{};
        std::array<std::uint8_t, 2> cdc_ctrl{};
        std::array<std::array<std::uint8_t, 4>, 2> cdc_head{};
        std::array<std::uint8_t, 4> cdc_stat{};
        std::uint8_t cdc_ar{};
        std::uint8_t cdc_irq{};
        int cdc_dma_dest{};

        // Stamp / rotation ASIC config (gate $58-$6B).
        std::uint16_t stamp_size{};
        std::uint16_t stamp_map_addr{};
        std::uint16_t img_buf_v_cell{};
        std::uint16_t img_buf_vector{};
        std::uint16_t img_v_step{};
        std::uint16_t img_h_step{};
        std::uint16_t img_buf_width{};
        std::uint16_t img_buf_height{};
        std::uint16_t img_buf_offset{};
        std::uint16_t trace_vector_addr{};

        // Advance the sub-CPU by `cycles` of its clock. No-op while held in reset.
        void run_cycles(std::uint64_t cycles);
        // Release the sub-CPU from reset and boot it from the $0/$4 vectors in
        // PRG-RAM (the main BIOS loads the Sub-CPU BIOS there before releasing it).
        void release_sub_reset();
        // Monotone sub-CPU timeline position for pacing. release_sub_reset()
        // zeroes the CPU's elapsed counter, so pacing anchors must not read
        // elapsed_cycles() directly: a mid-run SRES toggle would replay every
        // pre-reset cycle as one burst. The base absorbs each release edge.
        [[nodiscard]] std::uint64_t sub_position() const noexcept {
            return sub_elapsed_base + sub_cpu.elapsed_cycles();
        }
        std::uint64_t sub_elapsed_base{0}; // accumulated pre-release elapsed counts
        void assert_sub_reset() noexcept { sub_reset_asserted = true; }
        void reset();

        // Gate-array register access. The sub side ($FF8000 / $0FF800 on the
        // sub-bus) and the main side ($A12000, wired in phase D) differ on the
        // memory-mode register ($03): the main CPU owns the PRG-RAM bank + DMNA,
        // the sub-CPU owns RET. $01 controls the sub-CPU reset / bus request.
        [[nodiscard]] std::uint8_t gate_read(std::uint8_t offset);
        void gate_write_main(std::uint8_t offset, std::uint8_t value);
        void gate_write_sub(std::uint8_t offset, std::uint8_t value);

        // Sub-CPU interrupt controller. A source latches its bit into
        // sub_irq_pending; the highest-priority enabled (masked by $33) pending
        // source drives the sub-CPU IPL. Pending bits retire on the IACK cycle
        // (acknowledge_irq), per level -- never a clear-all.
        // Driven by phase-C devices (CDD/CDC/timer) and the main CPU (IFL2).
        void raise_sub_irq(std::uint8_t source_bit);
        void update_sub_irq();
        // Acknowledge (retire) one IRQ level -- the sub-CPU IACK cycle: clears that
        // level's pending bit (and the main-side IFL2 flag for level 2). Public so
        // the IRQ controller can be unit-tested without driving a real IACK cycle.
        void acknowledge_irq(int level);
        [[nodiscard]] int pending_irq_level() const noexcept;

        // CDD (drive). attach_disc plugs a borrowed disc image; cdd_process_command
        // runs when the CPU commits a command ($4B); cdd_update is the 75 Hz
        // CD-frame tick that advances the read head and resolves pending seeks.
        void attach_disc(const mnemos::disc::disc_image* image);
        void cdd_process_command();
        void cdd_update();

        // Pull the next 44.1 kHz stereo CD-DA sample (the host audio path drains
        // these while an audio track plays). Returns false when none is available
        // (no disc, not playing audio, or the sector read failed).
        [[nodiscard]] bool cdda_next_sample(std::int16_t& out_l, std::int16_t& out_r);

      private:
        void cdd_set_status();
        void cdd_report_toc();
        void cdd_commit_status();
        [[nodiscard]] std::int32_t cdd_seek_target_lba() const;
        [[nodiscard]] std::uint32_t disc_total_lbas() const;
        [[nodiscard]] bool disc_lba_is_data(std::int32_t lba) const;
        [[nodiscard]] int disc_track_of_lba(std::int32_t lba) const;
        // CDC (LC8951). cdc_decoder_update fills the ring buffer from the disc
        // and raises the decoder IRQ; cdc_reg_w/r are the indirect register file;
        // the DMA paths move decoded data to PRG/word/PCM RAM or the host port.
        void cdc_decoder_update(std::uint32_t header);
        void cdc_reg_w(std::uint8_t value);
        [[nodiscard]] std::uint8_t cdc_reg_r();
        void cdc_update_irq(std::uint8_t prev_irq);
        void cdc_dma_init();
        void cdc_dma_run();
        void cdc_dma_finish();
        void cdc_host_advance();
        void cdda_play(std::uint32_t start, std::uint32_t end);
        void cdda_stop();
        // Stamp / rotation graphics ASIC ($58-$6B).
        void stamp_reg_write(std::uint8_t offset, std::uint8_t value);
        void stamp_renderer_run();
        void graphics_complete();
    };

    // Build a Sega CD sub side and wire the sub-bus. `bios` may be empty (the
    // sub-CPU then boots from whatever is loaded into PRG-RAM, e.g. unit tests).
    // Opt-in disc-boot debug tracing (MNEMOS_SEGACD_TRACE), shared by the gate
    // array, CDD, and the main-side bridge.
    [[nodiscard]] bool segacd_trace_enabled() noexcept;

    // The sub-CPU starts held in reset; call release_sub_reset() to run it. The
    // sub boots from PRG-RAM (loaded by the main BIOS), so no BIOS image is needed.
    [[nodiscard]] std::unique_ptr<segacd_system> assemble_segacd();

} // namespace mnemos::manifests::segacd
