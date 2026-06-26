#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <unordered_map>

#include "User.hpp"
#include "auth/AuthResult.hpp"

class UserManager {
private:
    std::mutex mutex_;
    std::unordered_map<std::string, User> users_by_login_id;

    bool verifyPassword(std::string_view password, std::string_view password_hash) const;

public:
    bool load(const std::string& path);
    bool createUser(User user);

    AuthenticationResult authenticate(std::string_view login_id, std::string_view password) const;
};