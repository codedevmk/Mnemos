#include "sega32x_pwm_dc_blocker.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <vector>

namespace {

    using mnemos::manifests::sega32x::pwm_dc_blocker;

    // The former float implementation, kept as the fidelity reference: the
    // fixed-point filter must reproduce it within one output LSB.
    struct float_dc_blocker {
        float px = 0.0F;
        float py = 0.0F;
        std::int16_t step(std::int16_t x) {
            const float y = static_cast<float>(x) - px + 0.999F * py;
            px = static_cast<float>(x);
            py = y;
            const float c = y < -32768.0F ? -32768.0F : (y > 32767.0F ? 32767.0F : y);
            return static_cast<std::int16_t>(c);
        }
    };

    // Deterministic signal: a tone, a pseudo-random component, and a sustained DC
    // pedestal in the second half -- transients, sign changes, and a held level.
    std::vector<std::int16_t> test_signal(std::size_t n) {
        std::vector<std::int16_t> s;
        s.reserve(n);
        std::uint32_t lcg = 0x12345678U;
        for (std::size_t i = 0; i < n; ++i) {
            lcg = lcg * 1664525U + 1013904223U;
            const int noise = static_cast<int>((lcg >> 16) & 0x1FFFU) - 0x1000;
            const int tone = static_cast<int>(20000.0 * std::sin(0.01 * static_cast<double>(i)));
            const int dc = i > n / 2 ? 12000 : 0;
            int v = tone + noise + dc;
            v = v < -32768 ? -32768 : (v > 32767 ? 32767 : v);
            s.push_back(static_cast<std::int16_t>(v));
        }
        return s;
    }

} // namespace

TEST_CASE("PWM DC blocker reproduces the float reference within one LSB") {
    pwm_dc_blocker fixed;
    float_dc_blocker reference;
    int max_abs_err = 0;
    for (const std::int16_t x : test_signal(20000)) {
        const int a = fixed.step(x);
        const int b = reference.step(x);
        max_abs_err = std::max(max_abs_err, std::abs(a - b));
    }
    CHECK(max_abs_err <= 1);
}

TEST_CASE("PWM DC blocker removes a held DC level") {
    pwm_dc_blocker f;
    std::int16_t out = 0;
    for (int i = 0; i < 50; ++i) {
        out = f.step(10000);
    }
    const int early = std::abs(static_cast<int>(out)); // pedestal still present
    for (int i = 0; i < 20000; ++i) {
        out = f.step(10000);
    }
    const int late = std::abs(static_cast<int>(out));
    CHECK(early > 1000); // the held level was genuinely there
    CHECK(late < early);  // and it decays...
    CHECK(late <= 1);     // ...to silence
}

TEST_CASE("PWM DC blocker passes an AC edge") {
    pwm_dc_blocker f;
    for (int i = 0; i < 5000; ++i) {
        static_cast<void>(f.step(8000)); // settle: the DC component is removed
    }
    const int before = std::abs(static_cast<int>(f.step(8000)));   // ~0
    const int edge = std::abs(static_cast<int>(f.step(-8000)));    // a 16000 swing
    CHECK(edge > before + 10000);
}

TEST_CASE("PWM DC blocker is reproducible across instances") {
    pwm_dc_blocker a;
    pwm_dc_blocker b;
    for (const std::int16_t x : test_signal(2000)) {
        CHECK(a.step(x) == b.step(x));
    }
}
