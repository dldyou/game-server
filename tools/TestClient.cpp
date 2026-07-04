#include "networks/Packet.hpp"
#include "networks/PacketParser.hpp"
#include "networks/PacketSerializer.hpp"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <charconv>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <mutex>
#include <sstream>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <chrono>
#include <vector>

namespace {

std::mutex output_mutex;

void printLine(const std::string& message) {
    std::lock_guard<std::mutex> lock(output_mutex);
    std::cout << message << '\n';
}

bool parseUnsigned(
    std::string_view text,
    std::uint32_t max_value,
    std::uint32_t& value
) {
    std::uint32_t parsed = 0;
    const char* begin = text.data();
    const char* end = begin + text.size();
    const auto result = std::from_chars(begin, end, parsed);

    if (result.ec != std::errc{} ||
        result.ptr != end ||
        parsed > max_value) {
        return false;
    }

    value = parsed;
    return true;
}
bool parseUnsigned64(std::string_view text, std::uint64_t max_value, std::uint64_t& value) {
    std::uint64_t parsed = 0;
    const char* begin = text.data();
    const char* end = begin + text.size();
    const auto result = std::from_chars(begin, end, parsed);

    if (result.ec != std::errc{} || result.ptr != end || parsed > max_value) {
        return false;
    }

    value = parsed;
    return true;
}
bool parseSigned(
    std::string_view text,
    std::int32_t min_value,
    std::int32_t max_value,
    std::int32_t& value
) {
    std::int32_t parsed = 0;
    const char* begin = text.data();
    const char* end = begin + text.size();
    const auto result = std::from_chars(begin, end, parsed);

    if (result.ec != std::errc{} ||
        result.ptr != end ||
        parsed < min_value ||
        parsed > max_value) {
        return false;
    }

    value = parsed;
    return true;
}

int connectToServer(const std::string& host, std::uint16_t port) {
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == -1) {
        perror("socket");
        return -1;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);

    const int conversion_result =
        inet_pton(AF_INET, host.c_str(), &address.sin_addr);
    if (conversion_result != 1) {
        if (conversion_result == 0) {
            std::cerr << "Invalid IPv4 address: " << host << '\n';
        } else {
            perror("inet_pton");
        }
        close(fd);
        return -1;
    }

    if (connect(
            fd,
            reinterpret_cast<const sockaddr*>(&address),
            sizeof(address)
        ) == -1) {
        perror("connect");
        close(fd);
        return -1;
    }

    return fd;
}

bool sendAll(int fd, std::span<const char> buffer) {
    std::size_t sent_bytes = 0;

    while (sent_bytes < buffer.size()) {
        const ssize_t result = send(
            fd,
            buffer.data() + sent_bytes,
            buffer.size() - sent_bytes,
            MSG_NOSIGNAL
        );

        if (result > 0) {
            sent_bytes += static_cast<std::size_t>(result);
            continue;
        }

        if (result == -1 && errno == EINTR) {
            continue;
        }

        if (result == -1) {
            perror("send");
        }
        return false;
    }

    return true;
}

void appendU16(std::vector<char>& buffer, std::uint16_t value) {
    const std::uint16_t network_value = htons(value);
    const auto* bytes =
        reinterpret_cast<const char*>(&network_value);
    buffer.insert(buffer.end(), bytes, bytes + sizeof(network_value));
}
void appendI16(std::vector<char>& buffer, std::int16_t value) {
    const std::uint16_t network_value = htons(static_cast<std::uint16_t>(value));
    const auto* bytes =
        reinterpret_cast<const char*>(&network_value);
    buffer.insert(buffer.end(), bytes, bytes + sizeof(network_value));
}

void appendU32(std::vector<char>& buffer, std::uint32_t value) {
    const std::uint32_t network_value = htonl(value);
    const auto* bytes =
        reinterpret_cast<const char*>(&network_value);
    buffer.insert(buffer.end(), bytes, bytes + sizeof(network_value));
}

