#include "RoomManager.hpp"

RoomOperationResult RoomManager::createRoom(const AuthenticatedUser & user, int session_fd, const CreateRoomRequest & request)
{
    return RoomOperationResult();
}

RoomOperationResult RoomManager::joinRoom(const AuthenticatedUser & user, int session_fd, std::uint32_t room_id)
{
    return RoomOperationResult();
}

RoomOperationResult RoomManager::leaveRoom(std::uint64_t user_id)
{
    return RoomOperationResult();
}

void RoomManager::removeSession(std::uint64_t user_id)
{
}

std::optional<std::uint32_t> RoomManager::roomIdOf(std::uint64_t user_id) const
{
    return std::optional<std::uint32_t>();
}
