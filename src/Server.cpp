#include "Server.hpp"

#include "networks/PacketParser.hpp"
#include "networks/PacketSerializer.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <unistd.h>
#include "users/UserManager.hpp"
#include "auth/LoginProtocol.hpp"
#include "rooms/RoomProtocol.hpp"
#include "chat/ChatProtocol.hpp"
#include "game/GameProtocol.hpp"

#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <cstdint>
#include <chrono>
#include <iostream>
#include <span>
#include <utility>

namespace {
    constexpr auto game_tick_interval = std::chrono::milliseconds(50);
}

bool Server::handleLogin(int epoll_fd, Session& session, const Packet& packet) {
    if (session.isAuthenticated()) {
        return sendLoginResult(
            epoll_fd, session, packet.sequence,
            { LoginResult::AlreadyAuthenticated, std::nullopt }
        );
    }

    auto request = LoginProtocol::decodeRequest(packet.payload);
    if (!request) {
        return sendLoginResult(
            epoll_fd, session, packet.sequence,
            { LoginResult::MalformedPayload, std::nullopt }
        );
    }

    AuthenticationResult result = user_manager.authenticate(
        request->login_id,
        request->password
    );

    if (result.result == LoginResult::Success && result.user) {
        session.authenticate(*result.user);
    }

    return sendLoginResult(
        epoll_fd,
        session,
        packet.sequence,
        { result.result, result.user }
    );
}

bool Server::handleCreateRoom(int epoll_fd, Session& session, const Packet& packet) {
    const AuthenticatedUser* user = session.authenticatedUser();
    if (user == nullptr) {
        return sendRoomResult(epoll_fd, session, packet.sequence, S2C_ROOM_CREATED, RoomResult::NotAuthenticated, 0);
    }

    auto request = RoomProtocol::decodeCreateRoom(packet.payload);
    if (!request) {
        return sendRoomResult(epoll_fd, session, packet.sequence, S2C_ROOM_CREATED, RoomResult::InvalidPayload, 0);
    }

    RoomOperationResult result = room_manager.createRoom(*user, session.fd(), *request);
    const std::uint32_t room_id = result.room_id.value_or(0);
    if (result.result == RoomResult::Success) {
        session.enterRoom(room_id);
    }

    if (!sendRoomResult(epoll_fd, session, packet.sequence, S2C_ROOM_CREATED, result.result, room_id)) {
        return false;
    }

    if (result.result == RoomResult::Success) {
        auto state = room_manager.roomState(room_id);
        if (state && !broadcastRoomState(epoll_fd, packet.sequence, *state)) {
            return false;
        }
    }

    return true;
}

bool Server::handleJoinRoom(int epoll_fd, Session& session, const Packet& packet) {
    const AuthenticatedUser* user = session.authenticatedUser();
    if (user == nullptr) {
        return sendRoomResult(epoll_fd, session, packet.sequence, S2C_ROOM_JOINED, RoomResult::NotAuthenticated, 0);
    }

    auto room_id = RoomProtocol::decodeJoinRoom(packet.payload);
    if (!room_id) {
        return sendRoomResult(epoll_fd, session, packet.sequence, S2C_ROOM_JOINED, RoomResult::InvalidPayload, 0);
    }

    RoomOperationResult result = room_manager.joinRoom(*user, session.fd(), *room_id);
    const std::uint32_t result_room_id = result.room_id.value_or(0);
    if (result.result == RoomResult::Success) {
        session.enterRoom(result_room_id);
    }

    if (!sendRoomResult(epoll_fd, session, packet.sequence, S2C_ROOM_JOINED, result.result, result_room_id)) {
        return false;
    }

    if (result.result == RoomResult::Success) {
        auto state = room_manager.roomState(result_room_id);
        if (state && !broadcastRoomState(epoll_fd, packet.sequence, *state)) {
            return false;
        }
    }

    return true;
}

