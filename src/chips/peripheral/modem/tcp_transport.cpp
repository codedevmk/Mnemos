#include "modem.hpp"

#include <cerrno>
#include <string>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <fcntl.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace mnemos::chips::peripheral {
    namespace {
#ifdef _WIN32
        using sock_t = SOCKET;
        constexpr sock_t k_invalid = INVALID_SOCKET;

        // Initialise Winsock once for the process (never torn down — fine for a CLI).
        bool ensure_winsock() {
            static const bool ok = []() {
                WSADATA data;
                return WSAStartup(MAKEWORD(2, 2), &data) == 0;
            }();
            return ok;
        }
        void close_sock(sock_t s) { closesocket(s); }
        void set_nonblocking(sock_t s) {
            u_long mode = 1U;
            (void)ioctlsocket(s, static_cast<long>(FIONBIO), &mode);
        }
        int do_send(sock_t s, const std::uint8_t* data, int len) {
            return ::send(s, reinterpret_cast<const char*>(data), len, 0);
        }
        int do_recv(sock_t s, std::uint8_t* out, int max) {
            return ::recv(s, reinterpret_cast<char*>(out), max, 0);
        }
        // A negative send/recv is transient only for would-block; everything
        // else (reset, aborted, net down) means the connection is gone.
        bool last_error_transient() {
            const int e = WSAGetLastError();
            return e == WSAEWOULDBLOCK || e == WSAEINTR;
        }
#else
        using sock_t = int;
        constexpr sock_t k_invalid = -1;

        void close_sock(sock_t s) { ::close(s); }
        void set_nonblocking(sock_t s) {
            const int flags = ::fcntl(s, F_GETFL, 0);
            (void)::fcntl(s, F_SETFL, flags | O_NONBLOCK);
        }
        int do_send(sock_t s, const std::uint8_t* data, int len) {
            return static_cast<int>(::send(s, data, static_cast<std::size_t>(len), MSG_NOSIGNAL));
        }
        int do_recv(sock_t s, std::uint8_t* out, int max) {
            return static_cast<int>(::recv(s, out, static_cast<std::size_t>(max), 0));
        }
        // A negative send/recv is transient only for would-block; everything
        // else (ECONNRESET, EPIPE, net down) means the connection is gone.
        bool last_error_transient() {
            return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
        }
#endif
    } // namespace

    tcp_transport::~tcp_transport() { disconnect(); }

    bool tcp_transport::connect(const std::string& host, std::uint16_t port) {
        disconnect();
        if (host.empty()) {
            return false;
        }
#ifdef _WIN32
        if (!ensure_winsock()) {
            return false;
        }
#endif
        addrinfo hints{};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        addrinfo* res = nullptr;
        const std::string port_str = std::to_string(port);
        if (::getaddrinfo(host.c_str(), port_str.c_str(), &hints, &res) != 0 || res == nullptr) {
            return false;
        }

        sock_t fd = k_invalid;
        for (addrinfo* ai = res; ai != nullptr; ai = ai->ai_next) {
            const sock_t s = ::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
            if (s == k_invalid) {
                continue;
            }
            if (::connect(s, ai->ai_addr, static_cast<socklen_t>(ai->ai_addrlen)) == 0) {
                fd = s;
                break;
            }
            close_sock(s);
        }
        ::freeaddrinfo(res);
        if (fd == k_invalid) {
            return false;
        }

        set_nonblocking(fd);
        fd_ = static_cast<std::intptr_t>(fd);
        connected_ = true;
        return true;
    }

    void tcp_transport::disconnect() {
        if (fd_ != -1) {
            close_sock(static_cast<sock_t>(fd_));
            fd_ = -1;
        }
        connected_ = false;
    }

    int tcp_transport::send(const std::uint8_t* data, int len) {
        if (!connected_ || fd_ == -1 || len <= 0) {
            return 0;
        }
        const int n = do_send(static_cast<sock_t>(fd_), data, len);
        if (n < 0 && !last_error_transient()) {
            connected_ = false; // hard error: the modem must see carrier drop
        }
        return n > 0 ? n : 0; // would-block sends nothing now
    }

    int tcp_transport::recv(std::uint8_t* out, int max) {
        if (!connected_ || fd_ == -1 || max <= 0) {
            return 0;
        }
        const int n = do_recv(static_cast<sock_t>(fd_), out, max);
        if (n == 0) {
            connected_ = false; // an orderly peer close
            return 0;
        }
        if (n < 0 && !last_error_transient()) {
            connected_ = false; // hard error (reset/abort): drop carrier
        }
        return n > 0 ? n : 0; // n < 0 transient: nothing buffered right now
    }

} // namespace mnemos::chips::peripheral
