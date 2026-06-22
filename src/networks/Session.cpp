#include "Session.hpp"

#include <utility>

Session::Session(int fd)
    : session_fd(fd) {
}

void Session::enqueueSend(std::vector<char> buffer) {
    if (!buffer.empty()) {
        send_queue.push_back(std::move(buffer));
    }
}

const std::vector<char>& Session::frontSendBuffer() const {
    return send_queue.front();
}

void Session::advanceSend(std::size_t byte_count) {
    if (send_queue.empty()) {
        return;
    }

    send_offset += byte_count;

    if (send_offset >= send_queue.front().size()) {
        send_queue.pop_front();
        send_offset = 0;
    }
}
