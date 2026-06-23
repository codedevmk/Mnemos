#include "asset_views.hpp"
#include "audio_views.hpp"
#include "capability_discovery.hpp"
#include "introspection_views.hpp"
#include "player_system.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

    using mnemos::chips::chip_class;
    using mnemos::chips::chip_metadata;
    using mnemos::chips::frame_buffer_view;
    using mnemos::chips::ichip;
    using mnemos::chips::register_descriptor;
    using mnemos::chips::reset_kind;
    using mnemos::chips::state_reader;
    using mnemos::chips::state_writer;
    using mnemos::debug::build_capability_control_summary;
    using mnemos::debug::capability_descriptor;
    using mnemos::debug::capability_kind;
    using mnemos::debug::capability_manifest;
    using mnemos::debug::capability_provider_api_version;
    using mnemos::debug::capability_provider_registration;
    using mnemos::debug::capability_provider_registry;
    using mnemos::debug::capability_scope;
    using mnemos::debug::capability_state;
    using mnemos::debug::collect_registered_capabilities;
    using mnemos::debug::diagnostic_severity;
    using mnemos::debug::discover_dump_capabilities;
    using mnemos::debug::format_capability_summary;
    using mnemos::debug::make_capability_descriptor;
    using mnemos::debug::make_diagnostic;
    using mnemos::debug::make_opaque_capability_descriptor;
    using mnemos::debug::merge_capability_manifests;
    using mnemos::debug::to_string;
    using mnemos::frontend_sdk::audio_chunk;
    using mnemos::frontend_sdk::controller_state;
    using mnemos::frontend_sdk::input_device_format;
    using mnemos::frontend_sdk::media_capability_info;
    using mnemos::frontend_sdk::media_hash_algorithm;
    using mnemos::frontend_sdk::media_image_info;
    using mnemos::frontend_sdk::media_page_hash;
    using mnemos::frontend_sdk::media_residency;
    using mnemos::frontend_sdk::player_system;
    using mnemos::frontend_sdk::session_capability_info;
    using mnemos::frontend_sdk::session_input_port;
    using mnemos::frontend_sdk::spec_field;
    using mnemos::frontend_sdk::video_region;
    using mnemos::instrumentation::asset_source;
    using mnemos::instrumentation::audio_source;
    using mnemos::instrumentation::debug_layer;
    using mnemos::instrumentation::ichip_introspection;
    using mnemos::instrumentation::memory_view;
    using mnemos::instrumentation::palette_view;
    using mnemos::instrumentation::reg_write_trace;
    using mnemos::instrumentation::register_view;
    using mnemos::instrumentation::sample_view;
    using mnemos::instrumentation::span_memory_view;
    using mnemos::instrumentation::trace_target;

    [[nodiscard]] capability_manifest
    manifest_with(std::initializer_list<capability_descriptor> descriptors) {
        capability_manifest manifest{};
        manifest.producer = {.id = "test.provider", .version = 1U};
        manifest.capabilities.assign(descriptors.begin(), descriptors.end());
        return manifest;
    }

    [[nodiscard]] std::vector<std::string>
    descriptor_order(const std::vector<capability_descriptor>& descriptors) {
        std::vector<std::string> order;
        order.reserve(descriptors.size());
        for (const auto& descriptor : descriptors) {
            order.push_back(std::string{descriptor.kind_name} + ":" + descriptor.id + ":" +
                            descriptor.provider);
        }
        return order;
    }

    [[nodiscard]] const capability_descriptor* find_descriptor(const capability_manifest& manifest,
                                                               std::string_view id) {
        for (const auto& descriptor : manifest.capabilities) {
            if (descriptor.id == id) {
                return &descriptor;
            }
        }
        return nullptr;
    }

    [[nodiscard]] bool has_diagnostic(const capability_manifest& manifest, std::string_view code) {
        for (const auto& diag : manifest.diagnostics) {
            if (diag.code == code) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] std::string_view payload_value(const capability_descriptor& descriptor,
                                                 std::string_view key) {
        for (const auto& field : descriptor.payload) {
            if (field.key == key) {
                return field.value;
            }
        }
        return {};
    }

    struct provider_context {
        std::string descriptor_provider{};
        std::string descriptor_id{};
        capability_kind kind{capability_kind::asset};
        std::size_t calls{0U};
    };

    bool collect_descriptor_provider(void* raw, capability_manifest& out,
                                     std::vector<mnemos::debug::diagnostic>&) noexcept {
        auto& context = *static_cast<provider_context*>(raw);
        ++context.calls;
        out.capabilities.push_back(
            make_capability_descriptor(context.kind, context.descriptor_provider,
                                       context.descriptor_id, capability_scope::host));
        return true;
    }

    bool collect_failing_provider(void* raw, capability_manifest&,
                                  std::vector<mnemos::debug::diagnostic>& diagnostics) noexcept {
        auto& context = *static_cast<provider_context*>(raw);
        ++context.calls;
        diagnostics.push_back(make_diagnostic(
            "provider.inner.failure", diagnostic_severity::warning, context.descriptor_id,
            "provider.inner.failure", "provider supplied a failure diagnostic"));
        return false;
    }

    class fake_trace final : public trace_target {
      public:
        void install(callback) override {}
    };

    class fake_reg_trace final : public reg_write_trace {
      public:
        void install(callback) override {}
    };

    class fake_registers final : public register_view {
      public:
        [[nodiscard]] std::span<const register_descriptor> registers() override { return regs_; }

      private:
        std::array<register_descriptor, 1> regs_{
            register_descriptor{.name = "PC", .value = 0x1234U, .bit_width = 24U}};
    };

    class fake_assets final : public asset_source {
      public:
        [[nodiscard]] std::span<const palette_view> palettes() const override { return {}; }
        [[nodiscard]] std::span<const mnemos::instrumentation::graphic_asset>
        graphics() const override {
            return {};
        }
    };

    class fake_audio final : public audio_source {
      public:
        [[nodiscard]] std::span<const sample_view> samples() const override { return {}; }
    };

    class fake_layer final : public debug_layer {
      public:
        [[nodiscard]] std::string_view name() const noexcept override { return "plane_a"; }
        [[nodiscard]] frame_buffer_view view() const override {
            return {.pixels = pixels_.data(), .width = 1U, .height = 1U, .stride = 0U};
        }

      private:
        std::array<std::uint32_t, 1> pixels_{0x010203U};
    };

    class cpu_intro final : public ichip_introspection {
      public:
        [[nodiscard]] register_view* registers() override { return &registers_; }
        [[nodiscard]] trace_target* trace() override { return &trace_; }

      private:
        fake_registers registers_;
        fake_trace trace_;
    };

    class video_intro final : public ichip_introspection {
      public:
        video_intro() { layer_table_[0] = &layer_; }

        [[nodiscard]] std::span<memory_view* const> memory_views() override {
            return memory_table_;
        }
        [[nodiscard]] std::span<debug_layer* const> debug_layers() override { return layer_table_; }
        [[nodiscard]] asset_source* assets() override { return &assets_; }

      private:
        std::array<std::uint8_t, 2> vram_{0x11U, 0x22U};
        span_memory_view vram_view_{"vram", vram_};
        std::array<memory_view*, 1> memory_table_{&vram_view_};
        fake_layer layer_;
        std::array<debug_layer*, 1> layer_table_{};
        fake_assets assets_;
    };

    class psg_intro final : public ichip_introspection {
      public:
        [[nodiscard]] register_view* registers() override { return &registers_; }
        [[nodiscard]] reg_write_trace* reg_writes() override { return &trace_; }
        [[nodiscard]] audio_source* audio() override { return &audio_; }

      private:
        fake_registers registers_;
        fake_reg_trace trace_;
        fake_audio audio_;
    };

    class fake_chip final : public ichip {
      public:
        fake_chip(chip_metadata metadata, ichip_introspection& intro)
            : metadata_(metadata), intro_(intro) {}

        [[nodiscard]] chip_metadata metadata() const noexcept override { return metadata_; }
        void tick(std::uint64_t) override {}
        void reset(reset_kind) override {}
        void save_state(state_writer&) const override {}
        void load_state(state_reader&) override {}
        [[nodiscard]] ichip_introspection& introspection() noexcept override { return intro_; }

      private:
        chip_metadata metadata_;
        ichip_introspection& intro_;
    };

    class rich_dump_system final : public player_system {
      public:
        rich_dump_system()
            : cpu_({.manufacturer = "test",
                    .part_number = "main-cpu",
                    .family = "test",
                    .klass = chip_class::cpu,
                    .revision = 1U},
                   cpu_intro_),
              video_({.manufacturer = "test",
                      .part_number = "vdp-1",
                      .family = "test",
                      .klass = chip_class::video,
                      .revision = 1U},
                     video_intro_),
              psg_({.manufacturer = "test",
                    .part_number = "SN76489",
                    .family = "test",
                    .klass = chip_class::audio_synth,
                    .revision = 1U},
                   psg_intro_) {
            chips_[0] = &cpu_;
            chips_[1] = &video_;
            chips_[2] = &psg_;
            system_memory_[0] = &work_ram_;
        }

        [[nodiscard]] video_region region() const noexcept override { return {60000U}; }
        [[nodiscard]] const std::vector<spec_field>& system_spec() const noexcept override {
            return spec_;
        }
        [[nodiscard]] frame_buffer_view current_frame() const noexcept override {
            return {.pixels = pixels_.data(), .width = 2U, .height = 1U, .stride = 0U};
        }
        void step_one_frame() override {}
        void apply_input(int, const controller_state&) noexcept override {}
        [[nodiscard]] audio_chunk drain_audio() noexcept override { return {}; }
        [[nodiscard]] std::span<ichip* const> chips() const noexcept override { return chips_; }
        [[nodiscard]] std::span<memory_view* const> memory_views() const noexcept override {
            return system_memory_;
        }

      private:
        std::vector<spec_field> spec_{};
        std::array<std::uint32_t, 2> pixels_{0x010203U, 0x040506U};
        std::array<std::uint8_t, 2> ram_{0xAAU, 0x55U};
        span_memory_view work_ram_{"work_ram", ram_};
        std::array<memory_view*, 1> system_memory_{};
        cpu_intro cpu_intro_;
        video_intro video_intro_;
        psg_intro psg_intro_;
        fake_chip cpu_;
        fake_chip video_;
        fake_chip psg_;
        std::array<ichip*, 3> chips_{};
    };

    class session_system final : public player_system {
      public:
        explicit session_system(session_capability_info session) : session_(std::move(session)) {}

        [[nodiscard]] video_region region() const noexcept override { return {60000U}; }
        [[nodiscard]] const std::vector<spec_field>& system_spec() const noexcept override {
            return spec_;
        }
        [[nodiscard]] frame_buffer_view current_frame() const noexcept override { return {}; }
        void step_one_frame() override {}
        void apply_input(int, const controller_state&) noexcept override {}
        [[nodiscard]] const session_capability_info&
        session_capabilities() const noexcept override {
            return session_;
        }
        [[nodiscard]] audio_chunk drain_audio() noexcept override { return {}; }

      private:
        session_capability_info session_{};
        std::vector<spec_field> spec_{};
    };

    class media_system final : public player_system {
      public:
        explicit media_system(media_capability_info media) : media_(std::move(media)) {}

        [[nodiscard]] video_region region() const noexcept override { return {60000U}; }
        [[nodiscard]] const std::vector<spec_field>& system_spec() const noexcept override {
            return spec_;
        }
        [[nodiscard]] frame_buffer_view current_frame() const noexcept override { return {}; }
        void step_one_frame() override {}
        void apply_input(int, const controller_state&) noexcept override {}
        [[nodiscard]] const media_capability_info& media_capabilities() const noexcept override {
            return media_;
        }
        [[nodiscard]] audio_chunk drain_audio() noexcept override { return {}; }

      private:
        media_capability_info media_{};
        std::vector<spec_field> spec_{};
    };

} // namespace

