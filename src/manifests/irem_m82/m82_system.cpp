#include "m82_system.hpp"

#include "crc32.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>

namespace mnemos::manifests::irem_m82 {

    namespace {
        inline constexpr std::uint32_t max_saved_dac_write_events = 1U << 20U;
        constexpr std::array<std::uint16_t, 4> fg_below{0x0001U, 0xFF01U, 0xFFFFU, 0xFFFFU};
        constexpr std::array<std::uint16_t, 4> fg_above{0xFFFFU, 0x00FFU, 0x0001U, 0x0001U};
        constexpr std::array<std::uint16_t, 4> bg_below{0x0000U, 0xFF00U, 0xFFFEU, 0xFFFEU};
        constexpr std::array<std::uint16_t, 4> bg_above{0xFFFFU, 0x00FFU, 0x0001U, 0x0001U};

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

        [[nodiscard]] std::uint32_t rgb_from_byte(std::uint8_t value, std::uint8_t tint) noexcept {
            const std::uint32_t r = static_cast<std::uint32_t>((value * 3U + tint) & 0xFFU);
            const std::uint32_t g =
                static_cast<std::uint32_t>(((value << 1U) ^ (tint * 5U)) & 0xFFU);
            const std::uint32_t b =
                static_cast<std::uint32_t>(((value >> 1U) + (tint * 11U)) & 0xFFU);
            return (r << 16U) | (g << 8U) | b;
        }

        [[nodiscard]] constexpr std::uint32_t expand5(std::uint8_t value) noexcept {
            const std::uint32_t v = value & 0x1FU;
            return (v << 3U) | (v >> 2U);
        }

        [[nodiscard]] std::uint32_t palette_rgb(std::span<const std::uint8_t> palette,
                                                std::size_t index) noexcept {
            if (palette.size() < 0xC00U) {
                return 0U;
            }
            const std::size_t byte = (index & 0xFFU) * 2U;
            const std::uint32_t r = expand5(palette[byte]);
            const std::uint32_t g = expand5(palette[byte + 0x400U]);
            const std::uint32_t b = expand5(palette[byte + 0x800U]);
            return (r << 16U) | (g << 8U) | b;
        }

        [[nodiscard]] std::uint16_t read_le16(std::span<const std::uint8_t> data,
                                              std::size_t offset) noexcept {
            if (offset + 1U >= data.size()) {
                return 0U;
            }
            return static_cast<std::uint16_t>(data[offset] | (data[offset + 1U] << 8U));
        }

        [[nodiscard]] bool has_nonzero(std::span<const std::uint8_t> data) noexcept {
            return std::any_of(data.begin(), data.end(),
                               [](std::uint8_t value) { return value != 0U; });
        }

        [[nodiscard]] std::uint32_t crc32_u64(std::uint32_t crc, std::uint64_t value) noexcept {
            std::array<std::uint8_t, 8> bytes{};
            for (std::size_t i = 0; i < bytes.size(); ++i) {
                bytes[i] = static_cast<std::uint8_t>((value >> (i * 8U)) & 0xFFU);
            }
            return security::cryptography::crc32(
                std::span<const std::uint8_t>(bytes.data(), bytes.size()), crc);
        }

        [[nodiscard]] std::uint32_t crc32_u16(std::uint32_t crc, std::uint16_t value) noexcept {
            std::array<std::uint8_t, 2> bytes{static_cast<std::uint8_t>(value),
                                              static_cast<std::uint8_t>(value >> 8U)};
            return security::cryptography::crc32(
                std::span<const std::uint8_t>(bytes.data(), bytes.size()), crc);
        }

        [[nodiscard]] std::uint32_t crc32_u8(std::uint32_t crc, std::uint8_t value) noexcept {
            std::array<std::uint8_t, 1> bytes{value};
            return security::cryptography::crc32(
                std::span<const std::uint8_t>(bytes.data(), bytes.size()), crc);
        }

