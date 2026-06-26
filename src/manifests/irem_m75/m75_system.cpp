#include "m75_system.hpp"

#include "crc32.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace mnemos::manifests::irem_m75 {

    namespace {
        inline constexpr std::uint32_t max_saved_dac_write_events = 1U << 20U;

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
            const std::uint32_t r = static_cast<std::uint32_t>((value * 3U + tint * 2U) & 0xFFU);
            const std::uint32_t g =
                static_cast<std::uint32_t>(((value << 1U) ^ (tint * 5U)) & 0xFFU);
            const std::uint32_t b =
                static_cast<std::uint32_t>(((value >> 1U) + (tint * 7U)) & 0xFFU);
            return (r << 16U) | (g << 8U) | b;
        }

        [[nodiscard]] constexpr std::uint32_t expand5(std::uint8_t value) noexcept {
            const std::uint32_t v = value & 0x1FU;
            return (v << 3U) | (v >> 2U);
        }

        [[nodiscard]] std::uint32_t palette_rgb(std::span<const std::uint8_t> palette,
                                                std::size_t index,
                                                std::uint8_t fallback) noexcept {
            const std::size_t bank = (index & 0x100U) != 0U ? 0x400U : 0U;
            const std::size_t byte = index & 0xFFU;
            if (palette.size() <= bank + byte + 0x200U) {
                return rgb_from_byte(fallback, 0x43U);
            }
            const std::uint32_t r = expand5(palette[bank + byte]);
            const std::uint32_t g = expand5(palette[bank + byte + 0x100U]);
            const std::uint32_t b = expand5(palette[bank + byte + 0x200U]);
            if ((r | g | b) == 0U) {
                return rgb_from_byte(fallback, 0x43U);
            }
            return (r << 16U) | (g << 8U) | b;
        }

        [[nodiscard]] std::uint8_t
        m75_palette_read(std::span<const std::uint8_t> palette,
                         std::uint32_t address) noexcept {
            const std::size_t offset =
                static_cast<std::size_t>((address - palette_ram_base) & 0x7FFU);
            if (offset >= palette.size()) {
                return 0xFFU;
            }
            return static_cast<std::uint8_t>(palette[offset] | 0xE0U);
        }

        void m75_palette_write(std::span<std::uint8_t> palette,
                               std::uint32_t address,
                               std::uint8_t value) noexcept {
            const std::size_t offset =
                static_cast<std::size_t>((address - palette_ram_base) & 0x7FFU);
            if (offset < palette.size()) {
                palette[offset] = static_cast<std::uint8_t>(value & 0x1FU);
            }
        }

        [[nodiscard]] std::uint8_t layout_code(std::string_view layout) noexcept {
            if (layout == "vigilant") {
                return 1U;
            }
            return 0U;
        }

        [[nodiscard]] std::uint8_t layout_tint(std::string_view layout) noexcept {
            return static_cast<std::uint8_t>(0x4DU + layout_code(layout) * 0x17U);
        }

        [[nodiscard]] std::uint32_t crc32_u64(std::uint32_t crc, std::uint64_t value) noexcept {
            std::array<std::uint8_t, 8> bytes{};
            for (std::size_t i = 0; i < bytes.size(); ++i) {
                bytes[i] = static_cast<std::uint8_t>((value >> (i * 8U)) & 0xFFU);
            }
            return security::cryptography::crc32(
                std::span<const std::uint8_t>(bytes.data(), bytes.size()), crc);
        }

        [[nodiscard]] std::uint32_t crc32_u8(std::uint32_t crc, std::uint8_t value) noexcept {
            std::array<std::uint8_t, 1> bytes{value};
            return security::cryptography::crc32(
                std::span<const std::uint8_t>(bytes.data(), bytes.size()), crc);
        }

        [[nodiscard]] std::uint32_t board_identity_crc(const common::rom_set_image& roms,
                                                       const m75_board_params& params) {
            std::uint32_t crc =
                security::cryptography::crc32(std::string_view{"m75.board.identity.v1"});
            crc = crc32_u8(crc, params.dsw1_default);
            crc = crc32_u8(crc, params.dsw2_default);
            crc = crc32_u8(crc, layout_code(params.rom_layout));
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

    m75_board_params board_params_for(std::string_view set_name) noexcept {
        if (set_name == "vigilant") {
            return {.dsw1_default = vigilant_dsw1_default,
                    .dsw2_default = vigilant_dsw2_default,
                    .rom_layout = "vigilant"};
        }
        return {};
    }

    m75_video::m75_video()
        : pixels_(static_cast<std::size_t>(visible_width) * visible_height, 0U) {
        reset(chips::reset_kind::power_on);
    }

    chips::chip_metadata m75_video::metadata() const noexcept {
        return {.manufacturer = "Irem",
                .part_number = "m75_video_first_pass",
                .family = "irem_m75",
                .klass = chips::chip_class::video,
                .revision = 1U};
    }

    void m75_video::tick(std::uint64_t cycles) { elapsed_cycles_ += cycles; }

    void m75_video::reset(chips::reset_kind /*kind*/) {
        std::fill(pixels_.begin(), pixels_.end(), 0U);
        elapsed_cycles_ = 0U;
        frame_index_ = 0U;
    }

    chips::frame_buffer_view m75_video::framebuffer() const noexcept {
        return {.pixels = pixels_.data(),
                .width = visible_width,
                .height = visible_height,
                .stride = visible_width};
    }

    void m75_video::compose(std::span<const std::uint8_t> main_program,
                            std::span<const std::uint8_t> sound_program,
                            std::span<const std::uint8_t> chars,
                            std::span<const std::uint8_t> sprites,
                            std::span<const std::uint8_t> bgtiles,
                            std::span<const std::uint8_t> samples,
                            std::span<const std::uint8_t> proms,
                            std::span<const std::uint8_t> video_ram,
                            std::span<const std::uint8_t> palette_ram,
                            std::span<const std::uint8_t> sprite_ram, std::uint16_t scroll,
                            std::uint16_t rear_scroll, std::uint8_t rear_color,
                            std::string_view rom_layout) {
        const bool rear_disabled = (rear_color & 0x40U) != 0U;
        const std::uint8_t rear_color_code = rear_color & 0x0DU;
        const std::uint8_t tint =
            static_cast<std::uint8_t>(layout_tint(rom_layout) ^ rear_color_code);
        for (std::uint32_t y = 0; y < visible_height; ++y) {
            for (std::uint32_t x = 0; x < visible_width; ++x) {
                const std::uint64_t linear =
                    static_cast<std::uint64_t>(y) * visible_width + x + frame_index_;
                const std::uint8_t bg = sample_byte(
                    bgtiles, linear + scroll + (static_cast<std::uint64_t>(y) << 3U), 0x21U);
                const std::uint8_t rear = sample_byte(
                    main_program, linear + rear_scroll + (static_cast<std::uint64_t>(x) << 2U),
                    0x63U);
                const std::uint8_t spr =
                    sample_byte(sprites, (linear >> 1U) + sample_byte(sprite_ram, x + y, 0U),
                                0x00U);
                const std::uint8_t chr =
                    sample_byte(chars, ((static_cast<std::uint64_t>(x >> 3U) +
                                         static_cast<std::uint64_t>(y >> 3U) * 32U) *
                                            8U) +
                                           (y & 7U),
                                0x18U);
                const std::uint8_t snd =
                    sample_byte(sound_program, (linear >> 3U) + sample_byte(samples, linear, 0U),
                                0x3CU);
                const std::uint8_t vram =
                    sample_byte(video_ram, (static_cast<std::uint64_t>(y >> 3U) * 32U) + (x >> 3U),
                                0x00U);
                const std::uint8_t prom = sample_byte(proms, bg ^ spr ^ chr, tint);
                const std::uint8_t mixed = static_cast<std::uint8_t>(
                    bg ^ static_cast<std::uint8_t>((rear_disabled ? 0U : rear) << 1U) ^ spr ^
                    static_cast<std::uint8_t>(chr << 2U) ^ snd ^ vram ^ prom ^ tint);
                const std::size_t palette_index =
                    rear_disabled
                        ? mixed
                        : (0x100U |
                           ((static_cast<std::size_t>(rear_color_code) * 16U +
                             static_cast<std::size_t>(mixed & 0x1FU)) &
                            0xFFU));
                pixels_[static_cast<std::size_t>(y) * visible_width + x] =
                    palette_rgb(palette_ram, palette_index, mixed);
            }
        }
        ++frame_index_;
    }

    void m75_video::save_state(chips::state_writer& writer) const {
        writer.u64(elapsed_cycles_);
        writer.u64(frame_index_);
        for (const std::uint32_t pixel : pixels_) {
            writer.u32(pixel);
        }
    }

    void m75_video::load_state(chips::state_reader& reader) {
        elapsed_cycles_ = reader.u64();
        frame_index_ = reader.u64();
        for (std::uint32_t& pixel : pixels_) {
            pixel = reader.u32();
        }
    }

    m75_system::m75_system(common::rom_set_image image, m75_board_params board_params)
        : roms(std::move(image)), params(board_params) {
        dsw1 = params.dsw1_default;
        dsw2 = params.dsw2_default;

        auto& main_prog = pinned_region(roms, "maincpu", main_rom_size);
        auto& sound_prog = pinned_region(roms, "soundcpu", sound_rom_size);
        (void)pinned_region(roms, "chars", char_gfx_size);
        (void)pinned_region(roms, "sprites", sprite_gfx_size);
        (void)pinned_region(roms, "bgtiles", bg_tile_gfx_size);
        (void)pinned_region(roms, "samples", sample_rom_size);
        (void)pinned_region(roms, "proms", proms_size);
        (void)pinned_region(roms, "plds", plds_size);

        main_bus.map_rom(main_fixed_rom_base,
                         std::span<const std::uint8_t>(main_prog).first(main_fixed_rom_size));
        main_bus.map_rom(
            main_bank_rom_base,
            std::span<const std::uint8_t>(main_prog).subspan(0x10000U, main_bank_rom_size));
        main_bus.map_ram(sprite_ram_base, sprite_ram, 1);
        main_bus.map_mmio(
            palette_ram_base, static_cast<std::uint32_t>(palette_ram_size),
            [this](std::uint32_t address) -> std::uint8_t {
                return m75_palette_read(palette_ram, address);
            },
            [this](std::uint32_t address, std::uint8_t value) {
                m75_palette_write(palette_ram, address, value);
            },
            1);
        main_bus.map_ram(video_ram_base, video_ram, 1);
        main_bus.map_ram(work_ram_base, work_ram, 1);
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
                return input_system;
            case port_in_dsw1:
                return dsw1;
            case port_in_dsw2:
                return dsw2;
            default:
                return 0xFFU;
            }
        });
        main_cpu.set_port_out([this](std::uint16_t port, std::uint8_t value) {
            switch (port & 0xFFU) {
            case port_out_sound_latch:
                sound_latch = value;
                sound_latch_irq = true;
                update_sound_irq();
                break;
            case port_out_control:
                control_register = value;
                break;
            case port_out_bank:
                set_bank(value);
                break;
            case port_out_scroll_lo:
                scroll = static_cast<std::uint16_t>((scroll & 0xFF00U) | value);
                break;
            case port_out_scroll_hi:
                scroll = static_cast<std::uint16_t>((scroll & 0x00FFU) |
                                                    (static_cast<std::uint16_t>(value) << 8U));
                break;
            case port_out_rear_scroll_lo:
                rear_scroll = static_cast<std::uint16_t>((rear_scroll & 0xFF00U) | value);
                break;
            case port_out_rear_scroll_hi:
                rear_scroll =
                    static_cast<std::uint16_t>((rear_scroll & 0x00FFU) |
                                               (static_cast<std::uint16_t>(value) << 8U));
                break;
            case port_out_rear_color:
                rear_color = value;
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
            case z80_port_latch_lo:
            case z80_port_latch_hi:
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
            case z80_port_latch_lo:
                sample_address = (sample_address & 0xFF00U) | value;
                break;
            case z80_port_latch_hi:
                sample_address =
                    (sample_address & 0x00FFU) | (static_cast<std::uint32_t>(value) << 8U);
                break;
            case z80_port_dac:
                record_dac_write(value);
                break;
            case z80_port_latch_ack:
                sound_latch_irq = false;
                update_sound_irq();
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

        main_cpu.reset(chips::reset_kind::power_on);
        sound_cpu.reset(chips::reset_kind::power_on);
        sound_cpu.set_reset_line(false);
    }

    void m75_system::run_frame() {
        main_cpu.tick(main_cycles_per_frame);
        if (!sound_cpu.reset_line_held()) {
            sound_cpu.tick(sound_cycles_per_frame);
        }
        fm.tick(fm_cycles_per_frame);
        dac.tick(dac_cycles_per_frame);
        video.tick(main_cycles_per_frame);

        const auto* main_program = roms.region("maincpu");
        const auto* sound_program = roms.region("soundcpu");
        const auto* chars = roms.region("chars");
        const auto* sprites = roms.region("sprites");
        const auto* bgtiles = roms.region("bgtiles");
        const auto* samples = roms.region("samples");
        const auto* proms = roms.region("proms");
        video.compose(main_program != nullptr ? std::span<const std::uint8_t>(*main_program)
                                              : std::span<const std::uint8_t>{},
                      sound_program != nullptr ? std::span<const std::uint8_t>(*sound_program)
                                               : std::span<const std::uint8_t>{},
                      chars != nullptr ? std::span<const std::uint8_t>(*chars)
                                       : std::span<const std::uint8_t>{},
                      sprites != nullptr ? std::span<const std::uint8_t>(*sprites)
                                         : std::span<const std::uint8_t>{},
                      bgtiles != nullptr ? std::span<const std::uint8_t>(*bgtiles)
                                         : std::span<const std::uint8_t>{},
                      samples != nullptr ? std::span<const std::uint8_t>(*samples)
                                         : std::span<const std::uint8_t>{},
                      proms != nullptr ? std::span<const std::uint8_t>(*proms)
                                       : std::span<const std::uint8_t>{},
                      video_ram, palette_ram, sprite_ram, scroll, rear_scroll, rear_color,
                      params.rom_layout);
    }

    void m75_system::set_inputs(std::uint8_t p1, std::uint8_t p2,
                                std::uint8_t system) noexcept {
        input_p1 = p1;
        input_p2 = p2;
        input_system = system;
    }

    void m75_system::set_bank(std::uint8_t value) {
        bank_register = value;
        auto& main_prog = roms.regions["maincpu"];
        const std::size_t bank = value & 0x07U;
        const std::size_t offset = 0x10000U + bank * main_bank_rom_size;
        main_bus.retarget_rom(
            main_bank_rom_base,
            std::span<const std::uint8_t>(main_prog).subspan(offset, main_bank_rom_size));
    }

    void m75_system::update_sound_irq() noexcept {
        sound_cpu.set_irq_line(sound_latch_irq || fm.irq_asserted());
    }

    void m75_system::record_dac_write(std::uint8_t value) {
        dac.write(value);
        const dac_write_event event{.sound_clock = fm.elapsed_clocks(), .output = dac.output()};
        if (!dac_write_events.empty() && dac_write_events.back().sound_clock == event.sound_clock) {
            dac_write_events.back() = event;
            return;
        }
        dac_write_events.push_back(event);
    }

    void m75_system::discard_dac_write_events_before(std::uint64_t sound_clock) {
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

    void m75_system::save_state(chips::state_writer& writer) const {
        writer.u32(m75_system_state_version);
        writer.u8(params.dsw1_default);
        writer.u8(params.dsw2_default);
        writer.u8(layout_code(params.rom_layout));
        writer.u32(board_identity_crc(roms, params));

        main_cpu.save_state(writer);
        sound_cpu.save_state(writer);
        video.save_state(writer);
        fm.save_state(writer);
        dac.save_state(writer);

        writer.bytes(sprite_ram);
        writer.bytes(palette_ram);
        writer.bytes(video_ram);
        writer.bytes(work_ram);
        writer.bytes(sound_ram);

        writer.u8(sound_latch);
        writer.u8(input_p1);
        writer.u8(input_p2);
        writer.u8(input_system);
        writer.u8(dsw1);
        writer.u8(dsw2);
        writer.u8(control_register);
        writer.u8(bank_register);
        writer.u16(scroll);
        writer.u16(rear_scroll);
        writer.u8(rear_color);
        writer.boolean(sound_latch_irq);
        writer.u32(sample_address);
        writer.u32(static_cast<std::uint32_t>(dac_write_events.size()));
        for (const auto& event : dac_write_events) {
            writer.u64(event.sound_clock);
            writer.u16(static_cast<std::uint16_t>(static_cast<std::int32_t>(event.output) + 32768));
        }
    }

    void m75_system::load_state(chips::state_reader& reader) {
        if (reader.u32() != m75_system_state_version) {
            reader.fail();
            return;
        }
        const std::uint8_t saved_dsw1 = reader.u8();
        const std::uint8_t saved_dsw2 = reader.u8();
        const std::uint8_t saved_layout = reader.u8();
        const std::uint32_t saved_identity = reader.u32();
        if (saved_dsw1 != params.dsw1_default || saved_dsw2 != params.dsw2_default ||
            saved_layout != layout_code(params.rom_layout) ||
            saved_identity != board_identity_crc(roms, params)) {
            reader.fail();
            return;
        }

        main_cpu.load_state(reader);
        sound_cpu.load_state(reader);
        video.load_state(reader);
        fm.load_state(reader);
        dac.load_state(reader);

        reader.bytes(sprite_ram);
        reader.bytes(palette_ram);
        reader.bytes(video_ram);
        reader.bytes(work_ram);
        reader.bytes(sound_ram);

        sound_latch = reader.u8();
        input_p1 = reader.u8();
        input_p2 = reader.u8();
        input_system = reader.u8();
        dsw1 = reader.u8();
        dsw2 = reader.u8();
        control_register = reader.u8();
        bank_register = reader.u8();
        scroll = reader.u16();
        rear_scroll = reader.u16();
        rear_color = reader.u8();
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
            set_bank(bank_register);
            update_sound_irq();
        }
    }

    std::unique_ptr<m75_system> assemble_m75(common::rom_set_image image,
                                             m75_board_params board_params) {
        return std::make_unique<m75_system>(std::move(image), board_params);
    }

} // namespace mnemos::manifests::irem_m75
