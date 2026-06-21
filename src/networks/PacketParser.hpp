#pragma once

#include <optional>
#include <span>
#include "Packet.hpp"

enum class ParseStatus {
    Complete,
    Pending,
    Invalid
};

struct ParseResult {
    ParseStatus status = ParseStatus::Pending;
    std::size_t consumed_bytes = 0;
    std::optional<Packet> packet;
};

class PacketParser {
public:
    static ParseResult parse(std::span<const char> buffer);
};

