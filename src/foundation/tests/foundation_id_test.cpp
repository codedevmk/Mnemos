#include <mnemos/foundation/id.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <type_traits>

using namespace mnemos::foundation::literals;

static_assert(std::is_trivially_copyable_v<mnemos::foundation::string_id>);
static_assert(sizeof(mnemos::foundation::string_id) == sizeof(mnemos::foundation::id_hash));
static_assert(mnemos::foundation::fnv1a_64("") == mnemos::foundation::fnv1a_64_offset_basis);
static_assert(mnemos::foundation::fnv1a_64_prime == 1'099'511'628'211ULL);
static_assert(mnemos::foundation::string_id{"mos.6510"} == "mos.6510"_mnemos_id);
static_assert(mnemos::foundation::chip_id{"mos.6510"} == "mos.6510"_chip_id);

TEST_CASE("fnv1a hash is stable for known values") {
    CHECK(mnemos::foundation::fnv1a_64("") == 0xCBF2'9CE4'8422'2325ULL);
    CHECK(mnemos::foundation::fnv1a_64("hello") == 0xA430'D846'80AA'BD0BULL);
}

TEST_CASE("string ids compare by hash value") {
    const mnemos::foundation::string_id cpu{"mos.6510"};
    const mnemos::foundation::string_id same_cpu{"mos.6510"};
    const mnemos::foundation::string_id video{"mos.6569"};

    CHECK(cpu == same_cpu);
    CHECK(cpu != video);
    CHECK_FALSE(cpu.is_empty());
    CHECK(cpu.value() == mnemos::foundation::fnv1a_64("mos.6510"));
}

TEST_CASE("chip id literal matches runtime hashing") {
    constexpr auto cpu = "mos.6510"_chip_id;
    constexpr auto audio = "yamaha.ym2612"_chip_id;

    CHECK(cpu == mnemos::foundation::chip_id{"mos.6510"});
    CHECK(audio == mnemos::foundation::chip_id{"yamaha.ym2612"});
    CHECK(cpu != audio);
}

TEST_CASE("empty string id is reserved for default construction") {
    constexpr mnemos::foundation::string_id empty;
    constexpr mnemos::foundation::string_id empty_text{""};

    CHECK(empty.is_empty());
    CHECK_FALSE(empty_text.is_empty());
    CHECK(empty.value() == 0U);
    CHECK(empty_text.value() == mnemos::foundation::fnv1a_64_offset_basis);
}
