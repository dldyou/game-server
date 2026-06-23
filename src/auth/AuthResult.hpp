#pragma once

#include <cstdint>
#include <optional>
#include <string>

enum class LoginResult : std::uint16_t {
    Success = 0,
    MalformedPayload = 1,
    InvalidCredentials = 2,
    AlreadyAuthenticated = 3,
    InternalError = 4,
};

struct AuthenticatedUser {
    std::uint64_t userId;
    std::string handle;
};

struct AuthenticationResult {
    LoginResult result;
    std::optional<AuthenticatedUser> user;
};