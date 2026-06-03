#include "tagged_allocator.hpp"

#include <cstdint>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cstdlib>
#include <unistd.h>
#endif

namespace mnemos::apm::memory {

    namespace {

        std::size_t page_size() noexcept {
#if defined(_WIN32)
            SYSTEM_INFO si{};
            GetSystemInfo(&si);
            return si.dwPageSize;
#else
            return static_cast<std::size_t>(sysconf(_SC_PAGESIZE));
#endif
        }

        // Reserve+commit page-aligned, writable memory of at least `bytes`.
        void* page_alloc(std::size_t bytes) noexcept {
#if defined(_WIN32)
            return VirtualAlloc(nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
#else
            void* p = nullptr;
            if (posix_memalign(&p, page_size(), bytes) != 0) {
                return nullptr;
            }
            return p;
#endif
        }

        void page_free(void* p) noexcept {
#if defined(_WIN32)
            VirtualFree(p, 0, MEM_RELEASE);
#else
            std::free(p);
#endif
        }

    } // namespace

    tagged_allocator::~tagged_allocator() {
        for (const tagged_bank& bank : registry_.banks()) {
            page_free(bank.host_ptr);
        }
    }

    void* tagged_allocator::allocate_bank(const memory_tag& tag, std::size_t bytes) {
        const std::size_t page = page_size();
        const std::size_t rounded = ((bytes + page - 1) / page) * page;
        void* p = page_alloc(rounded);
        if (p == nullptr) {
            return nullptr;
        }
        // Register the requested bank extent (not the page-rounded slack), so reverse
        // lookup matches exactly the guest-visible bank.
        registry_.add(tag, p, bytes);
        return p;
    }

    void tagged_allocator::free_bank(void* ptr) noexcept {
        if (ptr == nullptr) {
            return;
        }
        registry_.remove(ptr);
        page_free(ptr);
    }

} // namespace mnemos::apm::memory
