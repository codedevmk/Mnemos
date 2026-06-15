#pragma once

#include "asset_views.hpp"
#include "chip.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <vector>

namespace mnemos::chips::video {

    // Sega Genesis / Mega Drive VDP (Sega 315-5313, manufactured by Yamaha as the
    // YM7101). The video display processor: 64KB VRAM, 64-entry CRAM (9-bit colour),
    // 40-entry VSRAM (vertical scroll), 24 control registers, the two-word command
    // port protocol, a DMA engine (68K->VRAM, VRAM fill, VRAM copy), the H/V counter,
    // and V-blank (IRQ6) / H-blank (IRQ4) / external (IRQ2) interrupts.
    //
    // Built in phases. THIS phase is the control plane: VRAM/CRAM/VSRAM with the
    // hardware access quirks, the full register file, the command/data ports with
    // address auto-increment and the read-prefetch buffer, the DMA engine, the H/V
    // counter readback (hardware cycle tables), the status register, and the
    // scanline-accurate timing + interrupt engine driven by tick(). Rendering is
    // stubbed (the framebuffer stays blank); the background/window/sprite pixel
    // pipeline arrives in phase 2.
    //
    // tick(cycles) advances the raster by that many master clocks (a line is 3420);
    // each completed line updates timing, the H-int counter, and the pending IRQ
    // level (delivered via the IRQ callback). The 68000 accesses the chip through the
    // 16-bit read16/write16 ports ($C00000-$C0001F); DMA from 68K space is serviced
    // through the host-provided dma_read callback.
    class genesis_vdp final : public ivideo, public immio {
      public:
        static constexpr std::size_t vram_size = 0x10000;
        static constexpr int cram_entries = 64;
        static constexpr int vsram_entries = 40;
        static constexpr int register_count = 24;
        static constexpr int sprites_max = 80;
        static constexpr int scanlines_ntsc = 262;
        static constexpr int scanlines_pal = 313;
        static constexpr int fb_width = 320;  // H40 max; H32 (256) renders left-aligned
        static constexpr int fb_height = 480; // interlace-2 PAL worst case

        // A line is a fixed 3420 master clocks for both H32 and H40 (only the dot
        // clock differs); the H/V readback tables are indexed against it.
        static constexpr int master_clocks_per_line = 3420;

        genesis_vdp() { reset(reset_kind::power_on); }

        [[nodiscard]] chip_metadata metadata() const noexcept override;
        void tick(std::uint64_t cycles) override;
        void reset(reset_kind kind) override;

        void save_state(state_writer& writer) const override;
        void load_state(state_reader& reader) override;

        [[nodiscard]] instrumentation::ichip_introspection& introspection() noexcept override;
        void configure(const config_table& cfg, const callback_table& callbacks) override;

        // ivideo: frame counter + framebuffer view.
        [[nodiscard]] std::uint64_t frame_index() const noexcept override { return frame_index_; }
        [[nodiscard]] frame_buffer_view framebuffer() const noexcept override;

        // immio: generic byte access (mirrors read8/write8). The 16-bit ports are the
        // authoritative path -- the two-word command protocol requires whole-word
        // writes, so the Genesis manifest routes 68000 word accesses to write16.
        [[nodiscard]] std::uint8_t mmio_read(std::uint16_t offset) override {
            return read8(offset);
        }
        void mmio_write(std::uint16_t offset, std::uint8_t value) override {
            write8(offset, value);
        }

        // 68000-facing memory-mapped access; `offset` is relative to $C00000.
        [[nodiscard]] std::uint16_t read16(std::uint32_t offset) noexcept;
        void write16(std::uint32_t offset, std::uint16_t value) noexcept;
        [[nodiscard]] std::uint8_t read8(std::uint32_t offset) noexcept;
        void write8(std::uint32_t offset, std::uint8_t value) noexcept;

