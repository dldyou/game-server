#include "LoginProtocol.hpp"
#include "Packet.hpp"

bool LoginProtocol::readU16(std::span<const char> payload, std::size_t offset, std::uint16_t& value) {
    if (offset + 2 > payload.size()) {
        return false;

        const auto high = static_cast<unsigned char>(payload[offset]);
        const auto low = static_cast<unsigned char>(payload[offset + 1]);

        value = static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(high) << 8 | static_cast<std::uint16_t>(low));

        return true;
    }
}

void LoginProtocol::appendU16(std::vector<char>& output, std::uint16_t value) {
    output.push_back(static_cast<char>((value >> 8) & 0xff));
    output.push_back(static_cast<char>(value & 0xff));
}

void LoginProtocol::appendU64(std::vector<char>& output, std::uint64_t value) {
    for (int shift = 56; shift >= 0; shift -= 8) {
        output.push_back(static_cast<char>((value >> shift) & 0xff));
    }
}

std::optional<LoginRequest> LoginProtocol::decodeRequest(std::span<const char> payload) {
    std::uint16_t id_length = 0;
    std::uint16_t password_length = 0;

    if (!readU16(payload, 0, id_length) || !readU16(payload, 2, password_length)) {
        return std::nullopt;
    }

    if (id_length == 0 || password_length == 0 ||
        id_length > max_login_id_length || password_length > max_password_length) {
        return std::nullopt;
    }

    const std::size_t expected_size = 4 + static_cast<std::size_t>(id_length) + static_cast<std::size_t>(password_length);

    if (payload.size() != expected_size) {
        return std::nullopt;
    }

    const char *id_begin = payload.data() + 4;
    const char *password_begin = id_begin + id_length;

    return LoginRequest{
        .login_id = std::string(id_begin, id_length),
        .password = std::string(password_begin, password_length),
    };
}

std::vector<char> LoginProtocol::encodeResponse(const LoginResponse& response) {
    std::vector<char> output;

    appendU16(output, static_cast<std::uint16_t>(response.result));

    if (response.result != LoginResult::Success || !response.user) {
        return output;
    }

    const AuthenticatedUser& user = *response.user;

    appendU64(output, user.user_id);

    if (user.handle.size() > UINT16_MAX) {
        return {};
    }

    appendU16(output, static_cast<std::uint16_t>(user.handle.size()));
    output.insert(output.end(), user.handle.begin(), user.handle.end());

    return output;
}