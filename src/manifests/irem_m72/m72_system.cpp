#include "m72_system.hpp"

#include "crc32.hpp"

#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <utility>

namespace mnemos::manifests::irem_m72 {

    namespace {
        // DSW low byte is SW1 and high byte is SW2, matching the V30 reads at
        // ports 0x04/0x05.
        inline constexpr std::uint16_t dip_rtype = 0xFDFBU;
        inline constexpr std::uint16_t dip_rtype_prototype = 0xFDFFU;
        inline constexpr std::uint16_t dip_bchopper = 0xFDFFU;
        inline constexpr std::uint16_t dip_nspirit_imgfight_dbreed = 0xF5FFU;
        inline constexpr std::uint16_t dip_loht = 0xFDFBU;
        inline constexpr std::uint16_t dip_xmultipl = 0xFDFFU;
        inline constexpr std::uint16_t dip_hharry = 0xFDBFU;
        inline constexpr std::uint16_t dip_airduel = 0xFFBFU;
        inline constexpr std::uint16_t dip_gallop = 0xF9BFU;
        inline constexpr std::uint32_t max_saved_dac_write_events = 1U << 20U;

        // Return the named region's bytes, padded/created at `size` so the bus
        // map below always has full-size backing (absent dumps read 0xFF).
        [[nodiscard]] std::vector<std::uint8_t>&
        pinned_region(common::rom_set_image& image, const std::string& name, std::size_t size) {
            auto& bytes = image.regions[name];
            if (bytes.size() < size) {
                bytes.resize(size, 0xFFU);
            }
            return bytes;
        }

        // The no-dump profiles expose only board-facing sample trigger data.
        // Protection entry routines remain artifact-gated; no MCU code is
        // synthesized here.
        inline constexpr std::array<std::uint32_t, 9> dbreedm72_sample_starts{
            0x00000U, 0x00020U, 0x02C40U, 0x08160U, 0x0C8C0U,
            0x0FFE0U, 0x13000U, 0x15820U, 0x15F40U,
        };

        inline constexpr std::array<std::uint32_t, 28> dkgensanm72_sample_starts{
            0x00000U, 0x00020U, 0x01800U, 0x02DA0U, 0x03BE0U, 0x05AE0U, 0x06100U,
            0x06DE0U, 0x07260U, 0x07A60U, 0x08720U, 0x0A5C0U, 0x0C3C0U, 0x0C7A0U,
            0x0E140U, 0x0FB00U, 0x10FA0U, 0x10FC0U, 0x10FE0U, 0x11F40U, 0x12B20U,
            0x130A0U, 0x13C60U, 0x14740U, 0x153C0U, 0x197E0U, 0x1AF40U, 0x1C080U,
        };

        template <std::size_t Count>
        [[nodiscard]] std::optional<std::uint32_t>
        bounded_sample_start(const std::array<std::uint32_t, Count>& starts,
                             std::span<const std::uint8_t> samples,
                             std::uint8_t trigger) noexcept {
            if (trigger >= starts.size()) {
                return std::nullopt;
            }
            const std::uint32_t start = starts[trigger];
            if (start >= samples.size()) {
                return std::nullopt;
            }
            return start;
        }

        [[nodiscard]] std::optional<std::uint32_t>
        no_dump_hle_sample_start(std::string_view profile,
                                 std::span<const std::uint8_t> samples,
                                 std::uint8_t trigger) noexcept {
            if (samples.empty()) {
                return std::nullopt;
            }
            if (profile == "irem_m72.dbreedm72_no_dump_mcu") {
                return bounded_sample_start(dbreedm72_sample_starts, samples, trigger);
            }
            if (profile == "irem_m72.dkgensanm72_no_dump_mcu") {
                return bounded_sample_start(dkgensanm72_sample_starts, samples, trigger);
            }
            return std::nullopt;
        }

