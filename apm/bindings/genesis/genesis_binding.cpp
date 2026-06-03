#include "apm_plugin_abi.h"

#include "genesis_runtime.hpp" // build_genesis_runtime, genesis_runtime, schedule()
#include "m68000.hpp"          // chips::cpu::m68000 registers + current_instruction_addr
#include "scheduler.hpp"       // runtime::scheduler, scheduled_chip

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

// Thin bridge: drives the manifest-built Genesis runtime through the APM plugin C
// ABI. The engine is untouched; this binding is the only code that knows both
// sides. Built into mnemos_genesis.dll, which the tracer host loads.

struct apm_plugin {
    // Declaration order is destruction order (reversed): the scheduler holds an
    // ivideo* into sys->vdp(), so sys must outlive it -> sys first, sched after.
    std::unique_ptr<mnemos::manifests::genesis::genesis_runtime> sys;
    std::optional<mnemos::runtime::scheduler> sched;
    std::vector<apm_bank_info> banks;
};

namespace {

    // Chip ids for tagging (engine-agnostic on the ABI side; assigned here).
    constexpr std::uint32_t kChipSystem = 0;
    constexpr std::uint32_t kBankWorkRam = 0;

    apm_plugin* gb_create() { return new (std::nothrow) apm_plugin(); }

    void gb_destroy(apm_plugin* p) { delete p; }

    int gb_load_rom(apm_plugin* p, const std::uint8_t* data, std::size_t size) {
        if (p == nullptr || data == nullptr) {
            return -1;
        }
        std::vector<std::uint8_t> rom(data, data + size);
        p->sys = mnemos::manifests::genesis::build_genesis_runtime(std::move(rom), {});
        if (!p->sys) {
            return -1;
        }

        std::vector<mnemos::runtime::scheduled_chip> chips;
        for (const auto& entry : p->sys->schedule()) {
            chips.push_back({entry.chip, entry.weight});
        }
        p->sched.emplace(std::move(chips), p->sys->vdp());

        p->banks.clear();
        const std::span<std::uint8_t> wr = p->sys->graph.region_span("work_ram");
        p->banks.push_back(apm_bank_info{wr.data(), static_cast<std::uint64_t>(wr.size()),
                                         0xFF0000ULL, kChipSystem, kBankWorkRam, "work_ram"});
        return 0;
    }

    std::uint64_t gb_run_frame(apm_plugin* p) {
        if (p == nullptr || !p->sched) {
            return 0;
        }
        return p->sched->run_frame();
    }

    std::uint64_t gb_frame_index(apm_plugin* p) {
        if (p == nullptr || !p->sched) {
            return 0;
        }
        return p->sched->frame_index();
    }

    std::uint64_t gb_read_register(apm_plugin* p, int reg) {
        if (p == nullptr || !p->sys) {
            return 0;
        }
        auto* cpu = p->sys->cpu();
        if (cpu == nullptr) {
            return 0;
        }
        if (reg == APM_REG_INST) {
            return cpu->current_instruction_addr();
        }
        if (reg == APM_REG_CYCLES) {
            return cpu->elapsed_cycles();
        }
        const auto r = cpu->cpu_registers();
        if (reg == APM_REG_PC) {
            return r.pc;
        }
        if (reg == APM_REG_SR) {
            return r.sr;
        }
        if (reg >= APM_REG_D0 && reg < APM_REG_D0 + 8) {
            return r.d[static_cast<std::size_t>(reg - APM_REG_D0)];
        }
        if (reg >= APM_REG_A0 && reg < APM_REG_A0 + 8) {
            return r.a[static_cast<std::size_t>(reg - APM_REG_A0)];
        }
        return 0;
    }

    int gb_bank_count(apm_plugin* p) {
        return (p == nullptr) ? 0 : static_cast<int>(p->banks.size());
    }

    int gb_get_bank(apm_plugin* p, int index, apm_bank_info* out) {
        if (p == nullptr || out == nullptr || index < 0 ||
            index >= static_cast<int>(p->banks.size())) {
            return -1;
        }
        *out = p->banks[static_cast<std::size_t>(index)];
        return 0;
    }

    const apm_plugin_api g_api = {
        APM_PLUGIN_ABI_VERSION, "genesis",       &gb_create,        &gb_destroy,    &gb_load_rom,
        &gb_run_frame,          &gb_frame_index, &gb_read_register, &gb_bank_count, &gb_get_bank,
    };

} // namespace

#if defined(_WIN32)
#define APM_PLUGIN_EXPORT __declspec(dllexport)
#else
#define APM_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

extern "C" APM_PLUGIN_EXPORT const apm_plugin_api* apm_plugin_entry(void) { return &g_api; }
