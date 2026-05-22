#pragma once

#include <mnemos/foundation/expected_ext.hpp>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <new>
#include <span>

namespace mnemos::foundation {

    enum class allocator_error : std::uint8_t {
        empty_storage,
        invalid_size,
        invalid_alignment,
        out_of_memory,
        invalid_block,
        invalid_pointer,
    };

    struct memory_block final {
        std::byte* data{};
        std::size_t size{};

        [[nodiscard]] constexpr explicit operator bool() const noexcept {
            return data != nullptr && size != 0U;
        }

        [[nodiscard]] constexpr std::span<std::byte> bytes() const noexcept { return {data, size}; }
    };

    using allocation_result = expected<memory_block, allocator_error>;
    using allocator_status = status<allocator_error>;

    [[nodiscard]] constexpr bool is_valid_alignment(std::size_t alignment) noexcept {
        return alignment != 0U && std::has_single_bit(alignment);
    }

    [[nodiscard]] constexpr std::uintptr_t align_forward(std::uintptr_t value,
                                                         std::size_t alignment) noexcept {
        return (value + alignment - 1U) & ~(static_cast<std::uintptr_t>(alignment) - 1U);
    }

    class linear_arena final {
      public:
        explicit linear_arena(std::span<std::byte> storage) noexcept
            : begin_(storage.data()), capacity_(storage.size()) {}

        linear_arena(const linear_arena&) = delete;
        linear_arena& operator=(const linear_arena&) = delete;
        linear_arena(linear_arena&&) noexcept = default;
        linear_arena& operator=(linear_arena&&) noexcept = default;

        [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }

        [[nodiscard]] std::size_t used() const noexcept { return offset_; }

        [[nodiscard]] std::size_t remaining() const noexcept { return capacity_ - offset_; }

        [[nodiscard]] std::size_t peak_used() const noexcept { return peak_; }

        [[nodiscard]] bool owns(const void* pointer) const noexcept {
            if (begin_ == nullptr || pointer == nullptr) {
                return false;
            }

            const auto address = reinterpret_cast<std::uintptr_t>(pointer);
            const auto begin = reinterpret_cast<std::uintptr_t>(begin_);
            return address >= begin && address < begin + capacity_;
        }

        [[nodiscard]] allocation_result
        allocate(std::size_t size, std::size_t alignment = alignof(std::max_align_t)) noexcept {
            if (begin_ == nullptr || capacity_ == 0U) {
                return unexpected(allocator_error::empty_storage);
            }

            if (size == 0U) {
                return unexpected(allocator_error::invalid_size);
            }

            if (!is_valid_alignment(alignment)) {
                return unexpected(allocator_error::invalid_alignment);
            }

            const auto begin = reinterpret_cast<std::uintptr_t>(begin_);
            const auto current = begin + offset_;
            const auto end = begin + capacity_;
            const auto aligned = align_forward(current, alignment);

            if (aligned < current || aligned > end || size > end - aligned) {
                return unexpected(allocator_error::out_of_memory);
            }

            offset_ = static_cast<std::size_t>((aligned - begin) + size);
            if (offset_ > peak_) {
                peak_ = offset_;
            }

            return memory_block{
                .data = reinterpret_cast<std::byte*>(aligned),
                .size = size,
            };
        }

        void reset() noexcept { offset_ = 0U; }

      private:
        std::byte* begin_{};
        std::size_t capacity_{};
        std::size_t offset_{};
        std::size_t peak_{};
    };

    class fixed_block_pool final {
      public:
        fixed_block_pool() = default;

        fixed_block_pool(const fixed_block_pool&) = delete;
        fixed_block_pool& operator=(const fixed_block_pool&) = delete;
        fixed_block_pool(fixed_block_pool&&) noexcept = default;
        fixed_block_pool& operator=(fixed_block_pool&&) noexcept = default;