        // Host hooks. dma_read supplies a 16-bit word from 68K address space during a
        // 68K->VDP DMA. irq_callback is invoked with the new pending IRQ level (0/2/4/6)
        // whenever it changes so the system can drive the 68000 IPL pins.
        void set_dma_read(std::function<std::uint16_t(std::uint32_t)> fn) noexcept {
            dma_read_ = std::move(fn);
        }
        void set_irq_callback(std::function<void(int level)> cb) noexcept {
            irq_callback_ = std::move(cb);
        }
        // One-instruction-delayed IRQ raise: the VDP invokes this when a
        // V-int-enable register write must postpone the IRQ until the CPU
        // has executed one more instruction, so the IRQ's saved PC points
        // past that instruction rather than past the enabling MOVE.
        void set_delayed_irq_callback(std::function<void(int level)> cb) noexcept {
            delayed_irq_callback_ = std::move(cb);
        }
        // Invoked whenever the in_vblank state changes (rising edge true, falling
        // false). The Genesis system wires frame bookkeeping (frame counter,
        // pad timeouts) to this edge.
        void set_vblank_callback(std::function<void(bool in_vblank)> cb) noexcept {
            vblank_callback_ = std::move(cb);
        }
        // Invoked on the Z80 /INT line edges: asserted at V-blank entry,
        // released ONE SCANLINE later (the hardware pulse width). Deliberately
        // narrower than the vblank callback -- a sound driver whose handler
        // returns mid-V-blank must not be re-interrupted into a second tick.
        void set_z80_int_callback(std::function<void(bool asserted)> cb) noexcept {
            z80_int_callback_ = std::move(cb);
        }
        // Invoked on each H-interrupt latch edge (the line counter expired with
        // IE1 set -- the /HINT output the 32X adapter taps). Plain Genesis
        // systems leave this unset; the 68000 keeps its own pending-level path.
        void set_hint_callback(std::function<void()> cb) noexcept {
            hint_callback_ = std::move(cb);
        }
        [[nodiscard]] bool in_vblank() const noexcept { return in_vblank_; }
        void set_pal(bool pal) noexcept;

        // External (level-2) interrupt edge (light-gun TH falling edge, etc.). Gated
        // internally by IE2 (register 0 bit 3); dropped when IE2 is clear.
        void signal_external_int() noexcept;

        [[nodiscard]] int pending_irq_level() const noexcept;
        [[nodiscard]] bool irq_asserted() const noexcept { return pending_irq_level() != 0; }

        // Acknowledge (clear) the interrupt request at `level` -- the 68000 IACK cycle.
        // The V-blank (6) and H-blank (4) requests are normally cleared this way; the
        // status flag (vint_happened) stays set until a status read.
        void acknowledge_irq(int level) noexcept;

        // Geometry + H/V readback (the manifest samples HV against bus time).
        [[nodiscard]] int visible_width() const noexcept;
        [[nodiscard]] int visible_height() const noexcept;
        [[nodiscard]] int field_height() const noexcept;
        [[nodiscard]] int total_hclocks() const noexcept;
        [[nodiscard]] std::uint8_t hcounter_readback(int sample) const noexcept;
        [[nodiscard]] bool hcounter_in_hblank(int sample) const noexcept;
        [[nodiscard]] std::uint8_t vcounter_readback(int scanline, bool odd_frame) const noexcept;

        // Introspection / test accessors.
        [[nodiscard]] std::uint8_t reg(int index) const noexcept {
            return (index >= 0 && index < register_count) ? reg_[static_cast<std::size_t>(index)]
                                                          : std::uint8_t{0};
        }
        [[nodiscard]] std::uint16_t vram16(std::uint32_t addr) const noexcept;
        [[nodiscard]] std::uint16_t cram(int idx) const noexcept {
            return cram_[static_cast<std::size_t>(idx) & 0x3FU];
        }
        [[nodiscard]] std::uint16_t vsram(int idx) const noexcept;
        [[nodiscard]] int scanline() const noexcept { return scanline_; }
        // 32X composition support: opt-in per-pixel backdrop tracking. When
        // enabled, each rendered line records which pixels carried no plane
        // or sprite pixel (the backdrop fill) -- the cartridge connector
        // exposes this "transparent" signal and the 32X mixes its priority-0
        // pixels only where it is set. Off (and cost-free) for plain Genesis.
        void enable_backdrop_mask(bool on);
        [[nodiscard]] const std::uint8_t* backdrop_row(int display_line) const noexcept;
        [[nodiscard]] bool dma_busy() const noexcept { return dma_busy_; }

