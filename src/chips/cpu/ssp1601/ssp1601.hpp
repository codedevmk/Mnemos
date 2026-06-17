#pragma once

#include "chip.hpp"
#include "cpu_catch_up.hpp"
#include "introspection_adapters.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <span>

namespace mnemos::chips::cpu {

    // Samsung SSP1601 16-bit fixed-point DSP (the co-processor on a single
    // Sega Genesis cartridge; "SVP").
    //
    // Ported from the Emu reference (chips/ssp1601); clean-room per the Samsung
    // SSP1601 DSP datasheet. The Emu reference carries only the register-file
    // declarations + lifecycle (its opcode dispatch is a documented stub); this
    // port keeps that exact register-file layout and adds a clean-room
    // fetch/decode/execute for the common instruction classes.
    //
    // Architecture summary (per the SSP1601 datasheet):
    //   - 16-bit Harvard DSP: separate program space (word-addressed, 16-bit)
    //     and a register file of general + "external" pointer registers.
    //   - 16x16 multiply unit feeding a 32-bit product P; a 32-bit accumulator A.
    //   - A status register ST holding ALU flags (N/Z/V/C) plus mode/bank bits.
    //   - A 6-deep hardware return stack (CALL/RET, addressed as a register).
    //   - 1024-word on-chip IRAM (host-loadable) and 1024-word on-chip IROM.
    //
    // The DSP is instruction-stepped: step_instruction() executes exactly one
    // instruction and returns its cycle cost; tick(cycles) catches up by running
    // whole instructions. Program/operand memory is the attached ibus, addressed
    // in 16-bit WORDS (the bus address is the word index; the chip composes two
    // big-endian bytes per word through the byte ibus contract).
    //
    // SCOPE: this is a correct, compiling SUBSET of the ISA (partial port). The
    // implemented classes are: the LD register/immediate group (general + the
    // accumulator-low / status accumulator-load forms), the ALU group
    // (ADD/SUB/CMP/AND/OR/EOR against a register or immediate, plus the implied
    // accumulator forms), the 16x16 MLD/MPYA-style multiply into P with the
    // P->A move, the unconditional/conditional CALL/JMP/RET control transfers
    // through the hardware stack, and LD via the implied (ri)/(ext) pointer
    // operand. The remaining ASIC-specific addressing modes (modulo/ring
    // pointer auto-stepping, the external-memory PM bank programming, and the
    // exact per-mode cycle table) are stubbed: an unrecognised opcode is treated
    // as a 1-cycle NOP and latched in `last_undecoded_`.
    class ssp1601 final : public icpu, public cpu_catch_up<ssp1601> {
      public:
        // ST (status register) flag bits. ALU operations set N/Z/V/C; the low
        // byte carries mode/bank/ring-pointer state preserved across ALU ops.
        static constexpr std::uint16_t flag_l = 0x0001U;  // link/carry (bit 0)
        static constexpr std::uint16_t flag_ov = 0x0002U; // overflow
        static constexpr std::uint16_t flag_z = 0x0004U;  // zero
        static constexpr std::uint16_t flag_n = 0x0008U;  // negative
        static constexpr std::uint16_t flag_mask = flag_l | flag_ov | flag_z | flag_n;

        static constexpr unsigned iram_words = 1024U;
        static constexpr unsigned irom_words = 1024U;
        static constexpr unsigned stack_depth = 6U;
        static constexpr unsigned ext_regs = 8U;
        static constexpr unsigned gp_regs = 8U;

        // A snapshot / load image of the architectural register file. Mirrors the
        // Emu reference struct field-for-field.
        struct registers final {
            std::uint16_t x{};
            std::uint16_t y{};
            std::uint32_t a{}; // 32-bit accumulator (A_h:A_l)
            std::uint32_t p{}; // 32-bit product (P_h:P_l)
            std::uint16_t st{};
            std::uint16_t pc{};
            std::array<std::uint16_t, stack_depth> stack{};
            std::uint8_t sp{};
            std::array<std::uint16_t, ext_regs> ext{};
            std::array<std::uint16_t, gp_regs> r{};
            std::uint16_t ij{};
            std::uint16_t ik{};
            bool halted{};
        };

        ssp1601() {
            introspection_.with_registers([this] { return register_snapshot(); })
                .with_trace(instrumentation::pc_trace_installer(
                    trace_callback_, [this] { return elapsed_cycles(); }));
            reset(reset_kind::power_on);
        }

