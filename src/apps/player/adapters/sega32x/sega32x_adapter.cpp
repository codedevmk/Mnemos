#include "sega32x_adapter.hpp"

#include "adapter_registry.hpp"
#include "audio_resampler.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <utility>

using mnemos::dsp::clip_i16;
using mnemos::dsp::kOutputRate;
using mnemos::dsp::sample_channel_box;
using mnemos::dsp::sample_channel_linear;
using mnemos::dsp::scale_q12;

namespace mnemos::apps::player::adapters::sega32x {

    namespace {

        // Interleave granularity: one NTSC scanline of master cycles. The 68000
        // and the SH-2 pair hand-shake through the COMM registers continuously
        // during boot, so the SH-2s catch up after every scanline of main time
        // (the same per-scanline budget the reference uses).
        constexpr std::uint64_t kSliceMasterCycles = 3420;

        // Mixer gains (Q12). FM keeps its 3:1 bias over PSG, matching the
        // Genesis adapter; the PWM pair joins at the PCM level used by the
        // Sega CD adapter's sample sources.
        constexpr int kGainFm = 3072;
        constexpr int kGainPsg = 1024;
        constexpr int kGainPwm = 2048;

        // Scheduler order: VDP first (it drives the raster the 68000 samples),
        // then the gated 68000 (DMA stall) + gated Z80 (BUSREQ), FM, PSG. The
        // SH-2s are NOT scheduler chips -- the adapter paces them per slice
        // through the machine's catch-up (they run at exactly 3x the 68000).
        std::vector<runtime::scheduled_chip> build_schedule(manifests::genesis::genesis_system& g) {
            return {
                {&g.vdp, 1U}, {&g.cpu_gate, 7U}, {&g.z80_gate, 15U}, {&g.fm, 7U}, {&g.psg, 15U},
            };
        }

        // Opt-in raw-dump knobs: each env var names a file that receives a raw
        // s16 stream for offline analysis.
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996) // getenv/fopen: opt-in debug knobs
#endif
        bool env_set(const char* name) noexcept {
            const char* p = std::getenv(name);
            return p != nullptr && p[0] != '\0';
        }

        FILE* dump_file(const char* name) noexcept {
            const char* p = std::getenv(name);
            return p != nullptr && p[0] != '\0' ? std::fopen(p, "wb") : nullptr;
        }
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

        constexpr const char* kPwmDumpEnv = "MNEMOS_32X_PWM_DUMP";
        constexpr const char* kMixDumpEnv = "MNEMOS_32X_MIX_DUMP";
        constexpr const char* kFmDumpEnv = "MNEMOS_32X_FM_DUMP";
        constexpr const char* kPsgDumpEnv = "MNEMOS_32X_PSG_DUMP";