TEST_CASE("capability manifest merge orders descriptors deterministically",
          "[capability_discovery]") {
    capability_manifest a = manifest_with({
        make_capability_descriptor(capability_kind::debug, "provider.z", "screenshot"),
        make_capability_descriptor(capability_kind::asset, "provider.b", "tiles"),
    });
    capability_manifest b = manifest_with({
        make_capability_descriptor(capability_kind::audio, "provider.a", "pcm"),
        make_capability_descriptor(capability_kind::asset, "provider.a", "layers"),
    });

    const std::array manifests{a, b};
    const auto merged = merge_capability_manifests(manifests);

    REQUIRE(merged.capabilities.size() == 4U);
    CHECK(descriptor_order(merged.capabilities) ==
          std::vector<std::string>{"asset:layers:provider.a", "asset:tiles:provider.b",
                                   "audio:pcm:provider.a", "debug:screenshot:provider.z"});
    CHECK(merged.producer.id == "mnemos.debug.capability_discovery");
    CHECK(merged.schema == mnemos::debug::capability_manifest_schema_version);
}

TEST_CASE("capability manifest merge rejects duplicate provider id pairs",
          "[capability_discovery]") {
    capability_manifest a = manifest_with({
        make_capability_descriptor(capability_kind::asset, "provider.a", "same-id"),
    });
    capability_manifest b = manifest_with({
        make_capability_descriptor(capability_kind::memory, "provider.a", "same-id"),
    });

    const std::array manifests{a, b};
    const auto merged = merge_capability_manifests(manifests);

    CHECK(merged.capabilities.empty());
    REQUIRE(merged.diagnostics.size() == 1U);
    CHECK(merged.diagnostics[0].severity == diagnostic_severity::error);
    CHECK(merged.diagnostics[0].capability_id == "same-id");
    CHECK(merged.diagnostics[0].code == "capability.duplicate");
}

