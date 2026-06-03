#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace mnemos::apm::memory {

    enum class access_kind { read, write };

    // A single intercepted guest-memory access, reported from the OS fault handler.
    struct guard_event {
        const void* address;    // faulting host address (inside a watched range)
        access_kind kind;       // read or write, per the fault's access type
        std::uintptr_t host_ip; // host instruction pointer at the fault site
    };

    using guard_handler = std::function<void(const guard_event&)>;

    // OS page-protection watchpoints. Arms VirtualProtect over committed pages and
    // recovers each faulting access (record -> unprotect -> single-step -> reprotect)
    // so the watched code runs unmodified and the access still lands. Windows-only
    // for now; supported() reports availability. The traced program never sees this:
    // observation happens entirely from the outside, via the host's fault handler.
    class page_guard {
      public:
        page_guard();
        ~page_guard();
        page_guard(const page_guard&) = delete;
        page_guard& operator=(const page_guard&) = delete;

        [[nodiscard]] static bool supported() noexcept;

        // Reserve+commit page-aligned, writable bytes (rounded up to a page).
        // Returns nullptr on failure / unsupported platforms.
        [[nodiscard]] void* allocate(std::size_t bytes);

        // Watch [base, base+bytes) for `kind`; `handler` fires on each matching
        // access, after which the access completes normally.
        void watch(void* base, std::size_t bytes, access_kind kind, guard_handler handler);

        void unwatch(const void* base) noexcept;

      private:
        std::vector<void*> allocations_;   // owned VirtualAlloc regions, freed on destruction
        std::vector<const void*> watched_; // armed watch bases, disarmed on destruction
    };

} // namespace mnemos::apm::memory
