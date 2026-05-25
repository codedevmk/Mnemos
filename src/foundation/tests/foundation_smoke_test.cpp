#include "version.hpp"

#include <catch2/catch_test_macros.hpp>
#include <string_view>

TEST_CASE("foundation version reports the scaffold identity") {
    CHECK(mnemos::foundation::version::project_name == std::string_view{"Mnemos"});
    CHECK(mnemos::foundation::version::major == 0U);
    CHECK(mnemos::foundation::version::minor == 1U);
    CHECK(mnemos::foundation::version::patch == 0U);
}