void appendU64(std::vector<char>& buffer, std::uint64_t value) {
    for (int shift = 56; shift >= 0; shift -= 8) {
        buffer.push_back(static_cast<char>((value >> shift) & 0xffU));
    }
}
std::vector<char> makeLoginPayload(
    const std::string& id,
    const std::string& password
) {
    if (id.empty() ||
        id.size() > max_login_id_length ||
        password.empty() ||
        password.size() > max_password_length) {
        return {};
    }

    std::vector<char> payload;
    payload.reserve(4 + id.size() + password.size());
    appendU16(payload, static_cast<std::uint16_t>(id.size()));
    appendU16(payload, static_cast<std::uint16_t>(password.size()));
    payload.insert(payload.end(), id.begin(), id.end());
    payload.insert(payload.end(), password.begin(), password.end());
    return payload;
}

std::vector<char> makeCreateRoomPayload(const std::string& name, std::uint16_t max_players) {
    if (name.empty() || name.size() > 64 || max_players == 0 || max_players > 64) {
        return {};
    }

    std::vector<char> payload;
    payload.reserve(4 + name.size());
    appendU16(payload, static_cast<std::uint16_t>(name.size()));
    appendU16(payload, max_players);
    payload.insert(payload.end(), name.begin(), name.end());
    return payload;
}

std::vector<char> makeJoinRoomPayload(std::uint32_t room_id) {
    if (room_id == 0) {
        return {};
    }

    std::vector<char> payload;
    payload.reserve(sizeof(std::uint32_t));
    appendU32(payload, room_id);
    return payload;
}

std::vector<char> makeChatPayload(const std::string& message) {
    if (message.empty() || message.size() > 1024) {
        return {};
    }

    std::vector<char> payload;
    payload.reserve(2 + message.size());
    appendU16(payload, static_cast<std::uint16_t>(message.size()));
    payload.insert(payload.end(), message.begin(), message.end());
    return payload;
}
std::vector<char> makeMovePayload(std::int16_t dx, std::int16_t dy) {
    if (dx < -1 || dx > 1 || dy < -1 || dy > 1) {
        return {};
    }

    std::vector<char> payload;
    payload.reserve(4);
    appendI16(payload, dx);
    appendI16(payload, dy);
    return payload;
}
std::vector<char> makeAttackPayload(std::uint64_t target_user_id) {
    if (target_user_id == 0) {
        return {};
    }

    std::vector<char> payload;
    payload.reserve(8);
    appendU64(payload, target_user_id);
    return payload;
}
std::string packetTypeName(PacketType type) {
    switch (type) {
    case C2S_PING: return "C2S_PING";
    case S2C_PONG: return "S2C_PONG";
    case C2S_LOGIN: return "C2S_LOGIN";
    case S2C_LOGIN_RESULT: return "S2C_LOGIN_RESULT";
    case C2S_CREATE_ROOM: return "C2S_CREATE_ROOM";
    case C2S_JOIN_ROOM: return "C2S_JOIN_ROOM";
    case C2S_LEAVE_ROOM: return "C2S_LEAVE_ROOM";
    case S2C_ROOM_CREATED: return "S2C_ROOM_CREATED";
    case S2C_ROOM_JOINED: return "S2C_ROOM_JOINED";
    case S2C_ROOM_LEFT: return "S2C_ROOM_LEFT";
    case S2C_ROOM_STATE: return "S2C_ROOM_STATE";
    case C2S_ROOM_LIST: return "C2S_ROOM_LIST";
    case S2C_ROOM_LIST: return "S2C_ROOM_LIST";
    case C2S_SET_READY: return "C2S_SET_READY";
    case S2C_READY_SET: return "S2C_READY_SET";
    case C2S_START_GAME: return "C2S_START_GAME";
    case S2C_GAME_STARTED: return "S2C_GAME_STARTED";
    case C2S_CHAT: return "C2S_CHAT";
    case S2C_CHAT: return "S2C_CHAT";
    case C2S_MOVE: return "C2S_MOVE";
    case C2S_ATTACK: return "C2S_ATTACK";
    case S2C_SNAPSHOT: return "S2C_SNAPSHOT";
    case S2C_PLAYER_MOVED: return "S2C_PLAYER_MOVED";
    case S2C_ATTACK: return "S2C_ATTACK";
    case S2C_GAME_ENDED: return "S2C_GAME_ENDED";
    case S2C_ERROR: return "S2C_ERROR";
    }
    return "UNKNOWN";
}

