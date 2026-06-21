#include "fds.hpp"

#include <algorithm>
#include <array>
#include <cstddef>

namespace mnemos::manifests::nes {

    namespace {
        constexpr std::size_t k_header = 16U;   // "FDS\x1A" + side count + 11 reserved
        constexpr std::size_t k_ram = 0x8000U;  // 32 KiB PRG-RAM ($6000-$DFFF)
        constexpr std::size_t k_bios = 0x2000U; // 8 KiB BIOS ($E000-$FFFF)

        // Disk drive timing (CPU cycles). Hardware delivers a byte every ~149 cycles,
        // but because we hold each byte until the CPU reads it (no overrun is
        // possible), a shorter period just shortens the famously-long FDS load without
        // risking lost bytes. A modest lead-in after a transfer reset stands in for
        // the head seek.
        constexpr int k_cycles_per_byte = 50;
        constexpr int k_seek_delay = 8000;

        [[nodiscard]] bool has_fds_header(std::span<const std::uint8_t> d) noexcept {
            return d.size() >= k_header && d[0] == 'F' && d[1] == 'D' && d[2] == 'S' &&
                   d[3] == 0x1AU;
        }

        // The RP2C33 RAM adapter + disk drive, modelled as an nes_mapper so it reuses
        // the board's IRQ wiring + the per-scanline cpu-timer hook. It owns no PRG/CHR
        // banking (the BIOS loads files into the flat 32 KiB RAM); its job is the disk
        // byte stream + the disk/timer IRQs + the register window.
        class fds_mapper final : public nes_mapper {
          public:
            fds_mapper(topology::bus& bus, chips::video::ppu2c02& ppu,
                       std::span<const std::uint8_t> disk, std::span<const std::uint8_t> bios,
                       std::span<std::uint8_t> ram, std::span<std::uint8_t> chr_ram) noexcept
                : nes_mapper(bus, ppu, std::span<const std::uint8_t>{}, chr_ram,
                             /*chr_is_ram=*/true),
                  disk_(disk), bios_(bios), ram_(ram) {}

            void reset() override {
                // $6000-$DFFF: 32 KiB PRG-RAM the BIOS deposits disk files into.
                if (ram_.size() >= k_ram) {
                    bus_->map_ram(0x6000U, ram_.subspan(0, k_ram));
                }
                // $E000-$FFFF: the 8 KiB BIOS (carries the reset/NMI/IRQ vectors).
                if (bios_.size() >= k_bios) {
                    bus_->map_rom(0xE000U, bios_.subspan(0, k_bios));
                }
                attach_chr(); // 8 KiB CHR-RAM
                ppu_->set_mirroring(chips::video::ppu2c02::mirroring::horizontal);

                // $4020-$409F: the disk + sound register window. (The standard APU/IO
                // window $4000-$401F is mapped by the board; this sits just above it.)
                bus_->map_mmio(
                    0x4020U, 0x0080U,
                    [this](std::uint32_t addr) -> std::uint8_t {
                        return read_reg(static_cast<std::uint16_t>(addr));
                    },
                    [this](std::uint32_t addr, std::uint8_t value) {
                        write_reg(static_cast<std::uint16_t>(addr), value);
                    });

                // Disk-drive + IRQ state.
                motor_on_ = false;
                xfer_reset_ = true;
                read_mode_ = true;
                irq_xfer_enable_ = false;
                crc_enable_ = false;
                horizontal_ = true;
                byte_ready_ = false;
                disk_irq_ = false;
                end_of_head_ = false;
                current_side_ = 0;
                disk_pos_ = 0;
                seek_remaining_ = 0;
                byte_accum_ = 0;
                data_latch_ = 0;
                timer_reload_ = 0;
                timer_counter_ = 0;
                timer_enable_ = false;
                timer_repeat_ = false;
                timer_irq_ = false;
                io_enable_ = false;
                build_stream();
                publish_irq();
            }

            // The FDS has no $8000-$FFFF cartridge registers (that space is BIOS ROM);
            // all I/O is through the $4020-$409F MMIO window handled in write_reg.
            void write(std::uint16_t /*addr*/, std::uint8_t /*value*/) override {}

            // Advance the disk byte transfer + the timer IRQ. Called once per scanline
            // by the board (ungated by rendering), carrying that line's CPU-cycle count.
            void clock_cpu_timer(std::uint32_t cpu_cycles) override {
                clock_timer(static_cast<int>(cpu_cycles));
                clock_disk(static_cast<int>(cpu_cycles));
            }

