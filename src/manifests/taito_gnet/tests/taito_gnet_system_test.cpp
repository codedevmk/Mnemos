#include "taito_gnet_system.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace {

    namespace gnet = mnemos::manifests::taito_gnet;

    [[nodiscard]] constexpr std::uint32_t r(std::uint8_t rs, std::uint8_t rt, std::uint8_t rd,
                                            std::uint8_t sh, std::uint8_t fn) {
        return (static_cast<std::uint32_t>(rs) << 21U) |
               (static_cast<std::uint32_t>(rt) << 16U) |
               (static_cast<std::uint32_t>(rd) << 11U) |
               (static_cast<std::uint32_t>(sh) << 6U) | fn;
    }

    [[nodiscard]] constexpr std::uint32_t i(std::uint8_t op, std::uint8_t rs, std::uint8_t rt,
                                            std::uint16_t imm) {
        return (static_cast<std::uint32_t>(op) << 26U) |
               (static_cast<std::uint32_t>(rs) << 21U) |
               (static_cast<std::uint32_t>(rt) << 16U) | imm;
    }

    [[nodiscard]] constexpr std::uint32_t cop(std::uint8_t opcode, std::uint8_t cop_rs,
                                              std::uint8_t rt, std::uint8_t rd,
                                              std::uint32_t function = 0U) {
        return (static_cast<std::uint32_t>(opcode) << 26U) |
               (static_cast<std::uint32_t>(cop_rs) << 21U) |
               (static_cast<std::uint32_t>(rt) << 16U) |
               (static_cast<std::uint32_t>(rd) << 11U) | (function & 0x07FFU);
    }

    void put32(std::vector<std::uint8_t>& out, std::uint32_t value) {
        out.push_back(static_cast<std::uint8_t>(value));
        out.push_back(static_cast<std::uint8_t>(value >> 8U));
        out.push_back(static_cast<std::uint8_t>(value >> 16U));
        out.push_back(static_cast<std::uint8_t>(value >> 24U));
    }

    void write32_le(mnemos::topology::bus& bus, std::uint32_t address,
                    std::uint32_t value) {
        bus.write16_le(address, static_cast<std::uint16_t>(value));
        bus.write16_le(address + 2U, static_cast<std::uint16_t>(value >> 16U));
    }

    [[nodiscard]] std::uint32_t read32_le(mnemos::topology::bus& bus,
                                          std::uint32_t address) {
        return static_cast<std::uint32_t>(bus.read16_le(address)) |
               (static_cast<std::uint32_t>(bus.read16_le(address + 2U)) << 16U);
    }

    [[nodiscard]] std::vector<std::uint8_t>
    make_bios(const std::vector<std::uint32_t>& code) {
        std::vector<std::uint8_t> bios;
        bios.reserve(code.size() * 4U);
        for (const std::uint32_t op : code) {
            put32(bios, op);
        }
        return bios;
    }

    [[nodiscard]] gnet::gnet_flash_card_image make_card(std::string name,
                                                        std::vector<std::uint8_t> bytes) {
        mnemos::disc::chd::chd_file_info info{};
        info.version = 5U;
        info.header_bytes = 124U;
        info.logical_bytes = bytes.size();
        info.hunk_bytes = 512U;
        info.unit_bytes = 512U;
        info.hunk_count = 1U;
        return gnet::gnet_flash_card_image{
            .name = std::move(name),
            .media = mnemos::disc::chd::chd_block_image_data{.info = info,
                                                             .data = std::move(bytes)}};
    }

    [[nodiscard]] const char* opt_env(const char* name) {
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996) // std::getenv: opt-in test data path
#endif
        const char* value = std::getenv(name);
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
        return (value != nullptr && *value != '\0') ? value : nullptr;
    }

    [[nodiscard]] std::optional<std::vector<std::uint8_t>>
    read_file_bytes(const std::filesystem::path& path) {
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            return std::nullopt;
        }
        const std::vector<char> raw{std::istreambuf_iterator<char>{in},
                                    std::istreambuf_iterator<char>{}};
        return std::vector<std::uint8_t>{raw.begin(), raw.end()};
    }

} // namespace

TEST_CASE("taito_gnet system requires caller-provided BIOS and mounted flash media",
          "[taito_gnet][system]") {
    gnet::taito_gnet_config missing_bios;
    missing_bios.flash_cards.push_back(make_card("card.chd", {0x11U}));
    CHECK(gnet::assemble_taito_gnet(std::move(missing_bios)) == nullptr);

    gnet::taito_gnet_config missing_card;
    missing_card.bios = make_bios({0U});
    CHECK(gnet::assemble_taito_gnet(std::move(missing_card)) == nullptr);
}

