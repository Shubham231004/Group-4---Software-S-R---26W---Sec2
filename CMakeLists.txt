cmake_minimum_required(VERSION 3.16)

project(AircraftGroundControlSprint1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_definitions(_WINSOCK_DEPRECATED_NO_WARNINGS)

include_directories(include)

add_executable(server
    src/server_main.cpp
    src/common.cpp
)

add_executable(client
    src/client_main.cpp
    src/common.cpp
)

if (WIN32)
    target_link_libraries(server ws2_32)
    target_link_libraries(client ws2_32)
endif()