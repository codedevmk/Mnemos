#pragma once

#include "memory_tag.hpp"

#include <cstddef>

namespace mnemos::apm::memory {

    // The dependency-injection contract the engine depends on: each chip/system asks
    // an allocator for its bank's backing store, tagged for observation. The engine
    // owns this abstraction (it declares "I need memory from someone") and never
    // knows who implements it. Standalone, the engine gets a plain malloc-backed
    // allocator; under the tracer it gets a tagged, page-aligned one (tagged_allocator).
    //
    // NOTE: lives in apm/ during the standalone slices; relocates to src/foundation
    // (tier 0) when the engine is wired to source its banks from it.
    class imemory_allocator {
      public:
        virtual ~imemory_allocator() = default;

        // Backing store of `bytes` for the bank identified by `tag`.
        [[nodiscard]] virtual void* allocate_bank(const memory_tag& tag, std::size_t bytes) = 0;
        virtual void free_bank(void* ptr) noexcept = 0;
    };

} // namespace mnemos::apm::memory
