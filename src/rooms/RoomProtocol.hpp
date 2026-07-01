#pragma once

#include "RoomManager.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

class RoomProtocol {
private:
    static bool readU16(std::span<const char> payload, std::size_t offset, std::uint16_t& value);
    static bool readU32(std::span<const char> payload, std::size_t offset, std::uint32_t& value);
    static void appendU16(std::vector<char>& output, std::uint16_t value);
    static void appendU32(std::vector<char>& output, std::uint32_t value);
    static void appendU64(std::vector<char>& output, std::uint64_t value);

public:
    static std::optional<CreateRoomRequest> decodeCreateRoom(std::span<const char> payload);
    static std::optional<std::uint32_t> decodeJoinRoom(std::span<const char> payload);
    static std::vector<char> encodeRoomResult(RoomResult result, std::uint32_t room_id);
    static std::vector<char> encodeRoomState(const RoomState& state);
    static std::vector<char> encodeRoomList(const std::vector<RoomSummary>& rooms);
};