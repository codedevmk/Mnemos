#include "travrusa_system.hpp"

#include "crc32.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace mnemos::manifests::irem_travrusa {

    namespace {
        inline constexpr std::size_t gfx_plane_stride = 0x2000U;

        [[nodiscard]] std::vector<std::uint8_t>&
        pinned_region(common::rom_set_image& image, std::string_view name, std::size_t size) {
            auto& bytes = image.regions[std::string{name}];
            if (bytes.size() < size) {
                bytes.resize(size, 0xFFU);
            }
            return bytes;
        }

        [[nodiscard]] std::uint8_t sample_byte(std::span<const std::uint8_t> data,
                                               std::uint64_t index,
                                               std::uint8_t fallback) noexcept {
            if (data.empty()) {
                return fallback;
            }
            return data[static_cast<std::size_t>(index % data.size())];
        }

        [[nodiscard]] std::uint32_t rgb(std::uint8_t r, std::uint8_t g, std::uint8_t b) noexcept {
            return (static_cast<std::uint32_t>(r) << 16U) | (static_cast<std::uint32_t>(g) << 8U) |
                   b;
        }

        [[nodiscard]] std::uint32_t prom_color(std::span<const std::uint8_t> proms,
                                               std::uint16_t index,
                                               std::uint8_t mix) noexcept {
            const std::uint8_t v = sample_byte(proms, index, mix);
            const std::uint8_t r = static_cast<std::uint8_t>((((v >> 0U) & 0x07U) * 36U) ^ mix);
            const std::uint8_t g =
                static_cast<std::uint8_t>((((v >> 3U) & 0x07U) * 36U) ^ (mix >> 1U));
            const std::uint8_t b =
                static_cast<std::uint8_t>((((v >> 6U) & 0x03U) * 85U) ^ (mix << 1U));
            return rgb(r, g, b);
        }

        [[nodiscard]] std::uint8_t layout_code(std::string_view layout) noexcept {
            if (layout == "travrusa") {
                return 1U;
            }
            if (layout == "motorace_encrypted") {
                return 2U;
            }
            if (layout == "travrusa_bootleg") {
                return 3U;
            }
            return 0U;
        }

        [[nodiscard]] std::uint8_t tile_pixel_pen(std::span<const std::uint8_t> tiles,
                                                  std::uint16_t code,
                                                  std::uint32_t x,
                                                  std::uint32_t y,
                                                  bool flip_x,
                                                  bool flip_y) noexcept {
            if (flip_x) {
                x = 7U - x;
            }
            if (flip_y) {
                y = 7U - y;
            }
            std::uint8_t pen = 0U;
            const std::uint16_t tile = static_cast<std::uint16_t>(code & 0x03FFU);
            for (std::uint8_t plane = 0U; plane < 3U; ++plane) {
                const std::uint64_t offset = static_cast<std::uint64_t>(plane) * gfx_plane_stride +
                                             static_cast<std::uint64_t>(tile) * 8U + y;
                if ((sample_byte(tiles, offset, 0U) & (0x80U >> x)) != 0U) {
                    pen |= static_cast<std::uint8_t>(1U << plane);
                }
            }
            return pen;
        }

        [[nodiscard]] std::uint8_t sprite_pixel_pen(std::span<const std::uint8_t> sprites,
                                                    std::uint16_t code,
                                                    std::uint32_t x,
                                                    std::uint32_t y,
                                                    bool flip_x,
                                                    bool flip_y) noexcept {
            if (flip_x) {
                x = 15U - x;
            }
            if (flip_y) {
                y = 15U - y;
            }
            std::uint8_t pen = 0U;
            const std::uint16_t sprite = static_cast<std::uint16_t>(code & 0x00FFU);
            for (std::uint8_t plane = 0U; plane < 3U; ++plane) {
                const std::uint64_t offset = static_cast<std::uint64_t>(plane) * gfx_plane_stride +
                                             static_cast<std::uint64_t>(sprite) * 32U +
                                             static_cast<std::uint64_t>(y) * 2U + (x >> 3U);
                if ((sample_byte(sprites, offset, 0U) & (0x80U >> (x & 0x07U))) != 0U) {
                    pen |= static_cast<std::uint8_t>(1U << plane);
                }
            }
            return pen;
        }

        void plot_layer_pixel(std::vector<std::uint32_t>& pixels, std::uint32_t x,
                              std::uint32_t y, std::uint32_t color, bool flip_screen) noexcept {
            if (x >= visible_width || y >= visible_height) {
                return;
            }
            if (flip_screen) {
                x = visible_width - 1U - x;
                y = visible_height - 1U - y;
            }
            pixels[static_cast<std::size_t>(y) * visible_width + x] = color;
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
                                                       const travrusa_board_params& params) {
            std::uint32_t crc =
                security::cryptography::crc32(std::string_view{"travrusa.board.identity.v1"});
            const std::array<std::uint8_t, 3> params_bytes{
                layout_code(params.rom_layout), params.dsw1_default, params.dsw2_default};
            crc = security::cryptography::crc32(
                std::span<const std::uint8_t>(params_bytes.data(), params_bytes.size()), crc);
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
    } // namespace

    travrusa_board_params board_params_for(std::string_view set_name) noexcept {
        if (set_name == "motorace") {
            return {.rom_layout = "motorace_encrypted",
                    .dsw1_default = travrusa_dsw1_default,
                    .dsw2_default = travrusa_dsw2_default};
        }
        if (set_name == "travrusab" || set_name == "travrusab2") {
            return {.rom_layout = "travrusa_bootleg",
                    .dsw1_default = travrusa_dsw1_default,
                    .dsw2_default = travrusa_dsw2_default};
        }
        return {.rom_layout = "travrusa",
                .dsw1_default = travrusa_dsw1_default,
                .dsw2_default = travrusa_dsw2_default};
    }

    travrusa_video::travrusa_video()
        : pixels_(static_cast<std::size_t>(visible_width) * visible_height, 0U) {
        reset(chips::reset_kind::power_on);
    }

    chips::chip_metadata travrusa_video::metadata() const noexcept {
        return {.manufacturer = "Irem",
                .part_number = "travrusa_video_first_pass",
                .family = "irem_travrusa",
                .klass = chips::chip_class::video,
                .revision = 1U};
    }

    void travrusa_video::tick(std::uint64_t cycles) { elapsed_cycles_ += cycles; }

    void travrusa_video::reset(chips::reset_kind /*kind*/) {
        std::fill(pixels_.begin(), pixels_.end(), 0U);
        elapsed_cycles_ = 0U;
        frame_index_ = 0U;
    }

    chips::frame_buffer_view travrusa_video::framebuffer() const noexcept {
        return {.pixels = pixels_.data(),
                .width = visible_width,
                .height = visible_height,
                .stride = visible_width};
    }

    void travrusa_video::compose(std::span<const std::uint8_t> tiles,
                                 std::span<const std::uint8_t> sprites,
                                 std::span<const std::uint8_t> proms,
                                 std::span<const std::uint8_t> video_ram,
                                 std::span<const std::uint8_t> sprite_ram,
                                 std::uint16_t scroll_x,
                                 bool flip_screen,
                                 std::string_view rom_layout) {
        const std::uint8_t tint = static_cast<std::uint8_t>(0x25U + layout_code(rom_layout));
        const std::uint16_t scroll = static_cast<std::uint16_t>(scroll_x & 0x01FFU);
        for (std::uint32_t y = 0; y < visible_height; ++y) {
            for (std::uint32_t x = 0; x < visible_width; ++x) {
                const std::uint8_t road =
                    y >= 184U ? static_cast<std::uint8_t>(((x + scroll) >> 3U) & 0x0FU) : 0U;
                const std::uint8_t lane =
                    y >= 184U ? static_cast<std::uint8_t>((x >> 5U) ^ (y >> 4U)) : 0U;
                const std::uint8_t sky =
                    static_cast<std::uint8_t>(((y >> 4U) + (x >> 6U)) & 0x0FU);
                const std::uint8_t mix =
                    static_cast<std::uint8_t>(sky ^ road ^ lane ^ tint ^ frame_index_);
                pixels_[static_cast<std::size_t>(y) * visible_width + x] =
                    prom_color(proms, static_cast<std::uint16_t>(mix + ((x >> 3U) & 0x1FU)),
                               tint);
            }
        }

        for (std::size_t offset = 0; offset + 3U < sprite_ram.size(); offset += 4U) {
            const std::uint8_t raw_y = sprite_ram[offset + 0U];
            const std::uint8_t code = sprite_ram[offset + 1U];
            const std::uint8_t attr = sprite_ram[offset + 2U];
            const std::uint8_t raw_x = sprite_ram[offset + 3U];
            if ((raw_y == 0U && code == 0U && attr == 0U && raw_x == 0U) ||
                (raw_y == 0xFFU && code == 0xFFU && attr == 0xFFU && raw_x == 0xFFU)) {
                continue;
            }
            const int base_x = static_cast<int>(raw_x);
            const int base_y = 241 - static_cast<int>(raw_y);
            const bool flip_x = (attr & 0x40U) != 0U;
            const bool flip_y = (attr & 0x80U) != 0U;
            const std::uint8_t color = static_cast<std::uint8_t>(attr & 0x0FU);
            for (std::uint32_t py = 0; py < 16U; ++py) {
                const int y = base_y + static_cast<int>(py);
                if (y < 0 || y >= static_cast<int>(visible_height)) {
                    continue;
                }
                for (std::uint32_t px = 0; px < 16U; ++px) {
                    const int x = base_x + static_cast<int>(px);
                    if (x < 0 || x >= static_cast<int>(visible_width)) {
                        continue;
                    }
                    const std::uint8_t pen = sprite_pixel_pen(sprites, code, px, py, flip_x, flip_y);
                    if (pen != 0U) {
                        const std::uint16_t palette_index =
                            static_cast<std::uint16_t>(0x80U + color * 8U + pen);
                        plot_layer_pixel(pixels_, static_cast<std::uint32_t>(x),
                                         static_cast<std::uint32_t>(y),
                                         prom_color(proms, palette_index,
                                                    static_cast<std::uint8_t>(tint ^ code)),
                                         flip_screen);
                    }
                }
            }
        }

        for (std::uint32_t tile_y = 0; tile_y < 32U; ++tile_y) {
            for (std::uint32_t tile_x = 0; tile_x < 64U; ++tile_x) {
                const std::size_t index =
                    (static_cast<std::size_t>(tile_y) * 64U + tile_x) * 2U;
                const std::uint8_t code_low = sample_byte(video_ram, index, 0U);
                const std::uint8_t attr = sample_byte(video_ram, index + 1U, 0U);
                const std::uint16_t code = static_cast<std::uint16_t>(
                    code_low | (static_cast<std::uint16_t>(attr & 0xC0U) << 2U));
                const bool flip_y = (attr & 0x10U) != 0U;
                const bool flip_x = (attr & 0x20U) != 0U;
                const std::uint8_t color = static_cast<std::uint8_t>(attr & 0x0FU);

                int base_x = static_cast<int>(tile_x * 8U) - static_cast<int>(scroll) - 8;
                while (base_x < -511) {
                    base_x += 512;
                }
                while (base_x > 511) {
                    base_x -= 512;
                }
                const int base_y = static_cast<int>(tile_y * 8U);

                for (int wrap = -512; wrap <= 512; wrap += 512) {
                    const int wrapped_x = base_x + wrap;
                    if (wrapped_x >= static_cast<int>(visible_width) || wrapped_x <= -8) {
                        continue;
                    }
                    for (std::uint32_t py = 0; py < 8U; ++py) {
                        const int y = base_y + static_cast<int>(py);
                        if (y < 0 || y >= static_cast<int>(visible_height)) {
                            continue;
                        }
                        for (std::uint32_t px = 0; px < 8U; ++px) {
                            const int x = wrapped_x + static_cast<int>(px);
                            if (x < 0 || x >= static_cast<int>(visible_width)) {
                                continue;
                            }
                            const std::uint8_t pen =
                                tile_pixel_pen(tiles, code, px, py, flip_x, flip_y);
                            if (pen != 0U) {
                                const std::uint16_t palette_index =
                                    static_cast<std::uint16_t>(color * 8U + pen);
                                plot_layer_pixel(
                                    pixels_, static_cast<std::uint32_t>(x),
                                    static_cast<std::uint32_t>(y),
                                    prom_color(proms, palette_index,
                                               static_cast<std::uint8_t>(attr ^ tint)),
                                    flip_screen);
                            }
                        }
                    }
                }
            }
        }
        ++frame_index_;
    }

    void travrusa_video::save_state(chips::state_writer& writer) const {
        writer.u64(elapsed_cycles_);
        writer.u64(frame_index_);
        for (const std::uint32_t pixel : pixels_) {
            writer.u32(pixel);
        }
    }

    void travrusa_video::load_state(chips::state_reader& reader) {
        elapsed_cycles_ = reader.u64();
        frame_index_ = reader.u64();
        for (std::uint32_t& pixel : pixels_) {
            pixel = reader.u32();
        }
    }

    travrusa_system::travrusa_system(common::rom_set_image image,
                                     travrusa_board_params board_params)
        : roms(std::move(image)), params(board_params) {
        auto& main_prog = pinned_region(roms, "maincpu", main_rom_size);
        auto& sound_prog = pinned_region(roms, "soundcpu", sound_rom_size);
        (void)pinned_region(roms, "tiles", tiles_size);
        (void)pinned_region(roms, "sprites", sprites_size);
        (void)pinned_region(roms, "proms", proms_size);

        dsw1 = params.dsw1_default;
        dsw2 = params.dsw2_default;

        main_bus.map_rom(0x0000U, std::span<const std::uint8_t>(main_prog).first(main_rom_mapped_size));
        main_bus.map_ram(video_ram_base, video_ram, 1);
        main_bus.map_mmio(
            scroll_x_low_address, 1U,
            [this](std::uint32_t) -> std::uint8_t { return scroll_x_low; },
            [this](std::uint32_t, std::uint8_t value) {
                scroll_x_low = value;
                ++scroll_write_count;
            },
            2);
        main_bus.map_mmio(
            scroll_x_high_address, 1U,
            [this](std::uint32_t) -> std::uint8_t { return scroll_x_high; },
            [this](std::uint32_t, std::uint8_t value) {
                scroll_x_high = static_cast<std::uint8_t>(value & 0x01U);
                ++scroll_write_count;
            },
            2);
        main_bus.map_ram(sprite_ram_base, sprite_ram, 1);
        main_bus.map_mmio(
            input_system_address, 5U,
            [this](std::uint32_t address) -> std::uint8_t {
                switch (address & 0xFFFFU) {
                case input_system_address:
                    return input_system;
                case input_p1_address:
                    return input_p1;
                case input_p2_address:
                    return input_p2;
                case dsw1_address:
                    return dsw1;
                case dsw2_address:
                    return dsw2;
                default:
                    return 0xFFU;
                }
            },
            [this](std::uint32_t address, std::uint8_t value) {
                switch (address & 0xFFFFU) {
                case input_system_address:
                    latch_sound_command(value);
                    break;
                case input_p1_address:
                    flip_latch = value;
                    ++flip_write_count;
                    flip_screen = (value & 0x01U) != 0U;
                    break;
                default:
                    break;
                }
            },
            2);
        main_bus.map_ram(work_ram_base, work_ram, 1);
        main_cpu.attach_bus(main_bus);

        sound_bus.map_rom(sound_rom_base,
                          std::span<const std::uint8_t>(sound_prog).first(sound_rom_mapped_size),
                          0);
        sound_bus.map_ram(sound_work_ram_base, sound_ram, 1);
        sound_cpu.attach_bus(sound_bus);

        sound_cpu.set_port_in([this](std::uint16_t port) -> std::uint8_t {
            switch (port & 0xFFU) {
            case z80_port_ay0_data:
                return ay0.read();
            case z80_port_latch:
                return sound_command;
            case z80_port_ay1_data:
                return ay1.read();
            default:
                return 0xFFU;
            }
        });
        sound_cpu.set_port_out([this](std::uint16_t port, std::uint8_t value) {
            switch (port & 0xFFU) {
            case z80_port_ay0_address:
                sound_ay0_address = static_cast<std::uint8_t>(value & 0x0FU);
                ay0.address(sound_ay0_address);
                break;
            case z80_port_ay0_data:
                ay0.write(value);
                break;
            case z80_port_ay1_address:
                sound_ay1_address = static_cast<std::uint8_t>(value & 0x0FU);
                ay1.address(sound_ay1_address);
                break;
            case z80_port_ay1_data:
                ay1.write(value);
                break;
            case z80_port_latch_ack:
                sound_latch_irq = false;
                ++sound_latch_ack_count;
                update_sound_irq();
                break;
            case z80_port_msm_data:
                sound_cpu_write_msm(value);
                break;
            case z80_port_msm_control:
                msm.reset_w((value & 0x01U) != 0U);
                break;
            default:
                break;
            }
        });
        sound_cpu.set_irq_vector(
            [this]() -> std::uint8_t { return sound_latch_irq ? z80_rst_latch : z80_rst_idle; });

        main_cpu.reset(chips::reset_kind::power_on);
        sound_cpu.reset(chips::reset_kind::power_on);
        sound_cpu.set_reset_line(false);

        ay0.set_clock_divider(static_cast<int>(ssg_clock_divider));
        ay1.set_clock_divider(static_cast<int>(ssg_clock_divider));
        msm.set_clock_divider(static_cast<int>(ssg_clock_divider));
        ay0.enable_audio_capture(true);
        ay1.enable_audio_capture(true);
        msm.enable_audio_capture(true);
    }

    void travrusa_system::run_frame() {
        main_cpu.tick(main_cycles_per_frame);
        if (!sound_cpu.reset_line_held()) {
            sound_cpu.tick(sound_cycles_per_frame);
        }
        ay0.tick(main_cycles_per_frame);
        ay1.tick(main_cycles_per_frame);
        msm.tick(main_cycles_per_frame);
        video.tick(main_cycles_per_frame);

        const auto* tiles = roms.region("tiles");
        const auto* sprites = roms.region("sprites");
        const auto* proms = roms.region("proms");
        video.compose(tiles != nullptr ? std::span<const std::uint8_t>(*tiles)
                                       : std::span<const std::uint8_t>{},
                      sprites != nullptr ? std::span<const std::uint8_t>(*sprites)
                                         : std::span<const std::uint8_t>{},
                      proms != nullptr ? std::span<const std::uint8_t>(*proms)
                                       : std::span<const std::uint8_t>{},
                      video_ram, sprite_ram, scroll_x(), flip_screen, params.rom_layout);
    }

    void travrusa_system::set_inputs(std::uint8_t p1, std::uint8_t p2,
                                     std::uint8_t system) noexcept {
        input_p1 = p1;
        input_p2 = p2;
        input_system = system;
    }

    std::uint16_t travrusa_system::scroll_x() const noexcept {
        return static_cast<std::uint16_t>(
            scroll_x_low | (static_cast<std::uint16_t>(scroll_x_high & 0x01U) << 8U));
    }

    void travrusa_system::latch_sound_command(std::uint8_t value) noexcept {
        sound_command = value;
        sound_latch_irq = true;
        ++sound_command_write_count;
        update_sound_irq();
    }

    void travrusa_system::update_sound_irq() noexcept { sound_cpu.set_irq_line(sound_latch_irq); }

    void travrusa_system::sound_cpu_write_msm(std::uint8_t value) noexcept {
        msm.data_w(static_cast<std::uint8_t>(value & 0x0FU));
        msm.vclk_tick();
        ++sound_cpu_msm_write_count;
    }

    void travrusa_system::save_state(chips::state_writer& writer) const {
        writer.u32(travrusa_system_state_version);
        writer.u8(layout_code(params.rom_layout));
        writer.u8(params.dsw1_default);
        writer.u8(params.dsw2_default);
        writer.u32(board_identity_crc(roms, params));
        main_cpu.save_state(writer);
        sound_cpu.save_state(writer);
        video.save_state(writer);
        ay0.save_state(writer);
        ay1.save_state(writer);
        msm.save_state(writer);
        for (const auto& region :
             {std::span<const std::uint8_t>(video_ram), std::span<const std::uint8_t>(sprite_ram),
              std::span<const std::uint8_t>(work_ram), std::span<const std::uint8_t>(sound_ram)}) {
            writer.u32(static_cast<std::uint32_t>(region.size()));
            for (const std::uint8_t byte : region) {
                writer.u8(byte);
            }
        }
        writer.u8(input_system);
        writer.u8(input_p1);
        writer.u8(input_p2);
        writer.u8(dsw1);
        writer.u8(dsw2);
        writer.u8(scroll_x_low);
        writer.u8(scroll_x_high);
        writer.u8(sound_command);
        writer.u8(sound_ay0_address);
        writer.u8(sound_ay1_address);
        writer.u8(flip_latch);
        writer.boolean(flip_screen);
        writer.boolean(sound_latch_irq);
        writer.u64(sound_command_write_count);
        writer.u64(sound_latch_ack_count);
        writer.u64(sound_cpu_msm_write_count);
        writer.u64(flip_write_count);
        writer.u64(scroll_write_count);
    }

    void travrusa_system::load_state(chips::state_reader& reader) {
        if (reader.u32() != travrusa_system_state_version) {
            reader.fail();
            return;
        }
        if (reader.u8() != layout_code(params.rom_layout) || reader.u8() != params.dsw1_default ||
            reader.u8() != params.dsw2_default ||
            reader.u32() != board_identity_crc(roms, params)) {
            reader.fail();
            return;
        }
        main_cpu.load_state(reader);
        sound_cpu.load_state(reader);
        video.load_state(reader);
        ay0.load_state(reader);
        ay1.load_state(reader);
        msm.load_state(reader);

        const auto load_region = [&reader](std::span<std::uint8_t> region) {
            const std::uint32_t size = reader.u32();
            if (size != region.size()) {
                reader.fail();
                return false;
            }
            for (std::uint8_t& byte : region) {
                byte = reader.u8();
            }
            return true;
        };
        if (!load_region(video_ram) || !load_region(sprite_ram) || !load_region(work_ram) ||
            !load_region(sound_ram)) {
            return;
        }
        input_system = reader.u8();
        input_p1 = reader.u8();
        input_p2 = reader.u8();
        dsw1 = reader.u8();
        dsw2 = reader.u8();
        scroll_x_low = reader.u8();
        scroll_x_high = reader.u8();
        sound_command = reader.u8();
        sound_ay0_address = reader.u8();
        sound_ay1_address = reader.u8();
        flip_latch = reader.u8();
        flip_screen = reader.boolean();
        sound_latch_irq = reader.boolean();
        sound_command_write_count = reader.u64();
        sound_latch_ack_count = reader.u64();
        sound_cpu_msm_write_count = reader.u64();
        flip_write_count = reader.u64();
        scroll_write_count = reader.u64();
        if (reader.ok()) {
            ay0.address(sound_ay0_address);
            ay1.address(sound_ay1_address);
            sound_cpu.set_reset_line(false);
            update_sound_irq();
        }
    }

    std::unique_ptr<travrusa_system> assemble_travrusa(common::rom_set_image image,
                                                       travrusa_board_params board_params) {
        return std::make_unique<travrusa_system>(std::move(image), board_params);
    }

} // namespace mnemos::manifests::irem_travrusa
