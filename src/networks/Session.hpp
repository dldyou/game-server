#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

class Session {
private:
    int session_fd;
    std::vector<char> recv_buffer;
    std::deque<std::vector<char>> send_queue;
    std::size_t send_offset = 0;

    bool closing = false;

    std::uint64_t id = 0;
    std::string user_id;
    int room_id = -1;

public:
    explicit Session(int fd);

    int fd() const { return session_fd; }
    std::vector<char>& recvBuffer() { return recv_buffer; }
    std::deque<std::vector<char>>& sendQueue() { return send_queue; }
    bool isClosing() const { return closing; }

    void enqueueSend(std::vector<char> buffer);
    bool hasPendingSend() const { return !send_queue.empty(); }
    const std::vector<char>& frontSendBuffer() const;
    std::size_t sendOffset() const { return send_offset; }
    void advanceSend(std::size_t byte_count);
};
