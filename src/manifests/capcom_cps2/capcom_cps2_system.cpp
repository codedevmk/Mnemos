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
        // QSound 68K<->Z80 shared RAM (the Z80 side + odd/even protocol is phase 6).
        main_bus.map_ram(qsound_shared_base, std::span<std::uint8_t>(qsound_shared_), 1);

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
                case 0x8040E0U:
                case 0x8040E1U:
                    object_bank_ = static_cast<std::uint8_t>(value & 1U);
                    break;
                default: // $804041 sound-CPU reset etc. land in later phases
                    break;
                }
            },
            1);

        main_cpu.attach_bus(main_bus);
        // Reset reads the vector ($0 SSP / $4 PC) through the opcode path, so on a
        // keyed board it boots from the decrypted image.
        main_cpu.reset(chips::reset_kind::power_on);
    }

    void cps2_system::run_cycles(std::uint64_t cycles) {
        std::uint64_t ran = 0U;
        while (ran < cycles) {
            const int spent = main_cpu.step_instruction();
            ran += spent > 0 ? static_cast<std::uint64_t>(spent) : 1U;
        }
    }

} // namespace mnemos::manifests::capcom_cps2
