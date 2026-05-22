#include <mnemos/foundation/allocator.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

static_assert(!std::is_copy_constructible_v<mnemos::foundation::linear_arena>);
static_assert(!std::is_copy_assignable_v<mnemos::foundation::linear_arena>);
static_assert(!std::is_copy_constructible_v<mnemos::foundation::fixed_block_pool>);
static_assert(!std::is_copy_assignable_v<mnemos::foundation::fixed_block_pool>);

namespace {

    [[nodiscard]] bool is_aligned(const void* pointer, std::size_t alignment) noexcept {
        return (reinterpret_cast<std::uintptr_t>(pointer) % alignment) == 0U;
    }

} // namespace

TEST_CASE("allocator alignment validation accepts powers of two only") {
    CHECK_FALSE(mnemos::foundation::is_valid_alignment(0U));
    CHECK(mnemos::foundation::is_valid_alignment(1U));
    CHECK(mnemos::foundation::is_valid_alignment(8U));
    CHECK_FALSE(mnemos::foundation::is_valid_alignment(12U));
}

TEST_CASE("memory block exposes valid non empty byte ranges") {
    std::array<std::byte, 4U> storage{};
    const mnemos::foundation::memory_block valid{.data = storage.data(), .size = storage.size()};
    const mnemos::foundation::memory_block no_data{.data = nullptr, .size = storage.size()};
    const mnemos::foundation::memory_block no_size{.data = storage.data(), .size = 0U};

    CHECK(static_cast<bool>(valid));
    CHECK(valid.bytes().size() == storage.size());
    CHECK_FALSE(static_cast<bool>(no_data));
    CHECK_FALSE(static_cast<bool>(no_size));
}

TEST_CASE("linear arena allocates aligned blocks from caller storage") {
    alignas(64) std::array<std::byte, 128U> storage{};
    std::array<std::byte, 4U> external{};
    mnemos::foundation::linear_arena arena{storage};

    const auto first = arena.allocate(3U, 16U);
    const auto second = arena.allocate(8U, 32U);

    REQUIRE(first.has_value());
    REQUIRE(second.has_value());
    CHECK(is_aligned(first->data, 16U));
    CHECK(is_aligned(second->data, 32U));
    CHECK(first->size == 3U);
    CHECK(second->size == 8U);
    CHECK(arena.owns(first->data));
    CHECK(arena.owns(second->data));
    CHECK_FALSE(arena.owns(nullptr));
    CHECK_FALSE(arena.owns(external.data()));
    CHECK_FALSE(arena.owns(storage.data() + storage.size()));
    CHECK(arena.used() <= arena.capacity());
    CHECK(arena.remaining() == arena.capacity() - arena.used());
    CHECK(arena.peak_used() == arena.used());
}

TEST_CASE("linear arena rejects invalid requests without consuming storage") {
    std::array<std::byte, 16U> storage{};
    mnemos::foundation::linear_arena arena{storage};
    mnemos::foundation::linear_arena empty_arena{{}};
    mnemos::foundation::linear_arena zero_capacity_arena{std::span<std::byte>{storage.data(), 0U}};

    const auto empty = empty_arena.allocate(1U);
    const auto zero_capacity = zero_capacity_arena.allocate(1U);
    const auto zero = arena.allocate(0U);
    const auto bad_alignment = arena.allocate(1U, 3U);
    const auto too_large = arena.allocate(32U);

    CHECK_FALSE(empty_arena.owns(storage.data()));
    REQUIRE_FALSE(empty.has_value());
    CHECK(empty.error() == mnemos::foundation::allocator_error::empty_storage);
    REQUIRE_FALSE(zero_capacity.has_value());
    CHECK(zero_capacity.error() == mnemos::foundation::allocator_error::empty_storage);
    REQUIRE_FALSE(zero.has_value());
    CHECK(zero.error() == mnemos::foundation::allocator_error::invalid_size);
    REQUIRE_FALSE(bad_alignment.has_value());
    CHECK(bad_alignment.error() == mnemos::foundation::allocator_error::invalid_alignment);
    REQUIRE_FALSE(too_large.has_value());
    CHECK(too_large.error() == mnemos::foundation::allocator_error::out_of_memory);
    CHECK(arena.used() == 0U);
}

TEST_CASE("linear arena rejects alignment that rounds the cursor past the end") {
    // Regression: when alignment rounds the cursor beyond `end`, the remaining-space
    // check must not underflow and hand back out-of-bounds storage. With a 16-aligned
    // base, filling 17 bytes then requesting alignment 16 rounds to begin+32 > begin+20.
    alignas(16) std::array<std::byte, 20U> storage{};
    mnemos::foundation::linear_arena arena{storage};

    const auto filler = arena.allocate(17U, 1U);
    REQUIRE(filler.has_value());

    const auto rounds_past_end = arena.allocate(4U, 16U);

    REQUIRE_FALSE(rounds_past_end.has_value());
    CHECK(rounds_past_end.error() == mnemos::foundation::allocator_error::out_of_memory);
    CHECK(arena.used() <= arena.capacity());
    CHECK(arena.remaining() <= arena.capacity());
}

TEST_CASE("linear arena reset reuses storage and preserves peak") {
    std::array<std::byte, 32U> storage{};
    mnemos::foundation::linear_arena arena{storage};

    const auto first = arena.allocate(16U, 8U);
    REQUIRE(first.has_value());
    CHECK(arena.used() > 0U);

    const std::size_t peak = arena.peak_used();
    arena.reset();

    CHECK(arena.used() == 0U);
    CHECK(arena.peak_used() == peak);

    const auto second = arena.allocate(16U, 8U);
    REQUIRE(second.has_value());
    CHECK(second->data == first->data);
}