            void save_state(chips::state_writer& writer) const override {
                writer.boolean(motor_on_);
                writer.boolean(xfer_reset_);
                writer.boolean(read_mode_);
                writer.boolean(irq_xfer_enable_);
                writer.boolean(crc_enable_);
                writer.boolean(horizontal_);
                writer.boolean(byte_ready_);
                writer.boolean(disk_irq_);
                writer.boolean(end_of_head_);
                writer.u8(static_cast<std::uint8_t>(current_side_));
                writer.u32(static_cast<std::uint32_t>(disk_pos_));
                writer.u32(static_cast<std::uint32_t>(seek_remaining_));
                writer.u32(static_cast<std::uint32_t>(byte_accum_));
                writer.u8(data_latch_);
                writer.u16(timer_reload_);
                writer.u32(static_cast<std::uint32_t>(timer_counter_));
                writer.boolean(timer_enable_);
                writer.boolean(timer_repeat_);
                writer.boolean(timer_irq_);
                writer.boolean(io_enable_);
            }
            void load_state(chips::state_reader& reader) override {
                motor_on_ = reader.boolean();
                xfer_reset_ = reader.boolean();
                read_mode_ = reader.boolean();
                irq_xfer_enable_ = reader.boolean();
                crc_enable_ = reader.boolean();
                horizontal_ = reader.boolean();
                byte_ready_ = reader.boolean();
                disk_irq_ = reader.boolean();
                end_of_head_ = reader.boolean();
                current_side_ = reader.u8();
                disk_pos_ = static_cast<std::size_t>(reader.u32());
                seek_remaining_ = static_cast<int>(reader.u32());
                byte_accum_ = static_cast<int>(reader.u32());
                data_latch_ = reader.u8();
                timer_reload_ = reader.u16();
                timer_counter_ = static_cast<int>(reader.u32());
                timer_enable_ = reader.boolean();
                timer_repeat_ = reader.boolean();
                timer_irq_ = reader.boolean();
                io_enable_ = reader.boolean();
                ppu_->set_mirroring(horizontal_ ? chips::video::ppu2c02::mirroring::horizontal
                                                : chips::video::ppu2c02::mirroring::vertical);
                publish_irq();
            }

          private:
            // The raw .fds stores blocks back-to-back with the 2-byte CRC + the
            // inter-block gaps stripped. The real BIOS reads each block's data THEN
            // two CRC bytes, so we rebuild the current side with a 2-byte gap after
            // every block (the controller forces CRC-OK -- $4030 bit4 stays clear).
            void build_stream() {
                stream_.clear();
                const std::size_t base = current_side_ * k_fds_side_size;
                if (base >= disk_.size()) {
                    return;
                }
                const std::span<const std::uint8_t> side =
                    disk_.subspan(base, std::min(k_fds_side_size, disk_.size() - base));
                std::size_t p = 0;
                const auto emit = [&](std::size_t len) {
                    len = std::min(len, side.size() - p);
                    stream_.insert(stream_.end(), side.begin() + static_cast<std::ptrdiff_t>(p),
                                   side.begin() + static_cast<std::ptrdiff_t>(p + len));
                    stream_.push_back(0x00U); // synthetic CRC byte 0
                    stream_.push_back(0x00U); // synthetic CRC byte 1
                    p += len;
                };
                if (p >= side.size() || side[p] != 0x01U) {
                    return; // not a disk-info block -> leave the stream empty
                }
                emit(56U); // block 1: disk info
                if (p + 1U >= side.size() || side[p] != 0x02U) {
                    return;
                }
                const std::size_t files = side[p + 1U];
                emit(2U); // block 2: file amount
                for (std::size_t f = 0; f < files; ++f) {
                    if (p >= side.size() || side[p] != 0x03U) {
                        break; // block 3: file header
                    }
                    const std::size_t size =
                        (p + 14U < side.size())
                            ? static_cast<std::size_t>(side[p + 13U]) |
                                  (static_cast<std::size_t>(side[p + 14U]) << 8U)
                            : 0U;
                    emit(16U);
                    if (p >= side.size() || side[p] != 0x04U) {
                        break; // block 4: file data ($04 + `size` bytes)
                    }
                    emit(1U + size);
                }
            }