bool Server::handleLeaveRoom(int epoll_fd, Session& session, const Packet& packet) {
    const AuthenticatedUser* user = session.authenticatedUser();
    if (user == nullptr) {
        return sendRoomResult(epoll_fd, session, packet.sequence, S2C_ROOM_LEFT, RoomResult::NotAuthenticated, 0);
    }

    if (!packet.payload.empty()) {
        return sendRoomResult(epoll_fd, session, packet.sequence, S2C_ROOM_LEFT, RoomResult::InvalidPayload, 0);
    }

    RoomOperationResult result = room_manager.leaveRoom(user->user_id);
    const std::uint32_t room_id = result.room_id.value_or(0);
    if (result.result == RoomResult::Success) {
        session.leaveRoom();
        game_manager.removePlayer(room_id, user->user_id);
    }

    if (!sendRoomResult(epoll_fd, session, packet.sequence, S2C_ROOM_LEFT, result.result, room_id)) {
        return false;
    }

    if (result.result == RoomResult::Success) {
        auto state = room_manager.roomState(room_id);
        if (state) {
            if (!broadcastRoomState(epoll_fd, packet.sequence, *state)) {
                return false;
            }
        } else {
            game_manager.removeGame(room_id);
        }
    }

    return true;
}

bool Server::handleRoomList(int epoll_fd, Session& session, const Packet& packet) {
    if (!packet.payload.empty()) {
        std::cerr << "Invalid room list payload from session " << session.fd() << "\n";
        return true;
    }

    return sendRoomList(epoll_fd, session, packet.sequence, room_manager.roomList());
}

bool Server::handleSetReady(int epoll_fd, Session& session, const Packet& packet) {
    const AuthenticatedUser* user = session.authenticatedUser();
    if (user == nullptr) {
        return sendRoomResult(epoll_fd, session, packet.sequence, S2C_READY_SET, RoomResult::NotAuthenticated, 0);
    }

    if (packet.payload.size() != 1 || (packet.payload[0] != 0 && packet.payload[0] != 1)) {
        return sendRoomResult(epoll_fd, session, packet.sequence, S2C_READY_SET, RoomResult::InvalidPayload, 0);
    }

    RoomOperationResult result = room_manager.setReady(user->user_id, packet.payload[0] == 1);
    const std::uint32_t room_id = result.room_id.value_or(0);
    if (!sendRoomResult(epoll_fd, session, packet.sequence, S2C_READY_SET, result.result, room_id)) {
        return false;
    }

    if (result.result == RoomResult::Success) {
        auto state = room_manager.roomState(room_id);
        if (state && !broadcastRoomState(epoll_fd, packet.sequence, *state)) {
            return false;
        }
    }

    return true;
}

bool Server::handleStartGame(int epoll_fd, Session& session, const Packet& packet) {
    const AuthenticatedUser* user = session.authenticatedUser();
    if (user == nullptr) {
        return sendRoomResult(epoll_fd, session, packet.sequence, S2C_GAME_STARTED, RoomResult::NotAuthenticated, 0);
    }

    if (!packet.payload.empty()) {
        return sendRoomResult(epoll_fd, session, packet.sequence, S2C_GAME_STARTED, RoomResult::InvalidPayload, 0);
    }

    RoomOperationResult result = room_manager.startGame(user->user_id);
    const std::uint32_t room_id = result.room_id.value_or(0);
    if (!sendRoomResult(epoll_fd, session, packet.sequence, S2C_GAME_STARTED, result.result, room_id)) {
        return false;
    }

    if (result.result == RoomResult::Success) {
        auto state = room_manager.roomState(room_id);
        if (!state) {
            return false;
        }

        if (!game_manager.createGame(*state)) {
            std::cerr << "Failed to create game for room " << room_id << "\n";
            return false;
        }

        if (!broadcastRoomState(epoll_fd, packet.sequence, *state)) {
            return false;
        }
    }

    return true;
}

