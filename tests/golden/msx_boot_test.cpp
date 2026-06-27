// Golden-boot tests for MSX and MSX2.
//
// Generated BIOS programs run in every build to prove the CPU/bus/VDP
// framebuffer path without copyrighted artifacts. Real firmware cases render a
// fixed number of frames and hash the framebuffer, but they remain data-gated
// because firmware and cartridges are copyrighted and never committed.
//
// MSX:
//   MNEMOS_MSX_BIOS          path to the 32 KiB MSX BIOS image
//   MNEMOS_MSX_ROM           (optional) cartridge image
//   MNEMOS_MSX_ROM2          (optional) second cartridge image (alias: MNEMOS_MSX_CART2)
//   MNEMOS_MSX_DISK_ROM      (optional) disk interface ROM
//   MNEMOS_MSX_DSK           (optional) flat MSX DSK image
//   MNEMOS_MSX_CAS           (optional) MSX CAS tape image
//   MNEMOS_MSX_KANJI_ROM     (optional) Kanji ROM
//   MNEMOS_MSX_LOGO_ROM      (optional) C-BIOS style logo ROM at slot 0 $8000-$BFFF
//   MNEMOS_MSX_MAPPER        (optional) cartridge mapper override
//   MNEMOS_MSX_MAPPER2       (optional) second cartridge mapper override
//   MNEMOS_MSX_EXPANDED_SLOTS (optional) expanded primary slots; mask or comma list
//   MNEMOS_MSX_RAM_SLOT      (optional) RAM slot as primary or primary.secondary
//   MNEMOS_MSX_RAM_SIZE      (optional) mapper RAM size in bytes, or K/M suffix
//   MNEMOS_MSX_DISK_SLOT     (optional) disk ROM slot as primary or primary.secondary
//   MNEMOS_MSX_CART2_SLOT    (optional) second cartridge slot as primary or primary.secondary
//   MNEMOS_MSX_REGION        (optional) "ntsc" (default) or "pal"
//   MNEMOS_MSX_BOOT_KEYS     (optional) held boot keys: names or row.bit list
//   MNEMOS_MSX_BOOT_FRAMES   (optional) frames before hashing (default 200)
//   MNEMOS_MSX_BOOT_SHA256   (optional) golden framebuffer hash
//
// MSX2:
//   MNEMOS_MSX2_BIOS         path to the MSX2 main BIOS image (or packed main+sub image)
//   MNEMOS_MSX2_FIRMWARE     path to a packed main BIOS + sub-ROM (+ optional disk ROM)
//   MNEMOS_MSX2_SUB_ROM      path to the MSX2 sub-ROM image (alias: MNEMOS_MSX2_SUBROM)
//   MNEMOS_MSX2_LOGO_ROM     (optional) C-BIOS style logo ROM at slot 0 $8000-$BFFF
//   MNEMOS_MSX2_ROM          (optional) cartridge image
//   MNEMOS_MSX2_ROM2         (optional) second cartridge image (alias: MNEMOS_MSX2_CART2)
//   MNEMOS_MSX2_DISK_ROM     (optional) disk interface ROM (alias: MNEMOS_MSX2_DISKROM)
//   MNEMOS_MSX2_DSK          (optional) flat MSX DSK image
//   MNEMOS_MSX2_CAS          (optional) MSX CAS tape image
//   MNEMOS_MSX2_KANJI_ROM    (optional) Kanji ROM
//   MNEMOS_MSX2_MAPPER       (optional) cartridge mapper override
//   MNEMOS_MSX2_MAPPER2      (optional) second cartridge mapper override
//   MNEMOS_MSX2_EXPANDED_SLOTS (optional) expanded primary slots; mask or comma list
//   MNEMOS_MSX2_RAM_SLOT     (optional) RAM slot as primary or primary.secondary
//   MNEMOS_MSX2_SUB_SLOT     (optional) sub-ROM slot as primary or primary.secondary
//   MNEMOS_MSX2_DISK_SLOT    (optional) disk ROM slot as primary or primary.secondary
//   MNEMOS_MSX2_CART2_SLOT   (optional) second cartridge slot as primary or primary.secondary
//   MNEMOS_MSX2_RAM_SIZE     (optional) mapper RAM size in bytes, or K/M suffix
//   MNEMOS_MSX2_REGION       (optional) "ntsc" (default) or "pal"
//   MNEMOS_MSX2_BOOT_KEYS    (optional) held boot keys: names or row.bit list
//   MNEMOS_MSX2_BOOT_FRAMES  (optional) frames before hashing (default 200)
//   MNEMOS_MSX2_BOOT_SHA256  (optional) golden framebuffer hash
//   MNEMOS_MSX_PC_WATCH      (optional) trace high-RAM entry and optional range
//   MNEMOS_MSX_VDP_IO_WATCH  (optional) trace VDP I/O port reads/writes
//   MNEMOS_MSX_MEM_WATCH     (optional) trace memory writes in a range
//   MNEMOS_MSX_MEM_WATCH_PC  (optional) filter memory-write trace by PC range

