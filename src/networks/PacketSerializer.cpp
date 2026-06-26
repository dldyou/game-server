#include "PacketSerializer.hpp"

#include <arpa/inet.h>

#include <cstring>
#include <limits>

std::vector<char> PacketSerializer::serialize(const Packet& packet) {
    const std::size_t packet_size = packet_header_size + packet.payload.size();

    if (packet_size > max_packet_size ||
        packet_size > std::numeric_limits<std::uint16_t>::max()) {
        return {};
    }

    const std::uint16_t network_size = htons(static_cast<std::uint16_t>(packet_size));
    const std::uint16_t network_type = htons(static_cast<std::uint16_t>(packet.type));
    const std::uint32_t network_sequence = htonl(packet.sequence);

    std::vector<char> buffer(packet_size);
    std::memcpy(buffer.data(), &network_size, sizeof(network_size));
    std::memcpy(
        buffer.data() + sizeof(network_size),
        &network_type,
        sizeof(network_type)
    );
    std::memcpy(
        buffer.data() + sizeof(network_size) + sizeof(network_type),
        &network_sequence,
        sizeof(network_sequence)
    );

    if (!packet.payload.empty()) {
        std::memcpy(
            buffer.data() + packet_header_size,
            packet.payload.data(),
            packet.payload.size()
        );
    }

    return buffer;
}
