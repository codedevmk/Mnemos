#include "m62_system.hpp"

#include "crc32.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace mnemos::manifests::irem_m62 {

    namespace {
        [[nodiscard]] std::vector<std::uint8_t>&
        pinned_region(common::rom_set_image& image, std::string_view name, std::size_t size) {
            auto& bytes = image.regions[std::string{name}];
            if (bytes.size() < size) {
                bytes.resize(size, 0xFFU);
            }
            return bytes;
        }

        void derive_region_from_raw(common::rom_set_image& image, std::string_view name,
                                    std::size_t size, std::size_t raw_offset) {
            auto& target = pinned_region(image, name, size);
            const auto* raw = image.region("raw_media");
            if (raw == nullptr || raw->empty()) {
                return;
            }
            for (std::size_t i = 0; i < target.size(); ++i) {
                target[i] = (*raw)[(raw_offset + i) % raw->size()];
            }
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
            if (layout == "m62_ldrun_z80_raw_media") {
                return 1U;
            }
            if (layout == "m62_ldrun_regioned") {
                return 2U;
            }
            if (layout == "m62_ldrun2_regioned") {
                return 3U;
            }
            if (layout == "m62_ldrun3_regioned") {
                return 4U;
            }
            if (layout == "m62_ldrun4_regioned") {
                return 5U;
            }
            if (layout == "m62_lotlot_regioned") {
                return 6U;
            }
            if (layout == "m62_spelunk2_regioned") {
                return 7U;
            }
            return 0U;
        }

        [[nodiscard]] bool has_non_fill_byte(std::span<const std::uint8_t> bytes,
                                              std::uint8_t fill = 0xFFU) noexcept {
            return std::any_of(bytes.begin(), bytes.end(),
                               [fill](std::uint8_t value) { return value != fill; });
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
                                                       const m62_board_params& params) {
            std::uint32_t crc =
                security::cryptography::crc32(std::string_view{"m62.board.identity.v2"});
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

        [[nodiscard]] std::uint8_t read_main_rom_byte(const m62_system& system,
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

    m62_board_params board_params_for(std::string_view set_name) noexcept {
        if (set_name == "ldrun") {
            return {.cpu_clock_hz = cpu_clock_hz,
                    .rom_layout = "m62_ldrun_regioned",
                    .dip_default = ldrun_dip_default};
        }
        if (set_name == "ldrun2") {
            return {.cpu_clock_hz = cpu_clock_hz,
                    .rom_layout = "m62_ldrun2_regioned",
                    .dip_default = ldrun_dip_default};
        }
        if (set_name == "ldrun3") {
            return {.cpu_clock_hz = cpu_clock_hz,
                    .rom_layout = "m62_ldrun3_regioned",
                    .dip_default = ldrun_dip_default};
        }
        if (set_name == "ldrun4") {
            return {.cpu_clock_hz = cpu_clock_hz,
                    .rom_layout = "m62_ldrun4_regioned",
                    .dip_default = ldrun_dip_default};
        }
        if (set_name == "lotlot") {
            return {.cpu_clock_hz = cpu_clock_hz,
                    .rom_layout = "m62_lotlot_regioned",
                    .dip_default = ldrun_dip_default};
        }
        if (set_name == "spelunk2") {
            return {.cpu_clock_hz = cpu_clock_hz,
                    .rom_layout = "m62_spelunk2_regioned",
                    .dip_default = ldrun_dip_default};
        }
        if (!set_name.empty()) {
            return {.cpu_clock_hz = cpu_clock_hz,
                    .rom_layout = "m62_ldrun_z80_raw_media",
                    .dip_default = ldrun_dip_default};
        }
        return {};
    }

    std::uint8_t m62_cpu_bus::read8(std::uint32_t address) {
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

    void m62_cpu_bus::write8(std::uint32_t address, std::uint8_t value) {
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

    m62_video::m62_video()
        : pixels_(static_cast<std::size_t>(visible_width) * visible_height, 0U) {
        reset(chips::reset_kind::power_on);
    }

    chips::chip_metadata m62_video::metadata() const noexcept {
        return {.manufacturer = "Irem",
                .part_number = "m62_video_first_pass",
                .family = "irem_m62",
                .klass = chips::chip_class::video,
                .revision = 1U};
    }

    void m62_video::tick(std::uint64_t cycles) { elapsed_cycles_ += cycles; }

    void m62_video::reset(chips::reset_kind /*kind*/) {
        std::fill(pixels_.begin(), pixels_.end(), 0U);
        elapsed_cycles_ = 0U;
        frame_index_ = 0U;
    }

    chips::frame_buffer_view m62_video::framebuffer() const noexcept {
        return {.pixels = pixels_.data(),
                .width = visible_width,
                .height = visible_height,
                .stride = visible_width};
    }

    void m62_video::compose(std::span<const std::uint8_t> video_ram,
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

    void m62_video::save_state(chips::state_writer& writer) const {
        writer.u64(elapsed_cycles_);
        writer.u64(frame_index_);
        for (const std::uint32_t pixel : pixels_) {
            writer.u32(pixel);
        }
    }

    void m62_video::load_state(chips::state_reader& reader) {
        elapsed_cycles_ = reader.u64();
        frame_index_ = reader.u64();
        for (std::uint32_t& pixel : pixels_) {
            pixel = reader.u32();
        }
    }

    m62_system::m62_system(common::rom_set_image image, m62_board_params board_params)
        : roms(std::move(image)), params(board_params) {
        derive_region_from_raw(roms, "maincpu", main_rom_size, 0U);
        derive_region_from_raw(roms, "gfx1", gfx_rom_size, main_rom_size);
        auto& sound_prog = pinned_region(roms, "soundcpu", sound_rom_size);
        sound_cpu_enabled = has_non_fill_byte(sound_prog);
        dip_switches = params.dip_default;

        main_bus.attach(*this);
        main_cpu.attach_bus(main_bus);
        main_cpu.set_port_in([this](std::uint16_t port) { return read_io_port(port); });
        main_cpu.set_port_out(
            [this](std::uint16_t port, std::uint8_t value) { write_io_port(port, value); });
        main_cpu.reset(chips::reset_kind::power_on);

        sound_bus.map_mmio(
            sound_io_base, sound_io_size,
            [this](std::uint32_t address) -> std::uint8_t {
                switch ((address - sound_io_base) & 0xFFFFU) {
                case m6803_io_ay0_data:
                    return ay0.read();
                case m6803_io_latch:
                    return sound_latch;
                case m6803_io_ay1_data:
                    return ay1.read();
                default:
                    return 0xFFU;
                }
            },
            [this](std::uint32_t address, std::uint8_t value) {
                switch ((address - sound_io_base) & 0xFFFFU) {
                case m6803_io_ay0_address:
                    sound_ay0_address = static_cast<std::uint8_t>(value & 0x0FU);
                    ay0.address(sound_ay0_address);
                    break;
                case m6803_io_ay0_data:
                    ay0.write(value);
                    ++sound_cpu_psg_write_count;
                    break;
                case m6803_io_msm0_data:
                    sound_cpu_write_msm(0U, value);
                    break;
                case m6803_io_ay1_address:
                    sound_ay1_address = static_cast<std::uint8_t>(value & 0x0FU);
                    ay1.address(sound_ay1_address);
                    break;
                case m6803_io_ay1_data:
                    ay1.write(value);
                    ++sound_cpu_psg_write_count;
                    break;
                case m6803_io_latch_ack:
                    sound_latch_irq = false;
                    ++sound_latch_ack_count;
                    update_sound_irq();
                    break;
                case m6803_io_msm1_data:
                    sound_cpu_write_msm(1U, value);
                    break;
                default:
                    break;
                }
            },
            2);
        sound_bus.map_ram(sound_work_ram_base, sound_ram, 1);
        sound_bus.map_rom(
            sound_rom_base,
            std::span<const std::uint8_t>(sound_prog).subspan(sound_rom_base,
                                                              sound_rom_mapped_size),
            0);
        sound_cpu.attach_bus(sound_bus);
        sound_cpu.reset(chips::reset_kind::power_on);
        sound_cpu.set_reset_line(!sound_cpu_enabled);

        ay0.set_clock_divider(static_cast<int>(ssg_clock_divider));
        ay1.set_clock_divider(static_cast<int>(ssg_clock_divider));
        msm0.set_clock_divider(static_cast<int>(ssg_clock_divider));
        msm1.set_clock_divider(static_cast<int>(ssg_clock_divider));
        ay0.enable_audio_capture(true);
        ay1.enable_audio_capture(true);
        msm0.enable_audio_capture(true);
        msm1.enable_audio_capture(true);

        speaker.set_clock(params.cpu_clock_hz, audio_rate_hz);
        speaker.enable_audio_capture(true);
        speaker_output_high = speaker_output_from_latch(sound_latch);
        speaker.set_speaker(speaker_output_high);
    }

    void m62_system::run_frame() {
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
            const std::uint64_t sound_next =
                (sound_cycles_per_frame * static_cast<std::uint64_t>(line + 1U)) / frame_lines;
            const std::uint64_t sound_prev =
                (sound_cycles_per_frame * static_cast<std::uint64_t>(line)) / frame_lines;
            const std::uint64_t sound_line_cycles = sound_next - sound_prev;
            if (sound_cpu_enabled && !sound_cpu.reset_line_held()) {
                sound_cpu.tick(sound_line_cycles);
            }
            ay0.tick(sound_line_cycles);
            ay1.tick(sound_line_cycles);
            msm0.tick(sound_line_cycles);
            msm1.tick(sound_line_cycles);
            video.tick(line_cycles);
            cycles_elapsed = next_cycle;
        }
        main_cpu.set_irq_line(false);

        const auto* gfx = roms.region("gfx1");
        const auto* program = roms.region("maincpu");
        video.compose(video_ram, color_ram,
                      gfx != nullptr ? std::span<const std::uint8_t>(*gfx)
                                     : std::span<const std::uint8_t>{},
                      program != nullptr ? std::span<const std::uint8_t>(*program)
                                         : std::span<const std::uint8_t>{},
                      flip_screen, params.rom_layout);
    }

    void m62_system::set_inputs(std::uint8_t p1, std::uint8_t p2,
                                std::uint8_t system) noexcept {
        input_p1 = p1;
        input_p2 = p2;
        input_system = system;
        main_cpu.set_nmi_line((system & coin1_bit) != 0U);
    }

    std::uint8_t m62_system::read_io_port(std::uint16_t port) const noexcept {
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

    void m62_system::write_io_port(std::uint16_t port, std::uint8_t value) noexcept {
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

    void m62_system::write_sound_latch(std::uint8_t value) noexcept {
        ++sound_latch_write_count;
        sound_latch = value;
        sound_latch_irq = sound_cpu_enabled;
        update_sound_irq();
        const bool output = speaker_output_from_latch(value);
        if (output != speaker_output_high) {
            ++speaker_output_edge_count;
            speaker_output_high = output;
        }
        speaker.set_speaker(speaker_output_high);
    }

    void m62_system::update_sound_irq() noexcept {
        sound_cpu.set_irq_line(sound_cpu_enabled && sound_latch_irq);
    }

    void m62_system::sound_cpu_write_msm(std::uint8_t chip_index,
                                         std::uint8_t value) noexcept {
        chips::audio::msm5205& chip = chip_index == 0U ? msm0 : msm1;
        chip.data_w(static_cast<std::uint8_t>(value & 0x0FU));
        chip.vclk_tick();
        ++sound_cpu_msm_write_count;
    }

    void m62_system::save_state(chips::state_writer& writer) const {
        writer.u32(m62_system_state_version);
        writer.u8(layout_code(params.rom_layout));
        writer.u8(params.dip_default);
        writer.u32(params.cpu_clock_hz);
        writer.u32(board_identity_crc(roms, params));
        main_cpu.save_state(writer);
        sound_cpu.save_state(writer);
        video.save_state(writer);
        ay0.save_state(writer);
        ay1.save_state(writer);
        msm0.save_state(writer);
        msm1.save_state(writer);
        speaker.save_state(writer);
        writer.bytes(scratch_ram);
        writer.bytes(video_ram);
        writer.bytes(color_ram);
        writer.bytes(work_ram);
        writer.bytes(sound_ram);
        writer.u8(input_p1);
        writer.u8(input_p2);
        writer.u8(input_system);
        writer.u8(dip_switches);
        writer.u8(sound_latch);
        writer.u8(sound_ay0_address);
        writer.u8(sound_ay1_address);
        writer.u8(control_register);
        writer.boolean(flip_screen);
        writer.boolean(speaker_output_high);
        writer.boolean(sound_cpu_enabled);
        writer.boolean(sound_latch_irq);
        writer.u64(sound_latch_write_count);
        writer.u64(sound_latch_ack_count);
        writer.u64(sound_cpu_psg_write_count);
        writer.u64(sound_cpu_msm_write_count);
        writer.u64(speaker_output_edge_count);
        writer.u64(control_write_count);
    }

    void m62_system::load_state(chips::state_reader& reader) {
        if (reader.u32() != m62_system_state_version) {
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
        sound_cpu.load_state(reader);
        video.load_state(reader);
        ay0.load_state(reader);
        ay1.load_state(reader);
        msm0.load_state(reader);
        msm1.load_state(reader);
        speaker.load_state(reader);
        reader.bytes(scratch_ram);
        reader.bytes(video_ram);
        reader.bytes(color_ram);
        reader.bytes(work_ram);
        reader.bytes(sound_ram);
        input_p1 = reader.u8();
        input_p2 = reader.u8();
        input_system = reader.u8();
        dip_switches = reader.u8();
        sound_latch = reader.u8();
        sound_ay0_address = reader.u8();
        sound_ay1_address = reader.u8();
        control_register = reader.u8();
        flip_screen = reader.boolean();
        speaker_output_high = reader.boolean();
        sound_cpu_enabled = reader.boolean();
        sound_latch_irq = reader.boolean();
        sound_latch_write_count = reader.u64();
        sound_latch_ack_count = reader.u64();
        sound_cpu_psg_write_count = reader.u64();
        sound_cpu_msm_write_count = reader.u64();
        speaker_output_edge_count = reader.u64();
        control_write_count = reader.u64();
        if (reader.ok()) {
            ay0.address(sound_ay0_address);
            ay1.address(sound_ay1_address);
            speaker.set_speaker(speaker_output_high);
            main_cpu.set_nmi_line((input_system & coin1_bit) != 0U);
            sound_cpu.set_reset_line(!sound_cpu_enabled);
            update_sound_irq();
        }
    }

    std::unique_ptr<m62_system> assemble_m62(common::rom_set_image image,
                                             m62_board_params board_params) {
        return std::make_unique<m62_system>(std::move(image), board_params);
    }

} // namespace mnemos::manifests::irem_m62
