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
        // Why ok() is false: which stage failed and the OS error code.
        [[nodiscard]] const std::string& error() const noexcept { return error_; }

      private:
        void* module_ = nullptr; // HMODULE
        const apm_plugin_api* api_ = nullptr;
        std::string error_;
    };

} // namespace mnemos::apm::host
