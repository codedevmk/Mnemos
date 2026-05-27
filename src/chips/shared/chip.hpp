#pragma once

#include "introspection_views.hpp"

#include <cstdint>
#include <span>
#include <string_view>

namespace mnemos::chips {

    enum class chip_class : std::uint8_t {
        cpu,
        audio_synth,
        video,
        bus_controller,
        storage,
        mapper,
        peripheral,
    };

    [[nodiscard]] constexpr std::string_view chip_class_name(chip_class klass) noexcept {
        switch (klass) {
        case chip_class::cpu:
            return "cpu";
        case chip_class::audio_synth:
            return "audio_synth";
        case chip_class::video:
            return "video";
        case chip_class::bus_controller:
            return "bus_controller";
        case chip_class::storage:
            return "storage";
        case chip_class::mapper:
            return "mapper";
        case chip_class::peripheral:
            return "peripheral";
        }

        return "unknown";
    }

    struct chip_metadata final {
        std::string_view manufacturer;
        std::string_view part_number;
        std::string_view family;
        chip_class klass{chip_class::peripheral};
        std::uint32_t revision{};
    };

    enum class reset_kind : std::uint8_t {
        power_on,
        hard,
        soft,
    };

    enum class register_value_format : std::uint8_t {
        unsigned_integer,
        signed_integer,
        flags,
    };

    struct register_descriptor final {
        std::string_view name;
        std::uint64_t value{};
        std::uint8_t bit_width{};
        register_value_format format{register_value_format::unsigned_integer};
    };

    class state_writer;
    class state_reader;
    class ibus;

    class ichip {
      public:
        ichip() = default;
        ichip(const ichip&) = delete;
        ichip& operator=(const ichip&) = delete;
        virtual ~ichip() = default;

        [[nodiscard]] virtual chip_metadata metadata() const noexcept = 0;
        virtual void tick(std::uint64_t cycles) = 0;
        virtual void reset(reset_kind kind) = 0;

        virtual void save_state(state_writer& writer) const = 0;
        virtual void load_state(state_reader& reader) = 0;

        [[nodiscard]] virtual instrumentation::ichip_introspection& introspection() noexcept = 0;
    };

    // Each tier-2 subclass interface below selects the chip's classification
    // by inheritance. Code that needs the class at runtime queries
    // `metadata().klass`; code that needs it at compile time uses
    // `std::is_base_of_v<icpu, ConcreteChip>` etc. There used to be a third
    // path (`static constexpr chip_class static_class` on each interface);
    // it was redundant with both the inheritance check and the metadata
    // field, so it has been removed.

    class icpu : public ichip {
      public:
        // Inject the bus the CPU executes against. Called once at attach time,
        // before the first tick; the CPU observes but does not own the bus.
        virtual void attach_bus(ibus& bus) noexcept = 0;
    };

    class iaudio_synth : public ichip {};

    // A borrowed, immutable view of a video chip's most recent complete frame.
    // Pixels are 0x00RRGGBB (alpha unused), row-major. `width` is the count of
    // *visible* pixels per row; `stride` is the storage pitch in pixels, >=
    // width, used when the framebuffer is sized for a worst-case mode but the
    // active mode renders fewer columns (e.g. Genesis H32 mode: visible width
    // 256 but the storage row is 320 pixels wide). Iterate rows as
    // `pixels + row * effective_stride()` and read the first `width` pixels of
    // each row. The view is valid until the next tick that completes a frame.
    struct frame_buffer_view final {
        const std::uint32_t* pixels{};
        std::uint32_t width{};
        std::uint32_t height{};
        std::uint32_t stride{}; // pixels per row in storage; 0 = packed (== width)

        [[nodiscard]] constexpr std::uint32_t effective_stride() const noexcept {
            return stride != 0U ? stride : width;
        }
    };

    class ivideo : public ichip {
      public:
        // Monotonic count of completed frames; the runtime detects a frame
        // boundary by observing this increment.
        [[nodiscard]] virtual std::uint64_t frame_index() const noexcept = 0;

        // The current framebuffer (see frame_buffer_view). Stable geometry for a
        // given machine configuration.
        [[nodiscard]] virtual frame_buffer_view framebuffer() const noexcept = 0;
    };

    class ibus_controller : public ichip {};
    class istorage : public ichip {};
    class imapper : public ichip {};
    class iperipheral : public ichip {};

    // Memory-mapped register access. A chip that exposes an MMIO window mixes this
    // in alongside its class interface; the topology bus routes reads/writes in the
    // chip's mapped range to these, passing the offset from the region base.
    class immio {
      public:
        immio() = default;
        immio(const immio&) = delete;
        immio& operator=(const immio&) = delete;
        virtual ~immio() = default;

        [[nodiscard]] virtual std::uint8_t mmio_read(std::uint16_t offset) = 0;
        virtual void mmio_write(std::uint16_t offset, std::uint8_t value) = 0;
    };

} // namespace mnemos::chips
