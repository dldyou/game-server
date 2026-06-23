#pragma once

#include <optional>
#include <span>
#include <string>
#include <vector>

#include "AuthResult.hpp"

struct LoginRequest {
    std::string loginId;
    std::string password;
};

struct LoginResponse {
    LoginResult result;
    std::optional<AuthenticatedUser> user;
};

class LoginProtocol {
private:
    static bool readU16(
        std::span<const char> payload,
        std::size_t offset,
        std::uint16_t& value
    );

    static void appendU16(
        std::vector<char>& output,
        std::uint16_t value
    );

    static void appendU64(
        std::vector<char>& output,
        std::uint64_t value
    );

public:
    static std::optional<LoginRequest> decodeRequest(
        std::span<const char> payload
    );

    static std::vector<char> encodeResponse(
        const LoginResponse& response
    );
};