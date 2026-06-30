#include "amiga_system.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <utility>

namespace mnemos::manifests::amiga {

    namespace {
        constexpr std::uint32_t state_version = 26U;

        constexpr std::uint16_t reg_dmaconr = 0x002U;
        constexpr std::uint16_t reg_vposr = 0x004U;
        constexpr std::uint16_t reg_vhposr = 0x006U;
        constexpr std::uint16_t reg_joy0dat = 0x00AU;
        constexpr std::uint16_t reg_joy1dat = 0x00CU;
        constexpr std::uint16_t reg_clxdat = 0x00EU;
        constexpr std::uint16_t reg_adkconr = 0x010U;
        constexpr std::uint16_t reg_pot0dat = 0x012U;
        constexpr std::uint16_t reg_pot1dat = 0x014U;
        constexpr std::uint16_t reg_potinp = 0x016U;
        constexpr std::uint16_t reg_dskbytr = 0x01AU;
        constexpr std::uint16_t reg_intenar = 0x01CU;
        constexpr std::uint16_t reg_intreqr = 0x01EU;
        constexpr std::uint16_t reg_dskpth = 0x020U;
        constexpr std::uint16_t reg_dskptl = 0x022U;
        constexpr std::uint16_t reg_dsklen = 0x024U;
        constexpr std::uint16_t reg_dskdat = 0x026U;
        constexpr std::uint16_t reg_copcon = 0x02EU;
        constexpr std::uint16_t reg_potgo = 0x034U;
        constexpr std::uint16_t reg_joytest = 0x036U;
        constexpr std::uint16_t reg_dsksync = 0x07EU;
        constexpr std::uint16_t reg_bltcon0 = 0x040U;
        constexpr std::uint16_t reg_bltcon1 = 0x042U;
        constexpr std::uint16_t reg_bltafwm = 0x044U;
        constexpr std::uint16_t reg_bltalwm = 0x046U;
        constexpr std::uint16_t reg_bltcpt = 0x048U;
        constexpr std::uint16_t reg_bltbpt = 0x04CU;
        constexpr std::uint16_t reg_bltapt = 0x050U;
        constexpr std::uint16_t reg_bltdpt = 0x054U;
        constexpr std::uint16_t reg_bltsize = 0x058U;
        constexpr std::uint16_t reg_bltcmod = 0x060U;
        constexpr std::uint16_t reg_bltbmod = 0x062U;
        constexpr std::uint16_t reg_bltamod = 0x064U;
        constexpr std::uint16_t reg_bltdmod = 0x066U;
        constexpr std::uint16_t reg_bltcdat = 0x070U;
        constexpr std::uint16_t reg_bltbdat = 0x072U;
        constexpr std::uint16_t reg_bltadat = 0x074U;
        constexpr std::uint16_t reg_bltddat = 0x076U;
        constexpr std::uint16_t reg_cop1lch = 0x080U;
        constexpr std::uint16_t reg_cop1lcl = 0x082U;
        constexpr std::uint16_t reg_cop2lch = 0x084U;
        constexpr std::uint16_t reg_cop2lcl = 0x086U;
        constexpr std::uint16_t reg_copjmp1 = 0x088U;
        constexpr std::uint16_t reg_copjmp2 = 0x08AU;
        constexpr std::uint16_t reg_diwstrt = 0x08EU;
        constexpr std::uint16_t reg_diwstop = 0x090U;
        constexpr std::uint16_t reg_ddfstrt = 0x092U;
        constexpr std::uint16_t reg_ddfstop = 0x094U;
        constexpr std::uint16_t reg_dmacon = 0x096U;
        constexpr std::uint16_t reg_clxcon = 0x098U;
        constexpr std::uint16_t reg_intena = 0x09AU;
        constexpr std::uint16_t reg_intreq = 0x09CU;
        constexpr std::uint16_t reg_adkcon = 0x09EU;
        constexpr std::uint16_t reg_aud_base = 0x0A0U;
        constexpr std::uint16_t reg_aud_stride = 0x010U;
        constexpr std::uint16_t reg_bplpt_base = 0x0E0U;
        constexpr std::uint16_t reg_bplcon0 = 0x100U;
        constexpr std::uint16_t reg_bplcon1 = 0x102U;
        constexpr std::uint16_t reg_bplcon2 = 0x104U;
        constexpr std::uint16_t reg_bplcon3 = 0x106U;
        constexpr std::uint16_t reg_bpl1mod = 0x108U;
        constexpr std::uint16_t reg_bpl2mod = 0x10AU;
        constexpr std::uint16_t reg_sprpt_base = 0x120U;
        constexpr std::uint16_t reg_spr_base = 0x140U;
        constexpr std::uint16_t reg_color_base = 0x180U;
        constexpr std::uint16_t bltcon0_usea = 0x0800U;
        constexpr std::uint16_t bltcon0_useb = 0x0400U;
        constexpr std::uint16_t bltcon0_usec = 0x0200U;
        constexpr std::uint16_t bltcon0_used = 0x0100U;
        constexpr std::uint16_t bltcon1_efe = 0x0010U;
        constexpr std::uint16_t bltcon1_ife = 0x0008U;
        constexpr std::uint16_t bltcon1_fci = 0x0004U;
        constexpr std::uint16_t bltcon1_desc = 0x0002U;
        constexpr std::uint16_t bltcon1_line = 0x0001U;
        constexpr std::uint16_t bltcon1_line_sign = 0x0040U;
        constexpr std::uint16_t bltcon1_line_sud = 0x0010U;
        constexpr std::uint16_t bltcon1_line_sul = 0x0008U;
        constexpr std::uint16_t bltcon1_line_aul = 0x0004U;
        constexpr std::uint16_t bltcon1_line_sing = 0x0002U;
        constexpr std::uint16_t adkcon_wordsync = 0x0400U;
        constexpr std::uint32_t chip_dma_address_mask = 0x001FFFFEU;
        constexpr std::uint32_t chip_dma_address_high_mask = 0x001FU;
        constexpr std::uint32_t ocs_disk_dma_address_mask = 0x0007FFFFU;
        constexpr std::size_t blit_a = 0U;
        constexpr std::size_t blit_b = 1U;
        constexpr std::size_t blit_c = 2U;
        constexpr std::size_t blit_d = 3U;
        static_assert(chips::video::agnus::scanlines_ntsc - amiga_pot_reset_scanlines ==
                      amiga_pot_full_scale_scanlines);
        constexpr std::uint32_t non_nasty_blitter_release_wait_cycles = 6U;
        constexpr std::uint16_t weak_bit_lfsr_seed = amiga_floppy_weak_bit_lfsr_seed;
        constexpr std::uint16_t weak_bit_lfsr_feedback = 0xB400U;

        [[nodiscard]] std::uint32_t saturating_add(std::uint32_t lhs, std::uint32_t rhs) noexcept {
            const std::uint32_t room = std::numeric_limits<std::uint32_t>::max() - lhs;
            return rhs > room ? std::numeric_limits<std::uint32_t>::max() : lhs + rhs;
        }

        [[nodiscard]] zorro2_expansion_board*
        configured_zorro2_memory_board_at(amiga_system& sys, std::uint32_t address) noexcept {
            for (auto& board : sys.zorro2_boards) {
                if (!board.memory || !board.configured || board.shut_up ||
                    board.memory_size == 0U) {
                    continue;
                }
                if (address < board.assigned_base) {
                    continue;
                }
                const std::uint32_t offset = address - board.assigned_base;
                if (offset < board.memory_size) {
                    return &board;
                }
            }
            return nullptr;
        }

        void advance_zorro2_autoconfig(amiga_system& sys) noexcept {
            sys.zorro2_base_low_nibble = 0U;
            sys.zorro2_base_low_nibble_valid = false;
            if (sys.zorro2_autoconfig_index < sys.zorro2_boards.size()) {
                ++sys.zorro2_autoconfig_index;
            }
            while (sys.zorro2_autoconfig_index < sys.zorro2_boards.size()) {
                const auto& board = sys.zorro2_boards[sys.zorro2_autoconfig_index];
                if (!board.configured && !board.shut_up) {
                    return;
                }
                ++sys.zorro2_autoconfig_index;
            }
        }

        void zorro2_autoconfig_write(amiga_system& sys, std::uint32_t address,
                                     std::uint8_t value) noexcept {
            auto* board = sys.active_zorro2_autoconfig_board();
            if (board == nullptr) {
                return;
            }
            const std::uint32_t physical =
                (address - amiga_system::zorro2_autoconfig_base) & 0xFFFFU;
            const std::uint32_t even_physical = physical & ~0x01U;
            const bool base_high = even_physical == 0x48U || even_physical == 0x24U;
            const bool base_low = even_physical == 0x4AU || even_physical == 0x26U;
            const bool shut_up = even_physical == 0x4CU || even_physical == 0x4EU ||
                                 even_physical == 0x28U || even_physical == 0x2AU;
            if (base_low) {
                sys.zorro2_base_low_nibble = zorro2_write_nibble(value);
                sys.zorro2_base_low_nibble_valid = true;
                return;
            }
            if (base_high) {
                const bool encoded_nibble = (value & 0xF0U) == 0U;
                const std::uint8_t high = zorro2_write_nibble(value);
                const std::uint8_t low = sys.zorro2_base_low_nibble_valid
                                             ? sys.zorro2_base_low_nibble
                                             : static_cast<std::uint8_t>(
                                                   encoded_nibble ? 0U : (value & 0x0FU));
                board->assigned_base = static_cast<std::uint32_t>(
                    static_cast<std::uint32_t>((high << 4U) | low) << 16U);
                board->configured = true;
                board->shut_up = false;
                advance_zorro2_autoconfig(sys);
                return;
            }
            if (shut_up) {
                board->shut_up = true;
                board->configured = false;
                board->assigned_base = 0U;
                advance_zorro2_autoconfig(sys);
            }
        }

        [[nodiscard]] std::uint32_t chip_ram_address_mask(std::size_t size) noexcept {
            if (size >= amiga_system::chip_ram_size_1m) {
                return 0x000FFFFFU;
            }
            return ocs_disk_dma_address_mask;
        }

        [[nodiscard]] std::uint16_t chip_ram_address_high_mask(std::size_t size) noexcept {
            return static_cast<std::uint16_t>((chip_ram_address_mask(size) >> 16U) & 0x001FU);
        }

        [[nodiscard]] std::uint16_t apply_setclr(std::uint16_t current, std::uint16_t value,
                                                 std::uint16_t writable) noexcept {
            const std::uint16_t payload = value & writable;
            if ((value & amiga_system::setclr_bit) != 0U) {
                return static_cast<std::uint16_t>(current | payload);
            }
            return static_cast<std::uint16_t>(current & ~payload);
        }

        [[nodiscard]] std::uint32_t blitter_area_dma_slots(std::uint16_t control) noexcept {
            std::uint32_t slots = 0U;
            slots += (control & bltcon0_usea) != 0U ? 1U : 0U;
            slots += (control & bltcon0_useb) != 0U ? 1U : 0U;
            slots += (control & bltcon0_usec) != 0U ? 1U : 0U;
            slots += (control & bltcon0_used) != 0U ? 1U : 0U;
            return slots == 0U ? 1U : slots;
        }

        [[nodiscard]] std::uint32_t blitter_line_dma_slots(std::uint16_t control) noexcept {
            std::uint32_t slots = 1U; // line stepping still burns a blitter cycle.
            slots += (control & bltcon0_usec) != 0U ? 1U : 0U;
            slots += (control & bltcon0_used) != 0U ? 1U : 0U;
            return slots;
        }

        [[nodiscard]] std::uint64_t blitter_cycle_budget(std::uint32_t words_or_dots,
                                                         std::uint32_t height_or_one,
                                                         std::uint32_t slots) noexcept {
            return static_cast<std::uint64_t>(words_or_dots) *
                   static_cast<std::uint64_t>(height_or_one) *
                   static_cast<std::uint64_t>(slots == 0U ? 1U : slots);
        }

        [[nodiscard]] std::uint8_t raw_track_bit_at_phase(std::span<const std::uint8_t> track,
                                                          std::size_t stream_offset,
                                                          std::uint8_t stream_bit_offset) noexcept {
            if (track.empty()) {
                return 0U;
            }
            const std::size_t offset = stream_offset % track.size();
            const std::uint8_t bit = static_cast<std::uint8_t>(stream_bit_offset & 0x07U);
            return static_cast<std::uint8_t>((track[offset] >> static_cast<unsigned>(7U - bit)) &
                                             0x01U);
        }

        void write_raw_track_bit_at_phase(std::vector<std::uint8_t>& track,
                                          std::size_t stream_offset, std::uint8_t stream_bit_offset,
                                          std::uint8_t value) noexcept {
            if (track.empty()) {
                return;
            }
            const std::size_t offset = stream_offset % track.size();
            const std::uint8_t bit = static_cast<std::uint8_t>(stream_bit_offset & 0x07U);
            const auto mask = static_cast<std::uint8_t>(1U << static_cast<unsigned>(7U - bit));
            if ((value & 0x01U) != 0U) {
                track[offset] = static_cast<std::uint8_t>(track[offset] | mask);
            } else {
                track[offset] = static_cast<std::uint8_t>(track[offset] & ~mask);
            }
        }

        [[nodiscard]] bool has_marked_weak_bits(std::span<const std::uint8_t> weak_bits) noexcept {
            return std::any_of(weak_bits.begin(), weak_bits.end(),
                               [](std::uint8_t value) { return value != 0U; });
        }

        [[nodiscard]] std::uint8_t
        advance_weak_bit_lfsr(amiga_system::floppy_drive_state& drive) noexcept {
            std::uint16_t state =
                drive.weak_bit_lfsr == 0U ? weak_bit_lfsr_seed : drive.weak_bit_lfsr;
            const bool feedback = (state & 0x0001U) != 0U;
            state = static_cast<std::uint16_t>(state >> 1U);
            if (feedback) {
                state = static_cast<std::uint16_t>(state ^ weak_bit_lfsr_feedback);
            }
            drive.weak_bit_lfsr = state == 0U ? weak_bit_lfsr_seed : state;
            return static_cast<std::uint8_t>(drive.weak_bit_lfsr & 0x0001U);
        }

        [[nodiscard]] std::uint8_t
        sample_raw_track_bit(amiga_system::floppy_drive_state& drive) noexcept {
            if (drive.weak_bit_stream.size() == drive.track_stream.size() &&
                raw_track_bit_at_phase(std::span<const std::uint8_t>(drive.weak_bit_stream),
                                       drive.stream_offset, drive.stream_bit_offset) != 0U) {
                return advance_weak_bit_lfsr(drive);
            }
            return raw_track_bit_at_phase(std::span<const std::uint8_t>(drive.track_stream),
                                          drive.stream_offset, drive.stream_bit_offset);
        }

        void advance_raw_track_phase(amiga_system::floppy_drive_state& drive) noexcept {
            if (drive.track_stream.empty()) {
                drive.stream_offset = 0U;
                drive.stream_bit_offset = 0U;
                return;
            }

            drive.stream_bit_offset =
                static_cast<std::uint8_t>((drive.stream_bit_offset + 1U) & 0x07U);
            if (drive.stream_bit_offset == 0U) {
                drive.stream_offset = (drive.stream_offset + 1U) % drive.track_stream.size();
            }
        }

