#include "server.hpp"

#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <algorithm>
#include <iostream>
#include <cerrno>
#include <cstdlib>
#include <cstdio>

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

bool Server::setNonBlocking(int fd) {
    int flag = fcntl(fd, F_GETFL, 0);
    if (flag == -1) {
        perror("fcntl(F_GETFL)");
        return false;
    }

    if (fcntl(fd, F_SETFL, flag | O_NONBLOCK) == -1) {
        perror("fcntl(F_SETFL)");
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

    removeClient(client_fd);
    close(client_fd);
}

void Server::cleanupClients(void) {
    for (std::size_t i = 0; i < client_fds.size(); i++) {
        if (client_fds[i] != -1) {
            close(client_fds[i]);
            client_fds[i] = -1;
        }
    }
    client_nums = 0;
}

int Server::init(const ServerConfig& server_config) {
    int server_fd = 0;
    sockaddr_in server_addr = {};

    if (server_config.max_clients <= 0) {
        std::cerr << "Invalid max_clients value: " << server_config.max_clients << "\n";
        return -1;
    }
    if (server_config.buffer_size < 2) {
        std::cerr << "Invalid buffer_size value: " << server_config.buffer_size << "\n";
        return -1;
    }

    client_fds.resize(static_cast<std::size_t>(server_config.max_clients));
    initClients();

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
    } else if (result == -1) {
        perror("inet_pton");
        close(server_fd);
        return -1;
    }

    if (bind(server_fd, (sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
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
    const std::size_t buffer_size = server_config.buffer_size;

    if (server_fd < 0 || max_clients <= 0 || buffer_size < 2) {
        std::cerr << "Invalid server run parameters\n";
        return;
    }

    std::vector<char> buffer_in(buffer_size);

    int epoll_fd = -1;
    int event_count = 0;
    int local_wake_fd = -1;
    std::vector<epoll_event> events(static_cast<std::size_t>(max_events));

    socklen_t sockaddr_len = sizeof(sockaddr_in);

    if ((epoll_fd = epoll_create1(EPOLL_CLOEXEC)) == -1) {
        perror("epoll_create1");
        close(server_fd);
        return;
    }

    epoll_event event = {
        .events = EPOLLIN,
        .data = {
            .fd = server_fd,
        },
    };

    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &event) == -1) {
        perror("epoll_ctl");
        close(epoll_fd);
        close(server_fd);
        return;
    }

    if ((local_wake_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC)) == -1) {
        perror("eventfd");
        close(epoll_fd);
        close(server_fd);
        return;
    }
    wake_fd.store(local_wake_fd);

    event = {
        .events = EPOLLIN,
        .data = {
            .fd = local_wake_fd,
        },
    };

    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, local_wake_fd, &event) == -1) {
        perror("epoll_ctl");
        close(local_wake_fd);
        wake_fd.store(-1);
        close(epoll_fd);
        close(server_fd);
        return;
    }

    is_running = true;
    while (is_running) {
        if ((event_count = epoll_wait(epoll_fd, events.data(), max_events, -1)) == -1) {
            if (errno == EINTR) {
                continue;
            }
            perror("epoll_wait");
            break;
        }

        for (int i = 0; i < event_count; i++) {
            const int event_fd = events[i].data.fd;

            if (event_fd == local_wake_fd) {
                eventfd_t value;
                if (eventfd_read(local_wake_fd, &value) == -1 &&
                    errno != EAGAIN && errno != EWOULDBLOCK) {
                    perror("eventfd_read");
                }
                if (!is_running) {
                    break;
                }
            } else if (event_fd == server_fd) {
                int client_fd;
                sockaddr_in client_addr = {};

                sockaddr_len = sizeof(sockaddr_in);
                if ((client_fd = accept(server_fd, reinterpret_cast<sockaddr *>(&client_addr), &sockaddr_len)) == -1) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                        continue;
                    }
                    perror("accept");
                    continue;
                }

                // client is full
                if (client_nums >= client_fds.size()) {
                    closeClient(-1, client_fd);
                    continue;
                }

                if (!setNonBlocking(client_fd)) {
                    closeClient(-1, client_fd);
                    continue;
                }

                event = {
                    .events = EPOLLIN | EPOLLRDHUP,
                    .data = {
                        .fd = client_fd,
                    },
                };

                if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &event) == -1) {
                    perror("epoll_ctl");
                    closeClient(-1, client_fd);
                    continue;
                }

                if (!addClient(client_fd)) {
                    closeClient(epoll_fd, client_fd);
                }
            } else {
                int client_fd = event_fd;
                ssize_t num_bytes = 0;

                if ((events[i].events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) != 0 &&
                    (events[i].events & EPOLLIN) == 0) {
                    closeClient(epoll_fd, client_fd);
                    continue;
                }

                if ((events[i].events & EPOLLIN) == EPOLLIN) {
                    num_bytes = recv(client_fd, buffer_in.data(), buffer_in.size() - 1, 0);

                    if (num_bytes > 0) {
                        // process data
                        buffer_in[static_cast<std::size_t>(num_bytes)] = '\0';
                    } else if (num_bytes < 0) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                            continue;
                        }
                        closeClient(epoll_fd, client_fd);
                    } else {
                        // disconnect
                        closeClient(epoll_fd, client_fd);
                    }
                }

            }
        }
    }

    cleanupClients();
    if (local_wake_fd != -1) {
        close(local_wake_fd);
    }
    close(epoll_fd);
    close(server_fd);
    wake_fd.store(-1);
}

void Server::stop(void) {
    is_running = false;

    const int fd = wake_fd.load();
    if (fd != -1) {
        if (eventfd_write(fd, 1) == -1 &&
            errno != EAGAIN && errno != EWOULDBLOCK) {
            perror("eventfd_write");
        }
    }
}