TEST_CASE("taito_gnet system fetches R3000A reset code from boot ROM and writes RAM",
          "[taito_gnet][system]") {
    gnet::taito_gnet_config config;
    config.bios = make_bios({
        i(0x09U, 0U, 1U, 0x1234U), // ADDIU AT,R0,1234
        i(0x2BU, 0U, 1U, 0x0000U), // SW AT,0(R0)
        r(0U, 0U, 0U, 0U, 0x00U),  // NOP
    });
    config.flash_cards.push_back(make_card("game.chd", {0xCAU, 0xFEU, 0x01U, 0x02U}));

    const auto sys = gnet::assemble_taito_gnet(std::move(config));
    REQUIRE(sys != nullptr);
    CHECK(sys->flash_card_count() == 1U);
    REQUIRE(sys->flash_card_data(0).size() == 4U);
    CHECK(sys->flash_card_data(0)[0] == 0xCAU);

    sys->step_instructions(2U);
    CHECK(sys->main_ram[0] == 0x34U);
    CHECK(sys->main_ram[1] == 0x12U);
    CHECK(sys->main_ram[2] == 0x00U);
    CHECK(sys->main_ram[3] == 0x00U);
}

TEST_CASE("taito_gnet BIOS-visible COP2/GTE moves latch without reserved traps",
          "[taito_gnet][system]") {
    constexpr std::uint32_t gte_command = 0x4A180001U;

    gnet::taito_gnet_config config;
    config.bios = make_bios({
        i(0x0FU, 0U, 1U, 0x1234U), // LUI AT,1234
        i(0x0DU, 1U, 1U, 0x5678U), // ORI AT,AT,5678
        cop(0x12U, 0x04U, 1U, 4U), // MTC2 AT,D4
        gte_command,                // COP2/GTE command latch
        cop(0x12U, 0x00U, 2U, 4U), // MFC2 V0,D4
        r(2U, 0U, 3U, 0U, 0x21U),  // V1 sees old V0 due to load delay
        r(2U, 0U, 4U, 0U, 0x21U),  // A0 sees loaded V0
        i(0x2BU, 0U, 4U, 0x0024U), // SW A0,24(R0)
        cop(0x12U, 0x06U, 1U, 31U), // CTC2 AT,C31/FLAG
        cop(0x12U, 0x02U, 5U, 31U), // CFC2 A1,C31/FLAG
        r(5U, 0U, 6U, 0U, 0x21U),   // A2 sees old A1 due to load delay
        r(5U, 0U, 7U, 0U, 0x21U),   // A3 sees loaded A1
        i(0x2BU, 0U, 7U, 0x0028U),  // SW A3,28(R0)
    });
    config.flash_cards.push_back(make_card("game.chd", {0x00U}));

    const auto sys = gnet::assemble_taito_gnet(std::move(config));
    REQUIRE(sys != nullptr);
    sys->step_instructions(13U);

    const auto regs = sys->cpu.cpu_registers();
    CHECK(regs.cop2_data[4] == 0x12345678U);
    CHECK(regs.cop2_control[31] == 0x12345678U);
    CHECK(regs.cop2_command == (gte_command & 0x01FFFFFFU));
    CHECK(regs.r[3] == 0U);
    CHECK(regs.r[4] == 0x12345678U);
    CHECK(regs.r[6] == 0U);
    CHECK(regs.r[7] == 0x12345678U);
    CHECK(read32_le(sys->bus, 0x24U) == 0x12345678U);
    CHECK(read32_le(sys->bus, 0x28U) == 0x12345678U);
    CHECK(regs.last_exception !=
          mnemos::chips::cpu::r3000a::exception_code::reserved_instruction);
}

TEST_CASE("taito_gnet PCMCIA aperture exposes mounted flash-card bytes to the R3000A",
          "[taito_gnet][system]") {
    gnet::taito_gnet_config config;
    config.bios = make_bios({
        i(0x0FU, 0U, 1U, 0x1F20U), // LUI AT,1F20 -> PCMCIA window
        i(0x23U, 1U, 2U, 0x0000U), // LW V0,0(AT)
        r(0U, 0U, 0U, 0U, 0x00U),  // load-delay slot
        i(0x2BU, 0U, 2U, 0x0008U), // SW V0,8(R0)
        i(0x09U, 0U, 3U, 0x0055U), // ADDIU V1,R0,55
        i(0x28U, 1U, 3U, 0x0001U), // SB V1,1(AT)
    });
    config.flash_cards.push_back(make_card("game.chd", {0x78U, 0x56U, 0x34U, 0x12U}));

    const auto sys = gnet::assemble_taito_gnet(std::move(config));
    REQUIRE(sys != nullptr);
    sys->step_instructions(6U);

    CHECK(sys->main_ram[8] == 0x78U);
    CHECK(sys->main_ram[9] == 0x56U);
    CHECK(sys->main_ram[10] == 0x34U);
    CHECK(sys->main_ram[11] == 0x12U);
    REQUIRE(sys->flash_cards.front().media.data.size() >= 2U);
    CHECK(sys->flash_cards.front().media.data[1] == 0x55U);
}

