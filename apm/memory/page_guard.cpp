#include "page_guard.hpp"

// The single-step recovery below drives EFLAGS.TF, which exists only on x86;
// other Windows targets (ARM64) take the unsupported stub until a PSTATE.SS
// path exists -- supported() must answer honestly there.
#if defined(_WIN32) && (defined(_M_X64) || defined(_M_IX86))
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdint>
#include <mutex>
#include <utility>
#endif

namespace mnemos::apm::memory {

#if defined(_WIN32) && (defined(_M_X64) || defined(_M_IX86))

    namespace {

        constexpr DWORD trap_flag = 0x100U; // EFLAGS.TF: trap after the next instruction

        // One armed watch. Protection covers whole pages; the watched sub-range
        // [watch_begin, watch_end) filters which faults fire the handler.
        struct watch_entry {
            std::uintptr_t page_begin;
            std::uintptr_t page_end;
            std::uintptr_t watch_begin;
            std::uintptr_t watch_end;
            DWORD protect; // PAGE_READONLY (write-watch) or PAGE_NOACCESS (read-watch)
            access_kind kind;
            guard_handler handler;
        };

        std::mutex g_mutex;
        std::vector<watch_entry> g_watches;
        PVOID g_veh = nullptr;
        int g_refcount = 0;

        // Per-thread record of a page temporarily unprotected to let a faulting
        // access complete; reprotected on the following single-step trap.
        struct pending_reprotect {
            std::uintptr_t page_begin;
            std::size_t size;
            DWORD protect;
            bool active;
        };
        thread_local pending_reprotect t_pending{};

        std::size_t page_size() noexcept {
            SYSTEM_INFO si{};
            GetSystemInfo(&si);
            return si.dwPageSize;
        }

        std::uintptr_t host_instruction_pointer(const CONTEXT* ctx) noexcept {
#if defined(_M_X64)
            return static_cast<std::uintptr_t>(ctx->Rip);
#elif defined(_M_IX86)
            return static_cast<std::uintptr_t>(ctx->Eip);
#else
            (void)ctx;
            return 0U;
#endif
        }

        LONG CALLBACK vectored_handler(EXCEPTION_POINTERS* ep) {
            const DWORD code = ep->ExceptionRecord->ExceptionCode;

            // Stage 2: the access has re-run on an unprotected page; restore the
            // guard and stop single-stepping.
            if (code == EXCEPTION_SINGLE_STEP) {
                if (!t_pending.active) {
                    return EXCEPTION_CONTINUE_SEARCH;
                }
                DWORD old = 0;
                VirtualProtect(reinterpret_cast<void*>(t_pending.page_begin),
                               static_cast<SIZE_T>(t_pending.size), t_pending.protect, &old);
                t_pending.active = false;
                ep->ContextRecord->EFlags &= ~trap_flag;
                return EXCEPTION_CONTINUE_EXECUTION;
            }

            if (code != EXCEPTION_ACCESS_VIOLATION) {
                return EXCEPTION_CONTINUE_SEARCH;
            }

            const ULONG_PTR* info = ep->ExceptionRecord->ExceptionInformation;
            const bool is_write = (info[0] == 1U);
            const auto fault_addr = static_cast<std::uintptr_t>(info[1]);

            // The matched handler is copied out and invoked only after the lock
            // is dropped and the page is reopened: running user code while
            // holding the non-recursive g_mutex, on a still-protected page,
            // meant any handler that touched a watched page re-entered this
            // VEH and self-deadlocked on the lock.
            guard_handler handler_copy;
            guard_event ev{};
            bool matched = false;
            {
                std::lock_guard<std::mutex> lock(g_mutex);
                for (const watch_entry& w : g_watches) {
                    if (fault_addr < w.page_begin || fault_addr >= w.page_end) {
                        continue;
                    }

                    // Report only accesses that hit the exact watched range and
                    // kind; other faults on the shared page recover silently.
                    const bool in_range = fault_addr >= w.watch_begin && fault_addr < w.watch_end;
                    const bool kind_match =
                        (w.kind == access_kind::write)
                            ? is_write
                            : true; // read-watch (NOACCESS) traps reads and writes
                    if (in_range && kind_match && w.handler) {
                        handler_copy = w.handler;
                        ev = guard_event{reinterpret_cast<const void*>(fault_addr),
                                         is_write ? access_kind::write : access_kind::read,
                                         host_instruction_pointer(ep->ContextRecord)};
                    }

                    // Stage 1: open the page, re-run the faulting access,
                    // single-step back so we can reprotect it.
                    DWORD old = 0;
                    const auto span = static_cast<SIZE_T>(w.page_end - w.page_begin);
                    VirtualProtect(reinterpret_cast<void*>(w.page_begin), span, PAGE_READWRITE,
                                   &old);
                    t_pending.page_begin = w.page_begin;
                    t_pending.size = static_cast<std::size_t>(w.page_end - w.page_begin);
                    t_pending.protect = w.protect;
                    t_pending.active = true;
                    ep->ContextRecord->EFlags |= trap_flag;
                    matched = true;
                    break;
                }
            }
            if (!matched) {
                return EXCEPTION_CONTINUE_SEARCH;
            }
            if (handler_copy) {
                handler_copy(ev);
            }
            return EXCEPTION_CONTINUE_EXECUTION;
        }

    } // namespace

