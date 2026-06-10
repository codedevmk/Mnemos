#pragma once

// Capability sub-interfaces that a chip's `ichip_introspection` may expose so
// generic debug / observation code (the player frontend, debuggers, profilers,
// dump tools) can interact with chip state WITHOUT downcasting to concrete
// chip types. Tier 2: declared here so tier-2 chips can inherit and implement
// these capabilities; consumed by tier 6+ (instrumentation, frontend).
//
// Every capability is OPTIONAL. A chip that doesn't expose `trace_target`
// returns nullptr from `ichip_introspection::trace()`; the consumer treats
// the chip as untraceable. This keeps `ichip` lightweight while letting any
// chip surface as much as it can.

#include <cstdint>
#include <functional>
#include <span>
#include <string_view>

namespace mnemos::chips {
    struct register_descriptor; // chip.hpp
    struct frame_buffer_view;   // chip.hpp
} // namespace mnemos::chips

namespace mnemos::instrumentation {

    class asset_source; // asset_views.hpp -- decoded graphics-extraction surface
    class audio_source; // audio_views.hpp -- PCM sample-extraction surface

    // A dense byte-addressable region of a chip's state -- VRAM, work RAM, a
    // register file viewed as bytes, etc. Stable name + live bytes for the
    // duration of the chip. Callers must not retain the span across the chip's
    // next tick (the underlying storage is owned by the chip).
    class memory_view {
      public:
        memory_view() = default;
        memory_view(const memory_view&) = delete;
        memory_view& operator=(const memory_view&) = delete;
        virtual ~memory_view() = default;

        [[nodiscard]] virtual std::string_view name() const noexcept = 0;
        [[nodiscard]] virtual std::span<const std::uint8_t> bytes() const noexcept = 0;
    };

    // The common memory_view: a fixed name over a borrowed byte span (a chip's
    // VRAM, a system's work RAM, a register file viewed as bytes). The span must
    // outlive the view; the chip/adapter that owns the storage owns the view.
    class span_memory_view final : public memory_view {
      public:
        span_memory_view(std::string_view name, std::span<const std::uint8_t> bytes) noexcept
            : name_(name), bytes_(bytes) {}
        [[nodiscard]] std::string_view name() const noexcept override { return name_; }
        [[nodiscard]] std::span<const std::uint8_t> bytes() const noexcept override {
            return bytes_;
        }

      private:
        std::string_view name_;
        std::span<const std::uint8_t> bytes_;
    };

    // The chip's architectural register file as a list of named descriptors,
    // populated at the moment of the call. Same lifetime rule as `memory_view`:
    // don't retain the span across a tick.
    class register_view {
      public:
        register_view() = default;
        register_view(const register_view&) = delete;
        register_view& operator=(const register_view&) = delete;
        virtual ~register_view() = default;

        [[nodiscard]] virtual std::span<const chips::register_descriptor> registers() = 0;
    };

    // What the consumer gets fired on every instruction the CPU executes.
    // `pc` is the address of the instruction about to execute; `cycles` is
    // the chip's elapsed cycle counter at the same point. Both are produced
    // by the chip in whatever units it uses; consumers that compare across
    // chips must normalize themselves.
    struct trace_event final {
        std::uint32_t pc{};
        std::uint64_t cycles{};
    };

    // Per-instruction trace hook. Only CPUs typically expose this. Installing
    // an empty callback clears any previous installation. The chip owns the
    // callback's lifetime; calling `install` again replaces it.
    class trace_target {
      public:
        using callback = std::function<void(const trace_event&)>;

        trace_target() = default;
        trace_target(const trace_target&) = delete;
        trace_target& operator=(const trace_target&) = delete;
        virtual ~trace_target() = default;

        virtual void install(callback cb) = 0;
    };

    // One register write observed on a chip: the port/register index written and
    // the value. Fired live from the chip's register-write path as the write
    // happens. Timing is supplied by the CONSUMER (e.g. grouping events by
    // emulated frame) rather than carried here -- audio register logs are
    // consumed at frame cadence and the audio chips lack a unified cycle clock.
    struct reg_write_event final {
        std::uint16_t port{};
        std::uint8_t value{};
    };

    // Per-register-write hook -- the temporal complement to `register_view`'s
    // snapshot. Audio chips (PSG / FM / PCM) expose this so a consumer can
    // capture the register-write STREAM over time, the basis for a VGM-style
    // song / instrument dump (the actual music is the write sequence, not a
    // frozen register file). Installing an empty callback clears any previous
    // installation; the chip owns the callback's lifetime and `install` again
    // replaces it.
    class reg_write_trace {
      public:
        using callback = std::function<void(const reg_write_event&)>;

        reg_write_trace() = default;
        reg_write_trace(const reg_write_trace&) = delete;
        reg_write_trace& operator=(const reg_write_trace&) = delete;
        virtual ~reg_write_trace() = default;

        virtual void install(callback cb) = 0;
    };

    // A secondary framebuffer the chip can render for debug visualization --
    // a Genesis VDP plane A view, a sprite-only view, an SMS pattern-table
    // dump, an Amiga bitplane view, etc. Distinct from `ivideo::framebuffer()`
    // which is the chip's PRIMARY composited output.
    class debug_layer {
      public:
        debug_layer() = default;
        debug_layer(const debug_layer&) = delete;
        debug_layer& operator=(const debug_layer&) = delete;
        virtual ~debug_layer() = default;

        [[nodiscard]] virtual std::string_view name() const noexcept = 0;
        [[nodiscard]] virtual chips::frame_buffer_view view() const = 0;
    };

    // The capability container returned by `chips::ichip::introspection()`.
    // Each accessor is default-empty; chips override only what they expose.
    // The returned pointers/spans are valid for the lifetime of the chip.
    class ichip_introspection {
      public:
        ichip_introspection() = default;
        ichip_introspection(const ichip_introspection&) = delete;
        ichip_introspection& operator=(const ichip_introspection&) = delete;
        virtual ~ichip_introspection() = default;

        [[nodiscard]] virtual std::span<memory_view* const> memory_views() { return {}; }
        [[nodiscard]] virtual register_view* registers() { return nullptr; }
        [[nodiscard]] virtual trace_target* trace() { return nullptr; }
        // Per-register-write trace hook (the temporal sibling of registers()),
        // or nullptr when the chip does not expose its register-write stream.
        [[nodiscard]] virtual reg_write_trace* reg_writes() { return nullptr; }
        [[nodiscard]] virtual std::span<debug_layer* const> debug_layers() { return {}; }

        // The decoded graphics-extraction surface (palettes, tiles, sprites,
        // fonts), or nullptr when the chip exposes no extractable graphics.
        // Defined in asset_views.hpp; consumers that call this include it.
        [[nodiscard]] virtual asset_source* assets() { return nullptr; }

        // The PCM sample-extraction surface, or nullptr when the chip exposes no
        // extractable samples. Defined in audio_views.hpp; consumers include it.
        [[nodiscard]] virtual audio_source* audio() { return nullptr; }
    };

} // namespace mnemos::instrumentation