TEST_CASE("taito_gnet control register selects the FC-board wave flash bank",
          "[taito_gnet][system]") {
    gnet::taito_gnet_config config;
    config.bios = make_bios({
        i(0x0FU, 0U, 1U, 0x1FB4U), // LUI AT,1FB4 -> control register
        i(0x09U, 0U, 2U, 0x0004U), // ADDIU V0,R0,4
        i(0x28U, 1U, 2U, 0x0000U), // SB V0,0(AT) -> select flash bank 1
        i(0x0FU, 0U, 3U, 0x1F00U), // LUI V1,1F00 -> flash window
        i(0x23U, 3U, 4U, 0x0000U), // LW A0,0(V1)
        r(0U, 0U, 0U, 0U, 0x00U),  // load-delay slot
        i(0x2BU, 0U, 4U, 0x000CU), // SW A0,12(R0)
    });
    config.flash_cards.push_back(make_card("game.chd", {0x00U}));

    const auto sys = gnet::assemble_taito_gnet(std::move(config));
    REQUIRE(sys != nullptr);
    sys->wave_flash[0][0] = 0xEFU;
    sys->wave_flash[0][1] = 0xCDU;
    sys->wave_flash[0][2] = 0xABU;
    sys->wave_flash[0][3] = 0x89U;

    sys->step_instructions(7U);

    CHECK(sys->control == 0x04U);
    CHECK(sys->main_ram[12] == 0xEFU);
    CHECK(sys->main_ram[13] == 0xCDU);
    CHECK(sys->main_ram[14] == 0xABU);
    CHECK(sys->main_ram[15] == 0x89U);
}

TEST_CASE("taito_gnet PCMCIA controller exposes index/data registers",
          "[taito_gnet][system]") {
    gnet::taito_gnet_config config;
    config.bios = make_bios({
        i(0x0FU, 0U, 1U, 0x1FB0U), // LUI AT,1FB0 -> RF5C296 IO window
        i(0x09U, 0U, 2U, 0x0003U), // ADDIU V0,R0,3
        i(0x28U, 1U, 2U, 0x03E0U), // SB V0,index(AT)
        i(0x09U, 0U, 3U, 0x0040U), // ADDIU V1,R0,40
        i(0x28U, 1U, 3U, 0x03E1U), // SB V1,data(AT)
        i(0x24U, 1U, 4U, 0x03E0U), // LBU A0,index(AT)
        r(0U, 0U, 0U, 0U, 0x00U),  // load-delay slot
        i(0x2BU, 0U, 4U, 0x0010U), // SW A0,16(R0)
        i(0x24U, 1U, 5U, 0x03E1U), // LBU A1,data(AT)
        r(0U, 0U, 0U, 0U, 0x00U),  // load-delay slot
        i(0x2BU, 0U, 5U, 0x0014U), // SW A1,20(R0)
        i(0x28U, 1U, 0U, 0x03E1U), // SB R0,data(AT) -> assert card reset
    });
    config.flash_cards.push_back(make_card("game.chd", {0x00U}));

    const auto sys = gnet::assemble_taito_gnet(std::move(config));
    REQUIRE(sys != nullptr);
    sys->step_instructions(12U);

    CHECK(sys->main_ram[16] == 0x03U);
    CHECK(sys->main_ram[20] == 0x40U);
    CHECK(sys->pcmcia_register_index == gnet::taito_gnet_system::pcmcia_interrupt_control_register);
    CHECK(sys->pcmcia_registers[gnet::taito_gnet_system::pcmcia_interrupt_control_register] ==
          0x00U);
    CHECK(sys->pcmcia_reset_asserted);
}

TEST_CASE("taito_gnet PCMCIA IO window proxies mounted card bytes",
          "[taito_gnet][system]") {
    std::vector<std::uint8_t> card(32U, 0x00U);
    card[0x10U] = 0x9AU;

    gnet::taito_gnet_config config;
    config.bios = make_bios({
        i(0x0FU, 0U, 1U, 0x1FB0U), // LUI AT,1FB0 -> RF5C296 IO window
        i(0x24U, 1U, 2U, 0x0010U), // LBU V0,10(AT)
        r(0U, 0U, 0U, 0U, 0x00U),  // load-delay slot
        i(0x2BU, 0U, 2U, 0x0018U), // SW V0,24(R0)
        i(0x09U, 0U, 3U, 0x0077U), // ADDIU V1,R0,77
        i(0x28U, 1U, 3U, 0x0011U), // SB V1,11(AT)
    });
    config.flash_cards.push_back(make_card("game.chd", std::move(card)));

    const auto sys = gnet::assemble_taito_gnet(std::move(config));
    REQUIRE(sys != nullptr);
    sys->step_instructions(6U);

    CHECK(sys->main_ram[24] == 0x9AU);
    REQUIRE(sys->flash_cards.front().media.data.size() > 0x11U);
    CHECK(sys->flash_cards.front().media.data[0x11U] == 0x77U);
}

