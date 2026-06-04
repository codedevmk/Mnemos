#include "sms_runtime.hpp"

#include "manifest.hpp"      // parse_manifest
#include "sms_manifests.hpp" // manifest_toml

#include "codemasters_mapper.hpp"
#include "hicom_mapper.hpp"
#include "korean_mapper.hpp"
#include "korean_msx_mapper.hpp"
#include "mk3020.hpp" // default controller-port peripheral
#include "sms_mapper.hpp"

#include <optional>
#include <span>
#include <string_view>
#include <utility>

namespace mnemos::manifests::sms {

    std::unique_ptr<sms_runtime> build_sms_runtime(std::vector<std::uint8_t> rom,
                                                   const sms_config& config) {
        auto rt = std::make_unique<sms_runtime>();
        rt->rom = std::move(rom);

        // Resolve the effective mapper once (forced by config, else auto-detected;
        // Korean is force-only) -- the shared policy assemble_sms also uses.
        const sms_config::mapper kind =
            resolve_mapper(config, std::span<const std::uint8_t>(rt->rom));
        rt->codemasters_active = kind == sms_config::mapper::codemasters;
        rt->korean_active = kind == sms_config::mapper::korean;
        rt->korean_msx_active = kind == sms_config::mapper::korean_msx ||
                                kind == sms_config::mapper::korean_msx_nemesis;
        rt->korean_hicom_active = kind == sms_config::mapper::korean_hicom;

        // Host glue: every closure captures &rt->state, whose address is stable
        // because rt is heap-allocated. The chips copy the closures during
        // configure(), so the local tables can die after build_system returns.
        auto tables = make_sms_host_tables(rt->state);

        const auto parsed = parse_manifest(manifest_toml(config.video_region, kind));

        // The SMS manifests declare no rom-backed regions (the cart is supplied
        // through the mapper, not a [[bus.region]] backing="rom"), so the
        // rom_provider is never consulted.
        const auto no_roms = [](std::string_view) -> std::optional<std::vector<std::uint8_t>> {
            return std::nullopt;
        };
        auto built =
            build_system(*parsed.value, no_roms, tables.callbacks, {}, tables.mmio_factories);
        rt->graph = std::move(*built.value);

        // Wire chip pointers from the constructed graph into the state the
        // callbacks already close over.
        rt->state.cpu = dynamic_cast<chips::cpu::z80*>(rt->graph.chip("cpu"));
        rt->state.vdp = dynamic_cast<chips::video::sms_vdp*>(rt->graph.chip("video"));
        rt->state.psg = dynamic_cast<chips::audio::sn76489*>(rt->graph.chip("audio"));

        // Attach the cartridge to whichever mapper the manifest instantiated.
        // The Sega mapper is also published in state.mapper because its $FFFC
        // register overlay (an mmio_factory) writes through it; the Codemasters
        // and Korean mappers bank via writes inside the cartridge region and
        // need no such back-reference.
        const std::span<const std::uint8_t> rom_span(rt->rom);
        if (rt->korean_msx_active) {
            auto* msx = dynamic_cast<chips::mapper::korean_msx_mapper*>(rt->graph.chip("mapper"));
            if (kind == sms_config::mapper::korean_msx_nemesis) {
                msx->set_variant(chips::mapper::korean_msx_mapper::variant::nemesis);
            }
            msx->attach_rom(rom_span);
        } else if (rt->korean_active) {
            auto* korean = dynamic_cast<chips::mapper::korean_mapper*>(rt->graph.chip("mapper"));
            korean->attach_rom(rom_span);
        } else if (rt->codemasters_active) {
            auto* codies =
                dynamic_cast<chips::mapper::codemasters_mapper*>(rt->graph.chip("mapper"));
            codies->attach_rom(rom_span);
        } else if (rt->korean_hicom_active) {
            // Published in state.hicom because its $FFFF register overlay (an
            // mmio_factory) writes through it -- the same back-reference the Sega
            // mapper needs for $FFFC-$FFFF.
            auto* hicom = dynamic_cast<chips::mapper::hicom_mapper*>(rt->graph.chip("mapper"));
            rt->state.hicom = hicom;
            hicom->attach_rom(rom_span);
        } else {
            auto* mapper = dynamic_cast<chips::mapper::sms_mapper*>(rt->graph.chip("mapper"));
            rt->state.mapper = mapper;
            mapper->attach_rom(rom_span);
        }

        // Post-BIOS stack pointer: the SMS BIOS sets SP=$DFF0 before handing off
        // to the cart. We don't boot the BIOS, so emulate its post-init SP (same
        // fixup assemble_sms applies) to keep early CALLs off the mapper page
        // registers at $FFFC-$FFFF.
        auto regs = rt->state.cpu->cpu_registers();
        regs.sp = 0xDFF0U;
        rt->state.cpu->set_registers(regs);

        // Default-plug an MK-3020 Master System Control Pad into both sockets,
        // matching assemble_sms. Adapters can swap peripherals later.
        rt->state.ports[0] = std::make_unique<peripheral::input::mk3020>();
        rt->state.ports[1] = std::make_unique<peripheral::input::mk3020>();

        return rt;
    }

} // namespace mnemos::manifests::sms
