#include "Session.hpp"

#include <utility>

Session::Session(int fd)
    : session_fd(fd) {
}

bool Session::enqueueSend(std::vector<char> buffer, std::size_t max_pending_bytes) {
    if (buffer.empty()) {
        return true;
    }

    if (pending_send_bytes > max_pending_bytes ||
        buffer.size() > max_pending_bytes - pending_send_bytes) {
        return false;
    }

    pending_send_bytes += buffer.size();
    send_queue.push_back(std::move(buffer));
    return true;
}

const std::vector<char>& Session::frontSendBuffer() const {
    return send_queue.front();
}

void Session::advanceSend(std::size_t byte_count) {
    if (send_queue.empty()) {
        return;
    }

    const std::size_t remaining = send_queue.front().size() - send_offset;
    const std::size_t advanced = byte_count < remaining ? byte_count : remaining;

    send_offset += advanced;
    pending_send_bytes -= advanced;

    if (send_offset == send_queue.front().size()) {
        send_queue.pop_front();
        send_offset = 0;
    }
}

bool Session::authenticate(AuthenticatedUser user) {
    if (authenticated_user) {
        return false;
    }

    authenticated_user = std::move(user);
    return true;
}

void Session::clearAuthentication() {
    authenticated_user.reset();
}

bool Session::isInRoom() const
{
    return false;
}

void Session::enterRoom(std::uint32_t room_id)
{
}

void Session::leaveRoom()
{
}

std::optional<std::uint32_t> Session::currentRoomId() const
{
    return std::optional<std::uint32_t>();
}