TEST_CASE("taito_gnet scratchpad and memory-control registers are BIOS-visible",
          "[taito_gnet][system]") {
    gnet::taito_gnet_config config;
    config.bios = make_bios({
        i(0x0FU, 0U, 1U, 0x1F80U), // LUI AT,1F80 -> scratchpad
        i(0x09U, 0U, 2U, 0x1357U), // ADDIU V0,R0,1357
        i(0x2BU, 1U, 2U, 0x0000U), // SW V0,0(AT)
        i(0x23U, 1U, 3U, 0x0000U), // LW V1,0(AT)
        r(0U, 0U, 0U, 0U, 0x00U),  // load-delay slot
        i(0x2BU, 0U, 3U, 0x001CU), // SW V1,28(R0)
    });
    config.flash_cards.push_back(make_card("game.chd", {0x00U}));

    const auto sys = gnet::assemble_taito_gnet(std::move(config));
    REQUIRE(sys != nullptr);
    sys->step_instructions(6U);

    CHECK(sys->scratchpad[0] == 0x57U);
    CHECK(sys->scratchpad[1] == 0x13U);
    CHECK(sys->scratchpad[2] == 0x00U);
    CHECK(sys->scratchpad[3] == 0x00U);
    CHECK(sys->main_ram[28] == 0x57U);
    CHECK(sys->main_ram[29] == 0x13U);

    write32_le(sys->bus, gnet::taito_gnet_system::memory_control_base + 0x00U, 0x1F000000U);
    write32_le(sys->bus, gnet::taito_gnet_system::memory_control_base + 0x08U, 0x0013243FU);
    write32_le(sys->bus, gnet::taito_gnet_system::ram_size_address, 0x00000B88U);
    write32_le(sys->bus, gnet::taito_gnet_system::cache_control_address, 0x00000804U);

    CHECK(read32_le(sys->bus, gnet::taito_gnet_system::memory_control_base + 0x00U) ==
          0x1F000000U);
    CHECK(read32_le(sys->bus, gnet::taito_gnet_system::memory_control_base + 0x08U) ==
          0x0013243FU);
    CHECK(read32_le(sys->bus, gnet::taito_gnet_system::ram_size_address) == 0x00000B88U);
    CHECK(read32_le(sys->bus, gnet::taito_gnet_system::cache_control_address) == 0x00000804U);
}

TEST_CASE("taito_gnet GPU register aperture latches BIOS writes and exposes status",
          "[taito_gnet][system]") {
    gnet::taito_gnet_config config;
    config.bios = make_bios({
        i(0x0FU, 0U, 1U, 0x1F80U), // LUI AT,1F80 -> GPU register page
        i(0x0FU, 0U, 2U, 0xA000U), // LUI V0,A000 -> GP0 command
        i(0x2BU, 1U, 2U, 0x1810U), // SW V0,GP0(AT)
        i(0x0FU, 0U, 3U, 0xE100U), // LUI V1,E100 -> GP0 command
        i(0x2BU, 1U, 3U, 0x1810U), // SW V1,GP0(AT)
        i(0x0FU, 0U, 4U, 0x0300U), // LUI A0,0300 -> GP1 display command
        i(0x2BU, 1U, 4U, 0x1814U), // SW A0,GP1(AT)
        i(0x23U, 1U, 5U, 0x1814U), // LW A1,GPUSTAT(AT)
        r(0U, 0U, 0U, 0U, 0x00U),  // load-delay slot
        i(0x2BU, 0U, 5U, 0x0020U), // SW A1,32(R0)
    });
    config.flash_cards.push_back(make_card("game.chd", {0x00U}));

    const auto sys = gnet::assemble_taito_gnet(std::move(config));
    REQUIRE(sys != nullptr);
    sys->step_instructions(10U);

    CHECK(sys->gpu_gp0 == 0xE1000000U);
    CHECK(sys->gpu_gp1 == 0x03000000U);
    CHECK(sys->gpu_status == gnet::taito_gnet_system::gpu_reset_status);
    REQUIRE(sys->gpu_gp0_fifo_count == 2U);
    CHECK(sys->gpu_gp0_fifo[0] == 0xA0000000U);
    CHECK(sys->gpu_gp0_fifo[1] == 0xE1000000U);
    CHECK(read32_le(sys->bus, gnet::taito_gnet_system::gpu_gp0_address) == 0xE1000000U);
    CHECK(read32_le(sys->bus, gnet::taito_gnet_system::gpu_gp1_address) ==
          gnet::taito_gnet_system::gpu_reset_status);
    CHECK(read32_le(sys->bus, 0x20U) == gnet::taito_gnet_system::gpu_reset_status);

    write32_le(sys->bus, gnet::taito_gnet_system::gpu_gp1_address, 0x00000000U);
    CHECK(sys->gpu_gp0 == 0x00000000U);
    CHECK(sys->gpu_gp0_fifo_count == 0U);
    CHECK(read32_le(sys->bus, gnet::taito_gnet_system::gpu_gp1_address) ==
          gnet::taito_gnet_system::gpu_reset_status);
}

