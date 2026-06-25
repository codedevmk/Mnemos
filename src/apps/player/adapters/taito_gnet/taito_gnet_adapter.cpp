#include "taito_gnet_adapter.hpp"

#include "adapter_registry.hpp"
#include "crc32.hpp"

#include <cstddef>
#include <cstdio>
#include <memory>
#include <span>
#include <string_view>
#include <utility>

namespace mnemos::apps::player::adapters::taito_gnet {

    namespace {

        constexpr std::uint32_t taito_gnet_adapter_state_version = 1U;

        [[nodiscard]] std::string hex32(std::uint32_t value) {
            static constexpr char digits[] = "0123456789abcdef";
            std::string out(8U, '0');
            for (std::size_t i = 0; i < out.size(); ++i) {
                const auto shift = static_cast<unsigned>((out.size() - 1U - i) * 4U);
                out[i] = digits[(value >> shift) & 0x0FU];
            }
            return out;
        }

        [[nodiscard]] std::string crc32_hex(std::span<const std::uint8_t> bytes) {
            return hex32(mnemos::security::cryptography::crc32(bytes));
        }

        [[nodiscard]] frontend_sdk::session_capability_info make_session_capabilities() {
            frontend_sdk::session_capability_info session{};
            session.save_state_supported = true;
            session.frame_exact_save_state = true;
            return session;
        }

        [[nodiscard]] frontend_sdk::media_capability_info make_media_capabilities(
            const manifests::taito_gnet::taito_gnet_system& sys,
            std::string_view display_name) {
            frontend_sdk::media_capability_info media{};
            media.media.push_back(frontend_sdk::media_image_info{
                .id = "bios",
                .label = "G-NET BIOS",
                .residency = frontend_sdk::media_residency::resident,
                .byte_count = sys.bios.size(),
                .hash_algorithm = frontend_sdk::media_hash_algorithm::crc32,
                .full_hash = crc32_hex(std::span<const std::uint8_t>{sys.bios}),
                .provider_id = "taito_gnet.adapter",
                .revision = "loaded",
                .cache_hint = "resident"});

            for (std::size_t i = 0U; i < sys.flash_cards.size(); ++i) {
                const auto& card = sys.flash_cards[i];
                media.media.push_back(frontend_sdk::media_image_info{
                    .id = "flash_card_" + std::to_string(i),
                    .label = card.name.empty() ? (display_name.empty()
                                                      ? std::string{"Flash card"}
                                                      : std::string{display_name})
                                               : card.name,
                    .residency = frontend_sdk::media_residency::resident,
                    .byte_count = card.media.data.size(),
                    .page_size = card.media.info.unit_bytes,
                    .hash_algorithm = frontend_sdk::media_hash_algorithm::crc32,
                    .full_hash = crc32_hex(std::span<const std::uint8_t>{card.media.data}),
                    .provider_id = "taito_gnet.chd_flash_card",
                    .revision = "loaded",
                    .cache_hint = "resident"});
            }
            return media;
        }

        [[nodiscard]] std::string card_summary(
            const manifests::taito_gnet::taito_gnet_system& sys) {
            if (sys.flash_cards.empty()) {
                return "none";
            }
            std::uint64_t total = 0U;
            for (const auto& card : sys.flash_cards) {
                total += card.media.data.size();
            }
            return std::to_string(sys.flash_cards.size()) + " card(s), " +
                   std::to_string(total) + " bytes";
        }

    } // namespace

    taito_gnet_adapter::taito_gnet_adapter(std::vector<std::uint8_t> bios,
                                           std::vector<std::uint8_t> package,
                                           std::string display_name)
        : session_(make_session_capabilities()),
          sys_(manifests::taito_gnet::assemble_taito_gnet_from_package(
              std::move(bios), std::span<const std::uint8_t>{package})),
          framebuffer_(frame_width * frame_height, 0x00000000U) {
        if (sys_ == nullptr) {
            std::fprintf(stderr, "[taito_gnet] BIOS/package assembly failed\n");
            return;
        }

        media_ = make_media_capabilities(*sys_, display_name);
        chip_view_ = {&sys_->cpu};
        publish_memory_views();
        spec_ = {{"System", "Arcade"},
                 {"Board", "Taito G-NET / Sony ZN-2"},
                 {"Game", display_name.empty() ? std::string{"unknown"}
                                                : std::move(display_name)},
                 {"Media", card_summary(*sys_)},
                 {"Emulation", "board smoke: CPU/RAM/scratchpad/COP2 latch/GPU-OTC DMA/timer IRQ/flash/registers only"}};
        draw_placeholder_frame();
    }

