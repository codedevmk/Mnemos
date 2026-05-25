#pragma once

#include <cstdint>
#include <span>
#include <vector>

// Concrete serialisation for the i_chip save_state / load_state hooks (ADR 0008).
// Primitives are encoded little-endian so a state written on one platform reloads
// bit-identically on another. The reader is bounds-checked: an underrun flips the
// ok() flag and yields zero/empty instead of reading out of bounds, so a caller
// validates a chunk by checking ok() after decoding.
namespace mnemos::chips {

    class state_writer final {
      public:
        explicit state_writer(std::vector<std::uint8_t>& sink) noexcept : sink_(sink) {}

        void u8(std::uint8_t value);
        void u16(std::uint16_t value);
        void u32(std::uint32_t value);
        void u64(std::uint64_t value);
        void boolean(bool value) { u8(value ? 1U : 0U); }

        // Raw bytes, no length prefix (the layout is implied by the chunk schema).
        void bytes(std::span<const std::uint8_t> data);
        // Length-prefixed bytes (u32 length + payload).
        void blob(std::span<const std::uint8_t> data);

        [[nodiscard]] std::size_t size() const noexcept { return sink_.size(); }

      private:
        std::vector<std::uint8_t>& sink_;
    };

    class state_reader final {
      public:
        explicit state_reader(std::span<const std::uint8_t> data) noexcept : data_(data) {}

        [[nodiscard]] std::uint8_t u8() noexcept;
        [[nodiscard]] std::uint16_t u16() noexcept;
        [[nodiscard]] std::uint32_t u32() noexcept;
        [[nodiscard]] std::uint64_t u64() noexcept;
        [[nodiscard]] bool boolean() noexcept { return u8() != 0U; }

        // Read exactly out.size() raw bytes into out.
        void bytes(std::span<std::uint8_t> out) noexcept;
        // Read a length-prefixed blob written by blob().
        [[nodiscard]] std::vector<std::uint8_t> blob();

        [[nodiscard]] bool ok() const noexcept { return ok_; }
        [[nodiscard]] std::size_t remaining() const noexcept { return data_.size() - pos_; }

      private:
        [[nodiscard]] bool need(std::size_t count) noexcept;

        std::span<const std::uint8_t> data_;
        std::size_t pos_{};
        bool ok_{true};
    };

} // namespace mnemos::chips