std::string formatPayload(std::span<const char> payload) {
    if (payload.empty()) {
        return {};
    }

    bool printable = true;
    for (char byte : payload) {
        if (std::isprint(
                static_cast<unsigned char>(byte)
            ) == 0) {
            printable = false;
            break;
        }
    }

    std::ostringstream output;
    if (printable) {
        output << " payload=\""
               << std::string(payload.begin(), payload.end())
               << '"';
        return output.str();
    }

    output << " payload_hex=" << std::hex << std::setfill('0');
    for (char byte : payload) {
        output << std::setw(2)
               << static_cast<unsigned int>(
                      static_cast<unsigned char>(byte)
                  );
    }
    return output.str();
}

bool readU16(
    std::span<const char> payload,
    std::size_t offset,
    std::uint16_t& value
) {
    if (offset > payload.size() ||
        payload.size() - offset < sizeof(std::uint16_t)) {
        return false;
    }

    std::uint16_t network_value = 0;
    std::memcpy(
        &network_value,
        payload.data() + offset,
        sizeof(network_value)
    );
    value = ntohs(network_value);
    return true;
}

bool readU64(
    std::span<const char> payload,
    std::size_t offset,
    std::uint64_t& value
) {
    if (offset > payload.size() ||
        payload.size() - offset < sizeof(std::uint64_t)) {
        return false;
    }

    value = 0;
    for (std::size_t i = 0; i < sizeof(std::uint64_t); ++i) {
        value =
            (value << 8U) |
            static_cast<unsigned char>(payload[offset + i]);
    }
    return true;
}

bool readU32(
    std::span<const char> payload,
    std::size_t offset,
    std::uint32_t& value
) {
    if (offset > payload.size() ||
        payload.size() - offset < sizeof(std::uint32_t)) {
        return false;
    }

    std::uint32_t network_value = 0;
    std::memcpy(
        &network_value,
        payload.data() + offset,
        sizeof(network_value)
    );
    value = ntohl(network_value);
    return true;
}
bool readI32(
    std::span<const char> payload,
    std::size_t offset,
    std::int32_t& value
) {
    std::uint32_t raw = 0;
    if (!readU32(payload, offset, raw)) {
        return false;
    }

    if (raw <= 0x7fffffffU) {
        value = static_cast<std::int32_t>(raw);
    } else {
        value = static_cast<std::int32_t>(static_cast<std::int64_t>(raw) - 0x100000000LL);
    }
    return true;
}

std::string loginResultName(std::uint16_t result) {
    switch (result) {
    case 0: return "Success";
    case 1: return "MalformedPayload";
    case 2: return "InvalidCredentials";
    case 3: return "AlreadyAuthenticated";
    case 4: return "InternalError";
    default: return "Unknown";
    }
}

std::string roomResultName(std::uint16_t result) {
    switch (result) {
    case 0: return "Success";
    case 1: return "NotAuthenticated";
    case 2: return "RoomNotFound";
    case 3: return "RoomFull";
    case 4: return "AlreadyInRoom";
    case 5: return "NotInRoom";
    case 6: return "InvalidPayload";
    case 7: return "InternalError";
    case 8: return "RoomInProgress";
    case 9: return "NotOwner";
    case 10: return "NotReady";
    default: return "Unknown";
    }
}

std::string roomStatusName(std::uint16_t status) {
    switch (status) {
    case 0: return "Waiting";
    case 1: return "Playing";
    default: return "Unknown";
    }
}

std::string attackResultName(std::uint16_t result) {
    switch (result) {
    case 0: return "Hit";
    case 1: return "InvalidTarget";
    case 2: return "OutOfRange";
    case 3: return "AttackerDead";
    case 4: return "TargetDead";
    case 5: return "SelfTarget";
    default: return "Unknown";
    }
}