        // Real hardware stalls the 68000 while VDP DMA holds the bus. Mnemos
        // executes DMA payloads synchronously inside the control / data-port
        // write, so the chip's data is correct -- but to keep the 68K's wall-
        // clock work-per-frame budget right (which game code depends on for
        // timer-driven progress), the VDP estimates the DMA's master-clock
        // cost and asks the host scheduler to stop the 68K until that budget
        // is consumed. `dma_stall_master_cycles_` is the remaining stall debt;
        // `tick(cycles)` drains it. The Genesis manifest reads this via a
        // gated_chip around the 68K (see genesis_system.cpp).
        //
        // The same gate also carries write-FIFO back-pressure
        // (`fifo_stall_master_cycles_`): during active display a full data-port
        // FIFO stalls the 68K just like DMA, so the gate can be active with no
        // DMA in flight. See fifo_data_write().
        [[nodiscard]] bool dma_stall_active() const noexcept {
            return dma_stall_master_cycles_ > 0 || fifo_stall_master_cycles_ > 0;
        }
        [[nodiscard]] bool dma_fill_pending() const noexcept { return dma_fill_pending_; }

        // Master-clock position WITHIN the current scanline (0..master_clocks_per_line).
        // The FIFO-stall model (data_write) indexes the per-line access-slot table
        // by this. It is `line_accumulator_` clamped to the line length; accuracy is
        // bounded by the 68K's instruction-step granularity (the VDP has not yet
        // ticked the in-flight instruction's cycles when a write arrives), which is
        // the documented sub-instruction residual.
        [[nodiscard]] int current_line_master() const noexcept {
            const std::int64_t pos = line_accumulator_;
            if (pos < 0) {
                return 0;
            }
            if (pos > master_clocks_per_line) {
                return master_clocks_per_line;
            }
            return static_cast<int>(pos);
        }

        [[nodiscard]] std::uint8_t cmd_code() const noexcept { return cmd_code_; }
        [[nodiscard]] std::uint32_t cmd_addr() const noexcept { return cmd_addr_; }
        [[nodiscard]] std::span<const register_descriptor> register_snapshot() noexcept;

        // Diagnostic: rising-edge VINT count (vblank_pending_ false->true).
        [[nodiscard]] std::uint32_t vint_fired_count() const noexcept { return vint_fired_count_; }
        [[nodiscard]] std::uint32_t vint_drain_count() const noexcept { return vint_drain_count_; }
        [[nodiscard]] std::uint32_t vint_enabled_at_drain_count() const noexcept {
            return vint_enabled_at_drain_count_;
        }

      private:
        // Exposes the chip's bulk memories (VRAM/CRAM/VSRAM/regs) as
        // `memory_view`s, the architectural register file as a `register_view`,
        // and a lazily-rendered plane-A `debug_layer`. The player's
        // --screenshot path consumes these without depending on the VDP type.
        class introspection_surface final : public instrumentation::ichip_introspection {
          public:
            explicit introspection_surface(genesis_vdp& owner) noexcept;

            [[nodiscard]] std::span<instrumentation::memory_view* const> memory_views() override {
                return mem_table_;
            }
            [[nodiscard]] instrumentation::register_view* registers() override {
                return &registers_impl_;
            }
            [[nodiscard]] std::span<instrumentation::debug_layer* const> debug_layers() override {
                return layer_table_;
            }
            [[nodiscard]] instrumentation::asset_source* assets() override { return &assets_; }

