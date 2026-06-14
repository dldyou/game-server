#pragma once

#include <string>
#include <cstdint>
#include <fstream>
#include <iostream>

struct ServerConfig {
    std::string host = "0.0.0.0";
    std::uint16_t port = 8080;
    int max_clients = 128;
    std::size_t buffer_size = 4096;
};

class Config {
private:
    ServerConfig m_server;
public:
    bool load(const std::string& path);
    const ServerConfig& server() const {
        return m_server;
    }
};