        // Resample one source (mono or interleaved stereo, `count` frames at its
        // native rate) to `dst_count` output frames and accumulate it, scaled by
        // `gain` (Q12), into the L/R sum buffers. A mono source feeds both sides.
        void add_source(std::int32_t* acc_l, std::int32_t* acc_r, const std::int16_t* src,
                        int chans, int count, int gain, int dst_count) noexcept {
            if (src == nullptr || count <= 0 || dst_count <= 0) {
                return;
            }
            const int stride = chans;
            const double scale = static_cast<double>(count) / static_cast<double>(dst_count);
            for (int i = 0; i < dst_count; ++i) {
                int l = 0;
                int r = 0;
                if (scale > 1.0) {
                    l = sample_channel_box(src, stride, 0, count, scale * i, scale * (i + 1));
                    r = chans == 2
                            ? sample_channel_box(src, stride, 1, count, scale * i, scale * (i + 1))
                            : l;
                } else {
                    l = sample_channel_linear(src, stride, 0, count, scale * i);
                    r = chans == 2 ? sample_channel_linear(src, stride, 1, count, scale * i) : l;
                }
                acc_l[i] += scale_q12(l, gain);
                acc_r[i] += scale_q12(r, gain);
            }
        }

    } // namespace

    sega32x_adapter::sega32x_adapter(std::vector<std::uint8_t> cart,
                                     manifests::sega32x::sega32x_bios bios,
                                     const manifests::genesis::genesis_config& config,
                                     std::string display_name,
                                     frontend_sdk::scheduler_factory* scheduler_factory)
        : machine_(manifests::sega32x::assemble_sega32x_machine(std::move(cart), bios, config)),
          work_ram_view_("work_ram", machine_->genesis->work_ram),
          z80_ram_view_("z80_ram", machine_->genesis->z80_ram),
          sdram_view_("sdram", machine_->sega32x->sdram),
          fb_view_("32x_framebuffer", machine_->sega32x->framebuffer),
          scheduler_(frontend_sdk::make_scheduler(
              scheduler_factory, build_schedule(*machine_->genesis), &machine_->genesis->vdp)),
          region_(config.video_region),
          target_fps_(mnemos::target_fps[static_cast<std::size_t>(config.video_region)]) {
        machine_->genesis->fm.enable_audio_capture(true);
        machine_->genesis->psg.enable_audio_capture(true);
        machine_->genesis->vdp.enable_backdrop_mask(true);
        machine_->sega32x->enable_pwm_capture(true);

        chip_view_[0] = &machine_->genesis->vdp;
        chip_view_[1] = &machine_->genesis->cpu;
        chip_view_[2] = &machine_->genesis->z80;
        chip_view_[3] = &machine_->genesis->fm;
        chip_view_[4] = &machine_->genesis->psg;
        chip_view_[5] = &machine_->sega32x->master_cpu;
        chip_view_[6] = &machine_->sega32x->slave_cpu;
        chip_view_[7] = &machine_->sega32x->vdp;

        system_mem_view_[0] = &work_ram_view_;
        system_mem_view_[1] = &z80_ram_view_;
        system_mem_view_[2] = &sdram_view_;
        system_mem_view_[3] = &fb_view_;

        spec_.push_back({.label = "System", .value = "Sega 32X"});
        spec_.push_back(
            {.label = "Region",
             .value = config.video_region == mnemos::video_region::pal ? "PAL" : "NTSC"});
        if (!display_name.empty()) {
            spec_.push_back({.label = "Cart", .value = std::move(display_name)});
        }

        // Opt-in SH-2 bus write watch (MNEMOS_32X_BUSWATCH=<lo>:<hi>, hex):
        // logs every write whose mirror-collapsed address falls in [lo, hi)
        // with the writing CPU's PC. The tool that located both the slave
        // vector-table stubs and the status-bar scratch writes.
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996) // getenv: opt-in debug knob
#endif
        if (const char* spec = std::getenv("MNEMOS_32X_BUSWATCH");
            spec != nullptr && spec[0] != '\0') {
            char* sep = nullptr;
            const auto lo = static_cast<std::uint32_t>(std::strtoul(spec, &sep, 16));
            const auto hi = sep != nullptr && *sep == ':'
                                ? static_cast<std::uint32_t>(std::strtoul(sep + 1, nullptr, 16))
                                : lo + 4U;
            auto* tx = machine_->sega32x.get();
            const auto watch = [lo, hi](const char* cpu, std::uint32_t pc,
                                        const topology::access_event& ev) {
                const std::uint32_t a = ev.address & 0x1FFFFFFFU;
                if (ev.write && a >= lo && a < hi) {
                    std::fprintf(stderr, "[busw] %s pc=%08X [%08X]=%02X\n", cpu, pc, a, ev.value);
                }
            };
            machine_->sega32x->master_bus.set_access_observer(
                [tx, watch](const topology::access_event& ev) {
                    watch("M", tx->master_cpu.cpu_registers().pc, ev);
                });
            machine_->sega32x->slave_bus.set_access_observer(
                [tx, watch](const topology::access_event& ev) {
                    watch("S", tx->slave_cpu.cpu_registers().pc, ev);
                });
            // TEMP DEBUG: 68K-bus DATA access sampling in the same range
            // (reads + writes, instruction fetches filtered out, 1 in 16).
            auto* gen_sys = machine_->genesis.get();
            machine_->genesis->bus.set_access_observer(
                [gen_sys, lo, hi](const topology::access_event& ev) {
                    static std::uint32_t n = 0;
                    if (ev.address < lo || ev.address >= hi) {
                        return;
                    }
                    const std::uint32_t pc = gen_sys->cpu.cpu_registers().pc;
                    const std::uint32_t d = ev.address > pc ? ev.address - pc : pc - ev.address;
                    if (d < 0x40U) {
                        return; // instruction fetch
                    }
                    if (!ev.write && (++n <= 80U || (n & 0x3FFU) == 0U)) {
                        std::fprintf(stderr, "[busw] G pc=%08X %c [%08X]=%02X\n", pc,
                                     ev.write ? 'w' : 'r', ev.address, ev.value);
                    }
                });
        }

        // Opt-in 68000 work-RAM write watch (MNEMOS_WRAM_WATCH=<lo>:<hi>, hex
        // 16-bit work-RAM offsets): logs every work-RAM byte write whose
        // mirror-collapsed offset falls in [lo, hi) with the writing PC.
        // Claims the 68K-bus observer slot (don't combine with BUSWATCH).
        if (const char* spec = std::getenv("MNEMOS_WRAM_WATCH");
            spec != nullptr && spec[0] != '\0') {
            char* sep = nullptr;
            const auto lo = static_cast<std::uint32_t>(std::strtoul(spec, &sep, 16)) & 0xFFFFU;
            auto hi = sep != nullptr && *sep == ':'
                          ? static_cast<std::uint32_t>(std::strtoul(sep + 1, nullptr, 16))
                          : lo + 2U;
            hi = std::min(hi, 0x10000U);
            auto* gen_sys = machine_->genesis.get();
            machine_->genesis->bus.set_access_observer(
                [gen_sys, lo, hi](const topology::access_event& ev) {
                    if (!ev.write || (ev.address & 0xE00000U) != 0xE00000U) {
                        return;
                    }
                    const std::uint32_t off = ev.address & 0xFFFFU;
                    if (off >= lo && off < hi) {
                        std::fprintf(stderr, "[wramw] pc=%06X [FF%04X]=%02X\n",
                                     gen_sys->cpu.cpu_registers().pc, off, ev.value);
                    }
                });
        }
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

        // SH-2 worker thread (MNEMOS_32X_THREAD=0 disables): the depth-1
        // scanline pipeline overlaps the SH-2 pair with the Genesis side.
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996) // getenv: opt-out knob
#endif
        const char* thread_env = std::getenv("MNEMOS_32X_THREAD");
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
        if (thread_env == nullptr || thread_env[0] != '0') {
            machine_->start_sh2_worker();
        }

        // Seed the composed frame from the Genesis view geometry so
        // current_frame() is valid before the first step.
        const auto gen = machine_->genesis->vdp.framebuffer();
        composed_.assign(static_cast<std::size_t>(gen.effective_stride()) * gen.height, 0U);
        composed_view_ = {.pixels = composed_.data(),
                          .width = gen.width,
                          .height = gen.height,
                          .stride = gen.effective_stride()};
    }

    frontend_sdk::video_region sega32x_adapter::region() const noexcept {
        return {mnemos::fps_x1000[static_cast<std::size_t>(region_)]};
    }

    chips::frame_buffer_view sega32x_adapter::current_frame() const noexcept {
        return composed_view_;
    }

    void sega32x_adapter::compose_finished_line() noexcept {
        // Each 3420-master-cycle slice completes exactly one VDP line, so the
        // row the VDP just rendered is scanline()-1; V-blank lines have no
        // display row. The 32X overlay (a no-op while the bitmap mode is off)
        // samples the 32X VDP state as of this raster position.
        compose_line(machine_->genesis->vdp.scanline() - 1);
    }

    void sega32x_adapter::compose_line(int line) noexcept {
        const auto gen = machine_->genesis->vdp.framebuffer();
        if (line < 0 || line >= static_cast<int>(gen.height)) {
            return;
        }
        const std::size_t stride = gen.effective_stride();
        const std::size_t need = stride * gen.height;
        if (composed_.size() != need) {
            composed_.assign(need, 0U);
        }
        std::uint32_t* dst = composed_.data() + static_cast<std::size_t>(line) * stride;
        std::copy_n(gen.pixels + static_cast<std::size_t>(line) * stride, stride, dst);
        auto& tx = *machine_->sega32x;
        tx.vdp.compose_scanline(tx.framebuffer, std::span<std::uint32_t>{dst, gen.width}, line,
                                machine_->genesis->vdp.backdrop_row(line));
    }

    void sega32x_adapter::finish_composed_frame() noexcept {
        const auto gen = machine_->genesis->vdp.framebuffer();
        composed_view_ = {.pixels = composed_.data(),
                          .width = gen.width,
                          .height = gen.height,
                          .stride = static_cast<std::uint32_t>(gen.effective_stride())};
    }

    void sega32x_adapter::step_one_frame() {
        // Advance the Genesis a scanline of master cycles, then catch the SH-2
        // pair up to 3x the 68000's progress, until the VDP completes a frame.
        const std::uint64_t start_frame = scheduler_.frame_index();
        if (machine_->sh2_worker_running()) {
            // Depth-1 pipeline: the worker runs the SH-2 batch for line N-1
            // while the main thread emulates Genesis line N. Compose for a
            // line happens after its batch joins -- same data, one line later
            // in wall time. Mid-line interactions fence inside the machine.
            int pending_line = -1;
            bool have_pending = false;
            while (scheduler_.frame_index() == start_frame) {
                scheduler_.run_master_cycles(kSliceMasterCycles);
                machine_->join_sh2();
                if (have_pending) {
                    compose_line(pending_line);
                }
                pending_line = machine_->genesis->vdp.scanline() - 1;
                have_pending = true;
                machine_->schedule_sh2_catch_up();
            }
            machine_->join_sh2();
            if (have_pending) {
                compose_line(pending_line);
            }
        } else {
            while (scheduler_.frame_index() == start_frame) {
                machine_->begin_slice();
                scheduler_.run_master_cycles(kSliceMasterCycles);
                machine_->catch_up_sh2();
                compose_finished_line();
            }
        }
        finish_composed_frame();
        ++frames_stepped_;

        // Opt-in raw PWM dump (MNEMOS_32X_PWM_DUMP=path): drain the capture
        // queue here so headless runs (which never call drain_audio) record
        // interleaved s16 L/R pairs at the PWM step rate. NOTE: this consumes
        // the PWM queue, so a simultaneous mix dump records without PWM.
        static FILE* pwm_dump = dump_file(kPwmDumpEnv);
        if (pwm_dump != nullptr) {
            const std::size_t n = machine_->sega32x->pwm_pending_samples();
            if (n > 0U) {
                pwm_buf_.resize(n * 2U);
                machine_->sega32x->drain_pwm_samples(pwm_buf_.data(), n);
                std::fwrite(pwm_buf_.data(), sizeof(std::int16_t), n * 2U, pwm_dump);
                std::fflush(pwm_dump);
            }
        }

        // Opt-in mixed-output dump (MNEMOS_32X_MIX_DUMP=path): the exact 48 kHz
        // stereo mix the player would queue, recorded deterministically. The
        // per-chip dumps hook drain_audio, so drive it for them too.
        static FILE* mix_dump = dump_file(kMixDumpEnv);
        static const bool drain_for_dumps =
            mix_dump != nullptr || env_set(kFmDumpEnv) || env_set(kPsgDumpEnv);
        if (drain_for_dumps) {
            const auto chunk = drain_audio();
            if (mix_dump != nullptr && chunk.samples != nullptr && chunk.frame_count > 0U) {
                std::fwrite(chunk.samples, sizeof(std::int16_t),
                            static_cast<std::size_t>(chunk.frame_count) * 2U, mix_dump);
                std::fflush(mix_dump);
            }
        }

        // Opt-in boot probe (MNEMOS_32X_PROBE=1): one stderr line per frame with
        // both SH-2 positions and the COMM handshake words.
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996) // std::getenv: opt-in debug knob
#endif
        static const bool probe = [] {
            const char* p = std::getenv("MNEMOS_32X_PROBE");
            return p != nullptr && p[0] != '\0' && p[0] != '0';
        }();
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
        if (probe) {
            auto& tx = *machine_->sega32x;
            auto& gen = *machine_->genesis;
            const std::uint64_t z80_now = gen.z80.elapsed_cycles();
            const std::uint64_t z80_delta = z80_now - last_z80_cycles_;
            last_z80_cycles_ = z80_now;
            std::fprintf(stderr,
                         "[32x] f%05llu rst=%d mpc=%08X spc=%08X comm %04X %04X %04X %04X mode=%u "
                         "z80=%llu run=%d zpc=%04X dreq=%u/%u ctl=%02X pwm=%u/%u "
                         "smask=%02X ssr=%03X spend=%d slatch=%02X svbr=%08X sgbr=%08X "
                         "d0=%08X c4567 %04X %04X %04X %04X\n",
                         static_cast<unsigned long long>(frames_stepped_), tx.sh2_reset_asserted,
                         tx.master_cpu.cpu_registers().pc, tx.slave_cpu.cpu_registers().pc,
                         tx.comm[0], tx.comm[1], tx.comm[2], tx.comm[3],
                         static_cast<unsigned>(tx.vdp.mode()),
                         static_cast<unsigned long long>(z80_delta), gen.z80_running ? 1 : 0,
                         gen.z80.cpu_registers().pc, tx.dbg_dreq_pushes, tx.dbg_dreq_pops,
                         tx.dreq_ctrl, tx.dbg_pwm_writes, tx.dbg_pwm_irqs, tx.slave_irq_mask,
                         tx.slave_cpu.cpu_registers().sr & 0xFFFU, tx.slave_cpu.pending_irq_level(),
                         tx.slave_irq_latch, tx.slave_cpu.cpu_registers().vbr,
                         tx.slave_cpu.cpu_registers().gbr, gen.cpu.cpu_registers().d[0], tx.comm[4],
                         tx.comm[5], tx.comm[6], tx.comm[7]);
        }
    }

    void sega32x_adapter::apply_input(int port,
                                      const frontend_sdk::controller_state& state) noexcept {
        if (port < 0 || port >= static_cast<int>(ports_.size())) {
            return;
        }
        ports_[static_cast<std::size_t>(port)] = state;
        if (auto* dev = machine_->genesis->port_device(port)) {
            dev->apply_state(state);
        }
    }

    frontend_sdk::audio_chunk sega32x_adapter::drain_audio() noexcept {
        const std::size_t fm_count = machine_->genesis->fm.pending_samples();
        const std::size_t psg_count = machine_->genesis->psg.pending_samples();
        const std::size_t pwm_count = machine_->sega32x->pwm_pending_samples();

        if (fm_count == 0U && psg_count == 0U && pwm_count == 0U) {
            return {.samples = nullptr, .frame_count = 0U, .sample_rate = kOutputRate};
        }

        if (fm_count > 0U) {
            fm_buf_.resize(fm_count * 2U);
            machine_->genesis->fm.drain_samples(fm_buf_.data(), fm_count);
        }
        if (psg_count > 0U) {
            psg_buf_.resize(psg_count);
            machine_->genesis->psg.drain_samples(psg_buf_.data(), psg_count);
        }
        if (pwm_count > 0U) {
            pwm_buf_.resize(pwm_count * 2U);
            machine_->sega32x->drain_pwm_samples(pwm_buf_.data(), pwm_count);
        }

        // Opt-in per-chip dumps (MNEMOS_32X_FM_DUMP / MNEMOS_32X_PSG_DUMP=path):
        // the raw pre-mix streams at chip rate (FM interleaved stereo, PSG
        // mono), to attribute a bad mix to its source chip.
        static FILE* fm_dump = dump_file(kFmDumpEnv);
        static FILE* psg_dump = dump_file(kPsgDumpEnv);
        if (fm_dump != nullptr && fm_count > 0U) {
            std::fwrite(fm_buf_.data(), sizeof(std::int16_t), fm_count * 2U, fm_dump);
            std::fflush(fm_dump);
        }
        if (psg_dump != nullptr && psg_count > 0U) {
            std::fwrite(psg_buf_.data(), sizeof(std::int16_t), psg_count, psg_dump);
            std::fflush(psg_dump);
        }

        const double exact = (static_cast<double>(kOutputRate) / target_fps_) + audio_frac_;
        int dst_pairs = static_cast<int>(exact);
        if (dst_pairs <= 0) {
            dst_pairs = 1;
        }
        audio_frac_ = exact - static_cast<double>(dst_pairs);

        acc_l_.assign(static_cast<std::size_t>(dst_pairs), 0);
        acc_r_.assign(static_cast<std::size_t>(dst_pairs), 0);
        if (fm_count > 0U) {
            add_source(acc_l_.data(), acc_r_.data(), fm_buf_.data(), 2, static_cast<int>(fm_count),
                       kGainFm, dst_pairs);
        }
        if (psg_count > 0U) {
            add_source(acc_l_.data(), acc_r_.data(), psg_buf_.data(), 1,
                       static_cast<int>(psg_count), kGainPsg, dst_pairs);
        }
        if (pwm_count > 0U) {
            add_source(acc_l_.data(), acc_r_.data(), pwm_buf_.data(), 2,
                       static_cast<int>(pwm_count), kGainPwm, dst_pairs);
        }

        mix_buf_.resize(static_cast<std::size_t>(dst_pairs) * 2U);
        for (std::size_t i = 0; i < static_cast<std::size_t>(dst_pairs); ++i) {
            mix_buf_[i * 2U] = clip_i16(acc_l_[i]);
            mix_buf_[i * 2U + 1U] = clip_i16(acc_r_[i]);
        }
        return {.samples = mix_buf_.data(),
                .frame_count = static_cast<std::uint32_t>(dst_pairs),
                .sample_rate = kOutputRate};
    }

    void force_link() noexcept {}

    namespace {
        const auto register_sega32x = [] {
            mnemos::frontend_sdk::adapter_registry::instance().register_family(
                "sega32x",
                [](mnemos::frontend_sdk::adapter_options opts)
                    -> std::unique_ptr<mnemos::frontend_sdk::player_system> {
                    manifests::sega32x::sega32x_bios bios;
                    if (opts.bios_images.size() > 0U) {
                        bios.m_bios = std::move(opts.bios_images[0]);
                    }
                    if (opts.bios_images.size() > 1U) {
                        bios.s_bios = std::move(opts.bios_images[1]);
                    }
                    if (opts.bios_images.size() > 2U) {
                        bios.g_bios = std::move(opts.bios_images[2]);
                    }
                    return std::make_unique<sega32x_adapter>(
                        std::move(opts.rom), std::move(bios),
                        manifests::genesis::genesis_config{.video_region = opts.video_region},
                        std::move(opts.display_name), opts.scheduler_factory_override);
                });
            return 0;
        }();
    } // namespace

} // namespace mnemos::apps::player::adapters::sega32x
