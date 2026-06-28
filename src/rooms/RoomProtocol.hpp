#pragma once

#include "RoomManager.hpp"

#include <span>
#include <vector>

class RoomProtocol {
public:
    static std::optional<CreateRoomRequest> decodeCreateRoom(std::span<const char> payload);
    static std::optional<std::uint32_t> decodeJoinRoom(std::span<const char> payload);
    static std::vector<char> encodeRoomResult(RoomResult result, std::uint32_t room_id);
};