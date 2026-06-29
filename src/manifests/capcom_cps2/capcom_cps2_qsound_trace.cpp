#include "capcom_cps2_qsound_trace.hpp"

#include "capcom_cps2_system.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <span>
#include <string>
#include <string_view>

namespace mnemos::manifests::capcom_cps2 {
    namespace qsound_trace {

        constexpr const char* qsound_live_trace_limit_env = "MNEMOS_CPS2_QSOUND_LIVE_TRACE";
        constexpr const char* qsound_live_trace_path_env = "MNEMOS_CPS2_QSOUND_TRACE_PATH";
        constexpr const char* qsound_live_trace_noisy_env = "MNEMOS_CPS2_QSOUND_TRACE_NOISY";
        constexpr const char* qsound_live_trace_all_shared_env =
            "MNEMOS_CPS2_QSOUND_TRACE_ALL_SHARED";
        constexpr const char* qsound_live_trace_min_main_cycle_env =
            "MNEMOS_CPS2_QSOUND_TRACE_MIN_MAIN_CYC";
        constexpr const char* qsound_live_trace_max_main_cycle_env =
            "MNEMOS_CPS2_QSOUND_TRACE_MAX_MAIN_CYC";
        constexpr const char* qsound_live_trace_min_sound_cycle_env =
            "MNEMOS_CPS2_QSOUND_TRACE_MIN_SOUND_CYC";
        constexpr const char* qsound_live_trace_max_sound_cycle_env =
            "MNEMOS_CPS2_QSOUND_TRACE_MAX_SOUND_CYC";
        constexpr const char* qsound_live_trace_main_only_env =
            "MNEMOS_CPS2_QSOUND_TRACE_MAIN_ONLY";
        constexpr const char* qsound_live_trace_queue_only_env =
            "MNEMOS_CPS2_QSOUND_TRACE_QUEUE_ONLY";
        constexpr const char* qsound_live_trace_access_only_env =
            "MNEMOS_CPS2_QSOUND_TRACE_ACCESS_ONLY";
        constexpr const char* qsound_live_trace_writes_only_env =
            "MNEMOS_CPS2_QSOUND_TRACE_WRITES_ONLY";
        constexpr const char* qsound_live_trace_bank_reads_env =
            "MNEMOS_CPS2_QSOUND_TRACE_BANK_READS";
        constexpr const char* qsound_live_trace_all_work_env =
            "MNEMOS_CPS2_QSOUND_TRACE_ALL_WORK";
        constexpr const char* qsound_live_trace_z80_pc_env =
            "MNEMOS_CPS2_QSOUND_TRACE_Z80_PC";
        constexpr const char* qsound_live_trace_shared_read_edges_env =
            "MNEMOS_CPS2_QSOUND_TRACE_SHARED_READ_EDGES";
        constexpr const char* qsound_live_trace_no_work_env =
            "MNEMOS_CPS2_QSOUND_TRACE_NO_WORK";
        constexpr const char* cps2_dump_opcodes_path_env = "MNEMOS_CPS2_DUMP_OPCODES_PATH";
        constexpr std::uint32_t qsound_live_trace_default_limit = 50000U;
        constexpr std::uint32_t qsound_queue_watch_base = main_ram_base + 0x4C20U;
        constexpr std::uint32_t qsound_queue_watch_end = main_ram_base + 0x5020U;
        constexpr std::uint32_t qsound_settings_watch_base = main_ram_base + 0x27D0U;
        constexpr std::uint32_t qsound_settings_watch_end = main_ram_base + 0x27E0U;
        constexpr std::uint32_t qsound_boot_mode_watch_base = main_ram_base + 0x4B80U;
        constexpr std::uint32_t qsound_boot_mode_watch_end = main_ram_base + 0x4C40U;
        constexpr std::uint32_t qsound_boot_state_watch_base = main_ram_base + 0x78A0U;
        constexpr std::uint32_t qsound_boot_state_watch_end = main_ram_base + 0x78C0U;
        constexpr std::uint32_t qsound_config_watch_base = main_ram_base + 0xF910U;
        constexpr std::uint32_t qsound_config_watch_end = main_ram_base + 0xF980U;
        constexpr std::uint32_t qsound_audio_gate_watch_base = main_ram_base + 0xFB80U;
        constexpr std::uint32_t qsound_audio_gate_watch_end = main_ram_base + 0xFBA0U;

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

        [[nodiscard]] std::FILE* fopen_write_compat(const char* path) noexcept {
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
            std::FILE* file = std::fopen(path, "wb");
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
            return file;
        }

