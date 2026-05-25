#include <mnemos/chips/peripheral/modem.hpp>

#include <mnemos/chips/common/chip_registry.hpp>
#include <mnemos/chips/common/state.hpp>

#include <cctype>
#include <cstdlib>
#include <memory>
#include <span>

namespace mnemos::chips::peripheral {
    namespace {
        [[nodiscard]] char up(char c) noexcept {
            return static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }

        // Read a single decimal digit at *p, advancing past it; return dflt if the
        // next char is not a digit. Often called only for the pointer advance.
        int read_digit(const char*& p, int dflt) noexcept {
            if (*p >= '0' && *p <= '9') {
                const int v = *p - '0';
                ++p;
                return v;
            }
            return dflt;
        }

        [[nodiscard]] const char* result_text(modem::result code) noexcept {
            switch (code) {
            case modem::result::ok:
                return "OK";
            case modem::result::connect:
                return "CONNECT";
            case modem::result::ring:
                return "RING";
            case modem::result::no_carrier:
                return "NO CARRIER";
            case modem::result::error:
                return "ERROR";
            case modem::result::no_dialtone:
                return "NO DIALTONE";
            case modem::result::busy:
                return "BUSY";
            case modem::result::no_answer:
                return "NO ANSWER";
            }
            return "ERROR";
        }
    } // namespace

    chip_metadata modem::metadata() const noexcept {
        return {
            .manufacturer = "Generic",
            .part_number = "Hayes",
            .family = "modem",
            .klass = chip_class::peripheral,
            .revision = 1U,
        };
    }

    // ----- DCE -> DTE output ring -----

    void modem::out_push(std::uint8_t b) noexcept {
        if (out_count_ >= static_cast<int>(outbuf_len)) {
            return; // drop on overflow
        }
        outbuf_[static_cast<std::size_t>(out_head_)] = b;
        out_head_ = (out_head_ + 1) % static_cast<int>(outbuf_len);
        ++out_count_;
    }

    void modem::out_push_str(const char* s) noexcept {
        while (*s != '\0') {
            out_push(static_cast<std::uint8_t>(*s++));
        }
    }

    void modem::out_push_num3(std::uint8_t v) noexcept {
        out_push(static_cast<std::uint8_t>('0' + (v / 100U) % 10U));
        out_push(static_cast<std::uint8_t>('0' + (v / 10U) % 10U));
        out_push(static_cast<std::uint8_t>('0' + v % 10U));
    }

    void modem::emit_result(result code) noexcept {
        if (quiet_) {
            return;
        }
        const std::uint8_t cr = sreg_[sreg_cr];
        const std::uint8_t lf = sreg_[sreg_lf];
        if (verbose_) {
            out_push(cr);
            out_push(lf);
            out_push_str(result_text(code));
            out_push(cr);
            out_push(lf);
        } else {
            out_push(static_cast<std::uint8_t>('0' + static_cast<int>(code))); // single-digit
            out_push(cr);
        }
    }

    // ----- transport helpers -----

    void modem::transport_send_byte(std::uint8_t b) {
        if (connected_ && xport_ != nullptr) {
            (void)xport_->send(&b, 1);
        }
    }

    void modem::do_hangup() {
        if (connected_ && xport_ != nullptr) {
            xport_->disconnect();
        }
        connected_ = false;
        mode_ = mode::command;
        plus_count_ = 0;
    }

    // ----- lifecycle -----

    void modem::reset(reset_kind /*kind*/) {
        if (connected_ && xport_ != nullptr) {
            xport_->disconnect();
        }

        mode_ = mode::command;
        echo_ = true;
        verbose_ = true;
        quiet_ = false;

        sreg_.fill(0U);
        sreg_[sreg_escape] = 43U; // '+'
        sreg_[sreg_cr] = 13U;
        sreg_[sreg_lf] = 10U;
        sreg_[sreg_bs] = 8U;
        sreg_[sreg_guard] = 50U; // escape guard (tick units)

        cmdlen_ = 0;
        out_head_ = 0;
        out_tail_ = 0;
        out_count_ = 0;
        connected_ = false;
        plus_count_ = 0;
        idle_ticks_ = 0U;
        accum_ = 0U;
    }