            std::uint8_t read_reg(std::uint16_t addr) {
                switch (addr) {
                case 0x4030U: { // disk status
                    std::uint8_t s = 0U;
                    if (timer_irq_) {
                        s |= 0x01U;
                    }
                    if (disk_irq_) {
                        s |= 0x02U; // disk-IRQ-pending (compat mirror of the transfer flag)
                    }
                    if (horizontal_) {
                        s |= 0x08U;
                    }
                    // bit 4 (CRC error) forced clear: .fds carries no CRC bytes.
                    if (end_of_head_) {
                        s |= 0x40U;
                    }
                    if (byte_ready_) {
                        s |= 0x80U; // byte transfer flag (the canonical data-ready bit)
                    }
                    timer_irq_ = false; // reading $4030 acknowledges the timer IRQ
                    publish_irq();
                    return s;
                }
                case 0x4031U: { // read data: returns the latched byte, clears the flag + IRQ
                    byte_ready_ = false;
                    disk_irq_ = false;
                    publish_irq();
                    return data_latch_;
                }
                case 0x4032U: { // drive status (active-high error bits)
                    // Ready as soon as a disk is inserted (the head can reach it); the
                    // motor gates the byte transfer, not this flag. No disk => both the
                    // not-inserted and not-ready bits set. bit 2 (write protect) clear.
                    return disk_.empty() ? 0x03U : 0x00U;
                }
                case 0x4033U:
                    return 0x80U; // external connector: battery good
                default:
                    return 0x00U; // FDS sound read ports ($4090-$4097): wired in a later step
                }
            }

            void write_reg(std::uint16_t addr, std::uint8_t value) {
                switch (addr) {
                case 0x4020U:
                    timer_reload_ = static_cast<std::uint16_t>((timer_reload_ & 0xFF00U) | value);
                    break;
                case 0x4021U:
                    timer_reload_ = static_cast<std::uint16_t>(
                        (timer_reload_ & 0x00FFU) | (static_cast<std::uint16_t>(value) << 8U));
                    break;
                case 0x4022U:
                    timer_repeat_ = (value & 0x01U) != 0U;
                    timer_enable_ = io_enable_ && (value & 0x02U) != 0U;
                    if (timer_enable_) {
                        timer_counter_ = timer_reload_; // enabling reloads the counter
                    } else {
                        timer_irq_ = false;
                    }
                    publish_irq();
                    break;
                case 0x4023U:
                    io_enable_ = (value & 0x01U) != 0U; // gates the disk + timer machinery
                    if (!io_enable_) {
                        timer_enable_ = false;
                        timer_irq_ = false;
                        disk_irq_ = false;
                        publish_irq();
                    }
                    break;
                case 0x4024U:
                    break; // write-data port: write mode is not modelled (read-only games)
                case 0x4025U:
                    write_control(value);
                    break;
                default:
                    break; // FDS sound write ports ($4040-$408A): wired in a later step
                }
            }

            void write_control(std::uint8_t value) {
                // $4025 (the model the real BIOS targets): bit0 motor on, bit1 transfer
                // reset (1 = hold/rewind the head, 0 = run the transfer), bit2 read mode,
                // bit3 mirroring, bit6 CRC, bit7 byte-transfer IRQ. The BIOS's start
                // sequence is $2E (idle) -> $2F (motor on, held) -> $2D (motor on, run).
                motor_on_ = (value & 0x01U) != 0U;
                xfer_reset_ = (value & 0x02U) != 0U;
                read_mode_ = (value & 0x04U) != 0U;
                horizontal_ = (value & 0x08U) != 0U;
                crc_enable_ = (value & 0x40U) != 0U;
                irq_xfer_enable_ = (value & 0x80U) != 0U;
                ppu_->set_mirroring(horizontal_ ? chips::video::ppu2c02::mirroring::horizontal
                                                : chips::video::ppu2c02::mirroring::vertical);
                if (xfer_reset_) {
                    // The head rewinds to the start of the (gapped) side stream and
                    // waits out the lead-in before the first byte is delivered.
                    disk_pos_ = 0;
                    seek_remaining_ = k_seek_delay;
                    byte_accum_ = 0;
                    byte_ready_ = false;
                    end_of_head_ = false;
                }
                publish_irq();
            }

            void clock_timer(int cpu_cycles) {
                if (!timer_enable_) {
                    return;
                }
                if (timer_counter_ <= cpu_cycles) {
                    timer_irq_ = true;
                    if (timer_repeat_ && timer_reload_ != 0U) {
                        const int over = cpu_cycles - timer_counter_;
                        timer_counter_ = static_cast<int>(timer_reload_) -
                                         (over % static_cast<int>(timer_reload_));
                    } else {
                        timer_enable_ = false;
                        timer_counter_ = 0;
                    }
                    publish_irq();
                } else {
                    timer_counter_ -= cpu_cycles;
                }
            }

