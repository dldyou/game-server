#pragma once

#include <cstdint>
#include <string>
#include <optional>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "Room.hpp"
#include "auth/AuthResult.hpp"

enum class RoomResult {
    Success,
    NotAuthenticated,
    RoomNotFound,
    RoomFull,
    AlreadyInRoom,
    NotInRoom,
    InvalidPayload,
    InternalError,
};

struct CreateRoomRequest {
    std::string room_name;
    std::uint16_t max_players;
};

struct JoinRoomRequest {
    std::uint32_t room_id;
};

struct RoomOperationResult {
    RoomResult result;
    std::optional<std::uint32_t> room_id;
};

class RoomManager {
private:
    mutable std::mutex mutex_;
    std::uint32_t next_room_id = 1;

    std::unordered_map<std::uint32_t, Room> rooms;
    std::unordered_map<std::uint64_t, std::uint32_t> room_by_user_id;

public:
    RoomOperationResult createRoom(const AuthenticatedUser& user, int session_fd, const CreateRoomRequest& request);
    RoomOperationResult joinRoom(const AuthenticatedUser& user, int session_fd, std::uint32_t room_id);
    RoomOperationResult leaveRoom(std::uint64_t user_id);

    void removeSession(std::uint64_t user_id);
    std::optional<std::uint32_t> roomIdOf(std::uint64_t user_id) const;
    std::vector<RoomPlayer> playersInSameRoom(std::uint64_t user_id) const;
};