        void restore_raw_track_phase(amiga_system::floppy_drive_state& drive,
                                     std::size_t old_track_size, std::size_t old_stream_offset,
                                     std::uint8_t old_stream_bit_offset) noexcept {
            if (old_track_size == 0U || drive.track_stream.empty()) {
                drive.stream_offset = 0U;
                drive.stream_bit_offset = 0U;
                return;
            }

            const std::uint64_t old_bits = static_cast<std::uint64_t>(old_track_size) * 8U;
            const std::uint64_t new_bits =
                static_cast<std::uint64_t>(drive.track_stream.size()) * 8U;
            const std::uint64_t old_phase =
                ((static_cast<std::uint64_t>(old_stream_offset % old_track_size) * 8U) +
                 static_cast<std::uint64_t>(old_stream_bit_offset & 0x07U)) %
                old_bits;
            const std::uint64_t new_phase =
                old_bits == new_bits ? old_phase : ((old_phase * new_bits) / old_bits);

            drive.stream_offset =
                static_cast<std::size_t>((new_phase / 8U) % drive.track_stream.size());
            drive.stream_bit_offset = static_cast<std::uint8_t>(new_phase & 0x07U);
        }

        void normalize_kickstart(std::span<std::uint8_t> dst, std::span<const std::uint8_t> src) {
            std::fill(dst.begin(), dst.end(), std::uint8_t{0xFFU});
            if (src.empty()) {
                return;
            }
            if (src.size() == dst.size()) {
                std::memcpy(dst.data(), src.data(), dst.size());
                return;
            }
            if (src.size() == dst.size() / 2U) {
                std::memcpy(dst.data(), src.data(), src.size());
                std::memcpy(dst.data() + src.size(), src.data(), src.size());
                return;
            }
            const std::size_t n = std::min(dst.size(), src.size());
            std::memcpy(dst.data(), src.data(), n);
        }

        [[nodiscard]] std::uint32_t merge_ptr(std::uint32_t old, bool high,
                                              std::uint16_t value) noexcept {
            if (high) {
                return ((static_cast<std::uint32_t>(value) & chip_dma_address_high_mask) << 16U) |
                       (old & 0x0000FFFEU);
            }
            return (old & (chip_dma_address_high_mask << 16U)) |
                   (static_cast<std::uint32_t>(value) & 0x0000FFFEU);
        }

        [[nodiscard]] std::uint16_t copper_address_high_mask(std::uint32_t address_mask) noexcept {
            return static_cast<std::uint16_t>((address_mask >> 16U) & 0x001FU);
        }

        [[nodiscard]] std::uint32_t merge_copper_ptr(std::uint32_t old, bool high,
                                                     std::uint16_t value,
                                                     std::uint32_t address_mask) noexcept {
            const std::uint16_t high_mask = copper_address_high_mask(address_mask);
            if (high) {
                return (((static_cast<std::uint32_t>(value) & high_mask) << 16U) |
                        (old & 0x0000FFFEU)) &
                       address_mask;
            }
            return ((old & (static_cast<std::uint32_t>(high_mask) << 16U)) |
                    (static_cast<std::uint32_t>(value) & 0x0000FFFEU)) &
                   address_mask;
        }

        [[nodiscard]] std::size_t mirrored_chip_word_address(std::span<const std::uint8_t> ram,
                                                             std::uint32_t byte_address) noexcept {
            const std::size_t mirrored_bytes = ram.size() & ~std::size_t{1U};
            if (mirrored_bytes == 0U) {
                return 0U;
            }
            return static_cast<std::size_t>(byte_address & 0x001FFFFEU) % mirrored_bytes;
        }

        [[nodiscard]] std::size_t mirrored_chip_byte_address(std::span<const std::uint8_t> ram,
                                                             std::uint32_t byte_address) noexcept {
            if (ram.empty()) {
                return 0U;
            }
            return static_cast<std::size_t>(byte_address & 0x001FFFFFU) % ram.size();
        }

        [[nodiscard]] std::uint16_t read_chip_word(std::span<const std::uint8_t> ram,
                                                   std::uint32_t byte_address) noexcept {
            if (ram.size() < 2U) {
                return 0U;
            }
            const std::size_t a = mirrored_chip_word_address(ram, byte_address);
            return static_cast<std::uint16_t>((ram[a] << 8U) | ram[a + 1U]);
        }

        void write_chip_word(amiga_system& sys, std::uint32_t byte_address,
                             std::uint16_t value) noexcept {
            if (sys.chip_ram.size() < 2U) {
                return;
            }
            const std::size_t a = mirrored_chip_word_address(sys.chip_ram, byte_address);
            sys.chip_ram[a] = static_cast<std::uint8_t>(value >> 8U);
            sys.chip_ram[a + 1U] = static_cast<std::uint8_t>(value);
            sys.paula.chipram()[a] = sys.chip_ram[a];
            sys.paula.chipram()[a + 1U] = sys.chip_ram[a + 1U];
        }

        [[nodiscard]] std::uint16_t shifted_word(std::uint16_t previous, std::uint16_t current,
                                                 std::uint16_t shift) noexcept {
            if (shift == 0U) {
                return current;
            }
            const std::uint32_t composite = (static_cast<std::uint32_t>(previous) << 16U) | current;
            return static_cast<std::uint16_t>((composite >> shift) & 0xFFFFU);
        }

        [[nodiscard]] std::uint16_t blitter_minterm(std::uint8_t minterm, std::uint16_t a,
                                                    std::uint16_t b, std::uint16_t c) noexcept {
            std::uint16_t out = 0U;
            for (std::uint16_t bit = 0U; bit < 16U; ++bit) {
                const std::uint16_t mask = static_cast<std::uint16_t>(1U << bit);
                const std::uint8_t term = static_cast<std::uint8_t>(
                    (((a & mask) != 0U) ? 0x04U : 0U) | (((b & mask) != 0U) ? 0x02U : 0U) |
                    (((c & mask) != 0U) ? 0x01U : 0U));
                if ((minterm & (1U << term)) != 0U) {
                    out = static_cast<std::uint16_t>(out | mask);
                }
            }
            return out;
        }

        [[nodiscard]] std::uint16_t apply_blitter_fill(std::uint16_t value, bool inclusive_fill,
                                                       bool exclusive_fill,
                                                       bool& fill_state) noexcept {
            if (!inclusive_fill && !exclusive_fill) {
                return value;
            }

            std::uint16_t out = 0U;
            for (std::uint16_t bit = 0U; bit < 16U; ++bit) {
                const std::uint16_t mask = static_cast<std::uint16_t>(1U << bit);
                const bool source_bit = (value & mask) != 0U;
                if (source_bit) {
                    fill_state = !fill_state;
                }
                const bool output_bit = inclusive_fill ? (source_bit || fill_state) : fill_state;
                if (output_bit) {
                    out = static_cast<std::uint16_t>(out | mask);
                }
            }
            return out;
        }

        [[nodiscard]] std::uint16_t blitter_line_mask(std::uint16_t bit_index) noexcept {
            return static_cast<std::uint16_t>(0x8000U >> (bit_index & 0x000FU));
        }

        [[nodiscard]] std::int32_t blitter_line_error(std::uint32_t apt, bool sign_bit) noexcept {
            std::int32_t error = static_cast<std::int16_t>(apt & 0x0000FFFFU);
            if (sign_bit && error > 0) {
                error = -error;
            }
            return error;
        }

        [[nodiscard]] std::uint16_t blitter_line_texture(std::uint16_t texture,
                                                         std::uint16_t bit_index) noexcept {
            const std::uint16_t mask = static_cast<std::uint16_t>(1U << (bit_index & 0x000FU));
            return (texture & mask) != 0U ? 0xFFFFU : 0x0000U;
        }

        [[nodiscard]] std::uint16_t previous_texture_bit(std::uint16_t bit_index) noexcept {
            return bit_index == 0U ? 15U : static_cast<std::uint16_t>(bit_index - 1U);
        }

        [[nodiscard]] std::uint32_t step_blitter_pointer(std::uint32_t pointer,
                                                         std::int32_t delta) noexcept {
            return static_cast<std::uint32_t>(static_cast<std::int32_t>(pointer & chip_dma_address_mask) +
                                              delta) &
                   chip_dma_address_mask;
        }

        [[nodiscard]] std::uint32_t merge_disk_ptr(std::uint32_t old, bool high,
                                                   std::uint16_t value,
                                                   std::size_t chip_ram_size) noexcept {
            const std::uint16_t high_mask = chip_ram_address_high_mask(chip_ram_size);
            if (high) {
                return ((static_cast<std::uint32_t>(value) & high_mask) << 16U) |
                       (old & 0x0000FFFEU);
            }
            return (old & (static_cast<std::uint32_t>(high_mask) << 16U)) |
                   (static_cast<std::uint32_t>(value) & 0x0000FFFEU);
        }

        [[nodiscard]] std::uint8_t cia_register(std::uint32_t address) noexcept {
            return static_cast<std::uint8_t>((address >> 8U) & 0x0FU);
        }

        void append_be16(std::vector<std::uint8_t>& out, std::uint16_t value) {
            out.push_back(static_cast<std::uint8_t>(value >> 8U));
            out.push_back(static_cast<std::uint8_t>(value));
        }

        void append_be32(std::vector<std::uint8_t>& out, std::uint32_t value) {
            append_be16(out, static_cast<std::uint16_t>(value >> 16U));
            append_be16(out, static_cast<std::uint16_t>(value));
        }

        [[nodiscard]] std::uint32_t read_be32(std::span<const std::uint8_t> data,
                                              std::size_t offset) noexcept {
            return (static_cast<std::uint32_t>(data[offset + 0U]) << 24U) |
                   (static_cast<std::uint32_t>(data[offset + 1U]) << 16U) |
                   (static_cast<std::uint32_t>(data[offset + 2U]) << 8U) |
                   static_cast<std::uint32_t>(data[offset + 3U]);
        }

        [[nodiscard]] std::array<std::uint32_t, 2> mfm_odd_even(std::uint32_t raw) noexcept {
            constexpr std::uint32_t mask = 0x55555555U;
            return {((raw >> 1U) & mask), (raw & mask)};
        }

        [[nodiscard]] std::uint32_t mfm_decode_odd_even(std::uint32_t odd,
                                                        std::uint32_t even) noexcept {
            constexpr std::uint32_t mask = 0x55555555U;
            return ((odd & mask) << 1U) | (even & mask);
        }

        [[nodiscard]] std::uint32_t mfm_checksum(std::span<const std::uint32_t> words) noexcept {
            std::uint32_t checksum = 0U;
            for (std::uint32_t word : words) {
                checksum ^= word;
            }
            return checksum & 0x55555555U;
        }

        void append_mfm_long(std::vector<std::uint8_t>& out, std::uint32_t raw) {
            const auto encoded = mfm_odd_even(raw);
            append_be32(out, encoded[0]);
            append_be32(out, encoded[1]);
        }

        void append_amigados_sector(std::vector<std::uint8_t>& out,
                                    std::span<const std::uint8_t> sector_data, std::uint8_t track,
                                    std::uint8_t sector) {
            constexpr std::uint32_t mask = 0x55555555U;
            const std::uint32_t info =
                0xFF000000U | (static_cast<std::uint32_t>(track) << 16U) |
                (static_cast<std::uint32_t>(sector) << 8U) |
                static_cast<std::uint32_t>(amiga_system::floppy_sectors_per_track - sector);

            std::array<std::uint32_t, 10> header_encoded{};
            const auto info_encoded = mfm_odd_even(info);
            header_encoded[0] = info_encoded[0];
            header_encoded[1] = info_encoded[1];
            const std::uint32_t header_checksum = mfm_checksum(header_encoded);

            std::array<std::uint32_t, amiga_system::floppy_sector_size / 2U> data_encoded{};
            for (std::size_t i = 0U; i < amiga_system::floppy_sector_size / 4U; ++i) {
                const auto encoded = mfm_odd_even(read_be32(sector_data, i * 4U));
                data_encoded[i * 2U] = encoded[0];
                data_encoded[i * 2U + 1U] = encoded[1];
            }
            const std::uint32_t data_checksum = mfm_checksum(std::span<const std::uint32_t>(
                                                    data_encoded.data(), data_encoded.size())) &
                                                mask;

            append_be16(out, 0x4489U);
            append_be16(out, 0x4489U);
            for (std::uint32_t word : header_encoded) {
                append_be32(out, word);
            }
            append_mfm_long(out, header_checksum);
            append_mfm_long(out, data_checksum);
            for (std::uint32_t word : data_encoded) {
                append_be32(out, word);
            }
        }

        [[nodiscard]] bool decode_amigados_sector(
            std::span<const std::uint8_t> track_bytes, std::size_t offset,
            std::uint8_t expected_track, std::uint8_t& sector,
            std::array<std::uint8_t, amiga_system::floppy_sector_size>& sector_data) noexcept {
            constexpr std::uint32_t sync = 0x44894489U;
            constexpr std::size_t header_longs = 10U;
            constexpr std::size_t sync_bytes = 4U;
            constexpr std::size_t checksum_bytes = 8U;
            constexpr std::size_t data_encoded_longs = amiga_system::floppy_sector_size / 2U;
            constexpr std::size_t sector_bytes =
                sync_bytes + header_longs * 4U + checksum_bytes * 2U + data_encoded_longs * 4U;
            if (offset + sector_bytes > track_bytes.size() ||
                read_be32(track_bytes, offset) != sync) {
                return false;
            }

            std::size_t cursor = offset + sync_bytes;
            std::array<std::uint32_t, header_longs> header_encoded{};
            for (std::uint32_t& word : header_encoded) {
                word = read_be32(track_bytes, cursor);
                cursor += 4U;
            }

            const std::uint32_t stored_header_checksum = mfm_decode_odd_even(
                read_be32(track_bytes, cursor), read_be32(track_bytes, cursor + 4U));
            cursor += checksum_bytes;
            if (stored_header_checksum != mfm_checksum(header_encoded)) {
                return false;
            }

            const std::uint32_t stored_data_checksum = mfm_decode_odd_even(
                read_be32(track_bytes, cursor), read_be32(track_bytes, cursor + 4U));
            cursor += checksum_bytes;

            const std::uint32_t info = mfm_decode_odd_even(header_encoded[0], header_encoded[1]);
            if ((info & 0xFF000000U) != 0xFF000000U ||
                static_cast<std::uint8_t>(info >> 16U) != expected_track) {
                return false;
            }
            sector = static_cast<std::uint8_t>(info >> 8U);
            const auto sectors_remaining = static_cast<std::uint8_t>(info);
            if (sector >= amiga_system::floppy_sectors_per_track ||
                sectors_remaining !=
                    static_cast<std::uint8_t>(amiga_system::floppy_sectors_per_track - sector)) {
                return false;
            }

            std::array<std::uint32_t, data_encoded_longs> data_encoded{};
            for (std::uint32_t& word : data_encoded) {
                word = read_be32(track_bytes, cursor);
                cursor += 4U;
            }
            if (stored_data_checksum !=
                mfm_checksum(std::span<const std::uint32_t>(data_encoded))) {
                return false;
            }

            for (std::size_t i = 0U; i < amiga_system::floppy_sector_size / 4U; ++i) {
                const std::uint32_t raw =
                    mfm_decode_odd_even(data_encoded[i * 2U], data_encoded[i * 2U + 1U]);
                const std::size_t dst = i * 4U;
                sector_data[dst + 0U] = static_cast<std::uint8_t>(raw >> 24U);
                sector_data[dst + 1U] = static_cast<std::uint8_t>(raw >> 16U);
                sector_data[dst + 2U] = static_cast<std::uint8_t>(raw >> 8U);
                sector_data[dst + 3U] = static_cast<std::uint8_t>(raw);
            }
            return true;
        }
    } // namespace

