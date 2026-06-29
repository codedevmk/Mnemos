#include "m63_system.hpp"

#include "crc32.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace mnemos::manifests::irem_m63 {

    namespace {
        void ensure_region(common::rom_set_image& image, std::string_view name,
                           std::size_t size) {
            auto& bytes = image.regions[std::string{name}];
            if (bytes.size() < size) {
                bytes.resize(size, 0xFFU);
            }
        }

        void derive_region_from_raw_if_missing(common::rom_set_image& image,
                                               std::string_view name,
                                               std::size_t size,
                                               std::size_t raw_offset) {
            auto& target = image.regions[std::string{name}];
            if (!target.empty()) {
                if (target.size() < size) {
                    target.resize(size, 0xFFU);
                }
                return;
            }
            target.resize(size, 0xFFU);
            const auto* raw = image.region("raw_media");
            if (raw == nullptr || raw->empty()) {
                return;
            }
            for (std::size_t i = 0; i < target.size(); ++i) {
                target[i] = (*raw)[(raw_offset + i) % raw->size()];
            }
        }

        void append_visual_region(std::vector<std::uint8_t>& visual,
                                  const common::rom_set_image& image,
                                  std::string_view name,
                                  std::size_t size) {
            const auto* region = image.region(name);
            const std::size_t base = visual.size();
            visual.resize(base + size, 0xFFU);
            if (region == nullptr || region->empty()) {
                return;
            }
            const std::size_t copied = std::min(region->size(), size);
            std::copy_n(region->begin(), copied, visual.begin() + static_cast<std::ptrdiff_t>(base));
        }

        void rebuild_visual_rom(common::rom_set_image& image) {
            auto& visual = image.regions["gfx_combined"];
            visual.clear();
            visual.reserve(gfx_rom_size);
            append_visual_region(visual, image, "gfx1", gfx_char_size);
            append_visual_region(visual, image, "gfx2", gfx_sprite_a_size);
            append_visual_region(visual, image, "gfx3", gfx_sprite_b_size);
            append_visual_region(visual, image, "proms", proms_size);
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

        [[nodiscard]] std::uint8_t layout_code(std::string_view layout) noexcept {
            if (layout == "m63_wilytower_z80_artifact_media") {
                return 1U;
            }
            return 0U;
        }

        [[nodiscard]] std::uint32_t crc32_u64(std::uint32_t crc, std::uint64_t value) noexcept {
            std::array<std::uint8_t, 8> bytes{};
            for (std::size_t i = 0; i < bytes.size(); ++i) {
                bytes[i] = static_cast<std::uint8_t>((value >> (i * 8U)) & 0xFFU);
            }
            return security::cryptography::crc32(
                std::span<const std::uint8_t>(bytes.data(), bytes.size()), crc);
        }

        [[nodiscard]] std::uint32_t board_identity_crc(const common::rom_set_image& roms,
                                                       const m63_board_params& params) {
            std::uint32_t crc =
                security::cryptography::crc32(std::string_view{"m63.board.identity.v1"});
            const std::array<std::uint8_t, 2> params_bytes{layout_code(params.rom_layout),
                                                           params.dip_default};
            crc = security::cryptography::crc32(
                std::span<const std::uint8_t>(params_bytes.data(), params_bytes.size()), crc);
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

        [[nodiscard]] std::uint8_t read_main_rom_byte(const m63_system& system,
                                                      std::uint16_t address) noexcept {
            const auto* main_prog = system.roms.region("maincpu");
            if (main_prog == nullptr || address >= main_prog->size()) {
                return 0xFFU;
            }
            return (*main_prog)[address];
        }

        [[nodiscard]] bool speaker_output_from_latch(std::uint8_t latch) noexcept {
            return (latch & sound_speaker_bit) != 0U;
        }

        [[nodiscard]] std::uint32_t rgb(std::uint8_t r, std::uint8_t g, std::uint8_t b) noexcept {
            return (static_cast<std::uint32_t>(r) << 16U) | (static_cast<std::uint32_t>(g) << 8U) |
                   b;
        }

        [[nodiscard]] std::uint32_t palette_color(std::uint8_t color,
                                                  std::uint8_t mix) noexcept {
            const std::uint8_t r =
                static_cast<std::uint8_t>((((color >> 0U) & 0x03U) * 85U) ^ mix);
            const std::uint8_t g =
                static_cast<std::uint8_t>((((color >> 2U) & 0x03U) * 85U) ^ (mix >> 1U));
            const std::uint8_t b =
                static_cast<std::uint8_t>((((color >> 4U) & 0x03U) * 85U) ^ (mix << 1U));
            return rgb(r, g, b);
        }

        [[nodiscard]] bool glyph_pixel_on(std::span<const std::uint8_t> gfx_rom,
                                          std::uint8_t tile,
                                          std::uint32_t x,
                                          std::uint32_t y) noexcept {
            const std::uint32_t row = y & 0x07U;
            const std::uint32_t bit = 7U - (x & 0x07U);
            const std::uint64_t offset = static_cast<std::uint64_t>(tile) * 8U + row;
            return (sample_byte(gfx_rom, offset, static_cast<std::uint8_t>(tile ^ row)) &
                    (1U << bit)) != 0U;
        }

    } // namespace

    m63_board_params board_params_for(std::string_view set_name) noexcept {
        if (!set_name.empty()) {
            return {.cpu_clock_hz = cpu_clock_hz,
                    .rom_layout = "m63_wilytower_z80_artifact_media",
                    .dip_default = wilytowr_dip_default};
        }
        return {};
    }

    std::uint8_t m63_cpu_bus::read8(std::uint32_t address) {
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
        if (in_range(a, work_ram_base, work_ram_size)) {
            return system_->work_ram[a - work_ram_base];
        }

        switch (a) {
        case input_p1_address:
            return system_->input_p1;
        case input_p2_address:
            return system_->input_p2;
        case input_system_address:
            return system_->input_system;
        case dip_switch_address:
            return system_->dip_switches;
        default:
            break;
        }

        if (a >= program_rom_base && a < program_rom_limit) {
            return read_main_rom_byte(*system_, a);
        }
        return 0xFFU;
    }

    void m63_cpu_bus::write8(std::uint32_t address, std::uint8_t value) {
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
        if (in_range(a, work_ram_base, work_ram_size)) {
            system_->work_ram[a - work_ram_base] = value;
            return;
        }

        switch (a) {
        case sound_latch_address:
            system_->write_sound_latch(value);
            break;
        case control_register_address:
            system_->control_register = value;
            system_->flip_screen = (value & control_flip_bit) != 0U;
            ++system_->control_write_count;
            break;
        default:
            break;
        }
    }

    m63_video::m63_video()
        : pixels_(static_cast<std::size_t>(visible_width) * visible_height, 0U) {
        reset(chips::reset_kind::power_on);
    }

    chips::chip_metadata m63_video::metadata() const noexcept {
        return {.manufacturer = "Irem",
                .part_number = "m63_video_first_pass",
                .family = "irem_m63",
                .klass = chips::chip_class::video,
                .revision = 1U};
    }

    void m63_video::tick(std::uint64_t cycles) { elapsed_cycles_ += cycles; }

    void m63_video::reset(chips::reset_kind /*kind*/) {
        std::fill(pixels_.begin(), pixels_.end(), 0U);
        elapsed_cycles_ = 0U;
        frame_index_ = 0U;
    }

    chips::frame_buffer_view m63_video::framebuffer() const noexcept {
        return {.pixels = pixels_.data(),
                .width = visible_width,
                .height = visible_height,
                .stride = visible_width};
    }

    void m63_video::compose(std::span<const std::uint8_t> video_ram,
                            std::span<const std::uint8_t> color_ram,
                            std::span<const std::uint8_t> gfx_rom,
                            std::span<const std::uint8_t> program_rom,
                            bool flip_screen,
                            std::string_view rom_layout) {
        const std::uint8_t layout_tint = static_cast<std::uint8_t>(0x31U + layout_code(rom_layout));
        for (std::uint32_t y = 0; y < visible_height; ++y) {
            const std::uint32_t draw_y = flip_screen ? visible_height - 1U - y : y;
            for (std::uint32_t x = 0; x < visible_width; ++x) {
                const std::uint32_t draw_x = flip_screen ? visible_width - 1U - x : x;
                const std::uint16_t tile_index =
                    static_cast<std::uint16_t>(((y >> 3U) * 32U + (x >> 3U)) & 0x03FFU);
                std::uint8_t tile = sample_byte(video_ram, tile_index, 0U);
                if (tile == 0U) {
                    tile = static_cast<std::uint8_t>(
                        tile_index + sample_byte(program_rom, tile_index, 0U));
                }
                const std::uint8_t attr = sample_byte(
                    color_ram, tile_index,
                    static_cast<std::uint8_t>(sample_byte(program_rom, tile_index ^ 0x3FU, 0U) ^
                                              layout_tint));
                const std::uint8_t mix =
                    static_cast<std::uint8_t>(layout_tint ^ tile ^ attr ^ (x >> 4U) ^ (y >> 4U));
                pixels_[static_cast<std::size_t>(draw_y) * visible_width + draw_x] =
                    glyph_pixel_on(gfx_rom, tile, x, y) ? palette_color(attr, mix)
                                                        : palette_color(attr >> 1U, mix >> 2U);
            }
        }
        ++frame_index_;
    }

    void m63_video::save_state(chips::state_writer& writer) const {
        writer.u64(elapsed_cycles_);
        writer.u64(frame_index_);
        for (const std::uint32_t pixel : pixels_) {
            writer.u32(pixel);
        }
    }

    void m63_video::load_state(chips::state_reader& reader) {
        elapsed_cycles_ = reader.u64();
        frame_index_ = reader.u64();
        for (std::uint32_t& pixel : pixels_) {
            pixel = reader.u32();
        }
    }

    m63_system::m63_system(common::rom_set_image image, m63_board_params board_params)
        : roms(std::move(image)), params(board_params) {
        derive_region_from_raw_if_missing(roms, "maincpu", main_rom_size, 0U);
        derive_region_from_raw_if_missing(roms, "soundcpu", sound_rom_size, main_rom_size);
        derive_region_from_raw_if_missing(roms, "gfx1", gfx_char_size,
                                          main_rom_size + sound_rom_size);
        derive_region_from_raw_if_missing(roms, "gfx2", gfx_sprite_a_size,
                                          main_rom_size + sound_rom_size + gfx_char_size);
        derive_region_from_raw_if_missing(roms, "gfx3", gfx_sprite_b_size,
                                          main_rom_size + sound_rom_size + gfx_char_size +
                                              gfx_sprite_a_size);
        ensure_region(roms, "user1", user_rom_size);
        ensure_region(roms, "proms", proms_size);
        rebuild_visual_rom(roms);
        dip_switches = params.dip_default;

        main_bus.attach(*this);
        main_cpu.attach_bus(main_bus);
        main_cpu.set_port_in([this](std::uint16_t port) { return read_io_port(port); });
        main_cpu.set_port_out(
            [this](std::uint16_t port, std::uint8_t value) { write_io_port(port, value); });
        main_cpu.reset(chips::reset_kind::power_on);

        speaker.set_clock(params.cpu_clock_hz, audio_rate_hz);
        speaker.enable_audio_capture(true);
        speaker_output_high = speaker_output_from_latch(sound_latch);
        speaker.set_speaker(speaker_output_high);
    }

    void m63_system::run_frame() {
        std::uint64_t cycles_elapsed = 0U;
        for (std::uint32_t line = 0U; line < frame_lines; ++line) {
            const std::uint64_t next_cycle =
                (cpu_cycles_per_frame * static_cast<std::uint64_t>(line + 1U)) / frame_lines;
            const std::uint64_t line_cycles = next_cycle - cycles_elapsed;
            if (line == visible_height - 16U) {
                const std::uint64_t irq_pulse = std::min<std::uint64_t>(line_cycles, 16U);
                if (irq_pulse > 0U) {
                    main_cpu.set_irq_line(true);
                    main_cpu.tick(irq_pulse);
                    main_cpu.set_irq_line(false);
                }
                main_cpu.tick(line_cycles - irq_pulse);
            } else {
                main_cpu.tick(line_cycles);
            }
            speaker.tick(line_cycles);
            video.tick(line_cycles);
            cycles_elapsed = next_cycle;
        }
        main_cpu.set_irq_line(false);

        const auto* gfx = roms.region("gfx_combined");
        const auto* program = roms.region("maincpu");
        video.compose(video_ram, color_ram,
                      gfx != nullptr ? std::span<const std::uint8_t>(*gfx)
                                     : std::span<const std::uint8_t>{},
                      program != nullptr ? std::span<const std::uint8_t>(*program)
                                         : std::span<const std::uint8_t>{},
                      flip_screen, params.rom_layout);
    }

    void m63_system::set_inputs(std::uint8_t p1, std::uint8_t p2,
                                std::uint8_t system) noexcept {
        input_p1 = p1;
        input_p2 = p2;
        input_system = system;
        main_cpu.set_nmi_line((system & coin1_bit) != 0U);
    }

    std::uint8_t m63_system::read_io_port(std::uint16_t port) const noexcept {
        switch (port & 0x00FFU) {
        case 0x00U:
            return input_p1;
        case 0x01U:
            return input_p2;
        case 0x02U:
            return input_system;
        case 0x03U:
            return dip_switches;
        default:
            return 0xFFU;
        }
    }

    void m63_system::write_io_port(std::uint16_t port, std::uint8_t value) noexcept {
        switch (port & 0x00FFU) {
        case 0x04U:
            write_sound_latch(value);
            break;
        case 0x05U:
            control_register = value;
            flip_screen = (value & control_flip_bit) != 0U;
            ++control_write_count;
            break;
        default:
            break;
        }
    }

    void m63_system::write_sound_latch(std::uint8_t value) noexcept {
        ++sound_latch_write_count;
        sound_latch = value;
        const bool output = speaker_output_from_latch(value);
        if (output != speaker_output_high) {
            ++speaker_output_edge_count;
            speaker_output_high = output;
        }
        speaker.set_speaker(speaker_output_high);
    }

    void m63_system::save_state(chips::state_writer& writer) const {
        writer.u32(m63_system_state_version);
        writer.u8(layout_code(params.rom_layout));
        writer.u8(params.dip_default);
        writer.u32(params.cpu_clock_hz);
        writer.u32(board_identity_crc(roms, params));
        main_cpu.save_state(writer);
        video.save_state(writer);
        speaker.save_state(writer);
        writer.bytes(scratch_ram);
        writer.bytes(video_ram);
        writer.bytes(color_ram);
        writer.bytes(work_ram);
        writer.u8(input_p1);
        writer.u8(input_p2);
        writer.u8(input_system);
        writer.u8(dip_switches);
        writer.u8(sound_latch);
        writer.u8(control_register);
        writer.boolean(flip_screen);
        writer.boolean(speaker_output_high);
        writer.u64(sound_latch_write_count);
        writer.u64(speaker_output_edge_count);
        writer.u64(control_write_count);
    }

    void m63_system::load_state(chips::state_reader& reader) {
        if (reader.u32() != m63_system_state_version) {
            reader.fail();
            return;
        }
        if (reader.u8() != layout_code(params.rom_layout) || reader.u8() != params.dip_default ||
            reader.u32() != params.cpu_clock_hz ||
            reader.u32() != board_identity_crc(roms, params)) {
            reader.fail();
            return;
        }
        main_cpu.load_state(reader);
        video.load_state(reader);
        speaker.load_state(reader);
        reader.bytes(scratch_ram);
        reader.bytes(video_ram);
        reader.bytes(color_ram);
        reader.bytes(work_ram);
        input_p1 = reader.u8();
        input_p2 = reader.u8();
        input_system = reader.u8();
        dip_switches = reader.u8();
        sound_latch = reader.u8();
        control_register = reader.u8();
        flip_screen = reader.boolean();
        speaker_output_high = reader.boolean();
        sound_latch_write_count = reader.u64();
        speaker_output_edge_count = reader.u64();
        control_write_count = reader.u64();
        if (reader.ok()) {
            speaker.set_speaker(speaker_output_high);
            main_cpu.set_nmi_line((input_system & coin1_bit) != 0U);
        }
    }

    std::unique_ptr<m63_system> assemble_m63(common::rom_set_image image,
                                             m63_board_params board_params) {
        return std::make_unique<m63_system>(std::move(image), board_params);
    }

} // namespace mnemos::manifests::irem_m63
