#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>

enum class RoomStatus : std::uint16_t {
    Waiting = 0,
    Playing = 1,
};

struct RoomPlayer {
    std::uint64_t user_id;
    std::string handle;
    int session_fd;
    bool ready = false;
};

class Room {
private:
    std::uint32_t room_id;
    std::string name;
    std::unordered_map<std::uint64_t, RoomPlayer> players;
    std::size_t max_players;
    std::uint64_t owner_user_id;
    RoomStatus status = RoomStatus::Waiting;

    void transferOwnerIfNeeded(std::uint64_t removed_user_id);

public:
    Room(std::uint32_t room_id, std::string name, std::size_t max_players, std::uint64_t owner_user_id);

    bool addPlayer(RoomPlayer player);
    bool removePlayer(std::uint64_t user_id);
    bool hasPlayer(std::uint64_t user_id) const;
    bool setReady(std::uint64_t user_id, bool ready);
    bool allPlayersReady() const;
    bool canStart(std::uint64_t requester_user_id) const;
    bool startGame(std::uint64_t requester_user_id);
    bool finishGame();
    bool isFull() const;
    bool isWaiting() const { return status == RoomStatus::Waiting; }
    bool isPlaying() const { return status == RoomStatus::Playing; }

    std::uint32_t id() const { return room_id; }
    const std::string& roomName() const { return name; }
    std::size_t maxPlayers() const { return max_players; }
    std::size_t playerCount() const { return players.size(); }
    bool empty() const { return players.empty(); }
    std::uint64_t ownerUserId() const { return owner_user_id; }
    RoomStatus roomStatus() const { return status; }
    const std::unordered_map<std::uint64_t, RoomPlayer>& roomPlayers() const { return players; }
};