#include "GameProtocol.hpp"

#include <limits>

namespace {
    constexpr std::size_t move_payload_size = 4;
    constexpr std::int16_t min_direction = -1;
    constexpr std::int16_t max_direction = 1;

    std::int16_t decodeSigned16(std::uint16_t value) {
        if (value <= 0x7fffU) {
            return static_cast<std::int16_t>(value);
        }
        return static_cast<std::int16_t>(static_cast<int>(value) - 0x10000);
    }
}

bool GameProtocol::readI16(std::span<const char> payload, std::size_t offset, std::int16_t& value) {
    if (offset > payload.size() || payload.size() - offset < 2) {
        return false;
    }

    const auto high = static_cast<unsigned char>(payload[offset]);
    const auto low = static_cast<unsigned char>(payload[offset + 1]);
    const std::uint16_t raw = static_cast<std::uint16_t>((static_cast<std::uint16_t>(high) << 8U) | static_cast<std::uint16_t>(low));
    value = decodeSigned16(raw);
    return true;
}

void GameProtocol::appendU16(std::vector<char>& output, std::uint16_t value) {
    output.push_back(static_cast<char>((value >> 8U) & 0xffU));
    output.push_back(static_cast<char>(value & 0xffU));
}

void GameProtocol::appendU32(std::vector<char>& output, std::uint32_t value) {
    for (int shift = 24; shift >= 0; shift -= 8) {
        output.push_back(static_cast<char>((value >> shift) & 0xffU));
    }
}

void GameProtocol::appendU64(std::vector<char>& output, std::uint64_t value) {
    for (int shift = 56; shift >= 0; shift -= 8) {
        output.push_back(static_cast<char>((value >> shift) & 0xffU));
    }
}

void GameProtocol::appendI32(std::vector<char>& output, std::int32_t value) {
    appendU32(output, static_cast<std::uint32_t>(value));
}

std::optional<GameMoveInput> GameProtocol::decodeMove(std::uint64_t user_id, std::span<const char> payload) {
    if (user_id == 0 || payload.size() != move_payload_size) {
        return std::nullopt;
    }

    std::int16_t dx = 0;
    std::int16_t dy = 0;
    if (!readI16(payload, 0, dx) || !readI16(payload, 2, dy)) {
        return std::nullopt;
    }

    if (dx < min_direction || dx > max_direction || dy < min_direction || dy > max_direction) {
        return std::nullopt;
    }

    return GameMoveInput{ .user_id = user_id, .dx = dx, .dy = dy };
}

std::vector<char> GameProtocol::encodeSnapshot(const GameSnapshot& snapshot) {
    if (snapshot.room_id == 0 || snapshot.players.empty() || snapshot.players.size() > std::numeric_limits<std::uint16_t>::max()) {
        return {};
    }

    std::vector<char> output;
    output.reserve(14 + snapshot.players.size() * 18);
    appendU32(output, snapshot.room_id);
    appendU64(output, snapshot.tick);
    appendU16(output, static_cast<std::uint16_t>(snapshot.players.size()));

    for (const GamePlayerState& player : snapshot.players) {
        if (player.user_id == 0) {
            return {};
        }

        appendU64(output, player.user_id);
        appendI32(output, player.x);
        appendI32(output, player.y);
        appendU16(output, player.hp);
    }

    return output;
}
