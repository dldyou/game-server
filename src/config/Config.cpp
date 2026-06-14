#include "config/Config.hpp"
#include "utils/StringUtils.hpp"

bool Config::load(const std::string& path) {
    std::ifstream file(path);

    if (!file.is_open()) {
        std::cerr << "Failed to open config file: " << path << "\n";
        return false;
    }

    std::string section;
    std::string line;

    while (std::getline(file, line)) {
        line = string_utils::trim(line);

        if (line.empty() || line[0] == '#' || line[0] == ';') {
            continue;
        }

        if (line.front() == '[' && line.back() == ']') {
            section = string_utils::trim(line.substr(1, line.size() - 2));
            continue;
        }

        const std::size_t pos = line.find('=');

        if (pos == std::string::npos) {
            continue;
        }

        const std::string key = string_utils::trim(line.substr(0, pos));
        const std::string value = string_utils::trim(line.substr(pos + 1));

        try {
            if (section == "server") {
                if (key == "host") {
                    m_server.host = value;
                } else if (key == "port") {
                    const int port = std::stoi(value);

                    if (port < 0 || port > 65535) {
                        std::cerr << "Invalid port value: " << value << "\n";
                        continue;
                    }

                    m_server.port = static_cast<std::uint16_t>(port);
                } else if (key == "max_clients") {
                    const int max_clients = std::stoi(value);

                    if (max_clients <= 0) {
                        std::cerr << "Invalid max_clients value: " << value << "\n";
                        continue;
                    }

                    m_server.max_clients = max_clients;
                } else if (key == "buffer_size") {
                    const int buffer_size = std::stoi(value);

                    if (buffer_size <= 0) {
                        std::cerr << "Invalid buffer_size value: " << value << "\n";
                    }

                    m_server.buffer_size = static_cast<std::size_t>(buffer_size);
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "Invalid config value: " << key << "=" << value << " (" << e.what() << ")\n";
        }
    }

    return true;
}