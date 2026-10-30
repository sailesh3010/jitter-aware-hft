#pragma once
/// @file socket_transport.h
/// @brief Cross-platform POSIX / Winsock UDP socket abstraction for real network testing.

#include "common/types.h"
#include <cstdint>
#include <string>
#include <vector>
#include <optional>

#if defined(_WIN32) || defined(_WIN64)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <winsock2.h>
    #include <ws2tcpip.h>
    using SocketHandle = SOCKET;
    constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <fcntl.h>
    using SocketHandle = int;
    constexpr SocketHandle kInvalidSocket = -1;
#endif

namespace njt {

/// Cross-platform initialization helper (WSAStartup / WSACleanup on Windows).
class SocketSubsystem {
public:
    static bool initialize();
    static void cleanup();
};

/// High-performance non-blocking UDP socket.
class UdpSocket {
public:
    UdpSocket();
    ~UdpSocket();

    // Non-copyable
    UdpSocket(const UdpSocket&)            = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;

    // Movable
    UdpSocket(UdpSocket&& other) noexcept;
    UdpSocket& operator=(UdpSocket&& other) noexcept;

    /// Bind to a local port (e.g. 9876). Pass 0 to let OS assign an ephemeral port.
    bool bind(uint16_t port);

    /// Send a datagram to target IP and port.
    bool sendTo(const std::string& ip, uint16_t port, const void* data, size_t size);

    /// Non-blocking receive with optional timeout in microseconds (0 = poll immediately).
    /// Returns number of bytes received, or -1 on timeout/error.
    int receiveFrom(void* buffer, size_t max_size, uint64_t timeout_us = 0,
                    std::string* sender_ip = nullptr, uint16_t* sender_port = nullptr);

    [[nodiscard]] bool isValid() const noexcept { return socket_ != kInvalidSocket; }
    void close();

private:
    SocketHandle socket_ = kInvalidSocket;
};

} // namespace njt
