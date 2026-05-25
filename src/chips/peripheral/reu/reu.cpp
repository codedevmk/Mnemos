#include "reu.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <memory>

namespace mnemos::chips::peripheral {
    namespace {
        constexpr std::uint8_t status_irq = 0x80U;
        constexpr std::uint8_t status_end = 0x40U;
        constexpr std::uint8_t status_fault = 0x20U;
        constexpr std::uint8_t command_execute = 0x80U;
        constexpr std::uint8_t command_autoload = 0x20U;
    } // namespace

    std::size_t reu::ram_bytes(model m) noexcept {
        switch (m) {
        case model::ram_128k:
            return 128U * 1024U;
        case model::ram_256k:
            return 256U * 1024U;
        case model::ram_512k:
        default:
            return 512U * 1024U;
        }
    }

    reu::reu(model m) : ram_(ram_bytes(m), 0U) {}

    chip_metadata reu::metadata() const noexcept {
        return {
            .manufacturer = "Commodore",
            .part_number = "1750",
            .family = "REU",
            .klass = chip_class::peripheral,
            .revision = 1U,
        };
    }

    void reu::tick(std::uint64_t /*cycles*/) {}

    void reu::reset(reset_kind /*kind*/) {
        status_ = 0U;
        command_ = 0x10U;
        c64_addr_ = 0U;
        reu_addr_ = 0U;
        length_ = 0xFFFFU;
        irq_mask_ = 0U;
        addr_ctrl_ = 0U;
    }

    void reu::execute(std::uint8_t command) {
        if (bus_ == nullptr || ram_.empty()) {
            return;
        }
        const std::uint8_t type = static_cast<std::uint8_t>(command & 0x03U);
        std::uint16_t c64 = c64_addr_;
        std::uint32_t reu_a = reu_addr_;
        std::uint32_t len = (length_ == 0U) ? 0x10000U : length_;
        const bool fix_c64 = (addr_ctrl_ & 0x80U) != 0U;
        const bool fix_reu = (addr_ctrl_ & 0x40U) != 0U;
        bool fault = false;

        for (std::uint32_t i = 0; i < len; ++i) {
            const std::size_t r = static_cast<std::size_t>(reu_a % ram_.size());
            switch (type) {
            case 0U: // stash: C64 -> REU
                ram_[r] = bus_->read8(c64);
                break;
            case 1U: // fetch: REU -> C64
                bus_->write8(c64, ram_[r]);
                break;
            case 2U: { // swap
                const std::uint8_t from_c64 = bus_->read8(c64);
                bus_->write8(c64, ram_[r]);
                ram_[r] = from_c64;
                break;
            }
            default: // verify
                if (bus_->read8(c64) != ram_[r]) {
                    fault = true;
                }
                break;
            }
            if (fault) {
                break;
            }
            if (!fix_c64) {
                c64 = static_cast<std::uint16_t>(c64 + 1U);
            }
            if (!fix_reu) {
                ++reu_a;
            }
        }

        status_ = static_cast<std::uint8_t>(fault ? status_fault : status_end);
        const bool irq = (irq_mask_ & 0x80U) != 0U &&
                         (((status_ & status_end) != 0U && (irq_mask_ & status_end) != 0U) ||
                          ((status_ & status_fault) != 0U && (irq_mask_ & status_fault) != 0U));
        if (irq) {
            status_ = static_cast<std::uint8_t>(status_ | status_irq);
        }

        // Without autoload the address/length registers advance to the transfer end.
        if ((command & command_autoload) == 0U) {
            c64_addr_ = c64;
            reu_addr_ = reu_a & 0xFFFFFFU;
            length_ = 1U;
        }
    }

