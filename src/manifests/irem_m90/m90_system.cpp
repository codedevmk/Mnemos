#include "m90_system.hpp"

#include "crc32.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace mnemos::manifests::irem_m90 {

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
            if (layout == "bbman_program_pair") {
                return 1U;
            }
            if (layout == "bbman2_program_pair") {
                return 2U;
            }
            if (layout == "bbman2_audio_sample") {
                return 3U;
            }
            if (layout == "bbman2_audio_sample_alt") {
                return 4U;
            }
            if (layout == "bbmanw_complete") {
                return 5U;
            }
            if (layout == "hasamu_complete") {
                return 6U;
            }
            if (layout == "quizf1_banked") {
                return 7U;
            }
            if (layout == "riskchal_complete") {
                return 8U;
            }
            if (layout == "gussun_parented") {
                return 9U;
            }
            return 0U;
        }

        [[nodiscard]] std::uint8_t layout_tint(std::string_view layout) noexcept {
            switch (layout_code(layout)) {
            case 1U:
                return 0x39U;
            case 2U:
                return 0x55U;
            case 3U:
                return 0x6BU;
            case 4U:
                return 0x7DU;
            case 5U:
                return 0x91U;
            case 6U:
                return 0xA7U;
            case 7U:
                return 0xB3U;
            case 8U:
                return 0xC5U;
            case 9U:
                return 0xD9U;
            default:
                return 0x27U;
            }
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

        [[nodiscard]] std::uint32_t board_identity_crc(const common::rom_set_image& roms,
                                                       const m90_board_params& params,
                                                       const chips::cpu::v30& main_cpu) {
            std::uint32_t crc =
                security::cryptography::crc32(std::string_view{"m90.board.identity.v1"});
            crc = crc32_u16(crc, params.dip_default);
            crc = crc32_u8(crc, layout_code(params.rom_layout));
            crc = crc32_u8(crc, cpu_model_code(main_cpu.cpu_model()));
            crc = crc32_u32(crc, main_clock_hz);
            crc = crc32_u32(crc, sound_clock_hz);
            crc = crc32_u32(crc, fm_clock_hz);
            crc = crc32_u32(crc, dac_clock_hz);
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

    m90_board_params board_params_for(std::string_view set_name) noexcept {
        if (set_name == "atompunk") {
            return {.dip_default = 0xFFFFU, .rom_layout = "bbman_program_pair"};
        }
        if (set_name == "newapunk") {
            return {.dip_default = 0xFFFFU, .rom_layout = "bbman2_program_pair"};
        }
        if (set_name == "bbmanw") {
            return {.dip_default = 0xFFFFU, .rom_layout = "bbmanw_complete"};
        }
        if (set_name == "bbmanwj") {
            return {.dip_default = 0xFFFFU, .rom_layout = "bbman2_audio_sample"};
        }
        if (set_name == "bbmanwja") {
            return {.dip_default = 0xFFFFU, .rom_layout = "bbman2_audio_sample_alt"};
        }
        if (set_name == "hasamu") {
            return {.dip_default = 0xFFFFU, .rom_layout = "hasamu_complete"};
        }
        if (set_name == "quizf1") {
            return {.dip_default = 0xFFFFU, .rom_layout = "quizf1_banked"};
        }
        if (set_name == "riskchal") {
            return {.dip_default = 0xFFFFU, .rom_layout = "riskchal_complete"};
        }
        if (set_name == "gussun") {
            return {.dip_default = 0xFFFFU, .rom_layout = "gussun_parented"};
        }
        return {};
    }

    m90_video::m90_video() : pixels_(static_cast<std::size_t>(visible_width) * visible_height, 0U) {
        reset(chips::reset_kind::power_on);
    }

    chips::chip_metadata m90_video::metadata() const noexcept {
        return {.manufacturer = "Irem",
                .part_number = "GA25_first_pass",
                .family = "irem_m90",
                .klass = chips::chip_class::video,
                .revision = 1U};
    }

    void m90_video::tick(std::uint64_t cycles) { elapsed_cycles_ += cycles; }

    void m90_video::reset(chips::reset_kind /*kind*/) {
        std::fill(pixels_.begin(), pixels_.end(), 0U);
        elapsed_cycles_ = 0U;
        frame_index_ = 0U;
    }

    chips::frame_buffer_view m90_video::framebuffer() const noexcept {
        return {.pixels = pixels_.data(),
                .width = visible_width,
                .height = visible_height,
                .stride = visible_width};
    }

    void m90_video::compose(std::span<const std::uint8_t> main_program,
                            std::span<const std::uint8_t> sound_program,
                            std::span<const std::uint8_t> samples,
                            std::span<const std::uint8_t> graphics,
                            std::span<const std::uint8_t> vram,
                            std::span<const std::uint8_t> rowscroll,
                            std::span<const std::uint8_t> palette,
                            std::span<const std::uint8_t> sprite_ram, std::string_view rom_layout) {
        const std::uint8_t tint = layout_tint(rom_layout);
        for (std::uint32_t y = 0; y < visible_height; ++y) {
            const std::uint16_t row_scroll =
                rowscroll.empty()
                    ? 0U
                    : static_cast<std::uint16_t>(
                          rowscroll[(static_cast<std::size_t>(y) * 2U) % rowscroll.size()] |
                          (rowscroll[(static_cast<std::size_t>(y) * 2U + 1U) % rowscroll.size()]
                           << 8U));
            for (std::uint32_t x = 0; x < visible_width; ++x) {
                const std::uint64_t linear =
                    static_cast<std::uint64_t>(y) * visible_width + x + frame_index_;
                const std::uint8_t main = sample_byte(main_program, linear + row_scroll, 0x5AU);
                const std::uint8_t snd =
                    sample_byte(sound_program, (linear >> 2U) + (x >> 3U), 0x33U);
                const std::uint8_t smp = sample_byte(samples, (linear >> 1U) + tint, 0x11U);
                const std::uint8_t gfx = sample_byte(graphics, (linear << 1U) + (x & 0x1FU), 0x22U);
                const std::uint8_t vr = sample_byte(vram, linear + (x ^ y), 0x00U);
                const std::uint8_t sp = sample_byte(sprite_ram, (linear >> 2U) + y, 0x00U);
                const std::uint8_t pal = sample_byte(palette, (main ^ snd ^ smp ^ gfx) * 2U, tint);
                const std::uint8_t mixed = static_cast<std::uint8_t>(
                    main ^ static_cast<std::uint8_t>(snd << 1U) ^ smp ^
                    static_cast<std::uint8_t>(gfx << 2U) ^ vr ^ sp ^ pal ^ tint);
                pixels_[static_cast<std::size_t>(y) * visible_width + x] =
                    rgb_from_byte(mixed, tint);
            }
        }
        ++frame_index_;
    }

    void m90_video::save_state(chips::state_writer& writer) const {
        writer.u64(elapsed_cycles_);
        writer.u64(frame_index_);
        for (const std::uint32_t pixel : pixels_) {
            writer.u32(pixel);
        }
    }

    void m90_video::load_state(chips::state_reader& reader) {
        elapsed_cycles_ = reader.u64();
        frame_index_ = reader.u64();
        for (std::uint32_t& pixel : pixels_) {
            pixel = reader.u32();
        }
    }

    m90_system::m90_system(common::rom_set_image image, m90_board_params board_params)
        : roms(std::move(image)), params(board_params) {
        dip_switches = params.dip_default;

        auto& main_prog = pinned_region(roms, "maincpu", main_rom_size);
        auto& sound_prog = pinned_region(roms, "soundcpu", sound_rom_size);
        (void)pinned_region(roms, "samples", sample_rom_size);

        main_cpu.set_model(chips::cpu::v30::model::v35);

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
            switch (port & 0xFFU) {
            case port_out_sound_latch:
                sound_latch = value;
                sound_latch_irq = true;
                update_sound_irq();
                break;
            case port_out_control:
                control_register = value;
                sound_cpu.set_reset_line((value & 0x10U) == 0U);
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

        main_cpu.reset(chips::reset_kind::power_on);
        sound_cpu.reset(chips::reset_kind::power_on);
        sound_cpu.set_reset_line(false);
    }

    void m90_system::run_frame() {
        main_cpu.tick(main_cycles_per_frame);
        if (!sound_cpu.reset_line_held()) {
            sound_cpu.tick(sound_cycles_per_frame);
        }
        fm.tick(fm_cycles_per_frame);
        dac.tick(dac_cycles_per_frame);

        const auto* main_program = roms.region("maincpu");
        const auto* sound_program = roms.region("soundcpu");
        const auto* samples = roms.region("samples");
        const auto* graphics = roms.region("graphics");
        video.compose(main_program != nullptr ? std::span<const std::uint8_t>(*main_program)
                                              : std::span<const std::uint8_t>{},
                      sound_program != nullptr ? std::span<const std::uint8_t>(*sound_program)
                                               : std::span<const std::uint8_t>{},
                      samples != nullptr ? std::span<const std::uint8_t>(*samples)
                                         : std::span<const std::uint8_t>{},
                      graphics != nullptr ? std::span<const std::uint8_t>(*graphics)
                                          : std::span<const std::uint8_t>{},
                      vram, rowscroll_ram, palette_ram, sprite_ram, params.rom_layout);
    }

    void m90_system::set_inputs(std::uint8_t p1, std::uint8_t p2, std::uint8_t system) noexcept {
        input_p1 = p1;
        input_p2 = p2;
        input_system = system;
    }

    void m90_system::update_sound_irq() noexcept {
        sound_cpu.set_irq_line(sound_latch_irq || fm.irq_asserted());
    }

    void m90_system::record_dac_write(std::uint8_t value) {
        dac.write(value);
        const dac_write_event event{.sound_clock = sound_cpu.elapsed_cycles(),
                                    .output = dac.output()};
        if (!dac_write_events.empty() && dac_write_events.back().sound_clock == event.sound_clock) {
            dac_write_events.back() = event;
            return;
        }
        dac_write_events.push_back(event);
    }

    void m90_system::discard_dac_write_events_before(std::uint64_t sound_clock) {
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

    void m90_system::save_state(chips::state_writer& writer) const {
        writer.u32(m90_system_state_version);
        writer.u16(params.dip_default);
        writer.u8(layout_code(params.rom_layout));
        writer.u32(board_identity_crc(roms, params, main_cpu));

        main_cpu.save_state(writer);
        sound_cpu.save_state(writer);
        video.save_state(writer);
        fm.save_state(writer);
        dac.save_state(writer);

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
        writer.boolean(sound_latch_irq);
        writer.u32(sample_address);
        writer.u32(static_cast<std::uint32_t>(dac_write_events.size()));
        for (const auto& event : dac_write_events) {
            writer.u64(event.sound_clock);
            writer.u16(static_cast<std::uint16_t>(static_cast<std::int32_t>(event.output) + 32768));
        }
    }

    void m90_system::load_state(chips::state_reader& reader) {
        if (reader.u32() != m90_system_state_version) {
            reader.fail();
            return;
        }
        const std::uint16_t saved_dip = reader.u16();
        const std::uint8_t saved_layout = reader.u8();
        const std::uint32_t saved_identity = reader.u32();
        if (saved_dip != params.dip_default || saved_layout != layout_code(params.rom_layout) ||
            saved_identity != board_identity_crc(roms, params, main_cpu)) {
            reader.fail();
            return;
        }

        main_cpu.load_state(reader);
        sound_cpu.load_state(reader);
        video.load_state(reader);
        fm.load_state(reader);
        dac.load_state(reader);

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
            sound_cpu.set_reset_line((control_register & 0x10U) == 0U);
            update_sound_irq();
        }
    }

    std::unique_ptr<m90_system> assemble_m90(common::rom_set_image image,
                                             m90_board_params board_params) {
        return std::make_unique<m90_system>(std::move(image), board_params);
    }

} // namespace mnemos::manifests::irem_m90
