#pragma once

#include "expected_ext.hpp"

#include <cstdint>
#include <filesystem>
#include <system_error>

namespace mnemos::foundation {

    using fs_path = std::filesystem::path;

    enum class fs_error : std::uint8_t {
        empty_path,
        empty_root,
        rooted_child,
        path_escape,
        status_failed,
    };

    enum class path_kind : std::uint8_t {
        missing,
        regular_file,
        directory,
        symlink,
        other,
    };

    struct path_status final {
        path_kind kind;
    };

    using path_result = expected<fs_path, fs_error>;
    using path_status_result = expected<path_status, fs_error>;

    [[nodiscard]] inline bool path_has_parent_reference(const fs_path& path) {
        for (const fs_path& part : path) {
            if (part == "..") {
                return true;
            }
        }

        return false;
    }

    [[nodiscard]] inline path_result try_normalize_path(const fs_path& path) {
        if (path.empty()) {
            return unexpected(fs_error::empty_path);
        }

        return path.lexically_normal();
    }

    // Containment is purely lexical: it rejects rooted children and `..` escapes, but does not
    // resolve symlinks. Do not rely on it as a security sandbox boundary against symlink escapes.
    [[nodiscard]] inline path_result try_resolve_child_path(const fs_path& root,
                                                            const fs_path& child) {
        if (root.empty()) {
            return unexpected(fs_error::empty_root);
        }

        if (child.empty()) {
            return unexpected(fs_error::empty_path);
        }

        if (child.is_absolute() || child.has_root_name() || child.has_root_directory()) {
            return unexpected(fs_error::rooted_child);
        }

        const fs_path normalized_child = child.lexically_normal();
        if (path_has_parent_reference(normalized_child)) {
            return unexpected(fs_error::path_escape);
        }

        return (root.lexically_normal() / normalized_child).lexically_normal();
    }

    [[nodiscard]] inline path_status_result try_query_path_status(const fs_path& path) {
        if (path.empty()) {
            return unexpected(fs_error::empty_path);
        }

        std::error_code error;
        const std::filesystem::file_status status = std::filesystem::symlink_status(path, error);
        if (status.type() == std::filesystem::file_type::not_found) {
            return path_status{path_kind::missing};
        }

        if (error) {
            return unexpected(fs_error::status_failed);
        }

        if (std::filesystem::is_regular_file(status)) {
            return path_status{path_kind::regular_file};
        }

        if (std::filesystem::is_directory(status)) {
            return path_status{path_kind::directory};
        }

        if (std::filesystem::is_symlink(status)) {
            return path_status{path_kind::symlink};
        }

        return path_status{path_kind::other};
    }

} // namespace mnemos::foundation
