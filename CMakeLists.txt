cmake_minimum_required(VERSION 3.11)
project(game-server)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(game-server
    src/main.cpp
    src/Server.cpp
    src/networks/Session.cpp
    src/networks/PacketParser.cpp
    src/config/Config.cpp
    src/utils/StringUtils.cpp
)

target_include_directories(game-server PRIVATE src)
