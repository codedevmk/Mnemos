#include "apm_plugin_abi.h"
#include "page_guard.hpp"
#include "plugin_host.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

namespace {

    using mnemos::apm::host::plugin_host;
    using mnemos::apm::memory::access_kind;
    using mnemos::apm::memory::guard_event;
    using mnemos::apm::memory::page_guard;

    std::vector<std::uint8_t> read_file(const std::string& path) {
        std::ifstream f(path, std::ios::binary | std::ios::ate);
        if (!f) {
            return {};
        }
        const std::streamoff size = f.tellg();
        if (size <= 0) {
            return {};
        }
        f.seekg(0);
        std::vector<std::uint8_t> buf(static_cast<std::size_t>(size));
        f.read(reinterpret_cast<char*>(buf.data()), size);
        return buf;
    }

    struct options {
        std::string plugin = "mnemos_genesis.dll";
        std::string rom;
        std::uint64_t watch_addr = 0xFFF809;
        std::uint64_t frames = 130;
        bool read_watch = false;
    };

} // namespace

// Tracer host: load an emulator plugin, arm a page-protection watchpoint on a
// guest address, run N frames, and report each frame's end value plus the writer
// PCs intercepted that frame -- observing the engine entirely from the outside.
int main(int argc, char** argv) {
    options opt;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        const auto next = [&]() -> std::string {
            return (i + 1 < argc) ? std::string(argv[++i]) : std::string();
        };
        if (a == "--plugin") {
            opt.plugin = next();
        } else if (a == "--rom") {
            opt.rom = next();
        } else if (a == "--watch") {
            opt.watch_addr = std::strtoull(next().c_str(), nullptr, 16);
        } else if (a == "--frames") {
            opt.frames = std::strtoull(next().c_str(), nullptr, 10);
        } else if (a == "--read") {
            opt.read_watch = true;
        } else {
            std::fprintf(stderr, "unknown argument: %s\n", a.c_str());
            return 2;
        }
    }
    if (opt.rom.empty()) {
        std::fprintf(stderr, "usage: mnemos_tracer --rom <path> [--plugin <dll>] "
                             "[--watch <hex-guest-addr>] [--frames N] [--read]\n");
        return 2;
    }

    plugin_host host(opt.plugin);
    if (!host.ok()) {
        std::fprintf(stderr, "failed to load plugin: %s\n", opt.plugin.c_str());
        return 1;
    }
    const apm_plugin_api* api = host.api();

    apm_plugin* p = api->create();
    if (p == nullptr) {
        std::fprintf(stderr, "plugin create() failed\n");
        return 1;
    }

    const std::vector<std::uint8_t> rom = read_file(opt.rom);
    if (rom.empty()) {
        std::fprintf(stderr, "failed to read rom: %s\n", opt.rom.c_str());
        return 1;
    }
    if (api->load_rom(p, rom.data(), rom.size()) != 0) {
        std::fprintf(stderr, "plugin load_rom() failed\n");
        return 1;
    }

    // Optionally map the guest watch address into its live host bank
    // (--watch 0 profiles per-frame CPU cycles without arming a watchpoint).
    const bool watching = (opt.watch_addr != 0);
    std::uint8_t* host_addr = nullptr;
    if (watching) {
        const int banks = api->bank_count(p);
        for (int i = 0; i < banks; ++i) {
            apm_bank_info b{};
            if (api->get_bank(p, i, &b) != 0) {
                continue;
            }
            if (opt.watch_addr >= b.guest_base && opt.watch_addr < b.guest_base + b.size) {
                host_addr =
                    static_cast<std::uint8_t*>(b.host_ptr) + (opt.watch_addr - b.guest_base);
                std::printf("watch $%llX -> bank '%s' (guest_base $%llX) host %p\n",
                            static_cast<unsigned long long>(opt.watch_addr), b.name,
                            static_cast<unsigned long long>(b.guest_base),
                            static_cast<void*>(host_addr));
                break;
            }
        }
        if (host_addr == nullptr) {
            std::fprintf(stderr, "watch addr $%llX not in any bank\n",
                         static_cast<unsigned long long>(opt.watch_addr));
            return 1;
        }
        if (!page_guard::supported()) {
            std::fprintf(stderr, "page_guard not supported on this platform\n");
            return 1;
        }
    }

    struct write_record {
        std::uint64_t frame;
        std::uint64_t pc;
    };
    std::vector<write_record> records;
    records.reserve(1U << 20); // fixed: never reallocate inside the fault handler
    std::uint64_t current_frame = 0;

    page_guard guard;
    if (watching) {
        guard.watch(host_addr, 1, opt.read_watch ? access_kind::read : access_kind::write,
                    [&](const guard_event&) {
                        if (records.size() < records.capacity()) {
                            records.push_back({current_frame, api->read_register(p, APM_REG_INST)});
                        }
                    });
    }

    std::printf("frame   cpu_cycles  end_value  writer_pcs\n");
    for (std::uint64_t f = 1; f <= opt.frames; ++f) {
        current_frame = f;
        const std::size_t before = records.size();
        const std::uint64_t cyc_before = api->read_register(p, APM_REG_CYCLES);
        api->run_frame(p);
        const std::uint64_t cyc_delta = api->read_register(p, APM_REG_CYCLES) - cyc_before;
        std::printf("f=%-4llu  %8llu", static_cast<unsigned long long>(f),
                    static_cast<unsigned long long>(cyc_delta));
        if (watching) {
            const std::uint8_t value = *host_addr; // read-only page is still readable
            std::printf("  $%02X     ", static_cast<unsigned>(value));
            for (std::size_t i = before; i < records.size(); ++i) {
                std::printf(" pc=$%06llX", static_cast<unsigned long long>(records[i].pc));
            }
        }
        std::printf("\n");
    }

    if (watching) {
        guard.unwatch(host_addr);
    }
    api->destroy(p);
    return 0;
}
