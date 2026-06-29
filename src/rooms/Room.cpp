#include "Room.hpp"

#include <utility>

Room::Room(std::uint32_t room_id, std::string name, std::size_t max_players)
    : room_id(room_id), name(std::move(name)), max_players(max_players) {
}

bool Room::addPlayer(RoomPlayer player) {
    if (player.user_id == 0 || isFull() || hasPlayer(player.user_id)) {
        return false;
    }

    const std::uint64_t user_id = player.user_id;
    auto [_, inserted] = players.try_emplace(user_id, std::move(player));
    return inserted;
}

bool Room::removePlayer(std::uint64_t user_id) {
    return players.erase(user_id) > 0;
}

bool Room::hasPlayer(std::uint64_t user_id) const {
    return players.find(user_id) != players.end();
}

bool Room::isFull() const {
    return players.size() >= max_players;
}