    // ----- command-mode parsing -----

    void modem::apply_factory_defaults() noexcept {
        echo_ = true;
        verbose_ = true;
        quiet_ = false;
        sreg_[sreg_escape] = 43U;
        sreg_[sreg_guard] = 50U;
    }

    // Dial: connect to host[:port] after an optional D[T|P] modifier. The T/P
    // (tone/pulse) modifier is only the single char immediately after 'D' — we
    // must not strip a leading t/p/w from a hostname like "telnet.bbs".
    void modem::do_dial(const char* p) {
        if (up(*p) == 'T' || up(*p) == 'P') {
            ++p; // dial-type modifier
        }
        while (*p == ' ' || *p == ',') {
            ++p; // spaces / pause modifiers
        }

        std::string host;
        while (*p != '\0' && *p != ':') {
            host.push_back(*p++);
        }
        while (!host.empty() && host.back() == ' ') {
            host.pop_back(); // trim trailing spaces
        }

        std::uint16_t port = default_telnet_port;
        if (*p == ':') {
            port = static_cast<std::uint16_t>(std::atoi(p + 1));
        }

        if (host.empty()) {
            emit_result(result::no_carrier);
            return;
        }

        const bool ok = xport_ != nullptr && xport_->connect(host, port);
        if (ok) {
            connected_ = true;
            mode_ = mode::online;
            plus_count_ = 0;
            emit_result(result::connect);
        } else {
            connected_ = false;
            emit_result(result::no_carrier);
        }
    }

    int modem::parse_at_body(const char* p) {
        while (*p != '\0') {
            const char c = up(*p++);
            switch (c) {
            case ' ':
                break;
            case 'E':
                echo_ = read_digit(p, 0) != 0;
                break;
            case 'V':
                verbose_ = read_digit(p, 1) != 0;
                break;
            case 'Q':
                quiet_ = read_digit(p, 0) != 0;
                break;
            case 'H':
                read_digit(p, 0);
                do_hangup();
                break;
            case 'Z':
                read_digit(p, 0);
                reset(reset_kind::soft);
                return 0;
            case 'O':
                read_digit(p, 0);
                if (connected_) {
                    mode_ = mode::online;
                    emit_result(result::connect);
                } else {
                    emit_result(result::error);
                }
                return 2;
            case 'A': // answer — no incoming call modelled
                emit_result(result::no_carrier);
                return 2;
            case 'D': // dial consumes the rest of the line
                do_dial(p);
                return 2;
            case 'S': { // Sn=v / Sn?
                int reg = 0;
                while (*p >= '0' && *p <= '9') {
                    reg = reg * 10 + (*p - '0');
                    ++p;
                }
                if (*p == '=') {
                    ++p;
                    int val = 0;
                    while (*p >= '0' && *p <= '9') {
                        val = val * 10 + (*p - '0');
                        ++p;
                    }
                    if (reg >= 0 && reg < static_cast<int>(num_sreg)) {
                        sreg_[static_cast<std::size_t>(reg)] = static_cast<std::uint8_t>(val);
                    } else {
                        return 1;
                    }
                } else if (*p == '?') {
                    ++p;
                    if (reg >= 0 && reg < static_cast<int>(num_sreg)) {
                        out_push_num3(sreg_[static_cast<std::size_t>(reg)]);
                        out_push(sreg_[sreg_cr]);
                        out_push(sreg_[sreg_lf]);
                    } else {
                        return 1;
                    }
                } else {
                    return 1;
                }
                break;
            }
            case '&': { // extended: &F factory defaults, others ignored
                const char c2 = up(*p);
                if (*p != '\0') {
                    ++p;
                }
                if (c2 == 'F') {
                    apply_factory_defaults();
                }
                read_digit(p, 0);
                break;
            }
            // Accepted-but-ignored knobs (speaker, result-set, etc.).
            case 'I':
            case 'L':
            case 'M':
            case 'X':
            case 'B':
            case 'N':
            case 'W':
            case 'P':
            case 'T':
                read_digit(p, 0);
                break;
            default:
                return 1; // unknown command -> ERROR
            }
        }
        return 0; // whole line parsed -> OK
    }

