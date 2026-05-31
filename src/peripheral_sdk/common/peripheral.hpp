#pragma once

// Peripherals are plug-in devices that attach to a system's external ports
// (controller, expansion, serial). Unlike chips -- which live on the system's
// motherboard -- a peripheral is selected at runtime, can be swapped, and
// carries its own manufacturer + model + capability profile so a frontend
// can present them as catalog entries and an adapter can pick a default
// appropriate to the loaded software.

#include <cstdint>

namespace mnemos::peripheral {

    // System-agnostic abstract input state a frontend passes to a peripheral
    // each frame. Lives at this tier so the player adapters (tier 8) and the
    // peripheral devices (tier 2) can both reference the same shape without
    // a tier inversion. A peripheral consumes the subset of fields its
    // hardware exposes and ignores the rest -- a 3-button pad reads only
    // u/d/l/r/a/b/c/start; a mouse reads dx/dy/buttons; a lightgun reads
    // aim_x/aim_y/trigger; etc.
    struct controller_state final {
        bool up{};
        bool down{};
        bool left{};
        bool right{};
        bool start{};
        bool select{};
        bool a{};
        bool b{};
        bool c{};
        bool x{};
        bool y{};
        bool z{};
        bool mode{};
    };

    // The category a peripheral belongs to. Drives the category subtree the
    // implementation lives in (input/, output/, storage/, network/, ...) and
    // tells frontends how to surface the device in any picker UI.
    enum class kind : std::uint8_t {
        input_pad,      // joypad / control pad (digital + buttons)
        input_lightgun, // CRT-aimed lightgun (Menacer, Justifier, ...)
        input_mouse,    // X/Y mouse with buttons
        input_keyboard, // full keyboard
        input_paddle,   // analog rotary paddle
        multitap,       // splitter that multiplies one port into many
        storage,        // floppy, CD, SD card, ...
        network,        // modem, network adapter
        output,         // printer, plotter, audio expander
        unknown,
    };

    // System-compatibility bitfield. Each bit names a host system the
    // peripheral can plug into electrically + protocol-wise. A 6-button
    // Genesis pad sets both `genesis` and `sms` (it falls back to 3-button
    // mode on a Master System); a Menacer is `genesis` only.
    namespace host {
        inline constexpr std::uint32_t genesis = 1U << 0; // Mega Drive / Genesis
        inline constexpr std::uint32_t sms = 1U << 1;     // Master System
        inline constexpr std::uint32_t mega_cd = 1U << 2; // Mega-CD / Sega CD
        inline constexpr std::uint32_t sega_32x = 1U << 3;
        inline constexpr std::uint32_t game_gear = 1U << 4;
        inline constexpr std::uint32_t saturn = 1U << 5;
        inline constexpr std::uint32_t c64 = 1U << 6;
        inline constexpr std::uint32_t snes = 1U << 7;
        inline constexpr std::uint32_t nes = 1U << 8;
    } // namespace host

    // Each device exposes the same shape so frontends and adapters can pick,
    // describe, and route them uniformly.
    struct info final {
        const char* manufacturer; // "Sega", "Konami", ...
        const char* part_number;  // "MK-1650", "MK-1653", ...
        const char* family;       // "Mega Drive Control Pad", ...
        kind category;
        std::uint32_t compatible; // bitwise-OR of host:: constants
    };

    // Common port-side interface. The system's MMIO routes byte writes/reads
    // for the port the device is plugged into through these. Devices that
    // need per-frame state (timeouts, animation) hook on_vblank(). The
    // frontend pushes input through apply_state -- each device picks the
    // controller_state fields its hardware exposes and is responsible for
    // its own internal bit layout, so adapters never hand-craft pad bytes.
    class device {
      public:
        virtual ~device() = default;

        [[nodiscard]] virtual info describe() const noexcept = 0;

        // CPU-side writes to the device's data byte (e.g. $A10003 on Genesis
        // for port 1). The device latches whichever bits are configured as
        // outputs in its current direction state.
        virtual void write_data(std::uint8_t value) noexcept = 0;

        // CPU-side reads of the device's data byte: the live wire state.
        [[nodiscard]] virtual std::uint8_t read_data() const noexcept = 0;

        // Called by the host system at V-blank entry; default no-op. Devices
        // with phase/timeout state (the 6-button pad, future analog devices)
        // use it to reset internal counters that would otherwise drift past
        // the real-hardware timeout.
        virtual void on_vblank() noexcept {}

        // Frontend-pushed input snapshot. Default no-op for devices whose
        // state is set by other means (e.g. cartridge-side hardware).
        virtual void apply_state(const controller_state& /*state*/) noexcept {}
    };

} // namespace mnemos::peripheral