        [[nodiscard]] std::uint32_t rom_identity_crc(const common::rom_set_image& roms,
                                                     const m82_board_params& params) {
            std::uint32_t crc =
                security::cryptography::crc32(std::string_view{"m82.rom.identity.v1"});
            crc = crc32_u16(crc, params.dip_default);
            crc = crc32_u8(crc, params.bootleg_layout ? 1U : 0U);
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

    m82_board_params board_params_for(std::string_view set_name) noexcept {
        if (set_name == "rtype2m82b") {
            return {.dip_default = 0xFFFFU, .bootleg_layout = true};
        }
        if (set_name == "rtype2" || set_name == "rtype2j" || set_name == "rtype2jc") {
            return {.dip_default = 0xFFFFU, .bootleg_layout = false};
        }
        return {};
    }

    m82_video::m82_video() : pixels_(static_cast<std::size_t>(visible_width) * visible_height, 0U) {
        reset(chips::reset_kind::power_on);
    }

    chips::chip_metadata m82_video::metadata() const noexcept {
        return {.manufacturer = "Irem",
                .part_number = "m82_video",
                .family = "irem_m82",
                .klass = chips::chip_class::video,
                .revision = 1U};
    }

    void m82_video::tick(std::uint64_t cycles) { elapsed_cycles_ += cycles; }

    void m82_video::reset(chips::reset_kind /*kind*/) {
        std::fill(pixels_.begin(), pixels_.end(), 0U);
        sprite_buffer_.fill(0U);
        elapsed_cycles_ = 0U;
        frame_index_ = 0U;
        scroll_x_ = 0U;
        scroll_y_ = 0U;
        raster_compare_ = -1;
        display_enabled_ = true;
        flip_screen_ = false;
    }

    chips::frame_buffer_view m82_video::framebuffer() const noexcept {
        return {.pixels = pixels_.data(),
                .width = visible_width,
                .height = visible_height,
                .stride = visible_width};
    }

    void m82_video::latch_sprites(std::span<const std::uint8_t> sprite_ram) noexcept {
        sprite_buffer_.fill(0U);
        const std::size_t count = std::min(sprite_ram.size(), sprite_buffer_.size());
        std::copy_n(sprite_ram.begin(), count, sprite_buffer_.begin());
    }

    void m82_video::begin_frame() noexcept { std::fill(pixels_.begin(), pixels_.end(), 0U); }

    void m82_video::compose_scanline(std::span<const std::uint8_t> tiles,
                                     std::span<const std::uint8_t> sprites,
                                     std::span<const std::uint8_t> proms,
                                     std::span<const std::uint8_t> vram,
                                     std::span<const std::uint8_t> rowscroll,
                                     std::span<const std::uint8_t> palette, std::uint32_t line,
                                     bool bootleg_layout) {
        if (line >= visible_height) {
            return;
        }

        const std::uint32_t out_line = flip_screen_ ? (visible_height - 1U - line) : line;
        const auto row_offset = static_cast<std::ptrdiff_t>(out_line * visible_width);
        std::fill_n(pixels_.begin() + row_offset, visible_width, 0U);
        if (!display_enabled_) {
            return;
        }

        constexpr std::size_t tilemap_bytes = 64U * 64U * 4U;
        constexpr std::size_t tile_cell_bytes = 8U;
        constexpr std::size_t sprite_entry_bytes = 8U;
        constexpr std::size_t sprite_cell_bytes = 32U;
        constexpr std::uint32_t map_pixels = 64U * 8U;
        constexpr std::uint32_t beam_x_origin = 64U;

        const std::size_t tile_plane_size = tiles.size() / 4U;
        const std::size_t tile_count = tile_plane_size / tile_cell_bytes;
        const std::size_t sprite_plane_size = sprites.size() / 4U;
        const std::size_t sprite_cell_count = sprite_plane_size / sprite_cell_bytes;
        const bool tile_path_ready = tile_count > 0U && vram.size() >= tilemap_bytes &&
                                     palette.size() >= 0xC00U && has_nonzero(vram) &&
                                     has_nonzero(palette);
        const bool sprite_path_ready = sprite_cell_count > 0U && palette.size() >= 0xC00U &&
                                       has_nonzero(std::span<const std::uint8_t>(
                                           sprite_buffer_.data(), sprite_buffer_.size())) &&
                                       has_nonzero(palette);

        if (tile_path_ready || sprite_path_ready) {
            auto plot = [&](std::uint32_t x, std::uint32_t y, std::uint32_t rgb) {
                const std::uint32_t out_x = flip_screen_ ? (visible_width - 1U - x) : x;
                const std::uint32_t out_y = flip_screen_ ? (visible_height - 1U - y) : y;
                pixels_[static_cast<std::size_t>(out_y) * visible_width + out_x] = rgb;
            };

            auto render_layer = [&](std::span<const std::uint8_t> map,
                                    std::span<const std::uint16_t, 4> skip_masks,
                                    std::uint16_t scroll_bias) {
                if (map.size() < tilemap_bytes || tile_count == 0U) {
                    return;
                }
                const std::uint16_t row_scroll =
                    rowscroll.empty()
                        ? 0U
                        : static_cast<std::uint16_t>(
                              rowscroll[(static_cast<std::size_t>(line) * 2U) % rowscroll.size()] |
                              (rowscroll[(static_cast<std::size_t>(line) * 2U + 1U) %
                                         rowscroll.size()]
                               << 8U));
                const std::uint32_t map_y = (line + scroll_y_) & (map_pixels - 1U);
                for (std::uint32_t x = 0; x < visible_width; ++x) {
                    const std::uint32_t map_x =
                        (x + beam_x_origin + scroll_x_ + row_scroll + scroll_bias) &
                        (map_pixels - 1U);
                    const std::size_t entry =
                        (static_cast<std::size_t>(map_y / 8U) * 64U + map_x / 8U) * 4U;
                    const std::uint16_t word0 = read_le16(map, entry);
                    const std::uint16_t word1 = read_le16(map, entry + 2U);
                    const std::size_t code = (word0 & 0x3FFFU) % tile_count;
                    const std::uint32_t group = (word1 >> 6U) & 0x03U;
                    const std::uint32_t tx =
                        (word0 & 0x4000U) != 0U ? 7U - (map_x & 7U) : (map_x & 7U);
                    const std::uint32_t ty =
                        (word0 & 0x8000U) != 0U ? 7U - (map_y & 7U) : (map_y & 7U);
                    const std::size_t row = code * tile_cell_bytes + ty;
                    const std::uint32_t bit = 7U - tx;
                    std::uint32_t pixel = 0U;
                    for (std::uint32_t plane = 0; plane < 4U; ++plane) {
                        pixel |= ((tiles[plane * tile_plane_size + row] >> bit) & 1U) << plane;
                    }
                    if (((skip_masks[group] >> pixel) & 1U) != 0U) {
                        continue;
                    }
                    const std::size_t color = ((word1 & 0x0FU) * 16U) + pixel;
                    plot(x, line, palette_rgb(palette, color));
                }
            };

            auto render_sprites = [&]() {
                if (sprite_cell_count == 0U) {
                    return;
                }
                const auto word = [this](std::size_t word_index) -> std::uint16_t {
                    const std::size_t offset = word_index * 2U;
                    return static_cast<std::uint16_t>(sprite_buffer_[offset] |
                                                      (sprite_buffer_[offset + 1U] << 8U));
                };

                std::array<std::size_t, sprite_ram_size / sprite_entry_bytes> entries{};
                std::size_t entry_count = 0U;
                const std::size_t total_words = sprite_buffer_.size() / 2U;
                for (std::size_t offs = 0U;
                     offs + 3U < total_words && entry_count < entries.size();) {
                    entries[entry_count++] = offs;
                    const std::uint32_t blocks_w = 1U << ((word(offs + 2U) >> 14U) & 3U);
                    offs += static_cast<std::size_t>(blocks_w) * 4U;
                }

                for (std::size_t i = entry_count; i-- > 0U;) {
                    const std::size_t offs = entries[i];
                    const std::uint16_t w0 = word(offs);
                    const std::size_t code = word(offs + 1U);
                    const std::uint16_t w2 = word(offs + 2U);
                    const std::uint16_t w3 = word(offs + 3U);
                    const std::uint32_t color = w2 & 0x0FU;
                    const bool flip_y = (w2 & 0x0400U) != 0U;
                    const bool flip_x = (w2 & 0x0800U) != 0U;
                    const std::int32_t blocks_h = 1 << ((w2 >> 12U) & 3U);
                    const std::int32_t blocks_w = 1 << ((w2 >> 14U) & 3U);
                    const std::int32_t sx = static_cast<std::int32_t>(w3 & 0x3FFU) - 256 -
                                            static_cast<std::int32_t>(beam_x_origin);
                    const std::int32_t sy =
                        384 - static_cast<std::int32_t>(w0 & 0x1FFU) - 16 * blocks_h;

                    for (std::int32_t col = 0; col < blocks_w; ++col) {
                        for (std::int32_t row = 0; row < blocks_h; ++row) {
                            const std::size_t cell =
                                (code +
                                 static_cast<std::size_t>(flip_x ? 8 * (blocks_w - 1 - col)
                                                                 : 8 * col) +
                                 static_cast<std::size_t>(flip_y ? blocks_h - 1 - row : row)) %
                                sprite_cell_count;
                            const std::int32_t base_x = sx + 16 * col;
                            const std::int32_t base_y = sy + 16 * row;
                            for (std::int32_t py = 0; py < 16; ++py) {
                                const std::int32_t dest_y = base_y + py;
                                if (dest_y != static_cast<std::int32_t>(line)) {
                                    continue;
                                }
                                const std::uint32_t ty =
                                    static_cast<std::uint32_t>(flip_y ? 15 - py : py);
                                for (std::int32_t px = 0; px < 16; ++px) {
                                    const std::int32_t dest_x = base_x + px;
                                    if (dest_x < 0 ||
                                        dest_x >= static_cast<std::int32_t>(visible_width)) {
                                        continue;
                                    }
                                    const std::uint32_t tx =
                                        static_cast<std::uint32_t>(flip_x ? 15 - px : px);
                                    const std::size_t byte_index =
                                        cell * sprite_cell_bytes + (tx >= 8U ? 16U : 0U) + ty;
                                    const std::uint32_t bit = 7U - (tx & 7U);
                                    std::uint32_t pixel = 0U;
                                    for (std::uint32_t plane = 0; plane < 4U; ++plane) {
                                        pixel |=
                                            ((sprites[plane * sprite_plane_size + byte_index] >>
                                              bit) &
                                             1U)
                                            << plane;
                                    }
                                    if (pixel == 0U) {
                                        continue;
                                    }
                                    const std::uint32_t rgb =
                                        palette_rgb(palette, color * 16U + pixel);
                                    plot(static_cast<std::uint32_t>(dest_x),
                                         static_cast<std::uint32_t>(dest_y), rgb);
                                }
                            }
                        }
                    }
                }
            };

            if (tile_path_ready) {
                const auto back = vram.size() >= tilemap_bytes * 2U
                                      ? vram.subspan(tilemap_bytes, tilemap_bytes)
                                      : vram.first(tilemap_bytes);
                const auto front = vram.first(tilemap_bytes);
                render_layer(back, bg_below, 0U);
                if (vram.size() >= tilemap_bytes * 2U) {
                    render_layer(front, fg_below, 0U);
                }
                if (sprite_path_ready) {
                    render_sprites();
                }
                render_layer(back, bg_above, 0U);
                if (vram.size() >= tilemap_bytes * 2U) {
                    render_layer(front, fg_above, 0U);
                }
            } else if (sprite_path_ready) {
                render_sprites();
            }

            return;
        }

        const std::uint8_t layout_tint = bootleg_layout ? 0x53U : 0x21U;
        const std::uint16_t row_scroll =
            rowscroll.empty()
                ? 0U
                : static_cast<std::uint16_t>(
                      rowscroll[(static_cast<std::size_t>(line) * 2U) % rowscroll.size()] |
                      (rowscroll[(static_cast<std::size_t>(line) * 2U + 1U) % rowscroll.size()]
                       << 8U));
        for (std::uint32_t x = 0; x < visible_width; ++x) {
            const std::uint32_t sx = (x + scroll_x_ + row_scroll) & 0x1FFU;
            const std::uint32_t sy = (line + scroll_y_) & 0x1FFU;
            const std::uint64_t tile_index =
                (static_cast<std::uint64_t>(sy >> 3U) * 64U) + (sx >> 3U);
            const std::uint8_t tile = sample_byte(tiles, tile_index * 16U + (sy & 7U), 0x5AU);
            const std::uint8_t sprite = sample_byte(
                sprites,
                ((static_cast<std::uint64_t>(line) * visible_width + x) >> 4U) + frame_index_,
                0x00U);
            const std::uint8_t prom =
                sample_byte(proms, (tile ^ sprite) + (frame_index_ & 0xFFU), layout_tint);
            const std::uint8_t pal =
                sample_byte(palette, (static_cast<std::uint64_t>(prom) * 3U) % 0x400U, 0x40U);
            const std::uint8_t mixed = static_cast<std::uint8_t>(
                tile ^ sprite ^ prom ^ pal ^ static_cast<std::uint8_t>(frame_index_));
            const std::uint32_t out_x = flip_screen_ ? (visible_width - 1U - x) : x;
            pixels_[static_cast<std::size_t>(out_line) * visible_width + out_x] =
                rgb_from_byte(mixed, layout_tint);
        }
    }

    void m82_video::end_frame() noexcept { ++frame_index_; }

    void m82_video::compose(std::span<const std::uint8_t> tiles,
                            std::span<const std::uint8_t> sprites,
                            std::span<const std::uint8_t> proms, std::span<const std::uint8_t> vram,
                            std::span<const std::uint8_t> rowscroll,
                            std::span<const std::uint8_t> palette, bool bootleg_layout) {
        begin_frame();
        for (std::uint32_t line = 0; line < visible_height; ++line) {
            compose_scanline(tiles, sprites, proms, vram, rowscroll, palette, line, bootleg_layout);
        }
        end_frame();
    }

    void m82_video::save_state(chips::state_writer& writer) const {
        writer.u64(elapsed_cycles_);
        writer.u64(frame_index_);
        writer.u16(scroll_x_);
        writer.u16(scroll_y_);
        writer.u32(static_cast<std::uint32_t>(raster_compare_));
        writer.boolean(display_enabled_);
        writer.boolean(flip_screen_);
        writer.bytes(sprite_buffer_);
        for (const std::uint32_t pixel : pixels_) {
            writer.u32(pixel);
        }
    }

    void m82_video::load_state(chips::state_reader& reader) {
        elapsed_cycles_ = reader.u64();
        frame_index_ = reader.u64();
        scroll_x_ = reader.u16();
        scroll_y_ = reader.u16();
        raster_compare_ = static_cast<std::int32_t>(reader.u32());
        display_enabled_ = reader.boolean();
        flip_screen_ = reader.boolean();
        reader.bytes(sprite_buffer_);
        for (std::uint32_t& pixel : pixels_) {
            pixel = reader.u32();
        }
    }

    m82_system::m82_system(common::rom_set_image image, m82_board_params board_params)
        : roms(std::move(image)), params(board_params) {
        dip_switches = params.dip_default;

        auto& main_prog = pinned_region(roms, "maincpu", main_rom_size);
        auto& sound_prog = pinned_region(roms, "soundcpu", sound_rom_size);
        (void)pinned_region(roms, "samples", sample_rom_size);
        (void)pinned_region(roms, "tiles", 0U);
        (void)pinned_region(roms, "sprites", 0U);
        (void)pinned_region(roms, "proms", 0U);

        main_bus.map_rom(0x00000U, std::span<const std::uint8_t>(main_prog));
        main_bus.map_ram(work_ram_base, work_ram, 1);
        main_bus.map_ram(sprite_ram_base, sprite_ram, 1);
        main_bus.map_ram(palette_ram_base, palette_ram, 1);
        main_bus.map_ram(vram_base, vram, 1);
        main_bus.map_ram(rowscroll_ram_base, rowscroll_ram, 1);
        main_cpu.attach_bus(main_bus);

        sound_bus.map_rom(sound_rom_base,
                          std::span<const std::uint8_t>(sound_prog).first(sound_rom_mapped_size),
                          0);
        sound_bus.map_ram(sound_work_ram_base, sound_ram, 1);
        sound_cpu.attach_bus(sound_bus);

        main_cpu.set_port_in([this](std::uint16_t port) -> std::uint8_t {
            switch (port & 0xFFU) {
            case port_in_p1:
                return input_p1;
            case port_in_p2:
                return input_p2;
            case port_in_system:
                return static_cast<std::uint8_t>(input_system | 0x80U);
            case port_in_system + 1U:
                return 0xFFU;
            case port_in_dsw_lo:
                return static_cast<std::uint8_t>(dip_switches);
            case port_in_dsw_hi:
                return static_cast<std::uint8_t>(dip_switches >> 8U);
            case port_pic_a0:
                return pic.read(0U);
            case port_pic_a1:
                return pic.read(1U);
            default:
                return 0xFFU;
            }
        });
        main_cpu.set_port_out([this](std::uint16_t port, std::uint8_t value) {
            const std::uint16_t p = port & 0xFFU;
            if (p >= port_out_scroll_base && p < port_out_scroll_base + scroll_regs.size()) {
                scroll_regs[p - port_out_scroll_base] = value;
                const auto word = [this](std::size_t i) {
                    return static_cast<std::uint16_t>(scroll_regs[i] | (scroll_regs[i + 1U] << 8U));
                };
                video.set_scroll(word(2U), word(0U));
                return;
            }
            switch (p) {
            case port_out_sound_latch:
                sound_latch = value;
                sound_latch_irq = true;
                update_sound_irq();
                break;
            case port_out_control:
                for (std::size_t i = 0; i < coin_counters.size(); ++i) {
                    const auto mask = static_cast<std::uint8_t>(1U << i);
                    if ((control_register & mask) == 0U && (value & mask) != 0U) {
                        ++coin_counters[i];
                    }
                }
                control_register = value;
                video.set_flip_screen((value & 0x04U) != 0U);
                video.set_display_enable((value & 0x08U) == 0U);
                sound_cpu.set_reset_line((value & 0x10U) == 0U);
                break;
            case port_out_sprite_dma:
                video.latch_sprites(sprite_ram);
                break;
            case port_out_raster_lo:
            case port_out_raster_hi: {
                raster_regs[p - port_out_raster_lo] = value;
                const int line =
                    static_cast<int>((raster_regs[0] | (raster_regs[1] << 8U)) & 0x1FFU) - 128;
                video.set_raster_compare(line);
                pic.set_irq_line(2U, false);
                break;
            }
            case port_pic_a0:
                pic.write(0U, value);
                break;
            case port_pic_a1:
                pic.write(1U, value);
                break;
            default:
                break;
            }
        });

        sound_cpu.set_port_in([this](std::uint16_t port) -> std::uint8_t {
            switch (port & 0xFFU) {
            case z80_port_ym2151_addr:
            case z80_port_ym2151_data:
                return fm.read_status();
            case z80_port_latch:
                return sound_latch;
            case z80_port_sample_read: {
                const auto* samples = roms.region("samples");
                if (samples == nullptr || samples->empty()) {
                    return 0xFFU;
                }
                const std::uint8_t byte = (*samples)[sample_address % samples->size()];
                ++sample_address;
                return byte;
            }
            default:
                return 0xFFU;
            }
        });
        sound_cpu.set_port_out([this](std::uint16_t port, std::uint8_t value) {
            switch (port & 0xFFU) {
            case z80_port_ym2151_addr:
                fm.write_address(value);
                break;
            case z80_port_ym2151_data:
                fm.write_data(value);
                break;
            case z80_port_latch_ack:
                sound_latch_irq = false;
                update_sound_irq();
                break;
            case z80_port_sample_addr_lo:
                sample_address = (sample_address & 0xFF00U) | value;
                break;
            case z80_port_sample_addr_hi:
                sample_address =
                    (sample_address & 0x00FFU) | (static_cast<std::uint32_t>(value) << 8U);
                break;
            case z80_port_dac:
                record_dac_write(value);
                break;
            default:
                break;
            }
        });
        sound_cpu.set_irq_vector([this]() -> std::uint8_t {
            std::uint8_t vector = z80_rst_idle;
            if (sound_latch_irq) {
                vector &= z80_rst_latch;
            }
            if (fm.irq_asserted()) {
                vector &= z80_rst_ym;
            }
            return vector;
        });
        fm.set_irq([this](bool) { update_sound_irq(); });

        pic.set_int_callback([this](bool asserted) { main_cpu.set_irq_line(asserted); });
        main_cpu.set_irq_ack([this]() -> std::uint8_t { return pic.acknowledge(); });

        main_cpu.reset(chips::reset_kind::power_on);
        sound_cpu.reset(chips::reset_kind::power_on);
        sound_cpu.set_reset_line(false);
    }

    void m82_system::set_inputs(std::uint8_t p1, std::uint8_t p2, std::uint8_t system) noexcept {
        input_p1 = p1;
        input_p2 = p2;
        input_system = system;
    }

    void m82_system::update_sound_irq() noexcept {
        sound_cpu.set_irq_line(sound_latch_irq || fm.irq_asserted());
    }

    void m82_system::record_dac_write(std::uint8_t value) {
        dac.write(value);
        const dac_write_event event{.sound_clock = fm.elapsed_clocks(), .output = dac.output()};
        if (!dac_write_events.empty() && dac_write_events.back().sound_clock == event.sound_clock) {
            dac_write_events.back() = event;
            return;
        }
        dac_write_events.push_back(event);
    }

    void m82_system::discard_dac_write_events_before(std::uint64_t sound_clock) {
        std::size_t first_live = 0U;
        while (first_live < dac_write_events.size() &&
               dac_write_events[first_live].sound_clock < sound_clock) {
            ++first_live;
        }
        if (first_live == 0U) {
            return;
        }
        dac_write_events.erase(dac_write_events.begin(),
                               dac_write_events.begin() + static_cast<std::ptrdiff_t>(first_live));
    }

    void m82_system::run_frame() {
        std::uint64_t main_cycles_elapsed = 0U;
        video.begin_frame();
        for (std::uint32_t line = 0U; line < frame_lines; ++line) {
            const std::uint64_t next_cycle =
                (main_cycles_per_frame * static_cast<std::uint64_t>(line + 1U)) / frame_lines;
            if (line < visible_height) {
                video.compose_scanline(roms.regions["tiles"], roms.regions["sprites"],
                                       roms.regions["proms"], vram, rowscroll_ram, palette_ram,
                                       line, params.bootleg_layout);
            }
            pic.set_irq_line(0U, line == visible_height);
            pic.set_irq_line(2U, video.raster_compare_matches(line));
            main_cpu.tick(next_cycle - main_cycles_elapsed);
            main_cycles_elapsed = next_cycle;
        }
        pic.set_irq_line(0U, false);
        pic.set_irq_line(2U, false);
        if (!sound_cpu.reset_line_held()) {
            sound_cpu.tick(sound_cycles_per_frame);
        }
        fm.tick(sound_cycles_per_frame);
        dac.tick(sound_cycles_per_frame);
        video.end_frame();
    }

    void m82_system::save_state(chips::state_writer& writer) const {
        writer.u32(m82_system_state_version);
        writer.u16(params.dip_default);
        writer.boolean(params.bootleg_layout);
        writer.u32(rom_identity_crc(roms, params));

        main_cpu.save_state(writer);
        sound_cpu.save_state(writer);
        video.save_state(writer);
        fm.save_state(writer);
        dac.save_state(writer);
        pic.save_state(writer);

        writer.bytes(work_ram);
        writer.bytes(sprite_ram);
        writer.bytes(palette_ram);
        writer.bytes(vram);
        writer.bytes(rowscroll_ram);
        writer.bytes(sound_ram);

        writer.u8(sound_latch);
        writer.u8(input_p1);
        writer.u8(input_p2);
        writer.u8(input_system);
        writer.u16(dip_switches);
        writer.u8(control_register);
        writer.u32(coin_counters[0]);
        writer.u32(coin_counters[1]);
        writer.bytes(scroll_regs);
        writer.bytes(raster_regs);
        writer.boolean(sound_latch_irq);
        writer.u32(sample_address);
        writer.u32(static_cast<std::uint32_t>(dac_write_events.size()));
        for (const auto& event : dac_write_events) {
            writer.u64(event.sound_clock);
            writer.u16(static_cast<std::uint16_t>(static_cast<std::int32_t>(event.output) + 32768));
        }
    }

    void m82_system::load_state(chips::state_reader& reader) {
        if (reader.u32() != m82_system_state_version) {
            reader.fail();
            return;
        }
        const std::uint16_t saved_dip = reader.u16();
        const bool saved_bootleg = reader.boolean();
        const std::uint32_t saved_rom_identity = reader.u32();
        if (saved_dip != params.dip_default || saved_bootleg != params.bootleg_layout ||
            saved_rom_identity != rom_identity_crc(roms, params)) {
            reader.fail();
            return;
        }

        main_cpu.load_state(reader);
        sound_cpu.load_state(reader);
        video.load_state(reader);
        fm.load_state(reader);
        dac.load_state(reader);
        pic.load_state(reader);

        reader.bytes(work_ram);
        reader.bytes(sprite_ram);
        reader.bytes(palette_ram);
        reader.bytes(vram);
        reader.bytes(rowscroll_ram);
        reader.bytes(sound_ram);

        sound_latch = reader.u8();
        input_p1 = reader.u8();
        input_p2 = reader.u8();
        input_system = reader.u8();
        dip_switches = reader.u16();
        control_register = reader.u8();
        coin_counters[0] = reader.u32();
        coin_counters[1] = reader.u32();
        reader.bytes(scroll_regs);
        reader.bytes(raster_regs);
        sound_latch_irq = reader.boolean();
        sample_address = reader.u32();
        const std::uint32_t dac_event_count = reader.u32();
        if (dac_event_count > max_saved_dac_write_events) {
            reader.fail();
            return;
        }
        dac_write_events.clear();
        dac_write_events.reserve(dac_event_count);
        std::uint64_t previous_clock = 0U;
        for (std::uint32_t i = 0; i < dac_event_count; ++i) {
            dac_write_event event{};
            event.sound_clock = reader.u64();
            event.output =
                static_cast<std::int16_t>(static_cast<std::int32_t>(reader.u16()) - 32768);
            if (i != 0U && event.sound_clock < previous_clock) {
                reader.fail();
                return;
            }
            previous_clock = event.sound_clock;
            dac_write_events.push_back(event);
        }
        if (reader.ok()) {
            video.set_flip_screen((control_register & 0x04U) != 0U);
            video.set_display_enable((control_register & 0x08U) == 0U);
            sound_cpu.set_reset_line((control_register & 0x10U) == 0U);
            update_sound_irq();
        }
    }

    std::unique_ptr<m82_system> assemble_m82(common::rom_set_image image,
                                             m82_board_params board_params) {
        return std::make_unique<m82_system>(std::move(image), board_params);
    }

} // namespace mnemos::manifests::irem_m82
