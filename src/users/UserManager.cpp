#include "UserManager.hpp"

#include <iostream>

// TODO
bool UserManager::verifyPassword(std::string_view password, std::string_view passwordHash) const {

}

bool UserManager::load(const std::string& path) {

}

bool UserManager::createUser(User user) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto [it, inserted] = usersByLoginId.try_emplace(user.loginId, user);

    if (!inserted) {
        std::cerr << "user exists\n";
        return false;
    }

    return true;
}

AuthenticationResult UserManager::authenticate(std::string_view loginId, std::string_view password) const {

}