#include "m27_system.hpp"

#include "crc32.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace mnemos::manifests::irem_m27 {

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

        [[nodiscard]] std::uint8_t layout_code(std::string_view layout) noexcept {
            if (layout == "m27_panther_6502") {
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
                                                       const m27_board_params& params) {
            std::uint32_t crc =
                security::cryptography::crc32(std::string_view{"m27.board.identity.v1"});
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

        [[nodiscard]] std::uint8_t read_main_rom_byte(const m27_system& system,
                                                      std::uint16_t address) noexcept {
            const auto* main_prog = system.roms.region("maincpu");
            if (main_prog == nullptr || address >= main_prog->size()) {
                return 0xFFU;
            }

            const std::uint8_t byte = (*main_prog)[address];
            if (address >= vector_rom_base && byte == 0xFFU) {
                switch (address) {
                case 0xFFFCU:
                case 0xFFFEU:
                    return static_cast<std::uint8_t>(program_rom_base & 0x00FFU);
                case 0xFFFDU:
                case 0xFFFFU:
                    return static_cast<std::uint8_t>(program_rom_base >> 8U);
                default:
                    break;
                }
            }
            return byte;
        }

        [[nodiscard]] bool speaker_output_from_latch(std::uint8_t latch) noexcept {
            return (latch & sound_speaker_bit) != 0U;
        }

        [[nodiscard]] std::uint32_t rgb(std::uint8_t r, std::uint8_t g, std::uint8_t b) noexcept {
            return (static_cast<std::uint32_t>(r) << 16U) | (static_cast<std::uint32_t>(g) << 8U) |
                   b;
        }

        [[nodiscard]] std::uint32_t prom_color(std::span<const std::uint8_t> proms,
                                               std::uint16_t index,
                                               std::uint8_t mix) noexcept {
            const std::uint8_t v = sample_byte(proms, index, static_cast<std::uint8_t>(0x3DU + mix));
            const std::uint8_t r = static_cast<std::uint8_t>((((v >> 0U) & 0x07U) * 36U) ^ mix);
            const std::uint8_t g =
                static_cast<std::uint8_t>((((v >> 3U) & 0x07U) * 36U) ^ (mix >> 1U));
            const std::uint8_t b =
                static_cast<std::uint8_t>((((v >> 6U) & 0x03U) * 85U) ^ (mix << 1U));
            return rgb(r, g, b);
        }

        [[nodiscard]] bool marker_pixel_on(std::uint8_t tile, std::uint32_t x,
                                           std::uint32_t y) noexcept {
            const std::uint32_t bit = (x ^ y ^ tile) & 0x07U;
            return ((tile >> bit) & 0x01U) != 0U;
        }
    } // namespace

    m27_board_params board_params_for(std::string_view set_name) noexcept {
        if (set_name == "panther") {
            return {.cpu_clock_hz = cpu_clock_hz,
                    .rom_layout = "m27_panther_6502",
                    .dip_default = panther_dip_default};
        }
        return {};
    }

    std::uint8_t m27_cpu_bus::read8(std::uint32_t address) {
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

        if ((a >= program_rom_base && a < program_rom_limit) || a >= vector_rom_base) {
            return read_main_rom_byte(*system_, a);
        }
        return 0xFFU;
    }

    void m27_cpu_bus::write8(std::uint32_t address, std::uint8_t value) {
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

    m27_video::m27_video()
        : pixels_(static_cast<std::size_t>(visible_width) * visible_height, 0U) {
        reset(chips::reset_kind::power_on);
    }

    chips::chip_metadata m27_video::metadata() const noexcept {
        return {.manufacturer = "Irem",
                .part_number = "m27_video_first_pass",
                .family = "irem_m27",
                .klass = chips::chip_class::video,
                .revision = 1U};
    }

    void m27_video::tick(std::uint64_t cycles) { elapsed_cycles_ += cycles; }

    void m27_video::reset(chips::reset_kind /*kind*/) {
        std::fill(pixels_.begin(), pixels_.end(), 0U);
        elapsed_cycles_ = 0U;
        frame_index_ = 0U;
    }

    chips::frame_buffer_view m27_video::framebuffer() const noexcept {
        return {.pixels = pixels_.data(),
                .width = visible_width,
                .height = visible_height,
                .stride = visible_width};
    }

    void m27_video::compose(std::span<const std::uint8_t> video_ram,
                            std::span<const std::uint8_t> color_ram,
                            std::span<const std::uint8_t> proms,
                            bool flip_screen,
                            std::string_view rom_layout) {
        const std::uint8_t layout_tint = static_cast<std::uint8_t>(0x21U + layout_code(rom_layout));
        for (std::uint32_t y = 0; y < visible_height; ++y) {
            const std::uint32_t draw_y = flip_screen ? visible_height - 1U - y : y;
            for (std::uint32_t x = 0; x < visible_width; ++x) {
                const std::uint32_t draw_x = flip_screen ? visible_width - 1U - x : x;
                const std::uint16_t tile_index =
                    static_cast<std::uint16_t>(((y >> 3U) * 32U + (x >> 3U)) & 0x03FFU);
                const std::uint8_t tile = sample_byte(video_ram, tile_index, 0U);
                const std::uint8_t attr = sample_byte(color_ram, tile_index, 0U);
                const std::uint8_t grid = static_cast<std::uint8_t>((x >> 4U) ^ (y >> 4U));
                const std::uint8_t mix =
                    static_cast<std::uint8_t>(layout_tint ^ tile ^ attr ^ grid);
                std::uint32_t color =
                    prom_color(proms, static_cast<std::uint16_t>((attr << 2U) ^ tile ^ grid),
                               mix);
                if (tile != 0U && marker_pixel_on(tile, x, y)) {
                    color = prom_color(proms,
                                       static_cast<std::uint16_t>(0x80U | ((attr & 0x1FU) << 2U)),
                                       static_cast<std::uint8_t>(mix ^ 0x7BU));
                }
                pixels_[static_cast<std::size_t>(draw_y) * visible_width + draw_x] = color;
            }
        }
        ++frame_index_;
    }

    void m27_video::save_state(chips::state_writer& writer) const {
        writer.u64(elapsed_cycles_);
        writer.u64(frame_index_);
        for (const std::uint32_t pixel : pixels_) {
            writer.u32(pixel);
        }
    }

    void m27_video::load_state(chips::state_reader& reader) {
        elapsed_cycles_ = reader.u64();
        frame_index_ = reader.u64();
        for (std::uint32_t& pixel : pixels_) {
            pixel = reader.u32();
        }
    }

    m27_system::m27_system(common::rom_set_image image, m27_board_params board_params)
        : roms(std::move(image)), params(board_params) {
        (void)pinned_region(roms, "maincpu", main_rom_size);
        (void)pinned_region(roms, "audiocpu", audio_rom_size);
        (void)pinned_region(roms, "proms", proms_size);
        dip_switches = params.dip_default;

        main_bus.attach(*this);
        main_cpu.set_variant(chips::cpu::m6510::variant::mos_6502);
        main_cpu.set_port_enabled(false);
        main_cpu.attach_bus(main_bus);
        main_cpu.reset(chips::reset_kind::power_on);

        speaker.set_clock(params.cpu_clock_hz, audio_rate_hz);
        speaker.enable_audio_capture(true);
        speaker_output_high = speaker_output_from_latch(sound_latch);
        speaker.set_speaker(speaker_output_high);
    }

    void m27_system::run_frame() {
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

        const auto* proms = roms.region("proms");
        video.compose(video_ram, color_ram,
                      proms != nullptr ? std::span<const std::uint8_t>(*proms)
                                       : std::span<const std::uint8_t>{},
                      flip_screen, params.rom_layout);
    }

    void m27_system::set_inputs(std::uint8_t p1, std::uint8_t p2,
                                std::uint8_t system) noexcept {
        input_p1 = p1;
        input_p2 = p2;
        input_system = system;
        main_cpu.set_nmi_line((system & coin1_bit) != 0U);
    }

    void m27_system::write_sound_latch(std::uint8_t value) noexcept {
        ++sound_latch_write_count;
        sound_latch = value;
        const bool output = speaker_output_from_latch(value);
        if (output != speaker_output_high) {
            ++speaker_output_edge_count;
            speaker_output_high = output;
        }
        speaker.set_speaker(speaker_output_high);
    }

    void m27_system::save_state(chips::state_writer& writer) const {
        writer.u32(m27_system_state_version);
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

    void m27_system::load_state(chips::state_reader& reader) {
        if (reader.u32() != m27_system_state_version) {
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

    std::unique_ptr<m27_system> assemble_m27(common::rom_set_image image,
                                             m27_board_params board_params) {
        return std::make_unique<m27_system>(std::move(image), board_params);
    }

} // namespace mnemos::manifests::irem_m27