#include "cli.hpp"
#include "msx_cartridge_mapper.hpp"
#include "msx2_system.hpp"
#include "msx_system.hpp"
#include "scheduler.hpp"
#include "sha256.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
    namespace fs = std::filesystem;
    using mnemos::manifests::msx::assemble_msx;
    using mnemos::manifests::msx::msx_cartridge_mapper;
    using mnemos::manifests::msx::msx_config;
    using mnemos::manifests::msx::msx_system;
    using mnemos::manifests::msx2::assemble_msx2;
    using mnemos::manifests::msx2::msx2_cartridge_mapper;
    using mnemos::manifests::msx2::msx2_config;
    using mnemos::manifests::msx2::msx2_system;

    constexpr std::size_t k_msx_disk_rom_size = 0x4000U;
    constexpr std::size_t k_msx_logo_rom_size = 0x4000U;
    constexpr std::size_t k_msx2_main_bios_size = 0x8000U;
    constexpr std::size_t k_msx2_sub_rom_size = 0x4000U;
    constexpr std::size_t k_msx2_logo_rom_size = 0x4000U;
    constexpr std::size_t k_msx2_disk_rom_size = 0x4000U;
    constexpr std::size_t k_msx2_packed_main_sub_size = k_msx2_main_bios_size + k_msx2_sub_rom_size;
    constexpr std::size_t k_msx2_packed_main_sub_disk_size =
        k_msx2_packed_main_sub_size + k_msx2_disk_rom_size;

    struct msx2_packed_firmware final {
        std::vector<std::uint8_t> main_bios{};
        std::vector<std::uint8_t> sub_rom{};
        std::vector<std::uint8_t> disk_rom{};
    };

    struct slot_location final {
        std::uint8_t primary{};
        std::uint8_t secondary{};
    };

    struct slot_layout_overrides final {
        std::optional<std::uint8_t> expanded_primary_slots{};
        std::optional<slot_location> ram_slot{};
        std::optional<slot_location> sub_bios_slot{};
        std::optional<slot_location> disk_slot{};
        std::optional<slot_location> cartridge2_slot{};
        std::optional<std::size_t> ram_size{};
    };

    struct key_press final {
        std::uint8_t row{};
        std::uint8_t bit{};
    };

    [[nodiscard]] std::string hex8(std::uint8_t value) {
        std::ostringstream out;
        out << '$' << std::uppercase << std::hex << std::setfill('0') << std::setw(2)
            << static_cast<unsigned>(value);
        return out.str();
    }

    [[nodiscard]] std::string hex16(std::uint16_t value) {
        std::ostringstream out;
        out << '$' << std::uppercase << std::hex << std::setfill('0') << std::setw(4) << value;
        return out.str();
    }

    [[nodiscard]] std::size_t nonzero_bytes(std::span<const std::uint8_t> bytes) {
        return static_cast<std::size_t>(
            std::count_if(bytes.begin(), bytes.end(), [](std::uint8_t v) { return v != 0U; }));
    }

    [[nodiscard]] std::string byte_window_summary(std::span<const std::uint8_t> bytes,
                                                  std::size_t offset, std::size_t count) {
        std::ostringstream out;
        out << '[';
        for (std::size_t i = 0U; i < count && offset + i < bytes.size(); ++i) {
            if (i != 0U) {
                out << ',';
            }
            out << hex8(bytes[offset + i]);
        }
        out << ']';
        return out.str();
    }

    [[nodiscard]] std::string memory_nonzero_pages(std::span<const std::uint8_t> bytes,
                                                   std::size_t page_size) {
        std::ostringstream out;
        out << '[';
        for (std::size_t page = 0U; page * page_size < bytes.size(); ++page) {
            if (page != 0U) {
                out << ',';
            }
            const std::size_t begin = page * page_size;
            const std::size_t end = std::min(bytes.size(), begin + page_size);
            out << nonzero_bytes(bytes.subspan(begin, end - begin));
        }
        out << ']';
        return out.str();
    }

    template <typename ReadByte>
    [[nodiscard]] std::string cpu_window_summary(std::uint16_t center, ReadByte read) {
        const auto start = static_cast<std::uint16_t>(center - 8U);
        std::ostringstream out;
        out << '[';
        for (std::uint16_t i = 0U; i < 16U; ++i) {
            if (i != 0U) {
                out << ',';
            }
            const auto address = static_cast<std::uint16_t>(start + i);
            out << hex16(address) << '=' << hex8(read(address));
        }
        out << ']';
        return out.str();
    }

    [[nodiscard]] std::string ram_mapper_segment_summary(const std::array<std::uint8_t, 4>& pages) {
        std::ostringstream out;
        out << '[' << static_cast<unsigned>(pages[0]) << ',' << static_cast<unsigned>(pages[1])
            << ',' << static_cast<unsigned>(pages[2]) << ',' << static_cast<unsigned>(pages[3])
            << ']';
        return out.str();
    }

    [[nodiscard]] std::string msx_title_staging_summary(std::span<const std::uint8_t> ram) {
        std::ostringstream out;
        out << "d000=" << byte_window_summary(ram, 0xD000U, 16U)
            << " d7e0=" << byte_window_summary(ram, 0xD7E0U, 16U)
            << " d800=" << byte_window_summary(ram, 0xD800U, 32U)
            << " d819=" << byte_window_summary(ram, 0xD819U, 8U)
            << " d824=" << byte_window_summary(ram, 0xD824U, 8U)
            << " d9f0=" << byte_window_summary(ram, 0xD9F0U, 16U);
        return out.str();
    }

    template <typename ReadByte>
    [[nodiscard]] std::string msx_logical_state_summary(ReadByte read) {
        auto window = [&](std::uint16_t offset, std::uint8_t count) {
            std::ostringstream out;
            out << '[';
            for (std::uint8_t i = 0U; i < count; ++i) {
                if (i != 0U) {
                    out << ',';
                }
                out << hex8(read(static_cast<std::uint16_t>(offset + i)));
            }
            out << ']';
            return out.str();
        };

        std::ostringstream out;
        out << "d800=" << window(0xD800U, 16U) << " e0b0=" << window(0xE0B0U, 16U)
            << " e0e0=" << window(0xE0E0U, 32U) << " f390=" << window(0xF390U, 32U)
            << " fd90=" << window(0xFD90U, 32U);
        return out.str();
    }

    [[nodiscard]] std::optional<std::string> get_env(const char* name);

    template <typename ReadByte>
    void install_d800_watch(mnemos::chips::cpu::z80& cpu, ReadByte read,
                            std::vector<std::string>& events) {
        if (!get_env("MNEMOS_MSX_D800_WATCH")) {
            return;
        }
        auto* trace = cpu.introspection().trace();
        if (trace == nullptr) {
            return;
        }

        trace->install([read = std::move(read), &events, &cpu, previous_pc = std::uint32_t{},
                        previous_d800 = read(0xD800U),
                        previous_d801 = read(0xD801U)](
                           const mnemos::instrumentation::trace_event& event) mutable {
            const std::uint8_t current_d800 = read(0xD800U);
            const std::uint8_t current_d801 = read(0xD801U);
            const bool cartridge_pc = event.pc >= 0x4000U || previous_pc >= 0x4000U;
            if (cartridge_pc &&
                (current_d800 != previous_d800 || current_d801 != previous_d801) &&
                events.size() < 24U) {
                std::ostringstream out;
                const auto regs = cpu.cpu_registers();
                out << "after_pc=" << hex16(static_cast<std::uint16_t>(previous_pc))
                    << " next_pc=" << hex16(static_cast<std::uint16_t>(event.pc))
                    << " cycles=" << event.cycles << " d800=" << hex8(previous_d800) << "->"
                    << hex8(current_d800) << " d801=" << hex8(previous_d801) << "->"
                    << hex8(current_d801) << " bc=" << hex16(regs.bc)
                    << " de=" << hex16(regs.de) << " hl=" << hex16(regs.hl)
                    << " ix=" << hex16(regs.ix) << " iy=" << hex16(regs.iy)
                    << " sp=" << hex16(regs.sp);
                auto append_window = [&](std::string_view label, std::uint16_t base,
                                         std::uint8_t count) {
                    out << ' ' << label << "=[";
                    for (std::uint8_t i = 0U; i < count; ++i) {
                        if (i != 0U) {
                            out << ',';
                        }
                        out << hex8(read(static_cast<std::uint16_t>(base + i)));
                    }
                    out << ']';
                };
                auto read16 = [&](std::uint16_t base) {
                    return static_cast<std::uint16_t>(
                        read(base) | (static_cast<std::uint16_t>(
                                          read(static_cast<std::uint16_t>(base + 1U)))
                                      << 8U));
                };
                append_window("ix00", regs.ix, 8U);
                append_window("ix18", static_cast<std::uint16_t>(regs.ix + 0x18U), 8U);
                append_window("d851", 0xD851U, 8U);
                out << " d80a=" << hex16(read16(0xD80AU))
                    << " d852=" << hex16(read16(0xD852U))
                    << " d854=" << hex8(read(0xD854U))
                    << " ptr8431=" << hex16(read16(0x8431U))
                    << " ptr8439=" << hex16(read16(0x8439U))
                    << " a035=" << hex8(read(0xA035U)) << ',' << hex8(read(0xA036U));
                events.push_back(out.str());
            }
            previous_pc = event.pc;
            previous_d800 = current_d800;
            previous_d801 = current_d801;
        });
    }

    void append_d800_watch_info(const std::vector<std::string>& events) {
        if (events.empty()) {
            return;
        }
        std::ostringstream out;
        for (std::size_t i = 0U; i < events.size(); ++i) {
            if (i != 0U) {
                out << " | ";
            }
            out << events[i];
        }
        UNSCOPED_INFO("d800 watch: " << out.str());
    }

    [[nodiscard]] bool parse_hex16_watch_token(std::string_view token, std::uint16_t& value) {
        if (token.empty()) {
            return false;
        }
        if (token.size() > 1U && token[0] == '$') {
            token.remove_prefix(1U);
        } else if (token.size() > 2U && token[0] == '0' &&
                   (token[1] == 'x' || token[1] == 'X')) {
            token.remove_prefix(2U);
        }
        if (token.empty()) {
            return false;
        }
        unsigned parsed = 0U;
        for (const char ch : token) {
            unsigned nibble = 0U;
            if (ch >= '0' && ch <= '9') {
                nibble = static_cast<unsigned>(ch - '0');
            } else if (ch >= 'a' && ch <= 'f') {
                nibble = 10U + static_cast<unsigned>(ch - 'a');
            } else if (ch >= 'A' && ch <= 'F') {
                nibble = 10U + static_cast<unsigned>(ch - 'A');
            } else {
                return false;
            }
            parsed = (parsed << 4U) | nibble;
            if (parsed > 0xFFFFU) {
                return false;
            }
        }
        value = static_cast<std::uint16_t>(parsed);
        return true;
    }

    [[nodiscard]] std::pair<std::uint16_t, std::uint16_t>
    pc_watch_range_from_env(std::string_view value) {
        std::uint16_t first = 0xC900U;
        std::uint16_t last_exclusive = 0xC950U;
        const std::size_t separator = value.find_first_of("-:");
        if (separator == std::string_view::npos) {
            return {first, last_exclusive};
        }
        std::uint16_t parsed_first = 0U;
        std::uint16_t parsed_last = 0U;
        if (!parse_hex16_watch_token(value.substr(0U, separator), parsed_first) ||
            !parse_hex16_watch_token(value.substr(separator + 1U), parsed_last) ||
            parsed_last < parsed_first) {
            return {first, last_exclusive};
        }
        first = parsed_first;
        last_exclusive =
            parsed_last == 0xFFFFU ? parsed_last : static_cast<std::uint16_t>(parsed_last + 1U);
        return {first, last_exclusive};
    }

    template <typename ReadByte>
    void install_pc_range_watch(mnemos::chips::cpu::z80& cpu, ReadByte read,
                                std::vector<std::string>& events) {
        const auto watch_value = get_env("MNEMOS_MSX_PC_WATCH");
        if (!watch_value) {
            return;
        }
        auto* trace = cpu.introspection().trace();
        if (trace == nullptr) {
            return;
        }
        const auto [range_first, range_last_exclusive] = pc_watch_range_from_env(*watch_value);

        trace->install([read = std::move(read), &events, &cpu, previous_pc = std::uint32_t{},
                        saw_high_ram = false, range_first, range_last_exclusive](
                           const mnemos::instrumentation::trace_event& event) mutable {
            const auto current_pc = static_cast<std::uint16_t>(event.pc);
            const auto last_pc = static_cast<std::uint16_t>(previous_pc);
            const auto in_watch_range = [range_first, range_last_exclusive](std::uint16_t pc) {
                return pc >= range_first && pc < range_last_exclusive;
            };
            const bool high_ram_entry =
                !saw_high_ram && current_pc >= 0xC000U && last_pc < 0xC000U;
            if (current_pc >= 0xC000U) {
                saw_high_ram = true;
            }
            if ((high_ram_entry || in_watch_range(current_pc) || in_watch_range(last_pc)) &&
                events.size() < 96U) {
                const auto regs = cpu.cpu_registers();
                auto read16 = [&](std::uint16_t base) {
                    return static_cast<std::uint16_t>(
                        read(base) | (static_cast<std::uint16_t>(
                                          read(static_cast<std::uint16_t>(base + 1U)))
                                      << 8U));
                };
                const std::uint16_t ret0 = read16(regs.sp);
                std::ostringstream out;
                out << (high_ram_entry ? "high-ram-entry " : "range ")
                    << "previous_pc=" << hex16(last_pc) << " current_pc=" << hex16(current_pc)
                    << " cycles=" << event.cycles << " af=" << hex16(regs.af)
                    << " bc=" << hex16(regs.bc) << " de=" << hex16(regs.de)
                    << " hl=" << hex16(regs.hl) << " ix=" << hex16(regs.ix)
                    << " iy=" << hex16(regs.iy) << " sp=" << hex16(regs.sp)
                    << " ret0=" << hex16(ret0)
                    << " prev_code=" << cpu_window_summary(last_pc, read)
                    << " code=" << cpu_window_summary(current_pc, read);
                events.push_back(out.str());
            }
            previous_pc = event.pc;
        });
    }

    void append_pc_range_watch_info(const std::vector<std::string>& events) {
        if (events.empty()) {
            return;
        }
        std::ostringstream out;
        for (std::size_t i = 0U; i < events.size(); ++i) {
            if (i != 0U) {
                out << " | ";
            }
            out << events[i];
        }
        UNSCOPED_INFO("pc watch: " << out.str());
    }

    [[nodiscard]] bool memory_watch_address_in_scope(std::uint16_t address) {
        const auto watch_value = get_env("MNEMOS_MSX_MEM_WATCH");
        if (!watch_value) {
            return false;
        }
        if (*watch_value == "1" || *watch_value == "true" || *watch_value == "all") {
            return true;
        }
        const auto [first, last_exclusive] = pc_watch_range_from_env(*watch_value);
        return address >= first && address < last_exclusive;
    }

    [[nodiscard]] bool memory_watch_pc_in_scope(std::uint16_t pc) {
        const auto watch_value = get_env("MNEMOS_MSX_MEM_WATCH_PC");
        if (!watch_value) {
            return true;
        }
        if (*watch_value == "1" || *watch_value == "true" || *watch_value == "all") {
            return true;
        }
        const auto [first, last_exclusive] = pc_watch_range_from_env(*watch_value);
        return pc >= first && pc < last_exclusive;
    }

    template <typename ReadByte, typename StateSummary>
    void append_memory_write_event(std::uint16_t address, std::uint8_t value,
                                   const mnemos::chips::cpu::z80& cpu, ReadByte& read,
                                   StateSummary& state_summary,
                                   std::vector<std::string>& events) {
        const auto regs = cpu.cpu_registers();
        if (events.size() >= 256U || !memory_watch_address_in_scope(address) ||
            !memory_watch_pc_in_scope(regs.pc)) {
            return;
        }

        std::ostringstream out;
        out << "write pc=" << hex16(regs.pc) << " cycles=" << cpu.elapsed_cycles()
            << " address=" << hex16(address) << " value=" << hex8(value)
            << " af=" << hex16(regs.af) << " bc=" << hex16(regs.bc)
            << " de=" << hex16(regs.de) << " hl=" << hex16(regs.hl)
            << " ix=" << hex16(regs.ix) << " iy=" << hex16(regs.iy)
            << " sp=" << hex16(regs.sp) << state_summary()
            << " code=" << cpu_window_summary(regs.pc, read)
            << " window=" << cpu_window_summary(address, read);
        events.push_back(out.str());
    }

    void append_memory_watch_info(const std::vector<std::string>& events) {
        if (events.empty()) {
            return;
        }
        std::ostringstream out;
        for (std::size_t i = 0U; i < events.size(); ++i) {
            if (i != 0U) {
                out << " | ";
            }
            out << events[i];
        }
        UNSCOPED_INFO("memory watch: " << out.str());
    }

    void install_msx_memory_watch(msx_system& sys, std::vector<std::string>& events) {
        if (!get_env("MNEMOS_MSX_MEM_WATCH")) {
            return;
        }
        sys.bus.set_access_observer([&sys, &events](const mnemos::topology::access_event& event) {
            if (!event.write) {
                return;
            }
            auto read = [&sys](std::uint16_t address) { return sys.read_memory(address); };
            auto state_summary = [&sys] {
                std::ostringstream out;
                const std::uint8_t page3_slot =
                    static_cast<std::uint8_t>((sys.primary_slot_select >> 6U) & 0x03U);
                const std::uint8_t page3_subslot =
                    (sys.expanded_primary_slots & (1U << page3_slot)) != 0U
                        ? static_cast<std::uint8_t>(
                              (sys.secondary_slot_select[page3_slot] >> 6U) & 0x03U)
                        : 0U;
                out << " page3_slot=" << static_cast<unsigned>(page3_slot) << '.'
                    << static_cast<unsigned>(page3_subslot)
                    << " primary=" << hex8(sys.primary_slot_select)
                    << " secondary3=" << hex8(sys.secondary_slot_select[3])
                    << " ram_pages=" << ram_mapper_segment_summary(sys.ram_mapper_page);
                return out.str();
            };
            append_memory_write_event(static_cast<std::uint16_t>(event.address), event.value,
                                      sys.cpu, read, state_summary, events);
        });
    }

    void install_msx2_memory_watch(msx2_system& sys, std::vector<std::string>& events) {
        if (!get_env("MNEMOS_MSX_MEM_WATCH")) {
            return;
        }
        sys.bus.set_access_observer([&sys, &events](const mnemos::topology::access_event& event) {
            if (!event.write) {
                return;
            }
            auto read = [&sys](std::uint16_t address) { return sys.cpu_read(address); };
            auto state_summary = [&sys] {
                std::ostringstream out;
                const std::uint8_t page3_slot =
                    static_cast<std::uint8_t>((sys.primary_slot >> 6U) & 0x03U);
                const std::uint8_t page3_subslot =
                    sys.expanded_slot[page3_slot]
                        ? static_cast<std::uint8_t>((sys.secondary_slot[page3_slot] >> 6U) & 0x03U)
                        : 0U;
                out << " page3_slot=" << static_cast<unsigned>(page3_slot) << '.'
                    << static_cast<unsigned>(page3_subslot)
                    << " primary=" << hex8(sys.primary_slot)
                    << " secondary3=" << hex8(sys.secondary_slot[3])
                    << " ram_segments=" << ram_mapper_segment_summary(sys.ram_segment);
                return out.str();
            };
            append_memory_write_event(static_cast<std::uint16_t>(event.address), event.value,
                                      sys.cpu, read, state_summary, events);
        });
    }

    template <typename ReadReg>
    [[nodiscard]] std::array<std::uint8_t, 16> read_vdp_register_window(ReadReg& read_reg) {
        std::array<std::uint8_t, 16> regs{};
        for (std::size_t i = 0U; i < regs.size(); ++i) {
            regs[i] = read_reg(static_cast<int>(i));
        }
        return regs;
    }

    template <typename ReadByte, typename ReadReg>
    void install_vdp_register_watch(mnemos::chips::cpu::z80& cpu, ReadByte read,
                                    ReadReg read_reg, std::vector<std::string>& events) {
        if (!get_env("MNEMOS_MSX_VDP_WATCH")) {
            return;
        }
        auto* trace = cpu.introspection().trace();
        if (trace == nullptr) {
            return;
        }

        std::array<std::uint8_t, 16> initial = read_vdp_register_window(read_reg);
        trace->install([read = std::move(read), read_reg = std::move(read_reg), &events, &cpu,
                        previous = initial, previous_pc = std::uint32_t{}](
                           const mnemos::instrumentation::trace_event& event) mutable {
            std::array<std::uint8_t, 16> current = read_vdp_register_window(read_reg);
            if (current != previous && events.size() < 48U) {
                std::ostringstream out;
                const auto regs = cpu.cpu_registers();
                out << "after_pc=" << hex16(static_cast<std::uint16_t>(previous_pc))
                    << " next_pc=" << hex16(static_cast<std::uint16_t>(event.pc))
                    << " cycles=" << event.cycles << " changes=";
                bool first = true;
                for (std::size_t i = 0U; i < current.size(); ++i) {
                    if (current[i] == previous[i]) {
                        continue;
                    }
                    if (!first) {
                        out << ',';
                    }
                    first = false;
                    out << 'r' << i << ':' << hex8(previous[i]) << "->" << hex8(current[i]);
                }
                auto append_window = [&](std::string_view label, std::uint16_t base,
                                         std::uint8_t count) {
                    out << ' ' << label << "=[";
                    for (std::uint8_t i = 0U; i < count; ++i) {
                        if (i != 0U) {
                            out << ',';
                        }
                        out << hex8(read(static_cast<std::uint16_t>(base + i)));
                    }
                    out << ']';
                };
                auto read16 = [&](std::uint16_t base) {
                    return static_cast<std::uint16_t>(
                        read(base) | (static_cast<std::uint16_t>(
                                          read(static_cast<std::uint16_t>(base + 1U)))
                                      << 8U));
                };
                const std::uint16_t ret0 = read16(regs.sp);
                const std::uint16_t ret1 = read16(static_cast<std::uint16_t>(regs.sp + 2U));
                out << " af=" << hex16(regs.af) << " bc=" << hex16(regs.bc)
                    << " de=" << hex16(regs.de) << " hl=" << hex16(regs.hl)
                    << " ix=" << hex16(regs.ix) << " iy=" << hex16(regs.iy)
                    << " sp=" << hex16(regs.sp) << " ret0=" << hex16(ret0)
                    << " ret1=" << hex16(ret1);
                append_window("stack", regs.sp, 12U);
                append_window("ret0code", ret0, 12U);
                append_window("ret1code", ret1, 12U);
                append_window("rg0sav", 0xF3DFU, 16U);
                append_window("slotvars", 0xFCA0U, 16U);
                events.push_back(out.str());
            }
            previous = current;
            previous_pc = event.pc;
        });
    }

    void append_vdp_register_watch_info(const std::vector<std::string>& events) {
        if (events.empty()) {
            return;
        }
        std::ostringstream out;
        for (std::size_t i = 0U; i < events.size(); ++i) {
            if (i != 0U) {
                out << " | ";
            }
            out << events[i];
        }
        UNSCOPED_INFO("vdp register watch: " << out.str());
    }

    [[nodiscard]] bool watched_vdp_io_port(std::uint16_t port) noexcept {
        switch (static_cast<std::uint8_t>(port & 0xFFU)) {
        case 0x99U:
        case 0x9AU:
        case 0x9BU:
            return true;
        default:
            return false;
        }
    }

    [[nodiscard]] bool vdp_io_watch_pc_in_scope(std::uint16_t pc) {
        const auto watch_value = get_env("MNEMOS_MSX_VDP_IO_WATCH");
        if (!watch_value) {
            return false;
        }
        if (*watch_value == "1" || *watch_value == "true" || *watch_value == "all") {
            return true;
        }
        const auto [first, last_exclusive] = pc_watch_range_from_env(*watch_value);
        return pc >= first && pc < last_exclusive;
    }

    [[nodiscard]] const char*
    v9938_mode_name(mnemos::chips::video::v9938::display_mode mode) noexcept {
        using display_mode = mnemos::chips::video::v9938::display_mode;
        switch (mode) {
        case display_mode::graphics_i:
            return "graphics_i";
        case display_mode::text_i:
            return "text_i";
        case display_mode::text_ii:
            return "text_ii";
        case display_mode::multicolor:
            return "multicolor";
        case display_mode::graphics_ii:
            return "graphics_ii";
        case display_mode::graphics_iii:
            return "graphics_iii";
        case display_mode::graphics_iv:
            return "graphics_iv";
        case display_mode::graphics_v:
            return "graphics_v";
        case display_mode::graphics_vi:
            return "graphics_vi";
        case display_mode::graphics_vii:
            return "graphics_vii";
        }
        return "unknown";
    }

    template <typename ReadByte>
    void append_msx_vdp_io_event(const char* kind, std::uint16_t port, std::uint8_t value,
                                 const mnemos::chips::cpu::z80& cpu, ReadByte& read,
                                 const mnemos::chips::video::tms9918a& vdp,
                                 std::vector<std::string>& events) {
        const auto regs = cpu.cpu_registers();
        if (events.size() >= 256U || !watched_vdp_io_port(port) ||
            !vdp_io_watch_pc_in_scope(regs.pc)) {
            return;
        }
        std::ostringstream out;
        out << kind << " pc=" << hex16(regs.pc) << " cycles=" << cpu.elapsed_cycles()
            << " port=" << hex8(static_cast<std::uint8_t>(port & 0xFFU))
            << " value=" << hex8(value) << " af=" << hex16(regs.af)
            << " bc=" << hex16(regs.bc) << " de=" << hex16(regs.de)
            << " hl=" << hex16(regs.hl) << " ix=" << hex16(regs.ix)
            << " iy=" << hex16(regs.iy) << " sp=" << hex16(regs.sp)
            << " vdp_status=" << hex8(vdp.status()) << " r0=" << hex8(vdp.reg(0))
            << " r1=" << hex8(vdp.reg(1)) << " r2=" << hex8(vdp.reg(2))
            << " r7=" << hex8(vdp.reg(7)) << " e12d=" << hex8(read(0xE12DU))
            << " e3c5=" << hex8(read(0xE3C5U))
            << " code=" << cpu_window_summary(static_cast<std::uint16_t>(regs.pc - 4U), read);
        events.push_back(out.str());
    }

    template <typename ReadByte>
    void append_msx2_vdp_io_event(const char* kind, std::uint16_t port, std::uint8_t value,
                                  std::uint8_t selected_status,
                                  const mnemos::chips::cpu::z80& cpu, ReadByte& read,
                                  const mnemos::chips::video::v9938& vdp,
                                  std::vector<std::string>& events) {
        const auto regs = cpu.cpu_registers();
        if (events.size() >= 256U || !watched_vdp_io_port(port) ||
            !vdp_io_watch_pc_in_scope(regs.pc)) {
            return;
        }
        std::ostringstream out;
        out << kind << " pc=" << hex16(regs.pc) << " cycles=" << cpu.elapsed_cycles()
            << " port=" << hex8(static_cast<std::uint8_t>(port & 0xFFU))
            << " value=" << hex8(value) << " selected_s=" << static_cast<unsigned>(selected_status)
            << " selected_value=" << hex8(vdp.status(selected_status))
            << " s0=" << hex8(vdp.status(0)) << " s1=" << hex8(vdp.status(1))
            << " s2=" << hex8(vdp.status(2)) << " af=" << hex16(regs.af)
            << " bc=" << hex16(regs.bc) << " de=" << hex16(regs.de)
            << " hl=" << hex16(regs.hl) << " ix=" << hex16(regs.ix)
            << " iy=" << hex16(regs.iy) << " sp=" << hex16(regs.sp)
            << " frame=" << vdp.frame_index() << " mode=" << v9938_mode_name(vdp.mode())
            << " r0=" << hex8(vdp.reg(0)) << " r1=" << hex8(vdp.reg(1))
            << " r2=" << hex8(vdp.reg(2)) << " r5=" << hex8(vdp.reg(5))
            << " r6=" << hex8(vdp.reg(6)) << " r7=" << hex8(vdp.reg(7))
            << " r8=" << hex8(vdp.reg(8)) << " r9=" << hex8(vdp.reg(9))
            << " r11=" << hex8(vdp.reg(11)) << " r15=" << hex8(vdp.reg(15))
            << " r23=" << hex8(vdp.reg(23)) << " e12d=" << hex8(read(0xE12DU))
            << " e3c5=" << hex8(read(0xE3C5U))
            << " code=" << cpu_window_summary(static_cast<std::uint16_t>(regs.pc - 4U), read);
        events.push_back(out.str());
    }

    void append_vdp_io_watch_info(const std::vector<std::string>& events) {
        if (events.empty()) {
            return;
        }
        std::ostringstream out;
        for (std::size_t i = 0U; i < events.size(); ++i) {
            if (i != 0U) {
                out << " | ";
            }
            out << events[i];
        }
        UNSCOPED_INFO("vdp io watch: " << out.str());
    }

    void install_msx_vdp_io_watch(msx_system& sys, std::vector<std::string>& events) {
        if (!get_env("MNEMOS_MSX_VDP_IO_WATCH")) {
            return;
        }
        sys.cpu.set_port_in([&sys, &events](std::uint16_t port) {
            const std::uint8_t value = sys.read_io(port);
            if (sys.msx2_video()) {
                auto read = [&sys](std::uint16_t address) { return sys.read_memory(address); };
                const std::uint8_t selected = static_cast<std::uint8_t>(sys.vdp2.reg(15) & 0x0FU);
                append_msx2_vdp_io_event("in", port, value, selected, sys.cpu, read, sys.vdp2,
                                         events);
            } else {
                auto read = [&sys](std::uint16_t address) { return sys.read_memory(address); };
                append_msx_vdp_io_event("in", port, value, sys.cpu, read, sys.vdp, events);
            }
            return value;
        });
        sys.cpu.set_port_out([&sys, &events](std::uint16_t port, std::uint8_t value) {
            if (sys.msx2_video()) {
                const std::uint8_t selected = static_cast<std::uint8_t>(sys.vdp2.reg(15) & 0x0FU);
                sys.write_io(port, value);
                auto read = [&sys](std::uint16_t address) { return sys.read_memory(address); };
                append_msx2_vdp_io_event("out", port, value, selected, sys.cpu, read, sys.vdp2,
                                         events);
            } else {
                sys.write_io(port, value);
                auto read = [&sys](std::uint16_t address) { return sys.read_memory(address); };
                append_msx_vdp_io_event("out", port, value, sys.cpu, read, sys.vdp, events);
            }
        });
    }

    void install_msx2_vdp_io_watch(msx2_system& sys, std::vector<std::string>& events) {
        if (!get_env("MNEMOS_MSX_VDP_IO_WATCH")) {
            return;
        }
        sys.cpu.set_port_in([&sys, &events](std::uint16_t port) {
            const std::uint8_t selected = static_cast<std::uint8_t>(sys.vdp.reg(15) & 0x0FU);
            const std::uint8_t value = sys.io_read(port);
            auto read = [&sys](std::uint16_t address) { return sys.cpu_read(address); };
            append_msx2_vdp_io_event("in", port, value, selected, sys.cpu, read, sys.vdp, events);
            return value;
        });
        sys.cpu.set_port_out([&sys, &events](std::uint16_t port, std::uint8_t value) {
            const std::uint8_t selected = static_cast<std::uint8_t>(sys.vdp.reg(15) & 0x0FU);
            sys.io_write(port, value);
            auto read = [&sys](std::uint16_t address) { return sys.cpu_read(address); };
            append_msx2_vdp_io_event("out", port, value, selected, sys.cpu, read, sys.vdp, events);
        });
    }

    [[nodiscard]] std::string vram_page_nonzero_summary(std::span<const std::uint8_t> vram,
                                                        std::size_t page_size) {
        std::ostringstream out;
        out << '[';
        for (std::size_t page = 0; page * page_size < vram.size(); ++page) {
            if (page != 0U) {
                out << ',';
            }
            const std::size_t begin = page * page_size;
            const std::size_t end = std::min(vram.size(), begin + page_size);
            out << nonzero_bytes(vram.subspan(begin, end - begin));
        }
        out << ']';
        return out.str();
    }

    [[nodiscard]] std::size_t visible_graphics4_nonzero_bytes(std::span<const std::uint8_t> vram,
                                                              std::uint8_t reg2,
                                                              std::uint8_t reg9) {
        constexpr std::size_t k_graphics4_page_size = 0x8000U;
        constexpr std::size_t k_graphics4_stride = 128U;
        const std::size_t page = static_cast<std::size_t>((reg2 >> 5U) & 0x03U);
        const std::size_t base = page * k_graphics4_page_size;
        const std::size_t visible_lines = (reg9 & 0x80U) != 0U ? 212U : 192U;
        const std::size_t visible_bytes = visible_lines * k_graphics4_stride;
        if (base >= vram.size()) {
            return 0U;
        }
        return nonzero_bytes(vram.subspan(base, std::min(visible_bytes, vram.size() - base)));
    }

    [[nodiscard]] std::string visible_graphics4_nibble_histogram(std::span<const std::uint8_t> vram,
                                                                 std::uint8_t reg2,
                                                                 std::uint8_t reg9) {
        constexpr std::size_t k_graphics4_page_size = 0x8000U;
        constexpr std::size_t k_graphics4_stride = 128U;
        std::array<std::size_t, 16> histogram{};
        const std::size_t page = static_cast<std::size_t>((reg2 >> 5U) & 0x03U);
        const std::size_t base = page * k_graphics4_page_size;
        const std::size_t visible_lines = (reg9 & 0x80U) != 0U ? 212U : 192U;
        const std::size_t visible_bytes = visible_lines * k_graphics4_stride;
        if (base < vram.size()) {
            const std::span<const std::uint8_t> visible =
                vram.subspan(base, std::min(visible_bytes, vram.size() - base));
            for (const std::uint8_t packed : visible) {
                ++histogram[static_cast<std::size_t>(packed >> 4U)];
                ++histogram[static_cast<std::size_t>(packed & 0x0FU)];
            }
        }

        std::ostringstream out;
        out << '[';
        for (std::size_t i = 0; i < histogram.size(); ++i) {
            if (i != 0U) {
                out << ',';
            }
            out << histogram[i];
        }
        out << ']';
        return out.str();
    }

    [[nodiscard]] std::string first_visible_graphics4_nonzero_bytes(
        std::span<const std::uint8_t> vram, std::uint8_t reg2, std::uint8_t reg9) {
        constexpr std::size_t k_graphics4_page_size = 0x8000U;
        constexpr std::size_t k_graphics4_stride = 128U;
        const std::size_t page = static_cast<std::size_t>((reg2 >> 5U) & 0x03U);
        const std::size_t base = page * k_graphics4_page_size;
        const std::size_t visible_lines = (reg9 & 0x80U) != 0U ? 212U : 192U;
        const std::size_t visible_bytes = visible_lines * k_graphics4_stride;
        std::ostringstream out;
        out << '[';
        std::size_t emitted = 0U;
        if (base < vram.size()) {
            const std::size_t end = std::min(vram.size(), base + visible_bytes);
            for (std::size_t address = base; address < end && emitted < 8U; ++address) {
                if (vram[address] == 0U) {
                    continue;
                }
                if (emitted != 0U) {
                    out << ',';
                }
                out << hex16(static_cast<std::uint16_t>(address & 0xFFFFU)) << '='
                    << hex8(vram[address]);
                ++emitted;
            }
        }
        out << ']';
        return out.str();
    }

    [[nodiscard]] std::string graphics1_table_sample(std::span<const std::uint8_t> vram,
                                                     std::uint8_t reg2, std::uint8_t reg3,
                                                     std::uint8_t reg4, int line, int col) {
        const int tile_y = line >> 3;
        const int fine_y = line & 7;
        const auto read = [vram](std::uint32_t address) {
            return vram[address & static_cast<std::uint32_t>(vram.size() - 1U)];
        };
        const std::uint32_t nt = static_cast<std::uint32_t>(reg2 & 0x0FU) << 10U;
        const std::uint32_t pt = static_cast<std::uint32_t>(reg4 & 0x07U) << 11U;
        const std::uint32_t ct = static_cast<std::uint32_t>(reg3) << 6U;
        const std::uint8_t pattern =
            read(nt + static_cast<std::uint32_t>(tile_y * 32 + col));
        const std::uint32_t pattern_address =
            pt + static_cast<std::uint32_t>(pattern) * 8U + static_cast<std::uint32_t>(fine_y);
        const std::uint32_t color_address = ct + static_cast<std::uint32_t>(pattern >> 3U);

        std::ostringstream out;
        out << "line=" << line << " col=" << col << " nt=" << hex16(static_cast<std::uint16_t>(nt))
            << " pt=" << hex16(static_cast<std::uint16_t>(pt & 0xFFFFU))
            << " ct=" << hex16(static_cast<std::uint16_t>(ct & 0xFFFFU))
            << " pattern=" << hex8(pattern)
            << " pattern_addr=" << hex16(static_cast<std::uint16_t>(pattern_address & 0xFFFFU))
            << " pattern_bits=" << hex8(read(pattern_address))
            << " color_addr=" << hex16(static_cast<std::uint16_t>(color_address & 0xFFFFU))
            << " color=" << hex8(read(color_address));
        return out.str();
    }

    [[nodiscard]] std::string visible_graphics1_pen_histogram(std::span<const std::uint8_t> vram,
                                                             std::uint8_t reg2, std::uint8_t reg3,
                                                             std::uint8_t reg4) {
        std::array<std::size_t, 16> histogram{};
        const auto read = [vram](std::uint32_t address) {
            return vram[address & static_cast<std::uint32_t>(vram.size() - 1U)];
        };
        const std::uint32_t nt = static_cast<std::uint32_t>(reg2 & 0x0FU) << 10U;
        const std::uint32_t pt = static_cast<std::uint32_t>(reg4 & 0x07U) << 11U;
        const std::uint32_t ct = static_cast<std::uint32_t>(reg3) << 6U;
        for (int line = 0; line < 192; ++line) {
            const int tile_y = line >> 3;
            const int fine_y = line & 7;
            for (int col = 0; col < 32; ++col) {
                const std::uint8_t pattern =
                    read(nt + static_cast<std::uint32_t>(tile_y * 32 + col));
                const std::uint8_t bits =
                    read(pt + static_cast<std::uint32_t>(pattern) * 8U +
                         static_cast<std::uint32_t>(fine_y));
                const std::uint8_t colour =
                    read(ct + static_cast<std::uint32_t>(pattern >> 3U));
                const std::uint8_t fg = static_cast<std::uint8_t>(colour >> 4U);
                const std::uint8_t bg = static_cast<std::uint8_t>(colour & 0x0FU);
                for (int px = 0; px < 8; ++px) {
                    const bool on = (bits & (0x80U >> px)) != 0U;
                    ++histogram[static_cast<std::size_t>(on ? fg : bg)];
                }
            }
        }

        std::ostringstream out;
        out << '[';
        for (std::size_t i = 0; i < histogram.size(); ++i) {
            if (i != 0U) {
                out << ',';
            }
            out << histogram[i];
        }
        out << ']';
        return out.str();
    }

    [[nodiscard]] std::string first_visible_graphics1_non_backdrop_sample(
        std::span<const std::uint8_t> vram, std::uint8_t reg2, std::uint8_t reg3,
        std::uint8_t reg4, std::uint8_t reg7) {
        const auto read = [vram](std::uint32_t address) {
            return vram[address & static_cast<std::uint32_t>(vram.size() - 1U)];
        };
        const std::uint8_t backdrop = static_cast<std::uint8_t>(reg7 & 0x0FU);
        const std::uint32_t nt = static_cast<std::uint32_t>(reg2 & 0x0FU) << 10U;
        const std::uint32_t pt = static_cast<std::uint32_t>(reg4 & 0x07U) << 11U;
        const std::uint32_t ct = static_cast<std::uint32_t>(reg3) << 6U;
        for (int line = 0; line < 192; ++line) {
            const int tile_y = line >> 3;
            const int fine_y = line & 7;
            for (int col = 0; col < 32; ++col) {
                const std::uint8_t pattern =
                    read(nt + static_cast<std::uint32_t>(tile_y * 32 + col));
                const std::uint32_t pattern_address =
                    pt + static_cast<std::uint32_t>(pattern) * 8U +
                    static_cast<std::uint32_t>(fine_y);
                const std::uint32_t color_address =
                    ct + static_cast<std::uint32_t>(pattern >> 3U);
                const std::uint8_t bits = read(pattern_address);
                const std::uint8_t colour = read(color_address);
                const std::uint8_t fg = static_cast<std::uint8_t>(colour >> 4U);
                const std::uint8_t bg = static_cast<std::uint8_t>(colour & 0x0FU);
                for (int px = 0; px < 8; ++px) {
                    const bool on = (bits & (0x80U >> px)) != 0U;
                    const std::uint8_t pen = static_cast<std::uint8_t>(on ? fg : bg);
                    const std::uint8_t resolved = pen == 0U ? backdrop : pen;
                    if (resolved == backdrop) {
                        continue;
                    }
                    std::ostringstream out;
                    out << "line=" << line << " x=" << (col * 8 + px)
                        << " pen=" << hex8(pen)
                        << " resolved=" << hex8(resolved)
                        << " pattern=" << hex8(pattern)
                        << " pattern_addr="
                        << hex16(static_cast<std::uint16_t>(pattern_address & 0xFFFFU))
                        << " pattern_bits=" << hex8(bits)
                        << " color_addr="
                        << hex16(static_cast<std::uint16_t>(color_address & 0xFFFFU))
                        << " color=" << hex8(colour);
                    return out.str();
                }
            }
        }
        return "none";
    }

    [[nodiscard]] std::string graphics2_table_sample(std::span<const std::uint8_t> vram,
                                                     std::uint8_t reg2, std::uint8_t reg3,
                                                     std::uint8_t reg4, std::uint8_t reg10,
                                                     int line, int col) {
        const int tile_y = line >> 3;
        const int fine_y = line & 7;
        const int page = line >> 6;
        const auto read = [vram](std::uint32_t address) {
            return vram[address & static_cast<std::uint32_t>(vram.size() - 1U)];
        };
        const std::uint32_t nt = static_cast<std::uint32_t>(reg2 & 0x7FU) << 10U;
        const std::uint32_t pt = static_cast<std::uint32_t>(reg4 & 0x3CU) << 11U;
        const std::uint32_t ct = (static_cast<std::uint32_t>(reg10 & 0x07U) << 14U) |
                                 (static_cast<std::uint32_t>(reg3 & 0x80U) << 6U);
        const std::uint8_t pattern =
            read(nt + static_cast<std::uint32_t>(tile_y * 32 + col));
        const auto char_index =
            static_cast<std::uint16_t>((static_cast<std::uint16_t>(page) << 8U) | pattern);
        const auto offset =
            static_cast<std::uint16_t>((char_index << 3U) | static_cast<std::uint16_t>(fine_y));
        const auto pattern_mask = static_cast<std::uint16_t>(((reg4 & 0x03U) << 11U) | 0x07FFU);
        const auto color_mask = static_cast<std::uint16_t>(((reg3 & 0x7FU) << 6U) | 0x003FU);
        const std::uint32_t pattern_address = pt | static_cast<std::uint32_t>(offset & pattern_mask);
        const std::uint32_t color_address = ct | static_cast<std::uint32_t>(offset & color_mask);

        std::ostringstream out;
        out << "line=" << line << " col=" << col << " nt=" << hex16(static_cast<std::uint16_t>(nt))
            << " pt=" << hex16(static_cast<std::uint16_t>(pt & 0xFFFFU))
            << " ct=" << hex16(static_cast<std::uint16_t>(ct & 0xFFFFU))
            << " pattern=" << hex8(pattern)
            << " pattern_addr=" << hex16(static_cast<std::uint16_t>(pattern_address & 0xFFFFU))
            << " pattern_bits=" << hex8(read(pattern_address))
            << " color_addr=" << hex16(static_cast<std::uint16_t>(color_address & 0xFFFFU))
            << " color=" << hex8(read(color_address));
        return out.str();
    }

    [[nodiscard]] std::string visible_graphics2_pen_histogram(std::span<const std::uint8_t> vram,
                                                             std::uint8_t reg2, std::uint8_t reg3,
                                                             std::uint8_t reg4,
                                                             std::uint8_t reg10) {
        std::array<std::size_t, 16> histogram{};
        const auto read = [vram](std::uint32_t address) {
            return vram[address & static_cast<std::uint32_t>(vram.size() - 1U)];
        };
        const std::uint32_t nt = static_cast<std::uint32_t>(reg2 & 0x7FU) << 10U;
        const std::uint32_t pt = static_cast<std::uint32_t>(reg4 & 0x3CU) << 11U;
        const std::uint32_t ct = (static_cast<std::uint32_t>(reg10 & 0x07U) << 14U) |
                                 (static_cast<std::uint32_t>(reg3 & 0x80U) << 6U);
        for (int line = 0; line < 192; ++line) {
            const int tile_y = line >> 3;
            const int fine_y = line & 7;
            const int page = line >> 6;
            for (int col = 0; col < 32; ++col) {
                const std::uint8_t pattern =
                    read(nt + static_cast<std::uint32_t>(tile_y * 32 + col));
                const auto char_index =
                    static_cast<std::uint16_t>((static_cast<std::uint16_t>(page) << 8U) |
                                               pattern);
                const auto offset = static_cast<std::uint16_t>(
                    (char_index << 3U) | static_cast<std::uint16_t>(fine_y));
                const auto pattern_mask =
                    static_cast<std::uint16_t>(((reg4 & 0x03U) << 11U) | 0x07FFU);
                const auto color_mask =
                    static_cast<std::uint16_t>(((reg3 & 0x7FU) << 6U) | 0x003FU);
                const std::uint8_t bits =
                    read(pt | static_cast<std::uint32_t>(offset & pattern_mask));
                const std::uint8_t colour =
                    read(ct | static_cast<std::uint32_t>(offset & color_mask));
                const std::uint8_t fg = static_cast<std::uint8_t>(colour >> 4U);
                const std::uint8_t bg = static_cast<std::uint8_t>(colour & 0x0FU);
                for (int px = 0; px < 8; ++px) {
                    const bool on = (bits & (0x80U >> px)) != 0U;
                    ++histogram[static_cast<std::size_t>(on ? fg : bg)];
                }
            }
        }

        std::ostringstream out;
        out << '[';
        for (std::size_t i = 0; i < histogram.size(); ++i) {
            if (i != 0U) {
                out << ',';
            }
            out << histogram[i];
        }
        out << ']';
        return out.str();
    }

    [[nodiscard]] std::string first_visible_graphics2_non_backdrop_sample(
        std::span<const std::uint8_t> vram, std::uint8_t reg2, std::uint8_t reg3,
        std::uint8_t reg4, std::uint8_t reg7, std::uint8_t reg10) {
        const auto read = [vram](std::uint32_t address) {
            return vram[address & static_cast<std::uint32_t>(vram.size() - 1U)];
        };
        const std::uint8_t backdrop = static_cast<std::uint8_t>(reg7 & 0x0FU);
        const std::uint32_t nt = static_cast<std::uint32_t>(reg2 & 0x7FU) << 10U;
        const std::uint32_t pt = static_cast<std::uint32_t>(reg4 & 0x3CU) << 11U;
        const std::uint32_t ct = (static_cast<std::uint32_t>(reg10 & 0x07U) << 14U) |
                                 (static_cast<std::uint32_t>(reg3 & 0x80U) << 6U);
        for (int line = 0; line < 192; ++line) {
            const int tile_y = line >> 3;
            const int fine_y = line & 7;
            const int page = line >> 6;
            for (int col = 0; col < 32; ++col) {
                const std::uint8_t pattern =
                    read(nt + static_cast<std::uint32_t>(tile_y * 32 + col));
                const auto char_index =
                    static_cast<std::uint16_t>((static_cast<std::uint16_t>(page) << 8U) |
                                               pattern);
                const auto offset = static_cast<std::uint16_t>(
                    (char_index << 3U) | static_cast<std::uint16_t>(fine_y));
                const auto pattern_mask =
                    static_cast<std::uint16_t>(((reg4 & 0x03U) << 11U) | 0x07FFU);
                const auto color_mask =
                    static_cast<std::uint16_t>(((reg3 & 0x7FU) << 6U) | 0x003FU);
                const std::uint32_t pattern_address =
                    pt | static_cast<std::uint32_t>(offset & pattern_mask);
                const std::uint32_t color_address =
                    ct | static_cast<std::uint32_t>(offset & color_mask);
                const std::uint8_t bits = read(pattern_address);
                const std::uint8_t colour = read(color_address);
                const std::uint8_t fg = static_cast<std::uint8_t>(colour >> 4U);
                const std::uint8_t bg = static_cast<std::uint8_t>(colour & 0x0FU);
                for (int px = 0; px < 8; ++px) {
                    const bool on = (bits & (0x80U >> px)) != 0U;
                    const std::uint8_t pen = static_cast<std::uint8_t>(on ? fg : bg);
                    const std::uint8_t resolved = pen == 0U ? backdrop : pen;
                    if (resolved == backdrop) {
                        continue;
                    }
                    std::ostringstream out;
                    out << "line=" << line << " x=" << (col * 8 + px)
                        << " pen=" << hex8(pen)
                        << " resolved=" << hex8(resolved)
                        << " pattern=" << hex8(pattern)
                        << " pattern_addr="
                        << hex16(static_cast<std::uint16_t>(pattern_address & 0xFFFFU))
                        << " pattern_bits=" << hex8(bits)
                        << " color_addr="
                        << hex16(static_cast<std::uint16_t>(color_address & 0xFFFFU))
                        << " color=" << hex8(colour);
                    return out.str();
                }
            }
        }
        return "none";
    }

    [[nodiscard]] std::string palette_summary(const mnemos::chips::video::v9938& vdp) {
        std::ostringstream out;
        out << '[';
        for (int i = 0; i < mnemos::chips::video::v9938::palette_count; ++i) {
            if (i != 0) {
                out << ',';
            }
            out << hex16(vdp.palette(i));
        }
        out << ']';
        return out.str();
    }

    std::optional<msx2_packed_firmware>
    split_msx2_packed_firmware(std::span<const std::uint8_t> image) {
        if (image.size() < k_msx2_packed_main_sub_size) {
            return std::nullopt;
        }

        const auto main_begin = image.begin();
        const auto sub_begin = main_begin + static_cast<std::ptrdiff_t>(k_msx2_main_bios_size);
        const auto disk_begin = sub_begin + static_cast<std::ptrdiff_t>(k_msx2_sub_rom_size);

        msx2_packed_firmware split{};
        split.main_bios.assign(main_begin, sub_begin);
        split.sub_rom.assign(sub_begin, disk_begin);
        if (image.size() >= k_msx2_packed_main_sub_disk_size) {
            split.disk_rom.assign(disk_begin,
                                  disk_begin + static_cast<std::ptrdiff_t>(k_msx2_disk_rom_size));
        }
        return split;
    }

    // Portable getenv that does not trip MSVC's deprecation of std::getenv.
    std::optional<std::string> get_env(const char* name) {
#ifdef _WIN32
        char* buf = nullptr;
        std::size_t len = 0;
        if (_dupenv_s(&buf, &len, name) != 0 || buf == nullptr || len == 0U) {
            return std::nullopt;
        }
        std::string value(buf);
        std::free(buf);
        if (value.empty()) {
            return std::nullopt;
        }
        return value;
#else
        const char* value = std::getenv(name);
        if (value == nullptr || value[0] == '\0') {
            return std::nullopt;
        }
        return std::string(value);
#endif
    }

    std::optional<std::string> get_env(const char* primary, const char* fallback) {
        if (auto value = get_env(primary)) {
            return value;
        }
        return get_env(fallback);
    }

    std::optional<std::vector<std::uint8_t>> read_file(const fs::path& path) {
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            return std::nullopt;
        }
        return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(in)),
                                         std::istreambuf_iterator<char>());
    }

    std::vector<std::uint8_t> read_optional_file(const char* env_name) {
        const auto path = get_env(env_name);
        if (!path) {
            return {};
        }
        auto bytes = read_file(fs::path(*path));
        if (!bytes) {
            WARN(env_name << "=" << *path << " could not be read; ignoring optional image");
            return {};
        }
        return *bytes;
    }

    std::vector<std::uint8_t> read_optional_file(const char* primary, const char* fallback) {
        const auto path = get_env(primary, fallback);
        if (!path) {
            return {};
        }
        auto bytes = read_file(fs::path(*path));
        if (!bytes) {
            WARN(primary << "/" << fallback << "=" << *path
                         << " could not be read; ignoring optional image");
            return {};
        }
        return *bytes;
    }

    std::string sha_hex(std::span<const std::uint8_t> bytes) {
        return mnemos::security::cryptography::sha256(bytes).hex();
    }

    mnemos::video_region parse_region(const char* env_name) {
        if (const auto value = get_env(env_name); value && (*value == "pal" || *value == "PAL")) {
            return mnemos::video_region::pal;
        }
        return mnemos::video_region::ntsc;
    }

    std::uint64_t parse_frame_count(const char* env_name, std::uint64_t fallback) {
        const auto value = get_env(env_name);
        if (!value) {
            return fallback;
        }
        const std::uint64_t parsed = std::strtoull(value->c_str(), nullptr, 10);
        return parsed == 0U ? fallback : parsed;
    }

    [[nodiscard]] std::string_view trim_ascii(std::string_view value) noexcept {
        while (!value.empty() &&
               std::isspace(static_cast<unsigned char>(value.front())) != 0) {
            value.remove_prefix(1U);
        }
        while (!value.empty() &&
               std::isspace(static_cast<unsigned char>(value.back())) != 0) {
            value.remove_suffix(1U);
        }
        return value;
    }

    [[nodiscard]] bool parse_unsigned(std::string_view value, unsigned long long maximum,
                                      unsigned long long& parsed) {
        value = trim_ascii(value);
        if (value.empty() || value.front() == '-') {
            return false;
        }

        const std::string text(value);
        char* end = nullptr;
        const unsigned long long candidate = std::strtoull(text.c_str(), &end, 0);
        if (end == text.c_str() || end == nullptr || *end != '\0' || candidate > maximum) {
            return false;
        }

        parsed = candidate;
        return true;
    }

    [[nodiscard]] char key_name_char(char c) noexcept {
        if (c >= 'A' && c <= 'Z') {
            return static_cast<char>(c - 'A' + 'a');
        }
        return (c == '_' || c == ' ') ? '-' : c;
    }

    [[nodiscard]] bool key_name_equals(std::string_view lhs, std::string_view rhs) noexcept {
        lhs = trim_ascii(lhs);
        rhs = trim_ascii(rhs);
        if (lhs.size() != rhs.size()) {
            return false;
        }
        for (std::size_t i = 0; i < lhs.size(); ++i) {
            if (key_name_char(lhs[i]) != key_name_char(rhs[i])) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] std::optional<key_press> parse_boot_key_value(std::string_view value) {
        value = trim_ascii(value);
        if (value.empty()) {
            return std::nullopt;
        }

        struct named_key final {
            std::string_view name;
            key_press key;
        };
        constexpr std::array<named_key, 16> k_named_keys{{
            {"space", {8U, 0U}},
            {"home", {8U, 1U}},
            {"insert", {8U, 2U}},
            {"delete", {8U, 3U}},
            {"left", {8U, 4U}},
            {"up", {8U, 5U}},
            {"down", {8U, 6U}},
            {"right", {8U, 7U}},
            {"select", {6U, 0U}},
            {"shift", {6U, 0U}},
            {"ctrl", {6U, 1U}},
            {"graph", {6U, 2U}},
            {"stop", {7U, 4U}},
            {"escape", {7U, 2U}},
            {"return", {7U, 7U}},
            {"enter", {7U, 7U}},
        }};
        for (const named_key& named : k_named_keys) {
            if (key_name_equals(value, named.name)) {
                return named.key;
            }
        }

        const std::size_t separator = value.find_first_of(".:");
        if (separator == std::string_view::npos) {
            return std::nullopt;
        }

        unsigned long long row = 0U;
        unsigned long long bit = 0U;
        if (!parse_unsigned(value.substr(0U, separator), 15U, row) ||
            !parse_unsigned(value.substr(separator + 1U), 7U, bit)) {
            return std::nullopt;
        }
        return key_press{static_cast<std::uint8_t>(row), static_cast<std::uint8_t>(bit)};
    }

    [[nodiscard]] std::vector<key_press> parse_boot_keys_env(const char* env_name) {
        std::vector<key_press> keys;
        const auto value = get_env(env_name);
        if (!value) {
            return keys;
        }

        std::string_view text(*value);
        std::size_t begin = 0U;
        while (begin <= text.size()) {
            const std::size_t end = text.find_first_of(",;+", begin);
            const std::string_view token =
                text.substr(begin, end == std::string_view::npos ? std::string_view::npos
                                                                 : end - begin);
            if (const auto key = parse_boot_key_value(token)) {
                keys.push_back(*key);
            } else if (!trim_ascii(token).empty()) {
                WARN(env_name << "=" << *value
                              << " contains invalid key token '" << std::string(token)
                              << "'; expected name or row.bit");
                return {};
            }
            if (end == std::string_view::npos) {
                break;
            }
            begin = end + 1U;
        }
        return keys;
    }

    [[nodiscard]] std::string boot_key_summary(std::span<const key_press> keys) {
        if (keys.empty()) {
            return "none";
        }
        std::ostringstream out;
        for (std::size_t i = 0; i < keys.size(); ++i) {
            if (i != 0U) {
                out << ',';
            }
            out << static_cast<unsigned>(keys[i].row) << '.'
                << static_cast<unsigned>(keys[i].bit);
        }
        return out.str();
    }

    [[nodiscard]] std::optional<slot_location> parse_slot_location_value(std::string_view value) {
        value = trim_ascii(value);
        if (value.empty()) {
            return std::nullopt;
        }

        const std::size_t separator = value.find_first_of(".:");
        unsigned long long primary = 0U;
        unsigned long long secondary = 0U;
        if (separator == std::string_view::npos) {
            if (!parse_unsigned(value, 3U, primary)) {
                return std::nullopt;
            }
        } else {
            if (!parse_unsigned(value.substr(0U, separator), 3U, primary) ||
                !parse_unsigned(value.substr(separator + 1U), 3U, secondary)) {
                return std::nullopt;
            }
        }

        return slot_location{static_cast<std::uint8_t>(primary),
                             static_cast<std::uint8_t>(secondary)};
    }

    [[nodiscard]] std::optional<std::uint8_t>
    parse_expanded_primary_slots_value(std::string_view value) {
        value = trim_ascii(value);
        if (value.empty()) {
            return std::nullopt;
        }

        if (value.find_first_of(",;") == std::string_view::npos) {
            unsigned long long mask = 0U;
            if (!parse_unsigned(value, 0x0FU, mask)) {
                return std::nullopt;
            }
            return static_cast<std::uint8_t>(mask);
        }

        std::uint8_t mask = 0U;
        std::size_t begin = 0U;
        while (begin <= value.size()) {
            const std::size_t end = value.find_first_of(",;", begin);
            const std::string_view token =
                value.substr(begin, end == std::string_view::npos ? std::string_view::npos
                                                                  : end - begin);
            unsigned long long slot = 0U;
            if (!parse_unsigned(token, 3U, slot)) {
                return std::nullopt;
            }
            mask = static_cast<std::uint8_t>(mask | (1U << slot));
            if (end == std::string_view::npos) {
                break;
            }
            begin = end + 1U;
        }
        return mask;
    }

    [[nodiscard]] std::optional<std::size_t> parse_ram_size_value(std::string_view value) {
        value = trim_ascii(value);
        if (value.empty()) {
            return std::nullopt;
        }

        unsigned long long multiplier = 1U;
        const char suffix = value.back();
        if (suffix == 'k' || suffix == 'K') {
            multiplier = 1024U;
            value.remove_suffix(1U);
        } else if (suffix == 'm' || suffix == 'M') {
            multiplier = 1024U * 1024U;
            value.remove_suffix(1U);
        }

        constexpr unsigned long long k_min_ram_size = 0x4000ULL;
        constexpr unsigned long long k_max_ram_size = 0x400000ULL;
        unsigned long long units = 0U;
        if (!parse_unsigned(value, k_max_ram_size / multiplier, units)) {
            return std::nullopt;
        }

        const unsigned long long bytes = units * multiplier;
        if (bytes < k_min_ram_size || bytes > k_max_ram_size) {
            return std::nullopt;
        }
        return static_cast<std::size_t>(bytes);
    }

    [[nodiscard]] std::optional<slot_location> parse_slot_location_env(const char* env_name) {
        const auto value = get_env(env_name);
        if (!value) {
            return std::nullopt;
        }
        const auto slot = parse_slot_location_value(*value);
        if (!slot) {
            WARN(env_name << "=" << *value
                          << " is not a slot value; expected primary or primary.secondary");
        }
        return slot;
    }

    [[nodiscard]] std::optional<std::uint8_t> parse_expanded_primary_slots_env(
        const char* env_name) {
        const auto value = get_env(env_name);
        if (!value) {
            return std::nullopt;
        }
        const auto slots = parse_expanded_primary_slots_value(*value);
        if (!slots) {
            WARN(env_name << "=" << *value
                          << " is not an expanded-slot mask or comma-separated slot list");
        }
        return slots;
    }

    [[nodiscard]] std::optional<std::size_t> parse_ram_size_env(const char* env_name) {
        const auto value = get_env(env_name);
        if (!value) {
            return std::nullopt;
        }
        const auto size = parse_ram_size_value(*value);
        if (!size) {
            WARN(env_name << "=" << *value
                          << " is not a supported MSX2 RAM size; use bytes, K, or M");
        }
        return size;
    }

    [[nodiscard]] std::size_t ram_mapper_segments_for_size(std::size_t bytes) noexcept {
        constexpr std::size_t k_segment_size = 0x4000U;
        constexpr std::size_t k_min_segments = 4U;
        constexpr std::size_t k_max_segments = 0x100U;
        const std::size_t requested =
            (bytes + k_segment_size - 1U) / k_segment_size;
        return std::min<std::size_t>(std::max(requested, k_min_segments), k_max_segments);
    }

    [[nodiscard]] slot_layout_overrides parse_slot_layout_env(std::string_view prefix,
                                                              bool include_ram_size) {
        const std::string prefix_text(prefix);
        slot_layout_overrides layout{};

        const std::string expanded_name = prefix_text + "EXPANDED_SLOTS";
        const std::string ram_slot_name = prefix_text + "RAM_SLOT";
        const std::string sub_slot_name = prefix_text + "SUB_SLOT";
        const std::string disk_slot_name = prefix_text + "DISK_SLOT";
        const std::string cartridge2_slot_name = prefix_text + "CART2_SLOT";
        layout.expanded_primary_slots = parse_expanded_primary_slots_env(expanded_name.c_str());
        layout.ram_slot = parse_slot_location_env(ram_slot_name.c_str());
        layout.sub_bios_slot = parse_slot_location_env(sub_slot_name.c_str());
        layout.disk_slot = parse_slot_location_env(disk_slot_name.c_str());
        layout.cartridge2_slot = parse_slot_location_env(cartridge2_slot_name.c_str());

        if (include_ram_size) {
            const std::string ram_size_name = prefix_text + "RAM_SIZE";
            layout.ram_size = parse_ram_size_env(ram_size_name.c_str());
        }
        return layout;
    }

    void apply_slot_layout(msx_config& config, const slot_layout_overrides& layout) {
        if (layout.expanded_primary_slots) {
            config.expanded_primary_slots = *layout.expanded_primary_slots;
        }
        if (layout.ram_slot) {
            config.ram_primary_slot = layout.ram_slot->primary;
            config.ram_secondary_slot = layout.ram_slot->secondary;
        }
        if (layout.disk_slot) {
            config.disk_primary_slot = layout.disk_slot->primary;
            config.disk_secondary_slot = layout.disk_slot->secondary;
        }
        if (layout.cartridge2_slot) {
            config.cartridge2_primary_slot = layout.cartridge2_slot->primary;
            config.cartridge2_secondary_slot = layout.cartridge2_slot->secondary;
        }
        if (layout.ram_size) {
            config.ram_mapper_segments = ram_mapper_segments_for_size(*layout.ram_size);
        }
    }

    void apply_slot_layout(msx2_config& config, const slot_layout_overrides& layout) {
        if (layout.expanded_primary_slots) {
            config.expanded_primary_slots = *layout.expanded_primary_slots;
        }
        if (layout.ram_slot) {
            config.ram_primary_slot = layout.ram_slot->primary;
            config.ram_secondary_slot = layout.ram_slot->secondary;
        }
        if (layout.sub_bios_slot) {
            config.sub_bios_primary_slot = layout.sub_bios_slot->primary;
            config.sub_bios_secondary_slot = layout.sub_bios_slot->secondary;
        }
        if (layout.disk_slot) {
            config.disk_primary_slot = layout.disk_slot->primary;
            config.disk_secondary_slot = layout.disk_slot->secondary;
        }
        if (layout.cartridge2_slot) {
            config.cartridge2_primary_slot = layout.cartridge2_slot->primary;
            config.cartridge2_secondary_slot = layout.cartridge2_slot->secondary;
        }
        if (layout.ram_size) {
            config.ram_size = *layout.ram_size;
        }
    }

    [[nodiscard]] std::string slot_label(const std::optional<slot_location>& slot) {
        if (!slot) {
            return "default";
        }
        return std::to_string(slot->primary) + "." + std::to_string(slot->secondary);
    }

    [[nodiscard]] std::string slot_layout_label(const slot_layout_overrides& layout) {
        std::string label = "expanded=" +
                            (layout.expanded_primary_slots
                                 ? std::to_string(*layout.expanded_primary_slots)
                                 : std::string{"default"});
        label += " ram=" + slot_label(layout.ram_slot);
        label += " sub=" + slot_label(layout.sub_bios_slot);
        label += " disk=" + slot_label(layout.disk_slot);
        label += " cart2=" + slot_label(layout.cartridge2_slot);
        if (layout.ram_size) {
            label += " ram_size=" + std::to_string(*layout.ram_size);
        }
        return label;
    }

    [[nodiscard]] msx_cartridge_mapper
    msx_mapper_from_kind(mnemos::manifests::common::msx_cartridge_mapper_kind mapper) noexcept {
        using mnemos::manifests::common::msx_cartridge_mapper_kind;
        switch (mapper) {
        case msx_cartridge_mapper_kind::plain:
            return msx_cartridge_mapper::plain;
        case msx_cartridge_mapper_kind::ascii8:
            return msx_cartridge_mapper::ascii8;
        case msx_cartridge_mapper_kind::ascii8_sram8:
            return msx_cartridge_mapper::ascii8_sram8;
        case msx_cartridge_mapper_kind::ascii16:
            return msx_cartridge_mapper::ascii16;
        case msx_cartridge_mapper_kind::ascii16_sram2:
            return msx_cartridge_mapper::ascii16_sram2;
        case msx_cartridge_mapper_kind::generic8:
            return msx_cartridge_mapper::generic8;
        case msx_cartridge_mapper_kind::konami:
            return msx_cartridge_mapper::konami;
        case msx_cartridge_mapper_kind::konami_scc:
            return msx_cartridge_mapper::konami_scc;
        case msx_cartridge_mapper_kind::korean_msx:
            return msx_cartridge_mapper::korean_msx;
        case msx_cartridge_mapper_kind::korean_msx_nemesis:
            return msx_cartridge_mapper::korean_msx_nemesis;
        case msx_cartridge_mapper_kind::automatic:
        default:
            return msx_cartridge_mapper::automatic;
        }
    }

    [[nodiscard]] msx2_cartridge_mapper
    msx2_mapper_from_kind(mnemos::manifests::common::msx_cartridge_mapper_kind mapper) noexcept {
        using mnemos::manifests::common::msx_cartridge_mapper_kind;
        switch (mapper) {
        case msx_cartridge_mapper_kind::plain:
            return msx2_cartridge_mapper::plain;
        case msx_cartridge_mapper_kind::ascii8:
            return msx2_cartridge_mapper::ascii8;
        case msx_cartridge_mapper_kind::ascii8_sram8:
            return msx2_cartridge_mapper::ascii8_sram8;
        case msx_cartridge_mapper_kind::ascii16:
            return msx2_cartridge_mapper::ascii16;
        case msx_cartridge_mapper_kind::ascii16_sram2:
            return msx2_cartridge_mapper::ascii16_sram2;
        case msx_cartridge_mapper_kind::generic8:
            return msx2_cartridge_mapper::generic8;
        case msx_cartridge_mapper_kind::konami:
            return msx2_cartridge_mapper::konami;
        case msx_cartridge_mapper_kind::konami_scc:
            return msx2_cartridge_mapper::konami_scc;
        case msx_cartridge_mapper_kind::korean_msx:
            return msx2_cartridge_mapper::korean_msx;
        case msx_cartridge_mapper_kind::korean_msx_nemesis:
            return msx2_cartridge_mapper::korean_msx_nemesis;
        case msx_cartridge_mapper_kind::automatic:
        default:
            return msx2_cartridge_mapper::automatic;
        }
    }

    [[nodiscard]] msx_cartridge_mapper msx_mapper_from_name(std::string_view name) noexcept {
        return msx_mapper_from_kind(
            mnemos::manifests::common::parse_msx_cartridge_mapper_name(name));
    }

    [[nodiscard]] msx2_cartridge_mapper msx2_mapper_from_name(std::string_view name) noexcept {
        return msx2_mapper_from_kind(
            mnemos::manifests::common::parse_msx_cartridge_mapper_name(name));
    }

    [[nodiscard]] msx_cartridge_mapper msx_mapper_from_env(const char* env_name) {
        const auto value = get_env(env_name);
        return value ? msx_mapper_from_name(*value) : msx_cartridge_mapper::automatic;
    }

    [[nodiscard]] msx2_cartridge_mapper msx2_mapper_from_env(const char* env_name) {
        const auto value = get_env(env_name);
        return value ? msx2_mapper_from_name(*value) : msx2_cartridge_mapper::automatic;
    }

    [[nodiscard]] mnemos::manifests::common::msx_cartridge_mapper_kind
    mapper_kind(msx_cartridge_mapper mapper) noexcept {
        using mnemos::manifests::common::msx_cartridge_mapper_kind;
        switch (mapper) {
        case msx_cartridge_mapper::plain:
            return msx_cartridge_mapper_kind::plain;
        case msx_cartridge_mapper::ascii8:
            return msx_cartridge_mapper_kind::ascii8;
        case msx_cartridge_mapper::ascii8_sram8:
            return msx_cartridge_mapper_kind::ascii8_sram8;
        case msx_cartridge_mapper::ascii16:
            return msx_cartridge_mapper_kind::ascii16;
        case msx_cartridge_mapper::ascii16_sram2:
            return msx_cartridge_mapper_kind::ascii16_sram2;
        case msx_cartridge_mapper::generic8:
            return msx_cartridge_mapper_kind::generic8;
        case msx_cartridge_mapper::konami:
            return msx_cartridge_mapper_kind::konami;
        case msx_cartridge_mapper::konami_scc:
            return msx_cartridge_mapper_kind::konami_scc;
        case msx_cartridge_mapper::korean_msx:
            return msx_cartridge_mapper_kind::korean_msx;
        case msx_cartridge_mapper::korean_msx_nemesis:
            return msx_cartridge_mapper_kind::korean_msx_nemesis;
        case msx_cartridge_mapper::automatic:
        default:
            return msx_cartridge_mapper_kind::automatic;
        }
    }

    [[nodiscard]] mnemos::manifests::common::msx_cartridge_mapper_kind
    mapper_kind(msx2_cartridge_mapper mapper) noexcept {
        using mnemos::manifests::common::msx_cartridge_mapper_kind;
        switch (mapper) {
        case msx2_cartridge_mapper::plain:
            return msx_cartridge_mapper_kind::plain;
        case msx2_cartridge_mapper::ascii8:
            return msx_cartridge_mapper_kind::ascii8;
        case msx2_cartridge_mapper::ascii8_sram8:
            return msx_cartridge_mapper_kind::ascii8_sram8;
        case msx2_cartridge_mapper::ascii16:
            return msx_cartridge_mapper_kind::ascii16;
        case msx2_cartridge_mapper::ascii16_sram2:
            return msx_cartridge_mapper_kind::ascii16_sram2;
        case msx2_cartridge_mapper::generic8:
            return msx_cartridge_mapper_kind::generic8;
        case msx2_cartridge_mapper::konami:
            return msx_cartridge_mapper_kind::konami;
        case msx2_cartridge_mapper::konami_scc:
            return msx_cartridge_mapper_kind::konami_scc;
        case msx2_cartridge_mapper::korean_msx:
            return msx_cartridge_mapper_kind::korean_msx;
        case msx2_cartridge_mapper::korean_msx_nemesis:
            return msx_cartridge_mapper_kind::korean_msx_nemesis;
        case msx2_cartridge_mapper::automatic:
        default:
            return msx_cartridge_mapper_kind::automatic;
        }
    }

    [[nodiscard]] std::string_view mapper_label(msx_cartridge_mapper mapper) noexcept {
        return mnemos::manifests::common::msx_cartridge_mapper_label(mapper_kind(mapper));
    }

    [[nodiscard]] std::string_view mapper_label(msx2_cartridge_mapper mapper) noexcept {
        return mnemos::manifests::common::msx_cartridge_mapper_label(mapper_kind(mapper));
    }

    void append_out_immediate(std::vector<std::uint8_t>& program, std::uint8_t port,
                              std::uint8_t value) {
        program.push_back(0x3EU); // LD A,n
        program.push_back(value);
        program.push_back(0xD3U); // OUT (n),A
        program.push_back(port);
    }

    void append_vdp_vram_write(std::vector<std::uint8_t>& program, std::uint16_t address,
                               std::uint8_t value) {
        append_out_immediate(program, 0x99U, static_cast<std::uint8_t>(address & 0xFFU));
        append_out_immediate(program, 0x99U,
                             static_cast<std::uint8_t>(0x40U | ((address >> 8U) & 0x3FU)));
        append_out_immediate(program, 0x98U, value);
    }

    void append_vdp_register_write(std::vector<std::uint8_t>& program, std::uint8_t reg,
                                   std::uint8_t value) {
        append_out_immediate(program, 0x99U, value);
        append_out_immediate(program, 0x99U, static_cast<std::uint8_t>(0x80U | (reg & 0x3FU)));
    }

    std::vector<std::uint8_t> make_synthetic_vdp_boot_bios(std::size_t bios_size) {
        std::vector<std::uint8_t> bios(bios_size, 0x76U); // HALT if execution reaches padding.
        std::vector<std::uint8_t> program;
        program.reserve(96U);

        append_vdp_vram_write(program, 0x0008U, 0x80U); // pattern 1 row 0
        append_vdp_vram_write(program, 0x0800U, 0x01U); // top-left name table cell
        append_vdp_vram_write(program, 0x2000U, 0xF1U); // white on black
        append_vdp_register_write(program, 1U, 0x40U);  // display enable, Graphics I
        append_vdp_register_write(program, 2U, 0x02U);  // name table $0800
        append_vdp_register_write(program, 3U, 0x80U);  // color table $2000
        append_vdp_register_write(program, 4U, 0x00U);  // pattern table $0000
        program.push_back(0x76U);                       // HALT

        REQUIRE(program.size() <= bios.size());
        std::copy(program.begin(), program.end(), bios.begin());
        return bios;
    }

    bool framebuffer_is_uniform(const mnemos::chips::frame_buffer_view& fb) {
        if (fb.pixels == nullptr || fb.width == 0U || fb.height == 0U) {
            return true;
        }

        const std::uint32_t first = fb.pixels[0];
        const std::uint32_t stride = fb.effective_stride();
        for (std::uint32_t y = 0; y < fb.height; ++y) {
            const std::uint32_t* row = fb.pixels + static_cast<std::size_t>(y) * stride;
            for (std::uint32_t x = 0; x < fb.width; ++x) {
                if (row[x] != first) {
                    return false;
                }
            }
        }
        return true;
    }

    std::unique_ptr<msx_system> boot_msx(std::span<const std::uint8_t> bios,
                                         std::span<const std::uint8_t> cartridge,
                                         std::span<const std::uint8_t> cartridge2,
                                         std::span<const std::uint8_t> disk_rom,
                                         std::span<const std::uint8_t> disk_image,
                                         std::span<const std::uint8_t> kanji_rom,
                                         std::span<const std::uint8_t> logo_rom,
                                         std::span<const std::uint8_t> cassette_image,
                                         mnemos::video_region region, std::uint64_t frames,
                                         msx_cartridge_mapper mapper =
                                             msx_cartridge_mapper::automatic,
                                         msx_cartridge_mapper mapper2 =
                                             msx_cartridge_mapper::automatic,
                                         const slot_layout_overrides& slot_layout = {},
                                         std::span<const key_press> boot_keys = {}) {
        msx_config config{};
        config.video_region = region;
        config.cartridge_mapper = mapper;
        config.cartridge2_mapper = mapper2;
        config.disk_enabled = !disk_rom.empty() || !disk_image.empty();
        config.cassette_image = cassette_image;
        config.logo_rom = logo_rom;
        apply_slot_layout(config, slot_layout);

        auto sys = assemble_msx(bios, cartridge, cartridge2, disk_rom, disk_image, kanji_rom,
                                config);
        for (const key_press& key : boot_keys) {
            sys->set_key(key.row, key.bit, true);
        }
        std::vector<std::string> d800_watch_events;
        install_d800_watch(sys->cpu,
                           [sys = sys.get()](std::uint16_t address) {
                               return sys->read_memory(address);
                           },
                           d800_watch_events);
        std::vector<std::string> vdp_register_watch_events;
        install_vdp_register_watch(
            sys->cpu, [sys = sys.get()](std::uint16_t address) { return sys->read_memory(address); },
            [sys = sys.get()](int index) {
                return sys->msx2_video() ? sys->vdp2.reg(index) : sys->vdp.reg(index);
            },
            vdp_register_watch_events);
        std::vector<std::string> vdp_io_watch_events;
        install_msx_vdp_io_watch(*sys, vdp_io_watch_events);
        std::vector<std::string> memory_watch_events;
        install_msx_memory_watch(*sys, memory_watch_events);
        std::vector<std::string> pc_watch_events;
        install_pc_range_watch(
            sys->cpu, [sys = sys.get()](std::uint16_t address) { return sys->read_memory(address); },
            pc_watch_events);
        std::vector<mnemos::runtime::scheduled_chip> chips = {
            {&sys->active_video(), 1U}, {&sys->cpu, 1U}, {&sys->psg, 1U}, {&sys->cassette, 1U}};
        if (sys->disk_enabled) {
            chips.push_back({&sys->fdc, 1U});
        }
        mnemos::runtime::scheduler sched(std::move(chips), &sys->active_video());
        sched.run_frames(frames);
        if (auto* trace = sys->cpu.introspection().trace(); trace != nullptr) {
            trace->install({});
        }
        sys->bus.set_access_observer({});
        append_d800_watch_info(d800_watch_events);
        append_vdp_register_watch_info(vdp_register_watch_events);
        append_vdp_io_watch_info(vdp_io_watch_events);
        append_memory_watch_info(memory_watch_events);
        append_pc_range_watch_info(pc_watch_events);
        return sys;
    }

    std::unique_ptr<msx2_system>
    boot_msx2(std::span<const std::uint8_t> bios, std::span<const std::uint8_t> sub_bios,
              std::span<const std::uint8_t> cartridge, std::span<const std::uint8_t> cartridge2,
              std::span<const std::uint8_t> logo_rom, std::span<const std::uint8_t> disk_rom,
              std::span<const std::uint8_t> disk_image, std::span<const std::uint8_t> kanji_rom,
              std::span<const std::uint8_t> cassette_image, mnemos::video_region region,
              std::uint64_t frames,
              msx2_cartridge_mapper mapper = msx2_cartridge_mapper::automatic,
              msx2_cartridge_mapper mapper2 = msx2_cartridge_mapper::automatic,
              const slot_layout_overrides& slot_layout = {},
              std::span<const key_press> boot_keys = {}) {
        msx2_config config{};
        config.video_region = region;
        config.cartridge_mapper = mapper;
        config.cartridge2_mapper = mapper2;
        config.cartridge2 = cartridge2;
        config.sub_bios = sub_bios;
        config.logo_rom = logo_rom;
        config.disk_rom = disk_rom;
        config.disk_image = disk_image;
        config.kanji_rom = kanji_rom;
        config.cassette_image = cassette_image;
        config.disk_enabled = !disk_rom.empty() || !disk_image.empty();
        apply_slot_layout(config, slot_layout);

        auto sys = assemble_msx2(bios, cartridge, config);
        for (const key_press& key : boot_keys) {
            sys->set_key(key.row, key.bit, true);
        }
        std::vector<std::string> d800_watch_events;
        install_d800_watch(sys->cpu,
                           [sys = sys.get()](std::uint16_t address) {
                               return sys->cpu_read(address);
                           },
                           d800_watch_events);
        std::vector<std::string> vdp_register_watch_events;
        install_vdp_register_watch(
            sys->cpu, [sys = sys.get()](std::uint16_t address) { return sys->cpu_read(address); },
            [sys = sys.get()](int index) { return sys->vdp.reg(index); },
            vdp_register_watch_events);
        std::vector<std::string> vdp_io_watch_events;
        install_msx2_vdp_io_watch(*sys, vdp_io_watch_events);
        std::vector<std::string> memory_watch_events;
        install_msx2_memory_watch(*sys, memory_watch_events);
        std::vector<std::string> pc_watch_events;
        install_pc_range_watch(
            sys->cpu, [sys = sys.get()](std::uint16_t address) { return sys->cpu_read(address); },
            pc_watch_events);
        std::vector<mnemos::runtime::scheduled_chip> chips = {{&sys->vdp, 1U},
                                                              {&sys->cpu, 1U},
                                                              {&sys->psg, 1U},
                                                              {&sys->cassette, 1U},
                                                              {&sys->rtc, 1U}};
        if (sys->disk_enabled()) {
            chips.push_back({&sys->fdc, 1U});
        }
        mnemos::runtime::scheduler sched(std::move(chips), &sys->vdp);
        sched.run_frames(frames);
        if (auto* trace = sys->cpu.introspection().trace(); trace != nullptr) {
            trace->install({});
        }
        sys->bus.set_access_observer({});
        append_d800_watch_info(d800_watch_events);
        append_vdp_register_watch_info(vdp_register_watch_events);
        append_vdp_io_watch_info(vdp_io_watch_events);
        append_memory_watch_info(memory_watch_events);
        append_pc_range_watch_info(pc_watch_events);
        return sys;
    }
} // namespace

