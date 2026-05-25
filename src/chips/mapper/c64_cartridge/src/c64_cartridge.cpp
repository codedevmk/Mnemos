#include <mnemos/chips/mapper/c64_cartridge.hpp>

#include <mnemos/chips/common/chip_registry.hpp>
#include <mnemos/chips/common/state.hpp>

#include <algorithm>
#include <array>
#include <cstring>
#include <memory>
#include <string_view>

namespace mnemos::chips::mapper {
    namespace {

        constexpr std::string_view crt_magic = "C64 CARTRIDGE   "; // 16 bytes
        constexpr std::string_view chip_magic = "CHIP";

        std::uint16_t be16(std::span<const std::uint8_t> p, std::size_t off) {
            return static_cast<std::uint16_t>((p[off] << 8U) | p[off + 1U]);
        }
        std::uint32_t be32(std::span<const std::uint8_t> p, std::size_t off) {
            return (static_cast<std::uint32_t>(p[off]) << 24U) |
                   (static_cast<std::uint32_t>(p[off + 1U]) << 16U) |
                   (static_cast<std::uint32_t>(p[off + 2U]) << 8U) |
                   static_cast<std::uint32_t>(p[off + 3U]);
        }

    } // namespace

    chip_metadata c64_cartridge::metadata() const noexcept {
        return {
            .manufacturer = "Commodore",
            .part_number = "cartridge",
            .family = "expansion",
            .klass = chip_class::mapper,
            .revision = 1U,
        };
    }

    void c64_cartridge::tick(std::uint64_t /*cycles*/) {}

    void c64_cartridge::reset(reset_kind /*kind*/) {
        bank_ = 0U;
        enabled_ = true;
        game_ = game_default_;
        exrom_ = exrom_default_;
        ef_ram_.fill(0U);
    }

    void c64_cartridge::eject() noexcept {
        inserted_ = false;
        type_ = hardware::generic;
        bank_ = 0U;
        bank_count_ = 0U;
        roml_.clear();
        romh_.clear();
        game_ = true;
        exrom_ = true;
        game_default_ = true;
        exrom_default_ = true;
        enabled_ = true;
    }

    bool c64_cartridge::load_crt(std::span<const std::uint8_t> crt) {
        if (crt.size() < 0x40U ||
            std::string_view(reinterpret_cast<const char*>(crt.data()), 16U) != crt_magic) {
            return false;
        }
        const std::uint32_t header_len = std::max<std::uint32_t>(be32(crt, 0x10U), 0x40U);
        const std::uint16_t hw = be16(crt, 0x16U);
        const bool exrom_active = crt[0x18U] == 0U; // header byte: 0 = line asserted (low)
        const bool game_active = crt[0x19U] == 0U;

        eject();
        switch (hw) {
        case 5U:
            type_ = hardware::ocean;
            break;
        case 19U:
            type_ = hardware::magic_desk;
            break;
        case 32U:
            type_ = hardware::easyflash;
            break;
        default:
            type_ = hardware::generic;
            break;
        }
        exrom_default_ = !exrom_active;
        game_default_ = !game_active;

        // First pass: parse CHIP packets into temporary banks, tracking the max bank.
        struct chunk final {
            std::uint16_t bank;
            std::uint16_t load;
            std::vector<std::uint8_t> data;
        };
        std::vector<chunk> chunks;
        std::uint16_t max_bank = 0U;
        std::size_t pos = header_len;
        while (pos + 0x10U <= crt.size()) {
            if (std::string_view(reinterpret_cast<const char*>(crt.data() + pos), 4U) !=
                chip_magic) {
                break;
            }
            const std::uint32_t packet_len = be32(crt, pos + 4U);
            const std::uint16_t bank = be16(crt, pos + 0x0AU);
            const std::uint16_t load = be16(crt, pos + 0x0CU);
            const std::uint16_t image = be16(crt, pos + 0x0EU);
            const std::size_t data_off = pos + 0x10U;
            if (packet_len < 0x10U || data_off + image > crt.size()) {
                return false;
            }
            chunks.push_back({bank, load,
                              std::vector<std::uint8_t>(
                                  crt.begin() + static_cast<std::ptrdiff_t>(data_off),
                                  crt.begin() + static_cast<std::ptrdiff_t>(data_off + image))});
            max_bank = std::max(max_bank, bank);
            pos += std::max<std::size_t>(packet_len, 0x10U);
        }
        if (chunks.empty()) {
            return false;
        }

        bank_count_ = static_cast<std::uint16_t>(max_bank + 1U);
        roml_.assign(static_cast<std::size_t>(bank_count_) * bank_size, 0xFFU);
        romh_.assign(static_cast<std::size_t>(bank_count_) * bank_size, 0xFFU);
        bool any_romh = false;
        for (const chunk& c : chunks) {
            const std::size_t base = static_cast<std::size_t>(c.bank) * bank_size;
            if (c.load == 0x8000U) {
                const std::size_t low = std::min<std::size_t>(c.data.size(), bank_size);
                std::copy_n(c.data.begin(), low, roml_.begin() + base);
                if (c.data.size() > bank_size) { // 16K cart: second 8K is ROMH
                    const std::size_t high =
                        std::min<std::size_t>(c.data.size() - bank_size, bank_size);
                    std::copy_n(c.data.begin() + bank_size, high, romh_.begin() + base);
                    any_romh = true;
                }
            } else if (c.load == 0xA000U || c.load == 0xE000U) {
                const std::size_t high = std::min<std::size_t>(c.data.size(), bank_size);
                std::copy_n(c.data.begin(), high, romh_.begin() + base);
                any_romh = true;
            }
        }
        if (!any_romh) {
            romh_.clear();
        }

        inserted_ = true;
        game_ = game_default_;
        exrom_ = exrom_default_;
        return true;
    }

