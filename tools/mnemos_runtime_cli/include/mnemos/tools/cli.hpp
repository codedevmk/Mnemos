#pragma once

#include <mnemos/chips/common/chip.hpp>

#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <string>

namespace mnemos::tools {

    // Parsed command line for the headless runtime CLI.
    struct cli_options final {
        std::filesystem::path manifest;
        std::filesystem::path rom_dir;
        std::filesystem::path disk;      // optional .d64 mounted on drive 8
        std::filesystem::path drive_rom; // optional 16K 1541 DOS ROM -> cycle-accurate drive
        std::filesystem::path cart;      // optional .crt cartridge
        std::uint64_t frames{1U};
        bool dump_hash{false};
        bool help{false};
        // Deferred (save-state work): present so the flags are accepted and clearly
        // reported as unimplemented rather than silently ignored.
        std::filesystem::path save;
        std::filesystem::path load;
        std::filesystem::path input_log;
    };

    // Parse argv into options. Returns true on success; on failure writes a message
    // to `err` and returns false.
    [[nodiscard]] bool parse_args(int argc, const char* const* argv, cli_options& out,
                                  std::ostream& err);

    // Deterministic SHA-256 (lowercase hex) of a framebuffer, serialised as R,G,B,A
    // bytes per pixel so the hash is identical across platforms and endiannesses.
    [[nodiscard]] std::string hash_framebuffer(const chips::frame_buffer_view& fb);

    // Usage text.
    void print_usage(std::ostream& out);

    // Load the manifest + ROMs, assemble the system, run `frames` frames, and
    // optionally print the framebuffer hash. Returns a process exit code (0 = ok).
    [[nodiscard]] int run(const cli_options& options, std::ostream& out, std::ostream& err);

    // Parse argv then run; the entry point used by main().
    [[nodiscard]] int main_cli(int argc, const char* const* argv, std::ostream& out,
                               std::ostream& err);

} // namespace mnemos::tools
