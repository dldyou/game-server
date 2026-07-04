#include "Game.hpp"

#include <algorithm>
#include <utility>
#include <vector>

namespace {
    constexpr std::int32_t movement_speed = 1;
    constexpr std::int32_t min_position = -10;
    constexpr std::int32_t max_position = 10;
    constexpr std::uint32_t attack_range = 1;
    constexpr std::uint16_t attack_damage = 25;
    constexpr std::int32_t spawn_grid_min = -4;
    constexpr std::int32_t spawn_grid_width = 8;

    struct Position {
        std::int32_t x;
        std::int32_t y;
    };

    std::int16_t clampDirection(std::int16_t value) {
        if (value < -1) {
            return -1;
        }
        if (value > 1) {
            return 1;
        }
        return value;
    }

    std::int32_t clampPosition(std::int32_t value) {
        if (value < min_position) {
            return min_position;
        }
        if (value > max_position) {
            return max_position;
        }
        return value;
    }

    std::uint32_t axisDistance(std::int32_t lhs, std::int32_t rhs) {
        if (lhs >= rhs) {
            return static_cast<std::uint32_t>(lhs - rhs);
        }
        return static_cast<std::uint32_t>(rhs - lhs);
    }

    bool inAttackRange(const GamePlayerState& attacker, const GamePlayerState& target) {
        return axisDistance(attacker.x, target.x) <= attack_range && axisDistance(attacker.y, target.y) <= attack_range;
    }

    Position spawnPosition(std::size_t index) {
        return Position{
            .x = spawn_grid_min + static_cast<std::int32_t>(index % static_cast<std::size_t>(spawn_grid_width)),
            .y = spawn_grid_min + static_cast<std::int32_t>(index / static_cast<std::size_t>(spawn_grid_width)),
        };
    }

    bool samePosition(Position lhs, Position rhs) {
        return lhs.x == rhs.x && lhs.y == rhs.y;
    }

    bool hasContestedDestination(const std::vector<std::pair<std::uint64_t, Position>>& proposed_positions, std::uint64_t user_id, Position position) {
        std::size_t count = 0;
        for (const auto& item : proposed_positions) {
            if (samePosition(item.second, position)) {
                ++count;
            }
        }
        return count > 1 || (count == 1 && proposed_positions.size() == 1 && proposed_positions.front().first != user_id);
    }
}

Game::Game(const RoomState& room_state)
    : room_id(room_state.room_id), owner_user_id(room_state.owner_user_id), initial_player_count(room_state.players.size()) {
    std::size_t spawn_index = 0;
    for (const RoomPlayer& player : room_state.players) {
        const Position spawn = spawnPosition(spawn_index++);
        players.try_emplace(player.user_id, GamePlayerState{
            .user_id = player.user_id,
            .handle = player.handle,
            .session_fd = player.session_fd,
            .x = spawn.x,
            .y = spawn.y,
            .hp = 100,
        });
    }
}

bool Game::hasPlayer(std::uint64_t user_id) const {
    return players.find(user_id) != players.end();
}

bool Game::isAlive(std::uint64_t user_id) const {
    auto player_it = players.find(user_id);
    return player_it != players.end() && player_it->second.hp > 0;
}

bool Game::queueMove(GameMoveInput input) {
    if (finished || input.user_id == 0 || !isAlive(input.user_id)) {
        return false;
    }

    input.dx = clampDirection(input.dx);
    input.dy = clampDirection(input.dy);
    pending_moves[input.user_id] = input;
    return true;
}

bool Game::queueAttack(GameAttackInput input) {
    if (finished || input.attacker_user_id == 0 || input.target_user_id == 0 || !hasPlayer(input.attacker_user_id)) {
        return false;
    }

    pending_attacks[input.attacker_user_id] = input;
    return true;
}

bool Game::removePlayer(std::uint64_t user_id) {
    pending_moves.erase(user_id);
    pending_attacks.erase(user_id);
    return players.erase(user_id) > 0;
}