        [[nodiscard]] chip_metadata metadata() const noexcept override;
        void tick(std::uint64_t cycles) override;
        void reset(reset_kind kind) override;

        void save_state(state_writer& writer) const override;
        void load_state(state_reader& reader) override;

        [[nodiscard]] instrumentation::ichip_introspection& introspection() noexcept override;

        // icpu: the (word-addressed) program/operand space the DSP runs against.
        void attach_bus(ibus& bus) noexcept override { bus_ = &bus; }

        // Execute exactly one instruction; returns the cycles it consumed.
        int step_instruction();

        [[nodiscard]] registers cpu_registers() const noexcept;
        void set_registers(const registers& values) noexcept;
        [[nodiscard]] std::uint64_t elapsed_cycles() const noexcept { return elapsed_; }
        [[nodiscard]] bool at_instruction_boundary() const noexcept { return true; }

        // On-chip program/library memory (host-loaded before reset release).
        [[nodiscard]] std::span<std::uint16_t> iram() noexcept { return iram_; }
        [[nodiscard]] std::span<std::uint16_t> irom() noexcept { return irom_; }

        // True once the DSP executes a stop/halt-style transfer (or runs off a
        // RET with an empty stack); cleared by reset.
        [[nodiscard]] bool halted() const noexcept { return halted_; }

        // The last opcode the decoder did not recognise (diagnostic for the
        // partial port); 0 when every executed opcode has decoded.
        [[nodiscard]] std::uint16_t last_undecoded() const noexcept { return last_undecoded_; }

        [[nodiscard]] std::span<const register_descriptor> register_snapshot() noexcept;

      private:
        // ---- accumulator halves ----
        [[nodiscard]] std::uint16_t a_hi() const noexcept {
            return static_cast<std::uint16_t>(a_ >> 16U);
        }
        [[nodiscard]] std::uint16_t a_lo() const noexcept { return static_cast<std::uint16_t>(a_); }
        void set_a_hi(std::uint16_t v) noexcept {
            a_ = (a_ & 0x0000FFFFU) | (static_cast<std::uint32_t>(v) << 16U);
        }
        void set_a_lo(std::uint16_t v) noexcept { a_ = (a_ & 0xFFFF0000U) | v; }

        // ---- program/operand memory (16-bit words over the byte ibus) ----
        [[nodiscard]] std::uint16_t read_word(std::uint16_t word_addr) noexcept;
        void write_word(std::uint16_t word_addr, std::uint16_t value) noexcept;
        [[nodiscard]] std::uint16_t fetch() noexcept; // fetch a program word at PC, PC++

        // ---- hardware return stack ----
        void push_pc(std::uint16_t value) noexcept;
        [[nodiscard]] std::uint16_t pop_pc() noexcept;

        // ---- general-register file (3-bit operand selector) ----
        [[nodiscard]] std::uint16_t read_gr(int sel) noexcept;
        void write_gr(int sel, std::uint16_t value) noexcept;

        // ---- ALU ----
        void set_nz(std::uint32_t result) noexcept;
        [[nodiscard]] std::uint32_t alu(int op, std::uint32_t acc, std::uint16_t operand) noexcept;
        [[nodiscard]] bool test_cond(int cond) const noexcept;

        std::uint16_t x_{};
        std::uint16_t y_{};
        std::uint32_t a_{};
        std::uint32_t p_{};
        std::uint16_t st_{};
        std::uint16_t pc_{};
        std::array<std::uint16_t, stack_depth> stack_{};
        std::uint8_t sp_{};
        std::array<std::uint16_t, ext_regs> ext_{};
        std::array<std::uint16_t, gp_regs> r_{};
        std::uint16_t ij_{};
        std::uint16_t ik_{};
        bool halted_{};

        std::uint16_t last_undecoded_{};

        int step_cycles_{};
        friend class cpu_catch_up<ssp1601>;
        std::uint64_t elapsed_{};

        ibus* bus_{};

        std::array<std::uint16_t, iram_words> iram_{};
        std::array<std::uint16_t, irom_words> irom_{};

        std::function<void(std::uint32_t pc)> trace_callback_{};
        std::array<register_descriptor, 11> register_view_{};
        instrumentation::introspection_builder introspection_;
    };

} // namespace mnemos::chips::cpu
