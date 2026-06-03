#include "bank_registry.hpp"

#include <cstdint>

namespace mnemos::apm::memory {

    std::uint64_t tagged_bank::guest_address_of(const void* host_addr) const noexcept {
        const auto base = reinterpret_cast<std::uintptr_t>(host_ptr);
        const auto addr = reinterpret_cast<std::uintptr_t>(host_addr);
        return tag.guest_base + static_cast<std::uint64_t>(addr - base);
    }

    void bank_registry::add(const memory_tag& tag, void* host_ptr, std::size_t size) {
        banks_.push_back(tagged_bank{tag, host_ptr, size});
    }

    void bank_registry::remove(const void* host_ptr) noexcept {
        for (auto it = banks_.begin(); it != banks_.end(); ++it) {
            if (it->host_ptr == host_ptr) {
                banks_.erase(it);
                return;
            }
        }
    }

    const tagged_bank* bank_registry::find_by_address(const void* host_addr) const noexcept {
        const auto addr = reinterpret_cast<std::uintptr_t>(host_addr);
        for (const tagged_bank& b : banks_) {
            const auto begin = reinterpret_cast<std::uintptr_t>(b.host_ptr);
            if (addr >= begin && addr < begin + b.size) {
                return &b;
            }
        }
        return nullptr;
    }

    const tagged_bank* bank_registry::find_by_tag(std::uint32_t chip,
                                                  std::uint32_t bank) const noexcept {
        for (const tagged_bank& b : banks_) {
            if (b.tag.chip == chip && b.tag.bank == bank) {
                return &b;
            }
        }
        return nullptr;
    }

} // namespace mnemos::apm::memory