          private:
            // Decodes the VDP's live graphics into the asset-extraction contract:
            // the four 16-colour CRAM palettes, the 2048-pattern tile sheet, and
            // one image per sprite in the SAT linked list. Buffers rebuilt per
            // call (the contract's tick lifetime rule).
            class asset_source_impl final : public instrumentation::asset_source {
              public:
                explicit asset_source_impl(genesis_vdp& owner) noexcept : owner_(&owner) {}
                [[nodiscard]] std::span<const instrumentation::palette_view>
                palettes() const override;
                [[nodiscard]] std::span<const instrumentation::graphic_asset>
                graphics() const override;

              private:
                genesis_vdp* owner_;
                mutable std::array<std::array<std::uint32_t, 16>, 4> pal_rgb_{};
                mutable std::array<instrumentation::palette_view, 4> palettes_{};
                mutable std::vector<std::uint8_t> tileset_px_{};
                mutable std::vector<std::uint8_t> sprite_px_{};
                mutable std::vector<std::string> names_{};
                mutable std::vector<instrumentation::graphic_asset> assets_{};
            };

            class registers_impl final : public instrumentation::register_view {
              public:
                explicit registers_impl(genesis_vdp& owner) noexcept : owner_(&owner) {}
                [[nodiscard]] std::span<const register_descriptor> registers() override;

              private:
                genesis_vdp* owner_;
            };

            // Composes a scroll plane / the window plane into an RGB scene: the
            // full nametable map laid out with per-tile palette + H/V flip,
            // resolved through CRAM (no scroll, no sprites). plane_a/plane_b read
            // the scroll-plane nametables at the scroll-plane size; window reads
            // its own nametable at its pitch (64/32 cells in H40/H32, wider than
            // the visible display). Interlace mode 2 uses 16-px-tall cells.
            class plane_layer_impl final : public instrumentation::debug_layer {
              public:
                enum class which : std::uint8_t { plane_a, plane_b, window };
                plane_layer_impl(genesis_vdp& owner, which w) noexcept
                    : owner_(&owner), which_(w) {}
                [[nodiscard]] std::string_view name() const noexcept override {
                    switch (which_) {
                    case which::plane_a:
                        return "plane_a";
                    case which::plane_b:
                        return "plane_b";
                    case which::window:
                        return "window";
                    }
                    return "plane";
                }
                [[nodiscard]] frame_buffer_view view() const override;

              private:
                genesis_vdp* owner_;
                which which_;
                mutable std::vector<std::uint32_t> buf_{};
                mutable std::uint32_t width_{};
                mutable std::uint32_t height_{};
            };

            instrumentation::span_memory_view vram_view_;
            instrumentation::span_memory_view cram_view_;
            instrumentation::span_memory_view vsram_view_;
            instrumentation::span_memory_view regs_view_;
            registers_impl registers_impl_;
            plane_layer_impl plane_a_;
            plane_layer_impl plane_b_;
            plane_layer_impl window_;
            asset_source_impl assets_;

            std::array<instrumentation::memory_view*, 4> mem_table_{};
            std::array<instrumentation::debug_layer*, 3> layer_table_{};
        };

