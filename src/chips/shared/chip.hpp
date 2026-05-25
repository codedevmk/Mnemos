#pragma once

#include <cstdint>
#include <span>
#include <string_view>

namespace mnemos::instrumentation {
    class i_chip_introspection {
      public:
        i_chip_introspection() = default;
        i_chip_introspection(const i_chip_introspection&) = delete;
        i_chip_introspection& operator=(const i_chip_introspection&) = delete;
        virtual ~i_chip_introspection() = default;
    };
} // namespace mnemos::instrumentation

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
    class i_bus;

    class i_chip {
      public:
        i_chip() = default;
        i_chip(const i_chip&) = delete;
        i_chip& operator=(const i_chip&) = delete;
        virtual ~i_chip() = default;

        [[nodiscard]] virtual chip_metadata metadata() const noexcept = 0;
        virtual void tick(std::uint64_t cycles) = 0;
        virtual void reset(reset_kind kind) = 0;

        virtual void save_state(state_writer& writer) const = 0;
        virtual void load_state(state_reader& reader) = 0;

        [[nodiscard]] virtual instrumentation::i_chip_introspection& introspection() noexcept = 0;
    };

    class i_cpu : public i_chip {
      public:
        static constexpr chip_class static_class = chip_class::cpu;

        // Inject the bus the CPU executes against. Called once at attach time,
        // before the first tick; the CPU observes but does not own the bus.
        virtual void attach_bus(i_bus& bus) noexcept = 0;
    };

    class i_audio_synth : public i_chip {
      public:
        static constexpr chip_class static_class = chip_class::audio_synth;
    };

    // A borrowed, immutable view of a video chip's most recent complete frame.
    // Pixels are 0x00RRGGBB (alpha unused); row-major, `width` pixels per row.
    // The view is valid until the next tick that completes a frame.
    struct frame_buffer_view final {
        const std::uint32_t* pixels{};
        std::uint32_t width{};
        std::uint32_t height{};
    };

    class i_video : public i_chip {
      public:
        static constexpr chip_class static_class = chip_class::video;

        // Monotonic count of completed frames; the runtime detects a frame
        // boundary by observing this increment.
        [[nodiscard]] virtual std::uint64_t frame_index() const noexcept = 0;

        // The current framebuffer (see frame_buffer_view). Stable geometry for a
        // given machine configuration.
        [[nodiscard]] virtual frame_buffer_view framebuffer() const noexcept = 0;
    };

    class i_bus_controller : public i_chip {
      public:
        static constexpr chip_class static_class = chip_class::bus_controller;
    };

    class i_storage : public i_chip {
      public:
        static constexpr chip_class static_class = chip_class::storage;
    };

    class i_mapper : public i_chip {
      public:
        static constexpr chip_class static_class = chip_class::mapper;
    };

    class i_peripheral : public i_chip {
      public:
        static constexpr chip_class static_class = chip_class::peripheral;
    };

    // Memory-mapped register access. A chip that exposes an MMIO window mixes this
    // in alongside its class interface; the topology bus routes reads/writes in the
    // chip's mapped range to these, passing the offset from the region base.
    class i_mmio {
      public:
        i_mmio() = default;
        i_mmio(const i_mmio&) = delete;
        i_mmio& operator=(const i_mmio&) = delete;
        virtual ~i_mmio() = default;

        [[nodiscard]] virtual std::uint8_t mmio_read(std::uint16_t offset) = 0;
        virtual void mmio_write(std::uint16_t offset, std::uint8_t value) = 0;
    };

} // namespace mnemos::chips