std::string gameEndReasonName(std::uint16_t reason) {
    switch (reason) {
    case 0: return "LastPlayerStanding";
    case 1: return "NoPlayers";
    default: return "Unknown";
    }
}
std::string formatLoginResult(std::span<const char> payload) {
    std::uint16_t result = 0;
    if (!readU16(payload, 0, result)) {
        return " login_result=<malformed>";
    }

    std::ostringstream output;
    output << " login_result=" << loginResultName(result)
           << '(' << result << ')';

    if (result != 0) {
        return output.str();
    }

    std::uint64_t user_id = 0;
    std::uint16_t handle_length = 0;
    if (!readU64(payload, 2, user_id) ||
        !readU16(payload, 10, handle_length) ||
        payload.size() - 12 < handle_length) {
        output << " user=<malformed>";
        return output.str();
    }

    output << " user_id=" << user_id
           << " handle=\""
           << std::string(
                  payload.begin() + 12,
                  payload.begin() + 12 + handle_length
              )
           << '"';
    return output.str();
}

std::string formatRoomResult(std::span<const char> payload) {
    std::uint16_t result = 0;
    std::uint32_t room_id = 0;

    if (payload.size() != 6 || !readU16(payload, 0, result) || !readU32(payload, 2, room_id)) {
        return " room_result=<malformed>";
    }

    std::ostringstream output;
    output << " room_result=" << roomResultName(result)
           << '(' << result << ')'
           << " room_id=" << room_id;
    return output.str();
}

