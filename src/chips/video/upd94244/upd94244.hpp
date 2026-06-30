#pragma once

#include "chip.hpp"
#include "introspection_adapters.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace mnemos::chips::video {

    // NEC uPD94244-210 first-pass VDP surface.
    //
    // The real M119 graphics device is still undocumented in Mnemos. This chip
    // preserves the board-level contract: an external VDP ROM region, video RAM,
    // memory-mapped registers, deterministic frames, framebuffer introspection,
    // and save-state. Raster/tile/sprite fidelity remains a later silicon pass.
    class upd94244 final : public ivideo {
      public:
        static constexpr std::uint32_t visible_width = 512U;
        static constexpr std::uint32_t visible_height = 384U;
        static constexpr std::uint32_t frame_rate_x1000 = 60000U;
        static constexpr std::size_t vram_size = 0x20000U;
        static constexpr std::size_t register_count = 32U;

        upd94244();

        [[nodiscard]] chip_metadata metadata() const noexcept override;
        void tick(std::uint64_t cycles) override;
        void reset(reset_kind kind) override;
        void save_state(state_writer& writer) const override;
        void load_state(state_reader& reader) override;
        [[nodiscard]] instrumentation::ichip_introspection& introspection() noexcept override;

        [[nodiscard]] std::uint64_t frame_index() const noexcept override { return frame_index_; }
        [[nodiscard]] frame_buffer_view framebuffer() const noexcept override;

        void attach_vdp_rom(std::span<const std::uint8_t> rom) noexcept { vdp_rom_ = rom; }
        [[nodiscard]] std::span<const std::uint8_t> vdp_rom() const noexcept { return vdp_rom_; }

        void write_register(std::uint8_t offset, std::uint32_t value) noexcept;
        [[nodiscard]] std::uint32_t read_register(std::uint8_t offset) const noexcept;
        void write_vram(std::uint32_t offset, std::uint8_t value) noexcept;
        [[nodiscard]] std::uint8_t read_vram(std::uint32_t offset) const noexcept;
        [[nodiscard]] std::span<const std::uint8_t> vram() const noexcept { return vram_; }
        [[nodiscard]] std::span<std::uint8_t> mutable_vram() noexcept { return vram_; }

        void compose_diagnostic(std::span<const std::uint8_t> main_rom,
                                std::span<const std::uint8_t> ymz_rom,
                                std::span<const std::uint8_t> work_ram,
                                std::span<const std::uint8_t> nvram,
                                std::uint8_t input_latch,
                                std::uint8_t control_latch);

        [[nodiscard]] std::span<const register_descriptor> register_snapshot() noexcept;

      private:
        std::array<std::uint32_t, register_count> regs_{};
        std::array<std::uint8_t, vram_size> vram_{};
        std::span<const std::uint8_t> vdp_rom_{};
        std::vector<std::uint32_t> pixels_{};
        std::uint64_t elapsed_cycles_{};
        std::uint64_t frame_index_{};

        std::array<register_descriptor, 8> register_view_{};
        instrumentation::introspection_builder introspection_;
    };

} // namespace mnemos::chips::video
