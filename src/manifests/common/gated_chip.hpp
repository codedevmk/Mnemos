#pragma once

// System-agnostic chip-tick gate. The scheduler ticks a `gated_chip` like
// any other `ichip`; the wrapper consults its `gate` predicate per tick
// and forwards or drops accordingly. Everything else (metadata, reset,
// save/load, introspection) passes straight through to the wrapped inner
// chip.
//
// The builder uses these to honour manifest `[[gate]]` entries:
//
//   [[gate]]
//   chip = "z80"
//   predicate = "genesis.z80_running"
//
// produces a gated_chip wrapping the Z80, whose `gate_()` consults the
// host-supplied predicate. Inner chips remain reachable through
// `system_graph::chip_by_id` (which resolves to the ORIGINAL chip so
// introspection and register queries hit the right type); the gated
// wrapper lives only in the scheduler's tick list.
//
// Replaces the per-system `gated_chip` / `predicate_gated_chip` pair that
// historically lived in `manifests/genesis/genesis_system.hpp`.

#include "chip.hpp"
#include "predicates.hpp"

#include <cstdint>
#include <utility>

namespace mnemos::manifests {

    class gated_chip final : public chips::ichip {
      public:
        gated_chip(chips::ichip& inner, chips::predicate_fn gate) noexcept
            : inner_(&inner), gate_(std::move(gate)) {}

        [[nodiscard]] chips::chip_metadata metadata() const noexcept override {
            return inner_->metadata();
        }
        void tick(std::uint64_t cycles) override {
            if (gate_ && gate_()) {
                inner_->tick(cycles);
            }
        }
        void reset(chips::reset_kind kind) override { inner_->reset(kind); }
        void save_state(chips::state_writer& writer) const override { inner_->save_state(writer); }
        void load_state(chips::state_reader& reader) override { inner_->load_state(reader); }
        void configure(const chips::config_table& cfg,
                       const chips::callback_table& callbacks) override {
            inner_->configure(cfg, callbacks);
        }
        [[nodiscard]] instrumentation::ichip_introspection& introspection() noexcept override {
            return inner_->introspection();
        }

        // For tests + introspection that wants to bypass the wrapper.
        [[nodiscard]] chips::ichip& inner() noexcept { return *inner_; }

      private:
        chips::ichip* inner_;
        chips::predicate_fn gate_;
    };

} // namespace mnemos::manifests
