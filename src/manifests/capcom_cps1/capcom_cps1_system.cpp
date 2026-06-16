#include "capcom_cps1_system.hpp"

#include <algorithm>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace mnemos::manifests::capcom_cps1 {

    namespace {
        // The named region's bytes, padded to `size` so the bus map always has
        // full-size backing (absent dumps read 0xFF). `name` is a string_view (not
        // a const std::string&) so a literal callsite does not bind a temporary to
        // a reference param -- which GCC's -Wdangling-reference flags as a (false)
        // alias of the returned reference.
        [[nodiscard]] std::vector<std::uint8_t>&
        pinned_region(common::rom_set_image& image, std::string_view name, std::size_t size) {
            auto& bytes = image.regions[std::string(name)];
            if (bytes.size() < size) {
                bytes.resize(size, 0xFFU);
            }
            return bytes;
        }
    } // namespace

    cps1_board_params board_params_for(std::string_view /*set_name*/) {
        // Increment 1 carries no per-set wiring beyond the legacy default profile;
        // the romset TOML profile key that selects a board profile lands later.
        return {};
    }

    cps1_board_params board_params_from_decl(const common::rom_set_decl& decl) {
        cps1_board_params params{.cps_b_profile_id = decl.cps_b_profile.value_or(std::uint16_t{0U}),
                                 .vertical =
                                     decl.orientation == common::screen_orientation::vertical};
        if (decl.sound && *decl.sound == "qsound") {
            params.sound = sound_system::qsound;
        }
        if (decl.kabuki) {
            if (*decl.kabuki == "dino") {
                params.kabuki = kabuki_game::dino;
            } else if (*decl.kabuki == "wof") {
                params.kabuki = kabuki_game::wof;
            } else if (*decl.kabuki == "punisher") {
                params.kabuki = kabuki_game::punisher;
            } else if (*decl.kabuki == "slammast" || *decl.kabuki == "mbombrd") {
                // Muscle Bomber Duo shares Slam Masters' Kabuki key.
                params.kabuki = kabuki_game::slammast;
            }
        }
        return params;
    }

    cps1_system::cps1_system(common::rom_set_image image, cps1_board_params board_params)
        : roms(std::move(image)), params(board_params) {
        // --- main bus: program ROM low, RAM overlays above it ---
        auto& program = pinned_region(roms, "maincpu", main_rom_size);
        main_bus.map_rom(program_rom_base, std::span<const std::uint8_t>(program), 0);
        main_bus.map_ram(work_ram_base, work_ram, 1);
        main_bus.map_ram(gfx_ram_base, gfx_ram, 1);

        // CPS-A register file: the board writes the raw 32-word window.
        main_bus.map_mmio(
            cps_a_reg_base, cps_a_reg_size,
            [this](std::uint32_t address) -> std::uint8_t {
                const std::size_t idx = (address - cps_a_reg_base) >> 1U;
                if (idx >= cps_a_reg_count) {
                    return 0xFFU;
                }
                const std::uint16_t word = cps_a_regs[idx];
                return (address & 1U) == 0U ? static_cast<std::uint8_t>(word >> 8U)
                                            : static_cast<std::uint8_t>(word);
            },
            [this](std::uint32_t address, std::uint8_t value) {
                const std::size_t idx = (address - cps_a_reg_base) >> 1U;
                if (idx >= cps_a_reg_count) {
                    return;
                }
                std::uint16_t& word = cps_a_regs[idx];
                word = (address & 1U) == 0U
                           ? static_cast<std::uint16_t>((word & 0x00FFU) | (value << 8U))
                           : static_cast<std::uint16_t>((word & 0xFF00U) | value);
                // reg5 (the palette source pointer) triggers the palette DMA on
                // its low-byte write -- the point the full 16-bit pointer is set.
                if (idx == cps_a_palette_base && (address & 1U) != 0U) {
                    copy_palette_from_gfx_ram();
                }
            },
            1);

        // CPS-B register file: writes go to the video chip's raw register file;
        // reads decode through the active profile (board ID + 16x16 multiplier),
        // since the render registers are write latches the 68K can't read back.
        main_bus.map_mmio(
            cps_b_reg_base, cps_b_reg_size,
            [this](std::uint32_t address) -> std::uint8_t {
                const std::uint8_t offset =
                    static_cast<std::uint8_t>((address - cps_b_reg_base) & 0xFEU);
                const std::uint16_t word = cps_b_read_word(offset);
                return (address & 1U) == 0U ? static_cast<std::uint8_t>(word >> 8U)
                                            : static_cast<std::uint8_t>(word);
            },
            [this](std::uint32_t address, std::uint8_t value) {
                const std::uint8_t idx =
                    static_cast<std::uint8_t>((address - cps_b_reg_base) >> 1U);
                const std::uint16_t word = video.cps_b_reg(idx);
                const std::uint16_t merged =
                    (address & 1U) == 0U
                        ? static_cast<std::uint16_t>((word & 0x00FFU) | (value << 8U))
                        : static_cast<std::uint16_t>((word & 0xFF00U) | value);
                video.set_cps_b_reg(idx, merged);
            },
            1);

        if (uses_qsound()) {
            // QSound 68K<->Z80 comm: two shared-RAM windows ($F18000 -> bank 0,
            // $F1E000 -> bank 1). The 68K is 16-bit big-endian, so each shared byte
            // is the low (odd) byte of a word; the even byte reads open bus and its
            // writes are dropped (the data lane the hardware leaves unconnected).
            const auto map_comm = [this](std::uint32_t base, std::size_t bank) {
                main_bus.map_mmio(
                    base, qsound_comm_window,
                    [this, base, bank](std::uint32_t address) -> std::uint8_t {
                        if ((address & 1U) == 0U) {
                            return 0xFFU;
                        }
                        const std::size_t index = (address - base) >> 1U;
                        return index < qsound_shared_size ? qsound_shared[bank][index] : 0xFFU;
                    },
                    [this, base, bank](std::uint32_t address, std::uint8_t value) {
                        if ((address & 1U) == 0U) {
                            return;
                        }
                        const std::size_t index = (address - base) >> 1U;
                        if (index < qsound_shared_size) {
                            qsound_shared[bank][index] = value;
                        }
                    },
                    1);
            };
            map_comm(qsound_comm0_base, 0U);
            map_comm(qsound_comm1_base, 1U);

            // C-board I/O window: player 3/4 inputs (read-only, all-released until a
            // 4-player adapter drives them) + the serial 93C46 EEPROM at $F1C006.
            // 2-player QSound sets simply never read/write this window.
            main_bus.map_mmio(
                qsound_io_base, qsound_io_window,
                [this](std::uint32_t address) -> std::uint8_t {
                    if ((address & 0xFEU) == (qsound_eeprom_addr & 0xFEU)) {
                        // EEPROM data-out in bit0 of the low (odd) byte.
                        return (address & 1U) != 0U
                                   ? static_cast<std::uint8_t>(eeprom.data_out() ? 0x01U : 0x00U)
                                   : 0xFFU;
                    }
                    return 0xFFU; // IN2/IN3 all-released; coin2 is write-only
                },
                [this](std::uint32_t address, std::uint8_t value) {
                    if (address == qsound_eeprom_addr + 1U) {
                        // EEPROMOUT (low/odd byte): DI bit0, CLK bit6, CS bit7.
                        eeprom.update((value & qsound_eeprom_cs) != 0U,
                                      (value & qsound_eeprom_clk) != 0U,
                                      (value & qsound_eeprom_di) != 0U);
                    }
                },
                1);
        } else {
            // Sound-command latches ($800180 primary, $800188 secondary). The 68K
            // pokes commands here; the Z80 reads them at $F008 / $F00A. A primary
            // write arms the pending flag (cleared on the Z80 read). Only the low
            // byte of the secondary latch carries data on hardware. These ports are
            // write-only on the 68K side -- a read returns open bus (0xFF).
            main_bus.map_mmio(
                sound_latch_addr, sound_latch_window,
                [](std::uint32_t) -> std::uint8_t { return 0xFFU; },
                [this](std::uint32_t address, std::uint8_t value) {
                    if (address >= sound_latch_addr && address < sound_latch_addr + 8U) {
                        sound_latch = value;
                        sound_latch_pending = true;
                    } else if (address >= sound_latch2_addr && address < sound_latch2_addr + 8U &&
                               (address & 1U) != 0U) {
                        sound_latch2 = value;
                    }
                },
                1);
        }

        // Player-input word, mirrored across $800000-$800007 (P2 high byte, P1
        // low byte). Read-only; writes here are ignored.
        main_bus.map_mmio(
            player_input_base, player_input_window,
            [this](std::uint32_t address) -> std::uint8_t {
                return (address & 1U) != 0U ? static_cast<std::uint8_t>(input_p & 0x00FFU)
                                            : static_cast<std::uint8_t>((input_p >> 8U) & 0x00FFU);
            },
            [](std::uint32_t, std::uint8_t) {}, 1);

        // System input word + the three DIP-switch words at $800018-$80001F (word
        // 0 = system, words 1-3 = DIP A/B/C). Read-only.
        main_bus.map_mmio(
            system_dsw_base, system_dsw_window,
            [this](std::uint32_t address) -> std::uint8_t {
                const std::uint16_t word = system_dsw_word((address - system_dsw_base) >> 1U);
                return (address & 1U) != 0U ? static_cast<std::uint8_t>(word & 0x00FFU)
                                            : static_cast<std::uint8_t>((word >> 8U) & 0x00FFU);
            },
            [](std::uint32_t, std::uint8_t) {}, 1);

        // Coin control ($800030-$800037). Stub: latch the high-byte write so it
        // doesn't fall through; counters/lockout are not modelled in v1.
        main_bus.map_mmio(
            coin_control_base, coin_control_window,
            [](std::uint32_t) -> std::uint8_t { return 0xFFU; },
            [this](std::uint32_t address, std::uint8_t value) {
                if ((address & 1U) == 0U) {
                    coin_control = value;
                }
            },
            1);

        main_cpu.attach_bus(main_bus);

        // --- video: GFX ROM + unified GFX RAM + the board-owned palette ---
        const auto& gfx = pinned_region(roms, "gfx", 0U);
        video.attach_gfx(std::span<const std::uint8_t>(gfx));
        video.attach_tile_ram(gfx_ram);
        video.attach_object_ram(gfx_ram);
        video.attach_palette(palette);
        profile = profile_for_id(params.cps_b_profile_id)
                      .value_or(chips::video::cps_a_b::cps_b_profile{});
        video.set_cps_b_profile(profile);

        // --- sound subsystem ---
        if (uses_qsound()) {
            setup_qsound();
        } else {
            // Z80 + YM2151 + OKIM6295 on the sound bus.
            auto& sound_rom = pinned_region(roms, "audiocpu", z80_rom_window);
            sound_rom_size = static_cast<std::uint32_t>(sound_rom.size());

            // Fixed low 32 KiB of the sound program.
            sound_bus.map_rom(z80_rom_base,
                              std::span<const std::uint8_t>(sound_rom).first(z80_rom_window), 0);
            // Banked 16 KiB window: a register-selected slice of the upper sound ROM.
            // Capture the ROM as a span by value (not a reference into the ROM map).
            const std::span<const std::uint8_t> sound_rom_span{sound_rom};
            sound_bus.map_mmio(
                z80_bank_base, z80_bank_window,
                [this, sound_rom_span](std::uint32_t address) -> std::uint8_t {
                    const std::uint32_t rom_addr = z80_bank_rom_base() + (address - z80_bank_base);
                    return rom_addr < sound_rom_span.size() ? sound_rom_span[rom_addr] : 0xFFU;
                },
                [](std::uint32_t, std::uint8_t) {}, 0);
            // Z80 sound work RAM.
            sound_bus.map_ram(z80_ram_base, z80_ram, 0);
            // Sound I/O ($F000+): YM2151, OKIM6295, the bank + pin-7 registers, and
            // the two read-only sound latches.
            sound_bus.map_mmio(
                z80_io_base, z80_io_window,
                [this](std::uint32_t address) -> std::uint8_t {
                    switch (address) {
                    case z80_io_ym_addr:
                    case z80_io_ym_data:
                        return fm.read_status();
                    case z80_io_oki:
                        return oki.read_status();
                    case z80_io_latch:
                        sound_latch_pending = false; // the read consumes the armed latch
                        return sound_latch;
                    case z80_io_latch2:
                        return sound_latch2;
                    default:
                        return 0xFFU;
                    }
                },
                [this](std::uint32_t address, std::uint8_t value) {
                    switch (address) {
                    case z80_io_ym_addr:
                        fm.write_address(value);
                        break;
                    case z80_io_ym_data:
                        fm.write_data(value);
                        sync_sound_irq(); // a data write can clear/raise a timer flag
                        break;
                    case z80_io_oki:
                        oki.write_command(value);
                        break;
                    case z80_io_bank:
                        sound_bank = static_cast<std::uint8_t>(value & z80_bank_mask);
                        break;
                    case z80_io_oki_pin7:
                        oki.set_pin7((value & 0x01U) != 0U);
                        break;
                    default:
                        break;
                    }
                },
                0);
            sound_cpu.attach_bus(sound_bus);

            // OKIM6295 sample voice: the "oki" region is its external sample ROM; run
            // it from a 1 MHz input clock (pin-7 high = input/132 native rate).
            const auto& oki_rom = pinned_region(roms, "oki", 0U);
            oki.set_sample_rom(std::span<const std::uint8_t>(oki_rom));
            oki.set_input_clock(oki_clock_hz);
            oki.set_pin7(true);

            // The YM2151's timer-IRQ line drives the Z80 /INT (the edge callback
            // re-asserts on every timer-flag change).
            fm.set_irq([this](bool) { sync_sound_irq(); });
        } // end OKIM6295 sound path

        // Vblank IRQ: the beam entering vblank (after the frame renders) raises the
        // 68K level-2 autovector ($68); the CPU clears the line at its IACK. The
        // v1 (non-raster) video interrupt is level 2 (the 6/4 split is the raster
        // path, out of v1 scope). Wired before reset so the settings survive it.
        video.set_vblank_callback([this](std::uint32_t /*line*/) {
            main_cpu.set_irq_level(2);
            ++vblank_irq_raised;
        });
        main_cpu.set_irq_ack_callback([this](int /*level*/) {
            main_cpu.set_irq_level(0);
            ++vblank_irq_acked;
        });

        // Construction order: ROM mapped + bus attached, then power-on reset so the
        // 68K loads SSP/PC from the cart's reset vectors. The sound Z80 runs from
        // $0000 immediately (CPS1 has no sound-CPU reset gate on this path).
        main_cpu.reset(chips::reset_kind::power_on);
        sound_cpu.reset(chips::reset_kind::power_on);
        fm.reset(chips::reset_kind::power_on);
        oki.reset(chips::reset_kind::power_on);
        qdsp.reset(chips::reset_kind::power_on);
        eeprom.reset();
        video.reset(chips::reset_kind::power_on);
    }

    void cps1_system::setup_qsound() {
        // The QSound sound CPU program ROM. Its low $0000-$7FFF window is fixed; on
        // a Kabuki-encrypted set that window is decrypted into two streams (opcode
        // for M1 fetches, data for reads) the bus serves separately. The banked
        // $8000-$BFFF window selects one of 16 slices from $10000 up, plain.
        auto& sound_rom = pinned_region(roms, "audiocpu", z80_rom_window);
        sound_rom_size = static_cast<std::uint32_t>(sound_rom.size());
        const std::size_t low = std::min<std::size_t>(sound_rom.size(), z80_rom_window);

        if (params.kabuki) {
            // Decrypt the encrypted low window into the opcode + data streams.
            sound_opcode_rom.assign(low, 0x00U);
            sound_data_rom.assign(low, 0x00U);
            kabuki_decode(std::span<const std::uint8_t>(sound_rom).first(low),
                          kabuki_keys_for(*params.kabuki), sound_opcode_rom, sound_data_rom);
            sound_bus.map_rom(z80_rom_base, std::span<const std::uint8_t>(sound_data_rom), 0);
            sound_bus.map_opcode_rom(z80_rom_base, std::span<const std::uint8_t>(sound_opcode_rom));
        } else {
            // Plain QSound program: the low window is read directly (no split).
            sound_bus.map_rom(z80_rom_base, std::span<const std::uint8_t>(sound_rom).first(low), 0);
        }

        // Banked 16 KiB window ($8000-$BFFF): a $D003-selected 16 KiB slice from
        // $10000 in the sound ROM. Capture the ROM span by value.
        const std::span<const std::uint8_t> sound_rom_span{sound_rom};
        sound_bus.map_mmio(
            z80_bank_base, z80_bank_window,
            [this, sound_rom_span](std::uint32_t address) -> std::uint8_t {
                const std::uint32_t rom_addr = qsound_bank_base +
                                               (sound_bank & qsound_bank_mask) * z80_bank_window +
                                               (address - z80_bank_base);
                return rom_addr < sound_rom_span.size() ? sound_rom_span[rom_addr] : 0xFFU;
            },
            [](std::uint32_t, std::uint8_t) {}, 0);

        // The two 4 KiB shared-RAM comm banks ($C000 / $F000 on the Z80).
        sound_bus.map_ram(qsound_shared0_base, qsound_shared[0], 0);
        sound_bus.map_ram(qsound_shared1_base, qsound_shared[1], 0);

        // DL-1425 port window + the bank register ($D000-$D7FF). $D000-$D002 write
        // = the DSP 3-port interface (data hi / data lo / commit); $D003 write =
        // the banked-window select; $D007 read = the DSP ready flag.
        sound_bus.map_mmio(
            qsound_port_base, qsound_port_window,
            [this](std::uint32_t address) -> std::uint8_t {
                return address == qsound_ready_reg ? qdsp.read_status() : 0xFFU;
            },
            [this](std::uint32_t address, std::uint8_t value) {
                if (address == qsound_bank_reg) {
                    sound_bank = static_cast<std::uint8_t>(value & qsound_bank_mask);
                } else if (address >= qsound_port_base && address < qsound_bank_reg) {
                    qdsp.write_port(static_cast<std::uint8_t>(address - qsound_port_base), value);
                }
            },
            0);
        sound_cpu.attach_bus(sound_bus);

        // The DL-1425's external sample ROM (8-bit PCM, addressed as bank:addr).
        const auto& qsound_rom = pinned_region(roms, "qsound", 0U);
        qdsp.set_sample_rom(std::span<const std::uint8_t>(qsound_rom));
    }

    std::uint32_t cps1_system::z80_bank_rom_base() const noexcept {
        if (sound_rom_size <= z80_rom_window) {
            return z80_rom_window; // no upper banks: clamp to the fixed window edge
        }
        const std::uint32_t bank = sound_bank & z80_bank_mask;
        const std::uint32_t base =
            sound_rom_size >= z80_bank_split_threshold ? z80_bank_base_large : z80_bank_base_small;
        return base + bank * z80_bank_window;
    }

    void cps1_system::sync_sound_irq() noexcept { sound_cpu.set_irq_line(fm.irq_asserted()); }

    std::uint32_t cps1_system::gfx_ram_base_from_reg(std::uint16_t reg) const noexcept {
        const std::uint32_t addr = static_cast<std::uint32_t>(reg) << 8U;
        if (addr >= gfx_ram_base && addr < gfx_ram_base + gfx_ram_size) {
            return addr - gfx_ram_base;
        }
        return static_cast<std::uint32_t>(reg) * 256U;
    }

    std::uint32_t cps1_system::gfx_ram_base_aligned(std::uint16_t reg,
                                                    std::uint32_t boundary) const noexcept {
        std::uint32_t base = gfx_ram_base_from_reg(reg);
        if (boundary > 1U) {
            base &= ~(boundary - 1U);
        }
        return base;
    }

    std::uint16_t cps1_system::cps_b_read_word(std::uint8_t offset) const noexcept {
        // Odd offsets and the absent sentinel never name a readable port.
        if ((offset & 1U) != 0U) {
            return 0xFFFFU;
        }
        // Board-ID port: a fixed identity value (a board / PAL discriminator the
        // game's 68K protection check reads back).
        if (offset == profile.id_offset && profile.id_offset != chips::video::cps_a_b::reg_none) {
            return profile.id_value;
        }
        // 16x16 multiplier: factor1/factor2 are the raw CPS-B registers the 68K
        // wrote at the profile's factor offsets; the result lo/hi ports return the
        // product halves. mult_offset = {factor1, factor2, result-lo, result-hi}.
        const auto raw_reg = [this](std::uint8_t off) -> std::uint16_t {
            return (off != chips::video::cps_a_b::reg_none && (off & 1U) == 0U)
                       ? video.cps_b_reg(static_cast<std::uint8_t>(off >> 1U))
                       : 0U;
        };
        const std::uint8_t result_lo_off = profile.mult_offset[2];
        const std::uint8_t result_hi_off = profile.mult_offset[3];
        if (result_lo_off != chips::video::cps_a_b::reg_none && offset == result_lo_off) {
            const std::uint32_t product =
                static_cast<std::uint32_t>(raw_reg(profile.mult_offset[0])) *
                static_cast<std::uint32_t>(raw_reg(profile.mult_offset[1]));
            return static_cast<std::uint16_t>(product & 0xFFFFU);
        }
        if (result_hi_off != chips::video::cps_a_b::reg_none && offset == result_hi_off) {
            const std::uint32_t product =
                static_cast<std::uint32_t>(raw_reg(profile.mult_offset[0])) *
                static_cast<std::uint32_t>(raw_reg(profile.mult_offset[1]));
            return static_cast<std::uint16_t>(product >> 16U);
        }
        // No special port at this offset: the raw register (raster read-back is
        // out of v1, so its offsets fall here and return the raw latch).
        return video.cps_b_reg(static_cast<std::uint8_t>(offset >> 1U));
    }

    std::uint16_t cps1_system::system_dsw_word(std::uint32_t word_offset) const noexcept {
        std::uint8_t input = 0xFFU;
        switch (word_offset & 3U) {
        case 0U:
            input = static_cast<std::uint8_t>(input_sys & 0x00FFU);
            break;
        case 1U:
            input = dip_a;
            break;
        case 2U:
            input = dip_b;
            break;
        case 3U:
            input = dip_c;
            break;
        default:
            break;
        }
        return static_cast<std::uint16_t>((static_cast<std::uint16_t>(input) << 8U) | 0x00FFU);
    }

    void cps1_system::push_cps_a_to_video() noexcept {
        // Bootleg-board kludge (profile.bootleg_kludge, 0 on real boards => no
        // change): the sf2 hacks (low nibble 1) read sprites only from object
        // port 0x9100 and nudge the scroll layers a few pixels. The forced port
        // lives only in this local copy (re-applied every frame); cps_a_regs is
        // left untouched -- the reference instead overwrites the shared register.
        const std::uint8_t kludge_nibble = profile.bootleg_kludge & 0x0FU;
        std::uint16_t obj_reg = cps_a_regs[cps_a_obj_base];
        std::int16_t s1off = 0;
        std::int16_t s2off = 0;
        std::int16_t s3off = 0;
        if (kludge_nibble == 1U) {
            obj_reg = 0x9100U;
            s1off = -0x0C;
            s2off = -0x0E;
            s3off = -0x10;
        }

        // Name-table bases, each aligned to its table boundary: object 0x800,
        // scroll1/2/3 0x4000.
        video.set_object_base(gfx_ram_base_aligned(obj_reg, object_base_align));
        video.set_scroll1_base(
            gfx_ram_base_aligned(cps_a_regs[cps_a_scroll1_base], scroll_base_align));
        video.set_scroll2_base(
            gfx_ram_base_aligned(cps_a_regs[cps_a_scroll2_base], scroll_base_align));
        video.set_scroll3_base(
            gfx_ram_base_aligned(cps_a_regs[cps_a_scroll3_base], scroll_base_align));

        // Scroll offsets (signed pixel offsets, passed as raw 16-bit words; the
        // bootleg kludge nudges each layer by the offsets selected above).
        video.set_scroll1(static_cast<std::uint16_t>(cps_a_regs[cps_a_scroll1_x] + s1off),
                          cps_a_regs[cps_a_scroll1_y]);
        video.set_scroll2(static_cast<std::uint16_t>(cps_a_regs[cps_a_scroll2_x] + s2off),
                          cps_a_regs[cps_a_scroll2_y]);
        video.set_scroll3(static_cast<std::uint16_t>(cps_a_regs[cps_a_scroll3_x] + s3off),
                          cps_a_regs[cps_a_scroll3_y]);

        // Scroll2 row-scroll: video-control bit0 enables it; reg4 is the table base
        // (aligned to 0x800) and reg16 the per-line index bias.
        const std::uint16_t video_control = cps_a_regs[cps_a_video_control];
        video.set_rowscroll(
            (video_control & 0x0001U) != 0U,
            gfx_ram_base_aligned(cps_a_regs[cps_a_rowscroll_base], other_base_align),
            cps_a_regs[cps_a_rowscroll_offset]);

        // Video-control latch (flip-screen bit15, layer-enable bits 2/3) drives
        // both flip and the per-layer enable gates inside the mixer.
        video.set_video_control(video_control);
        video.set_display_enable(true);
    }

    void cps1_system::copy_palette_from_gfx_ram() noexcept {
        const std::uint16_t reg = cps_a_regs[cps_a_palette_base];
        if (reg == 0U) {
            return;
        }
        // The page-enable mask is the active CPS-B profile's palette-control
        // register; an unmapped offset means "all pages" (the legacy default).
        const std::uint8_t pal_ctrl_off = profile.palette_control_offset;
        const std::uint16_t ctrl =
            (pal_ctrl_off != chips::video::cps_a_b::reg_none && (pal_ctrl_off & 1U) == 0U)
                ? video.cps_b_reg(static_cast<std::uint8_t>(pal_ctrl_off >> 1U))
                : palette_control_default;

        // Source is the reg5 pointer aligned to a palette page; copy enabled pages
        // into the contiguous palette buffer, advancing the source across skipped
        // pages once any page has been copied (matches the hardware page walk).
        std::uint32_t source = gfx_ram_base_from_reg(reg) & ~(palette_page_bytes - 1U);
        bool source_advanced = false;
        for (std::uint32_t page = 0U; page < palette_copy_pages; ++page) {
            const bool copy_page = (ctrl & static_cast<std::uint16_t>(1U << page)) != 0U;
            if (copy_page) {
                const std::uint32_t dest = page * palette_page_bytes;
                if (source + palette_page_bytes <= gfx_ram_size &&
                    dest + palette_page_bytes <= palette.size()) {
                    std::copy_n(gfx_ram.begin() + source, palette_page_bytes,
                                palette.begin() + dest);
                }
                source += palette_page_bytes;
                source_advanced = true;
            } else if (source_advanced) {
                source += palette_page_bytes;
            }
        }
    }

    void cps1_system::run_frame() {
        // CPS1: ~10 MHz 68K at ~59.6 Hz. Frame-at-vblank model: decode the CPS-A
        // latch to the video, run the two CPUs + the sound chips across the
        // visible field, tick the video into vblank (which renders + raises the
        // level-2 IRQ via the callback), then run the CPUs through the vblank tail
        // so the IRQ is serviced. Exact CPU<->beam cycle sync is a later increment.
        constexpr std::uint64_t cpu_cycles_per_frame = m68k_clock_hz / frame_rate_hz;
        constexpr std::uint32_t visible_lines = chips::video::cps_a_b::vblank_start;
        constexpr std::uint64_t dots_total =
            static_cast<std::uint64_t>(chips::video::cps_a_b::frame_lines) *
            chips::video::cps_a_b::line_pixels;
        // The vblank render + IRQ fire on the dot that lands the beam at the start
        // of the first vblank line; tick the video that far so the IRQ is raised
        // before the CPU tail runs (one dot past the visible field).
        constexpr std::uint64_t dots_to_vblank =
            static_cast<std::uint64_t>(visible_lines) * chips::video::cps_a_b::line_pixels + 1U;
        // Split the CPU budget either side of vblank in proportion to the field.
        constexpr std::uint64_t cpu_visible =
            cpu_cycles_per_frame * visible_lines / chips::video::cps_a_b::frame_lines;

        push_cps_a_to_video();
        run_cpus(cpu_visible);      // 68K + Z80 + sound chips, visible field
        video.tick(dots_to_vblank); // renders + raises the vblank IRQ
        // Sprite-latch DMA: the hardware snapshots the object table into the
        // renderer's holding buffer at end-of-frame, double-buffering it for the
        // next frame's draw. Latch here (right after the render) to reproduce that
        // one-frame lag in the frame-at-vblank model.
        video.latch_sprites();
        run_cpus(cpu_cycles_per_frame - cpu_visible); // services the pending vblank IRQ
        video.tick(dots_total - dots_to_vblank);      // finish the frame back to line 0
    }

    void cps1_system::run_cpus(std::uint64_t cpu_cycles) {
        if (uses_qsound()) {
            run_qsound_cpus(cpu_cycles);
            return;
        }
        // Interleave the 68K and the sound Z80 in fixed slices so a sound command
        // the 68K latches mid-field is seen by the Z80 within the same frame, and
        // the YM2151 timers / OKIM6295 advance in step. The Z80 budget tracks the
        // 68K via a cycle accumulator (z80 += cpu * z80_clock / m68k_clock), so the
        // long-run ratio is exact even though each slice is rounded.
        constexpr std::uint64_t slice_cycles = 256U; // ~one scanline of 68K time
        std::uint64_t remaining = cpu_cycles;
        while (remaining > 0U) {
            const std::uint64_t slice = remaining < slice_cycles ? remaining : slice_cycles;
            remaining -= slice;

            main_cpu.tick(slice);

            cpu_cycle_accum_ += slice * z80_clock_hz;
            const std::uint64_t z80_cycles = cpu_cycle_accum_ / m68k_clock_hz;
            cpu_cycle_accum_ -= z80_cycles * m68k_clock_hz;
            if (z80_cycles > 0U) {
                sound_cpu.tick(z80_cycles);
                fm.tick(z80_cycles); // YM2151 shares the Z80 clock; advance timers
                sync_sound_irq();

                // OKIM6295 runs from its own 1 MHz input clock; advance it for the
                // equivalent wall-clock span of this Z80 slice.
                oki_cycle_accum_ += z80_cycles * oki_clock_hz;
                const std::uint64_t oki_cycles = oki_cycle_accum_ / z80_clock_hz;
                oki_cycle_accum_ -= oki_cycles * z80_clock_hz;
                if (oki_cycles > 0U) {
                    oki.tick(oki_cycles);
                }
            }
        }
    }

    void cps1_system::run_qsound_cpus(std::uint64_t cpu_cycles) {
        // The QSound run path: the 68K + the 8 MHz sound Z80, interleaved in fixed
        // slices (the QSound DSP is HLE-drained in the adapter, so no per-cycle
        // chip ticking here). The sound Z80 takes a periodic 250 Hz /INT: assert
        // the line when a period elapses and deassert it after that slice -- a
        // bounded pulse (the established Z80-IRQ pattern) so the handler runs once
        // per period rather than re-entering while the level stays high.
        constexpr std::uint64_t slice_cycles = 256U; // ~one scanline of 68K time
        constexpr std::uint64_t irq_period = qsound_z80_clock_hz / qsound_irq_hz; // 32000
        std::uint64_t remaining = cpu_cycles;
        while (remaining > 0U) {
            const std::uint64_t slice = remaining < slice_cycles ? remaining : slice_cycles;
            remaining -= slice;

            main_cpu.tick(slice);

            cpu_cycle_accum_ += slice * qsound_z80_clock_hz;
            const std::uint64_t z80_cycles = cpu_cycle_accum_ / m68k_clock_hz;
            cpu_cycle_accum_ -= z80_cycles * m68k_clock_hz;
            if (z80_cycles > 0U) {
                qsound_irq_accum_ += z80_cycles;
                const bool fire = qsound_irq_accum_ >= irq_period;
                if (fire) {
                    qsound_irq_accum_ -= irq_period;
                    sound_cpu.set_irq_line(true);
                }
                sound_cpu.tick(z80_cycles);
                if (fire) {
                    sound_cpu.set_irq_line(false);
                }
            }
        }
    }

    common::rom_set_decl cps1_rom_skeleton(std::string set_name) {
        common::rom_set_decl decl;
        decl.name = std::move(set_name);
        decl.board = "capcom_cps1";
        decl.regions.push_back(
            {.name = "maincpu", .size = main_rom_size, .fill = 0xFFU, .files = {}});
        return decl;
    }

    std::unique_ptr<cps1_system> assemble_cps1(common::rom_set_image image,
                                               cps1_board_params board_params) {
        return std::make_unique<cps1_system>(std::move(image), board_params);
    }

} // namespace mnemos::manifests::capcom_cps1
