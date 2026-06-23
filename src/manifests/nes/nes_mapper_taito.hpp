#pragma once

#include "nes_mapper_factory.hpp"

#include <memory>

namespace mnemos::manifests::nes {

    [[nodiscard]] std::unique_ptr<nes_mapper>
    make_taito_tc0190_mapper(nes_mapper_build_context context);
    [[nodiscard]] std::unique_ptr<nes_mapper>
    make_taito_tc0690_mapper(nes_mapper_build_context context);

} // namespace mnemos::manifests::nes
