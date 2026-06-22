#include "Session.hpp"

#include <utility>

Session::Session(int fd)
    : session_fd(fd) {
}

bool Session::enqueueSend(
    std::vector<char> buffer,
    std::size_t max_pending_bytes
) {
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

    const std::size_t remaining =
        send_queue.front().size() - send_offset;
    const std::size_t advanced =
        byte_count < remaining ? byte_count : remaining;

    send_offset += advanced;
    pending_send_bytes -= advanced;

    if (send_offset == send_queue.front().size()) {
        send_queue.pop_front();
        send_offset = 0;
    }
}