TEST_CASE("msx generated BIOS boots to a deterministic framebuffer", "[golden][msx][hermetic]") {
    const std::vector<std::uint8_t> bios = make_synthetic_vdp_boot_bios(0x8000U);

    auto sys = boot_msx(bios, {}, {}, {}, {}, {}, {}, {}, mnemos::video_region::ntsc, 2U);
    const auto fb = sys->framebuffer();
    const std::string hash = mnemos::tools::hash_framebuffer(fb);

    CHECK_FALSE(framebuffer_is_uniform(fb));
    CHECK(fb.pixels[0] == 0x00FFFFFFU);
    CHECK(fb.pixels[1] == 0x00000000U);

    auto sys2 = boot_msx(bios, {}, {}, {}, {}, {}, {}, {}, mnemos::video_region::ntsc, 2U);
    CHECK(mnemos::tools::hash_framebuffer(sys2->framebuffer()) == hash);
    CHECK(hash == "e784ba1c9ff15518e97fd1a6aed53eea5583385b2ca4f27844d253d099d1e32b");
}

TEST_CASE("msx2 generated BIOS boots to a deterministic framebuffer", "[golden][msx2][hermetic]") {
    const std::vector<std::uint8_t> bios = make_synthetic_vdp_boot_bios(0x8000U);

    auto sys = boot_msx2(bios, {}, {}, {}, {}, {}, {}, {}, {}, mnemos::video_region::ntsc, 2U);
    const auto fb = sys->vdp.framebuffer();
    const std::string hash = mnemos::tools::hash_framebuffer(fb);

    CHECK_FALSE(framebuffer_is_uniform(fb));
    CHECK(fb.pixels[0] == 0x00FFFFFFU);
    CHECK(fb.pixels[1] == 0x00000000U);

    auto sys2 = boot_msx2(bios, {}, {}, {}, {}, {}, {}, {}, {}, mnemos::video_region::ntsc, 2U);
    CHECK(mnemos::tools::hash_framebuffer(sys2->vdp.framebuffer()) == hash);
    CHECK(hash == "e784ba1c9ff15518e97fd1a6aed53eea5583385b2ca4f27844d253d099d1e32b");
}