    std::uint16_t amiga_system::read_custom_word(std::uint16_t reg) noexcept {
        reg = static_cast<std::uint16_t>(reg & 0x01FEU);
        if (reg >= reg_color_base &&
            reg < reg_color_base + chips::video::agnus::palette_entries * 2U) {
            const std::size_t index = (reg - reg_color_base) / 2U;
            return palette_words[index];
        }
        if (reg >= reg_aud_base && reg < reg_aud_base + reg_aud_stride * 4U) {
            const int channel = static_cast<int>((reg - reg_aud_base) / reg_aud_stride);
            const auto offset =
                static_cast<std::uint8_t>(((reg - reg_aud_base) % reg_aud_stride) / 2U);
            switch (offset) {
            case 0:
                return paula.read_reg(channel, chips::audio::paula::reg_lch);
            case 1:
                return paula.read_reg(channel, chips::audio::paula::reg_lcl);
            case 2:
                return paula.read_reg(channel, chips::audio::paula::reg_len);
            case 3:
                return paula.read_reg(channel, chips::audio::paula::reg_per);
            case 4:
                return paula.read_reg(channel, chips::audio::paula::reg_vol);
            case 5:
                return paula.read_reg(channel, chips::audio::paula::reg_dat);
            default:
                return 0U;
            }
        }
        switch (reg) {
        case reg_dmaconr:
            return agnus.read_dmaconr();
        case reg_vposr:
            return agnus.read_vposr();
        case reg_vhposr:
            return agnus.read_vhposr();
        case reg_joy0dat:
            return joydat[0];
        case reg_joy1dat:
            return joydat[1];
        case reg_clxdat:
            return agnus.read_clxdat();
        case reg_adkconr:
            return disk_adkcon;
        case reg_pot0dat:
            return read_pot_counter(0U);
        case reg_pot1dat:
            return read_pot_counter(1U);
        case reg_potinp:
            return read_potinp();
        case reg_dskbytr: {
            std::uint16_t value = static_cast<std::uint16_t>(disk_data & 0x00FFU);
            const auto* drive = active_floppy_drive_state();
            const bool dma_on = disk_dma_bytes_remaining != 0U && (disk_length & 0x8000U) != 0U &&
                                agnus.dma_disk() && drive != nullptr && !drive->image.empty() &&
                                floppy_selected && drive->motor_on;
            if (disk_byte_valid) {
                value = static_cast<std::uint16_t>(value | 0x8000U);
            }
            if (dma_on) {
                value = static_cast<std::uint16_t>(value | 0x4000U);
            }
            if ((disk_length & 0x4000U) != 0U) {
                value = static_cast<std::uint16_t>(value | 0x2000U);
            }
            if (disk_sync_match) {
                value = static_cast<std::uint16_t>(value | 0x1000U);
            }
            disk_byte_valid = false;
            disk_sync_match = false;
            return value;
        }
        case reg_intenar:
            return intena;
        case reg_intreqr:
            return visible_intreq();
        case reg_dskpth:
            return static_cast<std::uint16_t>((disk_pointer >> 16U) &
                                              chip_ram_address_high_mask(chip_ram.size()));
        case reg_dskptl:
            return static_cast<std::uint16_t>(disk_pointer & 0xFFFEU);
        case reg_dsklen:
            return disk_length;
        case reg_dskdat:
            return disk_data;
        case reg_dsksync:
            return disk_sync;
        case reg_bltcon0:
            return bltcon0;
        case reg_bltcon1:
            return bltcon1;
        case reg_bltafwm:
            return bltafwm;
        case reg_bltalwm:
            return bltalwm;
        case reg_bltcpt:
            return static_cast<std::uint16_t>((blitter_pointer[blit_c] >> 16U) &
                                              chip_dma_address_high_mask);
        case reg_bltcpt + 2U:
            return static_cast<std::uint16_t>(blitter_pointer[blit_c] & 0xFFFEU);
        case reg_bltbpt:
            return static_cast<std::uint16_t>((blitter_pointer[blit_b] >> 16U) &
                                              chip_dma_address_high_mask);
        case reg_bltbpt + 2U:
            return static_cast<std::uint16_t>(blitter_pointer[blit_b] & 0xFFFEU);
        case reg_bltapt:
            return static_cast<std::uint16_t>((blitter_pointer[blit_a] >> 16U) &
                                              chip_dma_address_high_mask);
        case reg_bltapt + 2U:
            return static_cast<std::uint16_t>(blitter_pointer[blit_a] & 0xFFFEU);
        case reg_bltdpt:
            return static_cast<std::uint16_t>((blitter_pointer[blit_d] >> 16U) &
                                              chip_dma_address_high_mask);
        case reg_bltdpt + 2U:
            return static_cast<std::uint16_t>(blitter_pointer[blit_d] & 0xFFFEU);
        case reg_bltsize:
            return bltsize;
        case reg_bltcmod:
            return static_cast<std::uint16_t>(blitter_modulo[blit_c]);
        case reg_bltbmod:
            return static_cast<std::uint16_t>(blitter_modulo[blit_b]);
        case reg_bltamod:
            return static_cast<std::uint16_t>(blitter_modulo[blit_a]);
        case reg_bltdmod:
            return static_cast<std::uint16_t>(blitter_modulo[blit_d]);
        case reg_bltcdat:
            return blitter_data[blit_c];
        case reg_bltbdat:
            return blitter_data[blit_b];
        case reg_bltadat:
            return blitter_data[blit_a];
        case reg_bltddat:
            return blitter_data[blit_d];
        case reg_bplcon0:
            return denise.read_bplcon0();
        default:
            return 0U;
        }
    }

    void amiga_system::write_custom_word(std::uint16_t reg, std::uint16_t value) noexcept {
        reg = static_cast<std::uint16_t>(reg & 0x01FEU);

        if (reg >= reg_color_base &&
            reg < reg_color_base + chips::video::agnus::palette_entries * 2U) {
            const std::size_t index = (reg - reg_color_base) / 2U;
            const std::uint16_t color = static_cast<std::uint16_t>(value & 0x0FFFU);
            palette_words[index] = color;
            palette_bytes[index * 2U] = static_cast<std::uint8_t>(color >> 8U);
            palette_bytes[index * 2U + 1U] = static_cast<std::uint8_t>(color);
            denise.write_color(index, color);
            return;
        }

        if (reg >= reg_bplpt_base &&
            reg < reg_bplpt_base + chips::video::agnus::max_bitplanes * 4U) {
            const std::uint32_t plane = (reg - reg_bplpt_base) / 4U;
            const bool high = ((reg - reg_bplpt_base) & 0x02U) == 0U;
            agnus.write_bitplane_pointer_word(plane, high, value);
            bitplane_pointer[plane] = agnus.bitplane_pointer(plane);
            return;
        }

        if (reg >= reg_sprpt_base && reg < reg_sprpt_base + chips::video::agnus::max_sprites * 4U) {
            const std::uint32_t sprite = (reg - reg_sprpt_base) / 4U;
            const bool high = ((reg - reg_sprpt_base) & 0x02U) == 0U;
            agnus.write_sprite_pointer_word(sprite, high, value);
            return;
        }

        if (reg >= reg_spr_base && reg < reg_spr_base + chips::video::agnus::max_sprites * 8U) {
            const std::uint32_t sprite = (reg - reg_spr_base) / 8U;
            const auto offset = static_cast<std::uint16_t>((reg - reg_spr_base) & 0x06U);
            switch (offset) {
            case 0x00U:
                agnus.write_sprite_pos(sprite, value);
                return;
            case 0x02U:
                agnus.write_sprite_ctl(sprite, value);
                return;
            case 0x04U:
                agnus.write_sprite_data_a(sprite, value);
                return;
            case 0x06U:
                agnus.write_sprite_data_b(sprite, value);
                return;
            default:
                return;
            }
        }

        if (reg >= reg_aud_base && reg < reg_aud_base + reg_aud_stride * 4U) {
            const int channel = static_cast<int>((reg - reg_aud_base) / reg_aud_stride);
            const auto offset =
                static_cast<std::uint8_t>(((reg - reg_aud_base) % reg_aud_stride) / 2U);
            switch (offset) {
            case 0:
                paula.write_reg(channel, chips::audio::paula::reg_lch, value);
                break;
            case 1:
                paula.write_reg(channel, chips::audio::paula::reg_lcl, value);
                break;
            case 2:
                paula.write_reg(channel, chips::audio::paula::reg_len, value);
                break;
            case 3:
                paula.write_reg(channel, chips::audio::paula::reg_per, value);
                break;
            case 4:
                paula.write_reg(channel, chips::audio::paula::reg_vol, value);
                break;
            case 5:
                paula.write_reg(channel, chips::audio::paula::reg_dat, value);
                break;
            default:
                break;
            }
            return;
        }

        switch (reg) {
        case reg_bltcon0:
            bltcon0 = value;
            return;
        case reg_bltcon1:
            bltcon1 = value;
            return;
        case reg_bltafwm:
            bltafwm = value;
            return;
        case reg_bltalwm:
            bltalwm = value;
            return;
        case reg_bltcpt:
            blitter_pointer[blit_c] = merge_ptr(blitter_pointer[blit_c], true, value);
            return;
        case reg_bltcpt + 2U:
            blitter_pointer[blit_c] = merge_ptr(blitter_pointer[blit_c], false, value);
            return;
        case reg_bltbpt:
            blitter_pointer[blit_b] = merge_ptr(blitter_pointer[blit_b], true, value);
            return;
        case reg_bltbpt + 2U:
            blitter_pointer[blit_b] = merge_ptr(blitter_pointer[blit_b], false, value);
            return;
        case reg_bltapt:
            blitter_pointer[blit_a] = merge_ptr(blitter_pointer[blit_a], true, value);
            return;
        case reg_bltapt + 2U:
            blitter_pointer[blit_a] = merge_ptr(blitter_pointer[blit_a], false, value);
            return;
        case reg_bltdpt:
            blitter_pointer[blit_d] = merge_ptr(blitter_pointer[blit_d], true, value);
            return;
        case reg_bltdpt + 2U:
            blitter_pointer[blit_d] = merge_ptr(blitter_pointer[blit_d], false, value);
            return;
        case reg_bltsize:
            start_blitter(value);
            return;
        case reg_bltcmod:
            blitter_modulo[blit_c] = static_cast<std::int16_t>(value);
            return;
        case reg_bltbmod:
            blitter_modulo[blit_b] = static_cast<std::int16_t>(value);
            return;
        case reg_bltamod:
            blitter_modulo[blit_a] = static_cast<std::int16_t>(value);
            return;
        case reg_bltdmod:
            blitter_modulo[blit_d] = static_cast<std::int16_t>(value);
            return;
        case reg_bltcdat:
            blitter_data[blit_c] = value;
            return;
        case reg_bltbdat:
            blitter_data[blit_b] = value;
            return;
        case reg_bltadat:
            blitter_data[blit_a] = value;
            return;
        case reg_bltddat:
            blitter_data[blit_d] = value;
            return;
        case reg_dskpth:
            disk_pointer = merge_disk_ptr(disk_pointer, true, value, chip_ram.size());
            return;
        case reg_dskptl:
            disk_pointer = merge_disk_ptr(disk_pointer, false, value, chip_ram.size());
            return;
        case reg_dsklen:
            start_disk_dma(value);
            return;
        case reg_dskdat:
            disk_data = value;
            return;
        case reg_potgo:
            potgo = value;
            if ((value & 0x0001U) != 0U) {
                pot_counter.fill(0U);
                pot_start_line_epoch = beam_line_epoch;
                pot_counters_running = true;
            }
            return;
        case reg_joytest:
            joydat[0] = static_cast<std::uint16_t>((joydat[0] & 0x0303U) | (value & 0xFCFCU));
            joydat[1] = static_cast<std::uint16_t>((joydat[1] & 0x0303U) | (value & 0xFCFCU));
            return;
        case reg_dsksync:
            disk_sync = value;
            return;
        case reg_copcon:
            agnus.write_copcon(value);
            return;
        case reg_cop1lch:
            cop1lc = merge_copper_ptr(cop1lc, true, value, copper_address_mask);
            agnus.write_cop1lc(cop1lc);
            return;
        case reg_cop1lcl:
            cop1lc = merge_copper_ptr(cop1lc, false, value, copper_address_mask);
            agnus.write_cop1lc(cop1lc);
            return;
        case reg_cop2lch:
            cop2lc = merge_copper_ptr(cop2lc, true, value, copper_address_mask);
            agnus.write_cop2lc(cop2lc);
            return;
        case reg_cop2lcl:
            cop2lc = merge_copper_ptr(cop2lc, false, value, copper_address_mask);
            agnus.write_cop2lc(cop2lc);
            return;
        case reg_copjmp1:
            agnus.strobe_copjmp1();
            return;
        case reg_copjmp2:
            agnus.strobe_copjmp2();
            return;
        case reg_diwstrt:
            agnus.set_diwstrt(value);
            return;
        case reg_diwstop:
            agnus.set_diwstop(value);
            return;
        case reg_ddfstrt:
            agnus.set_ddfstrt(value);
            return;
        case reg_ddfstop:
            agnus.set_ddfstop(value);
            return;
        case reg_dmacon: {
            agnus.write_dmacon(value);
            std::uint8_t mask = 0U;
            for (int i = 0; i < chips::audio::paula::channel_count; ++i) {
                if (agnus.dma_audio(i)) {
                    mask = static_cast<std::uint8_t>(mask | (1U << i));
                }
            }
            paula.set_dma(agnus.dma_master(), mask);
            return;
        }
        case reg_clxcon:
            agnus.write_clxcon(value);
            return;
        case reg_intena:
            intena = apply_setclr(intena, value, 0x7FFFU);
            update_irq_level();
            return;
        case reg_intreq:
            intreq = apply_setclr(intreq, value, 0x7FFFU);
            update_irq_level();
            return;
        case reg_adkcon:
            disk_adkcon = apply_setclr(disk_adkcon, value, 0x7FFFU);
            if ((disk_adkcon & adkcon_wordsync) == 0U) {
                disk_wordsync_waiting = false;
            }
            return;
        case reg_bplcon0:
            agnus.set_bplcon0(value);
            denise.write_bplcon0(value);
            return;
        case reg_bplcon1:
            agnus.set_bplcon1(value);
            denise.write_bplcon1(value);
            return;
        case reg_bplcon2:
            agnus.set_bplcon2(value);
            denise.write_bplcon2(value);
            return;
        case reg_bplcon3:
            denise.write_bplcon3(value);
            return;
        case reg_bpl1mod:
            agnus.set_bitplane_modulo_odd(static_cast<std::int16_t>(value));
            return;
        case reg_bpl2mod:
            agnus.set_bitplane_modulo_even(static_cast<std::int16_t>(value));
            return;
        default:
            return;
        }
    }

    std::uint8_t amiga_system::read_custom_byte(std::uint32_t address) noexcept {
        const std::uint16_t reg = static_cast<std::uint16_t>((address - custom_base) & 0x01FEU);
        const std::uint16_t value = read_custom_word(reg);
        return (address & 1U) == 0U ? static_cast<std::uint8_t>(value >> 8U)
                                    : static_cast<std::uint8_t>(value);
    }

