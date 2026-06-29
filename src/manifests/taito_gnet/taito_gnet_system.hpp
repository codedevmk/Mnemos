#pragma once

#include "bus.hpp"
#include "r3000a.hpp"
#include "state.hpp"
#include "taito_gnet_media.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace mnemos::manifests::taito_gnet {

    struct taito_gnet_config final {
        std::vector<std::uint8_t> bios;
        std::vector<gnet_flash_card_image> flash_cards;
        bool jp1_high_bank{};
    };

    // First board-level G-NET/ZN-2 shell: it provides the R3000A reset fetch path,
    // main RAM, package-loaded flash-card media, and the first BIOS-facing common
    // register blocks. The GPU surface is still a register/VRAM latch only, and
    // the R3000A only latches COP2/GTE register moves and command words, but DMA
    // channels 2 and 6 now execute the BIOS-visible RAM-to-GP0 and OTC paths.
    // The root timers raise first-pass target/overflow IRQs; exact timer sync and
    // clock-source modes remain deferred with SPU, real GTE command math, GPU
    // rendering, full DMA timing, JVS/I/O, and the locked-card PCMCIA
    // command/security path remain later compatibility increments.
    struct taito_gnet_system final {
        static constexpr std::uint32_t boot_rom_base = 0x1FC00000U;
        static constexpr std::uint32_t flash_window_base = 0x1F000000U;
        static constexpr std::uint32_t flash_window_bytes = 0x00800000U;
        static constexpr std::uint32_t flash_bank_stride = 0x08000000U;
        static constexpr std::uint32_t pcmcia_window_offset = 0x00200000U;
        static constexpr std::uint32_t control3_address = 0x1FA30000U;
        static constexpr std::uint32_t pcmcia_io_base = 0x1FB00000U;
        static constexpr std::uint32_t pcmcia_io_bytes = 0x00010000U;
        static constexpr std::uint32_t pcmcia_index_offset = 0x000003E0U;
        static constexpr std::uint32_t pcmcia_data_offset = 0x000003E1U;
        static constexpr std::uint32_t control_address = 0x1FB40000U;
        static constexpr std::uint32_t control2_address = 0x1FB60000U;
        static constexpr std::uint32_t config_id_address = 0x1FB70000U;
        static constexpr std::size_t boot_rom_max_bytes = 512U * 1024U;
        static constexpr std::size_t main_ram_bytes = 2U * 1024U * 1024U;
        static constexpr std::size_t firm_flash_bytes = 2U * 1024U * 1024U;
        static constexpr std::size_t zoom_program_flash_bytes = 512U * 1024U;
        static constexpr std::size_t wave_flash_bytes = 2U * 1024U * 1024U;
        static constexpr std::uint8_t pcmcia_interrupt_control_register = 0x03U;
        static constexpr std::uint8_t pcmcia_card_reset_bit = 0x40U;
        static constexpr std::uint32_t irq_status_address = 0x1F801070U;
        static constexpr std::uint32_t irq_mask_address = 0x1F801074U;
        static constexpr std::uint32_t dma_register_base = 0x1F801080U;
        static constexpr std::uint32_t dma_register_bytes = 0x00000080U;
        static constexpr std::uint32_t root_timer_base = 0x1F801100U;
        static constexpr std::uint32_t root_timer_register_bytes = 0x00000030U;
        static constexpr std::uint32_t scratchpad_base = 0x1F800000U;
        static constexpr std::size_t scratchpad_bytes = 1024U;
        static constexpr std::uint32_t memory_control_base = 0x1F801000U;
        static constexpr std::uint32_t memory_control_bytes = 0x00000024U;
        static constexpr std::uint32_t ram_size_address = 0x1F801060U;
        static constexpr std::uint32_t cache_control_address = 0xFFFE0130U;
        static constexpr std::uint32_t gpu_gp0_address = 0x1F801810U;
        static constexpr std::uint32_t gpu_gp1_address = 0x1F801814U;
        static constexpr std::uint32_t gpu_reset_status = 0x14802000U;
        static constexpr std::size_t gpu_vram_bytes = 1024U * 512U * sizeof(std::uint16_t);

        taito_gnet_system();

        struct dma_channel final {
            std::uint32_t base{};
            std::uint32_t block{};
            std::uint32_t control{};
        };

        struct root_timer final {
            std::uint16_t counter{};
            std::uint16_t mode{};
            std::uint16_t target{};
        };

        chips::cpu::r3000a cpu;
        topology::bus bus{32U, topology::endianness::little};
        std::array<std::uint8_t, main_ram_bytes> main_ram{};
        std::array<std::uint8_t, scratchpad_bytes> scratchpad{};
        std::array<std::uint8_t, firm_flash_bytes> firm_flash{};
        std::array<std::uint8_t, zoom_program_flash_bytes> zoom_program_flash{};
        std::array<std::array<std::uint8_t, wave_flash_bytes>, 3> wave_flash{};
        std::vector<std::uint8_t> bios;
        std::vector<gnet_flash_card_image> flash_cards;
        bool jp1_high_bank{};
        std::uint8_t control{0x10U};
        std::uint16_t control2{};
        std::uint8_t control3{};
        std::array<std::uint8_t, 256> pcmcia_registers{};
        std::uint8_t pcmcia_register_index{};
        bool pcmcia_reset_asserted{};
        std::uint16_t irq_status{};
        std::uint16_t irq_mask{};
        std::array<dma_channel, 7> dma_channels{};
        std::uint32_t dma_control{};
        std::uint32_t dma_interrupt{};
        std::array<root_timer, 3> root_timers{};
        std::array<std::uint32_t, memory_control_bytes / 4U> memory_control{};
        std::uint32_t ram_size{};
        std::uint32_t cache_control{};
        std::array<std::uint8_t, gpu_vram_bytes> gpu_vram{};
        std::uint32_t gpu_gp0{};
        std::uint32_t gpu_gp1{};
        std::uint32_t gpu_status{gpu_reset_status};
        std::array<std::uint32_t, 16> gpu_gp0_fifo{};
        std::uint8_t gpu_gp0_fifo_count{};

        void step_instructions(std::uint32_t count);
        void request_interrupt(std::uint16_t mask) noexcept;

        [[nodiscard]] std::size_t flash_card_count() const noexcept {
            return flash_cards.size();
        }
        [[nodiscard]] std::span<const std::uint8_t>
        flash_card_data(std::size_t index) const noexcept;

        void save_state(chips::state_writer& writer) const;
        void load_state(chips::state_reader& reader);

        [[nodiscard]] std::uint8_t read_flash_window(std::uint32_t address) const noexcept;
        void write_flash_window(std::uint32_t address, std::uint8_t value) noexcept;
        [[nodiscard]] std::uint8_t read_pcmcia_memory(std::uint32_t offset) const noexcept;
        void write_pcmcia_memory(std::uint32_t offset, std::uint8_t value) noexcept;
        [[nodiscard]] std::uint8_t read_pcmcia_io(std::uint32_t address) const noexcept;
        void write_pcmcia_io(std::uint32_t address, std::uint8_t value) noexcept;
        [[nodiscard]] std::uint8_t read_control(std::uint32_t address) const noexcept;
        void write_control(std::uint32_t address, std::uint8_t value) noexcept;
        [[nodiscard]] std::uint8_t read_memory_control(std::uint32_t address) const noexcept;
        void write_memory_control(std::uint32_t address, std::uint8_t value) noexcept;
        [[nodiscard]] std::uint8_t read_gpu(std::uint32_t address) const noexcept;
        void write_gpu(std::uint32_t address, std::uint8_t value) noexcept;
        void push_gpu_gp0(std::uint32_t value) noexcept;
        void apply_gpu_gp1(std::uint32_t value) noexcept;
        [[nodiscard]] std::uint8_t read_irq(std::uint32_t address) const noexcept;
        void write_irq(std::uint32_t address, std::uint8_t value) noexcept;
        [[nodiscard]] std::uint8_t read_dma(std::uint32_t address) const noexcept;
        void write_dma(std::uint32_t address, std::uint8_t value) noexcept;
        void execute_dma_channel(std::uint32_t channel_index) noexcept;
        void execute_gpu_dma(dma_channel& channel) noexcept;
        void execute_otc_dma(dma_channel& channel) noexcept;
        void complete_dma_channel(std::uint32_t channel_index) noexcept;
        [[nodiscard]] std::uint8_t read_root_timer(std::uint32_t address) const noexcept;
        void write_root_timer(std::uint32_t address, std::uint8_t value) noexcept;
        void clock_root_timers(std::uint32_t cycles) noexcept;
        void refresh_cpu_interrupt_line() noexcept;
    };

    [[nodiscard]] std::unique_ptr<taito_gnet_system>
    assemble_taito_gnet(taito_gnet_config config);

    [[nodiscard]] std::unique_ptr<taito_gnet_system>
    assemble_taito_gnet_from_package(std::vector<std::uint8_t> bios,
                                     std::span<const std::uint8_t> package_bytes,
                                     std::uint64_t max_card_bytes =
                                         128ULL * 1024ULL * 1024ULL);

} // namespace mnemos::manifests::taito_gnet