std::string formatRoomList(std::span<const char> payload) {
    std::uint16_t room_count = 0;
    if (!readU16(payload, 0, room_count)) {
        return " room_list=<malformed>";
    }

    std::ostringstream output;
    output << " room_list count=" << room_count << " rooms=[";

    std::size_t offset = 2;
    for (std::uint16_t i = 0; i < room_count; ++i) {
        std::uint32_t room_id = 0;
        std::uint16_t status = 0;
        std::uint16_t name_length = 0;
        if (!readU32(payload, offset, room_id) || !readU16(payload, offset + 4, status) || !readU16(payload, offset + 6, name_length)) {
            return " room_list=<malformed>";
        }

        const std::size_t name_offset = offset + 8;
        if (payload.size() < name_offset + name_length + 4) {
            return " room_list=<malformed>";
        }

        const std::size_t max_players_offset = name_offset + name_length;
        std::uint16_t max_players = 0;
        std::uint16_t player_count = 0;
        if (!readU16(payload, max_players_offset, max_players) || !readU16(payload, max_players_offset + 2, player_count)) {
            return " room_list=<malformed>";
        }

        if (i != 0) {
            output << ',';
        }
        output << room_id << ":\"" << std::string(payload.begin() + static_cast<std::ptrdiff_t>(name_offset), payload.begin() + static_cast<std::ptrdiff_t>(max_players_offset)) << "\"(" << player_count << '/' << max_players << ',' << roomStatusName(status) << ')';
        offset = max_players_offset + 4;
    }

    if (offset != payload.size()) {
        return " room_list=<malformed>";
    }

    output << ']';
    return output.str();
}
std::string formatRoomState(std::span<const char> payload) {
    std::uint32_t room_id = 0;
    std::uint16_t status = 0;
    std::uint64_t owner_user_id = 0;
    std::uint16_t name_length = 0;
    if (!readU32(payload, 0, room_id) || !readU16(payload, 4, status) || !readU64(payload, 6, owner_user_id) || !readU16(payload, 14, name_length)) {
        return " room_state=<malformed>";
    }

    const std::size_t name_offset = 16;
    if (payload.size() < name_offset + name_length + 4) {
        return " room_state=<malformed>";
    }

    const std::size_t max_players_offset = name_offset + name_length;
    std::uint16_t max_players = 0;
    std::uint16_t player_count = 0;
    if (!readU16(payload, max_players_offset, max_players) || !readU16(payload, max_players_offset + 2, player_count)) {
        return " room_state=<malformed>";
    }

    std::ostringstream output;
    output << " room_state room_id=" << room_id
           << " status=" << roomStatusName(status)
           << " owner_user_id=" << owner_user_id
           << " name=\"" << std::string(payload.begin() + static_cast<std::ptrdiff_t>(name_offset), payload.begin() + static_cast<std::ptrdiff_t>(max_players_offset)) << '\"'
           << " max_players=" << max_players
           << " players=[";

    std::size_t offset = max_players_offset + 4;
    for (std::uint16_t i = 0; i < player_count; ++i) {
        std::uint64_t user_id = 0;
        std::uint16_t handle_length = 0;
        if (!readU64(payload, offset, user_id) || payload.size() <= offset + 8 || !readU16(payload, offset + 9, handle_length)) {
            return " room_state=<malformed>";
        }

        const unsigned char ready = static_cast<unsigned char>(payload[offset + 8]);
        if (ready > 1) {
            return " room_state=<malformed>";
        }

        const std::size_t handle_offset = offset + 11;
        if (payload.size() < handle_offset + handle_length) {
            return " room_state=<malformed>";
        }

        if (i != 0) {
            output << ',';
        }
        output << user_id << ':' << std::string(payload.begin() + static_cast<std::ptrdiff_t>(handle_offset), payload.begin() + static_cast<std::ptrdiff_t>(handle_offset + handle_length)) << '(' << (ready == 1 ? "ready" : "not-ready");
        if (user_id == owner_user_id) {
            output << ",owner";
        }
        output << ')';
        offset = handle_offset + handle_length;
    }

    if (offset != payload.size()) {
        return " room_state=<malformed>";
    }

    output << ']';
    return output.str();
}
std::string formatChatMessage(std::span<const char> payload) {
    std::uint64_t user_id = 0;
    std::uint16_t handle_length = 0;
    if (!readU64(payload, 0, user_id) || !readU16(payload, 8, handle_length)) {
        return " chat=<malformed>";
    }

    const std::size_t handle_offset = 10;
    if (payload.size() < handle_offset + handle_length + 2) {
        return " chat=<malformed>";
    }

    const std::size_t message_length_offset = handle_offset + handle_length;
    std::uint16_t message_length = 0;
    if (!readU16(payload, message_length_offset, message_length) ||
        payload.size() != message_length_offset + 2 + message_length) {
        return " chat=<malformed>";
    }

    const std::size_t message_offset = message_length_offset + 2;
    std::ostringstream output;
    output << " chat_user_id=" << user_id
           << " handle=\"" << std::string(payload.begin() + static_cast<std::ptrdiff_t>(handle_offset), payload.begin() + static_cast<std::ptrdiff_t>(message_length_offset)) << '"'
           << " message=\"" << std::string(payload.begin() + static_cast<std::ptrdiff_t>(message_offset), payload.end()) << '"';
    return output.str();
}
std::string formatSnapshot(std::span<const char> payload) {
    std::uint32_t room_id = 0;
    std::uint64_t tick = 0;
    std::uint16_t player_count = 0;
    if (!readU32(payload, 0, room_id) || !readU64(payload, 4, tick) || !readU16(payload, 12, player_count)) {
        return " snapshot=<malformed>";
    }

    std::ostringstream output;
    output << " snapshot room_id=" << room_id << " tick=" << tick << " players=[";

    std::size_t offset = 14;
    for (std::uint16_t i = 0; i < player_count; ++i) {
        std::uint64_t user_id = 0;
        std::int32_t x = 0;
        std::int32_t y = 0;
        std::uint16_t hp = 0;
        if (!readU64(payload, offset, user_id) || !readI32(payload, offset + 8, x) || !readI32(payload, offset + 12, y) || !readU16(payload, offset + 16, hp)) {
            return " snapshot=<malformed>";
        }

        if (i != 0) {
            output << ',';
        }
        output << user_id << ":(" << x << ',' << y << ",hp=" << hp << ')';
        offset += 18;
    }

    if (offset != payload.size()) {
        return " snapshot=<malformed>";
    }

    output << ']';
    return output.str();
}
std::string formatAttackEvent(std::span<const char> payload) {
    std::uint32_t room_id = 0;
    std::uint64_t tick = 0;
    std::uint64_t attacker_user_id = 0;
    std::uint64_t target_user_id = 0;
    std::uint16_t result = 0;
    std::uint16_t damage = 0;
    std::uint16_t target_hp = 0;
    if (payload.size() != 34 || !readU32(payload, 0, room_id) || !readU64(payload, 4, tick) || !readU64(payload, 12, attacker_user_id) || !readU64(payload, 20, target_user_id) || !readU16(payload, 28, result) || !readU16(payload, 30, damage) || !readU16(payload, 32, target_hp)) {
        return " attack=<malformed>";
    }

    std::ostringstream output;
    output << " attack room_id=" << room_id
           << " tick=" << tick
           << " attacker=" << attacker_user_id
           << " target=" << target_user_id
           << " result=" << attackResultName(result) << '(' << result << ')'
           << " damage=" << damage
           << " target_hp=" << target_hp;
    return output.str();
}