TEST_CASE("capability manifest merge keeps unavailable and degraded descriptors visible",
          "[capability_discovery]") {
    auto unavailable =
        make_capability_descriptor(capability_kind::media, capability_state::unavailable,
                                   "provider.media", "paged-rom", capability_scope::media);
    unavailable.diagnostics.push_back(
        make_diagnostic("media.provider.unavailable", diagnostic_severity::warning, "paged-rom",
                        "media.provider.unavailable", "test provider offline"));

    auto degraded =
        make_capability_descriptor(capability_kind::session, capability_state::degraded,
                                   "provider.session", "lockstep", capability_scope::session);

    const std::array manifests{manifest_with({unavailable, degraded})};
    const auto merged = merge_capability_manifests(manifests);

    REQUIRE(merged.capabilities.size() == 2U);
    CHECK(merged.capabilities[0].id == "paged-rom");
    CHECK(merged.capabilities[0].state == capability_state::unavailable);
    REQUIRE(merged.capabilities[0].diagnostics.size() == 1U);
    CHECK(merged.capabilities[0].diagnostics[0].code == "media.provider.unavailable");

    CHECK(merged.capabilities[1].id == "lockstep");
    CHECK(merged.capabilities[1].state == capability_state::degraded);
}

TEST_CASE("capability manifest merge preserves opaque unknown capabilities",
          "[capability_discovery]") {
    auto custom = make_opaque_capability_descriptor("shader_overlay", "plugin.video",
                                                    "scanline-mask", capability_scope::host,
                                                    capability_state::experimental, 7U);
    custom.requirements.push_back({.id = "host.gpu.debug"});
    custom.payload.push_back({.key = "format", .value = "opaque/v1"});

    const std::array manifests{manifest_with({custom})};
    const auto merged = merge_capability_manifests(manifests);

    REQUIRE(merged.capabilities.size() == 1U);
    const auto& descriptor = merged.capabilities[0];
    CHECK(descriptor.kind == capability_kind::unknown);
    CHECK(descriptor.kind_name == "shader_overlay");
    CHECK(descriptor.provider == "plugin.video");
    CHECK(descriptor.id == "scanline-mask");
    CHECK(descriptor.version == 7U);
    CHECK(descriptor.state == capability_state::experimental);
    REQUIRE(descriptor.requirements.size() == 1U);
    CHECK(descriptor.requirements[0].id == "host.gpu.debug");
    REQUIRE(descriptor.payload.size() == 1U);
    CHECK(descriptor.payload[0].key == "format");
    CHECK(descriptor.payload[0].value == "opaque/v1");
    CHECK(to_string(descriptor.scope) == "host");
}

