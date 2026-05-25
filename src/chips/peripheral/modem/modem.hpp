#pragma once

#include "chip.hpp"

#include <array>
#include <cstdint>
#include <string>

namespace mnemos::chips::peripheral {

    // The DCE's link to the outside world: a loopback for tests, or a live TCP
    // backend (see tcp_transport). A modem with no transport — or one that never
    // connects — reports NO CARRIER on dial. send/recv take raw buffers so the
    // interface sits naturally on a socket.
    class modem_transport {
      public:
        virtual ~modem_transport() = default;
        [[nodiscard]] virtual bool connect(const std::string& host, std::uint16_t port) = 0;
        virtual void disconnect() = 0;
        virtual int send(const std::uint8_t* data, int len) = 0;
        virtual int recv(std::uint8_t* out, int max) = 0;
        [[nodiscard]] virtual bool is_connected() const = 0;
    };

    // Hayes "AT" command modem core (ported from the Emu reference per ADR 0006).
    //
    // The C64 talks to the modem as DTE over a serial line: it writes bytes
    // (dte_write) and reads result codes / received data back (dte_read). In
    // COMMAND mode bytes accumulate into "AT" command lines (terminated by CR)
    // that the modem parses — dial, hang up, configure — replying with result
    // codes (OK / CONNECT / NO CARRIER / ERROR / ...). In ONLINE mode the modem
    // is a transparent pipe to the remote peer; the guarded "+++" escape drops
    // back to COMMAND mode without hanging up.
    //
    // The core is transport-agnostic and I/O-free, so it is fully deterministic
    // and unit-tested. The bit-level CIA-2 userport bridge (rs232) and the live
    // TCP backend are separate pieces. As an iperipheral the modem is ticked by
    // the scheduler: tick advances the escape-guard timer (one guard tick per
    // guard_divider cycles) and pumps the link via poll().
    class modem final : public iperipheral {
      public:
        enum class mode : std::uint8_t { command, online };

        // Result codes; the numeric value is what ATV0 emits.
        enum class result : std::uint8_t {
            ok = 0,
            connect = 1,
            ring = 2,
            no_carrier = 3,
            error = 4,
            no_dialtone = 6,
            busy = 7,
            no_answer = 8,
        };

        static constexpr std::size_t cmdbuf_len = 80U;
        static constexpr std::size_t outbuf_len = 512U; // DCE -> DTE ring
        static constexpr std::size_t num_sreg = 28U;
        static constexpr std::uint16_t default_telnet_port = 23U;

        // S-register indices we model.
        static constexpr std::size_t sreg_autoanswer = 0U; // S0: rings before auto-answer
        static constexpr std::size_t sreg_escape = 2U;     // S2: escape char ('+' = 43)
        static constexpr std::size_t sreg_cr = 3U;         // S3: carriage return (13)
        static constexpr std::size_t sreg_lf = 4U;         // S4: line feed (10)
        static constexpr std::size_t sreg_bs = 5U;         // S5: backspace (8)
        static constexpr std::size_t sreg_guard = 12U;     // S12: escape guard (ticks)

        // Default guard cadence: one escape-guard tick per PAL frame, so the S12
        // default of 50 is roughly a one-second guard. Overridden at assembly.
        static constexpr std::uint64_t default_guard_divider = 19705U;

        modem() { reset(reset_kind::power_on); }

        // The transport is borrowed; the owner (CLI / test) outlives the modem.
        void set_transport(modem_transport* xport) noexcept { xport_ = xport; }

        // How many scheduler cycles make up one escape-guard tick (S12 unit).
        void set_guard_divider(std::uint64_t cycles) noexcept {
            guard_divider_ = cycles == 0U ? 1U : cycles;
        }

        [[nodiscard]] chip_metadata metadata() const noexcept override;
        void tick(std::uint64_t cycles) override;
        // ATZ / power-on defaults: command mode, echo+verbose on, default
        // S-registers, buffers cleared, line dropped (disconnecting if up).
        void reset(reset_kind kind) override;

        void save_state(state_writer& writer) const override;
        void load_state(state_reader& reader) override;

        [[nodiscard]] instrumentation::ichip_introspection& introspection() noexcept override;

