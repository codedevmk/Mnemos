// Sega CD CDD (CD drive controller): 10-byte command/status frames exchanged
// through the gate array, a seek/play state machine, and the 75 Hz CD-frame
// tick that advances the read head and feeds the decoder. Ported from the Emu
// reference (systems/sega/segacd). The CDC decode (C2) and CD-DA pump (C3) are
// reached through cdc_decoder_update()/cdda_play()/cdda_stop() seams.

#include "segacd_system.hpp"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <span>

namespace mnemos::manifests::segacd {
    namespace {
        // Opt-in CDD command trace (MNEMOS_SEGACD_TRACE) for disc-boot debugging.
        bool cdd_trace_enabled() {
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996) // std::getenv: opt-in diagnostic, not hot-path
#endif
            static const bool on = std::getenv("MNEMOS_SEGACD_TRACE") != nullptr;
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
            return on;
        }
        std::uint8_t bcd_byte(std::uint32_t v) {
            return static_cast<std::uint8_t>(((v / 10U) << 4U) | (v % 10U));
        }
        std::uint8_t cdd_checksum(const std::array<std::uint8_t, 10>& frame) {
            std::uint8_t sum = 0;
            for (int i = 0; i < 9; ++i) {
                sum = static_cast<std::uint8_t>(sum + (frame[static_cast<std::size_t>(i)] & 0x0FU));
            }
            return static_cast<std::uint8_t>(~sum & 0x0FU);
        }
    } // namespace

    void segacd_system::attach_disc(const mnemos::disc::disc_image* image) {
        disc = image;
        cdd_loaded = (image != nullptr) && (image->total_sectors() > 0U);
        if (cdd_loaded) {
            // The drive starts STOPPED, reporting the all-zero idle frame
            // (cdd_set_status) that the sub-CPU BIOS's early CDD sync validates --
            // it requires RS0=0 there, so starting at cdd_toc (RS0=9) regresses the
            // boot back to the $132C handshake wait. The correct STOP->TOC handover
            // needs the register-vs-internal-state decoupling (the handshake reads
            // the INITIAL all-zero registers, then Get-Status reports TOC) -- TODO.
            cdd_drive_status = cdd_stop;
            cdd_lba = 0;
        } else {
            cdd_drive_status = cdd_nodisc;
            cdd_lba = 0;
        }
        cdd_track = disc_track_of_lba(cdd_lba);
        cdd_pending_status = 0;
        cdd_latency = 0;
        cdda_active = false;
        cdd_set_status();
    }

    std::uint32_t segacd_system::disc_total_lbas() const {
        return disc != nullptr ? disc->total_sectors() : 0U;
    }

    bool segacd_system::disc_lba_is_data(std::int32_t lba) const {
        if (disc == nullptr || lba < 0) {
            return true;
        }
        const auto* t = disc->find_track(static_cast<std::uint32_t>(lba));
        return t == nullptr || t->type != mnemos::disc::track_type::audio;
    }

    int segacd_system::disc_track_of_lba(std::int32_t lba) const {
        if (disc == nullptr || lba < 0) {
            return 0;
        }
        const auto* t = disc->find_track(static_cast<std::uint32_t>(lba));
        if (t == nullptr) {
            return 0;
        }
        return static_cast<int>(t - disc->tracks().data());
    }

    void segacd_system::cdd_commit_status() {
        cdd_status[9] = cdd_checksum(cdd_status);
        for (std::size_t i = 0; i < 10; ++i) {
            gate_array[0x38U + i] = cdd_status[i];
        }
    }

    void segacd_system::cdd_set_status() {
        // RS0 = drive status. For STOP or any state ABOVE PAUSE (OPEN/TOC/NO_DISC/
        // END) the drive is not positioned, so RS1..RS8 stay zero (an all-zero MSF
        // with a valid RS9 checksum) -- the BIOS reads RS0 for the drive state. Only
        // the active states (PLAY/SEEK/SCAN/PAUSE) report the current absolute MSF.
        // Mirrors the reference Get-Status, which skips RS1..RS8 outside PLAY..PAUSE
        // (so STOP stays the all-zero frame and a loaded TOC drive reads as RS0=9).
        cdd_status[0] = static_cast<std::uint8_t>(cdd_drive_status & 0x0FU);
        if (cdd_drive_status == cdd_stop || cdd_drive_status > cdd_pause) {
            for (std::size_t i = 1; i < 9; ++i) {
                cdd_status[i] = 0;
            }
            // RS9 = $F for an unpositioned drive (STOP/OPEN/TOC/NO_DISC/END). The
            // BIOS's CDD sync wants RS8-RS9 = $000F regardless of RS0, so this is a
            // FIXED idle-frame trailer, not the RS0-inclusive checksum -- the
            // reference leaves the init RS9=$F untouched in these states, so a loaded
            // TOC drive reads as RS0=9 with RS9=$F (a stopped drive as RS0=0/RS9=$F).
            cdd_status[9] = 0x0FU;
            for (std::size_t i = 0; i < 10; ++i) {
                gate_array[0x38U + i] = cdd_status[i];
            }
            return;
        }
        std::int32_t abs_lba = cdd_lba + 150; // absolute = +2 s pre-gap
        if (abs_lba < 0) {
            abs_lba = 0;
        }
        std::uint32_t total = static_cast<std::uint32_t>(abs_lba);
        const std::uint32_t f = total % 75U;
        total /= 75U;
        const std::uint32_t s = total % 60U;
        const std::uint32_t m = total / 60U;
        // The status frame = TEN single-digit bytes (RS0..RS9), one BCD digit per
        // byte (low nibble). RS2..RS7 = MM SS FF as six separate digits.
        cdd_status[1] = 0x00;
        cdd_status[2] = static_cast<std::uint8_t>(m / 10U);
        cdd_status[3] = static_cast<std::uint8_t>(m % 10U);
        cdd_status[4] = static_cast<std::uint8_t>(s / 10U);
        cdd_status[5] = static_cast<std::uint8_t>(s % 10U);
        cdd_status[6] = static_cast<std::uint8_t>(f / 10U);
        cdd_status[7] = static_cast<std::uint8_t>(f % 10U);
        cdd_status[8] = disc_lba_is_data(cdd_lba) ? std::uint8_t{0x04U} : std::uint8_t{0x00U};
        cdd_commit_status();
    }

    void segacd_system::cdd_report_toc() {
        const std::uint8_t sub = cdd_command[3]; // $45 = TOC sub-command (the reference
                                                  // reads the $44 word's LOW byte, $45,
                                                  // not $44 -- $44 is the high/unused byte)
        for (std::size_t i = 1; i <= 8; ++i) {
            cdd_status[i] = 0;
        }
        cdd_status[0] = static_cast<std::uint8_t>(cdd_drive_status & 0x0FU);
        cdd_status[1] = static_cast<std::uint8_t>(sub & 0x0FU);
        const int tc = disc != nullptr ? disc->track_count() : 0;

        // RS2..RS7 = MM:SS:FF as six single BCD digits (one per byte, low nibble).
        const auto put_msf = [this](std::uint32_t abslba) {
            const std::uint32_t ff = abslba % 75U;
            const std::uint32_t rem = abslba / 75U;
            const std::uint32_t ss = rem % 60U;
            const std::uint32_t mm = rem / 60U;
            cdd_status[2] = static_cast<std::uint8_t>(mm / 10U);
            cdd_status[3] = static_cast<std::uint8_t>(mm % 10U);
            cdd_status[4] = static_cast<std::uint8_t>(ss / 10U);
            cdd_status[5] = static_cast<std::uint8_t>(ss % 10U);
            cdd_status[6] = static_cast<std::uint8_t>(ff / 10U);
            cdd_status[7] = static_cast<std::uint8_t>(ff % 10U);
        };

        switch (sub & 0x0FU) {
        case 0x00: { // current absolute time
            std::int32_t lba = cdd_lba + 150;
            if (lba < 0) {
                lba = 0;
            }
            put_msf(static_cast<std::uint32_t>(lba));
            cdd_status[8] = disc_lba_is_data(cdd_lba) ? std::uint8_t{0x04U} : std::uint8_t{0x00U};
            break;
        }
        case 0x01: { // current track-relative time
            const int trk = disc_track_of_lba(cdd_lba);
            const std::uint32_t start =
                (disc != nullptr && tc > 0)
                    ? disc->tracks()[static_cast<std::size_t>(trk)].start_lba
                    : 0U;
            std::int32_t rel = cdd_lba - static_cast<std::int32_t>(start);
            if (rel < 0) {
                rel = -rel;
            }
            put_msf(static_cast<std::uint32_t>(rel));
            cdd_status[8] = disc_lba_is_data(cdd_lba) ? std::uint8_t{0x04U} : std::uint8_t{0x00U};
            break;
        }
        case 0x02: { // current track number
            const std::uint32_t trk = static_cast<std::uint32_t>(disc_track_of_lba(cdd_lba)) + 1U;
            cdd_status[2] = static_cast<std::uint8_t>(trk / 10U);
            cdd_status[3] = static_cast<std::uint8_t>(trk % 10U);
            break;
        }
        case 0x03: { // total disc length
            put_msf(disc_total_lbas() + 150U);
            break;
        }
        case 0x04: { // first / last track numbers
            const std::uint32_t last =
                (disc != nullptr && tc > 0) ? static_cast<std::uint32_t>(tc) : 1U;
            cdd_status[2] = 0U;
            cdd_status[3] = 1U; // first track = 01
            cdd_status[4] = static_cast<std::uint8_t>(last / 10U);
            cdd_status[5] = static_cast<std::uint8_t>(last % 10U);
            break;
        }
        case 0x05: { // track start MSF for the track number at $46-$47
            // Track number = two single BCD digits: $46 = tens, $47 = units (one
            // digit per command byte, like the rest of the CDD frame). Reading both
            // nibbles of $46 alone instead returns the wrong track -- e.g. track 1,
            // sent as $46=0/$47=1, reads as track 0 -- so the BIOS gets a bad IP LBA.
            const std::uint32_t track =
                static_cast<std::uint32_t>(cdd_command[4]) * 10U +
                static_cast<std::uint32_t>(cdd_command[5]);
            std::uint32_t start = 0U;
            bool is_data = true;
            if (disc != nullptr && tc > 0 && track >= 1U &&
                track <= static_cast<std::uint32_t>(tc)) {
                const auto& t = disc->tracks()[track - 1U];
                start = t.start_lba;
                is_data = (t.type != mnemos::disc::track_type::audio);
            } else {
                start = disc_total_lbas(); // lead-out fallback
            }
            put_msf(start + 150U);
            // RS6 high nibble bit 3 flags a CD-ROM (data) track; RS8 = track low digit.
            cdd_status[6] = static_cast<std::uint8_t>(cdd_status[6] | (is_data ? 0x08U : 0x00U));
            cdd_status[8] = static_cast<std::uint8_t>(track % 10U);
            break;
        }
        default:
            break;
        }
        cdd_commit_status();
    }

    std::int32_t segacd_system::cdd_seek_target_lba() const {
        const int m = cdd_command[2] * 10 + cdd_command[3];
        const int s = cdd_command[4] * 10 + cdd_command[5];
        const int f = cdd_command[6] * 10 + cdd_command[7];
        return static_cast<std::int32_t>((m * 60 + s) * 75 + f) - 150;
    }

    void segacd_system::cdd_process_command() {
        // The CDD command code is the LOW nibble of the first command byte ($42),
        // exactly like the status frame's RS0 (one BCD digit per byte). Decoding the
        // HIGH nibble instead silently turns every real command (Play $03, Seek $04,
        // Read, Report-TOC $02, ...) into Get-Status ($00) -- the drive then never
        // seeks or reads, which stalls the disc-boot at Get-Status forever.
        const auto cmd = static_cast<std::uint8_t>(cdd_command[0] & 0x0FU);
        if (cdd_trace_enabled()) {
            std::fprintf(stderr, "[cdd] cmd=%X bytes=%02X%02X%02X%02X%02X%02X status=%02X lba=%d\n",
                         cmd, cdd_command[1], cdd_command[2], cdd_command[3], cdd_command[4],
                         cdd_command[5], cdd_command[6], cdd_drive_status, cdd_lba);
        }
        switch (cmd) {
        case 0x00: // Get Drive Status
            cdd_set_status();
            return;
        case 0x01: // Stop Drive
            cdda_stop();
            cdd_drive_status = cdd_loaded ? std::uint8_t{cdd_toc} : std::uint8_t{cdd_nodisc};
            cdd_pending_status = 0;
            cdd_latency = 0;
            cdd_status[0] = cdd_stop;
            cdd_status[1] = 0x0F;
            for (std::size_t i = 2; i <= 8; ++i) {
                cdd_status[i] = 0;
            }
            cdd_commit_status();
            return;
        case 0x02: // Report TOC
            cdd_report_toc();
            return;
        case 0x03: { // Play
            std::int32_t lba = cdd_seek_target_lba();
            if (lba < 0) {
                lba = 0;
            }
            cdd_lba = lba;
            cdd_track = disc_track_of_lba(lba);
            cdd_pending_status = cdd_play;
            cdd_latency = 3; // seek interrupt delay
            cdd_drive_status = cdd_seek;
            if (!disc_lba_is_data(lba)) {
                cdda_play(static_cast<std::uint32_t>(lba), disc_total_lbas());
            } else {
                cdda_stop();
            }
            cdd_status[0] = cdd_seek;
            cdd_status[1] = 0x0F;
            for (std::size_t i = 2; i <= 8; ++i) {
                cdd_status[i] = 0;
            }
            cdd_commit_status();
            return;
        }
        case 0x04: { // Seek
            std::int32_t lba = cdd_seek_target_lba();
            if (lba < 0) {
                lba = 0;
            }
            cdd_lba = lba;
            cdd_track = disc_track_of_lba(lba);
            cdd_pending_status = cdd_pause;
            cdd_latency = 3;
            cdd_drive_status = cdd_seek;
            cdda_stop();
            cdd_status[0] = cdd_seek;
            cdd_status[1] = 0x0F;
            for (std::size_t i = 2; i <= 8; ++i) {
                cdd_status[i] = 0;
            }
            cdd_commit_status();
            return;
        }
        case 0x06: // Pause
            cdda_stop();
            cdd_drive_status = cdd_pause;
            break;
        case 0x07: // Resume
            cdd_drive_status = cdd_play;
            if (!disc_lba_is_data(cdd_lba)) {
                cdda_play(static_cast<std::uint32_t>(cdd_lba < 0 ? 0 : cdd_lba), disc_total_lbas());
            }
            break;
        case 0x08: // Forward Scan
        case 0x09: // Rewind Scan
            cdd_drive_status = cdd_scan;
            break;
        case 0x0A: // Track Jump
            cdd_drive_status = cdd_pause;
            break;
        case 0x0C: // Close Tray
            cdd_drive_status = cdd_loaded ? std::uint8_t{cdd_toc} : std::uint8_t{cdd_nodisc};
            cdd_pending_status = 0;
            cdd_status[0] = cdd_stop;
            cdd_status[1] = 0x0F;
            for (std::size_t i = 2; i <= 8; ++i) {
                cdd_status[i] = 0;
            }
            cdd_commit_status();
            return;
        case 0x0D: // Open Tray
            cdd_drive_status = cdd_open;
            break;
        default:
            break;
        }
        cdd_set_status();
    }

    void segacd_system::cdd_update() {
        // STOP->TOC handover: the drive starts STOPPED so the BIOS's early CDD sync
        // (which requires the all-zero RS0=0 frame and re-runs through the boot
        // handshake) passes. Once the MAIN grants the sub word RAM via DMNA ($03
        // bit 1) -- which happens only AFTER the handshake, to stage the IP load --
        // a loaded drive leaves STOP for TOC, so Get-Status reports a loaded disc
        // (RS0=9) and the BIOS proceeds to Read-TOC / Seek / Read the IP.
        if (cdd_loaded && cdd_drive_status == cdd_stop && (gate_array[0x03] & 0x02U) != 0U) {
            cdd_drive_status = cdd_toc;
        }
        if (cdd_latency > 0) {
            --cdd_latency;
        } else if (cdd_drive_status == cdd_play) {
            const std::uint32_t total = disc_total_lbas();
            if (total == 0U || (cdd_lba >= 0 && static_cast<std::uint32_t>(cdd_lba) >= total)) {
                cdd_drive_status = cdd_end;
            } else if (disc_lba_is_data(cdd_lba)) {
                const auto msf = static_cast<std::uint32_t>(cdd_lba + 150);
                const std::uint8_t hm = bcd_byte((msf / 75U) / 60U);
                const std::uint8_t hs = bcd_byte((msf / 75U) % 60U);
                const std::uint8_t hf = bcd_byte(msf % 75U);
                std::uint8_t mode = 0x01U;
                if (disc != nullptr) {
                    mode = (disc->mode_at(static_cast<std::uint32_t>(cdd_lba)) ==
                            mnemos::disc::sector_mode::mode1)
                               ? std::uint8_t{0x01U}
                               : std::uint8_t{0x02U};
                }
                const std::uint32_t header = static_cast<std::uint32_t>(hm) |
                                             (static_cast<std::uint32_t>(hs) << 8U) |
                                             (static_cast<std::uint32_t>(hf) << 16U) |
                                             (static_cast<std::uint32_t>(mode) << 24U);
                cdc_decoder_update(header);
                ++cdd_lba;
            } else {
                cdc_decoder_update(0U); // audio track -- CD-DA pump handles output
                ++cdd_lba;
            }
        } else {
            cdc_decoder_update(0U); // keep the decoder ticking between sectors
        }

        if (cdd_pending_status != 0 && cdd_latency == 0) {
            cdd_drive_status = cdd_pending_status;
            cdd_pending_status = 0;
            cdd_track = disc_track_of_lba(cdd_lba);
        }
        cdd_set_status();

        // CDD level-4 IRQ -- the per-CD-frame "BIOS frame tick" that drives the
        // sub-CPU's disc state machine. It fires only while CDD communication is
        // enabled ($37 bit 2 / HOCK, set by the BIOS during CDD init); that gate
        // is what lets the disc logic advance past Get-Status to Read/Play.
        if (cdd_loaded && (gate_array[0x37] & 0x04U) != 0U) {
            raise_sub_irq(irq_cdd);
        }
    }

    // ---- CD-DA (Red Book audio) ----
    void segacd_system::cdda_play(std::uint32_t start, std::uint32_t end) {
        if (end < start) {
            end = start;
        }
        cdda_start_lba = start;
        cdda_end_lba = end;
        cdda_current_lba = start;
        cdda_sample_in_sector = 0;
        cdda_loop = false; // the CDD never loops; direct API callers could
        cdda_active = true;
    }

    void segacd_system::cdda_stop() {
        cdda_active = false;
        cdda_sample_in_sector = 0;
    }

    bool segacd_system::cdda_next_sample(std::int16_t& out_l, std::int16_t& out_r) {
        if (!cdda_active || disc == nullptr) {
            return false;
        }
        // One 2352-byte raw sector = 588 stereo little-endian 16-bit samples.
        constexpr std::uint16_t samples_per_frame = 588U;
        std::array<std::uint8_t, mnemos::disc::disc_image::raw_sector_size> sector{};
        if (!disc->read_raw_sector(
                cdda_current_lba,
                std::span<std::uint8_t, mnemos::disc::disc_image::raw_sector_size>{sector})) {
            cdda_active = false;
            return false;
        }
        const std::size_t base = static_cast<std::size_t>(cdda_sample_in_sector) * 4U;
        out_l = static_cast<std::int16_t>(static_cast<std::uint16_t>(sector[base]) |
                                          (static_cast<std::uint16_t>(sector[base + 1]) << 8U));
        out_r = static_cast<std::int16_t>(static_cast<std::uint16_t>(sector[base + 2]) |
                                          (static_cast<std::uint16_t>(sector[base + 3]) << 8U));
        ++cdda_sample_in_sector;
        if (cdda_sample_in_sector >= samples_per_frame) {
            cdda_sample_in_sector = 0;
            ++cdda_current_lba;
            if (cdda_current_lba > cdda_end_lba) {
                if (cdda_loop) {
                    cdda_current_lba = cdda_start_lba;
                } else {
                    cdda_active = false;
                }
            }
        }
        return true;
    }

} // namespace mnemos::manifests::segacd
