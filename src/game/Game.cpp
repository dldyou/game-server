#include "Game.hpp"

#include <algorithm>
#include <utility>

namespace {
    constexpr std::int32_t movement_speed = 1;

    std::int16_t clampDirection(std::int16_t value) {
        if (value < -1) {
            return -1;
        }
        if (value > 1) {
            return 1;
        }
        return value;
    }
}

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

bool Game::queueMove(GameMoveInput input) {
    if (input.user_id == 0 || !hasPlayer(input.user_id)) {
        return false;
    }

    input.dx = clampDirection(input.dx);
    input.dy = clampDirection(input.dy);
    pending_moves[input.user_id] = input;
    return true;
}

bool Game::removePlayer(std::uint64_t user_id) {
    pending_moves.erase(user_id);
    return players.erase(user_id) > 0;
}

GameSnapshot Game::tick() {
    for (const auto& item : pending_moves) {
        auto player_it = players.find(item.first);
        if (player_it == players.end()) {
            continue;
        }

        player_it->second.x += static_cast<std::int32_t>(item.second.dx) * movement_speed;
        player_it->second.y += static_cast<std::int32_t>(item.second.dy) * movement_speed;
    }

    pending_moves.clear();
    ++tick_count;
    return snapshot();
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

GameSnapshot Game::snapshot() const {
    return GameSnapshot{
        .room_id = room_id,
        .tick = tick_count,
        .players = playerStates(),
    };
}