TEST_CASE("capability provider registry collects deterministically and annotates provenance",
          "[capability_discovery][providers]") {
    provider_context beta{.descriptor_provider = "plugin.beta",
                          .descriptor_id = "asset.beta",
                          .kind = capability_kind::asset};
    provider_context alpha{.descriptor_provider = "plugin.alpha",
                           .descriptor_id = "asset.alpha",
                           .kind = capability_kind::asset};

    capability_provider_registry registry;
    registry.register_provider(
        capability_provider_registration{.id = "provider.beta",
                                         .provenance = {.pack_id = "pack.beta",
                                                        .pack_version = "2.0",
                                                        .plugin_id = "plugin.beta",
                                                        .plugin_version = "2.1",
                                                        .origin = "test-pack"},
                                         .context = &beta,
                                         .collect = &collect_descriptor_provider});
    registry.register_provider(
        capability_provider_registration{.id = "provider.alpha",
                                         .provenance = {.pack_id = "pack.alpha",
                                                        .pack_version = "1.0",
                                                        .plugin_id = "plugin.alpha",
                                                        .plugin_version = "1.1",
                                                        .origin = "test-pack"},
                                         .context = &alpha,
                                         .collect = &collect_descriptor_provider});

    const auto manifest = collect_registered_capabilities(registry);

    REQUIRE(manifest.capabilities.size() == 2U);
    CHECK(
        descriptor_order(manifest.capabilities) ==
        std::vector<std::string>{"asset:asset.alpha:plugin.alpha", "asset:asset.beta:plugin.beta"});
    CHECK(alpha.calls == 1U);
    CHECK(beta.calls == 1U);

    const auto* descriptor = find_descriptor(manifest, "asset.alpha");
    REQUIRE(descriptor != nullptr);
    CHECK(payload_value(*descriptor, "provider_registration_id") == "provider.alpha");
    CHECK(payload_value(*descriptor, "provider_api_version") == "1");
    CHECK(payload_value(*descriptor, "provider_pack_id") == "pack.alpha");
    CHECK(payload_value(*descriptor, "provider_pack_version") == "1.0");
    CHECK(payload_value(*descriptor, "provider_plugin_id") == "plugin.alpha");
    CHECK(payload_value(*descriptor, "provider_plugin_version") == "1.1");
    CHECK(payload_value(*descriptor, "provider_origin") == "test-pack");
}

TEST_CASE("capability provider registry skips disabled stale and unsatisfied providers",
          "[capability_discovery][providers]") {
    provider_context disabled{.descriptor_provider = "plugin.disabled",
                              .descriptor_id = "asset.disabled"};
    provider_context stale{.descriptor_provider = "plugin.stale", .descriptor_id = "asset.stale"};
    provider_context gated{.descriptor_provider = "plugin.gated", .descriptor_id = "asset.gated"};

    capability_provider_registry registry;
    registry.register_provider(
        capability_provider_registration{.id = "provider.disabled",
                                         .enabled = false,
                                         .context = &disabled,
                                         .collect = &collect_descriptor_provider});
    registry.register_provider(
        capability_provider_registration{.id = "provider.stale",
                                         .api_version = capability_provider_api_version + 1U,
                                         .context = &stale,
                                         .collect = &collect_descriptor_provider});
    registry.register_provider(
        capability_provider_registration{.id = "provider.gated",
                                         .requirements = {{.id = "runtime.save_state"}},
                                         .context = &gated,
                                         .collect = &collect_descriptor_provider});

    const auto skipped = collect_registered_capabilities(registry);

    CHECK(disabled.calls == 0U);
    CHECK(stale.calls == 0U);
    CHECK(gated.calls == 0U);
    CHECK(find_descriptor(skipped, "asset.gated") == nullptr);
    CHECK(has_diagnostic(skipped, "capability.provider.disabled"));
    CHECK(has_diagnostic(skipped, "capability.provider.stale_version"));
    CHECK(has_diagnostic(skipped, "capability.provider.requirement_unsatisfied"));

    const std::array<std::string, 1> satisfied{"runtime.save_state"};
    const auto collected = collect_registered_capabilities(registry, satisfied);

    CHECK(disabled.calls == 0U);
    CHECK(stale.calls == 0U);
    CHECK(gated.calls == 1U);
    REQUIRE(find_descriptor(collected, "asset.gated") != nullptr);
}

