#include "capcom_cps2_system.hpp"

#include <algorithm>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace mnemos::manifests::capcom_cps2 {
    namespace {
        inline constexpr std::size_t cps_a_file_offset = 0x100U;
        inline constexpr std::size_t cps_b_file_offset = 0x140U;

        // `name` is a string_view (not a std::string or const std::string&) so a
        // literal callsite does not materialise a temporary at the call expression --
        // which GCC's -Wdangling-reference would flag as a (false) alias of the
        // returned reference.
        [[nodiscard]] std::vector<std::uint8_t>& region(common::rom_set_image& image,
                                                        std::string_view name) {
            return image.regions[std::string(name)];
        }

        // Resolve the board key: an explicit param wins; otherwise a 20-byte "key"
        // region in the set (the loaded .key asset) is used if present.
        [[nodiscard]] std::optional<std::array<std::uint8_t, crypto_key_size>>
        resolve_key(const cps2_board_params& params, common::rom_set_image& image) {
            if (params.key.has_value()) {
                return params.key;
            }
            const auto it = image.regions.find("key");
            if (it != image.regions.end() && it->second.size() == crypto_key_size) {
                std::array<std::uint8_t, crypto_key_size> k{};
                std::copy(it->second.begin(), it->second.end(), k.begin());
                return k;
            }
            return std::nullopt;
        }
        // One CPS-2 gfx bank: the recursive unshuffle is applied to each 0x200000
        // span independently.
        constexpr std::size_t gfx_bank_bytes = 0x200000U;
        constexpr std::size_t gfx_unit_bytes = 8U; // an 8-byte (64-bit) gfx unit
    } // namespace

    void cps2_system::unshuffle_gfx_units(std::span<std::uint8_t> units) noexcept {
        // Recursive de-interleave on 8-byte units (transcribed from the reference):
        // split in half, unshuffle each half, then swap the inner quarter of the
        // first half with the outer quarter of the second. A run with <=2 units or
        // a non-multiple-of-4 unit count is already in order.
        const std::size_t n = units.size() / gfx_unit_bytes;
        if (n <= 2U || (n & 3U) != 0U) {
            return;
        }
        const std::size_t half = n / 2U;
        unshuffle_gfx_units(units.subspan(0U, half * gfx_unit_bytes));
        unshuffle_gfx_units(units.subspan(half * gfx_unit_bytes, half * gfx_unit_bytes));
        std::uint8_t* buf = units.data();
        for (std::size_t i = 0U; i < half / 2U; ++i) {
            std::uint8_t tmp[gfx_unit_bytes];
            std::uint8_t* a = buf + (half / 2U + i) * gfx_unit_bytes;
            std::uint8_t* b = buf + (half + i) * gfx_unit_bytes;
            std::memcpy(tmp, a, gfx_unit_bytes);
            std::memcpy(a, b, gfx_unit_bytes);
            std::memcpy(b, tmp, gfx_unit_bytes);
        }
    }

    void cps2_system::map_cps_reg_window(std::uint32_t base, std::size_t file_offset) {
        // Latch the CPS-A / CPS-B register file and forward the decoded side
        // effects to the board-owned video chip.
        main_bus.map_mmio(
            base, static_cast<std::uint32_t>(cps_reg_block),
            [this, base, file_offset](std::uint32_t address) -> std::uint8_t {
                return cps_regs_[file_offset + (address - base)];
            },
            [this, base, file_offset](std::uint32_t address, std::uint8_t value) {
                const std::size_t rel = address - base;
                cps_regs_[file_offset + rel] = value;

                const std::size_t word_index = rel >> 1U;
                const std::uint16_t word = cps_reg_word(file_offset, word_index);
                if (file_offset == cps_a_file_offset && word_index < cps_a_regs_.size()) {
                    cps_a_regs_[word_index] = word;
                } else if (file_offset == cps_b_file_offset) {
                    video_.set_cps_b_reg(static_cast<std::uint8_t>(word_index), word);
                }
            },
            1);
    }

    cps2_system::cps2_system(common::rom_set_image image, cps2_board_params board_params)
        : roms(std::move(image)), params(std::move(board_params)) {
        // The encrypted 68000 program, mapped at $000000 for DATA reads.
        std::vector<std::uint8_t>& program = region(roms, "maincpu");

        // Build the decrypted opcode image. Default to the raw encrypted bytes so
        // the opcode overlay is always valid storage; a valid key overwrites it
        // with the decrypted stream and marks the board executable. Decryption
        // needs an even, non-empty program.
        opcode_image.assign(program.begin(), program.end());
        const auto key_bytes = resolve_key(params, roms);
        if (key_bytes.has_value() && !program.empty() && (program.size() & 1U) == 0U) {
            cps2_crypto_key key{};
            if (decode_key(*key_bytes, key) && decrypt_opcodes(program, opcode_image, key)) {
                executable_ = true;
            }
        }

        if (!program.empty()) {
            main_bus.map_rom(program_base, std::span<const std::uint8_t>(program), 0);
            main_bus.map_opcode_rom(program_base, std::span<const std::uint8_t>(opcode_image));
        }

        // RAM regions (priority 1 = overlay over the program ROM image; none
        // actually overlap it, but keeps them authoritative).
        main_bus.map_ram(main_ram_base, std::span<std::uint8_t>(work_ram_), 1);
        main_bus.map_ram(video_ram_base, std::span<std::uint8_t>(video_ram_), 1);
        main_bus.map_ram(object_ram_base, std::span<std::uint8_t>(object_ram_), 1);
        main_bus.map_ram(extra_ram_base, std::span<std::uint8_t>(extra_ram_), 1);
        main_bus.map_ram(extra_ctrl_base, std::span<std::uint8_t>(extra_control_), 1);
        main_bus.map_ram(control_reg_base, std::span<std::uint8_t>(control_regs_), 1);
        // QSound 68K<->Z80 comm RAM: the 68K sees the 4 KiB buffer on the ODD byte
        // of an 8 KiB window (index = (addr-base)>>1); the Z80 sees it flat at $C000.
        main_bus.map_mmio(
            qsound_shared_base, static_cast<std::uint32_t>(qsound_shared_window),
            [this](std::uint32_t address) -> std::uint8_t {
                if ((address & 1U) == 0U) {
                    return 0xFFU; // even bytes read open bus
                }
                const std::uint32_t index = (address - qsound_shared_base) >> 1U;
                return index < qsound_shared_ram_.size() ? qsound_shared_ram_[index] : 0xFFU;
            },
            [this](std::uint32_t address, std::uint8_t value) {
                if ((address & 1U) == 0U) {
                    return;
                }
                const std::uint32_t index = (address - qsound_shared_base) >> 1U;
                if (index < qsound_shared_ram_.size()) {
                    qsound_shared_ram_[index] = value;
                }
            },
            1);

        // CPS-A / CPS-B register files, reachable via the primary + legacy mirror.
        map_cps_reg_window(cps_a_base, cps_a_file_offset);
        map_cps_reg_window(cps_b_base, cps_b_file_offset);
        map_cps_reg_window(cps_a_mirror_base, cps_a_file_offset);
        map_cps_reg_window(cps_b_mirror_base, cps_b_file_offset);

        // I/O: inputs (active-low) + QSound volume status + serial EEPROM. The byte
        // handlers mirror the reference's word ports decoded to bytes.
        main_bus.map_mmio(
            cps_io_base, static_cast<std::uint32_t>(cps_io_size),
            [this](std::uint32_t address) -> std::uint8_t {
                std::uint16_t word = 0xFFFFU;
                switch (address & 0xFFFFFEU) {
                case 0x804000U:
                    word = input0;
                    break;
                case 0x804010U:
                    word = input1;
                    break;
                case 0x804020U:
                    // System (start/coin) inputs; bit 0 is the EEPROM data-out.
                    word = input_sys;
                    if (!eeprom_.data_out()) {
                        word = static_cast<std::uint16_t>(word & ~0x0001U);
                    }
                    break;
                case 0x804030U:
                    word = qsound_volume_status;
                    break;
                default:
                    word = 0xFFFFU;
                    break;
                }
                return (address & 1U) != 0U ? static_cast<std::uint8_t>(word)
                                            : static_cast<std::uint8_t>(word >> 8U);
            },
            [this](std::uint32_t address, std::uint8_t value) {
                switch (address & 0xFFFFFFU) {
                case 0x804040U: // serial EEPROM: DI bit4, CLK bit5, CS bit6
                    eeprom_.update((value & eeprom_cs_bit) != 0U, (value & eeprom_clk_bit) != 0U,
                                   (value & eeprom_di_bit) != 0U);
                    break;
                case 0x804041U: // sound-CPU reset: bit3 set = run, clear = hold in reset
                    if ((value & 0x08U) != 0U) {
                        // Release: start the Z80 fresh from its reset vector.
                        if (sound_reset_asserted_) {
                            sound_cpu_.reset(chips::reset_kind::power_on);
                            sound_cpu_.set_irq_line(false);
                            sound_cycle_debt_ = 0;
                            qsound_irq_line_ = false;
                            qsound_irq_accum_ = 0U;
                        }
                        sound_reset_asserted_ = false;
                    } else {
                        sound_reset_asserted_ = true;
                        sound_cpu_.reset(chips::reset_kind::power_on);
                        sound_cpu_.set_irq_line(false);
                        qsound_irq_line_ = false;
                        qsound_irq_accum_ = 0U;
                    }
                    break;
                case 0x8040E0U:
                case 0x8040E1U:
                    object_bank_ = static_cast<std::uint8_t>(value & 1U);
                    break;
                default:
                    break;
                }
            },
            1);

        setup_sound();

        // Video: the CPS-2 chip reads the tile/attribute RAM ($900000) for the
        // scroll name tables + the palette DMA source, and the packed gfx ROM for
        // tile art. (The vblank IRQ is raised by run_frame, not the chip.)
        // The gfx mask ROMs load word/byte-lane interleaved; de-scramble each full
        // 0x200000 bank in place into the linear tile layout the decoder reads.
        auto& gfx = region(roms, "gfx");
        for (std::size_t base = 0U; base + gfx_bank_bytes <= gfx.size(); base += gfx_bank_bytes) {
            unshuffle_gfx_units(std::span<std::uint8_t>(gfx).subspan(base, gfx_bank_bytes));
        }
        video_.attach_gfx(std::span<const std::uint8_t>(gfx));
        video_.attach_video_ram(std::span<const std::uint8_t>(video_ram_));
        video_.attach_object_ram(std::span<const std::uint8_t>(object_ram_));

        main_cpu.attach_bus(main_bus);
        main_cpu.set_irq_ack_callback([this](int /*level*/) {
            main_cpu.set_irq_level(0);
            ++vblank_irq_acked_;
        });
        // Reset reads the vector ($0 SSP / $4 PC) through the opcode path, so on a
        // keyed board it boots from the decrypted image.
        main_cpu.reset(chips::reset_kind::power_on);
    }

    void cps2_system::setup_sound() {
        std::vector<std::uint8_t>& sound_rom = region(roms, "audiocpu");
        sound_rom_size_ = static_cast<std::uint32_t>(sound_rom.size());
        if (sound_rom_size_ == 0U) {
            return; // no sound program in this set (skeleton / synthetic data path)
        }
        const std::span<const std::uint8_t> rom_span{sound_rom};
        const std::size_t low = std::min<std::size_t>(sound_rom.size(), z80_rom_window);

        // Fixed low 32 KiB + the $D003-banked 16 KiB window from $10000 up.
        sound_bus_.map_rom(z80_rom_base, rom_span.first(low), 0);
        sound_bus_.map_mmio(
            z80_bank_base, z80_bank_window,
            [this, rom_span](std::uint32_t address) -> std::uint8_t {
                const std::uint32_t rom_addr = z80_bank_rom_base +
                                               (sound_bank_ & z80_bank_mask) * z80_bank_window +
                                               (address - z80_bank_base);
                return rom_addr < rom_span.size() ? rom_span[rom_addr] : 0xFFU;
            },
            [](std::uint32_t, std::uint8_t) {}, 0);

        // Comm RAM ($C000), Z80 scratch ($D000), work RAM ($F000).
        sound_bus_.map_ram(z80_shared_base, std::span<std::uint8_t>(qsound_shared_ram_), 0);
        sound_bus_.map_ram(z80_ram_base, std::span<std::uint8_t>(z80_ram_), 0);
        sound_bus_.map_ram(z80_work_base, std::span<std::uint8_t>(qsound_work_ram_), 0);

        // DL-1425 ports ($D000-$D002 = the 3-port DSP interface; $D003 = bank
        // select; $D007 = ready flag). Priority 1 overlays the $D000 scratch RAM.
        sound_bus_.map_mmio(
            z80_port_base, 0x8U,
            [this](std::uint32_t address) -> std::uint8_t {
                if (address == z80_ready_reg) {
                    return qdsp_.read_status();
                }
                return z80_ram_[address - z80_ram_base];
            },
            [this](std::uint32_t address, std::uint8_t value) {
                if (address == z80_bank_reg) {
                    sound_bank_ = static_cast<std::uint8_t>(value & z80_bank_mask);
                } else if (address >= z80_port_base && address <= z80_port_base + 2U) {
                    qdsp_.write_port(static_cast<std::uint8_t>(address - z80_port_base), value);
                } else {
                    z80_ram_[address - z80_ram_base] = value;
                }
            },
            1);

        sound_cpu_.attach_bus(sound_bus_);
        qdsp_.set_sample_rom(std::span<const std::uint8_t>(region(roms, "qsound")));
    }

    common::rom_set_decl cps2_rom_skeleton(std::string set_name) {
        common::rom_set_decl decl;
        decl.name = std::move(set_name);
        decl.board = "capcom_cps2";
        decl.regions.push_back(
            {.name = "maincpu", .size = main_rom_size, .fill = 0xFFU, .files = {}});
        decl.regions.push_back(
            {.name = "key", .size = crypto_key_size, .fill = 0x00U, .files = {}});
        return decl;
    }

    void cps2_system::run_cycles(std::uint64_t cycles) {
        if (!executable_) {
            return;
        }
        // The 68K (~11.8 MHz) drives; the sound Z80 (~8 MHz) catches up at the clock
        // ratio when out of reset, and the DSP advances with it. The Z80 sound
        // driver does its work in the 250 Hz DL-1425 /INT handler, so the loop
        // pulses that interrupt (raise on the period, release on the accept edge --
        // a taken IM1 interrupt clears IFF1).
        std::uint64_t ran = 0U;
        while (ran < cycles) {
            const int spent = main_cpu.step_instruction();
            const std::uint64_t step = spent > 0 ? static_cast<std::uint64_t>(spent) : 1U;
            ran += step;
            if (sound_rom_size_ != 0U && !sound_reset_asserted_) {
                // Z80 cycles owed ~= 68K cycles * 8 / 12 (11.8/8 ratio).
                sound_cycle_debt_ += static_cast<std::int64_t>(step) * 8 / 12;
                while (sound_cycle_debt_ > 0) {
                    const bool ack_possible = qsound_irq_line_ && sound_cpu_.iff1();
                    sound_cpu_.set_irq_line(qsound_irq_line_);
                    const int zc = sound_cpu_.step_instruction();
                    const int zstep = zc > 0 ? zc : 1;
                    sound_cycle_debt_ -= zstep;
                    // A taken /INT clears IFF1: release the level line so it is a pulse.
                    if (ack_possible && !sound_cpu_.iff1()) {
                        qsound_irq_line_ = false;
                        sound_cpu_.set_irq_line(false);
                    }
                    qsound_irq_accum_ += static_cast<std::uint32_t>(zstep);
                    while (qsound_irq_accum_ >= qsound_irq_period) {
                        qsound_irq_accum_ -= qsound_irq_period;
                        qsound_irq_line_ = true;
                    }
                }
                qdsp_.tick(step);
            }
        }
    }

    std::uint16_t cps2_system::cps_reg_word(std::size_t file_offset,
                                            std::size_t word_index) const noexcept {
        const std::size_t off = file_offset + word_index * 2U;
        return static_cast<std::uint16_t>((static_cast<std::uint16_t>(cps_regs_[off]) << 8U) |
                                          cps_regs_[off + 1U]);
    }

    std::uint32_t cps2_system::video_ram_base_from_reg(std::uint16_t reg) const noexcept {
        const std::uint32_t addr = static_cast<std::uint32_t>(reg) << 8U;
        if (addr >= video_ram_base && addr < video_ram_base + video_ram_size) {
            return addr - video_ram_base;
        }
        return static_cast<std::uint32_t>(reg) * 256U;
    }

    std::uint32_t cps2_system::video_ram_base_aligned(std::uint16_t reg,
                                                      std::uint32_t boundary) const noexcept {
        std::uint32_t base = video_ram_base_from_reg(reg);
        if (boundary > 1U) {
            base &= ~(boundary - 1U);
        }
        return base;
    }

    void cps2_system::push_cps_a_to_video() noexcept {
        // CPS-2 stores sprites in the dedicated $700000 object-RAM window; the
        // object bank latch selects the active 0x2000-byte object table. The
        // sprite x/y coordinate base comes from control regs 0x08/0x0A.
        video_.set_object_base(static_cast<std::uint32_t>(object_bank_ & 1U) * object_bank_bytes);
        video_.set_sprite_offsets(control_reg_word(0x08U), control_reg_word(0x0AU));
        // Priority compositor input: the object priority-control word (control reg
        // 0x04). Scroll layer enable comes from the CPS-B layer-control word.
        video_.set_object_priority(control_reg_word(0x04U));
        video_.set_scroll1_base(
            video_ram_base_aligned(cps_a_regs_[cps_a_scroll1_base], scroll_base_align));
        video_.set_scroll2_base(
            video_ram_base_aligned(cps_a_regs_[cps_a_scroll2_base], scroll_base_align));
        video_.set_scroll3_base(
            video_ram_base_aligned(cps_a_regs_[cps_a_scroll3_base], scroll_base_align));

        video_.set_scroll1(cps_a_regs_[cps_a_scroll1_x], cps_a_regs_[cps_a_scroll1_y]);
        video_.set_scroll2(cps_a_regs_[cps_a_scroll2_x], cps_a_regs_[cps_a_scroll2_y]);
        video_.set_scroll3(cps_a_regs_[cps_a_scroll3_x], cps_a_regs_[cps_a_scroll3_y]);

        const std::uint16_t video_control = cps_a_regs_[cps_a_video_control];
        video_.set_rowscroll(
            (video_control & 0x0001U) != 0U,
            video_ram_base_aligned(cps_a_regs_[cps_a_rowscroll_base], other_base_align),
            cps_a_regs_[cps_a_rowscroll_offset]);
        video_.set_video_control(video_control);
        video_.set_display_enable(true);
    }

    std::uint16_t cps2_system::control_reg_word(std::size_t offset) const noexcept {
        if (offset + 1U >= control_regs_.size()) {
            return 0U;
        }
        return static_cast<std::uint16_t>(
            (static_cast<std::uint16_t>(control_regs_[offset]) << 8U) | control_regs_[offset + 1U]);
    }

    std::uint32_t cps2_system::palette_source() const noexcept {
        const std::uint16_t reg = cps_a_regs_[cps_a_palette_base];
        return video_ram_base_from_reg(reg) & ~(palette_page_bytes - 1U);
    }

    void cps2_system::run_frame() {
        constexpr std::uint64_t cpu_cycles_per_frame = m68k_clock_hz / frame_rate_hz;
        constexpr std::uint32_t frame_lines = 262U; // total scanlines
        constexpr std::uint32_t visible_lines = chips::video::cps2_video::visible_height;
        constexpr std::uint64_t cpu_visible = cpu_cycles_per_frame * visible_lines / frame_lines;

        const auto run_main = [this](std::uint64_t cycles) {
            if (executable_) {
                run_cycles(cycles);
            }
        };

        // Run the visible field, then at vblank latch the CPS-A state, render the
        // frame from the current palette source, and raise the level-2 IRQ the game
        // services during vblank.
        run_main(cpu_visible);
        push_cps_a_to_video();
        video_.render(palette_source(), palette_control_default);
        main_cpu.set_irq_level(2);
        ++vblank_irq_raised_;
        run_main(cpu_cycles_per_frame - cpu_visible);
    }

} // namespace mnemos::manifests::capcom_cps2