bool Server::handleMove(int, Session& session, const Packet& packet) {
    const AuthenticatedUser* user = session.authenticatedUser();
    if (user == nullptr) {
        std::cerr << "Unauthenticated move packet\n";
        return true;
    }

    auto move = GameProtocol::decodeMove(user->user_id, packet.payload);
    if (!move) {
        std::cerr << "Invalid move payload from session " << session.fd() << "\n";
        return true;
    }

    auto room_id = session.currentRoomId();
    if (!room_id) {
        std::cerr << "Move from session outside room: " << session.fd() << "\n";
        return true;
    }

    if (!game_manager.queueMove(*room_id, *move)) {
        std::cerr << "Move ignored for user " << user->user_id << " in room " << *room_id << "\n";
    }
    return true;
}

bool Server::handleAttack(int, Session& session, const Packet& packet) {
    const AuthenticatedUser* user = session.authenticatedUser();
    if (user == nullptr) {
        std::cerr << "Unauthenticated attack packet\n";
        return true;
    }

    auto attack = GameProtocol::decodeAttack(user->user_id, packet.payload);
    if (!attack) {
        std::cerr << "Invalid attack payload from session " << session.fd() << "\n";
        return true;
    }

    auto room_id = session.currentRoomId();
    if (!room_id) {
        std::cerr << "Attack from session outside room: " << session.fd() << "\n";
        return true;
    }

    if (!game_manager.queueAttack(*room_id, *attack)) {
        std::cerr << "Attack ignored for user " << user->user_id << " in room " << *room_id << "\n";
    }
    return true;
}

bool Server::handleChat(int epoll_fd, Session& session, const Packet& packet) {
    const AuthenticatedUser* user = session.authenticatedUser();
    if (user == nullptr) {
        std::cerr << "Unauthenticated chat packet\n";
        return true;
    }

    auto chat = ChatProtocol::decodeMessage(packet.payload);
    if (!chat) {
        std::cerr << "Invalid chat payload from session " << session.fd() << "\n";
        return true;
    }

    if (!session.isInRoom()) {
        std::cerr << "Chat from session outside room: " << session.fd() << "\n";
        return true;
    }

    std::vector<RoomPlayer> recipients = room_manager.playersInSameRoom(user->user_id);
    if (recipients.empty()) {
        std::cerr << "Chat from user outside room: " << user->user_id << "\n";
        return true;
    }

    Packet chat_packet{
        .type = S2C_CHAT,
        .sequence = packet.sequence,
        .payload = ChatProtocol::encodeMessage({
            .user_id = user->user_id,
            .handle = user->handle,
            .message = chat->message,
        }),
    };

    if (chat_packet.payload.empty()) {
        std::cerr << "Failed to encode chat payload\n";
        return false;
    }

    for (const RoomPlayer& recipient : recipients) {
        auto session_it = sessions.find(recipient.session_fd);
        if (session_it == sessions.end()) {
            continue;
        }

        if (!queuePacket(epoll_fd, session_it->second, chat_packet)) {
            return false;
        }
    }

    return true;
}

bool Server::sendLoginResult(int epoll_fd, Session& session, std::uint32_t sequence, LoginResponse response) {
    Packet packet{
        .type = S2C_LOGIN_RESULT,
        .sequence = sequence,
        .payload = LoginProtocol::encodeResponse(response),
    };

    return queuePacket(epoll_fd, session, packet);
}

bool Server::sendRoomResult(int epoll_fd, Session& session, std::uint32_t sequence, PacketType type, RoomResult result, std::uint32_t room_id) {
    Packet packet{
        .type = type,
        .sequence = sequence,
        .payload = RoomProtocol::encodeRoomResult(result, room_id),
    };

    return queuePacket(epoll_fd, session, packet);
}

bool Server::sendRoomState(int epoll_fd, Session& session, std::uint32_t sequence, const RoomState& state) {
    Packet packet{
        .type = S2C_ROOM_STATE,
        .sequence = sequence,
        .payload = RoomProtocol::encodeRoomState(state),
    };

    if (packet.payload.empty()) {
        std::cerr << "Failed to encode room state payload\n";
        return false;
    }

    return queuePacket(epoll_fd, session, packet);
}