        // A byte travelled from the C64 (DTE) to the modem (DCE).
        void dte_write(std::uint8_t byte);
        // Pop one DCE -> DTE byte into out; false when the queue is empty.
        [[nodiscard]] bool dte_read(std::uint8_t& out);
        [[nodiscard]] int dte_available() const noexcept { return out_count_; }

        // Pump the link: surface peer bytes, notice a dropped connection, and
        // finish/abort a pending "+++" escape once its guard time elapses.
        void poll();

        [[nodiscard]] bool is_connected() const noexcept { return connected_; }
        [[nodiscard]] mode current_mode() const noexcept { return mode_; }
        [[nodiscard]] std::uint8_t sreg(std::size_t index) const noexcept {
            return index < num_sreg ? sreg_[index] : 0U;
        }

      private:
        class introspection_surface final : public instrumentation::ichip_introspection {};

        void out_push(std::uint8_t b) noexcept;
        void out_push_str(const char* s) noexcept;
        void out_push_num3(std::uint8_t v) noexcept;
        void emit_result(result code) noexcept;
        void transport_send_byte(std::uint8_t b);
        void do_hangup();
        void do_dial(const char* p);
        void apply_factory_defaults() noexcept;
        // Returns 0 = emit OK, 1 = emit ERROR, 2 = a response was already emitted.
        [[nodiscard]] int parse_at_body(const char* p);
        void parse_command();
        void handle_online_byte(std::uint8_t byte, std::uint32_t idle_before);

        modem_transport* xport_{};

        mode mode_{mode::command};
        bool echo_{true};
        bool verbose_{true};
        bool quiet_{false};
        std::array<std::uint8_t, num_sreg> sreg_{};

        std::array<char, cmdbuf_len> cmdbuf_{};
        int cmdlen_{};

        std::array<std::uint8_t, outbuf_len> outbuf_{};
        int out_head_{};
        int out_tail_{};
        int out_count_{};

        bool connected_{};
        int plus_count_{};           // consecutive escape chars seen (online)
        std::uint32_t idle_ticks_{}; // guard ticks since the last DTE byte
        std::uint64_t accum_{};      // scheduler cycles toward the next guard tick
        std::uint64_t guard_divider_{default_guard_divider};

        introspection_surface introspection_{};
    };

    // A deterministic in-memory transport: bytes "sent" are echoed straight back
    // as received bytes. Used by the unit tests and as a harmless default so an
    // ATDT to anything "connects" to its own echo.
    class loopback_transport final : public modem_transport {
      public:
        [[nodiscard]] bool connect(const std::string& host, std::uint16_t port) override;
        void disconnect() override;
        int send(const std::uint8_t* data, int len) override;
        int recv(std::uint8_t* out, int max) override;
        [[nodiscard]] bool is_connected() const override { return connected_; }

        [[nodiscard]] const std::string& last_host() const noexcept { return last_host_; }
        [[nodiscard]] std::uint16_t last_port() const noexcept { return last_port_; }

      private:
        bool connected_{};
        std::string last_host_;
        std::uint16_t last_port_{};
        std::string queue_; // sent bytes waiting to be echoed back
    };

    // A live TCP backend so the modem can dial real Telnet/BBS hosts. It opens a
    // blocking connect, then switches the socket to non-blocking so recv() returns
    // immediately with whatever is buffered (0 = nothing right now). Platform
    // sockets (Winsock on Windows, BSD sockets elsewhere); it touches the network,
    // so it is compiled on every platform but not exercised by CI.
    class tcp_transport final : public modem_transport {
      public:
        tcp_transport() = default;
        ~tcp_transport() override;
        tcp_transport(const tcp_transport&) = delete;
        tcp_transport& operator=(const tcp_transport&) = delete;
        tcp_transport(tcp_transport&&) = delete;
        tcp_transport& operator=(tcp_transport&&) = delete;

        [[nodiscard]] bool connect(const std::string& host, std::uint16_t port) override;
        void disconnect() override;
        int send(const std::uint8_t* data, int len) override;
        int recv(std::uint8_t* out, int max) override;
        [[nodiscard]] bool is_connected() const override { return connected_; }

      private:
        std::intptr_t fd_{-1}; // SOCKET (Windows) / int fd (POSIX); -1 = none
        bool connected_{};
    };

} // namespace mnemos::chips::peripheral