        // ---- register-field decode ----
        [[nodiscard]] bool hint_enabled() const noexcept { return (reg_[0] & 0x10U) != 0U; }
        [[nodiscard]] bool ext_int_enabled() const noexcept { return (reg_[0] & 0x08U) != 0U; }
        [[nodiscard]] bool hv_latch_enabled() const noexcept { return (reg_[0] & 0x02U) != 0U; }
        [[nodiscard]] bool display_enabled() const noexcept { return (reg_[1] & 0x40U) != 0U; }
        [[nodiscard]] bool vint_enabled() const noexcept { return (reg_[1] & 0x20U) != 0U; }
        [[nodiscard]] bool dma_enabled() const noexcept { return (reg_[1] & 0x10U) != 0U; }
        [[nodiscard]] bool m5_mode() const noexcept { return (reg_[1] & 0x04U) != 0U; }
        [[nodiscard]] bool v30_mode() const noexcept { return (reg_[1] & 0x08U) != 0U; }
        [[nodiscard]] bool h40_mode() const noexcept { return (reg_[12] & 0x81U) != 0U; }
        [[nodiscard]] int interlace_field() const noexcept { return (reg_[12] >> 1U) & 0x03; }
        [[nodiscard]] bool interlace_enabled() const noexcept { return interlace_field() != 0; }
        [[nodiscard]] std::uint8_t autoincrement() const noexcept { return reg_[15]; }
        [[nodiscard]] std::uint16_t dma_length() const noexcept;
        [[nodiscard]] std::uint32_t dma_source() const noexcept;
        [[nodiscard]] int dma_type() const noexcept { return (reg_[23] >> 6U) & 0x03; }

        // ---- rendering register decode ----
        [[nodiscard]] std::uint32_t nta_base() const noexcept {
            return (static_cast<std::uint32_t>(reg_[2]) & 0x38U) << 10U;
        }
        [[nodiscard]] std::uint32_t ntb_base() const noexcept {
            return (static_cast<std::uint32_t>(reg_[4]) & 0x07U) << 13U;
        }
        [[nodiscard]] std::uint32_t ntw_base() const noexcept {
            return (static_cast<std::uint32_t>(reg_[3]) & 0x3EU) << 10U;
        }
        [[nodiscard]] std::uint32_t sat_base() const noexcept {
            return (static_cast<std::uint32_t>(reg_[5]) & 0x7FU) << 9U;
        }
        [[nodiscard]] std::uint32_t hscroll_base() const noexcept {
            return (static_cast<std::uint32_t>(reg_[13]) & 0x3FU) << 10U;
        }
        [[nodiscard]] int bg_index() const noexcept {
            return ((reg_[7] >> 4U) & 0x03) * 16 + (reg_[7] & 0x0F);
        }
        [[nodiscard]] bool blank_left() const noexcept { return (reg_[0] & 0x20U) != 0U; }
        [[nodiscard]] bool sh_enabled() const noexcept { return (reg_[12] & 0x08U) != 0U; }
        [[nodiscard]] int hscroll_mode() const noexcept { return reg_[11] & 0x03; }
        [[nodiscard]] bool vscroll_per_column() const noexcept { return (reg_[11] & 0x04U) != 0U; }
        [[nodiscard]] int hsize_cells() const noexcept;
        [[nodiscard]] int vsize_cells() const noexcept;
        [[nodiscard]] bool interlace_mode2() const noexcept { return interlace_field() == 3; }
        [[nodiscard]] int display_line_for_field(int line) const noexcept;
        [[nodiscard]] int source_line_for_field(int line) const noexcept;

        // ---- memory access primitives ----
        [[nodiscard]] std::uint16_t vram_read16(std::uint32_t addr) const noexcept;
        void vram_write16(std::uint32_t addr, std::uint16_t value) noexcept;
        void vram_write8(std::uint32_t addr, std::uint8_t value) noexcept;
        void cram_write(int idx, std::uint16_t value) noexcept;
        void vsram_write(int idx, std::uint16_t value) noexcept;
        void write_target_word(int code_low, std::uint16_t word) noexcept;
        [[nodiscard]] std::uint16_t data_prefetch() noexcept;

        // ---- ports + DMA ----
        void ctrl_write(std::uint16_t value) noexcept;
        void data_write(std::uint16_t value) noexcept;
        [[nodiscard]] std::uint16_t data_read() noexcept;
        [[nodiscard]] std::uint16_t status_read() noexcept;
        [[nodiscard]] std::uint16_t hv_counter_live() const noexcept;
        void dma_transfer_step(std::uint16_t word) noexcept;
        void dma_copy_step() noexcept;
        void dma_fill_step() noexcept;

        // ---- timing + interrupts ----
        void run_scanline() noexcept;
        void refresh_irq() noexcept;