        void dump_opcode_image_if_requested(std::span<const std::uint8_t> opcode_image) noexcept {
            const char* path = getenv_compat(cps2_dump_opcodes_path_env);
            if (path == nullptr || path[0] == '\0') {
                return;
            }
            std::FILE* file = fopen_write_compat(path);
            if (file == nullptr) {
                return;
            }
            if (!opcode_image.empty()) {
                (void)std::fwrite(opcode_image.data(), 1U, opcode_image.size(), file);
            }
            std::fclose(file);
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

        [[nodiscard]] std::uint64_t qsound_live_trace_u64_env(const char* name,
                                                              std::uint64_t fallback) noexcept {
            const char* env = getenv_compat(name);
            if (env == nullptr || env[0] == '\0') {
                return fallback;
            }
            char* end = nullptr;
            const unsigned long long parsed = std::strtoull(env, &end, 0);
            return end != env ? static_cast<std::uint64_t>(parsed) : fallback;
        }

        [[nodiscard]] std::uint64_t qsound_live_trace_min_main_cycle() noexcept {
            static bool initialized = false;
            static std::uint64_t min_cycle = 0U;
            if (!initialized) {
                initialized = true;
                min_cycle = qsound_live_trace_u64_env(qsound_live_trace_min_main_cycle_env, 0U);
            }
            return min_cycle;
        }

        [[nodiscard]] std::uint64_t qsound_live_trace_max_main_cycle() noexcept {
            static bool initialized = false;
            static std::uint64_t max_cycle = UINT64_MAX;
            if (!initialized) {
                initialized = true;
                max_cycle =
                    qsound_live_trace_u64_env(qsound_live_trace_max_main_cycle_env, UINT64_MAX);
            }
            return max_cycle;
        }

        [[nodiscard]] bool qsound_live_trace_main_cycle_in_window(
            std::uint64_t main_cycles) noexcept {
            return main_cycles >= qsound_live_trace_min_main_cycle() &&
                   main_cycles <= qsound_live_trace_max_main_cycle();
        }

        [[nodiscard]] std::uint64_t qsound_live_trace_min_sound_cycle() noexcept {
            static bool initialized = false;
            static std::uint64_t min_cycle = 0U;
            if (!initialized) {
                initialized = true;
                min_cycle = qsound_live_trace_u64_env(qsound_live_trace_min_sound_cycle_env, 0U);
            }
            return min_cycle;
        }

        [[nodiscard]] std::uint64_t qsound_live_trace_max_sound_cycle() noexcept {
            static bool initialized = false;
            static std::uint64_t max_cycle = UINT64_MAX;
            if (!initialized) {
                initialized = true;
                max_cycle =
                    qsound_live_trace_u64_env(qsound_live_trace_max_sound_cycle_env, UINT64_MAX);
            }
            return max_cycle;
        }

        [[nodiscard]] bool qsound_live_trace_sound_cycle_in_window(
            std::uint64_t sound_cycles) noexcept {
            return sound_cycles >= qsound_live_trace_min_sound_cycle() &&
                   sound_cycles <= qsound_live_trace_max_sound_cycle();
        }

        [[nodiscard]] bool qsound_live_trace_noisy() noexcept {
            static bool initialized = false;
            static bool noisy = false;

            if (!initialized) {
                initialized = true;
                const char* env = getenv_compat(qsound_live_trace_noisy_env);
                noisy = env != nullptr && env[0] != '\0' && env[0] != '0';
            }

            return noisy;
        }

        [[nodiscard]] bool qsound_live_trace_main_only() noexcept {
            static bool initialized = false;
            static bool main_only = false;

            if (!initialized) {
                initialized = true;
                const char* env = getenv_compat(qsound_live_trace_main_only_env);
                main_only = env != nullptr && env[0] != '\0' && env[0] != '0';
            }

            return main_only;
        }

        [[nodiscard]] bool qsound_live_trace_queue_only() noexcept {
            static bool initialized = false;
            static bool queue_only = false;

            if (!initialized) {
                initialized = true;
                const char* env = getenv_compat(qsound_live_trace_queue_only_env);
                queue_only = env != nullptr && env[0] != '\0' && env[0] != '0';
            }

            return queue_only;
        }

        [[nodiscard]] bool qsound_live_trace_access_only() noexcept {
            static bool initialized = false;
            static bool access_only = false;

            if (!initialized) {
                initialized = true;
                const char* env = getenv_compat(qsound_live_trace_access_only_env);
                access_only = env != nullptr && env[0] != '\0' && env[0] != '0';
            }

            return access_only;
        }

        [[nodiscard]] bool qsound_live_trace_writes_only() noexcept {
            static bool initialized = false;
            static bool writes_only = false;

            if (!initialized) {
                initialized = true;
                const char* env = getenv_compat(qsound_live_trace_writes_only_env);
                writes_only = env != nullptr && env[0] != '\0' && env[0] != '0';
            }

            return writes_only;
        }

        [[nodiscard]] bool qsound_live_trace_bank_reads() noexcept {
            static bool initialized = false;
            static bool trace = false;

            if (!initialized) {
                initialized = true;
                const char* env = getenv_compat(qsound_live_trace_bank_reads_env);
                trace = env != nullptr && env[0] != '\0' && env[0] != '0';
            }

            return trace;
        }

        [[nodiscard]] bool qsound_live_trace_all_work() noexcept {
            static bool initialized = false;
            static bool trace = false;

            if (!initialized) {
                initialized = true;
                const char* env = getenv_compat(qsound_live_trace_all_work_env);
                trace = env != nullptr && env[0] != '\0' && env[0] != '0';
            }

            return trace;
        }

        [[nodiscard]] bool qsound_live_trace_z80_pc() noexcept {
            static bool initialized = false;
            static bool trace = false;

            if (!initialized) {
                initialized = true;
                const char* env = getenv_compat(qsound_live_trace_z80_pc_env);
                trace = env != nullptr && env[0] != '\0' && env[0] != '0';
            }

            return trace;
        }

        [[nodiscard]] bool qsound_live_trace_all_shared() noexcept {
            static bool initialized = false;
            static bool all_shared = false;

            if (!initialized) {
                initialized = true;
                const char* env = getenv_compat(qsound_live_trace_all_shared_env);
                all_shared = env != nullptr && env[0] != '\0' && env[0] != '0';
            }

            return all_shared;
        }

        [[nodiscard]] bool qsound_live_trace_shared_read_edges() noexcept {
            static bool initialized = false;
            static bool trace = false;

            if (!initialized) {
                initialized = true;
                const char* env = getenv_compat(qsound_live_trace_shared_read_edges_env);
                trace = env != nullptr && env[0] != '\0' && env[0] != '0';
            }

            return trace;
        }

        [[nodiscard]] bool qsound_live_trace_no_work() noexcept {
            static bool initialized = false;
            static bool no_work = false;

            if (!initialized) {
                initialized = true;
                const char* env = getenv_compat(qsound_live_trace_no_work_env);
                no_work = env != nullptr && env[0] != '\0' && env[0] != '0';
            }

            return no_work;
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
            if (qsound_live_trace_all_shared()) {
                return true;
            }
            return index < 0x0010U;
        }

        [[nodiscard]] bool qsound_live_trace_work_addr_is_interesting(
            std::uint16_t address) noexcept {
            if (address < z80_work_base) {
                return false;
            }
            const std::uint16_t offset = static_cast<std::uint16_t>(address - z80_work_base);
            if (qsound_live_trace_all_work()) {
                return true;
            }
            if (offset < 0x0020U) {
                return true;
            }
            if (offset >= 0x0100U && offset < 0x0200U) {
                return true;
            }
            return offset >= 0x0500U && offset < 0x0600U;
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
            qsound_live_trace_bytes(file, "f100", work_ram, 0x0100U, 0x0010U);
            qsound_live_trace_bytes(file, "f110", work_ram, 0x0110U, 0x0010U);
            qsound_live_trace_bytes(file, "f140", work_ram, 0x0140U, 0x0010U);
            qsound_live_trace_bytes(file, "f150", work_ram, 0x0150U, 0x0010U);
            qsound_live_trace_bytes(file, "f500", work_ram, 0x0500U, 0x10U);
            qsound_live_trace_bytes(file, "f540", work_ram, 0x0540U, 0x10U);
            qsound_live_trace_bytes(file, "f580", work_ram, 0x0580U, 0x10U);
            qsound_live_trace_bytes(file, "f5c0", work_ram, 0x05C0U, 0x10U);
            qsound_live_trace_bytes(file, "c000", shared_ram, 0x0000U, 0x0010U);
        }

        [[nodiscard]] std::uint16_t qsound_live_trace_main_ram_word(
            std::span<const std::uint8_t> main_ram,
            std::uint32_t address) noexcept {
            const std::uint32_t masked = address & m68k_address_mask;
            if (masked < main_ram_base) {
                return 0xFFFFU;
            }
            const std::uint32_t offset = masked - main_ram_base;
            if (offset + 1U >= main_ram.size()) {
                return 0xFFFFU;
            }
            return static_cast<std::uint16_t>(
                (static_cast<std::uint16_t>(main_ram[offset]) << 8U) | main_ram[offset + 1U]);
        }

        [[nodiscard]] std::uint8_t qsound_live_trace_main_ram_byte(
            std::span<const std::uint8_t> main_ram,
            std::uint32_t address) noexcept {
            const std::uint32_t masked = address & m68k_address_mask;
            if (masked < main_ram_base) {
                return 0xFFU;
            }
            const std::uint32_t offset = masked - main_ram_base;
            return offset < main_ram.size() ? main_ram[offset] : 0xFFU;
        }

        void qsound_live_trace_68k_context(
            std::FILE* file,
            const chips::cpu::m68000::registers& regs,
            std::span<const std::uint8_t> main_ram) noexcept {
            std::fprintf(file, " d=");
            for (std::size_t i = 0U; i < regs.d.size(); ++i) {
                std::fprintf(file,
                             "%s%08X",
                             i == 0U ? "" : ",",
                             static_cast<unsigned>(regs.d[i]));
            }
            std::fprintf(file, " a=");
            for (std::size_t i = 0U; i < regs.a.size(); ++i) {
                std::fprintf(file,
                             "%s%08X",
                             i == 0U ? "" : ",",
                             static_cast<unsigned>(regs.a[i]));
            }
            std::fprintf(file,
                         " sr=%04X nextpc=%06X spw=",
                         static_cast<unsigned>(regs.sr),
                         static_cast<unsigned>(regs.pc & m68k_address_mask));
            for (std::size_t i = 0U; i < 8U; ++i) {
                const std::uint16_t word = qsound_live_trace_main_ram_word(
                    main_ram,
                    regs.a[7] + static_cast<std::uint32_t>(i * 2U));
                std::fprintf(file, "%s%04X", i == 0U ? "" : ",", static_cast<unsigned>(word));
            }
        }

        [[nodiscard]] bool qsound_live_trace_main_ram_access_is_interesting(
            std::uint32_t address,
            bool write) noexcept {
            const std::uint32_t masked = address & m68k_address_mask;
            if (qsound_live_trace_writes_only() && !write) {
                return false;
            }
            if (qsound_live_trace_queue_only()) {
                return write && masked >= qsound_queue_watch_base &&
                       masked < qsound_queue_watch_end;
            }
            if (masked == 0x804010U || masked == 0x804011U || masked == 0x804020U ||
                masked == 0x804021U || masked == 0x804040U || masked == 0x804041U) {
                return true;
            }
            if (address >= qsound_audio_gate_watch_base && address < qsound_audio_gate_watch_end) {
                return true;
            }
            if (address >= qsound_config_watch_base && address < qsound_config_watch_end) {
                return true;
            }
            if (address >= qsound_settings_watch_base && address < qsound_settings_watch_end) {
                return true;
            }
            if (address >= qsound_boot_mode_watch_base && address < qsound_boot_mode_watch_end) {
                return true;
            }
            if (address >= qsound_boot_state_watch_base && address < qsound_boot_state_watch_end) {
                return true;
            }
            if (!write && (address == main_ram_base + 0x4C22U ||
                           address == main_ram_base + 0x4C23U)) {
                return true;
            }
            return write && address >= qsound_queue_watch_base &&
                   address < qsound_queue_watch_end;
        }

        void qsound_live_trace_main_ram_access(std::uint32_t address,
                                               std::uint8_t value,
                                               bool write,
                                               std::uint32_t pc,
                                               std::uint64_t main_cycles,
                                               const chips::cpu::m68000::registers& regs,
                                               std::span<const std::uint8_t> main_ram) noexcept {
            if (!qsound_live_trace_main_cycle_in_window(main_cycles)) {
                return;
            }
            std::FILE* file = nullptr;
            if (!qsound_live_trace_begin(&file)) {
                return;
            }
            std::fprintf(file,
                         "%s addr=%06X value=%02X pc=%06X maincyc=%llu",
                         write ? "MRAMW" : "MRAMR",
                         static_cast<unsigned>(address & m68k_address_mask),
                         static_cast<unsigned>(value),
                         static_cast<unsigned>(pc),
                         static_cast<unsigned long long>(main_cycles));
            if ((address >= qsound_queue_watch_base && address < qsound_queue_watch_end) ||
                (address >= qsound_audio_gate_watch_base &&
                 address < qsound_audio_gate_watch_end)) {
                qsound_live_trace_68k_context(file, regs, main_ram);
            }
            const std::uint32_t base = (address & m68k_address_mask) & ~0x0FU;
            std::fprintf(file, " row%06X=", static_cast<unsigned>(base));
            for (std::uint32_t i = 0U; i < 0x10U; ++i) {
                const std::uint8_t byte = qsound_live_trace_main_ram_byte(main_ram, base + i);
                std::fprintf(file, "%02X", static_cast<unsigned>(byte));
            }
            std::fprintf(file,
                         " ff27d3=%02X ff4c22=%04X ff4c26=%04X fffb9b=%02X",
                         static_cast<unsigned>(qsound_live_trace_main_ram_byte(
                             main_ram, main_ram_base + 0x27D3U)),
                         static_cast<unsigned>(qsound_live_trace_main_ram_word(
                             main_ram, main_ram_base + 0x4C22U)),
                         static_cast<unsigned>(qsound_live_trace_main_ram_word(
                             main_ram, main_ram_base + 0x4C26U)),
                         static_cast<unsigned>(qsound_live_trace_main_ram_byte(
                             main_ram, main_ram_base + 0xFB9BU)));
            std::fprintf(file, "\n");
        }

        [[nodiscard]] bool qsound_live_trace_pc_is_interesting(std::uint32_t pc) noexcept {
            if (qsound_live_trace_queue_only()) {
                return false;
            }
            pc &= m68k_address_mask;
            return (pc >= 0x000396U && pc <= 0x00047CU) ||
                   (pc >= 0x000900U && pc <= 0x0009E0U) ||
                   (pc >= 0x0005F0U && pc <= 0x000620U) ||
                   (pc >= 0x000B70U && pc <= 0x000BC0U) ||
                   (pc >= 0x000CE0U && pc <= 0x000D00U) ||
                   (pc >= 0x086558U && pc <= 0x086566U) ||
                   (pc >= 0x0866BAU && pc <= 0x0866F6U) ||
                   (pc >= 0x08CC50U && pc <= 0x08CC80U) ||
                   (pc >= 0x08D402U && pc <= 0x08D49CU) ||
                   (pc >= 0x08D520U && pc <= 0x08D562U) ||
                   (pc >= 0x08D58EU && pc <= 0x08D5B2U) ||
                   (pc >= 0x08E9C6U && pc <= 0x08EB20U) ||
                   (pc >= 0x08EFF6U && pc <= 0x08F288U) ||
                   (pc >= 0x097B20U && pc <= 0x097B50U) ||
                   (pc >= 0x098040U && pc <= 0x098090U) ||
                   (pc >= 0x09A430U && pc <= 0x09A470U) ||
                   (pc >= 0x08DCFAU && pc <= 0x08DD30U);
        }

        void qsound_live_trace_68k_pc(std::uint32_t pc,
                                      std::uint64_t main_cycles,
                                      const chips::cpu::m68000::registers& regs,
                                      std::span<const std::uint8_t> main_ram,
                                      std::span<const std::uint8_t> opcode_image) noexcept {
            if (!qsound_live_trace_main_cycle_in_window(main_cycles)) {
                return;
            }
            std::FILE* file = nullptr;
            if (!qsound_live_trace_begin(&file)) {
                return;
            }

            std::fprintf(file,
                         "68KPC pc=%06X maincyc=%llu",
                         static_cast<unsigned>(pc & m68k_address_mask),
                         static_cast<unsigned long long>(main_cycles));
            std::fprintf(file, " op=");
            const std::uint32_t opcode_base = pc & m68k_address_mask;
            for (std::uint32_t i = 0U; i < 0x10U; ++i) {
                const std::uint32_t index = opcode_base + i;
                const unsigned value = index < opcode_image.size() ? opcode_image[index] : 0xFFU;
                std::fprintf(file, "%02X", value);
            }
            qsound_live_trace_68k_context(file, regs, main_ram);
            std::fprintf(file,
                         " ff27d3=%02X ff489d=%02X ff4c22=%04X ff4c26=%04X ff4c28=%04X "
                         "fff912=%02X fff91a=%02X fffb9b=%02X",
                         static_cast<unsigned>(qsound_live_trace_main_ram_byte(
                             main_ram, main_ram_base + 0x27D3U)),
                         static_cast<unsigned>(qsound_live_trace_main_ram_byte(
                             main_ram, main_ram_base + 0x489DU)),
                         static_cast<unsigned>(qsound_live_trace_main_ram_word(
                             main_ram, main_ram_base + 0x4C22U)),
                         static_cast<unsigned>(qsound_live_trace_main_ram_word(
                             main_ram, main_ram_base + 0x4C26U)),
                         static_cast<unsigned>(qsound_live_trace_main_ram_word(
                             main_ram, main_ram_base + 0x4C28U)),
                         static_cast<unsigned>(qsound_live_trace_main_ram_byte(
                             main_ram, main_ram_base + 0xF912U)),
                         static_cast<unsigned>(qsound_live_trace_main_ram_byte(
                             main_ram, main_ram_base + 0xF91AU)),
                         static_cast<unsigned>(qsound_live_trace_main_ram_byte(
                             main_ram, main_ram_base + 0xFB9BU)));
            std::fprintf(file, "\n");
        }

        [[nodiscard]] std::uint8_t qsound_opcode_byte(
            std::span<const std::uint8_t> opcode_image,
            std::uint32_t address) noexcept {
            return address < opcode_image.size() ? opcode_image[address] : 0xFFU;
        }

        [[nodiscard]] std::uint32_t qsound_opcode_long(
            std::span<const std::uint8_t> opcode_image,
            std::uint32_t address) noexcept {
            return (static_cast<std::uint32_t>(qsound_opcode_byte(opcode_image, address)) << 24U) |
                   (static_cast<std::uint32_t>(qsound_opcode_byte(opcode_image, address + 1U))
                    << 16U) |
                   (static_cast<std::uint32_t>(qsound_opcode_byte(opcode_image, address + 2U))
                    << 8U) |
                   static_cast<std::uint32_t>(qsound_opcode_byte(opcode_image, address + 3U));
        }

        [[nodiscard]] bool qsound_opcode_target_is_logo_cue(std::uint32_t target) noexcept {
            target &= m68k_address_mask;
            return target >= 0x08DCFAU && target <= 0x08DD60U;
        }

        void qsound_live_trace_opcode_scan(
            std::span<const std::uint8_t> opcode_image) noexcept {
            if (!qsound_live_trace_noisy()) {
                return;
            }

            for (std::uint32_t pc = 0U; pc + 8U < opcode_image.size(); pc += 2U) {
                const std::uint16_t opcode =
                    static_cast<std::uint16_t>((static_cast<std::uint16_t>(
                                                    qsound_opcode_byte(opcode_image, pc))
                                                << 8U) |
                                               qsound_opcode_byte(opcode_image, pc + 1U));

                if (opcode == 0x223CU && qsound_opcode_byte(opcode_image, pc + 2U) == 0x00U &&
                    qsound_opcode_byte(opcode_image, pc + 3U) == 0x00U &&
                    qsound_opcode_byte(opcode_image, pc + 4U) == 0xFFU) {
                    std::FILE* file = nullptr;
                    if (qsound_live_trace_begin(&file)) {
                        std::fprintf(file,
                                     "OPSCAN cue_routine pc=%06X imm=FF%02X op=",
                                     static_cast<unsigned>(pc),
                                     static_cast<unsigned>(
                                         qsound_opcode_byte(opcode_image, pc + 5U)));
                        for (std::uint32_t i = 0U; i < 0x10U; ++i) {
                            std::fprintf(file,
                                         "%02X",
                                         static_cast<unsigned>(
                                             qsound_opcode_byte(opcode_image, pc + i)));
                        }
                        std::fprintf(file, "\n");
                    }
                }

                if (opcode == 0x4EB9U) {
                    const std::uint32_t target = qsound_opcode_long(opcode_image, pc + 2U);
                    if (qsound_opcode_target_is_logo_cue(target)) {
                        std::FILE* file = nullptr;
                        if (qsound_live_trace_begin(&file)) {
                            std::fprintf(file,
                                         "OPSCAN jsr_abs pc=%06X target=%06X op=",
                                         static_cast<unsigned>(pc),
                                         static_cast<unsigned>(target & m68k_address_mask));
                            for (std::uint32_t i = 0U; i < 0x10U; ++i) {
                                std::fprintf(file,
                                             "%02X",
                                             static_cast<unsigned>(
                                                 qsound_opcode_byte(opcode_image, pc + i)));
                            }
                            std::fprintf(file, "\n");
                        }
                    }
                }

                const std::array<std::uint16_t, 3> gate_displacements{
                    0xA7D3U, 0xCC22U, 0x7B9BU};
                for (const std::uint16_t displacement : gate_displacements) {
                    for (std::uint32_t offset = 0U; offset < 8U; ++offset) {
                        if (qsound_opcode_byte(opcode_image, pc + offset) !=
                                static_cast<std::uint8_t>(displacement >> 8U) ||
                            qsound_opcode_byte(opcode_image, pc + offset + 1U) !=
                                static_cast<std::uint8_t>(displacement)) {
                            continue;
                        }
                        std::FILE* file = nullptr;
                        if (qsound_live_trace_begin(&file)) {
                            std::fprintf(file,
                                         "OPSCAN gate_ref pc=%06X disp=%04X op=",
                                         static_cast<unsigned>(pc),
                                         static_cast<unsigned>(displacement));
                            for (std::uint32_t i = 0U; i < 0x10U; ++i) {
                                std::fprintf(file,
                                             "%02X",
                                             static_cast<unsigned>(
                                                 qsound_opcode_byte(opcode_image, pc + i)));
                            }
                            std::fprintf(file, "\n");
                        }
                    }
                }
            }
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
            if (!qsound_live_trace_noisy() || qsound_live_trace_main_only()) {
                return;
            }
            if (!qsound_live_trace_sound_cycle_in_window(sound_cycles)) {
                return;
            }
            if (qsound_live_trace_shared_read_edges() && std::strcmp(kind, "Z80SHR") == 0) {
                static std::array<std::uint8_t, qsound_shared_size> last_values{};
                static std::array<bool, qsound_shared_size> seen{};
                if (index < qsound_shared_size) {
                    const bool changed = !seen[index] || last_values[index] != value;
                    seen[index] = true;
                    last_values[index] = value;
                    if (!changed) {
                        return;
                    }
                }
            }

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

        void qsound_live_trace_z80_work_event(
            const char* kind,
            std::uint16_t address,
            std::uint16_t index,
            std::uint8_t value,
            const chips::cpu::z80::registers& regs,
            std::uint64_t sound_cycles,
            std::uint8_t bank,
            std::span<const std::uint8_t> work_ram,
            std::span<const std::uint8_t> shared_ram) noexcept {
            if (!qsound_live_trace_noisy() || qsound_live_trace_main_only()) {
                return;
            }
            if (qsound_live_trace_no_work()) {
                return;
            }
            if (qsound_live_trace_writes_only() && kind[7] == 'R') {
                return;
            }
            if (!qsound_live_trace_sound_cycle_in_window(sound_cycles)) {
                return;
            }
            if (!qsound_live_trace_work_addr_is_interesting(address)) {
                return;
            }

            std::FILE* file = nullptr;
            if (!qsound_live_trace_begin(&file)) {
                return;
            }
            std::fprintf(file,
                         "%s addr=%04X index=%04X value=%02X pc=%04X sndcyc=%llu bank=%u "
                         "af=%04X bc=%04X de=%04X hl=%04X ix=%04X iy=%04X sp=%04X",
                         kind,
                         static_cast<unsigned>(address),
                         static_cast<unsigned>(index),
                         static_cast<unsigned>(value),
                         static_cast<unsigned>(regs.pc),
                         static_cast<unsigned long long>(sound_cycles),
                         static_cast<unsigned>(bank),
                         static_cast<unsigned>(regs.af),
                         static_cast<unsigned>(regs.bc),
                         static_cast<unsigned>(regs.de),
                         static_cast<unsigned>(regs.hl),
                         static_cast<unsigned>(regs.ix),
                         static_cast<unsigned>(regs.iy),
                         static_cast<unsigned>(regs.sp));
            qsound_live_trace_snapshot(file, work_ram, shared_ram);
            std::fprintf(file, "\n");
        }

        void qsound_live_trace_68k_shared_write(std::uint16_t index,
                                                std::uint8_t value,
                                                std::uint32_t pc,
                                                std::uint64_t main_cycles,
                                                const chips::cpu::m68000::registers& regs,
                                                std::span<const std::uint8_t> main_ram,
                                                std::span<const std::uint8_t> work_ram,
                                                std::span<const std::uint8_t> shared_ram) noexcept {
            if (!qsound_live_trace_main_cycle_in_window(main_cycles)) {
                return;
            }
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
            if (index == 0x0000U || index == 0x000FU) {
                qsound_live_trace_68k_context(file, regs, main_ram);
            }
            qsound_live_trace_snapshot(file, work_ram, shared_ram);
            std::fprintf(file, "\n");
        }

        void qsound_live_trace_68k_shared_read(std::uint16_t index,
                                               std::uint8_t value,
                                               std::uint32_t pc,
                                               std::uint64_t main_cycles,
                                               std::span<const std::uint8_t> work_ram,
                                               std::span<const std::uint8_t> shared_ram) noexcept {
            if (!qsound_live_trace_main_cycle_in_window(main_cycles)) {
                return;
            }
            if (index != 0x0FFFU && index != 0x0FFDU && index != 0x000FU) {
                return;
            }

            std::FILE* file = nullptr;
            if (!qsound_live_trace_begin(&file)) {
                return;
            }
            std::fprintf(file,
                         "68KSHR index=%04X value=%02X pc=%06X maincyc=%llu",
                         static_cast<unsigned>(index),
                         static_cast<unsigned>(value),
                         static_cast<unsigned>(pc),
                         static_cast<unsigned long long>(main_cycles));
            qsound_live_trace_snapshot(file, work_ram, shared_ram);
            std::fprintf(file, "\n");
        }

        void qsound_live_trace_z80_shared_write(std::uint16_t address,
                                                std::uint16_t index,
                                                std::uint8_t value,
                                                std::uint16_t pc,
                                                std::uint64_t sound_cycles,
                                                std::uint8_t bank,
                                                std::span<const std::uint8_t> work_ram,
                                                std::span<const std::uint8_t> shared_ram) noexcept {
            if (qsound_live_trace_main_only()) {
                return;
            }
            if (!qsound_live_trace_sound_cycle_in_window(sound_cycles)) {
                return;
            }
            if (!qsound_live_trace_shared_index_is_interesting(index)) {
                return;
            }

            std::FILE* file = nullptr;
            if (!qsound_live_trace_begin(&file)) {
                return;
            }
            std::fprintf(file,
                         "Z80SHW addr=%04X index=%04X value=%02X pc=%04X sndcyc=%llu bank=%u",
                         static_cast<unsigned>(address),
                         static_cast<unsigned>(index),
                         static_cast<unsigned>(value),
                         static_cast<unsigned>(pc),
                         static_cast<unsigned long long>(sound_cycles),
                         static_cast<unsigned>(bank));
            qsound_live_trace_snapshot(file, work_ram, shared_ram);
            std::fprintf(file, "\n");
        }

        void qsound_live_trace_bank_write(std::uint8_t raw,
                                          std::uint8_t bank,
                                          std::uint16_t pc,
                                          std::uint64_t sound_cycles,
                                          std::span<const std::uint8_t> work_ram,
                                          std::span<const std::uint8_t> shared_ram) noexcept {
            if (!qsound_live_trace_noisy() || qsound_live_trace_main_only()) {
                return;
            }
            if (!qsound_live_trace_sound_cycle_in_window(sound_cycles)) {
                return;
            }

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
            if (qsound_live_trace_main_only()) {
                return;
            }
            if (!qsound_live_trace_sound_cycle_in_window(sound_cycles)) {
                return;
            }
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

        void qsound_live_trace_z80_bank_read(std::uint16_t address,
                                             std::uint32_t rom_address,
                                             std::uint8_t value,
                                             std::uint16_t pc,
                                             std::uint64_t sound_cycles,
                                             std::uint8_t bank,
                                             std::span<const std::uint8_t> work_ram,
                                             std::span<const std::uint8_t> shared_ram) noexcept {
            if (!qsound_live_trace_bank_reads() || qsound_live_trace_main_only()) {
                return;
            }
            if (!qsound_live_trace_sound_cycle_in_window(sound_cycles)) {
                return;
            }

            std::FILE* file = nullptr;
            if (!qsound_live_trace_begin(&file)) {
                return;
            }
            std::fprintf(file,
                         "Z80BANKR addr=%04X rom=%05X value=%02X pc=%04X sndcyc=%llu bank=%u",
                         static_cast<unsigned>(address),
                         static_cast<unsigned>(rom_address),
                         static_cast<unsigned>(value),
                         static_cast<unsigned>(pc),
                         static_cast<unsigned long long>(sound_cycles),
                         static_cast<unsigned>(bank));
            qsound_live_trace_snapshot(file, work_ram, shared_ram);
            std::fprintf(file, "\n");
        }

        void qsound_live_trace_z80_pc_event(
            std::uint32_t pc,
            std::uint64_t sound_cycles,
            const chips::cpu::z80::registers& regs,
            std::uint8_t bank,
            std::span<const std::uint8_t> work_ram,
            std::span<const std::uint8_t> shared_ram) noexcept {
            if (!qsound_live_trace_z80_pc() ||
                !qsound_live_trace_sound_cycle_in_window(sound_cycles)) {
                return;
            }

            std::FILE* file = nullptr;
            if (!qsound_live_trace_begin(&file)) {
                return;
            }
            std::fprintf(file,
                         "Z80PC pc=%04X soundcyc=%llu bank=%02X af=%04X bc=%04X "
                         "de=%04X hl=%04X ix=%04X iy=%04X sp=%04X iff1=%u",
                         static_cast<unsigned>(pc & 0xFFFFU),
                         static_cast<unsigned long long>(sound_cycles),
                         static_cast<unsigned>(bank),
                         static_cast<unsigned>(regs.af),
                         static_cast<unsigned>(regs.bc),
                         static_cast<unsigned>(regs.de),
                         static_cast<unsigned>(regs.hl),
                         static_cast<unsigned>(regs.ix),
                         static_cast<unsigned>(regs.iy),
                         static_cast<unsigned>(regs.sp),
                         regs.iff1 ? 1U : 0U);
            qsound_live_trace_snapshot(file, work_ram, shared_ram);
            std::fprintf(file, "\n");
        }

    } // namespace qsound_trace
} // namespace mnemos::manifests::capcom_cps2
