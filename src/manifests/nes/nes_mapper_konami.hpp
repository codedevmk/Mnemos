#pragma once

#include "nes_mapper_factory.hpp"

#include <memory>

namespace mnemos::manifests::nes {

    [[nodiscard]] std::unique_ptr<nes_mapper> make_vrc1_mapper(nes_mapper_build_context context);
    [[nodiscard]] std::unique_ptr<nes_mapper> make_vrc2_4_mapper(nes_mapper_build_context context,
                                                                 int mapper_number);
    [[nodiscard]] std::unique_ptr<nes_mapper> make_vrc3_mapper(nes_mapper_build_context context);
    [[nodiscard]] std::unique_ptr<nes_mapper> make_vrc6_mapper(nes_mapper_build_context context,
                                                               bool variant_b);
    [[nodiscard]] std::unique_ptr<nes_mapper> make_vrc7_mapper(nes_mapper_build_context context);

} // namespace mnemos::manifests::nes