    void modem::parse_command() {
        const char* s = cmdbuf_.data();
        while (*s == ' ') {
            ++s;
        }
        if (s[0] == '\0') {
            return; // empty line -> no response
        }
        if (up(s[0]) != 'A' || up(s[1]) != 'T') {
            emit_result(result::error);
            return;
        }
        const int r = parse_at_body(s + 2);
        if (r == 0) {
            emit_result(result::ok);
        } else if (r == 1) {
            emit_result(result::error);
        }
        // r == 2: a command already emitted its own result.
    }

    // ----- online-mode byte handling -----

    void modem::handle_online_byte(std::uint8_t byte, std::uint32_t idle_before) {
        const std::uint8_t esc = sreg_[sreg_escape];
        const std::uint32_t guard = sreg_[sreg_guard];

        if (byte == esc) {
            if (plus_count_ == 0) {
                // The first escape char only counts after a guard-time of silence.
                if (idle_before >= guard) {
                    plus_count_ = 1;
                    return;
                }
                transport_send_byte(byte);
                return;
            }
            if (plus_count_ < 3) {
                ++plus_count_;
                return;
            }
            // A 4th escape char: the run is data after all — flush + forward.
            for (int i = 0; i < plus_count_; ++i) {
                transport_send_byte(esc);
            }
            plus_count_ = 0;
            transport_send_byte(byte);
            return;
        }

        // Non-escape byte: any buffered escape chars were data — flush them.
        if (plus_count_ > 0) {
            for (int i = 0; i < plus_count_; ++i) {
                transport_send_byte(esc);
            }
            plus_count_ = 0;
        }
        transport_send_byte(byte);
    }

    // ----- public DTE interface -----

    void modem::dte_write(std::uint8_t byte) {
        const std::uint32_t idle_before = idle_ticks_;
        idle_ticks_ = 0U; // any DTE byte resets the guard
        accum_ = 0U;

        if (mode_ == mode::online) {
            handle_online_byte(byte, idle_before);
            return;
        }

        // Command mode.
        const std::uint8_t cr = sreg_[sreg_cr];
        const std::uint8_t bs = sreg_[sreg_bs];
        const std::uint8_t lf = sreg_[sreg_lf];

        if (echo_) {
            out_push(byte);
        }

        if (byte == cr) {
            cmdbuf_[static_cast<std::size_t>(cmdlen_)] = '\0';
            parse_command();
            cmdlen_ = 0;
            return;
        }
        if (byte == bs) {
            if (cmdlen_ > 0) {
                --cmdlen_;
            }
            return;
        }
        if (byte == lf) {
            return; // ignore LF inside a command line
        }
        if (cmdlen_ < static_cast<int>(cmdbuf_len) - 1) {
            cmdbuf_[static_cast<std::size_t>(cmdlen_++)] = static_cast<char>(byte);
        }
    }

    bool modem::dte_read(std::uint8_t& out) {
        if (out_count_ == 0) {
            return false;
        }
        out = outbuf_[static_cast<std::size_t>(out_tail_)];
        out_tail_ = (out_tail_ + 1) % static_cast<int>(outbuf_len);
        --out_count_;
        return true;
    }

    void modem::tick(std::uint64_t cycles) {
        accum_ += cycles;
        while (accum_ >= guard_divider_) {
            accum_ -= guard_divider_;
            if (idle_ticks_ < 0xFFFF0000U) { // saturate so the guard never wraps
                ++idle_ticks_;
            }
            poll();
        }
    }

