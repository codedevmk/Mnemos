#pragma once

#include <array>
#include <cstdint>

namespace mnemos::manifests::c64 {

    // The Commodore 64 keyboard matrix (8 columns on CIA1 PRA, 8 rows on CIA1 PRB)
    // and the two joysticks (joy 2 on PRA bits 0-4, joy 1 on PRB bits 0-4, both
    // active-low and overlaid on the keyboard lines). The KERNAL scans by driving a
    // column low on PRA and reading the rows on PRB; a pressed key shorts its column
    // to its row. This models the bidirectional matrix + the joystick overlay so the
    // CIA1 read callbacks can resolve either direction.
    class c64_input final {
      public:
        // Each key is (column << 3) | row, matching CIA1 PRA (column) / PRB (row).
        enum class key : std::uint8_t {
            // clang-format off
            ins_del = 000, ret = 001, crsr_lr = 002, f7 = 003, f1 = 004, f3 = 005, f5 = 006, crsr_ud = 007,
            k3 = 010, w = 011, a = 012, k4 = 013, z = 014, s = 015, e = 016, lshift = 017,
            k5 = 020, r = 021, d = 022, k6 = 023, c = 024, f = 025, t = 026, x = 027,
            k7 = 030, y = 031, g = 032, k8 = 033, b = 034, h = 035, u = 036, v = 037,
            k9 = 040, i = 041, j = 042, k0 = 043, m = 044, k = 045, o = 046, n = 047,
            plus = 050, p = 051, l = 052, minus = 053, period = 054, colon = 055, at = 056, comma = 057,
            pound = 060, asterisk = 061, semicolon = 062, home = 063, rshift = 064, equals = 065, up_arrow = 066, slash = 067,
            k1 = 070, left_arrow = 071, ctrl = 072, k2 = 073, space = 074, cbm = 075, q = 076, run_stop = 077,
            // clang-format on
        };

        // Joystick direction/fire bits (active when pressed).
        static constexpr std::uint8_t joy_up = 0x01U;
        static constexpr std::uint8_t joy_down = 0x02U;
        static constexpr std::uint8_t joy_left = 0x04U;
        static constexpr std::uint8_t joy_right = 0x08U;
        static constexpr std::uint8_t joy_fire = 0x10U;

        void set_key(key k, bool pressed) noexcept;
        void press(key k) noexcept { set_key(k, true); }
        void release(key k) noexcept { set_key(k, false); }
        void release_all_keys() noexcept { matrix_.fill(0U); }

        // Set joystick `port` (1 or 2) to the given direction/fire bitmask.
        void set_joystick(std::uint8_t port, std::uint8_t mask) noexcept;

        // CIA1 read resolution. read_rows is PRB given the PRA column strobe;
        // read_columns is PRA given the PRB row strobe. Both are active-low and
        // fold in the matching joystick.
        [[nodiscard]] std::uint8_t read_rows(std::uint8_t column_strobe) const noexcept;
        [[nodiscard]] std::uint8_t read_columns(std::uint8_t row_strobe) const noexcept;

      private:
        std::array<std::uint8_t, 8> matrix_{}; // matrix_[column] bit `row` = pressed
        std::uint8_t joy1_{};                  // PRB bits 0-4 (active-low overlay)
        std::uint8_t joy2_{};                  // PRA bits 0-4
    };

} // namespace mnemos::manifests::c64
