#pragma once

#include "apm_plugin_abi.h"

#include <string>

namespace mnemos::apm::host {

    // Loads an emulator plugin DLL, resolves apm_plugin_entry, and exposes the
    // plugin's C-ABI vtable. Unloads the DLL on destruction. The host link-depends
    // on nothing engine-side -- the engine arrives entirely at runtime.
    class plugin_host {
      public:
        explicit plugin_host(const std::string& dll_path);
        ~plugin_host();
        plugin_host(const plugin_host&) = delete;
        plugin_host& operator=(const plugin_host&) = delete;

        [[nodiscard]] bool ok() const noexcept { return api_ != nullptr; }
        [[nodiscard]] const apm_plugin_api* api() const noexcept { return api_; }

      private:
        void* module_ = nullptr; // HMODULE
        const apm_plugin_api* api_ = nullptr;
    };

} // namespace mnemos::apm::host