TEST_CASE("fixed block pool creates aligned blocks over caller storage") {
    alignas(64) std::array<std::byte, 128U> storage{};
    auto pool_result = mnemos::foundation::fixed_block_pool::create(storage, 16U, 16U);

    REQUIRE(pool_result.has_value());
    auto& pool = *pool_result;

    CHECK(pool.block_size() == 16U);
    CHECK(pool.capacity() == 8U);
    CHECK(pool.available() == 8U);

    const auto block = pool.allocate();
    REQUIRE(block.has_value());
    CHECK(block->size == 16U);
    CHECK(is_aligned(block->data, 16U));
    CHECK(pool.owns(block->data));
    CHECK_FALSE(pool.owns(nullptr));
    CHECK_FALSE(pool.owns(block->data + 1U));
    CHECK_FALSE(pool.owns(storage.data() + storage.size()));
    CHECK(pool.used() == 1U);
}

TEST_CASE("default fixed block pool owns no storage") {
    std::array<std::byte, 8U> storage{};
    mnemos::foundation::fixed_block_pool pool;

    CHECK_FALSE(pool.owns(storage.data()));
}

TEST_CASE("fixed block pool supports block sizes smaller than free list links") {
    alignas(16) std::array<std::byte, 64U> storage{};
    auto pool_result = mnemos::foundation::fixed_block_pool::create(storage, 1U, 8U);

    REQUIRE(pool_result.has_value());
    auto& pool = *pool_result;

    CHECK(pool.block_size() == 1U);
    CHECK(pool.capacity() == 8U);

    const auto block = pool.allocate();
    REQUIRE(block.has_value());
    CHECK(block->size == 1U);
    CHECK(pool.owns(block->data));
}

TEST_CASE("fixed block pool recycles blocks and rejects double free") {
    alignas(32) std::array<std::byte, 64U> storage{};
    auto pool_result = mnemos::foundation::fixed_block_pool::create(storage, 16U, 16U);
    REQUIRE(pool_result.has_value());
    auto& pool = *pool_result;

    const auto block = pool.allocate();
    REQUIRE(block.has_value());

    auto freed = pool.deallocate(*block);
    REQUIRE(freed.has_value());
    CHECK(pool.available() == pool.capacity());

    auto double_free = pool.deallocate(*block);
    REQUIRE_FALSE(double_free.has_value());
    CHECK(double_free.error() == mnemos::foundation::allocator_error::invalid_pointer);

    const auto reused = pool.allocate();
    REQUIRE(reused.has_value());
    CHECK(reused->data == block->data);
}

TEST_CASE("fixed block pool rejects invalid deallocation requests") {
    alignas(32) std::array<std::byte, 64U> storage{};
    std::array<std::byte, 16U> external{};
    auto pool_result = mnemos::foundation::fixed_block_pool::create(storage, 16U, 16U);
    REQUIRE(pool_result.has_value());
    auto& pool = *pool_result;

    const mnemos::foundation::memory_block empty{};
    const mnemos::foundation::memory_block wrong_size{.data = storage.data(), .size = 8U};
    const mnemos::foundation::memory_block external_block{.data = external.data(), .size = 16U};

    auto empty_result = pool.deallocate(empty);
    auto wrong_size_result = pool.deallocate(wrong_size);
    auto external_result = pool.deallocate(external_block);

    REQUIRE_FALSE(empty_result.has_value());
    CHECK(empty_result.error() == mnemos::foundation::allocator_error::invalid_block);
    REQUIRE_FALSE(wrong_size_result.has_value());
    CHECK(wrong_size_result.error() == mnemos::foundation::allocator_error::invalid_block);
    REQUIRE_FALSE(external_result.has_value());
    CHECK(external_result.error() == mnemos::foundation::allocator_error::invalid_pointer);
}

TEST_CASE("fixed block pool reports exhaustion") {
    alignas(16) std::array<std::byte, 32U> storage{};
    auto pool_result = mnemos::foundation::fixed_block_pool::create(storage, 16U, 16U);
    REQUIRE(pool_result.has_value());
    auto& pool = *pool_result;

    const auto first = pool.allocate();
    const auto second = pool.allocate();
    const auto third = pool.allocate();

    REQUIRE(first.has_value());
    REQUIRE(second.has_value());
    REQUIRE_FALSE(third.has_value());
    CHECK(third.error() == mnemos::foundation::allocator_error::out_of_memory);
}

TEST_CASE("fixed block pool rejects invalid construction requests") {
    std::array<std::byte, 8U> storage{};

    const auto empty = mnemos::foundation::fixed_block_pool::create({}, 16U);
    const auto zero_size = mnemos::foundation::fixed_block_pool::create(storage, 0U);
    const auto bad_alignment = mnemos::foundation::fixed_block_pool::create(storage, 4U, 3U);
    const auto too_small = mnemos::foundation::fixed_block_pool::create(storage, 16U, 16U);

    REQUIRE_FALSE(empty.has_value());
    CHECK(empty.error() == mnemos::foundation::allocator_error::empty_storage);
    REQUIRE_FALSE(zero_size.has_value());
    CHECK(zero_size.error() == mnemos::foundation::allocator_error::invalid_size);
    REQUIRE_FALSE(bad_alignment.has_value());
    CHECK(bad_alignment.error() == mnemos::foundation::allocator_error::invalid_alignment);
    REQUIRE_FALSE(too_small.has_value());
    CHECK(too_small.error() == mnemos::foundation::allocator_error::out_of_memory);
}
