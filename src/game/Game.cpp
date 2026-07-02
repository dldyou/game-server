#include "Game.hpp"

#include <algorithm>
#include <utility>

Game::Game(const RoomState& room_state)
    : room_id(room_state.room_id), owner_user_id(room_state.owner_user_id) {
    for (const RoomPlayer& player : room_state.players) {
        players.try_emplace(player.user_id, GamePlayerState{
            .user_id = player.user_id,
            .handle = player.handle,
            .session_fd = player.session_fd,
            .x = 0,
            .y = 0,
            .hp = 100,
        });
    }
}

bool Game::hasPlayer(std::uint64_t user_id) const {
    return players.find(user_id) != players.end();
}

bool Game::removePlayer(std::uint64_t user_id) {
    return players.erase(user_id) > 0;
}

void Game::tick() {
    ++tick_count;
}

std::vector<GamePlayerState> Game::playerStates() const {
    std::vector<GamePlayerState> states;
    states.reserve(players.size());
    for (const auto& item : players) {
        states.push_back(item.second);
    }

    std::sort(states.begin(), states.end(), [](const GamePlayerState& lhs, const GamePlayerState& rhs) {
        return lhs.user_id < rhs.user_id;
    });
    return states;
}

std::optional<GamePlayerState> Game::playerState(std::uint64_t user_id) const {
    auto player_it = players.find(user_id);
    if (player_it == players.end()) {
        return std::nullopt;
    }
    return player_it->second;
}