    void amiga_system::write_custom_byte(std::uint32_t address, std::uint8_t value) noexcept {
        const std::uint16_t reg = static_cast<std::uint16_t>((address - custom_base) & 0x01FEU);
        const auto current_word = [&]() noexcept -> std::uint16_t {
            if (reg >= reg_color_base &&
                reg < reg_color_base + chips::video::agnus::palette_entries * 2U) {
                return palette_words[(reg - reg_color_base) / 2U];
            }
            if (reg >= reg_bplpt_base &&
                reg < reg_bplpt_base + chips::video::agnus::max_bitplanes * 4U) {
                const std::uint32_t plane = (reg - reg_bplpt_base) / 4U;
                const bool high = ((reg - reg_bplpt_base) & 0x02U) == 0U;
                return high ? static_cast<std::uint16_t>((bitplane_pointer[plane] >> 16U) &
                                                         chip_dma_address_high_mask)
                            : static_cast<std::uint16_t>(bitplane_pointer[plane] & 0xFFFEU);
            }
            if (reg >= reg_sprpt_base && reg < reg_sprpt_base + chips::video::agnus::max_sprites * 4U) {
                const std::uint32_t sprite = (reg - reg_sprpt_base) / 4U;
                const bool high = ((reg - reg_sprpt_base) & 0x02U) == 0U;
                return high ? static_cast<std::uint16_t>((agnus.sprite_pointer(sprite) >> 16U) &
                                                         chip_dma_address_high_mask)
                            : static_cast<std::uint16_t>(agnus.sprite_pointer(sprite) & 0xFFFEU);
            }
            if (reg >= reg_aud_base && reg < reg_aud_base + reg_aud_stride * 4U) {
                const int channel = static_cast<int>((reg - reg_aud_base) / reg_aud_stride);
                const auto offset =
                    static_cast<std::uint8_t>(((reg - reg_aud_base) % reg_aud_stride) / 2U);
                return paula.read_reg(channel, offset);
            }
            switch (reg) {
            case reg_dmacon:
            case reg_intena:
            case reg_intreq:
            case reg_adkcon:
                return 0U;
            case reg_bltcon0:
                return bltcon0;
            case reg_bltcon1:
                return bltcon1;
            case reg_bltafwm:
                return bltafwm;
            case reg_bltalwm:
                return bltalwm;
            case reg_bltcpt:
                return static_cast<std::uint16_t>((blitter_pointer[blit_c] >> 16U) &
                                                  chip_dma_address_high_mask);
            case reg_bltcpt + 2U:
                return static_cast<std::uint16_t>(blitter_pointer[blit_c] & 0xFFFEU);
            case reg_bltbpt:
                return static_cast<std::uint16_t>((blitter_pointer[blit_b] >> 16U) &
                                                  chip_dma_address_high_mask);
            case reg_bltbpt + 2U:
                return static_cast<std::uint16_t>(blitter_pointer[blit_b] & 0xFFFEU);
            case reg_bltapt:
                return static_cast<std::uint16_t>((blitter_pointer[blit_a] >> 16U) &
                                                  chip_dma_address_high_mask);
            case reg_bltapt + 2U:
                return static_cast<std::uint16_t>(blitter_pointer[blit_a] & 0xFFFEU);
            case reg_bltdpt:
                return static_cast<std::uint16_t>((blitter_pointer[blit_d] >> 16U) &
                                                  chip_dma_address_high_mask);
            case reg_bltdpt + 2U:
                return static_cast<std::uint16_t>(blitter_pointer[blit_d] & 0xFFFEU);
            case reg_bltsize:
                return bltsize;
            case reg_bltcmod:
                return static_cast<std::uint16_t>(blitter_modulo[blit_c]);
            case reg_bltbmod:
                return static_cast<std::uint16_t>(blitter_modulo[blit_b]);
            case reg_bltamod:
                return static_cast<std::uint16_t>(blitter_modulo[blit_a]);
            case reg_bltdmod:
                return static_cast<std::uint16_t>(blitter_modulo[blit_d]);
            case reg_bltcdat:
                return blitter_data[blit_c];
            case reg_bltbdat:
                return blitter_data[blit_b];
            case reg_bltadat:
                return blitter_data[blit_a];
            case reg_bltddat:
                return blitter_data[blit_d];
            case reg_dskpth:
                return static_cast<std::uint16_t>((disk_pointer >> 16U) &
                                                  chip_ram_address_high_mask(chip_ram.size()));
            case reg_dskptl:
                return static_cast<std::uint16_t>(disk_pointer & 0xFFFEU);
            case reg_dsklen:
                return disk_length;
            case reg_dskdat:
                return disk_data;
            case reg_dsksync:
                return disk_sync;
            case reg_cop1lch:
                return static_cast<std::uint16_t>((cop1lc >> 16U) &
                                                  copper_address_high_mask(copper_address_mask));
            case reg_cop1lcl:
                return static_cast<std::uint16_t>(cop1lc & 0xFFFEU);
            case reg_cop2lch:
                return static_cast<std::uint16_t>((cop2lc >> 16U) &
                                                  copper_address_high_mask(copper_address_mask));
            case reg_cop2lcl:
                return static_cast<std::uint16_t>(cop2lc & 0xFFFEU);
            case reg_potgo:
                return potgo;
            case reg_joy0dat:
                return joydat[0];
            case reg_joy1dat:
                return joydat[1];
            case reg_intenar:
                return intena;
            case reg_intreqr:
                return visible_intreq();
            case reg_adkconr:
                return disk_adkcon;
            case reg_bplcon0:
                return denise.read_bplcon0();
            case reg_bplcon1:
                return denise.read_bplcon1();
            case reg_bplcon2:
                return denise.read_bplcon2();
            case reg_bplcon3:
                return denise.read_bplcon3();
            default:
                return 0U;
            }
        };
        const std::uint16_t previous = current_word();
        if ((address & 1U) == 0U) {
            write_custom_word(
                reg, static_cast<std::uint16_t>((previous & 0x00FFU) |
                                                (static_cast<std::uint16_t>(value) << 8U)));
            return;
        }
        write_custom_word(reg, static_cast<std::uint16_t>((previous & 0xFF00U) | value));
    }

    zorro2_expansion_board* amiga_system::active_zorro2_autoconfig_board() noexcept {
        if (zorro2_autoconfig_index < zorro2_boards.size()) {
            auto& board = zorro2_boards[zorro2_autoconfig_index];
            if (!board.configured && !board.shut_up) {
                return &board;
            }
        }
        for (std::size_t i = 0U; i < zorro2_boards.size(); ++i) {
            auto& board = zorro2_boards[i];
            if (!board.configured && !board.shut_up) {
                zorro2_autoconfig_index = i;
                return &board;
            }
        }
        zorro2_autoconfig_index = zorro2_boards.size();
        return nullptr;
    }

    const zorro2_expansion_board* amiga_system::active_zorro2_autoconfig_board() const noexcept {
        if (zorro2_autoconfig_index < zorro2_boards.size()) {
            const auto& board = zorro2_boards[zorro2_autoconfig_index];
            if (!board.configured && !board.shut_up) {
                return &board;
            }
        }
        for (const auto& board : zorro2_boards) {
            if (!board.configured && !board.shut_up) {
                return &board;
            }
        }
        return nullptr;
    }

    bool amiga_system::zorro2_autoconfig_pending() const noexcept {
        return active_zorro2_autoconfig_board() != nullptr;
    }

    void amiga_system::reset_zorro2_autoconfig() noexcept {
        zorro2_autoconfig_index = 0U;
        zorro2_base_low_nibble = 0U;
        zorro2_base_low_nibble_valid = false;
        for (auto& board : zorro2_boards) {
            board.assigned_base = 0U;
            board.configured = false;
            board.shut_up = false;
        }
        static_cast<void>(active_zorro2_autoconfig_board());
    }

    bool amiga_system::floppy_loaded(std::size_t drive) const noexcept {
        return drive < floppy_drives.size() && !floppy_drives[drive].image.empty();
    }

    std::size_t amiga_system::floppy_size(std::size_t drive) const noexcept {
        return drive < floppy_drives.size() ? floppy_drives[drive].image.size() : 0U;
    }

    std::uint8_t amiga_system::floppy_cylinder(std::size_t drive) const noexcept {
        return drive < floppy_drives.size() ? floppy_drives[drive].cylinder_pos : 0U;
    }

    bool amiga_system::mount_floppy(std::span<const std::uint8_t> adf_image) {
        return mount_floppy(0U, adf_image);
    }

    bool amiga_system::mount_floppy(std::size_t drive, std::span<const std::uint8_t> adf_image) {
        if (drive >= floppy_drives.size() || adf_image.size() != floppy_dd_size) {
            return false;
        }
        auto& df = floppy_drives[drive];
        df.connected = true;
        df.image.assign(adf_image.begin(), adf_image.end());
        amiga_clear_floppy_track_cache(df);
        df.cylinder_pos = 0U;
        amiga_reset_floppy_stream_phase(df);
        df.track_stream_track_index = 0U;
        df.weak_bit_stream.clear();
        df.weak_bit_lfsr = weak_bit_lfsr_seed;
        df.write_protected = true;
        df.change_latch = true;
        df.track_stream_dirty = false;
        df.index_line_accumulator = 0U;
        floppy_side_pos = 0U;
        disk_dma_bytes_remaining = 0U;
        df.track_stream.reserve(floppy_sectors_per_track * 1100U);
        update_floppy_track_stream(drive);
        return true;
    }

    void amiga_system::unmount_floppy() noexcept { unmount_floppy(0U); }

    void amiga_system::unmount_floppy(std::size_t drive) noexcept {
        if (drive >= floppy_drives.size()) {
            return;
        }
        auto& df = floppy_drives[drive];
        df.image.clear();
        df.track_stream.clear();
        amiga_clear_floppy_track_cache(df);
        amiga_reset_floppy_stream_phase(df);
        df.track_stream_track_index = 0U;
        df.weak_bit_stream.clear();
        df.weak_bit_lfsr = weak_bit_lfsr_seed;
        df.cylinder_pos = 0U;
        df.change_latch = true;
        df.track_stream_dirty = false;
        df.index_line_accumulator = 0U;
        floppy_side_pos = 0U;
        disk_dma_bytes_remaining = 0U;
    }

    void amiga_system::set_floppy_change_latch(std::size_t drive, bool changed) noexcept {
        if (drive < floppy_drives.size() && !floppy_drives[drive].image.empty()) {
            floppy_drives[drive].change_latch = changed;
        }
    }

    void amiga_system::set_floppy_write_protected(std::size_t drive,
                                                     bool write_protected) noexcept {
        if (drive < floppy_drives.size()) {
            floppy_drives[drive].write_protected = write_protected;
        }
    }

    std::uint32_t amiga_system::floppy_index_lines_per_revolution() const noexcept {
        const auto lines_per_frame = agnus.is_pal() ? chips::video::agnus::scanlines_pal
                                                    : chips::video::agnus::scanlines_ntsc;
        const std::uint32_t frames_per_second = agnus.is_pal() ? 50U : 60U;
        return (lines_per_frame * frames_per_second) / floppy_index_pulses_per_second;
    }

    void amiga_system::set_joystick(std::size_t hardware_port, std::uint8_t mask) noexcept {
        if (hardware_port >= joystick_state.size()) {
            return;
        }
        joystick_state[hardware_port] = amiga_sanitize_controller_mask(mask);
        joydat[hardware_port] = static_cast<std::uint16_t>(
            (joydat[hardware_port] & 0xFCFCU) |
            amiga_encode_joystick(joystick_state[hardware_port]));
    }

    void amiga_system::set_mouse(std::size_t hardware_port, std::int16_t delta_x,
                                    std::int16_t delta_y, bool left_button, bool right_button,
                                    bool middle_button) noexcept {
        if (hardware_port >= joystick_state.size()) {
            return;
        }

        const auto x =
            amiga_wrap_mouse_counter(static_cast<std::uint8_t>(joydat[hardware_port]), delta_x);
        const auto y =
            amiga_wrap_mouse_counter(static_cast<std::uint8_t>(joydat[hardware_port] >> 8U),
                                     delta_y);
        joydat[hardware_port] =
            static_cast<std::uint16_t>((static_cast<std::uint16_t>(y) << 8U) | x);

        joystick_state[hardware_port] =
            amiga_mouse_button_mask(left_button, right_button, middle_button);
    }

    void amiga_system::set_pot_position(std::size_t hardware_port, std::uint8_t x,
                                           std::uint8_t y) noexcept {
        if (hardware_port >= pot_target.size()) {
            return;
        }
        pot_target[hardware_port] = amiga_pack_pot_target(x, y);
    }

    bool amiga_system::enqueue_keyboard_key(std::uint8_t raw_keycode, bool pressed) noexcept {
        if (!amiga_keyboard_enqueue_key(keyboard, raw_keycode, pressed)) {
            return false;
        }
        service_keyboard_queue();
        return true;
    }

    bool amiga_system::enqueue_keyboard_control_code(std::uint8_t code) noexcept {
        if (!amiga_keyboard_enqueue_control_code(keyboard, code)) {
            return false;
        }
        service_keyboard_queue();
        return true;
    }

    bool amiga_system::press_caps_lock() noexcept {
        if (!amiga_keyboard_press_caps_lock(keyboard)) {
            return false;
        }
        service_keyboard_queue();
        return true;
    }

    bool amiga_system::enqueue_keyboard_code(std::uint8_t code) noexcept {
        if (!amiga_keyboard_enqueue_code(keyboard, code)) {
            return false;
        }
        service_keyboard_queue();
        return true;
    }

    void amiga_system::transmit_keyboard_code(std::uint8_t code) noexcept {
        const std::uint8_t sdr = amiga_keyboard_sdr(code);
        for (int bit = 7; bit >= 0; --bit) {
            cia_a.sp_level(((sdr >> static_cast<unsigned>(bit)) & 0x01U) != 0U);
            cia_a.cnt_edge(false);
            cia_a.cnt_edge(true);
        }
        cia_a.sp_level(true);
        keyboard_byte_in_flight = true;
        keyboard_ack_low_seen = false;
    }

    void amiga_system::service_keyboard_queue() noexcept {
        if (keyboard_byte_in_flight) {
            return;
        }
        std::uint8_t code = 0U;
        if (!amiga_keyboard_dequeue_code(keyboard, code)) {
            return;
        }
        transmit_keyboard_code(code);
    }

    void amiga_system::write_cia_a_sp(bool level) noexcept {
        if (!keyboard_byte_in_flight) {
            keyboard_ack_low_seen = false;
            return;
        }
        if (!level) {
            keyboard_ack_low_seen = true;
            return;
        }
        if (!keyboard_ack_low_seen) {
            return;
        }
        keyboard_ack_low_seen = false;
        keyboard_byte_in_flight = false;
    }

    std::uint8_t amiga_system::cia_a_port_a_inputs() const noexcept {
        std::uint8_t pins = 0xFFU;
        if ((joystick_state[0] & joy_fire) != 0U) {
            pins = static_cast<std::uint8_t>(pins & ~0x40U);
        }
        if ((joystick_state[1] & joy_fire) != 0U) {
            pins = static_cast<std::uint8_t>(pins & ~0x80U);
        }
        if (floppy_selected) {
            const auto* drive = active_floppy_drive_state();
            if (drive != nullptr && drive->connected) {
                const bool loaded = !drive->image.empty();
                // CIAA PRA disk sense lines are active low: PA5=/RDY, PA4=/TK0,
                // PA3=/WPRO, PA2=/CHNG. /CHNG remains low after media changes until
                // the selected drive receives a step pulse with media present.
                if (loaded && drive->motor_on) {
                    pins = static_cast<std::uint8_t>(pins & ~0x20U);
                }
                if (loaded && drive->cylinder_pos == 0U) {
                    pins = static_cast<std::uint8_t>(pins & ~0x10U);
                }
                if (loaded && drive->write_protected) {
                    pins = static_cast<std::uint8_t>(pins & ~0x08U);
                }
                if (!loaded || drive->change_latch) {
                    pins = static_cast<std::uint8_t>(pins & ~0x04U);
                }
            }
        }
        return pins;
    }

    std::uint16_t amiga_system::read_potinp() const noexcept {
        return amiga_potinp_value(potgo, joystick_state);
    }

    std::uint16_t amiga_system::read_pot_counter(std::size_t hardware_port) noexcept {
        if (hardware_port >= pot_counter.size()) {
            return 0xFFFFU;
        }
        if (!pot_counters_running) {
            return pot_counter[hardware_port];
        }

        const std::uint64_t elapsed_lines = beam_line_epoch - pot_start_line_epoch;
        pot_counter[hardware_port] =
            amiga_pot_counter_value(pot_target[hardware_port], elapsed_lines);
        return pot_counter[hardware_port];
    }