        [[nodiscard]] std::uint32_t
        crc32_u64(std::uint32_t crc, std::uint64_t value) noexcept {
            std::array<std::uint8_t, 8> bytes{};
            for (std::size_t i = 0; i < bytes.size(); ++i) {
                bytes[i] = static_cast<std::uint8_t>((value >> (i * 8U)) & 0xFFU);
            }
            return security::cryptography::crc32(
                std::span<const std::uint8_t>(bytes.data(), bytes.size()), crc);
        }

        [[nodiscard]] std::uint32_t
        crc32_u32(std::uint32_t crc, std::uint32_t value) noexcept {
            std::array<std::uint8_t, 4> bytes{};
            for (std::size_t i = 0; i < bytes.size(); ++i) {
                bytes[i] = static_cast<std::uint8_t>((value >> (i * 8U)) & 0xFFU);
            }
            return security::cryptography::crc32(
                std::span<const std::uint8_t>(bytes.data(), bytes.size()), crc);
        }

        [[nodiscard]] std::uint32_t
        crc32_u16(std::uint32_t crc, std::uint16_t value) noexcept {
            std::array<std::uint8_t, 2> bytes{};
            for (std::size_t i = 0; i < bytes.size(); ++i) {
                bytes[i] = static_cast<std::uint8_t>((value >> (i * 8U)) & 0xFFU);
            }
            return security::cryptography::crc32(
                std::span<const std::uint8_t>(bytes.data(), bytes.size()), crc);
        }

        [[nodiscard]] std::uint32_t
        crc32_u8(std::uint32_t crc, std::uint8_t value) noexcept {
            std::array<std::uint8_t, 1> bytes{value};
            return security::cryptography::crc32(
                std::span<const std::uint8_t>(bytes.data(), bytes.size()), crc);
        }