bool Server::sendRoomList(int epoll_fd, Session& session, std::uint32_t sequence, const std::vector<RoomSummary>& rooms) {
    Packet packet{
        .type = S2C_ROOM_LIST,
        .sequence = sequence,
        .payload = RoomProtocol::encodeRoomList(rooms),
    };

    if (packet.payload.empty()) {
        std::cerr << "Failed to encode room list payload\n";
        return false;
    }

    return queuePacket(epoll_fd, session, packet);
}

bool Server::sendGameSnapshot(int epoll_fd, Session& session, const GameSnapshot& snapshot) {
    Packet packet{
        .type = S2C_SNAPSHOT,
        .sequence = static_cast<std::uint32_t>(snapshot.tick & 0xffffffffULL),
        .payload = GameProtocol::encodeSnapshot(snapshot),
    };

    if (packet.payload.empty()) {
        std::cerr << "Failed to encode game snapshot payload\n";
        return false;
    }

    return queuePacket(epoll_fd, session, packet);
}

bool Server::sendAttackEvent(int epoll_fd, Session& session, const GameAttackEvent& event) {
    Packet packet{
        .type = S2C_ATTACK,
        .sequence = static_cast<std::uint32_t>(event.tick & 0xffffffffULL),
        .payload = GameProtocol::encodeAttackEvent(event),
    };

    if (packet.payload.empty()) {
        std::cerr << "Failed to encode game attack payload\n";
        return false;
    }

    return queuePacket(epoll_fd, session, packet);
}

bool Server::sendGameEnded(int epoll_fd, Session& session, const GameEndEvent& event) {
    Packet packet{
        .type = S2C_GAME_ENDED,
        .sequence = static_cast<std::uint32_t>(event.tick & 0xffffffffULL),
        .payload = GameProtocol::encodeGameEnded(event),
    };

    if (packet.payload.empty()) {
        std::cerr << "Failed to encode game ended payload\n";
        return false;
    }

    return queuePacket(epoll_fd, session, packet);
}

bool Server::broadcastGameSnapshot(int epoll_fd, const GameSnapshot& snapshot) {
    for (const GamePlayerState& player : snapshot.players) {
        auto session_it = sessions.find(player.session_fd);
        if (session_it == sessions.end()) {
            continue;
        }

        if (!sendGameSnapshot(epoll_fd, session_it->second, snapshot)) {
            return false;
        }
    }
    return true;
}

bool Server::broadcastAttackEvent(int epoll_fd, const GameSnapshot& snapshot, const GameAttackEvent& event) {
    for (const GamePlayerState& player : snapshot.players) {
        auto session_it = sessions.find(player.session_fd);
        if (session_it == sessions.end()) {
            continue;
        }

        if (!sendAttackEvent(epoll_fd, session_it->second, event)) {
            return false;
        }
    }
    return true;
}

bool Server::broadcastGameEnded(int epoll_fd, const GameSnapshot& snapshot, const GameEndEvent& event) {
    for (const GamePlayerState& player : snapshot.players) {
        auto session_it = sessions.find(player.session_fd);
        if (session_it == sessions.end()) {
            continue;
        }

        if (!sendGameEnded(epoll_fd, session_it->second, event)) {
            return false;
        }
    }
    return true;
}

bool Server::broadcastRoomState(int epoll_fd, std::uint32_t sequence, const RoomState& state) {
    for (const RoomPlayer& player : state.players) {
        auto session_it = sessions.find(player.session_fd);
        if (session_it == sessions.end()) {
            continue;
        }

        if (!sendRoomState(epoll_fd, session_it->second, sequence, state)) {
            return false;
        }
    }

    return true;
}

bool Server::requiresAuthentication(PacketType type) const {
    switch (type) {
    case C2S_CREATE_ROOM:
    case C2S_JOIN_ROOM:
    case C2S_LEAVE_ROOM:
    case C2S_ROOM_LIST:
    case C2S_SET_READY:
    case C2S_START_GAME:
    case C2S_CHAT:
    case C2S_MOVE:
    case C2S_ATTACK:
        return true;
    default:
        return false;
    }
}

