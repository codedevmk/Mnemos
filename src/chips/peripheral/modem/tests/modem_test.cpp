#include <mnemos/chips/peripheral/modem.hpp>

#include <mnemos/chips/common/chip_registry.hpp>
#include <mnemos/chips/common/state.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace {
    using mnemos::chips::peripheral::loopback_transport;
    using mnemos::chips::peripheral::modem;
    using mnemos::chips::peripheral::modem_transport;
    using reset_kind = mnemos::chips::reset_kind;

    // A scriptable mock peer: records sent bytes + dial target, serves queued
    // bytes back, and can be told to fail connects or vanish mid-session.
    class mock_peer final : public modem_transport {
      public:
        bool connect(const std::string& host, std::uint16_t port) override {
            ++connect_calls;
            if (connect_fail) {
                return false;
            }
            last_host = host;
            last_port = port;
            connected = true;
            return true;
        }
        void disconnect() override {
            ++disconnect_calls;
            connected = false;
        }
        int send(const std::uint8_t* data, int len) override {
            for (int i = 0; i < len; ++i) {
                sent.push_back(data[i]);
            }
            return len;
        }
        int recv(std::uint8_t* out, int max) override {
            int n = 0;
            while (n < max && recv_head < to_recv.size()) {
                out[n++] = to_recv[recv_head++];
            }
            return n;
        }
        [[nodiscard]] bool is_connected() const override { return connected; }

        void queue_recv(const std::string& s) {
            to_recv.assign(s.begin(), s.end());
            recv_head = 0;
        }

        bool connected{};
        bool connect_fail{};
        std::vector<std::uint8_t> sent;
        std::vector<std::uint8_t> to_recv;
        std::size_t recv_head{};
        std::string last_host;
        std::uint16_t last_port{};
        int connect_calls{};
        int disconnect_calls{};
    };

    // Feed an ASCII string to the modem byte by byte (DTE -> DCE).
    void feed(modem& m, const std::string& s) {
        for (const char c : s) {
            m.dte_write(static_cast<std::uint8_t>(c));
        }
    }
    // Drain everything the modem has queued for the DTE into a string.
    std::string drain(modem& m) {
        std::string out;
        std::uint8_t b = 0U;
        while (m.dte_read(b)) {
            out.push_back(static_cast<char>(b));
        }
        return out;
    }
    // Advance the escape-guard timer by `ticks` guard units (divider == 1 cycle).
    void tick_guard(modem& m, std::uint32_t ticks) {
        m.tick(ticks); // set_guard_divider(1) makes one cycle == one guard tick
    }
} // namespace

TEST_CASE("modem replies OK to AT and honours echo", "[modem]") {
    mock_peer peer;
    modem m;
    m.set_transport(&peer);

    feed(m, "AT\r");
    std::string buf = drain(m);
    CHECK(buf.find("OK") != std::string::npos);
    CHECK(buf.find("AT") != std::string::npos); // echo on by default

    feed(m, "ATE0\r");
    (void)drain(m);
    feed(m, "AT\r");
    buf = drain(m);
    CHECK(buf.find("AT") == std::string::npos); // echo suppressed
    CHECK(buf.find("OK") != std::string::npos);
}

TEST_CASE("modem emits numeric result codes under ATV0", "[modem]") {
    mock_peer peer;
    modem m;
    m.set_transport(&peer);

    feed(m, "ATV0E0\r"); // numeric results + echo off
    (void)drain(m);
    feed(m, "AT\r");
    const std::string buf = drain(m);
    REQUIRE(buf.size() >= 1U);
    CHECK(buf[0] == '0');                       // numeric OK
    CHECK(buf.find('\r') != std::string::npos); // CR-terminated
}

TEST_CASE("modem dials and parses the host/port", "[modem]") {
    SECTION("explicit host:port") {
        mock_peer peer;
        modem m;
        m.set_transport(&peer);
        feed(m, "ATDT bbs.example.com:6400\r");
        const std::string buf = drain(m);
        CHECK(buf.find("CONNECT") != std::string::npos);
        CHECK(peer.connect_calls == 1);
        CHECK(peer.last_host == "bbs.example.com");
        CHECK(peer.last_port == 6400U);
        CHECK(m.is_connected());
    }
    SECTION("default Telnet port when none given") {
        mock_peer peer;
        modem m;
        m.set_transport(&peer);
        feed(m, "ATDThost.only\r");
        CHECK(peer.last_port == 23U);
    }
    SECTION("a failed connect reports NO CARRIER") {
        mock_peer peer;
        peer.connect_fail = true;
        modem m;
        m.set_transport(&peer);
        feed(m, "ATDT down\r");
        const std::string buf = drain(m);
        CHECK(buf.find("NO CARRIER") != std::string::npos);
        CHECK_FALSE(m.is_connected());
    }
}

TEST_CASE("modem passes data through in online mode", "[modem]") {
    mock_peer peer;
    modem m;
    m.set_transport(&peer);

    feed(m, "ATDT host\r");
    (void)drain(m);
    peer.sent.clear();

    feed(m, "HELLO"); // DTE -> peer
    REQUIRE(peer.sent.size() == 5U);
    CHECK(std::string(peer.sent.begin(), peer.sent.end()) == "HELLO");

    peer.queue_recv("WORLD"); // peer -> DTE
    m.poll();
    const std::string buf = drain(m);
    CHECK(buf.find("WORLD") != std::string::npos);
}

