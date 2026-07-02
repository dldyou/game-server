#include "GameManager.hpp"

#include <algorithm>
#include <utility>

bool GameManager::createGame(const RoomState& room_state) {
    if (room_state.room_id == 0 || room_state.owner_user_id == 0 || room_state.players.empty() || room_state.status != RoomStatus::Playing) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto [_, inserted] = games.try_emplace(room_state.room_id, room_state);
    return inserted;
}

bool GameManager::removeGame(std::uint32_t room_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    return games.erase(room_id) > 0;
}

bool GameManager::removePlayer(std::uint32_t room_id, std::uint64_t user_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto game_it = games.find(room_id);
    if (game_it == games.end()) {
        return false;
    }

    const bool removed = game_it->second.removePlayer(user_id);
    if (game_it->second.playerCount() == 0) {
        games.erase(game_it);
    }
    return removed;
}

bool GameManager::hasGame(std::uint32_t room_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return games.find(room_id) != games.end();
}

std::optional<Game> GameManager::game(std::uint32_t room_id) const {
    std::lock_guard<std::mutex> lock(mutex_);

    auto game_it = games.find(room_id);
    if (game_it == games.end()) {
        return std::nullopt;
    }
    return game_it->second;
}

std::vector<std::uint32_t> GameManager::gameRoomIds() const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<std::uint32_t> room_ids;
    room_ids.reserve(games.size());
    for (const auto& item : games) {
        room_ids.push_back(item.first);
    }

    std::sort(room_ids.begin(), room_ids.end());
    return room_ids;
}

std::size_t GameManager::gameCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return games.size();
}

void GameManager::tickAll() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& item : games) {
        item.second.tick();
    }
}