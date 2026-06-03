#pragma once

#include "bank_registry.hpp"
#include "imemory_allocator.hpp"

#include <cstddef>

namespace mnemos::apm::memory {

    // Page-aligned, tagged bank allocator -- the tracer's implementation of the
    // engine's imemory_allocator contract. Each bank starts on its own page
    // boundary so page_guard can protect it independently, and is recorded in a
    // bank_registry for reverse address->tag lookup. Windows: VirtualAlloc; other
    // platforms: page-aligned heap allocation.
    class tagged_allocator final : public imemory_allocator {
      public:
        tagged_allocator() = default;
        ~tagged_allocator() override;
        tagged_allocator(const tagged_allocator&) = delete;
        tagged_allocator& operator=(const tagged_allocator&) = delete;

        [[nodiscard]] void* allocate_bank(const memory_tag& tag, std::size_t bytes) override;
        void free_bank(void* ptr) noexcept override;

        [[nodiscard]] const bank_registry& registry() const noexcept { return registry_; }

      private:
        bank_registry registry_;
    };

} // namespace mnemos::apm::memory
