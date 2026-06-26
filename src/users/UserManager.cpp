#include "UserManager.hpp"

#include <iostream>

// TODO
bool UserManager::verifyPassword(std::string_view password, std::string_view password_hash) const {

}

bool UserManager::load(const std::string& path) {

}

bool UserManager::createUser(User user) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto [it, inserted] = users_by_login_id.try_emplace(user.login_id, user);

    if (!inserted) {
        std::cerr << "user exists\n";
        return false;
    }

    return true;
}

AuthenticationResult UserManager::authenticate(std::string_view login_id, std::string_view password) const {

}