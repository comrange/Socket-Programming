#include "common/http.h"
#include "common/socket_support.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

class Logger {
public:
    explicit Logger(const std::filesystem::path& path) : output_(path, std::ios::app) {
        if (!output_) {
            throw std::runtime_error("Cannot open log file: " + path.string());
        }
    }

    void write(const std::string& category, const std::string& message) {
        const std::string line = timestamp() + " [" + category + "] " + message;
        std::lock_guard<std::mutex> lock(mutex_);
        std::cout << line << std::endl;
        output_ << line << std::endl;
    }

private:
    static std::string timestamp() {
        const auto now = std::chrono::system_clock::now();
        const std::time_t time = std::chrono::system_clock::to_time_t(now);
        std::tm local_time{};
        localtime_s(&local_time, &time);

        std::ostringstream text;
        text << std::put_time(&local_time, "%Y-%m-%d %H:%M:%S");
        return text.str();
    }

    std::mutex mutex_;
    std::ofstream output_;
};

struct ServerStats {
    std::atomic<std::uint64_t> total_requests{0};
    std::atomic<std::uint32_t> active_connections{0};
};

struct HeaderReadResult {
    bool complete = false;
    bool socket_error = false;
    std::string data;
};

HeaderReadResult read_header(SOCKET client) {
    HeaderReadResult result;
    std::array<char, 2048> buffer{};

    while (result.data.find("\r\n\r\n") == std::string::npos) {
        const int received = recv(client, buffer.data(), static_cast<int>(buffer.size()), 0);
        if (received == SOCKET_ERROR) {
            result.socket_error = true;
            return result;
        }
        if (received == 0) {
            return result;
        }

        result.data.append(buffer.data(), static_cast<std::size_t>(received));
        if (result.data.size() > http::kMaxHeaderSize) {
            return result;
        }
    }

    result.complete = true;
    return result;
}

bool send_response(SOCKET client,
                   int status,
                   const std::string& reason,
                   const std::string& content_type,
                   const std::vector<char>& body,
                   const std::string& extra_headers = {}) {
    const std::string header =
        http::response_header(status, reason, content_type, body.size(), extra_headers);
    return net::send_all(client, header) &&
           (body.empty() || net::send_all(client, body.data(), body.size()));
}

std::size_t send_text_response(SOCKET client,
                               int status,
                               const std::string& reason,
                               const std::string& message,
                               const std::string& extra_headers = {}) {
    const std::vector<char> body(message.begin(), message.end());
    send_response(client, status, reason, "text/plain; charset=utf-8", body, extra_headers);
    return body.size();
}

bool is_safe_target(const std::string& target) {
    return !target.empty() && target.front() == '/' && target.rfind("//", 0) != 0 &&
           target.find("..") == std::string::npos && target.find('\\') == std::string::npos &&
           target.find(':') == std::string::npos;
}

std::filesystem::path target_to_file(const std::filesystem::path& root, std::string target) {
    const std::size_t query = target.find_first_of("?#");
    if (query != std::string::npos) {
        target.erase(query);
    }
    if (target == "/") {
        target = "/index.html";
    }
    return root / std::filesystem::path(target.substr(1));
}

std::vector<char> read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return {};
    }
    return std::vector<char>(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

void handle_client(SOCKET client,
                   const std::filesystem::path& document_root,
                   const std::string& endpoint,
                   std::uint64_t request_id,
                   Logger& logger,
                   ServerStats& stats) {
    const auto started_at = std::chrono::steady_clock::now();
    std::string method = "-";
    std::string target = "-";

    const auto log_result = [&](int status, std::size_t bytes, const std::string& detail = {}) {
        const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started_at);
        std::ostringstream message;
        message << "id=" << request_id << " client=" << endpoint << " method=" << method
                << " path=" << target << " status=" << status << " bytes=" << bytes
                << " duration_ms=" << duration.count()
                << " active=" << stats.active_connections.load();
        if (!detail.empty()) {
            message << " detail=" << detail;
        }
        logger.write("REQUEST", message.str());
    };

    const HeaderReadResult header = read_header(client);
    if (header.socket_error) {
        log_result(0, 0, "receive_error");
        return;
    }
    if (!header.complete) {
        if (header.data.empty()) {
            log_result(0, 0, "closed_without_request");
        } else {
            const std::size_t bytes =
                send_text_response(client, 400, "Bad Request", "400 Bad Request\n");
            log_result(400, bytes, "incomplete_or_oversized_header");
        }
        return;
    }

    http::Request request;
    if (http::parse_request(header.data, request) != http::ParseStatus::ok) {
        const std::size_t bytes =
            send_text_response(client, 400, "Bad Request", "400 Bad Request\n");
        log_result(400, bytes, "malformed_request");
        return;
    }

    method = request.method;
    target = request.target;
    if (request.method != "GET") {
        const std::size_t bytes = send_text_response(client, 405, "Method Not Allowed",
                                                     "405 Method Not Allowed\n", "Allow: GET\r\n");
        log_result(405, bytes);
        return;
    }

    std::string clean_target = request.target;
    const std::size_t query = clean_target.find_first_of("?#");
    if (query != std::string::npos) {
        clean_target.erase(query);
    }
    if (!is_safe_target(clean_target)) {
        const std::size_t bytes =
            send_text_response(client, 400, "Bad Request", "400 Bad Request\n");
        log_result(400, bytes, "unsafe_path");
        return;
    }

    const std::filesystem::path file_path = target_to_file(document_root, clean_target);
    std::error_code filesystem_error;
    if (!std::filesystem::is_regular_file(file_path, filesystem_error)) {
        const std::size_t bytes = send_text_response(client, 404, "Not Found", "404 Not Found\n");
        log_result(404, bytes);
        return;
    }

    const std::vector<char> body = read_file(file_path);
    if (body.empty() && std::filesystem::file_size(file_path, filesystem_error) != 0) {
        const std::size_t bytes = send_text_response(client, 404, "Not Found", "404 Not Found\n");
        log_result(404, bytes, "file_read_failed");
        return;
    }

    send_response(client, 200, "OK", http::mime_type(file_path), body);
    log_result(200, body.size());
}

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

}  // namespace