            void clock_disk(int cpu_cycles) {
                if (!motor_on_ || xfer_reset_ || !read_mode_ || disk_.empty() || end_of_head_) {
                    return;
                }
                int cyc = cpu_cycles;
                if (seek_remaining_ > 0) {
                    if (seek_remaining_ >= cyc) {
                        seek_remaining_ -= cyc;
                        return;
                    }
                    cyc -= seek_remaining_;
                    seek_remaining_ = 0;
                }
                // Hold the current byte until the CPU reads $4031: the BIOS does not
                // read in a cycle-tight loop during setup, so a free-running drive
                // would lose the first hundreds of bytes (the block-1 marker) before
                // its reader engages. Delivering on demand keeps the byte stream in
                // order from the start, paced at no faster than the real ~149 cyc/byte.
                if (byte_ready_) {
                    return;
                }
                byte_accum_ += cyc;
                if (byte_accum_ >= k_cycles_per_byte && !end_of_head_) {
                    byte_accum_ -= k_cycles_per_byte;
                    deliver_byte();
                }
            }

            void deliver_byte() {
                if (disk_pos_ >= stream_.size()) {
                    end_of_head_ = true;
                    publish_irq();
                    return;
                }
                data_latch_ = stream_[disk_pos_++];
                byte_ready_ = true;
                disk_irq_ = true;
                publish_irq();
            }

            void publish_irq() {
                // The disk byte-transfer IRQ tracks the byte-ready flag (set on each
                // delivered byte, cleared only by a $4031 read or a transfer reset), so
                // enabling the IRQ ($4025 bit7) while a byte already waits still fires
                // it. The timer IRQ is independent. Both share the cartridge /IRQ line.
                raise_irq(timer_irq_ || (byte_ready_ && irq_xfer_enable_));
            }

            std::span<const std::uint8_t> disk_;
            std::span<const std::uint8_t> bios_;
            std::span<std::uint8_t> ram_;
            std::vector<std::uint8_t> stream_; // current side rebuilt with synthetic CRC gaps

            bool motor_on_{};
            bool xfer_reset_{true};
            bool read_mode_{true};
            bool irq_xfer_enable_{};
            bool crc_enable_{};
            bool horizontal_{true};
            bool byte_ready_{};
            bool disk_irq_{};
            bool end_of_head_{};
            std::size_t current_side_{};
            std::size_t disk_pos_{};
            int seek_remaining_{};
            int byte_accum_{};
            std::uint8_t data_latch_{};

            std::uint16_t timer_reload_{};
            int timer_counter_{};
            bool timer_enable_{};
            bool timer_repeat_{};
            bool timer_irq_{};
            bool io_enable_{};
        };
    } // namespace

    bool looks_like_fds(std::span<const std::uint8_t> data) noexcept {
        if (has_fds_header(data)) {
            return true;
        }
        // Headerless: a positive multiple of the side size that is not an iNES image.
        const bool is_ines = data.size() >= 4U && data[0] == 'N' && data[1] == 'E' &&
                             data[2] == 'S' && data[3] == 0x1AU;
        return !is_ines && data.size() >= k_fds_side_size && (data.size() % k_fds_side_size) == 0U;
    }

    std::vector<std::uint8_t> parse_fds_sides(std::span<const std::uint8_t> data) {
        if (!looks_like_fds(data)) {
            return {};
        }
        const std::size_t offset = has_fds_header(data) ? k_header : 0U;
        if (data.size() < offset) {
            return {};
        }
        const std::size_t body = data.size() - offset;
        const std::size_t sides = body / k_fds_side_size;
        if (sides == 0U) {
            return {};
        }
        return {data.begin() + static_cast<std::ptrdiff_t>(offset),
                data.begin() + static_cast<std::ptrdiff_t>(offset + sides * k_fds_side_size)};
    }

    std::unique_ptr<nes_mapper> make_fds(topology::bus& bus, chips::video::ppu2c02& ppu,
                                         std::span<const std::uint8_t> disk,
                                         std::span<const std::uint8_t> bios,
                                         std::span<std::uint8_t> ram,
                                         std::span<std::uint8_t> chr_ram) {
        return std::make_unique<fds_mapper>(bus, ppu, disk, bios, ram, chr_ram);
    }

} // namespace mnemos::manifests::nes
