#pragma once

#include "nes_mapper_factory.hpp"

#include <memory>

namespace mnemos::manifests::nes {

    [[nodiscard]] std::unique_ptr<nes_mapper> make_nrom_mapper(nes_mapper_build_context context);
    [[nodiscard]] std::unique_ptr<nes_mapper> make_uxrom_mapper(nes_mapper_build_context context);
    [[nodiscard]] std::unique_ptr<nes_mapper> make_cnrom_mapper(nes_mapper_build_context context);
    [[nodiscard]] std::unique_ptr<nes_mapper> make_axrom_mapper(nes_mapper_build_context context);
    [[nodiscard]] std::unique_ptr<nes_mapper> make_gxrom_mapper(nes_mapper_build_context context);
    [[nodiscard]] std::unique_ptr<nes_mapper>
    make_color_dreams_mapper(nes_mapper_build_context context);
    [[nodiscard]] std::unique_ptr<nes_mapper>
    make_camerica_mapper(nes_mapper_build_context context);
    [[nodiscard]] std::unique_ptr<nes_mapper>
    make_bnrom_nina_mapper(nes_mapper_build_context context);

} // namespace mnemos::manifests::nes
