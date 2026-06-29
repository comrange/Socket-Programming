#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <winsock2.h>
#include <ws2tcpip.h>

#include <cstddef>
#include <string>

namespace net {

class WsaSession {
public:
    WsaSession();
    ~WsaSession();

    WsaSession(const WsaSession&) = delete;
    WsaSession& operator=(const WsaSession&) = delete;
};

class Socket {
public:
    Socket() noexcept = default;
    explicit Socket(SOCKET handle) noexcept;
    ~Socket();

    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    Socket(Socket&& other) noexcept;
    Socket& operator=(Socket&& other) noexcept;

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] SOCKET get() const noexcept;
    void reset(SOCKET handle = INVALID_SOCKET) noexcept;

private:
    SOCKET handle_ = INVALID_SOCKET;
};

[[nodiscard]] std::string error_message(int error_code = WSAGetLastError());
[[nodiscard]] bool send_all(SOCKET socket, const char* data, std::size_t size);
[[nodiscard]] bool send_all(SOCKET socket, const std::string& data);

}  // namespace net
