#include "rom_loader.hpp"

#include "zip_archive.hpp"

#include <fstream>
#include <iterator>

namespace mnemos::apps::player::adapters {

    std::optional<std::vector<std::uint8_t>> read_file(const std::string& path) {
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            return std::nullopt;
        }
        return std::vector<std::uint8_t>{std::istreambuf_iterator<char>(in),
                                         std::istreambuf_iterator<char>()};
    }

    std::optional<loaded_rom> load_rom(const std::string& path) {
        using mnemos::compression::zip_archive;
        using mnemos::compression::zip_entry;
        using mnemos::compression::zip_method;

        auto bytes = read_file(path);
        if (!bytes) {
            return std::nullopt;
        }
        // ZIP local-file-header magic "PK\3\4". A bare console ROM never starts
        // with it, so the signature reliably distinguishes archive from image.
        const bool is_zip = bytes->size() >= 4U && (*bytes)[0] == 'P' && (*bytes)[1] == 'K' &&
                            (*bytes)[2] == 0x03U && (*bytes)[3] == 0x04U;
        if (!is_zip) {
            return loaded_rom{.bytes = std::move(*bytes), .name = path};
        }

        const auto archive = zip_archive::open(*bytes);
        if (!archive) {
            return std::nullopt;
        }
        const zip_entry* best = nullptr;
        for (const zip_entry& e : archive->entries()) {
            const bool is_dir = !e.name.empty() && e.name.back() == '/';
            if (e.method == zip_method::unsupported || e.uncompressed_size == 0U || is_dir) {
                continue;
            }
            if (best == nullptr || e.uncompressed_size > best->uncompressed_size) {
                best = &e;
            }
        }
        if (best == nullptr) {
            return std::nullopt;
        }
        auto extracted = archive->extract(*best);
        if (!extracted) {
            return std::nullopt;
        }
        return loaded_rom{.bytes = std::move(*extracted), .name = best->name};
    }

    std::string clean_rom_name(const std::string& path) {
        const auto slash = path.find_last_of("/\\");
        const auto begin = slash == std::string::npos ? 0U : slash + 1U;
        const auto dot = path.find_last_of('.');
        const auto end = (dot == std::string::npos || dot < begin) ? path.size() : dot;
        return path.substr(begin, end - begin);
    }

} // namespace mnemos::apps::player::adapters