    void amiga_system::write_cia_b_port_b(std::uint8_t value) {
        const std::uint8_t next_selected_mask = static_cast<std::uint8_t>((~value >> 3U) & 0x0FU);
        const std::uint8_t newly_selected_mask =
            static_cast<std::uint8_t>(next_selected_mask & ~floppy_selected_mask);
        std::uint8_t next_active_drive = no_floppy_drive;
        for (std::size_t drive = 0U; drive < floppy_drive_count; ++drive) {
            if ((next_selected_mask & (1U << drive)) != 0U) {
                next_active_drive = static_cast<std::uint8_t>(drive);
                break;
            }
        }
        const bool next_selected = next_active_drive != no_floppy_drive;
        const bool next_motor_latch = (value & 0x80U) == 0U;
        const auto next_side = static_cast<std::uint8_t>((value & 0x04U) == 0U ? 1U : 0U);
        const bool next_direction_inward = (value & 0x02U) != 0U;
        const bool next_step_line = (value & 0x01U) != 0U;
        const std::uint8_t old_active_drive = floppy_active_drive;
        const std::uint8_t old_side = floppy_side_pos;

        const bool stepping = floppy_selected && floppy_step_line && !next_step_line;
        floppy_selected_mask = next_selected_mask;
        floppy_selected = next_selected;
        floppy_active_drive = next_active_drive;
        floppy_side_pos = next_side;
        floppy_direction_inward = next_direction_inward;

        for (std::size_t drive = 0U; drive < floppy_drive_count; ++drive) {
            if ((newly_selected_mask & (1U << drive)) == 0U) {
                continue;
            }
            auto& df = floppy_drives[drive];
            df.motor_on = next_motor_latch;
            if (!df.motor_on) {
                df.index_line_accumulator = 0U;
                df.byte_clock_accumulator = 0U;
                df.stream_write_latch = 0U;
                df.stream_write_shift = 0U;
                df.stream_write_bits_remaining = 0U;
            }
        }

        if (const auto* drive = active_floppy_drive_state(); floppy_selected && drive != nullptr) {
            floppy_motor_on = drive->motor_on;
        } else {
            floppy_motor_on = false;
        }

        if (stepping) {
            for (std::size_t drive = 0U; drive < floppy_drive_count; ++drive) {
                if ((floppy_selected_mask & (1U << drive)) == 0U) {
                    continue;
                }
                auto& df = floppy_drives[drive];
                if (!df.connected) {
                    continue;
                }
                if (floppy_direction_inward &&
                    static_cast<std::size_t>(df.cylinder_pos) + 1U < floppy_cylinders) {
                    ++df.cylinder_pos;
                } else if (!floppy_direction_inward && df.cylinder_pos > 0U) {
                    --df.cylinder_pos;
                }
                if (!df.image.empty()) {
                    df.change_latch = false;
                }
                update_floppy_track_stream(drive);
            }
        } else if (next_active_drive != old_active_drive || next_side != old_side) {
            update_floppy_track_stream();
        }
        floppy_step_line = next_step_line;
    }

    amiga_system::floppy_drive_state* amiga_system::active_floppy_drive_state() noexcept {
        if (static_cast<std::size_t>(floppy_active_drive) >= floppy_drives.size()) {
            return nullptr;
        }
        return &floppy_drives[static_cast<std::size_t>(floppy_active_drive)];
    }

    const amiga_system::floppy_drive_state*
    amiga_system::active_floppy_drive_state() const noexcept {
        if (static_cast<std::size_t>(floppy_active_drive) >= floppy_drives.size()) {
            return nullptr;
        }
        return &floppy_drives[static_cast<std::size_t>(floppy_active_drive)];
    }

    void amiga_system::preserve_dirty_floppy_track(std::size_t drive) noexcept {
        if (drive >= floppy_drives.size()) {
            return;
        }
        auto& df = floppy_drives[drive];
        const bool has_weak_mask =
            df.weak_bit_stream.size() == df.track_stream.size() &&
            has_marked_weak_bits(std::span<const std::uint8_t>(df.weak_bit_stream));
        if ((!df.track_stream_dirty && !has_weak_mask) || df.track_stream.empty() ||
            df.track_stream_track_index >= df.raw_track_cache.size()) {
            return;
        }

        df.raw_track_cache[df.track_stream_track_index] = df.track_stream;
        if (has_weak_mask) {
            df.weak_bit_cache[df.track_stream_track_index] = df.weak_bit_stream;
        } else {
            df.weak_bit_cache[df.track_stream_track_index].clear();
        }
        df.track_stream_dirty = false;
    }

    void amiga_system::update_floppy_track_stream() {
        if (static_cast<std::size_t>(floppy_active_drive) < floppy_drives.size()) {
            update_floppy_track_stream(floppy_active_drive);
        }
    }

    void amiga_system::update_floppy_track_stream(std::size_t drive) {
        if (drive >= floppy_drives.size()) {
            return;
        }
        auto& df = floppy_drives[drive];
        const std::size_t old_track_size = df.track_stream.size();
        const std::size_t old_stream_offset = df.stream_offset;
        const std::uint8_t old_stream_bit_offset = df.stream_bit_offset;
        preserve_dirty_floppy_track(drive);
        df.track_stream.clear();
        df.weak_bit_stream.clear();
        df.stream_offset = 0U;
        df.stream_bit_offset = 0U;
        df.track_stream_dirty = false;
        if (df.image.empty()) {
            df.track_stream_track_index = 0U;
            df.stream_read_shift = 0U;
            df.stream_read_bit_count = 0U;
            df.stream_write_latch = 0U;
            df.stream_write_shift = 0U;
            df.stream_write_bits_remaining = 0U;
            return;
        }

        const std::size_t track_index =
            (static_cast<std::size_t>(df.cylinder_pos) * floppy_heads) + floppy_side_pos;
        df.track_stream_track_index = track_index;
        if (track_index < df.raw_track_cache.size() && !df.raw_track_cache[track_index].empty()) {
            df.track_stream = df.raw_track_cache[track_index];
            if (!df.weak_bit_cache[track_index].empty() &&
                df.weak_bit_cache[track_index].size() == df.track_stream.size()) {
                df.weak_bit_stream = df.weak_bit_cache[track_index];
            }
            restore_raw_track_phase(df, old_track_size, old_stream_offset, old_stream_bit_offset);
            return;
        }

        const std::size_t track_base = track_index * floppy_sectors_per_track * floppy_sector_size;
        const auto track = static_cast<std::uint8_t>(track_index);
        for (std::uint8_t sector = 0U; sector < floppy_sectors_per_track; ++sector) {
            const std::size_t sector_base =
                track_base + static_cast<std::size_t>(sector) * floppy_sector_size;
            append_amigados_sector(
                df.track_stream,
                std::span<const std::uint8_t>(df.image).subspan(sector_base, floppy_sector_size),
                track, sector);
        }
        for (std::size_t i = 0U; i < 32U; ++i) {
            append_be16(df.track_stream, 0xAAAAU);
        }
        if (track_index < df.weak_bit_cache.size() && !df.weak_bit_cache[track_index].empty() &&
            df.weak_bit_cache[track_index].size() == df.track_stream.size()) {
            df.weak_bit_stream = df.weak_bit_cache[track_index];
        }
        restore_raw_track_phase(df, old_track_size, old_stream_offset, old_stream_bit_offset);
    }

    bool amiga_system::flush_floppy_track_to_image(std::size_t drive) noexcept {
        if (drive >= floppy_drives.size()) {
            return false;
        }
        auto& df = floppy_drives[drive];
        if (df.image.size() != floppy_dd_size || df.track_stream.empty()) {
            return false;
        }

        const std::size_t track_index =
            (static_cast<std::size_t>(df.cylinder_pos) * floppy_heads) + floppy_side_pos;
        if (track_index >= floppy_track_count) {
            return false;
        }
        const auto expected_track = static_cast<std::uint8_t>(track_index);
        bool wrote_sector = false;
        std::array<std::uint8_t, floppy_sector_size> sector_data{};
        for (std::size_t offset = 0U; offset + 4U <= df.track_stream.size(); ++offset) {
            if (read_be32(std::span<const std::uint8_t>(df.track_stream), offset) != 0x44894489U) {
                continue;
            }
            std::uint8_t sector = 0U;
            if (!decode_amigados_sector(std::span<const std::uint8_t>(df.track_stream), offset,
                                        expected_track, sector, sector_data)) {
                continue;
            }
            const std::size_t sector_base =
                (track_index * floppy_sectors_per_track + sector) * floppy_sector_size;
            if (sector_base + floppy_sector_size > df.image.size()) {
                continue;
            }
            std::copy(sector_data.begin(), sector_data.end(), df.image.begin() + sector_base);
            wrote_sector = true;
        }
        return wrote_sector;
    }

    std::uint8_t amiga_system::next_floppy_byte() noexcept {
        auto* drive = active_floppy_drive_state();
        if (drive == nullptr || drive->track_stream.empty()) {
            disk_data = 0U;
            return 0U;
        }
        std::uint8_t value = 0U;
        for (std::uint8_t bit = 0U; bit < 8U; ++bit) {
            value = static_cast<std::uint8_t>((value << 1U) | sample_raw_track_bit(*drive));
            advance_raw_track_phase(*drive);
        }
        accept_floppy_byte(value);
        return value;
    }

    void amiga_system::accept_floppy_byte(std::uint8_t byte) noexcept {
        disk_data = static_cast<std::uint16_t>((disk_data << 8U) | byte);
        disk_shift = static_cast<std::uint16_t>((disk_shift << 8U) | byte);
        disk_byte_valid = true;
        if (disk_shift == disk_sync) {
            disk_sync_match = true;
            request_interrupt(int_dsksyn);
        }
    }

    bool amiga_system::shift_floppy_read_bit() noexcept {
        auto* drive = active_floppy_drive_state();
        if (drive == nullptr || drive->track_stream.empty()) {
            return false;
        }

        const std::uint8_t bit = sample_raw_track_bit(*drive);
        drive->stream_read_shift =
            static_cast<std::uint8_t>((drive->stream_read_shift << 1U) | bit);
        drive->stream_read_bit_count = static_cast<std::uint8_t>(
            std::min<std::uint8_t>(8U, drive->stream_read_bit_count + 1U));
        advance_raw_track_phase(*drive);
        if (drive->stream_read_bit_count < 8U) {
            return false;
        }

        accept_floppy_byte(drive->stream_read_shift);
        drive->stream_read_shift = 0U;
        drive->stream_read_bit_count = 0U;
        return true;
    }

    void amiga_system::shift_floppy_write_bit() noexcept {
        auto* drive = active_floppy_drive_state();
        if (drive == nullptr || drive->track_stream.empty()) {
            return;
        }

        if (drive->stream_write_bits_remaining == 0U) {
            if (disk_dma_bytes_remaining == 0U || (disk_length & 0xC000U) != 0xC000U ||
                !agnus.dma_disk()) {
                return;
            }
            const std::size_t addr = mirrored_chip_byte_address(chip_ram, disk_pointer);
            const std::uint8_t byte = chip_ram[addr];
            disk_pointer = (disk_pointer + 1U) & chip_ram_address_mask(chip_ram.size());
            drive->stream_write_latch = byte;
            drive->stream_write_shift = byte;
            drive->stream_write_bits_remaining = 8U;
        }

        const std::uint8_t bit =
            static_cast<std::uint8_t>((drive->stream_write_shift >> 7U) & 0x01U);
        if (!drive->write_protected) {
            write_raw_track_bit_at_phase(drive->track_stream, drive->stream_offset,
                                         drive->stream_bit_offset, bit);
            if (drive->weak_bit_stream.size() == drive->track_stream.size()) {
                write_raw_track_bit_at_phase(drive->weak_bit_stream, drive->stream_offset,
                                             drive->stream_bit_offset, 0U);
            }
            drive->track_stream_dirty = true;
        }
        drive->stream_write_shift = static_cast<std::uint8_t>(drive->stream_write_shift << 1U);
        --drive->stream_write_bits_remaining;
        advance_raw_track_phase(*drive);

        if (drive->stream_write_bits_remaining != 0U) {
            return;
        }

        disk_data = static_cast<std::uint16_t>((disk_data << 8U) | drive->stream_write_latch);
        disk_shift = static_cast<std::uint16_t>((disk_shift << 8U) | drive->stream_write_latch);
        disk_byte_valid = true;
        if (disk_shift == disk_sync) {
            disk_sync_match = true;
            request_interrupt(int_dsksyn);
        }
        const std::uint8_t write_drive = floppy_active_drive;
        const bool should_flush_track = !drive->write_protected && disk_dma_bytes_remaining == 1U;
        complete_disk_dma_byte();
        if (should_flush_track) {
            (void)flush_floppy_track_to_image(write_drive);
        }
    }

    void amiga_system::tick_floppy_scanline() noexcept {
        auto* drive = active_floppy_drive_state();
        if (!floppy_selected || drive == nullptr || !drive->motor_on || drive->image.empty()) {
            if (drive != nullptr) {
                drive->index_line_accumulator = 0U;
                drive->byte_clock_accumulator = 0U;
            }
            return;
        }

        const std::uint32_t lines_per_revolution = floppy_index_lines_per_revolution();
        if (lines_per_revolution == 0U) {
            return;
        }

        ++drive->index_line_accumulator;
        if (drive->index_line_accumulator >= lines_per_revolution) {
            drive->index_line_accumulator = 0U;
            cia_b.flag_edge();
        }
    }

    void amiga_system::tick_floppy_data_cycle() noexcept {
        auto* drive = active_floppy_drive_state();
        if (!floppy_selected || drive == nullptr || !drive->motor_on || drive->image.empty()) {
            if (drive != nullptr) {
                drive->byte_clock_accumulator = 0U;
            }
            return;
        }
        if (drive->track_stream.empty()) {
            return;
        }

        const std::uint32_t lines_per_revolution = floppy_index_lines_per_revolution();
        if (lines_per_revolution == 0U) {
            return;
        }
        const std::uint64_t clocks_per_revolution =
            static_cast<std::uint64_t>(lines_per_revolution) *
            chips::video::agnus::color_clocks_per_line;
        if (clocks_per_revolution == 0U) {
            return;
        }

        drive->byte_clock_accumulator +=
            static_cast<std::uint64_t>(drive->track_stream.size()) * 8U;
        while (drive->byte_clock_accumulator >= clocks_per_revolution) {
            drive->byte_clock_accumulator -= clocks_per_revolution;
            const bool write_dma =
                (disk_dma_bytes_remaining != 0U && (disk_length & 0xC000U) == 0xC000U) ||
                drive->stream_write_bits_remaining != 0U;
            if (write_dma) {
                shift_floppy_write_bit();
            } else if (shift_floppy_read_bit()) {
                const std::uint8_t byte = static_cast<std::uint8_t>(disk_data);
                if (disk_wordsync_waiting) {
                    if (disk_shift == disk_sync) {
                        disk_wordsync_waiting = false;
                    }
                    continue;
                }
                transfer_disk_dma_byte(byte);
            }
        }
    }

    void amiga_system::complete_disk_dma_byte() noexcept {
        --disk_dma_bytes_remaining;
        if (disk_dma_bytes_remaining == 0U) {
            disk_length = static_cast<std::uint16_t>(disk_length & 0xC000U);
            disk_wordsync_waiting = false;
            request_interrupt(int_dskblk);
            return;
        }

        const std::uint16_t words_remaining =
            static_cast<std::uint16_t>((disk_dma_bytes_remaining + 1U) / 2U);
        disk_length = static_cast<std::uint16_t>((disk_length & 0xC000U) | words_remaining);
    }

