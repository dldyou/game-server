cmake_minimum_required(VERSION 3.11)
project(game-server)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(game-server
    src/main.cpp
    src/Server.cpp
    src/networks/Session.cpp
    src/networks/PacketParser.cpp
    src/networks/PacketSerializer.cpp
    src/config/Config.cpp
    src/utils/StringUtils.cpp
    src/auth/LoginProtocol.cpp
    src/users/UserManager.cpp
    src/rooms/Room.cpp
    src/rooms/RoomManager.cpp
    src/rooms/RoomProtocol.cpp
    src/chat/ChatProtocol.cpp
)

target_include_directories(game-server PRIVATE src)

add_executable(test-client
    tools/TestClient.cpp
    src/networks/PacketParser.cpp
    src/networks/PacketSerializer.cpp
)

target_include_directories(test-client PRIVATE src)

