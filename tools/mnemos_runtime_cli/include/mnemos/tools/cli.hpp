#pragma once

#include <mnemos/chips/common/chip.hpp>
#include <mnemos/runtime/input.hpp>

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
        std::filesystem::path tape;      // optional .tap datasette image
        std::uint64_t frames{1U};
        std::uint32_t reu_kib{0U}; // RAM Expansion Unit size in KiB (0 = none; 128/256/512)
        bool modem{false};         // attach the RS-232 userport modem
        std::string dial;          // host[:port] to dial over TCP (implies --modem)
        bool dump_hash{false};
        bool sid_8580{false}; // use the 8580 SID instead of the 6581
        bool dual_sid{false}; // add a second SID at $D420
        bool help{false};
        // Deferred (save-state work): present so the flags are accepted and clearly
        // reported as unimplemented rather than silently ignored.
        std::filesystem::path save;
        std::filesystem::path load;
        std::filesystem::path input_log; // frame-tagged input script replayed during the run
    };

    // Device codes used in the replayed runtime::input_event stream (the input log
    // is C64-specific): keyboard transitions carry a key code, joysticks carry an
    // absolute direction mask, paddles carry one axis each.
    namespace input_device {
        inline constexpr std::uint8_t keyboard = 0U;    // code = c64_input::key, pressed = state
        inline constexpr std::uint8_t joystick1 = 1U;   // code = joy_* mask (absolute)
        inline constexpr std::uint8_t joystick2 = 2U;   // code = joy_* mask (absolute)
        inline constexpr std::uint8_t paddle1_x = 3U;   // code = POT X (0-255)
        inline constexpr std::uint8_t paddle1_y = 4U;   // code = POT Y (0-255)
        inline constexpr std::uint8_t paddle2_x = 5U;   // code = POT X (0-255)
        inline constexpr std::uint8_t paddle2_y = 6U;   // code = POT Y (0-255)
        inline constexpr std::uint8_t release_all = 7U; // release every held key
    } // namespace input_device

    // Parse a text input-log script into a frame-tagged buffer. Each non-blank,
    // non-comment line is "<frame> <command> [args]" (see the CLI usage / the
    // tests for the grammar). Returns true on success; on a malformed line writes
    // a diagnostic to `err` and returns false.
    [[nodiscard]] bool parse_input_log(std::istream& in, runtime::input_buffer& out,
                                       std::ostream& err);

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