    void amiga_system::transfer_disk_dma_byte(std::uint8_t byte) noexcept {
        if (disk_dma_bytes_remaining == 0U || (disk_length & 0x8000U) == 0U ||
            (disk_length & 0x4000U) != 0U || disk_wordsync_waiting || !agnus.dma_disk()) {
            return;
        }

        const std::size_t addr = mirrored_chip_byte_address(chip_ram, disk_pointer);
        chip_ram[addr] = byte;
        paula.chipram()[addr] = byte;
        disk_pointer = (disk_pointer + 1U) & chip_ram_address_mask(chip_ram.size());
        complete_disk_dma_byte();
    }

    void amiga_system::start_disk_dma(std::uint16_t value) noexcept {
        disk_length = value;
        const auto reset_pending_write_byte = [this]() noexcept {
            if (auto* drive = active_floppy_drive_state(); drive != nullptr) {
                drive->stream_write_latch = 0U;
                drive->stream_write_shift = 0U;
                drive->stream_write_bits_remaining = 0U;
            }
        };
        const bool enabled = (value & 0x8000U) != 0U;
        const std::uint16_t words = static_cast<std::uint16_t>(value & 0x3FFFU);
        if (!enabled || words == 0U) {
            reset_pending_write_byte();
            disk_dma_armed = false;
            disk_dma_bytes_remaining = 0U;
            disk_wordsync_waiting = false;
            disk_last_length_write = value;
            return;
        }
        if (!disk_dma_armed || disk_last_length_write != value) {
            disk_dma_armed = true;
            disk_last_length_write = value;
            return;
        }
        disk_dma_armed = false;
        reset_pending_write_byte();
        const auto* drive = active_floppy_drive_state();
        if (!agnus.dma_disk() || drive == nullptr || drive->image.empty() || !floppy_selected ||
            !drive->motor_on) {
            disk_dma_bytes_remaining = 0U;
            disk_wordsync_waiting = false;
            return;
        }

        disk_dma_bytes_remaining = static_cast<std::uint32_t>(words) * 2U;
        disk_wordsync_waiting = (value & 0x4000U) == 0U && (disk_adkcon & adkcon_wordsync) != 0U;
    }

    void amiga_system::start_blitter_line(std::uint32_t length) noexcept {
        const auto minterm = static_cast<std::uint8_t>(bltcon0 & 0x00FFU);
        const std::uint16_t texture = blitter_data[blit_b];
        std::uint16_t texture_bit = static_cast<std::uint16_t>((bltcon1 >> 12U) & 0x000FU);
        std::uint16_t bit_index = static_cast<std::uint16_t>((bltcon0 >> 12U) & 0x000FU);
        std::int32_t error =
            blitter_line_error(blitter_pointer[blit_a], (bltcon1 & bltcon1_line_sign) != 0U);
        const std::int32_t minor_error_step = blitter_modulo[blit_a];
        const std::int32_t major_error_step = blitter_modulo[blit_b];
        const std::int32_t c_row_step = blitter_modulo[blit_c];
        // In line mode the read/modify/write destination follows BLTCMOD;
        // Kickstart leaves BLTDMOD zero for these blits.
        const std::int32_t d_row_step = blitter_modulo[blit_c];
        const bool major_horizontal = (bltcon1 & bltcon1_line_sud) != 0U;
        const int major_direction = (bltcon1 & bltcon1_line_aul) != 0U ? -1 : 1;
        const int minor_direction = (bltcon1 & bltcon1_line_sul) != 0U ? -1 : 1;
        const bool single_dot_per_raster = (bltcon1 & bltcon1_line_sing) != 0U;
        const bool use_c = (bltcon0 & bltcon0_usec) != 0U;
        const bool use_d = (bltcon0 & bltcon0_used) != 0U;

        std::uint32_t cptr = blitter_pointer[blit_c];
        std::uint32_t dptr = blitter_pointer[blit_d];
        std::int32_t logical_y = 0;
        std::int32_t last_draw_y = 0;
        bool have_draw_y = false;
        bool zero = true;

        const auto step_x = [&](int direction) noexcept {
            if (direction >= 0) {
                if (bit_index == 15U) {
                    bit_index = 0U;
                    cptr = step_blitter_pointer(cptr, 2);
                    dptr = step_blitter_pointer(dptr, 2);
                } else {
                    ++bit_index;
                }
                return;
            }
            if (bit_index == 0U) {
                bit_index = 15U;
                cptr = step_blitter_pointer(cptr, -2);
                dptr = step_blitter_pointer(dptr, -2);
            } else {
                --bit_index;
            }
        };
        const auto step_y = [&](int direction) noexcept {
            cptr = step_blitter_pointer(cptr, c_row_step * direction);
            dptr = step_blitter_pointer(dptr, d_row_step * direction);
            logical_y += direction;
        };
        const auto step_major = [&]() noexcept {
            if (major_horizontal) {
                step_x(major_direction);
            } else {
                step_y(major_direction);
            }
        };
        const auto step_minor = [&]() noexcept {
            if (major_horizontal) {
                step_y(minor_direction);
            } else {
                step_x(minor_direction);
            }
        };

        for (std::uint32_t dot = 0U; dot < length; ++dot) {
            const std::uint16_t c = use_c ? read_chip_word(chip_ram, cptr) : blitter_data[blit_c];
            if (use_c) {
                blitter_data[blit_c] = c;
            }

            const bool draw_dot =
                !single_dot_per_raster || !have_draw_y || logical_y != last_draw_y;
            const std::uint16_t a = draw_dot ? blitter_line_mask(bit_index) : 0U;
            const std::uint16_t b = blitter_line_texture(texture, texture_bit);
            const std::uint16_t d = blitter_minterm(minterm, a, b, c);
            blitter_data[blit_a] = a;
            blitter_data[blit_d] = d;
            if (d != 0U) {
                zero = false;
            }
            if (use_d) {
                write_chip_word(*this, dptr, d);
            }
            if (draw_dot) {
                last_draw_y = logical_y;
                have_draw_y = true;
            }

            texture_bit = previous_texture_bit(texture_bit);
            if (error >= 0) {
                step_minor();
                error += minor_error_step;
            } else {
                error += major_error_step;
            }
            step_major();
        }

        blitter_pointer[blit_a] = (blitter_pointer[blit_a] &
                                   (chip_dma_address_high_mask << 16U)) |
                                  (static_cast<std::uint16_t>(error) & 0x0000FFFEU);
        blitter_pointer[blit_c] = cptr;
        blitter_pointer[blit_d] = dptr;
        if (error < 0) {
            bltcon1 = static_cast<std::uint16_t>(bltcon1 | bltcon1_line_sign);
        } else {
            bltcon1 = static_cast<std::uint16_t>(bltcon1 & ~bltcon1_line_sign);
        }
        agnus.set_blitter_zero(zero);
    }

    void amiga_system::start_blitter(std::uint16_t value) noexcept {
        bltsize = value;
        const std::uint32_t width_words = (value & 0x003FU) == 0U ? 64U : (value & 0x003FU);
        const std::uint32_t height = (value >> 6U) == 0U ? 1024U : (value >> 6U);
        const bool line_mode = (bltcon1 & bltcon1_line) != 0U;

        agnus.set_blitter_busy(true);
        if (!agnus.dma_blitter()) {
            blitter_cycles_remaining = 0U;
            agnus.set_blitter_busy(false);
            return;
        }
        if (line_mode) {
            blitter_cycles_remaining =
                blitter_cycle_budget(height, 1U, blitter_line_dma_slots(bltcon0));
            start_blitter_line(height);
            return;
        }
        blitter_cycles_remaining =
            blitter_cycle_budget(width_words, height, blitter_area_dma_slots(bltcon0));

        const bool descending = (bltcon1 & bltcon1_desc) != 0U;
        const std::array<bool, 4> use_channel{
            (bltcon0 & bltcon0_usea) != 0U,
            (bltcon0 & bltcon0_useb) != 0U,
            (bltcon0 & bltcon0_usec) != 0U,
            (bltcon0 & bltcon0_used) != 0U,
        };
        std::array<std::uint32_t, 4> ptr = blitter_pointer;
        const std::int32_t word_step = descending ? -2 : 2;
        const auto modulo_step = [&](std::size_t channel) noexcept -> std::int32_t {
            const std::int32_t modulo = blitter_modulo[channel];
            return descending ? -modulo : modulo;
        };
        const std::uint16_t ashift = static_cast<std::uint16_t>((bltcon0 >> 12U) & 0x000FU);
        const std::uint16_t bshift = static_cast<std::uint16_t>((bltcon1 >> 12U) & 0x000FU);
        const auto minterm = static_cast<std::uint8_t>(bltcon0 & 0x00FFU);
        const bool fill_enabled = descending && (bltcon1 & (bltcon1_ife | bltcon1_efe)) != 0U;
        const bool inclusive_fill = fill_enabled && (bltcon1 & bltcon1_ife) != 0U;
        const bool exclusive_fill =
            fill_enabled && !inclusive_fill && (bltcon1 & bltcon1_efe) != 0U;
        bool zero = true;

        std::uint16_t previous_a = blitter_data[blit_a];
        std::uint16_t previous_b = blitter_data[blit_b];
        for (std::uint32_t y = 0U; y < height; ++y) {
            bool fill_state = (bltcon1 & bltcon1_fci) != 0U;
            for (std::uint32_t x = 0U; x < width_words; ++x) {
                std::uint16_t a = use_channel[blit_a] ? read_chip_word(chip_ram, ptr[blit_a])
                                                      : blitter_data[blit_a];
                std::uint16_t mask = 0xFFFFU;
                if (x == 0U) {
                    mask = static_cast<std::uint16_t>(mask & bltafwm);
                }
                if (x + 1U == width_words) {
                    mask = static_cast<std::uint16_t>(mask & bltalwm);
                }
                a = static_cast<std::uint16_t>(a & mask);
                if (use_channel[blit_a]) {
                    blitter_data[blit_a] = a;
                }
                const std::uint16_t shifted_a = shifted_word(previous_a, a, ashift);
                previous_a = a;

                const std::uint16_t b = use_channel[blit_b] ? read_chip_word(chip_ram, ptr[blit_b])
                                                            : blitter_data[blit_b];
                if (use_channel[blit_b]) {
                    blitter_data[blit_b] = b;
                }
                const std::uint16_t shifted_b = shifted_word(previous_b, b, bshift);
                previous_b = b;

                const std::uint16_t c = use_channel[blit_c] ? read_chip_word(chip_ram, ptr[blit_c])
                                                            : blitter_data[blit_c];
                if (use_channel[blit_c]) {
                    blitter_data[blit_c] = c;
                }

                const std::uint16_t raw_d = blitter_minterm(minterm, shifted_a, shifted_b, c);
                const std::uint16_t d =
                    apply_blitter_fill(raw_d, inclusive_fill, exclusive_fill, fill_state);
                blitter_data[blit_d] = d;
                if (d != 0U) {
                    zero = false;
                }
                if (use_channel[blit_d]) {
                    write_chip_word(*this, ptr[blit_d], d);
                }

                for (std::size_t channel = 0U; channel < ptr.size(); ++channel) {
                    if (use_channel[channel]) {
                        ptr[channel] = step_blitter_pointer(ptr[channel], word_step);
                    }
                }
            }
            for (std::size_t channel = 0U; channel < ptr.size(); ++channel) {
                if (use_channel[channel]) {
                    ptr[channel] = step_blitter_pointer(ptr[channel], modulo_step(channel));
                }
            }
        }

        blitter_pointer = ptr;
        agnus.set_blitter_zero(zero);
    }

    void amiga_system::tick_blitter_cycle() noexcept {
        if (blitter_cycles_remaining == 0U || !agnus.dma_blitter()) {
            return;
        }
        if (agnus.display_dma_cpu_wait_cycles(0U) != 0U) {
            return;
        }
        --blitter_cycles_remaining;
        if (blitter_cycles_remaining == 0U) {
            agnus.set_blitter_busy(false);
            request_interrupt(int_blit);
        }
    }

    void amiga_system::request_interrupt(std::uint16_t mask) noexcept {
        intreq = static_cast<std::uint16_t>(intreq | (mask & 0x7FFFU));
        update_irq_level();
    }

    std::uint16_t amiga_system::visible_intreq() const noexcept {
        std::uint16_t visible = intreq;
        if (cia_a_irq) {
            visible = static_cast<std::uint16_t>(visible | int_ports);
        }
        if (cia_b_irq) {
            visible = static_cast<std::uint16_t>(visible | int_exter);
        }
        return static_cast<std::uint16_t>(visible & 0x7FFFU);
    }

    void amiga_system::update_irq_level() noexcept {
        if ((intena & int_master) == 0U) {
            cpu.set_irq_level(0);
            return;
        }

        const std::uint16_t pending =
            static_cast<std::uint16_t>(intena & visible_intreq() & 0x3FFFU);

        int level = 0;
        if ((pending & int_exter) != 0U) {
            level = 6;
        } else if ((pending & (int_rbf | int_dsksyn)) != 0U) {
            level = 5;
        } else if ((pending & (int_aud0 | int_aud1 | int_aud2 | int_aud3)) != 0U) {
            level = 4;
        } else if ((pending & (int_coper | int_vertb | int_blit)) != 0U) {
            level = 3;
        } else if ((pending & int_ports) != 0U) {
            level = 2;
        } else if ((pending & (int_tbe | int_dskblk | int_soft)) != 0U) {
            level = 1;
        }
        cpu.set_irq_level(level);
    }

    void amiga_system::update_overlay_from_cia() noexcept {
        overlay_active = (cia_a.port_a_pins() & 0x01U) != 0U;
    }

    void amiga_system::reset_board(chips::reset_kind kind) noexcept {
        agnus.reset(kind);
        denise.reset(kind);
        paula.reset(kind);
        cia_a.reset(kind);
        cia_b.reset(kind);

        palette_words.fill(0U);
        palette_bytes.fill(0U);
        bitplane_pointer.fill(0U);
        blitter_pointer.fill(0U);
        blitter_modulo.fill(0);
        blitter_data.fill(0U);
        bltcon0 = 0U;
        bltcon1 = 0U;
        bltafwm = 0xFFFFU;
        bltalwm = 0xFFFFU;
        bltsize = 0U;
        blitter_cycles_remaining = 0U;
        cop1lc = 0U;
        cop2lc = 0U;
        custom_high_latch = 0U;

        disk_pointer = 0U;
        disk_length = 0U;
        disk_sync = 0x4489U;
        disk_adkcon = 0U;
        disk_data = 0U;
        disk_last_length_write = 0U;
        disk_shift = 0U;
        disk_dma_bytes_remaining = 0U;
        disk_dma_armed = false;
        disk_byte_valid = false;
        disk_sync_match = false;
        disk_wordsync_waiting = false;

        joydat.fill(0U);
        pot_counter.fill(0xFFFFU);
        pot_target.fill(0xFFFFU);
        beam_line_epoch = 0U;
        pot_start_line_epoch = 0U;
        potgo = 0U;
        pot_counters_running = false;

        intena = 0U;
        intreq = 0U;
        cia_a_irq = false;
        cia_b_irq = false;
        frame_index = 0U;
        cpu.set_irq_level(0);

        overlay_active = true;
        reset_zorro2_autoconfig();
        floppy_selected_mask = 0U;
        floppy_active_drive = no_floppy_drive;
        floppy_side_pos = 0U;
        floppy_motor_on = false;
        floppy_selected = false;
        floppy_step_line = true;
        floppy_direction_inward = false;
        for (std::size_t drive_index = 0U; drive_index < floppy_drives.size(); ++drive_index) {
            auto& drive = floppy_drives[drive_index];
            preserve_dirty_floppy_track(drive_index);
            if (drive_index == 0U) {
                drive.connected = true;
            }
            drive.motor_on = false;
            amiga_reset_floppy_stream_phase(drive);
            drive.weak_bit_lfsr = weak_bit_lfsr_seed;
            drive.index_line_accumulator = 0U;
            drive.track_stream_dirty = false;
        }

        amiga_keyboard_reset(keyboard);
        keyboard_byte_in_flight = false;
        keyboard_ack_low_seen = false;

        std::copy(chip_ram.begin(), chip_ram.end(), paula.chipram().begin());
        agnus.attach_chip_ram(chip_ram);
        agnus.attach_palette(palette_bytes);
    }

