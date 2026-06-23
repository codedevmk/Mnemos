#pragma once

#include "nes_mapper_factory.hpp"

#include <memory>

namespace mnemos::manifests::nes {

    [[nodiscard]] std::unique_ptr<nes_mapper> make_mmc3_mapper(nes_mapper_build_context context);
    [[nodiscard]] std::unique_ptr<nes_mapper> make_rambo1_mapper(nes_mapper_build_context context);
    [[nodiscard]] std::unique_ptr<nes_mapper>
    make_namco118_mapper(nes_mapper_build_context context);

} // namespace mnemos::manifests::nes
