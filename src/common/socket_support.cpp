#include "common/socket_support.h"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace net {

WsaSession::WsaSession() {
    WSADATA data{};
    const int result = WSAStartup(MAKEWORD(2, 2), &data);
    if (result != 0) {
        throw std::runtime_error("WSAStartup failed: " + error_message(result));
    }

    if (LOBYTE(data.wVersion) != 2 || HIBYTE(data.wVersion) != 2) {
        WSACleanup();
        throw std::runtime_error("Winsock 2.2 is not available.");
    }
}

WsaSession::~WsaSession() {
    WSACleanup();
}

Socket::Socket(SOCKET handle) noexcept : handle_(handle) {}

Socket::~Socket() {
    reset();
}

Socket::Socket(Socket&& other) noexcept : handle_(std::exchange(other.handle_, INVALID_SOCKET)) {}

Socket& Socket::operator=(Socket&& other) noexcept {
    if (this != &other) {
        reset(std::exchange(other.handle_, INVALID_SOCKET));
    }
    return *this;
}

bool Socket::valid() const noexcept {
    return handle_ != INVALID_SOCKET;
}

SOCKET Socket::get() const noexcept {
    return handle_;
}

void Socket::reset(SOCKET handle) noexcept {
    if (handle_ != INVALID_SOCKET) {
        closesocket(handle_);
    }
    handle_ = handle;
}

std::string error_message(int error_code) {
    char* message = nullptr;
    const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                        FORMAT_MESSAGE_IGNORE_INSERTS;
    const DWORD length = FormatMessageA(flags, nullptr, static_cast<DWORD>(error_code), 0,
                                        reinterpret_cast<char*>(&message), 0, nullptr);
    if (length == 0 || message == nullptr) {
        return "Winsock error " + std::to_string(error_code);
    }

    std::string result(message, length);
    LocalFree(message);
    while (!result.empty() && (result.back() == '\r' || result.back() == '\n' || result.back() == ' ')) {
        result.pop_back();
    }
    return result;
}

bool send_all(SOCKET socket, const char* data, std::size_t size) {
    std::size_t sent = 0;
    while (sent < size) {
        const std::size_t remaining = size - sent;
        const int chunk_size = static_cast<int>(
            std::min(remaining, static_cast<std::size_t>(std::numeric_limits<int>::max())));
        const int result = send(socket, data + sent, chunk_size, 0);
        if (result == SOCKET_ERROR || result == 0) {
            return false;
        }
        sent += static_cast<std::size_t>(result);
    }
    return true;
}

bool send_all(SOCKET socket, const std::string& data) {
    return send_all(socket, data.data(), data.size());
}

}  // namespace net