TEST_CASE("msx2 packed firmware splits into deterministic BIOS regions",
          "[golden][msx2][hermetic]") {
    std::vector<std::uint8_t> firmware(k_msx2_packed_main_sub_disk_size, std::uint8_t{0x00U});
    std::fill_n(firmware.begin(), k_msx2_main_bios_size, std::uint8_t{0x22U});
    std::fill_n(firmware.begin() + static_cast<std::ptrdiff_t>(k_msx2_main_bios_size),
                k_msx2_sub_rom_size, std::uint8_t{0x33U});
    std::fill_n(firmware.begin() + static_cast<std::ptrdiff_t>(k_msx2_packed_main_sub_size),
                k_msx2_disk_rom_size, std::uint8_t{0x44U});

    const auto split =
        split_msx2_packed_firmware(std::span<const std::uint8_t>{firmware.data(), firmware.size()});
    REQUIRE(split);
    CHECK(split->main_bios.size() == k_msx2_main_bios_size);
    CHECK(split->main_bios.front() == 0x22U);
    CHECK(split->main_bios.back() == 0x22U);
    CHECK(split->sub_rom.size() == k_msx2_sub_rom_size);
    CHECK(split->sub_rom.front() == 0x33U);
    CHECK(split->sub_rom.back() == 0x33U);
    CHECK(split->disk_rom.size() == k_msx2_disk_rom_size);
    CHECK(split->disk_rom.front() == 0x44U);
    CHECK(split->disk_rom.back() == 0x44U);
}