TEST_CASE("taito_gnet interrupt controller masks, acknowledges, and drives R3000A IRQ",
          "[taito_gnet][system]") {
    gnet::taito_gnet_config config;
    config.bios = make_bios({
        r(0U, 0U, 0U, 0U, 0x00U), // NOP; should not execute before the IRQ vector
    });
    config.flash_cards.push_back(make_card("game.chd", {0x00U}));

    const auto sys = gnet::assemble_taito_gnet(std::move(config));
    REQUIRE(sys != nullptr);

    auto regs = sys->cpu.cpu_registers();
    regs.cop0[mnemos::chips::cpu::r3000a::cop0_status] =
        mnemos::chips::cpu::r3000a::status_bev |
        mnemos::chips::cpu::r3000a::status_interrupt_enable |
        mnemos::chips::cpu::r3000a::status_external_irq2_mask;
    sys->cpu.set_registers(regs);

    sys->bus.write16_le(gnet::taito_gnet_system::irq_mask_address, 0x0001U);
    sys->request_interrupt(0x0001U);

    CHECK(sys->bus.read16_le(gnet::taito_gnet_system::irq_status_address) == 0x0001U);
    CHECK(sys->bus.read16_le(gnet::taito_gnet_system::irq_mask_address) == 0x0001U);
    CHECK(sys->cpu.external_interrupt_line());

    sys->step_instructions(1U);
    regs = sys->cpu.cpu_registers();
    CHECK(regs.pc == mnemos::chips::cpu::r3000a::boot_exception_vector);
    CHECK(regs.cop0[mnemos::chips::cpu::r3000a::cop0_epc] ==
          mnemos::chips::cpu::r3000a::reset_vector);
    CHECK(((regs.cop0[mnemos::chips::cpu::r3000a::cop0_cause] &
            mnemos::chips::cpu::r3000a::cause_exception_code_mask) >>
           2U) == static_cast<std::uint32_t>(
                     mnemos::chips::cpu::r3000a::exception_code::interrupt));

    sys->bus.write16_le(gnet::taito_gnet_system::irq_status_address, 0xFFFEU);
    CHECK(sys->bus.read16_le(gnet::taito_gnet_system::irq_status_address) == 0x0000U);
    CHECK_FALSE(sys->cpu.external_interrupt_line());
}

TEST_CASE("taito_gnet exposes BIOS-facing DMA and root-timer register latches",
          "[taito_gnet][system]") {
    gnet::taito_gnet_config config;
    config.bios = make_bios({
        r(0U, 0U, 0U, 0U, 0x00U),
        r(0U, 0U, 0U, 0U, 0x00U),
    });
    config.flash_cards.push_back(make_card("game.chd", {0x00U}));

    const auto sys = gnet::assemble_taito_gnet(std::move(config));
    REQUIRE(sys != nullptr);

    const std::uint32_t channel2 = gnet::taito_gnet_system::dma_register_base + 0x20U;
    write32_le(sys->bus, channel2 + 0x00U, 0x00123450U);
    write32_le(sys->bus, channel2 + 0x04U, 0x00040020U);
    write32_le(sys->bus, channel2 + 0x08U, 0x00000201U);
    write32_le(sys->bus, gnet::taito_gnet_system::dma_register_base + 0x70U, 0x07654321U);
    write32_le(sys->bus, gnet::taito_gnet_system::dma_register_base + 0x74U, 0x00FF0000U);

    CHECK(read32_le(sys->bus, channel2 + 0x00U) == 0x00123450U);
    CHECK(read32_le(sys->bus, channel2 + 0x04U) == 0x00040020U);
    CHECK(read32_le(sys->bus, channel2 + 0x08U) == 0x00000201U);
    CHECK(read32_le(sys->bus, gnet::taito_gnet_system::dma_register_base + 0x70U) ==
          0x07654321U);
    CHECK(read32_le(sys->bus, gnet::taito_gnet_system::dma_register_base + 0x74U) ==
          0x00FF0000U);

    sys->bus.write16_le(gnet::taito_gnet_system::root_timer_base + 0x00U, 0xFFFEU);
    sys->bus.write16_le(gnet::taito_gnet_system::root_timer_base + 0x04U, 0x0123U);
    sys->bus.write16_le(gnet::taito_gnet_system::root_timer_base + 0x08U, 0x1000U);
    sys->step_instructions(2U);

    CHECK(sys->bus.read16_le(gnet::taito_gnet_system::root_timer_base + 0x00U) == 0x0000U);
    CHECK(sys->bus.read16_le(gnet::taito_gnet_system::root_timer_base + 0x04U) == 0x1123U);
    CHECK(sys->bus.read16_le(gnet::taito_gnet_system::root_timer_base + 0x08U) == 0x1000U);
}