    void amiga_system::reset_board_from_cpu() noexcept {
        reset_board(chips::reset_kind::hard);
    }

    std::uint32_t
    amiga_system::cpu_bus_wait_cycles(std::uint32_t address, bool /*program*/, bool write,
                                         std::uint32_t instruction_cycles_before_access,
                                         std::uint32_t instruction_wait_cycles) const noexcept {
        const bool chip_ram_window = (address & 0x00FFFFFFU) < chip_ram.size();
        if (!chip_ram_window) {
            return 0U;
        }
        // Reads below $080000 see Kickstart while the reset overlay is active;
        // writes still fall through to chip RAM and can contend with DMA.
        if (!write && overlay_active) {
            return 0U;
        }

        std::uint32_t wait = agnus.display_dma_cpu_wait(instruction_cycles_before_access).cycles;
        wait = std::max(wait, agnus.sprite_dma_cpu_wait_cycles(instruction_cycles_before_access));

        const std::uint16_t dma = agnus.read_dmaconr();
        const std::uint16_t busy_mask = static_cast<std::uint16_t>(
            chips::video::agnus::dmacon_dmaen | chips::video::agnus::dmacon_blten |
            chips::video::agnus::dmacon_bbusy);
        const bool blitter_dma_busy =
            (dma & busy_mask) == busy_mask && blitter_cycles_remaining != 0U;
        const bool blitter_priority = (dma & chips::video::agnus::dmacon_bltpri) != 0U;
        if (blitter_dma_busy && blitter_priority) {
            const std::uint32_t owned_dma_wait = wait;
            const std::uint32_t blitter_wait = static_cast<std::uint32_t>(std::min<std::uint64_t>(
                blitter_cycles_remaining, std::numeric_limits<std::uint32_t>::max()));
            const std::uint32_t blitter_residual = blitter_wait > instruction_wait_cycles
                                                       ? blitter_wait - instruction_wait_cycles
                                                       : 0U;
            wait = saturating_add(owned_dma_wait, blitter_residual);
        } else if (blitter_dma_busy) {
            // In non-nasty mode Agnus forces the blitter to release one slot
            // after three consecutive unsatisfied 68000 memory cycles. Display
            // DMA delays also leave the CPU unsatisfied, so they count toward
            // that release window instead of stacking as an independent wait.
            const std::uint32_t blitter_residual =
                non_nasty_blitter_release_wait_cycles > instruction_wait_cycles
                    ? non_nasty_blitter_release_wait_cycles - instruction_wait_cycles
                    : 0U;
            wait = std::max(wait, blitter_residual);
        }
        return wait;
    }

    void amiga_system::save_state(chips::state_writer& writer) const {
        writer.u32(state_version);
        writer.boolean(overlay_active);
        writer.u16(intena);
        writer.u16(intreq);
        writer.boolean(cia_a_irq);
        writer.boolean(cia_b_irq);
        writer.u64(frame_index);
        writer.u32(cop1lc);
        writer.u32(cop2lc);
        writer.u16(custom_high_latch);
        writer.u32(disk_pointer);
        writer.u16(disk_length);
        writer.u16(disk_sync);
        writer.u16(disk_adkcon);
        writer.u16(disk_data);
        writer.u16(disk_last_length_write);
        writer.u16(disk_shift);
        writer.u32(disk_dma_bytes_remaining);
        writer.boolean(disk_dma_armed);
        writer.boolean(disk_byte_valid);
        writer.boolean(disk_sync_match);
        writer.boolean(disk_wordsync_waiting);
        for (std::uint16_t value : joydat) {
            writer.u16(value);
        }
        for (std::uint8_t value : joystick_state) {
            writer.u8(value);
        }
        for (std::uint16_t value : pot_counter) {
            writer.u16(value);
        }
        for (std::uint16_t value : pot_target) {
            writer.u16(value);
        }
        writer.u64(beam_line_epoch);
        writer.u64(pot_start_line_epoch);
        writer.u16(potgo);
        writer.boolean(pot_counters_running);
        writer.u8(floppy_selected_mask);
        writer.u8(floppy_active_drive);
        writer.u8(floppy_side_pos);
        writer.boolean(floppy_motor_on);
        writer.boolean(floppy_selected);
        writer.boolean(floppy_step_line);
        writer.boolean(floppy_direction_inward);
        for (const auto& drive : floppy_drives) {
            writer.u64(static_cast<std::uint64_t>(drive.stream_offset));
            writer.u8(static_cast<std::uint8_t>(drive.stream_bit_offset & 0x07U));
            writer.u8(drive.cylinder_pos);
            writer.boolean(drive.connected);
            writer.boolean(drive.motor_on);
            writer.boolean(drive.write_protected);
            writer.boolean(drive.change_latch);
            writer.u32(drive.index_line_accumulator);
            writer.u64(drive.byte_clock_accumulator);
            writer.u16(static_cast<std::uint16_t>(
                std::min(drive.track_stream_track_index, drive.raw_track_cache.size() - 1U)));
            writer.boolean(drive.track_stream_dirty);
            writer.u8(drive.stream_read_shift);
            writer.u8(
                static_cast<std::uint8_t>(std::min<std::uint8_t>(drive.stream_read_bit_count, 7U)));
            writer.u8(drive.stream_write_latch);
            writer.u8(drive.stream_write_shift);
            writer.u8(static_cast<std::uint8_t>(
                std::min<std::uint8_t>(drive.stream_write_bits_remaining, 8U)));
            writer.u16(drive.weak_bit_lfsr == 0U ? weak_bit_lfsr_seed : drive.weak_bit_lfsr);
            writer.blob(std::span<const std::uint8_t>(drive.image));
            writer.blob(std::span<const std::uint8_t>(drive.track_stream));
            writer.blob(std::span<const std::uint8_t>(drive.weak_bit_stream));
            std::uint16_t cached_track_count = 0U;
            for (const auto& cached_track : drive.raw_track_cache) {
                if (!cached_track.empty()) {
                    ++cached_track_count;
                }
            }
            writer.u16(cached_track_count);
            for (std::size_t track = 0U; track < drive.raw_track_cache.size(); ++track) {
                const auto& cached_track = drive.raw_track_cache[track];
                if (cached_track.empty()) {
                    continue;
                }
                writer.u16(static_cast<std::uint16_t>(track));
                writer.blob(std::span<const std::uint8_t>(cached_track));
            }
            std::uint16_t cached_weak_track_count = 0U;
            for (const auto& cached_weak_bits : drive.weak_bit_cache) {
                if (!cached_weak_bits.empty()) {
                    ++cached_weak_track_count;
                }
            }
            writer.u16(cached_weak_track_count);
            for (std::size_t track = 0U; track < drive.weak_bit_cache.size(); ++track) {
                const auto& cached_weak_bits = drive.weak_bit_cache[track];
                if (cached_weak_bits.empty()) {
                    continue;
                }
                writer.u16(static_cast<std::uint16_t>(track));
                writer.blob(std::span<const std::uint8_t>(cached_weak_bits));
            }
        }
        writer.u8(static_cast<std::uint8_t>(keyboard.count));
        writer.boolean(keyboard_byte_in_flight);
        writer.boolean(keyboard_ack_low_seen);
        writer.boolean(keyboard.caps_lock_led);
        for (std::size_t i = 0U; i < keyboard.queue.size(); ++i) {
            const std::uint8_t value =
                i < keyboard.count
                    ? keyboard.queue[(keyboard.head + i) % keyboard.queue.size()]
                    : 0U;
            writer.u8(value);
        }
        for (bool down : keyboard.key_down) {
            writer.boolean(down);
        }
        for (std::size_t plane = 0U; plane < bitplane_pointer.size(); ++plane) {
            writer.u32(agnus.bitplane_pointer(static_cast<std::uint32_t>(plane)));
        }
        for (std::uint32_t ptr : blitter_pointer) {
            writer.u32(ptr);
        }
        for (std::int16_t modulo : blitter_modulo) {
            writer.u16(static_cast<std::uint16_t>(modulo));
        }
        for (std::uint16_t data : blitter_data) {
            writer.u16(data);
        }
        writer.u16(bltcon0);
        writer.u16(bltcon1);
        writer.u16(bltafwm);
        writer.u16(bltalwm);
        writer.u16(bltsize);
        writer.u64(blitter_cycles_remaining);
        writer.bytes(chip_ram);
        writer.u64(static_cast<std::uint64_t>(fast_ram.size()));
        writer.bytes(fast_ram);
        writer.u64(static_cast<std::uint64_t>(zorro2_autoconfig_index));
        writer.u8(static_cast<std::uint8_t>(zorro2_base_low_nibble & 0x0FU));
        writer.boolean(zorro2_base_low_nibble_valid);
        writer.u64(static_cast<std::uint64_t>(zorro2_boards.size()));
        for (const auto& board : zorro2_boards) {
            writer.u32(board.assigned_base);
            writer.boolean(board.configured);
            writer.boolean(board.shut_up);
        }
        for (std::uint16_t color : palette_words) {
            writer.u16(color);
        }
    }

