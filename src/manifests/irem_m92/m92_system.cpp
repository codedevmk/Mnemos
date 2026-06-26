#include "m92_system.hpp"

#include "crc32.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace mnemos::manifests::irem_m92 {

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
            if (layout == "m92_b_b") {
                return 1U;
            }
            if (layout == "m92_b_a") {
                return 2U;
            }
            if (layout == "m92_b_g") {
                return 3U;
            }
            if (layout == "m92_d_a") {
                return 4U;
            }
            if (layout == "m92_e_b") {
                return 5U;
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
            case 3U:
                return 0x71U;
            case 4U:
                return 0x43U;
            case 5U:
                return 0x29U;
            default:
                return 0x23U;
            }
        }

        [[nodiscard]] std::uint32_t board_identity_crc(const common::rom_set_image& roms,
                                                       const m92_board_params& params,
                                                       const chips::cpu::v30& main_cpu,
                                                       const chips::cpu::v30& sound_cpu) {
            std::uint32_t crc =
                security::cryptography::crc32(std::string_view{"m92.board.identity.v2"});
            crc = crc32_u16(crc, params.dip_default);
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

    m92_board_params board_params_for(std::string_view set_name) noexcept {
        if (set_name == "bmaster") {
            return {.dip_default = 0xFFFFU, .rom_layout = "m92_b_b"};
        }
        if (set_name == "gunforce" || set_name == "gunforcej" || set_name == "gunforceu") {
            return {.dip_default = 0xFFFFU, .rom_layout = "m92_b_a"};
        }
        if (set_name == "gunforc2") {
            return {.dip_default = 0xFFFFU, .rom_layout = "m92_b_g"};
        }
        if (set_name == "hook") {
            return {.dip_default = 0xFFFFU, .rom_layout = "m92_d_a"};
        }
        if (set_name == "inthunt") {
            return {.dip_default = 0xFFFFU, .rom_layout = "m92_e_b"};
        }
        return {};
    }

    m92_video::m92_video() : pixels_(static_cast<std::size_t>(visible_width) * visible_height, 0U) {
        reset(chips::reset_kind::power_on);
    }

    chips::chip_metadata m92_video::metadata() const noexcept {
        return {.manufacturer = "Irem",
                .part_number = "m92_video_first_pass",
                .family = "irem_m92",
                .klass = chips::chip_class::video,
                .revision = 1U};
    }

    void m92_video::tick(std::uint64_t cycles) { elapsed_cycles_ += cycles; }

    void m92_video::reset(chips::reset_kind /*kind*/) {
        std::fill(pixels_.begin(), pixels_.end(), 0U);
        elapsed_cycles_ = 0U;
        frame_index_ = 0U;
    }

    chips::frame_buffer_view m92_video::framebuffer() const noexcept {
        return {.pixels = pixels_.data(),
                .width = visible_width,
                .height = visible_height,
                .stride = visible_width};
    }

    void
    m92_video::compose(std::span<const std::uint8_t> tiles, std::span<const std::uint8_t> sprites,
                       std::span<const std::uint8_t> plds, std::span<const std::uint8_t> samples,
                       std::span<const std::uint8_t> vram, std::span<const std::uint8_t> palette,
                       std::span<const std::uint8_t> sprite_ram, std::string_view rom_layout) {
        const std::uint8_t tint = layout_tint(rom_layout);
        for (std::uint32_t y = 0; y < visible_height; ++y) {
            for (std::uint32_t x = 0; x < visible_width; ++x) {
                const std::uint64_t linear =
                    static_cast<std::uint64_t>(y) * visible_width + x + frame_index_;
                const std::uint8_t g = sample_byte(tiles, linear * 3U + (x >> 3U), 0x5AU);
                const std::uint8_t s = sample_byte(sprites, linear + (y << 2U), 0x00U);
                const std::uint8_t a = sample_byte(samples, (linear >> 1U) + tint, 0x11U);
                const std::uint8_t vr = sample_byte(vram, linear + (x ^ y), 0x00U);
                const std::uint8_t sp = sample_byte(sprite_ram, (linear >> 2U) + y, 0x00U);
                const std::uint8_t pal = sample_byte(palette, (g ^ s ^ a) * 2U, tint);
                const std::uint8_t board = sample_byte(plds, (x + y + tint) & 0x7FFU, 0x00U);
                const std::uint8_t mixed = static_cast<std::uint8_t>(
                    g ^ static_cast<std::uint8_t>(s << 1U) ^ a ^ vr ^ sp ^ pal ^ board ^ tint);
                pixels_[static_cast<std::size_t>(y) * visible_width + x] =
                    rgb_from_byte(mixed, tint);
            }
        }
        ++frame_index_;
    }

    void m92_video::save_state(chips::state_writer& writer) const {
        writer.u64(elapsed_cycles_);
        writer.u64(frame_index_);
        for (const std::uint32_t pixel : pixels_) {
            writer.u32(pixel);
        }
    }

    void m92_video::load_state(chips::state_reader& reader) {
        elapsed_cycles_ = reader.u64();
        frame_index_ = reader.u64();
        for (std::uint32_t& pixel : pixels_) {
            pixel = reader.u32();
        }
    }

    m92_system::m92_system(common::rom_set_image image, m92_board_params board_params)
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
            case port_in_system:
                return input_system;
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
                sound_latch = value;
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
            sound_latch_addr, 1U, [this](std::uint32_t) -> std::uint8_t { return sound_latch; },
            [](std::uint32_t, std::uint8_t) {}, 1);
        sound_bus.map_mmio(
            sound_reply_addr, 1U, [this](std::uint32_t) -> std::uint8_t { return sound_reply; },
            [this](std::uint32_t, std::uint8_t value) { sound_reply = value; }, 1);
        sound_cpu.attach_bus(sound_bus);
        sound_cpu.set_port_in([this](std::uint16_t port) -> std::uint8_t {
            if (port >= sound_port_ga20_base && port < sound_port_ga20_limit) {
                return pcm.read_register(static_cast<std::uint8_t>(port - sound_port_ga20_base));
            }
            switch (port) {
            case sound_port_latch:
                return sound_latch;
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
                sound_reply = value;
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

        dip_switches = params.dip_default;
        pcm.set_input_clock(pcm_clock_hz);
        pcm.set_capture_divider(pcm_capture_divider);
        pcm.set_sample_rom(std::span<const std::uint8_t>(samples));
        pcm.enable_audio_capture(true);
    }

    void m92_system::run_frame() {
        main_cpu.tick(main_cycles_per_frame);
        sound_cpu.tick(sound_cycles_per_frame);
        fm.tick(fm_cycles_per_frame);
        pcm.tick(pcm_cycles_per_frame);

        const auto* tiles = roms.region("tiles");
        const auto* sprites = roms.region("sprites");
        const auto* plds = roms.region("plds");
        const auto* samples = roms.region("samples");
        video.compose(tiles != nullptr ? std::span<const std::uint8_t>(*tiles)
                                       : std::span<const std::uint8_t>{},
                      sprites != nullptr ? std::span<const std::uint8_t>(*sprites)
                                         : std::span<const std::uint8_t>{},
                      plds != nullptr ? std::span<const std::uint8_t>(*plds)
                                      : std::span<const std::uint8_t>{},
                      samples != nullptr ? std::span<const std::uint8_t>(*samples)
                                         : std::span<const std::uint8_t>{},
                      vram, palette_ram, sprite_ram, params.rom_layout);
    }

    void m92_system::set_inputs(std::uint8_t p1, std::uint8_t p2, std::uint8_t system) noexcept {
        input_p1 = p1;
        input_p2 = p2;
        input_system = system;
    }

    void m92_system::save_state(chips::state_writer& writer) const {
        writer.u32(m92_system_state_version);
        writer.u16(params.dip_default);
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
        writer.u8(sound_latch);
        writer.u8(sound_reply);
        writer.u8(control_register);
        writer.u8(ym_address);
    }

    void m92_system::load_state(chips::state_reader& reader) {
        if (reader.u32() != m92_system_state_version) {
            reader.fail();
            return;
        }
        const std::uint16_t saved_dip = reader.u16();
        const std::uint8_t saved_layout = reader.u8();
        const std::uint32_t saved_identity = reader.u32();
        if (saved_dip != params.dip_default || saved_layout != layout_code(params.rom_layout) ||
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
        sound_latch = reader.u8();
        sound_reply = reader.u8();
        control_register = reader.u8();
        ym_address = reader.u8();
    }

    std::unique_ptr<m92_system> assemble_m92(common::rom_set_image image,
                                             m92_board_params board_params) {
        return std::make_unique<m92_system>(std::move(image), board_params);
    }

} // namespace mnemos::manifests::irem_m92
