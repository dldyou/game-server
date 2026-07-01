#include "RoomManager.hpp"

#include <algorithm>
#include <utility>

namespace {
RoomState makeRoomState(const Room& room) {
    RoomState state{
        .room_id = room.id(),
        .room_name = room.roomName(),
        .max_players = static_cast<std::uint16_t>(room.maxPlayers()),
        .owner_user_id = room.ownerUserId(),
        .status = room.roomStatus(),
        .players = {},
    };

    state.players.reserve(room.roomPlayers().size());
    for (const auto& item : room.roomPlayers()) {
        state.players.push_back(item.second);
    }

    std::sort(state.players.begin(), state.players.end(), [](const RoomPlayer& lhs, const RoomPlayer& rhs) {
        return lhs.user_id < rhs.user_id;
    });
    return state;
}

RoomSummary makeRoomSummary(const Room& room) {
    return RoomSummary{
        .room_id = room.id(),
        .room_name = room.roomName(),
        .max_players = static_cast<std::uint16_t>(room.maxPlayers()),
        .player_count = static_cast<std::uint16_t>(room.playerCount()),
        .status = room.roomStatus(),
    };
}
}

RoomOperationResult RoomManager::createRoom(const AuthenticatedUser& user, int session_fd, const CreateRoomRequest& request) {
    if (user.user_id == 0) {
        return { RoomResult::NotAuthenticated, std::nullopt };
    }

    if (request.room_name.empty() || request.max_players == 0) {
        return { RoomResult::InvalidPayload, std::nullopt };
    }

    std::lock_guard<std::mutex> lock(mutex_);

    auto current_room_it = room_by_user_id.find(user.user_id);
    if (current_room_it != room_by_user_id.end()) {
        return { RoomResult::AlreadyInRoom, current_room_it->second };
    }

    const std::uint32_t room_id = next_room_id++;
    if (next_room_id == 0) {
        next_room_id = 1;
    }

    Room room(room_id, request.room_name, static_cast<std::size_t>(request.max_players), user.user_id);
    if (!room.addPlayer({ .user_id = user.user_id, .handle = user.handle, .session_fd = session_fd, .ready = false })) {
        return { RoomResult::InternalError, std::nullopt };
    }

    auto [room_it, room_inserted] = rooms.try_emplace(room_id, std::move(room));
    if (!room_inserted) {
        return { RoomResult::InternalError, std::nullopt };
    }

    auto [_, mapping_inserted] = room_by_user_id.try_emplace(user.user_id, room_id);
    if (!mapping_inserted) {
        rooms.erase(room_it);
        return { RoomResult::InternalError, std::nullopt };
    }

    return { RoomResult::Success, room_id };
}

RoomOperationResult RoomManager::joinRoom(const AuthenticatedUser& user, int session_fd, std::uint32_t room_id) {
    if (user.user_id == 0) {
        return { RoomResult::NotAuthenticated, std::nullopt };
    }

    if (room_id == 0) {
        return { RoomResult::InvalidPayload, std::nullopt };
    }

    std::lock_guard<std::mutex> lock(mutex_);

    auto current_room_it = room_by_user_id.find(user.user_id);
    if (current_room_it != room_by_user_id.end()) {
        return { RoomResult::AlreadyInRoom, current_room_it->second };
    }

    auto room_it = rooms.find(room_id);
    if (room_it == rooms.end()) {
        return { RoomResult::RoomNotFound, room_id };
    }

    Room& room = room_it->second;
    if (!room.isWaiting()) {
        return { RoomResult::RoomInProgress, room_id };
    }

    if (room.isFull()) {
        return { RoomResult::RoomFull, room_id };
    }

    if (!room.addPlayer({ .user_id = user.user_id, .handle = user.handle, .session_fd = session_fd, .ready = false })) {
        return { RoomResult::InternalError, room_id };
    }

    auto [_, mapping_inserted] = room_by_user_id.try_emplace(user.user_id, room_id);
    if (!mapping_inserted) {
        room.removePlayer(user.user_id);
        return { RoomResult::InternalError, room_id };
    }

    return { RoomResult::Success, room_id };
}

RoomOperationResult RoomManager::leaveRoom(std::uint64_t user_id) {
    if (user_id == 0) {
        return { RoomResult::NotAuthenticated, std::nullopt };
    }

    std::lock_guard<std::mutex> lock(mutex_);

    auto mapping_it = room_by_user_id.find(user_id);
    if (mapping_it == room_by_user_id.end()) {
        return { RoomResult::NotInRoom, std::nullopt };
    }

    const std::uint32_t room_id = mapping_it->second;
    room_by_user_id.erase(mapping_it);

    auto room_it = rooms.find(room_id);
    if (room_it == rooms.end()) {
        return { RoomResult::RoomNotFound, room_id };
    }

    if (!room_it->second.removePlayer(user_id)) {
        return { RoomResult::InternalError, room_id };
    }

    if (room_it->second.empty()) {
        rooms.erase(room_it);
    }

    return { RoomResult::Success, room_id };
}

