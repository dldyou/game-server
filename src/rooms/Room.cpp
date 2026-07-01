#include "Room.hpp"

#include <algorithm>
#include <utility>

Room::Room(std::uint32_t room_id, std::string name, std::size_t max_players, std::uint64_t owner_user_id)
    : room_id(room_id), name(std::move(name)), max_players(max_players), owner_user_id(owner_user_id) {
}

bool Room::addPlayer(RoomPlayer player) {
    if (player.user_id == 0 || !isWaiting() || isFull() || hasPlayer(player.user_id)) {
        return false;
    }

    player.ready = false;
    const std::uint64_t user_id = player.user_id;
    auto [_, inserted] = players.try_emplace(user_id, std::move(player));
    return inserted;
}

bool Room::removePlayer(std::uint64_t user_id) {
    const bool removed = players.erase(user_id) > 0;
    if (removed) {
        transferOwnerIfNeeded(user_id);
    }
    return removed;
}

bool Room::hasPlayer(std::uint64_t user_id) const {
    return players.find(user_id) != players.end();
}

bool Room::setReady(std::uint64_t user_id, bool ready) {
    if (!isWaiting()) {
        return false;
    }

    auto player_it = players.find(user_id);
    if (player_it == players.end()) {
        return false;
    }

    player_it->second.ready = ready;
    return true;
}

bool Room::allPlayersReady() const {
    if (players.empty()) {
        return false;
    }

    return std::all_of(players.begin(), players.end(), [](const auto& item) {
        return item.second.ready;
    });
}

bool Room::canStart(std::uint64_t requester_user_id) const {
    return isWaiting() && requester_user_id == owner_user_id && allPlayersReady();
}

bool Room::startGame(std::uint64_t requester_user_id) {
    if (!canStart(requester_user_id)) {
        return false;
    }

    status = RoomStatus::Playing;
    return true;
}

bool Room::isFull() const {
    return players.size() >= max_players;
}

void Room::transferOwnerIfNeeded(std::uint64_t removed_user_id) {
    if (removed_user_id != owner_user_id) {
        return;
    }

    if (players.empty()) {
        owner_user_id = 0;
        return;
    }

    const auto owner_it = std::min_element(players.begin(), players.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.first < rhs.first;
    });
    owner_user_id = owner_it->first;
}