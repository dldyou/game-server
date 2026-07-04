#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "Game.hpp"

class GameProtocol {
private:
    static bool readI16(std::span<const char> payload, std::size_t offset, std::int16_t& value);
    static void appendU16(std::vector<char>& output, std::uint16_t value);
    static void appendU32(std::vector<char>& output, std::uint32_t value);
    static void appendU64(std::vector<char>& output, std::uint64_t value);
    static void appendI32(std::vector<char>& output, std::int32_t value);

public:
    static std::optional<GameMoveInput> decodeMove(std::uint64_t user_id, std::span<const char> payload);
    static std::vector<char> encodeSnapshot(const GameSnapshot& snapshot);
};