        [[nodiscard]] std::uint32_t
        rom_identity_crc(const common::rom_set_image& roms,
                         const m72_board_params& params) {
            std::uint32_t crc =
                security::cryptography::crc32(std::string_view{"m72.rom.identity.v1"});
            crc = crc32_u32(crc, params.work_ram_base);
            crc = crc32_u16(crc, params.dip_default);
            crc = crc32_u8(crc, static_cast<std::uint8_t>(
                                    params.protection_hle_profile.has_value() ? 1U : 0U));
            if (params.protection_hle_profile.has_value()) {
                crc = crc32_u64(crc, params.protection_hle_profile->size());
                crc = security::cryptography::crc32(*params.protection_hle_profile, crc);
            }

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

    bool supported_protection_hle_profile(std::string_view profile) noexcept {
        return profile == "irem_m72.dbreedm72_no_dump_mcu" ||
               profile == "irem_m72.dkgensanm72_no_dump_mcu";
    }

    m72_board_params board_params_for(std::string_view set_name) {
        // Work-RAM wiring + factory DIP defaults per declared set. Only true
        // M72-family maps are listed here; M81/M82/M84/M85/M92 need their own
        // board profiles because their video, palette, IRQ, or CPU maps differ.
        if (set_name == "rtype" || set_name == "rtypej" || set_name == "rtypeu" ||
            set_name == "rtypeb") {
            return {.work_ram_base = 0x40000U, .dip_default = dip_rtype};
        }
        if (set_name == "rtypejp") {
            return {.work_ram_base = 0x40000U, .dip_default = dip_rtype_prototype};
        }
        if (set_name == "xmultiplm72") {
            return {.work_ram_base = 0x80000U, .dip_default = dip_xmultipl};
        }
        if (set_name == "dbreedm72" || set_name == "dbreedjm72") {
            return {.work_ram_base = 0x90000U, .dip_default = dip_nspirit_imgfight_dbreed};
        }
        if (set_name == "bchopper" || set_name == "mrheli") {
            return {.work_ram_base = 0xA0000U, .dip_default = dip_bchopper};
        }
        if (set_name == "nspirit" || set_name == "nspiritj" || set_name == "imgfight" ||
            set_name == "imgfightj" || set_name == "imgfightjb") {
            return {.work_ram_base = 0xA0000U, .dip_default = dip_nspirit_imgfight_dbreed};
        }
        if (set_name == "loht" || set_name == "lohtj" || set_name == "lohtb2" ||
            set_name == "lohtb3") {
            return {.work_ram_base = 0xA0000U, .dip_default = dip_loht};
        }
        if (set_name == "dkgensanm72") {
            return {.work_ram_base = 0xA0000U, .dip_default = dip_hharry};
        }
        if (set_name == "airduelm72" || set_name == "airdueljm72") {
            return {.work_ram_base = 0xA0000U, .dip_default = dip_airduel};
        }
        if (set_name == "gallopm72") {
            return {.work_ram_base = 0xA0000U, .dip_default = dip_gallop};
        }
        return {};
    }

    m72_system::m72_system(common::rom_set_image image, m72_board_params board_params)
        : roms(std::move(image)), params(board_params) {
        dip_switches = params.dip_default;
        const auto* sound_rom_region = roms.region("soundcpu");
        sound_rom_present = sound_rom_region != nullptr && !sound_rom_region->empty();

        // --- main bus: program ROM under the RAM overlays ---
        auto& main_prog = pinned_region(roms, "maincpu", main_rom_size);
        main_bus.map_rom(0x00000U, std::span<const std::uint8_t>(main_prog));
        main_bus.map_ram(sprite_ram_base, sprite_ram, 1);
        main_bus.map_ram(palette_a_base, palette_a, 1);
        main_bus.map_ram(palette_b_base, palette_b, 1);
        main_bus.map_ram(vram_a_base, vram_a, 1);
        main_bus.map_ram(vram_b_base, vram_b, 1);
        main_bus.map_ram(params.work_ram_base, work_ram, 1);
        if (!sound_rom_present) {
            // Original M72 sound path: the Z80's program RAM is byte-visible to
            // the V30 so the boot path can upload the sound program before reset
            // release.
            main_bus.map_ram(sound_ram_window, sound_ram, 1);
        }
        main_cpu.attach_bus(main_bus);

        // --- sound bus: R-Type-style upload RAM or later-board sound ROM ---
        if (sound_rom_present) {
            auto& sound_rom = pinned_region(roms, "soundcpu", sound_rom_region_size);
            sound_bus.map_rom(
                sound_rom_base,
                std::span<const std::uint8_t>(sound_rom).first(sound_rom_mapped_size), 0);
            sound_bus.map_ram(
                sound_work_ram_base,
                std::span<std::uint8_t>(sound_ram).subspan(sound_work_ram_base, sound_work_ram_size),
                1);
        } else {
            sound_bus.map_ram(0x0000U, sound_ram, 0);
        }
        sound_cpu.attach_bus(sound_bus);

        // --- V30 I/O space ---
        main_cpu.set_port_in([this](std::uint16_t port) -> std::uint8_t {
            switch (port & 0xFFU) {
            case port_in_p1:
                return input_p1;
            case port_in_p2:
                return input_p2;
            case port_in_system:
                // Bit 7 is the sprite-DMA-complete flag; the copy is
                // instantaneous here, so it always reads done.
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
            case port_mcu_latch:
                return mcu_to_main;
            default:
                return 0xFFU; // open bus
            }
        });
        main_cpu.set_port_out([this](std::uint16_t port, std::uint8_t value) {
            const std::uint16_t p = port & 0xFFU;
            if (p >= port_out_scroll_base && p < port_out_scroll_base + scroll_regs.size()) {
                scroll_regs[p - port_out_scroll_base] = value;
                const auto word = [this](std::size_t i) {
                    return static_cast<std::uint16_t>(scroll_regs[i] | (scroll_regs[i + 1U] << 8U));
                };
                video.set_scroll_a(word(2U), word(0U));
                video.set_scroll_b(word(6U), word(4U));
                return;
            }
            switch (p) {
            case port_out_sound_latch:
                sound_latch = value;
                sound_latch_irq = true; // latch write knocks on the Z80
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
                // Bit 3: display disable. Bit 4: sound-CPU /RESET, active
                // low -- the V30 raises it once the sound program upload is
                // done. Bits 0/1 pulse the physical coin meters; bit 2 mirrors
                // the completed video composite.
                video.set_flip_screen((value & 0x04U) != 0U);
                video.set_display_enable((value & 0x08U) == 0U);
                sound_cpu.set_reset_line((value & 0x10U) == 0U);
                break;
            case port_out_sprite_dma:
                video.latch_sprites();
                break;
            case port_out_raster_lo:
            case port_out_raster_hi: {
                raster_regs[p - port_out_raster_lo] = value;
                const int line =
                    static_cast<int>((raster_regs[0] | (raster_regs[1] << 8U)) & 0x1FFU) - 128;
                video.set_raster_compare(line);
                // The write also withdraws a pending, unserviced request.
                pic.set_irq_line(2U, false);
                break;
            }
            case port_pic_a0:
                pic.write(0U, value);
                break;
            case port_pic_a1:
                pic.write(1U, value);
                break;
            case port_mcu_latch:
                main_to_mcu = value;
                if (mcu_present) { // knock: edge-pulse the MCU's INT1
                    mcu.set_int1_line(true);
                    mcu.set_int1_line(false);
                } else if (protection_hle_present) {
                    // No-dump HLE can only acknowledge the board-facing command
                    // latch; per-game protection entry routines stay artifact-gated.
                    mcu_to_main = value;
                    const auto* samples = roms.region("samples");
                    if (samples != nullptr) {
                        if (const auto start =
                                no_dump_hle_sample_start(*params.protection_hle_profile,
                                                         *samples, value)) {
                            sample_address = *start;
                        }
                    }
                }
                break;
            default:
                break; // remaining board ports land with their subsystems
            }
        });

        // --- Z80 I/O space: YM2151 at 0/1, the latch at 2, its interrupt
        // acknowledge at 6 ---
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
                sample_address = (sample_address & 0x00FFU) |
                                 (static_cast<std::uint32_t>(value) << 8U);
                break;
            case z80_port_dac:
                record_dac_write(value);
                break;
            default:
                break;
            }
        });
        // The IM0 jam byte: the pending sources' RST opcodes AND together
        // (open-collector negative buffer); idle floats to RST 38h.
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