TEST_CASE("capability provider registry reports missing callbacks and collect failures",
          "[capability_discovery][providers]") {
    provider_context failing{.descriptor_provider = "plugin.failure",
                             .descriptor_id = "asset.failure"};

    capability_provider_registry registry;
    registry.register_provider(capability_provider_registration{.id = "provider.missing"});
    registry.register_provider(capability_provider_registration{
        .id = "provider.failure", .context = &failing, .collect = &collect_failing_provider});

    const auto manifest = collect_registered_capabilities(registry);

    CHECK(failing.calls == 1U);
    CHECK(find_descriptor(manifest, "asset.failure") == nullptr);
    CHECK(has_diagnostic(manifest, "capability.provider.collect_missing"));
    CHECK(has_diagnostic(manifest, "capability.provider.collect_failed"));
    CHECK(has_diagnostic(manifest, "provider.inner.failure"));
}

TEST_CASE("capability provider registry routes duplicates through manifest merge",
          "[capability_discovery][providers]") {
    provider_context first{.descriptor_provider = "plugin.duplicate",
                           .descriptor_id = "asset.same"};
    provider_context second{.descriptor_provider = "plugin.duplicate",
                            .descriptor_id = "asset.same"};

    capability_provider_registry registry;
    registry.register_provider(capability_provider_registration{
        .id = "provider.first", .context = &first, .collect = &collect_descriptor_provider});
    registry.register_provider(capability_provider_registration{
        .id = "provider.second", .context = &second, .collect = &collect_descriptor_provider});

    const auto manifest = collect_registered_capabilities(registry);

    CHECK(first.calls == 1U);
    CHECK(second.calls == 1U);
    CHECK(find_descriptor(manifest, "asset.same") == nullptr);
    CHECK(has_diagnostic(manifest, "capability.duplicate"));
}

TEST_CASE("capability summaries derive frontend controls from state and diagnostics",
          "[capability_discovery][summary]") {
    auto screenshot =
        make_capability_descriptor(capability_kind::debug, "provider.debug", "debug.screenshot");
    auto rollback = make_capability_descriptor(capability_kind::session, capability_state::degraded,
                                               "provider.session", "session.mode.rollback",
                                               capability_scope::session);
    rollback.diagnostics.push_back(
        make_diagnostic("session.rollback.degraded", diagnostic_severity::warning, rollback.id,
                        "session.rollback.degraded", "save states are not frame exact"));
    auto audio = make_capability_descriptor(capability_kind::audio, capability_state::unavailable,
                                            "provider.audio", "audio.samples");
    audio.diagnostics.push_back(
        make_diagnostic("audio.provider.unavailable", diagnostic_severity::warning, audio.id,
                        "audio.provider.unavailable", "samples not exposed"));

    capability_manifest manifest = manifest_with({screenshot, rollback, audio});
    manifest.diagnostics.push_back(rollback.diagnostics.front());
    manifest.diagnostics.push_back(audio.diagnostics.front());
    const std::array manifests{manifest};
    const auto merged = merge_capability_manifests(manifests);

    const auto controls = build_capability_control_summary(merged);

    REQUIRE(controls.size() == 3U);
    CHECK(controls[0].id == "audio.samples");
    CHECK_FALSE(controls[0].visible);
    CHECK_FALSE(controls[0].enabled);
    CHECK(controls[0].explanation == "audio.provider.unavailable: samples not exposed");
    CHECK(controls[1].id == "debug.screenshot");
    CHECK(controls[1].visible);
    CHECK(controls[1].enabled);
    CHECK(controls[1].explanation.empty());
    CHECK(controls[2].id == "session.mode.rollback");
    CHECK(controls[2].visible);
    CHECK_FALSE(controls[2].enabled);
    CHECK(controls[2].explanation == "session.rollback.degraded: save states are not frame exact");

    CHECK(format_capability_summary(merged) ==
          "schema=1 producer=mnemos.debug.capability_discovery@1 capabilities=3 diagnostics=2\n"
          "capability audio audio.samples state=unavailable control=hidden scope=system "
          "provider=provider.audio explanation=audio.provider.unavailable: samples not exposed\n"
          "capability debug debug.screenshot state=available control=enabled scope=system "
          "provider=provider.debug\n"
          "capability session session.mode.rollback state=degraded control=disabled scope=session "
          "provider=provider.session explanation=session.rollback.degraded: save states are not "
          "frame exact\n"
          "diagnostic warning audio.provider.unavailable capability=audio.samples detail=samples "
          "not exposed\n"
          "diagnostic warning session.rollback.degraded capability=session.mode.rollback "
          "detail=save states are not frame exact\n");
}

