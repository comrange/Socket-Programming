#include "common/socket_support.h"

#include <array>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

constexpr std::size_t kMaxResponseSize = 10 * 1024 * 1024;

std::uint16_t parse_port(const char* value) {
    try {
        const unsigned long port = std::stoul(value);
        if (port == 0 || port > 65535) {
            throw std::out_of_range("port");
        }
        return static_cast<std::uint16_t>(port);
    } catch (...) {
        throw std::runtime_error("Port must be a number from 1 to 65535.");
    }
}

net::Socket connect_to_server(const std::string& host, std::uint16_t port) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    addrinfo* addresses = nullptr;
    const std::string port_text = std::to_string(port);
    const int result = getaddrinfo(host.c_str(), port_text.c_str(), &hints, &addresses);
    if (result != 0) {
        throw std::runtime_error("Cannot resolve host: " + std::string(gai_strerrorA(result)));
    }

    net::Socket connected;
    for (addrinfo* address = addresses; address != nullptr; address = address->ai_next) {
        net::Socket candidate(socket(address->ai_family, address->ai_socktype, address->ai_protocol));
        if (!candidate.valid()) {
            continue;
        }
        if (connect(candidate.get(), address->ai_addr, static_cast<int>(address->ai_addrlen)) == 0) {
            connected = std::move(candidate);
            break;
        }
    }
    freeaddrinfo(addresses);

    if (!connected.valid()) {
        throw std::runtime_error("Cannot connect to " + host + ':' + port_text + ": " +
                                 net::error_message());
    }
    return connected;
}

std::string receive_response(SOCKET socket) {
    std::string response;
    std::array<char, 4096> buffer{};
    while (true) {
        const int received = recv(socket, buffer.data(), static_cast<int>(buffer.size()), 0);
        if (received == 0) {
            break;
        }
        if (received == SOCKET_ERROR) {
            throw std::runtime_error("Receive failed: " + net::error_message());
        }
        response.append(buffer.data(), static_cast<std::size_t>(received));
        if (response.size() > kMaxResponseSize) {
            throw std::runtime_error("Response exceeds the 10 MB demo limit.");
        }
    }
    return response;
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc != 4) {
        std::cerr << "Usage: socket_client <host> <port> <path>\n"
                  << "Example: socket_client 127.0.0.1 8080 /index.html\n";
        return 1;
    }

    try {
        const std::string host = argv[1];
        const std::uint16_t port = parse_port(argv[2]);
        std::string path = argv[3];
        if (path.empty() || path.front() != '/') {
            path.insert(path.begin(), '/');
        }

        const net::WsaSession winsock;
        net::Socket server = connect_to_server(host, port);

        std::ostringstream request;
        request << "GET " << path << " HTTP/1.1\r\n"
                << "Host: " << host << ':' << port << "\r\n"
                << "User-Agent: SocketDemo-Client/1.0\r\n"
                << "Accept: */*\r\n"
                << "Connection: close\r\n\r\n";

        std::cout << "[CLIENT] Connected to " << host << ':' << port << '\n'
                  << "[CLIENT] Sending GET " << path << "\n\n";
        if (!net::send_all(server.get(), request.str())) {
            throw std::runtime_error("Send failed: " + net::error_message());
        }

        const std::string response = receive_response(server.get());
        const std::size_t header_end = response.find("\r\n\r\n");
        if (header_end == std::string::npos) {
            throw std::runtime_error("Server returned an invalid HTTP response.");
        }

        std::cout << "===== RESPONSE HEADERS =====\n"
                  << response.substr(0, header_end) << "\n\n"
                  << "===== RESPONSE BODY =====\n";
        std::cout.write(response.data() + header_end + 4,
                        static_cast<std::streamsize>(response.size() - header_end - 4));
        if (response.empty() || response.back() != '\n') {
            std::cout << '\n';
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[ERROR] " << error.what() << '\n';
        return 1;
    }
}