RoomOperationResult RoomManager::setReady(std::uint64_t user_id, bool ready) {
    if (user_id == 0) {
        return { RoomResult::NotAuthenticated, std::nullopt };
    }

    std::lock_guard<std::mutex> lock(mutex_);

    auto mapping_it = room_by_user_id.find(user_id);
    if (mapping_it == room_by_user_id.end()) {
        return { RoomResult::NotInRoom, std::nullopt };
    }

    const std::uint32_t room_id = mapping_it->second;
    auto room_it = rooms.find(room_id);
    if (room_it == rooms.end()) {
        return { RoomResult::RoomNotFound, room_id };
    }

    if (!room_it->second.isWaiting()) {
        return { RoomResult::RoomInProgress, room_id };
    }

    if (!room_it->second.setReady(user_id, ready)) {
        return { RoomResult::InternalError, room_id };
    }

    return { RoomResult::Success, room_id };
}

RoomOperationResult RoomManager::startGame(std::uint64_t user_id) {
    if (user_id == 0) {
        return { RoomResult::NotAuthenticated, std::nullopt };
    }

    std::lock_guard<std::mutex> lock(mutex_);

    auto mapping_it = room_by_user_id.find(user_id);
    if (mapping_it == room_by_user_id.end()) {
        return { RoomResult::NotInRoom, std::nullopt };
    }

    const std::uint32_t room_id = mapping_it->second;
    auto room_it = rooms.find(room_id);
    if (room_it == rooms.end()) {
        return { RoomResult::RoomNotFound, room_id };
    }

    Room& room = room_it->second;
    if (!room.isWaiting()) {
        return { RoomResult::RoomInProgress, room_id };
    }

    if (room.ownerUserId() != user_id) {
        return { RoomResult::NotOwner, room_id };
    }

    if (!room.allPlayersReady()) {
        return { RoomResult::NotReady, room_id };
    }

    if (!room.startGame(user_id)) {
        return { RoomResult::InternalError, room_id };
    }

    return { RoomResult::Success, room_id };
}

void RoomManager::removeSession(std::uint64_t user_id) {
    if (user_id == 0) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    auto mapping_it = room_by_user_id.find(user_id);
    if (mapping_it == room_by_user_id.end()) {
        return;
    }

    const std::uint32_t room_id = mapping_it->second;
    room_by_user_id.erase(mapping_it);

    auto room_it = rooms.find(room_id);
    if (room_it == rooms.end()) {
        return;
    }

    room_it->second.removePlayer(user_id);
    if (room_it->second.empty()) {
        rooms.erase(room_it);
    }
}

std::optional<std::uint32_t> RoomManager::roomIdOf(std::uint64_t user_id) const {
    std::lock_guard<std::mutex> lock(mutex_);

    auto mapping_it = room_by_user_id.find(user_id);
    if (mapping_it == room_by_user_id.end()) {
        return std::nullopt;
    }

    return mapping_it->second;
}

std::optional<RoomState> RoomManager::roomState(std::uint32_t room_id) const {
    std::lock_guard<std::mutex> lock(mutex_);

    auto room_it = rooms.find(room_id);
    if (room_it == rooms.end()) {
        return std::nullopt;
    }

    return makeRoomState(room_it->second);
}

std::optional<RoomState> RoomManager::roomStateOf(std::uint64_t user_id) const {
    std::lock_guard<std::mutex> lock(mutex_);

    auto mapping_it = room_by_user_id.find(user_id);
    if (mapping_it == room_by_user_id.end()) {
        return std::nullopt;
    }

    auto room_it = rooms.find(mapping_it->second);
    if (room_it == rooms.end()) {
        return std::nullopt;
    }

    return makeRoomState(room_it->second);
}

std::vector<RoomSummary> RoomManager::roomList() const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<RoomSummary> summaries;
    summaries.reserve(rooms.size());
    for (const auto& item : rooms) {
        summaries.push_back(makeRoomSummary(item.second));
    }

    std::sort(summaries.begin(), summaries.end(), [](const RoomSummary& lhs, const RoomSummary& rhs) {
        return lhs.room_id < rhs.room_id;
    });
    return summaries;
}

std::vector<RoomPlayer> RoomManager::playersInSameRoom(std::uint64_t user_id) const {
    std::lock_guard<std::mutex> lock(mutex_);

    auto mapping_it = room_by_user_id.find(user_id);
    if (mapping_it == room_by_user_id.end()) {
        return {};
    }

    auto room_it = rooms.find(mapping_it->second);
    if (room_it == rooms.end()) {
        return {};
    }

    std::vector<RoomPlayer> players;
    players.reserve(room_it->second.roomPlayers().size());
    for (const auto& [_, player] : room_it->second.roomPlayers()) {
        players.push_back(player);
    }
    return players;
}