#pragma once

// System-agnostic audio sample extraction over an assembled player_system. The
// audio analogue of asset_export: walks `sys.chips()` and, for every chip that
// exposes an `audio_source` (introspection().audio()), writes its PCM samples as
// WAV files plus a JSON manifest. Knows nothing about which system is loaded -- a
// new system gains audio export by implementing audio_source on a chip.

#include "player_system.hpp"

#include <cstddef>
#include <string>

namespace mnemos::debug {

    // For each chip in `sys.chips()` exposing an audio_source and/or a register
    // file (register_view):
    //   each sample -> <base>.<chip>.sample.<name>.wav (16-bit PCM RIFF/WAVE)
    // and writes one <base>.audio.json manifest listing every such chip's samples
    // (name, sample_rate, channels, frame_count, loop_start, source_addr, file)
    // and its register file (name, value, bits) -- a synth's voice/instrument
    // state is its register file, so it reuses register_view rather than a
    // bespoke type. <chip> is the chip's part_number sanitized to a path segment.
    //
    // Returns the number of WAV files successfully written. A file that fails to
    // write is reported on stderr and skipped (omitted from the count but still
    // listed in the manifest). The manifest is always written, even when no chip
    // exposes samples (an empty chip list).
    std::size_t export_audio(const frontend_sdk::player_system& sys, const std::string& base_path);

} // namespace mnemos::debug