TEST_CASE("taito_gnet root timers raise target and overflow IRQs",
          "[taito_gnet][system]") {
    gnet::taito_gnet_config config;
    config.bios = make_bios({
        r(0U, 0U, 0U, 0U, 0x00U),
        r(0U, 0U, 0U, 0U, 0x00U),
        r(0U, 0U, 0U, 0U, 0x00U),
        r(0U, 0U, 0U, 0U, 0x00U),
    });
    config.flash_cards.push_back(make_card("game.chd", {0x00U}));

    const auto sys = gnet::assemble_taito_gnet(std::move(config));
    REQUIRE(sys != nullptr);

    constexpr std::uint16_t timer0_irq = 0x0010U;
    constexpr std::uint16_t timer1_irq = 0x0020U;
    constexpr std::uint16_t target_irq_mode = 0x0010U;
    constexpr std::uint16_t overflow_irq_mode = 0x0020U;
    constexpr std::uint16_t reached_target = 0x0800U;
    constexpr std::uint16_t reached_overflow = 0x1000U;

    sys->bus.write16_le(gnet::taito_gnet_system::irq_mask_address,
                        timer0_irq | timer1_irq);
    sys->bus.write16_le(gnet::taito_gnet_system::root_timer_base + 0x08U, 0x0002U);
    sys->bus.write16_le(gnet::taito_gnet_system::root_timer_base + 0x04U,
                        target_irq_mode);

    sys->step_instructions(2U);

    CHECK(sys->bus.read16_le(gnet::taito_gnet_system::root_timer_base + 0x00U) == 0x0002U);
    CHECK((sys->bus.read16_le(gnet::taito_gnet_system::root_timer_base + 0x04U) &
           reached_target) != 0U);
    CHECK((sys->bus.read16_le(gnet::taito_gnet_system::irq_status_address) & timer0_irq) !=
          0U);
    CHECK(sys->cpu.external_interrupt_line());

    sys->bus.write16_le(gnet::taito_gnet_system::irq_status_address,
                        static_cast<std::uint16_t>(~timer0_irq));
    CHECK_FALSE(sys->cpu.external_interrupt_line());

    const std::uint32_t timer1_base = gnet::taito_gnet_system::root_timer_base + 0x10U;
    sys->bus.write16_le(timer1_base + 0x00U, 0xFFFFU);
    sys->bus.write16_le(timer1_base + 0x04U, overflow_irq_mode);

    sys->step_instructions(1U);

    CHECK(sys->bus.read16_le(timer1_base + 0x00U) == 0x0000U);
    CHECK((sys->bus.read16_le(timer1_base + 0x04U) & reached_overflow) != 0U);
    CHECK((sys->bus.read16_le(gnet::taito_gnet_system::irq_status_address) & timer1_irq) !=
          0U);
    CHECK(sys->cpu.external_interrupt_line());
}

TEST_CASE("taito_gnet executes GPU DMA block and linked-list transfers",
          "[taito_gnet][system][dma]") {
    gnet::taito_gnet_config config;
    config.bios = make_bios({
        r(0U, 0U, 0U, 0U, 0x00U),
        r(0U, 0U, 0U, 0U, 0x00U),
    });
    config.flash_cards.push_back(make_card("game.chd", {0x00U}));

    const auto sys = gnet::assemble_taito_gnet(std::move(config));
    REQUIRE(sys != nullptr);

    constexpr std::uint32_t dma_master_enable = 1U << 23U;
    constexpr std::uint32_t dma_gpu_enable = 1U << 18U;
    constexpr std::uint32_t dma_gpu_flag = 1U << 26U;
    constexpr std::uint32_t dma_master_flag = 1U << 31U;
    const std::uint32_t channel2 = gnet::taito_gnet_system::dma_register_base + 0x20U;

    write32_le(sys->bus, 0x40U, 0xA0000000U);
    write32_le(sys->bus, 0x44U, 0xE1000000U);
    write32_le(sys->bus, gnet::taito_gnet_system::dma_register_base + 0x74U,
               dma_master_enable | dma_gpu_enable);
    sys->bus.write16_le(gnet::taito_gnet_system::irq_mask_address, 0x0008U);
    write32_le(sys->bus, channel2 + 0x00U, 0x00000040U);
    write32_le(sys->bus, channel2 + 0x04U, 0x00000002U);
    write32_le(sys->bus, channel2 + 0x08U, 0x11000001U);

    CHECK(sys->gpu_gp0 == 0xE1000000U);
    REQUIRE(sys->gpu_gp0_fifo_count == 2U);
    CHECK(sys->gpu_gp0_fifo[0] == 0xA0000000U);
    CHECK(sys->gpu_gp0_fifo[1] == 0xE1000000U);
    CHECK((sys->dma_channels[2].control & 0x11000000U) == 0U);
    CHECK((sys->dma_interrupt & dma_gpu_flag) != 0U);
    CHECK((sys->dma_interrupt & dma_master_flag) != 0U);
    CHECK((sys->irq_status & 0x0008U) != 0U);
    CHECK(sys->cpu.external_interrupt_line());

    write32_le(sys->bus, gnet::taito_gnet_system::gpu_gp1_address, 0x00000000U);
    write32_le(sys->bus, 0x100U, 0x02FFFFFFU);
    write32_le(sys->bus, 0x104U, 0xE3000100U);
    write32_le(sys->bus, 0x108U, 0xE4000200U);
    write32_le(sys->bus, channel2 + 0x00U, 0x00000100U);
    write32_le(sys->bus, channel2 + 0x04U, 0x00000000U);
    write32_le(sys->bus, channel2 + 0x08U, 0x11000401U);

    CHECK(sys->gpu_gp0 == 0xE4000200U);
    REQUIRE(sys->gpu_gp0_fifo_count == 2U);
    CHECK(sys->gpu_gp0_fifo[0] == 0xE3000100U);
    CHECK(sys->gpu_gp0_fifo[1] == 0xE4000200U);
    CHECK((sys->dma_channels[2].control & 0x11000000U) == 0U);
}

