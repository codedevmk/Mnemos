#pragma once

#include "chip.hpp"
#include "id.hpp"

#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace mnemos::chips {

    using chip_factory_fn = std::unique_ptr<i_chip> (*)();

    enum class chip_registry_error : std::uint8_t {
        none,
        invalid_id,
        duplicate_id,
        null_factory,
        missing_factory,
    };

    struct chip_factory_descriptor final {
        std::string canonical_id;
        foundation::chip_id id;
        chip_class klass;
        chip_factory_fn create;
    };

    [[nodiscard]] const chip_factory_descriptor*
    find_factory(std::string_view canonical_id) noexcept;

    class factory_registration final {
      public:
        factory_registration() = default;

        factory_registration(chip_registry_error error, std::string canonical_id)
            : error_(error), canonical_id_(std::move(canonical_id)) {}

        [[nodiscard]] bool registered() const noexcept {
            return error_ == chip_registry_error::none;
        }

        [[nodiscard]] explicit operator bool() const noexcept { return registered(); }

        [[nodiscard]] chip_registry_error error() const noexcept { return error_; }

        [[nodiscard]] std::string_view canonical_id() const noexcept {
            return {canonical_id_.data(), canonical_id_.size()};
        }

        [[nodiscard]] const chip_factory_descriptor* descriptor() const noexcept;

      private:
        chip_registry_error error_{chip_registry_error::missing_factory};
        std::string canonical_id_;
    };

    [[nodiscard]] bool is_canonical_chip_id(std::string_view canonical_id) noexcept;

    [[nodiscard]] factory_registration register_factory(std::string_view canonical_id,
                                                        chip_class klass, chip_factory_fn create);

    [[nodiscard]] std::unique_ptr<i_chip> create_chip(std::string_view canonical_id);

    [[nodiscard]] std::span<const chip_factory_descriptor> registered_factories() noexcept;

} // namespace mnemos::chips
