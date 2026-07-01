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

void appendU32(std::vector<char>& buffer, std::uint32_t value) {
    const std::uint32_t network_value = htonl(value);
    const auto* bytes =
        reinterpret_cast<const char*>(&network_value);
    buffer.insert(buffer.end(), bytes, bytes + sizeof(network_value));
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
    case C2S_CHAT: return "C2S_CHAT";
    case S2C_CHAT: return "S2C_CHAT";
    case C2S_MOVE: return "C2S_MOVE";
    case C2S_ATTACK: return "C2S_ATTACK";
    case S2C_SNAPSHOT: return "S2C_SNAPSHOT";
    case S2C_PLAYER_MOVED: return "S2C_PLAYER_MOVED";
    case S2C_ATTACK: return "S2C_ATTACK";
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
               packet.type == S2C_ROOM_LEFT) {
        output << formatRoomResult(packet.payload);
    } else if (packet.type == S2C_CHAT) {
        output << formatChatMessage(packet.payload);
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
