#include "m102_system.hpp"

#include "crc32.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace mnemos::manifests::irem_m102 {

    namespace {
        [[nodiscard]] std::vector<std::uint8_t>&
        pinned_region(common::rom_set_image& image, std::string_view name, std::size_t size,
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

        [[nodiscard]] std::uint8_t layout_code(std::string_view layout) noexcept {
            if (layout == "m102_hclimber_z80_ga20_first_pass") {
                return 1U;
            }
            return 0U;
        }

        [[nodiscard]] std::uint32_t rgb(std::uint8_t r, std::uint8_t g,
                                        std::uint8_t b) noexcept {
            return (static_cast<std::uint32_t>(r) << 16U) |
                   (static_cast<std::uint32_t>(g) << 8U) | b;
        }

        [[nodiscard]] std::uint8_t expand_3bit(std::uint8_t value) noexcept {
            return static_cast<std::uint8_t>((value & 0x07U) * 0x24U);
        }

        [[nodiscard]] std::uint8_t expand_2bit(std::uint8_t value) noexcept {
            return static_cast<std::uint8_t>((value & 0x03U) * 0x55U);
        }

        [[nodiscard]] std::uint32_t diagnostic_color(std::uint8_t value,
                                                     std::uint8_t mix) noexcept {
            const std::uint8_t r = static_cast<std::uint8_t>(expand_3bit(value) ^ mix);
            const std::uint8_t g =
                static_cast<std::uint8_t>(expand_3bit(value >> 3U) ^ (mix >> 1U));
            const std::uint8_t b =
                static_cast<std::uint8_t>(expand_2bit(value >> 6U) ^ (mix << 1U));
            return rgb(r, g, b);
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
                                                       const m102_board_params& params) {
            std::uint32_t crc =
                security::cryptography::crc32(std::string_view{"m102.board.identity.v1"});
            const std::array<std::uint8_t, 3> params_bytes{layout_code(params.rom_layout),
                                                           params.dsw1_default,
                                                           params.dsw2_default};
            crc = security::cryptography::crc32(
                std::span<const std::uint8_t>(params_bytes.data(), params_bytes.size()), crc);
            crc = crc32_u64(crc, params.cpu_clock_hz);
            crc = crc32_u64(crc, params.ga20_clock_hz);
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

    m102_board_params board_params_for(std::string_view set_name) noexcept {
        if (set_name == "hclimber") {
            return {.cpu_clock_hz = cpu_clock_hz,
                    .ga20_clock_hz = ga20_clock_hz,
                    .rom_layout = "m102_hclimber_z80_ga20_first_pass",
                    .dsw1_default = dsw1_default,
                    .dsw2_default = dsw2_default};
        }
        return {};
    }

    m102_video::m102_video()
        : pixels_(static_cast<std::size_t>(visible_width) * visible_height, 0U) {
        reset(chips::reset_kind::power_on);
    }

    chips::chip_metadata m102_video::metadata() const noexcept {
        return {.manufacturer = "Irem",
                .part_number = "m102_medal_video_first_pass",
                .family = "irem_m102",
                .klass = chips::chip_class::video,
                .revision = 1U};
    }

    void m102_video::tick(std::uint64_t cycles) { elapsed_cycles_ += cycles; }

    void m102_video::reset(chips::reset_kind /*kind*/) {
        std::fill(pixels_.begin(), pixels_.end(), 0U);
        elapsed_cycles_ = 0U;
        frame_index_ = 0U;
    }

    chips::frame_buffer_view m102_video::framebuffer() const noexcept {
        return {.pixels = pixels_.data(),
                .width = visible_width,
                .height = visible_height,
                .stride = visible_width};
    }

    void m102_video::compose(std::span<const std::uint8_t> main_rom,
                             std::span<const std::uint8_t> ga20_rom,
                             std::span<const std::uint8_t> work_ram,
                             std::span<const std::uint8_t> video_ram,
                             std::span<const std::uint8_t> medal_ram,
                             std::uint8_t bank_select,
                             std::uint8_t output_latch) {
        const std::uint8_t frame_mix =
            static_cast<std::uint8_t>(frame_index_ ^ (bank_select * 0x31U) ^ output_latch);
        for (std::uint32_t y = 0; y < visible_height; ++y) {
            for (std::uint32_t x = 0; x < visible_width; ++x) {
                const std::uint64_t linear = static_cast<std::uint64_t>(y) * visible_width + x;
                const std::uint8_t program =
                    sample_byte(main_rom, (linear >> 2U) + frame_index_,
                                static_cast<std::uint8_t>(x ^ y));
                const std::uint8_t sample =
                    sample_byte(ga20_rom, (linear >> 1U) ^ (x << 3U),
                                static_cast<std::uint8_t>(x + y));
                const std::uint8_t vram =
                    sample_byte(video_ram, (linear >> 3U) + (y & 0x0FU), program);
                const std::uint8_t wr =
                    sample_byte(work_ram, (linear >> 5U) ^ x, static_cast<std::uint8_t>(~sample));
                const std::uint8_t medal =
                    sample_byte(medal_ram, (linear >> 4U) + y, output_latch);
                const std::uint8_t color =
                    static_cast<std::uint8_t>(program ^ sample ^ vram ^ wr ^ medal ^ frame_mix);
                pixels_[static_cast<std::size_t>(y) * visible_width + x] =
                    diagnostic_color(color, static_cast<std::uint8_t>(frame_mix + (x >> 3U)));
            }
        }
        ++frame_index_;
    }

    void m102_video::save_state(chips::state_writer& writer) const {
        writer.u64(elapsed_cycles_);
        writer.u64(frame_index_);
        for (const std::uint32_t pixel : pixels_) {
            writer.u32(pixel);
        }
    }

    void m102_video::load_state(chips::state_reader& reader) {
        elapsed_cycles_ = reader.u64();
        frame_index_ = reader.u64();
        for (std::uint32_t& pixel : pixels_) {
            pixel = reader.u32();
        }
    }

    m102_system::m102_system(common::rom_set_image image, m102_board_params board_params)
        : roms(std::move(image)), params(board_params) {
        auto& main_prog = pinned_region(roms, "maincpu", main_rom_size);
        auto& ga20_rom = pinned_region(roms, "ga20", ga20_rom_size);
        (void)pinned_region(roms, "plds", pld_region_size, 0x00U);

        dsw1 = params.dsw1_default;
        dsw2 = params.dsw2_default;

        main_bus.map_rom(fixed_rom_base,
                         std::span<const std::uint8_t>(main_prog).first(fixed_rom_size));
        main_bus.map_rom(bank_rom_base,
                         std::span<const std::uint8_t>(main_prog).subspan(0U, bank_rom_size));
        main_bus.map_ram(video_ram_base, video_ram);
        main_bus.map_ram(medal_ram_base, medal_ram);
        main_bus.map_ram(work_ram_base, work_ram);
        main_cpu.attach_bus(main_bus);
        main_cpu.set_port_in([this](std::uint16_t port) { return read_io_port(port); });
        main_cpu.set_port_out(
            [this](std::uint16_t port, std::uint8_t value) { write_io_port(port, value); });
        main_cpu.reset(chips::reset_kind::power_on);

        ga20.set_input_clock(params.ga20_clock_hz);
        ga20.set_sample_rom(ga20_rom);
        ga20.set_capture_divider(ga20_capture_divider);
        ga20.enable_audio_capture(true);
    }

    void m102_system::retarget_bank_window() noexcept {
        const auto* main_prog = roms.region("maincpu");
        if (main_prog == nullptr || main_prog->size() < bank_rom_size) {
            return;
        }
        const std::size_t bank_count = std::max<std::size_t>(1U, main_prog->size() / bank_rom_size);
        const std::size_t bank = static_cast<std::size_t>(bank_select) % bank_count;
        const std::size_t offset = bank * bank_rom_size;
        main_bus.retarget_rom(bank_rom_base,
                              std::span<const std::uint8_t>(*main_prog).subspan(offset,
                                                                                bank_rom_size));
    }

    void m102_system::run_frame() {
        main_cpu.tick(cpu_cycles_per_frame);
        ga20.tick(ga20_cycles_per_frame);
        video.tick(cpu_cycles_per_frame);

        const auto* main_prog = roms.region("maincpu");
        const auto* samples = roms.region("ga20");
        video.compose(main_prog != nullptr ? std::span<const std::uint8_t>(*main_prog)
                                           : std::span<const std::uint8_t>{},
                      samples != nullptr ? std::span<const std::uint8_t>(*samples)
                                         : std::span<const std::uint8_t>{},
                      work_ram, video_ram, medal_ram, bank_select, output_latch);
    }

    void m102_system::set_inputs(std::uint8_t in0, std::uint8_t in1) noexcept {
        input0 = in0;
        input1 = in1;
    }

    std::uint8_t m102_system::read_io_port(std::uint16_t port) const noexcept {
        const auto p = static_cast<std::uint8_t>(port & 0x00FFU);
        if (p <= port_ga20_end) {
            return ga20.read_register(p);
        }
        switch (p) {
        case port_input0:
            return input0;
        case port_input1:
            return input1;
        case port_dsw1:
            return dsw1;
        case port_dsw2:
            return dsw2;
        case port_bank:
            return bank_select;
        case port_output:
            return output_latch;
        default:
            return 0xFFU;
        }
    }

    void m102_system::write_io_port(std::uint16_t port, std::uint8_t value) noexcept {
        const auto p = static_cast<std::uint8_t>(port & 0x00FFU);
        if (p <= port_ga20_end) {
            ga20.write_register(p, value);
            ++ga20_register_write_count;
            if ((p & 0x07U) == chips::audio::irem_ga20::reg_control &&
                (value & chips::audio::irem_ga20::control_key_on) != 0U) {
                ++ga20_key_on_count;
            }
            return;
        }
        switch (p) {
        case port_bank:
            set_bank(value);
            break;
        case port_output:
            output_latch = value;
            ++output_write_count;
            break;
        default:
            break;
        }
    }

    void m102_system::set_bank(std::uint8_t value) noexcept {
        if (bank_select != value) {
            ++bank_switch_count;
        }
        bank_select = value;
        retarget_bank_window();
    }

    void m102_system::save_state(chips::state_writer& writer) const {
        writer.u32(m102_system_state_version);
        writer.u8(layout_code(params.rom_layout));
        writer.u8(params.dsw1_default);
        writer.u8(params.dsw2_default);
        writer.u32(params.cpu_clock_hz);
        writer.u32(params.ga20_clock_hz);
        writer.u32(board_identity_crc(roms, params));
        main_cpu.save_state(writer);
        video.save_state(writer);
        ga20.save_state(writer);
        writer.bytes(video_ram);
        writer.bytes(medal_ram);
        writer.bytes(work_ram);
        writer.u8(input0);
        writer.u8(input1);
        writer.u8(dsw1);
        writer.u8(dsw2);
        writer.u8(bank_select);
        writer.u8(output_latch);
        writer.u64(bank_switch_count);
        writer.u64(output_write_count);
        writer.u64(ga20_register_write_count);
        writer.u64(ga20_key_on_count);
    }

    void m102_system::load_state(chips::state_reader& reader) {
        if (reader.u32() != m102_system_state_version) {
            reader.fail();
            return;
        }
        if (reader.u8() != layout_code(params.rom_layout) ||
            reader.u8() != params.dsw1_default || reader.u8() != params.dsw2_default ||
            reader.u32() != params.cpu_clock_hz || reader.u32() != params.ga20_clock_hz ||
            reader.u32() != board_identity_crc(roms, params)) {
            reader.fail();
            return;
        }
        main_cpu.load_state(reader);
        video.load_state(reader);
        ga20.load_state(reader);
        reader.bytes(video_ram);
        reader.bytes(medal_ram);
        reader.bytes(work_ram);
        input0 = reader.u8();
        input1 = reader.u8();
        dsw1 = reader.u8();
        dsw2 = reader.u8();
        bank_select = reader.u8();
        output_latch = reader.u8();
        bank_switch_count = reader.u64();
        output_write_count = reader.u64();
        ga20_register_write_count = reader.u64();
        ga20_key_on_count = reader.u64();
        if (reader.ok()) {
            retarget_bank_window();
            ga20.enable_audio_capture(true);
            ga20.set_capture_divider(ga20_capture_divider);
        }
    }

    std::unique_ptr<m102_system> assemble_m102(common::rom_set_image image,
                                               m102_board_params board_params) {
        return std::make_unique<m102_system>(std::move(image), board_params);
    }

} // namespace mnemos::manifests::irem_m102
