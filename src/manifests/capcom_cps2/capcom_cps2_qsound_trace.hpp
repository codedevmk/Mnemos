#ifndef MNEMOS_MANIFESTS_CAPCOM_CPS2_CAPCOM_CPS2_QSOUND_TRACE_HPP
#define MNEMOS_MANIFESTS_CAPCOM_CPS2_CAPCOM_CPS2_QSOUND_TRACE_HPP

// Env-gated QSound debug/trace scaffolding for the CPS-2 board. None of this runs
// unless the MNEMOS_CPS2_QSOUND_* / MNEMOS_CPS2_DUMP_OPCODES_PATH env vars are set;
// it is relocated here verbatim out of capcom_cps2_system.cpp to keep the board
// assembler focused on emulation. Only the functions the system.cpp members call
// are declared below -- the remaining moved functions are internal helpers that
// stay private to capcom_cps2_qsound_trace.cpp.

#include "m68000.hpp"
#include "z80.hpp"

#include <cstdint>
#include <span>

namespace mnemos::manifests::capcom_cps2 {
    namespace qsound_trace {

        void dump_opcode_image_if_requested(std::span<const std::uint8_t> opcode_image) noexcept;

        void qsound_live_trace_z80_work_event(
            const char* kind,
            std::uint16_t address,
            std::uint16_t index,
            std::uint8_t value,
            const chips::cpu::z80::registers& regs,
            std::uint64_t sound_cycles,
            std::uint8_t bank,
            std::span<const std::uint8_t> work_ram,
            std::span<const std::uint8_t> shared_ram) noexcept;

        void qsound_live_trace_z80_event(const char* kind,
                                         std::uint16_t address,
                                         std::uint16_t index,
                                         std::uint8_t value,
                                         std::uint16_t pc,
                                         std::uint64_t sound_cycles,
                                         std::uint8_t bank,
                                         std::span<const std::uint8_t> work_ram,
                                         std::span<const std::uint8_t> shared_ram) noexcept;

        [[nodiscard]] bool qsound_live_trace_shared_index_is_interesting(
            std::uint16_t index) noexcept;

        void qsound_live_trace_z80_shared_write(std::uint16_t address,
                                                std::uint16_t index,
                                                std::uint8_t value,
                                                std::uint16_t pc,
                                                std::uint64_t sound_cycles,
                                                std::uint8_t bank,
                                                std::span<const std::uint8_t> work_ram,
                                                std::span<const std::uint8_t> shared_ram) noexcept;

        void qsound_live_trace_z80_pc_event(
            std::uint32_t pc,
            std::uint64_t sound_cycles,
            const chips::cpu::z80::registers& regs,
            std::uint8_t bank,
            std::span<const std::uint8_t> work_ram,
            std::span<const std::uint8_t> shared_ram) noexcept;

        [[nodiscard]] bool qsound_live_trace_z80_pc() noexcept;

        void qsound_live_trace_z80_bank_read(std::uint16_t address,
                                             std::uint32_t rom_address,
                                             std::uint8_t value,
                                             std::uint16_t pc,
                                             std::uint64_t sound_cycles,
                                             std::uint8_t bank,
                                             std::span<const std::uint8_t> work_ram,
                                             std::span<const std::uint8_t> shared_ram) noexcept;

        void qsound_live_trace_register(std::uint8_t reg,
                                        std::uint16_t data,
                                        std::uint16_t pc,
                                        std::uint64_t sound_cycles,
                                        std::uint8_t bank,
                                        std::span<const std::uint8_t> work_ram,
                                        std::span<const std::uint8_t> shared_ram) noexcept;

        [[nodiscard]] bool qsound_live_trace_pc_is_interesting(std::uint32_t pc) noexcept;

        void qsound_live_trace_opcode_scan(
            std::span<const std::uint8_t> opcode_image) noexcept;

        [[nodiscard]] bool qsound_live_trace_noisy() noexcept;

        [[nodiscard]] bool qsound_live_trace_main_ram_access_is_interesting(
            std::uint32_t address,
            bool write) noexcept;

        void qsound_live_trace_main_ram_access(std::uint32_t address,
                                               std::uint8_t value,
                                               bool write,
                                               std::uint32_t pc,
                                               std::uint64_t main_cycles,
                                               const chips::cpu::m68000::registers& regs,
                                               std::span<const std::uint8_t> main_ram) noexcept;

        void qsound_live_trace_bank_write(std::uint8_t raw,
                                          std::uint8_t bank,
                                          std::uint16_t pc,
                                          std::uint64_t sound_cycles,
                                          std::span<const std::uint8_t> work_ram,
                                          std::span<const std::uint8_t> shared_ram) noexcept;

        [[nodiscard]] bool qsound_live_trace_access_only() noexcept;

        void qsound_live_trace_68k_shared_write(std::uint16_t index,
                                                std::uint8_t value,
                                                std::uint32_t pc,
                                                std::uint64_t main_cycles,
                                                const chips::cpu::m68000::registers& regs,
                                                std::span<const std::uint8_t> main_ram,
                                                std::span<const std::uint8_t> work_ram,
                                                std::span<const std::uint8_t> shared_ram) noexcept;

        void qsound_live_trace_68k_shared_read(std::uint16_t index,
                                               std::uint8_t value,
                                               std::uint32_t pc,
                                               std::uint64_t main_cycles,
                                               std::span<const std::uint8_t> work_ram,
                                               std::span<const std::uint8_t> shared_ram) noexcept;

        void qsound_live_trace_68k_pc(std::uint32_t pc,
                                      std::uint64_t main_cycles,
                                      const chips::cpu::m68000::registers& regs,
                                      std::span<const std::uint8_t> main_ram,
                                      std::span<const std::uint8_t> opcode_image) noexcept;

    } // namespace qsound_trace
} // namespace mnemos::manifests::capcom_cps2

#endif // MNEMOS_MANIFESTS_CAPCOM_CPS2_CAPCOM_CPS2_QSOUND_TRACE_HPP