    void taito_gnet_adapter::publish_memory_views() {
        if (sys_ == nullptr) {
            return;
        }

        const std::size_t view_count = 8U + sys_->flash_cards.size();
        memory_view_storage_.reserve(view_count);
        memory_view_names_.reserve(view_count);
        system_mem_view_.reserve(view_count);

        const auto publish = [this](std::string name, std::span<const std::uint8_t> bytes) {
            memory_view_names_.push_back(std::move(name));
            memory_view_storage_.push_back(
                std::make_unique<instrumentation::span_memory_view>(memory_view_names_.back(),
                                                                     bytes));
            system_mem_view_.push_back(memory_view_storage_.back().get());
        };

        publish("main_ram", std::span<const std::uint8_t>{sys_->main_ram});
        publish("scratchpad", std::span<const std::uint8_t>{sys_->scratchpad});
        publish("gpu_vram", std::span<const std::uint8_t>{sys_->gpu_vram});
        publish("firm_flash", std::span<const std::uint8_t>{sys_->firm_flash});
        publish("zoom_program_flash", std::span<const std::uint8_t>{sys_->zoom_program_flash});
        for (std::size_t i = 0U; i < sys_->wave_flash.size(); ++i) {
            publish("wave_flash_" + std::to_string(i),
                    std::span<const std::uint8_t>{sys_->wave_flash[i]});
        }
        for (std::size_t i = 0U; i < sys_->flash_cards.size(); ++i) {
            publish("flash_card_" + std::to_string(i),
                    std::span<const std::uint8_t>{sys_->flash_cards[i].media.data});
        }
    }

    void taito_gnet_adapter::draw_placeholder_frame() noexcept {
        if (framebuffer_.empty()) {
            return;
        }

        const auto regs = sys_ != nullptr ? sys_->cpu.cpu_registers()
                                          : mnemos::chips::cpu::r3000a::registers{};
        const std::uint32_t phase = static_cast<std::uint32_t>(frames_stepped_ & 0x0FU);
        const std::uint32_t pc_tint = (regs.pc >> 2U) & 0x3FU;

        for (std::uint32_t y = 0U; y < frame_height; ++y) {
            for (std::uint32_t x = 0U; x < frame_width; ++x) {
                const bool grid = (((x >> 4U) + (y >> 4U) + phase) & 1U) != 0U;
                const std::uint32_t base = grid ? 0x00182028U : 0x000A1016U;
                const std::uint32_t signal =
                    ((x + pc_tint) & 0x1FU) == 0U ? 0x00305020U : 0x00000000U;
                framebuffer_[static_cast<std::size_t>(y) * frame_width + x] = base | signal;
            }
        }
    }

    void taito_gnet_adapter::step_one_frame() {
        if (sys_ != nullptr) {
            sys_->step_instructions(instructions_per_frame);
        }
        ++frames_stepped_;
        draw_placeholder_frame();
    }

    void taito_gnet_adapter::apply_input(
        int port,
        const frontend_sdk::controller_state& state) noexcept {
        (void)port;
        (void)state;
    }

    std::vector<std::uint8_t> taito_gnet_adapter::save_state() {
        return runtime::write_save_state(build_save_target(*this));
    }

    runtime::load_result taito_gnet_adapter::load_state(std::span<const std::uint8_t> data) {
        runtime::save_target target = build_save_target(*this);
        const runtime::load_result result = runtime::read_save_state(data, target);
        if (result.ok()) {
            draw_placeholder_frame();
        }
        return result;
    }

    void force_link() noexcept {}

    runtime::save_target build_save_target(taito_gnet_adapter& adapter) {
        runtime::save_target target;
        target.manifest_id = "taito_gnet.adapter";
        target.manifest_rev = 1U;
        target.master_cycle =
            adapter.sys_ != nullptr ? adapter.sys_->cpu.elapsed_cycles() : 0U;
        target.components.push_back(
            {"board",
             [&adapter](chips::state_writer& writer) {
                 if (adapter.sys_ != nullptr) {
                     adapter.sys_->save_state(writer);
                 }
             },
             [&adapter](chips::state_reader& reader) {
                 if (adapter.sys_ != nullptr) {
                     adapter.sys_->load_state(reader);
                 } else {
                     reader.fail();
                 }
             }});
        target.components.push_back(
            {"adapter",
             [&adapter](chips::state_writer& writer) {
                 writer.u32(taito_gnet_adapter_state_version);
                 writer.u64(adapter.frames_stepped_);
             },
             [&adapter](chips::state_reader& reader) {
                 const std::uint32_t version = reader.u32();
                 if (version == 0U || version > taito_gnet_adapter_state_version) {
                     reader.fail();
                     return;
                 }
                 adapter.frames_stepped_ = reader.u64();
             }});
        return target;
    }

    namespace {
        const auto register_taito_gnet = [] {
            mnemos::frontend_sdk::adapter_registry::instance().register_family(
                "taito_gnet",
                [](mnemos::frontend_sdk::adapter_options opts)
                    -> std::unique_ptr<mnemos::frontend_sdk::player_system> {
                    if (opts.rom.empty() || opts.bios_images.empty() ||
                        opts.bios_images.front().empty()) {
                        return nullptr;
                    }
                    auto adapter = std::make_unique<taito_gnet_adapter>(
                        std::move(opts.bios_images.front()), std::move(opts.rom),
                        std::move(opts.display_name));
                    if (!adapter->valid()) {
                        return nullptr;
                    }
                    return adapter;
                });
            return 0;
        }();
    } // namespace

} // namespace mnemos::apps::player::adapters::taito_gnet
