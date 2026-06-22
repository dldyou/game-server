#pragma once

#include <vector>
#include "Packet.hpp"

class PacketSerializer {
public:
    static std::vector<char> serialize(const Packet& packet);
};