TEST_CASE("taito_gnet executes OTC DMA and reports DMA completion IRQ",
          "[taito_gnet][system][dma]") {
    gnet::taito_gnet_config config;
    config.bios = make_bios({
        r(0U, 0U, 0U, 0U, 0x00U),
        r(0U, 0U, 0U, 0U, 0x00U),
    });
    config.flash_cards.push_back(make_card("game.chd", {0x00U}));

    const auto sys = gnet::assemble_taito_gnet(std::move(config));
    REQUIRE(sys != nullptr);

    constexpr std::uint32_t dma_master_enable = 1U << 23U;
    constexpr std::uint32_t dma_otc_enable = 1U << 22U;
    constexpr std::uint32_t dma_otc_flag = 1U << 30U;
    constexpr std::uint32_t dma_master_flag = 1U << 31U;
    const std::uint32_t channel6 = gnet::taito_gnet_system::dma_register_base + 0x60U;

    write32_le(sys->bus, gnet::taito_gnet_system::dma_register_base + 0x74U,
               dma_master_enable | dma_otc_enable);
    sys->bus.write16_le(gnet::taito_gnet_system::irq_mask_address, 0x0008U);
    write32_le(sys->bus, channel6 + 0x00U, 0x00000080U);
    write32_le(sys->bus, channel6 + 0x04U, 0x00000003U);
    write32_le(sys->bus, channel6 + 0x08U, 0x11000002U);

    CHECK(read32_le(sys->bus, 0x80U) == 0x0000007CU);
    CHECK(read32_le(sys->bus, 0x7CU) == 0x00000078U);
    CHECK(read32_le(sys->bus, 0x78U) == 0x00FFFFFFU);
    CHECK((sys->dma_channels[6].control & 0x11000000U) == 0U);
    CHECK((sys->dma_interrupt & dma_otc_flag) != 0U);
    CHECK((sys->dma_interrupt & dma_master_flag) != 0U);
    CHECK((sys->irq_status & 0x0008U) != 0U);
    CHECK(sys->cpu.external_interrupt_line());
}

TEST_CASE("taito_gnet real BIOS and CHD package assemble into reset-visible board",
          "[taito_gnet][system][data]") {
    const char* bios_env = opt_env("MNEMOS_TAITO_GNET_BIOS");
    const char* package_env = opt_env("MNEMOS_TAITO_GNET_PACKAGE");
    if (bios_env == nullptr || package_env == nullptr) {
        SKIP("set MNEMOS_TAITO_GNET_BIOS and MNEMOS_TAITO_GNET_PACKAGE for a real G-NET board smoke");
    }

    auto bios = read_file_bytes(bios_env);
    if (!bios) {
        SKIP("MNEMOS_TAITO_GNET_BIOS could not be read");
    }
    auto package = read_file_bytes(package_env);
    if (!package) {
        SKIP("MNEMOS_TAITO_GNET_PACKAGE could not be read");
    }

    const auto sys = gnet::assemble_taito_gnet_from_package(
        std::move(*bios), std::span<const std::uint8_t>{*package});
    REQUIRE(sys != nullptr);
    REQUIRE(sys->bios.size() >= 4U);
    CHECK(sys->flash_card_count() >= 1U);
    CHECK(sys->cpu.cpu_registers().pc == mnemos::chips::cpu::r3000a::reset_vector);

    CHECK(sys->bus.read8(gnet::taito_gnet_system::boot_rom_base) == sys->bios[0]);
    CHECK(sys->bus.read8(gnet::taito_gnet_system::boot_rom_base + 1U) == sys->bios[1]);
    CHECK(sys->bus.read8(gnet::taito_gnet_system::boot_rom_base + 2U) == sys->bios[2]);
    CHECK(sys->bus.read8(gnet::taito_gnet_system::boot_rom_base + 3U) == sys->bios[3]);
    CHECK(sys->read_pcmcia_memory(0U) == sys->flash_cards.front().media.data[0]);
}