TEST_CASE("dump capability discovery reports existing export surfaces", "[capability_discovery]") {
    rich_dump_system sys;

    const auto manifest = discover_dump_capabilities(sys);

    REQUIRE(find_descriptor(manifest, "debug.screenshot") != nullptr);
    CHECK(find_descriptor(manifest, "debug.screenshot")->state == capability_state::available);
    CHECK(find_descriptor(manifest, "debug.main_cpu.cpu_trace") != nullptr);
    CHECK(find_descriptor(manifest, "memory.system.work_ram") != nullptr);
    CHECK(find_descriptor(manifest, "memory.main_cpu.registers") != nullptr);
    CHECK(find_descriptor(manifest, "memory.vdp_1.vram") != nullptr);
    CHECK(find_descriptor(manifest, "memory.sn76489.registers") != nullptr);
    CHECK(find_descriptor(manifest, "asset.vdp_1.graphics") != nullptr);
    CHECK(find_descriptor(manifest, "asset.vdp_1.layer.plane_a") != nullptr);
    CHECK(find_descriptor(manifest, "audio.sn76489.samples") != nullptr);
    CHECK(find_descriptor(manifest, "audio.sn76489.reg_write_vgm") != nullptr);
    CHECK_FALSE(has_diagnostic(manifest, "debug.chips.empty"));
    CHECK_FALSE(has_diagnostic(manifest, "debug.introspection.empty"));
}

TEST_CASE("dump capability discovery diagnoses a system without framebuffer or chips",
          "[capability_discovery]") {
    class empty_system final : public player_system {
      public:
        [[nodiscard]] video_region region() const noexcept override { return {60000U}; }
        [[nodiscard]] const std::vector<spec_field>& system_spec() const noexcept override {
            return spec_;
        }
        [[nodiscard]] frame_buffer_view current_frame() const noexcept override { return {}; }
        void step_one_frame() override {}
        void apply_input(int, const controller_state&) noexcept override {}
        [[nodiscard]] audio_chunk drain_audio() noexcept override { return {}; }

      private:
        std::vector<spec_field> spec_{};
    };

    empty_system sys;
    const auto manifest = discover_dump_capabilities(sys);

    const auto* screenshot = find_descriptor(manifest, "debug.screenshot");
    REQUIRE(screenshot != nullptr);
    CHECK(screenshot->state == capability_state::unavailable);
    CHECK(has_diagnostic(manifest, "debug.framebuffer.unavailable"));
    CHECK(has_diagnostic(manifest, "debug.chips.empty"));
}

TEST_CASE("dump capability discovery diagnoses chips with no introspection surfaces",
          "[capability_discovery]") {
    class plain_system final : public player_system {
      public:
        plain_system()
            : chip_({.manufacturer = "test",
                     .part_number = "plain",
                     .family = "test",
                     .klass = chip_class::peripheral,
                     .revision = 1U},
                    intro_) {
            chips_[0] = &chip_;
        }

        [[nodiscard]] video_region region() const noexcept override { return {60000U}; }
        [[nodiscard]] const std::vector<spec_field>& system_spec() const noexcept override {
            return spec_;
        }
        [[nodiscard]] frame_buffer_view current_frame() const noexcept override {
            return {.pixels = pixels_.data(), .width = 1U, .height = 1U, .stride = 0U};
        }
        void step_one_frame() override {}
        void apply_input(int, const controller_state&) noexcept override {}
        [[nodiscard]] audio_chunk drain_audio() noexcept override { return {}; }
        [[nodiscard]] std::span<ichip* const> chips() const noexcept override { return chips_; }

      private:
        std::vector<spec_field> spec_{};
        std::array<std::uint32_t, 1> pixels_{0x010203U};
        ichip_introspection intro_{};
        fake_chip chip_;
        std::array<ichip*, 1> chips_{};
    };

    plain_system sys;
    const auto manifest = discover_dump_capabilities(sys);

    REQUIRE(find_descriptor(manifest, "debug.screenshot") != nullptr);
    CHECK(find_descriptor(manifest, "debug.screenshot")->state == capability_state::available);
    CHECK(has_diagnostic(manifest, "debug.introspection.empty"));
}

TEST_CASE("session capability discovery reports local-only input metadata",
          "[capability_discovery][session]") {
    session_capability_info session{};
    session.input_ports.push_back(session_input_port{.port_index = 0U,
                                                     .player_slot = 1U,
                                                     .format = input_device_format::digital_pad,
                                                     .device_id = "mk1650",
                                                     .label = "P1",
                                                     .connected = true});
    session.deterministic_frame_input = false;

    session_system sys{std::move(session)};
    const auto manifest = discover_dump_capabilities(sys);

    const auto* local = find_descriptor(manifest, "session.mode.local");
    REQUIRE(local != nullptr);
    CHECK(local->state == capability_state::available);
    CHECK(payload_value(*local, "port_count") == "1");

    const auto* port = find_descriptor(manifest, "session.input.port.0");
    REQUIRE(port != nullptr);
    CHECK(port->state == capability_state::available);
    CHECK(payload_value(*port, "format") == "digital_pad");
    CHECK(payload_value(*port, "player_slot") == "1");

    const auto* lockstep = find_descriptor(manifest, "session.mode.lockstep");
    REQUIRE(lockstep != nullptr);
    CHECK(lockstep->state == capability_state::unavailable);
    CHECK(has_diagnostic(manifest, "session.lockstep.unavailable"));
}

