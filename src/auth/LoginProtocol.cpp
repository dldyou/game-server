#include "LoginProtocol.hpp"

bool LoginProtocol::readU16(
    std::span<const char> payload,
    std::size_t offset,
    std::uint16_t& value
) {

}

void LoginProtocol::appendU16(
    std::vector<char>& output,
    std::uint16_t value
) {

}

void LoginProtocol::appendU64(
    std::vector<char>& output,
    std::uint64_t value
) {

}

std::optional<LoginRequest> LoginProtocol::decodeRequest(
    std::span<const char> payload
) {

}

std::vector<char> LoginProtocol::encodeResponse(
    const LoginResponse& response
) {

}