        // ---- rendering (phase 2) ----
        // Pixel line-buffer markers: bits 5-0 = CRAM index, bit 6 = priority,
        // bit 7 = "sourced from a sprite" (for shadow/highlight rules).
        static constexpr std::uint8_t pix_priority = 0x40;
        static constexpr std::uint8_t pix_source_sprite = 0x80;
        // Shade levels for shadow/highlight.
        static constexpr std::uint8_t shade_shadow = 0;
        static constexpr std::uint8_t shade_normal = 1;
        static constexpr std::uint8_t shade_highlight = 2;

        void fetch_pattern_row(int tile_idx, int row, bool hflip, bool interlace2,
                               std::uint8_t* pix) const noexcept;
        void render_scroll_plane(std::uint32_t nt_base, int line, bool is_plane_a,
                                 std::uint8_t* linebuf) const noexcept;
        void render_window(int line, std::uint8_t* linebuf) const noexcept;
        void render_sprites(int line, std::uint8_t* linebuf, std::uint8_t* shade,
                            std::uint8_t* backdrop) noexcept;
        void render_scanline(int line) noexcept;
        void fill_line_background(int display_line) noexcept;
        [[nodiscard]] static std::uint32_t cram_to_rgb(std::uint16_t cram_value,
                                                       std::uint8_t shade) noexcept;

        // Memory.
        std::array<std::uint8_t, vram_size> vram_{};
        std::array<std::uint16_t, cram_entries> cram_{};
        std::array<std::uint16_t, vsram_entries> vsram_{};
        std::array<std::uint8_t, register_count> reg_{};

        // Command-port state machine.
        std::uint32_t cmd_addr_{};
        std::uint8_t cmd_code_{};
        bool cmd_pending_{};
        std::uint16_t cmd_first_{};
        std::uint16_t read_buffer_{};

        // DMA state.
        bool dma_fill_pending_{};
        std::uint8_t dma_fill_byte_{};
        std::uint8_t dma_fill_code_{};
        std::uint16_t dma_fill_word_{};
        // Auto-increment latched when the fill is armed: a fill runs at the
        // triggering data-port write, but the program may rewrite reg $0F (for
        // its following normal writes) before that write lands, so the live
        // value would stride the fill wrong. The arm-time value is what clears.
        std::uint8_t dma_fill_increment_{};
        std::uint32_t dma_source_{};
        bool dma_busy_{};
        // Remaining master-clock cycles the VDP's DMA is conceptually still
        // holding the bus. Decremented in tick(); when > 0, the host gates
        // the 68000 off. ONLY set for 68K-bus DMAs (types 0 and 1: 68K to
        // CRAM/VSRAM and 68K to VRAM). VRAM fill and VRAM copy (types 2
        // and 3) run in parallel with the CPU on real hardware.
        std::int64_t dma_stall_master_cycles_{};
        // Remaining master-clock cycles the DMA-busy *status bit* should
        // stay set. Independent from dma_stall_master_cycles_ because VRAM
        // fill/copy keep busy=1 (so a polling loop spins) without locking
        // the 68K out. Decremented in tick(); when 0, dma_busy_ flips back.
        std::int64_t dma_busy_master_cycles_{};

        // VDP write-FIFO back-pressure. During active
        // display the 68K can outrun the VDP's data-port drain rate; when the
        // 4-entry FIFO fills, the 68K stalls until the oldest entry drains.
        // fifo_drain_[i] = the within-line master-cycle at which slot i drains
        // (may exceed master_clocks_per_line when it spills into the next line).
        // fifo_idx_ = next slot to write. fifo_stall_master_cycles_ accumulates
        // the stall debt, folded into the dma_stall gate so the host pauses the
        // 68K exactly as for DMA. All saved/loaded + reset with the chip.
        std::array<std::int64_t, 4> fifo_drain_{};
        int fifo_idx_{};
        std::int64_t fifo_stall_master_cycles_{};
        // Write-accept gating (config "write_accept", default on): master-cycle
        // just past the last accepted data-port write, so a back-to-back burst
        // paces to one VDP access slot per 16-bit word (hardware /DTACK) instead
        // of opcode cadence. Kept in the current line's frame of reference;
        // shifted on line wrap.
        bool write_accept_{true};
        std::int64_t fifo_accept_cursor_master_{};

