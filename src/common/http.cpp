#include "common/http.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace http {
namespace {

bool is_token_character(unsigned char value) {
    return std::isalnum(value) != 0 || value == '-' || value == '_';
}

std::string lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

}  // namespace

ParseStatus parse_request(std::string_view raw_header, Request& request) {
    const std::size_t request_line_end = raw_header.find("\r\n");
    if (request_line_end == std::string_view::npos) {
        return ParseStatus::bad_request;
    }

    std::istringstream request_line(std::string(raw_header.substr(0, request_line_end)));
    std::string extra;
    if (!(request_line >> request.method >> request.target >> request.version) || (request_line >> extra)) {
        return ParseStatus::bad_request;
    }

    if (request.method.empty() ||
        !std::all_of(request.method.begin(), request.method.end(), [](unsigned char character) {
            return std::isupper(character) != 0;
        }) ||
        request.target.empty() || request.target.front() != '/' || request.version != "HTTP/1.1") {
        return ParseStatus::bad_request;
    }

    bool has_host = false;
    std::size_t line_start = request_line_end + 2;
    while (line_start < raw_header.size()) {
        const std::size_t line_end = raw_header.find("\r\n", line_start);
        if (line_end == std::string_view::npos) {
            return ParseStatus::bad_request;
        }
        if (line_end == line_start) {
            break;
        }

        const std::string_view line = raw_header.substr(line_start, line_end - line_start);
        const std::size_t colon = line.find(':');
        if (colon == std::string_view::npos || colon == 0 ||
            !std::all_of(line.begin(), line.begin() + static_cast<std::ptrdiff_t>(colon),
                         [](unsigned char character) { return is_token_character(character); })) {
            return ParseStatus::bad_request;
        }

        if (lowercase(std::string(line.substr(0, colon))) == "host") {
            const std::string_view value = line.substr(colon + 1);
            if (value.find_first_not_of(" \t") != std::string_view::npos) {
                has_host = true;
            }
        }
        line_start = line_end + 2;
    }

    return has_host ? ParseStatus::ok : ParseStatus::bad_request;
}

std::string response_header(int status_code,
                            std::string_view reason,
                            std::string_view content_type,
                            std::size_t content_length,
                            std::string_view extra_headers) {
    std::ostringstream response;
    response << "HTTP/1.1 " << status_code << ' ' << reason << "\r\n"
             << "Server: SocketDemo/1.0\r\n"
             << "Content-Length: " << content_length << "\r\n"
             << "Content-Type: " << content_type << "\r\n"
             << "Connection: close\r\n";
    if (!extra_headers.empty()) {
        response << extra_headers;
        if (extra_headers.size() < 2 || extra_headers.substr(extra_headers.size() - 2) != "\r\n") {
            response << "\r\n";
        }
    }
    response << "\r\n";
    return response.str();
}

std::string mime_type(const std::filesystem::path& file_path) {
    const std::string extension = lowercase(file_path.extension().string());
    if (extension == ".html" || extension == ".htm") return "text/html; charset=utf-8";
    if (extension == ".txt") return "text/plain; charset=utf-8";
    if (extension == ".css") return "text/css; charset=utf-8";
    if (extension == ".js") return "text/javascript; charset=utf-8";
    if (extension == ".json") return "application/json; charset=utf-8";
    if (extension == ".svg") return "image/svg+xml";
    if (extension == ".png") return "image/png";
    if (extension == ".jpg" || extension == ".jpeg") return "image/jpeg";
    if (extension == ".gif") return "image/gif";
    if (extension == ".ico") return "image/x-icon";
    return "application/octet-stream";
}

}  // namespace http