    void amiga_system::load_state(chips::state_reader& reader) {
        if (reader.u32() != state_version) {
            reader.fail();
            return;
        }
        overlay_active = reader.boolean();
        intena = reader.u16();
        intreq = reader.u16();
        cia_a_irq = reader.boolean();
        cia_b_irq = reader.boolean();
        frame_index = reader.u64();
        cop1lc = reader.u32() & copper_address_mask;
        cop2lc = reader.u32() & copper_address_mask;
        custom_high_latch = reader.u16();
        disk_pointer = reader.u32() & chip_ram_address_mask(chip_ram.size());
        disk_length = reader.u16();
        disk_sync = reader.u16();
        disk_adkcon = reader.u16();
        disk_data = reader.u16();
        disk_last_length_write = reader.u16();
        disk_shift = reader.u16();
        disk_dma_bytes_remaining = reader.u32();
        disk_dma_armed = reader.boolean();
        disk_byte_valid = reader.boolean();
        disk_sync_match = reader.boolean();
        disk_wordsync_waiting = reader.boolean();
        for (std::uint16_t& value : joydat) {
            value = reader.u16();
        }
        for (std::uint8_t& value : joystick_state) {
            value = reader.u8();
        }
        for (std::uint16_t& value : pot_counter) {
            value = reader.u16();
        }
        for (std::uint16_t& value : pot_target) {
            value = reader.u16();
        }
        beam_line_epoch = reader.u64();
        pot_start_line_epoch = reader.u64();
        potgo = reader.u16();
        pot_counters_running = reader.boolean();
        floppy_selected_mask = static_cast<std::uint8_t>(reader.u8() & 0x0FU);
        floppy_active_drive = reader.u8();
        if (static_cast<std::size_t>(floppy_active_drive) >= floppy_drives.size()) {
            floppy_active_drive = no_floppy_drive;
        }
        floppy_side_pos = reader.u8();
        floppy_motor_on = reader.boolean();
        floppy_selected = reader.boolean();
        floppy_step_line = reader.boolean();
        floppy_direction_inward = reader.boolean();
        std::array<std::uint64_t, floppy_drive_count> saved_floppy_stream_offset{};
        std::array<std::size_t, floppy_drive_count> saved_floppy_track_index{};
        std::array<std::uint8_t, floppy_drive_count> saved_floppy_stream_bit_offset{};
        std::array<std::uint8_t, floppy_drive_count> saved_floppy_read_shift{};
        std::array<std::uint8_t, floppy_drive_count> saved_floppy_read_bit_count{};
        std::array<std::uint8_t, floppy_drive_count> saved_floppy_write_latch{};
        std::array<std::uint8_t, floppy_drive_count> saved_floppy_write_shift{};
        std::array<std::uint8_t, floppy_drive_count> saved_floppy_write_bits_remaining{};
        std::array<std::uint16_t, floppy_drive_count> saved_floppy_weak_bit_lfsr{};
        std::array<bool, floppy_drive_count> saved_floppy_connected{};
        std::array<bool, floppy_drive_count> saved_floppy_motor_on{};
        std::array<bool, floppy_drive_count> saved_floppy_track_dirty{};
        std::array<std::vector<std::uint8_t>, floppy_drive_count> saved_floppy_image{};
        std::array<std::vector<std::uint8_t>, floppy_drive_count> saved_floppy_track_stream{};
        std::array<std::vector<std::uint8_t>, floppy_drive_count> saved_floppy_weak_bit_stream{};
        for (std::size_t drive = 0U; drive < floppy_drives.size(); ++drive) {
            auto& df = floppy_drives[drive];
            saved_floppy_stream_offset[drive] = reader.u64();
            saved_floppy_stream_bit_offset[drive] = static_cast<std::uint8_t>(reader.u8() & 0x07U);
            df.cylinder_pos = reader.u8();
            saved_floppy_connected[drive] = reader.boolean();
            saved_floppy_motor_on[drive] = reader.boolean();
            df.write_protected = reader.boolean();
            df.change_latch = reader.boolean();
            df.index_line_accumulator = reader.u32();
            df.byte_clock_accumulator = reader.u64();
            saved_floppy_track_index[drive] =
                std::min<std::size_t>(reader.u16(), df.raw_track_cache.size() - 1U);
            saved_floppy_track_dirty[drive] = reader.boolean();
            saved_floppy_read_shift[drive] = reader.u8();
            saved_floppy_read_bit_count[drive] =
                static_cast<std::uint8_t>(std::min<std::uint8_t>(reader.u8(), 7U));
            saved_floppy_write_latch[drive] = reader.u8();
            saved_floppy_write_shift[drive] = reader.u8();
            saved_floppy_write_bits_remaining[drive] =
                static_cast<std::uint8_t>(std::min<std::uint8_t>(reader.u8(), 8U));
            saved_floppy_weak_bit_lfsr[drive] = reader.u16();
            saved_floppy_image[drive] = reader.blob();
            if (!saved_floppy_image[drive].empty() &&
                saved_floppy_image[drive].size() != floppy_dd_size) {
                reader.fail();
                return;
            }
            saved_floppy_track_stream[drive] = reader.blob();
            saved_floppy_weak_bit_stream[drive] = reader.blob();
            amiga_clear_floppy_track_cache(df);
            df.track_stream_dirty = false;
            const std::uint16_t cached_track_count = reader.u16();
            for (std::uint16_t cached = 0U; cached < cached_track_count; ++cached) {
                const std::uint16_t track = reader.u16();
                auto track_blob = reader.blob();
                if (track < df.raw_track_cache.size()) {
                    df.raw_track_cache[track] = std::move(track_blob);
                }
            }
            const std::uint16_t cached_weak_track_count = reader.u16();
            for (std::uint16_t cached = 0U; cached < cached_weak_track_count; ++cached) {
                const std::uint16_t track = reader.u16();
                auto weak_blob = reader.blob();
                if (track < df.weak_bit_cache.size()) {
                    df.weak_bit_cache[track] = std::move(weak_blob);
                }
            }
        }
        const bool active_drive_selected =
            floppy_active_drive != no_floppy_drive &&
            (floppy_selected_mask & (1U << static_cast<unsigned>(floppy_active_drive))) != 0U;
        if (!active_drive_selected) {
            floppy_active_drive = no_floppy_drive;
            for (std::size_t drive = 0U; drive < floppy_drive_count; ++drive) {
                if ((floppy_selected_mask & (1U << drive)) != 0U) {
                    floppy_active_drive = static_cast<std::uint8_t>(drive);
                    break;
                }
            }
        }
        floppy_selected = floppy_active_drive != no_floppy_drive;
        const std::uint8_t saved_keyboard_queue_count = reader.u8();
        keyboard_byte_in_flight = reader.boolean();
        keyboard_ack_low_seen = reader.boolean();
        keyboard.caps_lock_led = reader.boolean();
        keyboard.head = 0U;
        keyboard.count = std::min<std::size_t>(saved_keyboard_queue_count, keyboard.queue.size());
        for (std::size_t i = 0U; i < keyboard.queue.size(); ++i) {
            const std::uint8_t value = reader.u8();
            keyboard.queue[i] = i < keyboard.count ? value : 0U;
        }
        for (bool& down : keyboard.key_down) {
            down = reader.boolean();
        }
        for (std::uint32_t& ptr : bitplane_pointer) {
            ptr = reader.u32() & chip_dma_address_mask;
        }
        for (std::uint32_t& ptr : blitter_pointer) {
            ptr = reader.u32() & chip_dma_address_mask;
        }
        for (std::int16_t& modulo : blitter_modulo) {
            modulo = static_cast<std::int16_t>(reader.u16());
        }
        for (std::uint16_t& data : blitter_data) {
            data = reader.u16();
        }
        bltcon0 = reader.u16();
        bltcon1 = reader.u16();
        bltafwm = reader.u16();
        bltalwm = reader.u16();
        bltsize = reader.u16();
        blitter_cycles_remaining = reader.u64();
        reader.bytes(chip_ram);
        const std::uint64_t saved_fast_ram_size = reader.u64();
        if (saved_fast_ram_size != static_cast<std::uint64_t>(fast_ram.size())) {
            reader.fail();
            return;
        }
        reader.bytes(fast_ram);
        zorro2_autoconfig_index = static_cast<std::size_t>(reader.u64());
        zorro2_base_low_nibble = static_cast<std::uint8_t>(reader.u8() & 0x0FU);
        zorro2_base_low_nibble_valid = reader.boolean();
        const std::uint64_t saved_zorro2_board_count = reader.u64();
        if (saved_zorro2_board_count != static_cast<std::uint64_t>(zorro2_boards.size())) {
            reader.fail();
            return;
        }
        for (auto& board : zorro2_boards) {
            board.assigned_base = reader.u32() & 0x00FF0000U;
            board.configured = reader.boolean();
            board.shut_up = reader.boolean();
            if (board.configured && board.memory) {
                const std::uint32_t expansion_end =
                    amiga_system::zorro2_expansion_ram_base +
                    amiga_system::zorro2_expansion_ram_size;
                const std::uint64_t board_end = static_cast<std::uint64_t>(board.assigned_base) +
                                                static_cast<std::uint64_t>(board.memory_size);
                if (board.assigned_base < amiga_system::zorro2_expansion_ram_base ||
                    board_end > expansion_end) {
                    reader.fail();
                    return;
                }
            }
        }
        if (zorro2_autoconfig_index > zorro2_boards.size()) {
            reader.fail();
            return;
        }
        for (std::size_t i = 0; i < palette_words.size(); ++i) {
            const std::uint16_t color = reader.u16();
            palette_words[i] = color;
            palette_bytes[i * 2U] = static_cast<std::uint8_t>(color >> 8U);
            palette_bytes[i * 2U + 1U] = static_cast<std::uint8_t>(color);
            denise.write_color(i, color);
        }
        if (reader.ok()) {
            static_cast<void>(active_zorro2_autoconfig_board());
            std::copy(chip_ram.begin(), chip_ram.end(), paula.chipram().begin());
            agnus.attach_chip_ram(chip_ram);
            agnus.attach_palette(palette_bytes);
            for (std::size_t plane = 0; plane < bitplane_pointer.size(); ++plane) {
                agnus.set_bitplane_pointer(static_cast<std::uint32_t>(plane),
                                           bitplane_pointer[plane]);
            }
            agnus.write_cop1lc(cop1lc);
            agnus.write_cop2lc(cop2lc);
            agnus.set_blitter_busy(blitter_cycles_remaining != 0U);
            const std::uint32_t index_lines = floppy_index_lines_per_revolution();
            for (std::size_t drive = 0U; drive < floppy_drives.size(); ++drive) {
                auto& df = floppy_drives[drive];
                df.image = std::move(saved_floppy_image[drive]);
                df.connected = drive == 0U || saved_floppy_connected[drive] || !df.image.empty();
                df.motor_on = saved_floppy_motor_on[drive] && df.connected;
                df.track_stream.clear();
                df.weak_bit_stream.clear();
                df.track_stream_dirty = false;
                update_floppy_track_stream(drive);
                if (!saved_floppy_track_stream[drive].empty()) {
                    df.track_stream = std::move(saved_floppy_track_stream[drive]);
                }
                if (!saved_floppy_weak_bit_stream[drive].empty() &&
                    saved_floppy_weak_bit_stream[drive].size() == df.track_stream.size()) {
                    df.weak_bit_stream = std::move(saved_floppy_weak_bit_stream[drive]);
                } else if (!df.weak_bit_stream.empty() &&
                           df.weak_bit_stream.size() != df.track_stream.size()) {
                    df.weak_bit_stream.clear();
                }
                df.weak_bit_lfsr = saved_floppy_weak_bit_lfsr[drive] == 0U
                                       ? weak_bit_lfsr_seed
                                       : saved_floppy_weak_bit_lfsr[drive];
                if (!df.track_stream.empty()) {
                    df.stream_offset = static_cast<std::size_t>(saved_floppy_stream_offset[drive] %
                                                                df.track_stream.size());
                    df.stream_bit_offset = saved_floppy_stream_bit_offset[drive];
                    df.track_stream_track_index = saved_floppy_track_index[drive];
                    df.track_stream_dirty = saved_floppy_track_dirty[drive];
                    df.stream_read_shift = saved_floppy_read_shift[drive];
                    df.stream_read_bit_count = saved_floppy_read_bit_count[drive];
                    df.stream_write_latch = saved_floppy_write_latch[drive];
                    df.stream_write_shift = saved_floppy_write_shift[drive];
                    df.stream_write_bits_remaining = saved_floppy_write_bits_remaining[drive];
                } else {
                    df.index_line_accumulator = 0U;
                    df.weak_bit_stream.clear();
                    amiga_reset_floppy_stream_phase(df);
                    df.track_stream_dirty = false;
                }
                if (index_lines != 0U) {
                    df.index_line_accumulator %= index_lines;
                    const std::uint64_t index_clocks = static_cast<std::uint64_t>(index_lines) *
                                                       chips::video::agnus::color_clocks_per_line;
                    if (index_clocks != 0U) {
                        df.byte_clock_accumulator %= index_clocks;
                    }
                }
            }
            if (const auto* drive = active_floppy_drive_state();
                floppy_selected && drive != nullptr) {
                floppy_motor_on = drive->motor_on;
            } else {
                floppy_motor_on = false;
            }
            update_irq_level();
        }
    }

    std::unique_ptr<amiga_system> assemble_amiga(std::vector<std::uint8_t> kickstart_rom,
                                                       const amiga_config& config) {
        auto sys = std::make_unique<amiga_system>();
        amiga_system* s = sys.get();
        const auto& model = amiga_model_profile(config.model);
        const auto& chipset = amiga_chipset_profile(model.chipset);
        const std::size_t active_chip_ram_size = model.chip_ram_size;
        const std::size_t active_fast_ram_size =
            amiga_fast_ram_size_for_config(config, amiga_system::fast_ram_max_size);
        s->copper_address_mask = chipset.copper_address_mask;
        s->chip_ram.assign(active_chip_ram_size, 0U);
        s->fast_ram.assign(active_fast_ram_size, 0U);
        if (!s->fast_ram.empty()) {
            s->zorro2_boards.push_back(zorro2_expansion_board{
                .product = 0x01U,
                .manufacturer = 2011U,
                .serial = 0x4D4E0001U,
                .memory_size = s->fast_ram.size(),
                .assigned_base = 0U,
                .memory = true,
                .configured = false,
                .shut_up = false});
        }
        s->paula.resize_chipram(active_chip_ram_size);
        normalize_kickstart(s->kickstart_rom, kickstart_rom);

        const bool pal = config.video_region == mnemos::video_region::pal;
        const std::uint32_t frame_hz = pal ? 50U : 60U;
        const std::uint32_t scanlines_per_frame = pal ? chips::video::agnus::scanlines_pal
                                                      : chips::video::agnus::scanlines_ntsc;
        const std::uint32_t hsync_hz = scanlines_per_frame * frame_hz;
        s->agnus.set_pal(pal);
        s->agnus.set_copper_address_mask(s->copper_address_mask);
        s->agnus.attach_chip_ram(s->chip_ram);
        s->agnus.attach_palette(s->palette_bytes);
        s->paula.set_clock_divider(1);
        s->cia_a.configure(chips::peripheral::cia8520::config{
            .read_port_a = [s] { return s->cia_a_port_a_inputs(); },
            .read_port_b = [] { return static_cast<std::uint8_t>(0xFFU); },
            .write_port_a = [s](std::uint8_t) { s->update_overlay_from_cia(); },
            .write_port_b = {},
            .write_sp = [s](bool level) { s->write_cia_a_sp(level); },
            .irq_edge =
                [s](bool asserted) {
                    s->cia_a_irq = asserted;
                    s->update_irq_level();
                },
            .tod_tick_hz = pal ? 709'379U : 715'909U,
            .tod_src_hz = frame_hz,
        });
        s->cia_b.configure(chips::peripheral::cia8520::config{
            .read_port_a = [] { return static_cast<std::uint8_t>(0xFFU); },
            .read_port_b = [] { return static_cast<std::uint8_t>(0xFFU); },
            .write_port_a = {},
            .write_port_b = [s](std::uint8_t) { s->write_cia_b_port_b(s->cia_b.port_b_pins()); },
            .irq_edge =
                [s](bool asserted) {
                    s->cia_b_irq = asserted;
                    s->update_irq_level();
                },
            .tod_tick_hz = pal ? 709'379U : 715'909U,
            .tod_src_hz = hsync_hz,
        });
        s->overlay_active = true;

        // Chip RAM is mapped through MMIO callbacks so Paula's DMA-visible RAM
        // stays coherent with CPU writes.
        s->bus.map_mmio(
            amiga_system::chip_ram_base, static_cast<std::uint32_t>(s->chip_ram.size()),
            [s](std::uint32_t a) {
                return s->chip_ram[mirrored_chip_byte_address(s->chip_ram, a)];
            },
            [s](std::uint32_t a, std::uint8_t v) {
                const std::size_t off = mirrored_chip_byte_address(s->chip_ram, a);
                s->chip_ram[off] = v;
                s->paula.chipram()[off] = v;
            },
            0);
        if (!s->fast_ram.empty()) {
            s->bus.map_mmio(
                amiga_system::zorro2_expansion_ram_base,
                amiga_system::zorro2_expansion_ram_size,
                [s](std::uint32_t a) {
                    const auto* board = configured_zorro2_memory_board_at(*s, a);
                    if (board == nullptr) {
                        return static_cast<std::uint8_t>(0xFFU);
                    }
                    return s->fast_ram[a - board->assigned_base];
                },
                [s](std::uint32_t a, std::uint8_t v) {
                    const auto* board = configured_zorro2_memory_board_at(*s, a);
                    if (board == nullptr) {
                        return;
                    }
                    s->fast_ram[a - board->assigned_base] = v;
                },
                0,
                [s](std::uint32_t a, bool) {
                    return configured_zorro2_memory_board_at(*s, a) != nullptr;
                });
            s->bus.map_mmio16(
                amiga_system::zorro2_autoconfig_base,
                amiga_system::zorro2_autoconfig_size,
                [s](std::uint32_t a) {
                    const auto* board = s->active_zorro2_autoconfig_board();
                    return board == nullptr ? static_cast<std::uint8_t>(0xFFU)
                                            : zorro2_autoconfig_read(
                                                  *board, amiga_system::zorro2_autoconfig_base, a);
                },
                [s](std::uint32_t a, std::uint8_t v) { zorro2_autoconfig_write(*s, a, v); },
                [s](std::uint32_t a) {
                    const auto* board = s->active_zorro2_autoconfig_board();
                    if (board == nullptr) {
                        return static_cast<std::uint16_t>(0xFFFFU);
                    }
                    const std::uint8_t value = zorro2_autoconfig_read(
                        *board, amiga_system::zorro2_autoconfig_base, a);
                    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(value) << 8U) |
                                                      value);
                },
                [s](std::uint32_t a, std::uint16_t v) {
                    zorro2_autoconfig_write(*s, a, static_cast<std::uint8_t>(v >> 8U));
                    zorro2_autoconfig_write(*s, a + 1U, static_cast<std::uint8_t>(v));
                },
                0,
                [s](std::uint32_t, bool) { return s->zorro2_autoconfig_pending(); });
        }
        // Reset overlay: Kickstart answers reads at $000000 until CIA-A PA0 is
        // driven high. Writes always fall through to chip RAM.
        s->bus.map_rom(
            amiga_system::chip_ram_base, s->kickstart_rom, 10,
            [s](std::uint32_t, bool is_write) { return !is_write && s->overlay_active; });
        s->bus.map_rom(amiga_system::kickstart_base, s->kickstart_rom, 0);
        s->bus.map_mmio16(
            amiga_system::custom_base, 0x2000U,
            [s](std::uint32_t a) { return s->read_custom_byte(a); },
            [s](std::uint32_t a, std::uint8_t v) { s->write_custom_byte(a, v); },
            [s](std::uint32_t a) {
                const auto reg = static_cast<std::uint16_t>(
                    (a - amiga_system::custom_base) & 0x01FEU);
                return s->read_custom_word(reg);
            },
            [s](std::uint32_t a, std::uint16_t v) {
                const auto reg = static_cast<std::uint16_t>(
                    (a - amiga_system::custom_base) & 0x01FEU);
                s->write_custom_word(reg, v);
            },
            0);
        s->bus.map_mmio(
            amiga_system::cia_a_base, 0x1000U,
            [s](std::uint32_t a) {
                return (a & 1U) != 0U ? s->cia_a.read(cia_register(a))
                                      : static_cast<std::uint8_t>(0xFFU);
            },
            [s](std::uint32_t a, std::uint8_t v) {
                if ((a & 1U) != 0U) {
                    s->cia_a.write(cia_register(a), v);
                }
            },
            0);
        s->bus.map_mmio(
            amiga_system::cia_b_base, 0x1000U,
            [s](std::uint32_t a) { return s->cia_b.read(cia_register(a)); },
            [s](std::uint32_t a, std::uint8_t v) { s->cia_b.write(cia_register(a), v); }, 0);

        s->agnus.set_scanline_callback([s](std::uint32_t) {
            ++s->beam_line_epoch;
            s->tick_floppy_scanline();
        });
        s->agnus.set_cycle_callback([s] {
            s->tick_blitter_cycle();
            s->tick_floppy_data_cycle();
        });
        s->agnus.set_vblank_callback([s](std::uint32_t) {
            ++s->frame_index;
            s->request_interrupt(amiga_system::int_vertb);
        });
        s->agnus.set_custom_write_callback(
            [s](std::uint16_t reg, std::uint16_t value) { s->write_custom_word(reg, value); });

        s->cpu.attach_bus(s->bus);
        s->cpu.set_bus_wait_callback([s](std::uint32_t address, bool program, bool write,
                                          std::uint32_t instruction_cycles_before_access,
                                          std::uint32_t instruction_wait_cycles) {
            return s->cpu_bus_wait_cycles(address, program, write, instruction_cycles_before_access,
                                           instruction_wait_cycles);
        });
        s->cpu.set_reset_callback([s] { s->reset_board_from_cpu(); });
        s->reset_board(chips::reset_kind::power_on);
        s->cpu.reset(chips::reset_kind::power_on);
        return sys;
    }

} // namespace mnemos::manifests::amiga