    page_guard::page_guard() {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_refcount++ == 0) {
            g_veh = AddVectoredExceptionHandler(1U, &vectored_handler);
        }
    }

    page_guard::~page_guard() {
        for (const void* base : watched_) {
            unwatch(base);
        }
        for (void* p : allocations_) {
            VirtualFree(p, 0, MEM_RELEASE);
        }
        std::lock_guard<std::mutex> lock(g_mutex);
        if (--g_refcount == 0 && g_veh != nullptr) {
            RemoveVectoredExceptionHandler(g_veh);
            g_veh = nullptr;
        }
    }

    bool page_guard::supported() noexcept { return true; }

    void* page_guard::allocate(std::size_t bytes) {
        const std::size_t page = page_size();
        if (bytes == 0U || bytes > SIZE_MAX - page + 1U) {
            return nullptr; // rounding below would wrap
        }
        const std::size_t rounded = ((bytes + page - 1) / page) * page;
        void* p = VirtualAlloc(nullptr, rounded, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (p != nullptr) {
            allocations_.push_back(p);
        }
        return p;
    }

    void page_guard::watch(void* base, std::size_t bytes, access_kind kind, guard_handler handler) {
        const auto page = static_cast<std::uintptr_t>(page_size());
        const auto b = reinterpret_cast<std::uintptr_t>(base);
        const std::uintptr_t page_begin = b & ~(page - 1U);
        const std::uintptr_t page_end = (b + bytes + page - 1U) & ~(page - 1U);
        const DWORD protect = (kind == access_kind::read) ? PAGE_NOACCESS : PAGE_READONLY;

        {
            std::lock_guard<std::mutex> lock(g_mutex);
            g_watches.push_back(
                watch_entry{page_begin, page_end, b, b + bytes, protect, kind, std::move(handler)});
        }

        DWORD old = 0;
        if (VirtualProtect(reinterpret_cast<void*>(page_begin),
                           static_cast<SIZE_T>(page_end - page_begin), protect, &old) == 0) {
            // A failed protect must not leave a registered-but-unarmed watch:
            // the caller would believe accesses are observed when none are.
            std::lock_guard<std::mutex> lock(g_mutex);
            if (!g_watches.empty() && g_watches.back().watch_begin == b) {
                g_watches.pop_back();
            }
            return;
        }
        watched_.push_back(base);
    }

    void page_guard::unwatch(const void* base) noexcept {
        std::lock_guard<std::mutex> lock(g_mutex);
        const auto b = reinterpret_cast<std::uintptr_t>(base);
        for (auto it = g_watches.begin(); it != g_watches.end(); ++it) {
            if (it->watch_begin != b) {
                continue;
            }
            DWORD old = 0;
            VirtualProtect(reinterpret_cast<void*>(it->page_begin),
                           static_cast<SIZE_T>(it->page_end - it->page_begin), PAGE_READWRITE,
                           &old);
            g_watches.erase(it);
            return;
        }
    }

#else // !(_WIN32 && x86/x64)

    page_guard::page_guard() = default;
    page_guard::~page_guard() = default;
    bool page_guard::supported() noexcept { return false; }
    void* page_guard::allocate(std::size_t bytes) {
        (void)bytes;
        return nullptr;
    }
    void page_guard::watch(void* base, std::size_t bytes, access_kind kind, guard_handler handler) {
        (void)base;
        (void)bytes;
        (void)kind;
        (void)handler;
    }
    void page_guard::unwatch(const void* base) noexcept { (void)base; }

#endif

} // namespace mnemos::apm::memory
