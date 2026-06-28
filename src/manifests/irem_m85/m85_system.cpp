#include "m85_system.hpp"

#include <memory>
#include <utility>

namespace mnemos::manifests::irem_m85 {

    namespace {

        [[nodiscard]] std::uint8_t layout_code(std::string_view layout) noexcept {
            if (layout == "m85_program_pair") {
                return 1U;
            }
            return 0U;
        }

        [[nodiscard]] std::uint8_t cpu_model_code(chips::cpu::v30::model model) noexcept {
            switch (model) {
            case chips::cpu::v30::model::v33:
                return 2U;
            case chips::cpu::v30::model::v35:
                return 3U;
            case chips::cpu::v30::model::v30:
            default:
                return 1U;
            }
        }

        [[nodiscard]] irem_m81::m81_board_params
        compatible_m81_params(m85_board_params params) noexcept {
            return {.dip_default = params.dip_default,
                    .rom_layout = params.rom_layout == "m85_program_pair" ? "large_boot_pair"
                                                                          : params.rom_layout};
        }

    } // namespace

    m85_board_params board_params_for(std::string_view set_name) noexcept {
        if (set_name == "poundfor" || set_name == "poundforj") {
            return {};
        }
        return {};
    }

    m85_system::m85_system(common::rom_set_image image, m85_board_params board_params)
        : board(std::move(image), compatible_m81_params(board_params)), params(board_params),
          main_cpu(board.main_cpu), sound_cpu(board.sound_cpu), video(board.video), fm(board.fm),
          dac(board.dac), pic(board.pic), work_ram(board.work_ram), sprite_ram(board.sprite_ram),
          palette_ram(board.palette_ram), vram(board.vram), rowscroll_ram(board.rowscroll_ram),
          sound_ram(board.sound_ram), input_system(board.input_system),
          dip_switches(board.dip_switches), dac_write_events(board.dac_write_events) {
        main_cpu.set_model(params.main_cpu_model);
    }

    void m85_system::run_frame() { board.run_frame(); }

    void m85_system::set_inputs(std::uint8_t p1, std::uint8_t p2, std::uint8_t system) noexcept {
        board.set_inputs(p1, p2, system);
    }

    void m85_system::discard_dac_write_events_before(std::uint64_t sound_clock) {
        board.discard_dac_write_events_before(sound_clock);
    }

    void m85_system::save_state(chips::state_writer& writer) const {
        writer.u32(m85_system_state_version);
        writer.u16(params.dip_default);
        writer.u8(layout_code(params.rom_layout));
        writer.u8(cpu_model_code(params.main_cpu_model));
        board.save_state(writer);
    }

    void m85_system::load_state(chips::state_reader& reader) {
        if (reader.u32() != m85_system_state_version) {
            reader.fail();
            return;
        }
        const std::uint16_t saved_dip = reader.u16();
        const std::uint8_t saved_layout = reader.u8();
        const std::uint8_t saved_cpu = reader.u8();
        if (saved_dip != params.dip_default || saved_layout != layout_code(params.rom_layout) ||
            saved_cpu != cpu_model_code(params.main_cpu_model)) {
            reader.fail();
            return;
        }
        board.load_state(reader);
    }

    std::unique_ptr<m85_system> assemble_m85(common::rom_set_image image,
                                             m85_board_params board_params) {
        return std::make_unique<m85_system>(std::move(image), board_params);
    }

} // namespace mnemos::manifests::irem_m85