TEST_CASE("msx golden boot mapper overrides use the shared mapper parser",
          "[golden][msx][msx2][hermetic][mapper]") {
    CHECK(msx_mapper_from_name("ascii8-sram8") == msx_cartridge_mapper::ascii8_sram8);
    CHECK(msx_mapper_from_name("konamiscc") == msx_cartridge_mapper::konami_scc);
    CHECK(msx_mapper_from_name("generic8") == msx_cartridge_mapper::generic8);
    CHECK(msx_mapper_from_name("korean-msx-nemesis") ==
          msx_cartridge_mapper::korean_msx_nemesis);

    CHECK(msx2_mapper_from_name("ascii16-sram2") == msx2_cartridge_mapper::ascii16_sram2);
    CHECK(msx2_mapper_from_name("scc") == msx2_cartridge_mapper::konami_scc);
    CHECK(msx2_mapper_from_name("unknown") == msx2_cartridge_mapper::automatic);
}

TEST_CASE("msx golden boot slot overrides use shared machine-profile parsing",
          "[golden][msx][msx2][hermetic][slots]") {
    const auto ram_slot = parse_slot_location_value("3.2");
    REQUIRE(ram_slot);
    CHECK(ram_slot->primary == 3U);
    CHECK(ram_slot->secondary == 2U);

    const auto disk_slot = parse_slot_location_value("2:1");
    REQUIRE(disk_slot);
    CHECK(disk_slot->primary == 2U);
    CHECK(disk_slot->secondary == 1U);

    CHECK(parse_slot_location_value("1")->primary == 1U);
    CHECK_FALSE(parse_slot_location_value("4.0"));
    CHECK_FALSE(parse_slot_location_value("3.4"));
    CHECK_FALSE(parse_slot_location_value("3.1.0"));

    const auto expanded_list = parse_expanded_primary_slots_value("0,3");
    REQUIRE(expanded_list);
    CHECK(*expanded_list == 0x09U);
    CHECK(parse_expanded_primary_slots_value("0x5") == 0x05U);
    CHECK_FALSE(parse_expanded_primary_slots_value("0,4"));

    CHECK(parse_ram_size_value("128K") == 0x20000U);
    CHECK(parse_ram_size_value("1M") == 0x100000U);
    CHECK_FALSE(parse_ram_size_value("8K"));

    slot_layout_overrides layout{};
    layout.expanded_primary_slots = static_cast<std::uint8_t>(0x09U);
    layout.ram_slot = slot_location{3U, 2U};
    layout.sub_bios_slot = slot_location{3U, 0U};
    layout.disk_slot = slot_location{0U, 1U};
    layout.cartridge2_slot = slot_location{2U, 3U};
    layout.ram_size = 0x40000U;

    msx_config msx{};
    apply_slot_layout(msx, layout);
    CHECK(msx.expanded_primary_slots == 0x09U);
    CHECK(msx.ram_primary_slot == 3U);
    CHECK(msx.ram_secondary_slot == 2U);
    CHECK(msx.disk_primary_slot == 0U);
    CHECK(msx.disk_secondary_slot == 1U);
    CHECK(msx.cartridge2_primary_slot == 2U);
    CHECK(msx.cartridge2_secondary_slot == 3U);
    CHECK(msx.ram_mapper_segments == 16U);

    msx2_config msx2{};
    apply_slot_layout(msx2, layout);
    CHECK(msx2.expanded_primary_slots == 0x09U);
    CHECK(msx2.ram_primary_slot == 3U);
    CHECK(msx2.ram_secondary_slot == 2U);
    CHECK(msx2.sub_bios_primary_slot == 3U);
    CHECK(msx2.sub_bios_secondary_slot == 0U);
    CHECK(msx2.disk_primary_slot == 0U);
    CHECK(msx2.disk_secondary_slot == 1U);
    CHECK(msx2.cartridge2_primary_slot == 2U);
    CHECK(msx2.cartridge2_secondary_slot == 3U);
    CHECK(msx2.ram_size == 0x40000U);
}

