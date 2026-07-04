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

bool GameManager::queueMove(std::uint32_t room_id, GameMoveInput input) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto game_it = games.find(room_id);
    if (game_it == games.end()) {
        return false;
    }

    return game_it->second.queueMove(input);
}

bool GameManager::queueAttack(std::uint32_t room_id, GameAttackInput input) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto game_it = games.find(room_id);
    if (game_it == games.end()) {
        return false;
    }

    return game_it->second.queueAttack(input);
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

std::optional<GameSnapshot> GameManager::snapshot(std::uint32_t room_id) const {
    std::lock_guard<std::mutex> lock(mutex_);

    auto game_it = games.find(room_id);
    if (game_it == games.end()) {
        return std::nullopt;
    }
    return game_it->second.snapshot();
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

std::vector<GameTickResult> GameManager::tickAll() {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<GameTickResult> results;
    std::vector<std::uint32_t> ended_room_ids;
    results.reserve(games.size());

    for (auto& item : games) {
        GameTickResult result = item.second.tick();
        if (result.ended) {
            ended_room_ids.push_back(item.first);
        }
        results.push_back(std::move(result));
    }

    for (std::uint32_t room_id : ended_room_ids) {
        games.erase(room_id);
    }
    return results;
}