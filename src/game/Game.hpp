#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "rooms/RoomManager.hpp"

struct GamePlayerState {
    std::uint64_t user_id;
    std::string handle;
    int session_fd;
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::uint16_t hp = 100;
};

class Game {
private:
    std::uint32_t room_id;
    std::uint64_t owner_user_id;
    std::uint64_t tick_count = 0;
    std::unordered_map<std::uint64_t, GamePlayerState> players;

public:
    explicit Game(const RoomState& room_state);

    std::uint32_t roomId() const { return room_id; }
    std::uint64_t ownerUserId() const { return owner_user_id; }
    std::uint64_t tickCount() const { return tick_count; }
    std::size_t playerCount() const { return players.size(); }
    bool hasPlayer(std::uint64_t user_id) const;
    bool removePlayer(std::uint64_t user_id);
    void tick();

    std::vector<GamePlayerState> playerStates() const;
    std::optional<GamePlayerState> playerState(std::uint64_t user_id) const;
};