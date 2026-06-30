#include "redalert_system.hpp"

#include "crc32.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace mnemos::manifests::irem_redalert {

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
            if (layout == "redalert_ww3_m27mb") {
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
                                                       const redalert_board_params& params) {
            std::uint32_t crc =
                security::cryptography::crc32(std::string_view{"redalert.board.identity.v1"});
            const std::array<std::uint8_t, 3> params_bytes{
                layout_code(params.rom_layout), params.dip_default, params.video_control_xor};
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

        [[nodiscard]] std::uint8_t read_main_rom_byte(const redalert_system& system,
                                                      std::uint16_t address) noexcept {
            const auto* main_prog = system.roms.region("maincpu");
            if (main_prog == nullptr) {
                return 0xFFU;
            }
            std::uint32_t offset = address;
            if (address >= vector_mirror_base) {
                offset = static_cast<std::uint32_t>(vector_mirror_source) +
                         (static_cast<std::uint32_t>(address) - vector_mirror_base);
            }
            if (offset >= main_prog->size()) {
                return 0xFFU;
            }
            return (*main_prog)[static_cast<std::size_t>(offset)];
        }

        [[nodiscard]] std::uint32_t rgb(std::uint8_t r, std::uint8_t g, std::uint8_t b) noexcept {
            return (static_cast<std::uint32_t>(r) << 16U) | (static_cast<std::uint32_t>(g) << 8U) |
                   b;
        }

        [[nodiscard]] std::uint32_t char_prom_color(std::span<const std::uint8_t> proms,
                                                    std::uint16_t index) noexcept {
            const std::uint8_t v = sample_byte(proms, index, 0x3DU);
            const std::uint8_t r_bits =
                static_cast<std::uint8_t>(((v >> 2U) & 0x01U) | (((v >> 6U) & 0x01U) << 1U) |
                                          (((v >> 4U) & 0x01U) << 2U));
            const std::uint8_t g_bits =
                static_cast<std::uint8_t>(((v >> 1U) & 0x01U) | (((v >> 3U) & 0x01U) << 1U) |
                                          (((v >> 5U) & 0x01U) << 2U));
            const std::uint8_t b_bits =
                static_cast<std::uint8_t>(((v >> 0U) & 0x01U) | (((v >> 7U) & 0x01U) << 1U));
            return rgb(static_cast<std::uint8_t>(r_bits * 36U),
                       static_cast<std::uint8_t>(g_bits * 36U),
                       static_cast<std::uint8_t>(b_bits * 85U));
        }

        [[nodiscard]] std::uint32_t bitmap_color(std::uint8_t index) noexcept {
            return rgb((index & 0x04U) != 0U ? 0xFFU : 0x00U,
                       (index & 0x02U) != 0U ? 0xFFU : 0x00U,
                       (index & 0x01U) != 0U ? 0xFFU : 0x00U);
        }

        [[nodiscard]] std::uint32_t background_color(std::uint8_t mix) noexcept {
            return rgb(static_cast<std::uint8_t>(0x18U ^ (mix << 1U)),
                       static_cast<std::uint8_t>(0x24U ^ mix),
                       static_cast<std::uint8_t>(0x24U ^ (mix >> 1U)));
        }
    } // namespace

    redalert_board_params board_params_for(std::string_view set_name) noexcept {
        if (set_name == "ww3") {
            return {.cpu_clock_hz = cpu_clock_hz,
                    .rom_layout = "redalert_ww3_m27mb",
                    .dip_default = ww3_dip_default,
                    .video_control_xor = video_flip_bit};
        }
        return {};
    }

    std::uint8_t redalert_cpu_bus::read8(std::uint32_t address) {
        if (system_ == nullptr) {
            return 0xFFU;
        }

        const auto a = static_cast<std::uint16_t>(address);
        if (in_range(a, ram_base, ram_size)) {
            return system_->ram[a - ram_base];
        }
        if (in_range(a, bitmap_ram_base, bitmap_ram_size)) {
            return system_->bitmap_ram[a - bitmap_ram_base];
        }
        if (in_range(a, char_ram_base, char_ram_size)) {
            return system_->char_ram[a - char_ram_base];
        }

        switch (a & io_decode_mask) {
        case dip_switch_address:
            return system_->dip_switches;
        case key1_address:
            return system_->key1;
        case key2_address:
            return static_cast<std::uint8_t>(
                system_->key2 | (system_->sound_handshake ? key2_sound_status_bit : 0U));
        case interrupt_clear_address:
            system_->main_cpu.set_irq_line(false);
            ++system_->interrupt_ack_count;
            return 0U;
        default:
            break;
        }

        if ((a >= program_rom_base && a < program_rom_limit) || a >= vector_mirror_base) {
            return read_main_rom_byte(*system_, a);
        }
        return 0xFFU;
    }

    void redalert_cpu_bus::write8(std::uint32_t address, std::uint8_t value) {
        if (system_ == nullptr) {
            return;
        }

        const auto a = static_cast<std::uint16_t>(address);
        if (in_range(a, ram_base, ram_size)) {
            system_->ram[a - ram_base] = value;
            return;
        }
        if (in_range(a, bitmap_ram_base, bitmap_ram_size)) {
            const std::size_t offset = static_cast<std::size_t>(a - bitmap_ram_base);
            system_->bitmap_ram[offset] = value;
            system_->bitmap_color_ram[offset >> 3U] =
                static_cast<std::uint8_t>(system_->bitmap_color & 0x07U);
            return;
        }
        if (in_range(a, char_ram_base, char_ram_size)) {
            system_->char_ram[a - char_ram_base] = value;
            return;
        }

        switch (a & io_decode_mask) {
        case audio_command_address:
            system_->write_audio_command(value);
            break;
        case video_control_address:
            system_->video_control = value;
            ++system_->video_control_write_count;
            break;
        case bitmap_color_address:
            system_->bitmap_color = static_cast<std::uint8_t>(value & 0x07U);
            ++system_->bitmap_color_write_count;
            break;
        case interrupt_clear_address:
            system_->main_cpu.set_irq_line(false);
            ++system_->interrupt_ack_count;
            break;
        default:
            break;
        }
    }

    redalert_video::redalert_video()
        : pixels_(static_cast<std::size_t>(visible_width) * visible_height, 0U) {
        reset(chips::reset_kind::power_on);
    }

    chips::chip_metadata redalert_video::metadata() const noexcept {
        return {.manufacturer = "Irem",
                .part_number = "redalert_video_first_pass",
                .family = "irem_redalert",
                .klass = chips::chip_class::video,
                .revision = 1U};
    }

    void redalert_video::tick(std::uint64_t cycles) { elapsed_cycles_ += cycles; }

    void redalert_video::reset(chips::reset_kind /*kind*/) {
        std::fill(pixels_.begin(), pixels_.end(), 0U);
        elapsed_cycles_ = 0U;
        frame_index_ = 0U;
    }

    chips::frame_buffer_view redalert_video::framebuffer() const noexcept {
        return {.pixels = pixels_.data(),
                .width = visible_width,
                .height = visible_height,
                .stride = visible_width};
    }

    void redalert_video::compose(std::span<const std::uint8_t> bitmap_ram,
                                 std::span<const std::uint8_t> bitmap_color_ram,
                                 std::span<const std::uint8_t> char_ram,
                                 std::span<const std::uint8_t> proms,
                                 std::uint8_t video_control,
                                 std::uint8_t control_xor,
                                 std::string_view rom_layout) {
        const bool normal_orientation = ((video_control ^ control_xor) & video_flip_bit) != 0U;
        const std::uint8_t layout_mix = static_cast<std::uint8_t>(0x31U + layout_code(rom_layout));
        for (std::uint32_t block = 0; block < bitmap_ram_size; ++block) {
            const std::uint32_t source_y = block & 0xFFU;
            const std::uint32_t source_x = ((~block >> 8U) & 0x1FU) << 3U;
            std::uint8_t bitmap_data = sample_byte(bitmap_ram, block, 0U);
            const std::uint8_t bitmap_pen = sample_byte(bitmap_color_ram, block >> 3U, 0U);
            const std::uint16_t char_cell = static_cast<std::uint16_t>((block >> 3U) & 0x03FFU);
            const std::uint8_t char_code = sample_byte(char_ram, char_cell, 0U);
            const std::uint16_t char_row =
                static_cast<std::uint16_t>(((char_code & 0x7FU) << 3U) | (block & 0x07U));
            std::uint8_t plane_a = 0U;
            std::uint8_t plane_b = 0U;
            if ((char_code & 0x80U) != 0U) {
                plane_a = sample_byte(char_ram, 0x0400U | char_row, 0U);
                plane_b = sample_byte(char_ram, 0x0C00U | char_row, 0U);
            } else {
                plane_b = sample_byte(char_ram, 0x0800U | char_row, 0U);
            }

            for (std::uint32_t pixel = 0; pixel < 8U; ++pixel) {
                const bool bitmap_on = (bitmap_data & 0x80U) != 0U;
                const std::uint8_t char_pen =
                    static_cast<std::uint8_t>(((plane_b & 0x80U) >> 6U) |
                                              ((plane_a & 0x80U) >> 7U));
                std::uint32_t color = background_color(layout_mix);
                if (char_pen == 0U || (bitmap_on && (char_code & 0xC0U) == 0xC0U)) {
                    if (bitmap_on) {
                        color = bitmap_color(bitmap_pen);
                    }
                } else {
                    color = char_prom_color(
                        proms, static_cast<std::uint16_t>(((char_code & 0xFEU) << 1U) | char_pen));
                }

                const std::uint32_t x = source_x + pixel;
                const std::uint32_t y = source_y;
                const std::uint32_t draw_x = normal_orientation ? x : (visible_width - 1U - x);
                const std::uint32_t draw_y = normal_orientation ? y : (visible_height - 1U - y);
                if (draw_x < visible_width && draw_y < visible_height) {
                    pixels_[static_cast<std::size_t>(draw_y) * visible_width + draw_x] = color;
                }
                bitmap_data <<= 1U;
                plane_a <<= 1U;
                plane_b <<= 1U;
            }
        }
        ++frame_index_;
    }

    void redalert_video::save_state(chips::state_writer& writer) const {
        writer.u64(elapsed_cycles_);
        writer.u64(frame_index_);
        for (const std::uint32_t pixel : pixels_) {
            writer.u32(pixel);
        }
    }

    void redalert_video::load_state(chips::state_reader& reader) {
        elapsed_cycles_ = reader.u64();
        frame_index_ = reader.u64();
        for (std::uint32_t& pixel : pixels_) {
            pixel = reader.u32();
        }
    }

    redalert_system::redalert_system(common::rom_set_image image,
                                     redalert_board_params board_params)
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
        speaker_output_high = (audio_command & audio_nmi_low_bit) == 0U;
        speaker.set_speaker(speaker_output_high);
    }

    void redalert_system::run_frame() {
        std::uint64_t cycles_elapsed = 0U;
        for (std::uint32_t line = 0U; line < frame_lines; ++line) {
            const std::uint64_t next_cycle =
                (cpu_cycles_per_frame * static_cast<std::uint64_t>(line + 1U)) / frame_lines;
            const std::uint64_t line_cycles = next_cycle - cycles_elapsed;
            if (line == visible_height) {
                const std::uint64_t irq_pulse = std::min<std::uint64_t>(line_cycles, 16U);
                if (irq_pulse > 0U) {
                    main_cpu.set_irq_line(true);
                    main_cpu.tick(irq_pulse);
                }
                main_cpu.tick(line_cycles - irq_pulse);
            } else {
                main_cpu.tick(line_cycles);
            }
            speaker.tick(line_cycles);
            video.tick(line_cycles);
            cycles_elapsed = next_cycle;
        }

        const auto* proms = roms.region("proms");
        video.compose(bitmap_ram, bitmap_color_ram, char_ram,
                      proms != nullptr ? std::span<const std::uint8_t>(*proms)
                                       : std::span<const std::uint8_t>{},
                      video_control, params.video_control_xor, params.rom_layout);
    }

    void redalert_system::set_inputs(std::uint8_t p1, std::uint8_t p2,
                                     std::uint8_t coins) noexcept {
        key1 = p1;
        key2 = static_cast<std::uint8_t>(p2 & ~key2_sound_status_bit);
        coin_inputs = coins;
        main_cpu.set_nmi_line((coin_inputs & (coin1_bit | coin2_bit | service_bit)) != 0U);
    }

    void redalert_system::write_audio_command(std::uint8_t value) noexcept {
        ++audio_command_write_count;
        audio_command = value;
        sound_handshake = true;
        const bool output = (value & audio_nmi_low_bit) == 0U;
        if (output != speaker_output_high) {
            ++speaker_output_edge_count;
            speaker_output_high = output;
        }
        speaker.set_speaker(speaker_output_high);
    }

    bool redalert_system::flip_screen() const noexcept {
        return ((video_control ^ params.video_control_xor) & video_flip_bit) == 0U;
    }

    void redalert_system::save_state(chips::state_writer& writer) const {
        writer.u32(redalert_system_state_version);
        writer.u8(layout_code(params.rom_layout));
        writer.u8(params.dip_default);
        writer.u8(params.video_control_xor);
        writer.u32(params.cpu_clock_hz);
        writer.u32(board_identity_crc(roms, params));
        main_cpu.save_state(writer);
        video.save_state(writer);
        speaker.save_state(writer);
        writer.bytes(ram);
        writer.bytes(bitmap_ram);
        writer.bytes(bitmap_color_ram);
        writer.bytes(char_ram);
        writer.u8(key1);
        writer.u8(key2);
        writer.u8(coin_inputs);
        writer.u8(dip_switches);
        writer.u8(audio_command);
        writer.u8(video_control);
        writer.u8(bitmap_color);
        writer.boolean(sound_handshake);
        writer.boolean(speaker_output_high);
        writer.u64(audio_command_write_count);
        writer.u64(speaker_output_edge_count);
        writer.u64(video_control_write_count);
        writer.u64(bitmap_color_write_count);
        writer.u64(interrupt_ack_count);
    }

    void redalert_system::load_state(chips::state_reader& reader) {
        if (reader.u32() != redalert_system_state_version) {
            reader.fail();
            return;
        }
        if (reader.u8() != layout_code(params.rom_layout) || reader.u8() != params.dip_default ||
            reader.u8() != params.video_control_xor || reader.u32() != params.cpu_clock_hz ||
            reader.u32() != board_identity_crc(roms, params)) {
            reader.fail();
            return;
        }
        main_cpu.load_state(reader);
        video.load_state(reader);
        speaker.load_state(reader);
        reader.bytes(ram);
        reader.bytes(bitmap_ram);
        reader.bytes(bitmap_color_ram);
        reader.bytes(char_ram);
        key1 = reader.u8();
        key2 = reader.u8();
        coin_inputs = reader.u8();
        dip_switches = reader.u8();
        audio_command = reader.u8();
        video_control = reader.u8();
        bitmap_color = reader.u8();
        sound_handshake = reader.boolean();
        speaker_output_high = reader.boolean();
        audio_command_write_count = reader.u64();
        speaker_output_edge_count = reader.u64();
        video_control_write_count = reader.u64();
        bitmap_color_write_count = reader.u64();
        interrupt_ack_count = reader.u64();
        if (reader.ok()) {
            speaker.set_speaker(speaker_output_high);
            main_cpu.set_nmi_line((coin_inputs & (coin1_bit | coin2_bit | service_bit)) != 0U);
        }
    }

    std::unique_ptr<redalert_system> assemble_redalert(common::rom_set_image image,
                                                       redalert_board_params board_params) {
        return std::make_unique<redalert_system>(std::move(image), board_params);
    }

} // namespace mnemos::manifests::irem_redalert
