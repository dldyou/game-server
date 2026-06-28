#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>
#include <optional>

#include "auth/AuthResult.hpp"

class Session {
private:
    int session_fd;
    std::vector<char> recv_buffer;
    std::deque<std::vector<char>> send_queue;
    std::size_t send_offset = 0;
    std::size_t pending_send_bytes = 0;

    bool closing = false;
    bool peer_closed = false;

    std::optional<AuthenticatedUser> authenticated_user;
    std::optional<std::uint32_t> room_id;

public:
    explicit Session(int fd);

    int fd() const { return session_fd; }
    std::vector<char>& recvBuffer() { return recv_buffer; }
    std::deque<std::vector<char>>& sendQueue() { return send_queue; }
    bool isClosing() const { return closing; }

    bool enqueueSend(
        std::vector<char> buffer,
        std::size_t max_pending_bytes
    );
    bool hasPendingSend() const { return !send_queue.empty(); }
    const std::vector<char>& frontSendBuffer() const;
    std::size_t sendOffset() const { return send_offset; }
    std::size_t pendingSendBytes() const { return pending_send_bytes; }
    void advanceSend(std::size_t byte_count);

    void markPeerClosed() { peer_closed = true; }
    bool isPeerClosed() const { return peer_closed; }

    // Authentication
    bool isAuthenticated() const {
        return authenticated_user.has_value();
    }

    const AuthenticatedUser* authenticatedUser() const {
        return authenticated_user ? &authenticated_user.value() : nullptr;
    }

    bool authenticate(AuthenticatedUser user);
    void clearAuthentication();

    // Room
    bool isInRoom() const;
    void enterRoom(std::uint32_t room_id);
    void leaveRoom();
    std::optional<std::uint32_t> currentRoomId() const;
};
