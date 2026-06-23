#pragma once

#include "nes_mapper_factory.hpp"

#include <memory>

namespace mnemos::manifests::nes {

    [[nodiscard]] std::unique_ptr<nes_mapper>
    make_bandai_fcg_mapper(nes_mapper_build_context context);

} // namespace mnemos::manifests::nes
