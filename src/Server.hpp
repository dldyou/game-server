#pragma once

#include <atomic>
#include <cstddef>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "config/Config.hpp"
#include "networks/Packet.hpp"
#include "networks/Session.hpp"
#include "users/UserManager.hpp"
#include "auth/LoginProtocol.hpp"
#include "rooms/RoomManager.hpp"

class Server {
private:
    static constexpr std::size_t max_pending_send_bytes = 256 * 1024;

    std::unordered_map<int, Session> sessions;
    std::atomic<bool> is_running = false;
    int wake_fd = -1;
    std::mutex wake_mutex;

    UserManager user_manager;
    RoomManager room_manager;

    void initClients();
    bool setNonBlocking(int fd);
    bool updateClientEvents(int epoll_fd, const Session& session);
    void closeClient(int epoll_fd, int client_fd);
    void cleanupClients();

    bool processPackets(int epoll_fd, Session& session);
    bool handlePacket(int epoll_fd, Session& session, const Packet& packet);
    bool queuePacket(int epoll_fd, Session& session, const Packet& packet);
    bool flushSendQueue(int epoll_fd, Session& session);

    bool handleLogin(int epoll_fd, Session& session, const Packet& packet);
    bool handleCreateRoom(int epoll_fd, Session& session, const Packet& packet);
    bool handleJoinRoom(int epoll_fd, Session& session, const Packet& packet);
    bool handleLeaveRoom(int epoll_fd, Session& session, const Packet& packet);
    bool handleRoomList(int epoll_fd, Session& session, const Packet& packet);
    bool handleSetReady(int epoll_fd, Session& session, const Packet& packet);
    bool handleStartGame(int epoll_fd, Session& session, const Packet& packet);
    bool handleChat(int epoll_fd, Session& session, const Packet& packet);

    bool sendLoginResult(
        int epoll_fd,
        Session& session,
        std::uint32_t sequence,
        LoginResponse response
    );
    bool sendRoomResult(int epoll_fd, Session& session, std::uint32_t sequence, PacketType type, RoomResult result, std::uint32_t room_id);
    bool sendRoomState(int epoll_fd, Session& session, std::uint32_t sequence, const RoomState& state);
    bool sendRoomList(int epoll_fd, Session& session, std::uint32_t sequence, const std::vector<RoomSummary>& rooms);
    bool broadcastRoomState(int epoll_fd, std::uint32_t sequence, const RoomState& state);

    bool requiresAuthentication(PacketType type) const;

public:
    int init(const ServerConfig& server_config);
    void run(int server_fd, const ServerConfig& server_config);
    void stop();
};
