#include "capcom_cps2_system.hpp"

#include "state.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
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
        constexpr std::uint32_t cps2_system_state_version = 8U;
        constexpr std::uint32_t m68k_address_mask = 0x00FFFFFFU;
        constexpr int m68k_vblank_irq_level = 2;
        constexpr const char* qsound_live_trace_limit_env = "MNEMOS_CPS2_QSOUND_LIVE_TRACE";
        constexpr const char* qsound_live_trace_path_env = "MNEMOS_CPS2_QSOUND_TRACE_PATH";
        constexpr std::uint32_t qsound_live_trace_default_limit = 50000U;

        [[nodiscard]] const char* getenv_compat(const char* name) noexcept {
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
            const char* value = std::getenv(name);
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
            return value;
        }

        [[nodiscard]] std::FILE* fopen_append_compat(const char* path) noexcept {
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
            std::FILE* file = std::fopen(path, "ab");
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
            return file;
        }

        [[nodiscard]] std::uint32_t object_ram_index(std::uint32_t address,
                                                     std::uint8_t object_bank,
                                                     std::uint32_t window_base,
                                                     std::uint8_t bank_selector) noexcept {
            std::uint32_t local = address - window_base;
            if (bank_selector != 0U) {
                local &= object_bank_bytes - 1U;
            }
            const std::uint32_t bank = (static_cast<std::uint32_t>(object_bank & 1U) ^
                                        static_cast<std::uint32_t>(bank_selector & 1U));
            return bank * object_bank_bytes + local;
        }

        [[nodiscard]] std::uint32_t read_be32(std::span<const std::uint8_t> bytes,
                                              std::size_t offset) noexcept {
            if (offset + 3U >= bytes.size()) {
                return 0U;
            }
            return (static_cast<std::uint32_t>(bytes[offset]) << 24U) |
                   (static_cast<std::uint32_t>(bytes[offset + 1U]) << 16U) |
                   (static_cast<std::uint32_t>(bytes[offset + 2U]) << 8U) |
                   static_cast<std::uint32_t>(bytes[offset + 3U]);
        }

        [[nodiscard]] bool address_in_program_image(std::size_t image_size,
                                                    std::uint32_t address) noexcept {
            return (address & 1U) == 0U && (address & m68k_address_mask) < image_size;
        }

        [[nodiscard]] bool reset_vectors_executable(std::span<const std::uint8_t> image) noexcept {
            if (image.size() < 8U) {
                return false;
            }
            const std::uint32_t reset_ssp = read_be32(image, 0U);
            const std::uint32_t reset_pc = read_be32(image, 4U);
            return (reset_ssp & 1U) == 0U && address_in_program_image(image.size(), reset_pc);
        }

        [[nodiscard]] bool valid_analog_input_mode(std::uint8_t value) noexcept {
            switch (static_cast<cps2_analog_input_mode>(value)) {
            case cps2_analog_input_mode::none:
            case cps2_analog_input_mode::eco_fighters:
            case cps2_analog_input_mode::puzz_loop_2:
                return true;
            }
            return false;
        }

        [[nodiscard]] std::uint32_t qsound_live_trace_limit() noexcept {
            static bool initialized = false;
            static std::uint32_t limit = 0U;

            if (!initialized) {
                initialized = true;
                const char* env = getenv_compat(qsound_live_trace_limit_env);
                if (env != nullptr && env[0] != '\0' && env[0] != '0') {
                    char* end = nullptr;
                    const unsigned long parsed = std::strtoul(env, &end, 0);
                    limit = (end != env && parsed > 1UL)
                                ? static_cast<std::uint32_t>(
                                      std::min<unsigned long>(parsed, 1000000UL))
                                : qsound_live_trace_default_limit;
                }
            }

            return limit;
        }

        [[nodiscard]] std::FILE* qsound_live_trace_file() noexcept {
            static bool opened = false;
            static std::FILE* file = nullptr;

            if (qsound_live_trace_limit() == 0U) {
                return nullptr;
            }

            if (!opened) {
                opened = true;
                const char* path = getenv_compat(qsound_live_trace_path_env);
                if (path == nullptr || path[0] == '\0') {
                    path = "build/scratch/cps2_qsound_live_trace.txt";
                }
                file = fopen_append_compat(path);
                if (file == nullptr) {
                    file = stderr;
                }
            }

            return file;
        }

        [[nodiscard]] bool qsound_live_trace_begin(std::FILE** out_file) noexcept {
            static std::uint32_t count = 0U;
            const std::uint32_t limit = qsound_live_trace_limit();
            if (limit == 0U || count >= limit) {
                return false;
            }

            std::FILE* file = qsound_live_trace_file();
            if (file == nullptr) {
                return false;
            }

            ++count;
            std::fprintf(file, "[mnemos cps2 qsound] #%u ", static_cast<unsigned>(count));
            *out_file = file;
            return true;
        }

        [[nodiscard]] bool qsound_live_trace_shared_index_is_interesting(
            std::uint16_t index) noexcept {
            return index < 0x0010U;
        }

        [[nodiscard]] bool qsound_live_trace_work_addr_is_interesting(
            std::uint16_t address) noexcept {
            if (address < z80_work_base) {
                return false;
            }
            const std::uint16_t offset = static_cast<std::uint16_t>(address - z80_work_base);
            return offset < 0x0010U || (offset >= 0x0100U && offset < 0x0160U);
        }

        void qsound_live_trace_bytes(std::FILE* file,
                                     const char* label,
                                     std::span<const std::uint8_t> bytes,
                                     std::size_t offset,
                                     std::size_t count) noexcept {
            std::fprintf(file, " %s=", label);
            for (std::size_t i = 0U; i < count; ++i) {
                const std::size_t index = offset + i;
                const unsigned value = index < bytes.size() ? bytes[index] : 0xFFU;
                std::fprintf(file, "%02X", value);
            }
        }

        void qsound_live_trace_snapshot(std::FILE* file,
                                        std::span<const std::uint8_t> work_ram,
                                        std::span<const std::uint8_t> shared_ram) noexcept {
            qsound_live_trace_bytes(file, "f000", work_ram, 0x0000U, 0x0010U);
            qsound_live_trace_bytes(file, "f140", work_ram, 0x0140U, 0x0010U);
            qsound_live_trace_bytes(file, "f150", work_ram, 0x0150U, 0x0010U);
            qsound_live_trace_bytes(file, "c000", shared_ram, 0x0000U, 0x0010U);
        }

        void qsound_live_trace_z80_event(const char* kind,
                                         std::uint16_t address,
                                         std::uint16_t index,
                                         std::uint8_t value,
                                         std::uint16_t pc,
                                         std::uint64_t sound_cycles,
                                         std::uint8_t bank,
                                         std::span<const std::uint8_t> work_ram,
                                         std::span<const std::uint8_t> shared_ram) noexcept {
            std::FILE* file = nullptr;
            if (!qsound_live_trace_begin(&file)) {
                return;
            }
            std::fprintf(file,
                         "%s addr=%04X index=%04X value=%02X pc=%04X sndcyc=%llu bank=%u",
                         kind,
                         static_cast<unsigned>(address),
                         static_cast<unsigned>(index),
                         static_cast<unsigned>(value),
                         static_cast<unsigned>(pc),
                         static_cast<unsigned long long>(sound_cycles),
                         static_cast<unsigned>(bank));
            qsound_live_trace_snapshot(file, work_ram, shared_ram);
            std::fprintf(file, "\n");
        }

        void qsound_live_trace_68k_shared_write(std::uint16_t index,
                                                std::uint8_t value,
                                                std::uint32_t pc,
                                                std::uint64_t main_cycles,
                                                std::span<const std::uint8_t> work_ram,
                                                std::span<const std::uint8_t> shared_ram) noexcept {
            if (!qsound_live_trace_shared_index_is_interesting(index)) {
                return;
            }

            std::FILE* file = nullptr;
            if (!qsound_live_trace_begin(&file)) {
                return;
            }
            std::fprintf(file,
                         "68KSHW index=%04X value=%02X pc=%06X maincyc=%llu",
                         static_cast<unsigned>(index),
                         static_cast<unsigned>(value),
                         static_cast<unsigned>(pc),
                         static_cast<unsigned long long>(main_cycles));
            qsound_live_trace_snapshot(file, work_ram, shared_ram);
            std::fprintf(file, "\n");
        }

        void qsound_live_trace_bank_write(std::uint8_t raw,
                                          std::uint8_t bank,
                                          std::uint16_t pc,
                                          std::uint64_t sound_cycles,
                                          std::span<const std::uint8_t> work_ram,
                                          std::span<const std::uint8_t> shared_ram) noexcept {
            std::FILE* file = nullptr;
            if (!qsound_live_trace_begin(&file)) {
                return;
            }
            std::fprintf(file,
                         "BANK raw=%02X bank=%u pc=%04X sndcyc=%llu",
                         static_cast<unsigned>(raw),
                         static_cast<unsigned>(bank),
                         static_cast<unsigned>(pc),
                         static_cast<unsigned long long>(sound_cycles));
            qsound_live_trace_snapshot(file, work_ram, shared_ram);
            std::fprintf(file, "\n");
        }

        void qsound_live_trace_register(std::uint8_t reg,
                                        std::uint16_t data,
                                        std::uint16_t pc,
                                        std::uint64_t sound_cycles,
                                        std::uint8_t bank,
                                        std::span<const std::uint8_t> work_ram,
                                        std::span<const std::uint8_t> shared_ram) noexcept {
            std::FILE* file = nullptr;
            if (!qsound_live_trace_begin(&file)) {
                return;
            }
            std::fprintf(file,
                         "REG reg=%02X data=%04X pc=%04X sndcyc=%llu bank=%u",
                         static_cast<unsigned>(reg),
                         static_cast<unsigned>(data),
                         static_cast<unsigned>(pc),
                         static_cast<unsigned long long>(sound_cycles),
                         static_cast<unsigned>(bank));
            qsound_live_trace_snapshot(file, work_ram, shared_ram);
            std::fprintf(file, "\n");
        }
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
        : roms(std::move(image)), params(std::move(board_params)),
          analog_input_mode_(params.analog_input) {
        // The encrypted 68000 program, mapped at $000000 for DATA reads.
        std::vector<std::uint8_t>& program = region(roms, "maincpu");

        // Build the decrypted opcode image. Default to the raw encrypted bytes so
        // the opcode overlay is always valid storage; a valid key overwrites it
        // with the decrypted stream. The board only becomes executable when the
        // decrypted reset vectors are safe; wrong regional keys can decrypt to
        // bytes that are not a bootable CPS-2 program.
        opcode_image.assign(program.begin(), program.end());
        const auto key_bytes = resolve_key(params, roms);
        if (key_bytes.has_value() && !program.empty() && (program.size() & 1U) == 0U) {
            cps2_crypto_key key{};
            if (decode_key(*key_bytes, key) && decrypt_opcodes(program, opcode_image, key)) {
                executable_ =
                    reset_vectors_executable(std::span<const std::uint8_t>(opcode_image));
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
        main_bus.map_ram(extra_ram_base, std::span<std::uint8_t>(extra_ram_), 1);
        main_bus.map_ram(extra_ctrl_base, std::span<std::uint8_t>(extra_control_), 1);
        main_bus.map_ram(control_reg_base, std::span<std::uint8_t>(control_regs_), 1);
        const auto map_object_window = [this](std::uint32_t base, std::uint32_t size,
                                              std::uint8_t bank_selector) {
            main_bus.map_mmio(
                base, size,
                [this, base, bank_selector](std::uint32_t address) -> std::uint8_t {
                    const std::uint32_t index =
                        object_ram_index(address, object_bank_, base, bank_selector);
                    return index < object_ram_.size() ? object_ram_[index] : 0xFFU;
                },
                [this, base, bank_selector](std::uint32_t address, std::uint8_t value) {
                    const std::uint32_t index =
                        object_ram_index(address, object_bank_, base, bank_selector);
                    if (index < object_ram_.size()) {
                        object_ram_[index] = value;
                    }
                },
                1);
        };
        // CPS-2 exposes an 8 KiB selected object bank at $700000 and a 32 KiB
        // alternate mirror at $708000 that addresses the opposite bank.
        map_object_window(object_ram_base, object_bank_bytes, 0U);
        map_object_window(object_ram_alt_base, object_ram_alt_window_bytes, 1U);
        // QSound 68K<->Z80 comm RAM: the 68K sees the 4 KiB buffer on the ODD byte
        // of an 8 KiB window (index = (addr-base)>>1); the Z80 sees it flat at $C000.
        main_bus.map_mmio(
            qsound_shared_base, static_cast<std::uint32_t>(qsound_shared_window),
            [this](std::uint32_t address) -> std::uint8_t {
                const std::uint32_t index = (address - qsound_shared_base) >> 1U;
                std::uint8_t value = 0xFFU;
                ++qsound_bus_.shared_68k_read_count;
                if (index == 0x0FFFU) {
                    ++qsound_bus_.shared_68k_status_read_count;
                } else if (index == 0x0FFDU) {
                    ++qsound_bus_.shared_68k_magic_read_count;
                }
                if ((address & 1U) == 0U) {
                    ++qsound_bus_.shared_68k_even_read_count;
                } else {
                    ++qsound_bus_.shared_68k_odd_read_count;
                    value = index < qsound_shared_ram_.size() ? qsound_shared_ram_[index] : 0xFFU;
                }
                const std::uint32_t pc = main_cpu.current_instruction_addr() & m68k_address_mask;
                if (index == 0x0FFFU) {
                    qsound_bus_.shared_status_last_read_value = value;
                    qsound_bus_.shared_status_last_read_pc = pc;
                    if (!qsound_bus_.shared_status_first_read_seen) {
                        qsound_bus_.shared_status_first_read_seen = true;
                        qsound_bus_.shared_status_first_read_value = value;
                        qsound_bus_.shared_status_first_read_pc = pc;
                    }
                }
                qsound_bus_.shared_last_68k_read_index = static_cast<std::uint16_t>(index);
                qsound_bus_.shared_last_68k_read_value = value;
                qsound_bus_.shared_last_68k_read_pc = pc;
                return value;
            },
            [this](std::uint32_t address, std::uint8_t value) {
                const std::uint32_t index = (address - qsound_shared_base) >> 1U;
                if (index >= qsound_shared_ram_.size()) {
                    return;
                }
                const std::uint32_t pc = main_cpu.current_instruction_addr() & m68k_address_mask;
                if ((address & 1U) == 0U) {
                    ++qsound_bus_.shared_68k_even_write_count;
                    qsound_bus_.shared_last_even_68k_index = static_cast<std::uint16_t>(index);
                    qsound_bus_.shared_last_even_68k_value = value;
                    if (value != 0xFFU) {
                        ++qsound_bus_.shared_68k_even_non_ff_write_count;
                    }
                    return;
                }
                qsound_shared_ram_[index] = value;
                ++qsound_bus_.shared_68k_write_count;
                qsound_bus_.shared_last_68k_write_pc = pc;
                if (value != 0xFFU) {
                    ++qsound_bus_.shared_68k_non_ff_write_count;
                    qsound_bus_.shared_last_68k_non_ff_write_pc = pc;
                }
                if (index == 0x000FU) {
                    ++qsound_bus_.shared_68k_command_signal_write_count;
                    qsound_bus_.shared_command_signal_last_68k_value = value;
                    qsound_bus_.shared_command_signal_last_68k_pc = pc;
                    std::copy_n(qsound_shared_ram_.begin(),
                                qsound_bus_.shared_command_snapshot.size(),
                                qsound_bus_.shared_command_snapshot.begin());
                }
                qsound_bus_.shared_last_68k_index = static_cast<std::uint16_t>(index);
                qsound_bus_.shared_last_68k_value = value;
                qsound_live_trace_68k_shared_write(static_cast<std::uint16_t>(index),
                                                   value,
                                                   pc,
                                                   main_cpu.elapsed_cycles(),
                                                   std::span<const std::uint8_t>(
                                                       qsound_work_ram_),
                                                   std::span<const std::uint8_t>(
                                                       qsound_shared_ram_));
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
                if (address >= development_dip_base &&
                    address < development_dip_base + development_dip_size) {
                    return development_dips_[address - development_dip_base];
                }
                std::uint16_t word = 0xFFFFU;
                switch (address & 0xFFFFFEU) {
                case 0x804000U:
                    word = input0_read_word((address & 1U) == 0U);
                    break;
                case 0x804010U:
                    word = input1;
                    break;
                case 0x804020U:
                    // System (start/coin) inputs; bit 0 is the EEPROM data-out.
                    word = eeprom_.data_out()
                               ? static_cast<std::uint16_t>(input_sys | 0x0001U)
                               : static_cast<std::uint16_t>(input_sys & ~0x0001U);
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
                    if (analog_input_mode_ == cps2_analog_input_mode::eco_fighters) {
                        read_paddle_ = (value & 0x01U) != 0U;
                    }
                    break;
                case 0x804041U: // sound-CPU reset: bit3 set = run, clear = hold in reset
                    if (analog_input_mode_ == cps2_analog_input_mode::puzz_loop_2) {
                        read_paddle_ = (value & 0x02U) != 0U;
                    }
                    update_coin_outputs(value);
                    if ((value & 0x08U) != 0U) {
                        // Release: start the Z80 fresh from its reset vector.
                        if (sound_reset_asserted_) {
                            sound_cpu_.reset(chips::reset_kind::power_on);
                            sound_cpu_.set_irq_line(false);
                            sound_cycle_debt_ = 0;
                            sound_cycle_accum_ = 0U;
                            qsound_irq_line_ = false;
                            qsound_irq_accum_ = 0U;
                        }
                        sound_reset_asserted_ = false;
                    } else {
                        sound_reset_asserted_ = true;
                        sound_cpu_.reset(chips::reset_kind::power_on);
                        sound_cpu_.set_irq_line(false);
                        sound_cycle_debt_ = 0;
                        sound_cycle_accum_ = 0U;
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
        video_.set_zero_layer_control_defaults(false);
        video_.attach_gfx(std::span<const std::uint8_t>(gfx));
        video_.attach_video_ram(std::span<const std::uint8_t>(video_ram_));
        video_.attach_object_ram(std::span<const std::uint8_t>(object_ram_));

        main_cpu.attach_bus(main_bus);
        main_cpu.set_irq_ack_callback([this](int level) {
            main_cpu.set_irq_level(0);
            if (level == m68k_vblank_irq_level) {
                ++vblank_irq_acked_;
            }
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
        if (sound_rom_size_ > 0x20000U && sound_rom_size_ < z80_qsound_cpu_rom_region_size) {
            sound_rom.resize(z80_qsound_cpu_rom_region_size, 0x00U);
            sound_rom_size_ = static_cast<std::uint32_t>(sound_rom.size());
        }
        const std::span<const std::uint8_t> rom_span{sound_rom};
        const std::size_t low = std::min<std::size_t>(sound_rom.size(), z80_rom_window);

        // Fixed low 32 KiB + the $D003-banked 16 KiB window from $10000 up.
        sound_bus_.map_rom(z80_rom_base, rom_span.first(low), 0);
        sound_bus_.map_mmio(
            z80_bank_base, z80_bank_window,
            [this, rom_span](std::uint32_t address) -> std::uint8_t {
                const std::uint32_t bank_base = sound_bank_rom_base();
                if (bank_base == 0xFFFFFFFFU) {
                    return 0xFFU;
                }
                const std::uint32_t rom_addr = bank_base +
                                               (sound_bank_ & z80_bank_mask) * z80_bank_window +
                                               (address - z80_bank_base);
                return rom_addr < rom_span.size() ? rom_span[rom_addr] : 0xFFU;
            },
            [](std::uint32_t, std::uint8_t) {}, 0);

        // Comm RAM ($C000), Z80 scratch ($D000), work RAM ($F000).
        sound_bus_.map_mmio(
            z80_shared_base, qsound_shared_size,
            [this](std::uint32_t address) -> std::uint8_t {
                const auto offset = static_cast<std::uint16_t>(address - z80_shared_base);
                const std::uint8_t value = qsound_shared_ram_[offset];
                if (offset == 0x000FU) {
                    ++qsound_bus_.shared_z80_command_signal_read_count;
                    qsound_bus_.shared_command_signal_last_z80_value = value;
                }
                if (qsound_live_trace_shared_index_is_interesting(offset)) {
                    qsound_live_trace_z80_event(
                        "Z80SHR", static_cast<std::uint16_t>(address), offset, value,
                        sound_cpu_.cpu_registers().pc, sound_cpu_.elapsed_cycles(), sound_bank_,
                        std::span<const std::uint8_t>(qsound_work_ram_),
                        std::span<const std::uint8_t>(qsound_shared_ram_));
                }
                return value;
            },
            [this](std::uint32_t address, std::uint8_t value) {
                const auto offset = static_cast<std::uint16_t>(address - z80_shared_base);
                qsound_shared_ram_[offset] = value;
                ++qsound_bus_.shared_z80_write_count;
                qsound_bus_.shared_last_z80_addr = static_cast<std::uint16_t>(address);
                qsound_bus_.shared_last_z80_value = value;
                if (qsound_live_trace_shared_index_is_interesting(offset)) {
                    qsound_live_trace_z80_event(
                        "Z80SHW", static_cast<std::uint16_t>(address), offset, value,
                        sound_cpu_.cpu_registers().pc, sound_cpu_.elapsed_cycles(), sound_bank_,
                        std::span<const std::uint8_t>(qsound_work_ram_),
                        std::span<const std::uint8_t>(qsound_shared_ram_));
                }
            },
            0);
        sound_bus_.map_ram(z80_ram_base, std::span<std::uint8_t>(z80_ram_), 0);
        sound_bus_.map_mmio(
            z80_work_base, z80_work_window,
            [this](std::uint32_t address) -> std::uint8_t {
                return qsound_work_ram_[address - z80_work_base];
            },
            [this](std::uint32_t address, std::uint8_t value) {
                qsound_work_ram_[address - z80_work_base] = value;
                ++qsound_bus_.work_z80_write_count;
                qsound_bus_.work_last_z80_addr = static_cast<std::uint16_t>(address);
                qsound_bus_.work_last_z80_value = value;
                if (qsound_live_trace_work_addr_is_interesting(
                        static_cast<std::uint16_t>(address))) {
                    qsound_live_trace_z80_event(
                        "Z80WORKW", static_cast<std::uint16_t>(address),
                        static_cast<std::uint16_t>(address - z80_work_base), value,
                        sound_cpu_.cpu_registers().pc, sound_cpu_.elapsed_cycles(), sound_bank_,
                        std::span<const std::uint8_t>(qsound_work_ram_),
                        std::span<const std::uint8_t>(qsound_shared_ram_));
                }
            },
            0);

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
                    qsound_live_trace_bank_write(
                        value, sound_bank_, sound_cpu_.cpu_registers().pc,
                        sound_cpu_.elapsed_cycles(), std::span<const std::uint8_t>(qsound_work_ram_),
                        std::span<const std::uint8_t>(qsound_shared_ram_));
                } else if (address >= z80_port_base && address <= z80_port_base + 2U) {
                    qdsp_.write_port_with_pc(
                        static_cast<std::uint8_t>(address - z80_port_base), value,
                        sound_cpu_.cpu_registers().pc);
                    if (address == z80_port_base + 2U) {
                        qsound_live_trace_register(
                            qdsp_.last_register(), qdsp_.last_register_data(),
                            qdsp_.last_register_pc(), sound_cpu_.elapsed_cycles(), sound_bank_,
                            std::span<const std::uint8_t>(qsound_work_ram_),
                            std::span<const std::uint8_t>(qsound_shared_ram_));
                    }
                } else {
                    z80_ram_[address - z80_ram_base] = value;
                }
            },
            1);

        sound_cpu_.attach_bus(sound_bus_);
        qdsp_.set_sample_rom(std::span<const std::uint8_t>(region(roms, "qsound")));
    }

    std::uint32_t cps2_system::sound_bank_rom_base() const noexcept {
        if (sound_rom_size_ <= z80_rom_window) {
            return 0xFFFFFFFFU;
        }
        return sound_rom_size_ >= z80_qsound_cpu_rom_region_size ||
                       sound_rom_size_ > 0x20000U
                   ? z80_bank_rom_base_large
                   : z80_bank_rom_base_small;
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
        // The 68K drives; the sound Z80 (8 MHz) catches up at the board clock
        // ratio when out of reset, and the DSP advances with it. The CPS2 QSound
        // driver cadence matches the reference board path when each 68K slice
        // runs whole Z80 instructions and discards that slice's instruction
        // overshoot instead of banking it into the next slice.
        std::uint64_t ran = 0U;
        while (ran < cycles) {
            const int spent = main_cpu.step_instruction();
            const std::uint64_t step = spent > 0 ? static_cast<std::uint64_t>(spent) : 1U;
            ran += step;
            scanline_cycles_ += static_cast<std::uint32_t>(step);
            frame_cycles_ += static_cast<std::uint32_t>(step);
            if (sound_rom_size_ != 0U && !sound_reset_asserted_) {
                // Z80 cycles owed = 68K cycles scaled by the board clocks. Keep
                // the fractional remainder so the long-run cadence does not drift.
                sound_cycle_accum_ += step * static_cast<std::uint64_t>(qsound_z80_clock_hz);
                const std::uint64_t due = sound_cycle_accum_ / m68k_clock_hz;
                sound_cycle_accum_ -= due * m68k_clock_hz;
                std::uint64_t consumed_sound = 0U;
                while (consumed_sound < due) {
                    const bool ack_possible =
                        qsound_irq_line_ && sound_cpu_.iff1() && !sound_cpu_.ei_pending();
                    sound_cpu_.set_irq_line(qsound_irq_line_);
                    const int zc = sound_cpu_.step_instruction();
                    const int zstep = zc > 0 ? zc : 1;
                    consumed_sound += static_cast<std::uint64_t>(zstep);
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
                sound_cpu_.set_irq_line(qsound_irq_line_);
                qdsp_.tick(step);
            }
            while (scanline_cycles_ >= cpu_cycles_per_scanline) {
                scanline_cycles_ -= cpu_cycles_per_scanline;
                ++scanline_;
                if (scanline_ >= frame_scanlines) {
                    scanline_ = 0U;
                    frame_cycles_ = 0U;
                    main_cpu.set_irq_level(0);
                } else if (scanline_ == vblank_start_line) {
                    push_cps_a_to_video();
                    video_.latch_objects();
                    video_.render(palette_source(), palette_control());
                    main_cpu.set_irq_level(m68k_vblank_irq_level);
                    ++vblank_irq_raised_;
                }
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

    std::uint32_t cps2_system::object_ram_base_from_reg(std::uint16_t reg) const noexcept {
        const std::uint32_t addr = static_cast<std::uint32_t>(reg) << 8U;
        if (addr >= object_ram_base && addr < object_ram_base + object_ram_size) {
            return addr - object_ram_base;
        }
        return static_cast<std::uint32_t>(reg) * 256U;
    }

    std::uint32_t cps2_system::object_ram_base_aligned(std::uint16_t reg,
                                                       std::uint32_t boundary) const noexcept {
        std::uint32_t base = object_ram_base_from_reg(reg);
        if (boundary > 1U) {
            base &= ~(boundary - 1U);
        }
        return base;
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
        // object bank latch selects the active 0x2000-byte object table. Unlike
        // CPS-1, the CPS-A OBJ base register is not the CPS-2 sprite table base.
        // Real CPS-2 games still write values such as $7080 there, but sprites
        // are latched from the selected bank start.
        const std::uint32_t bank_base =
            static_cast<std::uint32_t>(object_bank_ & 1U) * object_bank_bytes;
        video_.set_object_base(bank_base);
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

    void cps2_system::set_analog_dial(std::uint8_t player, std::uint16_t value) noexcept {
        if (player >= analog_dial_.size()) {
            return;
        }
        const std::uint16_t mask =
            analog_input_mode_ == cps2_analog_input_mode::puzz_loop_2 ? 0x00FFU : 0x0FFFU;
        analog_dial_[player] = static_cast<std::uint16_t>(value & mask);
    }

    std::uint16_t cps2_system::analog_dial(std::uint8_t player) const noexcept {
        return player < analog_dial_.size() ? analog_dial_[player] : 0U;
    }

    std::uint64_t cps2_system::coin_counter(std::uint8_t slot) const noexcept {
        return slot < coin_counters_.size() ? coin_counters_[slot] : 0U;
    }

    bool cps2_system::coin_lockout(std::uint8_t slot) const noexcept {
        return slot < coin_lockouts_.size() ? coin_lockouts_[slot] : false;
    }

    void cps2_system::update_coin_outputs(std::uint8_t value) noexcept {
        for (std::uint8_t i = 0U; i < coin_counters_.size(); ++i) {
            if (i == 1U && analog_input_mode_ == cps2_analog_input_mode::puzz_loop_2) {
                continue; // bit 1 selects stick/paddle reads on Puzz Loop 2.
            }
            const bool line = ((value >> i) & 1U) != 0U;
            if (line && !coin_counter_lines_[i]) {
                ++coin_counters_[i];
            }
            coin_counter_lines_[i] = line;
        }
        for (std::uint8_t i = 0U; i < coin_lockouts_.size(); ++i) {
            const bool bit = ((value >> (4U + i)) & 1U) != 0U;
            coin_lockouts_[i] = params.coin_lockout_active_high ? bit : !bit;
        }
    }

    void cps2_system::update_ecofighters_dial_direction() noexcept {
        for (std::size_t i = 0U; i < analog_dial_.size(); ++i) {
            const std::uint16_t dial = static_cast<std::uint16_t>(analog_dial_[i] & 0x0FFFU);
            const std::uint16_t last =
                static_cast<std::uint16_t>(ecofighters_dial_last_[i] & 0x0FFFU);
            if ((dial & 0x0800U) == (last & 0x0800U)) {
                ecofighters_dial_direction_[i] = dial > last ? 1U : 0U;
            } else if ((dial & 0x0800U) > (last & 0x0800U)) {
                ecofighters_dial_direction_[i] = 0U;
            } else {
                ecofighters_dial_direction_[i] = 1U;
            }
            ecofighters_dial_last_[i] = dial;
        }
    }

    std::uint16_t cps2_system::input0_read_word(bool side_effects) {
        switch (analog_input_mode_) {
        case cps2_analog_input_mode::puzz_loop_2:
            if (read_paddle_) {
                return input0;
            }
            return static_cast<std::uint16_t>((analog_dial_[0] & 0x00FFU) |
                                              ((analog_dial_[1] & 0x00FFU) << 8U));
        case cps2_analog_input_mode::eco_fighters: {
            const bool spinners_enabled = (input1 & 0x0010U) == 0U;
            if (!read_paddle_ || !spinners_enabled) {
                std::uint16_t word = input0;
                if (spinners_enabled) {
                    word = static_cast<std::uint16_t>(word & 0xDFDFU);
                    word = static_cast<std::uint16_t>(
                        word |
                        (static_cast<std::uint16_t>(ecofighters_dial_direction_[0] & 1U) << 5U) |
                        (static_cast<std::uint16_t>(ecofighters_dial_direction_[1] & 1U)
                         << 13U));
                }
                return word;
            }
            const std::uint16_t word = static_cast<std::uint16_t>(
                (analog_dial_[0] & 0x00FFU) | ((analog_dial_[1] & 0x00FFU) << 8U));
            if (side_effects) {
                update_ecofighters_dial_direction();
            }
            return word;
        }
        case cps2_analog_input_mode::none:
        default:
            return input0;
        }
    }

    std::uint32_t cps2_system::palette_source() const noexcept {
        const std::uint16_t reg = cps_a_regs_[cps_a_palette_base];
        return video_ram_base_from_reg(reg) & ~(palette_page_bytes - 1U);
    }

    std::uint16_t cps2_system::palette_control() const noexcept {
        return cps_reg_word(cps_b_file_offset, cps_b_palette_control_word);
    }

    void cps2_system::run_frame() {
        if (executable_) {
            const std::uint64_t budget =
                frame_budget_overshoot_ < cpu_cycles_per_frame
                    ? cpu_cycles_per_frame - frame_budget_overshoot_
                    : 1U;
            const std::uint64_t before = main_cpu.elapsed_cycles();
            run_cycles(budget);
            const std::uint64_t spent = main_cpu.elapsed_cycles() - before;
            frame_budget_overshoot_ = spent > budget ? spent - budget : 0U;
        }
    }

    void cps2_system::save_state(chips::state_writer& writer) const {
        writer.u32(cps2_system_state_version);
        writer.boolean(executable_);
        writer.u32(sound_rom_size_);

        main_cpu.save_state(writer);
        sound_cpu_.save_state(writer);
        qdsp_.save_state(writer);
        video_.save_state(writer);
        eeprom_.save_state(writer);

        writer.bytes(std::span<const std::uint8_t>(work_ram_));
        writer.bytes(std::span<const std::uint8_t>(video_ram_));
        writer.bytes(std::span<const std::uint8_t>(object_ram_));
        writer.bytes(std::span<const std::uint8_t>(extra_ram_));
        writer.bytes(std::span<const std::uint8_t>(control_regs_));
        writer.bytes(std::span<const std::uint8_t>(extra_control_));
        writer.bytes(std::span<const std::uint8_t>(cps_regs_));
        for (const std::uint16_t value : cps_a_regs_) {
            writer.u16(value);
        }
        writer.u8(object_bank_);
        writer.u64(vblank_irq_raised_);
        writer.u64(vblank_irq_acked_);
        writer.u32(scanline_);
        writer.u32(scanline_cycles_);
        writer.u32(frame_cycles_);
        writer.u64(frame_budget_overshoot_);
        writer.u16(input0);
        writer.u16(input1);
        writer.u16(input_sys);
        writer.bytes(std::span<const std::uint8_t>(development_dips_));
        writer.u8(static_cast<std::uint8_t>(analog_input_mode_));
        writer.boolean(read_paddle_);
        for (const std::uint16_t value : analog_dial_) {
            writer.u16(value);
        }
        for (const std::uint16_t value : ecofighters_dial_last_) {
            writer.u16(value);
        }
        for (const std::uint8_t value : ecofighters_dial_direction_) {
            writer.u8(value);
        }
        for (const std::uint64_t value : coin_counters_) {
            writer.u64(value);
        }
        for (const bool value : coin_counter_lines_) {
            writer.boolean(value);
        }
        for (const bool value : coin_lockouts_) {
            writer.boolean(value);
        }

        writer.bytes(std::span<const std::uint8_t>(qsound_shared_ram_));
        writer.bytes(std::span<const std::uint8_t>(z80_ram_));
        writer.bytes(std::span<const std::uint8_t>(qsound_work_ram_));
        writer.u32(qsound_bus_.shared_68k_write_count);
        writer.u32(qsound_bus_.shared_68k_non_ff_write_count);
        writer.u32(qsound_bus_.shared_68k_even_write_count);
        writer.u32(qsound_bus_.shared_68k_even_non_ff_write_count);
        writer.u32(qsound_bus_.shared_68k_read_count);
        writer.u32(qsound_bus_.shared_68k_odd_read_count);
        writer.u32(qsound_bus_.shared_68k_even_read_count);
        writer.u32(qsound_bus_.shared_68k_status_read_count);
        writer.u32(qsound_bus_.shared_68k_magic_read_count);
        writer.u32(qsound_bus_.shared_68k_command_signal_write_count);
        writer.u32(qsound_bus_.shared_z80_command_signal_read_count);
        writer.u32(qsound_bus_.shared_z80_write_count);
        writer.u32(qsound_bus_.work_z80_write_count);
        writer.u16(qsound_bus_.shared_last_68k_index);
        writer.u8(qsound_bus_.shared_last_68k_value);
        writer.u32(qsound_bus_.shared_last_68k_write_pc);
        writer.u32(qsound_bus_.shared_last_68k_non_ff_write_pc);
        writer.u16(qsound_bus_.shared_last_68k_read_index);
        writer.u8(qsound_bus_.shared_last_68k_read_value);
        writer.u32(qsound_bus_.shared_last_68k_read_pc);
        writer.u16(qsound_bus_.shared_last_even_68k_index);
        writer.u8(qsound_bus_.shared_last_even_68k_value);
        writer.u16(qsound_bus_.shared_last_z80_addr);
        writer.u8(qsound_bus_.shared_last_z80_value);
        writer.u8(qsound_bus_.shared_status_first_read_value);
        writer.u8(qsound_bus_.shared_status_last_read_value);
        writer.u32(qsound_bus_.shared_status_first_read_pc);
        writer.u32(qsound_bus_.shared_status_last_read_pc);
        writer.boolean(qsound_bus_.shared_status_first_read_seen);
        writer.u8(qsound_bus_.shared_command_signal_last_68k_value);
        writer.u32(qsound_bus_.shared_command_signal_last_68k_pc);
        writer.u8(qsound_bus_.shared_command_signal_last_z80_value);
        writer.bytes(std::span<const std::uint8_t>(qsound_bus_.shared_command_snapshot));
        writer.u16(qsound_bus_.work_last_z80_addr);
        writer.u8(qsound_bus_.work_last_z80_value);
        writer.u8(sound_bank_);
        writer.boolean(sound_reset_asserted_);
        writer.u64(static_cast<std::uint64_t>(sound_cycle_debt_));
        writer.u64(sound_cycle_accum_);
        writer.boolean(qsound_irq_line_);
        writer.u32(qsound_irq_accum_);
    }

    void cps2_system::load_state(chips::state_reader& reader) {
        const std::uint32_t version = reader.u32();
        if (version < 1U || version > cps2_system_state_version) {
            reader.fail();
            return;
        }

        const bool saved_executable = reader.boolean();
        const std::uint32_t saved_sound_rom_size = reader.u32();
        if (saved_executable != executable_ || saved_sound_rom_size != sound_rom_size_) {
            reader.fail();
            return;
        }

        main_cpu.load_state(reader);
        sound_cpu_.load_state(reader);
        qdsp_.load_state(reader);
        video_.load_state(reader);
        eeprom_.load_state(reader);

        reader.bytes(std::span<std::uint8_t>(work_ram_));
        reader.bytes(std::span<std::uint8_t>(video_ram_));
        reader.bytes(std::span<std::uint8_t>(object_ram_));
        reader.bytes(std::span<std::uint8_t>(extra_ram_));
        reader.bytes(std::span<std::uint8_t>(control_regs_));
        reader.bytes(std::span<std::uint8_t>(extra_control_));
        reader.bytes(std::span<std::uint8_t>(cps_regs_));
        for (std::uint16_t& value : cps_a_regs_) {
            value = reader.u16();
        }
        object_bank_ = reader.u8();
        vblank_irq_raised_ = reader.u64();
        vblank_irq_acked_ = reader.u64();
        if (version >= 7U) {
            scanline_ = reader.u32();
            scanline_cycles_ = reader.u32();
            frame_cycles_ = reader.u32();
            if (scanline_ >= frame_scanlines || scanline_cycles_ >= cpu_cycles_per_scanline) {
                reader.fail();
                return;
            }
        } else {
            scanline_ = 0U;
            scanline_cycles_ = 0U;
            frame_cycles_ = 0U;
        }
        if (version >= 8U) {
            frame_budget_overshoot_ = reader.u64();
            if (frame_budget_overshoot_ >= cpu_cycles_per_frame) {
                reader.fail();
                return;
            }
        } else {
            frame_budget_overshoot_ = 0U;
        }
        input0 = reader.u16();
        input1 = reader.u16();
        input_sys = reader.u16();
        if (version >= 3U) {
            reader.bytes(std::span<std::uint8_t>(development_dips_));
        } else {
            development_dips_.fill(0xFFU);
        }
        if (version >= 4U) {
            const std::uint8_t saved_analog_mode = reader.u8();
            if (!valid_analog_input_mode(saved_analog_mode) ||
                static_cast<cps2_analog_input_mode>(saved_analog_mode) != analog_input_mode_) {
                reader.fail();
                return;
            }
            read_paddle_ = reader.boolean();
            for (std::uint16_t& value : analog_dial_) {
                value = reader.u16();
            }
            for (std::uint16_t& value : ecofighters_dial_last_) {
                value = reader.u16();
            }
            for (std::uint8_t& value : ecofighters_dial_direction_) {
                value = reader.u8();
            }
        } else {
            read_paddle_ = false;
            analog_dial_.fill(0U);
            ecofighters_dial_last_.fill(0U);
            ecofighters_dial_direction_.fill(0U);
        }
        if (version >= 5U) {
            for (std::uint64_t& value : coin_counters_) {
                value = reader.u64();
            }
            for (bool& value : coin_counter_lines_) {
                value = reader.boolean();
            }
            for (bool& value : coin_lockouts_) {
                value = reader.boolean();
            }
        } else {
            coin_counters_.fill(0U);
            coin_counter_lines_.fill(false);
            coin_lockouts_.fill(false);
        }

        reader.bytes(std::span<std::uint8_t>(qsound_shared_ram_));
        reader.bytes(std::span<std::uint8_t>(z80_ram_));
        reader.bytes(std::span<std::uint8_t>(qsound_work_ram_));
        if (version >= 6U) {
            qsound_bus_.shared_68k_write_count = reader.u32();
            qsound_bus_.shared_68k_non_ff_write_count = reader.u32();
            qsound_bus_.shared_68k_even_write_count = reader.u32();
            qsound_bus_.shared_68k_even_non_ff_write_count = reader.u32();
            qsound_bus_.shared_68k_read_count = reader.u32();
            qsound_bus_.shared_68k_odd_read_count = reader.u32();
            qsound_bus_.shared_68k_even_read_count = reader.u32();
            qsound_bus_.shared_68k_status_read_count = reader.u32();
            qsound_bus_.shared_68k_magic_read_count = reader.u32();
            qsound_bus_.shared_68k_command_signal_write_count = reader.u32();
            qsound_bus_.shared_z80_command_signal_read_count = reader.u32();
            qsound_bus_.shared_z80_write_count = reader.u32();
            qsound_bus_.work_z80_write_count = reader.u32();
            qsound_bus_.shared_last_68k_index = reader.u16();
            qsound_bus_.shared_last_68k_value = reader.u8();
            qsound_bus_.shared_last_68k_write_pc = reader.u32();
            qsound_bus_.shared_last_68k_non_ff_write_pc = reader.u32();
            qsound_bus_.shared_last_68k_read_index = reader.u16();
            qsound_bus_.shared_last_68k_read_value = reader.u8();
            qsound_bus_.shared_last_68k_read_pc = reader.u32();
            qsound_bus_.shared_last_even_68k_index = reader.u16();
            qsound_bus_.shared_last_even_68k_value = reader.u8();
            qsound_bus_.shared_last_z80_addr = reader.u16();
            qsound_bus_.shared_last_z80_value = reader.u8();
            qsound_bus_.shared_status_first_read_value = reader.u8();
            qsound_bus_.shared_status_last_read_value = reader.u8();
            qsound_bus_.shared_status_first_read_pc = reader.u32();
            qsound_bus_.shared_status_last_read_pc = reader.u32();
            qsound_bus_.shared_status_first_read_seen = reader.boolean();
            qsound_bus_.shared_command_signal_last_68k_value = reader.u8();
            qsound_bus_.shared_command_signal_last_68k_pc = reader.u32();
            qsound_bus_.shared_command_signal_last_z80_value = reader.u8();
            reader.bytes(std::span<std::uint8_t>(qsound_bus_.shared_command_snapshot));
            qsound_bus_.work_last_z80_addr = reader.u16();
            qsound_bus_.work_last_z80_value = reader.u8();
        } else {
            qsound_bus_ = {};
        }
        sound_bank_ = reader.u8();
        sound_reset_asserted_ = reader.boolean();
        (void)reader.u64(); // older states stored a carried Z80 overshoot debt here.
        sound_cycle_debt_ = 0;
        sound_cycle_accum_ = version >= 2U ? reader.u64() : 0U;
        qsound_irq_line_ = reader.boolean();
        qsound_irq_accum_ = reader.u32();
        sound_cpu_.set_irq_line(qsound_irq_line_);
    }

} // namespace mnemos::manifests::capcom_cps2
