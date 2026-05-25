#include <mnemos/chips/common/state.hpp>

#include <algorithm>
#include <cstring>

namespace mnemos::chips {

    void state_writer::u8(std::uint8_t value) { sink_.push_back(value); }

    void state_writer::u16(std::uint16_t value) {
        u8(static_cast<std::uint8_t>(value & 0xFFU));
        u8(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
    }

    void state_writer::u32(std::uint32_t value) {
        u16(static_cast<std::uint16_t>(value & 0xFFFFU));
        u16(static_cast<std::uint16_t>((value >> 16U) & 0xFFFFU));
    }

    void state_writer::u64(std::uint64_t value) {
        u32(static_cast<std::uint32_t>(value & 0xFFFFFFFFU));
        u32(static_cast<std::uint32_t>((value >> 32U) & 0xFFFFFFFFU));
    }

    void state_writer::bytes(std::span<const std::uint8_t> data) {
        sink_.insert(sink_.end(), data.begin(), data.end());
    }

    void state_writer::blob(std::span<const std::uint8_t> data) {
        u32(static_cast<std::uint32_t>(data.size()));
        bytes(data);
    }

    bool state_reader::need(std::size_t count) noexcept {
        if (!ok_ || pos_ + count > data_.size()) {
            ok_ = false;
            return false;
        }
        return true;
    }

    std::uint8_t state_reader::u8() noexcept {
        if (!need(1U)) {
            return 0U;
        }
        return data_[pos_++];
    }

    std::uint16_t state_reader::u16() noexcept {
        if (!need(2U)) {
            return 0U;
        }
        const std::uint16_t lo = u8();
        const std::uint16_t hi = u8();
        return static_cast<std::uint16_t>(lo | (hi << 8U));
    }

    std::uint32_t state_reader::u32() noexcept {
        if (!need(4U)) {
            return 0U;
        }
        const std::uint32_t lo = u16();
        const std::uint32_t hi = u16();
        return lo | (hi << 16U);
    }

    std::uint64_t state_reader::u64() noexcept {
        if (!need(8U)) {
            return 0U;
        }
        const std::uint64_t lo = u32();
        const std::uint64_t hi = u32();
        return lo | (hi << 32U);
    }

    void state_reader::bytes(std::span<std::uint8_t> out) noexcept {
        if (!need(out.size())) {
            std::fill(out.begin(), out.end(), std::uint8_t{0});
            return;
        }
        if (!out.empty()) {
            std::memcpy(out.data(), data_.data() + pos_, out.size());
            pos_ += out.size();
        }
    }

    std::vector<std::uint8_t> state_reader::blob() {
        const std::uint32_t length = u32();
        if (!need(length)) {
            return {};
        }
        std::vector<std::uint8_t> out(data_.begin() + static_cast<std::ptrdiff_t>(pos_),
                                      data_.begin() + static_cast<std::ptrdiff_t>(pos_ + length));
        pos_ += length;
        return out;
    }

} // namespace mnemos::chips