TEST_CASE("msx golden boot key parser accepts names and matrix positions",
          "[golden][msx][msx2][hermetic][input]") {
    const auto return_key = parse_boot_key_value("return");
    REQUIRE(return_key);
    CHECK(return_key->row == 7U);
    CHECK(return_key->bit == 7U);

    const auto space_key = parse_boot_key_value("SPACE");
    REQUIRE(space_key);
    CHECK(space_key->row == 8U);
    CHECK(space_key->bit == 0U);

    const auto raw_dot_key = parse_boot_key_value("8.4");
    REQUIRE(raw_dot_key);
    CHECK(raw_dot_key->row == 8U);
    CHECK(raw_dot_key->bit == 4U);

    const auto raw_colon_key = parse_boot_key_value("6:1");
    REQUIRE(raw_colon_key);
    CHECK(raw_colon_key->row == 6U);
    CHECK(raw_colon_key->bit == 1U);

    CHECK_FALSE(parse_boot_key_value("16.0"));
    CHECK_FALSE(parse_boot_key_value("7.8"));
    CHECK_FALSE(parse_boot_key_value("unknown"));

    const std::array<key_press, 2> keys{{key_press{7U, 7U}, key_press{8U, 0U}}};
    CHECK(boot_key_summary(keys) == "7.7,8.0");
}

