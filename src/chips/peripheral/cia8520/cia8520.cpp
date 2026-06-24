#include "cia8520.hpp"

#include "chip_registry.hpp"

#include <memory>
#include <utility>

namespace mnemos::chips::peripheral {
    namespace {

        [[nodiscard]] bus_controller::cia_6526::config make_core_config(cia8520::config cfg) {
            bus_controller::cia_6526::config core{};
            core.read_port_a = std::move(cfg.read_port_a);
            core.read_port_b = std::move(cfg.read_port_b);
            core.write_port_a = std::move(cfg.write_port_a);
            core.write_port_b = std::move(cfg.write_port_b);
            core.write_sp = std::move(cfg.write_sp);
            core.irq_edge = std::move(cfg.irq_edge);
            core.tod_tick_hz = cfg.tod_tick_hz;
            core.tod_src_hz = cfg.tod_src_hz;
            core.rev = bus_controller::cia_6526::revision::nmos_6526;
            return core;
        }

    } // namespace

    chip_metadata cia8520::metadata() const noexcept {
        return {
            .manufacturer = "CSG",
            .part_number = "8520",
            .family = "CIA",
            .klass = chip_class::peripheral,
            .revision = 1U,
        };
    }

    void cia8520::configure(config cfg) {
        core_.configure(make_core_config(std::move(cfg)));
        introspection_.with_registers([this] { return register_snapshot(); });
    }

    void cia8520::reset(reset_kind kind) { core_.reset(kind); }

    void cia8520::tick(std::uint64_t cycles) { core_.tick(cycles); }

    void cia8520::save_state(state_writer& writer) const { core_.save_state(writer); }

    void cia8520::load_state(state_reader& reader) { core_.load_state(reader); }

    instrumentation::ichip_introspection& cia8520::introspection() noexcept {
        return introspection_;
    }

    std::uint8_t cia8520::read(std::uint8_t address) { return core_.read(address); }

    void cia8520::write(std::uint8_t address, std::uint8_t value) { core_.write(address, value); }

    void cia8520::flag_edge() { core_.flag_edge(); }

    void cia8520::cnt_edge(bool new_level) { core_.cnt_edge(new_level); }

    void cia8520::sp_level(bool new_level) { core_.sp_level(new_level); }

    std::span<const register_descriptor> cia8520::register_snapshot() noexcept {
        return core_.register_snapshot();
    }

    namespace {
        [[maybe_unused]] const auto cia8520_registration = register_factory(
            "commodore.cia8520", chip_class::peripheral,
            []() -> std::unique_ptr<ichip> { return std::make_unique<cia8520>(); });
    } // namespace

} // namespace mnemos::chips::peripheral
