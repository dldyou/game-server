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

struct GameAttackInput {
    std::uint64_t attacker_user_id;
    std::uint64_t target_user_id;
};

enum class GameAttackResult : std::uint16_t {
    Hit = 0,
    InvalidTarget = 1,
    OutOfRange = 2,
    AttackerDead = 3,
    TargetDead = 4,
    SelfTarget = 5,
};

enum class GameEndReason : std::uint16_t {
    LastPlayerStanding = 0,
    NoPlayers = 1,
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

struct GameAttackEvent {
    std::uint32_t room_id;
    std::uint64_t tick;
    std::uint64_t attacker_user_id;
    std::uint64_t target_user_id;
    GameAttackResult result;
    std::uint16_t damage;
    std::uint16_t target_hp;
};

struct GameEndEvent {
    std::uint32_t room_id;
    std::uint64_t tick;
    std::uint64_t winner_user_id;
    GameEndReason reason;
};

struct GameTickResult {
    GameSnapshot snapshot;
    std::vector<GameAttackEvent> attacks;
    std::optional<GameEndEvent> ended;
};

class Game {
private:
    std::uint32_t room_id;
    std::uint64_t owner_user_id;
    std::uint64_t tick_count = 0;
    std::size_t initial_player_count = 0;
    bool finished = false;
    std::unordered_map<std::uint64_t, GamePlayerState> players;
    std::unordered_map<std::uint64_t, GameMoveInput> pending_moves;
    std::unordered_map<std::uint64_t, GameAttackInput> pending_attacks;

    bool isAlive(std::uint64_t user_id) const;
    std::optional<GameEndEvent> makeEndEventIfNeeded() const;

public:
    explicit Game(const RoomState& room_state);

    std::uint32_t roomId() const { return room_id; }
    std::uint64_t ownerUserId() const { return owner_user_id; }
    std::uint64_t tickCount() const { return tick_count; }
    std::size_t playerCount() const { return players.size(); }
    bool hasPlayer(std::uint64_t user_id) const;
    bool isFinished() const { return finished; }
    bool queueMove(GameMoveInput input);
    bool queueAttack(GameAttackInput input);
    bool removePlayer(std::uint64_t user_id);
    GameTickResult tick();

    std::vector<GamePlayerState> playerStates() const;
    std::optional<GamePlayerState> playerState(std::uint64_t user_id) const;
    GameSnapshot snapshot() const;
};