TEST_CASE("msx boots real firmware to a deterministic golden framebuffer", "[golden][msx]") {
    const auto bios_path = get_env("MNEMOS_MSX_BIOS");
    if (!bios_path) {
        SKIP("set MNEMOS_MSX_BIOS to a 32 KiB MSX BIOS image (copyrighted, never committed)");
    }

    auto bios = read_file(fs::path(*bios_path));
    if (!bios || bios->size() < 0x8000U) {
        SKIP("MNEMOS_MSX_BIOS=" << *bios_path << " could not be read as a 32 KiB MSX BIOS image");
    }

    const std::vector<std::uint8_t> cartridge = read_optional_file("MNEMOS_MSX_ROM");
    const std::vector<std::uint8_t> cartridge2 =
        read_optional_file("MNEMOS_MSX_ROM2", "MNEMOS_MSX_CART2");
    const std::vector<std::uint8_t> disk_rom = read_optional_file("MNEMOS_MSX_DISK_ROM");
    const std::vector<std::uint8_t> disk_image = read_optional_file("MNEMOS_MSX_DSK");
    const std::vector<std::uint8_t> kanji_rom = read_optional_file("MNEMOS_MSX_KANJI_ROM");
    const std::vector<std::uint8_t> logo_rom = read_optional_file("MNEMOS_MSX_LOGO_ROM");
    const std::vector<std::uint8_t> cassette_image = read_optional_file("MNEMOS_MSX_CAS");
    if (!disk_image.empty() && disk_rom.size() < k_msx_disk_rom_size) {
        SKIP("MNEMOS_MSX_DSK is set but MNEMOS_MSX_DISK_ROM is not a 16 KiB disk-interface ROM");
    }
    if (!logo_rom.empty() && logo_rom.size() < k_msx_logo_rom_size) {
        SKIP("MNEMOS_MSX_LOGO_ROM is set but could not be read as a 16 KiB MSX logo ROM image");
    }
    const msx_cartridge_mapper mapper = msx_mapper_from_env("MNEMOS_MSX_MAPPER");
    const msx_cartridge_mapper mapper2 = msx_mapper_from_env("MNEMOS_MSX_MAPPER2");
    const mnemos::video_region region = parse_region("MNEMOS_MSX_REGION");
    const std::uint64_t frames = parse_frame_count("MNEMOS_MSX_BOOT_FRAMES", 200U);
    const slot_layout_overrides slot_layout = parse_slot_layout_env("MNEMOS_MSX_", true);
    const std::vector<key_press> boot_keys = parse_boot_keys_env("MNEMOS_MSX_BOOT_KEYS");

    INFO("bios sha256: " << sha_hex(*bios));
    if (!cartridge.empty()) {
        INFO("cartridge sha256: " << sha_hex(cartridge));
    }
    if (!cartridge2.empty()) {
        INFO("cartridge2 sha256: " << sha_hex(cartridge2));
    }
    if (!disk_rom.empty()) {
        INFO("disk-rom sha256: " << sha_hex(disk_rom));
    }
    if (!disk_image.empty()) {
        INFO("disk sha256: " << sha_hex(disk_image));
    }
    if (!kanji_rom.empty()) {
        INFO("kanji-rom sha256: " << sha_hex(kanji_rom));
    }
    if (!logo_rom.empty()) {
        INFO("logo-rom sha256: " << sha_hex(logo_rom));
    }
    if (!cassette_image.empty()) {
        INFO("cassette sha256: " << sha_hex(cassette_image));
    }
    INFO("region: " << (region == mnemos::video_region::pal ? "pal" : "ntsc"));
    INFO("frames rendered: " << frames);
    INFO("cartridge mapper: " << mapper_label(mapper));
    INFO("cartridge2 mapper: " << mapper_label(mapper2));
    INFO("slot layout: " << slot_layout_label(slot_layout));
    INFO("boot keys: " << boot_key_summary(boot_keys));

    auto sys = boot_msx(*bios, cartridge, cartridge2, disk_rom, disk_image, kanji_rom, logo_rom,
                        cassette_image, region, frames, mapper, mapper2, slot_layout, boot_keys);
    INFO("resolved cartridge mapper: " << mapper_label(sys->mapper));
    INFO("resolved cartridge2 mapper: " << mapper_label(sys->cartridge2_mapper));
    const auto regs = sys->cpu.cpu_registers();
    INFO("cpu pc/sp/af/bc/de/hl: " << hex16(regs.pc) << '/' << hex16(regs.sp) << '/'
                                   << hex16(regs.af) << '/' << hex16(regs.bc) << '/'
                                   << hex16(regs.de) << '/' << hex16(regs.hl)
                                   << " halted=" << (regs.halted ? "true" : "false")
                                   << " iff1=" << (regs.iff1 ? "true" : "false")
                                   << " iff2=" << (regs.iff2 ? "true" : "false")
                                   << " im=" << static_cast<unsigned>(regs.im)
                                   << " cycles=" << sys->cpu.elapsed_cycles());
    INFO("slot state: primary=" << hex8(sys->primary_slot_select)
                                << " secondary0=" << hex8(sys->secondary_slot_select[0])
                                << " secondary1=" << hex8(sys->secondary_slot_select[1])
                                << " secondary2=" << hex8(sys->secondary_slot_select[2])
                                << " secondary3=" << hex8(sys->secondary_slot_select[3]));
    INFO("ram state: pages16k=" << memory_nonzero_pages(sys->work_ram(), 0x4000U) << ' '
                                << msx_title_staging_summary(sys->work_ram()));
    INFO("logical memory state: "
         << msx_logical_state_summary([sys = sys.get()](std::uint16_t address) {
                return sys->bus.read8(address);
            }));
    INFO("pc window: "
         << cpu_window_summary(regs.pc, [sys = sys.get()](std::uint16_t address) {
                return sys->bus.read8(address);
            }));
    const auto fb = sys->framebuffer();
    const auto vram = sys->msx2_video() ? sys->vdp2.vram() : sys->vdp.vram();
    INFO("vdp state: frame=" << sys->active_video().frame_index()
                             << " mode="
                             << static_cast<unsigned>(sys->msx2_video()
                                                          ? static_cast<std::uint8_t>(
                                                                sys->vdp2.mode())
                                                          : static_cast<std::uint8_t>(
                                                                sys->vdp.mode()))
                             << " r0=" << hex8(sys->msx2_video() ? sys->vdp2.reg(0) : sys->vdp.reg(0))
                             << " r1=" << hex8(sys->msx2_video() ? sys->vdp2.reg(1) : sys->vdp.reg(1))
                             << " r2=" << hex8(sys->msx2_video() ? sys->vdp2.reg(2) : sys->vdp.reg(2))
                             << " r7=" << hex8(sys->msx2_video() ? sys->vdp2.reg(7) : sys->vdp.reg(7))
                             << " vram_nonzero=" << nonzero_bytes(vram)
                             << " first_pixel="
                             << (fb.pixels == nullptr ? 0U : static_cast<unsigned>(fb.pixels[0])));
    if (sys->msx2_video()) {
        UNSCOPED_INFO("v9938 extended state: r8=" << hex8(sys->vdp2.reg(8))
                                                   << " r9=" << hex8(sys->vdp2.reg(9))
                                                   << " r3=" << hex8(sys->vdp2.reg(3))
                                                   << " r4=" << hex8(sys->vdp2.reg(4))
                                                   << " r5=" << hex8(sys->vdp2.reg(5))
                                                   << " r6=" << hex8(sys->vdp2.reg(6))
                                                   << " r10=" << hex8(sys->vdp2.reg(10))
                                                   << " r11=" << hex8(sys->vdp2.reg(11))
                                                   << " r13=" << hex8(sys->vdp2.reg(13))
                                                   << " r23=" << hex8(sys->vdp2.reg(23))
                                                   << " r44=" << hex8(sys->vdp2.reg(44))
                                                   << " r45=" << hex8(sys->vdp2.reg(45))
                                                   << " r46=" << hex8(sys->vdp2.reg(46))
                                                   << " s2=" << hex8(sys->vdp2.status(2))
                                                   << " vram_pages_32k="
                                                   << vram_page_nonzero_summary(vram, 0x8000U)
                                                   << " visible_g4_nonzero="
                                                   << visible_graphics4_nonzero_bytes(
                                                          vram, sys->vdp2.reg(2), sys->vdp2.reg(9))
                                                   << " visible_g4_hist="
                                                   << visible_graphics4_nibble_histogram(
                                                          vram, sys->vdp2.reg(2), sys->vdp2.reg(9))
                                                   << " visible_g4_first_nonzero="
                                                   << first_visible_graphics4_nonzero_bytes(
                                                          vram, sys->vdp2.reg(2), sys->vdp2.reg(9))
                                                   << " visible_g1_hist="
                                                   << visible_graphics1_pen_histogram(
                                                          vram, sys->vdp2.reg(2), sys->vdp2.reg(3),
                                                          sys->vdp2.reg(4))
                                                   << " visible_g1_first_non_backdrop="
                                                   << first_visible_graphics1_non_backdrop_sample(
                                                          vram, sys->vdp2.reg(2), sys->vdp2.reg(3),
                                                          sys->vdp2.reg(4), sys->vdp2.reg(7))
                                                   << " g1_sample_l0="
                                                   << graphics1_table_sample(vram, sys->vdp2.reg(2),
                                                                             sys->vdp2.reg(3),
                                                                             sys->vdp2.reg(4), 0,
                                                                             0)
                                                   << " g1_sample_l48="
                                                   << graphics1_table_sample(vram, sys->vdp2.reg(2),
                                                                             sys->vdp2.reg(3),
                                                                             sys->vdp2.reg(4), 48,
                                                                             0)
                                                   << " g2_sample_l0="
                                                   << graphics2_table_sample(vram, sys->vdp2.reg(2),
                                                                             sys->vdp2.reg(3),
                                                                             sys->vdp2.reg(4),
                                                                             sys->vdp2.reg(10), 0,
                                                                             0)
                                                   << " g2_sample_l48="
                                                   << graphics2_table_sample(vram, sys->vdp2.reg(2),
                                                                             sys->vdp2.reg(3),
                                                                             sys->vdp2.reg(4),
                                                                             sys->vdp2.reg(10), 48,
                                                                             0)
                                                   << " palette=" << palette_summary(sys->vdp2));
    } else {
        UNSCOPED_INFO("tms9918a extended state: r3="
                      << hex8(sys->vdp.reg(3)) << " r4=" << hex8(sys->vdp.reg(4))
                      << " r5=" << hex8(sys->vdp.reg(5)) << " r6="
                      << hex8(sys->vdp.reg(6)) << " visible_g2_hist="
                      << visible_graphics2_pen_histogram(vram, sys->vdp.reg(2), sys->vdp.reg(3),
                                                         sys->vdp.reg(4), 0)
                      << " visible_g2_first_non_backdrop="
                      << first_visible_graphics2_non_backdrop_sample(
                             vram, sys->vdp.reg(2), sys->vdp.reg(3), sys->vdp.reg(4),
                             sys->vdp.reg(7), 0)
                      << " g2_sample_l0="
                      << graphics2_table_sample(vram, sys->vdp.reg(2), sys->vdp.reg(3),
                                                sys->vdp.reg(4), 0, 0, 0)
                      << " g2_sample_l48="
                      << graphics2_table_sample(vram, sys->vdp.reg(2), sys->vdp.reg(3),
                                                sys->vdp.reg(4), 0, 48, 0)
                      << " visible_g1_hist="
                      << visible_graphics1_pen_histogram(vram, sys->vdp.reg(2), sys->vdp.reg(3),
                                                         sys->vdp.reg(4))
                      << " visible_g1_first_non_backdrop="
                      << first_visible_graphics1_non_backdrop_sample(
                             vram, sys->vdp.reg(2), sys->vdp.reg(3), sys->vdp.reg(4),
                             sys->vdp.reg(7))
                      << " g1_sample_l0="
                      << graphics1_table_sample(vram, sys->vdp.reg(2), sys->vdp.reg(3),
                                                sys->vdp.reg(4), 0, 0)
                      << " g1_sample_l48="
                      << graphics1_table_sample(vram, sys->vdp.reg(2), sys->vdp.reg(3),
                                                sys->vdp.reg(4), 48, 0));
    }
    const std::string hash = mnemos::tools::hash_framebuffer(fb);

    CHECK_FALSE(framebuffer_is_uniform(fb));

    auto sys2 = boot_msx(*bios, cartridge, cartridge2, disk_rom, disk_image, kanji_rom, logo_rom,
                         cassette_image, region, frames, mapper, mapper2, slot_layout, boot_keys);
    CHECK(mnemos::tools::hash_framebuffer(sys2->framebuffer()) == hash);

    INFO("boot framebuffer sha256: " << hash);

    const auto golden = get_env("MNEMOS_MSX_BOOT_SHA256");
    if (golden) {
        CHECK(hash == *golden);
    } else {
        WARN("no golden hash set; computed MSX boot framebuffer sha256 = "
             << hash << " (set MNEMOS_MSX_BOOT_SHA256 to lock it)");
    }
}

