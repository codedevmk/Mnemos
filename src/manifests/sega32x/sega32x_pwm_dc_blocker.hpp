#pragma once

#include <cstdint>

namespace mnemos::manifests::sega32x {

    // Deterministic one-pole DC blocker for the 32X PWM audio path.
    //
    // The PWM output is AC-coupled into the mix so a held duty (a carrier nobody
    // feeds) decays to silence instead of sitting as a DC pedestal. This is the
    // high-pass y[n] = (x[n] - x[n-1]) + R*y[n-1] with R = 0.999, evaluated in
    // fixed point: ARCH-004 §16 forbids platform-dependent floating point on the
    // determinism-relevant path, and the contract's "audio sample sequence"
    // includes the PWM stream. Integer math makes the output bit-identical on
    // every target.
    //
    // The accumulator y is Q16; the coefficient R is Q30, so the realised pole
    // 1072668082/2^30 differs from 0.999 by ~1.6e-10 (far under one output LSB).
    // The impulse response has L1 norm 2, so |y| <= 2*32768 for int16 input and
    // the int64 coefficient product stays below 2^62 -- no overflow. Only the
    // returned sample is clamped; the accumulator is left unclamped, matching the
    // reference filter's feedback.
    struct pwm_dc_blocker final {
        static constexpr int accum_frac_bits = 16;
        static constexpr int coeff_frac_bits = 30;
        static constexpr std::int64_t coeff_r = 1072668082; // round(0.999 * 2^30)

        std::int32_t prev_x = 0;  // x[n-1] (exact int16 input)
        std::int64_t y_accum = 0; // y[n-1] in Q16

        [[nodiscard]] std::int16_t step(std::int16_t x) noexcept {
            const std::int64_t y =
                (static_cast<std::int64_t>(x - prev_x) << accum_frac_bits) +
                ((coeff_r * y_accum) >> coeff_frac_bits);
            prev_x = x;
            y_accum = y;
            const std::int64_t sample = y / (std::int64_t{1} << accum_frac_bits); // trunc toward 0
            const std::int64_t clamped =
                sample < -32768 ? -32768 : (sample > 32767 ? 32767 : sample);
            return static_cast<std::int16_t>(clamped);
        }
    };

} // namespace mnemos::manifests::sega32x