        // --- optional protection MCU: program from the "mcu" region, sample
        // latches at MOVX 0x0000-0x0002, and the shared work window visible as
        // V30 0xB0000 / MCU MOVX 0xC000. No-dump protected sets may instead
        // declare a known HLE profile; that maps only the startup-visible
        // inverted shared-RAM surface and never schedules a fake MCU.
        const auto* mcu_program = roms.region("mcu");
        mcu_present = mcu_program != nullptr && !mcu_program->empty();
        if (params.protection_hle_profile.has_value() &&
            !supported_protection_hle_profile(*params.protection_hle_profile)) {
            roms.issues.push_back(
                {"mcu",
                 "unsupported M72 MCU HLE profile '" + *params.protection_hle_profile + "'"});
            params.protection_hle_profile.reset();
        }
        protection_hle_present =
            !mcu_present && params.protection_hle_profile.has_value() &&
            supported_protection_hle_profile(*params.protection_hle_profile);
        if (mcu_present) {
            main_bus.map_ram(mcu_shared_main_base, mcu_shared_ram, 1);
            mcu.attach_program(*mcu_program);
            mcu_bus.map_mmio(
                mcu_sample_data, 3U,
                [this](std::uint32_t address) -> std::uint8_t {
                    switch (address) {
                    case mcu_sample_data: {
                        const auto* samples = roms.region("samples");
                        if (samples == nullptr || samples->empty()) {
                            return 0xFFU;
                        }
                        const std::uint8_t sample =
                            (*samples)[mcu_sample_address % samples->size()];
                        ++mcu_sample_address;
                        return sample;
                    }
                    case mcu_latch:
                        return main_to_mcu;
                    default:
                        return 0xFFU;
                    }
                },
                [this](std::uint32_t address, std::uint8_t value) {
                    switch (address) {
                    case mcu_sample_data:
                        mcu_sample_address =
                            (mcu_sample_address & 0xFFE000U) |
                            (static_cast<std::uint32_t>(value) << 5U);
                        break;
                    case mcu_sample_addr_high:
                        mcu_sample_address =
                            (mcu_sample_address & 0x001FFFU) |
                            (static_cast<std::uint32_t>(value) << 13U);
                        break;
                    case mcu_latch:
                        mcu_to_main = value;
                        break;
                    default:
                        break;
                    }
                },
                1);
            mcu_bus.map_ram(mcu_shared_movx_base, mcu_shared_ram, 0);
            mcu.attach_bus(mcu_bus);
        } else if (protection_hle_present) {
            main_bus.map_mmio(
                mcu_shared_main_base, static_cast<std::uint32_t>(mcu_shared_ram_size),
                [this](std::uint32_t address) -> std::uint8_t {
                    const auto offset =
                        static_cast<std::size_t>(address - mcu_shared_main_base);
                    return mcu_shared_ram[offset];
                },
                [this](std::uint32_t address, std::uint8_t value) {
                    const auto offset =
                        static_cast<std::size_t>(address - mcu_shared_main_base);
                    mcu_shared_ram[offset] = static_cast<std::uint8_t>(~value);
                },
                1);
        }

