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

#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <cstdint>
#include <iostream>
#include <span>
#include <utility>

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
        request->loginId,
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

bool Server::sendLoginResult(int epoll_fd, Session& session, std::uint32_t sequence, LoginResponse response) {
    // TODO
}

bool Server::requiresAuthentication(PacketType type) const {
    // TODO
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
        // TODO: need to check authentication before these packets
    case C2S_JOIN_ROOM:
    case C2S_LEAVE_ROOM:
    case C2S_CHAT:
    case C2S_MOVE:
    case C2S_ATTACK:
        return true;
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

    sessions.erase(client_fd);
    close(client_fd);
}

void Server::cleanupClients() {
    for (const auto& item : sessions) {
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
        event_count = epoll_wait(epoll_fd, events.data(), max_events, -1);

        if (event_count == -1) {
            if (errno == EINTR) {
                continue;
            }
            perror("epoll_wait");
            break;
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
