#pragma once

#include "nes_mapper_factory.hpp"

#include <memory>

namespace mnemos::manifests::nes {

    [[nodiscard]] std::unique_ptr<nes_mapper>
    make_sunsoft3_mapper(nes_mapper_build_context context);
    [[nodiscard]] std::unique_ptr<nes_mapper>
    make_sunsoft4_mapper(nes_mapper_build_context context);
    [[nodiscard]] std::unique_ptr<nes_mapper>
    make_sunsoft5b_mapper(nes_mapper_build_context context);

} // namespace mnemos::manifests::nes