int main(int argc, char* argv[]) {
    try {
        if (argc > 4) {
            std::cerr << "Usage: socket_server [port] [document_root] [bind_address]\n";
            return 1;
        }

        const std::uint16_t port = argc >= 2 ? parse_port(argv[1]) : 8080;
        const std::filesystem::path document_root =
            std::filesystem::absolute(argc >= 3 ? argv[2] : "www");
        const std::string bind_address = argc >= 4 ? argv[3] : "127.0.0.1";

        if (!std::filesystem::is_directory(document_root)) {
            std::cerr << "Document root does not exist: " << document_root << '\n';
            return 1;
        }

        Logger logger("server.log");
        ServerStats stats;
        const net::WsaSession winsock;
        net::Socket listener(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
        if (!listener.valid()) {
            throw std::runtime_error("Cannot create listening socket: " + net::error_message());
        }

        const BOOL reuse_address = TRUE;
        setsockopt(listener.get(), SOL_SOCKET, SO_REUSEADDR,
                   reinterpret_cast<const char*>(&reuse_address), sizeof(reuse_address));

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(port);
        if (inet_pton(AF_INET, bind_address.c_str(), &address.sin_addr) != 1) {
            throw std::runtime_error("Bind address must be a valid IPv4 address.");
        }

        if (bind(listener.get(), reinterpret_cast<const sockaddr*>(&address), sizeof(address)) ==
            SOCKET_ERROR) {
            throw std::runtime_error("Bind failed: " + net::error_message());
        }
        if (listen(listener.get(), SOMAXCONN) == SOCKET_ERROR) {
            throw std::runtime_error("Listen failed: " + net::error_message());
        }

        std::ostringstream startup;
        startup << "bind=" << bind_address << " port=" << port << " root=" << document_root.string();
        logger.write("SERVER", startup.str());
        logger.write("SERVER", "ready=true stop=Ctrl+C");

        while (true) {
            sockaddr_in client_address{};
            int address_length = sizeof(client_address);
            net::Socket client(accept(listener.get(), reinterpret_cast<sockaddr*>(&client_address),
                                      &address_length));
            if (!client.valid()) {
                logger.write("ERROR", "accept_failed error=\"" + net::error_message() + "\"");
                continue;
            }

            char client_ip[INET_ADDRSTRLEN]{};
            inet_ntop(AF_INET, &client_address.sin_addr, client_ip, sizeof(client_ip));
            const std::string endpoint =
                std::string(client_ip) + ':' + std::to_string(ntohs(client_address.sin_port));
            const std::uint64_t request_id = stats.total_requests.fetch_add(1) + 1;
            const std::uint32_t active = stats.active_connections.fetch_add(1) + 1;
            logger.write("CONNECT", "id=" + std::to_string(request_id) + " client=" + endpoint +
                                        " active=" + std::to_string(active));

            try {
                std::thread([client = std::move(client), document_root, endpoint, request_id, &logger,
                             &stats]() mutable {
                    try {
                        handle_client(client.get(), document_root, endpoint, request_id, logger, stats);
                    } catch (const std::exception& error) {
                        logger.write("ERROR", "worker_failed id=" + std::to_string(request_id) +
                                                  " client=" + endpoint + " error=\"" +
                                                  error.what() + "\"");
                    } catch (...) {
                        logger.write("ERROR", "worker_failed id=" + std::to_string(request_id) +
                                                  " client=" + endpoint +
                                                  " error=\"unknown exception\"");
                    }
                    const std::uint32_t remaining = stats.active_connections.fetch_sub(1) - 1;
                    logger.write("DISCONNECT", "id=" + std::to_string(request_id) +
                                                   " client=" + endpoint +
                                                   " active=" + std::to_string(remaining));
                }).detach();
            } catch (const std::exception& error) {
                stats.active_connections.fetch_sub(1);
                logger.write("ERROR", "thread_start_failed client=" + endpoint + " error=\"" +
                                          error.what() + "\"");
            }
        }
    } catch (const std::exception& error) {
        std::cerr << "[FATAL] " << error.what() << '\n';
        return 1;
    }
}