TEST_CASE("session capability discovery reports lockstep and input-delay readiness",
          "[capability_discovery][session]") {
    session_capability_info session{};
    session.input_ports.push_back(session_input_port{.port_index = 0U,
                                                     .player_slot = 1U,
                                                     .format = input_device_format::arcade_panel,
                                                     .device_id = "cps1-p1",
                                                     .label = "P1",
                                                     .connected = true});
    session.input_ports.push_back(session_input_port{.port_index = 1U,
                                                     .player_slot = 2U,
                                                     .format = input_device_format::arcade_panel,
                                                     .device_id = "cps1-p2",
                                                     .label = "P2",
                                                     .connected = true});
    session.deterministic_frame_input = true;
    session.max_input_delay_frames = 8U;

    session_system sys{std::move(session)};
    const auto manifest = discover_dump_capabilities(sys);

    const auto* lockstep = find_descriptor(manifest, "session.mode.lockstep");
    REQUIRE(lockstep != nullptr);
    CHECK(lockstep->state == capability_state::available);
    CHECK(payload_value(*lockstep, "deterministic_frame_input") == "true");

    const auto* input_delay = find_descriptor(manifest, "session.mode.input_delay");
    REQUIRE(input_delay != nullptr);
    CHECK(input_delay->state == capability_state::available);
    CHECK(payload_value(*input_delay, "max_input_delay_frames") == "8");

    const auto* rollback = find_descriptor(manifest, "session.mode.rollback");
    REQUIRE(rollback != nullptr);
    CHECK(rollback->state == capability_state::unavailable);
    CHECK(payload_value(*rollback, "save_state_supported") == "false");
    CHECK(has_diagnostic(manifest, "session.rollback.unavailable"));
}

TEST_CASE("session capability discovery reports rollback readiness from save-state facts",
          "[capability_discovery][session]") {
    session_capability_info session{};
    session.input_ports.push_back(session_input_port{.port_index = 0U,
                                                     .player_slot = 1U,
                                                     .format = input_device_format::digital_pad,
                                                     .device_id = "pad",
                                                     .label = "P1",
                                                     .connected = true});
    session.deterministic_frame_input = true;
    session.save_state_supported = true;
    session.frame_exact_save_state = true;
    session.max_input_delay_frames = 4U;

    session_system sys{std::move(session)};
    const auto manifest = discover_dump_capabilities(sys);

    const auto* rollback = find_descriptor(manifest, "session.mode.rollback");
    REQUIRE(rollback != nullptr);
    CHECK(rollback->state == capability_state::available);
    CHECK(payload_value(*rollback, "save_state_supported") == "true");
    CHECK(payload_value(*rollback, "frame_exact_save_state") == "true");
    CHECK_FALSE(has_diagnostic(manifest, "session.rollback.unavailable"));
}

TEST_CASE("session capability discovery diagnoses missing ports and unknown device shapes",
          "[capability_discovery][session]") {
    SECTION("missing metadata") {
        session_system sys{session_capability_info{}};
        const auto manifest = discover_dump_capabilities(sys);

        const auto* local = find_descriptor(manifest, "session.mode.local");
        REQUIRE(local != nullptr);
        CHECK(local->state == capability_state::degraded);
        CHECK(has_diagnostic(manifest, "session.input.ports_missing"));
    }

    SECTION("unknown device format") {
        session_capability_info session{};
        session.input_ports.push_back(session_input_port{.port_index = 3U,
                                                         .player_slot = 1U,
                                                         .format = input_device_format::unknown,
                                                         .device_id = "custom",
                                                         .label = "Expansion",
                                                         .connected = true});
        session.deterministic_frame_input = true;

        session_system sys{std::move(session)};
        const auto manifest = discover_dump_capabilities(sys);

        const auto* port = find_descriptor(manifest, "session.input.port.3");
        REQUIRE(port != nullptr);
        CHECK(port->state == capability_state::degraded);
        CHECK(payload_value(*port, "format") == "unknown");
        CHECK(has_diagnostic(manifest, "session.input.device_unknown"));
    }
}

