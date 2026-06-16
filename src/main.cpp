#include <arpa/inet.h>
#include <unistd.h>

#include <iostream>

#include "config/Config.hpp"
#include "server.hpp"

int main(void) {
    Config config;
    Server server;
    int server_fd = -1;

    if (!config.load("config/config.ini")) {
        std::cerr << "Failed to load config.ini\n";
    }

    const ServerConfig& server_config = config.server();

    std::cout << "host: " << server_config.host << "\n";
    std::cout << "port: " << server_config.port << "\n";
    std::cout << "max_clients: " << server_config.max_clients << "\n";
    std::cout << "buffer_size: " << server_config.buffer_size << "\n";

    if ((server_fd = server.init(server_config)) < 0) {
        std::cerr << "server_fd: " << server_fd << "\n";
        return 0;
    }
    server.run(server_fd, server_config);

    return 0;
}