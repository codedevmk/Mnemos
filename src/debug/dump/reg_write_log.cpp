#include "reg_write_log.hpp"

#include "introspection_views.hpp"
#include "vgm.hpp"

#include <cstdint>
#include <vector>

namespace mnemos::debug {

    namespace {
        // SMS / Game Gear PSG master clock (Hz). The VGM player pitches the song
        // against this; other systems' PSG clocks are a follow-up.
        constexpr std::uint32_t sn76489_clock = 3579545U;
        constexpr std::uint8_t vgm_psg_write = 0x50U;  // SN76489 data port
        constexpr std::uint8_t vgm_psg_stereo = 0x4FU; // SN76489 GG stereo port
        constexpr std::uint8_t vgm_wait_ntsc = 0x62U;  // wait 735 samples
        constexpr std::uint8_t vgm_wait_pal = 0x63U;   // wait 882 samples
        constexpr std::uint8_t vgm_end = 0x66U;        // end of sound data
    } // namespace

    struct reg_write_log_session::state final {
        std::vector<std::uint8_t> body{};
    };

    reg_write_log_session::reg_write_log_session(frontend_sdk::player_system& sys,
                                                 const std::string& vgm_path)
        : path_(vgm_path) {
        chips::ichip* candidate{};
        instrumentation::reg_write_trace* rt{};
        for (chips::ichip* chip : sys.chips()) {
            if (chip == nullptr || chip->metadata().part_number != "SN76489") {
                continue;
            }
            if (auto* t = chip->introspection().reg_writes()) {
                candidate = chip;
                rt = t;
                break;
            }
        }
        if (rt == nullptr) {
            return;
        }

        psg_clock_ = sn76489_clock;
        if (sys.region().frames_per_second_x1000 < 55000U) {
            frame_wait_cmd_ = vgm_wait_pal;
            samples_per_frame_ = 882U;
            rate_ = 50U;
        } else {
            frame_wait_cmd_ = vgm_wait_ntsc;
            samples_per_frame_ = 735U;
            rate_ = 60U;
        }

        state_ = std::make_unique<state>();
        target_ = candidate;

        state* s = state_.get();
        rt->install([s](const instrumentation::reg_write_event& ev) {
            s->body.push_back(ev.port == 1U ? vgm_psg_stereo : vgm_psg_write);
            s->body.push_back(ev.value);
        });
    }

    void reg_write_log_session::mark_frame() noexcept {
        if (state_) {
            state_->body.push_back(frame_wait_cmd_);
            ++frames_;
        }
    }

    reg_write_log_session::~reg_write_log_session() {
        if (target_ != nullptr) {
            if (auto* rt = target_->introspection().reg_writes()) {
                rt->install({});
            }
        }
        if (!state_) {
            return;
        }
        state_->body.push_back(vgm_end);
        audio::vgm_header header{};
        header.sn76489_clock = psg_clock_;
        header.rate = rate_;
        header.total_samples = static_cast<std::uint32_t>(frames_ * samples_per_frame_);
        (void)audio::write_vgm(path_, header, state_->body);
    }

} // namespace mnemos::debug