TEST_CASE("msx2 boots real firmware to a deterministic golden framebuffer", "[golden][msx2]") {
    const auto sub_bios_path = get_env("MNEMOS_MSX2_SUB_ROM", "MNEMOS_MSX2_SUBROM");
    std::vector<std::uint8_t> bios;
    std::vector<std::uint8_t> sub_bios;
    std::vector<std::uint8_t> logo_rom;
    std::vector<std::uint8_t> disk_rom;

    if (const auto firmware_path = get_env("MNEMOS_MSX2_FIRMWARE")) {
        auto firmware = read_file(fs::path(*firmware_path));
        const auto split = firmware ? split_msx2_packed_firmware(std::span<const std::uint8_t>{
                                          firmware->data(), firmware->size()})
                                    : std::optional<msx2_packed_firmware>{};
        if (!split) {
            SKIP("MNEMOS_MSX2_FIRMWARE="
                 << *firmware_path
                 << " could not be read as packed MSX2 main BIOS + sub-ROM firmware");
        }
        bios = std::move(split->main_bios);
        sub_bios = std::move(split->sub_rom);
        disk_rom = std::move(split->disk_rom);
    } else {
        const auto bios_path = get_env("MNEMOS_MSX2_BIOS");
        if (!bios_path) {
            SKIP("set MNEMOS_MSX2_FIRMWARE to a packed image, or MNEMOS_MSX2_BIOS and "
                 "MNEMOS_MSX2_SUB_ROM to split MSX2 firmware (copyrighted, never committed)");
        }

        auto bios_image = read_file(fs::path(*bios_path));
        if (!bios_image || bios_image->size() < k_msx2_main_bios_size) {
            SKIP("MNEMOS_MSX2_BIOS=" << *bios_path
                                     << " could not be read as an MSX2 main BIOS image");
        }

        if (auto split = split_msx2_packed_firmware(
                std::span<const std::uint8_t>{bios_image->data(), bios_image->size()})) {
            bios = std::move(split->main_bios);
            if (!sub_bios_path) {
                sub_bios = std::move(split->sub_rom);
            }
            disk_rom = std::move(split->disk_rom);
        } else {
            bios = std::move(*bios_image);
        }
    }

    const std::vector<std::uint8_t> cartridge = read_optional_file("MNEMOS_MSX2_ROM");
    const std::vector<std::uint8_t> cartridge2 =
        read_optional_file("MNEMOS_MSX2_ROM2", "MNEMOS_MSX2_CART2");
    if (sub_bios_path) {
        auto explicit_sub_bios = read_file(fs::path(*sub_bios_path));
        if (!explicit_sub_bios || explicit_sub_bios->size() < k_msx2_sub_rom_size) {
            SKIP("MNEMOS_MSX2_SUB_ROM=" << *sub_bios_path
                                        << " could not be read as an MSX2 sub-ROM image");
        }
        sub_bios = std::move(*explicit_sub_bios);
    }
    if (const auto logo_rom_path = get_env("MNEMOS_MSX2_LOGO_ROM")) {
        auto explicit_logo_rom = read_file(fs::path(*logo_rom_path));
        if (!explicit_logo_rom || explicit_logo_rom->size() < k_msx2_logo_rom_size) {
            SKIP("MNEMOS_MSX2_LOGO_ROM=" << *logo_rom_path
                                         << " could not be read as an MSX2 logo ROM image");
        }
        logo_rom = std::move(*explicit_logo_rom);
    }
    if (sub_bios.empty()) {
        SKIP("set MNEMOS_MSX2_SUB_ROM to an MSX2 sub-ROM, or use MNEMOS_MSX2_FIRMWARE/"
             "packed MNEMOS_MSX2_BIOS (copyrighted, never committed)");
    }
    std::vector<std::uint8_t> explicit_disk_rom =
        read_optional_file("MNEMOS_MSX2_DISK_ROM", "MNEMOS_MSX2_DISKROM");
    if (!explicit_disk_rom.empty()) {
        disk_rom = std::move(explicit_disk_rom);
    }
    const std::vector<std::uint8_t> disk_image = read_optional_file("MNEMOS_MSX2_DSK");
    const std::vector<std::uint8_t> cassette_image = read_optional_file("MNEMOS_MSX2_CAS");
    const std::vector<std::uint8_t> kanji_rom = read_optional_file("MNEMOS_MSX2_KANJI_ROM");
    if (!disk_image.empty() && disk_rom.size() < k_msx2_disk_rom_size) {
        SKIP("MNEMOS_MSX2_DSK is set but no packed or explicit 16 KiB MSX2 disk-interface ROM "
             "is available");
    }
    const msx2_cartridge_mapper mapper = msx2_mapper_from_env("MNEMOS_MSX2_MAPPER");
    const msx2_cartridge_mapper mapper2 = msx2_mapper_from_env("MNEMOS_MSX2_MAPPER2");
    const mnemos::video_region region = parse_region("MNEMOS_MSX2_REGION");
    const std::uint64_t frames = parse_frame_count("MNEMOS_MSX2_BOOT_FRAMES", 200U);
    const slot_layout_overrides slot_layout = parse_slot_layout_env("MNEMOS_MSX2_", true);
    const std::vector<key_press> boot_keys = parse_boot_keys_env("MNEMOS_MSX2_BOOT_KEYS");

    INFO("bios sha256: " << sha_hex(bios));
    INFO("sub-rom sha256: " << sha_hex(sub_bios));
    if (!logo_rom.empty()) {
        INFO("logo-rom sha256: " << sha_hex(logo_rom));
    }
    if (!cartridge.empty()) {
        INFO("cartridge sha256: " << sha_hex(cartridge));
    }
    if (!cartridge2.empty()) {
        INFO("cartridge2 sha256: " << sha_hex(cartridge2));
    }
    if (!disk_rom.empty()) {
        INFO("disk-rom sha256: " << sha_hex(disk_rom));
    }
    if (!disk_image.empty()) {
        INFO("disk sha256: " << sha_hex(disk_image));
    }
    if (!cassette_image.empty()) {
        INFO("cassette sha256: " << sha_hex(cassette_image));
    }
    if (!kanji_rom.empty()) {
        INFO("kanji-rom sha256: " << sha_hex(kanji_rom));
    }
    INFO("region: " << (region == mnemos::video_region::pal ? "pal" : "ntsc"));
    INFO("frames rendered: " << frames);
    INFO("cartridge mapper: " << mapper_label(mapper));
    INFO("cartridge2 mapper: " << mapper_label(mapper2));
    INFO("slot layout: " << slot_layout_label(slot_layout));
    INFO("boot keys: " << boot_key_summary(boot_keys));

    auto sys = boot_msx2(bios, sub_bios, cartridge, cartridge2, logo_rom, disk_rom, disk_image,
                         kanji_rom, cassette_image, region, frames, mapper, mapper2, slot_layout,
                         boot_keys);
    INFO("resolved cartridge mapper: " << mapper_label(sys->cart_mapper));
    INFO("resolved cartridge2 mapper: " << mapper_label(sys->cart2_mapper));
    const auto regs = sys->cpu.cpu_registers();
    INFO("cpu pc/sp/af/bc/de/hl: " << hex16(regs.pc) << '/' << hex16(regs.sp) << '/'
                                   << hex16(regs.af) << '/' << hex16(regs.bc) << '/'
                                   << hex16(regs.de) << '/' << hex16(regs.hl)
                                   << " halted=" << (regs.halted ? "true" : "false")
                                   << " iff1=" << (regs.iff1 ? "true" : "false")
                                   << " iff2=" << (regs.iff2 ? "true" : "false")
                                   << " im=" << static_cast<unsigned>(regs.im)
                                   << " cycles=" << sys->cpu.elapsed_cycles());
    INFO("slot state: primary=" << hex8(sys->primary_slot)
                                << " secondary0=" << hex8(sys->secondary_slot[0])
                                << " secondary1=" << hex8(sys->secondary_slot[1])
                                << " secondary2=" << hex8(sys->secondary_slot[2])
                                << " secondary3=" << hex8(sys->secondary_slot[3]));
    INFO("ram state: pages16k=" << memory_nonzero_pages(sys->ram_view(), 0x4000U) << ' '
                                << msx_title_staging_summary(sys->ram_view()));
    INFO("ram mapper segments: " << ram_mapper_segment_summary(sys->ram_segment));
    INFO("logical memory state: "
         << msx_logical_state_summary([sys = sys.get()](std::uint16_t address) {
                return sys->cpu_read(address);
            }));
    INFO("pc window: "
         << cpu_window_summary(regs.pc, [sys = sys.get()](std::uint16_t address) {
                return sys->cpu_read(address);
            }));
    const auto fb = sys->vdp.framebuffer();
    INFO("vdp state: frame=" << sys->vdp.frame_index()
                             << " mode=" << static_cast<unsigned>(sys->vdp.mode())
                             << " r0=" << hex8(sys->vdp.reg(0))
                             << " r1=" << hex8(sys->vdp.reg(1))
                             << " r2=" << hex8(sys->vdp.reg(2))
                             << " r7=" << hex8(sys->vdp.reg(7))
                             << " r15=" << hex8(sys->vdp.reg(15))
                             << " s0=" << hex8(sys->vdp.status(0))
                             << " s1=" << hex8(sys->vdp.status(1))
                             << " irq=" << (sys->vdp.irq_asserted() ? "true" : "false")
                             << " vram_nonzero=" << nonzero_bytes(sys->vdp.vram())
                             << " first_pixel="
                             << (fb.pixels == nullptr ? 0U : static_cast<unsigned>(fb.pixels[0])));
    INFO("v9938 extended state: r8=" << hex8(sys->vdp.reg(8))
                                     << " r9=" << hex8(sys->vdp.reg(9))
                                     << " r3=" << hex8(sys->vdp.reg(3))
                                     << " r4=" << hex8(sys->vdp.reg(4))
                                     << " r5=" << hex8(sys->vdp.reg(5))
                                     << " r6=" << hex8(sys->vdp.reg(6))
                                     << " r10=" << hex8(sys->vdp.reg(10))
                                     << " r11=" << hex8(sys->vdp.reg(11))
                                     << " r18=" << hex8(sys->vdp.reg(18))
                                     << " r13=" << hex8(sys->vdp.reg(13))
                                     << " r23=" << hex8(sys->vdp.reg(23))
                                     << " r44=" << hex8(sys->vdp.reg(44))
                                     << " r45=" << hex8(sys->vdp.reg(45))
                                     << " r46=" << hex8(sys->vdp.reg(46))
                                     << " s2=" << hex8(sys->vdp.status(2))
                                     << " vram_pages_32k="
                                     << vram_page_nonzero_summary(sys->vdp.vram(), 0x8000U)
                                     << " visible_g4_nonzero="
                                     << visible_graphics4_nonzero_bytes(
                                            sys->vdp.vram(), sys->vdp.reg(2), sys->vdp.reg(9))
                                     << " visible_g4_hist="
                                     << visible_graphics4_nibble_histogram(
                                            sys->vdp.vram(), sys->vdp.reg(2), sys->vdp.reg(9))
                                     << " visible_g4_first_nonzero="
                                     << first_visible_graphics4_nonzero_bytes(
                                            sys->vdp.vram(), sys->vdp.reg(2), sys->vdp.reg(9))
                                     << " visible_g1_hist="
                                     << visible_graphics1_pen_histogram(
                                            sys->vdp.vram(), sys->vdp.reg(2), sys->vdp.reg(3),
                                            sys->vdp.reg(4))
                                     << " visible_g1_first_non_backdrop="
                                     << first_visible_graphics1_non_backdrop_sample(
                                            sys->vdp.vram(), sys->vdp.reg(2), sys->vdp.reg(3),
                                            sys->vdp.reg(4), sys->vdp.reg(7))
                                     << " g1_sample_l0="
                                     << graphics1_table_sample(
                                            sys->vdp.vram(), sys->vdp.reg(2), sys->vdp.reg(3),
                                            sys->vdp.reg(4), 0, 0)
                                     << " g1_sample_l48="
                                     << graphics1_table_sample(
                                            sys->vdp.vram(), sys->vdp.reg(2), sys->vdp.reg(3),
                                            sys->vdp.reg(4), 48, 0)
                                     << " g2_sample_l0="
                                     << graphics2_table_sample(
                                            sys->vdp.vram(), sys->vdp.reg(2), sys->vdp.reg(3),
                                            sys->vdp.reg(4), sys->vdp.reg(10), 0, 0)
                                     << " g2_sample_l48="
                                     << graphics2_table_sample(
                                            sys->vdp.vram(), sys->vdp.reg(2), sys->vdp.reg(3),
                                            sys->vdp.reg(4), sys->vdp.reg(10), 48, 0)
                                     << " palette=" << palette_summary(sys->vdp));
    const std::string hash = mnemos::tools::hash_framebuffer(fb);

    CHECK_FALSE(framebuffer_is_uniform(fb));

    auto sys2 = boot_msx2(bios, sub_bios, cartridge, cartridge2, logo_rom, disk_rom, disk_image,
                          kanji_rom, cassette_image, region, frames, mapper, mapper2, slot_layout,
                          boot_keys);
    CHECK(mnemos::tools::hash_framebuffer(sys2->vdp.framebuffer()) == hash);

    INFO("boot framebuffer sha256: " << hash);

    const auto golden = get_env("MNEMOS_MSX2_BOOT_SHA256");
    if (golden) {
        CHECK(hash == *golden);
    } else {
        WARN("no golden hash set; computed MSX2 boot framebuffer sha256 = "
             << hash << " (set MNEMOS_MSX2_BOOT_SHA256 to lock it)");
    }
}
