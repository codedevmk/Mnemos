#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace mnemos::manifests::amiga {

    inline constexpr std::size_t amiga_keyboard_raw_key_count = 128U;
    inline constexpr std::size_t amiga_keyboard_queue_capacity = 16U;

    inline constexpr std::uint8_t amiga_keyboard_reset_warning_code = 0x78U;
    inline constexpr std::uint8_t amiga_keyboard_last_code_bad_code = 0xF9U;
    inline constexpr std::uint8_t amiga_keyboard_buffer_overflow_code = 0xFAU;
    inline constexpr std::uint8_t amiga_keyboard_self_test_fail_code = 0xFCU;
    inline constexpr std::uint8_t amiga_keyboard_powerup_stream_start_code = 0xFDU;
    inline constexpr std::uint8_t amiga_keyboard_powerup_stream_end_code = 0xFEU;
    inline constexpr std::uint8_t amiga_keyboard_caps_lock_raw_key = 0x62U;

    struct amiga_keyboard_queue_state final {
        std::array<std::uint8_t, amiga_keyboard_queue_capacity> queue{};
        std::array<bool, amiga_keyboard_raw_key_count> key_down{};
        std::size_t head{};
        std::size_t count{};
        bool caps_lock_led{};
    };

    [[nodiscard]] std::uint8_t amiga_keyboard_raw_key(std::uint8_t raw_keycode) noexcept;

    [[nodiscard]] std::uint8_t amiga_keyboard_edge_code(std::uint8_t raw_key,
                                                        bool pressed) noexcept;

    [[nodiscard]] std::uint8_t amiga_keyboard_sdr(std::uint8_t raw_code) noexcept;

    [[nodiscard]] bool amiga_keyboard_enqueue_code(amiga_keyboard_queue_state& keyboard,
                                                   std::uint8_t code) noexcept;

    [[nodiscard]] bool amiga_keyboard_enqueue_key(amiga_keyboard_queue_state& keyboard,
                                                  std::uint8_t raw_keycode,
                                                  bool pressed) noexcept;

    [[nodiscard]] bool amiga_keyboard_enqueue_control_code(amiga_keyboard_queue_state& keyboard,
                                                           std::uint8_t code) noexcept;

    [[nodiscard]] bool amiga_keyboard_press_caps_lock(amiga_keyboard_queue_state& keyboard) noexcept;

    [[nodiscard]] bool amiga_keyboard_dequeue_code(amiga_keyboard_queue_state& keyboard,
                                                   std::uint8_t& code) noexcept;

    void amiga_keyboard_reset(amiga_keyboard_queue_state& keyboard) noexcept;

} // namespace mnemos::manifests::amiga
