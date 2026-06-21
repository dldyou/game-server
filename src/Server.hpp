#pragma once

#include <vector>
#include <unordered_map>
#include <atomic>
#include <mutex>

#include "config/Config.hpp"
#include "networks/Session.hpp"
#include "networks/Packet.hpp"

class Server {
private:
    std::unordered_map<int, Session> sessions;
    std::atomic<bool> is_running = false;
    int wake_fd = -1;
    std::mutex wake_mutex;

    void initClients();
    bool setNonBlocking(int fd);
    void closeClient(int epoll_fd, int client_fd);
    void cleanupClients();

    void processPackets(int epoll_fd, Session& session);
    void handlePacket(Session& session, const Packet& packet);
public:
    int init(const ServerConfig& server_config);
    void run(int server_fd, const ServerConfig& server_config);
    void stop();
};