TEST_CASE("modem honours the guarded +++ escape", "[modem]") {
    mock_peer peer;
    modem m;
    m.set_transport(&peer);
    m.set_guard_divider(1U);

    feed(m, "ATDT host\r");
    (void)drain(m);
    peer.sent.clear();
    const std::uint32_t guard = m.sreg(modem::sreg_guard);

    // Guarded "+++": silence, then +++, then silence -> command mode.
    tick_guard(m, guard);
    m.dte_write('+');
    m.dte_write('+');
    m.dte_write('+');
    tick_guard(m, guard);
    m.poll();
    std::string buf = drain(m);
    CHECK(buf.find("OK") != std::string::npos);
    CHECK(peer.sent.empty()); // the +'s were not forwarded
    CHECK(m.is_connected());  // escape does not hang up
    CHECK(m.current_mode() == modem::mode::command);

    feed(m, "ATO\r"); // return online
    buf = drain(m);
    CHECK(buf.find("CONNECT") != std::string::npos);

    // Unguarded "+++" (no preceding silence) is forwarded as data.
    peer.sent.clear();
    m.dte_write('+'); // idle was reset by ATO's CR
    m.dte_write('+');
    m.dte_write('+');
    CHECK(peer.sent.size() == 3U);
}

TEST_CASE("modem hangs up and reports a dropped link", "[modem]") {
    mock_peer peer;
    modem m;
    m.set_transport(&peer);
    m.set_guard_divider(1U);

    feed(m, "ATDT host\r");
    (void)drain(m);

    const std::uint32_t guard = m.sreg(modem::sreg_guard);
    tick_guard(m, guard);
    feed(m, "+++");
    tick_guard(m, guard);
    m.poll();
    (void)drain(m);
    feed(m, "ATH\r");
    (void)drain(m);
    CHECK_FALSE(m.is_connected());
    CHECK(peer.disconnect_calls >= 1);

    feed(m, "ATDT host\r");
    (void)drain(m);
    peer.connected = false; // the peer vanished
    m.poll();
    const std::string buf = drain(m);
    CHECK(buf.find("NO CARRIER") != std::string::npos);
    CHECK_FALSE(m.is_connected());
}

TEST_CASE("modem sets and queries S-registers", "[modem]") {
    mock_peer peer;
    modem m;
    m.set_transport(&peer);

    feed(m, "ATS0=2\r");
    (void)drain(m);
    CHECK(m.sreg(0U) == 2U);

    feed(m, "ATS0?\r");
    std::string buf = drain(m);
    CHECK(buf.find("002") != std::string::npos);

    feed(m, "ATsomethingweird?\r");
    buf = drain(m);
    CHECK(buf.find("ERROR") != std::string::npos);
}

TEST_CASE("modem loopback transport echoes sent bytes", "[modem]") {
    loopback_transport loop;
    modem m;
    m.set_transport(&loop);

    feed(m, "ATDT echo.host:1234\r");
    (void)drain(m);
    CHECK(loop.last_host() == "echo.host");
    CHECK(loop.last_port() == 1234U);

    feed(m, "PING");
    m.poll(); // loopback serves the sent bytes straight back
    const std::string buf = drain(m);
    CHECK(buf.find("PING") != std::string::npos);
}

TEST_CASE("modem round-trips its state", "[modem]") {
    mock_peer peer;
    modem m;
    m.set_transport(&peer);
    feed(m, "ATE0V0\r");  // flip echo + verbose off
    feed(m, "ATS7=30\r"); // a non-default S-register
    (void)drain(m);

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    m.save_state(writer);

    modem restored;
    restored.set_transport(&peer);
    mnemos::chips::state_reader reader(blob);
    restored.load_state(reader);
    REQUIRE(reader.ok());

    CHECK(restored.sreg(7U) == 30U);
    // Echo + verbose were turned off, so a fresh AT replies with a bare numeric 0.
    feed(restored, "AT\r");
    std::string buf;
    std::uint8_t b = 0U;
    while (restored.dte_read(b)) {
        buf.push_back(static_cast<char>(b));
    }
    CHECK(buf.find("AT") == std::string::npos); // echo stayed off after restore
    CHECK(buf[0] == '0');                       // numeric result preserved
}

TEST_CASE("tcp_transport is safe before any connection", "[modem]") {
    // Network-free checks only: CI never dials a real host. This verifies the
    // socket backend links and behaves sanely while disconnected.
    mnemos::chips::peripheral::tcp_transport tcp;
    CHECK_FALSE(tcp.is_connected());

    std::array<std::uint8_t, 4> buf{};
    CHECK(tcp.recv(buf.data(), static_cast<int>(buf.size())) == 0); // nothing to read
    const std::uint8_t byte = 0x41U;
    CHECK(tcp.send(&byte, 1) == 0);    // dropped while disconnected
    CHECK_FALSE(tcp.connect("", 80U)); // empty host never dials
    tcp.disconnect();                  // idempotent / safe
    CHECK_FALSE(tcp.is_connected());
}

TEST_CASE("modem registers under generic.hayes_modem", "[modem]") {
    auto chip = mnemos::chips::create_chip("generic.hayes_modem");
    REQUIRE(chip != nullptr);
    CHECK(chip->metadata().family == std::string("modem"));
    CHECK(chip->metadata().klass == mnemos::chips::chip_class::peripheral);
}
