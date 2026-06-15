#include "server.hpp"

#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <algorithm>

void Server::initClients() {
    std::fill(client_fds.begin(), client_fds.end(), -1);
    client_nums = 0;
}

bool Server::addClient(int client_fd) {
    for (std::size_t i = 0; i < client_fds.size(); i++) {
        if (client_fds[i] == -1) {
            client_fds[i] = client_fd;
            client_nums += 1;
            return true;
        }
    }
    return false;
}

bool Server::removeClient(int client_fd) {
    for (std::size_t i = 0; i < client_fds.size(); i++) {
        if (client_fds[i] == client_fd) {
            client_fds[i] = -1;
            client_nums -= 1;
            return true;
        }
    }
    return false;
}

void Server::cleanupClients(void) {
    for (std::size_t i = 0; i < client_fds.size(); i++) {
        if (client_fds[i] != -1) {
            close(client_fds[i]);
        }
    }
}

int Server::init(const ServerConfig& server_config) {
    int server_fd = 0;
    sockaddr_in server_addr = {};

    client_fds.resize(server_config.max_clients);
    initClients();

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_config.port);

    const int result = inet_pton(AF_INET, server_config.host.c_str(), &server_addr.sin_addr);
    if (result == 0) {
        std::cerr << "Invalid IPv4 address: " << server_config.host << "\n";
        exit(EXIT_FAILURE);
    } else if (result == -1) {
        perror("inet_pton");
        exit(EXIT_FAILURE);
    }

    if (bind(server_fd, (sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("bind");
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, server_config.max_clients) == -1) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    return server_fd;
}

void Server::run(int server_fd, const ServerConfig& server_config) {
    const int max_clients = server_config.max_clients;
    const int buffer_size = server_config.buffer_size;

    char buffer_in[buffer_size];
    char buffer_out[buffer_size];

    int epoll_fd, event_count;
    epoll_event events[max_clients];

    socklen_t sockaddr_len = sizeof(sockaddr_in);

    if ((epoll_fd = epoll_create(max_clients)) == -1) {
        perror("epoll_create");
        exit(EXIT_FAILURE);
    }

    epoll_event event = {
        .events = EPOLLIN,
        .data = {
            .fd = server_fd,
        },
    };

    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &event) == -1) {
        perror("epoll_ctl");
        exit(EXIT_FAILURE);
    }

    is_running = true;
    while (is_running) {
        if ((event_count = epoll_wait(epoll_fd, events, max_clients, -1)) == -1) {
            perror("epoll_wait");
            exit(EXIT_FAILURE);
        }

        for (std::size_t i = 0; i < event_count; i++) {
            const int event_fd = events[i].data.fd;

            if (event_fd == server_fd) {
                int client_fd;
                sockaddr_in client_addr = {};

                if ((client_fd = accept(server_fd, reinterpret_cast<sockaddr *>(&client_addr), &sockaddr_len)) == -1) {
                    perror("accept");
                    exit(EXIT_FAILURE);
                }

                // client is full
                if (client_nums >= client_fds.size()) {
                    close(client_fd);
                    continue;
                }

                event = {
                    .events = EPOLLIN,
                    .data = {
                        .fd = client_fd,
                    },
                };

                if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &event) == -1) {
                    perror("epoll_ctl");
                    close(client_fd);
                    continue;
                }

                addClient(client_fd);
            } else {
                int client_fd = event_fd;
                ssize_t num_bytes = 0;

                if ((events[i].events & EPOLLIN) == EPOLLIN) {
                    if ((num_bytes = recv(client_fd, buffer_in, sizeof(buffer_in) - 1, 0)) == -1) {
                        perror("recv");
                        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, NULL);
                        removeClient(client_fd);
                        close(client_fd);
                        continue;
                    }
                }

                // disconnected
                if (num_bytes == 0) {
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, NULL);
                    removeClient(client_fd);
                    close(client_fd);
                    continue;
                }

                buffer_in[num_bytes] = '\0';
            }
        }
    }

    cleanupClients();
    close(epoll_fd);
    close(server_fd);
}

void Server::stop(void) {
    is_running = false;
}