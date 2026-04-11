cmake_minimum_required(VERSION 3.16)

project(AircraftGroundControl LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_definitions(_WINSOCK_DEPRECATED_NO_WARNINGS)

add_library(agc_core
    src/common.cpp
    src/client_logic.cpp
    src/server_logic.cpp
)

target_include_directories(agc_core PUBLIC include)

add_executable(server
    src/server_main.cpp
)

add_executable(client
    src/client_main.cpp
)

target_link_libraries(server PRIVATE agc_core)
target_link_libraries(client PRIVATE agc_core)

if (WIN32)
    target_link_libraries(server PRIVATE ws2_32)
    target_link_libraries(client PRIVATE ws2_32)
endif()