GameTickResult Game::tick() {
    ++tick_count;

    std::vector<std::pair<std::uint64_t, Position>> proposed_positions;
    proposed_positions.reserve(players.size());
    for (const auto& item : players) {
        if (item.second.hp == 0) {
            continue;
        }
        proposed_positions.push_back({ item.first, { .x = item.second.x, .y = item.second.y } });
    }

    std::vector<GameMoveInput> moves;
    moves.reserve(pending_moves.size());
    for (const auto& item : pending_moves) {
        moves.push_back(item.second);
    }
    std::sort(moves.begin(), moves.end(), [](const GameMoveInput& lhs, const GameMoveInput& rhs) {
        return lhs.user_id < rhs.user_id;
    });

    for (const GameMoveInput& move : moves) {
        auto player_it = players.find(move.user_id);
        if (player_it == players.end() || player_it->second.hp == 0) {
            continue;
        }

        const Position next_position{
            .x = clampPosition(player_it->second.x + static_cast<std::int32_t>(move.dx) * movement_speed),
            .y = clampPosition(player_it->second.y + static_cast<std::int32_t>(move.dy) * movement_speed),
        };

        auto proposed_it = std::find_if(proposed_positions.begin(), proposed_positions.end(), [user_id = move.user_id](const auto& item) {
            return item.first == user_id;
        });
        if (proposed_it != proposed_positions.end()) {
            proposed_it->second = next_position;
        }
    }

    for (const auto& item : proposed_positions) {
        if (hasContestedDestination(proposed_positions, item.first, item.second)) {
            continue;
        }

        auto player_it = players.find(item.first);
        if (player_it != players.end()) {
            player_it->second.x = item.second.x;
            player_it->second.y = item.second.y;
        }
    }

    std::vector<GameAttackInput> attacks;
    attacks.reserve(pending_attacks.size());
    for (const auto& item : pending_attacks) {
        attacks.push_back(item.second);
    }
    std::sort(attacks.begin(), attacks.end(), [](const GameAttackInput& lhs, const GameAttackInput& rhs) {
        return lhs.attacker_user_id < rhs.attacker_user_id;
    });

    std::vector<GameAttackEvent> attack_events;
    attack_events.reserve(attacks.size());
    for (const GameAttackInput& attack : attacks) {
        GameAttackEvent event{
            .room_id = room_id,
            .tick = tick_count,
            .attacker_user_id = attack.attacker_user_id,
            .target_user_id = attack.target_user_id,
            .result = GameAttackResult::InvalidTarget,
            .damage = 0,
            .target_hp = 0,
        };

        auto attacker_it = players.find(attack.attacker_user_id);
        auto target_it = players.find(attack.target_user_id);
        if (attacker_it == players.end() || target_it == players.end()) {
            attack_events.push_back(event);
            continue;
        }

        event.target_hp = target_it->second.hp;
        if (attack.attacker_user_id == attack.target_user_id) {
            event.result = GameAttackResult::SelfTarget;
        } else if (attacker_it->second.hp == 0) {
            event.result = GameAttackResult::AttackerDead;
        } else if (target_it->second.hp == 0) {
            event.result = GameAttackResult::TargetDead;
        } else if (!inAttackRange(attacker_it->second, target_it->second)) {
            event.result = GameAttackResult::OutOfRange;
        } else {
            event.result = GameAttackResult::Hit;
            event.damage = attack_damage;
            if (target_it->second.hp <= attack_damage) {
                target_it->second.hp = 0;
            } else {
                target_it->second.hp = static_cast<std::uint16_t>(target_it->second.hp - attack_damage);
            }
            event.target_hp = target_it->second.hp;
        }

        attack_events.push_back(event);
    }

    pending_moves.clear();
    pending_attacks.clear();

    auto ended = makeEndEventIfNeeded();
    if (ended) {
        finished = true;
    }

    return GameTickResult{ .snapshot = snapshot(), .attacks = std::move(attack_events), .ended = ended };
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
    return GameSnapshot{ .room_id = room_id, .tick = tick_count, .players = playerStates() };
}

std::optional<GameEndEvent> Game::makeEndEventIfNeeded() const {
    if (finished || initial_player_count <= 1) {
        return std::nullopt;
    }

    std::uint64_t winner_user_id = 0;
    std::size_t alive_count = 0;
    for (const auto& item : players) {
        if (item.second.hp == 0) {
            continue;
        }
        winner_user_id = item.first;
        ++alive_count;
    }

    if (alive_count == 0) {
        return GameEndEvent{ .room_id = room_id, .tick = tick_count, .winner_user_id = 0, .reason = GameEndReason::NoPlayers };
    }
    if (alive_count == 1) {
        return GameEndEvent{ .room_id = room_id, .tick = tick_count, .winner_user_id = winner_user_id, .reason = GameEndReason::LastPlayerStanding };
    }
    return std::nullopt;
}