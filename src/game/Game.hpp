#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "rooms/RoomManager.hpp"

struct GameMoveInput {
    std::uint64_t user_id;
    std::int16_t dx;
    std::int16_t dy;
};

struct GamePlayerState {
    std::uint64_t user_id;
    std::string handle;
    int session_fd;
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::uint16_t hp = 100;
};

struct GameSnapshot {
    std::uint32_t room_id;
    std::uint64_t tick;
    std::vector<GamePlayerState> players;
};

class Game {
private:
    std::uint32_t room_id;
    std::uint64_t owner_user_id;
    std::uint64_t tick_count = 0;
    std::unordered_map<std::uint64_t, GamePlayerState> players;
    std::unordered_map<std::uint64_t, GameMoveInput> pending_moves;

public:
    explicit Game(const RoomState& room_state);

    std::uint32_t roomId() const { return room_id; }
    std::uint64_t ownerUserId() const { return owner_user_id; }
    std::uint64_t tickCount() const { return tick_count; }
    std::size_t playerCount() const { return players.size(); }
    bool hasPlayer(std::uint64_t user_id) const;
    bool queueMove(GameMoveInput input);
    bool removePlayer(std::uint64_t user_id);
    GameSnapshot tick();

    std::vector<GamePlayerState> playerStates() const;
    std::optional<GamePlayerState> playerState(std::uint64_t user_id) const;
    GameSnapshot snapshot() const;
};