    void modem::poll() {
        const std::uint32_t guard = sreg_[sreg_guard];

        // Complete a "+++" escape: 3 escape chars then guard silence.
        if (plus_count_ == 3 && idle_ticks_ >= guard) {
            plus_count_ = 0;
            mode_ = mode::command; // stay connected
            emit_result(result::ok);
            return;
        }
        // A partial run (1-2) that timed out was data — flush it.
        if (plus_count_ > 0 && plus_count_ < 3 && idle_ticks_ >= guard) {
            for (int i = 0; i < plus_count_; ++i) {
                transport_send_byte(sreg_[sreg_escape]);
            }
            plus_count_ = 0;
        }

        // Notice a dropped link.
        if (connected_ && xport_ != nullptr && !xport_->is_connected()) {
            connected_ = false;
            mode_ = mode::command;
            emit_result(result::no_carrier);
            return;
        }

        // Pull peer bytes into the DTE queue while online.
        if (mode_ == mode::online && connected_ && xport_ != nullptr) {
            std::array<std::uint8_t, 64> tmp{};
            const int n = xport_->recv(tmp.data(), static_cast<int>(tmp.size()));
            for (int i = 0; i < n; ++i) {
                out_push(tmp[static_cast<std::size_t>(i)]);
            }
        }
    }

    void modem::save_state(state_writer& writer) const {
        writer.u8(static_cast<std::uint8_t>(mode_));
        writer.boolean(echo_);
        writer.boolean(verbose_);
        writer.boolean(quiet_);
        writer.bytes(std::span<const std::uint8_t>(sreg_));
        writer.u32(static_cast<std::uint32_t>(cmdlen_));
        writer.bytes(std::span<const std::uint8_t>(
            reinterpret_cast<const std::uint8_t*>(cmdbuf_.data()), cmdbuf_.size()));
        writer.u32(static_cast<std::uint32_t>(out_head_));
        writer.u32(static_cast<std::uint32_t>(out_tail_));
        writer.u32(static_cast<std::uint32_t>(out_count_));
        writer.bytes(std::span<const std::uint8_t>(outbuf_));
        writer.boolean(connected_);
        writer.u32(static_cast<std::uint32_t>(plus_count_));
        writer.u32(idle_ticks_);
        writer.u64(accum_);
    }

    void modem::load_state(state_reader& reader) {
        mode_ = static_cast<mode>(reader.u8());
        echo_ = reader.boolean();
        verbose_ = reader.boolean();
        quiet_ = reader.boolean();
        reader.bytes(std::span<std::uint8_t>(sreg_));
        cmdlen_ = static_cast<int>(reader.u32());
        reader.bytes(std::span<std::uint8_t>(reinterpret_cast<std::uint8_t*>(cmdbuf_.data()),
                                             cmdbuf_.size()));
        out_head_ = static_cast<int>(reader.u32());
        out_tail_ = static_cast<int>(reader.u32());
        out_count_ = static_cast<int>(reader.u32());
        reader.bytes(std::span<std::uint8_t>(outbuf_));
        connected_ = reader.boolean();
        plus_count_ = static_cast<int>(reader.u32());
        idle_ticks_ = reader.u32();
        accum_ = reader.u64();
    }

    instrumentation::i_chip_introspection& modem::introspection() noexcept {
        return introspection_;
    }

    // ----- loopback transport -----

    bool loopback_transport::connect(const std::string& host, std::uint16_t port) {
        last_host_ = host;
        last_port_ = port;
        connected_ = true;
        queue_.clear();
        return true;
    }

    void loopback_transport::disconnect() {
        connected_ = false;
        queue_.clear();
    }

    int loopback_transport::send(const std::uint8_t* data, int len) {
        for (int i = 0; i < len; ++i) {
            queue_.push_back(static_cast<char>(data[i]));
        }
        return len;
    }

    int loopback_transport::recv(std::uint8_t* out, int max) {
        int n = 0;
        while (n < max && !queue_.empty()) {
            out[n++] = static_cast<std::uint8_t>(queue_.front());
            queue_.erase(queue_.begin());
        }
        return n;
    }

    namespace {
        [[maybe_unused]] const auto modem_registration =
            register_factory("generic.hayes_modem", chip_class::peripheral,
                             []() -> std::unique_ptr<i_chip> { return std::make_unique<modem>(); });
    } // namespace

} // namespace mnemos::chips::peripheral
