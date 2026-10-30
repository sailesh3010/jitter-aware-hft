/// @file socket_transport.cpp
/// @brief Cross-platform POSIX / Winsock UDP implementation.

#include "network/socket_transport.h"
#include <cstring>
#include <iostream>

namespace njt {

bool SocketSubsystem::initialize() {
#if defined(_WIN32) || defined(_WIN64)
    WSADATA wsa;
    int res = WSAStartup(MAKEWORD(2, 2), &wsa);
    return res == 0;
#else
    return true;
#endif
}

void SocketSubsystem::cleanup() {
#if defined(_WIN32) || defined(_WIN64)
    WSACleanup();
#endif
}

UdpSocket::UdpSocket() {
    SocketSubsystem::initialize();
    socket_ = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket_ != kInvalidSocket) {
        // Allow reuse
        int opt = 1;
#if defined(_WIN32) || defined(_WIN64)
        setsockopt(socket_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt));
#else
        setsockopt(socket_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif
    }
}

UdpSocket::~UdpSocket() {
    close();
}

UdpSocket::UdpSocket(UdpSocket&& other) noexcept : socket_(other.socket_) {
    other.socket_ = kInvalidSocket;
}

UdpSocket& UdpSocket::operator=(UdpSocket&& other) noexcept {
    if (this != &other) {
        close();
        socket_ = other.socket_;
        other.socket_ = kInvalidSocket;
    }
    return *this;
}

bool UdpSocket::bind(uint16_t port) {
    if (socket_ == kInvalidSocket) return false;

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

    int res = ::bind(socket_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    return res == 0;
}

bool UdpSocket::sendTo(const std::string& ip, uint16_t port, const void* data, size_t size) {
    if (socket_ == kInvalidSocket) return false;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port);
    inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);

    auto sent = ::sendto(socket_, reinterpret_cast<const char*>(data),
                         static_cast<int>(size), 0,
                         reinterpret_cast<sockaddr*>(&addr), sizeof(addr));

    return sent == static_cast<int>(size);
}

int UdpSocket::receiveFrom(void* buffer, size_t max_size, uint64_t timeout_us,
                           std::string* sender_ip, uint16_t* sender_port)
{
    if (socket_ == kInvalidSocket) return -1;

    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(socket_, &read_fds);

    timeval tv{};
    tv.tv_sec  = static_cast<long>(timeout_us / 1'000'000ULL);
    tv.tv_usec = static_cast<long>(timeout_us % 1'000'000ULL);

    int sel = ::select(static_cast<int>(socket_ + 1), &read_fds, nullptr, nullptr, &tv);
    if (sel <= 0) return -1; // timeout or error

    sockaddr_in from_addr{};
    socklen_t from_len = sizeof(from_addr);

    int bytes = ::recvfrom(socket_, reinterpret_cast<char*>(buffer),
                           static_cast<int>(max_size), 0,
                           reinterpret_cast<sockaddr*>(&from_addr), &from_len);

    if (bytes > 0) {
        if (sender_ip) {
            char ip_buf[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &from_addr.sin_addr, ip_buf, sizeof(ip_buf));
            *sender_ip = ip_buf;
        }
        if (sender_port) {
            *sender_port = ntohs(from_addr.sin_port);
        }
    }

    return bytes;
}

void UdpSocket::close() {
    if (socket_ != kInvalidSocket) {
#if defined(_WIN32) || defined(_WIN64)
        ::closesocket(socket_);
#else
        ::close(socket_);
#endif
        socket_ = kInvalidSocket;
    }
}

} // namespace njt
