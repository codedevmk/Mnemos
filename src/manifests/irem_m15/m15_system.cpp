#include "m15_system.hpp"

#include "crc32.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace mnemos::manifests::irem_m15 {

    namespace {
        inline constexpr std::uint8_t flag_c = 0x01U;
        inline constexpr std::uint8_t flag_z = 0x40U;
        inline constexpr std::uint8_t flag_s = 0x80U;

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

        [[nodiscard]] bool has_nonzero(std::span<const std::uint8_t> data) noexcept {
            return std::any_of(data.begin(), data.end(),
                               [](std::uint8_t value) { return value != 0U; });
        }

        [[nodiscard]] std::uint32_t rgb_from_luma(std::uint8_t luma,
                                                  std::uint8_t tint) noexcept {
            const std::uint32_t r = static_cast<std::uint32_t>((luma * 5U + tint) & 0xFFU);
            const std::uint32_t g =
                static_cast<std::uint32_t>(((luma << 1U) ^ (tint * 3U)) & 0xFFU);
            const std::uint32_t b =
                static_cast<std::uint32_t>(((luma >> 1U) + (tint * 9U)) & 0xFFU);
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

        [[nodiscard]] std::uint32_t crc32_u8(std::uint32_t crc, std::uint8_t value) noexcept {
            std::array<std::uint8_t, 1> bytes{value};
            return security::cryptography::crc32(
                std::span<const std::uint8_t>(bytes.data(), bytes.size()), crc);
        }

        [[nodiscard]] std::uint8_t layout_code(std::string_view layout) noexcept {
            if (layout == "head_on_i8080") {
                return 1U;
            }
            return 0U;
        }

        [[nodiscard]] std::uint8_t layout_tint(std::string_view layout) noexcept {
            return layout_code(layout) == 1U ? 0x2DU : 0x17U;
        }

        [[nodiscard]] std::uint32_t rom_identity_crc(const common::rom_set_image& roms,
                                                     const m15_board_params& params) {
            std::uint32_t crc =
                security::cryptography::crc32(std::string_view{"m15.rom.identity.v1"});
            crc = crc32_u8(crc, params.dip_default);
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

        [[nodiscard]] std::uint16_t make_word(std::uint8_t lo, std::uint8_t hi) noexcept {
            return static_cast<std::uint16_t>(lo | (hi << 8U));
        }
    } // namespace

    m15_board_params board_params_for(std::string_view set_name) noexcept {
        if (set_name == "headoni") {
            return {.cpu_clock_hz = cpu_clock_hz, .rom_layout = "head_on_i8080",
                    .dip_default = 0xFFU};
        }
        return {};
    }

    m15_i8080_cpu::m15_i8080_cpu() {
        introspection_.with_registers([this] { return register_snapshot(); })
            .with_trace(instrumentation::pc_trace_installer(
                trace_callback_, [this] { return elapsed_cycles(); }));
        reset(chips::reset_kind::power_on);
    }

    chips::chip_metadata m15_i8080_cpu::metadata() const noexcept {
        return {.manufacturer = "Intel",
                .part_number = "i8080_first_pass",
                .family = "Intel 8080",
                .klass = chips::chip_class::cpu,
                .revision = 1U};
    }

    void m15_i8080_cpu::attach_bus(chips::ibus& /*bus*/) noexcept {}

    void m15_i8080_cpu::set_memory(read_fn read, write_fn write) noexcept {
        read_ = std::move(read);
        write_ = std::move(write);
    }

    void m15_i8080_cpu::set_ports(port_in_fn input, port_out_fn output) noexcept {
        port_in_ = std::move(input);
        port_out_ = std::move(output);
    }

    std::uint8_t m15_i8080_cpu::read8(std::uint16_t address) const {
        return read_ ? read_(address) : 0xFFU;
    }

    void m15_i8080_cpu::write8(std::uint16_t address, std::uint8_t value) {
        if (write_) {
            write_(address, value);
        }
    }

    std::uint8_t m15_i8080_cpu::fetch8() {
        const std::uint8_t value = read8(pc_);
        pc_ = static_cast<std::uint16_t>(pc_ + 1U);
        return value;
    }

    std::uint16_t m15_i8080_cpu::fetch16() {
        const std::uint8_t lo = fetch8();
        const std::uint8_t hi = fetch8();
        return make_word(lo, hi);
    }

    std::uint8_t m15_i8080_cpu::reg_at(std::uint8_t index) const noexcept {
        switch (index & 7U) {
        case 0U:
            return b_;
        case 1U:
            return c_;
        case 2U:
            return d_;
        case 3U:
            return e_;
        case 4U:
            return h_;
        case 5U:
            return l_;
        case 7U:
            return a_;
        default:
            return read8(hl());
        }
    }

    void m15_i8080_cpu::set_reg_at(std::uint8_t index, std::uint8_t value) noexcept {
        switch (index & 7U) {
        case 0U:
            b_ = value;
            break;
        case 1U:
            c_ = value;
            break;
        case 2U:
            d_ = value;
            break;
        case 3U:
            e_ = value;
            break;
        case 4U:
            h_ = value;
            break;
        case 5U:
            l_ = value;
            break;
        case 7U:
            a_ = value;
            break;
        default:
            write8(hl(), value);
            break;
        }
    }

    void m15_i8080_cpu::set_zero_sign_flags(std::uint8_t value) noexcept {
        flags_ = static_cast<std::uint8_t>(flags_ & flag_c);
        if (value == 0U) {
            flags_ |= flag_z;
        }
        if ((value & 0x80U) != 0U) {
            flags_ |= flag_s;
        }
    }

    int m15_i8080_cpu::step_instruction() {
        if (halted_) {
            return 4;
        }

        const std::uint16_t op_pc = pc_;
        if (trace_callback_) {
            trace_callback_(op_pc);
        }
        const std::uint8_t op = fetch8();

        if ((op & 0xC0U) == 0x40U && op != 0x76U) {
            set_reg_at(static_cast<std::uint8_t>((op >> 3U) & 7U),
                       reg_at(static_cast<std::uint8_t>(op & 7U)));
            return ((op & 7U) == 6U || ((op >> 3U) & 7U) == 6U) ? 7 : 5;
        }

        switch (op) {
        case 0x00U: // NOP
            return 4;
        case 0x06U:
        case 0x0EU:
        case 0x16U:
        case 0x1EU:
        case 0x26U:
        case 0x2EU:
        case 0x36U:
        case 0x3EU:
            set_reg_at(static_cast<std::uint8_t>((op >> 3U) & 7U), fetch8());
            return (op == 0x36U) ? 10 : 7;
        case 0x01U:
            c_ = fetch8();
            b_ = fetch8();
            return 10;
        case 0x11U:
            e_ = fetch8();
            d_ = fetch8();
            return 10;
        case 0x21U:
            set_hl(fetch16());
            return 10;
        case 0x31U:
            sp_ = fetch16();
            return 10;
        case 0x23U:
            set_hl(static_cast<std::uint16_t>(hl() + 1U));
            return 5;
        case 0x32U:
            write8(fetch16(), a_);
            return 13;
        case 0x3AU:
            a_ = read8(fetch16());
            set_zero_sign_flags(a_);
            return 13;
        case 0x3CU:
            a_ = static_cast<std::uint8_t>(a_ + 1U);
            set_zero_sign_flags(a_);
            return 5;
        case 0x3DU:
            a_ = static_cast<std::uint8_t>(a_ - 1U);
            set_zero_sign_flags(a_);
            return 5;
        case 0x76U:
            halted_ = true;
            return 7;
        case 0xAFU:
            a_ = 0U;
            flags_ = flag_z;
            return 4;
        case 0xB7U:
            set_zero_sign_flags(a_);
            return 4;
        case 0xC2U: {
            const std::uint16_t target = fetch16();
            if ((flags_ & flag_z) == 0U) {
                pc_ = target;
            }
            return 10;
        }
        case 0xC3U:
            pc_ = fetch16();
            return 10;
        case 0xCAU: {
            const std::uint16_t target = fetch16();
            if ((flags_ & flag_z) != 0U) {
                pc_ = target;
            }
            return 10;
        }
        case 0xC6U: {
            const std::uint16_t sum = static_cast<std::uint16_t>(a_) + fetch8();
            a_ = static_cast<std::uint8_t>(sum);
            flags_ = sum > 0xFFU ? flag_c : 0U;
            set_zero_sign_flags(a_);
            return 7;
        }
        case 0xCDU: {
            const std::uint16_t target = fetch16();
            sp_ = static_cast<std::uint16_t>(sp_ - 2U);
            write8(sp_, static_cast<std::uint8_t>(pc_));
            write8(static_cast<std::uint16_t>(sp_ + 1U), static_cast<std::uint8_t>(pc_ >> 8U));
            pc_ = target;
            return 17;
        }
        case 0xC9U:
            pc_ = make_word(read8(sp_), read8(static_cast<std::uint16_t>(sp_ + 1U)));
            sp_ = static_cast<std::uint16_t>(sp_ + 2U);
            return 10;
        case 0xD3U: {
            const std::uint8_t port = fetch8();
            if (port_out_) {
                port_out_(port, a_);
            }
            return 10;
        }
        case 0xDBU: {
            const std::uint8_t port = fetch8();
            a_ = port_in_ ? port_in_(port) : 0xFFU;
            set_zero_sign_flags(a_);
            return 10;
        }
        case 0xD6U: {
            const std::uint8_t rhs = fetch8();
            const std::uint16_t diff = static_cast<std::uint16_t>(a_ - rhs);
            a_ = static_cast<std::uint8_t>(diff);
            flags_ = diff > 0xFFU ? flag_c : 0U;
            set_zero_sign_flags(a_);
            return 7;
        }
        case 0xF3U:
        case 0xFBU:
            return 4;
        case 0xFEU: {
            const std::uint8_t rhs = fetch8();
            const std::uint16_t diff = static_cast<std::uint16_t>(a_ - rhs);
            std::uint8_t flags = diff > 0xFFU ? flag_c : 0U;
            if (static_cast<std::uint8_t>(diff) == 0U) {
                flags |= flag_z;
            }
            if ((diff & 0x80U) != 0U) {
                flags |= flag_s;
            }
            flags_ = flags;
            return 7;
        }
        default:
            if ((op & 0xF8U) == 0x80U) {
                const std::uint16_t sum =
                    static_cast<std::uint16_t>(a_) + reg_at(static_cast<std::uint8_t>(op & 7U));
                a_ = static_cast<std::uint8_t>(sum);
                flags_ = sum > 0xFFU ? flag_c : 0U;
                set_zero_sign_flags(a_);
                return (op & 7U) == 6U ? 7 : 4;
            }
            if ((op & 0xF8U) == 0x90U) {
                const std::uint16_t diff =
                    static_cast<std::uint16_t>(a_ - reg_at(static_cast<std::uint8_t>(op & 7U)));
                a_ = static_cast<std::uint8_t>(diff);
                flags_ = diff > 0xFFU ? flag_c : 0U;
                set_zero_sign_flags(a_);
                return (op & 7U) == 6U ? 7 : 4;
            }
            if ((op & 0xF8U) == 0xA0U) {
                a_ = static_cast<std::uint8_t>(a_ & reg_at(static_cast<std::uint8_t>(op & 7U)));
                flags_ = 0U;
                set_zero_sign_flags(a_);
                return (op & 7U) == 6U ? 7 : 4;
            }
            if ((op & 0xF8U) == 0xB0U) {
                a_ = static_cast<std::uint8_t>(a_ | reg_at(static_cast<std::uint8_t>(op & 7U)));
                flags_ = 0U;
                set_zero_sign_flags(a_);
                return (op & 7U) == 6U ? 7 : 4;
            }
            ++unsupported_opcodes_;
            return 4;
        }
    }

    void m15_i8080_cpu::tick(std::uint64_t cycles) {
        std::uint64_t consumed = 0U;
        while (consumed < cycles) {
            const int step = step_instruction();
            consumed += static_cast<std::uint64_t>(std::max(step, 1));
        }
        elapsed_cycles_ += consumed;
    }

    void m15_i8080_cpu::reset(chips::reset_kind /*kind*/) {
        a_ = 0U;
        b_ = 0U;
        c_ = 0U;
        d_ = 0U;
        e_ = 0U;
        h_ = 0U;
        l_ = 0U;
        flags_ = 0U;
        pc_ = 0U;
        sp_ = 0x2400U;
        elapsed_cycles_ = 0U;
        unsupported_opcodes_ = 0U;
        halted_ = false;
    }

    void m15_i8080_cpu::save_state(chips::state_writer& writer) const {
        writer.u32(1U);
        writer.u8(a_);
        writer.u8(b_);
        writer.u8(c_);
        writer.u8(d_);
        writer.u8(e_);
        writer.u8(h_);
        writer.u8(l_);
        writer.u8(flags_);
        writer.u16(pc_);
        writer.u16(sp_);
        writer.u64(elapsed_cycles_);
        writer.u32(unsupported_opcodes_);
        writer.boolean(halted_);
    }

    void m15_i8080_cpu::load_state(chips::state_reader& reader) {
        if (reader.u32() != 1U) {
            reader.fail();
            return;
        }
        a_ = reader.u8();
        b_ = reader.u8();
        c_ = reader.u8();
        d_ = reader.u8();
        e_ = reader.u8();
        h_ = reader.u8();
        l_ = reader.u8();
        flags_ = reader.u8();
        pc_ = reader.u16();
        sp_ = reader.u16();
        elapsed_cycles_ = reader.u64();
        unsupported_opcodes_ = reader.u32();
        halted_ = reader.boolean();
    }

    std::span<const chips::register_descriptor> m15_i8080_cpu::register_snapshot() noexcept {
        using fmt = chips::register_value_format;
        register_view_[0] = {"A", a_, 8U, fmt::unsigned_integer};
        register_view_[1] = {"B", b_, 8U, fmt::unsigned_integer};
        register_view_[2] = {"C", c_, 8U, fmt::unsigned_integer};
        register_view_[3] = {"D", d_, 8U, fmt::unsigned_integer};
        register_view_[4] = {"E", e_, 8U, fmt::unsigned_integer};
        register_view_[5] = {"H", h_, 8U, fmt::unsigned_integer};
        register_view_[6] = {"L", l_, 8U, fmt::unsigned_integer};
        register_view_[7] = {"F", flags_, 8U, fmt::flags};
        register_view_[8] = {"PC", pc_, 16U, fmt::unsigned_integer};
        register_view_[9] = {"SP", sp_, 16U, fmt::unsigned_integer};
        register_view_[10] = {"HALT", halted_ ? 1U : 0U, 1U, fmt::flags};
        register_view_[11] = {"UNSUPPORTED", unsupported_opcodes_, 32U,
                              fmt::unsigned_integer};
        return register_view_;
    }

    m15_video::m15_video()
        : pixels_(static_cast<std::size_t>(visible_width) * visible_height, 0U) {
        reset(chips::reset_kind::power_on);
    }

    chips::chip_metadata m15_video::metadata() const noexcept {
        return {.manufacturer = "Irem",
                .part_number = "m15_video_first_pass",
                .family = "irem_m15",
                .klass = chips::chip_class::video,
                .revision = 1U};
    }

    void m15_video::tick(std::uint64_t cycles) { elapsed_cycles_ += cycles; }

    void m15_video::reset(chips::reset_kind /*kind*/) {
        std::fill(pixels_.begin(), pixels_.end(), 0U);
        elapsed_cycles_ = 0U;
        frame_index_ = 0U;
    }

    chips::frame_buffer_view m15_video::framebuffer() const noexcept {
        return {.pixels = pixels_.data(),
                .width = visible_width,
                .height = visible_height,
                .stride = visible_width};
    }

    void m15_video::compose(std::span<const std::uint8_t> program_rom,
                            std::span<const std::uint8_t> video_ram,
                            std::span<const std::uint8_t> color_ram,
                            std::span<const std::uint8_t> work_ram, std::uint8_t control,
                            std::string_view rom_layout) {
        const bool video_ready = has_nonzero(video_ram);
        const std::uint8_t tint = static_cast<std::uint8_t>(layout_tint(rom_layout) ^ control);
        for (std::uint32_t y = 0; y < visible_height; ++y) {
            for (std::uint32_t x = 0; x < visible_width; ++x) {
                const std::uint64_t linear =
                    static_cast<std::uint64_t>(y) * visible_width + x + frame_index_;
                const std::uint8_t src = video_ready
                                             ? sample_byte(video_ram, linear >> 3U, 0x00U)
                                             : sample_byte(program_rom, linear >> 2U, 0x5AU);
                const std::uint8_t bit =
                    static_cast<std::uint8_t>((src >> (7U - (x & 7U))) & 1U);
                const std::uint8_t color =
                    sample_byte(color_ram, (linear >> 3U) + y, tint);
                const std::uint8_t work =
                    sample_byte(work_ram, (linear >> 4U) + (x * 3U), 0x21U);
                const std::uint8_t luma = static_cast<std::uint8_t>(
                    (bit != 0U ? 0xD0U : 0x20U) ^ color ^ work ^ tint);
                pixels_[static_cast<std::size_t>(y) * visible_width + x] =
                    rgb_from_luma(luma, tint);
            }
        }
        ++frame_index_;
    }

    void m15_video::save_state(chips::state_writer& writer) const {
        writer.u64(elapsed_cycles_);
        writer.u64(frame_index_);
        for (const std::uint32_t pixel : pixels_) {
            writer.u32(pixel);
        }
    }

    void m15_video::load_state(chips::state_reader& reader) {
        elapsed_cycles_ = reader.u64();
        frame_index_ = reader.u64();
        for (std::uint32_t& pixel : pixels_) {
            pixel = reader.u32();
        }
    }

    m15_system::m15_system(common::rom_set_image image, m15_board_params board_params)
        : roms(std::move(image)), params(board_params) {
        (void)pinned_region(roms, "maincpu", main_rom_size);
        dip_switches = params.dip_default;

        main_cpu.set_memory(
            [this](std::uint16_t address) -> std::uint8_t {
                const auto* main_prog = roms.region("maincpu");
                if (main_prog != nullptr && address < main_prog->size()) {
                    return (*main_prog)[address];
                }
                if (address >= work_ram_base &&
                    address < work_ram_base + static_cast<std::uint16_t>(work_ram.size())) {
                    return work_ram[address - work_ram_base];
                }
                if (address >= video_ram_base &&
                    address < video_ram_base + static_cast<std::uint16_t>(video_ram.size())) {
                    return video_ram[address - video_ram_base];
                }
                if (address >= color_ram_base &&
                    address < color_ram_base + static_cast<std::uint16_t>(color_ram.size())) {
                    return color_ram[address - color_ram_base];
                }
                return 0xFFU;
            },
            [this](std::uint16_t address, std::uint8_t value) {
                if (address >= work_ram_base &&
                    address < work_ram_base + static_cast<std::uint16_t>(work_ram.size())) {
                    work_ram[address - work_ram_base] = value;
                } else if (address >= video_ram_base &&
                           address <
                               video_ram_base + static_cast<std::uint16_t>(video_ram.size())) {
                    video_ram[address - video_ram_base] = value;
                } else if (address >= color_ram_base &&
                           address <
                               color_ram_base + static_cast<std::uint16_t>(color_ram.size())) {
                    color_ram[address - color_ram_base] = value;
                }
            });

        main_cpu.set_ports(
            [this](std::uint16_t port) -> std::uint8_t {
                switch (port & 0x03U) {
                case port_in_p1:
                    return input_p1;
                case port_in_p2:
                    return input_p2;
                case port_in_system:
                    return input_system;
                case port_in_dip:
                    return dip_switches;
                default:
                    return 0xFFU;
                }
            },
            [this](std::uint16_t port, std::uint8_t value) {
                switch (port & 0x03U) {
                case port_out_speaker:
                    speaker_latch = value;
                    speaker.set_speaker((value & 0x01U) != 0U);
                    break;
                case port_out_control:
                    control_register = value;
                    break;
                default:
                    break;
                }
            });

        speaker.set_clock(params.cpu_clock_hz, audio_rate_hz);
        speaker.enable_audio_capture(true);
    }

    void m15_system::run_frame() {
        main_cpu.tick(cpu_cycles_per_frame);
        speaker.tick(cpu_cycles_per_frame);
        const auto* main_prog = roms.region("maincpu");
        video.compose(main_prog != nullptr ? std::span<const std::uint8_t>(*main_prog)
                                           : std::span<const std::uint8_t>{},
                      video_ram, color_ram, work_ram, control_register, params.rom_layout);
    }

    void m15_system::set_inputs(std::uint8_t p1, std::uint8_t p2,
                                std::uint8_t system) noexcept {
        input_p1 = p1;
        input_p2 = p2;
        input_system = system;
    }

    void m15_system::save_state(chips::state_writer& writer) const {
        writer.u32(m15_system_state_version);
        writer.u8(params.dip_default);
        writer.u8(layout_code(params.rom_layout));
        writer.u32(rom_identity_crc(roms, params));
        main_cpu.save_state(writer);
        video.save_state(writer);
        speaker.save_state(writer);
        writer.bytes(work_ram);
        writer.bytes(video_ram);
        writer.bytes(color_ram);
        writer.u8(input_p1);
        writer.u8(input_p2);
        writer.u8(input_system);
        writer.u8(dip_switches);
        writer.u8(control_register);
        writer.u8(speaker_latch);
    }

    void m15_system::load_state(chips::state_reader& reader) {
        if (reader.u32() != m15_system_state_version) {
            reader.fail();
            return;
        }
        const std::uint8_t saved_dip = reader.u8();
        const std::uint8_t saved_layout = reader.u8();
        const std::uint32_t saved_identity = reader.u32();
        if (saved_dip != params.dip_default || saved_layout != layout_code(params.rom_layout) ||
            saved_identity != rom_identity_crc(roms, params)) {
            reader.fail();
            return;
        }
        main_cpu.load_state(reader);
        video.load_state(reader);
        speaker.load_state(reader);
        reader.bytes(work_ram);
        reader.bytes(video_ram);
        reader.bytes(color_ram);
        input_p1 = reader.u8();
        input_p2 = reader.u8();
        input_system = reader.u8();
        dip_switches = reader.u8();
        control_register = reader.u8();
        speaker_latch = reader.u8();
        if (reader.ok()) {
            speaker.set_speaker((speaker_latch & 0x01U) != 0U);
        }
    }

    std::unique_ptr<m15_system> assemble_m15(common::rom_set_image image,
                                             m15_board_params board_params) {
        return std::make_unique<m15_system>(std::move(image), board_params);
    }

} // namespace mnemos::manifests::irem_m15