        // Master-clock duration of a DMA of `length_units` units against the
        // current display state. `dma_type`:
        //   0 = 68K -> CRAM / VSRAM   (1 access slot per word)
        //   1 = 68K -> VRAM           (2 access slots per word)
        //   2 = VRAM fill             (1 access slot per byte, treated as VRAM)
        //   3 = VRAM copy             (2 access slots per byte, read + write)
        // For 68K-bus DMA (types 0/1) this feeds dma_stall_master_cycles_ (the
        // 68K is held off the bus); for VDP-internal DMA (types 2/3) it feeds
        // dma_busy_master_cycles_ (the busy status bit is held, 68K runs free).
        [[nodiscard]] std::int64_t estimate_dma_transfer_cycles(std::uint32_t length_units,
                                                                int dma_type) const noexcept;

        // FIFO back-pressure for a single 68K data-port write. Advances the FIFO
        // drain schedule by one slot and, if the FIFO is full at the current
        // within-line position, accrues the 68K stall onto
        // fifo_stall_master_cycles_. No-op outside active display.
        void fifo_data_write() noexcept;

        // Latch the H-interrupt request and pulse the /HINT output hook.
        void latch_hint() noexcept {
            hblank_pending_ = true;
            if (hint_callback_) {
                hint_callback_();
            }
        }

        // Diagnostic counters (see accessors above).
        std::uint32_t vint_fired_count_{};
        std::uint32_t vint_drain_count_{};
        std::uint32_t vint_enabled_at_drain_count_{};

        // Timing / position.
        int scanline_{};
        int hcounter_{};
        int vcounter_{};
        int total_scanlines_{scanlines_ntsc};
        bool pal_mode_{};
        bool odd_frame_{};
        int hint_counter_{};
        bool hv_latched_{};
        std::uint16_t hv_latch_value_{};
        std::int64_t line_accumulator_{};

        // Status + interrupt flags.
        bool vint_happened_{};

        // Master-cycle countdown until VINT pending fires. The VBLANK
        // status flag goes high at the start of the VBLANK-entry scanline,
        // but the VINT IRQ itself asserts ~770 (H32) / 788 (H40) master
        // cycles into that line; several games depend on observing the
        // status flag before the IRQ. -1 = no delay scheduled.
        std::int64_t vint_pending_delay_master_{-1};
        bool sprite_overflow_{};
        bool sprite_collision_{};
        bool in_vblank_{};
        bool in_hblank_{};
        bool vblank_pending_{};
        bool hblank_pending_{};
        bool ext_pending_{};

        // Output.
        std::vector<std::uint32_t> framebuffer_; // fb_width * fb_height, 0x00RRGGBB
        bool backdrop_mask_enabled_{};
        std::vector<std::uint8_t> backdrop_mask_; // 1 = backdrop pixel (when enabled)
        std::uint64_t frame_index_{};

        // Host hooks.
        std::function<std::uint16_t(std::uint32_t)> dma_read_{};
        std::function<void(int)> irq_callback_{};
        std::function<void(int)> delayed_irq_callback_{};
        std::function<void(bool)> vblank_callback_{};
        std::function<void()> hint_callback_{};
        std::function<void(bool)> z80_int_callback_{};
        int last_irq_level_{};
        bool last_in_vblank_{};
        bool z80_int_line_{};      // one-scanline /INT pulse at V-blank entry
        bool last_z80_int_line_{}; // edge tracking for z80_int_callback_

        std::array<register_descriptor, 16> register_view_{};
        introspection_surface introspection_{*this};
    };

} // namespace mnemos::chips::video
