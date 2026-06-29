#include "m119_system.hpp"

#include "crc32.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace mnemos::manifests::irem_m119 {

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

        [[nodiscard]] std::uint8_t layout_code(std::string_view layout) noexcept {
            if (layout == "m119_scumimon_sh7708_upd94244_ymz280b_first_pass") {
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
                                                       const m119_board_params& params) {
            std::uint32_t crc =
                security::cryptography::crc32(std::string_view{"m119.board.identity.v2"});
            crc = crc32_u64(crc, layout_code(params.rom_layout));
            crc = crc32_u64(crc, params.main_clock_hz);
            crc = crc32_u64(crc, params.ymz_clock_hz);
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

        [[nodiscard]] constexpr bool is_vdp_mmio(std::uint32_t offset) noexcept {
            return offset >= mmio_vdp_base && offset <= mmio_vdp_end;
        }

        [[nodiscard]] constexpr std::uint8_t vdp_mmio_register(std::uint32_t offset) noexcept {
            return static_cast<std::uint8_t>((offset - mmio_vdp_base) /
                                             mmio_vdp_register_stride);
        }

        [[nodiscard]] constexpr std::uint8_t vdp_mmio_shift(std::uint32_t offset) noexcept {
            return static_cast<std::uint8_t>(
                (mmio_vdp_register_stride - 1U -
                 ((offset - mmio_vdp_base) % mmio_vdp_register_stride)) *
                8U);
        }
    } // namespace

    m119_board_params board_params_for(std::string_view set_name) noexcept {
        if (set_name == "scumimon") {
            return {.main_clock_hz = main_clock_hz,
                    .ymz_clock_hz = ymz_clock_hz,
                    .rom_layout = "m119_scumimon_sh7708_upd94244_ymz280b_first_pass"};
        }
        return {};
    }

    m119_system::m119_system(common::rom_set_image image, m119_board_params board_params)
        : roms(std::move(image)), params(board_params) {
        auto& main_prog = pinned_region(roms, "maincpu", main_rom_size);
        auto& vdp_rom = pinned_region(roms, "vdp", vdp_rom_size, 0x00U);
        auto& ymz_rom = pinned_region(roms, "ymz", ymz_rom_size, 0x80U);

        main_bus.map_rom(main_rom_base, std::span<const std::uint8_t>(main_prog));
        main_bus.map_ram(work_ram_base, work_ram);
        main_bus.map_ram(video_ram_base, video.mutable_vram());
        main_bus.map_ram(nvram_base, nvram);
        main_bus.map_mmio(
            mmio_base, mmio_size,
            [this](std::uint32_t address) { return read_mmio(address - mmio_base); },
            [this](std::uint32_t address, std::uint8_t value) {
                write_mmio(address - mmio_base, value);
            });
        main_cpu.attach_bus(main_bus);
        main_cpu.reset(chips::reset_kind::power_on);

        video.attach_vdp_rom(vdp_rom);
        ymz.set_input_clock(params.ymz_clock_hz);
        ymz.set_sample_rom(ymz_rom);
        ymz.set_capture_divider(ymz_capture_divider);
        ymz.enable_audio_capture(true);
        prime_audio_preview();
    }

    void m119_system::prime_audio_preview() noexcept {
        const auto* samples = roms.region("ymz");
        if (samples == nullptr || samples->empty()) {
            return;
        }
        const std::uint32_t end =
            static_cast<std::uint32_t>(std::min<std::size_t>(samples->size(), 0x4000U));
        if (end > 1U) {
            ymz.key_channel(0U, 0U, end, 0x40U, true);
            ++ymz_key_on_count;
        }
    }

    std::uint8_t m119_system::read_mmio(std::uint32_t offset) noexcept {
        ++mmio_read_count;
        if (offset == mmio_input) {
            return input_latch;
        }
        if (offset == mmio_control) {
            return control_latch;
        }
        if (is_vdp_mmio(offset)) {
            const std::uint32_t value = video.read_register(vdp_mmio_register(offset));
            return static_cast<std::uint8_t>((value >> vdp_mmio_shift(offset)) & 0xFFU);
        }
        if (offset >= mmio_ymz_base && offset <= mmio_ymz_end) {
            return ymz.read_register(static_cast<std::uint8_t>(offset - mmio_ymz_base));
        }
        return 0xFFU;
    }

    void m119_system::write_mmio(std::uint32_t offset, std::uint8_t value) noexcept {
        ++mmio_write_count;
        if (offset == mmio_control) {
            control_latch = value;
            return;
        }
        if (is_vdp_mmio(offset)) {
            const std::uint8_t reg = vdp_mmio_register(offset);
            const std::uint8_t shift = vdp_mmio_shift(offset);
            const std::uint32_t lane_mask = 0xFFU << shift;
            const std::uint32_t previous = video.read_register(reg);
            video.write_register(reg, (previous & ~lane_mask) |
                                          (static_cast<std::uint32_t>(value) << shift));
            ++vdp_register_write_count;
            return;
        }
        if (offset >= mmio_ymz_base && offset <= mmio_ymz_end) {
            const auto reg = static_cast<std::uint8_t>(offset - mmio_ymz_base);
            ymz.write_register(reg, value);
            ++ymz_register_write_count;
            if ((reg % chips::audio::ymz280b::channel_register_count) ==
                    chips::audio::ymz280b::reg_control &&
                (value & chips::audio::ymz280b::control_key_on) != 0U) {
                ++ymz_key_on_count;
            }
        }
    }

    void m119_system::run_frame() {
        main_cpu.tick(first_pass_cpu_cycles_per_frame);
        ymz.tick(ymz_cycles_per_frame);
        video.tick(first_pass_cpu_cycles_per_frame);

        const auto* main_prog = roms.region("maincpu");
        const auto* vdp_rom = roms.region("vdp");
        const auto* ymz_rom = roms.region("ymz");
        video.attach_vdp_rom(vdp_rom != nullptr ? std::span<const std::uint8_t>(*vdp_rom)
                                                : std::span<const std::uint8_t>{});
        video.compose_diagnostic(main_prog != nullptr ? std::span<const std::uint8_t>(*main_prog)
                                                      : std::span<const std::uint8_t>{},
                                 ymz_rom != nullptr ? std::span<const std::uint8_t>(*ymz_rom)
                                                    : std::span<const std::uint8_t>{},
                                 work_ram, nvram, input_latch, control_latch);
        ++frames_run;
    }

    void m119_system::save_state(chips::state_writer& writer) const {
        writer.u32(m119_system_state_version);
        writer.u8(layout_code(params.rom_layout));
        writer.u32(params.main_clock_hz);
        writer.u32(params.ymz_clock_hz);
        writer.u32(board_identity_crc(roms, params));
        main_cpu.save_state(writer);
        video.save_state(writer);
        ymz.save_state(writer);
        writer.bytes(work_ram);
        writer.bytes(nvram);
        writer.u8(input_latch);
        writer.u8(control_latch);
        writer.u64(frames_run);
        writer.u64(mmio_read_count);
        writer.u64(mmio_write_count);
        writer.u64(vdp_register_write_count);
        writer.u64(ymz_register_write_count);
        writer.u64(ymz_key_on_count);
    }

    void m119_system::load_state(chips::state_reader& reader) {
        if (reader.u32() != m119_system_state_version) {
            reader.fail();
            return;
        }
        if (reader.u8() != layout_code(params.rom_layout) ||
            reader.u32() != params.main_clock_hz || reader.u32() != params.ymz_clock_hz ||
            reader.u32() != board_identity_crc(roms, params)) {
            reader.fail();
            return;
        }
        main_cpu.load_state(reader);
        video.load_state(reader);
        ymz.load_state(reader);
        reader.bytes(work_ram);
        reader.bytes(nvram);
        input_latch = reader.u8();
        control_latch = reader.u8();
        frames_run = reader.u64();
        mmio_read_count = reader.u64();
        mmio_write_count = reader.u64();
        vdp_register_write_count = reader.u64();
        ymz_register_write_count = reader.u64();
        ymz_key_on_count = reader.u64();
        if (reader.ok()) {
            ymz.enable_audio_capture(true);
            ymz.set_capture_divider(ymz_capture_divider);
        }
    }

    std::unique_ptr<m119_system> assemble_m119(common::rom_set_image image,
                                               m119_board_params board_params) {
        return std::make_unique<m119_system>(std::move(image), board_params);
    }

} // namespace mnemos::manifests::irem_m119
