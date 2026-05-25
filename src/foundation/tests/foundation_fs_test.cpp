#include "fs.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>

TEST_CASE("fs normalizes non empty paths lexically") {
    const auto result = mnemos::foundation::try_normalize_path(std::filesystem::path{"systems"} /
                                                               ".." / "roms" / "." / "kernal.bin");

    REQUIRE(result.has_value());
    CHECK(result->generic_string() == "roms/kernal.bin");
}

TEST_CASE("fs rejects empty paths before normalization") {
    const auto result = mnemos::foundation::try_normalize_path({});

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error() == mnemos::foundation::fs_error::empty_path);
}

TEST_CASE("fs resolves relative children inside a root") {
    const auto result = mnemos::foundation::try_resolve_child_path(
        std::filesystem::path{"library"},
        std::filesystem::path{"systems"} / ".." / "roms" / "c64" / "kernal.bin");

    REQUIRE(result.has_value());
    CHECK(result->generic_string() == "library/roms/c64/kernal.bin");
}

TEST_CASE("fs rejects child paths that escape the root") {
    const auto result = mnemos::foundation::try_resolve_child_path(std::filesystem::path{"library"},
                                                                   std::filesystem::path{"roms"} /
                                                                       ".." / ".." / "outside.bin");

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error() == mnemos::foundation::fs_error::path_escape);
}

TEST_CASE("fs rejects empty root and child paths") {
    const auto empty_root =
        mnemos::foundation::try_resolve_child_path({}, std::filesystem::path{"rom.bin"});
    const auto empty_child =
        mnemos::foundation::try_resolve_child_path(std::filesystem::path{"library"}, {});

    REQUIRE_FALSE(empty_root.has_value());
    CHECK(empty_root.error() == mnemos::foundation::fs_error::empty_root);
    REQUIRE_FALSE(empty_child.has_value());
    CHECK(empty_child.error() == mnemos::foundation::fs_error::empty_path);
}

TEST_CASE("fs rejects rooted child paths") {
    const std::filesystem::path absolute = std::filesystem::current_path();
    const auto result =
        mnemos::foundation::try_resolve_child_path(std::filesystem::path{"library"}, absolute);

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error() == mnemos::foundation::fs_error::rooted_child);
}

TEST_CASE("fs reports empty status paths before querying filesystem") {
    const auto result = mnemos::foundation::try_query_path_status({});

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error() == mnemos::foundation::fs_error::empty_path);
}

TEST_CASE("fs reports regular file status without throwing") {
    const auto result = mnemos::foundation::try_query_path_status(std::filesystem::path{__FILE__});

    REQUIRE(result.has_value());
    CHECK(result->kind == mnemos::foundation::path_kind::regular_file);
}

TEST_CASE("fs reports existing directory status without throwing") {
    const auto result = mnemos::foundation::try_query_path_status(std::filesystem::current_path());

    REQUIRE(result.has_value());
    CHECK(result->kind == mnemos::foundation::path_kind::directory);
}

TEST_CASE("fs reports missing path status without throwing") {
    const auto result = mnemos::foundation::try_query_path_status(
        std::filesystem::path{"mnemos_missing_path_status_test_sentinel"});

    REQUIRE(result.has_value());
    CHECK(result->kind == mnemos::foundation::path_kind::missing);
}
