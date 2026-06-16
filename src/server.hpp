#pragma once

#include <vector>
#include <atomic>

#include "config/Config.hpp"

class Server {
private:
    std::vector<int> client_fds;
    std::size_t client_nums = 0;
    std::atomic<bool> is_running = false;
    std::atomic<int> wake_fd{-1};

    void initClients(void);
    bool addClient(int client_fd);
    bool removeClient(int client_fd);
    bool setNonBlocking(int fd);
    void closeClient(int epoll_fd, int client_fd);
    void cleanupClients(void);
public:
    int init(const ServerConfig& server_config);
    void run(int server_fd, const ServerConfig& server_config);
    void stop();
};