        // --- video: VRAM/palette/tile spans + the scanline pulses into the
        // interrupt controller (IR0 = vblank line, IR2 = raster compare; each
        // asserted at its line and withdrawn at the next) ---
        video.attach_vram_a(vram_a);
        video.attach_vram_b(vram_b);
        video.attach_palette_a(palette_a);
        video.attach_palette_b(palette_b);
        video.attach_sprite_ram(sprite_ram);
        video.attach_tiles_a(roms.regions["tiles_a"]);
        video.attach_tiles_b(roms.regions["tiles_b"]);
        video.attach_sprites(roms.regions["sprites"]);
        video.set_scanline_callback([this](std::uint32_t line) {
            pic.set_irq_line(0U, line == chips::video::irem_m72_video::visible_height);
            pic.set_irq_line(2U, video.raster_compare_matches(line));
        });
        pic.set_int_callback([this](bool asserted) { main_cpu.set_irq_line(asserted); });
        main_cpu.set_irq_ack([this]() -> std::uint8_t { return pic.acknowledge(); });

        main_cpu.reset(chips::reset_kind::power_on);
        sound_cpu.reset(chips::reset_kind::power_on);
        if (!sound_rom_present) {
            // No sound ROM exists: the Z80 waits in reset until the V30 uploads
            // its program into the shared RAM and releases it via port 0x02.
            sound_cpu.set_reset_line(true);
        }
    }

    void m72_system::record_dac_write(std::uint8_t value) {
        dac.write(value);
        const dac_write_event event{.sound_clock = fm.elapsed_clocks(),
                                    .output = dac.output()};
        if (!dac_write_events.empty() &&
            dac_write_events.back().sound_clock == event.sound_clock) {
            dac_write_events.back() = event;
            return;
        }
        dac_write_events.push_back(event);
    }

    void m72_system::discard_dac_write_events_before(std::uint64_t sound_clock) {
        std::size_t first_live = 0U;
        while (first_live < dac_write_events.size() &&
               dac_write_events[first_live].sound_clock < sound_clock) {
            ++first_live;
        }
        if (first_live == 0U) {
            return;
        }
        dac_write_events.erase(dac_write_events.begin(),
                               dac_write_events.begin() +
                                   static_cast<std::ptrdiff_t>(first_live));
    }

    namespace {
        // Whole-board state format. Bump when the layout below changes.
        constexpr std::uint32_t m72_system_state_version = 6U;
    } // namespace

    void m72_system::save_state(chips::state_writer& writer) const {
        writer.u32(m72_system_state_version);

        // Structural compatibility guard: loading into a differently assembled
        // board, or one backed by a different ROM image, would leave CPU/RAM
        // state running against the wrong immutable media.
        writer.u32(params.work_ram_base);
        writer.boolean(mcu_present);
        writer.boolean(protection_hle_present);
        writer.boolean(sound_rom_present);
        writer.u32(rom_identity_crc(roms, params));

        main_cpu.save_state(writer);
        sound_cpu.save_state(writer);
        video.save_state(writer);
        fm.save_state(writer);
        dac.save_state(writer);
        pic.save_state(writer);
        mcu.save_state(writer);

        writer.bytes(sprite_ram);
        writer.bytes(palette_a);
        writer.bytes(palette_b);
        writer.bytes(vram_a);
        writer.bytes(vram_b);
        writer.bytes(work_ram);
        writer.bytes(mcu_shared_ram);
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
            writer.u16(static_cast<std::uint16_t>(
                static_cast<std::int32_t>(event.output) + 32768));
        }
        writer.u8(main_to_mcu);
        writer.u8(mcu_to_main);
        writer.u32(mcu_sample_address);
    }

    void m72_system::load_state(chips::state_reader& reader) {
        if (reader.u32() != m72_system_state_version) {
            reader.fail();
            return;
        }
        const std::uint32_t saved_work_ram_base = reader.u32();
        const bool saved_mcu_present = reader.boolean();
        const bool saved_protection_hle_present = reader.boolean();
        const bool saved_sound_rom_present = reader.boolean();
        const std::uint32_t saved_rom_identity = reader.u32();
        if (saved_work_ram_base != params.work_ram_base ||
            saved_mcu_present != mcu_present ||
            saved_protection_hle_present != protection_hle_present ||
            saved_sound_rom_present != sound_rom_present ||
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
        mcu.load_state(reader);

        reader.bytes(sprite_ram);
        reader.bytes(palette_a);
        reader.bytes(palette_b);
        reader.bytes(vram_a);
        reader.bytes(vram_b);
        reader.bytes(work_ram);
        reader.bytes(mcu_shared_ram);
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
        std::uint64_t previous_dac_event_clock = 0U;
        for (std::uint32_t i = 0; i < dac_event_count; ++i) {
            dac_write_event event{};
            event.sound_clock = reader.u64();
            event.output = static_cast<std::int16_t>(
                static_cast<std::int32_t>(reader.u16()) - 32768);
            if (i != 0U && event.sound_clock < previous_dac_event_clock) {
                reader.fail();
                return;
            }
            previous_dac_event_clock = event.sound_clock;
            dac_write_events.push_back(event);
        }
        main_to_mcu = reader.u8();
        mcu_to_main = reader.u8();
        mcu_sample_address = reader.u32();

        if (reader.ok()) {
            update_sound_irq();
        }
    }

    common::rom_set_decl m72_rom_skeleton(std::string set_name) {
        common::rom_set_decl decl;
        decl.name = std::move(set_name);
        decl.regions.push_back(
            {.name = "maincpu", .size = main_rom_size, .fill = 0xFFU, .files = {}});
        return decl;
    }

    std::unique_ptr<m72_system> assemble_m72(common::rom_set_image image,
                                             m72_board_params board_params) {
        return std::make_unique<m72_system>(std::move(image), board_params);
    }

} // namespace mnemos::manifests::irem_m72
