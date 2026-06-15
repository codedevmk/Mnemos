#include "rom_set.hpp"

#include "crc32.hpp"
#include "file.hpp"
#include "zip_archive.hpp"

#include <memory>
#include <utility>

namespace mnemos::manifests::common {

    namespace {
        [[nodiscard]] std::string hex32(std::uint32_t value) {
            static constexpr char digits[] = "0123456789abcdef";
            std::string out = "0x";
            for (int shift = 28; shift >= 0; shift -= 4) {
                out.push_back(digits[(value >> static_cast<unsigned>(shift)) & 0xFU]);
            }
            return out;
        }
    } // namespace

    rom_set_image load_rom_set(const rom_set_decl& decl, const rom_file_provider& provider) {
        rom_set_image image;

        for (const rom_set_region& region : decl.regions) {
            if (region.size == 0U) {
                image.issues.push_back({region.name, "region declares size 0"});
                continue;
            }
            std::vector<std::uint8_t> bytes(region.size, region.fill);

            for (const rom_set_file& file : region.files) {
                const auto data = provider ? provider(file.name) : std::nullopt;
                if (!data.has_value()) {
                    image.issues.push_back({file.name, "missing from the ROM set"});
                    continue;
                }
                if (file.size != 0U && data->size() != file.size) {
                    image.issues.push_back(
                        {file.name, "size mismatch: got " + std::to_string(data->size()) +
                                        ", expected " + std::to_string(file.size)});
                }
                if (file.crc32.has_value()) {
                    const std::uint32_t actual = security::cryptography::crc32(*data);
                    if (actual != *file.crc32) {
                        image.issues.push_back({file.name, "crc32 mismatch: got " + hex32(actual) +
                                                               ", expected " + hex32(*file.crc32)});
                    }
                }

                // Place what fits even when verification flagged the file --
                // issues report the problem, the data still loads. The source
                // span (`length` bytes from `source_offset`, default = the rest
                // of the file) is laid down in `unit`-byte chunks, each advancing
                // the destination by `stride`; `swap` reverses the bytes within a
                // chunk. unit 1 / stride 1 reproduces a plain contiguous copy.
                const std::size_t stride = file.stride != 0U ? file.stride : 1U;
                const std::size_t unit = file.unit != 0U ? file.unit : 1U;
                const std::size_t src_start = file.source_offset;
                const std::size_t available =
                    data->size() > src_start ? data->size() - src_start : 0U;
                std::size_t copy_len = file.length != 0U ? file.length : available;
                // A source offset at/past the file end places nothing -- surface
                // it rather than silently dropping the file (a default `length`
                // would otherwise reduce the copy to zero with no diagnostic).
                if (!data->empty() && src_start >= data->size()) {
                    image.issues.push_back(
                        {file.name, "source offset " + std::to_string(src_start) +
                                        " is at or past the end of the file (size " +
                                        std::to_string(data->size()) + ")"});
                    copy_len = 0U;
                } else if (copy_len > available) {
                    image.issues.push_back(
                        {file.name, "source range exceeds file: wanted " +
                                        std::to_string(file.length) + " bytes from offset " +
                                        std::to_string(src_start) + ", file holds " +
                                        std::to_string(data->size())});
                    copy_len = available;
                }
                if (copy_len % unit != 0U) {
                    image.issues.push_back({file.name, "copy length " + std::to_string(copy_len) +
                                                           " is not a multiple of unit " +
                                                           std::to_string(unit)});
                }
                // Each unit occupies the contiguous destination span [base, base +
                // unit); validate the whole span up-front so a unit straddling the
                // region end is rejected atomically (swap permutes within the span,
                // so a per-byte check could leave an in-range byte unwritten).
                const std::size_t chunks = copy_len / unit;
                for (std::size_t c = 0; c < chunks; ++c) {
                    const std::size_t base = file.offset + c * stride;
                    if (base + (unit - 1U) >= region.size) {
                        image.issues.push_back(
                            {file.name, "overflows region '" + region.name + "' at byte " +
                                            std::to_string(base + (unit - 1U)) + " (region size " +
                                            std::to_string(region.size) + ")"});
                        break;
                    }
                    for (std::size_t b = 0; b < unit; ++b) {
                        bytes[base + (file.swap ? unit - 1U - b : b)] =
                            (*data)[src_start + c * unit + b];
                    }
                }
            }

            image.regions.emplace(region.name, std::move(bytes));
        }

        return image;
    }

    rom_file_provider make_directory_rom_provider(std::string directory) {
        return [dir = std::move(directory)](
                   std::string_view name) -> std::optional<std::vector<std::uint8_t>> {
            return io::read_file(dir + "/" + std::string(name));
        };
    }

    std::optional<rom_file_provider> make_zip_rom_provider(std::vector<std::uint8_t> archive) {
        // The zip_archive borrows the buffer it was opened over, so both move
        // into shared ownership inside the provider (a vector move keeps the
        // heap buffer, and with it the archive's internal spans, valid).
        auto bytes = std::make_shared<std::vector<std::uint8_t>>(std::move(archive));
        auto opened = compression::zip_archive::open(*bytes);
        if (!opened.has_value()) {
            return std::nullopt;
        }
        auto zip = std::make_shared<compression::zip_archive>(std::move(*opened));
        return rom_file_provider{
            [bytes, zip](std::string_view name) -> std::optional<std::vector<std::uint8_t>> {
                for (const compression::zip_entry& entry : zip->entries()) {
                    if (entry.name == name) {
                        return zip->extract(entry);
                    }
                }
                return std::nullopt;
            }};
    }

} // namespace mnemos::manifests::common
