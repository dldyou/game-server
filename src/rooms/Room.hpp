#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>

struct RoomPlayer {
    std::uint64_t user_id;
    std::string handle;
    int session_fd;
};

class Room {
private:
    std::uint32_t room_id;
    std::string name;
    std::unordered_map<std::uint64_t, RoomPlayer> players;
    std::size_t max_players;

public:
    Room(
        std::uint32_t room_id,
        std::string name,
        std::size_t max_players
    );

    bool addPlayer(RoomPlayer player);
    bool removePlayer(std::uint64_t user_id);
    bool hasPlayer(std::uint64_t user_id) const;
    bool isFull() const;

    std::uint32_t id() const { return room_id; }
    const std::string& roomName() const { return name; }
    std::size_t maxPlayers() const { return max_players; }
    std::size_t playerCount() const { return players.size(); }
    bool empty() const { return players.empty(); }

    const std::unordered_map<std::uint64_t, RoomPlayer>& roomPlayers() const {
        return players;
    }
};

