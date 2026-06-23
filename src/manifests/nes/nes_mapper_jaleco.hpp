#pragma once

#include "nes_mapper_factory.hpp"

#include <memory>

namespace mnemos::manifests::nes {

    [[nodiscard]] std::unique_ptr<nes_mapper>
    make_jaleco_ss88006_mapper(nes_mapper_build_context context);

} // namespace mnemos::manifests::nes
