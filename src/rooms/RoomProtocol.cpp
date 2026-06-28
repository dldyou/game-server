#include "RoomProtocol.hpp"

#include <limits>
#include <string>

namespace {
    constexpr std::size_t create_room_header_size = 4;
    constexpr std::size_t join_room_payload_size = 4;
    constexpr std::size_t max_room_name_length = 64;
    constexpr std::uint16_t min_room_players = 1;
    constexpr std::uint16_t max_room_players = 64;
}

bool RoomProtocol::readU16(std::span<const char> payload, std::size_t offset, std::uint16_t& value) {
    if (offset > payload.size() || payload.size() - offset < 2) {
        return false;
    }

    const auto high = static_cast<unsigned char>(payload[offset]);
    const auto low = static_cast<unsigned char>(payload[offset + 1]);

    value = static_cast<std::uint16_t>((static_cast<std::uint16_t>(high) << 8U) | static_cast<std::uint16_t>(low));

    return true;
}

bool RoomProtocol::readU32(std::span<const char> payload, std::size_t offset, std::uint32_t& value) {
    if (offset > payload.size() || payload.size() - offset < 4) {
        return false;
    }

    value = 0;
    for (std::size_t i = 0; i < 4; ++i) {
        value =
            (value << 8U) |
            static_cast<unsigned char>(payload[offset + i]);
    }

    return true;
}

void RoomProtocol::appendU16(std::vector<char>& output, std::uint16_t value) {
    output.push_back(static_cast<char>((value >> 8U) & 0xffU));
    output.push_back(static_cast<char>(value & 0xffU));
}

void RoomProtocol::appendU32(std::vector<char>& output, std::uint32_t value) {
    for (int shift = 24; shift >= 0; shift -= 8) {
        output.push_back(static_cast<char>((value >> shift) & 0xffU));
    }
}

std::optional<CreateRoomRequest> RoomProtocol::decodeCreateRoom(std::span<const char> payload) {
    std::uint16_t name_length = 0;
    std::uint16_t max_players = 0;

    if (!readU16(payload, 0, name_length) || !readU16(payload, 2, max_players)) {
        return std::nullopt;
    }

    if (name_length == 0 || name_length > max_room_name_length ||
        max_players < min_room_players || max_players > max_room_players) {
        return std::nullopt;
    }

    const std::size_t expected_size = create_room_header_size + static_cast<std::size_t>(name_length);
    if (payload.size() != expected_size) {
        return std::nullopt;
    }

    return CreateRoomRequest{
        .room_name = std::string(
            payload.begin() + static_cast<std::ptrdiff_t>(create_room_header_size),
            payload.end()
        ),
        .max_players = max_players,
    };
}

std::optional<std::uint32_t> RoomProtocol::decodeJoinRoom(std::span<const char> payload) {
    if (payload.size() != join_room_payload_size) {
        return std::nullopt;
    }

    std::uint32_t room_id = 0;
    if (!readU32(payload, 0, room_id) || room_id == 0) {
        return std::nullopt;
    }

    return room_id;
}

std::vector<char> RoomProtocol::encodeRoomResult(RoomResult result, std::uint32_t room_id) {
    std::vector<char> output;
    output.reserve(6);

    appendU16(output, static_cast<std::uint16_t>(result));
    appendU32(output, room_id);

    return output;
}

