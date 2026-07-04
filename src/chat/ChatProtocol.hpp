#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

struct ChatMessage {
    std::string message;
};

struct OutgoingChatMessage {
    std::uint64_t user_id;
    std::string handle;
    std::string message;
};

class ChatProtocol {
private:
    static bool readU16(std::span<const char> payload, std::size_t offset, std::uint16_t& value);
    static void appendU16(std::vector<char>& output, std::uint16_t value);
    static void appendU64(std::vector<char>& output, std::uint64_t value);

public:
    static std::optional<ChatMessage> decodeMessage(std::span<const char> payload);
    static std::vector<char> encodeMessage(const OutgoingChatMessage& message);
};

