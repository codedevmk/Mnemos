#include "image.hpp"

#include "file.hpp"

namespace mnemos::graphics::images {

    bool image::write(const std::string& path) const {
        return mnemos::io::write_file(path, encode());
    }

} // namespace mnemos::graphics::images
