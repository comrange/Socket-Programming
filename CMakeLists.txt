cmake_minimum_required(VERSION 3.20)

project(SocketDemo VERSION 1.0 LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "SocketDemo currently targets Windows and Winsock2.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

find_package(Threads REQUIRED)

add_library(socket_common
    src/common/http.cpp
    src/common/socket_support.cpp
)

target_include_directories(socket_common PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(socket_common PUBLIC ws2_32)

if(MINGW)
    target_compile_options(socket_common PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(socket_server src/server/main.cpp)
target_link_libraries(socket_server PRIVATE socket_common Threads::Threads)

add_executable(socket_client src/client/main.cpp)
target_link_libraries(socket_client PRIVATE socket_common)

# Keep the latest demo website beside the executables after every server build.
add_custom_command(TARGET socket_server POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/www"
            "${CMAKE_BINARY_DIR}/www"
)
