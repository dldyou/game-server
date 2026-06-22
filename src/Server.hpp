#pragma once

#include <atomic>
#include <cstddef>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "config/Config.hpp"
#include "networks/Packet.hpp"
#include "networks/Session.hpp"

class Server {
private:
    static constexpr std::size_t max_pending_send_bytes = 256 * 1024;

    std::unordered_map<int, Session> sessions;
    std::atomic<bool> is_running = false;
    int wake_fd = -1;
    std::mutex wake_mutex;

    void initClients();
    bool setNonBlocking(int fd);
    bool updateClientEvents(int epoll_fd, const Session& session);
    void closeClient(int epoll_fd, int client_fd);
    void cleanupClients();

    bool processPackets(int epoll_fd, Session& session);
    bool handlePacket(int epoll_fd, Session& session, const Packet& packet);
    bool queuePacket(int epoll_fd, Session& session, const Packet& packet);
    bool flushSendQueue(int epoll_fd, Session& session);

public:
    int init(const ServerConfig& server_config);
    void run(int server_fd, const ServerConfig& server_config);
    void stop();
};
