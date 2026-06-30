#include "m78_system.hpp"

#include "crc32.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace mnemos::manifests::irem_m78 {

    namespace {
        inline constexpr std::uint32_t max_saved_dac_write_events = 1U << 20U;

        [[nodiscard]] std::vector<std::uint8_t>& pinned_region(common::rom_set_image& image,
                                                               std::string_view name,
                                                               std::size_t size,
                                                               std::uint8_t fill = 0xFFU) {
            auto& bytes = image.regions[std::string{name}];
            if (bytes.size() < size) {
                bytes.resize(size, fill);
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

        [[nodiscard]] std::uint8_t expand_2bit(std::uint8_t value) noexcept {
            return static_cast<std::uint8_t>((value & 0x03U) * 0x55U);
        }

        [[nodiscard]] std::uint32_t prom_color(std::span<const std::uint8_t> proms,
                                               std::uint16_t index,
                                               std::uint8_t fallback) noexcept {
            const std::uint8_t lo = sample_byte(proms, index & 0xFFU, fallback);
            const std::uint8_t hi = sample_byte(proms, 0x100U | (index & 0xFFU),
                                                static_cast<std::uint8_t>(fallback ^ 0x5AU));
            const std::uint8_t packed =
                static_cast<std::uint8_t>(((hi & 0x0FU) << 4U) | (lo & 0x0FU));
            const std::uint8_t intensity = static_cast<std::uint8_t>((packed & 0x03U) * 12U);
            const auto clamp_u8 = [](unsigned value) noexcept {
                return static_cast<std::uint8_t>(std::min(255U, value));
            };
            const std::uint8_t g =
                clamp_u8(static_cast<unsigned>(expand_2bit(packed >> 2U)) + intensity);
            const std::uint8_t r =
                clamp_u8(static_cast<unsigned>(expand_2bit(packed >> 4U)) + intensity);
            const std::uint8_t b =
                clamp_u8(static_cast<unsigned>(expand_2bit(packed >> 6U)) + intensity);
            return rgb(r, g, b);
        }

        [[nodiscard]] std::uint8_t tile_pen(std::span<const std::uint8_t> graphics,
                                            std::uint16_t code, std::uint32_t x, std::uint32_t y,
                                            bool flip_x, bool flip_y) noexcept {
            if (graphics.empty()) {
                return static_cast<std::uint8_t>(((code >> 1U) ^ x ^ y) & 0x07U);
            }
            if (flip_x) {
                x = 7U - x;
            }
            if (flip_y) {
                y = 7U - y;
            }
            const std::size_t plane_size = graphics.size() / 3U;
            if (plane_size == 0U) {
                return 0U;
            }
            const std::size_t tiles_per_plane = std::max<std::size_t>(1U, plane_size / 8U);
            const std::size_t tile = static_cast<std::size_t>(code) % tiles_per_plane;
            std::uint8_t pen = 0U;
            for (std::size_t plane = 0; plane < 3U; ++plane) {
                const std::size_t plane_base = plane_size * (2U - plane);
                const std::size_t offset = plane_base + tile * 8U + y;
                if ((sample_byte(graphics, offset, 0U) & (0x80U >> x)) != 0U) {
                    pen |= static_cast<std::uint8_t>(1U << plane);
                }
            }
            return pen;
        }

        void draw_tile_layer(std::vector<std::uint32_t>& pixels,
                             std::span<const std::uint8_t> graphics,
                             std::span<const std::uint8_t> proms,
                             std::span<const std::uint8_t> tile_ram,
                             std::span<const std::uint8_t> attr_ram,
                             std::span<const std::uint8_t> color_ram, bool transparent_zero,
                             std::uint8_t layer_tint) {
            for (std::uint32_t tile_y = 0; tile_y < 48U; ++tile_y) {
                for (std::uint32_t tile_x = 0; tile_x < 64U; ++tile_x) {
                    const std::size_t index = static_cast<std::size_t>(tile_x) * 64U + tile_y;
                    const std::uint8_t tile = sample_byte(tile_ram, index, 0U);
                    const std::uint8_t attr = sample_byte(attr_ram, index, 0U);
                    const std::uint8_t color = sample_byte(color_ram, index, 0U);
                    const std::uint16_t code =
                        static_cast<std::uint16_t>(tile | ((attr & 0x1FU) << 8U));
                    const bool flip_x = (attr & 0x10U) != 0U;
                    const bool flip_y = (attr & 0x20U) != 0U;
                    const std::uint32_t base_x = tile_x * 8U;
                    const std::uint32_t base_y = tile_y * 8U;
                    for (std::uint32_t py = 0; py < 8U; ++py) {
                        for (std::uint32_t px = 0; px < 8U; ++px) {
                            const std::uint8_t pen =
                                tile_pen(graphics, code, px, py, flip_x, flip_y);
                            if (transparent_zero && pen == 0U) {
                                continue;
                            }
                            const std::uint16_t color_index =
                                static_cast<std::uint16_t>(((color & 0x0FU) << 4U) | pen);
                            pixels[static_cast<std::size_t>(base_y + py) * visible_width + base_x +
                                   px] = prom_color(proms, color_index,
                                                    static_cast<std::uint8_t>(color ^ layer_tint));
                        }
                    }
                }
            }
        }

        [[nodiscard]] std::uint32_t crc32_u64(std::uint32_t crc, std::uint64_t value) noexcept {
            std::array<std::uint8_t, 8> bytes{};
            for (std::size_t i = 0; i < bytes.size(); ++i) {
                bytes[i] = static_cast<std::uint8_t>((value >> (i * 8U)) & 0xFFU);
            }
            return security::cryptography::crc32(
                std::span<const std::uint8_t>(bytes.data(), bytes.size()), crc);
        }

        [[nodiscard]] std::uint32_t board_identity_crc(const common::rom_set_image& roms) {
            std::uint32_t crc =
                security::cryptography::crc32(std::string_view{"m78.board.identity.v1"});
            crc = crc32_u64(crc, main_clock_hz);
            crc = crc32_u64(crc, sound_clock_hz);
            crc = crc32_u64(crc, visible_width);
            crc = crc32_u64(crc, visible_height);
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

    m78_video::m78_video() : pixels_(static_cast<std::size_t>(visible_width) * visible_height, 0U) {
        reset(chips::reset_kind::power_on);
    }

    chips::chip_metadata m78_video::metadata() const noexcept {
        return {.manufacturer = "Irem",
                .part_number = "m78_video_first_pass",
                .family = "irem_m78",
                .klass = chips::chip_class::video,
                .revision = 1U};
    }

    void m78_video::tick(std::uint64_t cycles) { elapsed_cycles_ += cycles; }

    void m78_video::reset(chips::reset_kind /*kind*/) {
        std::fill(pixels_.begin(), pixels_.end(), 0U);
        elapsed_cycles_ = 0U;
        frame_index_ = 0U;
    }

    chips::frame_buffer_view m78_video::framebuffer() const noexcept {
        return {.pixels = pixels_.data(),
                .width = visible_width,
                .height = visible_height,
                .stride = visible_width};
    }

    void m78_video::compose(
        std::span<const std::uint8_t> tiles0, std::span<const std::uint8_t> tiles1,
        std::span<const std::uint8_t> proms, std::span<const std::uint8_t> layer0_tile,
        std::span<const std::uint8_t> layer0_attr, std::span<const std::uint8_t> layer0_color,
        std::span<const std::uint8_t> layer1_tile, std::span<const std::uint8_t> layer1_attr,
        std::span<const std::uint8_t> layer1_color, std::span<const std::uint8_t> vregs,
        std::span<const std::uint8_t> layer_control) {
        const std::uint8_t scroll_mix = static_cast<std::uint8_t>(
            sample_byte(vregs, 0U, 0U) ^ sample_byte(layer_control, 0U, 0U) ^ frame_index_);
        for (std::uint32_t y = 0; y < visible_height; ++y) {
            for (std::uint32_t x = 0; x < visible_width; ++x) {
                const std::uint64_t linear =
                    static_cast<std::uint64_t>(y) * visible_width + x + scroll_mix;
                const std::uint8_t gfx0 =
                    sample_byte(tiles0, linear >> 3U, static_cast<std::uint8_t>(x ^ y));
                const std::uint8_t gfx1 = sample_byte(tiles1, (linear >> 2U) + x,
                                                      static_cast<std::uint8_t>((x + y) & 0xFFU));
                const std::uint8_t color =
                    static_cast<std::uint8_t>((gfx0 ^ gfx1 ^ (x >> 3U) ^ (y >> 3U)) & 0xFFU);
                pixels_[static_cast<std::size_t>(y) * visible_width + x] =
                    prom_color(proms, color, static_cast<std::uint8_t>(color ^ 0x78U));
            }
        }

        draw_tile_layer(pixels_, tiles1, proms, layer1_tile, layer1_attr, layer1_color, false,
                        0x31U);
        draw_tile_layer(pixels_, tiles0, proms, layer0_tile, layer0_attr, layer0_color, true,
                        0x67U);
        ++frame_index_;
    }

    void m78_video::save_state(chips::state_writer& writer) const {
        writer.u64(elapsed_cycles_);
        writer.u64(frame_index_);
        for (const std::uint32_t pixel : pixels_) {
            writer.u32(pixel);
        }
    }

    void m78_video::load_state(chips::state_reader& reader) {
        elapsed_cycles_ = reader.u64();
        frame_index_ = reader.u64();
        for (std::uint32_t& pixel : pixels_) {
            pixel = reader.u32();
        }
    }

    m78_system::m78_system(common::rom_set_image image) : roms(std::move(image)) {
        auto& main_prog = pinned_region(roms, "maincpu", main_rom_size);
        auto& sound_prog = pinned_region(roms, "audiocpu", audio_rom_size);
        (void)pinned_region(roms, "tiles", tiles_rom_size);
        (void)pinned_region(roms, "tiles2", tiles2_rom_size);
        (void)pinned_region(roms, "m72_audio", m72_audio_rom_size, 0x00U);
        (void)pinned_region(roms, "proms", proms_size);

        main_bus.map_rom(0x0000U,
                         std::span<const std::uint8_t>(main_prog).first(main_rom_mapped_size));
        main_bus.map_ram(work_ram_base, work_ram, 1);
        main_cpu.attach_bus(main_bus);
        main_cpu.set_port_in(
            [this](std::uint16_t port) -> std::uint8_t { return main_port_read(port); });
        main_cpu.set_port_out(
            [this](std::uint16_t port, std::uint8_t value) { main_port_write(port, value); });

        sound_bus.map_rom(sound_rom_base,
                          std::span<const std::uint8_t>(sound_prog).first(sound_rom_mapped_size),
                          0);
        sound_bus.map_ram(sound_work_ram_base, sound_ram, 1);
        sound_cpu.attach_bus(sound_bus);
        sound_cpu.set_port_in(
            [this](std::uint16_t port) -> std::uint8_t { return sound_port_read(port); });
        sound_cpu.set_port_out(
            [this](std::uint16_t port, std::uint8_t value) { sound_port_write(port, value); });
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

        main_cpu.reset(chips::reset_kind::power_on);
        sound_cpu.reset(chips::reset_kind::power_on);
        sound_cpu.set_reset_line(false);
    }

    std::uint8_t m78_system::main_port_read(std::uint16_t port) const noexcept {
        const std::uint16_t address = static_cast<std::uint16_t>(port & 0xF000U);
        const std::size_t offset = static_cast<std::size_t>(port & 0x0FFFU);
        switch (address) {
        case port_dsw1:
            return dsw1;
        case port_in1:
            return input1;
        case port_in0:
            return input0;
        case port_layer1_tile:
            return tile_ram[1][offset];
        case port_layer1_attr:
            return attr_ram[1][offset];
        case port_layer1_color:
            return color_ram[1][offset];
        case port_vregs:
            return vregs[offset];
        case port_layer0_tile:
            return tile_ram[0][offset];
        case port_layer0_attr:
            return attr_ram[0][offset];
        case port_layer0_color:
            return color_ram[0][offset];
        case port_layer_control:
            return layer_control[offset];
        default:
            return 0xFFU;
        }
    }

    void m78_system::main_port_write(std::uint16_t port, std::uint8_t value) noexcept {
        const std::uint16_t address = static_cast<std::uint16_t>(port & 0xF000U);
        const std::size_t offset = static_cast<std::size_t>(port & 0x0FFFU);
        switch (address) {
        case port_dsw1:
            latch_sound_command(value);
            break;
        case port_layer1_tile:
            tile_ram[1][offset] = value;
            break;
        case port_layer1_attr:
            attr_ram[1][offset] = value;
            break;
        case port_layer1_color:
            color_ram[1][offset] = value;
            break;
        case port_vregs:
            vregs[offset] = value;
            ++vreg_write_count;
            break;
        case port_layer0_tile:
            tile_ram[0][offset] = value;
            break;
        case port_layer0_attr:
            attr_ram[0][offset] = value;
            break;
        case port_layer0_color:
            color_ram[0][offset] = value;
            break;
        case port_layer_control:
            layer_control[offset] = value;
            ++layer_control_write_count;
            break;
        default:
            break;
        }
    }

    std::uint8_t m78_system::sound_port_read(std::uint16_t port) noexcept {
        switch (port & 0xFFU) {
        case z80_port_ym2151_addr:
        case z80_port_ym2151_data:
            return fm.read_status();
        case z80_port_latch:
            return sound_latch;
        case z80_port_sample_read: {
            const auto* samples = roms.region("m72_audio");
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
    }

    void m78_system::sound_port_write(std::uint16_t port, std::uint8_t value) {
        switch (port & 0xFFU) {
        case z80_port_ym2151_addr:
            fm.write_address(value);
            break;
        case z80_port_ym2151_data:
            fm.write_data(value);
            ++ym2151_write_count;
            break;
        case z80_port_latch_ack:
            sound_latch_irq = false;
            ++sound_latch_ack_count;
            update_sound_irq();
            break;
        case z80_port_sample_addr_lo:
            sample_address = (sample_address & 0xFF00U) | value;
            break;
        case z80_port_sample_addr_hi:
            sample_address = (sample_address & 0x00FFU) | (static_cast<std::uint32_t>(value) << 8U);
            break;
        case z80_port_dac:
            record_dac_write(value);
            break;
        default:
            break;
        }
    }

    void m78_system::run_frame() {
        main_cpu.tick(main_cycles_per_frame);
        if (!sound_cpu.reset_line_held()) {
            sound_cpu.tick(sound_cycles_per_frame);
        }
        fm.tick(fm_cycles_per_frame);
        dac.tick(dac_cycles_per_frame);
        video.tick(main_cycles_per_frame);

        const auto* tiles0 = roms.region("tiles");
        const auto* tiles1 = roms.region("tiles2");
        const auto* proms = roms.region("proms");
        video.compose(tiles0 != nullptr ? std::span<const std::uint8_t>(*tiles0)
                                        : std::span<const std::uint8_t>{},
                      tiles1 != nullptr ? std::span<const std::uint8_t>(*tiles1)
                                        : std::span<const std::uint8_t>{},
                      proms != nullptr ? std::span<const std::uint8_t>(*proms)
                                       : std::span<const std::uint8_t>{},
                      tile_ram[0], attr_ram[0], color_ram[0], tile_ram[1], attr_ram[1],
                      color_ram[1], vregs, layer_control);
    }

    void m78_system::set_inputs(std::uint8_t in0, std::uint8_t in1) noexcept {
        input0 = in0;
        input1 = in1;
    }

    void m78_system::latch_sound_command(std::uint8_t value) noexcept {
        sound_latch = value;
        sound_latch_irq = true;
        ++sound_command_write_count;
        update_sound_irq();
    }

    void m78_system::update_sound_irq() noexcept {
        sound_cpu.set_irq_line(sound_latch_irq || fm.irq_asserted());
    }

    void m78_system::record_dac_write(std::uint8_t value) {
        dac.write(value);
        const dac_write_event event{.sound_clock = sound_cpu.elapsed_cycles(),
                                    .output = dac.output()};
        if (!dac_write_events.empty() && dac_write_events.back().sound_clock == event.sound_clock) {
            dac_write_events.back() = event;
            return;
        }
        dac_write_events.push_back(event);
        ++dac_write_count;
    }

    void m78_system::discard_dac_write_events_before(std::uint64_t sound_clock) {
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

    void m78_system::save_state(chips::state_writer& writer) const {
        writer.u32(m78_system_state_version);
        writer.u32(board_identity_crc(roms));
        main_cpu.save_state(writer);
        sound_cpu.save_state(writer);
        video.save_state(writer);
        fm.save_state(writer);
        dac.save_state(writer);
        writer.bytes(work_ram);
        writer.bytes(sound_ram);
        for (std::size_t layer = 0; layer < 2U; ++layer) {
            writer.bytes(tile_ram[layer]);
            writer.bytes(attr_ram[layer]);
            writer.bytes(color_ram[layer]);
        }
        writer.bytes(vregs);
        writer.bytes(layer_control);
        writer.u8(input0);
        writer.u8(input1);
        writer.u8(dsw1);
        writer.u8(sound_latch);
        writer.boolean(sound_latch_irq);
        writer.u32(sample_address);
        writer.u64(sound_command_write_count);
        writer.u64(sound_latch_ack_count);
        writer.u64(ym2151_write_count);
        writer.u64(dac_write_count);
        writer.u64(vreg_write_count);
        writer.u64(layer_control_write_count);
        writer.u32(static_cast<std::uint32_t>(dac_write_events.size()));
        for (const auto& event : dac_write_events) {
            writer.u64(event.sound_clock);
            writer.u16(static_cast<std::uint16_t>(static_cast<std::int32_t>(event.output) + 32768));
        }
    }

    void m78_system::load_state(chips::state_reader& reader) {
        if (reader.u32() != m78_system_state_version) {
            reader.fail();
            return;
        }
        if (reader.u32() != board_identity_crc(roms)) {
            reader.fail();
            return;
        }
        main_cpu.load_state(reader);
        sound_cpu.load_state(reader);
        video.load_state(reader);
        fm.load_state(reader);
        dac.load_state(reader);
        reader.bytes(work_ram);
        reader.bytes(sound_ram);
        for (std::size_t layer = 0; layer < 2U; ++layer) {
            reader.bytes(tile_ram[layer]);
            reader.bytes(attr_ram[layer]);
            reader.bytes(color_ram[layer]);
        }
        reader.bytes(vregs);
        reader.bytes(layer_control);
        input0 = reader.u8();
        input1 = reader.u8();
        dsw1 = reader.u8();
        sound_latch = reader.u8();
        sound_latch_irq = reader.boolean();
        sample_address = reader.u32();
        sound_command_write_count = reader.u64();
        sound_latch_ack_count = reader.u64();
        ym2151_write_count = reader.u64();
        dac_write_count = reader.u64();
        vreg_write_count = reader.u64();
        layer_control_write_count = reader.u64();
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
            update_sound_irq();
        }
    }

    std::unique_ptr<m78_system> assemble_m78(common::rom_set_image image) {
        return std::make_unique<m78_system>(std::move(image));
    }

} // namespace mnemos::manifests::irem_m78
