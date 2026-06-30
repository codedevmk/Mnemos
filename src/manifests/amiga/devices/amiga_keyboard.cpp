#include "devices/amiga_keyboard.hpp"

namespace mnemos::manifests::amiga {

    std::uint8_t amiga_keyboard_raw_key(std::uint8_t raw_keycode) noexcept {
        return static_cast<std::uint8_t>(raw_keycode & 0x7FU);
    }

    std::uint8_t amiga_keyboard_edge_code(std::uint8_t raw_key, bool pressed) noexcept {
        return static_cast<std::uint8_t>(amiga_keyboard_raw_key(raw_key) |
                                         (pressed ? 0x00U : 0x80U));
    }

    std::uint8_t amiga_keyboard_sdr(std::uint8_t raw_code) noexcept {
        const auto inverted = static_cast<std::uint8_t>(~raw_code);
        return static_cast<std::uint8_t>((inverted << 1U) | (inverted >> 7U));
    }

    bool amiga_keyboard_enqueue_code(amiga_keyboard_queue_state& keyboard,
                                     std::uint8_t code) noexcept {
        if (keyboard.count >= keyboard.queue.size()) {
            return false;
        }

        const std::size_t tail = (keyboard.head + keyboard.count) % keyboard.queue.size();
        keyboard.queue[tail] = code;
        ++keyboard.count;
        return true;
    }

    bool amiga_keyboard_enqueue_key(amiga_keyboard_queue_state& keyboard,
                                    std::uint8_t raw_keycode, bool pressed) noexcept {
        const std::uint8_t key = amiga_keyboard_raw_key(raw_keycode);
        if (keyboard.key_down[key] == pressed) {
            return false;
        }

        if (!amiga_keyboard_enqueue_code(keyboard, amiga_keyboard_edge_code(key, pressed))) {
            return false;
        }

        keyboard.key_down[key] = pressed;
        return true;
    }

    bool amiga_keyboard_enqueue_control_code(amiga_keyboard_queue_state& keyboard,
                                             std::uint8_t code) noexcept {
        return amiga_keyboard_enqueue_code(keyboard, code);
    }

    bool amiga_keyboard_press_caps_lock(amiga_keyboard_queue_state& keyboard) noexcept {
        const bool next_led = !keyboard.caps_lock_led;
        const std::uint8_t code =
            amiga_keyboard_edge_code(amiga_keyboard_caps_lock_raw_key, next_led);
        if (!amiga_keyboard_enqueue_code(keyboard, code)) {
            return false;
        }

        keyboard.caps_lock_led = next_led;
        return true;
    }

    bool amiga_keyboard_dequeue_code(amiga_keyboard_queue_state& keyboard,
                                     std::uint8_t& code) noexcept {
        if (keyboard.count == 0U) {
            return false;
        }

        code = keyboard.queue[keyboard.head];
        keyboard.head = (keyboard.head + 1U) % keyboard.queue.size();
        --keyboard.count;
        return true;
    }

    void amiga_keyboard_reset(amiga_keyboard_queue_state& keyboard) noexcept {
        keyboard.queue.fill(0U);
        keyboard.key_down.fill(false);
        keyboard.head = 0U;
        keyboard.count = 0U;
        keyboard.caps_lock_led = false;
    }

} // namespace mnemos::manifests::amiga
