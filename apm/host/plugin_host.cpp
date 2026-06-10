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

    namespace {
        // Version equality is not enough: a stale or partially initialised
        // plugin with null entries would crash the tracer at the first call.
        bool api_complete(const apm_plugin_api* api) noexcept {
            return api->system_name != nullptr && api->create != nullptr &&
                   api->destroy != nullptr && api->load_rom != nullptr &&
                   api->run_frame != nullptr && api->frame_index != nullptr &&
                   api->read_register != nullptr && api->bank_count != nullptr &&
                   api->get_bank != nullptr;
        }
    } // namespace

    plugin_host::plugin_host(const std::string& dll_path) {
#if defined(_WIN32)
        module_ = ::LoadLibraryA(dll_path.c_str());
        if (module_ == nullptr) {
            error_ = "LoadLibrary failed (code " + std::to_string(::GetLastError()) + ")";
            return;
        }
        const FARPROC proc =
            ::GetProcAddress(static_cast<HMODULE>(module_), APM_PLUGIN_ENTRY_SYMBOL);
        if (proc == nullptr) {
            error_ = "missing symbol " APM_PLUGIN_ENTRY_SYMBOL " (code " +
                     std::to_string(::GetLastError()) + ")";
            return;
        }
#pragma warning(suppress : 4191) // FARPROC -> typed entry fn is the documented pattern
        const auto entry = reinterpret_cast<apm_plugin_entry_fn>(proc);
        const apm_plugin_api* api = entry();
        if (api == nullptr) {
            error_ = "plugin entry returned null";
            return;
        }
        if (api->abi_version != APM_PLUGIN_ABI_VERSION) {
            error_ = "ABI version mismatch (plugin " + std::to_string(api->abi_version) +
                     ", host " + std::to_string(APM_PLUGIN_ABI_VERSION) + ")";
            return;
        }
        if (!api_complete(api)) {
            error_ = "plugin API table has null entries";
            return;
        }
        api_ = api;
#else
        (void)dll_path;
        error_ = "plugin loading is implemented on Windows only";
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
