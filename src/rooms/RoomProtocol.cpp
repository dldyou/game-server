#include "RoomProtocol.hpp"

std::optional<CreateRoomRequest> RoomProtocol::decodeCreateRoom(std::span<const char> payload)
{
    return std::optional<CreateRoomRequest>();
}

std::optional<std::uint32_t> RoomProtocol::decodeJoinRoom(std::span<const char> payload)
{
    return std::optional<std::uint32_t>();
}

std::vector<char> RoomProtocol::encodeRoomResult(RoomResult result, std::uint32_t room_id)
{
    return std::vector<char>();
}
