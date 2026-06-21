#include "PacketParser.hpp"

#include <arpa/inet.h>
#include <cstring>

ParseResult PacketParser::parse(std::span<const char> buffer) {
    if (buffer.size() < packet_header_size) {
        return { ParseStatus::Pending, 0, std::nullopt };
    }

    std::uint16_t network_size;
    std::uint16_t network_type;
    std::uint32_t network_sequence;

    std::memcpy(&network_size, buffer.data(), 2);
    std::memcpy(&network_type, buffer.data() + 2, 2);
    std::memcpy(&network_sequence, buffer.data() + 4, 4);

    const std::size_t size = ntohs(network_size);

    if (size < packet_header_size || size > max_packet_size) {
        return { ParseStatus::Invalid, 0, std::nullopt };
    }

    if (buffer.size() < size) {
        return { ParseStatus::Pending, 0, std::nullopt };
    }

    Packet packet{
        .type = static_cast<PacketType>(ntohs(network_type)),
        .sequence = ntohl(network_sequence),
        .payload = std::vector<char>(
            buffer.begin() + packet_header_size,
            buffer.begin() + size
        ),
    };

    return { ParseStatus::Complete, size, std::move(packet) };
}