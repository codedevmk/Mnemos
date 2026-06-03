#include "plugin_host.hpp"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace mnemos::apm::host {

    plugin_host::plugin_host(const std::string& dll_path) {
#if defined(_WIN32)
        module_ = ::LoadLibraryA(dll_path.c_str());
        if (module_ == nullptr) {
            return;
        }
        const FARPROC proc =
            ::GetProcAddress(static_cast<HMODULE>(module_), APM_PLUGIN_ENTRY_SYMBOL);
        if (proc == nullptr) {
            return;
        }
#pragma warning(suppress : 4191) // FARPROC -> typed entry fn is the documented pattern
        const auto entry = reinterpret_cast<apm_plugin_entry_fn>(proc);
        const apm_plugin_api* api = entry();
        if (api != nullptr && api->abi_version == APM_PLUGIN_ABI_VERSION) {
            api_ = api;
        }
#else
        (void)dll_path;
#endif
    }

    plugin_host::~plugin_host() {
#if defined(_WIN32)
        if (module_ != nullptr) {
            ::FreeLibrary(static_cast<HMODULE>(module_));
        }
#endif
    }

} // namespace mnemos::apm::host
