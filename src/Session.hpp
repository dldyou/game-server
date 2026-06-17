#pragma once

#include <vector>
#include <deque>
#include <string>
#include <cstdint>

class Session {
private:
    int session_fd;
    std::vector<char> recv_buffer;
    std::deque<std::vector<char>> send_queue;

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
};