std::string formatGameEnded(std::span<const char> payload) {
    std::uint32_t room_id = 0;
    std::uint64_t tick = 0;
    std::uint64_t winner_user_id = 0;
    std::uint16_t reason = 0;
    if (payload.size() != 22 || !readU32(payload, 0, room_id) || !readU64(payload, 4, tick) || !readU64(payload, 12, winner_user_id) || !readU16(payload, 20, reason)) {
        return " game_ended=<malformed>";
    }

    std::ostringstream output;
    output << " game_ended room_id=" << room_id
           << " tick=" << tick
           << " winner_user_id=" << winner_user_id
           << " reason=" << gameEndReasonName(reason) << '(' << reason << ')';
    return output.str();
}
std::string formatPacket(const Packet& packet) {
    std::ostringstream output;
    output << "[recv] type=" << packetTypeName(packet.type)
           << '(' << static_cast<std::uint16_t>(packet.type) << ')'
           << " sequence=" << packet.sequence
           << " payload_size=" << packet.payload.size();

    if (packet.type == S2C_LOGIN_RESULT) {
        output << formatLoginResult(packet.payload);
    } else if (packet.type == S2C_ROOM_CREATED ||
               packet.type == S2C_ROOM_JOINED ||
               packet.type == S2C_ROOM_LEFT ||
               packet.type == S2C_READY_SET ||
               packet.type == S2C_GAME_STARTED) {
        output << formatRoomResult(packet.payload);
    } else if (packet.type == S2C_ROOM_STATE) {
        output << formatRoomState(packet.payload);
    } else if (packet.type == S2C_ROOM_LIST) {
        output << formatRoomList(packet.payload);
    } else if (packet.type == S2C_CHAT) {
        output << formatChatMessage(packet.payload);
    } else if (packet.type == S2C_SNAPSHOT) {
        output << formatSnapshot(packet.payload);
    } else if (packet.type == S2C_ATTACK) {
        output << formatAttackEvent(packet.payload);
    } else if (packet.type == S2C_GAME_ENDED) {
        output << formatGameEnded(packet.payload);
    } else {
        output << formatPayload(packet.payload);
    }
    return output.str();
}

void receivePackets(int fd, std::atomic<bool>& running) {
    std::vector<char> read_buffer(4096);
    std::vector<char> packet_buffer;

    while (running.load()) {
        const ssize_t received = recv(
            fd,
            read_buffer.data(),
            read_buffer.size(),
            0
        );

        if (received > 0) {
            packet_buffer.insert(
                packet_buffer.end(),
                read_buffer.begin(),
                read_buffer.begin() + received
            );

            std::size_t consumed_bytes = 0;
            while (consumed_bytes < packet_buffer.size()) {
                const std::span<const char> remaining(
                    packet_buffer.data() + consumed_bytes,
                    packet_buffer.size() - consumed_bytes
                );
                ParseResult result = PacketParser::parse(remaining);

                if (result.status == ParseStatus::Pending) {
                    break;
                }

                if (result.status == ParseStatus::Invalid ||
                    !result.packet) {
                    printLine("[error] invalid packet received");
                    running = false;
                    return;
                }

                printLine(formatPacket(*result.packet));
                consumed_bytes += result.consumed_bytes;
            }

            if (consumed_bytes > 0) {
                packet_buffer.erase(
                    packet_buffer.begin(),
                    packet_buffer.begin() +
                        static_cast<std::ptrdiff_t>(consumed_bytes)
                );
            }
            continue;
        }

        if (received == 0) {
            printLine("[info] server disconnected");
            running = false;
            return;
        }

        if (errno == EINTR) {
            continue;
        }

        if (running.load()) {
            perror("recv");
        }
        running = false;
        return;
    }
}