TEST_CASE("media capability discovery reports resident paged and streamed metadata",
          "[capability_discovery][media]") {
    media_capability_info media{};
    media.media.push_back(media_image_info{.id = "cart",
                                           .label = "Cartridge",
                                           .residency = media_residency::resident,
                                           .byte_count = 0x80000U,
                                           .hash_algorithm = media_hash_algorithm::sha256,
                                           .full_hash = "sha256-cart",
                                           .provider_id = "resident.cart",
                                           .revision = "world",
                                           .cache_hint = "resident"});
    media.media.push_back(media_image_info{
        .id = "disc",
        .label = "Disc image",
        .residency = media_residency::paged,
        .byte_count = 0x200000U,
        .page_size = 4096U,
        .hash_algorithm = media_hash_algorithm::sha1,
        .full_hash = "sha1-disc",
        .page_hashes = {media_page_hash{.offset = 0U, .byte_count = 4096U, .hash = "p0"},
                        media_page_hash{.offset = 4096U, .byte_count = 4096U, .hash = "p1"}},
        .provider_id = "disc.cache",
        .revision = "rev-a",
        .cache_hint = "read_ahead"});
    media.media.push_back(media_image_info{.id = "stream",
                                           .label = "ADPCM stream",
                                           .residency = media_residency::streamed,
                                           .byte_count = 0x100000U,
                                           .hash_algorithm = media_hash_algorithm::crc32,
                                           .full_hash = "89abcdef",
                                           .provider_id = "sample.stream",
                                           .revision = "rev-b",
                                           .cache_hint = "sequential"});

    media_system sys{std::move(media)};
    const auto manifest = discover_dump_capabilities(sys);

    const auto* cart = find_descriptor(manifest, "media.cart");
    REQUIRE(cart != nullptr);
    CHECK(cart->state == capability_state::available);
    CHECK(payload_value(*cart, "residency") == "resident");
    CHECK(payload_value(*cart, "hash_algorithm") == "sha256");
    CHECK(payload_value(*cart, "provider_id") == "resident.cart");

    const auto* disc = find_descriptor(manifest, "media.disc");
    REQUIRE(disc != nullptr);
    CHECK(disc->state == capability_state::available);
    CHECK(payload_value(*disc, "residency") == "paged");
    CHECK(payload_value(*disc, "page_size") == "4096");
    CHECK(payload_value(*disc, "page_hash_count") == "2");
    CHECK(payload_value(*disc, "page_hash.1.offset") == "4096");
    CHECK(payload_value(*disc, "page_hash.1.hash") == "p1");
    CHECK(payload_value(*disc, "cache_hint") == "read_ahead");

    const auto* stream = find_descriptor(manifest, "media.stream");
    REQUIRE(stream != nullptr);
    CHECK(stream->state == capability_state::available);
    CHECK(payload_value(*stream, "residency") == "streamed");
    CHECK(payload_value(*stream, "hash_algorithm") == "crc32");
}

TEST_CASE("media capability discovery diagnoses missing hashes provider outages and revisions",
          "[capability_discovery][media]") {
    media_capability_info media{};
    media.media.push_back(media_image_info{.id = "no_hash",
                                           .label = "Missing hash",
                                           .residency = media_residency::resident,
                                           .byte_count = 1024U,
                                           .hash_algorithm = media_hash_algorithm::sha256,
                                           .provider_id = "resident.cart",
                                           .revision = "rev-ok"});
    media.media.push_back(media_image_info{.id = "offline",
                                           .label = "Remote disc",
                                           .residency = media_residency::paged,
                                           .byte_count = 4096U,
                                           .page_size = 2048U,
                                           .hash_algorithm = media_hash_algorithm::sha1,
                                           .full_hash = "sha1-offline",
                                           .provider_id = "remote.cache",
                                           .provider_available = false,
                                           .revision = "rev-ok",
                                           .cache_hint = "read_ahead"});
    media.media.push_back(media_image_info{.id = "bad_rev",
                                           .label = "Unsupported revision",
                                           .residency = media_residency::streamed,
                                           .byte_count = 8192U,
                                           .hash_algorithm = media_hash_algorithm::sha256,
                                           .full_hash = "sha256-stream",
                                           .provider_id = "stream.cache",
                                           .revision = "rev-unsupported",
                                           .revision_supported = false});
    media.media.push_back(media_image_info{.id = "bad_page",
                                           .label = "Paged without size",
                                           .residency = media_residency::paged,
                                           .byte_count = 8192U,
                                           .hash_algorithm = media_hash_algorithm::sha1,
                                           .full_hash = "sha1-paged",
                                           .provider_id = "disc.cache",
                                           .revision = "rev-ok"});

    media_system sys{std::move(media)};
    const auto manifest = discover_dump_capabilities(sys);

    const auto* no_hash = find_descriptor(manifest, "media.no_hash");
    REQUIRE(no_hash != nullptr);
    CHECK(no_hash->state == capability_state::degraded);
    CHECK(has_diagnostic(manifest, "media.hash.missing"));

    const auto* offline = find_descriptor(manifest, "media.offline");
    REQUIRE(offline != nullptr);
    CHECK(offline->state == capability_state::unavailable);
    CHECK(payload_value(*offline, "provider_available") == "false");
    CHECK(has_diagnostic(manifest, "media.provider.unavailable"));

    const auto* bad_rev = find_descriptor(manifest, "media.bad_rev");
    REQUIRE(bad_rev != nullptr);
    CHECK(bad_rev->state == capability_state::degraded);
    CHECK(payload_value(*bad_rev, "revision_supported") == "false");
    CHECK(has_diagnostic(manifest, "media.revision.unsupported"));

    const auto* bad_page = find_descriptor(manifest, "media.bad_page");
    REQUIRE(bad_page != nullptr);
    CHECK(bad_page->state == capability_state::degraded);
    CHECK(has_diagnostic(manifest, "media.page_size.missing"));
}
