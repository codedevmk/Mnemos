#include <mnemos/chips/common/chip_registry.hpp>

#include <string>
#include <vector>

namespace mnemos::chips {
    namespace {

        [[nodiscard]] bool is_chip_id_character(char value) noexcept {
            return (value >= 'a' && value <= 'z') || (value >= '0' && value <= '9') || value == '_';
        }

        [[nodiscard]] std::vector<chip_factory_descriptor>& factory_registry() {
            static std::vector<chip_factory_descriptor> registry;
            return registry;
        }

    } // namespace

    const chip_factory_descriptor* factory_registration::descriptor() const noexcept {
        if (canonical_id_.empty()) {
            return nullptr;
        }

        return find_factory({canonical_id_.data(), canonical_id_.size()});
    }

    bool is_canonical_chip_id(std::string_view canonical_id) noexcept {
        if (canonical_id.empty() || canonical_id.front() == '.' || canonical_id.back() == '.') {
            return false;
        }

        bool saw_separator = false;
        bool previous_was_separator = false;
        for (char value : canonical_id) {
            if (value == '.') {
                if (previous_was_separator) {
                    return false;
                }

                saw_separator = true;
                previous_was_separator = true;
                continue;
            }

            if (!is_chip_id_character(value)) {
                return false;
            }

            previous_was_separator = false;
        }

        return saw_separator;
    }

    factory_registration register_factory(std::string_view canonical_id, chip_class klass,
                                          chip_factory_fn create) {
        if (!is_canonical_chip_id(canonical_id)) {
            return {chip_registry_error::invalid_id, {}};
        }

        if (create == nullptr) {
            return {chip_registry_error::null_factory, {}};
        }

        std::vector<chip_factory_descriptor>& registry = factory_registry();
        const foundation::chip_id id{canonical_id};
        for (const chip_factory_descriptor& descriptor : registry) {
            const std::string_view descriptor_id{descriptor.canonical_id.data(),
                                                 descriptor.canonical_id.size()};
            if (descriptor.id == id || descriptor_id == canonical_id) {
                return {chip_registry_error::duplicate_id, descriptor.canonical_id};
            }
        }

        registry.push_back(chip_factory_descriptor{
            .canonical_id = std::string{canonical_id},
            .id = id,
            .klass = klass,
            .create = create,
        });

        return {chip_registry_error::none, std::string{canonical_id}};
    }

    const chip_factory_descriptor* find_factory(std::string_view canonical_id) noexcept {
        if (!is_canonical_chip_id(canonical_id)) {
            return nullptr;
        }

        const foundation::chip_id id{canonical_id};
        for (const chip_factory_descriptor& descriptor : factory_registry()) {
            const std::string_view descriptor_id{descriptor.canonical_id.data(),
                                                 descriptor.canonical_id.size()};
            if (descriptor.id == id && descriptor_id == canonical_id) {
                return &descriptor;
            }
        }

        return nullptr;
    }

    std::unique_ptr<i_chip> create_chip(std::string_view canonical_id) {
        const chip_factory_descriptor* descriptor = find_factory(canonical_id);
        if (descriptor == nullptr || descriptor->create == nullptr) {
            return nullptr;
        }

        return descriptor->create();
    }

    std::span<const chip_factory_descriptor> registered_factories() noexcept {
        const std::vector<chip_factory_descriptor>& registry = factory_registry();
        return registry;
    }

} // namespace mnemos::chips
