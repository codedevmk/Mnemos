#include "sh3.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <memory>
#include <string_view>

namespace mnemos::chips::cpu {

    namespace {
        constexpr std::uint32_t sh3_save_state_version = 1U;

        constexpr std::uint32_t sh7708_mmucr = 0xFFFFFF84U;
        constexpr std::uint32_t sh7708_ccr = 0xFFFFFFECU;
        constexpr std::uint32_t sh7708_bcr1 = 0xFFFFFF60U;
        constexpr std::uint32_t sh7708_bcr2 = 0xFFFFFF62U;
        constexpr std::uint32_t sh7708_wcr1 = 0xFFFFFF64U;
        constexpr std::uint32_t sh7708_wcr2 = 0xFFFFFF66U;
        constexpr std::uint32_t sh7708_mcr = 0xFFFFFF68U;

        [[nodiscard]] constexpr std::uint32_t model_code(sh3::model chip_model) noexcept {
            switch (chip_model) {
            case sh3::model::hd6417708s:
                return 0U;
            }
            return 0U;
        }

        [[nodiscard]] constexpr sh3::model model_from_code(std::uint32_t code) noexcept {
            return code == 0U ? sh3::model::hd6417708s : sh3::model::hd6417708s;
        }
    } // namespace

    sh3::sh3(model chip_model) : model_(chip_model) {
        introspection_.with_registers([this] { return register_snapshot(); });
        reset(reset_kind::power_on);
    }

    chip_metadata sh3::metadata() const noexcept {
        return {
            .manufacturer = "Hitachi",
            .part_number = "HD6417708S",
            .family = "SH-3",
            .klass = chip_class::cpu,
            .revision = 1U,
        };
    }

    void sh3::attach_bus(ibus& bus) noexcept { core_.attach_bus(bus); }

    void sh3::tick(std::uint64_t cycles) { core_.tick(cycles); }

    int sh3::step_instruction() { return core_.step_instruction(); }

    void sh3::reset(reset_kind kind) {
        core_.reset(kind);
        reset_local_registers();
    }

    void sh3::reset_local_registers() noexcept {
        mmucr_ = 0U;
        ccr_ = 0U;
        bcr1_ = 0U;
        bcr2_ = 0U;
        wcr1_ = 0U;
        wcr2_ = 0U;
        mcr_ = 0U;
    }

    sh3::registers sh3::cpu_registers() const noexcept {
        return {.core = core_.cpu_registers(),
                .mmucr = mmucr_,
                .ccr = ccr_,
                .bcr1 = bcr1_,
                .bcr2 = bcr2_,
                .wcr1 = wcr1_,
                .wcr2 = wcr2_,
                .mcr = mcr_};
    }

    void sh3::set_registers(const registers& values) noexcept {
        core_.set_registers(values.core);
        mmucr_ = values.mmucr;
        ccr_ = values.ccr;
        bcr1_ = values.bcr1;
        bcr2_ = values.bcr2;
        wcr1_ = values.wcr1;
        wcr2_ = values.wcr2;
        mcr_ = values.mcr;
    }

    std::uint32_t sh3::read_onchip_register(std::uint32_t address) const noexcept {
        switch (address) {
        case sh7708_mmucr:
            return mmucr_;
        case sh7708_ccr:
            return ccr_;
        case sh7708_bcr1:
            return bcr1_;
        case sh7708_bcr2:
            return bcr2_;
        case sh7708_wcr1:
            return wcr1_;
        case sh7708_wcr2:
            return wcr2_;
        case sh7708_mcr:
            return mcr_;
        default:
            return 0xFFFFFFFFU;
        }
    }

    void sh3::write_onchip_register(std::uint32_t address, std::uint32_t value) noexcept {
        switch (address) {
        case sh7708_mmucr:
            mmucr_ = value;
            break;
        case sh7708_ccr:
            ccr_ = value;
            break;
        case sh7708_bcr1:
            bcr1_ = value;
            break;
        case sh7708_bcr2:
            bcr2_ = value;
            break;
        case sh7708_wcr1:
            wcr1_ = value;
            break;
        case sh7708_wcr2:
            wcr2_ = value;
            break;
        case sh7708_mcr:
            mcr_ = value;
            break;
        default:
            break;
        }
    }

    void sh3::save_state(state_writer& writer) const {
        writer.u32(sh3_save_state_version);
        writer.u32(model_code(model_));
        writer.u32(mmucr_);
        writer.u32(ccr_);
        writer.u32(bcr1_);
        writer.u32(bcr2_);
        writer.u32(wcr1_);
        writer.u32(wcr2_);
        writer.u32(mcr_);
        core_.save_state(writer);
    }

    void sh3::load_state(state_reader& reader) {
        const std::uint32_t version = reader.u32();
        if (version != sh3_save_state_version) {
            reader.fail();
            return;
        }
        model_ = model_from_code(reader.u32());
        mmucr_ = reader.u32();
        ccr_ = reader.u32();
        bcr1_ = reader.u32();
        bcr2_ = reader.u32();
        wcr1_ = reader.u32();
        wcr2_ = reader.u32();
        mcr_ = reader.u32();
        core_.load_state(reader);
    }

    instrumentation::ichip_introspection& sh3::introspection() noexcept { return introspection_; }

    std::span<const register_descriptor> sh3::register_snapshot() noexcept {
        using fmt = register_value_format;
        static constexpr std::array<std::string_view, 16> rn = {
            "R0", "R1", "R2",  "R3",  "R4",  "R5",  "R6",  "R7",
            "R8", "R9", "R10", "R11", "R12", "R13", "R14", "R15"};

        const sh2::registers core = core_.cpu_registers();
        for (std::size_t i = 0; i < 16U; ++i) {
            register_view_[i] = {rn[i], core.r[i], 32U, fmt::unsigned_integer};
        }
        register_view_[16] = {"PC", core.pc, 32U, fmt::unsigned_integer};
        register_view_[17] = {"PR", core.pr, 32U, fmt::unsigned_integer};
        register_view_[18] = {"SR", core.sr, 32U, fmt::flags};
        register_view_[19] = {"GBR", core.gbr, 32U, fmt::unsigned_integer};
        register_view_[20] = {"VBR", core.vbr, 32U, fmt::unsigned_integer};
        register_view_[21] = {"MACH", core.mach, 32U, fmt::unsigned_integer};
        register_view_[22] = {"MACL", core.macl, 32U, fmt::unsigned_integer};
        register_view_[23] = {"MMUCR", mmucr_, 32U, fmt::flags};
        register_view_[24] = {"CCR", ccr_, 32U, fmt::flags};
        register_view_[25] = {"BCR1", bcr1_, 32U, fmt::flags};
        register_view_[26] = {"BCR2", bcr2_, 32U, fmt::flags};
        register_view_[27] = {"WCR1", wcr1_, 32U, fmt::flags};
        register_view_[28] = {"WCR2", wcr2_, 32U, fmt::flags};
        register_view_[29] = {"MCR", mcr_, 32U, fmt::flags};
        return register_view_;
    }

    namespace {
        [[maybe_unused]] const auto sh7708_registration = register_factory(
            "hitachi.hd6417708s", chip_class::cpu,
            []() -> std::unique_ptr<ichip> { return std::make_unique<sh3>(); });
        [[maybe_unused]] const auto sh3_registration =
            register_factory("hitachi.sh3", chip_class::cpu,
                             []() -> std::unique_ptr<ichip> {
                                 return std::make_unique<sh3>();
                             });
    } // namespace

} // namespace mnemos::chips::cpu
