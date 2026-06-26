#include "m107_system.hpp"

#include "crc32.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace mnemos::manifests::irem_m107 {

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

        [[nodiscard]] std::uint32_t rgb_from_byte(std::uint8_t value, std::uint8_t tint) noexcept {
            const std::uint32_t r = static_cast<std::uint32_t>((value * 5U + tint) & 0xFFU);
            const std::uint32_t g =
                static_cast<std::uint32_t>(((value << 1U) ^ (tint * 7U)) & 0xFFU);
            const std::uint32_t b =
                static_cast<std::uint32_t>(((value >> 1U) + (tint * 13U)) & 0xFFU);
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

        [[nodiscard]] std::uint32_t crc32_u16(std::uint32_t crc, std::uint16_t value) noexcept {
            std::array<std::uint8_t, 2> bytes{static_cast<std::uint8_t>(value),
                                              static_cast<std::uint8_t>(value >> 8U)};
            return security::cryptography::crc32(
                std::span<const std::uint8_t>(bytes.data(), bytes.size()), crc);
        }

        [[nodiscard]] std::uint32_t crc32_u32(std::uint32_t crc, std::uint32_t value) noexcept {
            std::array<std::uint8_t, 4> bytes{
                static_cast<std::uint8_t>(value),
                static_cast<std::uint8_t>(value >> 8U),
                static_cast<std::uint8_t>(value >> 16U),
                static_cast<std::uint8_t>(value >> 24U),
            };
            return security::cryptography::crc32(
                std::span<const std::uint8_t>(bytes.data(), bytes.size()), crc);
        }

        [[nodiscard]] std::uint32_t crc32_u8(std::uint32_t crc, std::uint8_t value) noexcept {
            std::array<std::uint8_t, 1> bytes{value};
            return security::cryptography::crc32(
                std::span<const std::uint8_t>(bytes.data(), bytes.size()), crc);
        }

        [[nodiscard]] std::uint8_t layout_code(std::string_view layout) noexcept {
            if (layout == "air_assault") {
                return 1U;
            }
            if (layout == "fire_barrel") {
                return 2U;
            }
            return 0U;
        }

        [[nodiscard]] std::uint8_t cpu_model_code(chips::cpu::v30::model model) noexcept {
            switch (model) {
            case chips::cpu::v30::model::v33:
                return 2U;
            case chips::cpu::v30::model::v35:
                return 3U;
            case chips::cpu::v30::model::v30:
            default:
                return 1U;
            }
        }

        [[nodiscard]] std::uint8_t layout_tint(std::string_view layout) noexcept {
            switch (layout_code(layout)) {
            case 1U:
                return 0x37U;
            case 2U:
                return 0x59U;
            default:
                return 0x23U;
            }
        }

        [[nodiscard]] std::uint32_t board_identity_crc(const common::rom_set_image& roms,
                                                       const m107_board_params& params,
                                                       const chips::cpu::v30& main_cpu,
                                                       const chips::cpu::v30& sound_cpu) {
            std::uint32_t crc =
                security::cryptography::crc32(std::string_view{"m107.board.identity.v4"});
            crc = crc32_u16(crc, params.dip_default);
            crc = crc32_u16(crc, params.coins_dsw3_default);
            crc = crc32_u8(crc, layout_code(params.rom_layout));
            crc = crc32_u8(crc, cpu_model_code(main_cpu.cpu_model()));
            crc = crc32_u32(crc, main_clock_hz);
            crc = crc32_u8(crc, cpu_model_code(sound_cpu.cpu_model()));
            crc = crc32_u32(crc, sound_cpu_clock_hz);
            crc = crc32_u32(crc, fm_clock_hz);
            crc = crc32_u32(crc, pcm_clock_hz);
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

    m107_board_params board_params_for(std::string_view set_name) noexcept {
        if (set_name == "airass") {
            return {.dip_default = 0xFFFFU, .rom_layout = "air_assault"};
        }
        if (set_name == "firebarr") {
            return {.dip_default = 0xFFFFU, .rom_layout = "fire_barrel"};
        }
        return {};
    }

    m107_video::m107_video()
        : pixels_(static_cast<std::size_t>(visible_width) * visible_height, 0U) {
        reset(chips::reset_kind::power_on);
    }

    chips::chip_metadata m107_video::metadata() const noexcept {
        return {.manufacturer = "Irem",
                .part_number = "m107_video_first_pass",
                .family = "irem_m107",
                .klass = chips::chip_class::video,
                .revision = 1U};
    }

    void m107_video::tick(std::uint64_t cycles) { elapsed_cycles_ += cycles; }

    void m107_video::reset(chips::reset_kind /*kind*/) {
        std::fill(pixels_.begin(), pixels_.end(), 0U);
        elapsed_cycles_ = 0U;
        frame_index_ = 0U;
    }

    chips::frame_buffer_view m107_video::framebuffer() const noexcept {
        return {.pixels = pixels_.data(),
                .width = visible_width,
                .height = visible_height,
                .stride = visible_width};
    }

    void m107_video::compose(std::span<const std::uint8_t> gfx,
                             std::span<const std::uint8_t> subdata,
                             std::span<const std::uint8_t> samples,
                             std::span<const std::uint8_t> vram,
                             std::span<const std::uint8_t> palette,
                             std::span<const std::uint8_t> sprites, std::string_view rom_layout) {
        const std::uint8_t tint = layout_tint(rom_layout);
        for (std::uint32_t y = 0; y < visible_height; ++y) {
            for (std::uint32_t x = 0; x < visible_width; ++x) {
                const std::uint64_t linear =
                    static_cast<std::uint64_t>(y) * visible_width + x + frame_index_;
                const std::uint8_t g = sample_byte(gfx, linear * 3U + (x >> 3U), 0x5AU);
                const std::uint8_t s = sample_byte(subdata, linear + (y << 2U), 0x00U);
                const std::uint8_t a = sample_byte(samples, (linear >> 1U) + tint, 0x11U);
                const std::uint8_t vr = sample_byte(vram, linear + (x ^ y), 0x00U);
                const std::uint8_t sp = sample_byte(sprites, (linear >> 2U) + y, 0x00U);
                const std::uint8_t pal = sample_byte(palette, (g ^ s ^ a) * 2U, tint);
                const std::uint8_t mixed = static_cast<std::uint8_t>(
                    g ^ static_cast<std::uint8_t>(s << 1U) ^ a ^ vr ^ sp ^ pal ^ tint);
                pixels_[static_cast<std::size_t>(y) * visible_width + x] =
                    rgb_from_byte(mixed, tint);
            }
        }
        ++frame_index_;
    }

    void m107_video::save_state(chips::state_writer& writer) const {
        writer.u64(elapsed_cycles_);
        writer.u64(frame_index_);
        for (const std::uint32_t pixel : pixels_) {
            writer.u32(pixel);
        }
    }

    void m107_video::load_state(chips::state_reader& reader) {
        elapsed_cycles_ = reader.u64();
        frame_index_ = reader.u64();
        for (std::uint32_t& pixel : pixels_) {
            pixel = reader.u32();
        }
    }

    m107_system::m107_system(common::rom_set_image image, m107_board_params board_params)
        : roms(std::move(image)), params(board_params) {
        auto& main_prog = pinned_region(roms, "maincpu", main_rom_size);
        auto& sound_prog = pinned_region(roms, "soundcpu", sound_rom_size);
        auto& samples = pinned_region(roms, "samples", 0U);

        main_cpu.set_model(chips::cpu::v30::model::v33);
        sound_cpu.set_model(chips::cpu::v30::model::v35);

        main_bus.map_rom(0x00000U, std::span<const std::uint8_t>(main_prog));
        main_bus.map_ram(work_ram_base, work_ram, 1);
        main_bus.map_ram(sprite_ram_base, sprite_ram, 1);
        main_bus.map_ram(palette_ram_base, palette_ram, 1);
        main_bus.map_ram(vram_base, vram, 1);
        main_cpu.attach_bus(main_bus);
        main_cpu.set_port_in([this](std::uint16_t port) -> std::uint8_t {
            switch (port) {
            case port_in_p1:
                return input_p1;
            case port_in_p2:
                return input_p2;
            case port_in_coins_dsw3_lo:
                return input_system;
            case port_in_coins_dsw3_hi:
                return static_cast<std::uint8_t>(coins_dsw3 >> 8U);
            case port_in_dsw_lo:
                return static_cast<std::uint8_t>(dip_switches);
            case port_in_dsw_hi:
                return static_cast<std::uint8_t>(dip_switches >> 8U);
            default:
                return 0xFFU;
            }
        });
        main_cpu.set_port_out([this](std::uint16_t port, std::uint8_t value) {
            switch (port) {
            case port_out_sound_latch:
                write_sound_latch(value);
                break;
            case port_out_control:
                control_register = value;
                break;
            default:
                break;
            }
        });

        sound_bus.map_rom(0x00000U, std::span<const std::uint8_t>(sound_prog));
        sound_bus.map_rom(0xE0000U, std::span<const std::uint8_t>(sound_prog));
        sound_bus.map_ram(sound_work_ram_base, sound_ram, 1);
        sound_bus.map_mmio(
            sound_ga20_base, chips::audio::irem_ga20::register_count,
            [this](std::uint32_t address) -> std::uint8_t {
                return pcm.read_register(static_cast<std::uint8_t>(address - sound_ga20_base));
            },
            [this](std::uint32_t address, std::uint8_t value) {
                pcm.write_register(static_cast<std::uint8_t>(address - sound_ga20_base), value);
            },
            1);
        sound_bus.map_mmio(
            sound_ym2151_base, 4U,
            [this](std::uint32_t address) -> std::uint8_t {
                (void)address;
                return fm.read_status();
            },
            [this](std::uint32_t address, std::uint8_t value) {
                if ((address & 1U) == 0U) {
                    ym_address = value;
                    fm.write_address(value);
                } else {
                    fm.write_address(ym_address);
                    fm.write_data(value);
                }
            },
            1);
        sound_bus.map_mmio(
            sound_latch_addr, 1U,
            [this](std::uint32_t) -> std::uint8_t { return read_sound_latch(); },
            [this](std::uint32_t, std::uint8_t) { acknowledge_sound_latch(); }, 1);
        sound_bus.map_mmio(
            sound_reply_addr, 1U,
            [this](std::uint32_t) -> std::uint8_t { return read_sound_reply(); },
            [this](std::uint32_t, std::uint8_t value) { write_sound_reply(value); }, 1);
        sound_cpu.attach_bus(sound_bus);
        sound_cpu.set_port_in([this](std::uint16_t port) -> std::uint8_t {
            if (port >= sound_port_ga20_base && port < sound_port_ga20_limit) {
                return pcm.read_register(static_cast<std::uint8_t>(port - sound_port_ga20_base));
            }
            switch (port) {
            case sound_port_latch:
                return read_sound_latch();
            default:
                return 0xFFU;
            }
        });
        sound_cpu.set_port_out([this](std::uint16_t port, std::uint8_t value) {
            if (port >= sound_port_ga20_base && port < sound_port_ga20_limit) {
                pcm.write_register(static_cast<std::uint8_t>(port - sound_port_ga20_base), value);
                return;
            }
            switch (port) {
            case sound_port_reply:
                write_sound_reply(value);
                break;
            case sound_port_ym2151_addr:
                ym_address = value;
                fm.write_address(value);
                break;
            case sound_port_ym2151_data:
                fm.write_address(ym_address);
                fm.write_data(value);
                break;
            default:
                break;
            }
        });
        sound_cpu.set_irq_ack([this]() -> std::uint8_t {
            if (fm.irq_asserted()) {
                return sound_irq_vector_ym2151;
            }
            return sound_irq_vector_command_latch;
        });
        fm.set_irq([this](bool) { update_sound_irq(); });

        dip_switches = params.dip_default;
        coins_dsw3 = params.coins_dsw3_default;
        pcm.set_input_clock(pcm_clock_hz);
        pcm.set_capture_divider(pcm_capture_divider);
        pcm.set_sample_rom(std::span<const std::uint8_t>(samples));
        pcm.enable_audio_capture(true);
    }

    void m107_system::run_frame() {
        main_cpu.tick(main_cycles_per_frame);
        sound_cpu.tick(sound_cycles_per_frame);
        fm.tick(fm_cycles_per_frame);
        pcm.tick(pcm_cycles_per_frame);

        const auto* gfx = roms.region("gfx");
        const auto* subdata = roms.region("subdata");
        const auto* samples = roms.region("samples");
        video.compose(gfx != nullptr ? std::span<const std::uint8_t>(*gfx)
                                     : std::span<const std::uint8_t>{},
                      subdata != nullptr ? std::span<const std::uint8_t>(*subdata)
                                         : std::span<const std::uint8_t>{},
                      samples != nullptr ? std::span<const std::uint8_t>(*samples)
                                         : std::span<const std::uint8_t>{},
                      vram, palette_ram, sprite_ram, params.rom_layout);
    }

    void m107_system::set_inputs(std::uint8_t p1, std::uint8_t p2, std::uint8_t system) noexcept {
        input_p1 = p1;
        input_p2 = p2;
        input_system = system;
    }

    void m107_system::update_sound_irq() noexcept {
        sound_cpu.set_irq_line(sound_latch_pending || fm.irq_asserted());
    }

    void m107_system::write_sound_latch(std::uint8_t value) noexcept {
        sound_latch = value;
        sound_latch_pending = true;
        update_sound_irq();
    }

    std::uint8_t m107_system::read_sound_latch() noexcept { return sound_latch; }

    void m107_system::acknowledge_sound_latch() noexcept {
        sound_latch_pending = false;
        update_sound_irq();
    }

    void m107_system::write_sound_reply(std::uint8_t value) noexcept {
        sound_reply = value;
        sound_reply_pending = true;
    }

    std::uint8_t m107_system::read_sound_reply() noexcept {
        sound_reply_pending = false;
        return sound_reply;
    }

    void m107_system::save_state(chips::state_writer& writer) const {
        writer.u32(m107_system_state_version);
        writer.u16(params.dip_default);
        writer.u16(params.coins_dsw3_default);
        writer.u8(layout_code(params.rom_layout));
        writer.u32(board_identity_crc(roms, params, main_cpu, sound_cpu));
        main_cpu.save_state(writer);
        sound_cpu.save_state(writer);
        video.save_state(writer);
        fm.save_state(writer);
        pcm.save_state(writer);
        writer.bytes(work_ram);
        writer.bytes(sprite_ram);
        writer.bytes(palette_ram);
        writer.bytes(vram);
        writer.bytes(sound_ram);
        writer.u8(input_p1);
        writer.u8(input_p2);
        writer.u8(input_system);
        writer.u16(dip_switches);
        writer.u16(coins_dsw3);
        writer.u8(sound_latch);
        writer.u8(sound_reply);
        writer.u8(control_register);
        writer.u8(ym_address);
        writer.boolean(sound_latch_pending);
        writer.boolean(sound_reply_pending);
    }

    void m107_system::load_state(chips::state_reader& reader) {
        if (reader.u32() != m107_system_state_version) {
            reader.fail();
            return;
        }
        const std::uint16_t saved_dip = reader.u16();
        const std::uint16_t saved_coins_dsw3 = reader.u16();
        const std::uint8_t saved_layout = reader.u8();
        const std::uint32_t saved_identity = reader.u32();
        if (saved_dip != params.dip_default || saved_coins_dsw3 != params.coins_dsw3_default ||
            saved_layout != layout_code(params.rom_layout) ||
            saved_identity != board_identity_crc(roms, params, main_cpu, sound_cpu)) {
            reader.fail();
            return;
        }
        main_cpu.load_state(reader);
        sound_cpu.load_state(reader);
        video.load_state(reader);
        fm.load_state(reader);
        pcm.load_state(reader);
        reader.bytes(work_ram);
        reader.bytes(sprite_ram);
        reader.bytes(palette_ram);
        reader.bytes(vram);
        reader.bytes(sound_ram);
        input_p1 = reader.u8();
        input_p2 = reader.u8();
        input_system = reader.u8();
        dip_switches = reader.u16();
        coins_dsw3 = reader.u16();
        sound_latch = reader.u8();
        sound_reply = reader.u8();
        control_register = reader.u8();
        ym_address = reader.u8();
        sound_latch_pending = reader.boolean();
        sound_reply_pending = reader.boolean();
        update_sound_irq();
    }

    std::unique_ptr<m107_system> assemble_m107(common::rom_set_image image,
                                               m107_board_params board_params) {
        return std::make_unique<m107_system>(std::move(image), board_params);
    }

} // namespace mnemos::manifests::irem_m107