    std::uint8_t c64_cartridge::read_roml(std::uint16_t offset) const noexcept {
        if (!inserted_ || !enabled_) {
            return 0xFFU;
        }
        const std::size_t idx = static_cast<std::size_t>(bank_) * bank_size + (offset & 0x1FFFU);
        return idx < roml_.size() ? roml_[idx] : 0xFFU;
    }

    std::uint8_t c64_cartridge::read_romh(std::uint16_t offset) const noexcept {
        if (!inserted_ || !enabled_) {
            return 0xFFU;
        }
        const std::size_t idx = static_cast<std::size_t>(bank_) * bank_size + (offset & 0x1FFFU);
        return idx < romh_.size() ? romh_[idx] : 0xFFU;
    }

    std::uint8_t c64_cartridge::mmio_read(std::uint16_t offset) {
        if (type_ == hardware::easyflash && offset >= 0x100U) {
            return ef_ram_[offset & 0xFFU]; // I/O-2 = 256-byte RAM
        }
        return 0xFFU;
    }

    void c64_cartridge::mmio_write(std::uint16_t offset, std::uint8_t value) {
        switch (type_) {
        case hardware::ocean:
            bank_ = static_cast<std::uint16_t>(value & 0x3FU); // $DE00 bank select
            break;
        case hardware::magic_desk:
            bank_ = static_cast<std::uint16_t>(value & 0x3FU);
            enabled_ = (value & 0x80U) == 0U; // bit 7 = cartridge disabled
            exrom_ = !enabled_;               // disabled -> /EXROM released (RAM visible)
            break;
        case hardware::easyflash:
            if (offset == 0x00U) {
                bank_ = static_cast<std::uint16_t>(value & 0x3FU); // $DE00 bank
            } else if (offset == 0x02U) {                          // $DE02 control
                exrom_ = (value & 0x02U) == 0U;                    // bit1=1 -> /EXROM low
                game_ = (value & 0x04U) != 0U ? (value & 0x01U) != 0U : false; // bit2=mode
            } else if (offset >= 0x100U) {
                ef_ram_[offset & 0xFFU] = value; // $DF00 RAM
            }
            break;
        case hardware::generic:
        default:
            break;
        }
        if (bank_count_ != 0U) {
            bank_ = static_cast<std::uint16_t>(bank_ % bank_count_);
        }
    }

    void c64_cartridge::save_state(state_writer& writer) const {
        writer.u16(bank_);
        writer.boolean(enabled_);
        writer.boolean(game_);
        writer.boolean(exrom_);
        writer.bytes(std::span<const std::uint8_t>(ef_ram_));
    }

    void c64_cartridge::load_state(state_reader& reader) {
        bank_ = reader.u16();
        enabled_ = reader.boolean();
        game_ = reader.boolean();
        exrom_ = reader.boolean();
        reader.bytes(std::span<std::uint8_t>(ef_ram_));
    }

    instrumentation::i_chip_introspection& c64_cartridge::introspection() noexcept {
        return introspection_;
    }

    namespace {
        [[maybe_unused]] const auto c64_cartridge_registration = register_factory(
            "commodore.cartridge", chip_class::mapper,
            []() -> std::unique_ptr<i_chip> { return std::make_unique<c64_cartridge>(); });
    } // namespace

} // namespace mnemos::chips::mapper
