#include "adapter_registry.hpp"

#include <algorithm>
#include <utility>

namespace mnemos::frontend_sdk {

    adapter_registry& adapter_registry::instance() {
        static adapter_registry r;
        return r;
    }

    void adapter_registry::register_family(std::string family, factory fn) {
        std::lock_guard<std::mutex> lock(mu_);
        factories_[std::move(family)] = std::move(fn);
    }

    std::unique_ptr<player_system>
    adapter_registry::create(std::string_view family, adapter_options options) const {
        factory copy;
        {
            std::lock_guard<std::mutex> lock(mu_);
            const auto it = factories_.find(std::string{family});
            if (it == factories_.end()) {
                return nullptr;
            }
            copy = it->second;
        }
        return copy(std::move(options));
    }

    std::vector<std::string> adapter_registry::families() const {
        std::vector<std::string> out;
        std::lock_guard<std::mutex> lock(mu_);
        out.reserve(factories_.size());
        for (const auto& [name, _] : factories_) {
            out.push_back(name);
        }
        std::sort(out.begin(), out.end());
        return out;
    }

} // namespace mnemos::frontend_sdk