bool Server::processPackets(int epoll_fd, Session& session) {
    std::vector<char>& buffer = session.recvBuffer();
    std::size_t consumed_bytes = 0;

    while (consumed_bytes < buffer.size()) {
        const std::span<const char> remaining(
            buffer.data() + consumed_bytes,
            buffer.size() - consumed_bytes
        );

        ParseResult result = PacketParser::parse(remaining);

        if (result.status == ParseStatus::Pending) {
            break;
        }

        if (result.status == ParseStatus::Invalid || !result.packet) {
            return false;
        }

        if (!handlePacket(epoll_fd, session, *result.packet)) {
            return false;
        }

        consumed_bytes += result.consumed_bytes;
    }

    if (consumed_bytes > 0) {
        buffer.erase(
            buffer.begin(),
            buffer.begin() + static_cast<std::ptrdiff_t>(consumed_bytes)
        );
    }

    return true;
}

bool Server::handlePacket(int epoll_fd, Session& session, const Packet& packet) {
    if (requiresAuthentication(packet.type) && !session.isAuthenticated()) {
        std::cerr << "Unauthenticated packet: " << static_cast<std::uint16_t>(packet.type) << "\n";
        return true;
    }

    switch (packet.type) {
    case C2S_PING: {
        Packet pong{
            .type = S2C_PONG,
            .sequence = packet.sequence,
            .payload = packet.payload,
        };
        return queuePacket(epoll_fd, session, pong);
    }
    case C2S_LOGIN:
        return handleLogin(epoll_fd, session, packet);
    case C2S_CREATE_ROOM:
        return handleCreateRoom(epoll_fd, session, packet);
    case C2S_JOIN_ROOM:
        return handleJoinRoom(epoll_fd, session, packet);
    case C2S_LEAVE_ROOM:
        return handleLeaveRoom(epoll_fd, session, packet);
    case C2S_ROOM_LIST:
        return handleRoomList(epoll_fd, session, packet);
    case C2S_SET_READY:
        return handleSetReady(epoll_fd, session, packet);
    case C2S_START_GAME:
        return handleStartGame(epoll_fd, session, packet);
    case C2S_CHAT:
        return handleChat(epoll_fd, session, packet);
    case C2S_MOVE:
        return handleMove(epoll_fd, session, packet);
    case C2S_ATTACK:
        return handleAttack(epoll_fd, session, packet);
    default:
        std::cerr << "Unknown packet type: " << static_cast<std::uint16_t>(packet.type) << "\n";
        return true;
    }
}

bool Server::queuePacket(int epoll_fd, Session& session, const Packet& packet) {
    std::vector<char> buffer = PacketSerializer::serialize(packet);

    if (buffer.empty()) {
        std::cerr << "Failed to serialize packet type " << static_cast<std::uint16_t>(packet.type) << "\n";
        return false;
    }

    const bool enable_write = !session.hasPendingSend();

    if (!session.enqueueSend(std::move(buffer), max_pending_send_bytes)) {
        std::cerr << "Send queue limit exceeded for session " << session.fd() << "\n";
        return false;
    }

    if (enable_write && !updateClientEvents(epoll_fd, session)) {
        return false;
    }

    return true;
}

bool Server::flushSendQueue(int epoll_fd, Session& session) {
    while (session.hasPendingSend()) {
        const std::vector<char>& buffer = session.frontSendBuffer();
        const std::size_t offset = session.sendOffset();
        const std::size_t remaining = buffer.size() - offset;

        const ssize_t sent = send(
            session.fd(),
            buffer.data() + offset,
            remaining,
            MSG_NOSIGNAL
        );

        if (sent > 0) {
            session.advanceSend(static_cast<std::size_t>(sent));
            continue;
        }

        if (sent == -1 && errno == EINTR) {
            continue;
        }

        if (sent == -1 &&
            (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return true;
        }

        if (sent == -1) {
            perror("send");
        }
        return false;
    }

    if (session.isPeerClosed()) {
        return true;
    }

    return updateClientEvents(epoll_fd, session);
}

void Server::initClients() {
    sessions.clear();
}

bool Server::setNonBlocking(int fd) {
    const int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        perror("fcntl(F_GETFL)");
        return false;
    }

    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        perror("fcntl(F_SETFL)");
        return false;
    }

    return true;
}