    std::uint8_t reu::mmio_read(std::uint16_t offset) {
        switch (offset & 0x1FU) {
        case 0x00U: {
            // Status: report size (>128K sets bit 4); reading clears the latches.
            const std::uint8_t size_bit = ram_.size() > (128U * 1024U) ? 0x10U : 0x00U;
            const std::uint8_t value = static_cast<std::uint8_t>(status_ | size_bit);
            status_ =
                static_cast<std::uint8_t>(status_ & ~(status_irq | status_end | status_fault));
            return value;
        }
        case 0x01U:
            return command_;
        case 0x02U:
            return static_cast<std::uint8_t>(c64_addr_ & 0xFFU);
        case 0x03U:
            return static_cast<std::uint8_t>((c64_addr_ >> 8U) & 0xFFU);
        case 0x04U:
            return static_cast<std::uint8_t>(reu_addr_ & 0xFFU);
        case 0x05U:
            return static_cast<std::uint8_t>((reu_addr_ >> 8U) & 0xFFU);
        case 0x06U:
            return static_cast<std::uint8_t>(((reu_addr_ >> 16U) & 0x07U) |
                                             0xF8U); // unused high = 1
        case 0x07U:
            return static_cast<std::uint8_t>(length_ & 0xFFU);
        case 0x08U:
            return static_cast<std::uint8_t>((length_ >> 8U) & 0xFFU);
        case 0x09U:
            return static_cast<std::uint8_t>(irq_mask_ | 0x1FU);
        case 0x0AU:
            return static_cast<std::uint8_t>(addr_ctrl_ | 0x3FU);
        default:
            return 0xFFU;
        }
    }

    void reu::mmio_write(std::uint16_t offset, std::uint8_t value) {
        switch (offset & 0x1FU) {
        case 0x01U:
            command_ = value;
            if ((value & command_execute) != 0U) {
                execute(value);
            }
            return;
        case 0x02U:
            c64_addr_ = static_cast<std::uint16_t>((c64_addr_ & 0xFF00U) | value);
            return;
        case 0x03U:
            c64_addr_ = static_cast<std::uint16_t>((c64_addr_ & 0x00FFU) | (value << 8U));
            return;
        case 0x04U:
            reu_addr_ = (reu_addr_ & 0xFFFF00U) | value;
            return;
        case 0x05U:
            reu_addr_ = (reu_addr_ & 0xFF00FFU) | (static_cast<std::uint32_t>(value) << 8U);
            return;
        case 0x06U:
            reu_addr_ =
                (reu_addr_ & 0x00FFFFU) | (static_cast<std::uint32_t>(value & 0x07U) << 16U);
            return;
        case 0x07U:
            length_ = static_cast<std::uint16_t>((length_ & 0xFF00U) | value);
            return;
        case 0x08U:
            length_ = static_cast<std::uint16_t>((length_ & 0x00FFU) | (value << 8U));
            return;
        case 0x09U:
            irq_mask_ = value;
            return;
        case 0x0AU:
            addr_ctrl_ = value;
            return;
        default:
            return; // status + unused registers ignore writes
        }
    }

    void reu::save_state(state_writer& writer) const {
        writer.u8(status_);
        writer.u8(command_);
        writer.u16(c64_addr_);
        writer.u32(reu_addr_);
        writer.u16(length_);
        writer.u8(irq_mask_);
        writer.u8(addr_ctrl_);
        writer.blob(ram_);
    }

    void reu::load_state(state_reader& reader) {
        status_ = reader.u8();
        command_ = reader.u8();
        c64_addr_ = reader.u16();
        reu_addr_ = reader.u32();
        length_ = reader.u16();
        irq_mask_ = reader.u8();
        addr_ctrl_ = reader.u8();
        std::vector<std::uint8_t> ram = reader.blob();
        if (ram.size() == ram_.size()) {
            ram_ = std::move(ram);
        }
    }

    instrumentation::ichip_introspection& reu::introspection() noexcept { return introspection_; }

    namespace {
        [[maybe_unused]] const auto reu_registration =
            register_factory("commodore.1750", chip_class::peripheral,
                             []() -> std::unique_ptr<ichip> { return std::make_unique<reu>(); });
    } // namespace

} // namespace mnemos::chips::peripheral