TEST_CASE("taito_gnet system save state preserves CPU and main RAM",
          "[taito_gnet][system]") {
    gnet::taito_gnet_config config;
    config.bios = make_bios({
        i(0x09U, 0U, 1U, 0x0042U), // ADDIU AT,R0,0042
        i(0x2BU, 0U, 1U, 0x0004U), // SW AT,4(R0)
        r(0U, 0U, 0U, 0U, 0x00U),  // NOP
    });
    config.flash_cards.push_back(make_card("game.chd", {0xAAU}));

    const auto sys = gnet::assemble_taito_gnet(std::move(config));
    REQUIRE(sys != nullptr);
    sys->step_instructions(2U);
    sys->control = 0x04U;
    sys->firm_flash[0] = 0xA5U;
    sys->flash_cards.front().media.data[0] = 0x5AU;
    sys->pcmcia_register_index = gnet::taito_gnet_system::pcmcia_interrupt_control_register;
    sys->pcmcia_registers[gnet::taito_gnet_system::pcmcia_interrupt_control_register] = 0x40U;
    sys->pcmcia_reset_asserted = false;
    sys->scratchpad[0] = 0x66U;
    sys->memory_control[0] = 0x1F000000U;
    sys->memory_control[2] = 0x0013243FU;
    sys->ram_size = 0x00000B88U;
    sys->cache_control = 0x00000804U;
    sys->gpu_vram[0] = 0x44U;
    sys->gpu_gp0 = 0xE1000000U;
    sys->gpu_gp1 = 0x03000000U;
    sys->gpu_status = 0x14802000U;
    sys->gpu_gp0_fifo_count = 2U;
    sys->gpu_gp0_fifo[0] = 0xA0000000U;
    sys->gpu_gp0_fifo[1] = 0xE1000000U;
    sys->irq_status = 0x0003U;
    sys->irq_mask = 0x0001U;
    sys->refresh_cpu_interrupt_line();
    sys->dma_channels[2].base = 0x00123450U;
    sys->dma_channels[2].block = 0x00040020U;
    sys->dma_channels[2].control = 0x01000201U;
    sys->dma_control = 0x07654321U;
    sys->dma_interrupt = 0x00FF0000U;
    sys->root_timers[1].counter = 0x1234U;
    sys->root_timers[1].mode = 0x0101U;
    sys->root_timers[1].target = 0x2000U;

    std::vector<std::uint8_t> state;
    mnemos::chips::state_writer writer(state);
    sys->save_state(writer);

    sys->main_ram[4] = 0x00U;
    sys->control = 0x00U;
    sys->firm_flash[0] = 0x00U;
    sys->flash_cards.front().media.data[0] = 0x00U;
    sys->pcmcia_register_index = 0x00U;
    sys->pcmcia_registers[gnet::taito_gnet_system::pcmcia_interrupt_control_register] = 0x00U;
    sys->pcmcia_reset_asserted = true;
    sys->scratchpad[0] = 0x00U;
    sys->memory_control[0] = 0x00000000U;
    sys->memory_control[2] = 0x00000000U;
    sys->ram_size = 0x00000000U;
    sys->cache_control = 0x00000000U;
    sys->gpu_vram[0] = 0x00U;
    sys->gpu_gp0 = 0x00000000U;
    sys->gpu_gp1 = 0x00000000U;
    sys->gpu_status = 0x00000000U;
    sys->gpu_gp0_fifo_count = 0U;
    sys->gpu_gp0_fifo[0] = 0x00000000U;
    sys->gpu_gp0_fifo[1] = 0x00000000U;
    sys->irq_status = 0x0000U;
    sys->irq_mask = 0x0000U;
    sys->refresh_cpu_interrupt_line();
    sys->dma_channels[2] = {};
    sys->dma_control = 0x00000000U;
    sys->dma_interrupt = 0x00000000U;
    sys->root_timers[1].counter = 0x0000U;
    sys->root_timers[1].mode = 0x0000U;
    sys->root_timers[1].target = 0x0000U;
    sys->step_instructions(1U);

    mnemos::chips::state_reader reader(std::span<const std::uint8_t>{state});
    sys->load_state(reader);
    REQUIRE(reader.ok());
    CHECK(sys->main_ram[4] == 0x42U);
    CHECK(sys->control == 0x04U);
    CHECK(sys->firm_flash[0] == 0xA5U);
    CHECK(sys->flash_cards.front().media.data[0] == 0x5AU);
    CHECK(sys->pcmcia_register_index == gnet::taito_gnet_system::pcmcia_interrupt_control_register);
    CHECK(sys->pcmcia_registers[gnet::taito_gnet_system::pcmcia_interrupt_control_register] ==
          0x40U);
    CHECK_FALSE(sys->pcmcia_reset_asserted);
    CHECK(sys->scratchpad[0] == 0x66U);
    CHECK(sys->memory_control[0] == 0x1F000000U);
    CHECK(sys->memory_control[2] == 0x0013243FU);
    CHECK(sys->ram_size == 0x00000B88U);
    CHECK(sys->cache_control == 0x00000804U);
    CHECK(sys->gpu_vram[0] == 0x44U);
    CHECK(sys->gpu_gp0 == 0xE1000000U);
    CHECK(sys->gpu_gp1 == 0x03000000U);
    CHECK(sys->gpu_status == 0x14802000U);
    CHECK(sys->gpu_gp0_fifo_count == 2U);
    CHECK(sys->gpu_gp0_fifo[0] == 0xA0000000U);
    CHECK(sys->gpu_gp0_fifo[1] == 0xE1000000U);
    CHECK(sys->irq_status == 0x0003U);
    CHECK(sys->irq_mask == 0x0001U);
    CHECK(sys->cpu.external_interrupt_line());
    CHECK(sys->dma_channels[2].base == 0x00123450U);
    CHECK(sys->dma_channels[2].block == 0x00040020U);
    CHECK(sys->dma_channels[2].control == 0x01000201U);
    CHECK(sys->dma_control == 0x07654321U);
    CHECK(sys->dma_interrupt == 0x00FF0000U);
    CHECK(sys->root_timers[1].counter == 0x1234U);
    CHECK(sys->root_timers[1].mode == 0x0101U);
    CHECK(sys->root_timers[1].target == 0x2000U);
    CHECK(sys->cpu.cpu_registers().pc == mnemos::chips::cpu::r3000a::reset_vector + 8U);
}
