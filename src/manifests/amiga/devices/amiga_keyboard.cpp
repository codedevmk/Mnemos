#include "devices/amiga_keyboard.hpp"

#include "state.hpp"

#include <algorithm>

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

    bool amiga_keyboard_serial_busy(const amiga_keyboard_queue_state& keyboard) noexcept {
        return keyboard.byte_in_flight;
    }

    bool amiga_keyboard_ack_low_seen(const amiga_keyboard_queue_state& keyboard) noexcept {
        return keyboard.ack_low_seen;
    }

    void amiga_keyboard_begin_serial_byte(amiga_keyboard_queue_state& keyboard) noexcept {
        keyboard.byte_in_flight = true;
        keyboard.ack_low_seen = false;
    }

    void amiga_keyboard_accept_serial_ack_level(amiga_keyboard_queue_state& keyboard,
                                                bool level) noexcept {
        if (!keyboard.byte_in_flight) {
            keyboard.ack_low_seen = false;
            return;
        }
        if (!level) {
            keyboard.ack_low_seen = true;
            return;
        }
        if (!keyboard.ack_low_seen) {
            return;
        }
        keyboard.ack_low_seen = false;
        keyboard.byte_in_flight = false;
    }

    void amiga_keyboard_save_state(const amiga_keyboard_queue_state& keyboard,
                                   chips::state_writer& writer) {
        writer.u8(static_cast<std::uint8_t>(keyboard.count));
        writer.boolean(keyboard.byte_in_flight);
        writer.boolean(keyboard.ack_low_seen);
        writer.boolean(keyboard.caps_lock_led);
        for (std::size_t i = 0U; i < keyboard.queue.size(); ++i) {
            const std::uint8_t value =
                i < keyboard.count
                    ? keyboard.queue[(keyboard.head + i) % keyboard.queue.size()]
                    : 0U;
            writer.u8(value);
        }
        for (bool down : keyboard.key_down) {
            writer.boolean(down);
        }
    }

    void amiga_keyboard_load_state(amiga_keyboard_queue_state& keyboard,
                                   chips::state_reader& reader) {
        const std::uint8_t saved_queue_count = reader.u8();
        keyboard.byte_in_flight = reader.boolean();
        keyboard.ack_low_seen = reader.boolean();
        keyboard.caps_lock_led = reader.boolean();
        keyboard.head = 0U;
        keyboard.count = std::min<std::size_t>(saved_queue_count, keyboard.queue.size());
        for (std::size_t i = 0U; i < keyboard.queue.size(); ++i) {
            const std::uint8_t value = reader.u8();
            keyboard.queue[i] = i < keyboard.count ? value : 0U;
        }
        for (bool& down : keyboard.key_down) {
            down = reader.boolean();
        }
    }

    void amiga_keyboard_reset(amiga_keyboard_queue_state& keyboard) noexcept {
        keyboard.queue.fill(0U);
        keyboard.key_down.fill(false);
        keyboard.head = 0U;
        keyboard.count = 0U;
        keyboard.caps_lock_led = false;
        keyboard.byte_in_flight = false;
        keyboard.ack_low_seen = false;
    }

} // namespace mnemos::manifests::amiga
