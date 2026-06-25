#pragma once

// Reusable instrumentation adapters: a chip configures its introspection surface by
// supplying callbacks, instead of hand-writing a nested ichip_introspection plus a
// per-capability bridge class. Replaces the repeated introspection_surface / *_impl
// boilerplate (review A2). This is the cold path (debugger / exporters), so the
// std::function indirection is irrelevant to emulation speed.

#include "introspection_views.hpp"

#include <functional>
#include <optional>
#include <span>
#include <utility>

namespace mnemos::instrumentation {

    // register_view backed by a snapshot provider (the chip's register_snapshot()).
    class callback_register_view final : public register_view {
      public:
        using provider = std::function<std::span<const chips::register_descriptor>()>;
        explicit callback_register_view(provider snapshot) : snapshot_(std::move(snapshot)) {}
        [[nodiscard]] std::span<const chips::register_descriptor> registers() override {
            return snapshot_ ? snapshot_() : std::span<const chips::register_descriptor>{};
        }

      private:
        provider snapshot_;
    };

    // trace_target that forwards install() to a chip-supplied installer, which wires
    // the generic callback into the chip's own per-instruction trace slot.
    class function_trace_target final : public trace_target {
      public:
        using installer = std::function<void(callback)>;
        explicit function_trace_target(installer install_fn) : install_(std::move(install_fn)) {}
        void install(callback cb) override {
            if (install_) {
                install_(std::move(cb));
            }
        }

      private:
        installer install_;
    };

    // Bridge a chip's PC-only trace slot to the generic trace_target: the returned
    // installer stores into `slot` a wrapper that packages (pc, cycles()) into a
    // trace_event. Every CPU core's per-instruction hook is PC-only with cycles
    // queried at fire time. `slot` and `cycles` must outlive the target (both are
    // the owning chip's members).
    [[nodiscard]] inline function_trace_target::installer
    pc_trace_installer(std::function<void(std::uint32_t)>& slot,
                       std::function<std::uint64_t()> cycles) {
        return [&slot, cycles = std::move(cycles)](trace_target::callback cb) {
            if (cb) {
                slot = [cb = std::move(cb), cycles](std::uint32_t pc) {
                    cb(trace_event{.pc = pc, .cycles = cycles()});
                };
            } else {
                slot = {};
            }
        };
    }

    // reg_write_trace counterpart for chips (audio) that expose a register-write stream.
    class function_reg_write_trace final : public reg_write_trace {
      public:
        using installer = std::function<void(callback)>;
        explicit function_reg_write_trace(installer install_fn) : install_(std::move(install_fn)) {}
        void install(callback cb) override {
            if (install_) {
                install_(std::move(cb));
            }
        }

      private:
        installer install_;
    };

    // A configurable ichip_introspection a chip populates in its constructor:
    //   introspection_.with_registers([this] { return register_snapshot(); })
    //                 .with_trace([this](auto cb) { /* wire cb into the chip */ });
    // Each capability is exposed only when configured; the rest stay nullptr. Pinned
    // (the configured callbacks capture the owning chip's `this`): never moved.
    class introspection_builder final : public ichip_introspection {
      public:
        introspection_builder() = default;
        introspection_builder(const introspection_builder&) = delete;
        introspection_builder& operator=(const introspection_builder&) = delete;
        introspection_builder(introspection_builder&&) = delete;
        introspection_builder& operator=(introspection_builder&&) = delete;

        introspection_builder& with_registers(callback_register_view::provider snapshot) {
            registers_.emplace(std::move(snapshot));
            return *this;
        }
        introspection_builder& with_memory_views(std::span<memory_view* const> views) {
            memory_views_ = views;
            return *this;
        }
        introspection_builder& with_trace(function_trace_target::installer install_fn) {
            trace_.emplace(std::move(install_fn));
            return *this;
        }
        introspection_builder& with_reg_writes(function_reg_write_trace::installer install_fn) {
            reg_writes_.emplace(std::move(install_fn));
            return *this;
        }
        // Capability surfaces a chip implements directly (audio PCM export, etc.)
        // are registered by borrowed pointer; the chip owns the implementation.
        introspection_builder& with_audio(audio_source* source) {
            audio_ = source;
            return *this;
        }

        [[nodiscard]] register_view* registers() override {
            return registers_ ? &*registers_ : nullptr;
        }
        [[nodiscard]] std::span<memory_view* const> memory_views() override {
            return memory_views_;
        }
        [[nodiscard]] trace_target* trace() override { return trace_ ? &*trace_ : nullptr; }
        [[nodiscard]] reg_write_trace* reg_writes() override {
            return reg_writes_ ? &*reg_writes_ : nullptr;
        }
        [[nodiscard]] audio_source* audio() override { return audio_; }

      private:
        std::span<memory_view* const> memory_views_{};
        std::optional<callback_register_view> registers_;
        std::optional<function_trace_target> trace_;
        std::optional<function_reg_write_trace> reg_writes_;
        audio_source* audio_ = nullptr;
    };

} // namespace mnemos::instrumentation
