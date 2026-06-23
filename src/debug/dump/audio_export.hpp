#pragma once

// System-agnostic audio sample extraction over an assembled player_system. The
// audio analogue of asset_export: walks `sys.chips()` and, for every chip that
// exposes an `audio_source` (introspection().audio()), writes its PCM samples as
// WAV files plus a JSON manifest. Knows nothing about which system is loaded -- a
// new system gains audio export by implementing audio_source on a chip.

#include "player_system.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace mnemos::debug {

    // For each chip that exposes an audio_source, or is an audio chip
    // (chip_class::audio_synth) exposing a register file (register_view) -- a
    // non-audio chip's registers belong in a CPU/debug dump, not here:
    //   each sample -> <base>.<chip>.sample.<name>.wav (16-bit PCM RIFF/WAVE)
    // and writes one <base>.audio.json manifest listing every such chip's samples
    // (name, sample_rate, channels, frame_count, loop_start, source_addr, file)
    // and its register file (name, value, bits) -- a synth's voice/instrument
    // state is its register file, so it reuses register_view rather than a
    // bespoke type. Dynamic path segments are sanitized; manifest display fields
    // keep their original introspection names.
    //
    // Returns the number of WAV files successfully written. A file that fails to
    // write is reported on stderr and skipped (omitted from the count but still
    // listed in the manifest). The manifest is always written, even when no chip
    // exposes samples (an empty chip list).
    std::size_t export_audio(const frontend_sdk::player_system& sys, const std::string& base_path);

    // Records what the machine actually PLAYS (the mixed stereo output) rather than
    // a sample chip's stored PCM: steps `sys` for `frames` frames, draining
    // drain_audio() each frame, and writes the concatenated stream to
    // <base_path>.rendered.wav (16-bit stereo RIFF/WAVE). `before_frame(i)`, if
    // set, runs before frame i so the caller can drive input. Returns the number of
    // (L,R) frames captured. This is the only audio export that works for pure
    // synth chips (the NES APU, SN76489, ...) which expose no stored samples.
    std::size_t export_rendered_audio(frontend_sdk::player_system& sys, std::uint64_t frames,
                                      const std::string& base_path,
                                      const std::function<void(std::uint64_t)>& before_frame = {});

} // namespace mnemos::debug
