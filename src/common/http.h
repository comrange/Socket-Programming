#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>

namespace http {

inline constexpr std::size_t kMaxHeaderSize = 16 * 1024;

struct Request {
    std::string method;
    std::string target;
    std::string version;
};

enum class ParseStatus {
    ok,
    bad_request,
};

[[nodiscard]] ParseStatus parse_request(std::string_view raw_header, Request& request);
[[nodiscard]] std::string response_header(int status_code,
                                          std::string_view reason,
                                          std::string_view content_type,
                                          std::size_t content_length,
                                          std::string_view extra_headers = {});
[[nodiscard]] std::string mime_type(const std::filesystem::path& file_path);

}  // namespace http
