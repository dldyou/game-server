#include "UserManager.hpp"

#include <iostream>
#include <fstream>
#include <sstream>

bool UserManager::verifyPassword(std::string_view password, std::string_view password_hash) const {
    // TODO: replace with password hashing
    return password == password_hash;
}

bool UserManager::load(const std::string& path) {
    std::ifstream file(path);
    if (!file) {
        std::cerr << "Failed to open users file: " << path << "\n";
        return false;
    }

    std::unordered_map<std::string, User> loaded_users;
    std::string line;

    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') {
            continue;
        }

        std::istringstream input(line);
        User user{};

        if (!(input >> user.id >> user.handle >> user.login_id >> user.password)) {
            std::cerr << "Invalid user line: " << line << "\n";
            return false;
        }

        if (user.login_id.empty() || user.password.empty()) {
            return false;
        }

        auto [it, inserted] = loaded_users.try_emplace(user.login_id, std::move(user));
        if (!inserted) {
            std::cerr << "Duplicated login id in user file\n";
            return false;
        }
    }

    std::lock_guard<std::mutex> lock(mutex_);
    users_by_login_id = std::move(loaded_users);

    return true;
}

bool UserManager::createUser(User user) {
    if (user.login_id.empty() || user.password.empty()) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto [it, inserted] = users_by_login_id.try_emplace(user.login_id, std::move(user));

    return inserted;
}

AuthenticationResult UserManager::authenticate(std::string_view login_id, std::string_view password) const {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = users_by_login_id.find(std::string(login_id));
    if (it == users_by_login_id.end()) {
        return { LoginResult::InvalidCredentials, std::nullopt };
    }

    const User& user = it->second;
    if (!verifyPassword(password, user.password)) {
        return { LoginResult::InvalidCredentials, std::nullopt };
    }

    return {
        LoginResult::Success,
        AuthenticatedUser{
            .user_id = user.id,
            .handle = user.handle,
        }
    };
}