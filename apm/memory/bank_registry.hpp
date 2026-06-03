#pragma once

#include "memory_tag.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace mnemos::apm::memory {

    // A registered guest memory bank: its tag plus the host-side backing it lives in.
    struct tagged_bank {
        memory_tag tag;
        void* host_ptr;
        std::size_t size;

        // Translate a host address inside this bank to its guest address.
        [[nodiscard]] std::uint64_t guest_address_of(const void* host_addr) const noexcept;
    };

    // Tracks the allocated banks and answers the reverse question the fault handler
    // needs: "which chip/bank owns this faulting host address?" Plus tag lookup for
    // arming watches by (chip, bank). Linear over a handful of banks; not hot.
    class bank_registry {
      public:
        void add(const memory_tag& tag, void* host_ptr, std::size_t size);
        void remove(const void* host_ptr) noexcept;

        // The bank whose [host_ptr, host_ptr+size) contains `host_addr`, or nullptr.
        [[nodiscard]] const tagged_bank* find_by_address(const void* host_addr) const noexcept;

        // The bank with this (chip, bank) identity, or nullptr.
        [[nodiscard]] const tagged_bank* find_by_tag(std::uint32_t chip,
                                                     std::uint32_t bank) const noexcept;

        [[nodiscard]] const std::vector<tagged_bank>& banks() const noexcept { return banks_; }

      private:
        std::vector<tagged_bank> banks_;
    };

} // namespace mnemos::apm::memory