void printHelp() {
    printLine(
        "Commands:\n"
        "  ping [message]          send C2S_PING\n"
        "  login <id> <password>   send C2S_LOGIN\n"
        "  create-room <name> <max_players>\n"
        "  join-room <room_id>\n"
        "  leave-room\n"
        "  list-rooms              send C2S_ROOM_LIST\n"
        "  ready                   send C2S_SET_READY true\n"
        "  unready                 send C2S_SET_READY false\n"
        "  start-game              send C2S_START_GAME\n"
        "  move <dx> <dy>          send C2S_MOVE (-1..1)\n"
        "  attack <target_user_id> send C2S_ATTACK\n"
        "  chat <message>          send C2S_CHAT\n"
        "  send <type> [payload]   send an arbitrary packet type\n"
        "  wait [ms]               pause before the next command\n"
        "  help                    show commands\n"
        "  quit                    disconnect"
    );
}

std::string remainingText(std::istringstream& input) {
    std::string value;
    std::getline(input, value);
    if (!value.empty() && value.front() == ' ') {
        value.erase(value.begin());
    }
    return value;
}

} // namespace

int main(int argc, char* argv[]) {
    const std::string host = argc >= 2 ? argv[1] : "127.0.0.1";
    std::uint16_t port = 8080;

    if (argc >= 3) {
        std::uint32_t parsed_port = 0;
        if (!parseUnsigned(
                argv[2],
                std::numeric_limits<std::uint16_t>::max(),
                parsed_port
            ) ||
            parsed_port == 0) {
            std::cerr << "Invalid port: " << argv[2] << '\n';
            return 1;
        }
        port = static_cast<std::uint16_t>(parsed_port);
    }

    const int fd = connectToServer(host, port);
    if (fd == -1) {
        return 1;
    }

    std::cout << std::unitbuf;
    printLine(
        "[info] connected to " + host + ':' + std::to_string(port)
    );
    printHelp();

    std::atomic<bool> running = true;
    std::thread receiver(receivePackets, fd, std::ref(running));
    std::uint32_t next_sequence = 1;
    std::string line;

    while (running.load()) {
        {
            std::lock_guard<std::mutex> lock(output_mutex);
            std::cout << "> ";
        }

        if (!std::getline(std::cin, line)) {
            break;
        }

        std::istringstream input(line);
        std::string command;
        input >> command;

        if (command.empty()) {
            continue;
        }

        if (command == "quit" || command == "exit") {
            break;
        }

        if (command == "help") {
            printHelp();
            continue;
        }

        if (command == "wait") {
            std::uint32_t milliseconds = 500;
            std::string value;
            input >> value;

            if (!value.empty() &&
                !parseUnsigned(value, 60000, milliseconds)) {
                printLine("[error] usage: wait [ms]");
                continue;
            }

            std::this_thread::sleep_for(
                std::chrono::milliseconds(milliseconds)
            );
            continue;
        }

        Packet packet{};
        packet.sequence = next_sequence++;

        if (command == "ping") {
            packet.type = C2S_PING;
            const std::string message = remainingText(input);
            packet.payload.assign(message.begin(), message.end());
        } else if (command == "login") {
            std::string id;
            std::string password;
            input >> id >> password;
            packet.type = C2S_LOGIN;
            packet.payload = makeLoginPayload(id, password);

            if (packet.payload.empty()) {
                printLine(
                    "[error] usage: login <id> <password> "
                    "(id 1-32 bytes, password 1-64 bytes)"
                );
                continue;
            }
        } else if (command == "create-room") {
            std::string name;
            std::string max_players_text;
            input >> name >> max_players_text;

            std::uint32_t max_players = 0;
            if (name.empty() ||
                !parseUnsigned(max_players_text, 64, max_players) ||
                max_players == 0) {
                printLine(
                    "[error] usage: create-room <name> <max_players> "
                    "(name 1-64 bytes, max_players 1-64)"
                );
                continue;
            }

            packet.type = C2S_CREATE_ROOM;
            packet.payload = makeCreateRoomPayload(
                name,
                static_cast<std::uint16_t>(max_players)
            );

            if (packet.payload.empty()) {
                printLine(
                    "[error] usage: create-room <name> <max_players> "
                    "(name 1-64 bytes, max_players 1-64)"
                );
                continue;
            }
        } else if (command == "join-room") {
            std::string room_id_text;
            input >> room_id_text;

            std::uint32_t room_id = 0;
            if (!parseUnsigned(
                    room_id_text,
                    std::numeric_limits<std::uint32_t>::max(),
                    room_id
                ) ||
                room_id == 0) {
                printLine("[error] usage: join-room <room_id>");
                continue;
            }

            packet.type = C2S_JOIN_ROOM;
            packet.payload = makeJoinRoomPayload(room_id);
        } else if (command == "leave-room") {
            packet.type = C2S_LEAVE_ROOM;
        } else if (command == "list-rooms") {
            packet.type = C2S_ROOM_LIST;
        } else if (command == "ready") {
            packet.type = C2S_SET_READY;
            packet.payload = { static_cast<char>(1) };
        } else if (command == "unready") {
            packet.type = C2S_SET_READY;
            packet.payload = { static_cast<char>(0) };
        } else if (command == "start-game") {
            packet.type = C2S_START_GAME;
        } else if (command == "move") {
            std::string dx_text;
            std::string dy_text;
            input >> dx_text >> dy_text;

            std::int32_t dx = 0;
            std::int32_t dy = 0;
            if (!parseSigned(dx_text, -1, 1, dx) || !parseSigned(dy_text, -1, 1, dy)) {
                printLine("[error] usage: move <dx> <dy> (each -1..1)");
                continue;
            }

            packet.type = C2S_MOVE;
            packet.payload = makeMovePayload(static_cast<std::int16_t>(dx), static_cast<std::int16_t>(dy));
        } else if (command == "attack") {
            std::string target_text;
            input >> target_text;

            std::uint64_t target_user_id = 0;
            if (!parseUnsigned64(target_text, std::numeric_limits<std::uint64_t>::max(), target_user_id) || target_user_id == 0) {
                printLine("[error] usage: attack <target_user_id>");
                continue;
            }

            packet.type = C2S_ATTACK;
            packet.payload = makeAttackPayload(target_user_id);
        } else if (command == "chat") {
            packet.type = C2S_CHAT;
            const std::string message = remainingText(input);
            packet.payload = makeChatPayload(message);

            if (packet.payload.empty()) {
                printLine("[error] usage: chat <message> (message 1-1024 bytes)");
                continue;
            }
        } else if (command == "send") {
            std::string type_text;
            input >> type_text;

            std::uint32_t type_value = 0;
            if (!parseUnsigned(
                    type_text,
                    std::numeric_limits<std::uint16_t>::max(),
                    type_value
                )) {
                printLine("[error] usage: send <type> [payload]");
                continue;
            }

            packet.type =
                static_cast<PacketType>(
                    static_cast<std::uint16_t>(type_value)
                );
            const std::string payload = remainingText(input);
            packet.payload.assign(payload.begin(), payload.end());
        } else {
            printLine("[error] unknown command: " + command);
            continue;
        }

        std::vector<char> serialized =
            PacketSerializer::serialize(packet);
        if (serialized.empty()) {
            printLine("[error] packet is too large");
            continue;
        }

        if (!sendAll(fd, serialized)) {
            running = false;
            break;
        }

        printLine(
            "[sent] type=" + packetTypeName(packet.type) +
            '(' +
            std::to_string(
                static_cast<std::uint16_t>(packet.type)
            ) +
            ") sequence=" + std::to_string(packet.sequence) +
            " payload_size=" +
            std::to_string(packet.payload.size())
        );
    }

    running = false;
    shutdown(fd, SHUT_RDWR);
    if (receiver.joinable()) {
        receiver.join();
    }
    close(fd);
    printLine("[info] disconnected");
    return 0;
}