        [[nodiscard]] static expected<fixed_block_pool, allocator_error>
        create(std::span<std::byte> storage, std::size_t block_size,
               std::size_t alignment = alignof(std::max_align_t)) noexcept {
            if (storage.empty() || storage.data() == nullptr) {
                return unexpected(allocator_error::empty_storage);
            }

            if (block_size == 0U) {
                return unexpected(allocator_error::invalid_size);
            }

            if (!is_valid_alignment(alignment)) {
                return unexpected(allocator_error::invalid_alignment);
            }

            fixed_block_pool pool;
            pool.block_size_ = block_size;
            const std::size_t minimum_stride =
                block_size < sizeof(free_node) ? sizeof(free_node) : block_size;
            pool.stride_ = align_forward(minimum_stride, alignment);
            if (pool.stride_ < minimum_stride) {
                return unexpected(allocator_error::out_of_memory);
            }

            const auto storage_begin = reinterpret_cast<std::uintptr_t>(storage.data());
            const auto aligned_begin = align_forward(storage_begin, alignment);
            const auto storage_end = storage_begin + storage.size();
            if (storage_end < storage_begin || aligned_begin < storage_begin ||
                aligned_begin >= storage_end || pool.stride_ > storage_end - aligned_begin) {
                return unexpected(allocator_error::out_of_memory);
            }

            pool.begin_ = reinterpret_cast<std::byte*>(aligned_begin);
            pool.capacity_ = static_cast<std::size_t>((storage_end - aligned_begin) / pool.stride_);
            pool.free_count_ = pool.capacity_;

            for (std::size_t index = 0U; index < pool.capacity_; ++index) {
                auto* node = ::new (pool.begin_ + (index * pool.stride_)) free_node{pool.free_};
                pool.free_ = node;
            }

            return pool;
        }

        [[nodiscard]] std::size_t block_size() const noexcept { return block_size_; }

        [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }

        [[nodiscard]] std::size_t available() const noexcept { return free_count_; }

        [[nodiscard]] std::size_t used() const noexcept { return capacity_ - free_count_; }

        [[nodiscard]] bool owns(const void* pointer) const noexcept {
            if (begin_ == nullptr || pointer == nullptr || capacity_ == 0U) {
                return false;
            }

            const auto address = reinterpret_cast<std::uintptr_t>(pointer);
            const auto begin = reinterpret_cast<std::uintptr_t>(begin_);
            const auto end = begin + (capacity_ * stride_);
            return address >= begin && address < end && ((address - begin) % stride_) == 0U;
        }

        [[nodiscard]] allocation_result allocate() noexcept {
            if (free_ == nullptr) {
                return unexpected(allocator_error::out_of_memory);
            }

            free_node* node = free_;
            free_ = node->next;
            node->~free_node();
            --free_count_;

            return memory_block{
                .data = reinterpret_cast<std::byte*>(node),
                .size = block_size_,
            };
        }

        [[nodiscard]] allocator_status deallocate(memory_block block) noexcept {
            if (!block || block.size != block_size_) {
                return unexpected(allocator_error::invalid_block);
            }

            if (!owns(block.data)) {
                return unexpected(allocator_error::invalid_pointer);
            }

            auto* node = reinterpret_cast<free_node*>(block.data);
            if (contains_free_node(node)) {
                return unexpected(allocator_error::invalid_pointer);
            }

            node = ::new (block.data) free_node{free_};
            free_ = node;
            ++free_count_;
            return {};
        }

      private:
        struct free_node final {
            free_node* next;
        };

        [[nodiscard]] bool contains_free_node(const free_node* node) const noexcept {
            for (const free_node* current = free_; current != nullptr; current = current->next) {
                if (current == node) {
                    return true;
                }
            }

            return false;
        }

        std::byte* begin_{};
        std::size_t block_size_{};
        std::size_t stride_{};
        std::size_t capacity_{};
        std::size_t free_count_{};
        free_node* free_{};
    };

} // namespace mnemos::foundation