bool Server::updateClientEvents(int epoll_fd, const Session& session) {
    epoll_event event{};
    event.events = EPOLLRDHUP;

    if (!session.isPeerClosed()) {
        event.events |= EPOLLIN;
    }

    if (session.hasPendingSend()) {
        event.events |= EPOLLOUT;
    }

    event.data.fd = session.fd();

    if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD, session.fd(), &event) == -1) {
        perror("epoll_ctl(MOD)");
        return false;
    }

    return true;
}

void Server::closeClient(int epoll_fd, int client_fd) {
    if (client_fd < 0) {
        return;
    }

    if (epoll_fd != -1) {
        if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, nullptr) == -1 &&
            errno != ENOENT && errno != EBADF) {
            perror("epoll_ctl(DEL)");
        }
    }

    std::uint32_t removed_room_id = 0;
    auto session_it = sessions.find(client_fd);
    if (session_it != sessions.end()) {
        if (const AuthenticatedUser* user = session_it->second.authenticatedUser()) {
            if (auto room_id = room_manager.roomIdOf(user->user_id)) {
                removed_room_id = *room_id;
            }
            room_manager.removeSession(user->user_id);
            if (removed_room_id != 0) {
                game_manager.removePlayer(removed_room_id, user->user_id);
            }
        }
    }

    sessions.erase(client_fd);

    if (removed_room_id != 0) {
        auto state = room_manager.roomState(removed_room_id);
        if (state) {
            if (!broadcastRoomState(epoll_fd, 0, *state)) {
                std::cerr << "Failed to broadcast room state after disconnect\n";
            }
        } else {
            game_manager.removeGame(removed_room_id);
        }
    }

    close(client_fd);
}

void Server::cleanupClients() {
    for (const auto& item : sessions) {
        if (const AuthenticatedUser* user = item.second.authenticatedUser()) {
            room_manager.removeSession(user->user_id);
        }
        close(item.first);
    }
    sessions.clear();
}

int Server::init(const ServerConfig& server_config) {
    int server_fd = 0;
    sockaddr_in server_addr{};

    if (server_config.max_clients <= 0) {
        std::cerr << "Invalid max_clients value: " << server_config.max_clients << "\n";
        return -1;
    }

    if (server_config.buffer_size < 2) {
        std::cerr << "Invalid buffer_size value: " << server_config.buffer_size << "\n";
        return -1;
    }

    initClients();
    user_manager.createUser({ .id = 1, .handle = "tester", .login_id = "test", .password = "password" });
    user_manager.createUser({ .id = 2, .handle = "tester2", .login_id = "test2", .password = "password" });
    user_manager.createUser({ .id = 3, .handle = "tester3", .login_id = "test3", .password = "password" });
    user_manager.createUser({ .id = 4, .handle = "tester4", .login_id = "test4", .password = "password" });

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("socket");
        return -1;
    }

    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        perror("setsockopt");
        close(server_fd);
        return -1;
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_config.port);

    const int result = inet_pton(AF_INET, server_config.host.c_str(), &server_addr.sin_addr);

    if (result == 0) {
        std::cerr << "Invalid IPv4 address: " << server_config.host << "\n";
        close(server_fd);
        return -1;
    }

    if (result == -1) {
        perror("inet_pton");
        close(server_fd);
        return -1;
    }

    if (bind(server_fd, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr)) == -1) {
        perror("bind");
        close(server_fd);
        return -1;
    }

    if (!setNonBlocking(server_fd)) {
        close(server_fd);
        return -1;
    }

    if (listen(server_fd, server_config.max_clients) == -1) {
        perror("listen");
        close(server_fd);
        return -1;
    }

    return server_fd;
}

