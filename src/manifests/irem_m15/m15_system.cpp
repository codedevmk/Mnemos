#include "m15_system.hpp"

#include "crc32.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace mnemos::manifests::irem_m15 {

    namespace {
        [[nodiscard]] std::vector<std::uint8_t>&
        pinned_region(common::rom_set_image& image, std::string_view name, std::size_t size) {
            auto& bytes = image.regions[std::string{name}];
            if (bytes.size() < size) {
                bytes.resize(size, 0xFFU);
            }
            return bytes;
        }

        [[nodiscard]] bool in_range(std::uint16_t address, std::uint16_t base,
                                    std::size_t size) noexcept {
            return address >= base &&
                   address < static_cast<std::uint16_t>(base + static_cast<std::uint16_t>(size));
        }

        [[nodiscard]] std::uint8_t sample_byte(std::span<const std::uint8_t> data,
                                               std::uint64_t index,
                                               std::uint8_t fallback) noexcept {
            if (data.empty()) {
                return fallback;
            }
            return data[static_cast<std::size_t>(index % data.size())];
        }

        [[nodiscard]] bool has_nonzero(std::span<const std::uint8_t> data) noexcept {
            return std::any_of(data.begin(), data.end(),
                               [](std::uint8_t value) { return value != 0U; });
        }

        [[nodiscard]] std::uint32_t rgb_from_luma(std::uint8_t luma,
                                                  std::uint8_t tint) noexcept {
            const std::uint32_t r = static_cast<std::uint32_t>((luma * 5U + tint) & 0xFFU);
            const std::uint32_t g =
                static_cast<std::uint32_t>(((luma << 1U) ^ (tint * 3U)) & 0xFFU);
            const std::uint32_t b =
                static_cast<std::uint32_t>(((luma >> 1U) + (tint * 9U)) & 0xFFU);
            return (r << 16U) | (g << 8U) | b;
        }

        [[nodiscard]] std::uint32_t crc32_u64(std::uint32_t crc, std::uint64_t value) noexcept {
            std::array<std::uint8_t, 8> bytes{};
            for (std::size_t i = 0; i < bytes.size(); ++i) {
                bytes[i] = static_cast<std::uint8_t>((value >> (i * 8U)) & 0xFFU);
            }
            return security::cryptography::crc32(
                std::span<const std::uint8_t>(bytes.data(), bytes.size()), crc);
        }

        [[nodiscard]] std::uint8_t layout_code(std::string_view layout) noexcept {
            if (layout == "m15_headon_6502") {
                return 1U;
            }
            return 0U;
        }

        [[nodiscard]] std::uint8_t layout_tint(std::string_view layout) noexcept {
            return layout_code(layout) == 1U ? 0x31U : 0x17U;
        }

        [[nodiscard]] std::uint32_t rom_identity_crc(const common::rom_set_image& roms,
                                                     const m15_board_params& params) {
            std::uint32_t crc =
                security::cryptography::crc32(std::string_view{"m15.rom.identity.v2"});
            crc = security::cryptography::crc32(
                std::span<const std::uint8_t>(&params.dip_default, 1U), crc);
            const std::uint8_t layout = layout_code(params.rom_layout);
            crc = security::cryptography::crc32(std::span<const std::uint8_t>(&layout, 1U), crc);
            crc = crc32_u64(crc, params.cpu_clock_hz);
            crc = crc32_u64(crc, roms.regions.size());
            for (const auto& [name, bytes] : roms.regions) {
                crc = crc32_u64(crc, name.size());
                crc = security::cryptography::crc32(std::string_view{name}, crc);
                crc = crc32_u64(crc, bytes.size());
                crc = security::cryptography::crc32(
                    std::span<const std::uint8_t>(bytes.data(), bytes.size()), crc);
            }
            return crc;
        }

        [[nodiscard]] std::uint8_t read_rom_byte(const m15_system& system,
                                                 std::uint16_t address) noexcept {
            const auto* main_prog = system.roms.region("maincpu");
            if (main_prog == nullptr || address >= main_prog->size()) {
                return 0xFFU;
            }
            return (*main_prog)[address];
        }
    } // namespace

    m15_board_params board_params_for(std::string_view set_name) noexcept {
        if (set_name == "headoni") {
            return {.cpu_clock_hz = cpu_clock_hz, .rom_layout = "m15_headon_6502",
                    .dip_default = 0xFFU};
        }
        return {};
    }

    std::uint8_t m15_cpu_bus::read8(std::uint32_t address) {
        if (system_ == nullptr) {
            return 0xFFU;
        }

        const auto a = static_cast<std::uint16_t>(address);
        if (in_range(a, scratch_ram_base, scratch_ram_size)) {
            return system_->scratch_ram[a - scratch_ram_base];
        }
        if (in_range(a, video_ram_base, video_ram_size)) {
            return system_->video_ram[a - video_ram_base];
        }
        if (in_range(a, color_ram_base, color_ram_size)) {
            return system_->color_ram[a - color_ram_base];
        }
        if (in_range(a, chargen_ram_base, chargen_ram_size)) {
            return system_->chargen_ram[a - chargen_ram_base];
        }

        switch (a) {
        case input_p2_address:
            return system_->input_p2;
        case dip_switch_address:
            return system_->dip_switches;
        case input_p1_address:
            return static_cast<std::uint8_t>(system_->input_p1 & system_->input_system);
        default:
            break;
        }

        if ((a >= program_rom_base && a < program_rom_limit) || a >= vector_rom_base) {
            return read_rom_byte(*system_, a);
        }
        return 0xFFU;
    }

    void m15_cpu_bus::write8(std::uint32_t address, std::uint8_t value) {
        if (system_ == nullptr) {
            return;
        }

        const auto a = static_cast<std::uint16_t>(address);
        if (in_range(a, scratch_ram_base, scratch_ram_size)) {
            system_->scratch_ram[a - scratch_ram_base] = value;
            return;
        }
        if (in_range(a, video_ram_base, video_ram_size)) {
            system_->video_ram[a - video_ram_base] = value;
            return;
        }
        if (in_range(a, color_ram_base, color_ram_size)) {
            system_->color_ram[a - color_ram_base] = value;
            return;
        }
        if (in_range(a, chargen_ram_base, chargen_ram_size)) {
            system_->chargen_ram[a - chargen_ram_base] = value;
            return;
        }

        switch (a) {
        case sound_latch_address:
            system_->speaker_latch = value;
            system_->speaker.set_speaker((value & 0x01U) != 0U);
            break;
        case control_register_address:
            system_->control_register = value;
            break;
        default:
            break;
        }
    }

    m15_video::m15_video()
        : pixels_(static_cast<std::size_t>(visible_width) * visible_height, 0U) {
        reset(chips::reset_kind::power_on);
    }

    chips::chip_metadata m15_video::metadata() const noexcept {
        return {.manufacturer = "Irem",
                .part_number = "m15_video_first_pass",
                .family = "irem_m15",
                .klass = chips::chip_class::video,
                .revision = 2U};
    }

    void m15_video::tick(std::uint64_t cycles) { elapsed_cycles_ += cycles; }

    void m15_video::reset(chips::reset_kind /*kind*/) {
        std::fill(pixels_.begin(), pixels_.end(), 0U);
        elapsed_cycles_ = 0U;
        frame_index_ = 0U;
    }

    chips::frame_buffer_view m15_video::framebuffer() const noexcept {
        return {.pixels = pixels_.data(),
                .width = visible_width,
                .height = visible_height,
                .stride = visible_width};
    }

    void m15_video::compose(std::span<const std::uint8_t> program_rom,
                            std::span<const std::uint8_t> video_ram,
                            std::span<const std::uint8_t> color_ram,
                            std::span<const std::uint8_t> chargen_ram,
                            std::span<const std::uint8_t> scratch_ram, std::uint8_t control,
                            std::string_view rom_layout) {
        const bool video_ready = has_nonzero(video_ram);
        const bool chargen_ready = has_nonzero(chargen_ram);
        const std::uint8_t tint = static_cast<std::uint8_t>(layout_tint(rom_layout) ^ control);

        for (std::uint32_t y = 0; y < visible_height; ++y) {
            for (std::uint32_t x = 0; x < visible_width; ++x) {
                const std::uint64_t linear =
                    static_cast<std::uint64_t>(y) * visible_width + x + frame_index_;
                std::uint8_t bit = 0U;
                std::uint8_t color = tint;
                if (video_ready && chargen_ready) {
                    const std::uint32_t tile_x = x >> 3U;
                    const std::uint32_t tile_y = y >> 3U;
                    const std::uint32_t tile_index = (tile_y * 32U + tile_x) & 0x03FFU;
                    const std::uint8_t tile = sample_byte(video_ram, tile_index, 0U);
                    const std::uint8_t glyph =
                        sample_byte(chargen_ram,
                                    static_cast<std::uint64_t>(tile) * 8U + (y & 7U), 0U);
                    bit = static_cast<std::uint8_t>((glyph >> (7U - (x & 7U))) & 1U);
                    color = sample_byte(color_ram, tile_index, tint);
                } else {
                    const std::uint8_t src = video_ready
                                                 ? sample_byte(video_ram, linear >> 3U, 0x00U)
                                                 : sample_byte(program_rom, linear >> 2U, 0x5AU);
                    bit = static_cast<std::uint8_t>((src >> (7U - (x & 7U))) & 1U);
                    color = sample_byte(color_ram, (linear >> 3U) + y, tint);
                }
                const std::uint8_t scratch =
                    sample_byte(scratch_ram, (linear >> 4U) + (x * 3U), 0x21U);
                const std::uint8_t luma = static_cast<std::uint8_t>(
                    (bit != 0U ? 0xD0U : 0x20U) ^ color ^ scratch ^ tint);
                pixels_[static_cast<std::size_t>(y) * visible_width + x] =
                    rgb_from_luma(luma, tint);
            }
        }
        ++frame_index_;
    }

    void m15_video::save_state(chips::state_writer& writer) const {
        writer.u64(elapsed_cycles_);
        writer.u64(frame_index_);
        for (const std::uint32_t pixel : pixels_) {
            writer.u32(pixel);
        }
    }

    void m15_video::load_state(chips::state_reader& reader) {
        elapsed_cycles_ = reader.u64();
        frame_index_ = reader.u64();
        for (std::uint32_t& pixel : pixels_) {
            pixel = reader.u32();
        }
    }

    m15_system::m15_system(common::rom_set_image image, m15_board_params board_params)
        : roms(std::move(image)), params(board_params) {
        (void)pinned_region(roms, "maincpu", main_rom_size);
        dip_switches = params.dip_default;

        main_bus.attach(*this);
        main_cpu.set_variant(chips::cpu::m6510::variant::mos_6502);
        main_cpu.set_port_enabled(false);
        main_cpu.attach_bus(main_bus);
        main_cpu.reset(chips::reset_kind::power_on);

        speaker.set_clock(params.cpu_clock_hz, audio_rate_hz);
        speaker.enable_audio_capture(true);
    }

    void m15_system::run_frame() {
        const std::uint64_t irq_at =
            (cpu_cycles_per_frame * static_cast<std::uint64_t>(visible_height - 16U)) /
            frame_lines;
        const std::uint64_t pre_irq = std::min(irq_at, cpu_cycles_per_frame);
        const std::uint64_t remaining = cpu_cycles_per_frame - pre_irq;
        const std::uint64_t irq_pulse = std::min<std::uint64_t>(remaining, 16U);

        main_cpu.tick(pre_irq);
        if (irq_pulse > 0U) {
            main_cpu.set_irq_line(true);
            main_cpu.tick(irq_pulse);
            main_cpu.set_irq_line(false);
        }
        main_cpu.tick(remaining - irq_pulse);

        speaker.tick(cpu_cycles_per_frame);
        video.tick(cpu_cycles_per_frame);
        const auto* main_prog = roms.region("maincpu");
        video.compose(main_prog != nullptr ? std::span<const std::uint8_t>(*main_prog)
                                           : std::span<const std::uint8_t>{},
                      video_ram, color_ram, chargen_ram, scratch_ram, control_register,
                      params.rom_layout);
    }

    void m15_system::set_inputs(std::uint8_t p1, std::uint8_t p2,
                                std::uint8_t system) noexcept {
        input_p1 = p1;
        input_p2 = p2;
        input_system = system;
    }

    void m15_system::save_state(chips::state_writer& writer) const {
        writer.u32(m15_system_state_version);
        writer.u8(params.dip_default);
        writer.u8(layout_code(params.rom_layout));
        writer.u32(params.cpu_clock_hz);
        writer.u32(rom_identity_crc(roms, params));
        main_cpu.save_state(writer);
        video.save_state(writer);
        speaker.save_state(writer);
        writer.bytes(scratch_ram);
        writer.bytes(video_ram);
        writer.bytes(color_ram);
        writer.bytes(chargen_ram);
        writer.u8(input_p1);
        writer.u8(input_p2);
        writer.u8(input_system);
        writer.u8(dip_switches);
        writer.u8(control_register);
        writer.u8(speaker_latch);
    }

    void m15_system::load_state(chips::state_reader& reader) {
        if (reader.u32() != m15_system_state_version) {
            reader.fail();
            return;
        }
        const std::uint8_t saved_dip = reader.u8();
        const std::uint8_t saved_layout = reader.u8();
        const std::uint32_t saved_clock = reader.u32();
        const std::uint32_t saved_identity = reader.u32();
        if (saved_dip != params.dip_default || saved_layout != layout_code(params.rom_layout) ||
            saved_clock != params.cpu_clock_hz || saved_identity != rom_identity_crc(roms, params)) {
            reader.fail();
            return;
        }
        main_cpu.load_state(reader);
        video.load_state(reader);
        speaker.load_state(reader);
        reader.bytes(scratch_ram);
        reader.bytes(video_ram);
        reader.bytes(color_ram);
        reader.bytes(chargen_ram);
        input_p1 = reader.u8();
        input_p2 = reader.u8();
        input_system = reader.u8();
        dip_switches = reader.u8();
        control_register = reader.u8();
        speaker_latch = reader.u8();
        if (reader.ok()) {
            speaker.set_speaker((speaker_latch & 0x01U) != 0U);
        }
    }

    std::unique_ptr<m15_system> assemble_m15(common::rom_set_image image,
                                             m15_board_params board_params) {
        return std::make_unique<m15_system>(std::move(image), board_params);
    }

} // namespace mnemos::manifests::irem_m15
