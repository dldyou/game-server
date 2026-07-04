#include "ChatProtocol.hpp"

namespace {
    constexpr std::size_t chat_message_header_size = 2;
    constexpr std::size_t max_chat_message_length = 1024;
    constexpr std::size_t max_chat_handle_length = 64;
}

bool ChatProtocol::readU16(std::span<const char> payload, std::size_t offset, std::uint16_t& value) {
    if (offset > payload.size() || payload.size() - offset < 2) {
        return false;
    }

    const auto high = static_cast<unsigned char>(payload[offset]);
    const auto low = static_cast<unsigned char>(payload[offset + 1]);
    value = static_cast<std::uint16_t>((static_cast<std::uint16_t>(high) << 8U) | static_cast<std::uint16_t>(low));
    return true;
}

void ChatProtocol::appendU16(std::vector<char>& output, std::uint16_t value) {
    output.push_back(static_cast<char>((value >> 8U) & 0xffU));
    output.push_back(static_cast<char>(value & 0xffU));
}

void ChatProtocol::appendU64(std::vector<char>& output, std::uint64_t value) {
    for (int shift = 56; shift >= 0; shift -= 8) {
        output.push_back(static_cast<char>((value >> shift) & 0xffU));
    }
}

std::optional<ChatMessage> ChatProtocol::decodeMessage(std::span<const char> payload) {
    std::uint16_t message_length = 0;
    if (!readU16(payload, 0, message_length)) {
        return std::nullopt;
    }

    if (message_length == 0 || message_length > max_chat_message_length) {
        return std::nullopt;
    }

    const std::size_t expected_size = chat_message_header_size + static_cast<std::size_t>(message_length);
    if (payload.size() != expected_size) {
        return std::nullopt;
    }

    return ChatMessage{
        .message = std::string(payload.begin() + static_cast<std::ptrdiff_t>(chat_message_header_size), payload.end()),
    };
}

std::vector<char> ChatProtocol::encodeMessage(const OutgoingChatMessage& message) {
    if (message.user_id == 0 ||
        message.handle.empty() ||
        message.handle.size() > max_chat_handle_length ||
        message.message.empty() ||
        message.message.size() > max_chat_message_length) {
        return {};
    }

    std::vector<char> output;
    output.reserve(8 + 2 + message.handle.size() + 2 + message.message.size());

    appendU64(output, message.user_id);
    appendU16(output, static_cast<std::uint16_t>(message.handle.size()));
    output.insert(output.end(), message.handle.begin(), message.handle.end());
    appendU16(output, static_cast<std::uint16_t>(message.message.size()));
    output.insert(output.end(), message.message.begin(), message.message.end());

    return output;
}

