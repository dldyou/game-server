#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

#include "Game.hpp"

class GameManager {
private:
    mutable std::mutex mutex_;
    std::unordered_map<std::uint32_t, Game> games;

public:
    bool createGame(const RoomState& room_state);
    bool removeGame(std::uint32_t room_id);
    bool removePlayer(std::uint32_t room_id, std::uint64_t user_id);
    bool hasGame(std::uint32_t room_id) const;
    std::optional<Game> game(std::uint32_t room_id) const;
    std::vector<std::uint32_t> gameRoomIds() const;
    std::size_t gameCount() const;
    void tickAll();
};