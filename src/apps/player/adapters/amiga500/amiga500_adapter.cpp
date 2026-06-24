#include "amiga500_adapter.hpp"

#include "adapter_registry.hpp"
#include "audio_resampler.hpp"

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace mnemos::apps::player::adapters::amiga500 {

    namespace {
        std::vector<runtime::scheduled_chip>
        build_schedule(manifests::amiga500::amiga500_system& sys) {
            return {
                {&sys.agnus, 2U}, // OCS display/color clock from a 68K-cycle master.
                {&sys.cpu, 1U},   {&sys.paula, 2U}, {&sys.cia_a, 10U}, {&sys.cia_b, 10U},
            };
        }

        frontend_sdk::session_capability_info make_session_capabilities() {
            frontend_sdk::session_capability_info session{};
            session.input_ports = {
                {.port_index = 0U,
                 .player_slot = 1U,
                 .format = frontend_sdk::input_device_format::digital_pad,
                 .device_id = "amiga.joystick.port.2",
                 .label = "Joystick Port 2"},
                {.port_index = 1U,
                 .player_slot = 2U,
                 .format = frontend_sdk::input_device_format::digital_pad,
                 .device_id = "amiga.joystick.port.1",
                 .label = "Joystick Port 1"},
                {.port_index = 2U,
                 .player_slot = 1U,
                 .format = frontend_sdk::input_device_format::keyboard,
                 .device_id = "amiga.keyboard",
                 .label = "Keyboard"},
                {.port_index = 3U,
                 .player_slot = 1U,
                 .format = frontend_sdk::input_device_format::mouse,
                 .device_id = "amiga.mouse.port.1",
                 .label = "Mouse Port 1"},
                {.port_index = 4U,
                 .player_slot = 1U,
                 .format = frontend_sdk::input_device_format::analog,
                 .device_id = "amiga.pot.port.1",
                 .label = "Analog POT Port 1"},
                {.port_index = 5U,
                 .player_slot = 2U,
                 .format = frontend_sdk::input_device_format::analog,
                 .device_id = "amiga.pot.port.2",
                 .label = "Analog POT Port 2"},
            };
            session.deterministic_frame_input = true;
            session.max_input_delay_frames = 4U;
            return session;
        }

        frontend_sdk::media_image_info make_media(std::string id, std::string label,
                                                  std::uint64_t byte_count, std::string provider_id,
                                                  std::string cache_hint) {
            return frontend_sdk::media_image_info{
                .id = std::move(id),
                .label = std::move(label),
                .residency = frontend_sdk::media_residency::resident,
                .byte_count = byte_count,
                .hash_algorithm = frontend_sdk::media_hash_algorithm::none,
                .provider_id = std::move(provider_id),
                .revision = "loaded",
                .cache_hint = std::move(cache_hint)};
        }

        [[nodiscard]] std::uint8_t
        pack_joystick(const frontend_sdk::controller_state& state) noexcept {
            std::uint8_t mask = 0U;
            if (state.up) {
                mask =
                    static_cast<std::uint8_t>(mask | manifests::amiga500::amiga500_system::joy_up);
            }
            if (state.down) {
                mask = static_cast<std::uint8_t>(mask |
                                                 manifests::amiga500::amiga500_system::joy_down);
            }
            if (state.left) {
                mask = static_cast<std::uint8_t>(mask |
                                                 manifests::amiga500::amiga500_system::joy_left);
            }
            if (state.right) {
                mask = static_cast<std::uint8_t>(mask |
                                                 manifests::amiga500::amiga500_system::joy_right);
            }
            if (state.a || state.trigger) {
                mask = static_cast<std::uint8_t>(mask |
                                                 manifests::amiga500::amiga500_system::joy_fire);
            }
            if (state.b || state.c) {
                mask = static_cast<std::uint8_t>(
                    mask | manifests::amiga500::amiga500_system::joy_secondary_fire);
            }
            return mask;
        }

        [[nodiscard]] std::uint8_t pot_axis(std::int16_t value) noexcept {
            if (value < 0) {
                return 0xFFU;
            }
            if (value > 0xFF) {
                return 0xFFU;
            }
            return static_cast<std::uint8_t>(value);
        }

        void queue_key_edge(manifests::amiga500::amiga500_system& sys, bool old_pressed,
                            bool new_pressed, std::uint8_t raw_keycode) noexcept {
            if (old_pressed != new_pressed) {
                (void)sys.enqueue_keyboard_key(raw_keycode, new_pressed);
            }
        }

        void queue_key_usage(manifests::amiga500::amiga500_system& sys,
                             const frontend_sdk::controller_state& old_state,
                             const frontend_sdk::controller_state& new_state,
                             std::uint16_t usage, std::uint8_t raw_keycode) noexcept {
            queue_key_edge(sys, old_state.key_down(usage), new_state.key_down(usage), raw_keycode);
        }

        struct physical_key_map final {
            std::uint16_t usage;
            std::uint8_t raw_keycode;
        };

        constexpr std::uint16_t hid_caps_lock = 0x39U;
        constexpr physical_key_map physical_keyboard_map[] = {
            {0x35U, 0x00U}, // `
            {0x1EU, 0x01U}, {0x1FU, 0x02U}, {0x20U, 0x03U}, {0x21U, 0x04U},
            {0x22U, 0x05U}, {0x23U, 0x06U}, {0x24U, 0x07U}, {0x25U, 0x08U},
            {0x26U, 0x09U}, {0x27U, 0x0AU}, {0x2DU, 0x0BU}, {0x2EU, 0x0CU},
            {0x31U, 0x0DU}, {0x89U, 0x0EU}, {0x62U, 0x0FU},
            {0x14U, 0x10U}, {0x1AU, 0x11U}, {0x08U, 0x12U}, {0x15U, 0x13U},
            {0x17U, 0x14U}, {0x1CU, 0x15U}, {0x18U, 0x16U}, {0x0CU, 0x17U},
            {0x12U, 0x18U}, {0x13U, 0x19U}, {0x2FU, 0x1AU}, {0x30U, 0x1BU},
            {0x59U, 0x1DU}, {0x5AU, 0x1EU}, {0x5BU, 0x1FU},
            {0x04U, 0x20U}, {0x16U, 0x21U}, {0x07U, 0x22U}, {0x09U, 0x23U},
            {0x0AU, 0x24U}, {0x0BU, 0x25U}, {0x0DU, 0x26U}, {0x0EU, 0x27U},
            {0x0FU, 0x28U}, {0x33U, 0x29U}, {0x34U, 0x2AU}, {0x32U, 0x2BU},
            {0x5CU, 0x2DU}, {0x5DU, 0x2EU}, {0x5EU, 0x2FU},
            {0x64U, 0x30U}, {0x1DU, 0x31U}, {0x1BU, 0x32U}, {0x06U, 0x33U},
            {0x19U, 0x34U}, {0x05U, 0x35U}, {0x11U, 0x36U}, {0x10U, 0x37U},
            {0x36U, 0x38U}, {0x37U, 0x39U}, {0x38U, 0x3AU}, {0x87U, 0x3BU},
            {0x63U, 0x3CU}, {0x5FU, 0x3DU}, {0x60U, 0x3EU}, {0x61U, 0x3FU},
            {0x2CU, 0x40U}, {0x2AU, 0x41U}, {0x2BU, 0x42U}, {0x58U, 0x43U},
            {0x28U, 0x44U}, {0x29U, 0x45U}, {0x4CU, 0x46U}, {0x56U, 0x4AU},
            {0x52U, 0x4CU}, {0x51U, 0x4DU}, {0x4FU, 0x4EU}, {0x50U, 0x4FU},
            {0x3AU, 0x50U}, {0x3BU, 0x51U}, {0x3CU, 0x52U}, {0x3DU, 0x53U},
            {0x3EU, 0x54U}, {0x3FU, 0x55U}, {0x40U, 0x56U}, {0x41U, 0x57U},
            {0x42U, 0x58U}, {0x43U, 0x59U}, {0x54U, 0x5CU}, {0x55U, 0x5DU},
            {0x57U, 0x5EU}, {0x75U, 0x5FU}, {0xE1U, 0x60U}, {0xE5U, 0x61U},
            {0xE0U, 0x63U}, {0xE2U, 0x64U}, {0xE6U, 0x65U}, {0xE3U, 0x66U},
            {0xE7U, 0x67U},
        };

        void route_physical_keyboard_edges(manifests::amiga500::amiga500_system& sys,
                                           const frontend_sdk::controller_state& old_state,
                                           const frontend_sdk::controller_state& new_state)
            noexcept {
            for (const auto& key : physical_keyboard_map) {
                queue_key_usage(sys, old_state, new_state, key.usage, key.raw_keycode);
            }
            if (!old_state.key_down(hid_caps_lock) && new_state.key_down(hid_caps_lock)) {
                (void)sys.press_caps_lock();
            }
        }

        void route_keyboard_edges(manifests::amiga500::amiga500_system& sys,
                                  const frontend_sdk::controller_state& old_state,
                                  const frontend_sdk::controller_state& new_state,
                                  bool keyboard_port) noexcept {
            queue_key_edge(sys, old_state.select, new_state.select, 0x40U); // Space
            queue_key_edge(sys, old_state.start, new_state.start, 0x44U);   // Return
            queue_key_edge(sys, old_state.up, new_state.up, 0x4CU);         // Cursor up
            queue_key_edge(sys, old_state.down, new_state.down, 0x4DU);     // Cursor down
            queue_key_edge(sys, old_state.right, new_state.right, 0x4EU);   // Cursor right
            queue_key_edge(sys, old_state.left, new_state.left, 0x4FU);     // Cursor left
            queue_key_edge(sys, old_state.mode, new_state.mode, 0x60U);     // Left Shift
            if (!keyboard_port) {
                return;
            }
            queue_key_edge(sys, old_state.x, new_state.x, 0x45U); // Escape
            queue_key_edge(sys, old_state.a, new_state.a, 0x64U); // Left Alt
            queue_key_edge(sys, old_state.b, new_state.b, 0x65U); // Right Alt
            queue_key_edge(sys, old_state.c, new_state.c, 0x63U); // Control
            queue_key_edge(sys, old_state.z, new_state.z, 0x5FU); // Help
            if (!old_state.y && new_state.y) {
                (void)sys.press_caps_lock();
            }
            route_physical_keyboard_edges(sys, old_state, new_state);
        }

        frontend_sdk::media_capability_info
        make_media_capabilities(std::string_view display_name, std::uint64_t kickstart_byte_count,
                                const std::vector<std::vector<std::uint8_t>>& disks) {
            frontend_sdk::media_capability_info media{};
            media.media.push_back(make_media("kickstart", "Kickstart", kickstart_byte_count,
                                             "amiga500.kickstart", "resident"));
            for (std::size_t i = 0U; i < disks.size(); ++i) {
                const std::string label =
                    disks.size() == 1U
                        ? (display_name.empty() ? std::string{"Disk"} : std::string{display_name})
                        : ((display_name.empty() ? std::string{"Disk"}
                                                 : std::string{display_name}) +
                           " disk " + std::to_string(i + 1U));
                const std::string provider_id =
                    i < manifests::amiga500::amiga500_system::floppy_drive_count
                        ? "amiga500.df" + std::to_string(i)
                        : "amiga500.df0";
                media.media.push_back(
                    make_media("disk." + std::to_string(i), label, disks[i].size(), provider_id,
                               disks.size() == 1U ? "resident" : "resident_removable"));
            }
            return media;
        }
    } // namespace

    amiga500_adapter::amiga500_adapter(std::vector<std::uint8_t> kickstart_rom,
                                       const manifests::amiga500::amiga500_config& config,
                                       std::string display_name,
                                       frontend_sdk::scheduler_factory* scheduler_factory)
        : amiga500_adapter(std::move(kickstart_rom), config, std::move(display_name), {},
                           scheduler_factory) {}

    amiga500_adapter::amiga500_adapter(std::vector<std::uint8_t> kickstart_rom,
                                       const manifests::amiga500::amiga500_config& config,
                                       std::string display_name,
                                       std::vector<std::vector<std::uint8_t>> disks,
                                       frontend_sdk::scheduler_factory* scheduler_factory)
        : session_(make_session_capabilities()),
          media_(make_media_capabilities(display_name, kickstart_rom.size(), disks)),
          sys_(manifests::amiga500::assemble_amiga500(std::move(kickstart_rom), config)),
          chip_ram_view_("chip_ram", sys_->chip_ram),
          scheduler_(
              frontend_sdk::make_scheduler(scheduler_factory, build_schedule(*sys_), &sys_->agnus)),
          region_(config.video_region),
          target_fps_(mnemos::target_fps[static_cast<std::size_t>(config.video_region)]),
          disks_(std::move(disks)) {
        sys_->paula.enable_audio_capture(true);
        const std::size_t mounted_drives =
            std::min(disks_.size(), manifests::amiga500::amiga500_system::floppy_drive_count);
        for (std::size_t drive = 0U; drive < mounted_drives; ++drive) {
            (void)sys_->mount_floppy(drive, disks_[drive]);
        }
        chip_view_[0] = &sys_->agnus;
        chip_view_[1] = &sys_->cpu;
        chip_view_[2] = &sys_->paula;
        chip_view_[3] = &sys_->denise;
        chip_view_[4] = &sys_->cia_a;
        chip_view_[5] = &sys_->cia_b;
        system_mem_view_[0] = &chip_ram_view_;

        spec_.push_back({.label = "System", .value = "Amiga 500"});
        spec_.push_back(
            {.label = "Region",
             .value = config.video_region == mnemos::video_region::pal ? "PAL" : "NTSC"});
        if (!display_name.empty()) {
            spec_.push_back({.label = disks_.empty() ? "BIOS" : "Disk", .value = display_name});
        }
        if (disks_.size() > 1U) {
            spec_.push_back({.label = "Disks", .value = std::to_string(disks_.size())});
        }
    }

    frontend_sdk::video_region amiga500_adapter::region() const noexcept {
        return {mnemos::fps_x1000[static_cast<std::size_t>(region_)]};
    }

    void amiga500_adapter::step_one_frame() {
        scheduler_.run_frame();
        sys_->service_keyboard_queue();
    }

    void amiga500_adapter::apply_input(int port,
                                       const frontend_sdk::controller_state& state) noexcept {
        if (port < 0 || port >= static_cast<int>(ports_.size())) {
            return;
        }
        const auto old_state = ports_[static_cast<std::size_t>(port)];
        ports_[static_cast<std::size_t>(port)] = state;

        if (port == 3) {
            std::int16_t delta_x = 0;
            std::int16_t delta_y = 0;
            if (state.aim_x < 0 || state.aim_y < 0) {
                mouse_position_valid_ = false;
            } else {
                if (mouse_position_valid_) {
                    delta_x = static_cast<std::int16_t>(static_cast<int>(state.aim_x) -
                                                        static_cast<int>(mouse_x_));
                    delta_y = static_cast<std::int16_t>(static_cast<int>(state.aim_y) -
                                                        static_cast<int>(mouse_y_));
                }
                mouse_position_valid_ = true;
                mouse_x_ = state.aim_x;
                mouse_y_ = state.aim_y;
            }
            sys_->set_mouse(0U, delta_x, delta_y, state.trigger, state.a, state.b);
            return;
        }

        if (port == 4 || port == 5) {
            const std::size_t hardware_port = static_cast<std::size_t>(port - 4);
            sys_->set_pot_position(hardware_port, pot_axis(state.aim_x), pot_axis(state.aim_y));
            return;
        }

        if (port == 0 || port == 1) {
            // Player 1 drives the right Amiga controller pair (JOY1DAT/CIAA bit 7),
            // the socket most games expect for a joystick. Player 2 maps to JOY0DAT.
            const std::size_t hardware_port = port == 0 ? 1U : 0U;
            sys_->set_joystick(hardware_port, pack_joystick(state));
        }
        if (port == 0 || port == 2) {
            route_keyboard_edges(*sys_, old_state, state, port == 2);
        }
    }

    bool amiga500_adapter::insert_media(std::size_t index) noexcept {
        if (index >= disks_.size()) {
            return false;
        }
        if (!sys_->mount_floppy(0U, disks_[index])) {
            return false;
        }
        disk_index_ = index;
        return true;
    }

    frontend_sdk::audio_chunk amiga500_adapter::drain_audio() noexcept {
        const std::size_t pairs = sys_->paula.pending_samples();
        if (pairs == 0U) {
            return {.samples = nullptr, .frame_count = 0U, .sample_rate = mnemos::dsp::kOutputRate};
        }
        paula_buf_.resize(pairs * 2U);
        sys_->paula.drain_samples(paula_buf_.data(), pairs);

        const double exact =
            (static_cast<double>(mnemos::dsp::kOutputRate) / target_fps_) + audio_frac_;
        int dst_pairs = static_cast<int>(exact);
        if (dst_pairs <= 0) {
            dst_pairs = 1;
        }
        audio_frac_ = exact - static_cast<double>(dst_pairs);
        mix_buf_.resize(static_cast<std::size_t>(dst_pairs) * 2U);

        const double scale = static_cast<double>(pairs) / static_cast<double>(dst_pairs);
        for (int i = 0; i < dst_pairs; ++i) {
            int left = 0;
            int right = 0;
            if (scale > 1.0) {
                left = mnemos::dsp::sample_channel_box(
                    paula_buf_.data(), 2, 0, static_cast<int>(pairs), scale * i, scale * (i + 1));
                right = mnemos::dsp::sample_channel_box(
                    paula_buf_.data(), 2, 1, static_cast<int>(pairs), scale * i, scale * (i + 1));
            } else {
                left = mnemos::dsp::sample_channel_linear(paula_buf_.data(), 2, 0,
                                                          static_cast<int>(pairs), scale * i);
                right = mnemos::dsp::sample_channel_linear(paula_buf_.data(), 2, 1,
                                                           static_cast<int>(pairs), scale * i);
            }
            mix_buf_[static_cast<std::size_t>(i) * 2U] = mnemos::dsp::clip_i16(left);
            mix_buf_[static_cast<std::size_t>(i) * 2U + 1U] = mnemos::dsp::clip_i16(right);
        }
        return {.samples = mix_buf_.data(),
                .frame_count = static_cast<std::uint32_t>(dst_pairs),
                .sample_rate = mnemos::dsp::kOutputRate};
    }

    void force_link() noexcept {}

    runtime::save_target build_save_target(amiga500_adapter& adapter) {
        auto& sys = adapter.system();
        auto& sched = adapter.scheduler();

        runtime::save_target target;
        target.manifest_id = "amiga500";
        target.manifest_rev = 1U;
        target.master_cycle = sched.master_cycle();
        target.chips.push_back({"cpu", &sys.cpu});
        target.chips.push_back({"agnus", &sys.agnus});
        target.chips.push_back({"denise", &sys.denise});
        target.chips.push_back({"paula", &sys.paula});
        target.chips.push_back({"cia_a", &sys.cia_a});
        target.chips.push_back({"cia_b", &sys.cia_b});
        target.components.push_back(runtime::save_component{
            .id = "system",
            .save = [&sys](chips::state_writer& writer) { sys.save_state(writer); },
            .load = [&sys](chips::state_reader& reader) { sys.load_state(reader); }});
        target.components.push_back(runtime::save_component{
            .id = "scheduler",
            .save = [&sched](chips::state_writer& writer) { sched.save_state(writer); },
            .load = [&sched](chips::state_reader& reader) { sched.load_state(reader); }});
        return target;
    }

    namespace {
        const auto register_amiga500 = [] {
            mnemos::frontend_sdk::adapter_registry::instance().register_family(
                "amiga500",
                [](mnemos::frontend_sdk::adapter_options opts)
                    -> std::unique_ptr<mnemos::frontend_sdk::player_system> {
                    return std::make_unique<amiga500_adapter>(
                        std::move(opts.rom),
                        manifests::amiga500::amiga500_config{.video_region = opts.video_region},
                        std::move(opts.display_name), std::move(opts.additional_media),
                        opts.scheduler_factory_override);
                });
            return 0;
        }();
    } // namespace

} // namespace mnemos::apps::player::adapters::amiga500