void Server::run(int server_fd, const ServerConfig& server_config) {
    const int max_clients = server_config.max_clients;
    const int max_events = max_clients + 2;
    const std::size_t max_sessions = static_cast<std::size_t>(max_clients);
    const std::size_t buffer_size = server_config.buffer_size;

    if (server_fd < 0 || max_clients <= 0 || buffer_size < 2) {
        std::cerr << "Invalid server run parameters\n";
        return;
    }

    is_running = true;

    std::vector<char> buffer_in(buffer_size);
    int epoll_fd = -1;
    int event_count = 0;
    int local_wake_fd = -1;
    std::vector<epoll_event> events(static_cast<std::size_t>(max_events));

    socklen_t sockaddr_len = sizeof(sockaddr_in);
    auto next_game_tick = std::chrono::steady_clock::now() + game_tick_interval;

    if ((epoll_fd = epoll_create1(EPOLL_CLOEXEC)) == -1) {
        perror("epoll_create1");
        close(server_fd);
        is_running = false;
        return;
    }

    epoll_event event{};
    event.events = EPOLLIN;
    event.data.fd = server_fd;

    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &event) == -1) {
        perror("epoll_ctl(ADD server)");
        close(epoll_fd);
        close(server_fd);
        is_running = false;
        return;
    }

    local_wake_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (local_wake_fd == -1) {
        perror("eventfd");
        close(epoll_fd);
        close(server_fd);
        is_running = false;
        return;
    }

    {
        std::lock_guard<std::mutex> lock(wake_mutex);
        wake_fd = local_wake_fd;
    }

    event = {};
    event.events = EPOLLIN;
    event.data.fd = local_wake_fd;

    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, local_wake_fd, &event) == -1) {
        perror("epoll_ctl(ADD wake)");
        {
            std::lock_guard<std::mutex> lock(wake_mutex);
            if (wake_fd == local_wake_fd) {
                wake_fd = -1;
            }
            close(local_wake_fd);
            local_wake_fd = -1;
        }
        close(epoll_fd);
        close(server_fd);
        is_running = false;
        return;
    }

    while (is_running) {
        const auto now = std::chrono::steady_clock::now();
        int timeout_ms = 0;
        if (now < next_game_tick) {
            const auto remaining_ms = std::chrono::duration_cast<std::chrono::milliseconds>(next_game_tick - now).count();
            timeout_ms = remaining_ms > 0 ? static_cast<int>(remaining_ms) : 1;
        }

        event_count = epoll_wait(epoll_fd, events.data(), max_events, timeout_ms);

        if (event_count == -1) {
            if (errno == EINTR) {
                continue;
            }
            perror("epoll_wait");
            break;
        }

        const auto after_wait = std::chrono::steady_clock::now();
        while (after_wait >= next_game_tick) {
            std::vector<GameTickResult> results = game_manager.tickAll();
            for (const GameTickResult& result : results) {
                for (const GameAttackEvent& attack : result.attacks) {
                    if (!broadcastAttackEvent(epoll_fd, result.snapshot, attack)) {
                        std::cerr << "Failed to broadcast attack event for room " << attack.room_id << "\n";
                    }
                }

                if (!broadcastGameSnapshot(epoll_fd, result.snapshot)) {
                    std::cerr << "Failed to broadcast game snapshot for room " << result.snapshot.room_id << "\n";
                }

                if (result.ended) {
                    if (!broadcastGameEnded(epoll_fd, result.snapshot, *result.ended)) {
                        std::cerr << "Failed to broadcast game end for room " << result.ended->room_id << "\n";
                    }

                    if (!room_manager.finishGame(result.ended->room_id)) {
                        std::cerr << "Failed to finish room " << result.ended->room_id << " after game end\n";
                    } else if (auto state = room_manager.roomState(result.ended->room_id)) {
                        if (!broadcastRoomState(epoll_fd, 0, *state)) {
                            std::cerr << "Failed to broadcast room state after game end\n";
                        }
                    }
                }
            }
            next_game_tick += game_tick_interval;
        }

        if (event_count == 0) {
            continue;
        }

        for (int i = 0; i < event_count; ++i) {
            const int event_fd = events[i].data.fd;
            const std::uint32_t event_flags = events[i].events;

            if (event_fd == local_wake_fd) {
                eventfd_t value;
                if (eventfd_read(local_wake_fd, &value) == -1 &&
                    errno != EAGAIN && errno != EWOULDBLOCK) {
                    perror("eventfd_read");
                }

                if (!is_running) {
                    break;
                }
                continue;
            }

            if (event_fd == server_fd) {
                int client_fd;
                sockaddr_in client_addr{};

                sockaddr_len = sizeof(client_addr);
                client_fd = accept(server_fd, reinterpret_cast<sockaddr*>(&client_addr), &sockaddr_len);

                if (client_fd == -1) {
                    if (errno == EAGAIN ||
                        errno == EWOULDBLOCK ||
                        errno == EINTR) {
                        continue;
                    }
                    perror("accept");
                    continue;
                }

                if (sessions.size() >= max_sessions) {
                    closeClient(-1, client_fd);
                    continue;
                }

                if (!setNonBlocking(client_fd)) {
                    closeClient(-1, client_fd);
                    continue;
                }

                event = {};
                event.events = EPOLLIN | EPOLLRDHUP;
                event.data.fd = client_fd;

                if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &event) == -1) {
                    perror("epoll_ctl(ADD client)");
                    closeClient(-1, client_fd);
                    continue;
                }

                if (!sessions.emplace(client_fd, Session(client_fd)).second) {
                    closeClient(epoll_fd, client_fd);
                }
                continue;
            }

            auto session_it = sessions.find(event_fd);
            if (session_it == sessions.end()) {
                continue;
            }

            Session& session = session_it->second;
            bool close_session = false;

            if ((event_flags & EPOLLIN) != 0) {
                while (true) {
                    const ssize_t num_bytes = recv(event_fd, buffer_in.data(), buffer_in.size(), 0);

                    if (num_bytes > 0) {
                        std::vector<char>& recv_buffer = session.recvBuffer();
                        recv_buffer.insert(recv_buffer.end(), buffer_in.begin(), buffer_in.begin() + num_bytes);

                        if (!processPackets(epoll_fd, session)) {
                            close_session = true;
                            break;
                        }
                        continue;
                    }

                    if (num_bytes == 0) {
                        session.markPeerClosed();
                        break;
                    }

                    if (errno == EINTR) {
                        continue;
                    }

                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        break;
                    }

                    close_session = true;
                    break;
                }
            }

            if (close_session) {
                closeClient(epoll_fd, event_fd);
                continue;
            }

            if ((event_flags & (EPOLLHUP | EPOLLRDHUP)) != 0) {
                session.markPeerClosed();
            }

            if ((event_flags & EPOLLERR) != 0) {
                closeClient(epoll_fd, event_fd);
                continue;
            }

            if ((event_flags & EPOLLOUT) != 0 || (session.isPeerClosed() && session.hasPendingSend())) {
                if (!flushSendQueue(epoll_fd, session)) {
                    closeClient(epoll_fd, event_fd);
                    continue;
                }
            }

            if (session.isPeerClosed()) {
                if (!session.hasPendingSend()) {
                    closeClient(epoll_fd, event_fd);
                    continue;
                }

                if (!updateClientEvents(epoll_fd, session)) {
                    closeClient(epoll_fd, event_fd);
                }
            }
        }
    }

    cleanupClients();

    if (local_wake_fd != -1) {
        std::lock_guard<std::mutex> lock(wake_mutex);
        if (wake_fd == local_wake_fd) {
            wake_fd = -1;
        }
        close(local_wake_fd);
    }

    close(epoll_fd);
    close(server_fd);
}

void Server::stop() {
    is_running = false;

    std::lock_guard<std::mutex> lock(wake_mutex);
    if (wake_fd != -1) {
        if (eventfd_write(wake_fd, 1) == -1 &&
            errno != EAGAIN && errno != EWOULDBLOCK) {
            perror("eventfd_write");
        }
    }
}
