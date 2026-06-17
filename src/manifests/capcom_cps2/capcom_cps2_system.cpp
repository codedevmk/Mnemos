#include "capcom_cps2_system.hpp"

#include <span>
#include <string>
#include <utility>

namespace mnemos::manifests::capcom_cps2 {
    namespace {
        // Pass the region name by value (see the CPS-1 assembler note: a string_view
        // overload would bind a temporary to a reference param under
        // GCC -Wdangling-reference).
        [[nodiscard]] std::vector<std::uint8_t>& region(common::rom_set_image& image,
                                                        std::string name) {
            return image.regions[std::move(name)];
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
    } // namespace

    void cps2_system::map_cps_reg_window(std::uint32_t base, std::size_t file_offset) {
        // Latch the CPS-A / CPS-B register file (the video decode reads it later).
        main_bus.map_mmio(
            base, static_cast<std::uint32_t>(cps_reg_block),
            [this, base, file_offset](std::uint32_t address) -> std::uint8_t {
                return cps_regs_[file_offset + (address - base)];
            },
            [this, base, file_offset](std::uint32_t address, std::uint8_t value) {
                cps_regs_[file_offset + (address - base)] = value;
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
        map_cps_reg_window(cps_a_base, 0x100U);
        map_cps_reg_window(cps_b_base, 0x140U);
        map_cps_reg_window(cps_a_mirror_base, 0x100U);
        map_cps_reg_window(cps_b_mirror_base, 0x140U);

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
                        }
                        sound_reset_asserted_ = false;
                    } else {
                        sound_reset_asserted_ = true;
                        sound_cpu_.reset(chips::reset_kind::power_on);
                        sound_cpu_.set_irq_line(false);
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

        main_cpu.attach_bus(main_bus);
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

    void cps2_system::run_cycles(std::uint64_t cycles) {
        // The 68K (~11.8 MHz) drives; the sound Z80 (~8 MHz) catches up at the clock
        // ratio when out of reset, and the DSP advances with it. (The exact QSound
        // /INT cadence + audio mixing land with the first real-ROM bring-up.)
        std::uint64_t ran = 0U;
        while (ran < cycles) {
            const int spent = main_cpu.step_instruction();
            const std::uint64_t step = spent > 0 ? static_cast<std::uint64_t>(spent) : 1U;
            ran += step;
            if (sound_rom_size_ != 0U && !sound_reset_asserted_) {
                // Z80 cycles owed ~= 68K cycles * 8 / 12 (11.8/8 ratio).
                sound_cycle_debt_ += static_cast<std::int64_t>(step) * 8 / 12;
                while (sound_cycle_debt_ > 0) {
                    const int zc = sound_cpu_.step_instruction();
                    sound_cycle_debt_ -= zc > 0 ? zc : 1;
                }
                qdsp_.tick(step);
            }
        }
    }

} // namespace mnemos::manifests::capcom_cps2
