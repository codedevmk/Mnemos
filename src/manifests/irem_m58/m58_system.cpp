#include "m58_system.hpp"

#include "crc32.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace mnemos::manifests::irem_m58 {

    namespace {
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

        [[nodiscard]] std::uint8_t tile_pixel_pen(std::span<const std::uint8_t> tile_gfx,
                                                  std::uint16_t code, std::uint32_t x,
                                                  std::uint32_t y,
                                                  bool flip_screen) noexcept {
            if (flip_screen) {
                x = 7U - x;
                y = 7U - y;
            }
            std::uint8_t pen = 0U;
            const std::uint16_t tile = static_cast<std::uint16_t>(code & 0x03FFU);
            for (std::uint32_t plane = 0; plane < 3U; ++plane) {
                const std::uint64_t base =
                    static_cast<std::uint64_t>(plane) * 0x2000U +
                    static_cast<std::uint64_t>(tile) * 8U + y;
                if ((sample_byte(tile_gfx, base, 0U) & (0x80U >> x)) != 0U) {
                    pen |= static_cast<std::uint8_t>(1U << plane);
                }
            }
            return pen;
        }

        [[nodiscard]] std::uint8_t sprite_pixel_pen(std::span<const std::uint8_t> sprite_gfx,
                                                    std::uint16_t code, std::uint32_t x,
                                                    std::uint32_t y, bool flip_x,
                                                    bool flip_y) noexcept {
            if (flip_x) {
                x = 15U - x;
            }
            if (flip_y) {
                y = 15U - y;
            }
            std::uint8_t pen = 0U;
            const std::uint16_t sprite = static_cast<std::uint16_t>(code & 0x01FFU);
            for (std::uint32_t plane = 0; plane < 3U; ++plane) {
                const std::uint64_t base =
                    static_cast<std::uint64_t>(plane) * 0x4000U +
                    static_cast<std::uint64_t>(sprite) * 32U + y * 2U + (x >> 3U);
                if ((sample_byte(sprite_gfx, base, 0U) & (0x80U >> (x & 0x07U))) != 0U) {
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

        [[nodiscard]] std::uint8_t layout_code(std::string_view layout) noexcept {
            if (layout == "ten_yard_fight") {
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
                                                       const m58_board_params& params) {
            std::uint32_t crc =
                security::cryptography::crc32(std::string_view{"m58.board.identity.v1"});
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

    m58_board_params board_params_for(std::string_view set_name) noexcept {
        if (set_name == "10yard" || set_name == "10yardj" || set_name == "vs10yard" ||
            set_name == "vs10yardj") {
            return {.rom_layout = "ten_yard_fight",
                    .dsw1_default = tenyard_dsw1_default,
                    .dsw2_default = tenyard_dsw2_default};
        }
        return {};
    }

    m58_video::m58_video() : pixels_(static_cast<std::size_t>(visible_width) * visible_height, 0U) {
        reset(chips::reset_kind::power_on);
    }

    chips::chip_metadata m58_video::metadata() const noexcept {
        return {.manufacturer = "Irem",
                .part_number = "m58_video_first_pass",
                .family = "irem_m58",
                .klass = chips::chip_class::video,
                .revision = 1U};
    }

    void m58_video::tick(std::uint64_t cycles) { elapsed_cycles_ += cycles; }

    void m58_video::reset(chips::reset_kind /*kind*/) {
        std::fill(pixels_.begin(), pixels_.end(), 0U);
        elapsed_cycles_ = 0U;
        frame_index_ = 0U;
    }

    chips::frame_buffer_view m58_video::framebuffer() const noexcept {
        return {.pixels = pixels_.data(),
                .width = visible_width,
                .height = visible_height,
                .stride = visible_width};
    }

    void m58_video::compose(std::span<const std::uint8_t> tile_gfx,
                            std::span<const std::uint8_t> sprite_gfx,
                            std::span<const std::uint8_t> proms,
                            std::span<const std::uint8_t> video_ram,
                            std::span<const std::uint8_t> color_ram,
                            std::span<const std::uint8_t> sprite_ram,
                            const std::array<std::uint8_t, 32>& scroll_regs,
                            bool flip_screen,
                            std::string_view rom_layout) {
        const std::uint8_t tint = static_cast<std::uint8_t>(0x44U + layout_code(rom_layout));
        for (std::uint32_t y = 0; y < visible_height; ++y) {
            const std::uint8_t stripe =
                static_cast<std::uint8_t>(((y >> 3U) ^ scroll_regs[(y >> 3U) & 0x1FU]) & 0x1FU);
            for (std::uint32_t x = 0; x < visible_width; ++x) {
                const std::uint8_t yard =
                    static_cast<std::uint8_t>(((x >> 4U) + ((y + stripe) >> 5U)) & 0x1FU);
                pixels_[static_cast<std::size_t>(y) * visible_width + x] =
                    prom_color(proms, static_cast<std::uint16_t>(yard), tint);
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
            const std::uint16_t code_banked =
                static_cast<std::uint16_t>(code | ((attr & 0x20U) != 0U ? 0x100U : 0U));
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
                    const std::uint8_t pen = sprite_pixel_pen(sprite_gfx, code_banked, px, py,
                                                              flip_x, flip_y);
                    if (pen != 0U) {
                        const std::uint16_t color_index =
                            static_cast<std::uint16_t>(((attr & 0x1FU) << 3U) | pen);
                        plot_layer_pixel(pixels_, static_cast<std::uint32_t>(x),
                                         static_cast<std::uint32_t>(y),
                                         prom_color(proms, color_index,
                                                    static_cast<std::uint8_t>(tint ^ code)),
                                         flip_screen);
                    }
                }
            }
        }

        for (std::uint32_t tile_y = 0; tile_y < 32U; ++tile_y) {
            for (std::uint32_t tile_x = 0; tile_x < 32U; ++tile_x) {
                const std::size_t index = static_cast<std::size_t>(tile_y) * 32U + tile_x;
                const std::uint8_t tile = sample_byte(video_ram, index, 0U);
                const std::uint8_t attr = sample_byte(color_ram, index, 0U);
                const std::uint16_t code =
                    static_cast<std::uint16_t>(tile | ((attr & 0xC0U) << 2U));
                const std::uint32_t base_x = tile_x * 8U;
                const std::uint32_t base_y = tile_y * 8U;
                const std::uint8_t row_scroll = scroll_regs[tile_y & 0x1FU];
                for (std::uint32_t py = 0; py < 8U; ++py) {
                    const std::uint32_t y = base_y + py;
                    if (y >= visible_height) {
                        continue;
                    }
                    for (std::uint32_t px = 0; px < 8U; ++px) {
                        const std::uint32_t x = (base_x + px + row_scroll) & 0xFFU;
                        const std::uint8_t pen =
                            tile_pixel_pen(tile_gfx, code, px, py, flip_screen);
                        if (pen != 0U) {
                            const std::uint16_t color_index =
                                static_cast<std::uint16_t>(((attr & 0x1FU) << 3U) | pen);
                            plot_layer_pixel(
                                pixels_, x, y,
                                prom_color(proms, color_index,
                                           static_cast<std::uint8_t>(attr + tint)),
                                flip_screen);
                        }
                    }
                }
            }
        }
        ++frame_index_;
    }

    void m58_video::save_state(chips::state_writer& writer) const {
        writer.u64(elapsed_cycles_);
        writer.u64(frame_index_);
        for (const std::uint32_t pixel : pixels_) {
            writer.u32(pixel);
        }
    }

    void m58_video::load_state(chips::state_reader& reader) {
        elapsed_cycles_ = reader.u64();
        frame_index_ = reader.u64();
        for (std::uint32_t& pixel : pixels_) {
            pixel = reader.u32();
        }
    }

    m58_system::m58_system(common::rom_set_image image, m58_board_params board_params)
        : roms(std::move(image)), params(board_params) {
        auto& main_prog = pinned_region(roms, "maincpu", main_rom_size);
        auto& sound_prog = pinned_region(roms, "soundcpu", sound_rom_size);
        (void)pinned_region(roms, "tiles", tile_gfx_size);
        (void)pinned_region(roms, "sprites", sprite_gfx_size);
        (void)pinned_region(roms, "proms", proms_size);

        dsw1 = params.dsw1_default;
        dsw2 = params.dsw2_default;

        main_bus.map_rom(0x0000U, std::span<const std::uint8_t>(main_prog).first(0x8000U));
        main_bus.map_ram(video_ram_base, video_ram, 1);
        main_bus.map_ram(color_ram_base, color_ram, 1);
        main_bus.map_ram(sprite_ram_base, sprite_ram, 1);
        main_bus.map_ram(work_ram_base, work_ram, 1);
        main_bus.map_mmio(
            input0_address, 5U,
            [this](std::uint32_t address) -> std::uint8_t {
                switch (address & 0xFFFFU) {
                case input0_address:
                    return input0;
                case input1_address:
                    return input1;
                case input2_address:
                    return input2;
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
                case input0_address:
                    latch_sound_command(value);
                    break;
                case input1_address:
                    flip_latch = value;
                    ++flip_write_count;
                    flip_screen = (value & 0x01U) != 0U;
                    break;
                default:
                    break;
                }
            },
            2);

        main_cpu.attach_bus(main_bus);

        sound_bus.map_rom(sound_rom_base,
                          std::span<const std::uint8_t>(sound_prog).first(sound_rom_mapped_size),
                          0);
        sound_bus.map_ram(sound_work_ram_base, sound_ram, 1);
        sound_cpu.attach_bus(sound_bus);

        main_cpu.set_port_out([this](std::uint16_t port, std::uint8_t value) {
            const std::uint8_t p = static_cast<std::uint8_t>(port & 0xFFU);
            if (p >= 0x10U && p <= 0x1FU) {
                scroll_regs[p - 0x10U] = value;
            }
        });

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
                ++sound_cpu_psg_write_count;
                break;
            case z80_port_ay1_address:
                sound_ay1_address = static_cast<std::uint8_t>(value & 0x0FU);
                ay1.address(sound_ay1_address);
                break;
            case z80_port_ay1_data:
                ay1.write(value);
                ++sound_cpu_psg_write_count;
                break;
            case z80_port_latch_ack:
                sound_latch_irq = false;
                ++sound_latch_ack_count;
                update_sound_irq();
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
        ay0.enable_audio_capture(true);
        ay1.enable_audio_capture(true);
    }

    void m58_system::run_frame() {
        main_cpu.tick(main_cycles_per_frame);
        if (!sound_cpu.reset_line_held()) {
            sound_cpu.tick(sound_cycles_per_frame);
        }
        ay0.tick(main_cycles_per_frame);
        ay1.tick(main_cycles_per_frame);
        video.tick(main_cycles_per_frame);

        const auto* tile_gfx = roms.region("tiles");
        const auto* sprite_gfx = roms.region("sprites");
        const auto* proms = roms.region("proms");
        video.compose(tile_gfx != nullptr ? std::span<const std::uint8_t>(*tile_gfx)
                                          : std::span<const std::uint8_t>{},
                      sprite_gfx != nullptr ? std::span<const std::uint8_t>(*sprite_gfx)
                                            : std::span<const std::uint8_t>{},
                      proms != nullptr ? std::span<const std::uint8_t>(*proms)
                                       : std::span<const std::uint8_t>{},
                      video_ram, color_ram, sprite_ram, scroll_regs, flip_screen,
                      params.rom_layout);
    }

    void m58_system::set_inputs(std::uint8_t p1, std::uint8_t p2, std::uint8_t system) noexcept {
        input0 = p1;
        input1 = p2;
        input2 = system;
    }

    void m58_system::latch_sound_command(std::uint8_t value) noexcept {
        sound_command = value;
        sound_latch_irq = true;
        ++sound_command_write_count;
        update_sound_irq();
    }

    void m58_system::update_sound_irq() noexcept { sound_cpu.set_irq_line(sound_latch_irq); }

    void m58_system::save_state(chips::state_writer& writer) const {
        writer.u32(m58_system_state_version);
        writer.u8(layout_code(params.rom_layout));
        writer.u8(params.dsw1_default);
        writer.u8(params.dsw2_default);
        writer.u32(board_identity_crc(roms, params));
        main_cpu.save_state(writer);
        sound_cpu.save_state(writer);
        video.save_state(writer);
        ay0.save_state(writer);
        ay1.save_state(writer);
        for (const auto& region :
             {std::span<const std::uint8_t>(video_ram), std::span<const std::uint8_t>(color_ram),
              std::span<const std::uint8_t>(sprite_ram), std::span<const std::uint8_t>(work_ram),
              std::span<const std::uint8_t>(sound_ram)}) {
            writer.u32(static_cast<std::uint32_t>(region.size()));
            for (const std::uint8_t byte : region) {
                writer.u8(byte);
            }
        }
        for (const std::uint8_t value : scroll_regs) {
            writer.u8(value);
        }
        writer.u8(input0);
        writer.u8(input1);
        writer.u8(input2);
        writer.u8(dsw1);
        writer.u8(dsw2);
        writer.u8(sound_command);
        writer.u8(sound_ay0_address);
        writer.u8(sound_ay1_address);
        writer.u8(flip_latch);
        writer.boolean(flip_screen);
        writer.boolean(sound_latch_irq);
        writer.u64(sound_command_write_count);
        writer.u64(sound_latch_ack_count);
        writer.u64(sound_cpu_psg_write_count);
        writer.u64(flip_write_count);
    }

    void m58_system::load_state(chips::state_reader& reader) {
        if (reader.u32() != m58_system_state_version) {
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
        if (!load_region(video_ram) || !load_region(color_ram) || !load_region(sprite_ram) ||
            !load_region(work_ram) || !load_region(sound_ram)) {
            return;
        }
        for (std::uint8_t& value : scroll_regs) {
            value = reader.u8();
        }
        input0 = reader.u8();
        input1 = reader.u8();
        input2 = reader.u8();
        dsw1 = reader.u8();
        dsw2 = reader.u8();
        sound_command = reader.u8();
        sound_ay0_address = reader.u8();
        sound_ay1_address = reader.u8();
        flip_latch = reader.u8();
        flip_screen = reader.boolean();
        sound_latch_irq = reader.boolean();
        sound_command_write_count = reader.u64();
        sound_latch_ack_count = reader.u64();
        sound_cpu_psg_write_count = reader.u64();
        flip_write_count = reader.u64();
        if (reader.ok()) {
            ay0.address(sound_ay0_address);
            ay1.address(sound_ay1_address);
            sound_cpu.set_reset_line(false);
            update_sound_irq();
        }
    }

    std::unique_ptr<m58_system> assemble_m58(common::rom_set_image image,
                                             m58_board_params board_params) {
        return std::make_unique<m58_system>(std::move(image), board_params);
    }

} // namespace mnemos::manifests::irem_m58
